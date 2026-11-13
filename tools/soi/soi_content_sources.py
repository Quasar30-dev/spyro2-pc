"""Read-only ROM reference frames and code evidence for the content continuation.

Exports all frames, source positions and palettes; does not invent clip labels,
character placement or behavior. Includes Grendor's original arena-only bank.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

import capstone
import numpy as np
from PIL import Image,ImageDraw

from gba import Rom,ROM_PATH,ROM_SHA256
import soi_sprites as sprites


ROOT=Path(__file__).resolve().parents[2]


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out',type=Path,default=ROOT/'data/soi/content-20261105')
    a=ap.parse_args(); a.out.mkdir(parents=True,exist_ok=True)
    rom=Rom(); digest=hashlib.sha256(rom.data).hexdigest(); assert digest==ROM_SHA256
    functions=sprites.functions(); src=sprites.slot_sources(rom,functions); palettes=sprites.obj_palettes(rom,functions)
    index={'rom':str(ROM_PATH),'sha256':digest,'banks':[],
           'limits':['Frame numbers retained; semantic state/direction labels require original state-table analysis.',
                     'Only original initializer palettes are bound; effect/state palette changes remain unproven.',
                     'Grendor reference export is not an authored model or in-game integration.']}
    print('ORIGINAL',ROM_PATH,'sha256',digest,flush=True)
    for level,slot,name in [(6,0x16,'return-portal'),(6,0x23,'critter'),(6,0x25,'spear-guard'),
                            (6,0x26,'robed-rhynoc'),(15,0x25,'grendor-arena15'),(16,0x25,'grendor-arena16')]:
        sources=dict(src[None]);sources.update(src.get(level,{})); addr,raw_size=sources[slot]
        bank=sprites.bank(rom,addr); folder=a.out/name;folder.mkdir(exist_ok=True)
        palette_bank=sprites.INITIAL_PALETTES[(level,slot)]
        frames=[]; ims=[]
        for i in range(bank['n']):
            rgba,dx,dy=sprites.frame_image(bank,i,palettes[level],palette_bank); im=Image.fromarray(rgba)
            path=folder/f'frame-{i:03d}.png';im.save(path)
            frames.append({'frame':i,'frame_record':bank['frames'][i],'origin':[dx,dy],
                           'dimensions':[im.width,im.height],'palette_bank':palette_bank,
                           'path':str(path.resolve()),'rgba_sha256':hashlib.sha256(rgba.tobytes()).hexdigest()})
            ims.append(im)
        # Paginate to retain readable every-frame coverage without huge images.
        sheets=[]
        for page in range(0,len(ims),48):
            subset=ims[page:page+48]; w=max(im.width for im in subset)*2+12; h=max(im.height for im in subset)*2+26
            sheet=Image.new('RGB',(w*6,h*((len(subset)+5)//6)),(28,32,42));draw=ImageDraw.Draw(sheet)
            for j,im in enumerate(subset):
                x,y=(j%6)*w,(j//6)*h;big=im.resize((im.width*2,im.height*2),Image.Resampling.NEAREST)
                sheet.paste(big,(x+6,y+20),big);draw.text((x+5,y+4),f'{name} #{page+j}',fill='white')
            path=a.out/f'{name}-all-frames-{page//48:02d}.png';sheet.save(path);sheets.append(str(path.resolve()))
        row={'name':name,'level':level,'slot':slot,'source':hex(addr),'source_size':raw_size,
             'header':bank['header'],'frames':frames,'sheets':sheets,
             'initial_palette_bank':palette_bank,
             'palette_sha256':hashlib.sha256(palettes[level].tobytes()).hexdigest()}
        index['banks'].append(row)
        print('EXPORTED',name,'original bank',hex(addr),'frames',len(frames),'sheets',len(sheets),flush=True)
    requested=['080170a4','0801c474','0801d428','0801d6d8','0801d764','0801d8a4',
               '08021ec0','08021f88','080221c4','080223f0','08022958','08022980','080229b0',
               '0801fbc4','0801fd3c','0801fec0','0802be90','0802c478','0803398c','080374e8',
               '0803283c','08033b80','08033c78','08033eb4']
    md=capstone.Cs(capstone.CS_ARCH_ARM,capstone.CS_MODE_THUMB)
    for address in requested:
        text=functions.get(address)
        if not text:continue
        literals={k:hex(rom.u32(int(k[4:],16))) for k in sorted(set(re.findall('DAT_[0-9a-f]{8}',text)))}
        (a.out/f'{address}.c').write_text(text+'\n// Original ROM literals\n'+json.dumps(literals,indent=2))
        start=int(address,16)
        # Bound the excerpt by the next exported entry, not a guessed body size.
        end=min((int(k,16) for k in functions if int(k,16)>start),default=start+256)
        dis='\n'.join(f'{i.address:08x} {i.bytes.hex():8} {i.mnemonic} {i.op_str}' for i in md.disasm(rom.bytes(start,end-start),start))
        (a.out/f'{address}.thumb.txt').write_text(dis)
    level=json.loads((ROOT/'data/soi/levels/06-market-mesa/level.json').read_text())
    values=list(rom.bytes(rom.u32(0x801c5dc),4)); assert values==[1,2,5,10]
    actors=[]
    for obj in level['objects']['objects']:
        if obj.get('gem_value'):continue
        row=dict(obj); family=obj['family']; typ=obj['type']
        if family.startswith('bg_container'):
            row['drop_gem_value']=values[(typ+(0x7d if typ<135 else 0x78))&3]
            row['evidence']='0801fec0 -> 0801c474 -> 081ab8f0'
        elif family=='enemy_180':
            row['drop_gem_value']=values[(typ+0x5f)&3];row['evidence']='0802be90 +0x5f -> 0801c474'
        elif family=='enemy_185':
            row['drop_gem_value']=values[(typ+0x46)&3];row['evidence']='0802c478 +0x46 -> 0801c474'
        elif family=='critter':
            row['drop_gem_value']=None;row['evidence']='08021ec0 literal08021f78 -> type16 table081abcf8 -> 08022a60 -> 08022958 -> 08022980 -> 080374e8 health/butterfly'
            row['limitation']='Original type16 health/butterfly dispatch is bound. Three-actor PS1/PC runtime installation remains unfinished; no gem drop is inferred.'
        if family.startswith('enemy'):
            row['drop_value_basis']='Original already-killed factory path; live death dispatch and persistence still require proof.'
        actors.append(row)
    index['market_mesa_actors']=actors
    index['container_total']=sum(e.get('drop_gem_value') or 0 for e in actors if e['family'].startswith('bg_container'))
    index['enemy_drop_total']=sum(e.get('drop_gem_value') or 0 for e in actors if e['family'].startswith('enemy'))
    path=a.out/'source-manifest.json';path.write_text(json.dumps(index,indent=2))
    print('SOURCE MANIFEST',path.resolve(),'sha256',hashlib.sha256(path.read_bytes()).hexdigest(),flush=True)
    print('MEASURED source container gems',index['container_total'],'enemy gems',index['enemy_drop_total'],
          'loose gems',sum(e.get('gem_value',0) for e in level['objects']['objects']),flush=True)
    print('LIMIT semantic clip/palette changes, exact portal gate/save binding, model conversion and runtime behavior remain unfinished; type41 is not an ice barrier',flush=True)


if __name__=='__main__':main()
