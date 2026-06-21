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

// Boolean Facts fields settable from YAML via led_ring_controller.set_flag.
enum class LedFlag {
  WARNING,
  JACK_PLUGGED,
  JACK_UNPLUGGED,
  VOLUME_BUTTONS_TOUCHED,
  BTN_ACTION,
  INIT_IN_PROGRESS,
  IMPROV_BLE,
  MASTER_MUTE,
  MEDIA_MUTED,
  NETWORK_OK,
  TIMER_RINGING,
  IS_TIMER_ACTIVE,
};

// Momentary state changes signalled from YAML via led_ring_controller.event.
enum class LedEvent {
  WARNING,
  JACK_PLUGGED,
  JACK_UNPLUGGED,
  XMOS_FLASH_START,
  XMOS_FLASH_PROGRESS,
  XMOS_SUCCESS,
  XMOS_ERROR,
};

class LedRingController : public Component {
 public:
  void set_strip(light::AddressableLightState *strip) { strip_ = strip; }
  void set_user_light(light::LightState *user_light) { user_light_ = user_light; }
  void set_frame_interval_ms(uint32_t ms) { frame_interval_ms_ = ms; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  // Fact mutation surface — the actions in automation.h forward to these. The engine
  // re-resolves the active scene every frame, so callers only need to update a field.
  void set_va_phase(int phase) { this->facts_.va_phase = phase; }
  void set_flag(LedFlag flag, bool value);
  void set_media_volume(float volume) {
    this->facts_.media_volume = volume;
    this->facts_.media_muted = (volume == 0.0f);
  }
  void set_timer_ratio(float ratio) { this->facts_.timer_ratio = ratio; }
  void handle_event(LedEvent event, float value);

 protected:
  // Converts the work buffer (linear float RGB) to the AddressableLight and schedules a show.
  // Per-channel: uint8_t(clamp(v, 0, 1) * 255 + 0.5). GRB ordering + the strip's configured
  // colour/gamma correction are handled by ESPHome's ESPColorView, matching the prior system.
  void write_frame_(const FrameBuffer &frame);

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
