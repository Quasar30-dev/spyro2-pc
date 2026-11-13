"""Decode resident interface/font descriptors from the mounted US retail assets."""
from pathlib import Path
import struct
from PIL import Image, ImageDraw
from spyrodisc import wad_index

root=Path(__file__).resolve().parents[1]
out=root/'data/analysis/interface';out.mkdir(parents=True,exist_ok=True)
exe=(root/'data/disc/SCUS_944.25').read_bytes()
f=(root/'data/disc/WAD.WAD').open('rb');entries=wad_index(f.read(0x10000),f.seek(0,2))
def bank(slot):
 e=entries[slot];f.seek(e.offset);return f.read(e.size)
archive=bank(17);child=wad_index(archive,len(archive))[0];vram=bytearray(archive[child.offset:child.offset+0x80000])
def word(x,y):
 assert 512<=x<1024 and 0<=y<512,(x,y)
 return struct.unpack_from('<H',vram,(y*512+x-512)*2)[0]
def texture(page,u,v,clut,w,h):
 image=Image.new('RGBA',(w,h));depth=(page>>7)&3
 for y in range(h):
  for x in range(w):
   xx=(u+x)&255; yy=(v+y)&255
   p=word((page&15)*64+xx//(2 if depth else 4),((page>>4)&1)*256+yy)
   index=(p>>((xx%(2 if depth else 4))*(8 if depth else 4)))&(255 if depth else 15)
   c=word((clut&63)*16+index,clut>>6)
   image.putpixel((x,y),tuple(((c>>(k*5))&31)*255//31 for k in range(3))+(255 if c else 0,))
 return image
def offset(address):return address-0x80010000+0x800
font=Image.new('RGBA',(16*24,8*20));draw=ImageDraw.Draw(font)
for c in range(128):
 glyph=struct.unpack_from('<H',exe,offset(0x800636a4)+c*2)[0]
 if glyph&255==255:continue
 index=glyph&255
 im=texture(0x618,((index&31)*12)&255,(index>>5)*9,0x4962 if c in (60,62,123,125) else 0x4922,12,9)
 x,y=c%16*24,c//16*20;font.paste(im,(x,y),im);draw.text((x,y+9),str(c),fill='white')
font.resize((768,320)).save(out/'font.png')

def upload(raw,width,height):
 for y in range(height):vram[(128+y)*1024:(128+y)*1024+width*2]=raw[y*width*2:(y+1)*width*2]
for title,raw,width,height,base,count in [('pause',archive[child.offset+0x80000:child.offset+0x86000],192,64,0x8006387c,26),('book',bank(4)[:0xc000],256,96,0x80063e38,17),('glimmer',bank(4)[2*0xc000:3*0xc000],256,96,0x80063ec0,22)]:
 upload(raw,width,height)
 sheet=Image.new('RGBA',(6*270,4*260),(55,40,70,255));draw=ImageDraw.Draw(sheet)
 for i in range(count):
  a,v,b,c,w,h=exe[offset(base)+i*6:offset(base)+i*6+6]
  if not w or not h:continue
  depth=int((title=='book' and i<3) or (title=='glimmer' and i in (0,14,18)))
  im=texture((a>>6)+8+depth*128,(a&63)*(2 if depth else 4),(v+128)&255,(b&127)+c*64+0x2020,w,h)
  im.save(out/f'{title}-sprite-{i:02}.png')
  x,y=i%6*270,i//6*260;sheet.paste(im,(x,y+16),im);draw.text((x,y),f'{i}: {w}x{h}',fill='white')
 sheet.save(out/f'{title}-sprites.png')
print(out)
