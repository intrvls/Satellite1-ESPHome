#include "animation.h"

namespace esphome {
namespace led_ring_controller {

// Concrete primitive animations (Spin, Pulse, Ripple, ...) are added in issues 08 and 09.
// SolidFill is the milestone stub used to wire up the scene library and controller loop.

void SolidFill::start(const RenderCtx &ctx) {
  this->start_ms_ = ctx.now_ms;
  this->finished_ = false;
}

void SolidFill::render(FrameBuffer &buffer, const RenderCtx &ctx) {
  if (this->duration_ms_ != 0 && (ctx.now_ms - this->start_ms_) >= this->duration_ms_)
    this->finished_ = true;

  Pixel c;
  if (this->use_fixed_) {
    c = {this->fixed_.r * ctx.base_brightness, this->fixed_.g * ctx.base_brightness,
         this->fixed_.b * ctx.base_brightness};
  } else {
    if (this->respect_light_on_ && !ctx.light_on) {
      buffer.fill({0.0f, 0.0f, 0.0f});
      return;
    }
    c = {ctx.base_color.r * ctx.base_brightness, ctx.base_color.g * ctx.base_brightness,
         ctx.base_color.b * ctx.base_brightness};
  }
  buffer.fill(c);
}

}  // namespace led_ring_controller
}  // namespace esphome
