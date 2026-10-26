# Serielles Protokoll Display <-> NodeMCU (RS485). Transport für beide Seiten.
import esphome.codegen as cg
from esphome.components import uart
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["ofen_logic"]

ofen_link_ns = cg.esphome_ns.namespace("ofen_link")
OfenLink = ofen_link_ns.class_("OfenLink", cg.Component, uart.UARTDevice)

CONFIG_SCHEMA = (
    cv.Schema({cv.GenerateID(): cv.declare_id(OfenLink)})
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
