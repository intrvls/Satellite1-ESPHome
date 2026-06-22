#pragma once

#include "animation.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace esphome {
namespace led_ring_controller {

// All valid scene identifiers. Listed roughly in priority order as a convention hint; the
// authoritative order is the table in state_machine.cpp (issue 05).
enum class SceneId : uint8_t {
  // XMOS flashing states (highest priority)
  XMOS_FLASH,       // xmos_flashing_state == XMOS_FLASHING; sweep while flashing
  XMOS_SUCCESS,     // xmos_flashing_state == XMOS_SUCCESS;  one-shot green pulse
  XMOS_ERROR,       // xmos_flashing_state == XMOS_ERROR;    one-shot red pulse

  // Onboarding / connectivity
  IMPROV,           // improv_ble active; warm-white twinkle
  INIT,             // init_in_progress && network_ok; blue twinkle
  INIT_NO_NETWORK,  // init_in_progress && !network_ok; warm-white solid
  NO_HA,            // !network_ok (post-init); red twinkle

  // Transient user interactions
  VOLUME,           // volume_buttons_touched; arc showing media volume
  ACTION_BUTTON,    // btn_action; solid full-ring flash
  JACK_PLUGGED,     // one-shot outward ripple
  JACK_UNPLUGGED,   // one-shot inward ripple

  // Alerts
  WARNING,          // one-shot red pulse x5, auto-clears facts_.warning
  TIMER_RING,       // timer_ringing; full-ring pulse with 2-position mute overlay

  // Voice assistant phases
  WAITING,          // va_phase == VA_WAITING;   slow CW blob
  LISTENING,        // va_phase == VA_LISTENING; fast CW blob
  THINKING,         // va_phase == VA_THINKING;  2-LED blink at positions 2+14
  REPLYING,         // va_phase == VA_REPLYING;  fast CCW blob
  ERROR,            // va_phase == VA_ERROR;     sustained red pulse (not a one-shot)
  NOT_READY,        // va_phase == VA_NOT_READY; red twinkle

  // Background
  LOUDNESS,         // audio_visualizer_enabled && audio_level > eps; ring glow tracking loudness
  TIMER_TICK,       // is_timer_active; arc showing time remaining
  MUTED,            // master_mute || media_muted; solid + marker overlays
  IDLE,             // default; reflect led_ring on/off + user color

  // XMOS flash result (one-shots, triggered last so XMOS_FLASH plays first)
  SUCCESS,          // standalone green pulse (XMOS flash success result)
};

// Number of scene slots. SUCCESS is the last enumerator, so its value + 1 is the count —
// computed without modifying the enum.
constexpr size_t SCENE_COUNT = static_cast<size_t>(SceneId::SUCCESS) + 1;

// Controls how the controller resolves RenderCtx.base_brightness from the led_ring entity's
// current brightness each frame.
enum class BrightnessMode : uint8_t {
  USER,     // clamp(led_ring_brightness, 0.2, 1.0)        — no boost
  BOOSTED,  // clamp(led_ring_brightness + 0.1, 0.2, 1.0)  — +10%, floor 0.2
  FIXED,    // Scene::fixed_brightness (ignores user setting)
};

enum class BlendMode : uint8_t { OVER, ADD };

struct Layer {
  std::unique_ptr<Animation> anim;
  BlendMode blend{BlendMode::OVER};
  float alpha{1.0f};

  // When true, the animation renders directly into the composited output buffer instead of an
  // isolated scratch buffer, so it can edit pixels already drawn by earlier layers (and leave
  // the rest untouched). Used by overlay markers (PositionMarkers) that cut into the base — a
  // scratch+blend pass would wipe the base with the marker layer's black pixels. blend/alpha
  // are ignored for in-place layers.
  bool in_place{false};

  // Nullary predicate — nullptr means always enabled. Constructed in
  // scene_library::build(Facts&) by closing over specific Facts fields, e.g.
  //   layer.enabled_pred = [&facts]{ return facts.master_mute; };
  // The compositor calls enabled_pred() with no arguments, keeping the engine Facts-agnostic.
  std::function<bool()> enabled_pred{nullptr};
};

struct Scene {
  SceneId id{SceneId::IDLE};
  uint32_t transition_in_ms{0};

  BrightnessMode brightness_mode{BrightnessMode::USER};
  float fixed_brightness{1.0f};  // only used when brightness_mode == FIXED

  std::vector<Layer> layers;

  bool one_shot{false};

  // Nullary callback — nullptr if not a one-shot. Constructed in scene_library::build(Facts&)
  // by closing over specific Facts fields, e.g.
  //   scene.on_finished = [&facts]{ facts.warning = false; };
  // Called once by the controller after all layers report is_finished(). Signature is void()
  // so the engine has no dependency on the Facts type.
  std::function<void()> on_finished{nullptr};
};

}  // namespace led_ring_controller
}  // namespace esphome
