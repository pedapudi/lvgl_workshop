#pragma once

#if defined(noreturn)
#undef noreturn
#endif
#include <memory>

#include "lvgl_cpp.h"

class WorkshopUI {
 public:
  WorkshopUI();
  ~WorkshopUI() = default;

  void init(lvgl::Display& display);
  void next_animal();

 private:
  void setup_hummingbird(lvgl::Object& parent);
  void setup_raccoon(lvgl::Object& parent);
  void setup_whale(lvgl::Object& parent);

  enum class Animal { Hummingbird, Raccoon, Whale };

  Animal current_animal_ = Animal::Hummingbird;
  std::unique_ptr<lvgl::Object> screen_;
  std::unique_ptr<lvgl::Image> current_image_;
};
