#pragma once
// NodeMCU-Seite: verbindet Messwerte und Telegramme mit ofen::step() und sendet den Status.
#include <cstdlib>
#include <cstring>

#include "ofen_link.h"

namespace esphome {
namespace ofen_link {

static const char *const TAG_CTRL = "ofen";

struct OfenController {
  ofen::Memory mem;  // frisch nach jedem Boot: STOP, Pumpe aus
  ofen::Config cfg;
  ofen::ChainThresholds th;
  ofen::ChainFilter chain_filter;
  ofen::Chain chain = ofen::Chain::FEHLER;
  ofen::Outputs out;
  bool cfg_changed = false;  // YAML speichert dann die Kalibrierwerte im Flash
  ofen::Chain raw_fault = ofen::Chain::OK;  // Fehler aus einer einzelnen ungemittelten A0-Messung
  uint32_t status_ms = 0;
  bool status_sent = false;

  // Kalibrierwerte aus dem Flash übernehmen; unplausible Werte bleiben auf dem Standard. false = Wert verworfen.
  bool load(float min_pwm, float max_pwm, float flamme_aus_c, float abgas_max_c) {
    bool ok = ofen::set_calibration(this->cfg, "max_pwm", max_pwm) == ofen::Reason::OK;
    ok &= ofen::set_calibration(this->cfg, "min_pwm", min_pwm) == ofen::Reason::OK;
    ok &= ofen::set_calibration(this->cfg, "flamme_aus_c", flamme_aus_c) == ofen::Reason::OK;
    ok &= ofen::set_calibration(this->cfg, "abgas_max_c", abgas_max_c) == ofen::Reason::OK;
    return ok;
  }

  // Ein Schritt alle 100 ms (STOP wirkt damit sofort), Status 1x pro Sekunde.
  // a0_volt: gemittelte Spannung an A0. Liefert den PWM-Wert für Q1.
  float tick(OfenLink &link, float a0_volt, bool luefter, float abgas_c, uint32_t now) {
    ofen::Chain raw = this->raw_fault != ofen::Chain::OK ? this->raw_fault : ofen::classify_chain(a0_volt, this->th);
    this->raw_fault = ofen::Chain::OK;
    this->chain = this->chain_filter.update(raw, now);
    ofen::Inputs in;
    in.chain = this->chain;
    in.luefter = luefter;
    in.abgas_c = abgas_c;
    in.heartbeat = link.hb_new;
    link.hb_new = false;
    in.leistung_soll = (float) link.hb.leistung;
    in.raum_c = link.hb.raum_c;
    in.soll_c = link.hb.soll_c;

    const ofen::State vorher = this->mem.state;
    const ofen::Alarm alarm_vorher = this->mem.alarm;
    bool stepped = false;
    while (!link.rx.empty()) {
      ofen::CommandMsg c = link.rx.front();
      link.rx.pop_front();
      ofen::Reason r;
      if (c.type == 'C') {
        in.cmd = parse_cmd(c.name);
        in.cmd_arg = (float) atol(c.arg.c_str()) / 1000.0f;  // KAL: PWM in Promille
        if (in.cmd == ofen::Cmd::NONE) {
          r = ofen::Reason::UNBEKANNT;
        } else {
          this->out = ofen::step(this->mem, in, this->cfg, now);
          stepped = true;
          r = this->out.reply;
        }
        in.cmd = ofen::Cmd::NONE;
      } else if (c.type == 'K') {
        char *end;
        float v = strtof(c.arg.c_str(), &end);
        r = *end ? ofen::Reason::WERT : ofen::set_calibration(this->cfg, c.name.c_str(), v, this->mem.state);
        if (r == ofen::Reason::OK) this->cfg_changed = true;
      } else {
        continue;  // Antworten sind für das Display
      }
      ESP_LOGI(TAG_CTRL, "Befehl %c %s %s -> %s", c.type, c.name.c_str(), c.arg.c_str(), ofen::reason_text(r));
      link.send(ofen::encode(ofen::CommandMsg{'A', c.seq, r == ofen::Reason::OK ? "OK" : "ERR",
                                              std::to_string((int) r)}));
    }
    if (!stepped) this->out = ofen::step(this->mem, in, this->cfg, now);

    if (this->mem.state != vorher)
      ESP_LOGI(TAG_CTRL, "Zustand %s -> %s", ofen::state_name(vorher), ofen::state_name(this->mem.state));
    if (this->mem.alarm != alarm_vorher && this->mem.alarm != ofen::Alarm::KEINER)
      ESP_LOGW(TAG_CTRL, "ALARM: %s", ofen::alarm_text(this->mem.alarm));

    if (this->status_sent && now - this->status_ms < 1000) return this->out.pwm;
    this->status_sent = true;
    this->status_ms = now;
    ofen::StatusMsg s;
    s.seq = link.next_seq();
    s.state = (long) this->mem.state;
    s.alarm = (long) this->mem.alarm;
    s.pwm_promille = lroundf(this->out.pwm * 1000.0f);
    s.kw = this->out.kw;
    s.abgas_c = abgas_c;
    s.kette = (long) this->chain;
    s.luefter = luefter ? 1 : 0;
    s.anlauf_rest_s = (long) this->out.anlauf_rest_s;
    s.uptime_s = (long) (now / 1000);
    s.warn = this->mem.warn;
    link.send(ofen::encode(s));
    return this->out.pwm;
  }

  static ofen::Cmd parse_cmd(const std::string &n) {
    if (n == "START") return ofen::Cmd::START;
    if (n == "STOP") return ofen::Cmd::STOP;
    if (n == "ACK") return ofen::Cmd::ACK;
    if (n == "KAL") return ofen::Cmd::KAL;
    return ofen::Cmd::NONE;
  }
};

}  // namespace ofen_link
}  // namespace esphome
