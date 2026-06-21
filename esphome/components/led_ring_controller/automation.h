#pragma once

#include "led_ring_controller.h"

#include "esphome/core/automation.h"

namespace esphome {
namespace led_ring_controller {

// led_ring_controller.set_phase
template<typename... Ts> class SetPhaseAction : public Action<Ts...>, public Parented<LedRingController> {
 public:
  TEMPLATABLE_VALUE(int, phase)
  void play(Ts... x) override { this->parent_->set_va_phase(this->phase_.value(x...)); }
};

// led_ring_controller.set_flag
template<typename... Ts> class SetFlagAction : public Action<Ts...>, public Parented<LedRingController> {
 public:
  TEMPLATABLE_VALUE(bool, value)
  void set_flag(LedFlag flag) { this->flag_ = flag; }
  void play(Ts... x) override { this->parent_->set_flag(this->flag_, this->value_.value(x...)); }

 protected:
  LedFlag flag_{LedFlag::WARNING};
};

// led_ring_controller.set_media_volume
template<typename... Ts> class SetMediaVolumeAction : public Action<Ts...>, public Parented<LedRingController> {
 public:
  TEMPLATABLE_VALUE(float, volume)
  void play(Ts... x) override { this->parent_->set_media_volume(this->volume_.value(x...)); }
};

// led_ring_controller.set_timer_ratio
template<typename... Ts> class SetTimerRatioAction : public Action<Ts...>, public Parented<LedRingController> {
 public:
  TEMPLATABLE_VALUE(float, ratio)
  void play(Ts... x) override { this->parent_->set_timer_ratio(this->ratio_.value(x...)); }
};

// led_ring_controller.event
template<typename... Ts> class EventAction : public Action<Ts...>, public Parented<LedRingController> {
 public:
  TEMPLATABLE_VALUE(float, value)  // used by XMOS_FLASH_PROGRESS only; 0 otherwise
  void set_event(LedEvent event) { this->event_ = event; }
  void play(Ts... x) override { this->parent_->handle_event(this->event_, this->value_.value(x...)); }

 protected:
  LedEvent event_{LedEvent::WARNING};
};

}  // namespace led_ring_controller
}  // namespace esphome
