#include "led_ring_controller.h"

#include "easing.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace esphome {
namespace led_ring_controller {

static const char *const TAG = "led_ring_controller";

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static inline uint8_t to_byte(float v) {
  v = clampf(v, 0.0f, 1.0f);
  return static_cast<uint8_t>(v * 255.0f + 0.5f);
}

void LedRingController::setup() {
  if (this->strip_ == nullptr) {
    ESP_LOGE(TAG, "strip not set");
    this->mark_failed();
    return;
  }
  this->strip_out_ = static_cast<light::AddressableLight *>(this->strip_->get_output());

  uint8_t num_leds = static_cast<uint8_t>(this->strip_out_->size());
  this->work_ = FrameBuffer(num_leds);
  this->prev_ = FrameBuffer(num_leds);
  this->compositor_ = Compositor(num_leds);

  this->library_.build(this->facts_);
  this->facts_.init_in_progress = true;  // seed correct boot state
  this->last_frame_ms_ = millis();

  this->run_selftest_();  // temporary — remove before issue 07 merge
}

void LedRingController::loop() {
  // 1. THROTTLE
  uint32_t now_ms = millis();
  uint32_t elapsed = now_ms - this->last_frame_ms_;
  if (elapsed < this->frame_interval_ms_)
    return;
  if (elapsed > this->frame_interval_ms_ + this->frame_interval_ms_ / 2)
    ESP_LOGW(TAG, "frame overrun: %ums elapsed (interval %ums)", elapsed, this->frame_interval_ms_);
  float dt = elapsed / 1000.0f;

  // 2. BUILD RenderCtx
  auto lv = this->user_light_->current_values;
  Pixel base_color{lv.get_red(), lv.get_green(), lv.get_blue()};

  SceneId next_id = this->sm_.resolve(this->facts_);
  const Scene &scene = this->library_.get(next_id);

  float raw_b = lv.get_brightness();
  float base_brightness;
  switch (scene.brightness_mode) {
    case BrightnessMode::USER:
      base_brightness = clampf(raw_b, 0.2f, 1.0f);
      break;
    case BrightnessMode::BOOSTED:
      base_brightness = clampf(raw_b + 0.1f, 0.2f, 1.0f);
      break;
    case BrightnessMode::FIXED:
    default:
      base_brightness = scene.fixed_brightness;
      break;
  }

  RenderCtx ctx{now_ms,           dt,
                base_color,       base_brightness,
                lv.is_on(),       this->facts_.media_volume,
                this->facts_.timer_ratio, this->facts_.xmos_flash_progress};

  // 3. SCENE CHANGE DETECTION
  if (next_id != this->active_scene_) {
    this->transition_.begin(this->prev_, scene.transition_in_ms, ease_in_out);
    for (const auto &layer : scene.layers)
      layer.anim->start(ctx);
    this->active_scene_ = next_id;
  }

  // 4. COMPOSITE
  this->compositor_.render(this->work_, scene, ctx);

  // 5. CROSSFADE (if active)
  if (this->transition_.active())
    this->transition_.apply(this->work_, now_ms);

  // 6. ONE-SHOT CHECK
  if (scene.one_shot) {
    bool all_done = true;
    for (const auto &layer : scene.layers) {
      if (!layer.anim->is_finished()) {
        all_done = false;
        break;
      }
    }
    if (all_done && scene.on_finished)
      scene.on_finished();  // clears the owning fact; resolve() picks another scene next frame
  }

  // 7. COMMIT
  this->prev_ = this->work_;
  this->write_frame_(this->work_);

  // 8.
  this->last_frame_ms_ = now_ms;
}

void LedRingController::write_frame_(const FrameBuffer &frame) {
  auto &strip = *this->strip_out_;
  uint8_t n = std::min<uint8_t>(frame.size(), static_cast<uint8_t>(strip.size()));
  for (uint8_t i = 0; i < n; i++) {
    strip[i].set_rgb(to_byte(frame[i].r), to_byte(frame[i].g), to_byte(frame[i].b));
  }
  strip.schedule_show();
}

void LedRingController::dump_config() {
  ESP_LOGCONFIG(TAG, "LedRingController:");
  ESP_LOGCONFIG(TAG, "  Strip LEDs: %d", this->strip_out_ != nullptr ? this->strip_out_->size() : 0);
  ESP_LOGCONFIG(TAG, "  Frame interval: %u ms", this->frame_interval_ms_);
}

void LedRingController::run_selftest_() {
  // Temporary verification of issues 03/04/05. Remove before issue 07 merge.

  // issue 05 — resolve() priority table.
  auto check = [this](const char *name, const Facts &f, SceneId expect) {
    SceneId got = this->sm_.resolve(f);
    ESP_LOGD(TAG, "resolve %-16s -> %d (expect %d)%s", name, static_cast<int>(got),
             static_cast<int>(expect), got == expect ? "" : "  <-- MISMATCH");
  };
  {
    Facts f;
    f.xmos_flashing_state = XMOS_FLASHING;
    check("xmos_flashing", f, SceneId::XMOS_FLASH);
  }
  {
    Facts f;
    f.init_in_progress = true;
    f.network_ok = false;
    check("init_no_network", f, SceneId::INIT_NO_NETWORK);
  }
  {
    Facts f;
    f.init_in_progress = true;
    f.network_ok = true;
    check("init", f, SceneId::INIT);
  }
  {
    Facts f;
    f.init_in_progress = false;
    f.network_ok = false;
    check("no_ha", f, SceneId::NO_HA);
  }
  {
    Facts f;
    f.init_in_progress = false;
    f.network_ok = true;
    f.warning = true;
    f.va_phase = VA_WAITING;
    check("warning>va", f, SceneId::WARNING);
  }
  {
    Facts f;
    f.init_in_progress = false;
    f.network_ok = true;
    f.va_phase = VA_IDLE;
    check("idle", f, SceneId::IDLE);
  }
  {
    Facts f;
    f.init_in_progress = false;
    f.network_ok = true;
    f.va_phase = VA_IDLE;
    f.master_mute = true;
    check("muted", f, SceneId::MUTED);
  }
  {
    Facts f;
    f.init_in_progress = false;
    f.network_ok = true;
    f.va_phase = VA_IDLE;
    f.is_timer_active = true;
    check("timer_tick", f, SceneId::TIMER_TICK);
  }

  // issue 04 — crossfade midpoint math.
  {
    FrameBuffer a(4), b(4);
    a.fill({1.0f, 0.0f, 0.0f});
    b.fill({0.0f, 0.0f, 1.0f});
    auto mid = FrameBuffer::crossfade(a, b, 0.5f);
    ESP_LOGD(TAG, "crossfade(red,blue,0.5): r=%.2f b=%.2f (expect 0.50 0.50)", mid[0].r, mid[0].b);
  }

  // issue 03 — two-layer composite with a predicate-gated overlay.
  {
    Scene sc;
    Layer base;
    base.anim = std::make_unique<SolidFill>(Pixel{0.0f, 0.0f, 1.0f});
    sc.layers.push_back(std::move(base));

    bool gate = true;
    Layer overlay;
    overlay.anim = std::make_unique<SolidFill>(Pixel{1.0f, 0.0f, 0.0f});
    overlay.enabled_pred = [&gate] { return gate; };
    sc.layers.push_back(std::move(overlay));

    FrameBuffer out(4);
    RenderCtx ctx{millis(), 0.0f, {}, 1.0f, true, 0.0f, 0.0f, 0.0f};

    this->compositor_.render(out, sc, ctx);
    ESP_LOGD(TAG, "2-layer gate=on : r=%.2f b=%.2f (expect 1.00 0.00)", out[0].r, out[0].b);

    gate = false;
    this->compositor_.render(out, sc, ctx);
    ESP_LOGD(TAG, "2-layer gate=off: r=%.2f b=%.2f (expect 0.00 1.00)", out[0].r, out[0].b);
  }
}

}  // namespace led_ring_controller
}  // namespace esphome
