#pragma once

#include "esphome/core/component.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/light/addressable_light.h"

#include "compositor.h"
#include "frame.h"
#include "scene.h"
#include "scene_library.h"
#include "state_machine.h"
#include "transition.h"

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
  // Converts the work buffer (linear float RGB) to the AddressableLight and schedules a show.
  // Per-channel: uint8_t(clamp(v, 0, 1) * 255 + 0.5). GRB ordering + the strip's configured
  // colour/gamma correction are handled by ESPHome's ESPColorView, matching the prior system.
  void write_frame_(const FrameBuffer &frame);

  // Temporary — remove before issue 07 merge. Logs issue 03/04/05 acceptance checks.
  void run_selftest_();

  light::AddressableLightState *strip_{nullptr};
  light::LightState *user_light_{nullptr};
  uint32_t frame_interval_ms_{20};

  // Resolved once in setup() from strip_->get_output().
  light::AddressableLight *strip_out_{nullptr};

  Facts facts_;
  StateMachine sm_;
  SceneLibrary library_;
  Compositor compositor_;
  TransitionManager transition_;

  FrameBuffer work_{24};
  FrameBuffer prev_{24};

  SceneId active_scene_{SceneId::IDLE};
  uint32_t last_frame_ms_{0};
};

}  // namespace led_ring_controller
}  // namespace esphome
