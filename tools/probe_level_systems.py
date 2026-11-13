"""Inventory every campaign scene and export bounded MIPS case evidence.

This reports authored records, not a claim that their native gameplay exists.
"""
import collections,json,struct,sys
from pathlib import Path
from capstone import Cs,CS_ARCH_MIPS,CS_MODE_MIPS32,CS_MODE_LITTLE_ENDIAN
from spyrodisc import wad_index,u32
from probe_cut_behaviour import trace,BASE

root=Path(sys.argv[1] if len(sys.argv)>1 else 'data/disc')
out=Path('data/analysis/level-systems');out.mkdir(parents=True,exist_ok=True)
wad=(root/'WAD.WAD').read_bytes();exe=(root/'SCUS_944.25').read_bytes();slots=wad_index(wad[:2048],len(wad))
bank=lambda s:wad[slots[s].offset:slots[s].offset+slots[s].size]
profiles=json.loads(Path('data/analysis/powerups.json').read_text())['scenes']
cs=Cs(CS_ARCH_MIPS,CS_MODE_MIPS32|CS_MODE_LITTLE_ENDIAN)
result=[]
for profile in profiles[:29]:
 slot=profile['scene_slot'];b=bank(slot);d=b[u32(b,24):];at=0x2c
 for _ in range(8):at+=u32(d,at)
 records=[];groups=collections.defaultdict(list)
 for i in range(u32(d,at+4)):
  p=at+8+88*i;k=struct.unpack_from('<H',d,p+54)[0];q=u32(d,p)
  record=dict(id=i,type=f'{k:03x}',parameter=q,position=struct.unpack_from('<iii',d,p+12),rotation=list(d[p+68:p+71]),state=d[p+72],variant=d[p+73],held=d[p+80],spirit=d[p+83])
  records.append(record);groups[k].append(i)
 code=bank(slot-1);dispatch=int(profile['callbacks']['objects'],16);spawn=int(profile['callbacks']['spawn_object'],16)
 cases=[]
 if slot in (29,39,43,53,63):
  traced=[trace(code,dispatch,k) for k in range(0x420)]
  starts=sorted(set(int(r['entry'],16) for r in traced))
  for start,end in zip(starts,starts[1:]+[spawn]):
   if not BASE<=start<end<=BASE+len(code):continue
   kinds=[int(r['class'],16) for r in traced if int(r['entry'],16)==start]
   if not any(k in groups for k in kinds):continue
   name=f'{slot-1:03d}_{start:08x}.txt'
   (out/name).write_text('\n'.join(f'{i.address:08x} {i.mnemonic:8} {i.op_str}' for i in cs.disasm(code[start-BASE:end-BASE],start))+'\n')
   cases.append(dict(classes=[f'{k:03x}' for k in kinds if k in groups],start=f'{start:08x}',end=f'{end:08x}',file=name))
  (out/f'overlay_{slot-1:03d}.bin').write_bytes(code)
 result.append(dict(slot=slot,name=profile['name'],objects=len(records),classes={f'{k:03x}':v for k,v in groups.items()},records=records,gates=profile['gates'],cases=cases))
(out/'inventory.json').write_text(json.dumps(result,indent=2)+'\n')
print('Inventoried',len(result),'campaign scenes,',sum(r['objects'] for r in result),'records; exported five overlay case maps.')
