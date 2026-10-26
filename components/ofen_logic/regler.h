#pragma once
// Raumregelung, Zeitprogramm und Kalibrier-Rechnungen (laufen auf dem Display).
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace ofen {

// PI-Regler ohne D-Anteil. Ausgang 0..100 % (= 17..33 kW).
struct PiRegler {
  float kp = 20.0f;      // %/K
  float ti_min = 30.0f;  // Nachstellzeit in Minuten
  float totband = 0.3f;  // K
  float i = 0;           // I-Anteil, begrenzt auf 0..100

  // dt_s: Abtastzeit (30 s). betrieb = false friert den I-Anteil ein (Anti-Windup).
  float update(float soll, float ist, float dt_s, bool betrieb) {
    if (std::isnan(soll) || std::isnan(ist)) return clamp(i);
    float e = soll - ist;
    if (std::fabs(e) <= totband) e = 0;
    if (betrieb && ti_min > 0) i = clamp(i + kp * e * dt_s / (ti_min * 60.0f));
    return clamp(kp * e + i);
  }
  static float clamp(float v) { return std::fmax(0.0f, std::fmin(100.0f, v)); }
};

// --- Zeitprogramm: ändert nur den Sollwert, startet nie den Ofen ---

struct Zeitfenster {
  uint16_t von_min = 0, bis_min = 0;  // Minuten seit Mitternacht, bis exklusiv
  float soll = 20.0f;
  bool aktiv = false;
};

struct Wochenprogramm {
  Zeitfenster tag[7][4];  // [0] = Montag
  float absenkung = 12.0f;

  // Index des Fensters, das zu (wochentag 0=Mo, minute) gilt, sonst -1.
  int fenster(int wochentag, int minute) const {
    for (int k = 0; k < 4; k++) {
      const Zeitfenster &z = tag[wochentag][k];
      if (z.aktiv && minute >= z.von_min && minute < z.bis_min) return k;
    }
    return -1;
  }
  float soll(int wochentag, int minute) const {
    int k = fenster(wochentag, minute);
    return k < 0 ? absenkung : tag[wochentag][k].soll;
  }
  // true genau in der Minute, in der ein Fenster beginnt (für Hinweis/Push "Heizzeit beginnt").
  bool beginnt(int wochentag, int minute) const {
    for (int k = 0; k < 4; k++) {
      const Zeitfenster &z = tag[wochentag][k];
      if (z.aktiv && z.von_min == minute) return true;
    }
    return false;
  }
};

// Liest einen Wochentag im Format "06:00-08:00=21 17:00-22:30=21.5" (bis 4 Fenster, Leer- oder Komma-
// getrennt, "24:00" als Ende erlaubt). false bei Formfehler; tag bleibt dann unverändert.
inline bool parse_tag(const char *s, Zeitfenster (&tag)[4]) {
  Zeitfenster neu[4];
  int n = 0;
  while (true) {
    while (*s == ' ' || *s == ',') s++;
    if (!*s) break;
    int h1, m1, h2, m2, len = 0;
    float soll;
    if (n == 4 || sscanf(s, "%d:%d-%d:%d=%f%n", &h1, &m1, &h2, &m2, &soll, &len) != 5 || len == 0) return false;
    int von = h1 * 60 + m1, bis = h2 * 60 + m2;
    if (h1 < 0 || m1 < 0 || m1 > 59 || m2 < 0 || m2 > 59 || von >= bis || bis > 1440 || soll < 5 || soll > 28)
      return false;
    neu[n++] = {(uint16_t) von, (uint16_t) bis, soll, true};
    s += len;
  }
  for (int k = 0; k < 4; k++) tag[k] = neu[k];
  return true;
}

// --- Kalibrierung Pumpenkennlinie ---

struct Gerade {
  float a = 0, b = 0;  // y = a + b*x
  bool ok = false;
};

inline Gerade regression(const float *x, const float *y, int n) {
  Gerade g;
  if (n < 2) return g;
  double sx = 0, sy = 0, sxx = 0, sxy = 0;
  for (int i = 0; i < n; i++) sx += x[i], sy += y[i], sxx += x[i] * x[i], sxy += x[i] * y[i];
  double den = n * sxx - sx * sx;
  if (std::fabs(den) < 1e-12) return g;
  g.b = (float) ((n * sxy - sx * sy) / den);
  g.a = (float) ((sy - g.b * sx) / n);
  g.ok = g.b > 0;  // mehr PWM muss mehr Öl fördern
  return g;
}

struct PumpenKalibrierung {
  float pwm_17 = 0, pwm_33 = 0;  // 0..1
  bool ok = false;
};

// pwm[] 0..1, ml[] je Stufe gemessene Menge, minuten = Dauer je Stufe, dichte in kg/l.
inline PumpenKalibrierung pumpe_kalibrieren(const float *pwm, const float *ml, int n, float minuten, float dichte) {
  PumpenKalibrierung r;
  if (n < 2 || n > 8 || minuten <= 0 || dichte <= 0) return r;
  float kgh[8];
  for (int i = 0; i < n; i++) kgh[i] = ml[i] / 1000.0f * dichte * 60.0f / minuten;
  Gerade g = regression(pwm, kgh, n);
  if (!g.ok) return r;
  r.pwm_17 = (1.46f - g.a) / g.b;
  r.pwm_33 = (2.80f - g.a) / g.b;
  r.ok = r.pwm_17 > 0.0f && r.pwm_33 <= 1.0f && r.pwm_17 < r.pwm_33;
  return r;
}

// Vorschlag Flamme-aus-Grenze: 60 % des Abgas-Mittels auf Minimum, mindestens 80 °C.
inline float flamme_aus_vorschlag(float mittel_abgas_c) { return std::fmax(80.0f, 0.6f * mittel_abgas_c); }

// --- Regelstrecke aus Sprungversuch (2-Punkt-Methode nach Smith) + Lambda-Tuning ---

struct Strecke {
  float k = 0, t_min = 0, l_min = 0;  // K/%, Zeitkonstante, Totzeit
  float kp = 0, ti_min = 0;           // Reglervorschlag
  bool ok = false;
};

// t[] Minuten seit Sprung, y[] Raumtemperatur, du = Sprunghöhe in %.
inline Strecke sprung_auswerten(const float *t, const float *y, int n, float du) {
  Strecke s;
  if (n < 3 || du <= 0) return s;
  float y0 = y[0], dy = y[n - 1] - y0;
  if (dy <= 0.1f) return s;
  auto zeit_bei = [&](float anteil) -> float {
    float ziel = y0 + anteil * dy;
    for (int i = 1; i < n; i++)
      if (y[i] >= ziel) return t[i - 1] + (t[i] - t[i - 1]) * (ziel - y[i - 1]) / (y[i] - y[i - 1]);
    return NAN;
  };
  float t28 = zeit_bei(0.283f), t63 = zeit_bei(0.632f);
  if (std::isnan(t28) || std::isnan(t63) || t63 <= t28) return s;
  s.t_min = 1.5f * (t63 - t28);
  s.l_min = std::fmax(0.0f, t63 - s.t_min);
  s.k = dy / du;
  float lambda = s.t_min;
  s.kp = s.t_min / (s.k * (lambda + s.l_min));
  s.ti_min = s.t_min;
  s.ok = true;
  return s;
}

}  // namespace ofen
