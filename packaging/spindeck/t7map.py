"""Draws the Spin Deck x Inpulse T7 control map (docs/screenshots/t7-map.png)."""
import os, sys
from PIL import Image, ImageDraw, ImageFont
HERE=os.path.dirname(os.path.abspath(__file__))
F=os.path.join(HERE,'fonts')+'/'
OUT=sys.argv[1] if len(sys.argv)>1 else 't7-map.png'
LOCK=sys.argv[2] if len(sys.argv)>2 else os.path.join(HERE,'../media/lockup.png')
W,H=1920,1480
im=Image.new('RGB',(W,H),(13,13,13)); d=ImageDraw.Draw(im)
anton=lambda s: ImageFont.truetype(F+'Anton-Regular.ttf',s)
def arch(s,w=700):
    f=ImageFont.truetype(F+'Archivo.ttf',s); f.set_variation_by_axes([w,100]); return f
GREEN=(61,220,132); WHITE=(245,245,242); SOFT=(185,185,180); LINE=(55,55,55); PANEL=(22,22,22)
lock=Image.open(LOCK).convert('RGBA'); lw=430; lock=lock.resize((lw,int(lock.height*lw/lock.width)))
im.paste(lock,(60,48),lock)
d.text((60+lw+36,44),"×  INPULSE T7 CONTROL MAP",font=anton(78),fill=WHITE)
def panel(x,y,w,h,title):
    d.rounded_rectangle((x,y,x+w,y+h),22,fill=PANEL,outline=WHITE,width=3)
    d.text((x+28,y+18),title,font=anton(46),fill=GREEN)
X='*'
deck=[("SYNC","Sync on / off","Make sync leader"),
("CUE","Cue (hold to preview)","Go to cue and stop"),
("PLAY","Play / pause, instant start","Stutter from cue"),
("IN / OUT","Set loop start / end","Halve / double loop"),
("AUTOLOOP knob","Turn: size · Push: loop on/off","Turn: move · Push: delete loop"),
("PARAM  <  >","Hold: slow down / speed up","PARAM < : QUANTIZE on/off*"),
("RANGE","Tempo range 8 / 16 / 50%","Key lock"),
("BROWSE knob","Scroll · Push: load song","Scroll 10 at a time"),
("ASSIST","Open selected item","SAVE A GOOD MIX*"),
("BACK","Back to the song list","Big library view"),
("VOCAL","Vocals only, on / off*","same"),
("INSTRUMENTAL","Music only, on / off*","same"),
("PLATTER","Scratch and nudge","same"),
("PAD MODE 1–4","Pad pages 1–4","Pad pages 5–8")]
x,y,w=50,150,1150; rh=58
dh=150+rh*len(deck)
panel(x,y,w,dh,"EACH DECK  (both sides work the same)")
cx=[x+28,x+330,x+760]; hy=y+86
for i,hdr in enumerate(["CONTROL","PLAIN","WITH SHIFT"]):
    d.text((cx[i],hy),hdr,font=arch(24,850),fill=SOFT)
for r,(a,b,c) in enumerate(deck):
    yy=hy+44+r*rh
    d.line((x+20,yy-8,x+w-20,yy-8),fill=LINE,width=2)
    d.text((cx[0],yy),a,font=arch(27,850),fill=WHITE)
    for txt,col in ((b,cx[1]),(c,cx[2])):
        d.text((col,yy),txt.rstrip(X),font=arch(26,620),fill=GREEN if txt.endswith(X) else WHITE)
my=y+dh+26; mh=200
panel(x,my,w,mh,"MIXER + FX")
mix=["Faders, gain, EQ, filter, tempo: as printed","Crossfader switch: smooth / scratch / off","Headphone button: split cue on / off","FX 1 / 2: arm slots · SHIFT: next effect","FX lever: effect on while held","FX − / +: effect amount"]
for i,t in enumerate(mix):
    d.text((x+28+(i//3)*560,my+82+(i%3)*38),"• "+t,font=arch(23,600),fill=WHITE)
pages=[("1  HOT CUE","Jump to / set hot cue 1–8","SHIFT: delete"),
("2  LOOP","Loops 1/4 to 32 beats","SHIFT: loop roll"),
("3  STEMS*","Pads 1–4 mute drums, bass,\nother, vocals","Lit = playing\nRed = muted"),
("4  SAMPLER","Hold to play sampler 1–8",""),
("5  BEAT JUMP","Back (1–4) / forward (5–8)","SHIFT: 4x farther"),
("6  ROLL","Loop roll 1/16 to 8 beats",""),
("7  FX","1–3: arm effects · 4: unit on",""),
("8  KEY","Key down, up, reset, sync","")]
px,py,pw=1230,150,640
ph=my+mh-py
panel(px,py,pw,ph,"PAD PAGES")
step=(ph-110)/len(pages)
for r,(t,b,c) in enumerate(pages):
    yy=py+92+r*step
    d.text((px+28,yy),t.rstrip(X),font=anton(34),fill=GREEN if t.endswith(X) else WHITE)
    d.multiline_text((px+28,yy+46),b,font=arch(23,600),fill=WHITE,spacing=2)
    if c:
        fc=arch(21,600)
        d.multiline_text((px+pw-28-max(d.textlength(l,font=fc) for l in c.split('\n')),yy+8),c,font=fc,fill=SOFT,spacing=2,align='right')
d.text((50,H-100),"Green = Spin Deck extras  ·  Pick the T7 once in Preferences › Controllers, and set the sound to the T7 in Sound Hardware",font=arch(26,700),fill=GREEN)
d.text((50,H-52),"T7 setup by Infinite-Grok (GPL) with Spin Deck additions · Hercules and DJControl Inpulse are trademarks of Guillemot Corporation; not affiliated",font=ImageFont.truetype(F+'IBMPlexMono-Medium.ttf',21),fill=SOFT)
im.save(OUT)
