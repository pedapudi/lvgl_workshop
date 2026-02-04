#if defined(noreturn)
#undef noreturn
#endif
#include "gc9a01.h"

#include "esp_lcd_gc9a01.h"
#include "esp_log.h"

static const char* TAG = "Gc9a01";

/**
 * GC9A01 DISPLAY DRIVER: Implementation
 * -------------------------------------
 * This file handles the low-level SPI communication and LCD command
 * sequences for the Seeed Round Display.
 */

Gc9a01::Gc9a01(const Config& config) : config_(config) {}

Gc9a01::~Gc9a01() {
  if (panel_handle_) {
    esp_lcd_panel_del(panel_handle_);
  }
  if (io_handle_) {
    esp_lcd_panel_io_del(io_handle_);
  }
}

esp_err_t Gc9a01::init() {
  // 1. SPI BUS INITIALIZATION
  // -------------------------
  // We configure the SPI bus to handle the high speeds (80MHz) required
  // for smooth animations.
  ESP_LOGI(TAG, "Initialize SPI bus");
  spi_bus_config_t buscfg = {};
  buscfg.mosi_io_num = config_.mosi_io_num;
  buscfg.miso_io_num = -1;  // No input needed from the display
  buscfg.sclk_io_num = config_.sclk_io_num;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  // Support full frame 240x240x2 DMA transfers (essential for Phase 4)
  buscfg.max_transfer_sz = (int)(240 * 240 * sizeof(uint16_t));

  ESP_ERROR_CHECK(spi_bus_initialize(config_.host, &buscfg, SPI_DMA_CH_AUTO));

  // 2. PANEL I/O CONFIGURATION
  // --------------------------
  // Link the SPI bus to the LCD-specific protocol (CS, DC, Speed).
  esp_lcd_panel_io_spi_config_t io_config = {
      .cs_gpio_num = (gpio_num_t)config_.cs_io_num,
      .dc_gpio_num = (gpio_num_t)config_.dc_io_num,
      .spi_mode = 0,
      .pclk_hz =
          config_.pclk_hz,  // Consumes SPI_BUS_SPEED from workshop_config.h
      .trans_queue_depth = 10,
      .on_color_trans_done = NULL,
      .user_ctx = NULL,
      .lcd_cmd_bits = 8,
      .lcd_param_bits = 8,
      .cs_ena_pretrans = 0,
      .cs_ena_posttrans = 0,
      .flags =
          {
              .dc_high_on_cmd = 0,
              .dc_low_on_data = 0,
              .dc_low_on_param = 0,
              .octal_mode = 0,
              .quad_mode = 0,
              .sio_mode = 0,
              .lsb_first = 0,
              .cs_high_active = 0,
          },
  };
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
      (esp_lcd_spi_bus_handle_t)config_.host, &io_config, &io_handle_));

  // 3. GC9A01 PANEL SPECIFICS
  // -------------------------
  // Install the manufacturer-specific initialization sequence.
  ESP_LOGI(TAG, "Install GC9A01 panel driver");
  esp_lcd_panel_dev_config_t panel_config = {
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
      .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
      .bits_per_pixel = 16,
      .reset_gpio_num = GPIO_NUM_NC,
      .vendor_config = NULL,
      .flags = {.reset_active_high = 0},
  };
  ESP_ERROR_CHECK(
      esp_lcd_new_panel_gc9a01(io_handle_, &panel_config, &panel_handle_));

  // Power on and reset the display.
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle_));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle_));

  // Custom display parameters for the Round Screen
  ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle_, true));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle_, true));
  ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle_, true));
  ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle_, true, true));

  // 4. BACKLIGHT CONTROL
  // --------------------
  // Simple GPIO-based backlight logic.
  ESP_LOGI(TAG, "Initialize Backlight");
  gpio_num_t bl_gpio = (gpio_num_t)config_.bl_io_num;
  gpio_reset_pin(bl_gpio);
  gpio_set_direction(bl_gpio, GPIO_MODE_OUTPUT);
  gpio_set_level(bl_gpio, 1);

  return ESP_OK;
}
