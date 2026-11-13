"""Inventory retail portal records, collision triggers and whirlwind parameters."""
import json
import struct
from pathlib import Path
from spyrodisc import wad_index, u32

def probe():
    wad=Path('data/disc/WAD.WAD').read_bytes()
    result=[]
    for e in wad_index(wad,len(wad)):
        if not (15<=e.index<=93 and e.index%2): continue
        b=wad[e.offset:e.offset+e.size]; children=wad_index(b,len(b)); p=children[1].offset
        surfaces=[]
        for n in range(6):
            if n==4:
                for i in range(u32(b,p+4)):
                    q=p+4+u32(b,p+8+i*4)
                    surfaces.append(dict(index=i,type=b[q],words=list(struct.unpack_from('<4I',b,q))))
            p+=u32(b,p)
        p+=12;p+=u32(b,p);p+=u32(b,p)
        portals=[]
        for i in range(u32(b,p+4)):
            q=p+8+i*68
            portals.append(dict(index=i,points=[struct.unpack_from('<3i',b,q+j*12) for j in range(5)],picture=u32(b,q+60),flags=list(b[q+64:q+68])))
        g=b[children[3].offset:children[3].offset+children[3].size];q=0x2c
        for _ in range(8):q+=u32(g,q)
        objects=[]
        for i in range(u32(g,q+4)):
            r=q+8+i*88;t=struct.unpack_from('<H',g,r+54)[0];a=u32(g,r)
            if t not in (0x400,0x1aa) and e.index not in (15,33,57): continue
            objects.append(dict(id=i,type=f'{t:03x}',position=struct.unpack_from('<3i',g,r+12),rotation=list(g[r+68:r+71]),state=struct.unpack_from('<b',g,r+72)[0],parameters=a,words=list(struct.unpack_from('<12I',g,a)) if a and a+48<=len(g) else []))
        p+=u32(b,p);q=p+8;particles=[]
        for _ in range(u32(b,p+4)):
            kind,size=struct.unpack_from('<HH',b,q);particles.append(kind);q+=4+size
        gates=[]
        for o in objects:
            a=o['parameters'];t=o['type']
            if not a or t not in ('077','16d','1b6'): continue
            subtype=g[a]
            if t=='077' and subtype in (12,14,52):
                gates.append(dict(npc=o['id'],kind='purchase',purchase=u32(g,a+0x48),portal=struct.unpack_from('<H',g,a+0x4c)[0],barrier=u32(g,a+0x44)))
            elif t=='16d' and subtype in (9,11,50):
                gates.append(dict(npc=o['id'],kind='orbs' if subtype!=50 else 'ripto-complete',orbs={9:3,11:6,50:0}[subtype],portal=struct.unpack_from('<H',g,a+0x4c)[0],barrier=u32(g,a+0x40),lift=u32(g,a+0x38) if subtype==11 else None))
            elif t=='1b6' and subtype in (51,54):
                gates.append(dict(npc=o['id'],kind='orbs',orbs=15 if subtype==51 else 25,portal=u32(g,a+0x40),barrier=u32(g,a+0x48)))
        result.append(dict(slot=e.index,portals=portals,surfaces=surfaces,objects=objects,particles=particles,gates=gates))
    return result

if __name__=='__main__':
    result=probe();out=Path('data/analysis/portals/inventory.json');out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(result,indent=2))
    for l in result:
        print(l['slot'],'portals',[(p['index'],p['flags']) for p in l['portals']], 'lifts',[(o['id'],o['state'],o['words'][:4]) for o in l['objects'] if o['type']=='400'],'surfaces',[(s['index'],s['words'][:2]) for s in l['surfaces'] if s['type']==6])
