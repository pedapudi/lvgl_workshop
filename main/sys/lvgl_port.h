#pragma once

#include <memory>
#include <vector>

#include "display/display.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "lvgl_cpp/indev/pointer_input.h"
#include "lvgl_cpp/lvgl_cpp.h"

namespace lvgl {
class Esp32Spi;
}

/**
 * LVGL PORTING LAYER
 * -------------------
 * This class handles the integration between the generic LVGL library
 * and the ESP32-S3 hardware. It manages the rendering task, memory
 * allocation for buffers, and synchronization.
 */
class LvglPort {
 public:
  struct Config {
    int h_res = 240;
    int v_res = 240;
    uint32_t tick_period_ms = 5;
    uint32_t task_stack_size = 32 * 1024;
    int task_priority = 5;
    BaseType_t task_affinity = tskNO_AFFINITY;
  };

  explicit LvglPort(const Config& config);
  ~LvglPort();

  /**
   * Initialize the LVGL porting layer.
   * @param panel_handle The esp_lcd panel handle.
   * @param io_handle The esp_lcd panel IO handle.
   */
  void init(esp_lcd_panel_handle_t panel_handle,
            esp_lcd_panel_io_handle_t io_handle);

  /**
   * Lock the LVGL API for thread-safe access.
   * @param timeout_ms The timeout in milliseconds.
   * @return True if successful, false otherwise.
   */
  bool lock(uint32_t timeout_ms);

  /**
   * Unlock the LVGL API.
   */
  void unlock();

  /**
   * Get the active LVGL display object.
   * @return A pointer to the display object.
   */
  lvgl::Display* get_display();

  /**
   * Set the display rotation.
   */
  void set_rotation(lvgl::Display::Rotation rotation);

  /**
   * Register a touch driver for interactive input.
   * @param driver A pointer to a driver object that implements read(x, y,
   * pressed).
   */
  template <typename T>
  void register_touch_driver(T* driver) {
    if (indev_) {
      indev_->set_read_cb([this, driver](lvgl::IndevData& data) {
        uint16_t x = 0, y = 0;
        bool pressed = false;
        if (driver->read(&x, &y, &pressed) == ESP_OK) {
          if (pressed) {
            data.set_point(x, y);
            data.set_state(lvgl::IndevState::Pressed);
          } else {
            data.set_state(lvgl::IndevState::Released);
          }
        }
      });
    }
  }

  /**
   * Wake the rendering task via event bits.
   */
  void notify_event(uint32_t event_bit);

 private:
  static void flush_cb_trampoline(lv_display_t* disp, const lv_area_t* area,
                                  uint8_t* px_map);
  void flush_cb(lvgl::Display& disp, const lv_area_t& area, uint8_t* px_map);

  static bool notify_flush_ready_trampoline(
      esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t* edata,
      void* user_ctx);
  static void tick_increment_trampoline(void* arg);
  static void task_trampoline(void* arg);

  void task_loop();

  Config config_;
  SemaphoreHandle_t api_lock_;
  TaskHandle_t task_handle_ = nullptr;
  TaskHandle_t creator_task_ = nullptr;
  esp_timer_handle_t tick_timer_ = nullptr;

  esp_lcd_panel_handle_t panel_handle_ = nullptr;

  std::unique_ptr<lvgl::Esp32Spi> display_driver_;
  std::unique_ptr<lvgl::Display> display_;
  uint8_t* draw_buf_ = nullptr;
  uint8_t* draw_buf2_ = nullptr;
  size_t draw_buf_size_ = 0;
  std::unique_ptr<lvgl::PointerInput> indev_;
};
