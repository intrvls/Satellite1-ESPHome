#include "transition.h"

#include "esphome/core/hal.h"

#include <algorithm>

namespace esphome {
namespace led_ring_controller {

void TransitionManager::begin(const FrameBuffer &prev_final, uint32_t duration_ms, EasingFn easing) {
  if (duration_ms == 0) {
    // Hard cut — no crossfade frames.
    this->active_ = false;
    return;
  }
  this->prev_final_ = prev_final;  // FrameBuffer copy-assign resizes to match
  this->duration_ms_ = duration_ms;
  this->easing_ = easing ? easing : linear;
  this->start_ms_ = millis();
  this->active_ = true;
}

void TransitionManager::apply(FrameBuffer &new_frame, uint32_t now_ms) const {
  if (!this->active_)
    return;

  float t = static_cast<float>(now_ms - this->start_ms_) / static_cast<float>(this->duration_ms_);
  if (t >= 1.0f) {
    this->active_ = false;
    return;
  }

  float e = this->easing_(t);
  float inv = 1.0f - e;
  uint8_t n = std::min(new_frame.size(), this->prev_final_.size());
  for (uint8_t i = 0; i < n; i++) {
    new_frame[i].r = this->prev_final_[i].r * inv + new_frame[i].r * e;
    new_frame[i].g = this->prev_final_[i].g * inv + new_frame[i].g * e;
    new_frame[i].b = this->prev_final_[i].b * inv + new_frame[i].b * e;
  }
}

}  // namespace led_ring_controller
}  // namespace esphome
