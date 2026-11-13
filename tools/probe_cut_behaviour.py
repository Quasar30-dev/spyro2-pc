"""Bounded, read-only MIPS decision-tree tracing for selected retail Moby cases.

The decoder follows the class comparison tree only. It does not execute the
game, emulate a complete object script, or prove whole-game reachability.
Capstone is used only to write human-readable instruction evidence.
"""
import argparse,json,struct
from pathlib import Path
from capstone import Cs,CS_ARCH_MIPS,CS_MODE_MIPS32,CS_MODE_LITTLE_ENDIAN
from spyrodisc import wad_index,u32
from verify_secondary import Retail,Mips,signed
BASE=0x8006d264

def trace(code,entry,kind):
    # Locate the object's signed 16-bit class load at Moby +0x36.
    for a in range(entry,entry+0x200,4):
        w=u32(code,a-BASE)
        if w>>26==0x21 and w&65535==0x36:break
    else:raise ValueError('Missing Moby class load')
    r=[None]*32;r[0]=0;r[(w>>16)&31]=kind
    pc=a+4;next_pc=pc+4;last_target=None;visited=[]
    for _ in range(200):
        here=pc;visited.append(here);w=u32(code,pc-BASE);pc,next_pc=next_pc,next_pc+4
        op,rs,rt,rd=w>>26,w>>21&31,w>>16&31,w>>11&31
        im=signed(w,16);value=None;dest=None
        if op==0:
            fn=w&63
            if fn==0:dest=rd;value=None if r[rt] is None else r[rt]<<((w>>6)&31)
            elif fn in (0x21,0x25):
                dest=rd
                if r[rs] is not None and r[rt] is not None:value=r[rs]+r[rt] if fn==0x21 else r[rs]|r[rt]
            else:break
        elif op in (8,9,10,11,12,13,15):
            dest=rt
            if op==15:value=(w&65535)<<16
            elif r[rs] is not None:
                if op in (8,9):value=r[rs]+im
                elif op==10:value=int(signed(r[rs])<im)
                elif op==11:value=int(r[rs]<(im&0xffffffff))
                elif op==12:value=r[rs]&(w&65535)
                else:value=r[rs]|(w&65535)
        elif op in (4,5):
            if r[rs] is None or r[rt] is None:break
            taken=(r[rs]==r[rt]) if op==4 else (r[rs]!=r[rt])
            if taken:next_pc=here+4+im*4;last_target=next_pc
        elif op==2:next_pc=((here+4)&0xf0000000)|((w&0x3ffffff)<<2);last_target=next_pc
        elif op in (0x28,0x29,0x2b):
            # The pre-selector stores drawing flags; object-field stores mark
            # a case body, so do not follow them as if they were comparisons.
            if len(visited)>8:break
        else:break
        if dest:r[dest]=None if value is None else value&0xffffffff
        r[0]=0
    if last_target is None:raise ValueError('Did not reach a case edge')
    return {'class':f'{kind:03x}','entry':f'{last_target:08x}',
            'stopped':f'{here:08x}','trace':[f'{v:08x}' for v in visited]}

def inspect(source,output):
    output.mkdir(parents=True,exist_ok=True);retail=Retail(source)
    try:
        exe=retail.read('SCUS_944.25');wad=retail.read('WAD.WAD');entries=wad_index(wad[:2048],len(wad))
        bank=lambda n:wad[entries[n].offset:entries[n].offset+entries[n].size]
        cpu=Mips();cpu.memory[0x10000:0x10000+len(exe)-2048]=exe[2048:]
        loader=bank(1);cpu.memory[BASE&0x1fffffff:(BASE&0x1fffffff)+len(loader)]=loader
        cs=Cs(CS_ARCH_MIPS,CS_MODE_MIPS32+CS_MODE_LITTLE_ENDIAN)
        result=[]
        for slot in (17,19,27,47,49,57,59,61,67):
            level_id=exe[0x800+0x54994+(slot-15)//2];cpu.write(0x80066f90,level_id);cpu.call(0x8006d960)
            entry=cpu.read(0x800670f0);code=bank(slot-1)
            cases=[]
            for kind in range(0x300):
                try:cases.append(trace(code,entry,kind))
                except ValueError:continue
            # Each case interval ends at the next distinct selector target.
            starts=sorted({int(row['entry'],16) for row in cases})
            rows=[]
            for start,end in zip(starts,starts[1:]+[cpu.read(0x800670b4)]):
                if not BASE<=start<end<=BASE+len(code):continue
                kinds=[row['class'] for row in cases if int(row['entry'],16)==start]
                name=f'overlay{slot-1:03d}_{start:08x}.txt'
                instructions='\n'.join(f'{i.address:08x} {i.mnemonic:8} {i.op_str}' for i in cs.disasm(code[start-BASE:end-BASE],start))
                (output/name).write_text(instructions+'\n',encoding='utf-8')
                rows.append({'classes':kinds,'entry':f'{start:08x}','end':f'{end:08x}','disassembly':name})
            result.append({'slot':slot,'overlay':slot-1,'dispatch':f'{entry:08x}',
                           'cases':rows,'selector_traces':cases})
        (output/'moby_cases.json').write_text(json.dumps(result,indent=2)+'\n')
        print('Exported case-selection traces for',len(result),'overlays')
    finally:retail.close()

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('game',type=Path)
    p.add_argument('--output',type=Path,default=Path('data/analysis/cut-content/mips'))
    a=p.parse_args();inspect(a.game,a.output)
