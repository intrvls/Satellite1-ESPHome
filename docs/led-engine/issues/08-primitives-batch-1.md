# 08 — Port primitives batch 1 (solid/blob/pulse) + VA phase scenes

**Epic:** [Native LED Ring Animation Engine](../EPIC.md) · **Depends on:** 06, 07

## Goal

Port the most-used primitives and wire the voice-assistant phase scenes plus alert one-shots.
After this issue, the VA pipeline is fully animated with crossfades.

## Primitives

All primitives live in `animation.h/.cpp` (flat in the component root — see
[EPIC.md](../EPIC.md#architecture)). `RotatingBlob` and `Pulse` take POD `*Params` structs.

> **Implementation notes.** `SolidFill` keeps its issue-06 constructor form (no `SolidFillParams`
> struct): `SolidFill()` for IDLE (base colour, respects `light_on`) and `SolidFill(false)` for
> always-lit fills. `FixedColorPulse` derives from `Pulse` and stores its colour as a member,
> overriding a `pulse_color(ctx)` hook the base class calls each frame (rather than a colour
> field inside `PulseParams`). Mic dimming and trail factors (0.75, 0.50) are ported verbatim.

### `SolidFill`

```cpp
struct SolidFillParams { };  // uses base_color * base_brightness from RenderCtx
```

Fills all pixels uniformly: `pixel = base_color * base_brightness`. When `ctx.light_on ==
false`, outputs black (respects HA on/off state).

### `RotatingBlob`

```cpp
struct RotatingBlobParams {
  float speed;       // LEDs per frame (positive = CW, negative = CCW)
  uint8_t trail_len; // number of trailing pixels (2 in all current scenes)
};
```

Two diametrically opposite blobs rotate around the ring. Exact port of the "Rotating Blob"
lambda — each blob at position `p` and `p+12`:
- Lead pixel: `base_color * base_brightness`
- Trail −1: `base_color * base_brightness * 0.75f`
- Trail −2: `base_color * base_brightness * 0.5f`

**Mic-position dimming (keep from original):** after drawing the blobs, for each mic
position `{0, 6, 12, 18}`, if the pixel's max channel exceeds 128/255, scale it down so
the max channel equals 128/255. This subtly dims LEDs over mic holes to reduce light
bleed into the microphone.

`pos_` (float, persists between frames) is a member updated each `render()`:
`pos_ += params_.speed; if (pos_ >= 24) pos_ -= 24; if (pos_ < 0) pos_ += 24;`

`start()` resets `pos_ = 0.f`.

### `Pulse`

```cpp
struct PulseParams {
  float    min_b;       // minimum brightness multiplier (0..1)
  float    max_b;       // maximum brightness multiplier (0..1)
  uint32_t period_ms;   // full oscillation period (ramp up + ramp down)
  uint8_t  max_cycles;  // 0 = infinite loop; >0 = stop after N complete cycles
  // Optional: restrict pulsing to specific LED positions.
  // Empty = all LEDs pulse. Non-empty = only these positions pulse; rest are black.
  std::vector<uint8_t> positions;
};
```

Brightness oscillates as a triangle wave between `min_b` and `max_b`. One cycle = `period_ms`
ms. `is_finished()` returns true when `max_cycles > 0 && cycle_count_ >= max_cycles`.

Color is `base_color * (brightness_factor * base_brightness)` where `brightness_factor`
is the oscillation value in `[min_b, max_b]`. For fixed-color scenes (error, warning,
success), `base_color` is overridden by the scene's hard-coded color rather than coming
from `RenderCtx` — see the `FixedColorPulse` variant note below.

**`FixedColorPulse`** (same struct, separate subclass for clarity): same as `Pulse` but
`render()` uses a fixed `Color color` field from params instead of `ctx.base_color`.
Used for ERROR (red), WARNING (red), SUCCESS (green), XMOS_SUCCESS (green), XMOS_ERROR (red).

## Scenes

All constructed in `scene_library::build()`. `BrightnessMode` and `fixed_brightness` are set
on the `Scene` struct; the controller resolves `RenderCtx.base_brightness` before
calling the compositor.

### VA Phase scenes

| SceneId | Primitive | BrightnessMode | Params | transition_in_ms |
|---------|-----------|----------------|--------|-----------------|
| `IDLE` | `SolidFill` | `USER` | — | 500 |
| `WAITING` | `RotatingBlob` | `USER` | speed=0.5, trail=2 | 300 |
| `LISTENING` | `RotatingBlob` | `USER` | speed=1.0, trail=2 | 200 |
| `THINKING` | `Pulse` | `USER` | min=0, max=1, period=200ms, cycles=0, positions=[2,14] | 200 |
| `REPLYING` | `RotatingBlob` | `USER` | speed=−1.0, trail=2 | 200 |
| `ERROR` | `FixedColorPulse` | `FIXED` 0.8 | color=red(255,0,0), min=0, max=1, period=200ms, **cycles=0** | 200 |
| `NOT_READY` | — (Twinkle, issue 09) | `FIXED` 0.66 | color=red(255,0,0) | 0 |
| `ACTION_BUTTON` | `SolidFill` | `BOOSTED` | — | 0 |

**`ERROR` is NOT a one-shot.** `va_phase == VA_ERROR` is sustained by the voice assistant
component until the next wake word triggers a phase transition. `max_cycles=0` (infinite).
The 2-second blocking `delay` in the old YAML was a workaround; in the new engine `va_phase`
changes when the VA is ready and the scene automatically re-resolves.

### Alert one-shots

| SceneId | Primitive | BrightnessMode | Params | on_finished |
|---------|-----------|----------------|--------|-------------|
| `WARNING` | `FixedColorPulse` | `FIXED` 0.8 | color=red, min=0, max=1, period=200ms, cycles=5 | `facts_.warning = false` |
| `SUCCESS` | `FixedColorPulse` | `FIXED` 0.8 | color=green(0,255,0), min=0, max=1, period=200ms, cycles=2 | `facts_.xmos_flashing_state = XMOS_IDLE` |
| `XMOS_SUCCESS` | same as SUCCESS | `FIXED` 0.8 | same | `facts_.xmos_flashing_state = XMOS_IDLE` |
| `XMOS_ERROR` | `FixedColorPulse` | `FIXED` 0.8 | color=red, min=0, max=1, period=200ms, cycles=2 | `facts_.xmos_flashing_state = XMOS_IDLE` |

`SUCCESS` and `XMOS_SUCCESS` may share the same `Scene` definition in `scene_library` if
convenient — they have identical visuals and callbacks. `SceneId` values are distinct for
clarity in the priority table.

## Acceptance

- On device, driving `set_phase` through the VA cycle (waiting → listening → thinking →
  replying) reproduces the current spin/blink visuals with crossfades between phases.
- `ERROR` phase renders a sustained red pulse while `va_phase == VA_ERROR`; it stops and
  reverts to idle when `set_phase: 1` is called (no manual delay needed).
- `WARNING` event fires a 5-cycle red pulse and automatically reverts; `facts_.warning`
  is false after the animation ends.
- Mic-position dimming is visible: quadrant LEDs are noticeably dimmer than adjacent ring
  LEDs during the spinning blob.
