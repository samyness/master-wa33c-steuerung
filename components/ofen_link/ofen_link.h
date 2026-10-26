#pragma once
// Empfängt und sendet Protokollzeilen über die UART. Die Auswertung macht die jeweilige Seite
// (ofen_controller.h auf dem NodeMCU, Lambdas im Display-YAML).
#include <deque>
#include <string>

#include "esphome/components/ofen_logic/ofen_logic.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace ofen_link {

class OfenLink : public Component, public uart::UARTDevice {
 public:
  void loop() override {
    uint8_t c;
    while (this->available() && this->read_byte(&c)) {
      if (!this->buf_.feed((char) c)) continue;
      ofen::Fields f;
      if (!ofen::parse_line(this->buf_.line(), f)) {
        this->invalid++;
        continue;
      }
      const uint32_t now = millis();
      bool ok;
      switch (f.f[0][0]) {
        case 'S':
          if ((ok = ofen::decode(f, this->status))) this->status_ms = now, this->have_status = true;
          break;
        case 'H':
          if ((ok = ofen::decode(f, this->hb))) this->hb_ms = now, this->hb_new = true;
          break;
        default: {
          ofen::CommandMsg cmd;
          // STOP wird nie verworfen, andere Befehle nur bis zur Queue-Grenze angenommen
          ok = ofen::decode(f, cmd) && (this->rx.size() < 8 || (cmd.type == 'C' && cmd.name == "STOP"));
          if (ok) this->rx.push_back(cmd);
        }
      }
      if (!ok) this->invalid++;
    }
  }

  void send(const std::string &line) { this->write_str(line.c_str()); }
  long next_seq() { return ++this->seq_; }

  // Sendet einen Befehl (Display -> NodeMCU), liefert die Sequenznummer für die Antwort.
  long send_cmd(char type, const std::string &name, const std::string &arg = "") {
    long seq = this->next_seq();
    this->send(ofen::encode(ofen::CommandMsg{type, seq, name, arg}));
    return seq;
  }

  bool status_fresh(uint32_t max_age_ms = 5000) const {
    return this->have_status && millis() - this->status_ms < max_age_ms;
  }

  float get_setup_priority() const override { return setup_priority::DATA; }

  // Zuletzt empfangene Telegramme
  ofen::StatusMsg status;
  uint32_t status_ms = 0;
  bool have_status = false;
  ofen::HeartbeatMsg hb;
  uint32_t hb_ms = 0;
  bool hb_new = false;
  std::deque<ofen::CommandMsg> rx;  // C, K und A in Empfangsreihenfolge
  uint32_t invalid = 0;

 protected:
  ofen::LineBuffer buf_;
  long seq_ = 0;
};

}  // namespace ofen_link
}  // namespace esphome
