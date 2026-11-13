"""Which moby class writes a RAM word: every store to ADDRESS in a level overlay,
with the dispatcher case (class) whose range contains it.

Usage: python tools/find_writers.py <slot> <address hex>   (e.g. 59 80066fec)
"""
import json,struct,sys
from pathlib import Path
slot=int(sys.argv[1]);addr=int(sys.argv[2],16)
d=Path(f'data/overlays/overlay_{slot-1:03d}.bin').read_bytes();base=0x8006d264
w=struct.unpack_from(f'<{len(d)//4}I',d)
disp=int(json.loads(Path('data/analysis/moby-dispatch/dispatch.json').read_text())[str(slot)]['dispatcher'],16)
cases=[]
for i,x in enumerate(w):
    a=base+i*4
    if not (disp<=a<disp+0x1800):continue
    if x>>26 in (9,13) and (x>>21)&31==0:
        rt=(x>>16)&31;imm=x&0xffff
        for j in range(i+1,min(i+4,len(w))):
            b=w[j]
            if b>>26 in (4,5) and ((b>>16)&31==rt or (b>>21)&31==rt):
                off=b&0xffff;off=off-0x10000 if off&0x8000 else off
                cases.append((base+(j+1+off)*4,imm));break
cases.sort()
lo=addr&0xffff;hi=(addr>>16)+(1 if lo&0x8000 else 0)
for i,x in enumerate(w):
    if x>>26 in (0x2b,0x29,0x28) and (x&0xffff)==lo and any((w[k]>>26)==0xf and (w[k]&0xffff)==hi for k in range(max(0,i-4),i)):
        a=base+i*4;c=[cc for cc in cases if cc[0]<=a];print(hex(a), 'class %x'%c[-1][1] if c else '?')
