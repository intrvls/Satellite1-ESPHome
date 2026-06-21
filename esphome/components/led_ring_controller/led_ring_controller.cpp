#include "led_ring_controller.h"

#include "easing.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <algorithm>

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

void LedRingController::set_flag(LedFlag flag, bool value) {
  switch (flag) {
    case LedFlag::WARNING:
      this->facts_.warning = value;
      break;
    case LedFlag::JACK_PLUGGED:
      this->facts_.jack_plugged = value;
      break;
    case LedFlag::JACK_UNPLUGGED:
      this->facts_.jack_unplugged = value;
      break;
    case LedFlag::VOLUME_BUTTONS_TOUCHED:
      this->facts_.volume_buttons_touched = value;
      break;
    case LedFlag::BTN_ACTION:
      this->facts_.btn_action = value;
      break;
    case LedFlag::INIT_IN_PROGRESS:
      this->facts_.init_in_progress = value;
      break;
    case LedFlag::IMPROV_BLE:
      this->facts_.improv_ble = value;
      break;
    case LedFlag::MASTER_MUTE:
      this->facts_.master_mute = value;
      break;
    case LedFlag::MEDIA_MUTED:
      this->facts_.media_muted = value;
      break;
    case LedFlag::NETWORK_OK:
      this->facts_.network_ok = value;
      break;
    case LedFlag::TIMER_RINGING:
      this->facts_.timer_ringing = value;
      break;
    case LedFlag::IS_TIMER_ACTIVE:
      this->facts_.is_timer_active = value;
      break;
  }
}

void LedRingController::handle_event(LedEvent event, float value) {
  switch (event) {
    case LedEvent::WARNING:
      this->facts_.warning = true;
      break;
    case LedEvent::JACK_PLUGGED:
      this->facts_.jack_plugged = true;
      break;
    case LedEvent::JACK_UNPLUGGED:
      this->facts_.jack_unplugged = true;
      break;
    case LedEvent::XMOS_FLASH_START:
      this->facts_.xmos_flashing_state = XMOS_FLASHING;
      break;
    case LedEvent::XMOS_FLASH_PROGRESS:
      this->facts_.xmos_flash_progress = value / 100.0f;
      break;
    case LedEvent::XMOS_SUCCESS:
      this->facts_.xmos_flashing_state = XMOS_SUCCESS;
      break;
    case LedEvent::XMOS_ERROR:
      this->facts_.xmos_flashing_state = XMOS_ERROR;
      break;
  }
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

}  // namespace led_ring_controller
}  // namespace esphome
