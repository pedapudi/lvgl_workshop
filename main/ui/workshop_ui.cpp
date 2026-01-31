#include "workshop_ui.h"

#include <cstring>

#include "../hummingbird.h"
#include "../raccoon.h"
#include "../whale.h"
#include "esp_log.h"

/**
 * WORKSHOP UI: Implementation
 * ---------------------------
 * This file contains the design and animation logic for the workshop.
 * We use the high-level `lvgl_cpp` wrappers to keep the code clean and
 * object-oriented.
 */

static const char* TAG = "WorkshopUI";

/**
 * SVG-TO-LVGL ANIMATION BRIDGE
 * ----------------------------
 * This helper uses LVGL's internal cubic-bezier engine to perfectly match
 * the "keySplines" found in SVG specifications.
 */
static int32_t svg_bezier_path(const lv_anim_t* a, int32_t x1, int32_t y1,
                               int32_t x2, int32_t y2) {
  // 1. Map current time to 0..1024 range
  int32_t t = lv_map(a->act_time, 0, a->duration, 0, LV_BEZIER_VAL_MAX);

  // 2. Compute the Bezier step (0..1024)
  int32_t step = lv_cubic_bezier(t, x1, y1, x2, y2);

  // 3. Interpolate between start and end values
  int32_t range = a->end_value - a->start_value;
  int32_t val = (step * range) >> LV_BEZIER_VAL_SHIFT;
  return a->start_value + val;
}

WorkshopUI::WorkshopUI() : current_animal_(Animal::Hummingbird) {}

void WorkshopUI::init(lvgl::Display& display) {
  ESP_LOGI(TAG, "Initializing UI");

  // Create and load the base screen object.
  screen_ = std::make_unique<lvgl::Object>();
  display.load_screen(*screen_);

  // Configure the screen with a soft blue background.
  screen_->style()
      .bg_color(lvgl::Color::from_hex(0xE0F2FE))
      .bg_opa(lvgl::Opacity::Cover)
      .border_width(0)
      .radius(0);

  // Toggle between animals when the screen is clicked/touched.
  screen_->add_event_cb(lvgl::EventCode::Clicked,
                        [this](lvgl::Event& e) { this->next_animal(); });

  // Start with the Hummingbird view.
  setup_hummingbird(*screen_);
}

void WorkshopUI::next_animal() {
  if (current_animal_ == Animal::Hummingbird) {
    current_animal_ = Animal::Raccoon;
    setup_raccoon(*screen_);
  } else if (current_animal_ == Animal::Raccoon) {
    current_animal_ = Animal::Whale;
    setup_whale(*screen_);
  } else {
    current_animal_ = Animal::Hummingbird;
    setup_hummingbird(*screen_);
  }
}

void WorkshopUI::setup_whale(lvgl::Object& parent) {
  parent.clean();
  current_image_.reset();

  ESP_LOGI(TAG, "Setting up Whale");

  const char* raw_svg_ptr = whale_svg;
  while (*raw_svg_ptr && *raw_svg_ptr != '<') raw_svg_ptr++;

  // Whale is rendered at 150x150 pixels.
  static lvgl::ImageDescriptor whale_dsc(150, 150, LV_COLOR_FORMAT_RAW,
                                         (const uint8_t*)raw_svg_ptr,
                                         strlen(raw_svg_ptr) + 1);

  current_image_ = std::make_unique<lvgl::Image>(parent);
  current_image_->set_src(whale_dsc).center();

  // LAYERED WHALE ANIMATION:
  // We interpret the SVG's <animateTransform> tags and map them to LVGL
  // objects.

  // Component 1: BOBBING (Translate Y)
  // SVG: values="0 2; 0 -2; 0 2", keySplines="0.45 0 0.55 1"
  lvgl::Animation bob;
  bob.set_var(*current_image_)
      .set_values(6, -6)  // Slightly amplified for visual impact
      .set_duration(2000)
      .set_playback_duration(2000)
      .set_repeat_count(LV_ANIM_REPEAT_INFINITE)
      .set_path_cb(
          static_cast<lvgl::Animation::PathCallback>([](const lv_anim_t* a) {
            // Curve: Ease-In-Out (0.45, 0, 0.55, 1)
            return svg_bezier_path(a, 461, 0, 563, 1024);
          }))
      .set_exec_cb(
          [](lvgl::Object& obj, int32_t val) { obj.style().translate_y(val); })
      .start();

  // Component 2: SWIMMING TILT (Rotation)
  // SVG: values="-8 0 0; 8 0 0; -8 0 0", dur="2s"
  lvgl::Animation tilt;
  tilt.set_var(*current_image_)
      .set_values(-80, 80)  // +/- 8.0 degrees
      .set_duration(1000)
      .set_playback_duration(1000)
      .set_repeat_count(LV_ANIM_REPEAT_INFINITE)
      .set_path_cb(
          static_cast<lvgl::Animation::PathCallback>([](const lv_anim_t* a) {
            // Match the same smooth spline
            return svg_bezier_path(a, 461, 0, 563, 1024);
          }))
      .set_exec_cb([](lvgl::Object& obj, int32_t val) {
        static_cast<lvgl::Image&>(obj).set_rotation(val);
      })
      .start();
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

  // RACCOON BREATHING & BLINKING:
  // Combines scale-based breathing with a subtle position shift.

  lvgl::Animation breathe;
  breathe.set_var(*current_image_)
      .set_values(140, 240)
      .set_duration(4000)
      .set_repeat_count(LV_ANIM_REPEAT_INFINITE)
      .set_playback_duration(4000)
      .set_path_cb(
          static_cast<lvgl::Animation::PathCallback>([](const lv_anim_t* a) {
            // Custom "Slow Breathing" curve: heavy ease-in-out
            return svg_bezier_path(a, 680, 0, 340, 1024);
          }))
      .set_exec_cb([](lvgl::Object& obj, int32_t val) {
        static_cast<lvgl::Image&>(obj).set_scale(val);
      })
      .start();

  // Subtle Bobbing to make it feel less static
  lvgl::Animation bob;
  bob.set_var(*current_image_)
      .set_values(0, 4)
      .set_duration(4000)
      .set_playback_duration(4000)
      .set_repeat_count(LV_ANIM_REPEAT_INFINITE)
      .set_exec_cb(
          [](lvgl::Object& obj, int32_t val) { obj.style().translate_y(val); })
      .start();
}
