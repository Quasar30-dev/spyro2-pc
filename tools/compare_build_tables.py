"""Locate unchanged data blocks and record changed animation/header layouts."""
from pathlib import Path
import json,struct
ROOT=Path(__file__).resolve().parents[1]
retail=(ROOT/'data/disc/SCUS_944.25').read_bytes()
tables={'loops':(0x80060334,248),'transitions':(0x8006042c,3844),'secondary':(0x80061330,64),
 'secondary_clips':(0x80066d54,8),'codebook':(0x80062674,500),'blends':(0x80062868,256),
 'sine':(0x80061bd8,512),'cosine':(0x80061c58,512),'camera_follow':(0x80061560,60),
 'camera_swim':(0x800615d8,20),'camera_states':(0x80061468,248),'camera_spring':(0x8006172c,20),
 'hud_enter':(0x80063430,24),'hud_exit':(0x80063448,24),'hud_sprites':(0x8006351c,55*4),
 'font':(0x800636a4,256),'font_colors':(0x800637d4,56),'gem_values':(0x80064428,5),
 'gem_variants':(0x8006440c,28),'purchase_prices':(0x80064670,80),'environment_radii':(0x800625ac,129),
 'environment_angles':(0x80062630,65),'sparx_glow':(0x8006430c,256),'sparx_colors':(0x80061adc,16)}
out=ROOT/'data/analysis/prerelease-20260930';out.mkdir(exist_ok=True)
report={}
for build in ['july-1999','august-1999']:
 root=ROOT/'data/builds'/build;exe=(root/'disc/SPYRO2.EXE').read_bytes();found={}
 for name,(address,size) in tables.items():
  at=address-0x8000f800;pattern=retail[at:at+size]
  hits=[];start=0
  while True:
   p=exe.find(pattern,start)
   if p<0:break
   hits.append(hex(p+0x8000f800));start=p+1
  prefix=[]
  if not hits and size>=24:
   short=pattern[:24];start=0
   while True:
    p=exe.find(short,start)
    if p<0:break
    prefix.append(hex(p+0x8000f800));start=p+1
  found[name]={'retail':hex(address),'bytes':size,'exact_matches':hits,'prefix24_matches':prefix}
 idx=json.loads((root/'wad-index.json').read_text());f=(root/'disc/WAD.WAD').open('rb');e=idx[0];f.seek(e['offset']);g=f.read(e['size']);at=struct.unpack_from('<I',g,4)[0]
 clips=[]
 for i,p in enumerate(struct.unpack_from('<62I',g,at+60)):
  if p==0xffffffff:continue
  desc=struct.unpack_from('<4B4I',g,at+p);clips.append({'id':i,'offset':at+p,'frames':desc[0],'colors':desc[1],'split':desc[2],'vertices':desc[3]})
 scenes=[]
 for e in idx:
  if e['size']<0x80000:continue
  f.seek(e['offset']);header=f.read(0x200)
  words=struct.unpack('<128I',header)
  if words[0]!=0x800 or words[1]<0x80000:continue
  valid=all(words[2*i]>=0x800 and words[2*i]+words[2*i+1]<=e['size'] for i in range(4))
  if valid:scenes.append({'slot':e['id'],'bytes':e['size'],'first_child_bytes':words[1],
     'models_at_0x38':hex(words[14]),'models_at_0x40':hex(words[16])})
 report[build]={'tables':found,'global_clips':clips,'level_candidates':scenes}
 print(build,'scenes',[s['slot'] for s in scenes]);print('clips',[(c['id'],c['frames'],c['vertices']) for c in clips])
 for n,t in found.items():print(n,t['exact_matches'] or ('prefix',t['prefix24_matches']))
(out/'table-comparison.json').write_text(json.dumps(report,indent=2))
