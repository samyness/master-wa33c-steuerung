#pragma once
// Zustandsautomat des Ofen-Controllers (NodeMCU). Reine Logik ohne ESPHome-Abhängigkeit.
// step() wird jede Sekunde aufgerufen und entscheidet allein über die Pumpe.
#include <cmath>
#include <cstdint>

#include "chain.h"

namespace ofen {

enum class State : uint8_t {
  STOP = 0,
  ANZUENDEN = 1,
  ANLAUF = 2,
  BETRIEB = 3,
  AUSLOESCHEN = 4,
  ALARM = 5,
  KALIBRIERUNG_PUMPE = 6,
};

enum class Alarm : uint8_t {
  KEINER = 0,
  KETTE_UEBERLAUF = 1,
  KETTE_STB = 2,
  KETTE_FEHLER = 3,
  ZUENDUNG = 4,
  FLAMME_AUS = 5,
  ABGAS_UEBERTEMP = 6,
  LUEFTER = 7,
  SENSOR_ABGAS = 8,
};

enum class Cmd : uint8_t { NONE, START, STOP, ACK, KAL };

// Antwort auf einen Befehl (Protokoll: A;<seq>;OK|ERR;<grund>)
enum class Reason : uint8_t { OK = 0, ZUSTAND, KETTE, KAMMER_WARM, ABGAS, URSACHE, WERT, UNBEKANNT };

// Warnungen als Bitmaske. W_RAUM_WARM und W_SENSOR_AUS bleiben bis zum nächsten Start stehen.
enum : uint8_t {
  W_DISPLAY = 1,      // keine Verbindung zum Display > 60 s
  W_RAUM = 2,         // Raumfühler fehlt > 10 min
  W_ABGAS = 4,        // Abgassensor liefert im Betrieb keinen Wert
  W_RAUM_WARM = 8,    // wegen Raum-Überhitzung ausgelöscht
  W_SENSOR_AUS = 16,  // wegen Abgassensor-Ausfall ausgelöscht
};

constexpr uint32_t MIN_MS = 60000;

struct Config {
  // Kalibrierwerte (im Flash des NodeMCU gespeichert)
  float min_pwm = 0.35f;  // 17 kW
  float max_pwm = 0.80f;  // 33 kW
  float flamme_aus_c = 120.0f;
  float abgas_max_c = 450.0f;
  // Feste Grenzen aus den Sicherheitsregeln
  float start_abgas_max_c = 80.0f;  // Start nur bei Abgas darunter
  float abgas_heiss_c = 80.0f;      // muss in der Anlaufphase erreicht werden
  float abgas_ungueltig_c = 1000.0f;
  float flamme_rel_abfall = 0.40f;
  float raum_warm_k = 3.0f;
  float rampe_pro_min = 5.0f;  // % pro Minute
  uint32_t zuend_ms = 20 * MIN_MS;
  uint32_t anlauf_ms = 30 * MIN_MS;
  uint32_t abgas_heiss_ms = 30 * MIN_MS;
  uint32_t uebertemp_ms = MIN_MS;
  uint32_t luefter_ms = MIN_MS;
  uint32_t sensor_kulanz_ms = 10000;  // kurze Aussetzer des MAX6675 tolerieren
  uint32_t sensor_aus_ms = 10 * MIN_MS;
  uint32_t abgas_null_ms = 5 * MIN_MS;  // Anzünden: so lange exakt 0 °C trotz Feuer = Sensorfehler
  float kal_raum_max_c = 26.0f;
  uint32_t raum_ms = 10 * MIN_MS;
  uint32_t display_ms = MIN_MS;
  uint32_t raum_warm_ms = 30 * MIN_MS;
  uint32_t kal_ms = 20 * MIN_MS;
};

struct Inputs {
  Chain chain = Chain::FEHLER;
  bool luefter = false;       // D0: Lüfter hat Spannung = Kammer > 40 °C
  float abgas_c = NAN;        // MAX6675, NAN = kein Wert
  bool heartbeat = false;     // H-Telegramm in diesem Schritt empfangen
  float leistung_soll = 0;    // 0..100 % aus dem letzten H
  float raum_c = NAN;         // aus dem letzten H, NAN = fehlt
  float soll_c = NAN;         // aus dem letzten H
  Cmd cmd = Cmd::NONE;
  float cmd_arg = 0;          // KAL: PWM 0..1
};

struct Outputs {
  float pwm = 0;         // 0..1 an Q1
  float leistung = 0;    // 0..100 %, nur im Betrieb > 0
  float kw = 0;          // aktuelle Heizleistung, 0 wenn Pumpe aus
  uint32_t anlauf_rest_s = 0;
  Reason reply = Reason::OK;
};

// Zeit, seit der eine Bedingung ununterbrochen gilt (0, wenn sie nicht gilt).
struct Timer {
  bool on = false;
  uint32_t t0 = 0;
  uint32_t run(bool cond, uint32_t now) {
    if (!cond) return on = false, 0;
    if (!on) on = true, t0 = now;
    return now - t0;
  }
};

// Gesamter Zustand. Ein frisch konstruiertes Memory (= Neustart) ist immer STOP mit Pumpe aus.
struct Memory {
  State state = State::STOP;
  Alarm alarm = Alarm::KEINER;
  uint8_t warn = 0;
  uint32_t since = 0;     // Eintritt in den aktuellen Zustand
  uint32_t start_ms = 0;  // Zeitpunkt des Starts
  bool abgas_war_heiss = false;
  float leistung = 0;     // Rampenwert 0..100 %
  float kal_pwm = 0;
  bool have_last = false;
  uint32_t last_ms = 0;
  // Abgasverlauf, ein Wert pro Minute, [0] = ältester
  float hist[20] = {};
  uint8_t hist_n = 0;
  uint32_t hist_ms = 0;
  Timer t_display, t_raum, t_abgas_bad, t_abgas_null, t_abgas_low, t_uebertemp, t_luefter_aus, t_raum_warm;
};

inline bool pump_running(State s) { return s == State::ANZUENDEN || s == State::ANLAUF || s == State::BETRIEB; }

inline bool abgas_valid(float c, const Config &cfg) { return !std::isnan(c) && c < cfg.abgas_ungueltig_c; }

inline Alarm chain_alarm(Chain c) {
  switch (c) {
    case Chain::UEBERLAUF: return Alarm::KETTE_UEBERLAUF;
    case Chain::STB: return Alarm::KETTE_STB;
    default: return Alarm::KETTE_FEHLER;
  }
}

inline void enter(Memory &m, State s, uint32_t now) {
  m.state = s;
  m.since = now;
  m.leistung = 0;
}

inline void go_alarm(Memory &m, Alarm a, uint32_t now) {
  enter(m, State::ALARM, now);
  m.alarm = a;
}

inline void add_hist(Memory &m, float c) {
  if (m.hist_n == 20) {
    for (int i = 0; i < 19; i++) m.hist[i] = m.hist[i + 1];
    m.hist_n = 19;
  }
  m.hist[m.hist_n++] = c;
}

// Relativer Flammenabriss: aktueller Wert > 40 % unter dem Mittel der 15 min vor den letzten 5 min.
inline bool flame_drop(const Memory &m, float abgas, const Config &c) {
  if (m.hist_n < 20) return false;
  float sum = 0;
  for (int i = 0; i < 15; i++) sum += m.hist[i];
  return abgas < (1.0f - c.flamme_rel_abfall) * (sum / 15.0f);
}

inline Reason handle_cmd(Memory &m, const Inputs &in, const Config &c, uint32_t now) {
  const bool abgas_ok = abgas_valid(in.abgas_c, c);
  switch (in.cmd) {
    case Cmd::NONE: return Reason::OK;
    case Cmd::START:
      if (m.state != State::STOP) return Reason::ZUSTAND;
      if (in.chain != Chain::OK) return Reason::KETTE;
      if (in.luefter) return Reason::KAMMER_WARM;
      if (!abgas_ok || in.abgas_c >= c.start_abgas_max_c) return Reason::ABGAS;
      enter(m, State::ANZUENDEN, now);
      m.start_ms = now;
      m.abgas_war_heiss = false;
      m.hist_n = 0;
      m.hist_ms = now;
      m.warn &= ~(W_RAUM_WARM | W_SENSOR_AUS);
      return Reason::OK;
    case Cmd::STOP:
      if (pump_running(m.state)) enter(m, State::AUSLOESCHEN, now);
      else if (m.state == State::KALIBRIERUNG_PUMPE) enter(m, State::STOP, now);
      return Reason::OK;
    case Cmd::ACK: {
      if (m.state != State::ALARM) return Reason::ZUSTAND;
      if (in.chain != Chain::OK) return Reason::KETTE;
      if (in.luefter) return Reason::KAMMER_WARM;
      bool cause_active = (m.alarm == Alarm::ABGAS_UEBERTEMP && !(abgas_ok && in.abgas_c < c.abgas_max_c)) ||
                          (m.alarm == Alarm::SENSOR_ABGAS && !abgas_ok);
      if (cause_active) return Reason::URSACHE;
      enter(m, State::STOP, now);
      m.alarm = Alarm::KEINER;
      return Reason::OK;
    }
    case Cmd::KAL:
      if (!(in.cmd_arg >= 0.0f && in.cmd_arg <= 1.0f)) return Reason::WERT;
      if (m.state == State::KALIBRIERUNG_PUMPE) {
        m.kal_pwm = in.cmd_arg;
        return Reason::OK;
      }
      if (m.state != State::STOP) return Reason::ZUSTAND;
      if (in.chain != Chain::OK) return Reason::KETTE;
      if (in.luefter) return Reason::KAMMER_WARM;
      if (!abgas_ok || in.abgas_c >= c.start_abgas_max_c) return Reason::ABGAS;
      enter(m, State::KALIBRIERUNG_PUMPE, now);
      m.kal_pwm = in.cmd_arg;
      return Reason::OK;
  }
  return Reason::UNBEKANNT;
}

inline Outputs step(Memory &m, const Inputs &in, const Config &c, uint32_t now) {
  Outputs out;
  const uint32_t dt = m.have_last ? now - m.last_ms : 0;
  m.have_last = true;
  m.last_ms = now;

  // Regel 3: Sicherheitskette offen oder undefiniert -> sofort Alarm, Pumpe aus. Gilt in jedem Zustand.
  if (in.chain != Chain::OK) {
    if (m.state != State::ALARM || m.alarm != chain_alarm(in.chain)) go_alarm(m, chain_alarm(in.chain), now);
  }

  out.reply = handle_cmd(m, in, c, now);

  const State s = m.state;
  const bool running = pump_running(s);
  const bool abgas_ok = abgas_valid(in.abgas_c, c);
  // Im Anlauf/Betrieb muss das Abgas warm sein; dauerhaft 0 °C heißt Sensorfehler.
  const bool abgas_bad = !abgas_ok || ((s == State::ANLAUF || s == State::BETRIEB) && in.abgas_c <= 0.0f);

  const bool display_lost = m.t_display.run(!in.heartbeat, now) >= c.display_ms;
  const bool raum_fehlt = m.t_raum.run(std::isnan(in.raum_c), now) >= c.raum_ms;
  const uint32_t bad_for = m.t_abgas_bad.run(running && abgas_bad, now);
  // MAX6675 kann nicht unter 0 °C messen: 0 beim Start ist in kalter Werkstatt normal, nach 5 min Feuer nicht
  const uint32_t null_for = m.t_abgas_null.run(s == State::ANZUENDEN && abgas_ok && in.abgas_c <= 0.0f, now);
  const uint32_t low_for = m.t_abgas_low.run(s == State::BETRIEB && !abgas_bad && in.abgas_c < c.flamme_aus_c, now);
  const uint32_t heiss_for = m.t_uebertemp.run(running && abgas_ok && in.abgas_c > c.abgas_max_c, now);
  const uint32_t luefter_aus_for = m.t_luefter_aus.run((s == State::ANLAUF || s == State::BETRIEB) && !in.luefter, now);
  const bool zu_warm = !display_lost && !std::isnan(in.raum_c) && !std::isnan(in.soll_c) &&
                       in.raum_c > in.soll_c + c.raum_warm_k && m.leistung <= 0.0f;
  const uint32_t warm_for = m.t_raum_warm.run((s == State::ANLAUF || s == State::BETRIEB) && zu_warm, now);

  m.warn &= ~(W_DISPLAY | W_RAUM | W_ABGAS);
  if (display_lost) m.warn |= W_DISPLAY;
  if (raum_fehlt) m.warn |= W_RAUM;

  if (running) {
    if (abgas_ok && in.abgas_c > c.abgas_heiss_c) m.abgas_war_heiss = true;
    if (!abgas_bad && now - m.hist_ms >= MIN_MS) {
      add_hist(m, in.abgas_c);
      m.hist_ms = now;
    }

    if (heiss_for >= c.uebertemp_ms) {
      go_alarm(m, Alarm::ABGAS_UEBERTEMP, now);
    } else if (luefter_aus_for >= c.luefter_ms) {
      go_alarm(m, Alarm::LUEFTER, now);
    } else if (s != State::BETRIEB && (bad_for >= c.sensor_kulanz_ms || null_for >= c.abgas_null_ms)) {
      go_alarm(m, Alarm::SENSOR_ABGAS, now);
    } else if (s != State::BETRIEB && !m.abgas_war_heiss && now - m.start_ms >= c.abgas_heiss_ms) {
      go_alarm(m, Alarm::FLAMME_AUS, now);
    } else if (s == State::BETRIEB && (low_for >= c.sensor_kulanz_ms || (!abgas_bad && flame_drop(m, in.abgas_c, c)))) {
      go_alarm(m, Alarm::FLAMME_AUS, now);
    } else if (s == State::BETRIEB && bad_for >= c.sensor_aus_ms) {
      enter(m, State::AUSLOESCHEN, now);
      m.warn |= W_SENSOR_AUS;
    } else if (warm_for >= c.raum_warm_ms) {
      enter(m, State::AUSLOESCHEN, now);
      m.warn |= W_RAUM_WARM;
    } else if (s == State::ANZUENDEN) {
      if (in.luefter) enter(m, State::ANLAUF, now);
      else if (now - m.since >= c.zuend_ms) go_alarm(m, Alarm::ZUENDUNG, now);
    } else if (s == State::ANLAUF) {
      if (now - m.since >= c.anlauf_ms) enter(m, State::BETRIEB, now);
      else out.anlauf_rest_s = (c.anlauf_ms - (now - m.since)) / 1000;
    } else {
      // Regel 10/11: bei fehlendem Sensor, Raumfühler oder Display nur Minimalleistung, kein Abschalten.
      const bool limit = abgas_bad || display_lost || raum_fehlt;
      if (abgas_bad) m.warn |= W_ABGAS;
      float target = limit ? 0.0f : std::fmax(0.0f, std::fmin(100.0f, in.leistung_soll));
      // Regel 16: Rampe max. 5 %/min in beide Richtungen
      float max_delta = c.rampe_pro_min * (float) dt / (float) MIN_MS;
      float d = std::fmax(-max_delta, std::fmin(max_delta, target - m.leistung));
      m.leistung = std::fmax(0.0f, std::fmin(100.0f, m.leistung + d));
    }
  } else if (s == State::AUSLOESCHEN) {
    if (!in.luefter) enter(m, State::STOP, now);
  } else if (s == State::KALIBRIERUNG_PUMPE) {
    // Pumpe ohne Flamme: abbrechen bei warmer Kammer/Abgas, Sensorfehler, Raum zu warm, fehlendem Display oder nach 20 min
    const bool abbruch = in.luefter || !abgas_ok || in.abgas_c >= c.start_abgas_max_c ||
                         in.raum_c > c.kal_raum_max_c || display_lost || now - m.since >= c.kal_ms;
    if (abbruch) enter(m, State::STOP, now);
  }

  switch (m.state) {
    case State::ANZUENDEN:
    case State::ANLAUF:
      out.pwm = c.min_pwm;
      break;
    case State::BETRIEB:
      out.pwm = c.min_pwm + (c.max_pwm - c.min_pwm) * m.leistung / 100.0f;
      break;
    case State::KALIBRIERUNG_PUMPE:
      out.pwm = m.kal_pwm;
      break;
    default:
      out.pwm = 0;
  }
  if (m.state == State::BETRIEB) out.leistung = m.leistung;
  if (pump_running(m.state)) out.kw = 17.0f + 16.0f * out.leistung / 100.0f;
  return out;
}

// Kalibrierwert per K-Befehl setzen. Liefert WERT bei unplausiblen Werten, Config bleibt dann unverändert.
// PWM-Grenzen nur in STOP: im Betrieb würde die Pumpe sonst ohne Rampe springen.
inline Reason set_calibration(Config &c, const char *key, float v, State s = State::STOP) {
  auto eq = [&](const char *k) {
    const char *a = key;
    while (*a && *a == *k) a++, k++;
    return *a == *k;
  };
  if (!std::isfinite(v)) return Reason::WERT;
  if ((eq("min_pwm") || eq("max_pwm")) && s != State::STOP) return Reason::ZUSTAND;
  if (eq("min_pwm")) {
    if (v < 0.05f || v >= c.max_pwm) return Reason::WERT;
    c.min_pwm = v;
  } else if (eq("max_pwm")) {
    if (v > 1.0f || v <= c.min_pwm) return Reason::WERT;
    c.max_pwm = v;
  } else if (eq("flamme_aus_c")) {
    if (v < 50.0f || v > 400.0f) return Reason::WERT;
    c.flamme_aus_c = v;
  } else if (eq("abgas_max_c")) {
    if (v < 150.0f || v > 800.0f) return Reason::WERT;
    c.abgas_max_c = v;
  } else {
    return Reason::UNBEKANNT;
  }
  return Reason::OK;
}

inline const char *state_name(State s) {
  switch (s) {
    case State::STOP: return "STOP";
    case State::ANZUENDEN: return "ANZUENDEN";
    case State::ANLAUF: return "ANLAUF";
    case State::BETRIEB: return "BETRIEB";
    case State::AUSLOESCHEN: return "AUSLOESCHEN";
    case State::ALARM: return "ALARM";
    case State::KALIBRIERUNG_PUMPE: return "KALIBRIERUNG_PUMPE";
  }
  return "?";
}

inline const char *alarm_text(Alarm a) {
  switch (a) {
    case Alarm::KEINER: return "";
    case Alarm::KETTE_UEBERLAUF: return "Überlaufschalter offen";
    case Alarm::KETTE_STB: return "STB ausgelöst oder Sicherung F3 defekt";
    case Alarm::KETTE_FEHLER: return "Sicherheitskette: Leitungs- oder Bauteilfehler";
    case Alarm::ZUENDUNG: return "Zündung fehlgeschlagen";
    case Alarm::FLAMME_AUS: return "Flamme aus";
    case Alarm::ABGAS_UEBERTEMP: return "Abgas-Übertemperatur";
    case Alarm::LUEFTER: return "Lüfter/T40 meldet aus";
    case Alarm::SENSOR_ABGAS: return "Abgassensor ausgefallen";
  }
  return "?";
}

inline const char *alarm_hint(Alarm a) {
  switch (a) {
    case Alarm::KEINER: return "";
    case Alarm::KETTE_UEBERLAUF: return "Brenntopf auf Ölüberlauf prüfen, Öl entfernen, Überlaufschalter prüfen.";
    case Alarm::KETTE_STB: return "Ofen abkühlen lassen, STB am Ofen zurücksetzen, Sicherung F3 prüfen.";
    case Alarm::KETTE_FEHLER: return "Leitungen und Widerstände der Statuserkennung (A0) prüfen.";
    case Alarm::ZUENDUNG: return "Kammer abkühlen lassen, Öl im Topf prüfen, neu anzünden.";
    case Alarm::FLAMME_AUS: return "Kammer abkühlen lassen, Brennteller reinigen, Öl prüfen, neu anzünden.";
    case Alarm::ABGAS_UEBERTEMP: return "Leistung/Kalibrierung prüfen, Abgasweg und Thermoelement prüfen.";
    case Alarm::LUEFTER: return "Lüfter, T40 und Optokoppler OK1 prüfen.";
    case Alarm::SENSOR_ABGAS: return "Thermoelement und MAX6675-Verkabelung prüfen.";
  }
  return "";
}

inline const char *reason_text(Reason r) {
  switch (r) {
    case Reason::OK: return "OK";
    case Reason::ZUSTAND: return "im aktuellen Zustand nicht erlaubt";
    case Reason::KETTE: return "Sicherheitskette offen";
    case Reason::KAMMER_WARM: return "Kammer noch warm";
    case Reason::ABGAS: return "Abgastemperatur unplausibel oder zu hoch";
    case Reason::URSACHE: return "Ursache noch nicht behoben";
    case Reason::WERT: return "ungültiger Wert";
    case Reason::UNBEKANNT: return "unbekannter Befehl";
  }
  return "?";
}

}  // namespace ofen
