# Hardware- und ESPHome-Notizen

Stand: 26. Oktober 2026. Installiert und für die Kompilierung verwendet:
**ESPHome 2026.10.0** (`.venv/Scripts/esphome version`).

## Waveshare ESP32-S3-Touch-LCD-7

Die aktuelle ESPHome-Plattform ist 'mipi_rgb'. Sie ersetzt für neue
Konfigurationen die bisher gebräuchliche 'rpi_dpi_rgb'-Konfiguration und bringt
mit 'model: ESP32-S3-TOUCH-LCD-7-800X480' bereits ein vollständiges Profil für
dieses Board mit. Das Profil setzt das 800×480-RGB565-Panel einschließlich der
Timings und Pins. ESP-IDF und PSRAM sind erforderlich.

Zur Kontrolle die physische Pinbelegung:

| Funktion | Anschluss |
| --- | --- |
| RGB Rot R0…R4 | GPIO1, 2, 42, 41, 40 |
| RGB Grün G0…G5 | GPIO39, 0, 45, 48, 47, 21 |
| RGB Blau B0…B4 | GPIO14, 38, 18, 17, 10 |
| DE / VSYNC / HSYNC / PCLK | GPIO5 / 3 / 46 / 7 |
| I²C SDA / SCL | GPIO8 / GPIO9, 400 kHz |
| GT911 IRQ / Reset | GPIO4 / CH422G EXIO1 |
| Hintergrundbeleuchtung | CH422G EXIO2 ('DISP') |
| LCD-Reset / LCD-Versorgung | CH422G EXIO3 / EXIO6 |

Der CH422G wird nativ von ESPHome unterstützt. Seine Register verwenden mehrere
feste I²C-Adressen; im YAML wird deshalb keine Adresse angegeben. Die
modellbasierte 'mipi_rgb'-Konfiguration steuert LCD-Reset, Versorgung und
Beleuchtung. Der GT911 wird separat mit IRQ und Reset konfiguriert.

Quellen:

- ESPHome MIPI-RGB-Dokumentation und fertiges Boardprofil: https://esphome.io/components/display/mipi_rgb/
- ESPHome CH422G: https://esphome.io/components/ch422g/
- ESPHome GT911: https://esphome.io/components/touchscreen/gt911/
- Waveshare Board-/Pin-Dokumentation: https://docs.waveshare.com/ESP32-S3-Touch-LCD-7
- Waveshare Schaltplan: https://files.waveshare.net/wiki/ESP32-S3-Touch-LCD-7/ESP32-S3-Touch-LCD-7-Sch.pdf
- Community-Konfiguration mit derselben RGB-Belegung: https://github.com/phdindota/waveshare-esp32s3-dashboard

## Onboard-RS485

Der eingebaute RS485-Transceiver hängt am UART mit **ESP32-TX GPIO16** und
**ESP32-RX GPIO15**. Waveshares ältere Tabelle kann wegen der Benennung aus Sicht
des Transceivers umgekehrt gelesen werden; die FAQ stellt die tatsächliche
ESP32-Richtung ausdrücklich klar. Der Transceiver besitzt eine automatische
Sende-/Empfangsumschaltung, daher gibt es keinen DE/RE-Pin im YAML.

GPIO15/16 kollidieren weder mit USB-OTG (GPIO19/20) noch mit UART0/Logger
(GPIO43/44). Der separate USB-UART-Port und ESPHome-Logs können daher parallel
benutzt werden. Der UART1/UART2-Schiebeschalter betrifft laut Waveshare den
separaten UART-Anschluss bzw. USB-UART-Pfad, nicht den RS485-PHY. Der 120-Ω-
Abschluss ist auf dem Board per Jumper zuschaltbar und ab Werk verbunden.

Quellen:

- Waveshare RS485-/USB-/UART-Übersicht: https://docs.waveshare.com/ESP32-S3-Touch-LCD-7
- Waveshare FAQ zur vertauscht wirkenden TX/RX-Benennung: https://docs.waveshare.com/ESP32-S3-Touch-LCD-7B/FAQ
- Waveshare Schaltplan: https://files.waveshare.net/wiki/ESP32-S3-Touch-LCD-7/ESP32-S3-Touch-LCD-7-Sch.pdf

## Funk-Raumfühler

Gewählt ist ESPHomes natives 'packet_transport' mit der Plattform 'espnow'.
'espnow' allein transportiert Bytepakete; 'packet_transport' bildet dagegen
ESPHome-Sensor-IDs direkt ab, sendet zyklisch und bietet Verschlüsselung sowie
Rolling Codes. Damit entfällt eigener Serialisierungs- und Empfangscode.

Der Fühler sendet alle 30 s verschlüsselt als Unicast an die Display-MAC. Sein
ESP-NOW-Kanal kommt aus 'wifi_channel'. Beim Display bestimmt wegen des aktiven
WLANs der Router den Funkkanal; die WLAN-Netzwerkkonfiguration akzeptiert daher
nur den ebenfalls in 'wifi_channel' festgelegten Kanal. Der Router/AP muss
tatsächlich auf Kanal 1 fest eingestellt sein. Die Funkübertragung selbst ist
direkt und benötigt keinen Router; der Display-Knoten benötigt WLAN jedoch für
Webinterface und SNTP.

Quellen:

- ESPHome ESP-NOW: https://esphome.io/components/espnow/
- ESPHome ESP-NOW Packet Transport: https://esphome.io/components/packet_transport/espnow/
- ESPHome Packet Transport und Verschlüsselung: https://esphome.io/components/packet_transport/
- ESPHome WLAN-Kanalbindung: https://esphome.io/components/wifi/

## Noch am realen Board zu prüfen

- Hardware-Revision und exakte Flash-Bestückung: Waveshare nennt je nach Quelle
  N8R8 bzw. das Projekt 16 MB Flash; der Gesamtauftrag nennt N16R8.
- Touch-Ausrichtung und Farbfolge nach dem ersten Flashen.
- Stellung des RS485-Abschlussjumpers passend zu den beiden Busenden.
