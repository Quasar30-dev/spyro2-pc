"""Inspect the player's retail title sprites and cinematic. Generated assets stay in data/."""
from pathlib import Path
import struct
from PIL import Image, ImageDraw
from spyrodisc import wad_index

root = Path(__file__).resolve().parents[1]
out = root / 'data/analysis/title'
out.mkdir(parents=True, exist_ok=True)
exe = (root / 'data/disc/SCUS_944.25').read_bytes()
with (root / 'data/disc/WAD.WAD').open('rb') as stream:
    entries = wad_index(stream.read(0x10000), stream.seek(0, 2))
    def read(slot):
        e = entries[slot]; stream.seek(e.offset); return stream.read(e.size)
    title = read(95)
    movie = read(96)
    (out / 'title-cinematic.bin').write_bytes(movie)
children = wad_index(title[:0x800], len(title))
vram = title[children[0].offset:children[0].offset+0x80000]
def word(x, y):
    assert 512 <= x < 1024 and 0 <= y < 512, (x, y)
    return struct.unpack_from('<H', vram, (y*512+x-512)*2)[0]
sheet = Image.new('RGB', (768, 26*92), '#322143')
draw = ImageDraw.Draw(sheet)
for i in list(range(28))+list(range(32,75)):
    at = (0x80063f44 if i < 32 else 0x80063f2c) - 0x80010000 + 0x800 + i*6
    a,v,b,c,w,h = exe[at:at+6]
    page = (a>>6) + (0x98 if i < 32 else 0x1c)
    u = (a&63)*(2 if i < 32 else 4)
    clut = (b&127)+c*64+0x4020+(0 if i<32 else 16)
    if (clut&63)*16 < 512 or (clut>>6)>=512:
        print('Unused sprite', i); continue
    depth=(page>>7)&3
    im=Image.new('RGBA',(w,h))
    for y in range(h):
        for x in range(w):
            p=word((page&15)*64+(u+x)//(2 if depth else 4),((page>>4)&1)*256+v+y)
            index=(p>>(((u+x)%(2 if depth else 4))*(8 if depth else 4)))&(255 if depth else 15)
            color=word((clut&63)*16+index,clut>>6)
            im.putpixel((x,y),tuple(((color>>(k*5))&31)*255//31 for k in range(3))+(255 if color else 0,))
    im.save(out/f'sprite-{i:02}.png')
    xx,yy=i%3*256,i//3*92
    sheet.paste(im,(xx,yy+14),im)
    draw.text((xx,yy),f'{i}: {w}x{h}',fill='white')
sheet.save(out/'sprites.png')
print('Cinematic header',struct.unpack_from('<5I',movie))
print('Camera/chunk pointers', [hex(x) for x in struct.unpack_from('<30I',movie,20)])
print('Sprites:',out/'sprites.png')
