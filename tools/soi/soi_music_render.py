"""Draft SoI Market Mesa resynthesis from original events, waves and pitch table.

The modest optional enhancement uses band-limited interpolation, smooth release
ramps, narrow stem separation and 4% filtered early reflections. No replacement
instruments, notes, tempo or source samples. This is not yet CD-XA integration
or a claim of bit-exact GBA hardware audio.
"""
import argparse
import hashlib
import json
import math
import struct
import wave
from pathlib import Path

import numpy as np
from scipy.signal import butter,sosfilt
from gba import Rom,ROM_SHA256


VBLANK=16777216/280896
RATE=37800


def interpolate(pcm,positions,loop_start,looped,enhanced):
    size=len(pcm)
    def get(idx):
        valid=idx>=0
        if looped:idx=np.where(idx>=size,loop_start+(idx-loop_start)%(size-loop_start),idx)
        else:valid &= idx<size
        return pcm[np.clip(idx,0,size-1)]*valid
    base=np.floor(positions).astype(np.int64);frac=positions-base
    if not enhanced:return get(base)*(1-frac)+get(base+1)*frac
    # Short windowed-sinc interpolation; reduce cutoff for downsampling.
    step=positions[1]-positions[0] if len(positions)>1 else 1
    cutoff=min(1,1/max(1,step))*.94
    output=np.zeros(len(positions));weight=np.zeros(len(positions))
    for tap in range(-5,7):
        d=tap-frac;w=np.sinc(d*cutoff)*np.where(abs(d)<6,.5+.5*np.cos(np.pi*d/6),0)*cutoff
        output+=get(base+tap)*w;weight+=w
    return output/np.maximum(weight,1e-8)


def write_wav(path,data):
    peak=float(np.max(abs(data)));gain=10**(-2/20)/max(peak,1e-8)
    normalized=data*gain
    assert np.isfinite(normalized).all() and np.max(abs(normalized))<1
    with wave.open(str(path),'wb') as f:
        f.setnchannels(2);f.setsampwidth(2);f.setframerate(RATE)
        f.writeframes(np.round(normalized*32767).astype('<i2').tobytes())
    return {'path':str(path.resolve()),'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
            'frames':len(data),'seconds':len(data)/RATE,'source_peak':peak,'gain':gain,
            'peak_dbfs':20*math.log10(float(np.max(abs(normalized)))),'rms':float(np.sqrt(np.mean(normalized**2)))}


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('source',type=Path)
    ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--retail-extent',action='store_true',help='Retain Glimmer original music address/count/channel; no executable patch')
    a=ap.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    source=json.loads(a.source.read_text());rom=Rom();assert hashlib.sha256(rom.data).hexdigest()==source['rom_sha256']==ROM_SHA256
    tracks=source['tracks'];loops=[t['loop'] for t in tracks]
    begin,end=loops[0]['start_tick'],loops[0]['end_tick'];assert all((l['start_tick'],l['end_tick'])==(begin,end) for l in loops)
    tempos=[e['bpm_nominal'] for t in tracks for e in t['events'] if e['kind']=='tempo'];assert tempos==[108]
    ticks_per_second=VBLANK*tempos[0]/150;period=end-begin
    retail=None
    main_frames=round(end/ticks_per_second*RATE)
    if a.retail_extent:
        import mipsdis
        base,exe=mipsdis.exe_image();track=exe[0x80060140-base+1]
        sector,count,channel=struct.unpack_from('<IHH',exe,0x80064de4-base+track*8)
        path=Path(__file__).resolve().parents[2]/'data/disc/SPEECH.STR.raw'
        with path.open('rb') as f:f.seek(sector*2352);raw=f.read(count*2352)
        physical=np.frombuffer(raw,np.uint8).reshape(-1,2352)
        matching=np.flatnonzero((physical[:,16]==1)&(physical[:,17]==channel)&((physical[:,18]&0x24)==0x24))
        assert len(matching)>0 and np.all(physical[matching,19]==1)
        main_frames=len(matching)*2016
        retail={'original_exe_sha256':hashlib.sha256(mipsdis.EXE.read_bytes()).hexdigest(),
                'track':track,'start_sector':sector,'physical_sectors':count,'channel':channel,
                'matched_sector_offsets':matching.tolist(),'xa_sectors':len(matching),'frames':main_frames,
                'extent_sha256':hashlib.sha256(raw).hexdigest(),
                'choice':'Original CD extent retained; source intro followed by repeated original loop. Final0.3s fade complements original CD-controller fade; no original executable/table changes.'}
    frames=max(main_frames,round((end+period)/ticks_per_second*RATE))+RATE*3
    instruments={i['voice']:{m['key']:m for m in i['key_mapping']} for i in source['instruments']}
    samples={s['address']:s for s in source['samples']};pcm={}
    for address,s in samples.items():
        raw=Path(s['path']).read_bytes();assert hashlib.sha256(raw).hexdigest()==s['pcm_sha256']
        pcm[address]=np.frombuffer(raw,np.int8).astype(float)/128
    result=[];note_count=0
    for enhanced in (False,True):
        mix=np.zeros((frames,2),float)
        for ti,track in enumerate(tracks):
            for e in track['events']:
                if e['kind']!='note':continue
                assert e['duration'] is not None and e['bend']==0 and e['mod']==0 and e['tune']==0
                mapping=instruments[e['voice']][e['key']];record=bytes.fromhex(mapping['record_bytes'])
                assert record[:2]==bytes((0,60)) and record[8:11]==bytes((255,0,255))
                s=samples[mapping['sample']];waveform=pcm[mapping['sample']];release=record[11]/256
                note=e['key']+e['shift'];assert 0<=note<179
                packed=rom.u8(0x080d9c80+note)
                ratio=rom.u32(0x080d9d34+(packed&15)*4)>>(packed>>4)
                frequency=(s['frequency_q10']*ratio)>>32
                assert frequency>0
                duration=e['duration']/ticks_per_second
                tail=math.log(1/512)/math.log(release)/VBLANK if release>0 else 0
                count=max(1,int(math.ceil((duration+tail)*RATE)))
                positions=np.arange(count)*frequency/RATE
                signal=interpolate(waveform,positions,s['loop_start'],bool(int(s['loop_flags'],16)&0x40000000),enhanced)
                elapsed=np.maximum(np.arange(count)/RATE-duration,0)*VBLANK
                if release:
                    envelope=release**(elapsed if enhanced else np.floor(elapsed))
                else:envelope=(np.arange(count)/RATE<duration).astype(float)
                signal*=envelope*(e['velocity']/127)*(e['volume']/127)
                pan=e['pan']/64+([0,-.12,.12,-.06,.10,-.10][ti] if enhanced else 0)
                stereo=np.column_stack((signal*math.sqrt((1-pan)/2),signal*math.sqrt((1+pan)/2)))
                starts=[e['tick']]
                if e['tick']>=begin:
                    starts.extend(e['tick']+period*k for k in range(1,math.ceil((frames/RATE*ticks_per_second-begin)/period)))
                for tick in starts:
                    at=int(round(tick/ticks_per_second*RATE))
                    if at>=frames:continue
                    mix[at:at+count]+=stereo[:max(0,min(count,frames-at))]
                note_count+=not enhanced
        if enhanced:
            wet=sosfilt(butter(2,4800,fs=RATE,output='sos'),mix,axis=0)
            for delay,amount,swap in [(71,.018,True),(113,.012,False),(173,.010,True)]:
                shift=round(delay/1000*RATE);mix[shift:]+=amount*wet[:-shift,::-1 if swap else 1]
        tag='ps1-mix-draft' if enhanced else 'source-resynthesis-draft'
        selected=mix[:main_frames].copy()
        if retail:
            fade=round(.3*RATE);selected[-fade:]*=np.linspace(1,0,fade)[:,None]
        preview=write_wav(a.out/f'market-mesa-{tag}.wav',selected)
        preview.update(enhanced=enhanced)
        if enhanced:
            # Use second loop after prior note/reverb tails have reached steady state.
            lo=round((begin+period)/ticks_per_second*RATE);hi=round((end+period)/ticks_per_second*RATE)
            preview['loop']=write_wav(a.out/'market-mesa-steady-loop-draft.wav',mix[lo:hi])
        result.append(preview)
    report={'source_sha256':hashlib.sha256(a.source.read_bytes()).hexdigest(),'rom_sha256':ROM_SHA256,
            'probe_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'rate':RATE,
            'vblank_hz':VBLANK,'nominal_bpm':108,'ticks_per_second':ticks_per_second,
            'intro_ticks':begin,'loop_ticks':period,'notes':note_count,'outputs':result,'retail_extent':retail,
            'limits':['Draft resynthesis; no bit-exact GBA audio or user listening acceptance claimed.',
                      'Initial constant voice/volume state and supported PCM envelopes checked from source; hardware voice stealing, reverb and exact mixer arithmetic are not replicated.',
                      'No PS1 CD-XA encoding, soundtrack extent replacement or in-game playback yet.']}
    path=a.out/'market-mesa-render-draft.json';path.write_text(json.dumps(report,indent=2))
    print('SOURCE',a.source.resolve(),'sha256',report['source_sha256'])
    print('MEASURED',note_count,'source notes;',ticks_per_second,'ticks/sec;',begin/ticks_per_second,'intro seconds;',period/ticks_per_second,'loop seconds')
    for r in result:print('DRAFT PCM',json.dumps(r))
    print('REPORT',path.resolve(),'sha256',hashlib.sha256(path.read_bytes()).hexdigest())
    print('LIMIT','; '.join(report['limits']))


if __name__=='__main__':main()
