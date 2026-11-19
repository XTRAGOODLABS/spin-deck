"""Builds the Spin Deck icon and title artwork. Run on a Mac: python3 packaging/spindeck/make-icon.py
Uses the Impact font that ships with macOS (not included in this repo) and needs Pillow."""
from PIL import Image, ImageDraw, ImageFont
import struct, io, os
ROOT=os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
os.chdir(ROOT)
crate=Image.open('packaging/spindeck/crate-white.png')
F=os.environ.get('IMPACT_FONT','/System/Library/Fonts/Supplemental/Impact.ttf' if os.path.exists('/System/Library/Fonts/Supplemental/Impact.ttf') else '/usr/share/fonts/truetype/google-fonts/ofl/anton/Anton-Regular.ttf')
def word(t, width, track=-0.025):
    f=ImageFont.truetype(F,600); sz=600
    adv=[f.getlength(ch) for ch in t]
    total=sum(adv)+track*sz*(len(t)-1)
    asc,desc=f.getmetrics()
    im=Image.new('L',(int(total)+40,asc+desc),0); d=ImageDraw.Draw(im); x=0
    for ch,a in zip(t,adv):
        d.text((x,0),ch,font=f,fill=255); x+=a+track*sz
    im=im.crop(im.getbbox())
    return im.resize((width,int(im.size[1]*width/im.size[0])),Image.LANCZOS)
def icon(S=1024):
    im=Image.new('RGBA',(S,S),(0,0,0,0)); d=ImageDraw.Draw(im)
    pad=100; d.rounded_rectangle([pad,pad,S-pad,S-pad],185,fill=(0,0,0,255))
    L=pad+52; R=S-pad-52
    cw=250; c=crate.resize((cw,int(crate.size[1]*cw/crate.size[0])),Image.LANCZOS)
    gapx=34; tw=R-L-cw-gapx; gap=22
    ws=[word('SPIN',tw),word('DECK',tw)]
    th=sum(w.size[1] for w in ws)+gap
    maxh=S-2*pad-120
    sc=min(1,maxh/th); tw2=int(tw*sc)
    total_w=cw+gapx+tw2; x0=(S-total_w)//2
    im.alpha_composite(c,(x0,(S-c.size[1])//2))
    y=(S-th*sc)/2
    for w in ws:
        ww,hh=int(w.size[0]*sc),int(w.size[1]*sc); w2=w.resize((ww,hh),Image.LANCZOS)
        white=Image.new('RGBA',(ww,hh),(255,255,255,255)); white.putalpha(w2)
        im.alpha_composite(white,(x0+cw+gapx,int(y))); y+=hh+gap*sc
    return im
big=icon(); big.save('packaging/spindeck/icon.png')
def png(img,n):
    b=io.BytesIO(); img.resize((n,n),Image.LANCZOS).save(b,'PNG'); return b.getvalue()
parts=[(b'icp4',16),(b'icp5',32),(b'icp6',64),(b'ic07',128),(b'ic08',256),(b'ic09',512),(b'ic10',1024),(b'ic11',32),(b'ic12',64),(b'ic13',256),(b'ic14',512)]
body=b''.join(t+struct.pack('>I',8+len(p))+p for t,n in parts for p in [png(big,n)])
open('res/osx/application.icns','wb').write(b'icns'+struct.pack('>I',8+len(body))+body)
# brand lockup for the page top bar: crate + GOOD MIXX one line, white on transparent

def lockup(h=104):
    c=crate.resize((int(crate.size[0]*h/crate.size[1]),h),Image.LANCZOS)
    w=word('SPIN DECK',2000); w=w.resize((int(w.size[0]*h*0.78/w.size[1]),int(h*0.78)),Image.LANCZOS)
    gap=int(h*0.14); W=c.size[0]+gap+w.size[0]
    im=Image.new('RGBA',(W,h),(0,0,0,0)); im.alpha_composite(c,(0,0))
    white=Image.new('RGBA',w.size,(255,255,255,255)); white.putalpha(w)
    im.alpha_composite(white,(c.size[0]+gap,(h-w.size[1])//2))
    return im
lockup().save('packaging/spindeck/lockup.png')
lockup(320).save('packaging/spindeck/lockup-big.png')
