"""Hash-bound XA sector checks and independent FFmpeg PCM comparison."""
import argparse
import hashlib
import json
import shutil
import wave
from pathlib import Path

import numpy as np
import ps1disc


def pcm(path):
    with wave.open(str(path),'rb') as f:
        assert (f.getnchannels(),f.getsampwidth(),f.getframerate())==(2,2,37800)
        return np.frombuffer(f.readframes(f.getnframes()),'<i2')


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('report',type=Path)
    ap.add_argument('--independent',type=Path,required=True);a=ap.parse_args()
    r=json.loads(a.report.read_text());p=Path(r['xa']);raw=p.read_bytes();digest=hashlib.sha256(raw).hexdigest()
    assert digest==r['xa_sha256'];s=np.frombuffer(raw,np.uint8).reshape(-1,2352)
    print('ARTIFACT',p.resolve(),'sha256',digest,flush=True)
    f=Path(shutil.which('ffmpeg'));print('INDEPENDENT DECODER',f,'sha256',hashlib.sha256(f.read_bytes()).hexdigest(),flush=True)
    print('DECODED',a.independent.resolve(),'sha256',hashlib.sha256(a.independent.read_bytes()).hexdigest(),flush=True)
    assert np.array_equal(s[:,16:20],s[:,20:24])
    assert np.all(s[:,16:18]==[1,1]) and np.all(s[:,19]==1)
    assert np.all(s[:-1,18]==0x64) and s[-1,18]==0xe5
    assert np.array_equal(ps1disc.edc(s[:,16:2348]),s[:,2348:2352].copy().view('<u4').ravel())
    groups=s[:,24:2328].reshape(-1,128)
    assert np.array_equal(groups[:,:4],groups[:,4:8]) and np.array_equal(groups[:,8:12],groups[:,12:16])
    assert np.all(groups[:,4:12]>>4<4) and np.all((groups[:,4:12]&15)<=12)
    print('XA STRUCTURE PASS',digest,len(s),'Form2 sectors; stereo37800Hz4bpp; duplicated headers/parameters; every EDC; bounded predictors',flush=True)
    own=pcm(Path(r['readback']));independent=pcm(a.independent)
    assert len(own)==len(independent)==len(s)*4032 and np.array_equal(own,independent)
    print('INDEPENDENT PCM PASS',digest,len(own),'signed16 samples bit-exact with FFmpeg; SNR',r['snr_db'],'dB',flush=True)
    print('LIMIT standalone artifact only; original CD-controller timing, image integration and user listening acceptance remain unfinished',flush=True)


if __name__=='__main__':main()
