import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light
from esphome.const import CONF_ID

CODEOWNERS = ["@futureproofhomes"]

led_ring_controller_ns = cg.esphome_ns.namespace("led_ring_controller")
LedRingController = led_ring_controller_ns.class_("LedRingController", cg.Component)

CONF_STRIP_ID = "strip_id"
CONF_USER_LIGHT_ID = "user_light_id"
CONF_FRAME_INTERVAL = "frame_interval"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(LedRingController),
        cv.Required(CONF_STRIP_ID): cv.use_id(light.AddressableLightState),
        cv.Required(CONF_USER_LIGHT_ID): cv.use_id(light.LightState),
        cv.Optional(CONF_FRAME_INTERVAL, default="20ms"): cv.positive_time_period_milliseconds,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    strip = await cg.get_variable(config[CONF_STRIP_ID])
    cg.add(var.set_strip(strip))

    user_light = await cg.get_variable(config[CONF_USER_LIGHT_ID])
    cg.add(var.set_user_light(user_light))

    cg.add(var.set_frame_interval_ms(config[CONF_FRAME_INTERVAL].total_milliseconds))
