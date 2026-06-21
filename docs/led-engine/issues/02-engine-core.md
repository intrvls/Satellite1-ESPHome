# 02 — Engine core: FrameBuffer, easing, Animation base

**Epic:** [Native LED Ring Animation Engine](../EPIC.md) · **Depends on:** 01

## Goal

Add the light-agnostic engine foundation under `engine/`. Nothing in this directory may
depend on ESPHome light types or `Facts` — it operates solely on `FrameBuffer` and `RenderCtx`.

## Scope

### `engine/frame.h` — `FrameBuffer`

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

  // Write to an ESPHome AddressableLight (GRB WS2812).
  // Converts linear float to uint8: clamp(channel, 0, 1) * 255 + 0.5, cast to uint8_t.
  // Writes raw Color — no additional gamma applied here.
  void write_to_strip(light::AddressableLight& strip) const;

 private:
  std::vector<Pixel> pixels_;
};
```

`num_leds` is passed in from `LedRingController::setup()` via
`strip_->get_addressable()->size()`, making `FrameBuffer` reusable for any strip length.

### `engine/easing.h/.cpp` — easing functions

```cpp
// Type alias for all easing functions. Function pointer (not std::function) —
// all easings are stateless free functions; avoids heap allocation.
using EasingFn = float (*)(float t);  // t in [0, 1] → output in [0, 1]

float linear(float t);
float ease_in_out(float t);   // smoothstep: 3t² - 2t³
float sine(float t);          // 0.5 - 0.5*cos(π*t)
```

### `engine/animation.h/.cpp` — `Animation` base + `RenderCtx`

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

- Compiles with no ESPHome light type includes.
- A throwaway `SolidFill` animation (all pixels = `base_color * base_brightness`) renders into
  a `FrameBuffer` and `crossfade(a, b, 0.5f)` produces the expected midpoint values (verified
  via a temporary `ESP_LOGD` in `setup()`, removed before merge).
- `blend_over` with `alpha=1.0` fully replaces dst pixels with src.
- `write_to_strip` produces `Color(255, 0, 0)` for a pixel `{r=1.0, g=0.0, b=0.0}` (GRB
  swap handled by ESPHome's `ESPColorView`).
