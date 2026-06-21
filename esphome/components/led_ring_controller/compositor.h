#pragma once

#include "animation.h"
#include "frame.h"
#include "scene.h"

#include <cstdint>

namespace esphome {
namespace led_ring_controller {

class Compositor {
 public:
  Compositor() = default;
  explicit Compositor(uint8_t num_leds) : scratch_(num_leds) {}

  // Renders scene into out. Uses an internal scratch buffer per layer. Does not take Facts —
  // layer visibility is resolved via Layer::enabled_pred().
  void render(FrameBuffer &out, const Scene &scene, const RenderCtx &ctx);

 private:
  FrameBuffer scratch_{24};
};

}  // namespace led_ring_controller
}  // namespace esphome
