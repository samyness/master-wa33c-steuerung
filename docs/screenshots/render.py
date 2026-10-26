"""Gerenderte Vorschau der Display-Bildschirme (800 × 480).

Positionen, Größen, Farben und Inter-Schnitte entsprechen ofen-display.yaml.
Aufruf: python docs/screenshots/render.py (braucht Edge oder Chrome und Internet für Inter)
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
@import url('https://fonts.googleapis.com/css2?family=Inter:wght@300;500;600&display=block');
*{box-sizing:border-box} body{margin:0;width:800px;height:480px;overflow:hidden;background:#0B0F14;color:#E8ECF1;
font-family:Inter,sans-serif;font-size:20px;font-weight:500;position:relative}.w{position:absolute}.card{position:absolute;background:#141A22;
border:1px solid #222B36;border-radius:16px}.eyebrow{position:absolute;color:#8B95A5;font-size:14px;font-weight:600;letter-spacing:2px}
.title{position:absolute;font-size:28px;font-weight:600}.light40{font-size:40px;font-weight:300;line-height:1}.light96{font-size:96px;font-weight:300;line-height:1}
.btn{position:absolute;height:64px;border:1px solid #222B36;border-radius:12px;background:#141A22;display:flex;align-items:center;
justify-content:center;font-size:20px;font-weight:500}.primary{border:0;background:#22C55E;color:#0B0F14}.stop{left:640px;top:400px;width:144px;background:#EF4444;border:0}
.dot{position:absolute;border-radius:50%}.muted{color:#8B95A5}.small{font-size:14px}.line{position:absolute;height:1px;background:#1C2430}
"""
def div(cls="", style="", text=""):
    return f'<div class="{cls}" style="{style}">{text}</div>'
def button(x,y,w,text,cls=""):
    return div(f"btn {cls}",f"left:{x}px;top:{y}px;width:{w}px",text)
STOPP=button(640,400,144,"STOPP","stop")

def uebersicht():
    kw=25.3
    fill=(kw-17)/16*164
    return "".join([
      div("card","left:16px;top:16px;width:352px;height:368px"),
      div("eyebrow","left:36px;top:33px","RAUM"),
      div("w light96","left:34px;top:68px","21,4°"),
      div("line","left:36px;top:190px;width:312px"),
      div("eyebrow","left:36px;top:215px","SOLL"),
      div("btn","left:36px;top:254px;width:64px;border-radius:32px;font-size:28px","–"),
      div("w light40","left:128px;top:263px;width:104px;text-align:center","21,0"),
      div("w muted","left:231px;top:271px","°C"),
      div("btn","left:284px;top:254px;width:64px;border-radius:32px;font-size:28px","+"),
      div("w small","left:36px;top:343px;width:312px;color:#F59E0B",""),
      div("card","left:380px;top:16px;width:404px;height:100px"),
      div("eyebrow","left:396px;top:31px","ZUSTAND"),
      div("dot","left:398px;top:72px;width:12px;height:12px;background:#22C55E"),
      div("w","left:424px;top:63px","Betrieb"),
      div("card","left:380px;top:128px;width:196px;height:122px"),
      div("eyebrow","left:396px;top:143px","LEISTUNG"),
      div("w light40","left:396px;top:171px","25,3"),
      div("w muted","left:504px;top:184px","kW"),
      div("w","left:396px;top:226px;width:164px;height:7px;border-radius:4px;background:#1C2430",
          div("w","left:0;top:0;width:%.0fpx;height:7px;border-radius:4px;background:#22C55E"%fill)),
      div("card","left:588px;top:128px;width:196px;height:122px"),
      div("eyebrow","left:604px;top:143px","ABGAS"),
      div("w light40","left:604px;top:171px","231,7"),
      div("w muted","left:714px;top:184px","°C"),
      div("card","left:380px;top:262px;width:404px;height:122px"),
      div("dot","left:398px;top:284px;width:10px;height:10px;background:#22C55E"),
      div("w small","left:422px;top:278px","STB und Überlauf zu"),
      div("dot","left:398px;top:319px;width:10px;height:10px;background:#22C55E"),
      div("w small","left:422px;top:313px","Lüfter an"),
      div("line","left:396px;top:345px;width:372px"),
      div("w small muted","left:396px;top:357px;width:226px;white-space:nowrap","Di 06:00 -> 21,0 °C"),
      div("w small muted","left:626px;top:357px;width:142px;text-align:right;white-space:nowrap","37,5 h Reinigung"),
      button(16,400,128,"Verlauf"),button(156,400,170,"Einstellungen"),button(338,400,142,"START"),STOPP])
def start():
    return "".join([div("title","left:32px;top:28px","Manueller Start"),div("card","left:32px;top:88px;width:736px;height:264px"),
      div("eyebrow","left:56px;top:117px","STARTFREIGABE"),div("w","left:56px;top:160px;width:688px;text-align:center","Ofen angezündet? Papier brennt?"),
      div("btn primary","left:220px;top:230px;width:360px;height:80px","JA – STARTEN"),button(32,400,144,"Zurück"),STOPP])
def alarm():
    return "".join([div("w","inset:0;background:#7F1D1D"),div("title","left:32px;top:26px","ALARM"),
      div("card","left:32px;top:82px;width:736px;height:300px;background:#991B1B;border-color:#B91C1C"),
      div("eyebrow","left:56px;top:105px;color:#FECACA","URSACHE"),div("title","left:56px;top:137px;width:688px","STB ausgelöst oder Sicherung F3 defekt"),
      div("line","left:56px;top:190px;width:688px;background:#B91C1C"),div("eyebrow","left:56px;top:211px;color:#FECACA","NÄCHSTE SCHRITTE"),
      div("w","left:56px;top:243px;width:688px","Ofen abkühlen lassen, STB am Ofen zurücksetzen, Sicherung F3 prüfen."),
      div("btn","left:56px;top:304px;width:276px;height:58px;background:#E8ECF1;color:#7F1D1D;border:0","QUITTIEREN"),STOPP])
def verlauf():
    n,x0,y0,w,h=288,32,76,736,252
    daten=[[],[],[],[]]
    for i in range(n):
      t=i/12; an=6<=t<22; s=21 if an else 12
      r=14+(6.8*(1-math.exp(-(t-6)/2.5)) if an else 0)+.15*math.sin(i/7)
      if t>=22:r=20.8-(t-22)*1.4
      p=0 if not an else (33 if t<7 else max(17,33-(t-7)*6)+1.5*math.sin(i/9))
      g=25 if not an else (60+(t-6)*400 if t<6.5 else 160+p*4+6*math.sin(i/5))
      if t>=22:g=max(25,230-(t-22)*300)
      for a,v in zip(daten,(r*10,s*10,p*10,g)):a.append(v)
    def poly(vals,maxv,color):
      pts=" ".join(f"{x0+i*w/(n-1):.1f},{y0+h-min(v,maxv)/maxv*h:.1f}" for i,v in enumerate(vals))
      return f'<polyline points="{pts}" fill="none" stroke="{color}" stroke-width="2"/>'
    grid="".join(f'<line x1="{x0}" x2="{x0+w}" y1="{y0+k*h/5}" y2="{y0+k*h/5}" stroke="#1C2430"/>' for k in range(6))
    svg=f'<svg class="w" width="800" height="480"><rect x="{x0}" y="{y0}" width="{w}" height="{h}" rx="16" fill="#141A22" stroke="#222B36"/>{grid}{poly(daten[0],350,"#38BDF8")}{poly(daten[1],350,"#E8ECF1")}{poly(daten[2],350,"#22C55E")}{poly(daten[3],600,"#F59E0B")}</svg>'
    legend=[]
    for x,c,t in [(40,"#38BDF8","Raum"),(140,"#E8ECF1","Soll"),(232,"#22C55E","Leistung"),(366,"#F59E0B","Abgas")]:
      legend += [div("dot",f"left:{x}px;top:350px;width:8px;height:8px;background:{c}"),div("w small muted",f"left:{x+16}px;top:343px",t)]
    return "".join([div("title","left:32px;top:26px","Verlauf · 24 Stunden"),svg,*legend,button(32,400,144,"Zurück"),STOPP])
def kalibrierung():
    return "".join([div("title","left:32px;top:28px;color:#F59E0B","Pumpe ohne Flamme"),
      div("card","left:32px;top:82px;width:736px;height:278px;border-color:#F59E0B"),div("eyebrow","left:56px;top:107px;color:#F59E0B","ACHTUNG"),
      div("w","left:56px;top:141px;width:688px;line-height:1.35","Ofen ist kalt. Die Ölleitung ist vom Brenner gelöst und steckt im Messbecher. Die Pumpe läuft fünf Stufen à 3 min. STOPP bricht jederzeit ab."),
      div("btn","left:170px;top:248px;width:460px;height:80px;background:#F59E0B;border:0;color:#0B0F14","JA, LEITUNG IM BECHER"),button(32,400,144,"Abbrechen"),STOPP])
BILDSCHIRME={"uebersicht":uebersicht,"start":start,"alarm":alarm,"verlauf":verlauf,"kalibrierung":kalibrierung}
def main():
    browser=next((b for b in BROWSER if pathlib.Path(b).exists()),BROWSER[-1])
    with tempfile.TemporaryDirectory() as tmp:
      for name,fn in BILDSCHIRME.items():
        html=pathlib.Path(tmp)/f"{name}.html"
        html.write_text(f"<!doctype html><meta charset='utf-8'><style>{CSS}</style><body>{fn()}</body>",encoding="utf-8")
        png=HIER/f"{name}.png"
        subprocess.run([browser,"--headless=new","--disable-gpu","--no-sandbox","--hide-scrollbars","--force-device-scale-factor=1",
          "--window-size=800,480","--virtual-time-budget=5000",f"--screenshot={png}",html.as_uri()],check=True,capture_output=True)
        print(png)
if __name__=="__main__":main()
