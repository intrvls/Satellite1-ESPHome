#include "led_ring_controller.h"

#include "easing.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include "esp_heap_caps.h"
#include "esp_err.h"

#include <algorithm>
#include <cstring>

namespace esphome {
namespace led_ring_controller {

static const char *const TAG = "led_ring_controller";

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static inline uint8_t to_byte(float v) {
  v = clampf(v, 0.0f, 1.0f);
  return static_cast<uint8_t>(v * 255.0f + 0.5f);
}

void LedRingController::setup() {
  if (this->user_light_ == nullptr) {
    ESP_LOGE(TAG, "user_light not set");
    this->mark_failed();
    return;
  }

  this->work_ = FrameBuffer(this->num_leds_);
  this->prev_ = FrameBuffer(this->num_leds_);
  this->compositor_ = Compositor(this->num_leds_);

  if (!this->init_rmt_()) {
    this->mark_failed();
    return;
  }

  this->library_.build(this->facts_);
  this->facts_.init_in_progress = true;  // seed correct boot state

#ifdef USE_LED_RING_JSON_LOADER
  // Validate the embedded default scene set parses (single source of truth check). Does not
  // replace the compiled scenes — call load_scenes() to install at runtime.
  if (this->default_scenes_json_ != nullptr) {
    SceneFactory factory(this->facts_);
    std::vector<Scene> scenes;
    std::vector<PriorityRule> priority;
    if (factory.load(this->default_scenes_json_, scenes, priority)) {
      ESP_LOGD(TAG, "default_scenes.json OK: %u scenes, %u priority rules",
               static_cast<unsigned>(scenes.size()), static_cast<unsigned>(priority.size()));
    } else {
      ESP_LOGE(TAG, "default_scenes.json failed to parse");
    }
  }
#endif

  // Spin up the dedicated render+transmit task. Pinned to render_core_ so it ticks at a steady
  // cadence regardless of how the main loop (core 0) jitters under WiFi/audio/API load. A 4 KB
  // stack covers the compositor scratch + IDF RMT call path.
  BaseType_t ok = xTaskCreatePinnedToCore(&LedRingController::render_task_trampoline_, "led_render",
                                          4096, this, 2, &this->task_handle_, this->render_core_);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "failed to start render task");
    this->mark_failed();
  }
}

bool LedRingController::init_rmt_() {
  this->tx_buf_size_ = static_cast<size_t>(this->num_leds_) * 3;
  this->tx_buf_ = static_cast<uint8_t *>(heap_caps_malloc(this->tx_buf_size_, MALLOC_CAP_INTERNAL));
  if (this->tx_buf_ == nullptr) {
    ESP_LOGE(TAG, "failed to allocate %u-byte tx buffer", static_cast<unsigned>(this->tx_buf_size_));
    return false;
  }
  memset(this->tx_buf_, 0, this->tx_buf_size_);

  rmt_tx_channel_config_t tx_chan_config = {};
  tx_chan_config.clk_src = RMT_CLK_SRC_DEFAULT;
  tx_chan_config.gpio_num = static_cast<gpio_num_t>(this->pin_);
  tx_chan_config.mem_block_symbols = 64;
  tx_chan_config.resolution_hz = 10 * 1000 * 1000;  // 10 MHz -> 0.1 us per tick
  tx_chan_config.trans_queue_depth = 4;
  esp_err_t err = rmt_new_tx_channel(&tx_chan_config, &this->channel_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "rmt_new_tx_channel failed: %s", esp_err_to_name(err));
    return false;
  }

  // WS2812 bit timings at 0.1 us/tick: 0 = 0.4 us high / 0.8 us low, 1 = 0.8 us high / 0.4 us low.
  // The >=50 us reset latch is covered by the inter-frame gap (>= frame_interval_ms_), so no
  // trailing reset symbol is needed.
  rmt_bytes_encoder_config_t bytes_cfg = {};
  bytes_cfg.bit0.level0 = 1;
  bytes_cfg.bit0.duration0 = 4;
  bytes_cfg.bit0.level1 = 0;
  bytes_cfg.bit0.duration1 = 8;
  bytes_cfg.bit1.level0 = 1;
  bytes_cfg.bit1.duration0 = 8;
  bytes_cfg.bit1.level1 = 0;
  bytes_cfg.bit1.duration1 = 4;
  bytes_cfg.flags.msb_first = 1;
  err = rmt_new_bytes_encoder(&bytes_cfg, &this->encoder_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "rmt_new_bytes_encoder failed: %s", esp_err_to_name(err));
    return false;
  }

  err = rmt_enable(this->channel_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "rmt_enable failed: %s", esp_err_to_name(err));
    return false;
  }
  return true;
}

void LedRingController::render_task_trampoline_(void *arg) {
  static_cast<LedRingController *>(arg)->render_task_();
}

void LedRingController::render_task_() {
  TickType_t last_wake = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(this->frame_interval_ms_);
  uint32_t last_frame_ms = millis();

  for (;;) {
    // Fixed-cadence tick. vTaskDelayUntil absorbs the time spent rendering above, so the wake-up
    // cadence stays locked to frame_interval_ms_ -- the smooth-50fps guarantee.
    vTaskDelayUntil(&last_wake, period);

    uint32_t now_ms = millis();
    float dt = (now_ms - last_frame_ms) / 1000.0f;
    last_frame_ms = now_ms;

#ifdef USE_LED_RING_JSON_LOADER
    // Apply any scene set handed over by load_scenes() on the main loop. Done here so library_ /
    // json_priority_ are only ever mutated by this task -- no lock on the render hot path.
    bool do_install = false;
    std::vector<Scene> scenes;
    std::vector<PriorityRule> priority;
    portENTER_CRITICAL(&this->install_mux_);
    if (this->install_pending_) {
      do_install = true;
      scenes = std::move(this->pending_scenes_);
      priority = std::move(this->pending_priority_);
      this->install_pending_ = false;
    }
    portEXIT_CRITICAL(&this->install_mux_);
    if (do_install) {
      this->library_.install(std::move(scenes));
      this->json_priority_ = std::move(priority);
      this->active_scene_ = SceneId::IDLE;  // force fresh resolve + crossfade next frame
      ESP_LOGI(TAG, "installed %u JSON priority rules",
               static_cast<unsigned>(this->json_priority_.size()));
    }
#endif

    this->render_one_frame_(now_ms, dt);
    this->transmit_frame_(this->work_);
  }
}

void LedRingController::render_one_frame_(uint32_t now_ms, float dt) {
  // BUILD RenderCtx. current_values is owned by the main loop; the few floats read here are
  // word-atomic and a single frame of skew is visually irrelevant.
  auto lv = this->user_light_->current_values;
  Pixel base_color{lv.get_red(), lv.get_green(), lv.get_blue()};

  SceneId next_id = this->resolve_scene_();
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

  // 7. COMMIT. prev_ feeds the next crossfade; the actual transmit happens back in render_task_().
  this->prev_ = this->work_;
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

SceneId LedRingController::resolve_scene_() {
#ifdef USE_LED_RING_JSON_LOADER
  if (!this->json_priority_.empty()) {
    for (auto &rule : this->json_priority_) {
      if (rule.first && rule.first())
        return rule.second;
    }
    return SceneId::IDLE;  // no rule matched (a well-formed table ends with a default)
  }
#endif
  return this->sm_.resolve(this->facts_);
}

bool LedRingController::load_scenes(const std::string &json) {
#ifdef USE_LED_RING_JSON_LOADER
  SceneFactory factory(this->facts_);
  std::vector<Scene> scenes;
  std::vector<PriorityRule> priority;
  if (!factory.load(json, scenes, priority))
    return false;

  // Parsing ran on the caller's thread (main loop). Hand the result to the render task, which
  // performs the actual install at the top of its next frame -- keeps library_ single-writer.
  portENTER_CRITICAL(&this->install_mux_);
  this->pending_scenes_ = std::move(scenes);
  this->pending_priority_ = std::move(priority);
  this->install_pending_ = true;
  portEXIT_CRITICAL(&this->install_mux_);
  return true;
#else
  (void) json;
  ESP_LOGW(TAG, "JSON loader disabled; set enable_json_loader: true to use load_scenes");
  return false;
#endif
}

void LedRingController::transmit_frame_(const FrameBuffer &frame) {
  uint8_t n = std::min<uint8_t>(frame.size(), this->num_leds_);
  for (uint8_t i = 0; i < n; i++) {
    uint8_t rgb[3] = {to_byte(frame[i].r), to_byte(frame[i].g), to_byte(frame[i].b)};
    uint8_t *out = &this->tx_buf_[static_cast<size_t>(i) * 3];
    out[0] = rgb[this->order_[0]];
    out[1] = rgb[this->order_[1]];
    out[2] = rgb[this->order_[2]];
  }

  rmt_transmit_config_t tx_conf = {};
  tx_conf.loop_count = 0;
  esp_err_t err = rmt_transmit(this->channel_, this->encoder_, this->tx_buf_, this->tx_buf_size_, &tx_conf);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "rmt_transmit failed: %s", esp_err_to_name(err));
    return;
  }
  // Block this task (never the main loop) until the frame is on the wire. A 24-LED WS2812 frame is
  // ~720 us; the 100 ms ceiling only guards against a wedged channel.
  rmt_tx_wait_all_done(this->channel_, 100);
}

void LedRingController::dump_config() {
  ESP_LOGCONFIG(TAG, "LedRingController:");
  ESP_LOGCONFIG(TAG, "  LEDs: %u (pin GPIO%u)", this->num_leds_, this->pin_);
  ESP_LOGCONFIG(TAG, "  Render core: %u", this->render_core_);
  ESP_LOGCONFIG(TAG, "  Frame interval: %u ms", this->frame_interval_ms_);
}

}  // namespace led_ring_controller
}  // namespace esphome
