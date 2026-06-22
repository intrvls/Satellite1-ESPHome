#include "animation.h"

#include "esphome/core/helpers.h"

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

  // Add `color` at a fractional ring position, splitting its intensity across the two adjacent
  // LEDs by the fractional distance. This is sub-pixel anti-aliasing: the blob's centre of mass
  // glides continuously even though only 24 physical LEDs exist, so motion reads as smooth rather
  // than snapping one LED at a time. Writes are additive so overlapping splits / both blobs sum.
  auto add_aa = [&](float position, Pixel color) {
    int i0 = static_cast<int>(std::floor(position));
    float frac = position - static_cast<float>(i0);  // 0.0 .. 1.0
    uint8_t a = static_cast<uint8_t>(((i0 % n) + n) % n);
    uint8_t b = static_cast<uint8_t>((((i0 + 1) % n) + n) % n);
    Pixel near = scale(color, 1.0f - frac);
    Pixel far = scale(color, frac);
    buffer[a] = {buffer[a].r + near.r, buffer[a].g + near.g, buffer[a].b + near.b};
    buffer[b] = {buffer[b].r + far.r, buffer[b].g + far.g, buffer[b].b + far.b};
  };

  auto add_blob = [&](float center) {
    add_aa(center, base);  // head at full intensity
    for (uint8_t k = 1; k <= this->params_.trail_len; k++) {
      float factor = 1.0f - 0.25f * static_cast<float>(k);  // 0.75, 0.50, ...
      if (factor < 0.0f)
        factor = 0.0f;
      // Gamma-shape the fade (~2.2) so the trail tapers smoothly in perceived brightness rather
      // than in raw PWM, where the linear steps look lumpy on WS2812s.
      factor = std::pow(factor, 2.2f);
      add_aa(center - static_cast<float>(k), scale(base, factor));
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

  this->pos_ += this->params_.speed * ctx.dt;
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

void ProgressArc::start(const RenderCtx &ctx) { this->start_ms_ = ctx.now_ms; }

void ProgressArc::render(FrameBuffer &buffer, const RenderCtx &ctx) {
  uint8_t n = buffer.size();
  float ratio = this->params_.use_timer_ratio ? ctx.timer_ratio : ctx.media_volume;
  if (ratio < 0.0f)
    ratio = 0.0f;
  if (ratio > 1.0f)
    ratio = 1.0f;
  float fill = ratio * static_cast<float>(n);

  Pixel color = scale(ctx.base_color, ctx.base_brightness);
  for (uint8_t i = 0; i < n; i++) {
    if (static_cast<float>(i) <= fill) {
      float frac = fill - static_cast<float>(i);
      float b = frac < 1.0f ? frac : 1.0f;
      buffer[i] = scale(color, b);
    } else {
      buffer[i] = {0.0f, 0.0f, 0.0f};
    }
  }

  // Backwards-sweeping tick: dim one lit arc pixel to 0.9 as it travels CCW.
  if (this->params_.moving_tick && n > 0) {
    uint32_t step = this->params_.tick_step_ms == 0 ? 1 : this->params_.tick_step_ms;
    uint32_t steps = (ctx.now_ms - this->start_ms_) / step;
    uint8_t tick = static_cast<uint8_t>((n - 1) - (steps % n));  // CCW
    uint8_t last_lit = fill >= 1.0f ? static_cast<uint8_t>(std::ceil(fill)) - 1 : 0;
    if (static_cast<float>(tick) <= fill && tick != last_lit)
      buffer[tick] = scale(buffer[tick], 0.9f);
  }

  if (ratio == 0.0f)
    buffer[0] = this->params_.zero_indicator;
}

void Twinkle::start(const RenderCtx &ctx) {
  this->last_progress_ms_ = ctx.now_ms;
  std::fill(this->data_.begin(), this->data_.end(), 0);
}

void Twinkle::render(FrameBuffer &buffer, const RenderCtx &ctx) {
  uint8_t n = buffer.size();
  if (this->data_.size() != n)
    this->data_.assign(n, 0);

  constexpr uint32_t PROGRESS_INTERVAL = 4;  // ms per phase step (ESPHome default)
  uint8_t pos_add = 0;
  if (ctx.now_ms - this->last_progress_ms_ > PROGRESS_INTERVAL) {
    uint32_t steps = (ctx.now_ms - this->last_progress_ms_) / PROGRESS_INTERVAL;
    pos_add = steps > 255 ? 255 : static_cast<uint8_t>(steps);
    this->last_progress_ms_ += steps * PROGRESS_INTERVAL;
  }

  for (uint8_t i = 0; i < n; i++) {
    uint8_t d = this->data_[i];
    if (d != 0) {
      float level = std::sin(static_cast<float>(M_PI) * static_cast<float>(d) / 255.0f);
      buffer[i] = scale(this->params_.color, level * ctx.base_brightness);
      uint16_t np = static_cast<uint16_t>(d) + pos_add;
      this->data_[i] = np > 255 ? 0 : static_cast<uint8_t>(np);
    } else {
      buffer[i] = {0.0f, 0.0f, 0.0f};
    }
  }

  while (random_float() < this->params_.probability) {
    uint8_t pos = static_cast<uint8_t>(random_uint32() % n);
    if (this->data_[pos] == 0)
      this->data_[pos] = 1;
  }
}

void Ripple::start(const RenderCtx &ctx) {
  this->start_ms_ = ctx.now_ms;
  this->finished_ = false;
}

void Ripple::render(FrameBuffer &buffer, const RenderCtx &ctx) {
  uint8_t n = buffer.size();
  buffer.fill({0.0f, 0.0f, 0.0f});

  constexpr uint32_t STEP_MS = 40;
  uint32_t index = (ctx.now_ms - this->start_ms_) / STEP_MS;
  if (index > 12) {
    this->finished_ = true;
    return;  // ring dark on the final frame
  }

  Pixel color = scale(ctx.base_color, ctx.base_brightness);
  if (this->params_.outward) {
    buffer[index % n] = color;
    buffer[(n - index) % n] = color;
  } else {
    buffer[(12 - index + n) % n] = color;
    buffer[(12 + index) % n] = color;
  }
}

void Sweep::start(const RenderCtx &ctx) {
  this->start_ms_ = ctx.now_ms;
  this->finished_ = false;
}

void Sweep::render(FrameBuffer &buffer, const RenderCtx &ctx) {
  uint8_t n = buffer.size();
  int index;
  if (this->params_.step_interval_ms == 0) {
    index = static_cast<int>(ctx.xmos_flash_progress * static_cast<float>(n));
  } else {
    index = static_cast<int>((ctx.now_ms - this->start_ms_) / this->params_.step_interval_ms);
    if (index > n - 1)
      this->finished_ = true;
  }

  Pixel color = scale(this->params_.color, ctx.base_brightness);
  for (uint8_t i = 0; i < n; i++) {
    bool lit = this->params_.fill_below ? (i <= index) : (i > index);
    buffer[i] = lit ? color : Pixel{0.0f, 0.0f, 0.0f};
  }
}

void PositionMarkers::render(FrameBuffer &buffer, const RenderCtx & /*ctx*/) {
  uint8_t n = buffer.size();
  for (uint8_t p : this->params_.positions) {
    // Blank the guard LEDs before the run.
    for (uint8_t g = 1; g <= this->params_.guard; g++)
      buffer[(p + n - g) % n] = {0.0f, 0.0f, 0.0f};
    // Light the run.
    for (uint8_t r = 0; r < this->params_.run; r++)
      buffer[(p + r) % n] = this->params_.color;
    // Blank the guard LEDs after the run.
    for (uint8_t g = 0; g < this->params_.guard; g++)
      buffer[(p + this->params_.run + g) % n] = {0.0f, 0.0f, 0.0f};
  }
}

}  // namespace led_ring_controller
}  // namespace esphome
