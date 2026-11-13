"""Check the exact Market Mesa score inside a candidate disc, without decoding.

Requires the source-extent/render and codec receipts. All selected XA payloads
and every other sector of the original music extent are checked separately.
"""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
import ps1disc


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('cue',type=Path)
    ap.add_argument('--codec-report',type=Path,required=True);ap.add_argument('--render-report',type=Path,required=True)
    ap.add_argument('--placement-report',type=Path,help='Exact-image compact or appended-file report')
    ap.add_argument('--out',type=Path,required=True);a=ap.parse_args()
    codec=json.loads(a.codec_report.read_text());render=json.loads(a.render_report.read_text());e=render['retail_extent']
    xa_path=Path(codec['xa']);assert ps1disc.sha256_file(xa_path)==codec['xa_sha256']
    xa=np.frombuffer(xa_path.read_bytes(),np.uint8).reshape(-1,2352)
    disc=ps1disc.DiscModel(a.cue);digest=ps1disc.sha256_file(disc.bin)
    speech=next(f for f in disc.files if f.path=='SPEECH.STR');start=speech.lba+e['start_sector'];kind='original-extent'
    placement=None
    if a.placement_report:
        placement=json.loads(a.placement_report.read_text());assert placement['sha256']==digest
        assert placement['xa_sha256']==codec['xa_sha256']
        if placement.get('new_file')=='MESA.XA':
            kind='appended-file';new=next(f for f in disc.files if f.path=='MESA.XA')
            assert new.lba==placement['new_file_lba'] and new.size==placement['new_file_sectors']*2048
            start=new.lba+1
        else:
            kind='relocated-extent';assert speech.lba==placement['speech_lba']
    extent=disc.raw[start:start+e['physical_sectors']]
    selected=np.asarray(e['matched_sector_offsets'],int);rest=np.ones(len(extent),bool);rest[selected]=False
    assert len(selected)==len(xa)==codec['sectors']==3806
    assert np.array_equal(extent[selected,16:],xa[:,16:])
    assert np.all(extent[selected,:12]==xa[:,:12])
    # Original asset is read-only; restoring only the chosen audio blocks must
    # reproduce the original raw music extent hash, including the physical MSF.
    original_all=np.fromfile(Path(__file__).resolve().parents[2]/'data/disc/SPEECH.STR.raw',np.uint8).reshape(-1,2352)
    original=original_all[e['start_sector']:e['start_sector']+e['physical_sectors']]
    if kind=='appended-file':
        assert np.array_equal(disc.raw[speech.lba:speech.lba+len(original_all)],original_all),'original speech/Glimmer audio changed'
        assert np.all(extent[rest,24:24+2324]==0),'unexpected audio in appended unused sectors'
        assert np.all(extent[rest,16:20]==np.array([0,0,0x20,0],np.uint8))
    else:
        assert np.array_equal(extent[rest,16:],original[rest,16:]),'other channels/subheaders/payloads changed'
    lba=np.arange(start,start+len(extent))+150
    bcd=lambda n:((n//10)<<4)|(n%10)
    headers=np.stack((bcd(lba//4500),bcd((lba//75)%60),bcd(lba%75)),axis=1)
    assert np.array_equal(extent[:,12:15],headers) and np.all(extent[:,15]==2),'raw MSF does not match actual physical LBA'
    if kind=='original-extent':assert np.array_equal(extent[:,:16],original[:,:16])
    assert hashlib.sha256(original.tobytes()).hexdigest()==e['extent_sha256']
    result={'feature':'soi_market_mesa_music','bin':str(disc.bin),'bin_sha256':digest,
            'xa_sha256':codec['xa_sha256'],'xa_sectors':len(selected),'frames':codec['source_frames'],
            'rate':37800,'channels':2,'original_music_track':e['track'],'original_channel':e['channel'],
            'render_report_sha256':ps1disc.sha256_file(a.render_report),'codec_report_sha256':ps1disc.sha256_file(a.codec_report),
            'placement_kind':kind,'first_audio_lba':start,
            'placement_report_sha256':ps1disc.sha256_file(a.placement_report) if a.placement_report else None,
            'listening':'User: "Listened to the mix; sounds good" (2026-11-05); mix approved, not an in-game playback confirmation.',
            'limits':['Presence check only; runtime CD/SPU/native evidence is recorded separately.']}
    a.out.write_text(json.dumps(result,indent=2))
    print('ARTIFACT',disc.bin,'sha256',digest,flush=True)
    print('MUSIC PRESENCE PASS',digest,len(selected),'exact XA payloads;',kind,'at LBA',start,
          '; original Glimmer/speech sectors exact' if kind=='appended-file' else '; other channels/subheaders/payloads exact',
          '; raw MSF headers match physical LBAs; codec',codec['xa_sha256'],flush=True)
    print('FEATURE',a.out.resolve(),'sha256',ps1disc.sha256_file(a.out),flush=True)
    print('USER LISTENING',result['listening'],flush=True)


if __name__=='__main__':main()
