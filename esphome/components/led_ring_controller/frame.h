#pragma once

#include <cstdint>
#include <vector>

namespace esphome {
namespace led_ring_controller {

struct Pixel {
  float r{0.0f};
  float g{0.0f};
  float b{0.0f};
};

class FrameBuffer {
 public:
  FrameBuffer() = default;
  explicit FrameBuffer(uint8_t num_leds) : pixels_(num_leds) {}

  void clear();
  void fill(Pixel p);

  Pixel &operator[](uint8_t i) { return pixels_[i]; }
  const Pixel &operator[](uint8_t i) const { return pixels_[i]; }
  uint8_t size() const { return static_cast<uint8_t>(pixels_.size()); }

  // Alpha-composite src over this buffer in place: dst = src*alpha + dst*(1-alpha)
  void blend_over(const FrameBuffer &src, float alpha);

  // Return a new buffer linearly interpolated between a and b: result = a*(1-t) + b*t
  static FrameBuffer crossfade(const FrameBuffer &a, const FrameBuffer &b, float t);

 private:
  std::vector<Pixel> pixels_;
};

}  // namespace led_ring_controller
}  // namespace esphome
