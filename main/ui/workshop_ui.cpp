#include "workshop_ui.h"

#include <cstring>

#include "../hummingbird.h"
#include "../raccoon.h"
#include "esp_log.h"

/**
 * WORKSHOP UI: Implementation
 * ---------------------------
 * This file contains the design and animation logic for the workshop.
 * We use the high-level `lvgl_cpp` wrappers to keep the code clean and
 * object-oriented.
 */

static const char* TAG = "WorkshopUI";

WorkshopUI::WorkshopUI() : current_animal_(Animal::Hummingbird) {}

void WorkshopUI::init(lvgl::Display& display) {
  ESP_LOGI(TAG, "Initializing UI");

  // Create and load the base screen object.
  // We use std::unique_ptr for automatic memory management.
  screen_ = std::make_unique<lvgl::Object>();
  display.load_screen(*screen_);

  // Create a full-screen container to act as the root for our visuals.
  // It provides a soft blue background.
  container_ = std::make_unique<lvgl::Object>(screen_.get());
  container_->set_size(LV_PCT(100), LV_PCT(100));
  container_->style()
      .bg_color(lvgl::Color::from_hex(0xE0F2FE))
      .bg_opa(lvgl::Opacity::Cover)
      .border_width(0)
      .radius(0);

  // Toggle between animals when the screen is clicked/touched.
  container_->add_event_cb(lvgl::EventCode::Clicked,
                           [this](lvgl::Event& e) { this->next_animal(); });

  // Start with the Hummingbird view.
  setup_hummingbird(*container_);
}

void WorkshopUI::next_animal() {
  if (current_animal_ == Animal::Hummingbird) {
    current_animal_ = Animal::Raccoon;
    setup_raccoon(*container_);
  } else {
    current_animal_ = Animal::Hummingbird;
    setup_hummingbird(*container_);
  }
}

void WorkshopUI::setup_hummingbird(lvgl::Object& parent) {
  // Clean up previous UI elements to free memory.
  parent.clean();
  current_image_.reset();

  ESP_LOGI(TAG, "Setting up Hummingbird");

  // SVG Pointer Logic:
  // We skip any leading metadata/whitespace in the header file to find
  // the actual XML start tag '<'.
  const char* raw_svg_ptr = hummingbird_svg;
  while (*raw_svg_ptr && *raw_svg_ptr != '<') raw_svg_ptr++;

  // Image Descriptor:
  // ThorVG reads the SVG data from this static descriptor.
  static lvgl::ImageDescriptor bird_dsc(75, 75, LV_COLOR_FORMAT_RAW,
                                        (const uint8_t*)raw_svg_ptr,
                                        strlen(raw_svg_ptr) + 1);

  // Display the SVG using a standard LVGL Image object.
  current_image_ = std::make_unique<lvgl::Image>(parent);
  current_image_->set_src(bird_dsc).center();
}

void WorkshopUI::setup_raccoon(lvgl::Object& parent) {
  parent.clean();
  current_image_.reset();

  ESP_LOGI(TAG, "Setting up Raccoon");

  // Similar SVG pointer logic for the Raccoon.
  const char* raw_svg_ptr = raccoon_svg;
  while (*raw_svg_ptr && *raw_svg_ptr != '<') raw_svg_ptr++;

  // Raccoon is rendered at 180x180 pixels.
  // This size was carefully chosen to balance visual quality and
  // rasterization speed on the ESP32-S3.
  static lvgl::ImageDescriptor raccoon_dsc(180, 180, LV_COLOR_FORMAT_RAW,
                                           (const uint8_t*)raw_svg_ptr,
                                           strlen(raw_svg_ptr) + 1);

  current_image_ = std::make_unique<lvgl::Image>(parent);
  current_image_->set_src(raccoon_dsc).center();

  // BREATHING ANIMATION:
  // Animates the scale of the image to create a "breathing" effect.
  // 160-220 scaling range (where 256 is 100%).
  lvgl::Animation anim;
  anim.set_var(*current_image_)
      .set_values(140, 240)
      .set_duration(4000)
      .set_repeat_count(LV_ANIM_REPEAT_INFINITE)
      .set_playback_duration(4000)
      .set_path_cb(lvgl::Animation::Path::EaseInOut())
      .set_exec_cb([](lvgl::Object& obj, int32_t val) {
        // We cast to Image to access the .set_scale() method.
        static_cast<lvgl::Image&>(obj).set_scale(val);
      })
      .start();
}
