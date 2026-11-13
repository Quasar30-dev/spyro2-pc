"""Original slot-17 VRAM ownership, in halfwords, with conservative reservations.

Never calls an unclassified word free. Model faces include every static pose,
animated near/far face stream and every frame-dependent face patch. The bank
layout is from original loaders 80070018/8004753c and 80044618; the Python
decoder is an audit of their inputs, not evidence that those consumers ran.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from collections import Counter
from pathlib import Path

import numpy as np
from PIL import Image,ImageDraw

import mesa_ps1 as mesa
from s2scene import read_archive
from spyrodisc import wad_index


def u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


def i32(b, o):
    return struct.unpack_from('<i', b, o)[0]


class Ownership:
    def __init__(self):
        self.masks = {}
        self.references = Counter()

    def rect(self, owner, x, y, w, h):
        assert 512 <= x < x+w <= 1024 and 0 <= y < y+h <= 512, (owner,x,y,w,h)
        self.masks.setdefault(owner, np.zeros((512,512), bool))[y:y+h,x-512:x+w-512] = True
        self.references[owner] += 1

    def palette(self, owner, clut, n, h=1):
        self.rect(owner, (clut & 63)*16, (clut >> 6)&511, n, h)

    def texture(self, owner, page, clut, uv, palette=True):
        depth = (page >> 7)&3
        assert depth <= 1, (owner,hex(page))
        ppw = 2 if depth else 4
        uv = np.asarray(uv, int)
        lo, hi = uv.min(0), uv.max(0)
        x = (page&15)*64 + lo[0]//ppw
        y = ((page>>4)&1)*256 + lo[1]
        self.rect(owner, int(x), int(y), int(hi[0]//ppw-lo[0]//ppw+1), int(hi[1]-lo[1]+1))
        if palette:
            self.palette(owner,clut,256 if depth else 16)

    def faces(self, b, at, owner, force_textured=False):
        end = at+4+u32(b,at)
        assert at+4 <= end <= len(b), (owner,hex(at),hex(end),len(b))
        p = at+4
        while p < end:
            assert p+8 <= end
            textured = force_textured or bool(u32(b,p+4)&0x80000000)
            n = 20 if textured else 8
            assert p+n <= end
            if textured:
                uv = [(b[p+k],b[p+k+1]) for k in (8,12,16,18)]
                self.texture(owner,struct.unpack_from('<H',b,p+14)[0],
                             struct.unpack_from('<H',b,p+10)[0],uv)
            p += n
        assert p == end

    def model(self, b, owner):
        count = i32(b,0)
        assert count and abs(count) <= 255, (owner,count)
        if count < 0:
            common = u32(b,16)
            for j in range(-count):
                pose = u32(b,20+j*4)
                self.faces(b,common+struct.unpack_from('<H',b,pose+8)[0],owner)
            return
        common, patches = u32(b,56), u32(b,52)
        seen = set()
        for clip in range(count):
            desc = u32(b,60+clip*4)
            assert desc+36+b[desc]*8 <= len(b)
            controls = [u32(b,desc+36+f*8) for f in range(b[desc])]
            for offset in (16,24):
                face = common+u32(b,desc+offset)
                if offset == 24 and not b[desc+5]:
                    continue
                if face not in seen:
                    self.faces(b,face,owner)
                    seen.add(face)
                for control in controls:
                    if control&0x80000000:
                        continue
                    variant = (face, (control>>24)&0x7c)
                    if variant in seen:
                        continue
                    seen.add(variant)
                    image = bytearray(b[face:face+4+u32(b,face)])
                    p = patches+u32(b,patches+variant[1])
                    for run in range(4096):
                        cmd = u32(b,p); p += 4
                        words, start = (cmd>>24)&127, cmd&65535
                        assert words and start%4 == 0 and 4 <= start <= start+4*words <= len(image)
                        assert p+4*words <= len(b)
                        image[start:start+4*words] = b[p:p+4*words]; p += 4*words
                        if cmd&0x80000000:
                            break
                    else:
                        raise AssertionError('unterminated patches')
                    self.faces(image,0,owner)


def inventory():
    arc = read_archive(mesa.DISC,17)
    children = wad_index(arc[:65536],len(arc))
    sc = mesa.s2scene_build.split(arc[children[1].offset:children[1].offset+children[1].size])
    own = Ownership()
    for m in range(u32(sc['parts'][0],4)):
        for k in range(6):
            d = sc['parts'][0][8+m*48+k*8:16+m*48+k*8]
            clut = struct.unpack_from('<H',d,2)[0]
            own.texture('terrain pixels',d[6],clut,[(d[0],d[1]),(d[4],d[5])],False)
            own.palette('terrain near palettes' if k>=2 else 'terrain fade palettes',clut,256 if k>=2 else 16,1 if k>=2 else 16)
    for j in range(1,63):
        start,end = u32(arc,64+j*4),u32(arc,68+j*4)
        if not end:
            break
        assert children[2].offset <= start < end <= children[2].offset+children[2].size
        cls = struct.unpack_from('<H',arc,0x140+j*2)[0]
        own.model(arc[start:end],f'model {cls:03x}')
    # All common Spyro clips use 20-byte textured polygons, original 80041dbc.
    global_bank = read_archive(mesa.DISC,0)
    model = u32(global_bank,4); common = model+u32(global_bank,model+56)
    for clip in range(u32(global_bank,model)):
        rel = u32(global_bank,model+60+clip*4)
        if rel == 0xffffffff:
            continue
        desc = model+rel
        own.faces(global_bank,common+u32(global_bank,desc+8),'Spyro common clips',True)
    # The level-local Spyro extra clips use the same format (directory index 0).
    local = u32(arc,64)
    for guard in range(63):
        clip = u32(arc,local)
        if clip == 0xffffffff:
            break
        size = u32(arc,local+4)
        assert clip < 62 and local+8+size <= children[2].offset+children[2].size
        # Extra records change vertices/streams; their face/palette offsets
        # still address the global common geometry (80041dbc).
        own.faces(global_bank,common+u32(arc,local+8+8),'Spyro scene clips',True)
        local += 8+size
    else:
        raise AssertionError('unterminated scene Spyro records')
    # Resident sprite page and font are reserved in full, covering dynamic sprite
    # descriptors as well as the observed resident tables. No holes are allocated.
    own.rect('resident sprites and effects page',512,256,64,256)  # TPAGE 0x38
    own.rect('font page',512,256,64,256)  # 0x618, 4bpp; includes every original glyph
    own.palette('font palettes',0x4922,16)
    own.palette('font palettes',0x4962,16)
    g = children[3].offset
    environment = u32(arc,g+16)
    if environment:
        own.texture('environment material full page',environment&0x1ff,environment>>16,[(0,0),(255,255)])
    flame0,flame1 = u32(arc,g+20),u32(arc,g+24)
    own.texture('flame',flame1>>16,flame0>>16,[(flame0&255,(flame0>>8)&255),
                (flame1&255,(flame1>>8)&255)])
    # Paused guidebook uploads deliberately reuse terrain VRAM in retail.
    # Reserve their whole upload destination even while the scene is active,
    # so this pack does not depend on an unproven menu-restoration lifetime.
    own.rect('pause and guidebook uploads',512,128,256,96)
    # Reserve every original non-terrain word, including overlay-defined sprite
    # CLUTs, reflection materials and paused guidebook upload destinations. This
    # deliberately makes no claim of exhaustive symbolic identification.
    terrain = np.logical_or.reduce([v for k,v in own.masks.items() if k.startswith('terrain')])
    own.masks['unclassified reserved'] = ~terrain
    nonterrain = np.logical_or.reduce([v for k,v in own.masks.items() if not k.startswith('terrain') and k!='unclassified reserved'])
    safe = terrain & ~nonterrain
    return arc, own, terrain, safe


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out',type=Path,required=True)
    a = ap.parse_args(); a.out.mkdir(parents=True,exist_ok=True)
    arc,own,terrain,safe = inventory()
    summary = {'original_archive_sha256':hashlib.sha256(arc).hexdigest(),
               'owners':{k:{'words':int(v.sum()),'references':own.references[k],
                            'terrain_overlap':int((v&terrain).sum()) if not k.startswith('terrain') else None}
                         for k,v in own.masks.items()},
               'original_terrain_words':int(terrain.sum()),'safe_terrain_words':int(safe.sum()),
               'unknown_words_are_reserved':True,
               'limitation':'Unclassified words reserved; symbolic ownership of all overlay/global consumers remains incomplete.'}
    (a.out/'vram-ownership.json').write_text(json.dumps(summary,indent=2))
    np.savez_compressed(a.out/'vram-ownership.npz',**{k.replace(' ','_'):v for k,v in own.masks.items()},safe=safe)
    pixels=np.full((512,512,3),42,np.uint8)
    groups=[('original terrain',terrain,(50,100,180)),
            ('object models',np.logical_or.reduce([v for k,v in own.masks.items() if k.startswith('model ')]),(210,130,55)),
            ('Spyro',np.logical_or.reduce([v for k,v in own.masks.items() if k.startswith('Spyro')]),(175,80,200)),
            ('resident sprite/font pages',own.masks['resident sprites and effects page']|own.masks['font page'],(220,200,70)),
            ('flame',own.masks['flame'],(70,210,130)),
            ('menu uploads reserved',own.masks['pause and guidebook uploads'],(220,70,70))]
    for name,mask,color in groups:pixels[mask]=color
    canvas=Image.new('RGB',(1024,1220),(24,24,28));canvas.paste(Image.fromarray(pixels).resize((1024,1024),Image.Resampling.NEAREST),(0,0))
    draw=ImageDraw.Draw(canvas);y=1032
    for name,mask,color in groups:
        draw.rectangle((8,y,24,y+12),fill=color);draw.text((32,y),f'{name}: {int(mask.sum())} halfwords',fill='white');y+=22
    draw.text((8,y),'x=512..1023 / y=0..511. Dark grey=reserved unclassified. Overlaps use later legend colour.',fill='white')
    canvas.save(a.out/'vram-ownership.png')
    print('ORIGINAL',mesa.DISC/'WAD.WAD','slot17 sha256',summary['original_archive_sha256'])
    for k,v in summary['owners'].items():
        print('OWNER',k,json.dumps(v))
    print('MEASURED terrain words',summary['original_terrain_words'],'safe subset',summary['safe_terrain_words'])
    print('LIMITATION',summary['limitation'])


if __name__ == '__main__':
    main()
