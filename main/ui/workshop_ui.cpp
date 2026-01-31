#include "workshop_ui.h"

#include <cstring>

#include "../hummingbird.h"
#include "../raccoon.h"
#include "esp_log.h"

static const char* TAG = "WorkshopUI";

WorkshopUI::WorkshopUI() : current_animal_(Animal::Hummingbird) {}

void WorkshopUI::init(lvgl::Display& display) {
  ESP_LOGI(TAG, "Initializing UI");
  // Create and load the base screen object
  screen_ = std::make_unique<lvgl::Object>();
  display.load_screen(*screen_);

  // Create a full-screen container to act as the root for our visuals
  container_ = std::make_unique<lvgl::Object>(screen_.get());
  container_->set_size(LV_PCT(100), LV_PCT(100));
  container_->style()
      .bg_color(lvgl::Color::from_hex(0xE0F2FE))
      .bg_opa(lvgl::Opacity::Cover)
      .border_width(0)
      .radius(0);

  // Click to toggle
  container_->add_event_cb(lvgl::EventCode::Clicked,
                           [this](lvgl::Event& e) { this->next_animal(); });

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
  parent.clean();
  current_image_.reset();

  ESP_LOGI(TAG, "Setting up Hummingbird");

  const char* raw_svg_ptr = hummingbird_svg;
  while (*raw_svg_ptr && *raw_svg_ptr != '<') raw_svg_ptr++;

  static lvgl::ImageDescriptor bird_dsc(75, 75, LV_COLOR_FORMAT_RAW,
                                        (const uint8_t*)raw_svg_ptr,
                                        strlen(raw_svg_ptr) + 1);

  current_image_ = std::make_unique<lvgl::Image>(parent);
  current_image_->set_src(bird_dsc).center();
}

void WorkshopUI::setup_raccoon(lvgl::Object& parent) {
  parent.clean();
  current_image_.reset();

  ESP_LOGI(TAG, "Setting up Raccoon");

  const char* raw_svg_ptr = raccoon_svg;
  while (*raw_svg_ptr && *raw_svg_ptr != '<') raw_svg_ptr++;

  static lvgl::ImageDescriptor raccoon_dsc(180, 180, LV_COLOR_FORMAT_RAW,
                                           (const uint8_t*)raw_svg_ptr,
                                           strlen(raw_svg_ptr) + 1);

  current_image_ = std::make_unique<lvgl::Image>(parent);
  current_image_->set_src(raccoon_dsc).center();

  // Breathing animation
  lvgl::Animation anim;
  anim.set_var(*current_image_)
      .set_values(160, 220)
      .set_duration(3000)
      .set_repeat_count(LV_ANIM_REPEAT_INFINITE)
      .set_playback_duration(3000)
      .set_path_cb(lvgl::Animation::Path::EaseInOut())
      .set_exec_cb([](lvgl::Object& obj, int32_t val) {
        static_cast<lvgl::Image&>(obj).set_scale(val);
      })
      .start();
}
