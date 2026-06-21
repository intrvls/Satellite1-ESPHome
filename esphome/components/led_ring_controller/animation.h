#pragma once

#include "frame.h"

#include <cstdint>

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

}  // namespace led_ring_controller
}  // namespace esphome
