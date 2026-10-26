#pragma once
// Auswertung der Sicherheitskette (STB / Überlauf) über die Spannung an A0.
#include <cmath>
#include <cstdint>

namespace ofen {

// Werte entsprechen dem Protokollfeld <kette>.
enum class Chain : uint8_t { OK = 0, UEBERLAUF = 1, STB = 2, FEHLER = 3 };

// Schwellen in Volt an A0 (für R1 = R2 = 75 kΩ, R3 = 10 kΩ). Bei 68/82 kΩ anpassen.
struct ChainThresholds {
  float ok_min = 2.0f;         // > ok_min: alles zu
  float ueberlauf_min = 0.8f;  // ueberlauf_min..ueberlauf_max: Überlauf offen
  float ueberlauf_max = 1.8f;
  float stb_max = 0.5f;        // < stb_max: STB offen oder F3 durch
};

inline Chain classify_chain(float volt, const ChainThresholds &t = {}) {
  if (std::isnan(volt)) return Chain::FEHLER;
  if (volt > t.ok_min) return Chain::OK;
  if (volt >= t.ueberlauf_min && volt <= t.ueberlauf_max) return Chain::UEBERLAUF;
  if (volt < t.stb_max) return Chain::STB;
  return Chain::FEHLER;  // Zwischenbereich: Leitungs- oder Bauteilfehler
}

// Entprellung: Wechsel in einen Fehlerzustand gilt sofort, Rückkehr zu OK erst nach 2 s stabil.
// Die erste Messung nach dem Booten wird direkt übernommen (sie ist bereits über 1 s gemittelt).
class ChainFilter {
 public:
  ChainFilter(uint32_t stable_ms = 2000) : stable_ms_(stable_ms) {}

  Chain update(Chain raw, uint32_t now) {
    if (first_) {
      first_ = false;
      state_ = raw;
    } else if (raw != Chain::OK) {
      state_ = raw;
      ok_since_valid_ = false;
    } else if (state_ != Chain::OK) {
      if (!ok_since_valid_) {
        ok_since_valid_ = true;
        ok_since_ = now;
      }
      if (now - ok_since_ >= stable_ms_) state_ = Chain::OK;
    }
    if (raw != Chain::OK) ok_since_valid_ = false;
    return state_;
  }
  Chain state() const { return state_; }

 private:
  uint32_t stable_ms_;
  bool first_ = true;
  bool ok_since_valid_ = false;
  uint32_t ok_since_ = 0;
  Chain state_ = Chain::FEHLER;
};

}  // namespace ofen
