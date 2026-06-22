# Epic: Native LED Ring Animation Engine

## Summary

Replace the YAML-based LED system (`config/common/led_ring.yaml`: ~25 `addressable_lambda`
effects + the `control_leds` priority dispatcher) with a native C++ animation engine that
supports parameterized primitives, easing, **crossfade transitions**, a **layered compositor**
for overlays, and a **priority-resolved state machine** — all in C++.

Scenes start **hardcoded**; the primitive/scene model is data-driven so a future
**markup language** (see [MARKUP_FORMAT.md](MARKUP_FORMAT.md)) can construct scenes and
priority rules without touching engine internals.

## Why

- No transitions today — every state change is an instant cut (`light.turn_on: effect:`).
- Animation logic is duplicated across ~25 lambdas, each with its own `static` state.
- One-shots (warning/jack) block the YAML scheduler with `delay` + flag reset + re-dispatch.
- LEDs are individually addressable but the system can't compose, blend, or transition.
- Future goal: describe animations/states declaratively, eventually pushable at runtime.

## Hardware (resolved)

Active path is the **ESP32 RMT strip on GPIO21** (`esp32_rmt_led_strip` → `hw_led_ring`,
24× WS2812, GRB). The existing native `satellite1/light/led_ring.{h,cpp}` sends frames to the
**XMOS over SPI** — a legacy/alternate board path **not** used by the current config and left
untouched. The new engine drives `hw_led_ring` directly and becomes its **sole writer**.

## Architecture

```
esphome/components/led_ring_controller/
  __init__.py                 # schema, codegen, actions
  led_ring_controller.h/.cpp  # Component: frame-rate loop(), sole strip writer, owns engine
  automation.h                # Action templates (set_phase, set_flag, set_media_volume, event)
  # --- engine module group (light-agnostic; see note below) ---
  frame.h/.cpp                # FrameBuffer: N float-RGB pixels; blend_over(), crossfade()
  easing.h/.cpp               # linear / ease_in_out / sine; EasingFn = float(*)(float)
  animation.h/.cpp            # Animation base + primitive subclasses
  scene.h/.cpp                # SceneId enum, BrightnessMode enum, Layer, Scene structs
  compositor.h/.cpp           # composite a scene's layers into a FrameBuffer (no Facts dep)
  transition.h/.cpp           # TransitionManager: crossfade prev-final-frame -> new scene
  state_machine.h/.cpp        # Facts struct + constexpr phase/xmos constants + resolve()
  scene_library.h/.cpp        # build(Facts&): constructs all scenes; closures capture Facts
```

> **Note on the "engine" grouping.** These files were originally specced under an `engine/`
> subdirectory, but ESPHome's component loader only copies source files from a component's
> immediate directory into the build tree — it does **not** recurse into subdirectories. So all
> engine files live flat in the component root. "engine/" wherever it appears in these docs
> refers to this logical module group (the light-agnostic files), not a real directory; the
> separation is enforced by discipline (no light-type includes in these files), not by path.

### Key architectural constraints

- **`engine/` is `Facts`-agnostic at the type level.** `Layer::enabled_pred` and
  `Scene::on_finished` are **nullary** `std::function<bool()>` / `std::function<void()>` that
  capture specific `Facts` fields by reference at construction time in `scene_library::build()`.
  The compositor's `render()` never takes a `Facts` argument — it just calls `enabled_pred()`.
- **`scene_library::build(Facts& facts)`** is the single wiring point: it constructs every
  scene and closes its predicates and callbacks over the controller's `facts_` member.
- **`BrightnessMode`** on `Scene` (`USER` / `BOOSTED` / `FIXED`) drives how the controller
  resolves `RenderCtx.base_brightness` each frame from `led_ring_->current_values`.

`engine/` is light-agnostic (operates on `FrameBuffer` only) — the seam a markup language
plugs into. Only the controller touches `AddressableLight`.

## Ownership model

Engine is the **sole writer** to `hw_led_ring` (mark `internal: true`). The user-facing
`led_ring` light stays as the Home Assistant control surface (color/brightness/on-off); the
engine reads its `current_values` each frame. This removes the dual-partition
`voice_assistant_leds` workaround entirely.

## Issue breakdown (1 issue = 1 PR)

Dependency order top-to-bottom. Each issue links its own file.

| # | Issue | Depends on |
|---|-------|------------|
| 01 | [Scaffold component + codegen skeleton](issues/01-scaffold-component.md) | — |
| 02 | [Engine core: FrameBuffer, easing, Animation base](issues/02-engine-core.md) | 01 |
| 03 | [Compositor + Scene/Layer model](issues/03-compositor-scene-model.md) | 02 |
| 04 | [TransitionManager (crossfade)](issues/04-transition-manager.md) | 02, 03 |
| 05 | [StateMachine: Facts + table-driven resolve](issues/05-state-machine.md) | 03 |
| 06 | [Controller loop: sole writer, render idle scene](issues/06-controller-loop.md) | 02–05 |
| 07 | [Setter actions API (automation.h + codegen)](issues/07-actions-api.md) | 06 |
| 08 | [Port primitives batch 1 (solid/blob/pulse) + VA phase scenes](issues/08-primitives-batch-1.md) | 06, 07 |
| 09 | [Port primitives batch 2 (arc/twinkle/ripple/sweep/markers)](issues/09-primitives-batch-2.md) | 08 |
| 10 | [Full priority table + one-shot auto-revert](issues/10-priority-table-oneshots.md) | 09 |
| 11 | [YAML migration: rewire triggers, gut led_ring.yaml](issues/11-yaml-migration.md) | 10 |
| 12 | [Markup format: JSON scene descriptors + runtime loader](issues/12-markup-format.md) | 10 |
| 13 | [TTS loudness visualizer: amplitude-reactive ring](issues/13-tts-loudness-visualizer.md) | 11 |
| 14 | [Holistic TTS playout sync: state follows the PCM buffer](issues/14-tts-playout-holistic-sync.md) | 11, 13 |

Issues 01–06 are foundation (no behavior change to shipped firmware until 06 is wired behind a
build flag or variant). 11 is the cutover. 12 is the future-enabling capability and can land
after the cutover.

## Verification (per issue + end-to-end)

- `source scripts/setup_build_env.sh && esphome compile config/satellite1.yaml` after each PR.
- On device (`esphome upload`/`logs`): wake word → waiting/listening/thinking/replying spin,
  timer tick arc, volume buttons arc, mute markers overlay, jack plug/unplug ripple,
  warning one-shot auto-revert, XMOS flash sweep + success/error flash, improv/init/no-HA twinkle.
- Confirm user-facing `led_ring` still controls idle appearance + HA on/off.
- Watch logs for RMT/refresh-rate warnings and free-heap regressions.

## Risks

- **Two writers** — resolved by sole-writer engine + read-only `led_ring`.
- **Gamma/correction** — engine math in linear float, write raw `Color`, let the strip's
  hardware correction apply once. Never double-apply.
- **RMT timing** — 24 LEDs ≈ 0.7ms TX; 50fps safe. Respect `max_refresh_rate`.
- **Momentary flags** (`btn_action`, `volume_buttons_touched`) — YAML caller owns the clear
  timing (press/release and delay respectively); engine does not auto-clear these.
- **One-shot auto-clear** (`warning`, `jack_plugged`, `jack_unplugged`, `xmos_success`,
  `xmos_error`) — engine owns the clear via `Scene::on_finished`.
- **Boot** — `setup_priority HARDWARE`; seed `INIT_NO_NETWORK` scene for a correct first frame.
