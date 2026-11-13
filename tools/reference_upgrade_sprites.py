"""Export retail HUD references for the original PC upgrade artwork (local disc only)."""
from pathlib import Path
import struct
from PIL import Image, ImageDraw
from spyrodisc import wad_index

root = Path(__file__).resolve().parents[1]
out = root / 'assets/ui/upgrades/references'
out.mkdir(parents=True, exist_ok=True)
exe = (root / 'data/disc/SCUS_944.25').read_bytes()
with (root / 'data/disc/WAD.WAD').open('rb') as f:
    entries = wad_index(f.read(0x10000), f.seek(0, 2))
    f.seek(entries[17].offset)
    archive = f.read(entries[17].size)
child = wad_index(archive, len(archive))[0]
vram = archive[child.offset:child.offset+0x80000]
def word(x, y):
    return struct.unpack_from('<H', vram, (y*512+x-512)*2)[0]
types = [0]*10+[2]+[1]*11+[5]*4+[6,7,4,2,2,9]+[10]*4
ids = [0,1,2,11,12,13,17,19,20,21,22,25,26,27,31,32]
sheet = Image.new('RGBA', (1024, 640), (40, 29, 61, 255))
d = ImageDraw.Draw(sheet)
for n, i in enumerate(ids):
    p = 0x53d1c+i*4
    u,v,b,c = exe[p:p+4]
    w,h = exe[0x53cb8+types[i]],exe[0x53cd0+types[i]]
    clut = 0x4020+(b&3)+c*64
    im = Image.new('RGBA', (w,h))
    for y in range(h):
        for x in range(w):
            xx,yy = (u+x)&255,(v+y)&255
            index = (word(512+xx//4,256+yy)>>((xx&3)*4))&15
            color = word((clut&63)*16+index,clut>>6)
            im.putpixel((x,y),tuple((((color>>(k*5))&31)<<3)|(((color>>(k*5))&31)>>2) for k in range(3))+(255 if color else 0,))
    im.save(out/f'retail-{i:02}.png')
    scale = min(4, 128//max(w,h))
    im = im.resize((w*scale,h*scale),Image.Resampling.NEAREST)
    x,y = n%8*128,n//8*180
    sheet.alpha_composite(im,(x+(128-im.width)//2,y+28))
    d.text((x+8,y+8),f'Sprite {i} / {w}x{h}',fill=(232,218,170))
for n, i in enumerate([15,16,17,18,21,23]):
    im=Image.open(root/f'data/analysis/interface/pause-sprite-{i:02}.png').convert('RGBA')
    im=im.resize((im.width*3,im.height*3),Image.Resampling.NEAREST)
    sheet.alpha_composite(im,(n*160,410))
d.text((10,595),'SPYRO 2 RETAIL / direct 4-bit CLUT decode / nearest-neighbour enlargement',fill=(232,218,170))
sheet.save(out/'retail-hud-reference.png')
print(out/'retail-hud-reference.png')
