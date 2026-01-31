#if defined(noreturn)
#undef noreturn
#endif

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hw/chsc6x.h"
#include "hw/gc9a01.h"
#include "sys/lvgl_port.h"
#include "ui/workshop_ui.h"
#include "workshop_config.h"

/**
 * ANIMATION WORKSHOP: Entry Point
 * ------------------------------
 * This file orchestrates the initialization of hardware, power management,
 * and the LVGL graphics ecosystem.
 */

static const char* TAG = "main";

extern "C" void app_main(void) {
  // 0. TELEMETRY & PHASE REPORTING
  // We log the current workshop phase and hardware specs to the console.
  ESP_LOGI(TAG, "Starting Animation Workshop - PHASE %d", WORKSHOP_PHASE);
  ESP_LOGI(TAG, "CPU: %d MHz, Bus: %d MHz, Memory: %s", Workshop::CPU_FREQ_MHZ,
           (int)(Workshop::SPI_BUS_SPEED / 1000000),
           (Workshop::ALLOC_CAPS & MALLOC_CAP_SPIRAM) ? "PSRAM" : "SRAM");

  // POWER MANAGEMENT (CPU CLOCK SCALING)
  // We programmatically set the CPU frequency based on the current Phase.
  // Phase 1: 160MHz (Baseline)
  // Phase 2+: 240MHz (Maximum performance)
  esp_pm_config_t pm_config = {
      .max_freq_mhz = Workshop::CPU_FREQ_MHZ,
      .min_freq_mhz = Workshop::CPU_FREQ_MHZ,
      .light_sleep_enable = false,
  };
  ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

  // 1. Display Hardware
  // Initialize the GC9A01 SPI display with the pinout for the XIAO Round
  // Display shield.
  Gc9a01::Config display_cfg = {
      .host = SPI2_HOST,
      .cs_io_num = 2,
      .dc_io_num = 4,
      .sclk_io_num = 7,
      .mosi_io_num = 9,
      .bl_io_num = 43,
      .pclk_hz =
          Workshop::SPI_BUS_SPEED,  // Speed is managed by workshop_config.h
      .h_res = 240,
      .v_res = 240,
  };
  Gc9a01 display_hw(display_cfg);
  display_hw.init();

  // 2. Touch Hardware
  // Initialize the CHSC6X I2C touch controller.
  Chsc6x::Config touch_cfg = {
      .i2c_port = I2C_NUM_0,
      .sda_io_num = 5,
      .scl_io_num = 6,
      .int_io_num = 44,
      .clk_speed = 400000,
      .h_res = 240,
      .v_res = 240,
      .swap_xy = true,
      .mirror_x = true,
      .mirror_y = false,
  };
  Chsc6x chsc6x(touch_cfg);
  // Wait for the touch chip to finish its internal boot (approx 1s).
  vTaskDelay(pdMS_TO_TICKS(500));
  chsc6x.init();

  // 3. LVGL Porting Layer
  // This layer handles the "glue" between LVGL and the ESP32 (Memory, Tasks,
  // Timers).
  LvglPort::Config lvgl_config;
  lvgl_config.task_stack_size =
      Workshop::LVGL_STACK_SIZE;                   // Increases in Phase 2+
  lvgl_config.malloc_caps = Workshop::ALLOC_CAPS;  // Moves to PSRAM in Phase 4
  lvgl_config.double_buffered = Workshop::USE_DOUBLE_BUFFERING;
  lvgl_config.full_frame =
      (Workshop::BUFFER_MODE == Workshop::BufferMode::FullFrame);
  lvgl_config.use_intrinsics = Workshop::USE_XTENSA_INTRINSICS;
  lvgl_config.render_mode = Workshop::LVGL_RENDER_MODE;

  LvglPort lvgl_port(lvgl_config);
  // Initialize the port with the low-level panel and IO handles from the
  // display driver.
  lvgl_port.init(display_hw.get_panel_handle(), display_hw.get_io_handle());
  // Attach the touch driver to the LVGL pointer input.
  lvgl_port.register_touch_driver(&chsc6x);

  // 4. UI Layer
  // Initialize the high-level workshop UI using the modern C++ wrappers.
  static WorkshopUI ui;
  // All LVGL API calls must be wrapped in a lock for thread safety with the
  // lvgl_task.
  if (lvgl_port.lock(-1)) {
    if (auto* display = lvgl_port.get_display()) {
      ui.init(*display);
    }
    lvgl_port.unlock();
  }

  // The main task remains running for system maintenance.
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
