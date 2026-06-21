#pragma once

#include "scene.h"

namespace esphome {
namespace led_ring_controller {

// Voice assistant phases — mirror voice_assistant.yaml substitutions exactly.
constexpr int VA_IDLE = 1;
constexpr int VA_WAITING = 2;
constexpr int VA_LISTENING = 3;
constexpr int VA_THINKING = 4;
constexpr int VA_REPLYING = 5;
constexpr int VA_NOT_READY = 10;
constexpr int VA_ERROR = 11;

// XMOS flashing state values — mirror satellite1.base.yaml globals exactly.
constexpr int XMOS_IDLE = 0;
constexpr int XMOS_FLASHING = 1;  // in progress; sweep animation running
constexpr int XMOS_SUCCESS = 2;   // flash succeeded; one-shot green pulse
constexpr int XMOS_ERROR = 3;     // flash failed;   one-shot red pulse

struct Facts {
  // XMOS flashing pipeline
  int xmos_flashing_state{XMOS_IDLE};  // XMOS_IDLE / XMOS_FLASHING / XMOS_SUCCESS / XMOS_ERROR
  float xmos_flash_progress{0.f};      // 0..1; maps to LED index for Sweep animation

  // Onboarding / connectivity
  bool improv_ble{false};
  bool init_in_progress{true};  // true at boot; cleared on HA connect or 10-min timeout
  bool network_ok{false};       // true when BOTH wifi AND api are connected

  // Transient user interactions (momentary; YAML caller owns the clear timing)
  bool volume_buttons_touched{false};
  bool btn_action{false};

  // Jack events (one-shot; engine clears via on_finished after animation completes)
  bool jack_plugged{false};
  bool jack_unplugged{false};

  // Alerts
  bool warning{false};  // one-shot; engine clears via on_finished

  // Timer
  bool timer_ringing{false};
  bool is_timer_active{false};
  float timer_ratio{0.f};  // 0..1; seconds_left / total_seconds

  // Voice assistant
  int va_phase{VA_NOT_READY};

  // Audio state
  bool master_mute{false};
  bool media_muted{false};  // true when media_volume == 0 OR player is_muted()
  float media_volume{0.f};  // 0..1
};

class StateMachine {
 public:
  // Table evaluated top-to-bottom; first match wins.
  SceneId resolve(const Facts &f) const;
};

}  // namespace led_ring_controller
}  // namespace esphome
