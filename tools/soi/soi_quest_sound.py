"""Source bell notes -> finite PCM -> SPU, with retained original-ROM evidence.

The source bell callback 0802a038 chooses three single-note sequences. This
adapter retains their wave, keys, duration, hardware tick cadence and release
envelope. It resamples for PS1 SPU playback; it is not bit-exact GBA mixing.
"""
import argparse
import json
import math
from pathlib import Path
import re
import struct
import wave

import numpy as np
from gba import Rom, ROM_SHA256
from soi_music_source import track
from soi_music_render import interpolate, VBLANK
import soi_model_convert as fmt
from soi_sprites import functions


FILTERS=[(0,0),(60,0),(115,-52),(98,-55),(122,-60)]


def encode(pcm):
    """Bounded search with the actual separately truncated SPU predictors."""
    pcm=np.pad(pcm,(0,(-len(pcm))%28));result=bytearray();decoded=[];last=older=0
    for start in range(0,len(pcm),28):
        block=pcm[start:start+28];best=None
        for filt,(positive,negative) in enumerate(FILTERS):
            for shift in range(13):
                x,y=last,older;values=[];codes=[];error=0;step=4096>>shift
                for wanted in block:
                    pred=(x*positive>>6)+(y*negative>>6)
                    q=max(-8,min(7,int(round((int(wanted)-pred)/step))))
                    value=max(-32768,min(32767,q*step+pred));error+=(value-int(wanted))**2
                    codes.append(q&15);values.append(value);y,x=x,value
                if best is None or error<best[0]:best=(error,filt,shift,codes,values,x,y)
        _,filt,shift,codes,values,last,older=best
        result.extend(bytes([filt*16+shift,1 if start+28==len(pcm) else 0]))
        result.extend(codes[i]|codes[i+1]<<4 for i in range(0,28,2));decoded.extend(values)
    return bytes(result),np.asarray(decoded,dtype='<i2')


def install(archive,source):
    """Append samples/descriptors, preserving all earlier samples and VRAM."""
    proof=json.loads(Path(source).read_text());assert proof['rom_sha256']==ROM_SHA256
    ch,scene,sound_at,descriptors,samples=fmt.sound_table(archive,False)
    payload=bytearray(samples);previous=len(descriptors);mapping=[]
    for e in proof['bells']:
        raw=Path(e['spu']).read_bytes();assert fmt.sha(raw)==e['spu_sha256']
        descriptor=struct.pack('<I6H4B',len(payload),0,11468,1024,8192,2048,2048,0,128,0,0)
        mapping.append(len(descriptors));descriptors.append(descriptor);payload.extend(raw)
    assert len(payload)<=0x7e000,('SPU payload capacity',len(payload))
    chunk=bytearray(scene[sound_at:sound_at+264]);fmt.put32(chunk,0,264+20*len(descriptors));fmt.put32(chunk,260,len(descriptors))
    chunk.extend(b''.join(descriptors));scene=scene[:sound_at]+chunk+scene[sound_at+264+20*previous:]
    head=bytearray(archive[:ch[0].offset]);model=bytearray(archive[ch[2].offset:ch[2].offset+ch[2].size])
    found=False
    for i in range(1,63):
        start,end=struct.unpack_from('<II',archive,64+i*4)
        if not end:break
        if struct.unpack_from('<H',archive,0x140+i*2)[0]==0x2e6:
            model[start-ch[2].offset+4:start-ch[2].offset+7]=bytes(mapping);found=True
    assert found
    children=[archive[ch[0].offset:ch[0].offset+0x86000]+payload,scene,model,archive[ch[3].offset:ch[3].offset+ch[3].size]]
    result=head;offsets=[];sizes=[]
    for i,b in enumerate(children):
        result.extend(bytes((-len(result))%2048));offsets.append(len(result));sizes.append(fmt.align(len(b)))
        struct.pack_into('<II',result,i*8,len(result),fmt.align(len(b)));result.extend(b);result.extend(bytes((-len(result))%2048))
    for i in range(64):
        p=64+4*i;old=fmt.u32(archive,p)
        if not old:break
        fmt.put32(result,p,old+offsets[2]-ch[2].offset)
    after=fmt.sound_table(result,False)
    assert after[3][:previous]==fmt.sound_table(archive,False)[3] and after[4][:len(samples)]==samples
    assert result[offsets[0]:offsets[0]+0x86000]==archive[ch[0].offset:ch[0].offset+0x86000]
    return bytes(result),offsets,sizes,dict(source=str(Path(source).resolve()),source_sha256=fmt.sha(Path(source).read_bytes()),
        sample_indices=mapping,old_sample_count=previous,sample_count=len(descriptors),spu_payload_bytes=len(payload),
        original_samples_exact=True,vram_exact=True,rendered_rate=22050,spu_pitch=2048,
        limits=['Source-derived resampling and SPU quantization; not bit-exact GBA mixer or listening acceptance.'])


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--out',type=Path,required=True);a=ap.parse_args();out=a.out.resolve();assert not out.exists();out.mkdir(parents=True)
    rom=Rom();assert fmt.sha(rom.data)==ROM_SHA256;fs=functions();callback=fs['0802a038']
    literals={name:hex(rom.u32(int(name[4:],16))) for name in sorted(set(re.findall('DAT_[0-9a-f]{8}',callback)))}
    (out/'0802a038.c').write_text(callback+'\n// Original ROM literals\n'+json.dumps(literals,indent=2)+'\n')
    bells=[];rate=22050;durations=list(rom.bytes(0x080d9e64,49))
    for ordinal,header in enumerate([0x081aa638,0x081aa61c,0x081aa1f4]):
        assert rom.u8(header)==1;t=track(rom,rom.u32(header+8),durations)
        notes=[e for e in t['events'] if e['kind']=='note'];assert len(notes)==1 and notes[0]['key']==[60,67,72][ordinal]
        e=notes[0];record=rom.bytes(rom.u32(header+4)+12*e['voice'],12);assert record==bytes.fromhex('003c0000a01a0e08ff00fff2')
        sample=struct.unpack_from('<I',record,4)[0];raw=rom.bytes(sample+16,rom.u32(sample+12));pcm=np.frombuffer(raw,np.int8).astype(float)/128
        packed=rom.u8(0x080d9c80+e['key']);ratio=rom.u32(0x080d9d34+(packed&15)*4)>>(packed>>4)
        frequency=(rom.u32(sample+4)*ratio)>>32;duration=e['duration']/(VBLANK*100/150);release=record[11]/256
        tail=math.log(1/1024)/math.log(release)/VBLANK;count=math.ceil((duration+tail)*rate)
        signal=interpolate(pcm,np.arange(count)*frequency/rate,rom.u32(sample+8),True,True)
        signal*=release**(np.maximum(np.arange(count)/rate-duration,0)*VBLANK)
        # Leave 6 dB headroom. Same gain/envelope for each source bell.
        source_pcm=np.rint(signal*16383).astype('<i2');spu,decoded=encode(source_pcm)
        reference=np.pad(source_pcm,(0,len(decoded)-len(source_pcm))).astype(float);error=decoded.astype(float)-reference
        snr=10*math.log10(float(np.sum(reference**2))/max(1.,float(np.sum(error**2))));assert snr>28,(ordinal,snr)
        pcm_path=out/f'bell-{ordinal}-source.pcm';pcm_path.write_bytes(source_pcm.tobytes());path=out/f'bell-{ordinal}.spu';path.write_bytes(spu)
        decoded_path=out/f'bell-{ordinal}-decoded.pcm';decoded_path.write_bytes(decoded.tobytes())
        with wave.open(str(out/f'bell-{ordinal}.wav'),'wb') as w:w.setnchannels(1);w.setsampwidth(2);w.setframerate(rate);w.writeframes(decoded.tobytes())
        bells.append(dict(ordinal=ordinal,header=hex(header),sequence=t,key=e['key'],source_wave=hex(sample),source_wave_sha256=fmt.sha(raw),frequency=frequency,source_pcm=str(pcm_path),source_pcm_sha256=fmt.sha(source_pcm.tobytes()),
            decoded_pcm=str(decoded_path),decoded_pcm_sha256=fmt.sha(decoded.tobytes()),spu=str(path),spu_sha256=fmt.sha(spu),spu_bytes=len(spu),snr_db=snr,rate=rate,frames=len(decoded)))
        print('ENCODED SOURCE BELL',ordinal,'SNR',snr,'SPU bytes',len(spu),flush=True)
    proof=dict(rom_sha256=ROM_SHA256,source_callback='0802a038',probe_sha256=fmt.sha(Path(__file__).read_bytes()),bells=bells,
        limits=['Source-derived finite PCM, resampled with retained wave/notes/timing/release. SPU output is lossy; no hardware-mixer or user listening claim.'])
    path=out/'source-bell-sounds.json';path.write_text(json.dumps(proof,indent=2)+'\n')
    print('SOURCE BELL SOUNDS',fmt.sha(path.read_bytes()),path)
    for e in bells:print('BELL',e['ordinal'],'key',e['key'],'frequency',e['frequency'],'SPU',e['spu_sha256'],'bytes',e['spu_bytes'],'SNR dB',round(e['snr_db'],3))


if __name__=='__main__':main()
