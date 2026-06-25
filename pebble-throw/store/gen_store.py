#!/usr/bin/env python3
# Generate Pebble Throw store art (icons + banner) in the app's palette.
from PIL import Image, ImageDraw, ImageFont

SKY=(170,255,255); WATER=(0,170,255); SHIM=(205,246,255)
DARK=(70,70,72); WHITE=(255,255,255); SCREEN=(0,170,255)
RED=(226,75,72); GREEN=(102,170,40); LEAF=(85,170,0); STEM=(122,74,26)
ORANGE=(255,136,0); NAVY=(10,42,69)
FONT="/Users/r/src/pebble/kevinimal/resources/fonts/Poppins-Bold.ttf"

def watch(scale):
    W=int(56*scale); H=int(104*scale)
    img=Image.new("RGBA",(W,H),(0,0,0,0)); d=ImageDraw.Draw(img)
    cx=W//2
    bw=int(48*scale); bh=int(56*scale); sw=int(20*scale)
    d.rounded_rectangle([cx-sw//2,int(2*scale),cx+sw//2,int(24*scale)],radius=int(4*scale),fill=DARK)
    d.rounded_rectangle([cx-sw//2,H-int(24*scale),cx+sw//2,H-int(2*scale)],radius=int(4*scale),fill=DARK)
    by0=(H-bh)//2
    d.rounded_rectangle([cx-bw//2,by0,cx+bw//2,by0+bh],radius=int(13*scale),fill=DARK)
    mw=int(32*scale); mh=int(38*scale); mx0=cx-mw//2; my0=(H-mh)//2
    d.rounded_rectangle([mx0,my0,mx0+mw,my0+mh],radius=int(8*scale),fill=WHITE)
    pw=int(26*scale); ph=int(30*scale); px0=cx-pw//2; py0=(H-ph)//2
    d.rounded_rectangle([px0,py0,px0+pw,py0+ph],radius=int(6*scale),fill=SCREEN)
    return img

def pond(W,H,split):
    img=Image.new("RGB",(W,H),SKY); d=ImageDraw.Draw(img); wy=int(H*split)
    d.rectangle([0,wy,W,H],fill=WATER)
    d.rectangle([0,wy,W,wy+max(2,H//120)],fill=SHIM)
    return img,d,wy

def mlines(d,x,y,n,length,gap,col,w):
    for i in range(n):
        yy=y-(n//2)*gap+i*gap
        d.line([(x,yy+3),(x+length,yy-2)],fill=col,width=w)

def ripple(d,x,y,r,col,w=2):
    d.ellipse([x-r,y-r//2,x+r,y+r//2],outline=col,width=w)

def paste_watch(base,scale,cx,cy,angle):
    w=watch(scale).rotate(angle,expand=True,resample=Image.BICUBIC)
    base.paste(w,(cx-w.width//2,cy-w.height//2),w)

def apple(d,x,y,r):
    d.ellipse([x-r,y-r,x+r,y+r],fill=RED)
    d.ellipse([x+r-3,y-r-1,x+2*r-3,y+r-5],fill=SKY)   # bite
    d.rectangle([x-2,y-r-int(r*0.7),x+2,y-r+2],fill=STEM)
    d.ellipse([x+1,y-r-int(r*0.8),x+int(r*0.8),y-r+int(r*0.1)],fill=LEAF)

def android(d,x,y,r):
    d.line([(x-r//2,y-r),(x-r,y-2*r)],fill=GREEN,width=max(2,r//4))
    d.line([(x+r//2,y-r),(x+r,y-2*r)],fill=GREEN,width=max(2,r//4))
    d.ellipse([x-r,y-r,x+r,y+r],fill=GREEN)
    e=max(1,r//5)
    d.ellipse([x-r//2-e,y-e,x-r//2+e,y+e],fill=WHITE)
    d.ellipse([x+r//2-e,y-e,x+r//2+e,y+e],fill=WHITE)

# ---- icons ----
for size in (80,144):
    s=size/144.0
    img,d,wy=pond(size,size,0.46)
    mlines(d,int(size*0.14),int(size*0.52),3,int(size*0.18),int(size*0.14),ORANGE,max(2,int(size*0.035)))
    ripple(d,int(size*0.64),wy+int(size*0.05),int(size*0.12),WHITE,max(2,int(size*0.02)))
    paste_watch(img,1.0*s,int(size*0.52),int(size*0.50),-18)
    img.save(f"icon-{size}.png")

# ---- banner 720x320 ----
W,H=720,320
img,d,wy=pond(W,H,0.60)
fbig=ImageFont.truetype(FONT,86)
fsub=ImageFont.truetype(FONT,30)
d.text((54,34),"Pebble",font=fbig,fill=NAVY)
wp=d.textlength("Pebble ",font=fbig)
d.text((54+wp,34),"Throw",font=fbig,fill=ORANGE)
d.text((58,138),"how far can you skip?",font=fsub,fill=NAVY)
# scene along the waterline, below the title
for i,rx in enumerate((300,360,418)):
    ripple(d,rx,wy+10+i*2,24-i*4,WHITE,2)
mlines(d,330,wy+4,4,58,26,ORANGE,7)
paste_watch(img,1.15,452,wy+16,-18)
apple(d,566,wy+4,22)
android(d,634,wy+6,22)
img.save("banner.png")
print("done")
