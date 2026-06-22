#include "state_machine.h"

namespace esphome {
namespace led_ring_controller {

SceneId StateMachine::resolve(const Facts &f) const {
  // Table evaluated top-to-bottom; first match wins.
  if (f.xmos_flashing_state == XMOS_FLASHING)
    return SceneId::XMOS_FLASH;
  if (f.xmos_flashing_state == XMOS_SUCCESS)
    return SceneId::XMOS_SUCCESS;
  if (f.xmos_flashing_state == XMOS_ERROR)
    return SceneId::XMOS_ERROR;
  if (f.improv_ble)
    return SceneId::IMPROV;
  if (f.init_in_progress && f.network_ok)
    return SceneId::INIT;
  if (f.init_in_progress && !f.network_ok)
    return SceneId::INIT_NO_NETWORK;
  if (!f.network_ok)
    return SceneId::NO_HA;
  if (f.volume_buttons_touched)
    return SceneId::VOLUME;
  if (f.btn_action)
    return SceneId::ACTION_BUTTON;
  if (f.jack_plugged)
    return SceneId::JACK_PLUGGED;
  if (f.jack_unplugged)
    return SceneId::JACK_UNPLUGGED;
  if (f.warning)
    return SceneId::WARNING;
  if (f.timer_ringing)
    return SceneId::TIMER_RING;
  if (f.va_phase == VA_WAITING)
    return SceneId::WAITING;
  if (f.va_phase == VA_LISTENING)
    return SceneId::LISTENING;
  if (f.va_phase == VA_THINKING)
    return SceneId::THINKING;
  // Loudness glow sits just above REPLYING: a TTS reply is visualized by actual output amplitude
  // and stays lit until the PCM drains, regardless of when the `replying` phase clears (issue 14).
  // Checked after THINKING so the thinking-chime doesn't override the thinking blink.
  if (f.audio_visualizer_enabled && f.audio_level > 0.02f)
    return SceneId::LOUDNESS;
  if (f.va_phase == VA_REPLYING)
    return SceneId::REPLYING;
  if (f.va_phase == VA_ERROR)
    return SceneId::ERROR;
  if (f.va_phase == VA_NOT_READY)
    return SceneId::NOT_READY;
  if (f.is_timer_active)
    return SceneId::TIMER_TICK;
  if (f.master_mute || f.media_muted)
    return SceneId::MUTED;
  return SceneId::IDLE;
}

}  // namespace led_ring_controller
}  // namespace esphome
