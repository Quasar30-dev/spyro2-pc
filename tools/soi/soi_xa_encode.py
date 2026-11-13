"""Encode a private37.8kHz stereo draft into retail-shaped CD-XA sectors.

Only creates an audio artifact; does not alter a disc or executable. Parameters,
interleave and predictor histories are measured/read back separately. Encoder
selection is a tooling choice, not an inferred original-game implementation.
"""
import argparse
import hashlib
import json
import math
import wave
from pathlib import Path

import numpy as np
import ps1disc

POS=(0,60,115,98);NEG=(0,0,-52,-55)


def block(source,hist):
    best=None;src=[int(x) for x in source]
    for filt in range(4):
        a,b=POS[filt],NEG[filt]
        prior=np.r_[hist[0],source[:-1]].astype(np.int64)
        older=np.r_[hist[1],hist[0],source[:-2]].astype(np.int64)
        residual=np.asarray(source,dtype=np.int64)-((a*prior+b*older+32)>>6)
        power=max(0,min(12,int(math.ceil(math.log2(max(1,float(np.max(abs(residual)))/7.5))))))
        scale=1<<power;last,old=hist;values=[];decoded=[];error=0
        for value in src:
            predict=(a*last+b*old+32)>>6
            q=max(-8,min(7,int(round((value-predict)/scale))))
            result=max(-32768,min(32767,q*scale+predict))
            values.append(q&15);decoded.append(result);error+=(result-value)**2
            old,last=last,result
        candidate=(error,(filt<<4)|(12-power),values,(last,old),decoded)
        if best is None or candidate[0]<best[0]:best=candidate
    return best[1:]


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('wave',type=Path)
    ap.add_argument('--out',type=Path,required=True);a=ap.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    with wave.open(str(a.wave),'rb') as f:
        assert (f.getnchannels(),f.getsampwidth(),f.getframerate())==(2,2,37800)
        pcm=np.frombuffer(f.readframes(f.getnframes()),'<i2').reshape(-1,2)
    original_frames=len(pcm);n=(original_frames+2015)//2016
    pcm=np.pad(pcm,((0,n*2016-original_frames),(0,0)))
    sectors=np.zeros((n,2352),np.uint8);decoded=np.zeros_like(pcm)
    histories=[(0,0),(0,0)];params={}
    for si in range(n):
        sub=[1,1,0x64|(0x81 if si==n-1 else 0),1]
        sectors[si,16:24]=sub*2
        for group in range(18):
            out=sectors[si,24+group*128:24+(group+1)*128];headers=[];units=[]
            for unit in range(8):
                ch=unit&1;at=si*2016+group*112+(unit//2)*28
                header,values,hist,reconstruction=block(pcm[at:at+28,ch],histories[ch])
                histories[ch]=hist;headers.append(header);units.append(values)
                decoded[at:at+28,ch]=reconstruction;params[str(header)]=params.get(str(header),0)+1
            out[:4]=headers[:4];out[4:12]=headers;out[12:16]=headers[4:]
            for i in range(28):
                for j in range(4):out[16+i*4+j]=units[2*j][i]|(units[2*j+1][i]<<4)
        if si%256==0:print('ENCODE',si,'/',n,flush=True)
    ps1disc.encode(sectors,0,np.ones(n,dtype=bool),np.zeros(n,dtype=bool))
    path=a.out/'market-mesa-draft.xa';path.write_bytes(sectors.tobytes())
    decoded_path=a.out/'market-mesa-encoder-readback.wav'
    with wave.open(str(decoded_path),'wb') as f:
        f.setnchannels(2);f.setsampwidth(2);f.setframerate(37800);f.writeframes(decoded.astype('<i2').tobytes())
    error=decoded[:original_frames].astype(float)-pcm[:original_frames].astype(float)
    mse=float(np.mean(error**2));power=float(np.mean(pcm[:original_frames].astype(float)**2))
    report={'source':str(a.wave.resolve()),'source_sha256':hashlib.sha256(a.wave.read_bytes()).hexdigest(),
            'encoder_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            'xa':str(path.resolve()),'xa_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
            'readback':str(decoded_path.resolve()),'readback_sha256':hashlib.sha256(decoded_path.read_bytes()).hexdigest(),
            'source_frames':original_frames,'padded_frames':len(pcm),'sectors':n,
            'coding':1,'file':1,'channel':1,'parameter_histogram':params,
            'snr_db':10*math.log10(power/mse),'rms_error':math.sqrt(mse),'maximum_error':float(np.max(abs(error))),
            'limits':['Standalone codec artifact, placeholder sequential sector addresses; not integrated into a game image.',
                      'Encoder internal readback is not independent decoder or original-engine proof.',
                      'EOF/submode and soundtrack loop/controller behavior still require original consumer verification.']}
    (a.out/'xa-draft-report.json').write_text(json.dumps(report,indent=2))
    print('ENCODED',json.dumps(report),flush=True)


if __name__=='__main__':main()
