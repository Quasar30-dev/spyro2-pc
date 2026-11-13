"""Execute original SoI ARM sprite slices offline; reconstruct reference frames.

This deliberately does not change soi_sprites.py while the content worker owns
that file. 08000628 expands presence bits through DMA; 080003d4 emits large
sprite OAM blocks. Only those bounded, hashed ROM routines are executed. This
is not a whole-game runtime test or proof of semantic animation labels.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

import capstone
import numpy as np
from PIL import Image, ImageDraw

from gba import Rom, ROM_PATH, ROM_SHA256
import soi_sprites as sprites

MASK = 0xffffffff
ACTORS = {
    'grendor': (15,0x25,11,'0803283c actor+0x18 low6=0x2c'),
    'critter': (6,0x23,10,'08021ec0 actor+0x18 low6 palette=0x28'),
    'spear-guard': (6,0x25,11,'0802be90 actor+0x18 palette=0x2c'),
    'robed-rhynoc': (6,0x26,12,'0802c478 actor+0x18 palette=0x30'),
    'return-portal': (6,0x16,15,'0801d428 object+0x26 palette=0x3c;0801d764 type0x29 destination2'),
}


class ArmSlice:
    def __init__(self, rom):
        self.rom = rom
        self.mem = {}
        self.r = [0] * 16
        self.flags = [False] * 4  # N Z C V
        self.pc = 0
        self.dma = []

    def read(self, a, size=4):
        if 0x08000000 <= a < 0x08000000 + len(self.rom.data):
            return int.from_bytes(self.rom.bytes(a, size), 'little')
        return sum(self.mem.get(a+i, 0) << (8*i) for i in range(size))

    def write(self, a, value, size=4):
        for i in range(size):
            self.mem[a+i] = (value >> (8*i)) & 255
        if size == 4 and a in (0x040000b8, 0x040000dc) and value & 0x80000000:
            src, dst = self.read(a-8), self.read(a-4)
            assert value & 0x04000000, 'only original 32-bit DMA supported'
            count = value & 65535
            assert 0 < count <= 8
            self.dma.append((src, dst, count, value))
            for i in range(count):
                self.write(dst+4*i, self.read(src if value & 0x01000000 else src+4*i))

    def put(self, a, data):
        for i, x in enumerate(data):
            self.mem[a+i] = x

    def reg(self, i):
        return self.pc+8 if i == 15 else self.r[i]

    def condition(self, c):
        n,z,carry,v = self.flags
        return (z,not z,carry,not carry,n,not n,v,not v,carry and not z,
                not carry or z,n == v,n != v,not z and n == v,z or n != v,True,False)[c]

    def operand(self, w):
        if w & (1 << 25):
            v = w & 255; shift = ((w >> 8) & 15)*2
            return ((v >> shift) | (v << (32-shift))) & MASK if shift else v
        v = self.reg(w & 15); typ = (w >> 5) & 3
        byreg = bool(w & 16)
        sh = self.reg((w >> 8) & 15) & 255 if byreg else (w >> 7) & 31
        if byreg and not sh: return v
        if typ == 0: return (v << sh) & MASK if sh < 32 else 0
        if typ == 1: return v >> (sh or 32) if sh < 32 else 0
        if typ == 2:
            v = v if v < 0x80000000 else v - 0x100000000
            return (v >> min(sh or 32,32)) & MASK
        assert sh, 'RRX outside slice'
        sh %= 32
        if not sh: return v
        return ((v >> sh) | (v << (32-sh))) & MASK

    def run(self, address, limit=16000):
        self.pc = address; self.r[14] = 0x0203fff0
        for steps in range(limit):
            if self.pc == self.r[14]: return steps
            pc = self.pc; w = self.read(pc); self.pc = pc
            if not self.condition(w >> 28): self.pc += 4; continue
            next_pc = pc+4
            if w & 0x0ffffff0 == 0x012fff10:
                next_pc = self.reg(w & 15)
            elif (w >> 25) & 7 == 5:
                off = w & 0xffffff
                if off & 0x800000: off -= 0x1000000
                assert not w & (1 << 24), 'BL outside slice'
                next_pc = pc+8+off*4
            elif (w >> 25) & 7 == 4:
                rn = (w >> 16) & 15; regs = [i for i in range(16) if w & (1 << i)]
                base = self.reg(rn); up = bool(w & (1 << 23)); pre = bool(w & (1 << 24))
                a = base+(4 if pre else 0) if up else base-4*len(regs)+(0 if pre else 4)
                for i in regs:
                    if w & (1 << 20): self.r[i] = self.read(a)
                    else: self.write(a, self.reg(i))
                    a += 4
                if w & (1 << 21): self.r[rn] = (base+(4*len(regs) if up else -4*len(regs))) & MASK
            elif w & 0x0e000090 == 0x00000090:
                assert w & (1 << 20), 'only halfword/signed loads in slice'
                rn,rd = (w >> 16) & 15,(w >> 12) & 15
                off = ((w >> 4) & 0xf0) | (w & 15)
                a = self.reg(rn)+(off if w & (1 << 23) else -off)
                typ = (w >> 5) & 3; size = 1 if typ == 2 else 2
                val = self.read(a,size)
                if typ in (2,3) and val & (1 << (size*8-1)): val -= 1 << (size*8)
                self.r[rd] = val & MASK
            elif (w >> 26) & 3 == 1:
                rn,rd = (w >> 16) & 15,(w >> 12) & 15
                assert not w & (1 << 25), 'register offset load outside slice'
                base = self.reg(rn); off = (w & 4095)*(1 if w & (1 << 23) else -1)
                a = base+off if w & (1 << 24) else base
                size = 1 if w & (1 << 22) else 4
                if w & (1 << 20): self.r[rd] = self.read(a,size)
                else: self.write(a,self.reg(rd),size)
                if w & (1 << 21) or not w & (1 << 24): self.r[rn] = (base+off) & MASK
            elif (w >> 26) & 3 == 0:
                op = (w >> 21) & 15; a = self.reg((w >> 16) & 15); b = self.operand(w)
                if op in (0,8): result = a & b
                elif op == 1: result = a ^ b
                elif op in (2,10): result = a-b
                elif op == 3: result = b-a
                elif op in (4,11): result = a+b
                elif op == 12: result = a | b
                elif op == 13: result = b
                elif op == 14: result = a & ~b
                elif op == 15: result = ~b
                else: raise AssertionError((hex(pc),hex(w)))
                if w & (1 << 20):
                    self.flags[:2] = [bool(result & 0x80000000),(result & MASK) == 0]
                    if op in (2,10,3):
                        aa,bb = (b,a) if op == 3 else (a,b)
                        self.flags[2:] = [aa >= bb, bool((aa ^ bb) & (aa ^ result) & 0x80000000)]
                    elif op in (4,11):
                        self.flags[2:] = [result > MASK,bool(~(a ^ b) & (a ^ result) & 0x80000000)]
                if op not in (8,9,10,11): self.r[(w >> 12) & 15] = result & MASK
            else: raise AssertionError((hex(pc),hex(w)))
            self.pc = next_pc & MASK
        raise AssertionError(('instruction bound',hex(self.pc)))


def original_frame(rom, bank, frame, palette, palette_bank):
    tile0, attr, dx, dy = bank['frames'][frame]
    width,height = 8 << ((attr >> 3) & 7),8 << (attr & 7)
    assert not bank['bpp8'], 'this probe currently covers 4bpp source actors'
    count = width*height//64
    assert count <= bank['bits']
    cpu = ArmSlice(rom); cpu.r[13] = 0x02010000
    indices = bank['tiles']
    packed = (indices[:,:,::2] | (indices[:,:,1::2] << 4)).tobytes()
    cpu.put(0x02100000,packed)
    cpu.put(0x02200000,np.packbits(bank['presence'],bitorder='little').tobytes())
    cpu.r[:4] = [0x06010000,0x02000100,0x02100000+tile0*32,0x02200000*8+frame*bank['bits']]
    cpu.write(cpu.r[13],count)
    expansion_steps = cpu.run(0x08000628)
    present = bank['presence'][frame*bank['bits']:frame*bank['bits']+count]
    assert tile0+int(present.sum()) <= len(indices)
    assert len(cpu.dma) == count
    next_tile = tile0
    for k,(src,dst,words,control) in enumerate(cpu.dma):
        assert dst == 0x06010000+k*32 and words == 8
        if present[k]:
            assert src == 0x02100000+next_tile*32 and control == 0x84000008
            next_tile += 1
        else:
            assert src == 0x02000100 and control == 0x85000008
    cpu.r[13] = 0x02010000
    cpu.put(0x02001000,bytes.fromhex(bank['header']))
    cpu.put(0x02002000,struct.pack('<HBbb',tile0,attr,dx,dy))
    cpu.put(0x02000000,struct.pack('<hhHHH',64-dx,16-dy,0,0,palette_bank << 2))
    cpu.r[:4] = [0x02000000,0x02001000,0x02002000,0x02003000]
    cpu.write(cpu.r[13],0)
    render_steps = cpu.run(0x080003d4 if attr & 0x24 else 0x080001d0)
    n = cpu.r[0]; assert 1 <= n <= 4
    image = np.zeros((height,width,4),np.uint8); oams = []
    for j in range(n):
        a0,a1,a2,_ = struct.unpack('<4H',bytes(cpu.mem.get(0x02003000+8*j+k,0) for k in range(8)))
        tw,th = sprites.OAM[(a0 >> 14,(a1 >> 14) & 3)]
        x,y = (a1 & 511)-64,(a0 & 255)-16
        assert x >= 0 and y >= 0 and x+tw*8 <= width and y+th*8 <= height
        assert a2 >> 12 == palette_bank
        for ty in range(th):
            for tx in range(tw):
                addr = 0x06010000+32*((a2 & 1023)+ty*tw+tx)
                raw = np.array([cpu.mem.get(addr+k,0) for k in range(32)],np.uint8).reshape(8,4)
                idx = np.zeros((8,8),np.uint8);idx[:,::2] = raw & 15;idx[:,1::2] = raw >> 4
                region = image[y+ty*8:y+(ty+1)*8,x+tx*8:x+(tx+1)*8]
                region[:,:,:3] = palette[palette_bank*16+idx];region[:,:,3] = (idx != 0)*255
        oams.append([a0,a1,a2])
    return image,{'frame':frame,'dimensions':[width,height],'palette_bank':palette_bank,'oam':oams,
                  'original_instruction_steps':[expansion_steps,render_steps],'origin':[dx,dy]}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--actor',choices=ACTORS,default='grendor')
    a = ap.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    rom = Rom();assert hashlib.sha256(rom.data).hexdigest() == ROM_SHA256
    fn = sprites.functions();palettes = sprites.obj_palettes(rom,fn)
    # Original actor sprite+8: priority low2, palette bank bits2..5.
    # Frame attr high2 are OAM size, never the palette.
    level,slot,palette_bank,initializer = ACTORS[a.actor]
    sources=sprites.slot_sources(rom,fn);slots=dict(sources[None]);slots.update(sources[level])
    address,_=slots[slot];bank=sprites.bank(rom,address)
    print('ORACLE',ROM_PATH,'sha256',ROM_SHA256,flush=True)
    print('PROBE',Path(__file__).resolve(),'sha256',hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),flush=True)
    print('ACTOR',a.actor,'bank',hex(address),'level',level,'palette',palette_bank,'initializer',initializer,flush=True)
    print('ORIGINAL SLICES 08000628..080006e8 DMA expansion; 080003d4..08000624 composite OAM; 080001d0..080002b0 single OAM',flush=True)
    excerpts=[]
    for start,end,mode in [(0x080001d0,0x080006ec,capstone.CS_MODE_ARM),
                           (0x0803283c,0x08032960,capstone.CS_MODE_THUMB),
                           (0x08005284,0x0800540c,capstone.CS_MODE_THUMB)]:
        md=capstone.Cs(capstone.CS_ARCH_ARM,mode)
        excerpts.extend(f'{i.address:08x} {i.bytes.hex():8} {i.mnemonic} {i.op_str}'
                        for i in md.disasm(rom.bytes(start,end-start),start))
    (a.out/'original-consumers.txt').write_text('\n'.join(excerpts)+'\n')
    records=[];images=[]
    for i in range(bank['n']):
        rgba,record = original_frame(rom,bank,i,palettes[level],palette_bank)
        path = a.out/f'frame-{i:03d}.png';Image.fromarray(rgba).save(path)
        record.update(path=str(path.resolve()),rgba_sha256=hashlib.sha256(rgba.tobytes()).hexdigest())
        records.append(record);images.append(Image.fromarray(rgba))
    sheets=[]
    for page in range(0,len(images),48):
        sheet=Image.new('RGB',(6*268,8*284),(28,32,42));draw=ImageDraw.Draw(sheet)
        for j,im in enumerate(images[page:page+48]):
            x,y=(j%6)*268,(j//6)*284
            big=im.resize((im.width*2,im.height*2),Image.Resampling.NEAREST)
            sheet.paste(big,(x+6,y+22),big);draw.text((x+6,y+4),f'{a.actor} #{page+j} original ARM',fill='white')
        path=a.out/f'{a.actor}-original-arm-{page//48:02d}.png';sheet.save(path);sheets.append(str(path.resolve()))
    out={'rom_sha256':ROM_SHA256,'bank':hex(address),'actor':a.actor,'palette_initializer':initializer,
         'frames':records,'sheets':sheets,
         'limits':['Offline bounded original-consumer execution, not full GBA runtime.',
                   'Initial actor palette only; effect frames and palette-changing states require semantic binding.',
                   'Frame numbers retained; no invented direction/clip labels.']}
    manifest=a.out/'original-arm-manifest.json';manifest.write_text(json.dumps(out,indent=2))
    print('ORIGINAL ARM FRAME CHECK PASS',len(records),'frames; manifest',manifest.resolve(),'sha256',hashlib.sha256(manifest.read_bytes()).hexdigest(),flush=True)
    print('LIMIT', '; '.join(out['limits']),flush=True)


if __name__ == '__main__':main()
