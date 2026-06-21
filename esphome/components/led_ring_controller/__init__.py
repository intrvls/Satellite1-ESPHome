import os

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation, pins
from esphome.components import light
from esphome.const import CONF_ID, CONF_NUM_LEDS, CONF_PIN, CONF_RGB_ORDER, CONF_VALUE

CODEOWNERS = ["@futureproofhomes"]
AUTO_LOAD = ["json"]

led_ring_controller_ns = cg.esphome_ns.namespace("led_ring_controller")
LedRingController = led_ring_controller_ns.class_("LedRingController", cg.Component)

CONF_USER_LIGHT_ID = "user_light_id"
CONF_FRAME_INTERVAL = "frame_interval"
CONF_ENABLE_JSON_LOADER = "enable_json_loader"
CONF_RENDER_CORE = "render_core"

# Source-channel index for each WS2812 output byte position. WS2812 is GRB on the wire.
_CHANNEL_INDEX = {"R": 0, "G": 1, "B": 2}
RGB_ORDERS = ["RGB", "RBG", "GRB", "GBR", "BGR", "BRG"]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(LedRingController),
        cv.Required(CONF_USER_LIGHT_ID): cv.use_id(light.LightState),
        cv.Required(CONF_PIN): pins.internal_gpio_output_pin_number,
        cv.Optional(CONF_NUM_LEDS, default=24): cv.positive_not_null_int,
        cv.Optional(CONF_RGB_ORDER, default="GRB"): cv.one_of(*RGB_ORDERS, upper=True),
        # The render+transmit task is pinned to this core so it never contends with the main
        # loop / WiFi on core 0. Defaults to core 1 (the app's "other" core on the ESP32-S3).
        cv.Optional(CONF_RENDER_CORE, default=1): cv.one_of(0, 1, int=True),
        cv.Optional(CONF_FRAME_INTERVAL, default="20ms"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_ENABLE_JSON_LOADER, default=False): cv.boolean,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    user_light = await cg.get_variable(config[CONF_USER_LIGHT_ID])
    cg.add(var.set_user_light(user_light))

    cg.add(var.set_pin(config[CONF_PIN]))
    cg.add(var.set_num_leds(config[CONF_NUM_LEDS]))
    order = config[CONF_RGB_ORDER]
    cg.add(
        var.set_rgb_order(
            _CHANNEL_INDEX[order[0]], _CHANNEL_INDEX[order[1]], _CHANNEL_INDEX[order[2]]
        )
    )
    cg.add(var.set_render_core(config[CONF_RENDER_CORE]))
    cg.add(var.set_frame_interval_ms(config[CONF_FRAME_INTERVAL].total_milliseconds))

    # The JSON scene parser (~6 KB ArduinoJson) is compiled in only when requested. When enabled,
    # the checked-in default scene set is embedded and validated at boot as the single source of
    # truth, and the led_ring_controller.load_scenes action becomes usable at runtime.
    if config[CONF_ENABLE_JSON_LOADER]:
        cg.add_build_flag("-DUSE_LED_RING_JSON_LOADER")
        json_path = os.path.join(os.path.dirname(__file__), "default_scenes.json")
        with open(json_path, encoding="utf-8") as f:
            default_scenes = f.read()
        cg.add(var.set_default_scenes_json(default_scenes))


# --- Actions -----------------------------------------------------------------

CONF_PHASE = "phase"
CONF_FLAG = "flag"
CONF_VOLUME = "volume"
CONF_RATIO = "ratio"
CONF_EVENT = "event"

LedFlag = led_ring_controller_ns.enum("LedFlag", is_class=True)
LED_FLAGS = {
    "warning": LedFlag.WARNING,
    "jack_plugged": LedFlag.JACK_PLUGGED,
    "jack_unplugged": LedFlag.JACK_UNPLUGGED,
    "volume_buttons_touched": LedFlag.VOLUME_BUTTONS_TOUCHED,
    "btn_action": LedFlag.BTN_ACTION,
    "init_in_progress": LedFlag.INIT_IN_PROGRESS,
    "improv_ble": LedFlag.IMPROV_BLE,
    "master_mute": LedFlag.MASTER_MUTE,
    "media_muted": LedFlag.MEDIA_MUTED,
    "network_ok": LedFlag.NETWORK_OK,
    "timer_ringing": LedFlag.TIMER_RINGING,
    "is_timer_active": LedFlag.IS_TIMER_ACTIVE,
}

LedEvent = led_ring_controller_ns.enum("LedEvent", is_class=True)
LED_EVENTS = {
    "warning": LedEvent.WARNING,
    "jack_plugged": LedEvent.JACK_PLUGGED,
    "jack_unplugged": LedEvent.JACK_UNPLUGGED,
    "xmos_flash_start": LedEvent.XMOS_FLASH_START,
    "xmos_flash_progress": LedEvent.XMOS_FLASH_PROGRESS,
    "xmos_success": LedEvent.XMOS_SUCCESS,
    "xmos_error": LedEvent.XMOS_ERROR,
}

SetPhaseAction = led_ring_controller_ns.class_("SetPhaseAction", automation.Action)
SetFlagAction = led_ring_controller_ns.class_("SetFlagAction", automation.Action)
SetMediaVolumeAction = led_ring_controller_ns.class_("SetMediaVolumeAction", automation.Action)
SetTimerRatioAction = led_ring_controller_ns.class_("SetTimerRatioAction", automation.Action)
EventAction = led_ring_controller_ns.class_("EventAction", automation.Action)
LoadScenesAction = led_ring_controller_ns.class_("LoadScenesAction", automation.Action)

CONF_SCENES = "scenes"

SET_PHASE_SCHEMA = cv.maybe_simple_value(
    {
        cv.GenerateID(): cv.use_id(LedRingController),
        cv.Required(CONF_PHASE): cv.templatable(cv.int_),
    },
    key=CONF_PHASE,
)

SET_FLAG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(LedRingController),
        cv.Required(CONF_FLAG): cv.enum(LED_FLAGS, lower=True),
        cv.Required(CONF_VALUE): cv.templatable(cv.boolean),
    }
)

SET_MEDIA_VOLUME_SCHEMA = cv.maybe_simple_value(
    {
        cv.GenerateID(): cv.use_id(LedRingController),
        cv.Required(CONF_VOLUME): cv.templatable(cv.percentage),
    },
    key=CONF_VOLUME,
)

SET_TIMER_RATIO_SCHEMA = cv.maybe_simple_value(
    {
        cv.GenerateID(): cv.use_id(LedRingController),
        cv.Required(CONF_RATIO): cv.templatable(cv.percentage),
    },
    key=CONF_RATIO,
)

EVENT_SCHEMA = cv.maybe_simple_value(
    {
        cv.GenerateID(): cv.use_id(LedRingController),
        cv.Required(CONF_EVENT): cv.enum(LED_EVENTS, lower=True),
        cv.Optional(CONF_VALUE): cv.templatable(cv.float_),
    },
    key=CONF_EVENT,
)


@automation.register_action("led_ring_controller.set_phase", SetPhaseAction, SET_PHASE_SCHEMA, synchronous=True)
async def set_phase_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    template_ = await cg.templatable(config[CONF_PHASE], args, cg.int_)
    cg.add(var.set_phase(template_))
    return var


@automation.register_action("led_ring_controller.set_flag", SetFlagAction, SET_FLAG_SCHEMA, synchronous=True)
async def set_flag_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    cg.add(var.set_flag(config[CONF_FLAG]))
    template_ = await cg.templatable(config[CONF_VALUE], args, cg.bool_)
    cg.add(var.set_value(template_))
    return var


@automation.register_action(
    "led_ring_controller.set_media_volume", SetMediaVolumeAction, SET_MEDIA_VOLUME_SCHEMA, synchronous=True
)
async def set_media_volume_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    template_ = await cg.templatable(config[CONF_VOLUME], args, cg.float_)
    cg.add(var.set_volume(template_))
    return var


@automation.register_action(
    "led_ring_controller.set_timer_ratio", SetTimerRatioAction, SET_TIMER_RATIO_SCHEMA, synchronous=True
)
async def set_timer_ratio_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    template_ = await cg.templatable(config[CONF_RATIO], args, cg.float_)
    cg.add(var.set_ratio(template_))
    return var


@automation.register_action("led_ring_controller.event", EventAction, EVENT_SCHEMA, synchronous=True)
async def event_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    cg.add(var.set_event(config[CONF_EVENT]))
    if CONF_VALUE in config:
        template_ = await cg.templatable(config[CONF_VALUE], args, cg.float_)
        cg.add(var.set_value(template_))
    return var


LOAD_SCENES_SCHEMA = cv.maybe_simple_value(
    {
        cv.GenerateID(): cv.use_id(LedRingController),
        cv.Required(CONF_SCENES): cv.templatable(cv.string),
    },
    key=CONF_SCENES,
)


@automation.register_action(
    "led_ring_controller.load_scenes", LoadScenesAction, LOAD_SCENES_SCHEMA, synchronous=True
)
async def load_scenes_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    template_ = await cg.templatable(config[CONF_SCENES], args, cg.std_string)
    cg.add(var.set_scenes(template_))
    return var
