#pragma once

#include "esphome/core/component.h"
#include "esphome/components/light/light_state.h"

#include "compositor.h"
#include "frame.h"
#include "scene.h"
#include "scene_library.h"
#include "state_machine.h"
#include "transition.h"

#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"

#ifdef USE_LED_RING_JSON_LOADER
#include "scene_factory.h"
#include <utility>
#include <vector>
#endif

namespace esphome {
namespace led_ring_controller {

// Boolean Facts fields settable from YAML via led_ring_controller.set_flag.
enum class LedFlag {
  WARNING,
  JACK_PLUGGED,
  JACK_UNPLUGGED,
  VOLUME_BUTTONS_TOUCHED,
  BTN_ACTION,
  INIT_IN_PROGRESS,
  IMPROV_BLE,
  MASTER_MUTE,
  MEDIA_MUTED,
  NETWORK_OK,
  TIMER_RINGING,
  IS_TIMER_ACTIVE,
};

// Momentary state changes signalled from YAML via led_ring_controller.event.
enum class LedEvent {
  WARNING,
  JACK_PLUGGED,
  JACK_UNPLUGGED,
  XMOS_FLASH_START,
  XMOS_FLASH_PROGRESS,
  XMOS_SUCCESS,
  XMOS_ERROR,
};

class LedRingController : public Component {
 public:
  void set_user_light(light::LightState *user_light) { user_light_ = user_light; }
  void set_frame_interval_ms(uint32_t ms) { frame_interval_ms_ = ms; }
  void set_pin(uint8_t pin) { pin_ = pin; }
  void set_num_leds(uint16_t num_leds) { num_leds_ = static_cast<uint8_t>(num_leds); }
  void set_render_core(uint8_t core) { render_core_ = core; }
  // order[k] = source channel index (0=R,1=G,2=B) emitted at output byte position k.
  void set_rgb_order(uint8_t a, uint8_t b, uint8_t c) {
    this->order_[0] = a;
    this->order_[1] = b;
    this->order_[2] = c;
  }

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  // Fact mutation surface — the actions in automation.h forward to these. The engine
  // re-resolves the active scene every frame, so callers only need to update a field.
  void set_va_phase(int phase) { this->facts_.va_phase = phase; }
  void set_flag(LedFlag flag, bool value);
  void set_media_volume(float volume) {
    this->facts_.media_volume = volume;
    this->facts_.media_muted = (volume == 0.0f);
  }
  void set_timer_ratio(float ratio) { this->facts_.timer_ratio = ratio; }
  // Live output loudness (issue 13). Stashes the raw 0..1 level; the render task envelopes it
  // (fast attack / slow decay) into facts_.audio_level. No media_muted side effect.
  void set_audio_level(float level);
  void set_audio_visualizer_enabled(bool enabled) { this->facts_.audio_visualizer_enabled = enabled; }
  void handle_event(LedEvent event, float value);

#ifdef USE_LED_RING_JSON_LOADER
  void set_default_scenes_json(const char *json) { this->default_scenes_json_ = json; }
#endif
  // Parse and install a JSON scene set at runtime (e.g. from an HA service call). Returns false
  // on any parse/validation error (active scenes unchanged), or if the JSON loader was not
  // enabled at build time (enable_json_loader).
  bool load_scenes(const std::string &json);

 protected:
  // Allocates the RMT TX channel + WS2812 bytes encoder on pin_. Returns false on any IDF error.
  bool init_rmt_();

  // FreeRTOS entry point (pinned to render_core_) -> forwards to render_task_().
  static void render_task_trampoline_(void *arg);
  // The render loop: a fixed-cadence vTaskDelayUntil tick that renders + transmits one frame each
  // iteration, fully independent of ESPHome's cooperative main loop.
  void render_task_();
  // Renders exactly one frame into work_ (scene resolve -> composite -> transition -> one-shot).
  void render_one_frame_(uint32_t now_ms, float dt);
  // Advances the loudness envelope toward the latest input (or silence if input went stale).
  void update_audio_level_(uint32_t now_ms, float dt);

  // Encodes work_ to GRB-ordered bytes and pushes them out the RMT channel, blocking this task
  // (not the main loop) until the transmit completes.
  void transmit_frame_(const FrameBuffer &frame);

  // Selects the active scene. Uses the JSON priority table if one has been installed; otherwise
  // the compiled StateMachine table.
  SceneId resolve_scene_();

  light::LightState *user_light_{nullptr};
  uint32_t frame_interval_ms_{20};

  uint8_t pin_{0};
  uint8_t num_leds_{24};
  uint8_t render_core_{1};
  uint8_t order_[3]{1, 0, 2};  // default GRB

  rmt_channel_handle_t channel_{nullptr};
  rmt_encoder_handle_t encoder_{nullptr};
  uint8_t *tx_buf_{nullptr};  // num_leds_ * 3 bytes, internal RAM (non-DMA)
  size_t tx_buf_size_{0};

  TaskHandle_t task_handle_{nullptr};

  Facts facts_;
  StateMachine sm_;

  // Loudness input handoff: set_audio_level() (main loop) writes these; update_audio_level_()
  // (render task) reads them. Word-atomic, single-frame skew tolerated like the other facts.
  float incoming_audio_level_{0.0f};
  uint32_t incoming_level_ms_{0};
  SceneLibrary library_;
  Compositor compositor_;
  TransitionManager transition_;

  FrameBuffer work_{24};
  FrameBuffer prev_{24};

  SceneId active_scene_{SceneId::IDLE};

#ifdef USE_LED_RING_JSON_LOADER
  const char *default_scenes_json_{nullptr};
  std::vector<PriorityRule> json_priority_;  // empty -> use the compiled StateMachine table

  // Runtime scene-install handoff. load_scenes() parses on the caller's thread (main loop) and
  // stashes the result here under install_mux_; the render task picks it up at the top of a frame
  // and performs the actual library_.install(), so library_/json_priority_ are only ever mutated
  // by the render task -- no lock on the render hot path.
  portMUX_TYPE install_mux_ = portMUX_INITIALIZER_UNLOCKED;
  bool install_pending_{false};
  std::vector<Scene> pending_scenes_;
  std::vector<PriorityRule> pending_priority_;
#endif
};

}  // namespace led_ring_controller
}  // namespace esphome
