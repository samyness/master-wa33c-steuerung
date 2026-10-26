#pragma once
// Zeilenprotokoll Display <-> NodeMCU: "<feld>;<feld>;...*<crc8 hex>\n"
// CRC-8 (Polynom 0x07, Start 0x00) über alles vor dem '*'.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace ofen {

constexpr size_t MAX_LINE = 96;
constexpr int MAX_FIELDS = 12;

inline uint8_t crc8(const char *s, size_t n) {
  uint8_t crc = 0;
  for (size_t i = 0; i < n; i++) {
    crc ^= (uint8_t) s[i];
    for (int b = 0; b < 8; b++) crc = (crc & 0x80) ? (uint8_t) ((crc << 1) ^ 0x07) : (uint8_t) (crc << 1);
  }
  return crc;
}

// Hängt Prüfsumme und Zeilenende an.
inline std::string frame(const std::string &payload) {
  char tail[6];
  snprintf(tail, sizeof(tail), "*%02X\n", crc8(payload.data(), payload.size()));
  return payload + tail;
}

// Zerlegte, geprüfte Zeile. Felder zeigen in den internen Puffer.
struct Fields {
  char buf[MAX_LINE + 1];
  const char *f[MAX_FIELDS];
  int n = 0;
};

inline int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// Prüft CRC und zerlegt die Zeile (ohne '\n'). false bei jedem Formfehler.
inline bool parse_line(const char *line, Fields &out) {
  size_t len = strlen(line);
  while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n')) len--;
  if (len < 4 || len > MAX_LINE || line[len - 3] != '*') return false;
  int h = hexval(line[len - 2]), l = hexval(line[len - 1]);
  if (h < 0 || l < 0 || crc8(line, len - 3) != (uint8_t) (h * 16 + l)) return false;
  memcpy(out.buf, line, len - 3);
  out.buf[len - 3] = 0;
  out.n = 0;
  char *p = out.buf;
  while (true) {
    if (out.n == MAX_FIELDS) return false;
    out.f[out.n++] = p;
    char *sc = strchr(p, ';');
    if (!sc) break;
    *sc = 0;
    p = sc + 1;
  }
  return true;
}

// Strikte Ganzzahl; false bei Leerstring, Müll oder Überlauf.
inline bool to_long(const char *s, long &v) {
  if (!*s) return false;
  char *end;
  v = strtol(s, &end, 10);
  return *end == 0 && v > -100000000L && v < 100000000L;
}

// Zahl*10 oder "NA" -> float (NAN bei NA)
inline bool to_tenths(const char *s, float &v) {
  if (strcmp(s, "NA") == 0) return v = NAN, true;
  long l;
  if (!to_long(s, l)) return false;
  v = l / 10.0f;
  return true;
}

inline std::string tenths(float v) {
  if (std::isnan(v)) return "NA";
  return std::to_string(lroundf(v * 10.0f));
}

// --- Telegramme ---

struct StatusMsg {  // NodeMCU -> Display, jede Sekunde
  long seq = 0, state = 0, alarm = 0, pwm_promille = 0, kette = 3, luefter = 0, anlauf_rest_s = 0, uptime_s = 0;
  uint8_t warn = 0;
  float kw = 0, abgas_c = NAN;
};

struct HeartbeatMsg {  // Display -> NodeMCU, alle 2 s
  long seq = 0;
  long leistung = 0;  // 0..100 %
  float raum_c = NAN, soll_c = NAN;
};

struct CommandMsg {  // C: START/STOP/ACK/KAL, K: Kalibrierwert, A: Antwort
  char type = 0;
  long seq = 0;
  std::string name;  // C: Befehl, K: Schlüssel, A: OK|ERR
  std::string arg;   // C KAL: PWM in Promille, K: Wert, A: Grund
};

// Feld <warn> ist eine Erweiterung gegenüber docs/CLAUDE.md (Warnungs-Bitmaske, siehe controller.h).
inline std::string encode(const StatusMsg &s) {
  std::string p = "S;" + std::to_string(s.seq) + ";" + std::to_string(s.state) + ";" + std::to_string(s.alarm) + ";" +
                  std::to_string(s.pwm_promille) + ";" + tenths(s.kw) + ";" + tenths(s.abgas_c) + ";" +
                  std::to_string(s.kette) + ";" + std::to_string(s.luefter) + ";" + std::to_string(s.anlauf_rest_s) +
                  ";" + std::to_string(s.uptime_s) + ";" + std::to_string(s.warn);
  return frame(p);
}

// Feld <soll> ist eine Erweiterung: der NodeMCU braucht den Sollwert für Regel 13 (Raum zu warm).
inline std::string encode(const HeartbeatMsg &h) {
  return frame("H;" + std::to_string(h.seq) + ";" + std::to_string(h.leistung) + ";" + tenths(h.raum_c) + ";" +
               tenths(h.soll_c));
}

inline std::string encode(const CommandMsg &c) {
  std::string p = std::string(1, c.type) + ";" + std::to_string(c.seq) + ";" + c.name;
  if (!c.arg.empty() || c.type != 'C') p += ";" + c.arg;
  return frame(p);
}

inline bool decode(const Fields &f, StatusMsg &s) {
  long w;
  if (f.n != 12 || strcmp(f.f[0], "S") != 0) return false;
  return to_long(f.f[1], s.seq) && to_long(f.f[2], s.state) && to_long(f.f[3], s.alarm) &&
         to_long(f.f[4], s.pwm_promille) && to_tenths(f.f[5], s.kw) && to_tenths(f.f[6], s.abgas_c) &&
         to_long(f.f[7], s.kette) && to_long(f.f[8], s.luefter) && to_long(f.f[9], s.anlauf_rest_s) &&
         to_long(f.f[10], s.uptime_s) && to_long(f.f[11], w) && w >= 0 && w <= 255 && (s.warn = (uint8_t) w, true);
}

inline bool decode(const Fields &f, HeartbeatMsg &h) {
  if (f.n != 5 || strcmp(f.f[0], "H") != 0) return false;
  return to_long(f.f[1], h.seq) && to_long(f.f[2], h.leistung) && h.leistung >= 0 && h.leistung <= 100 &&
         to_tenths(f.f[3], h.raum_c) && to_tenths(f.f[4], h.soll_c);
}

inline bool decode(const Fields &f, CommandMsg &c) {
  if (f.n < 3 || f.n > 4 || strlen(f.f[0]) != 1 || !strchr("CKA", f.f[0][0])) return false;
  c.type = f.f[0][0];
  if (!to_long(f.f[1], c.seq) || !*f.f[2]) return false;
  c.name = f.f[2];
  c.arg = f.n == 4 ? f.f[3] : "";
  return c.type == 'C' || f.n == 4;
}

// Sammelt Zeichen zu Zeilen. Zu lange Zeilen werden bis zum nächsten '\n' verworfen.
class LineBuffer {
 public:
  // Liefert true, wenn eine vollständige Zeile in line() bereitsteht.
  bool feed(char c) {
    if (c == '\n') {
      bool ok = !overflow_ && len_ > 0;
      buf_[len_] = 0;
      len_ = 0;
      overflow_ = false;
      return ok;
    }
    if (len_ >= MAX_LINE) {
      overflow_ = true;
      return false;
    }
    buf_[len_++] = c;
    return false;
  }
  const char *line() const { return buf_; }

 private:
  char buf_[MAX_LINE + 1] = {};
  size_t len_ = 0;
  bool overflow_ = false;
};

}  // namespace ofen
