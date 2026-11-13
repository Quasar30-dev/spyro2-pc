"""Reproducible structural evidence, including scene IDs and roll mesh topology."""
from pathlib import Path
import json,struct,re
from spyrodisc import wad_index,u32
from probe_spyro_model import decode
ROOT=Path(__file__).resolve().parents[1]
out=ROOT/'data/analysis/prerelease-20260930'
builds={'retail':(ROOT/'data/disc/SCUS_944.25',ROOT/'data/disc/WAD.WAD'),
 'july-1999':(ROOT/'data/builds/july-1999/disc/SPYRO2.EXE',ROOT/'data/builds/july-1999/disc/WAD.WAD'),
 'august-1999':(ROOT/'data/builds/august-1999/disc/SPYRO2.EXE',ROOT/'data/builds/august-1999/disc/WAD.WAD'),
 'winter-jampack-1999':(ROOT/'data/builds/winter-jampack-1999/disc/SPYRO2/DEMO2.EXE',ROOT/'data/builds/winter-jampack-1999/disc/SPYRO2/WAD.WAD')}
report={}
for name,(exep,wadp) in builds.items():
 exe=exep.read_bytes();wad=wadp.read_bytes();index=wad_index(wad[:0x10000],len(wad))
 def entry(i):e=index[i];return wad[e.offset:e.offset+e.size]
 strings=[{'va':hex(m.start()+0x8000f800),'text':m[0].decode('ascii')} for m in re.finditer(rb'[ -~]{6,}\x00',exe) if any(x in m[0].lower() for x in [b'forest',b'glimmer',b'harbor',b'harbour',b'tundra',b'scorch',b'coloss',b'marsh',b'roll'])]
 scenes=[]
 for e in index:
  if name=='retail' and not (15<=e.index<=95 and e.index%2):continue
  a=entry(e.index)
  if len(a)<0x80000 or u32(a,0)!=0x800 or not 0x80000<=u32(a,4)<len(a):continue
  if not all(u32(a,i*8)>=0x800 and u32(a,i*8)+u32(a,i*8+4)<=len(a) for i in range(4)):continue
  shift=0 if a[32:40]==b'\0'*8 else -32
  clips=[];at=u32(a,0x40+shift)
  for _ in range(63):
   if not at:break
   if at+8>len(a):clips.append(['invalid',at]);break
   i=u32(a,at)
   if i==0xffffffff:break
   if i>=62:clips.append(['invalid_id',i]);break
   clips.append([i,a[at+8]]);at+=8+u32(a,at+4)
  ids=[]
  for i in range(1,63):
   if not u32(a,0x40+shift+(i+1)*4):break
   ids.append(struct.unpack_from('<H',a,0x140+shift+i*2)[0])
  before=entry(e.index-1).ljust(32,b'\0')
  texts=[m[0].decode('ascii') for m in re.finditer(rb'[ -~]{10,}',a[u32(a,24):u32(a,24)+u32(a,28)])][:6]
  scenes.append({'physical_slot':e.index,'overlay_words':[hex(x) for x in struct.unpack_from('<8I',before)],'header_shift':shift,'clips':clips,'model_ids':ids,'texts':texts})
 report[name]={'strings':strings,'scenes':scenes}
 print(name,len(index),'slots',[(x['physical_slot'],x['overlay_words'][0],x['clips'][:2]) for x in scenes])
 if name=='july-1999':
  g=entry(0);lookup=struct.unpack_from('<125I',exe,0x800649d8-0x8000f800)
  roll={}
  for clip in [0,10,11]:
   frames=[]
   for f in range(g[0x800+u32(g,0x800+60+clip*4)]):
    vertices,faces=decode(g,clip,f,lookup);frames.append(vertices)
   roll[clip]={'vertices':frames,'faces':faces,'ticks':[(u32(g,0x800+u32(g,0x800+60+clip*4)+20+f*20)>>18&15)+1 for f in range(len(frames))]}
  (out/'july-roll-meshes.json').write_text(json.dumps(roll))
(out/'layouts.json').write_text(json.dumps(report,indent=2))
