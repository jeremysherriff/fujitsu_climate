import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate, sensor
from esphome.const import CONF_ID, CONF_TRIGGER_ID
from esphome import automation

CODEOWNERS = []
DEPENDENCIES = ["climate"]

fujitsu_ns = cg.esphome_ns.namespace("fujitsu_ac")
FujitsuClimate = fujitsu_ns.class_("FujitsuClimate", climate.Climate, cg.Component)
FujitsuClimateFrameTrigger = fujitsu_ns.class_(
    "FujitsuClimateFrameTrigger", automation.Trigger.template(cg.std_vector.template(cg.uint8))
)

CONF_SENSOR_ID = "sensor_id"
CONF_ON_FRAME = "on_frame"

ByteVector = cg.std_vector.template(cg.uint8)

CONFIG_SCHEMA = climate.climate_schema(FujitsuClimate).extend({
    cv.Optional(CONF_SENSOR_ID): cv.use_id(sensor.Sensor),
    cv.Optional(CONF_ON_FRAME): automation.validate_automation(
        {
            cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(FujitsuClimateFrameTrigger),
        }
    ),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])

    if sensor_id := config.get(CONF_SENSOR_ID):
        sens = await cg.get_variable(sensor_id)
        cg.add(var.set_room_sensor(sens))

    for on_frame_config in config.get(CONF_ON_FRAME, []):
        trigger = cg.new_Pvariable(on_frame_config[CONF_TRIGGER_ID], var)
        await automation.build_automation(
            trigger,
            [(ByteVector, "x")],
            on_frame_config
        )

    await climate.register_climate(var, config)
    await cg.register_component(var, config)