"""Gerenderte Vorschau der Display-Bildschirme (800 x 480) mit Beispieldaten.

Keine echten Geräte-Screenshots: Positionen, Größen, Farben und Schriften sind aus ofen-display.yaml
übernommen; das echte LVGL-Rendering kann in Details abweichen. Bei Layoutänderungen hier nachziehen.

Aufruf: python docs/screenshots/render.py   (braucht Microsoft Edge oder Chrome, Internet für die Schrift)
"""
import math
import pathlib
import subprocess
import tempfile

HIER = pathlib.Path(__file__).parent
BROWSER = [
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    "chromium",
]

CSS = """
@import url('https://fonts.googleapis.com/css2?family=Roboto:wght@400;700&display=block');
* { box-sizing: border-box; margin: 0; padding: 0; }
body { width: 800px; height: 480px; overflow: hidden; background: #101820; color: #fff;
       font: 24px Roboto, sans-serif; position: relative; }
.w { position: absolute; white-space: pre-wrap; line-height: 1.15; }
.b36 { font-size: 36px; font-weight: 700; }
.b60 { font-size: 60px; font-weight: 700; }
.btn { position: absolute; display: flex; align-items: center; justify-content: center; text-align: center;
       background: #274C77; border-radius: 8px; box-shadow: 0 3px 6px rgba(0,0,0,.45); }
"""


def label(x, y, text, cls="", w=None, style=""):
    width = f"width:{w}px;" if w else ""
    return f'<div class="w {cls}" style="left:{x}px;top:{y}px;{width}{style}">{text}</div>'


def button(x, y, w, h, text, cls="", bg=None):
    farbe = f"background:#{bg:06X};" if bg else ""
    return f'<div class="btn {cls}" style="left:{x}px;top:{y}px;width:{w}px;height:{h}px;{farbe}">{text}</div>'


STOPP = button(650, 400, 145, 75, "STOPP", "b36", 0xC1121F)  # top_layer: auf jedem Bildschirm


def uebersicht():
    kw = 25.3
    balken = (kw - 17) / 16 * 220
    return "".join([
        label(35, 25, "21,4 °C", "b60"),
        label(40, 105, "Soll 21,0 °C", "b36"),
        button(360, 65, 75, 70, "-", "b36"),
        button(450, 65, 75, 70, "+", "b36"),
        f'<div class="w" style="left:560px;top:20px;width:220px;height:40px;border-radius:20px;background:#2b3a4a;overflow:hidden">'
        f'<div style="width:{balken:.0f}px;height:100%;background:#2196F3;border-radius:20px"></div></div>',
        label(560, 66, "17 ... 33 kW"),
        label(560, 96, "Nächste Heizzeit:\nDi 06:00 -> 21,0 °C", w=230),
        label(35, 165, "BETRIEB", "b36", 730,
              "height:56px;padding:8px;border-radius:8px;background:#2A9D3F"),
        label(35, 235, "Abgas 231,7 °C   Leistung 25,3 kW"),
        label(35, 275, "Kette: STB und Überlauf zu   Lüfter: AN"),
        label(35, 320, "", w=730, style="color:#FFD60A"),
        label(35, 355, "Tellerreinigung: 37,5 h"),
        button(15, 400, 145, 65, "START"),
        button(175, 400, 145, 65, "Verlauf"),
        button(335, 400, 200, 65, "Einstellungen"),
        STOPP,
    ])


def start():
    return "".join([
        label(40, 35, "Manueller Start", "b36"),
        label(40, 105, "Ofen angezündet? Papier brennt?", w=700),
        button(220, 195, 360, 100, "JA – STARTEN", "b36", 0x2A9D8F),
        button(40, 390, 150, 70, "Zurück"),
        STOPP,
    ])


def alarm():
    return "".join([
        '<style>body{background:#8B0000}</style>',
        label(35, 25, "ALARM", "b60"),
        label(35, 120, "STB ausgelöst oder Sicherung F3 defekt", "b36", 720),
        label(35, 185, "Ofen abkühlen lassen, STB am Ofen zurücksetzen, Sicherung F3 prüfen.", w=720),
        button(250, 300, 300, 80, "QUITTIEREN"),
        STOPP,
    ])


def verlauf():
    # 24 h, ein Punkt je 5 min: Ofen startet um 06:00, Raum steigt, Regler nimmt Leistung zurück
    n, x0, y0, w, h = 288, 30, 65, 720, 300
    raum, soll, abgas, leistung = [], [], [], []
    for i in range(n):
        t = i / 12  # Stunden seit Mitternacht
        an = 6 <= t < 22
        s = 21.0 if an else 12.0
        r = 14 + (6.8 * (1 - math.exp(-(t - 6) / 2.5)) if an else 0) + 0.15 * math.sin(i / 7)
        if t >= 22:
            r = 20.8 - (t - 22) * 1.4
        p = 0 if not an else (33 if t < 7 else max(17, 33 - (t - 7) * 6) + 1.5 * math.sin(i / 9))
        g = 0 if not an else (60 + (t - 6) * 400 if t < 6.5 else 160 + p * 4 + 6 * math.sin(i / 5))
        if t >= 22:
            g = max(25, 230 - (t - 22) * 300)
        raum.append(r * 10)
        soll.append(s * 10)
        leistung.append(p * 10)
        abgas.append(max(g, 25))

    def linie(werte, maxv, farbe):
        pts = " ".join(f"{x0 + i * w / (n - 1):.1f},{y0 + h - min(v, maxv) / maxv * h:.1f}" for i, v in enumerate(werte))
        return f'<polyline points="{pts}" fill="none" stroke="#{farbe:06X}" stroke-width="2"/>'

    gitter = "".join(f'<line x1="{x0}" x2="{x0 + w}" y1="{y0 + k * h / 5}" y2="{y0 + k * h / 5}" stroke="#2b3a4a"/>'
                     for k in range(6))
    svg = (f'<svg class="w" style="left:0;top:0" width="800" height="480">'
           f'<rect x="{x0}" y="{y0}" width="{w}" height="{h}" rx="6" fill="#18242f" stroke="#2b3a4a"/>{gitter}'
           + linie(raum, 350, 0x00D4FF) + linie(soll, 350, 0xFFFFFF) + linie(leistung, 350, 0x2A9D3F)
           + linie(abgas, 600, 0xFF7B00) + "</svg>")
    return "".join([
        label(25, 10, "Verlauf 24 Stunden", "b36"),
        svg,
        label(200, 375, "Blau Raum, Weiß Soll (0,1 °C), Grün Leistung (0,1 kW), Orange Abgas (°C, rechts)",
              w=440, style="font-size:20px"),
        button(25, 400, 150, 65, "Zurück"),
        STOPP,
    ])


def kalibrierung():
    return "".join([
        label(40, 35, "Pumpe ohne Flamme", "b36"),
        label(40, 100, "Ofen ist kalt. Die Ölleitung ist vom Brenner gelöst und steckt im Messbecher. "
                       "Die Pumpe läuft 5 Stufen à 3 min, STOPP bricht jederzeit ab.", w=720),
        button(170, 230, 460, 100, "JA, LEITUNG IM BECHER", "b36", 0xE76F00),
        button(40, 390, 160, 70, "Abbrechen"),
        STOPP,
    ])


BILDSCHIRME = {
    "uebersicht": uebersicht,
    "start": start,
    "alarm": alarm,
    "verlauf": verlauf,
    "kalibrierung": kalibrierung,
}


def main():
    browser = next((b for b in BROWSER if pathlib.Path(b).exists()), BROWSER[-1])
    with tempfile.TemporaryDirectory() as tmp:
        for name, inhalt in BILDSCHIRME.items():
            html = pathlib.Path(tmp) / f"{name}.html"
            html.write_text(f"<!doctype html><meta charset='utf-8'><style>{CSS}</style><body>{inhalt()}</body>",
                            encoding="utf-8")
            png = HIER / f"{name}.png"
            subprocess.run([browser, "--headless=new", "--disable-gpu", "--hide-scrollbars",
                            "--force-device-scale-factor=1", "--window-size=800,480",
                            "--virtual-time-budget=5000", f"--screenshot={png}", html.as_uri()],
                           check=True, capture_output=True)
            print(png)


if __name__ == "__main__":
    main()
