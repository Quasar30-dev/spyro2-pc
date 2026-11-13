"""Compare an owned original-game lossless capture with the exact XA score.

Correlation tolerates original CD resampling, mixer gain and incidental game
sounds. It does not claim bit-exact PS1 output or manual listening approval.
"""
import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

import numpy as np


def sha(p):
    with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()


def decoded(path,rate):
    p=subprocess.run([shutil.which('ffmpeg'),'-hide_banner','-loglevel','error','-i',str(path),'-f','f32le','-ac','1','-ar',str(rate),'pipe:1'],
                     capture_output=True,timeout=30,creationflags=subprocess.CREATE_NO_WINDOW)
    assert p.returncode==0,p.stderr.decode(errors='replace')
    return np.frombuffer(p.stdout,'<f4').astype(np.float64)


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('receipt',type=Path)
    ap.add_argument('--codec-report',required=True,type=Path);ap.add_argument('--out',required=True,type=Path)
    ap.add_argument('--restart-tail',action='store_true')
    a=ap.parse_args();r=json.loads(a.receipt.read_text());codec=json.loads(a.codec_report.read_text())
    capture=Path(r['capture']);reference=Path(codec['readback'])
    assert sha(capture)==r['capture_sha256'] and sha(reference)==codec['readback_sha256']
    print('ARTIFACT BIN sha256',r['image_sha256'],flush=True)
    print('ORIGINAL GAME CAPTURE',capture,'sha256',sha(capture),flush=True)
    print('ENCODED SCORE REFERENCE',reference,'sha256',sha(reference),flush=True)
    rate=2205;actual=decoded(capture,rate);expected=decoded(reference,rate)
    # Skip capture-start silence and compare a ten-second window. No hand-picked
    # note or guessed timestamp is used; search the complete encoded score.
    def compare(begin):
        window=actual[begin:begin+rate*10]
        assert len(window)>=rate*5 and np.sqrt(np.mean(window*window))>0.001
        window=window-window.mean();n=1<<(len(expected)+len(window)-1).bit_length()
        cross=np.fft.irfft(np.fft.rfft(expected,n)*np.fft.rfft(window[::-1],n),n)
        cross=cross[len(window)-1:len(expected)]
        sums=np.r_[0.,np.cumsum(expected)];squares=np.r_[0.,np.cumsum(expected*expected)]
        energy=squares[len(window):]-squares[:-len(window)]-(sums[len(window):]-sums[:-len(window)])**2/len(window)
        norm=np.sqrt(np.maximum(energy,1e-30)*np.dot(window,window));correlation=cross/norm
        offset=int(np.argmax(correlation));score=float(correlation[offset]);segment=expected[offset:offset+len(window)]
        gain=float(np.dot(segment,window)/np.dot(segment,segment));error=float(np.sqrt(np.mean((window-gain*segment)**2)))
        return {'capture_begin_seconds':begin/rate,'score_begin_seconds':offset/rate,
                'window_seconds':len(window)/rate,'correlation':score,'gain':gain,'rms_error':error}
    first=compare(min(rate,max(0,len(actual)-rate*11)));windows=[first]
    assert first['correlation']>.90,first
    if a.restart_tail:
        assert r['restart_observed'] and len(actual)>rate*205
        # GDB read/trace overhead can leave much more than ten seconds of
        # playback after the observed restart. The literal file tail may have
        # reached the later loop already. Search a finite30-second interval
        # immediately after the first complete encoded extent, accounting for
        # the capture's measured initial score offset and end-of-track fade.
        predicted=len(expected)/rate-first['score_begin_seconds']
        lower=max(0,int(predicted)-2);upper=min(int(len(actual)/rate)-10,int(predicted)+30)
        candidates=[compare(second*rate) for second in range(lower,upper+1,2)]
        matching=[row for row in candidates if row['correlation']>.9 and row['score_begin_seconds']<18.4]
        assert matching,('no original intro after complete first extent',candidates)
        tail=max(matching,key=lambda row:row['correlation'])
        tail['search_capture_seconds']=[lower,upper]
        tail['selection']='Highest correlation intro window after one complete physical CD extent; original CPU reset/reseek proven separately'
        windows.append(tail)
    out={'image_sha256':r['image_sha256'],'capture_sha256':sha(capture),'score_sha256':sha(reference),
         'comparison_rate':rate,**first,'windows':windows,
         'limits':['Automated waveform identity evidence; mixer gain/resampling and incidental SFX are retained.',
                   'Not bit-exact PS1 emulation or user listening/gameplay acceptance.']}
    a.out.write_text(json.dumps(out,indent=2))
    print('ORIGINAL GAME SCORE IDENTITY PASS',r['image_sha256'],'correlation',first['correlation'],'over',first['window_seconds'],
          'seconds; score offset',first['score_begin_seconds'],'capture offset',first['capture_begin_seconds'],'gain',first['gain'],'receipt',a.out.resolve(),flush=True)
    if a.restart_tail:print('ORIGINAL GAME RESTART AUDIO IDENTITY PASS',r['image_sha256'],json.dumps(tail),flush=True)
    print('LIMIT','; '.join(out['limits']),flush=True)


if __name__=='__main__':main()
