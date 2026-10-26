// Host-Unit-Tests für ofen_logic. Bau: siehe test/Makefile bzw. README.
#include <cstdio>
#include <string>

#include "../components/ofen_logic/ofen_logic.h"

using namespace ofen;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    g_checks++;                                                        \
    if (!(cond)) {                                                     \
      g_fail++;                                                        \
      printf("FEHLER %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
    }                                                                  \
  } while (0)

// Simulierter Ofen: step() einmal pro Sekunde, Display schickt Heartbeats.
struct Sim {
  Memory m;
  Config c;
  Inputs in;
  Outputs out;
  uint32_t now = 0;
  bool display = true;

  Sim() {
    in.chain = Chain::OK;
    in.abgas_c = 20;
    in.raum_c = 18;
    in.soll_c = 20;
  }
  Outputs tick() {
    now += 1000;
    in.heartbeat = display;
    out = step(m, in, c, now);
    in.cmd = Cmd::NONE;
    return out;
  }
  void run_s(uint32_t s) {
    for (uint32_t i = 0; i < s; i++) tick();
  }
  Reason cmd(Cmd k, float arg = 0) {
    in.cmd = k;
    in.cmd_arg = arg;
    return tick().reply;
  }
  // Bis in den Regelbetrieb fahren
  void to_betrieb() {
    CHECK(cmd(Cmd::START) == Reason::OK);
    run_s(60);
    in.abgas_c = 150;
    in.luefter = true;
    run_s(5);
    CHECK(m.state == State::ANLAUF);
    in.abgas_c = 200;
    run_s(30 * 60);
    CHECK(m.state == State::BETRIEB);
  }
};

static const State ALL_STATES[] = {State::STOP,        State::ANZUENDEN, State::ANLAUF,
                                   State::BETRIEB,     State::AUSLOESCHEN, State::ALARM,
                                   State::KALIBRIERUNG_PUMPE};

// Bringt eine Sim in den gewünschten Zustand.
static void reach(Sim &s, State target) {
  switch (target) {
    case State::STOP: break;
    case State::ANZUENDEN: s.cmd(Cmd::START); break;
    case State::ANLAUF:
      s.cmd(Cmd::START);
      s.in.luefter = true;
      s.in.abgas_c = 150;
      s.run_s(3);
      break;
    case State::BETRIEB: s.to_betrieb(); break;
    case State::AUSLOESCHEN:
      s.to_betrieb();
      s.cmd(Cmd::STOP);
      break;
    case State::ALARM:
      s.cmd(Cmd::START);
      s.run_s(20 * 60 + 5);
      break;
    case State::KALIBRIERUNG_PUMPE: s.cmd(Cmd::KAL, 0.5f); break;
  }
  CHECK(s.m.state == target);
}

// Regel 1 + 15: Neustart in jedem Zustand -> STOP, Pumpe aus; kein Weg aus STOP ohne START
static void test_neustart() {
  for (State st : ALL_STATES) {
    Sim s;
    reach(s, st);
    // Neustart = neues Memory; Eingänge bleiben wie sie sind (z. B. Kammer noch warm)
    s.m = Memory();
    CHECK(s.m.state == State::STOP);
    for (int i = 0; i < 3600; i++) {
      Outputs o = s.tick();
      CHECK(o.pwm == 0.0f);
      if (o.pwm != 0.0f) break;
    }
    CHECK(s.m.state == State::STOP || s.m.state == State::ALARM);
  }
}

// Regel 1: weder Leistungswunsch, Raumtemperatur noch Zeitprogramm starten den Ofen
static void test_kein_autostart() {
  Sim s;
  Wochenprogramm wp;
  wp.tag[0][0] = {6 * 60, 8 * 60, 22.0f, true};
  for (int min = 0; min < 24 * 60; min++) {
    s.in.soll_c = wp.soll(0, min);
    s.in.raum_c = 5;  // kalt -> Regler will 100 %
    s.in.leistung_soll = 100;
    Outputs o = s.tick();
    CHECK(o.pwm == 0.0f);
    if (o.pwm != 0.0f) break;
  }
  CHECK(s.m.state == State::STOP);
  CHECK(wp.beginnt(0, 6 * 60));
  CHECK(!wp.beginnt(0, 6 * 60 + 1));
  CHECK(wp.soll(0, 7 * 60) == 22.0f);
  CHECK(wp.soll(0, 9 * 60) == 12.0f);
  CHECK(wp.soll(1, 7 * 60) == 12.0f);
}

// Regel 4: Startbedingungen
static void test_start_bedingungen() {
  { Sim s; s.in.luefter = true; CHECK(s.cmd(Cmd::START) == Reason::KAMMER_WARM); CHECK(s.m.state == State::STOP); }
  { Sim s; s.in.abgas_c = 85; CHECK(s.cmd(Cmd::START) == Reason::ABGAS); }
  { Sim s; s.in.abgas_c = NAN; CHECK(s.cmd(Cmd::START) == Reason::ABGAS); }
  { Sim s; s.in.abgas_c = 1500; CHECK(s.cmd(Cmd::START) == Reason::ABGAS); }
  {
    Sim s;
    s.in.chain = Chain::UEBERLAUF;
    CHECK(s.cmd(Cmd::START) != Reason::OK);
    CHECK(s.tick().pwm == 0.0f);
  }
  {
    Sim s;  // Alarm offen
    reach(s, State::ALARM);
    CHECK(s.cmd(Cmd::START) == Reason::ZUSTAND);
    CHECK(s.m.state == State::ALARM);
  }
  {
    Sim s;  // während AUSLOESCHEN gesperrt (Regel 12)
    reach(s, State::AUSLOESCHEN);
    CHECK(s.cmd(Cmd::START) == Reason::ZUSTAND);
  }
  {
    Sim s;
    CHECK(s.cmd(Cmd::START) == Reason::OK);
    CHECK(s.m.state == State::ANZUENDEN);
    CHECK(s.out.pwm == s.c.min_pwm);
  }
}

// Regel 5: 20 min ohne Lüfter -> Alarm Zündung
static void test_zuendung() {
  Sim s;
  s.cmd(Cmd::START);
  s.in.abgas_c = 100;
  s.run_s(20 * 60 - 5);
  CHECK(s.m.state == State::ANZUENDEN);
  s.run_s(10);
  CHECK(s.m.state == State::ALARM);
  CHECK(s.m.alarm == Alarm::ZUENDUNG);
  CHECK(s.out.pwm == 0.0f);
}

// Regel 3: Kette öffnet in jedem Zustand -> sofort Pumpe 0, ALARM
static void test_kette_jeder_zustand() {
  const Chain faults[] = {Chain::UEBERLAUF, Chain::STB, Chain::FEHLER};
  for (State st : ALL_STATES) {
    for (Chain f : faults) {
      Sim s;
      reach(s, st);
      s.in.chain = f;
      Outputs o = s.tick();
      CHECK(o.pwm == 0.0f);
      CHECK(s.m.state == State::ALARM);
      CHECK(s.m.alarm == chain_alarm(f));
    }
  }
}

// A0-Auswertung inkl. Zwischenbereiche und Entprellung
static void test_a0() {
  CHECK(classify_chain(2.71f) == Chain::OK);
  CHECK(classify_chain(2.47f) == Chain::OK);
  CHECK(classify_chain(2.01f) == Chain::OK);
  CHECK(classify_chain(2.0f) == Chain::FEHLER);
  CHECK(classify_chain(1.9f) == Chain::FEHLER);
  CHECK(classify_chain(1.8f) == Chain::UEBERLAUF);
  CHECK(classify_chain(1.51f) == Chain::UEBERLAUF);
  CHECK(classify_chain(1.23f) == Chain::UEBERLAUF);
  CHECK(classify_chain(0.8f) == Chain::UEBERLAUF);
  CHECK(classify_chain(0.79f) == Chain::FEHLER);
  CHECK(classify_chain(0.5f) == Chain::FEHLER);
  CHECK(classify_chain(0.49f) == Chain::STB);
  CHECK(classify_chain(0.0f) == Chain::STB);
  CHECK(classify_chain(NAN) == Chain::FEHLER);
  ChainThresholds t82{2.2f, 0.9f, 1.9f, 0.5f};
  CHECK(classify_chain(2.1f, t82) == Chain::FEHLER);

  ChainFilter f;
  CHECK(f.update(Chain::OK, 0) == Chain::OK);  // erste Messung direkt
  CHECK(f.update(Chain::STB, 100) == Chain::STB);  // Fehler sofort
  CHECK(f.update(Chain::OK, 1000) == Chain::STB);
  CHECK(f.update(Chain::OK, 2900) == Chain::STB);
  CHECK(f.update(Chain::OK, 3000) == Chain::OK);  // nach 2 s stabil
  CHECK(f.update(Chain::FEHLER, 3100) == Chain::FEHLER);
  CHECK(f.update(Chain::OK, 3200) == Chain::FEHLER);
  CHECK(f.update(Chain::UEBERLAUF, 4000) == Chain::UEBERLAUF);  // Unterbrechung setzt zurück
  CHECK(f.update(Chain::OK, 5000) == Chain::UEBERLAUF);
  CHECK(f.update(Chain::OK, 6999) == Chain::UEBERLAUF);
  CHECK(f.update(Chain::OK, 7000) == Chain::OK);
  ChainFilter boot;
  CHECK(boot.update(Chain::STB, 0) == Chain::STB);
}

// Regel 6: Anlauf 30 min auf Minimum, Leistungswunsch ignoriert
static void test_anlauf() {
  Sim s;
  s.in.leistung_soll = 100;
  s.cmd(Cmd::START);
  s.in.luefter = true;
  s.in.abgas_c = 150;
  s.tick();
  CHECK(s.m.state == State::ANLAUF);
  for (int i = 0; i < 30 * 60 - 2; i++) {
    Outputs o = s.tick();
    CHECK(o.pwm == s.c.min_pwm);
    if (o.pwm != s.c.min_pwm) break;
  }
  CHECK(s.m.state == State::ANLAUF);
  CHECK(s.out.anlauf_rest_s <= 2);
  s.run_s(3);
  CHECK(s.m.state == State::BETRIEB);
}

// Regel 16: Rampe max. 5 %/min, nie unter Minimum
static void test_rampe() {
  Sim s;
  s.to_betrieb();
  s.in.leistung_soll = 100;
  s.run_s(60);
  CHECK(s.out.leistung > 4.9f && s.out.leistung < 5.1f);
  s.run_s(19 * 60);
  CHECK(s.out.leistung > 99.0f);
  CHECK(s.out.pwm <= s.c.max_pwm + 1e-6f);
  s.in.leistung_soll = 0;
  s.run_s(60);
  CHECK(s.out.leistung > 94.9f && s.out.leistung < 95.1f);
  s.run_s(30 * 60);
  CHECK(s.out.leistung == 0.0f);
  CHECK(s.out.pwm == s.c.min_pwm);
  s.in.leistung_soll = -50;
  s.run_s(60);
  CHECK(s.out.pwm >= s.c.min_pwm);
  CHECK(s.out.kw == 17.0f);
}

// Regel 7: Flamme aus absolut und relativ, Anlauf-Kriterium
static void test_flamme() {
  {
    Sim s;
    s.c.flamme_aus_c = 180;
    s.to_betrieb();
    s.in.abgas_c = 170;  // unter der Grenze, aber kein relativer Abfall > 40 %
    s.run_s(5);
    CHECK(s.m.state == State::BETRIEB);  // kurzer Ausreißer toleriert
    s.run_s(10);
    CHECK(s.m.state == State::ALARM);
    CHECK(s.m.alarm == Alarm::FLAMME_AUS);
    CHECK(s.out.pwm == 0.0f);
  }
  {
    Sim s;
    s.to_betrieb();
    s.in.abgas_c = 300;
    s.run_s(20 * 60);
    s.in.abgas_c = 170;  // > 40 % unter 300, aber über 120
    s.tick();
    CHECK(s.m.state == State::ALARM);
    CHECK(s.m.alarm == Alarm::FLAMME_AUS);
  }
  {
    Sim s;  // 30 % Abfall: kein Alarm
    s.to_betrieb();
    s.in.abgas_c = 300;
    s.run_s(20 * 60);
    s.in.abgas_c = 210;
    s.run_s(60);
    CHECK(s.m.state == State::BETRIEB);
  }
  {
    Sim s;  // Anlauf: Abgas erreicht 80 °C nicht in 30 min
    s.cmd(Cmd::START);
    s.in.luefter = true;
    s.in.abgas_c = 60;
    s.run_s(30 * 60 - 5);
    CHECK(s.m.state == State::ANLAUF);
    s.run_s(10);
    CHECK(s.m.state == State::ALARM);
    CHECK(s.m.alarm == Alarm::FLAMME_AUS);
  }
}

// Regel 8: Abgas > 450 °C für 60 s
static void test_uebertemp() {
  Sim s;
  s.to_betrieb();
  s.in.abgas_c = 460;
  s.run_s(55);
  CHECK(s.m.state == State::BETRIEB);
  s.run_s(10);
  CHECK(s.m.state == State::ALARM);
  CHECK(s.m.alarm == Alarm::ABGAS_UEBERTEMP);
  // Quittierung erst wenn abgekühlt und Kammer kalt
  s.in.luefter = false;
  CHECK(s.cmd(Cmd::ACK) == Reason::URSACHE);
  s.in.abgas_c = 100;
  CHECK(s.cmd(Cmd::ACK) == Reason::OK);
  CHECK(s.m.state == State::STOP);
}

// Regel 9: Lüfter fällt im Betrieb > 60 s ab
static void test_luefter() {
  Sim s;
  s.to_betrieb();
  s.in.luefter = false;
  s.run_s(55);
  CHECK(s.m.state == State::BETRIEB);
  s.run_s(10);
  CHECK(s.m.state == State::ALARM);
  CHECK(s.m.alarm == Alarm::LUEFTER);
  // Im Anzündfenster ist Lüfter aus normal
  Sim z;
  z.cmd(Cmd::START);
  z.run_s(5 * 60);
  CHECK(z.m.state == State::ANZUENDEN);
}

// Regel 10: Abgassensor fällt aus
static void test_sensor_abgas() {
  {
    Sim s;  // im Anzünden -> Alarm
    s.cmd(Cmd::START);
    s.in.abgas_c = NAN;
    s.run_s(15);
    CHECK(s.m.state == State::ALARM);
    CHECK(s.m.alarm == Alarm::SENSOR_ABGAS);
  }
  {
    Sim s;  // Start bei 0 °C erlaubt (MAX6675 in kalter Werkstatt), aber 5 min 0 °C im Anzünden = Sensorfehler
    s.in.abgas_c = 0;
    CHECK(s.cmd(Cmd::START) == Reason::OK);
    s.run_s(5 * 60 - 5);
    CHECK(s.m.state == State::ANZUENDEN);
    s.run_s(10);
    CHECK(s.m.state == State::ALARM);
    CHECK(s.m.alarm == Alarm::SENSOR_ABGAS);
  }
  {
    Sim s;  // im Anlauf: 0 °C dauerhaft -> Alarm
    reach(s, State::ANLAUF);
    s.in.abgas_c = 0;
    s.run_s(15);
    CHECK(s.m.alarm == Alarm::SENSOR_ABGAS);
  }
  {
    Sim s;  // im Betrieb -> Minimum + Warnung, nach 10 min auslöschen
    s.to_betrieb();
    s.in.leistung_soll = 100;
    s.run_s(10 * 60);
    float vorher = s.out.leistung;
    CHECK(vorher > 40.0f);
    s.in.abgas_c = 2000;
    s.run_s(60);
    CHECK(s.m.state == State::BETRIEB);
    CHECK(s.m.warn & W_ABGAS);
    CHECK(s.out.leistung < vorher);
    s.run_s(9 * 60);
    CHECK(s.m.state == State::BETRIEB);
    CHECK(s.out.leistung < 0.1f);
    s.run_s(61);
    CHECK(s.m.state == State::AUSLOESCHEN);
    CHECK(s.out.pwm == 0.0f);
    CHECK(s.m.warn & W_SENSOR_AUS);
  }
}

// Regel 10: Raumfühler fehlt -> Minimum, kein Stopp
static void test_raumfuehler() {
  Sim s;
  s.to_betrieb();
  s.in.leistung_soll = 100;
  s.run_s(10 * 60);
  s.in.raum_c = NAN;
  s.run_s(10 * 60 - 5);
  CHECK(!(s.m.warn & W_RAUM));
  s.run_s(10);
  CHECK(s.m.warn & W_RAUM);
  s.run_s(60 * 60);
  CHECK(s.m.state == State::BETRIEB);
  CHECK(s.out.leistung == 0.0f);
  CHECK(s.out.pwm == s.c.min_pwm);
}

// Regel 11: Display-Verbindung weg -> Minimum, kein Stopp
static void test_display_timeout() {
  Sim s;
  s.to_betrieb();
  s.in.leistung_soll = 100;
  s.run_s(10 * 60);
  s.display = false;
  s.run_s(65);
  CHECK(s.m.warn & W_DISPLAY);
  s.run_s(2 * 60 * 60);
  CHECK(s.m.state == State::BETRIEB);
  CHECK(s.out.pwm == s.c.min_pwm);
  s.display = true;
  s.tick();
  CHECK(!(s.m.warn & W_DISPLAY));
}

// Regel 12: Stopp -> sofort Pumpe aus, AUSLOESCHEN bis Lüfter aus
static void test_stopp() {
  Sim s;
  s.to_betrieb();
  CHECK(s.cmd(Cmd::STOP) == Reason::OK);
  CHECK(s.out.pwm == 0.0f);
  CHECK(s.m.state == State::AUSLOESCHEN);
  s.run_s(30 * 60);
  CHECK(s.m.state == State::AUSLOESCHEN);
  s.in.luefter = false;
  s.tick();
  CHECK(s.m.state == State::STOP);
}

// Regel 13: Raum zu warm 30 min trotz Minimum -> auslöschen, kein Neustart
static void test_raum_zu_warm() {
  Sim s;
  s.to_betrieb();
  s.in.leistung_soll = 0;
  s.in.raum_c = 23.5f;  // Soll 20 + 3,5
  s.run_s(30 * 60 - 5);
  CHECK(s.m.state == State::BETRIEB);
  s.run_s(10);
  CHECK(s.m.state == State::AUSLOESCHEN);
  CHECK(s.m.warn & W_RAUM_WARM);
  s.in.luefter = false;
  s.in.raum_c = 15;
  s.in.leistung_soll = 100;
  s.run_s(3 * 60 * 60);
  CHECK(s.m.state == State::STOP);
  CHECK(s.out.pwm == 0.0f);
  // Solange Leistung über Minimum: kein Auslöschen (Regler regelt erst herunter)
  Sim t;
  t.to_betrieb();
  t.in.leistung_soll = 100;
  t.run_s(20 * 60);
  t.in.raum_c = 24;
  t.run_s(25 * 60);  // Rampe noch nicht bei 0, Wunsch weiter 100
  CHECK(t.m.state == State::BETRIEB);
}

// Regel 14: Quittierung nur bei kalter Kammer, geschlossener Kette, Ursache weg; danach STOP
static void test_quittierung() {
  Sim s;
  s.to_betrieb();
  s.in.chain = Chain::STB;
  s.tick();
  CHECK(s.m.alarm == Alarm::KETTE_STB);
  CHECK(s.cmd(Cmd::ACK) == Reason::ZUSTAND || s.m.state == State::ALARM);
  CHECK(s.m.state == State::ALARM);
  s.in.chain = Chain::OK;
  CHECK(s.cmd(Cmd::ACK) == Reason::KAMMER_WARM);  // Lüfter noch an
  CHECK(s.m.state == State::ALARM);
  s.in.luefter = false;
  s.in.chain = Chain::UEBERLAUF;
  CHECK(s.cmd(Cmd::ACK) != Reason::OK);
  s.in.chain = Chain::OK;
  CHECK(s.cmd(Cmd::ACK) == Reason::OK);
  CHECK(s.m.state == State::STOP);
  CHECK(s.m.alarm == Alarm::KEINER);
  CHECK(s.out.pwm == 0.0f);
  CHECK(s.cmd(Cmd::ACK) == Reason::ZUSTAND);
}

// Kalibrierung Pumpe: nur aus STOP bei kalter Kammer, max. 20 min, abbrechbar
static void test_kalibrierung_pumpe() {
  {
    Sim s;
    s.in.luefter = true;
    CHECK(s.cmd(Cmd::KAL, 0.3f) == Reason::KAMMER_WARM);
  }
  {
    Sim s;
    CHECK(s.cmd(Cmd::KAL, 1.5f) == Reason::WERT);
    CHECK(s.cmd(Cmd::KAL, 0.3f) == Reason::OK);
    CHECK(s.out.pwm == 0.3f);
    CHECK(s.cmd(Cmd::KAL, 0.45f) == Reason::OK);
    CHECK(s.out.pwm == 0.45f);
    s.run_s(20 * 60 - 5);
    CHECK(s.m.state == State::KALIBRIERUNG_PUMPE);
    s.run_s(10);
    CHECK(s.m.state == State::STOP);
    CHECK(s.out.pwm == 0.0f);
  }
  {
    Sim s;
    s.cmd(Cmd::KAL, 0.3f);
    CHECK(s.cmd(Cmd::STOP) == Reason::OK);
    CHECK(s.m.state == State::STOP);
    CHECK(s.out.pwm == 0.0f);
  }
  {
    Sim s;  // Display weg -> Abbruch
    s.cmd(Cmd::KAL, 0.3f);
    s.display = false;
    s.run_s(65);
    CHECK(s.m.state == State::STOP);
  }
  {
    Sim s;  // im Betrieb nicht erlaubt
    s.to_betrieb();
    CHECK(s.cmd(Cmd::KAL, 0.3f) == Reason::ZUSTAND);
  }
  {
    Sim s;  // Abgas warm -> kein Start der Kalibrierung
    s.in.abgas_c = 90;
    CHECK(s.cmd(Cmd::KAL, 0.3f) == Reason::ABGAS);
  }
  // Abbruch durch den NodeMCU selbst: Abgas warm, Sensorfehler, Raum > 26 °C
  const float abgas[] = {120.0f, NAN, 20.0f};
  const float raum[] = {18.0f, 18.0f, 27.0f};
  for (int i = 0; i < 3; i++) {
    Sim s;
    CHECK(s.cmd(Cmd::KAL, 0.3f) == Reason::OK);
    s.in.abgas_c = abgas[i];
    s.in.raum_c = raum[i];
    CHECK(s.tick().pwm == 0.0f);
    CHECK(s.m.state == State::STOP);
  }
}

static void test_set_calibration() {
  Config c;
  CHECK(set_calibration(c, "min_pwm", 0.4f, State::BETRIEB) == Reason::ZUSTAND && c.min_pwm == 0.35f);
  CHECK(set_calibration(c, "max_pwm", 0.9f, State::ANLAUF) == Reason::ZUSTAND);
  CHECK(set_calibration(c, "flamme_aus_c", 110, State::BETRIEB) == Reason::OK);  // Flammengrenze lernt im Betrieb
  CHECK(set_calibration(c, "abgas_max_c", INFINITY) == Reason::WERT);
  // Boot: Werte aus dem Flash werden ebenso geprüft (erst max, dann min)
  Config boot;
  CHECK(set_calibration(boot, "max_pwm", NAN) == Reason::WERT && boot.max_pwm == 0.80f);
  CHECK(set_calibration(boot, "min_pwm", 2.0f) == Reason::WERT && boot.min_pwm == 0.35f);
  CHECK(set_calibration(c, "min_pwm", 0.4f) == Reason::OK && c.min_pwm == 0.4f);
  CHECK(set_calibration(c, "min_pwm", 0.9f) == Reason::WERT && c.min_pwm == 0.4f);
  CHECK(set_calibration(c, "max_pwm", 1.2f) == Reason::WERT);
  CHECK(set_calibration(c, "flamme_aus_c", 100) == Reason::OK && c.flamme_aus_c == 100);
  CHECK(set_calibration(c, "abgas_max_c", NAN) == Reason::WERT);
  CHECK(set_calibration(c, "abgas_max", 300) == Reason::UNBEKANNT);
}

// Protokoll: Roundtrip, CRC-Fehler, Müll, abgeschnittene Zeilen, Überlauf
static void test_protokoll() {
  CHECK(crc8("123456789", 9) == 0xF4);  // CRC-8/SMBUS Prüfwert

  StatusMsg s;
  s.seq = 42; s.state = 3; s.alarm = 0; s.pwm_promille = 512; s.kw = 25.3f; s.abgas_c = 231.7f;
  s.kette = 0; s.luefter = 1; s.anlauf_rest_s = 0; s.uptime_s = 123456; s.warn = W_RAUM;
  std::string line = encode(s);
  CHECK(line.back() == '\n');
  Fields f;
  CHECK(parse_line(line.c_str(), f));
  StatusMsg r;
  CHECK(decode(f, r));
  CHECK(r.seq == 42 && r.state == 3 && r.pwm_promille == 512 && r.uptime_s == 123456 && r.warn == W_RAUM);
  CHECK(std::fabs(r.kw - 25.3f) < 0.01f && std::fabs(r.abgas_c - 231.7f) < 0.01f);

  s.abgas_c = NAN;
  CHECK(parse_line(encode(s).c_str(), f) && decode(f, r) && std::isnan(r.abgas_c));

  HeartbeatMsg h{7, 55, 21.4f, NAN};
  CHECK(parse_line(encode(h).c_str(), f));
  HeartbeatMsg hr;
  CHECK(decode(f, hr) && hr.seq == 7 && hr.leistung == 55 && std::fabs(hr.raum_c - 21.4f) < 0.01f && std::isnan(hr.soll_c));
  CHECK(!decode(f, r));  // falscher Typ

  CommandMsg c{'C', 9, "START", ""};
  CHECK(encode(c) == frame("C;9;START"));
  CommandMsg cr;
  CHECK(parse_line(encode(c).c_str(), f) && decode(f, cr) && cr.type == 'C' && cr.seq == 9 && cr.name == "START");
  CommandMsg k{'K', 10, "min_pwm", "0.37"};
  CHECK(parse_line(encode(k).c_str(), f) && decode(f, cr) && cr.name == "min_pwm" && cr.arg == "0.37");
  CommandMsg a{'A', 9, "ERR", "KAMMER_WARM"};
  CHECK(parse_line(encode(a).c_str(), f) && decode(f, cr) && cr.type == 'A' && cr.name == "ERR" && cr.arg == "KAMMER_WARM");

  // kaputte Prüfsumme
  std::string bad = encode(h);
  bad[2] = bad[2] == '8' ? '9' : '8';
  CHECK(!parse_line(bad.c_str(), f));
  std::string badcrc = encode(h);
  badcrc[badcrc.size() - 2] = badcrc[badcrc.size() - 2] == '0' ? '1' : '0';
  CHECK(!parse_line(badcrc.c_str(), f));
  // abgeschnitten
  std::string cut = encode(s);
  CHECK(!parse_line(cut.substr(0, cut.size() / 2).c_str(), f));
  CHECK(!parse_line(cut.substr(0, cut.size() - 2).c_str(), f));
  // Müll
  CHECK(!parse_line("", f));
  CHECK(!parse_line("*00", f));
  CHECK(!parse_line("hallo welt", f));
  CHECK(!parse_line("\xff\xfe\x01*ZZ", f));
  // gültige CRC, aber kaputter Inhalt
  CHECK(parse_line(frame("H;1;abc;NA;NA").c_str(), f) && !decode(f, hr));
  CHECK(parse_line(frame("H;1;150;NA;NA").c_str(), f) && !decode(f, hr));  // Leistung > 100
  CHECK(parse_line(frame("S;1;2").c_str(), f) && !decode(f, r));
  CHECK(parse_line(frame("K;1;min_pwm").c_str(), f) && !decode(f, cr));
  CHECK(!parse_line(frame("S;1;2;3;4;5;6;7;8;9;10;11;12;13").c_str(), f));  // zu viele Felder

  // LineBuffer: Überlauf verwerfen, danach weiter
  LineBuffer lb;
  int lines = 0;
  for (int i = 0; i < 300; i++) lines += lb.feed('x');
  lines += lb.feed('\n');
  CHECK(lines == 0);
  std::string ok = encode(c);
  bool got = false;
  for (char ch : ok) got = lb.feed(ch);
  CHECK(got && parse_line(lb.line(), f) && decode(f, cr) && cr.name == "START");
  CHECK(!lb.feed('\n'));  // Leerzeile
}

static void test_zeitprogramm_text() {
  Zeitfenster tag[4];
  CHECK(parse_tag("06:00-08:00=21 17:00-22:30=21.5", tag));
  CHECK(tag[0].aktiv && tag[0].von_min == 360 && tag[0].bis_min == 480 && tag[0].soll == 21.0f);
  CHECK(tag[1].aktiv && tag[1].bis_min == 1350 && tag[1].soll == 21.5f);
  CHECK(!tag[2].aktiv && !tag[3].aktiv);
  CHECK(parse_tag("20:00-24:00=19", tag) && tag[0].bis_min == 1440 && !tag[1].aktiv);
  CHECK(parse_tag("", tag) && !tag[0].aktiv);  // leer = ganzer Tag Absenkung
  tag[0] = {60, 120, 20.0f, true};
  CHECK(!parse_tag("08:00-06:00=21", tag));  // Ende vor Beginn
  CHECK(!parse_tag("06:00-08:00=40", tag));  // Sollwert unplausibel
  CHECK(!parse_tag("06:00-08:00", tag));
  CHECK(!parse_tag("1:00-2:00=20 3:00-4:00=20 5:00-6:00=20 7:00-8:00=20 9:00-10:00=20", tag));  // > 4 Fenster
  CHECK(tag[0].aktiv && tag[0].von_min == 60);  // bei Fehler unverändert
}

static void test_pi() {
  PiRegler pi;
  CHECK(pi.update(20, 20.2f, 30, true) == 0.0f);  // Totband
  float u = pi.update(20, 19, 30, true);
  CHECK(u > 20.0f && u < 21.0f);
  for (int i = 0; i < 1000; i++) pi.update(20, 10, 30, true);
  CHECK(pi.i == 100.0f);  // Anti-Windup
  float i_vorher = pi.i;
  pi.update(20, 30, 30, false);  // eingefroren
  CHECK(pi.i == i_vorher);
  CHECK(pi.update(20, 30, 30, true) >= 0.0f);
  PiRegler p2;
  CHECK(p2.update(20, NAN, 30, true) == 0.0f);
}

static void test_regression() {
  // Ideale Pumpe: kg/h = 4 * pwm - 0.2 ; ml je 3 min = kg/h / 0.85 * 1000 / 20
  float pwm[5] = {0.30f, 0.45f, 0.60f, 0.75f, 0.90f}, ml[5];
  for (int i = 0; i < 5; i++) ml[i] = (4.0f * pwm[i] - 0.2f) / 0.85f * 1000.0f / 20.0f;
  PumpenKalibrierung k = pumpe_kalibrieren(pwm, ml, 5, 3.0f, 0.85f);
  CHECK(k.ok);
  CHECK(std::fabs(k.pwm_17 - (1.46f + 0.2f) / 4.0f) < 0.001f);
  CHECK(std::fabs(k.pwm_33 - (2.80f + 0.2f) / 4.0f) < 0.001f);
  float flat[5] = {10, 10, 10, 10, 10};
  CHECK(!pumpe_kalibrieren(pwm, flat, 5, 3.0f, 0.85f).ok);
  CHECK(flamme_aus_vorschlag(250) == 150.0f);
  CHECK(flamme_aus_vorschlag(100) == 80.0f);
}

static void test_sprung() {
  // PT1 mit Totzeit: K = 0.05 K/%, T = 40 min, L = 10 min, Sprung 100 %
  float t[91], y[91];
  for (int i = 0; i <= 90; i++) {
    t[i] = (float) i;
    float tt = t[i] - 10.0f;
    y[i] = 18.0f + (tt > 0 ? 5.0f * (1.0f - std::exp(-tt / 40.0f)) : 0.0f);
  }
  // Endwert nach 90 min noch nicht erreicht -> Verfahren nutzt den letzten Wert; nur Plausibilität prüfen
  Strecke s = sprung_auswerten(t, y, 91, 100.0f);
  CHECK(s.ok);
  CHECK(s.t_min > 0 && s.kp > 0 && s.ti_min == s.t_min);
  // Mit eingeschwungener Kurve exakt
  float t2[400], y2[400];
  for (int i = 0; i < 400; i++) {
    t2[i] = (float) i;
    float tt = t2[i] - 10.0f;
    y2[i] = 18.0f + (tt > 0 ? 5.0f * (1.0f - std::exp(-tt / 40.0f)) : 0.0f);
  }
  Strecke e = sprung_auswerten(t2, y2, 400, 100.0f);
  CHECK(e.ok && std::fabs(e.t_min - 40.0f) < 1.5f && std::fabs(e.l_min - 10.0f) < 1.5f && std::fabs(e.k - 0.05f) < 0.001f);
}

// millis() läuft nach 49 Tagen über: alle Zeiten müssen über den Überlauf hinweg stimmen
static void test_millis_ueberlauf() {
  Sim s;
  s.now = 0xFFFFFFFFu - 10u * 60000u;
  s.tick();
  s.to_betrieb();  // Überlauf mitten im Anlauf
  s.in.leistung_soll = 100;
  s.run_s(60);
  CHECK(s.out.leistung > 4.9f && s.out.leistung < 5.1f);
  Sim z;
  z.now = 0xFFFFFFFFu - 5u * 60000u;
  z.cmd(Cmd::START);
  z.run_s(20 * 60 - 5);
  CHECK(z.m.state == State::ANZUENDEN);
  z.run_s(10);
  CHECK(z.m.alarm == Alarm::ZUENDUNG);
}

int main() {
  test_neustart();
  test_kein_autostart();
  test_start_bedingungen();
  test_zuendung();
  test_kette_jeder_zustand();
  test_a0();
  test_anlauf();
  test_rampe();
  test_flamme();
  test_uebertemp();
  test_luefter();
  test_sensor_abgas();
  test_raumfuehler();
  test_display_timeout();
  test_stopp();
  test_raum_zu_warm();
  test_quittierung();
  test_kalibrierung_pumpe();
  test_set_calibration();
  test_millis_ueberlauf();
  test_protokoll();
  test_zeitprogramm_text();
  test_pi();
  test_regression();
  test_sprung();
  printf("%d Prüfungen, %d Fehler\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
