#pragma once

#if defined(noreturn)
#undef noreturn
#endif

#include <memory>
#include <vector>

#include "display/display.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "lvgl_cpp/indev/input_device.h"
#include "lvgl_cpp/indev/pointer_input.h"
#include "lvgl_cpp/lvgl_cpp.h"

/**
 * LVGL PORTING LAYER: Header
 * --------------------------
 * The LvglPort class acts as the bridge between the hardware-specific LCD
 * drivers and the platform-independent LVGL graphics library. It manages
 * memory, threading, and performance optimizations.
 */
class LvglPort {
 public:
  struct Config {
    uint16_t h_res = 240;
    uint16_t v_res = 240;
    uint32_t task_stack_size = 32768;  // Standard UI stack
    int task_priority = 5;             // High priority, but below system tasks
    uint32_t tick_period_ms = 5;       // UI Heartbeat period

    // Optimization Knobs (Driven by workshop_config.h)
    // ---------------------------------------------
    uint32_t malloc_caps = MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL;
    bool double_buffered = false;
    bool full_frame = false;
    bool use_intrinsics = false;
    lv_display_render_mode_t render_mode = LV_DISPLAY_RENDER_MODE_PARTIAL;
  };

  explicit LvglPort(const Config& config);
  ~LvglPort();

  /**
   * init: Initializes the LVGL ecosystem.
   * Links hardware handles to the software rendering engine.
   */
  void init(esp_lcd_panel_handle_t panel_handle,
            esp_lcd_panel_io_handle_t io_handle);

  /**
   * Thread safety: lock/unlock ensure that the background rendering task
   * and the UI thread (usually app_main) don't conflict.
   */
  bool lock(uint32_t timeout_ms = 0);
  void unlock();

  void set_rotation(lvgl::Display::Rotation rotation);
  lvgl::Display* get_display() { return display_.get(); }

  /**
   * register_touch_driver: Binds a low-level hardware touch driver (e.g.,
   * CHSC6X) to the LVGL event system. This uses a template to abstract away the
   * specific hardware implementation.
   */
  template <typename T>
  void register_touch_driver(T* driver) {
    if (indev_) {
      indev_->set_read_cb([driver](lvgl::IndevData& data) {
        uint16_t x = 0, y = 0;
        bool pressed = false;
        driver->read(&x, &y, &pressed);

        if (pressed) {
          ESP_LOGD("LvglPort", "Touch: x=%d, y=%d", x, y);
          data.set_state(lvgl::IndevState::Pressed);
          data.set_point(x, y);
        } else {
          data.set_state(lvgl::IndevState::Released);
        }
      });
    }
  }

 private:
  // Trampolines: Used to bridge the GAP between C callbacks (FreeRTOS/Lcd)
  // and our C++ class methods.
  static bool notify_flush_ready_trampoline(
      esp_lcd_panel_io_handle_t io_panel, esp_lcd_panel_io_event_data_t* edata,
      void* user_ctx);
  static void tick_increment_trampoline(void* arg);
  static void task_trampoline(void* arg);

  /**
   * task_loop: The internal rendering task that runs on a dedicated core.
   */
  void task_loop();

  Config config_;
  std::unique_ptr<lvgl::Display> display_;
  std::unique_ptr<lvgl::PointerInput> indev_;

  // Rendering Buffers
  uint8_t* draw_buffer_ = nullptr;
  uint8_t* draw_buffer2_ = nullptr;
  size_t draw_buffer_sz_ = 0;

  // Hardware Handles
  esp_lcd_panel_handle_t panel_handle_ = nullptr;

  // OS Primitive Handles
  SemaphoreHandle_t api_lock_ = nullptr;
  esp_timer_handle_t tick_timer_ = nullptr;
  TaskHandle_t task_handle_ = nullptr;
};
