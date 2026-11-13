"""Author reproducible native camera/input shots and titles from retail glyphs."""
from pathlib import Path
import json, math, struct
import numpy as np
from PIL import Image, ImageFilter
from spyrodisc import wad_index

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'output/hollowbloom-trailers'
for folder in ('source', 'gameplay', 'titles', 'review', 'stills'):
    (OUT / folder).mkdir(parents=True, exist_ok=True)

def shot(name, seconds, spawn, yaw, mode, cameras, route=(), actions=(), fov=50):
    frames = round(seconds * 60) + 5
    lines = ['S2SHOT 1', f'1920 1080 {frames}', ' '.join(map(str, (*spawn, yaw, 0, fov, mode))), str(len(route))]
    lines += [' '.join(map(str, p)) for p in route]
    lines += [str(len(actions))] + [' '.join(map(str, a)) for a in actions]
    lines += [str(len(cameras))] + [' '.join(map(str, (round(t*60), *eye, *target))) for t, eye, target in cameras]
    (OUT / 'source' / f'{name}.s2shot').write_text('\n'.join(lines)+'\n')

shot('01-vista', 7, (0,.15,-38), -1.13, 1,
     [(0,(-42,44,-60),(0,1,-3)),(7,(-30,36,-50),(0,1,-3))], fov=52)
shot('02-orchard-run', 8, (0,.15,-38), -1.13, 2,
     [(0,(4.5,3,-7),(0,.8,1)),(8,(3,2,-5),(0,.8,1))],
     route=[(0,.15,-38),(-15,1.15,-31),(-25,2.15,-17),(-28,3.15,2)])
shot('03-moonwell', 6, (-6,1.15,-3.3), 2.7, 1,
     [(0,(8,6,-14),(-4,1.5,0)),(6,(2,4,-10),(-4,1.5,0))], fov=46)
shot('04-basket', 5, (-19,1.88,-20.5), 0, 2,
     [(0,(2.5,1.6,-3.4),(0,.6,1.2)),(5,(3.2,2,-3.8),(0,.6,1.2))], actions=[(90,91,0,0,1)], fov=48)
shot('05-enemy', 5, (-30,3.17,2.5), 0, 1,
     [(0,(-25,6,-1),(-30,3.8,4.2)),(5,(-26.5,5,-.5),(-30,3.8,4.2))],
     route=[(-30,3.17,2.5),(-30,3.17,3.4)], actions=[(65,66,0,0,1)], fov=50)
shot('06-charge-jump', 5, (-7,5.15,27), math.pi/2, 2,
     [(0,(-3,1.8,-1.5),(1,.7,0)),(5,(-3.5,2,-1.5),(1,.7,0))],
     route=[(-7,5.15,27),(7,5.15,27),(22,4.15,17)], actions=[(10,240,1,0,0),(100,125,0,1,0)], fov=55)
shot('07-conservatory', 7, (-7,5.15,27), math.pi/2, 1,
     [(0,(0,20,15),(0,10,27)),(7,(-5,19,15),(0,10,27))],
     route=[(-7,5.15,27),(0,5.15,29)], fov=52)
shot('08-spyro', 6, (-6,1.15,-3.3), 2.7, 1,
     [(0,(-3.2,2.1,-6.6),(-6,1.55,-3.3)),(6,(-3.8,2,-6.2),(-6,1.55,-3.3))], fov=48)
shot('09-bridge', 7, (-23,4,15), 2.13, 2,
     [(0,(4,3,4),(0,.5,0)),(7,(3,2,3),(0,.5,0))],
     route=[(-23,4,15),(-15,2.6,10),(-8,1.5,5),(-3,1.08,1)], fov=50)

# Glyph pixels, spacing, vertical offsets and palette tints come from retail.
exe = (ROOT/'data/disc/SCUS_944.25').read_bytes()
with (ROOT/'data/disc/WAD.WAD').open('rb') as f:
    entries=wad_index(f.read(2048),f.seek(0,2));e=entries[17];f.seek(e.offset);archive=f.read(e.size)
entry=wad_index(archive,len(archive))[0];vram=archive[entry.offset:entry.offset+0x80000]
def offset(address): return address-0x80010000+0x800
def word(x,y): return struct.unpack_from('<H',vram,(y*512+x-512)*2)[0]
def glyph(c, color):
    d=struct.unpack_from('<H',exe,offset(0x800636a4)+ord(c)*2)[0];index=d&255
    if index==255:return None,10,0
    tint=struct.unpack_from('<I',exe,offset(0x800637d4)+color*4)[0]
    im=Image.new('RGBA',(12,9));u=(index&31)*12;v=index//32*9
    for y in range(9):
        for x in range(12):
            xx=(u+x)&255;p=word(512+xx//4,256+v+y);p=word((0x4922&63)*16+(p>>(xx%4*4)&15),0x4922>>6)
            rgb=[min(255,(((p>>(k*5)&31)<<3)|(p>>(k*5)&31)>>2)*(tint>>(k*8)&255)//128) for k in range(3)]
            im.putpixel((x,y),(*rgb,255 if p else 0))
    dy=(d>>12)&15;dy=dy-16 if dy>=8 else dy
    return im,(d>>8)&15,dy

def text_image(text,color=1,scale=6):
    pieces=[glyph(c,color) for c in text];w=sum(a for _,a,_ in pieces)+12
    im=Image.new('RGBA',(w,24));x=0
    for g,a,dy in pieces:
        if g is not None:im.alpha_composite(g,(x,dy+8))
        x+=a
    bounds=im.getbbox();im=im.crop(bounds) if bounds else im
    # Retail uses a 512-wide UI coordinate space displayed at 320 pixels.
    return im.resize((round(im.width*scale*.625),im.height*scale),Image.Resampling.LANCZOS)

def card(name, rows):
    canvas=Image.new('RGBA',(1920,1080))
    for label,y,scale,color in rows:
        im=text_image(label,color,scale);x=(1920-im.width)//2
        shadow=Image.new('RGBA',im.size,(0,0,0,0));shadow.putalpha(im.getchannel('A'))
        canvas.alpha_composite(shadow,(x+4,y+5));canvas.alpha_composite(im,(x,y))
    canvas.save(OUT/'titles'/f'{name}.png')

card('01-opening',[('SOMETHING STIRS',435,7,1),('BENEATH THE BLOSSOMS',530,6,3)])
card('02-discover',[('A FORGOTTEN SPRING',830,6,1)])
card('03-play',[('FOLLOW THE GEMS',830,6,1)])
card('04-title',[('HOLLOWBLOOM',390,15,1),('SPRINGS',550,13,1),('AN ORIGINAL SPYRO 2 PC WORLD',740,5,3)])
card('05-end',[('EXPLORE HOLLOWBLOOM SPRINGS',865,4,1),('FAN-MADE LEVEL  /  WORK IN PROGRESS',940,3,3)])
card('06-art-credit',[('ILLUSTRATED SEQUENCES CREATED WITH GPT IMAGE',974,2,3)])
card('07-native-credit',[('CAPTURED IN THE SPYRO 2 PC WORKBENCH',974,2,3)])
print('Authored 9 native shots and 7 retail-font overlays in',OUT)
