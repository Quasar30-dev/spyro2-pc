"""Finite original-game CD music check on an owned hidden DuckStation copy.

The private profile uses DuckStation's built-in lossless audio capture. The
original controller state is read without patching its code or music tables.
Only the already-established controller-input fixture writes RAM pad samples.
"""
import argparse
import configparser
import ctypes as c
import ctypes.wintypes as w
import json
import struct
import time
import wave
from pathlib import Path

import ds_harness as d
import ds_pad_proof as route
import ds_private_check as private


def setup_capture(cue,extra=()):
    home=d.EXE.parent;p=home/'settings.ini'
    cfg=configparser.ConfigParser();cfg.optionxform=str;cfg.read(p)
    cfg['Hotkeys']['ToggleAudioCapture']='Keyboard/F9'
    cfg['Hotkeys']['ToggleTurbo']='Keyboard/F8'
    cfg['MediaCapture']={'Backend':'MediaFoundation','AudioContainer':'wav','AudioCodec':'pcm'}
    cfg['Main']['EmulationSpeed']='1.0'
    cfg['Interface']={'EmulationSpeed':'1.0'}
    cfg['Folders']={'Videos':str(home/'videos')}
    (home/'videos').mkdir()
    with p.open('w') as f:cfg.write(f)
    return original_start(cue,extra)


original_start=d.start


def key(s,vk=0x78):
    # Finalizing a long MediaFoundation WAV can exceed a synchronous3-second
    # UI message timeout. Queue the two events once to the owned hidden window;
    # wait_capture verifies actual file completion via bounded filesystem events.
    pid=w.DWORD();private.U.GetWindowThreadProcessId(s.win,c.byref(pid))
    assert pid.value==s.pid,'capture hotkey window must belong to the owned process'
    private.U.PostMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM]
    private.U.PostMessageW.restype=w.BOOL
    scan=private.U.MapVirtualKeyW(vk,0)
    for message,flags in [(0x100,1|(scan<<16)),(0x101,1|(scan<<16)|0xc0000000)]:
        assert private.U.PostMessageW(s.win,message,vk,flags),'owned capture hotkey queue failed'


def wait_capture(s,complete=False):
    deadline=time.monotonic()+20
    while True:
        for p in (s.home/'videos').glob('*.wav'):
            try:
                # Media Foundation holds the open WAV exclusively. Its path is
                # observable through directory notifications, while stat/open
                # may fail until the stop hotkey finalizes and closes it.
                if not complete:return p
                with p.open('rb') as f:head=f.read(44)
                if len(head)>=12 and head[:4]==b'RIFF' and head[8:12]==b'WAVE':
                    if not complete or struct.unpack_from('<I',head,4)[0]+8==p.stat().st_size:
                        return p
            except (PermissionError,OSError):pass
        s.watch.wait(s.h,deadline)


def state(g):
    return dict(zip(['state','cd_error','volume','pending','stop','paused','timeout','failures','unknown14','unknown18',
                     'kind','start','current','end','channel','volume_pointer'],
                    struct.unpack('<16I',g.read(0x800682f4,64))))


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('cue',type=Path)
    ap.add_argument('--name',required=True);ap.add_argument('--task-root',required=True,type=Path)
    ap.add_argument('--ticks',type=int,default=600);ap.add_argument('--loop-event',action='store_true')
    ap.add_argument('--post-restart-ticks',type=int,default=600,choices=(600,900,1200),
                    help='Finite original logic samples after observed CD reseek; longer captures can distinguish music from incidental SFX')
    ap.add_argument('--disc-report',type=Path,help='hash-bound standalone report with relocated speech LBA')
    ap.add_argument('--expanded-report',type=Path,help='hash-bound full-game appended MESA.XA report')
    a=ap.parse_args()
    private.TASK=a.task_root.resolve();d.start=setup_capture
    def log(*x):print(*x,flush=True)
    digest=private.sha(a.cue.with_suffix('.bin'));log('ARTIFACT',a.cue,'BIN sha256',digest)
    speech_lba=90000
    if a.disc_report:
        disc=json.loads(a.disc_report.read_text());assert disc['sha256']==digest
        speech_lba=disc['speech_lba']
        log('RELOCATED STREAM REPORT',a.disc_report.resolve(),'sha256',private.sha(a.disc_report),'SPEECH LBA',speech_lba)
    expected_start=speech_lba+1;expected_end=expected_start+30448
    if a.expanded_report:
        assert not a.disc_report
        expanded=json.loads(a.expanded_report.read_text());assert expanded['sha256']==digest
        expected_start=expanded['new_file_lba']+1;expected_end=expected_start+30448
        log('EXPANDED MUSIC REPORT',a.expanded_report.resolve(),'sha256',private.sha(a.expanded_report))
    log('HARNESS',Path(__file__).resolve(),'sha256',private.sha(Path(__file__)))
    observed=[];restart=False
    with private.Session(a.cue,log,a.name) as s:
        pad=private.Pad(s);g=s.gdb
        if a.disc_report:
            from mesa_standalone_check import arrive
            arrive(pad,log,'standalone cold boot')
        else:
            assert route.to_glimmer(pad,log,limit=1200),'new-game Glimmer route'
            if a.expanded_report:
                pad.run(0,60)
                glimmer=state(g)
                assert glimmer['state']==7 and glimmer['cd_error']==0 and glimmer['current']>90001
                assert glimmer['start']==90001 and glimmer['end']==120449 and glimmer['channel']==1
                log('ORIGINAL GLIMMER MUSIC PLAYBACK PASS',digest,'original start90001/end120449/channel1 playing before Mesa entry',json.dumps(glimmer))
                from mesa_portal_check import enter_from_glimmer
                enter_from_glimmer(pad,log,digest)
        pad.run(0,60);initial=state(g)
        log('ORIGINAL CD INITIAL',json.dumps(initial))
        assert initial['kind']==1 and initial['channel']==1 and initial['state'] in (6,7)
        assert initial['start']==expected_start and initial['end']==expected_end
        # The input hotkey is addressed only to our hidden process window.
        pad.release();key(s);capture=wait_capture(s);pad.rearm()
        # Enable unthrottled emulation only after the wall-clock-sensitive
        # disc/movie loading route has completed; capture still uses emulated
        # 44100-Hz samples. This does not alter the original game or CD timing.
        key(s,0x77)
        log('OWNED LOSSLESS CAPTURE START',capture)
        previous=initial['current']
        for tick in range(0,a.ticks,300):
            pad.run(0,min(300,a.ticks-tick));row=state(g);observed.append(row)
            if row['current']+1000<previous and row['kind']==1:restart=True
            previous=row['current']
            log('FINITE MUSIC',min(tick+300,a.ticks),'/',a.ticks,json.dumps(row),'game',pad.state())
            assert row['channel']==1 and row['kind']==1 and row['start']==expected_start and row['end']==expected_end
        if a.loop_event:
            # Three exact original-code events replace thousands of idle pad
            # stops: end-of-extent fade, reset current sector, next Setloc.
            assert g._send('Z0,80013410,4')=='OK'
            pad.release();g.s.settimeout(300);stop=g._recv();s.running=False
            assert g.pc()==0x80013410,(stop,hex(g.pc()))
            end_state=state(g);observed.append(end_state)
            log('ORIGINAL EXTENT END EVENT',hex(g.pc()),json.dumps(end_state),'GPR',json.dumps(g.regs()))
            assert end_state['kind']==1 and end_state['current']>end_state['end']-100
            assert g._send('z0,80013410,4')=='OK' and g._send('Z0,80013628,4')=='OK'
            g.s.sendall(b'$c#63');g.s.settimeout(30);g._recv()
            assert g.pc()==0x80013628
            log('ORIGINAL RESET EVENT',hex(g.pc()),json.dumps(state(g)),'GPR',json.dumps(g.regs()))
            assert g._send('z0,80013628,4')=='OK' and g._send('Z0,800131f0,4')=='OK'
            g.s.sendall(b'$c#63');g._recv();assert g.pc()==0x800131f0
            reset=state(g);observed.append(reset)
            assert reset['current']==reset['start']==expected_start and reset['channel']==1 and reset['kind']==1
            log('ORIGINAL RESEEK EVENT',hex(g.pc()),json.dumps(reset))
            assert g._send('z0,800131f0,4')=='OK'
            g._send(f'Z0,{d.LOGIC:x},4');g.s.sendall(b'$c#63');pad._wait_stop()
            pad.run(0,a.post_restart_ticks);after=state(g);observed.append(after)
            assert after['state']==7 and after['current']>expected_start and after['current']<expected_start+15000
            log('ORIGINAL POST-RESTART PLAYBACK',json.dumps(after));restart=True
        pad.release();key(s);capture=wait_capture(s,True);pad.rearm()
        with wave.open(str(capture),'rb') as f:
            channels,width,rate,frames=f.getnchannels(),f.getsampwidth(),f.getframerate(),f.getnframes()
            raw=f.readframes(frames)
        assert channels==2 and width==2 and rate==44100 and frames>44100
        import numpy as np
        samples=np.frombuffer(raw,'<i2').astype(float);peak=float(np.max(np.abs(samples)));rms=float(np.sqrt(np.mean(samples*samples)))
        assert rms>50 and peak>100
        receipt={'image_sha256':digest,'emulator_sha256':private.sha(s.exe),'capture':str(capture),
                 'capture_sha256':private.sha(capture),'channels':channels,'sample_rate':rate,'frames':frames,
                 'peak':peak,'rms':rms,'initial':initial,'observations':observed,'restart_observed':restart,'speech_lba':speech_lba,
                 'limits':['Non-silent capture and CD state alone are not score-identity proof; compare captured PCM with the encoded score separately.',
                           'Automated private emulator evidence; no user listening/gameplay acceptance.']}
        receipt_path=s.home/'music-receipt.json';receipt_path.write_text(json.dumps(receipt,indent=2))
        log('ORIGINAL CD LOSSLESS CAPTURE PASS',digest,frames,'stereo44100 frames; RMS',rms,'peak',peak,'WAV',capture,'sha256',receipt['capture_sha256'])
        if a.ticks>=6600 or a.loop_event:
            assert restart,'original CD extent restart was not observed'
            log('ORIGINAL CD EXTENT RESTART PASS',digest,'relocated/appended stream, original end/reset/seek/playback code; unchanged channel and physical length' if a.disc_report or a.expanded_report else ('unmodified original start/end/channel; original end/reset/seek/playback events' if a.loop_event else f'{a.ticks} finite ticks'))
        log('RECEIPT',receipt_path,'sha256',private.sha(receipt_path))
    assert private.sha(a.cue.with_suffix('.bin'))==digest
    log('LIMIT original score identity still needs offline capture comparison; manual listening/gameplay belongs to user')


if __name__=='__main__':main()
