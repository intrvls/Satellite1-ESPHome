#pragma once

#include "esphome/core/component.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/light/addressable_light.h"

namespace esphome {
namespace led_ring_controller {

class LedRingController : public Component {
 public:
  void set_strip(light::AddressableLightState *strip) { strip_ = strip; }
  void set_user_light(light::LightState *user_light) { user_light_ = user_light; }
  void set_frame_interval_ms(uint32_t ms) { frame_interval_ms_ = ms; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

 protected:
  light::AddressableLightState *strip_{nullptr};
  light::LightState *user_light_{nullptr};
  uint32_t frame_interval_ms_{20};
};

}  // namespace led_ring_controller
}  // namespace esphome
