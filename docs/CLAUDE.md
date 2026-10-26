# Auftrag: Firmware für die Steuerung des Altölofens Master WA 33 C

Du baust die komplette Firmware für eine selbst gebaute Ofensteuerung auf Basis von **ESPHome**. Die Hardware ist bereits geplant und wird gerade aufgebaut. Lies diese Datei vollständig, bevor du anfängst. Die Sicherheitsregeln in Abschnitt 4 haben Vorrang vor allem anderen.

Sprache für Oberfläche, Logs und Kommentare: **Deutsch**. Code-Bezeichner dürfen englisch sein.

---

## 1. Ziel und Ergebnis

Am Ende gibt es in diesem Repository:

| Datei / Ordner | Inhalt |
| --- | --- |
| `ofen-controller.yaml` | ESPHome-Konfiguration für den **NodeMCU (ESP8266)**: Ablaufsteuerung, Pumpe, Sensoren, Sicherheit |
| `ofen-display.yaml` | ESPHome-Konfiguration für das **Waveshare ESP32-S3-Touch-LCD-7**: Touch-Oberfläche (LVGL), Raumregelung, Zeitprogramm, Kalibrierassistent, Webinterface |
| `raumfuehler.yaml` | ESPHome-Konfiguration für den **ESP32-C3 Super Mini** mit DS18B20 (Funk-Raumfühler) |
| `components/ofen_link/` | External Component: serielles Protokoll zwischen NodeMCU und Display (beide Seiten) |
| `components/ofen_logic/` | Reine C++-Logik ohne Hardwarebezug (Zustandsautomat, Statuserkennung, Regler, Plausibilität) als Header, von beiden YAMLs genutzt |
| `test/` | Host-Unit-Tests für `ofen_logic` (g++ oder CMake + einfache Assert-Tests, kein ESP nötig) |
| `secrets.yaml.example` | Vorlage für WLAN, OTA-Passwort, Web-Passwort, ntfy-Topic, ESP-NOW-/Transport-Schlüssel |
| `README.md` | Flashen, Inbetriebnahme, Kalibrierung, Fehlersuche – für einen Laien verständlich |

Alle drei YAMLs müssen mit der aktuellen ESPHome-Version **fehlerfrei kompilieren** (`esphome compile`). Die Unit-Tests müssen grün sein. Prüfe Komponenten-Syntax immer gegen die aktuelle Doku auf esphome.io, nicht aus dem Gedächtnis; ESPHome ändert Syntax regelmäßig.

---

## 2. Das Gerät

- **Master WA 33 C**: Altöl-Topfbrenner, 17–33 kW (1,46–2,8 kg/h Öl), keine automatische Zündung, kein Flammenwächter ab Werk. Wird von Hand mit Papier angezündet.
- Ablauf laut Hersteller: Anzünden → warten, bis die Brennkammer warm ist (T40 schließt nach 10–15 min) → **mindestens 30 min auf Minimalleistung (17 kW)** → danach Leistung frei regelbar → Stopp → Lüfter läuft nach, bis die Kammer unter 35 °C ist.
- Das originale Steuermodul existiert nicht mehr. Diese Steuerung ersetzt es vollständig.

---

## 3. Hardware

Die gesamte Elektronik sitzt in einem **Schaltkasten entfernt vom Ofen**, das Display ist auf dem Kasten montiert. Zum Ofen laufen Leitungen.

### 3.1 Was ohne Software passiert (nicht anfassen, nur kennen)

- **Lüfter:** Der Bimetallschalter **T40** an der Brennkammer schaltet den 230-V-Lüfter direkt (zu ab 40 °C, offen unter 35 °C). Die Software kann den Lüfter nicht schalten.
- **Pumpenkreis:** +12 V → Feinsicherung F3 → **STB** (öffnet bei 90 °C, Reset von Hand) → Knoten **S1** → **Überlaufschalter** (NC) → Knoten **S2** → Pumpe +. Pumpe − wird vom MOSFET-Modul Q1 gegen GND geschaltet. Öffnet STB oder Überlauf, ist die Pumpe stromlos, egal was die Software tut.
- **Q1 (D4184-Modul mit Optokoppler):** Eingang ohne Signal = aus. 10-kΩ-Pull-down am Eingang.

### 3.2 NodeMCU Amica V2 (ESP8266) – Ofen-Controller

| Pin | GPIO | Funktion | Details |
| --- | --- | --- | --- |
| D1 | 5 | Pumpe PWM → Q1 | `esp8266_pwm`, Frequenz ca. 1 kHz, beim Booten LOW |
| D0 | 16 | Eingang „Lüfter hat Spannung“ von OK1 | 230-V-Optokoppler-Modul, Ausgang meist **aktiv LOW**, Pull-up auf dem Modul. Bedeutet zugleich „Brennkammer > 40 °C“ (T40 zu). 1 s entprellen. GPIO16 hat keinen Interrupt – Polling ist ok. |
| A0 | ADC | Statuserkennung STB/Überlauf | Spannungsteiler, siehe 3.3 |
| D5 | 14 | MAX6675 SCK | Abgastemperatur, Thermoelement Typ K im Rauchrohr |
| D6 | 12 | MAX6675 SO (MISO) | |
| D7 | 13 | MAX6675 CS | |
| TX / RX | 1 / 3 | UART zum RS485-Modul U2 (automatische Richtungsumschaltung) | **Logger auf UART aus** (`logger: baud_rate: 0`), Logs nur per Web/OTA |
| D4 | 2 | optional: DS18B20 am Kabel als Rückfall-Raumfühler | 4,7 kΩ Pull-up; Board-LED hängt mit dran. Als optionale Komponente vorsehen (auskommentiert). |
| D2, D3, D8 | 4, 0, 15 | frei | D3/D8 sind Boot-Pins, nicht verwenden |

Versorgung: 5 V aus Mean Well HDR-15-5 an VIN.

### 3.3 Statuserkennung an A0

R1 = R2 = **75 kΩ** von S1 bzw. S2 nach A0, R3 = 10 kΩ nach GND, C1 = 100 nF. Der NodeMCU hat intern 220k/100k vor dem ADC; ESPHome liefert mit `platform: adc, pin: A0` direkt die Spannung 0–3,2 V (ggf. `raw: false` prüfen).

| Zustand | Spannung an A0 (12,0–13,2 V Netzteil) | Schwelle |
| --- | --- | --- |
| Alles zu (STB und Überlauf geschlossen) | 2,47–2,71 V | **> 2,0 V** |
| Überlauf offen (S1 hat 12 V, S2 nicht) | 1,23–1,51 V | **0,8–1,8 V** |
| STB offen oder F3 durchgebrannt | ~0 V | **< 0,5 V** |
| dazwischen | Leitungs- oder Bauteilfehler | **Fehler → Pumpe aus** |

Die Schwellen als `substitutions` bzw. Konstanten zentral ablegen, weil eventuell 68 kΩ oder 82 kΩ verbaut werden. Mittelwert über 1 s (z. B. 10 Messungen à 100 ms). Ein Zustandswechsel gilt erst, wenn er 2 s stabil ist; ein Wechsel **zu einem Fehler** wirkt aber sofort auf die Pumpe (Pumpe aus ohne Wartezeit).

### 3.4 Waveshare ESP32-S3-Touch-LCD-7 – Display

- 7″, 800 × 480, RGB-Panel (ST7262), Touch GT911 über I²C, IO-Expander **CH422G** (steuert u. a. Backlight und Touch-Reset), 16 MB Flash, 8 MB PSRAM, RS485-Transceiver auf der Platine.
- ESPHome: `display: platform: rpi_dpi_rgb` (bzw. die aktuell dafür vorgesehene Plattform), `touchscreen: platform: gt911`, `ch422g`, `psram`, `lvgl`. Es gibt eine Community-Konfiguration für genau dieses Board – such sie und nutze die Pinbelegung von dort, prüfe sie gegen das Waveshare-Wiki.
- Verbindung zum NodeMCU über den **eingebauten RS485-Anschluss** (A/B/GND), kurze verdrillte Leitung im Schaltkasten. Ermittle aus Schaltplan/Wiki, an welchem UART und welchen GPIOs der Onboard-Transceiver hängt und ob er Richtungsumschaltung braucht.
- Versorgung über USB-C aus dem HDR-15-5.
- WLAN für Webinterface, Zeit (SNTP) und Push. Das Display ist der einzige Teil mit WLAN-Pflicht; der NodeMCU darf WLAN haben, darf aber nie davon abhängen.

### 3.5 ESP32-C3 Super Mini – Funk-Raumfühler

- DS18B20 an **GPIO4**, 4,7 kΩ Pull-up (R5) nach 3V3. Versorgung über USB-Ladegerät.
- Sendet die Raumtemperatur ca. alle 30 s an das Display.
- Übertragung: bevorzugt **ESP-NOW** (funktioniert ohne Router). Prüfe, was die aktuelle ESPHome-Version bietet: `espnow`-Komponente und/oder `packet_transport` (Sensorwerte zwischen ESPHome-Knoten teilen, verschlüsselt). Wähle die am einfachsten wartbare Variante, die ESPHome nativ unterstützt; begründe die Wahl im README. Achtung: ESP-NOW und das WLAN des Displays müssen auf demselben Kanal laufen – Kanal fest einstellen und dokumentieren.
- Optional später Batteriebetrieb mit Deep Sleep: Code so vorbereiten (auskommentiert), nicht aktivieren.

---

## 4. Sicherheitsregeln (verbindlich)

Diese Regeln sind das Wichtigste am ganzen Projekt. Jede Regel braucht mindestens einen Unit-Test in `test/`.

1. **Keine automatische Wiederinbetriebnahme.** Der Ofen hat keine Zündung. Die Pumpe darf **nur** nach einem ausdrücklichen „Start“ durch einen Menschen (Touch oder Webinterface) laufen. Nach jedem Neustart des NodeMCU, jedem Stromausfall, jedem Alarm: Pumpe aus, Zustand `STOP` oder `GESPERRT`. Kein Zeitprogramm, kein Thermostat, keine Fernbedienung darf die Pumpe aus dem Stillstand einschalten.
2. **Die Entscheidung über die Pumpe trifft allein der NodeMCU.** Das Display schickt nur Wünsche (Start, Stopp, Leistung 0–100 %). Der NodeMCU prüft jeden Wunsch gegen Zustand und Sicherheitskette.
3. **Sicherheitskette offen → Pumpe sofort aus** (PWM 0), Zustand `ALARM`. Gilt auch für undefinierte A0-Werte.
4. **Start nur erlaubt**, wenn: A0 = „alles zu“ **und** Lüfter-Eingang = aus (Kammer kalt) **und** kein Alarm offen **und** Abgastemperatur plausibel (< 80 °C, Sensor liefert Werte).
5. **Anzünd-Zeitfenster:** Nach „Start“ läuft die Pumpe auf Minimum. Meldet D0 nicht innerhalb von **20 min** „Lüfter an“ (Kammer warm), → Pumpe aus, Alarm „Zündung fehlgeschlagen“.
6. **Anlaufphase:** Nach „Lüfter an“ mindestens **30 min Minimalleistung** (17 kW), Regler eingefroren. Erst danach Regelbetrieb.
7. **Flammenüberwachung über Abgas (MAX6675)** im Regelbetrieb: Fällt die Abgastemperatur unter den kalibrierten Grenzwert „Flamme aus“ (Standard 120 °C, aus Kalibrierung überschreibbar) oder sinkt sie in 5 min um mehr als 40 % gegenüber dem Mittel der letzten 15 min → Pumpe aus, Alarm „Flamme aus“. In der Anlaufphase gilt ein vereinfachtes Kriterium: Abgas muss innerhalb von 30 min nach Start über 80 °C steigen.
8. **Abgas-Übertemperatur:** Abgas > 450 °C (konfigurierbar) für 60 s → Pumpe aus, Alarm.
9. **Lüfter-Plausibilität:** Pumpe läuft, aber D0 meldet länger als 60 s „Lüfter aus“ (außerhalb des Anzündfensters) → Pumpe aus, Alarm „Lüfter/T40“.
10. **Sensorausfälle:**
    - MAX6675 liefert keinen Wert / offenes Thermoelement (NaN, 0 °C dauerhaft, > 1000 °C): im Anzünden/Anlauf → Pumpe aus, Alarm. Im Regelbetrieb → auf Minimalleistung zurück, Warnung; nach 10 min ohne Wert → kontrolliert auslöschen.
    - Raumfühler fehlt > 10 min: Leistung auf Minimum, Warnung „Raumfühler fehlt“. **Kein Abschalten** (Ofen würde ausgehen, Neustart nur von Hand).
11. **Verbindung Display ↔ NodeMCU verloren** > 60 s: NodeMCU regelt auf Minimum, läuft weiter, Statusanzeige über LED blinken (D4, falls nicht für DS18B20 belegt) und Web-Log. Kein Abschalten.
12. **Stopp:** Pumpe sofort aus. Zustand `AUSLOESCHEN` bis D0 „Lüfter aus“ meldet (Kammer < 35 °C), dann `STOP`. Während `AUSLOESCHEN` ist Start gesperrt.
13. **Überhitzung Raum:** Raum > Soll + 3 K für > 30 min trotz Minimalleistung → kontrolliert auslöschen (wie Stopp), Meldung. Danach kein automatischer Neustart.
14. **Alarm-Quittierung** nur, wenn Ursache behoben **und** Kammer kalt (D0 aus). Nach Quittierung Zustand `STOP`, nicht `BETRIEB`.
15. **Watchdog / Boot:** PWM-Ausgang wird in `on_boot` mit höchster Priorität auf 0 gesetzt, bevor irgendetwas anderes passiert. `restore_mode`/`restore_value` nirgends so, dass ein Betriebszustand über Neustarts gerettet wird. `reboot_timeout: 0s` für WLAN und API (keine Neustarts wegen fehlender Verbindung). **Kein `api:` ohne `reboot_timeout: 0s`.**
16. Leistungsänderungen höchstens **5 % pro Minute** (Rampe), nie unter das kalibrierte Minimum, solange die Pumpe läuft.

---

## 5. Zustandsautomat (NodeMCU, in `ofen_logic`)

```
STOP ──Start (Bedingungen 4.4)──▶ ANZUENDEN ──D0 an──▶ ANLAUF ──30 min──▶ BETRIEB
  ▲                                 │ 20 min ohne D0        │                 │
  │                                 ▼                       ▼                 ▼
  │                              ALARM ◀──────── Sicherheitsverletzung ───────┘
  │                                 │
  │                     Quittierung (Ursache weg, D0 aus)
  │                                 ▼
  └──────── D0 aus ──── AUSLOESCHEN ◀── Stopp / Raum zu warm / Sensor-Timeout
```

- Zustände: `STOP`, `ANZUENDEN`, `ANLAUF`, `BETRIEB`, `AUSLOESCHEN`, `ALARM`.
- `ALARM` merkt sich den Grund (Enum + Text). Pumpe in ALARM, AUSLOESCHEN und STOP immer 0.
- Die Logik ist eine reine Funktion `step(state, inputs, now_ms) -> (state, outputs)` ohne ESPHome-Abhängigkeit, damit sie auf dem PC testbar ist. ESPHome-Code ruft sie jede Sekunde auf.

---

## 6. Protokoll Display ↔ NodeMCU (`ofen_link`)

- UART/RS485, 9600 oder 19200 Baud, 8N1. Zeilenbasiert, ASCII, Ende `\n`, Prüfsumme CRC-8 als zwei Hex-Zeichen am Ende. Ungültige Zeilen verwerfen.
- **NodeMCU → Display**, jede Sekunde:
  `S;<seq>;<zustand>;<alarmcode>;<pwm_promille>;<leistung_kw*10>;<abgas_c*10>;<kette 0/1/2/3>;<luefter 0/1>;<anlauf_rest_s>;<uptime_s>*<crc>`
- **Display → NodeMCU**:
  - `H;<seq>;<leistung_soll_prozent>;<raum_c*10 oder NA>*<crc>` Heartbeat + Leistungswunsch, alle 2 s
  - `C;<seq>;START*<crc>`, `C;<seq>;STOP*<crc>`, `C;<seq>;ACK*<crc>` (Alarm quittieren)
  - `K;<seq>;<schluessel>;<wert>*<crc>` Kalibrierwerte setzen (min_pwm, max_pwm, flamme_aus_c, abgas_max_c)
- Jeder Befehl wird mit `A;<seq>;OK|ERR;<grund>*<crc>` beantwortet. Das Display zeigt abgelehnte Befehle mit Grund an („Start gesperrt: Kammer noch warm“).
- Kalibrierwerte speichert der **NodeMCU** im Flash (`globals` mit `restore_value: yes` ist hier erlaubt – es sind keine Betriebszustände). Defaults: `min_pwm = 0.35`, `max_pwm = 0.80`, `flamme_aus_c = 120`, `abgas_max_c = 450`.
- Encoder/Decoder als reine C++-Funktionen in `ofen_logic` oder `ofen_link`, mit Unit-Tests (Roundtrip, kaputte Prüfsumme, abgeschnittene Zeile, Überlauf).

---

## 7. Regelung (Display)

- Raumtemperatur vom Funkfühler, Sollwert vom Benutzer bzw. Zeitprogramm.
- **PI-Regler**, kein D-Anteil. Ausgang 0–100 % = 17–33 kW. Anti-Windup: I-Anteil auf Stellbereich begrenzen und einfrieren, solange der NodeMCU nicht `BETRIEB` meldet. Totband ±0,3 K. Abtastung 30 s.
- Startwerte: Kp = 20 %/K, Ti = 30 min (werden durch den Kalibrierassistenten ersetzt).
- Zeitprogramm: pro Wochentag bis zu 4 Zeitfenster mit eigener Solltemperatur, außerhalb „Absenkung“ (Standard 12 °C). **Das Zeitprogramm ändert nur den Sollwert, startet aber nie den Ofen.** Ist der Ofen aus, zeigt das Display bei einem beginnenden Zeitfenster nur einen Hinweis und optional eine Push-Nachricht „Heizzeit beginnt – Ofen anzünden?“.
- Die Regelung darf nichts tun, was Abschnitt 4 widerspricht; im Zweifel entscheidet der NodeMCU.

---

## 8. Kalibrierassistent (Display, Einstellungen → Kalibrierung, PIN-geschützt)

1. **Pumpenkennlinie** (Ofen kalt, Ölleitung im Messbecher): Assistent erklärt die Vorbereitung, fährt nacheinander 5 PWM-Stufen (z. B. 30, 45, 60, 75, 90 %) je 3 min. Diese Funktion darf die Pumpe ohne Flamme laufen lassen – **nur** in einem eigenen Zustand `KALIBRIERUNG_PUMPE` auf dem NodeMCU, nur bei kalter Kammer, mit Bestätigungsdialog „Ölleitung steckt im Messbecher“, Abbruch jederzeit, Gesamtdauer max. 20 min, danach automatisch Pumpe aus. Benutzer gibt pro Stufe die gemessene Menge in ml ein. Daraus lineare Regression → PWM für 1,46 kg/h (17 kW) und 2,8 kg/h (33 kW); Öldichte als Einstellwert (Standard 0,85 kg/l für Heizöl, Altöl ca. 0,88–0,90). Ergebnis anzeigen, erst nach „Übernehmen“ an den NodeMCU senden. Vorher-Werte für „Wiederherstellen“ behalten.
2. **Flammenerkennung** (läuft im Hintergrund im Regelbetrieb auf Minimum): mittlere Abgastemperatur über 20 min aufzeichnen → Vorschlag `flamme_aus_c = 60 % dieses Mittelwerts`, mindestens 80 °C.
3. **Regelstrecke** (Sprungversuch): nach ≥ 60 min stabilem Betrieb auf Minimum Sprung auf 100 %, Raumtemperatur 90 min aufzeichnen → Totzeit L, Zeitkonstante T, Verstärkung K (Wendetangenten- oder 2-Punkt-Methode) → Lambda-Tuning: `Kp = T / (K · (λ + L))`, `Ti = T`, `λ = T`. Ergebnis anzeigen, Übernehmen/Verwerfen.
4. **Abbruch** jedes Schritts bei Alarm, Raum > 26 °C, Laufzeit > 3 h oder Sensorausfall.

---

## 9. Touch-Oberfläche (LVGL, 800 × 480)

Große, mit Handschuhen bedienbare Elemente (min. 60 px Touchfläche), hoher Kontrast, Werkstatt-tauglich. Bildschirme:

- **Übersicht:** Raumtemperatur groß, Sollwert mit −/+ (0,5 K), Leistungsbalken 17–33 kW mit aktuellem Wert, Zustand als farbige Kachel (Stop grau, Anzünden/Anlauf orange mit Restzeit, Betrieb grün, Auslöschen blau, Alarm rot), Abgastemperatur, Status Sicherheitskette (STB / Überlauf), Lüfter an/aus, nächstes Zeitfenster, Betriebsstunden seit letzter Tellerreinigung (Zähler mit Reset).
- **Start-Dialog:** nur aktiv, wenn Start erlaubt; Text „Ofen angezündet? Papier brennt?“ mit großem Start-Knopf. Sonst Grund der Sperre anzeigen.
- **Alarm:** roter Vollbildschirm mit Grund, Handlungsanweisung (z. B. „STB prüfen und am Ofen zurücksetzen“), Quittier-Knopf, aktiv nur wenn Quittierung erlaubt.
- **Verlauf:** 24 h Raum, Soll, Abgas, Leistung als Linien (LVGL chart), Werte im RAM/PSRAM.
- **Einstellungen (PIN):** Zeitprogramm, Absenktemperatur, Kalibrierassistent, Funkfühler-Status (letzter Empfang, Signal), WLAN-Info, Push (ntfy-Topic an/aus), Displayhelligkeit, Bildschirmschoner nach X min (Touch weckt).
- Stopp-Knopf auf jedem Bildschirm erreichbar.

---

## 10. Webinterface und Push

- `web_server` (Version 3) auf dem Display mit Benutzer/Passwort aus `secrets.yaml`: Zustand, Temperaturen, Sollwert setzen, Stopp. **Start über das Web nur mit zusätzlicher Bestätigung** und nur, wenn Start erlaubt ist (der Mensch muss vor Ort angezündet haben – im Dialog darauf hinweisen).
- Push über **ntfy** (`http_request` an `https://ntfy.sh/<topic>`), abschaltbar: bei jedem Alarm, bei „Flamme aus“, bei „Raumfühler fehlt“, bei „Heizzeit beginnt“.
- NodeMCU: `web_server` optional, nur lesend plus Logs; OTA mit Passwort.

---

## 11. Vorgehen

1. Repository anlegen, `ofen_logic` mit Zustandsautomat, A0-Auswertung, Protokoll, Rampe, PI-Regler und Kennlinien-Regression **zuerst** schreiben und mit Unit-Tests absichern (`test/`, `make test` oder `cmake && ctest`). Testfälle mindestens:
   - Neustart in jedem Zustand → Pumpe 0, Zustand STOP
   - Start bei warmer Kammer / offener Kette / Alarm offen → abgelehnt
   - Anzünden ohne D0 nach 20 min → ALARM
   - Kette öffnet in jedem Zustand → Pumpe sofort 0, ALARM
   - A0-Grenzwerte inklusive Zwischenbereiche
   - Anlauf: Leistungswunsch 100 % wird 30 min ignoriert
   - Rampe max. 5 %/min, nie unter Minimum im Betrieb
   - Flamme aus (absolut und relativ), Abgas-Übertemperatur
   - Display-Timeout → Minimum, kein Stopp
   - Raumfühler fehlt → Minimum, kein Stopp
   - Raum zu warm 30 min → AUSLOESCHEN, danach kein Neustart
   - Quittierung nur bei kalter Kammer und geschlossener Kette
   - Zeitprogramm startet nie den Ofen
   - Protokoll: Roundtrip, CRC-Fehler, Müll, abgeschnittene Zeilen
2. `ofen-controller.yaml` schreiben, kompilieren.
3. `raumfuehler.yaml` schreiben, kompilieren.
4. `ofen-display.yaml`: erst Display + Touch + ein Testbildschirm zum Laufen bringen (Community-Config), dann Protokoll, dann Übersicht, Alarm, Start, dann Verlauf, Einstellungen, Kalibrierung.
5. **Simulationsmodus** für Tests am Tisch ohne Ofen: per `substitutions: sim: "true"` liefert der NodeMCU simulierte Eingänge (A0, D0, Abgas) und lässt sich über das Webinterface steuern (Schalter „Kette offen“, „Kammer warm“, Abgas-Schieberegler). Pumpe bleibt im Simulationsmodus physisch aus (PWM-Pin wird nicht angesteuert). Im README deutlich: vor dem Einbau `sim: "false"`.
6. README mit: Flashen per USB (`esphome run`), erste Inbetriebnahme Schritt für Schritt, Bedeutung aller Alarme und was zu tun ist, Kalibrierung, Simulationsmodus.

Wenn du Hardware angeschlossen findest (USB-Seriell), darfst du flashen und Logs lesen. **Schalte nie die echte Pumpe ein**, außer der Benutzer fordert es ausdrücklich im Rahmen der Pumpenkalibrierung an.

---

## 12. Abnahmekriterien

- [ ] Alle drei YAMLs kompilieren mit der aktuellen ESPHome-Version ohne Warnungen zu veralteter Syntax.
- [ ] Alle Unit-Tests grün, jede Regel aus Abschnitt 4 ist abgedeckt.
- [ ] Nach Stromausfall steht die Pumpe und der Zustand ist STOP – im Simulationsmodus nachgewiesen.
- [ ] Display-Kabel abziehen → NodeMCU läuft auf Minimum weiter, Display zeigt Verbindungsfehler.
- [ ] Funkfühler aus → nach 10 min Warnung, Minimum, kein Stopp.
- [ ] Kein Pfad im Code, der die Pumpe ohne menschlichen Start aus STOP/ALARM/AUSLOESCHEN einschaltet (außer `KALIBRIERUNG_PUMPE` mit Bestätigung).
- [ ] README für einen Laien verständlich.

---

## 13. Offene Werte (Platzhalter, später vom Benutzer gemessen)

| Wert | Platzhalter | Wo einstellbar |
| --- | --- | --- |
| PWM bei 17 kW | 0,35 | Kalibrierassistent / `K`-Befehl |
| PWM bei 33 kW | 0,80 | Kalibrierassistent |
| Flamme-aus-Grenze Abgas | 120 °C | Kalibrierassistent |
| Abgas-Maximum | 450 °C | Einstellungen |
| R1/R2 | 75 kΩ (alternativ 68/82 kΩ) | `substitutions` mit Schwellen |
| Öldichte | 0,85 kg/l | Einstellungen |
| WLAN-Kanal für ESP-NOW | 1 | `secrets.yaml` |

Frag nach, wenn eine Hardware-Angabe in dieser Datei der Realität widerspricht, die du beim Flashen siehst – rate nicht.
