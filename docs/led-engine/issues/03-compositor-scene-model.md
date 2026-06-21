# 03 — Compositor + Scene/Layer model

**Epic:** [Native LED Ring Animation Engine](../EPIC.md) · **Depends on:** 02

## Goal

Introduce the full type vocabulary (`SceneId`, `BrightnessMode`, `Layer`, `Scene`) and a
compositor that knows nothing about `Facts` or `AddressableLight`.

## Scope

### `engine/scene.h`

#### `SceneId` enum

All valid scene identifiers. Listed top-to-bottom in rough priority order as a convention hint;
the authoritative order is the table in `state_machine.cpp` (issue 05).

```cpp
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
  WARNING,          // one-shot red pulse ×5, auto-clears facts_.warning
  TIMER_RING,       // timer_ringing; full-ring pulse with 2-position mute overlay

  // Voice assistant phases
  WAITING,          // va_phase == VA_WAITING;   slow CW blob
  LISTENING,        // va_phase == VA_LISTENING; fast CW blob
  THINKING,         // va_phase == VA_THINKING;  2-LED blink at positions 2+14
  REPLYING,         // va_phase == VA_REPLYING;  fast CCW blob
  ERROR,            // va_phase == VA_ERROR;     sustained red pulse (not a one-shot)
  NOT_READY,        // va_phase == VA_NOT_READY; red twinkle

  // Background
  TIMER_TICK,       // is_timer_active; arc showing time remaining
  MUTED,            // master_mute || media_muted; solid + marker overlays
  IDLE,             // default; reflect led_ring on/off + user color

  // XMOS flash result (one-shots, triggered last so XMOS_FLASH plays first)
  SUCCESS,          // standalone green pulse (XMOS flash success result)
};
```

#### `BrightnessMode` enum

Controls how the controller resolves `RenderCtx.base_brightness` from the `led_ring`
entity's current brightness value each frame:

```cpp
enum class BrightnessMode : uint8_t {
  USER,     // clamp(led_ring_brightness, 0.2f, 1.0f)           — no boost
  BOOSTED,  // clamp(led_ring_brightness + 0.1f, 0.2f, 1.0f)   — +10%, floor 0.2
  FIXED,    // Scene::fixed_brightness (ignores user setting)
};
```

#### `Layer` struct

```cpp
enum class BlendMode : uint8_t { OVER, ADD };

struct Layer {
  std::unique_ptr<Animation> anim;
  BlendMode blend{BlendMode::OVER};
  float alpha{1.0f};

  // Nullary predicate — nullptr means always enabled.
  // Constructed in scene_library::build(Facts&) by closing over specific Facts fields:
  //   layer.enabled_pred = [&facts]{ return facts.master_mute; };
  // The compositor calls enabled_pred() with no arguments, keeping engine/ Facts-agnostic.
  std::function<bool()> enabled_pred{nullptr};
};
```

#### `Scene` struct

```cpp
struct Scene {
  SceneId id;
  uint32_t transition_in_ms{0};

  BrightnessMode brightness_mode{BrightnessMode::USER};
  float fixed_brightness{1.0f};  // only used when brightness_mode == FIXED

  std::vector<Layer> layers;

  bool one_shot{false};

  // Nullary callback — nullptr if not a one-shot.
  // Constructed in scene_library::build(Facts&) by closing over specific Facts fields:
  //   scene.on_finished = [&facts]{ facts.warning = false; };
  // Called once by the controller after all layers report is_finished().
  // Signature is void() so engine/scene.h has no dependency on the Facts type.
  std::function<void()> on_finished{nullptr};
};
```

### `engine/scene_library.h/.cpp`

```cpp
class SceneLibrary {
 public:
  // Constructs all scenes and wires enabled_pred / on_finished closures.
  // facts must outlive the SceneLibrary (owned by LedRingController).
  void build(Facts& facts);

  const Scene& get(SceneId id) const;

 private:
  std::array<Scene, /* num scenes */> scenes_;
};
```

`build()` is called once from `LedRingController::setup()`. Closure references into `facts`
are valid for the lifetime of the controller. `facts` must not be moved after `build()`.

> **Implementation ordering.** `build(Facts&)` needs the `Facts` type from issue 05, so
> `scene_library.h/.cpp` is added together with `state_machine` / the controller (issues 05-06)
> rather than strictly within issue 03. Issue 03 delivers the type vocabulary (`scene.h`) and
> the `Compositor`. Every scene in `build()` is wired with the `SolidFill` stub primitive (added
> to `animation.h` in this batch); the rich primitives replace most uses in issues 08-09.

### `engine/compositor.h/.cpp`

```cpp
class Compositor {
 public:
  // Renders scene into out. Uses a scratch buffer per layer.
  // Does not take Facts — layer visibility is resolved via enabled_pred().
  void render(FrameBuffer& out, const Scene& scene, const RenderCtx& ctx);

 private:
  FrameBuffer scratch_{24};
};
```

**Algorithm:**
1. `out.clear()`
2. For each `layer` in `scene.layers`:
   a. If `layer.enabled_pred && !layer.enabled_pred()` → skip.
   b. `scratch_.clear()`, `layer.anim->render(scratch_, ctx)`.
   c. `out.blend_over(scratch_, layer.alpha)` (for `BlendMode::OVER`).

## Acceptance

- A 2-layer scene: solid base (always enabled) + markers overlay gated by a bool captured in
  `enabled_pred` — composites correctly: overlay pixels replace base where enabled, base shows
  elsewhere.
- `Compositor::render()` compiles with no `#include` of `state_machine.h` or `Facts`.
- `SceneLibrary::build(Facts&)` compiles and constructs at least `IDLE` + one other scene.

## Notes

`SceneId::SUCCESS` and `SceneId::XMOS_SUCCESS`/`XMOS_ERROR` are separate: `SUCCESS` is the
standalone "green pulse" that fires after XMOS flash completes; `XMOS_SUCCESS` and `XMOS_ERROR`
are the same effect but triggered via the `xmos_flashing_state` fact path. In practice they
may share the same scene definition — the distinction is in the priority table, not the visuals.
