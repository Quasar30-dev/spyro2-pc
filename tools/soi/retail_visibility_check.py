"""Execute unmodified SCUS visibility instructions in a bounded MIPS-I interpreter.

Includes branch/load delay slots. No executable changes, downloads or native game
session. The independent decoder is checked against actual instruction bytes.
"""
import hashlib
import itertools
import struct
from pathlib import Path

import mesa_visibility as vis
import mipsdis


def execute(part, position=(0, 0, 0), limit=20000):
    base, code = mipsdis.exe_image()
    ram, scratch = bytearray(0x200000), bytearray(1024)
    def mem(a):
        if 0x80000000 <= a < 0x80200000:
            return ram, a - 0x80000000
        if 0x1f800000 <= a < 0x1f800400:
            return scratch, a - 0x1f800000
        raise AssertionError(f'out of bounds memory {a:08x}')
    def store(a, v, n):
        b,o = mem(a); b[o:o+n] = int(v & ((1 << (8*n))-1)).to_bytes(n,'little')
    def load(a,n):
        b,o = mem(a); return int.from_bytes(b[o:o+n],'little')
    ram[0x100000:0x100000+len(part)] = part
    store(0x800673dc, 0x80100004 if len(part)>8 else 0x80100000,4)
    store(0x800673e4, int(len(part)>8),4)
    for i,p in enumerate(position): store(0x80067eb8+4*i,p<<11,4)
    r=[0]*32; r[31]=0x8000fff0
    pc,npc=0x80048d18,0x80048d1c
    delayed=None
    signed=lambda x:x if x<0x80000000 else x-0x100000000
    for step in range(limit):
        if pc == r[31]: return bytes(scratch[:256]),r[2],step
        assert 0x80048d18 <= pc <= 0x80048f00,hex(pc)
        w=struct.unpack_from('<I',code,pc-base)[0]
        op=w>>26; rs=(w>>21)&31; rt=(w>>16)&31; rd=(w>>11)&31; sh=(w>>6)&31; fn=w&63
        imm=w&65535; simm=imm if imm<32768 else imm-65536
        following=(npc+4)&0xffffffff; writes={}; next_load=None
        if op==0:
            if fn==0: writes[rd]=r[rt]<<sh
            elif fn==2: writes[rd]=r[rt]>>sh
            elif fn==4: writes[rd]=r[rt]<<(r[rs]&31)
            elif fn in (32,33): writes[rd]=r[rs]+r[rt]
            elif fn in (34,35): writes[rd]=r[rs]-r[rt]
            elif fn==36: writes[rd]=r[rs]&r[rt]
            elif fn==8: following=r[rs]
            else: raise AssertionError((hex(pc),hex(w)))
        elif op in (8,9): writes[rt]=r[rs]+simm
        elif op==15: writes[rt]=imm<<16
        elif op==4:
            if r[rs]==r[rt]: following=pc+4+(simm<<2)
        elif op==5:
            if r[rs]!=r[rt]: following=pc+4+(simm<<2)
        elif op==1 and rt==0:
            if signed(r[rs])<0: following=pc+4+(simm<<2)
        elif op==2: following=((pc+4)&0xf0000000)|((w&0x3ffffff)<<2)
        elif op in (35,36): next_load=(rt,load((r[rs]+simm)&0xffffffff,4 if op==35 else 1))
        elif op in (43,40): store((r[rs]+simm)&0xffffffff,r[rt],4 if op==43 else 1)
        else: raise AssertionError((hex(pc),hex(w)))
        if delayed and delayed[0] not in writes: r[delayed[0]]=delayed[1]
        for k,v in writes.items(): r[k]=v&0xffffffff
        r[0]=0; delayed=next_load; pc,npc=npc,following&0xffffffff
    raise AssertionError('instruction bound exceeded')


def main():
    import s2scene_build
    from s2scene import read_archive
    root=Path(__file__).resolve().parents[2]
    print('ORACLE',mipsdis.EXE,'sha256',hashlib.sha256(mipsdis.EXE.read_bytes()).hexdigest())
    # Every count, including the byte-length boundary, through all eight branches.
    ntests=0
    for count in range(1,256):
        part=vis.encode(count)
        for pos in itertools.product((0,127),repeat=3):
            actual,ret,steps=execute(part,pos)
            assert actual==bytes([255])*count+bytes(256-count) and ret==1,(count,pos)
            assert (actual,ret)==vis.decode(part,pos)
            ntests+=1
    print('CHECK PASS all 255 region counts / 8 octants in original instructions',ntests)
    # Translation control against retail's actual tree, including depth-0 leaves.
    ch=s2scene_build.archive_children(root/'data/disc',17)[1]
    arc=read_archive(root/'data/disc',17)
    sc=s2scene_build.split(arc[ch[1].offset:ch[1].offset+ch[1].size])
    for pos in itertools.product((0,1,31,63,64,95,127,128,255),repeat=3):
        actual,ret,_=execute(sc['parts'][2],pos)
        assert (actual,ret)==vis.decode(sc['parts'][2],pos),pos
    print('CHECK PASS decoder agrees with original Glimmer tree at 729 cells')
    print('RETAIL VISIBILITY CHECK PASS')


if __name__=='__main__': main()
