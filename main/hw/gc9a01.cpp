#if defined(noreturn)
#undef noreturn
#endif
#include "gc9a01.h"

#include "esp_lcd_gc9a01.h"
#include "esp_log.h"

static const char* TAG = "Gc9a01";

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
  ESP_LOGI(TAG, "Initialize SPI bus");
  spi_bus_config_t buscfg = {};
  buscfg.mosi_io_num = config_.mosi_io_num;
  buscfg.miso_io_num = -1;
  buscfg.sclk_io_num = config_.sclk_io_num;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  // Support full frame 240x240x2 DMA transfers
  buscfg.max_transfer_sz = (int)(240 * 240 * sizeof(uint16_t));

  ESP_ERROR_CHECK(spi_bus_initialize(config_.host, &buscfg, SPI_DMA_CH_AUTO));

  esp_lcd_panel_io_spi_config_t io_config = {
      .cs_gpio_num = (gpio_num_t)config_.cs_io_num,
      .dc_gpio_num = (gpio_num_t)config_.dc_io_num,
      .spi_mode = 0,
      .pclk_hz = config_.pclk_hz,
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

  ESP_LOGI(TAG, "Install GC9A01 panel driver");
  esp_lcd_panel_dev_config_t panel_config = {
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
      .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
      .bits_per_pixel = 16,
      .reset_gpio_num = GPIO_NUM_NC,
      .vendor_config = NULL,
      .flags = {.reset_active_high = 0},
  };
  ESP_ERROR_CHECK(
      esp_lcd_new_panel_gc9a01(io_handle_, &panel_config, &panel_handle_));

  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle_));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle_));
  ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle_, false));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle_, true));
  ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle_, true));
  ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle_, true, true));

  ESP_LOGI(TAG, "Initialize Backlight");
  gpio_num_t bl_gpio = (gpio_num_t)config_.bl_io_num;
  gpio_reset_pin(bl_gpio);
  gpio_set_direction(bl_gpio, GPIO_MODE_OUTPUT);
  gpio_set_level(bl_gpio, 1);

  return ESP_OK;
}
