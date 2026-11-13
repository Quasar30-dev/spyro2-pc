"""Require hash-bound original/native music evidence for an exact release disc."""
import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

import ps1disc


HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
ROLES=('codec_report','render_report','approval','xa_log','presence_log','native_log',
       'runtime_log','score_log','runtime_receipt','score_receipt')


def sha(p):
    with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()


def verify(bundle_path,cue,work):
    bundle=json.loads(bundle_path.read_text());digest=sha(cue.with_suffix('.bin'))
    assert bundle['schema']==1 and bundle['bin_sha256']==digest
    paths={}
    for role in ROLES:
        row=bundle['files'][role];p=Path(row['path']);assert sha(p)==row['sha256'],(role,p)
        paths[role]=p
    if 'placement_report' in bundle['files']:
        row=bundle['files']['placement_report'];p=Path(row['path']);assert sha(p)==row['sha256']
        paths['placement_report']=p
    codec=json.loads(paths['codec_report'].read_text());render=json.loads(paths['render_report'].read_text())
    approval=json.loads(paths['approval'].read_text());runtime=json.loads(paths['runtime_receipt'].read_text())
    score=json.loads(paths['score_receipt'].read_text())
    assert sha(Path(codec['xa']))==codec['xa_sha256']
    assert sha(Path(codec['readback']))==codec['readback_sha256']
    mix=next(row for row in render['outputs'] if row['enhanced'])
    assert codec['source_sha256']==mix['sha256']==approval['retail_extent_mix_sha256']
    assert approval['user_quote']=='Listened to the mix; sounds good'
    assert approval['shared_cycle_correlation']>.9999
    assert runtime['image_sha256']==score['image_sha256']==digest and runtime['restart_observed']
    assert sha(Path(runtime['capture']))==runtime['capture_sha256']==score['capture_sha256']
    assert score['score_sha256']==codec['readback_sha256'] and len(score['windows'])==2
    assert all(w['correlation']>.9 and w['window_seconds']>=10 for w in score['windows'])
    assert score['windows'][1]['score_begin_seconds']<18.4
    checks={
        'xa_log':[('XA STRUCTURE PASS',codec['xa_sha256']),('INDEPENDENT PCM PASS',codec['xa_sha256'])],
        'presence_log':[('MUSIC PRESENCE PASS',digest)],
        'native_log':[('NATIVE DISC PCM PASS',digest),('PRIVATE APPLICATION MUSIC CAPTURE PASS',digest)],
        'runtime_log':[('ORIGINAL CD LOSSLESS CAPTURE PASS',digest),('ORIGINAL CD EXTENT RESTART PASS',digest)],
        'score_log':[('ORIGINAL GAME SCORE IDENTITY PASS',digest),('ORIGINAL GAME RESTART AUDIO IDENTITY PASS',digest)]}
    citations={}
    for role,markers in checks.items():
        lines=paths[role].read_text().splitlines();citations[role]=[]
        for marker,identity in markers:
            found=[(i+1,line) for i,line in enumerate(lines) if marker+' '+identity in line]
            assert len(found)==1,(role,marker,found)
            citations[role].append({'line':found[0][0],'text':found[0][1]})
    work.mkdir(parents=True,exist_ok=True)
    presence=work/'music-presence.json'
    command=[sys.executable,str(HERE/'mesa_music_presence.py'),str(cue),'--codec-report',str(paths['codec_report']),
             '--render-report',str(paths['render_report']),'--out',str(presence)]
    if 'placement_report' in paths:command+=['--placement-report',str(paths['placement_report'])]
    result=subprocess.run(command,capture_output=True,text=True,timeout=120,creationflags=subprocess.CREATE_NO_WINDOW)
    log=work/'music-presence.log';log.write_text(result.stdout+result.stderr)
    assert result.returncode==0 and 'MUSIC PRESENCE PASS '+digest in result.stdout,log
    model=ps1disc.DiscModel(cue);exe=next(f for f in model.files if f.path=='SCUS_944.25')
    payload=model.raw[exe.lba:exe.lba+(exe.size+2047)//2048,24:2072].tobytes()[:exe.size]
    assert hashlib.sha256(payload).hexdigest()==sha(ROOT/'data/disc/SCUS_944.25')
    checked={'feature':'soi_market_mesa_music','bin_sha256':digest,'xa_sha256':codec['xa_sha256'],
             'evidence_bundle_sha256':sha(bundle_path),'checker_sha256':sha(Path(__file__)),
             'original_resident_executable_unchanged':True,'fresh_presence_sha256':sha(presence),
             'fresh_presence_log_sha256':sha(log),'citations':citations,
             'user_listening':'Approved exact preview mix; original/native playback checked automatically.',
             'limits':['No user gameplay confirmation or bit-exact GBA/PS1 audio claim.']}
    (work/'music-release-check.json').write_text(json.dumps(checked,indent=2))
    print('MUSIC RELEASE EVIDENCE PASS',digest,'exact XA presence, original/native playback, CD restart audio identity and user-approved mix; bundle',sha(bundle_path),flush=True)
    return checked,paths,work


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('cue',type=Path)
    ap.add_argument('--evidence',type=Path,required=True);ap.add_argument('--work',type=Path,required=True)
    a=ap.parse_args();verify(a.evidence,a.cue,a.work)


if __name__=='__main__':main()
