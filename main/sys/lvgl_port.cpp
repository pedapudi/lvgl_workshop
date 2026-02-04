#include "sys/lvgl_port.h"

#include <cstdio>

#include "display/drivers/esp32_spi.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "workshop_config.h"

LvglPort::LvglPort(const Config& config)
    : config_(config), draw_buf_(nullptr), draw_buf2_(nullptr) {}

LvglPort::~LvglPort() {
  // Unique pointers and objects will clean themselves up
}

void LvglPort::init(esp_lcd_panel_handle_t panel_handle,
                    esp_lcd_panel_io_handle_t io_handle) {
  panel_handle_ = panel_handle;

  // 1. Initialize Port Service (Task & Timer)
  port_service_ = std::make_unique<lvgl::utility::Esp32Port>();
  lvgl::utility::Esp32PortConfig port_cfg;
  port_cfg.h_res = config_.h_res;
  port_cfg.v_res = config_.v_res;
  port_cfg.stack_size = config_.task_stack_size;
  port_cfg.task_priority = config_.task_priority;
  port_cfg.core_affinity = config_.task_affinity;

  if (!port_service_->init(port_cfg)) {
    ESP_LOGE("LvglPort", "Failed to initialize port service!");
    return;
  }

  // 2. Initialize Display Driver
  // --------------------------
  if (Workshop::USE_NATIVE_DRIVER) {
    // Phase 5: Native Driver (Double Buffered)
    lvgl::Esp32Spi::Config display_cfg;
    display_cfg.h_res = config_.h_res;
    display_cfg.v_res = config_.v_res;
    display_cfg.panel_handle = panel_handle;
    display_cfg.io_handle = io_handle;

    // Optimization: LVGL already handles byte-swapped output via
    // CONFIG_LV_COLOR_16_SWAP.
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

    // Allocate Buffers via Library Helper
    draw_buf_ = lvgl::draw::DrawBuf::allocate_dma(config_.h_res, buffer_lines,
                                                  lvgl::ColorFormat::RGB565,
                                                  Workshop::ALLOC_CAPS);

    if (Workshop::USE_DOUBLE_BUFFERING) {
      draw_buf2_ = lvgl::draw::DrawBuf::allocate_dma(
          config_.h_res, buffer_lines, lvgl::ColorFormat::RGB565,
          Workshop::ALLOC_CAPS);
    }

    if (!draw_buf_.raw() ||
        (Workshop::USE_DOUBLE_BUFFERING && !draw_buf2_.raw())) {
      ESP_LOGE("LvglPort", "Failed to allocate display buffer(s)!");
      return;
    }

    // Create Legacy Display Wrapper
    display_ = std::make_unique<lvgl::Display>(
        lvgl::Display::create(config_.h_res, config_.v_res));

    lv_display_set_user_data(display_->raw(), this);
    lv_display_set_flush_cb(display_->raw(), flush_cb_trampoline);

    display_->set_buffers(draw_buf_.data(), draw_buf2_.data(),
                          draw_buf_.data_size(), Workshop::LVGL_RENDER_MODE);

    // Register IO Callback for flush readiness
    esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = notify_flush_ready_trampoline,
    };
    esp_lcd_panel_io_register_event_callbacks(io_handle, &cbs, this);
  }

  // 3. Initialize Input Device
  auto ptr_input = lvgl::PointerInput::create();
  lvgl::Display* target_disp = get_display();
  if (target_disp) {
    lv_indev_set_disp(ptr_input.raw(), target_disp->raw());
  }
  indev_ = std::make_unique<lvgl::PointerInput>(std::move(ptr_input));
}

void LvglPort::flush_cb_trampoline(lv_display_t* disp, const lv_area_t* area,
                                   uint8_t* px_map) {
  auto* port = static_cast<LvglPort*>(lv_display_get_user_data(disp));
  if (port) {
    auto target_disp = port->get_display();
    if (target_disp) {
      port->flush_cb(*target_disp, *area, px_map);
    }
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
  auto target_disp = port->get_display();
  if (target_disp) {
    lv_display_flush_ready(target_disp->raw());
  }
  return false;
}

bool LvglPort::lock(uint32_t timeout_ms) {
  if (port_service_ && port_service_->get_lock()) {
    return xSemaphoreTakeRecursive(port_service_->get_lock(),
                                   timeout_ms == 0xFFFFFFFF
                                       ? portMAX_DELAY
                                       : pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
  }
  return false;
}

void LvglPort::unlock() {
  if (port_service_ && port_service_->get_lock()) {
    xSemaphoreGiveRecursive(port_service_->get_lock());
  }
}

lvgl::Display* LvglPort::get_display() {
  if (display_driver_ && display_driver_->display()) {
    return display_driver_->display();
  }
  return display_.get();
}

void LvglPort::set_rotation(lvgl::Display::Rotation rotation) {
  lvgl::Display* target_disp = get_display();
  if (target_disp) {
    target_disp->set_rotation(rotation);
  }
}

void LvglPort::notify_event(uint32_t event_bit) {
  if (port_service_) {
    // If we're calling from an interrupt, use the ISR-safe notification
    if (xPortInIsrContext()) {
      port_service_->notify_from_isr();
    } else {
      port_service_->notify();
    }
  }
}
