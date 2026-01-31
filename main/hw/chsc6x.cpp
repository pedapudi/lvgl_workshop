#include "chsc6x.h"

#include <string.h>

#include "esp_log.h"

static const char* TAG = "Chsc6x";
#define CHSC6X_I2C_ADDRESS 0x2E

/**
 * CHSC6X TOUCH DRIVER: Implementation
 * -----------------------------------
 * This file handles the I2C communication and coordinate mapping for the
 * capacitive touch controller on the XIAO Round Display.
 */

Chsc6x::Chsc6x(const Config& config) : config_(config) {}

Chsc6x::~Chsc6x() {
  if (dev_handle_) {
    i2c_master_bus_rm_device(dev_handle_);
  }
  if (bus_handle_) {
    i2c_del_master_bus(bus_handle_);
  }
}

esp_err_t Chsc6x::init() {
  // 1. I2C MASTER BUS INITIALIZATION
  // --------------------------------
  // Configure the I2C physical layer (Pins, internal pull-ups, logic).
  ESP_LOGI(TAG, "Initialize I2C bus");
  i2c_master_bus_config_t i2c_mst_config = {
      .i2c_port = config_.i2c_port,
      .sda_io_num = (gpio_num_t)config_.sda_io_num,
      .scl_io_num = (gpio_num_t)config_.scl_io_num,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,  // Hardware debounce for noisy lines
      .intr_priority = 0,
      .trans_queue_depth = 0,
      .flags =
          {
              .enable_internal_pullup = true,
              .allow_pd = false,
          },
  };
  ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_config, &bus_handle_));

  // 2. CHILD DEVICE CONFIGURATION
  // -----------------------------
  // Link the specific CHSC6X device to the I2C bus.
  i2c_device_config_t dev_cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = CHSC6X_I2C_ADDRESS,
      .scl_speed_hz = config_.clk_speed,  // Usually 400kHz
      .scl_wait_us =
          1000,  // Handle clock stretching potentially used by some touch chips
      .flags = {.disable_ack_check = 0},
  };
  ESP_ERROR_CHECK(
      i2c_master_bus_add_device(bus_handle_, &dev_cfg, &dev_handle_));

  return ESP_OK;
}

esp_err_t Chsc6x::read(uint16_t* x, uint16_t* y, bool* pressed) {
  if (!dev_handle_) {
    return ESP_ERR_INVALID_STATE;
  }

  // 3. READ RAW TOUCH PACKET
  // ------------------------
  // The CHSC6X returns a 6-byte packet containing touch state and coordinates.
  uint8_t data[6] = {0};
  // Use a reasonable timeout (100ms) to avoid blocking the UI task forever.
  esp_err_t ret = i2c_master_receive(dev_handle_, data, 6, 100);
  if (ret != ESP_OK) {
    if (ret == ESP_ERR_TIMEOUT) {
      // Throttle timeout logs to avoid console flooding if the cable is
      // disconnected.
      static uint32_t last_timeout_log = 0;
      if (esp_log_timestamp() - last_timeout_log > 5000) {
        ESP_LOGW(TAG, "I2C read timeout - touch controller not responding");
        last_timeout_log = esp_log_timestamp();
      }
    }
    return ret;
  }

  // Debugging: Log raw touch packets when a touch is detected (Status 0x01+)
  if (data[0] >= 0x01) {
    ESP_LOGI(TAG, "Raw: %02x %02x %02x %02x %02x %02x", data[0], data[1],
             data[2], data[3], data[4], data[5]);
  }

  // 4. COORDINATE MAPPING
  // ---------------------
  // Map the raw hardware coordinates (0.01) to the logical LVGL screen area.
  if (data[0] == 0x01) {
    *pressed = true;
    // Extract 10-bit coordinates from the byte stream.
    int tx = data[2] | ((data[3] & 0x03) << 8);
    int ty = data[4] | ((data[5] & 0x03) << 8);

    int x_coord = tx;
    int y_coord = ty;

    // Apply rotation/mirroring transformation based on configuration.
    if (config_.swap_xy) {
      x_coord = ty;
      y_coord = tx;
    }

    if (config_.mirror_x) {
      x_coord = config_.h_res - 1 - x_coord;
    }

    if (config_.mirror_y) {
      y_coord = config_.v_res - 1 - y_coord;
    }

    // Safety boundary clipping.
    if (x_coord < 0) x_coord = 0;
    if (x_coord >= config_.h_res) x_coord = config_.h_res - 1;
    *x = (uint16_t)x_coord;

    if (y_coord < 0) y_coord = 0;
    if (y_coord >= config_.v_res) y_coord = config_.v_res - 1;
    *y = (uint16_t)y_coord;

    ESP_LOGD(TAG, "Touch: x=%d, y=%d", *x, *y);
  } else {
    *pressed = false;
  }

  return ESP_OK;
}
