# 09 — Port primitives batch 2 (arc/twinkle/ripple/sweep/markers)

**Epic:** [Native LED Ring Animation Engine](../EPIC.md) · **Depends on:** 08

## Goal

Port the remaining primitives and all overlay markers, completing the effect catalog.

> **Implementation notes.**
> - Param structs use `Pixel` (engine float-RGB), not ESPHome's `Color` — the engine stays
>   light-type-free. The 0-255 colours in this doc map to normalised `Pixel` values.
> - **Overlay markers need a new `Layer::in_place` mode.** The compositor renders each layer
>   into an isolated scratch buffer and blends it over the output, so a marker layer (which
>   leaves most pixels black) would wipe the base. `in_place` layers instead render *directly*
>   into the composited buffer, editing only their own pixels. `PositionMarkers`, the
>   `TIMER_RING`/`TIMER_TICK`/`MUTED` overlays use it.
> - `PositionMarkers` is parameterised as `{positions, run, guard, color}`: blank `guard` LEDs
>   before, light `run` LEDs from each position, blank `guard` after. Mic = `run 1`; speaker =
>   `run 3` (positions shifted to the first lit LED, e.g. `{2,8,14,20}`).
> - The `TIMER_TICK` backwards sweep is implemented **inside `ProgressArc`** (`moving_tick`)
>   rather than as a separate single-pixel `Sweep` overlay — it dims one *lit arc* pixel to 0.9
>   exactly like the original, with no stray dot in the dark region. `Sweep` is therefore used
>   only for `XMOS_FLASH` (progress-driven).

## Primitives

### `ProgressArc`

```cpp
struct ProgressArcParams {
  bool  use_timer_ratio;   // true → reads ctx.timer_ratio; false → reads ctx.media_volume
  bool  reverse;           // true → arc fills from LED 23 downward (future use)
  Color zero_indicator;    // color shown at position 0 when ratio == 0 (set to red for volume)
};
```

Fills LEDs 0..N where N = `ratio * 24`. The last lit LED is partially dimmed based on the
fractional part: `it[last] = color * (ratio - floor(ratio)) * 255`. All others are full
brightness or black.

For `VOLUME` scene: `use_timer_ratio=false`, `zero_indicator=red(255,0,0)` (shown at LED 0
when `media_volume == 0`).

For `TIMER_TICK` scene: `use_timer_ratio=true`, `zero_indicator=Color::BLACK`.

The "tick" sweep indicator in the original "Timer Tick" animation (a slightly-dimmer pixel
that travels backwards around the ring) is implemented via a `Sweep` overlay layer on the
`TIMER_TICK` scene rather than inside `ProgressArc`.

### `Twinkle`

Wraps ESPHome's built-in `addressable_twinkle` effect logic ported to a `render()` call.

```cpp
struct TwinkleParams {
  float probability;  // per-pixel chance of sparkling each frame (0..1)
  Color color;        // fixed color; base_color from RenderCtx not used
};
```

Used for: `IMPROV` (warm-white `{255, 227, 181}`), `INIT` (user-blue `{24, 187, 242}`),
`INIT_NO_NETWORK` — see scenes table; `NO_HA` (red `{255, 0, 0}`), `NOT_READY` (red).

### `Ripple`

```cpp
struct RippleParams {
  bool  outward;   // true = JACK_PLUGGED (LEDs spread from 0 outward to ±12)
                   // false = JACK_UNPLUGGED (LEDs contract from ±12 inward to 0)
  Color color;     // base_color from RenderCtx
};
```

Exact port of the Jack Plugged / Jack Unplugged lambdas. `index_` advances from 0 to 12
over `update_interval=40ms` steps (6 steps/second, so 13 steps ≈ 480ms at native loop speed;
use real-time tracking via `now_ms` instead of frame counting for accuracy).

`is_finished()` returns true when `index_ > 12` (all 13 positions drawn, ring then dark).

Color: `ctx.base_color * ctx.base_brightness` (user color, BrightnessMode `BOOSTED` for both
jack scenes).

### `Sweep`

```cpp
struct SweepParams {
  bool fill_below;           // true = fill LEDs 0..index (Factory Reset, Tick-wave)
                             // false = fill LEDs index+1..23 (XMOS_FLASH, Tick-wave inverse)
  Color color;               // fixed; does not use base_color
  uint32_t step_interval_ms; // 0 = driven by ctx.xmos_flash_progress instead of self-advancing
};
```

When `step_interval_ms == 0`: `index_ = uint8_t(ctx.xmos_flash_progress * 24)` (driven
externally by `event(xmos_flash_progress)` actions from `on_progress_update`).

When `step_interval_ms > 0`: `index_` advances by 1 each `step_interval_ms` milliseconds
(tracked via `last_step_ms_` member). `is_finished()` true when `index_ > 23`.

Used for:
- `XMOS_FLASH`: `fill_below=false`, `color=blue(0,0,255)`, `step_interval_ms=0`
  (progress-driven). Shows LEDs not-yet-flashed as blue; wipes to black as flashing advances.
- Tick overlay on `TIMER_TICK`: `fill_below=false` single-pixel variant with a self-advancing
  step, shown as a slightly-dimmer pixel within the arc. Implemented as a `Sweep`-derived
  single-pixel marker that wraps; kept as a detail of `TIMER_TICK`'s layer composition.

### `PositionMarkers`

```cpp
struct PositionMarkersParams {
  std::vector<uint8_t> positions;   // center LED index for each marker
  uint8_t width;                     // LEDs each side that are blanked (typically 1)
  Color   color;                     // fixed marker color
};
```

For each `center` in `positions`: `it[center] = color`; surrounding `width` LEDs on each
side are set to `Color::BLACK` (cutting into the base layer). Used as overlay layers only.

## Scenes

### Background / state scenes

| SceneId | Layers | BrightnessMode | Notes |
|---------|--------|----------------|-------|
| `IMPROV` | Twinkle(prob=50%, color=warm-white `{255,227,181}`) | `FIXED` 0.66 | transition=0 |
| `INIT` | Twinkle(prob=50%, color=blue `{24,187,242}`) | `FIXED` 0.66 | transition=0 |
| `INIT_NO_NETWORK` | SolidFill, color=warm-white `{255,227,181}` | `FIXED` 0.33 | transition=0 |
| `NO_HA` | Twinkle(prob=50%, color=red `{255,0,0}`) | `FIXED` 0.66 | transition=300ms |
| `NOT_READY` | Twinkle(prob=50%, color=red `{255,0,0}`) | `FIXED` 0.66 | transition=200ms |

### `MUTED` — solid base + conditional overlays

Two layers, both evaluated every frame via `enabled_pred`:

```
Layer 0 (base):   SolidFill, enabled_pred=nullptr (always)
Layer 1 (mic):    PositionMarkers{positions=[0,6,12,18], width=1, color=red(255,0,0)}
                  enabled_pred = [&facts]{ return facts.master_mute; }
Layer 2 (speaker):PositionMarkers{positions=[1,7,13,19], width=0, color=dark-red(200,0,0)}
                  — actually 3 consecutive LEDs starting at each position (s, s+1, s+2)
                  enabled_pred = [&facts]{ return facts.media_muted; }
```

`BrightnessMode::BOOSTED`. The base solid uses `ctx.base_color * ctx.base_brightness`.

### `TIMER_RING` — pulsing full ring + 2-position mute overlay

```
Layer 0 (base): FixedColorPulse — wait, no: uses base_color from RenderCtx (user color)
                Actually a regular Pulse{min=0, max=1, period=200ms, cycles=0, positions=[]}
                with base_color from RenderCtx → BrightnessMode::BOOSTED
Layer 1 (mute): PositionMarkers{positions=[3,9], width=1, color=red(255,0,0)}
                enabled_pred = [&facts]{ return facts.master_mute; }
```

**Note:** Timer Ring uses only **2 mute marker positions** (`[3, 9]`), not the 4-position
mic array used in `MUTED`. This is intentional — the ring is already pulsing and 4 markers
would be visually noisy. The 2 positions correspond to quadrant tops. Do not change to 4.

`BrightnessMode::BOOSTED`. `one_shot=false` (sustained while `timer_ringing` is true).

### `TIMER_TICK` — progress arc + backwards-sweeping tick

```
Layer 0: ProgressArc{use_timer_ratio=true, zero_indicator=BLACK}
Layer 1: Single-pixel backwards sweep (Sweep single-pixel variant, fill_below=false,
         step_interval=20ms, wraps at 24). This adds the "scan line" effect from the
         original — a slightly-dimmer pixel that travels CCW over the arc.
```

`BrightnessMode::BOOSTED`. In the original: `brightness_dip = 0.9f` for the tick pixel vs 1.0 for the arc.

### `VOLUME` — progress arc for media volume

```
Layer 0: ProgressArc{use_timer_ratio=false, zero_indicator=red(255,0,0)}
```

`BrightnessMode::BOOSTED`. No overlay layers.

### Jack one-shots

| SceneId | Primitive | BrightnessMode | on_finished |
|---------|-----------|----------------|-------------|
| `JACK_PLUGGED` | Ripple{outward=true, color=base_color} | `BOOSTED` | `facts_.jack_plugged = false` |
| `JACK_UNPLUGGED` | Ripple{outward=false, color=base_color} | `BOOSTED` | `facts_.jack_unplugged = false` |

Both have `one_shot=true`. The YAML `delay: 800ms + jack_plugged_recently = false` is
**removed** in issue 11 — the engine owns the clear timing.

### `XMOS_FLASH`

```
Layer 0: Sweep{fill_below=false, color=blue(0,0,255), step_interval_ms=0}
```

`BrightnessMode::FIXED` 0.6. Not a one-shot — sustained while `xmos_flashing_state == XMOS_FLASHING`.

## Acceptance

- Each ported scene matches its current visual on device.
- `MUTED` scene shows mic markers (red dots) only when `master_mute=true`, and speaker
  markers only when `media_muted=true` — independently composited.
- Timer Ring mute overlay correctly uses positions `[3,9]` (not `[0,6,12,18]`).
- `JACK_PLUGGED` Ripple auto-clears `jack_plugged` after the animation; no YAML delay needed.
- `XMOS_FLASH` Sweep position tracks `xmos_flash_progress` updates in real time.
