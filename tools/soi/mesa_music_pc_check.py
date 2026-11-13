"""Exact-disc native music decoding and bounded private application capture.

Uses copied executables and an unseen Win32 desktop. No user session is
attached, and no manual listening/gameplay acceptance is inferred.
"""
import argparse
import hashlib
import json
import shutil
import subprocess
import wave
from pathlib import Path

import numpy as np
import mesa_pc_check as private
import soi_pc_proof as pc


def sha(p):
    with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()


def pcm(p):
    with wave.open(str(p),'rb') as f:
        assert (f.getnchannels(),f.getsampwidth(),f.getframerate())==(2,2,37800)
        return np.frombuffer(f.readframes(f.getnframes()),'<i2')


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('cue',type=Path)
    ap.add_argument('--codec-report',required=True,type=Path);ap.add_argument('--work',required=True,type=Path)
    ap.add_argument('--exe',type=Path,help='Successful candidate executable copied for private capture')
    a=ap.parse_args();a.work.mkdir(parents=True,exist_ok=False)
    root=Path(__file__).resolve().parents[2];digest=sha(a.cue.with_suffix('.bin'))
    print('ARTIFACT',a.cue.resolve(),'BIN sha256',digest,flush=True)
    probe_source=root/'build/Release/spyro_audio_probe.exe';probe=a.work/probe_source.name
    app_source=a.exe or root/'build-soi/spyro_workbench.exe';app=a.work/app_source.name
    for source,target in [(probe_source,probe),(app_source,app)]:
        shutil.copyfile(source,target);assert sha(target)==sha(source)
        print('PRIVATE EXE',target.resolve(),'sha256',sha(target),flush=True)
    decoded=a.work/'exact-disc-native.wav'
    command=[str(probe.resolve()),str(a.cue.resolve()),'--slot','17','--wave',str(decoded.resolve())]
    r=subprocess.run(command,cwd=a.work,capture_output=True,text=True,timeout=60,creationflags=subprocess.CREATE_NO_WINDOW)
    (a.work/'probe.stdout.txt').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stdout+r.stderr
    rows=[json.loads(s) for s in r.stdout.splitlines() if s.startswith('{')]
    assert rows[0]['slot']==17 and rows[0]['track']==1 and rows[0]['channel']==1
    codec=json.loads(a.codec_report.read_text());expected=Path(codec['readback'])
    assert sha(expected)==codec['readback_sha256']
    actual=pcm(decoded);reference=pcm(expected)
    assert np.array_equal(actual,reference) and len(actual)==codec['padded_frames']*2
    print('NATIVE DISC PCM PASS',digest,len(actual),'signed16 samples bit-exact with independent-checked XA; WAV',decoded.resolve(),'sha256',sha(decoded),flush=True)
    pc.SCRATCH=a.work
    capture=a.work/'application.bmp';video=a.work/'application.mp4'
    command=[str(app.resolve()),'--game',str(a.cue.resolve()),'--slot','17','--play','--headless',
             '--frames','360','--capture-size','320','240','--capture',str(capture.resolve()),
             '--video',str(video.resolve()),'--capture-music-volume','0.5']
    result=private.hidden_run(command,120)
    assert result.returncode==0,result
    report=json.loads(capture.with_suffix('.json').read_text())
    (a.work/'application-evidence.json').write_text(json.dumps({'bin_sha256':digest,'app_sha256':sha(app),'report':report},indent=2))
    assert report['wad_slot']==17 and report['retail_script_faults']==0
    assert report['video_frames']==360 and report['audio_music_samples_played']>0
    assert video.stat().st_size>10000
    assert sha(a.cue.with_suffix('.bin'))==digest
    print('PRIVATE APPLICATION MUSIC CAPTURE PASS',digest,'video_frames',report['video_frames'],'music_samples_played',report['audio_music_samples_played'],
          'video',video.resolve(),'sha256',sha(video),'report',capture.with_suffix('.json').resolve(),flush=True)
    print('LIMIT automated native evidence only; original PS1 CD/SPU playback and user listening/gameplay acceptance are separate',flush=True)


if __name__=='__main__':main()
