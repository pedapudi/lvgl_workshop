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
  // ------------------------------------
  // Embedded graphics are CPU-intensive. To ensure smooth 30+ GPS rendering of
  // complex SVGs, we dynamically scale the CPU frequency.
  // Foundation Phases (1-3) run at 160MHz to save power.
  // Expert Phases (4+) boost to 240MHz to handle the parallel overhead of DMA
  // and color conversion without jitter.
  esp_pm_config_t pm_config = {
      .max_freq_mhz = Workshop::CPU_FREQ_MHZ,
      .min_freq_mhz = Workshop::CPU_FREQ_MHZ,
      .light_sleep_enable = false,
  };
  ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

  // 1. Display Hardware
  // --------------------
  // This Gc9a01 object manages the raw SPI communication. It doesn't know
  // about "buttons" or "animations"—it only knows how to send raw pixel
  // streams to the round LCD glass.
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
  auto display_hw = std::make_unique<Gc9a01>(display_cfg);
  display_hw->init();

  // 2. Touch Hardware
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
  auto chsc6x = std::make_unique<Chsc6x>(touch_cfg);
  // Wait for the touch chip to finish its internal boot (approx 1s).
  vTaskDelay(pdMS_TO_TICKS(1000));
  chsc6x->init();

  // 3. LVGL Porting Layer
  LvglPort::Config lvgl_config;
  lvgl_config.h_res = 240;
  lvgl_config.v_res = 240;
  lvgl_config.task_stack_size = Workshop::LVGL_STACK_SIZE;
  lvgl_config.task_priority = 5;
  lvgl_config.task_affinity = Workshop::LVGL_TASK_CORE;

  ESP_LOGI(TAG, "Initializing LVGL Port on Core %d", Workshop::LVGL_TASK_CORE);
  auto lvgl_port = std::make_unique<LvglPort>(lvgl_config);
  lvgl_port->init(display_hw->get_panel_handle(), display_hw->get_io_handle());

  lvgl_port->register_touch_driver(chsc6x.get());

  // 4. UI Layer
  // -----------
  // Now that the foundations (Display, Touch, Port) are ready, we build the
  // visual world.
  static WorkshopUI ui;

  // CRITICAL: Since the LvglPort task is already running in the background,
  // we MUST lock the mutex before creating or modifying any UI elements.
  // Failing to do so would result in a race condition where the renderer
  // attempts to draw an object that is only half-initialized.
  if (lvgl_port->lock(-1)) {
    if (auto* display = lvgl_port->get_display()) {
      ui.init(*display);
    }
    lvgl_port->unlock();
  }

  // The main task remains running for system maintenance.
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
