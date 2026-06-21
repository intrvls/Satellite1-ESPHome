#include "compositor.h"

#include <algorithm>

namespace esphome {
namespace led_ring_controller {

void Compositor::render(FrameBuffer &out, const Scene &scene, const RenderCtx &ctx) {
  out.clear();
  for (const auto &layer : scene.layers) {
    if (layer.enabled_pred && !layer.enabled_pred())
      continue;

    // In-place layers edit the composited buffer directly (overlay markers cutting into base).
    if (layer.in_place) {
      layer.anim->render(out, ctx);
      continue;
    }

    this->scratch_.clear();
    layer.anim->render(this->scratch_, ctx);

    if (layer.blend == BlendMode::ADD) {
      uint8_t n = std::min(out.size(), this->scratch_.size());
      for (uint8_t i = 0; i < n; i++) {
        out[i].r = std::min(1.0f, out[i].r + this->scratch_[i].r * layer.alpha);
        out[i].g = std::min(1.0f, out[i].g + this->scratch_[i].g * layer.alpha);
        out[i].b = std::min(1.0f, out[i].b + this->scratch_[i].b * layer.alpha);
      }
    } else {
      out.blend_over(this->scratch_, layer.alpha);
    }
  }
}

}  // namespace led_ring_controller
}  // namespace esphome
