"""Read the original SoI level-music table and bounded Market Mesa sequence.

Oracle: ROM 08016d38 -> table081ab708 -> music start0800426c ->
0805c4d4. Original player0805b754 uses duration table080d9e64, running status
at0805b820 and notes>=CF at0805b826. Never substitutes a guessed song.
This exports source events/samples; it does not claim mixed or in-game audio.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

import capstone
from gba import Rom, ROM_PATH, ROM_SHA256
import soi_sprites


def track(rom,start,durations):
    pc=start;tick=0;stack=[];seen={};events=[];used=set();commands=set()
    state={'voice':0,'volume':64,'pan':0,'shift':0,'bend':0,'bend_range':2,
           'lfo_speed':22,'lfo_delay':0,'mod':0,'mod_type':0,'tune':0}
    key=60;velocity=100;running=None
    names={0xbc:'shift',0xbd:'voice',0xbe:'volume',0xbf:'pan',0xc0:'bend',
           0xc1:'bend_range',0xc2:'lfo_speed',0xc3:'lfo_delay',0xc4:'mod',
           0xc5:'mod_type',0xc8:'tune'}
    for instructions in range(100000):
        at=pc;seen.setdefault(at,tick);b=rom.u8(pc)
        if b>=128:
            pc+=1;cmd=b
            if b>=0xbd:running=b
        else:
            assert running is not None,('missing running status',hex(pc))
            cmd=running
        commands.add(cmd)
        if 0x80<=cmd<=0xb0:
            tick+=durations[cmd-0x80]
        elif cmd==0xb1:
            return {'start':hex(start),'events':events,'ticks':tick,'loop':None,
                    'voices':sorted(used),'commands':[hex(x) for x in sorted(commands)]}
        elif cmd in (0xb2,0xb3):
            target=rom.u32(pc);pc+=4;assert rom.is_ptr(target)
            if cmd==0xb3:
                assert len(stack)<8;stack.append(pc);pc=target
            elif not stack and target in seen:
                return {'start':hex(start),'events':events,'ticks':tick,
                        'loop':{'start_tick':seen[target],'end_tick':tick,'target':hex(target)},
                        'voices':sorted(used),'commands':[hex(x) for x in sorted(commands)]}
            else:pc=target
        elif cmd==0xb4:
            if stack:pc=stack.pop()
        elif cmd==0xbb:
            tempo=rom.u8(pc)*2;pc+=1
            events.append({'tick':tick,'kind':'tempo','bpm_nominal':tempo,'pc':hex(at)})
        elif cmd in names:
            value=rom.u8(pc);pc+=1
            if cmd==0xbc:value=value if value<128 else value-256
            if cmd in (0xbf,0xc0,0xc8):value-=64
            state[names[cmd]]=value
            events.append({'tick':tick,'kind':'state','name':names[cmd],'value':value,'pc':hex(at)})
        elif cmd==0xce:
            if rom.u8(pc)<128:key=rom.u8(pc);pc+=1
            events.append({'tick':tick,'kind':'tie_end','key':key,'pc':hex(at)})
        elif cmd>=0xcf:
            length=durations[cmd-0xcf];gate=0
            if rom.u8(pc)<128:
                key=rom.u8(pc);pc+=1
                if rom.u8(pc)<128:
                    velocity=rom.u8(pc);pc+=1
                    if rom.u8(pc)<128:gate=rom.u8(pc);pc+=1
            used.add(state['voice'])
            events.append(dict(state,tick=tick,kind='note',key=key,velocity=velocity,
                               duration=length+gate if cmd!=0xcf else None,pc=hex(at)))
        else:
            raise AssertionError(('unimplemented original sequence command',hex(cmd),hex(at)))
    raise AssertionError('source sequence instruction bound')


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out',type=Path,required=True)
    a=ap.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    rom=Rom();assert hashlib.sha256(rom.data).hexdigest()==ROM_SHA256
    fn=soi_sprites.functions()
    assert rom.u32(0x08016db0)==0x081ab708
    header=rom.u32(0x081ab708+6*4);assert header==0x081a36b8
    voices=rom.u32(header+4);n=rom.u8(header)
    durations=list(rom.bytes(0x080d9e64,49))
    tracks=[track(rom,rom.u32(header+8+i*4),durations) for i in range(n)]
    samples={};instruments=[]
    for voice in sorted({v for t in tracks for v in t['voices']}):
        row=voices+voice*12;raw=rom.bytes(row,12);keys=sorted({e['key'] for t in tracks for e in t['events'] if e['kind']=='note' and e['voice']==voice})
        mapping=[]
        for key in keys:
            use=row
            if raw[0]&0x40:use=rom.u32(row+4)+12*rom.u8(rom.u32(row+8)+key)
            elif raw[0]&0x80:use=rom.u32(row+4)+12*key
            v=rom.bytes(use,12);assert v[0]&7==0,('non-PCM source voice',voice,key,v.hex())
            sample=rom.u32(use+4);assert rom.is_ptr(sample)
            size=rom.u32(sample+12);assert 0<size<=1<<20
            source=rom.bytes(sample+16,size)
            if sample not in samples:
                path=a.out/f'pcm-{sample:08x}.s8';path.write_bytes(source)
                samples[sample]={'address':hex(sample),'bytes':size,'header':rom.bytes(sample,16).hex(),
                                'frequency_q10':rom.u32(sample+4),'loop_start':rom.u32(sample+8),
                                'loop_flags':hex(rom.u32(sample)),'pcm_sha256':hashlib.sha256(source).hexdigest(),
                                'path':str(path.resolve())}
            mapping.append({'key':key,'record':hex(use),'record_bytes':v.hex(),'sample':hex(sample)})
        instruments.append({'voice':voice,'source_record':hex(row),'source_bytes':raw.hex(),'key_mapping':mapping})
    excerpts=[]
    for start,end in [(0x08016d38,0x08016db8),(0x0800426c,0x080042bc),
                      (0x0805b754,0x0805b9ac),(0x0805ba30,0x0805bc30),
                      (0x0805c4d4,0x0805c5b8),(0x0805bcc0,0x0805bd28)]:
        md=capstone.Cs(capstone.CS_ARCH_ARM,capstone.CS_MODE_THUMB)
        excerpts.extend(f'{i.address:08x} {i.bytes.hex():8} {i.mnemonic} {i.op_str}'
                        for i in md.disasm(rom.bytes(start,end-start),start))
    (a.out/'original-music-consumers.txt').write_text('\n'.join(excerpts)+'\n')
    result={'rom':str(ROM_PATH),'rom_sha256':ROM_SHA256,'level':6,'song_header':hex(header),
            'voicegroup':hex(voices),'header':rom.bytes(header,8+n*4).hex(),'duration_table':durations,
            'tracks':tracks,'instruments':instruments,'samples':list(samples.values()),
            'limits':['Original-ROM source extraction only; no rendered enhancement or disc audio integration yet.',
                      'Event pitch/modulation and envelope interpretation still require original-player validation.',
                      'Nominal tempo uses original command x2; hardware frame cadence must be retained during rendering.']}
    path=a.out/'market-mesa-music-source.json';path.write_text(json.dumps(result,indent=2))
    print('ORACLE',ROM_PATH,'sha256',ROM_SHA256)
    print('PROBE',Path(__file__).resolve(),'sha256',hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    print('MARKET MESA ORIGINAL MUSIC level6 loader08016d38 table081ab708 header',hex(header),'voicegroup',hex(voices))
    print('TRACKS',json.dumps([{'track':i,'notes':sum(e['kind']=='note' for e in t['events']),'ticks':t['ticks'],'loop':t['loop'],'voices':t['voices'],'commands':t['commands']} for i,t in enumerate(tracks)]))
    print('SOURCE',path.resolve(),'sha256',hashlib.sha256(path.read_bytes()).hexdigest(),'PCM samples',len(samples))
    print('LIMIT','; '.join(result['limits']))


if __name__=='__main__':main()
