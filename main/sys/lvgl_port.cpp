#if defined(noreturn)
#undef noreturn
#endif

#include "lvgl_port.h"

#include <algorithm>

#include "esp_lcd_panel_ops.h"
#include "esp_log.h"

static const char* TAG = "LvglPort";

/**
 * notify_flush_ready_trampoline: Called by the ESP-LCD driver when a DMA
 * transfer is complete. It tells LVGL that the buffer they just "flushed" is
 * now free to be used again for rendering.
 */
bool LvglPort::notify_flush_ready_trampoline(
    esp_lcd_panel_io_handle_t io_panel, esp_lcd_panel_io_event_data_t* edata,
    void* user_ctx) {
  LvglPort* instance = static_cast<LvglPort*>(user_ctx);
  if (instance && instance->display_) {
    instance->display_->flush_ready();
  }
  return false;
}

/**
 * tick_increment_trampoline: Heartbeat for LVGL. Moves animations and timers
 * forward.
 */
void LvglPort::tick_increment_trampoline(void* arg) {
  lv_tick_inc(static_cast<LvglPort*>(arg)->config_.tick_period_ms);
}

/**
 * task_trampoline: Entry point for the FreeRTOS rendering task.
 */
void LvglPort::task_trampoline(void* arg) {
  auto* instance = static_cast<LvglPort*>(arg);
  instance->task_loop();
}

LvglPort::LvglPort(const Config& config) : config_(config) {
  // Use a recursive mutex so the same task can lock multiple times without
  // deadlocking.
  api_lock_ = xSemaphoreCreateRecursiveMutex();
}

LvglPort::~LvglPort() {
  if (task_handle_) {
    vTaskDelete(task_handle_);
  }
  if (tick_timer_) {
    esp_timer_stop(tick_timer_);
    esp_timer_delete(tick_timer_);
  }
  if (api_lock_) {
    vSemaphoreDelete(api_lock_);
  }
  if (draw_buffer_) heap_caps_free(draw_buffer_);
  if (draw_buffer2_) heap_caps_free(draw_buffer2_);
}

void LvglPort::init(esp_lcd_panel_handle_t panel_handle,
                    esp_lcd_panel_io_handle_t io_handle) {
  panel_handle_ = panel_handle;

  lv_init();

  // 1. Create the LVGL display object
  lv_display_t* raw_disp = lv_display_create(config_.h_res, config_.v_res);
  display_ = std::make_unique<lvgl::Display>(raw_disp);
  display_->set_color_format(LV_COLOR_FORMAT_RGB565);

  // 2. Buffer Size Calculation
  // -------------------------
  // In "Full-Frame" mode (Phase 1, 4), we allocate enough for the entire
  // screen. In "Strip-mode" (Phase 2, 3), we only allocate 20 lines to save
  // internal memory.
  if (config_.full_frame) {
    draw_buffer_sz_ = config_.h_res * config_.v_res * sizeof(uint16_t);
    ESP_LOGI(TAG, "Full-Frame buffer enabled (%u bytes)",
             (uint32_t)draw_buffer_sz_);
  } else {
    draw_buffer_sz_ = config_.h_res * 20 * sizeof(uint16_t);
    ESP_LOGI(TAG, "Strip-mode enabled (20 lines, %u bytes)",
             (uint32_t)draw_buffer_sz_);
  }

  // 3. Memory Allocation
  // --------------------
  // We use heap_caps_malloc to specify WHERE the memory lives (Internal SRAM vs
  // Octal PSRAM).
  draw_buffer_ =
      (uint8_t*)heap_caps_malloc(draw_buffer_sz_, config_.malloc_caps);

  if (config_.double_buffered) {
    // Phase 3+ uses a second buffer to allow simultaneous rendering and DMA
    // flushing.
    draw_buffer2_ =
        (uint8_t*)heap_caps_malloc(draw_buffer_sz_, config_.malloc_caps);
    ESP_LOGI(TAG, "Double buffering enabled");
  }

  if (!draw_buffer_) {
    ESP_LOGE(TAG, "Critical memory allocation failure!");
    return;
  }

  display_->set_buffers(draw_buffer_, draw_buffer2_, draw_buffer_sz_,
                        config_.render_mode);

  // 4. Flush Callback (The Rendering Pipeline)
  // ------------------------------------------
  display_->set_flush_cb(
      [this](lvgl::Display* disp, const lv_area_t* area, uint8_t* px_map) {
        uint32_t len = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);
        uint16_t* buf = reinterpret_cast<uint16_t*>(px_map);

        if (this->config_.use_intrinsics) {
          // PHASE 4: Single-cycle hardware instruction for byte-swapping.
          for (uint32_t i = 0; i < len; i++) {
            buf[i] = ~__builtin_bswap16(buf[i]);
          }
        } else {
          // PHASES 1-3: Manual byte-masking (Pedagogical).
          // Colors are inverted (~) to match the LCD panel logic.
          for (uint32_t i = 0; i < len; i++) {
            uint16_t color = buf[i];
            buf[i] = ~((color << 8) | (color >> 8));
          }
        }

        // Send the corrected bits to the LCD controller.
        esp_lcd_panel_draw_bitmap(this->panel_handle_, area->x1, area->y1,
                                  area->x2 + 1, area->y2 + 1, px_map);
      });

  // 5. Register LCD Event Callbacks
  // Links the "DMA Finish" signal back to our LvglPort instance.
  esp_lcd_panel_io_callbacks_t cbs = {
      .on_color_trans_done = notify_flush_ready_trampoline,
  };
  esp_lcd_panel_io_register_event_callbacks(io_handle, &cbs, this);

  // 6. System Timer (Heartbeat)
  // Provides high-resolution periodic ticks for LVGL.
  esp_timer_create_args_t periodic_timer_args = {};
  periodic_timer_args.callback = &tick_increment_trampoline;
  periodic_timer_args.arg = this;
  periodic_timer_args.name = "lvgl_tick";

  esp_timer_create(&periodic_timer_args, &tick_timer_);
  esp_timer_start_periodic(tick_timer_, config_.tick_period_ms * 1000);

  // 7. Rendering Task (The Main Loop)
  // We pin this to Core 1 to separate UI calculation from background system
  // logic.
  xTaskCreatePinnedToCore(task_trampoline, "lvgl_task", config_.task_stack_size,
                          this, config_.task_priority, &task_handle_, 1);

  // Initialize the modern C++ pointer input (Touch).
  indev_ = std::make_unique<lvgl::PointerInput>(lvgl::PointerInput::create());
}

bool LvglPort::lock(uint32_t timeout_ms) {
  // Thread-safe wrapper for the LVGL API.
  return xSemaphoreTakeRecursive(
             api_lock_, timeout_ms == -1 ? portMAX_DELAY
                                         : pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void LvglPort::unlock() { xSemaphoreGiveRecursive(api_lock_); }

void LvglPort::task_loop() {
  while (true) {
    if (lock(-1)) {
      // The actual LVGL engine call. Rasterizes widgets into the draw buffer.
      lv_timer_handler();
      unlock();
    }
    // Safety delay to prevent task starvation.
    vTaskDelay(pdMS_TO_TICKS(config_.tick_period_ms));
  }
}
