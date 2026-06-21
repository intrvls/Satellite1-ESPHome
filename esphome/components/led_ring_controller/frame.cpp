#include "frame.h"

#include <algorithm>

namespace esphome {
namespace led_ring_controller {

void FrameBuffer::clear() {
  for (auto &p : pixels_)
    p = {0.0f, 0.0f, 0.0f};
}

void FrameBuffer::fill(Pixel p) {
  for (auto &px : pixels_)
    px = p;
}

void FrameBuffer::blend_over(const FrameBuffer &src, float alpha) {
  float inv = 1.0f - alpha;
  uint8_t n = std::min(size(), src.size());
  for (uint8_t i = 0; i < n; i++) {
    pixels_[i].r = src[i].r * alpha + pixels_[i].r * inv;
    pixels_[i].g = src[i].g * alpha + pixels_[i].g * inv;
    pixels_[i].b = src[i].b * alpha + pixels_[i].b * inv;
  }
}

FrameBuffer FrameBuffer::crossfade(const FrameBuffer &a, const FrameBuffer &b, float t) {
  float inv = 1.0f - t;
  uint8_t n = std::min(a.size(), b.size());
  FrameBuffer result(n);
  for (uint8_t i = 0; i < n; i++) {
    result[i].r = a[i].r * inv + b[i].r * t;
    result[i].g = a[i].g * inv + b[i].g * t;
    result[i].b = a[i].b * inv + b[i].b * t;
  }
  return result;
}

}  // namespace led_ring_controller
}  // namespace esphome
