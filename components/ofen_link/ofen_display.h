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
  bool kal_abbrechen(const ofen::StatusMsg &s, bool frisch, float raum, uint32_t now) const {
    return kal != KalSchritt::AUS && (!frisch || s.state == (long) ofen::State::ALARM ||
      std::isnan(raum) || raum > 26.f || now - kal_start_ms > 3UL * 60UL * 60UL * 1000UL);
  }
};

inline DisplayRuntime display_runtime;

}  // namespace esphome::ofen_link
