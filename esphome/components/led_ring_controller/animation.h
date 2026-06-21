#pragma once

#include "frame.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace esphome {
namespace led_ring_controller {

struct RenderCtx {
  uint32_t now_ms;              // millis() at frame start
  float dt;                     // seconds since previous frame
  Pixel base_color;             // from led_ring.current_values (r, g, b normalised 0..1)
  float base_brightness;        // pre-resolved by controller from Scene::BrightnessMode
  bool light_on;                // led_ring on/off state
  float media_volume;           // 0..1
  float timer_ratio;            // 0..1 (seconds_left / total_seconds)
  float xmos_flash_progress;    // 0..1 (updated by event(xmos_flash_progress) actions)
};

class Animation {
 public:
  virtual ~Animation() = default;

  // Called when the owning scene becomes active. Subclasses reset per-instance state here
  // (e.g. pos_, cycle_count_, index_). Default is a no-op.
  virtual void start(const RenderCtx &ctx) {}

  // Render one frame into buffer. buffer is pre-cleared by the compositor before each call.
  virtual void render(FrameBuffer &buffer, const RenderCtx &ctx) = 0;

  // Returns true only for finite one-shots once all cycles have completed.
  // Infinite animations (max_cycles == 0) always return false.
  virtual bool is_finished() const { return false; }
};

// Fills the whole ring with a single colour. This is the milestone stub used to build every
// scene in issues 03-06; the rich primitives (Spin, Pulse, Ripple, Arc, ...) replace most uses
// in issues 08-09.
//
// Three modes:
//   SolidFill()                       — user base colour, respects light_on (off -> black). IDLE.
//   SolidFill(false)                  — user base colour, ignores light_on (always lit).
//   SolidFill(Pixel, duration_ms=0)   — fixed colour; if duration_ms != 0 it is a finite
//                                        one-shot that reports is_finished() once elapsed.
// All modes scale output by RenderCtx.base_brightness.
class SolidFill : public Animation {
 public:
  SolidFill() = default;
  explicit SolidFill(bool respect_light_on) : respect_light_on_(respect_light_on) {}
  explicit SolidFill(Pixel fixed_color, uint32_t duration_ms = 0)
      : use_fixed_(true), fixed_(fixed_color), duration_ms_(duration_ms) {}

  void start(const RenderCtx &ctx) override;
  void render(FrameBuffer &buffer, const RenderCtx &ctx) override;
  bool is_finished() const override { return finished_; }

 protected:
  bool use_fixed_{false};
  bool respect_light_on_{true};
  Pixel fixed_{};
  uint32_t duration_ms_{0};
  uint32_t start_ms_{0};
  bool finished_{false};
};

// Two diametrically opposite blobs rotating around the ring (port of the "Rotating Blob"
// lambda). Each blob is a lead pixel plus `trail_len` trailing pixels at 0.75, 0.50, ...
// brightness. Mic-position LEDs ({0,6,12,18}) are dimmed so the max channel never exceeds
// ~0.5, reducing light bleed into the microphones.
struct RotatingBlobParams {
  float speed;        // LEDs per frame (positive = CW, negative = CCW)
  uint8_t trail_len;  // number of trailing pixels (2 in all current scenes)
};

class RotatingBlob : public Animation {
 public:
  explicit RotatingBlob(RotatingBlobParams params) : params_(params) {}

  void start(const RenderCtx &ctx) override;
  void render(FrameBuffer &buffer, const RenderCtx &ctx) override;

 protected:
  RotatingBlobParams params_;
  float pos_{0.0f};  // persists between frames
};

// Triangle-wave brightness oscillation between min_b and max_b over period_ms. Pulses all
// LEDs, or only `positions` if non-empty (rest black). max_cycles == 0 loops forever; > 0
// makes it a finite one-shot that reports is_finished() after N cycles.
struct PulseParams {
  float min_b;            // minimum brightness multiplier (0..1)
  float max_b;            // maximum brightness multiplier (0..1)
  uint32_t period_ms;     // full oscillation period (ramp up + ramp down)
  uint8_t max_cycles;     // 0 = infinite; > 0 = stop after N complete cycles
  std::vector<uint8_t> positions;  // empty = all LEDs; else only these
};

class Pulse : public Animation {
 public:
  explicit Pulse(PulseParams params) : params_(std::move(params)) {}

  void start(const RenderCtx &ctx) override;
  void render(FrameBuffer &buffer, const RenderCtx &ctx) override;
  bool is_finished() const override { return finished_; }

 protected:
  // Colour the pulse uses each frame. Base class reads ctx.base_color; FixedColorPulse overrides.
  virtual Pixel pulse_color(const RenderCtx &ctx) const { return ctx.base_color; }

  PulseParams params_;
  uint32_t start_ms_{0};
  bool finished_{false};
};

// Pulse with a hard-coded colour (error/warning/success) instead of ctx.base_color.
class FixedColorPulse : public Pulse {
 public:
  FixedColorPulse(Pixel color, PulseParams params) : Pulse(std::move(params)), color_(color) {}

 protected:
  Pixel pulse_color(const RenderCtx & /*ctx*/) const override { return color_; }

  Pixel color_;
};

}  // namespace led_ring_controller
}  // namespace esphome
