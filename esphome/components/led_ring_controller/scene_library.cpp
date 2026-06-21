#include "scene_library.h"

#include "animation.h"

#include <memory>
#include <utility>

namespace esphome {
namespace led_ring_controller {

namespace {

// Reference colours (linear float 0..1), mirroring the originals in led_ring.yaml / control_leds.
constexpr Pixel RED{1.0f, 0.0f, 0.0f};
constexpr Pixel GREEN{0.0f, 1.0f, 0.0f};
constexpr Pixel BLUE{0.0f, 0.0f, 1.0f};
constexpr Pixel WARM_WHITE{1.0f, 0.89f, 0.71f};
constexpr Pixel INIT_BLUE{0.094f, 0.733f, 0.949f};

void add_layer(Scene &s, std::unique_ptr<Animation> anim) {
  Layer l;
  l.anim = std::move(anim);
  s.layers.push_back(std::move(l));
}

}  // namespace

void SceneLibrary::build(Facts &facts) {
  // NOTE: Every scene below currently uses the SolidFill stub primitive. The rich visuals
  // (spins, pulses, ripples, arcs, marker overlays) replace most of these in issues 08-10.
  // What is final here is the scene *structure*: priority slots, brightness modes, one-shot
  // wiring, and the predicate/callback closures over `facts`.

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
    add_layer(s, std::make_unique<RotatingBlob>(RotatingBlobParams{0.5f, 2}));
  }
  {
    Scene &s = slot(SceneId::LISTENING);  // fast CW spin
    s.transition_in_ms = 200;
    s.brightness_mode = BrightnessMode::USER;
    add_layer(s, std::make_unique<RotatingBlob>(RotatingBlobParams{1.0f, 2}));
  }
  {
    Scene &s = slot(SceneId::REPLYING);  // fast CCW spin
    s.transition_in_ms = 200;
    s.brightness_mode = BrightnessMode::USER;
    add_layer(s, std::make_unique<RotatingBlob>(RotatingBlobParams{-1.0f, 2}));
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

  // --- NOT_READY / NO_HA: red, fixed brightness. NOT_READY -> Twinkle in issue 09. ---
  {
    Scene &s = slot(SceneId::NOT_READY);
    s.brightness_mode = BrightnessMode::FIXED;
    s.fixed_brightness = 0.66f;
    add_layer(s, std::make_unique<SolidFill>(RED));
  }
  {
    Scene &s = slot(SceneId::NO_HA);
    s.transition_in_ms = 400;
    s.brightness_mode = BrightnessMode::FIXED;
    s.fixed_brightness = 0.66f;
    add_layer(s, std::make_unique<SolidFill>(RED));
  }

  // --- Onboarding / connectivity. ---
  {
    Scene &s = slot(SceneId::IMPROV);
    s.brightness_mode = BrightnessMode::FIXED;
    s.fixed_brightness = 0.66f;
    add_layer(s, std::make_unique<SolidFill>(WARM_WHITE));
  }
  {
    Scene &s = slot(SceneId::INIT);
    s.brightness_mode = BrightnessMode::FIXED;
    s.fixed_brightness = 0.66f;
    add_layer(s, std::make_unique<SolidFill>(INIT_BLUE));
  }
  {
    Scene &s = slot(SceneId::INIT_NO_NETWORK);
    s.brightness_mode = BrightnessMode::FIXED;
    s.fixed_brightness = 0.33f;
    add_layer(s, std::make_unique<SolidFill>(WARM_WHITE));
  }

  // --- Transient user interactions (base colour). ---
  {
    Scene &s = slot(SceneId::VOLUME);
    s.brightness_mode = BrightnessMode::BOOSTED;
    add_layer(s, std::make_unique<SolidFill>(false));
  }
  {
    Scene &s = slot(SceneId::ACTION_BUTTON);
    s.brightness_mode = BrightnessMode::BOOSTED;
    add_layer(s, std::make_unique<SolidFill>(false));
  }

  // --- Jack events: one-shot ripples; engine clears the owning fact via on_finished. ---
  {
    Scene &s = slot(SceneId::JACK_PLUGGED);
    s.brightness_mode = BrightnessMode::USER;
    s.one_shot = true;
    add_layer(s, std::make_unique<SolidFill>(BLUE, 800));
    s.on_finished = [&facts] { facts.jack_plugged = false; };
  }
  {
    Scene &s = slot(SceneId::JACK_UNPLUGGED);
    s.brightness_mode = BrightnessMode::BOOSTED;
    s.one_shot = true;
    add_layer(s, std::make_unique<SolidFill>(BLUE, 800));
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

  // --- Timer scenes (base colour). MUTED markers (issue 10) replace the stub overlay below. ---
  {
    Scene &s = slot(SceneId::TIMER_RING);
    s.brightness_mode = BrightnessMode::BOOSTED;
    add_layer(s, std::make_unique<SolidFill>(false));
  }
  {
    Scene &s = slot(SceneId::TIMER_TICK);
    s.brightness_mode = BrightnessMode::BOOSTED;
    add_layer(s, std::make_unique<SolidFill>(false));
  }

  // --- MUTED: two-layer scene demonstrating a predicate-gated overlay. ---
  // Layer 0: base colour. Layer 1: red marker overlay, only when master_mute is set.
  {
    Scene &s = slot(SceneId::MUTED);
    s.brightness_mode = BrightnessMode::BOOSTED;
    add_layer(s, std::make_unique<SolidFill>(false));

    Layer marker;
    marker.anim = std::make_unique<SolidFill>(RED);
    marker.alpha = 1.0f;
    marker.enabled_pred = [&facts] { return facts.master_mute; };
    s.layers.push_back(std::move(marker));
  }

  // --- XMOS flashing pipeline. ---
  {
    Scene &s = slot(SceneId::XMOS_FLASH);
    s.brightness_mode = BrightnessMode::FIXED;
    s.fixed_brightness = 0.6f;
    add_layer(s, std::make_unique<SolidFill>(BLUE));
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
