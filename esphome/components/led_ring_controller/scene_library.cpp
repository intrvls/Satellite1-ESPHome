#include "scene_library.h"

#include "animation.h"

#include <memory>
#include <utility>

namespace esphome {
namespace led_ring_controller {

namespace {

// Reference colours (linear float 0..1), mirroring the originals from the legacy LED scripts.
constexpr Pixel RED{1.0f, 0.0f, 0.0f};
constexpr Pixel DARK_RED{0.784f, 0.0f, 0.0f};  // 200/255
constexpr Pixel GREEN{0.0f, 1.0f, 0.0f};
constexpr Pixel BLUE{0.0f, 0.0f, 1.0f};
constexpr Pixel WARM_WHITE{1.0f, 0.890f, 0.710f};  // 255,227,181
constexpr Pixel INIT_BLUE{0.094f, 0.733f, 0.949f};  // 24,187,242

void add_layer(Scene &s, std::unique_ptr<Animation> anim) {
  Layer l;
  l.anim = std::move(anim);
  s.layers.push_back(std::move(l));
}

// Adds an in-place overlay layer gated by a predicate (used by marker overlays).
void add_overlay(Scene &s, std::unique_ptr<Animation> anim, std::function<bool()> pred) {
  Layer l;
  l.anim = std::move(anim);
  l.in_place = true;
  l.enabled_pred = std::move(pred);
  s.layers.push_back(std::move(l));
}

}  // namespace

void SceneLibrary::build(Facts &facts) {
  // Scene *structure* (priority slots, brightness modes, one-shot wiring, predicate/callback
  // closures over `facts`) plus the ported primitives from issues 08-09.

  // --- IDLE: user colour, respects light_on (off -> black). ---
  {
    Scene &s = slot(SceneId::IDLE);
    s.transition_in_ms = 500;
    s.brightness_mode = BrightnessMode::USER;
    add_layer(s, std::make_unique<SolidFill>());  // base colour, respect light_on
  }

  // --- Voice assistant phases: rotating blobs + thinking blink. ---
  {
    Scene &s = slot(SceneId::WAITING);  // slow CW spin
    s.transition_in_ms = 300;
    s.brightness_mode = BrightnessMode::USER;
    add_layer(s, std::make_unique<RotatingBlob>(RotatingBlobParams{31.25f, 2}));
  }
  {
    Scene &s = slot(SceneId::LISTENING);  // fast CW spin
    s.transition_in_ms = 200;
    s.brightness_mode = BrightnessMode::USER;
    add_layer(s, std::make_unique<RotatingBlob>(RotatingBlobParams{62.5f, 2}));
  }
  {
    Scene &s = slot(SceneId::REPLYING);  // fast CCW spin
    s.transition_in_ms = 200;
    s.brightness_mode = BrightnessMode::USER;
    add_layer(s, std::make_unique<RotatingBlob>(RotatingBlobParams{-62.5f, 2}));
  }
  {
    Scene &s = slot(SceneId::THINKING);  // blink at positions 2 + 14
    s.transition_in_ms = 200;
    s.brightness_mode = BrightnessMode::USER;
    add_layer(s, std::make_unique<Pulse>(PulseParams{0.0f, 1.0f, 200, 0, {2, 14}}));
  }

  // --- ERROR: sustained red pulse (NOT a one-shot; kept while va_phase == VA_ERROR). ---
  {
    Scene &s = slot(SceneId::ERROR);
    s.transition_in_ms = 200;
    s.brightness_mode = BrightnessMode::FIXED;
    s.fixed_brightness = 0.8f;
    add_layer(s, std::make_unique<FixedColorPulse>(RED, PulseParams{0.0f, 1.0f, 200, 0, {}}));
  }

  // --- NOT_READY / NO_HA: red twinkle, fixed brightness. ---
  {
    Scene &s = slot(SceneId::NOT_READY);
    s.transition_in_ms = 200;
    s.brightness_mode = BrightnessMode::FIXED;
    s.fixed_brightness = 0.66f;
    add_layer(s, std::make_unique<Twinkle>(TwinkleParams{0.5f, RED}));
  }
  {
    Scene &s = slot(SceneId::NO_HA);
    s.transition_in_ms = 300;
    s.brightness_mode = BrightnessMode::FIXED;
    s.fixed_brightness = 0.66f;
    add_layer(s, std::make_unique<Twinkle>(TwinkleParams{0.5f, RED}));
  }

  // --- Onboarding / connectivity. ---
  {
    Scene &s = slot(SceneId::IMPROV);  // warm-white twinkle
    s.brightness_mode = BrightnessMode::FIXED;
    s.fixed_brightness = 0.66f;
    add_layer(s, std::make_unique<Twinkle>(TwinkleParams{0.5f, WARM_WHITE}));
  }
  {
    Scene &s = slot(SceneId::INIT);  // blue twinkle
    s.brightness_mode = BrightnessMode::FIXED;
    s.fixed_brightness = 0.66f;
    add_layer(s, std::make_unique<Twinkle>(TwinkleParams{0.5f, INIT_BLUE}));
  }
  {
    Scene &s = slot(SceneId::INIT_NO_NETWORK);  // solid warm white
    s.brightness_mode = BrightnessMode::FIXED;
    s.fixed_brightness = 0.33f;
    add_layer(s, std::make_unique<SolidFill>(WARM_WHITE));
  }

  // --- VOLUME: media-volume progress arc, red zero indicator. ---
  {
    Scene &s = slot(SceneId::VOLUME);
    s.brightness_mode = BrightnessMode::BOOSTED;
    add_layer(s, std::make_unique<ProgressArc>(ProgressArcParams{false, false, RED}));
  }
  {
    Scene &s = slot(SceneId::ACTION_BUTTON);  // solid full-ring flash
    s.brightness_mode = BrightnessMode::BOOSTED;
    add_layer(s, std::make_unique<SolidFill>(false));
  }

  // --- Jack events: one-shot ripples; engine clears the owning fact via on_finished. ---
  {
    Scene &s = slot(SceneId::JACK_PLUGGED);  // outward ripple
    s.brightness_mode = BrightnessMode::BOOSTED;
    s.one_shot = true;
    add_layer(s, std::make_unique<Ripple>(RippleParams{true}));
    s.on_finished = [&facts] { facts.jack_plugged = false; };
  }
  {
    Scene &s = slot(SceneId::JACK_UNPLUGGED);  // inward ripple
    s.brightness_mode = BrightnessMode::BOOSTED;
    s.one_shot = true;
    add_layer(s, std::make_unique<Ripple>(RippleParams{false}));
    s.on_finished = [&facts] { facts.jack_unplugged = false; };
  }

  // --- WARNING: one-shot 5-cycle red pulse; engine clears facts.warning via on_finished. ---
  {
    Scene &s = slot(SceneId::WARNING);
    s.transition_in_ms = 100;
    s.brightness_mode = BrightnessMode::FIXED;
    s.fixed_brightness = 0.8f;
    s.one_shot = true;
    add_layer(s, std::make_unique<FixedColorPulse>(RED, PulseParams{0.0f, 1.0f, 200, 5, {}}));
    s.on_finished = [&facts] { facts.warning = false; };
  }

  // --- TIMER_RING: pulsing full ring + 2-position mute overlay (positions [3,9], NOT 4). ---
  {
    Scene &s = slot(SceneId::TIMER_RING);
    s.brightness_mode = BrightnessMode::BOOSTED;
    add_layer(s, std::make_unique<Pulse>(PulseParams{0.0f, 1.0f, 200, 0, {}}));
    add_overlay(s, std::make_unique<PositionMarkers>(PositionMarkersParams{{3, 9}, 1, 1, RED}),
                [&facts] { return facts.master_mute; });
  }

  // --- TIMER_TICK: time-remaining progress arc + backwards-sweeping tick. ---
  {
    Scene &s = slot(SceneId::TIMER_TICK);
    s.brightness_mode = BrightnessMode::BOOSTED;
    add_layer(s, std::make_unique<ProgressArc>(
                     ProgressArcParams{true, false, {0.0f, 0.0f, 0.0f}, true, 100}));
    // Mute markers at quadrant tops while master_mute is set.
    add_overlay(s, std::make_unique<PositionMarkers>(PositionMarkersParams{{3, 9}, 1, 1, RED}),
                [&facts] { return facts.master_mute; });
  }

  // --- MUTED: solid base + independently gated mic/speaker marker overlays. ---
  {
    Scene &s = slot(SceneId::MUTED);
    s.brightness_mode = BrightnessMode::BOOSTED;
    add_layer(s, std::make_unique<SolidFill>(false));
    // Mic markers: single red LED at each {0,6,12,18}, blanked either side.
    add_overlay(s, std::make_unique<PositionMarkers>(PositionMarkersParams{{0, 6, 12, 18}, 1, 1, RED}),
                [&facts] { return facts.master_mute; });
    // Speaker markers: 3 dark-red LEDs starting at {2,8,14,20}, blanked either side.
    add_overlay(s,
                std::make_unique<PositionMarkers>(PositionMarkersParams{{2, 8, 14, 20}, 3, 1, DARK_RED}),
                [&facts] { return facts.media_muted; });
  }

  // --- XMOS flashing pipeline. ---
  {
    Scene &s = slot(SceneId::XMOS_FLASH);  // blue sweep wiping as progress advances
    s.brightness_mode = BrightnessMode::FIXED;
    s.fixed_brightness = 0.6f;
    add_layer(s, std::make_unique<Sweep>(SweepParams{false, BLUE, 0}));
  }
  {
    Scene &s = slot(SceneId::XMOS_SUCCESS);  // 2-cycle green pulse
    s.brightness_mode = BrightnessMode::FIXED;
    s.fixed_brightness = 0.8f;
    s.one_shot = true;
    add_layer(s, std::make_unique<FixedColorPulse>(GREEN, PulseParams{0.0f, 1.0f, 200, 2, {}}));
    s.on_finished = [&facts] { facts.xmos_flashing_state = XMOS_IDLE; };
  }
  {
    Scene &s = slot(SceneId::XMOS_ERROR);  // 2-cycle red pulse
    s.brightness_mode = BrightnessMode::FIXED;
    s.fixed_brightness = 0.8f;
    s.one_shot = true;
    add_layer(s, std::make_unique<FixedColorPulse>(RED, PulseParams{0.0f, 1.0f, 200, 2, {}}));
    s.on_finished = [&facts] { facts.xmos_flashing_state = XMOS_IDLE; };
  }

  // --- SUCCESS: standalone 2-cycle green pulse (not reachable via resolve(); triggered
  // elsewhere). Identical visuals to XMOS_SUCCESS. ---
  {
    Scene &s = slot(SceneId::SUCCESS);
    s.brightness_mode = BrightnessMode::FIXED;
    s.fixed_brightness = 0.8f;
    s.one_shot = true;
    add_layer(s, std::make_unique<FixedColorPulse>(GREEN, PulseParams{0.0f, 1.0f, 200, 2, {}}));
    s.on_finished = [&facts] { facts.xmos_flashing_state = XMOS_IDLE; };
  }
}

}  // namespace led_ring_controller
}  // namespace esphome
