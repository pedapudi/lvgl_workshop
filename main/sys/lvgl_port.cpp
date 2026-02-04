#include "sys/lvgl_port.h"

#include <cstdio>

#include "display/drivers/esp32_spi.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "workshop_config.h"

LvglPort::LvglPort(const Config& config) : config_(config) {
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
  if (draw_buf_) {
    heap_caps_free(draw_buf_);
  }
  if (draw_buf2_) {
    heap_caps_free(draw_buf2_);
  }
}

void LvglPort::init(esp_lcd_panel_handle_t panel_handle,
                    esp_lcd_panel_io_handle_t io_handle) {
  panel_handle_ = panel_handle;
  creator_task_ = xTaskGetCurrentTaskHandle();

  lv_init();

  // 1. Initialize Display Driver
  // --------------------------
  if (Workshop::USE_NATIVE_DRIVER) {
    // Phase 5: Native Driver (Double Buffered)
    lvgl::Esp32Spi::Config display_cfg;
    display_cfg.h_res = config_.h_res;
    display_cfg.v_res = config_.v_res;
    display_cfg.panel_handle = panel_handle;
    display_cfg.io_handle = io_handle;

    // Optimization: LVGL already handles byte-swapped output via
    // CONFIG_LV_COLOR_16_SWAP. GC9A01 hardware inversion should be handled by
    // the panel driver if needed. By setting these to false, we avoid a
    // full-frame CPU pass over PSRAM.
    display_cfg.swap_bytes = true;
    display_cfg.invert_colors = false;
    display_cfg.render_mode = Workshop::LVGL_RENDER_MODE;

    display_driver_ = std::make_unique<lvgl::Esp32Spi>(display_cfg);
  } else {
    // Calculate buffer size based on Workshop mode
    size_t buffer_lines =
        (Workshop::BUFFER_MODE == Workshop::BufferMode::FullFrame)
            ? config_.v_res
            : 20;
    draw_buf_size_ = config_.h_res * buffer_lines * sizeof(lv_color_t);

    ESP_LOGI("LvglPort", "Allocating %zu bytes for display buffer 1 (%s)",
             draw_buf_size_,
             (Workshop::ALLOC_CAPS & MALLOC_CAP_SPIRAM) ? "PSRAM" : "Internal");
    draw_buf_ = (uint8_t*)heap_caps_aligned_alloc(64, draw_buf_size_,
                                                  Workshop::ALLOC_CAPS);

    if (Workshop::USE_DOUBLE_BUFFERING) {
      ESP_LOGI(
          "LvglPort", "Allocating %zu bytes for display buffer 2 (%s)",
          draw_buf_size_,
          (Workshop::ALLOC_CAPS & MALLOC_CAP_SPIRAM) ? "PSRAM" : "Internal");
      draw_buf2_ = (uint8_t*)heap_caps_aligned_alloc(64, draw_buf_size_,
                                                     Workshop::ALLOC_CAPS);
    }

    if (!draw_buf_ || (Workshop::USE_DOUBLE_BUFFERING && !draw_buf2_)) {
      ESP_LOGE("LvglPort", "Failed to allocate display buffer(s)! Free: %zu",
               heap_caps_get_free_size(Workshop::ALLOC_CAPS));
      return;
    }

    // Create Legacy Display Wrapper
    display_ = std::make_unique<lvgl::Display>(
        lvgl::Display::create(config_.h_res, config_.v_res));
    // Use raw C API for callbacks to avoid C++ wrapper signature mismatches
    lv_display_set_user_data(display_->raw(), this);
    lv_display_set_flush_cb(display_->raw(), flush_cb_trampoline);

    display_->set_buffers(draw_buf_, draw_buf2_, draw_buf_size_,
                          Workshop::LVGL_RENDER_MODE);

    // Register IO Callback for flush readiness
    esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = notify_flush_ready_trampoline,
    };
    esp_lcd_panel_io_register_event_callbacks(io_handle, &cbs, this);
  }

  // 2. System Timer (Heartbeat)
  esp_timer_create_args_t periodic_timer_args = {};
  periodic_timer_args.callback = &tick_increment_trampoline;
  periodic_timer_args.arg = this;
  periodic_timer_args.name = "lvgl_tick";

  ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &tick_timer_));
  ESP_ERROR_CHECK(
      esp_timer_start_periodic(tick_timer_, config_.tick_period_ms * 1000));

  // 3. Rendering Task
  ESP_LOGI("LvglPort",
           "Creating rendering task (Stack: %" PRIu32 " bytes, Priority: %d)",
           config_.task_stack_size, config_.task_priority);

  BaseType_t res;
  if (config_.task_affinity == tskNO_AFFINITY) {
    res = xTaskCreate(task_trampoline, "lvgl_task", config_.task_stack_size,
                      this, config_.task_priority, &task_handle_);
  } else {
    res = xTaskCreatePinnedToCore(
        task_trampoline, "lvgl_task", config_.task_stack_size, this,
        config_.task_priority, &task_handle_, config_.task_affinity);
  }

  if (res != pdPASS) {
    ESP_LOGE("LvglPort",
             "Failed to create rendering task! Free Internal heap: %zu bytes",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    return;
  }

  // Synchronize startup: Wait for task to initialize
  if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000)) == 0) {
    ESP_LOGE("LvglPort", "Task startup timeout!");
  }

  // 4. Initialize Input Device
  auto ptr_input = lvgl::PointerInput::create();
  lvgl::Display* target_disp =
      display_driver_ ? display_driver_->display() : display_.get();
  if (target_disp) {
    lv_indev_set_disp(ptr_input.raw(), target_disp->raw());
  }
  indev_ = std::make_unique<lvgl::PointerInput>(std::move(ptr_input));
}

void LvglPort::task_loop() {
  // Signal the creator task that we have started
  if (creator_task_) {
    ESP_LOGI("LvglPort", "Signaling task readiness to creator...");
    xTaskNotifyGive(creator_task_);
  }

  ESP_LOGI("LvglPort", "Starting optimized task loop on Core %d",
           xPortGetCoreID());
  uint32_t wait_ms = 0;

  while (true) {
    if (lock(-1)) {
      // The actual LVGL engine call. Rasterizes widgets into the draw buffer.
      wait_ms = lvgl::Timer::handler();
      unlock();
    } else {
      // Mutex lock failed - yield
      wait_ms = 1;
    }

    if (wait_ms == 0x10000000) {  // LV_NO_TIMER_READY
      wait_ms = 50;  // Sleep if nothing to do, but stay reasonably responsive
    } else if (wait_ms > 50) {
      wait_ms = 50;  // Cap max sleep to maintain responsiveness
    } else if (wait_ms < 1) {
      wait_ms = 1;  // Ensure we yield
    }

    vTaskDelay(pdMS_TO_TICKS(wait_ms));
  }
}

void LvglPort::flush_cb_trampoline(lv_display_t* disp, const lv_area_t* area,
                                   uint8_t* px_map) {
  auto* port = static_cast<LvglPort*>(lv_display_get_user_data(disp));
  if (port && port->display_) {
    port->flush_cb(*port->display_, *area, px_map);
  }
}

void LvglPort::flush_cb(lvgl::Display& disp, const lv_area_t& area,
                        uint8_t* px_map) {
  uint32_t w = lv_area_get_width(&area);
  uint32_t h = lv_area_get_height(&area);
  uint32_t len = w * h;

  uint16_t* buf16 = (uint16_t*)px_map;

  // BYTE SWAPPING & COLOR CORRECTION:
  // We must swap the Little-Endian bytes from the CPU for the Big-Endian LCD.
  // NOTE: Some panels require bitwise inversion (~), but the GC9A01 on the
  // Seeed XIAO Round Display uses standard logic. If your colors appear
  // inverted (negative), do NOT use the ~ operator.
  if (Workshop::USE_XTENSA_INTRINSICS) {
    while (len > 0) {
      *buf16 = __builtin_bswap16(*buf16);
      buf16++;
      len--;
    }
  } else {
    while (len > 0) {
      *buf16 = (uint16_t)((*buf16 >> 8) | (*buf16 << 8));
      buf16++;
      len--;
    }
  }

  // Transmit to panel
  esp_lcd_panel_draw_bitmap(panel_handle_, area.x1, area.y1, area.x2 + 1,
                            area.y2 + 1, px_map);
}

bool LvglPort::notify_flush_ready_trampoline(
    esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t* edata,
    void* user_ctx) {
  auto* port = static_cast<LvglPort*>(user_ctx);
  if (port && port->display_) {
    lv_display_flush_ready(port->display_->raw());
  }
  return false;
}

bool LvglPort::lock(uint32_t timeout_ms) {
  // Thread-safe wrapper for the LVGL API.
  return xSemaphoreTakeRecursive(
             api_lock_, timeout_ms == -1 ? portMAX_DELAY
                                         : pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void LvglPort::unlock() { xSemaphoreGiveRecursive(api_lock_); }

lvgl::Display* LvglPort::get_display() {
  if (display_driver_ && display_driver_->display()) {
    return display_driver_->display();
  }
  return display_.get();
}

void LvglPort::set_rotation(lvgl::Display::Rotation rotation) {
  lvgl::Display* target_disp = (display_driver_ && display_driver_->display())
                                   ? display_driver_->display()
                                   : display_.get();
  if (target_disp) {
    target_disp->set_rotation(rotation);
  }
}

void LvglPort::tick_increment_trampoline(void* arg) {
  lv_tick_inc(static_cast<LvglPort*>(arg)->config_.tick_period_ms);
}

void LvglPort::task_trampoline(void* arg) {
  auto* instance = static_cast<LvglPort*>(arg);
  instance->task_loop();
}
