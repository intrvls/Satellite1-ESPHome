#include "animation.h"

#include <algorithm>
#include <cmath>

namespace esphome {
namespace led_ring_controller {

namespace {

constexpr uint8_t NUM_MIC_POSITIONS = 4;
constexpr uint8_t MIC_POSITIONS[NUM_MIC_POSITIONS] = {0, 6, 12, 18};
constexpr float MIC_MAX_CHANNEL = 128.0f / 255.0f;  // dim mic LEDs so max channel <= ~0.5

inline Pixel scale(Pixel p, float s) { return {p.r * s, p.g * s, p.b * s}; }

}  // namespace

// Remaining primitives (Twinkle, Arc, Ripple, Sweep, markers) are added in issue 09.
// SolidFill is the milestone stub also used as the IDLE / ACTION_BUTTON fill.

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

void RotatingBlob::start(const RenderCtx & /*ctx*/) { this->pos_ = 0.0f; }

void RotatingBlob::render(FrameBuffer &buffer, const RenderCtx &ctx) {
  uint8_t n = buffer.size();
  buffer.fill({0.0f, 0.0f, 0.0f});

  Pixel base = scale(ctx.base_color, ctx.base_brightness);

  auto add_blob = [&](float center) {
    int lead = static_cast<int>(std::floor(center)) % n;
    if (lead < 0)
      lead += n;
    buffer[static_cast<uint8_t>(lead)] = base;
    for (uint8_t k = 1; k <= this->params_.trail_len; k++) {
      float factor = 1.0f - 0.25f * static_cast<float>(k);  // 0.75, 0.50, ...
      if (factor < 0.0f)
        factor = 0.0f;
      int idx = (lead + n - k) % n;
      buffer[static_cast<uint8_t>(idx)] = scale(base, factor);
    }
  };

  add_blob(this->pos_);
  add_blob(this->pos_ + static_cast<float>(n) / 2.0f);

  // Dim LEDs over the mic holes so their brightest channel stays <= ~0.5.
  for (uint8_t i = 0; i < NUM_MIC_POSITIONS; i++) {
    uint8_t mp = MIC_POSITIONS[i];
    if (mp >= n)
      continue;
    Pixel &px = buffer[mp];
    float max_ch = std::max(px.r, std::max(px.g, px.b));
    if (max_ch > MIC_MAX_CHANNEL) {
      float f = MIC_MAX_CHANNEL / max_ch;
      px = scale(px, f);
    }
  }

  this->pos_ += this->params_.speed;
  while (this->pos_ >= static_cast<float>(n))
    this->pos_ -= static_cast<float>(n);
  while (this->pos_ < 0.0f)
    this->pos_ += static_cast<float>(n);
}

void Pulse::start(const RenderCtx &ctx) {
  this->start_ms_ = ctx.now_ms;
  this->finished_ = false;
}

void Pulse::render(FrameBuffer &buffer, const RenderCtx &ctx) {
  uint32_t period = this->params_.period_ms == 0 ? 1 : this->params_.period_ms;
  uint32_t elapsed = ctx.now_ms - this->start_ms_;

  if (this->params_.max_cycles > 0 && (elapsed / period) >= this->params_.max_cycles) {
    this->finished_ = true;
    buffer.fill({0.0f, 0.0f, 0.0f});  // off once the cycles complete
    return;
  }

  // Triangle wave: 0 at cycle start, 1 at midpoint, back to 0.
  float t = static_cast<float>(elapsed % period) / static_cast<float>(period);
  float tri = t < 0.5f ? (t * 2.0f) : (2.0f - 2.0f * t);
  float bf = this->params_.min_b + (this->params_.max_b - this->params_.min_b) * tri;

  Pixel col = scale(this->pulse_color(ctx), bf * ctx.base_brightness);

  if (this->params_.positions.empty()) {
    buffer.fill(col);
  } else {
    buffer.fill({0.0f, 0.0f, 0.0f});
    for (uint8_t idx : this->params_.positions) {
      if (idx < buffer.size())
        buffer[idx] = col;
    }
  }
}

}  // namespace led_ring_controller
}  // namespace esphome
