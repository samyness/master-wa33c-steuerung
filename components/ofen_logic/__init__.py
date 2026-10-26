# Header-only-Logik; die Komponente sorgt nur dafür, dass ESPHome die Header in den Build kopiert.
import esphome.config_validation as cv

CODEOWNERS = []
CONFIG_SCHEMA = cv.Schema({})


async def to_code(config):
    pass
