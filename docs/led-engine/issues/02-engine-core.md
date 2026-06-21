# 02 — Engine core: FrameBuffer, easing, Animation base

**Epic:** [Native LED Ring Animation Engine](../EPIC.md) · **Depends on:** 01

## Goal

Add the light-agnostic engine foundation. Nothing in the engine module group may depend on
ESPHome light types or `Facts` — it operates solely on `FrameBuffer` and `RenderCtx`.

> The engine files live flat in the component root (`frame.h`, `easing.h`, …), not under an
> `engine/` subdirectory — ESPHome's loader does not recurse into component subdirectories.
> See the note in [EPIC.md](../EPIC.md#architecture). Light-agnosticism is enforced by
> discipline (no light-type includes), not by directory.

## Scope

### `frame.h` — `FrameBuffer`

```cpp
struct Pixel { float r, g, b; };  // linear float, 0..1 per channel

class FrameBuffer {
 public:
  explicit FrameBuffer(uint8_t num_leds);

  void clear();                          // set all pixels to {0,0,0}
  void fill(Pixel p);
  Pixel& operator[](uint8_t i);
  const Pixel& operator[](uint8_t i) const;
  uint8_t size() const;

  // Alpha-composite src over this buffer: dst = src*alpha + dst*(1-alpha)
  void blend_over(const FrameBuffer& src, float alpha);

  // Return a new buffer that is a linear blend: a*(1-t) + b*t
  static FrameBuffer crossfade(const FrameBuffer& a, const FrameBuffer& b, float t);

 private:
  std::vector<Pixel> pixels_;
};
```

`num_leds` is passed in from `LedRingController::setup()` via the strip's
`AddressableLight::size()` (reached through `strip_->get_output()` cast to `AddressableLight*`),
making `FrameBuffer` reusable for any strip length.

> **`write_to_strip` is intentionally *not* on `FrameBuffer`.** Writing pixels to an
> `AddressableLight` would pull an ESPHome light type into the engine, violating the
> light-agnostic constraint above. The float-RGB → `AddressableLight` conversion (GRB WS2812,
> `clamp(channel,0,1)*255+0.5`) lives in `LedRingController` instead — added in issue 06, the
> only place that touches the strip.

### `easing.h/.cpp` — easing functions

```cpp
// Type alias for all easing functions. Function pointer (not std::function) —
// all easings are stateless free functions; avoids heap allocation.
using EasingFn = float (*)(float t);  // t in [0, 1] → output in [0, 1]

float linear(float t);
float ease_in_out(float t);   // smoothstep: 3t² - 2t³
float sine(float t);          // 0.5 - 0.5*cos(π*t)
```

### `animation.h/.cpp` — `Animation` base + `RenderCtx`

```cpp
struct RenderCtx {
  uint32_t now_ms;         // millis() at frame start
  float    dt;             // seconds since previous frame
  Pixel    base_color;     // from led_ring.current_values (r,g,b normalised 0..1)
  float    base_brightness;// pre-resolved by controller from BrightnessMode (see issue 06)
  bool     light_on;       // led_ring on/off state
  float    media_volume;   // 0..1 (from Facts.media_volume)
  float    timer_ratio;    // 0..1 (seconds_left / total_seconds, from Facts.timer_ratio)
  float    xmos_flash_progress; // 0..1 (from Facts.xmos_flash_progress)
};

class Animation {
 public:
  virtual ~Animation() = default;
  virtual void start(const RenderCtx&) {}          // called on scene activation
  virtual void render(FrameBuffer&, const RenderCtx&) = 0;
  virtual bool is_finished() const { return false; } // true only for finite one-shots
};
```

**Why `media_volume`, `timer_ratio`, and `xmos_flash_progress` are in `RenderCtx` but not
other facts:** these three are read by animations every frame to compute pixel values directly.
All other facts gate layer visibility (via `enabled_pred`) or scene selection (via the priority
table) — they don't enter rendering math. Keeping only render-relevant values in `RenderCtx`
keeps the engine free of the full `Facts` type.

## Acceptance

- Engine files (`frame`, `easing`, `animation`) compile with no ESPHome light type includes.
- `crossfade(a, b, 0.5f)` produces the expected midpoint values and `blend_over(src, 0.5f)`
  composites correctly (verified via a temporary `ESP_LOGD` in `LedRingController::setup()`,
  removed before issue 06 merge).
- `blend_over` with `alpha=1.0` fully replaces dst pixels with src.
- (Strip write-out — float-RGB → `Color`, GRB WS2812 — is verified in issue 06, where it
  lives. It is deliberately absent from the engine here.)
