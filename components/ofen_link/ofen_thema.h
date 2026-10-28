#pragma once
// Hell/Dunkel zur Laufzeit: tauscht die Palettenfarben in allen Stilen aller Seiten aus.
#include <set>

#include "lvgl.h"
#include "lvgl_private.h"

namespace esphome::ofen_link {

inline bool thema_hell = false;

// Dunkle Palette (wie in ofen-display.yaml) und ihr helles Gegenstück. Akzentfarben bleiben gleich.
struct FarbPaar {
  uint32_t dunkel, hell;
};
inline constexpr FarbPaar PALETTE[] = {
    {0x0B0F14, 0xEEF1F5},  // Hintergrund
    {0x141A22, 0xFFFFFF},  // Karte
    {0x222B36, 0xD3D9E1},  // Rahmen
    {0x1C2430, 0xE5E9EF},  // Trennlinie
    {0xE8ECF1, 0x111827},  // Text
    {0x8B95A5, 0x4B5563},  // Nebentext
    {0x5B6573, 0x9CA3AF},  // gedämpft
};  // Alarmrot fehlt absichtlich: Alarmseite und STOPP bleiben in beiden Designs gleich.

// Farbe für das aktuelle Thema; Code gibt immer den Dunkel-Wert an.
inline uint32_t farbe(uint32_t dunkel) {
  if (thema_hell)
    for (auto &p : PALETTE)
      if (p.dunkel == dunkel) return p.hell;
  return dunkel;
}

inline void thema_setzen(bool hell) {
  lv_display_t *disp = lv_display_get_default();
  if (disp == nullptr || hell == thema_hell) return;
  thema_hell = hell;
  std::set<const lv_style_t *> fertig;  // gemeinsame Stile nur einmal tauschen, sonst kippen sie zurück
  auto tausche = [&](const lv_style_t *cs) {
    if (!fertig.insert(cs).second) return;
    auto *s = const_cast<lv_style_t *>(cs);
    for (lv_style_prop_t prop : {LV_STYLE_BG_COLOR, LV_STYLE_BORDER_COLOR, LV_STYLE_TEXT_COLOR, LV_STYLE_LINE_COLOR}) {
      lv_style_value_t v;
      if (lv_style_get_prop(s, prop, &v) != LV_STYLE_RES_FOUND) continue;
      uint32_t c = lv_color_to_u32(v.color) & 0xFFFFFF;
      for (auto &p : PALETTE)
        if (c == (hell ? p.dunkel : p.hell)) {
          v.color = lv_color_hex(hell ? p.hell : p.dunkel);
          lv_style_set_prop(s, prop, v);
          break;
        }
    }
  };
  auto walk = [&](auto &self, lv_obj_t *o) -> void {
    if (o == nullptr) return;
    for (uint32_t i = 0; i < o->style_cnt; i++) tausche(o->styles[i].style);
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) self(self, lv_obj_get_child(o, i));
  };
  for (uint32_t i = 0; i < disp->screen_cnt; i++) walk(walk, disp->screens[i]);
  walk(walk, disp->top_layer);
  walk(walk, disp->bottom_layer);
  lv_obj_report_style_change(nullptr);
}

}  // namespace esphome::ofen_link
