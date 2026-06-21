#pragma once

#include "easing.h"
#include "frame.h"

#include <cstdint>

namespace esphome {
namespace led_ring_controller {

class TransitionManager {
 public:
  // Snapshot prev_final as the "from" frame and start a timed crossfade.
  // duration_ms == 0: hard cut — begin() marks the transition immediately inactive.
  // easing: any EasingFn from easing.h (typically ease_in_out).
  void begin(const FrameBuffer &prev_final, uint32_t duration_ms, EasingFn easing);

  bool active() const { return active_; }

  // Blend prev_final_ into new_frame in-place using elapsed time:
  //   result[i] = crossfade(prev_final_[i], new_frame[i], easing_(t)),  t = elapsed/duration
  // Once t >= 1.0, marks itself inactive; subsequent calls are no-ops.
  void apply(FrameBuffer &new_frame, uint32_t now_ms) const;

 private:
  FrameBuffer prev_final_{24};
  uint32_t start_ms_{0};
  uint32_t duration_ms_{0};
  EasingFn easing_{linear};
  mutable bool active_{false};  // mutable: apply() flips it false once the crossfade completes
};

}  // namespace led_ring_controller
}  // namespace esphome
