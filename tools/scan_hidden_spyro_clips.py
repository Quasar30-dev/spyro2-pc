"""Search referenced and orphan Spyro descriptors in supplied early banks.

Candidates must share a verified face/palette bank and have bounded frame and
stream fields. A hit is not an animation identification until decoded/viewed.
"""
from pathlib import Path
import sys,json,struct
from spyrodisc import wad_index,u32
ROOT=Path(__file__).resolve().parents[1];report={}
for name,sub in [('july-1999',''),('winter-jampack-1999','SPYRO2/')]:
 path=ROOT/'data/builds'/name/'disc'/(sub+'WAD.WAD');wad=path.read_bytes();index=wad_index(wad[:0x10000],len(wad));entry=index[0];g=wad[entry.offset:entry.offset+entry.size]
 base=u32(g,4);common=base+u32(g,base+56);refs={base+p:i for i in range(u32(g,base)) if (p:=u32(g,base+60+i*4))!=0xffffffff}
 pairs={(u32(g,p+8),u32(g,p+12)) for p in refs};hits=[]
 for p in range(base+60+62*4,len(g)-20,4):
  frames,colors,split,vertices=g[p:p+4]
  if not 1<=frames<=128 or not colors or vertices!=230 or split!=115:continue
  if (u32(g,p+8),u32(g,p+12)) not in pairs:continue
  if p+20+frames*20>len(g) or common+u32(g,p+4)+vertices*4>len(g) or common+u32(g,p+16)>=len(g):continue
  hits.append({'offset':hex(p),'frames':frames,'slot':refs.get(p),'base_vertices':hex(u32(g,p+4)),'stream':hex(u32(g,p+16))})
 variants=[]
 layouts=json.loads((ROOT/'data/analysis/prerelease-20260930/layouts.json').read_text())[name]['scenes']
 for scene in layouts:
  e=index[scene['physical_slot']];a=wad[e.offset:e.offset+e.size];p=u32(a,0x20)
  while p and u32(a,p)!=0xffffffff:
   id=u32(a,p);size=u32(a,p+4)
   if id in [16,24,33,34,35]:variants.append({'physical_slot':e.index,'id':id,'frames':a[p+8]})
   p+=8+size
 report[name]={'global_descriptors':hits,'unreferenced':[x for x in hits if x['slot'] is None],'scene_glide_flight_banks':variants}
 print(name,'referenced candidates',len(hits),'orphans',report[name]['unreferenced'],'scene variants',variants)
(ROOT/'data/analysis/prerelease-20260930/hidden-spyro-scan.json').write_text(json.dumps(report,indent=2))
