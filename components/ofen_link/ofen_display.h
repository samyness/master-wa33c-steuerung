#pragma once
// Laufzeitmodell des Displays. Keine sicherheitskritische Pumpenentscheidung findet hier statt.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "esphome/components/ofen_logic/controller.h"
#include "esphome/components/ofen_logic/regler.h"

namespace esphome::ofen_link {

struct DisplayRuntime {
  ofen::PiRegler regler;
  ofen::Wochenprogramm programm;
  float leistung_soll = 0;
  float soll_c = 20;
  bool programm_an = false;
  bool raum_warnung = false;
  uint32_t letzter_raum_ms = 0;
  int letztes_fenster = -2;
  int letzter_tag = -1;
  ofen::Alarm letzter_alarm = ofen::Alarm::KEINER;
  std::string meldung;

  // Je fünf Minuten ein Punkt: 24 h = 288 Punkte. Die Arrays landen auf dem ESP32-S3 im PSRAM.
  static constexpr int HIST_N = 288;
  std::array<int16_t, HIST_N> hist_raum{}, hist_soll{}, hist_abgas{}, hist_leistung{};
  int hist_pos = 0, hist_count = 0;
  void *chart = nullptr, *chart_raum = nullptr, *chart_soll = nullptr, *chart_abgas = nullptr, *chart_leistung = nullptr;

  enum class KalSchritt : uint8_t { AUS, PUMPE, FLAMME, SPRUNG };
  KalSchritt kal = KalSchritt::AUS;
  uint32_t kal_start_ms = 0, stufe_start_ms = 0;
  int kal_stufe = 0;
  bool kal_wartet_ml = false;
  std::array<float, 5> kal_pwm{{.30f, .45f, .60f, .75f, .90f}};
  std::array<float, 5> kal_ml{};
  float alt_min_pwm = .35f, alt_max_pwm = .80f, alt_flamme = 120.f;
  float neu_min_pwm = NAN, neu_max_pwm = NAN, neu_flamme = NAN;
  std::array<float, 91> sprung_t{}, sprung_y{};
  int sprung_n = 0;
  uint32_t betrieb_seit_ms = 0;
  long letzter_status_state = -1;
  ofen::Strecke sprung_ergebnis;
  double flamme_summe = 0;
  int flamme_n = 0;

  // Text eines Wochentags (0 = Mo) übernehmen; bei Formfehler bleibt das alte Programm und es gibt eine Meldung.
  void set_tag(int d, const std::string &text) {
    static const char *const namen[7] = {"Mo", "Di", "Mi", "Do", "Fr", "Sa", "So"};
    if (!ofen::parse_tag(text.c_str(), programm.tag[d]))
      meldung = std::string("Zeitprogramm ") + namen[d] + ": Format z. B. 06:00-08:00=21 17:00-22:00=21.5";
  }

  // "Mo 06:00 → 21.0 °C" für das nächste beginnende Fenster innerhalb einer Woche, sonst "".
  std::string naechstes_fenster(int tag, int minute) const {
    static const char *const namen[7] = {"Mo", "Di", "Mi", "Do", "Fr", "Sa", "So"};
    for (int i = 0; i < 7 * 1440; i++) {
      int t = (tag + (minute + i) / 1440) % 7, m = (minute + i) % 1440;
      if (i > 0 && programm.beginnt(t, m)) {
        char b[40];
        snprintf(b, sizeof(b), "%s %02d:%02d -> %.1f °C", namen[t], m / 60, m % 60, programm.soll(t, m));
        return b;
      }
    }
    return "";
  }

  void history(float raum, float soll, float abgas, float leistung) {
    auto cv = [](float v) -> int16_t { return std::isnan(v) ? INT16_MIN : (int16_t) lroundf(v * 10); };
    hist_raum[hist_pos] = cv(raum); hist_soll[hist_pos] = cv(soll);
    hist_abgas[hist_pos] = cv(abgas); hist_leistung[hist_pos] = cv(leistung);
    hist_pos = (hist_pos + 1) % HIST_N;
    hist_count = std::min(HIST_N, hist_count + 1);
  }

  bool start_erlaubt(const ofen::StatusMsg &s, bool frisch) const {
    return frisch && s.state == (long) ofen::State::STOP && s.kette == 0 && !s.luefter &&
           !std::isnan(s.abgas_c) && s.abgas_c < 80.f;
  }
  std::string startsperre(const ofen::StatusMsg &s, bool frisch) const {
    if (!frisch) return "Keine Verbindung zum Ofen-Controller";
    if (s.state != (long) ofen::State::STOP) return "Zustand ist nicht STOP";
    if (s.kette != 0) return "Sicherheitskette ist nicht geschlossen";
    if (s.luefter) return "Kammer ist noch warm";
    if (std::isnan(s.abgas_c) || s.abgas_c >= 80.f) return "Abgastemperatur ungültig oder zu hoch";
    return "Start erlaubt";
  }
  void pumpe_auswerten(float dichte) {
    auto r = ofen::pumpe_kalibrieren(kal_pwm.data(), kal_ml.data(), 5, 3.f, dichte);
    neu_min_pwm = r.ok ? r.pwm_17 : NAN; neu_max_pwm = r.ok ? r.pwm_33 : NAN;
  }
  // Statuszeile der Kalibrierseite: laufender Schritt mit Fortschritt, sonst Ergebnis oder letzte Meldung.
  std::string kal_status(uint32_t now) const {
    char b[160];
    const unsigned min = (now - kal_start_ms) / 60000;
    switch (kal) {
      case KalSchritt::PUMPE: {
        if (kal_wartet_ml) {
          snprintf(b, sizeof(b), "Pumpe Stufe %d/5 fertig – MENGE EINGEBEN", kal_stufe + 1);
        } else {
          unsigned rest = (180000UL - std::min<uint32_t>(180000UL, now - stufe_start_ms)) / 1000;
          snprintf(b, sizeof(b), "Pumpe Stufe %d/5 (%.0f %%): noch %u:%02u min", kal_stufe + 1, kal_pwm[kal_stufe] * 100,
                   rest / 60, rest % 60);
        }
        return b;
      }
      case KalSchritt::FLAMME:
        snprintf(b, sizeof(b), "Flamme lernen läuft: %u von 20 min, Leistung auf Minimum", min);
        return b;
      case KalSchritt::SPRUNG:
        snprintf(b, sizeof(b), "Sprungtest läuft: %d von 90 min, Leistung 100 %%", sprung_n > 0 ? sprung_n - 1 : 0);
        return b;
      case KalSchritt::AUS:
        break;
    }
    if (!std::isnan(neu_min_pwm)) {
      snprintf(b, sizeof(b), "Ergebnis Pumpe: 17 kW = %.0f %%, 33 kW = %.0f %% – ÜBERNEHMEN", neu_min_pwm * 100,
               neu_max_pwm * 100);
      return b;
    }
    if (!std::isnan(neu_flamme)) {
      snprintf(b, sizeof(b), "Vorschlag Flamme aus: %.0f °C – ÜBERNEHMEN", neu_flamme);
      return b;
    }
    if (sprung_ergebnis.ok) {
      snprintf(b, sizeof(b), "Regler: Kp = %.1f %%/K, Ti = %.0f min – ÜBERNEHMEN", sprung_ergebnis.kp, sprung_ergebnis.ti_min);
      return b;
    }
    return meldung.empty() ? "Bereit. Flamme lernen und Sprungtest brauchen Regelbetrieb." : meldung;
  }

  // Grund, warum Flamme lernen / Sprungtest jetzt nicht starten darf ("" = erlaubt).
  std::string kal_sperre(const ofen::StatusMsg &s, bool frisch, bool sprung, uint32_t now) const {
    if (kal != KalSchritt::AUS) return "Es läuft bereits ein Kalibrierschritt";
    if (!frisch) return "Keine Verbindung zum Ofen-Controller";
    if (s.state != (long) ofen::State::BETRIEB) return "Nur im Regelbetrieb (BETRIEB) möglich";
    if (sprung && now - betrieb_seit_ms < 3600000UL) {
      char b[80];
      snprintf(b, sizeof(b), "Sprungtest erst nach 60 min Betrieb (noch %lu min)",
               (unsigned long) ((3600000UL - (now - betrieb_seit_ms)) / 60000 + 1));
      return b;
    }
    return "";
  }

  bool kal_abbrechen(const ofen::StatusMsg &s, bool frisch, float raum, uint32_t now) const {
    return kal != KalSchritt::AUS && (!frisch || s.state == (long) ofen::State::ALARM ||
      std::isnan(raum) || raum > 26.f || now - kal_start_ms > 3UL * 60UL * 60UL * 1000UL);
  }
};

inline DisplayRuntime display_runtime;

}  // namespace esphome::ofen_link
