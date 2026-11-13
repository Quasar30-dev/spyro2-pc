"""Finite original-CPU bell/contact/return tests on owned hidden emulators.

Bell approaches and attack mailboxes are named RAM fixtures; the source quest
state machine sets completion itself. The final portal approach uses controller
input. No completion/destination/result writes or on-disc fixtures are used.
"""
import argparse
import json
import struct
from pathlib import Path

import ds_private_check as private
import ds_pad_proof as route
from ds_mesa_play import Driver
from mesa_portal_check import enter_from_glimmer
from mesa_standalone_check import arrive, call


class ReturnPad(private.Pad):
    def _wait_stop(self,limit=30.,probe=None):
        self.g.s.settimeout(limit)
        for _ in range(32):
            stop=self.g._recv();self.s.running=False;pc=self.g.pc()
            if pc==0x80019928:
                assert self.g.regs()[4]==0xffffffff,'Source return must use original home metadata'
                self.return_calls+=1
                self.s.log('SOURCE PORTAL ORIGINAL RETURN CALL',self.digest,'a0=-1; original CPU at80019928; call',self.return_calls)
                assert self.g._send('z0,80019928,4')=='OK'
                self.g.s.sendall(b'$c#63');self.s.running=True;continue
            if pc==0x8001d2d0 and getattr(self,'loader_observer',None):
                if self.loader_observer():assert self.g._send('z0,8001d2d0,4')=='OK'
                self.g.s.sendall(b'$c#63');self.s.running=True;continue
            assert pc!=0x8007ded0,'Moneybags handler reached'
            return stop
        raise AssertionError('finite return observer bound')


def word(g,p):return struct.unpack('<I',g.read(p,4))[0]


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('cue',type=Path)
    ap.add_argument('--content-report',type=Path,required=True);ap.add_argument('--mode',choices=('full','compact'),required=True)
    ap.add_argument('--task-root',type=Path,required=True);ap.add_argument('--name',required=True)
    a=ap.parse_args();report=json.loads(a.content_report.read_text());j=json.loads(Path(report['actor_conversion']).read_text())
    overlay=Path(j['overlay']);assert private.sha(overlay)==j['overlay_sha256']
    assert private.sha(Path(j['archive']))==j['archive_sha256']==report['archive_sha256']
    digest=private.sha(a.cue.with_suffix('.bin'));private.TASK=a.task_root.resolve();log=private.d.Log(a.name)
    log('ARTIFACT',a.cue.resolve(),'BIN SHA256',digest);log('ARCHIVE',j['archive_sha256'],'OVERLAY',j['overlay_sha256'])
    ordinal=29 if a.mode=='full' else 1
    with private.Session(a.cue.resolve(),log,a.name) as s:
        pad=ReturnPad(s);pad.return_calls=0;pad.digest=digest;g=s.gdb
        if a.mode=='full':
            assert route.to_glimmer(pad,log,limit=1200);enter_from_glimmer(pad,log,digest)
        else:arrive(pad,log,'source quest return test')
        assert pad.state()[:2]==(0,ordinal)
        proof=json.loads(Path(report['quest_return']).read_text())
        entry=int(proof['entry'],16);size=proof['code_bytes']
        assert g.read(entry,size)==overlay.read_bytes()[entry-0x8006d264:entry-0x8006d264+size]
        base=word(g,0x80066f14);quest=j['quest'];q=word(g,base+88*quest['controller_index'])
        portal=next(e for e in j['actors'] if e['index']==quest['portal_index']);m=base+88*portal['index'];pos=portal['position']
        assert g._send('Z0,80019928,4')=='OK'
        def position(p,label):
            # A teleport must not carry the previous bell/high-drop velocity.
            # Original351d8(0) calls2e2bc, clearing the three motion vectors;
            # use that original initializer rather than writing motion results.
            call(pad,0x800351d8,0)
            assert g.write(private.d.SPYRO,struct.pack('<3i',*p))=='OK'
            log('NAMED SOURCE APPROACH FIXTURE',digest,label,p,'original351d8(0)/2e2bc motion reset plus player-origin RAM; no completion, destination or result write')
        position([pos[0],pos[1],pos[2]+320],'closed source portal')
        pad.run(0,8);assert word(g,q+32)==0 and pad.return_calls==0 and pad.state()[:2]==(0,ordinal)
        assert g.read(m+76,1)==b'\0'
        log('SOURCE RETURN CLOSED GATE PASS',digest,'source portal contact before bell completion does not call Return Home')
        def ring(n):
            e=next(e for e in j['actors'] if e['index']==quest['bell_indices'][n]);p=e['position'];bell=base+88*e['index']
            position([p[0]+900,p[1],p[2]+512],'bell '+str(n));pad.run(0,4)
            assert g.write(bell+24,struct.pack('<I',0x10000))=='OK'
            log('NAMED BELL FLAME MAILBOX FIXTURE',digest,n,'original actor damage mailbox; source state machine must complete ring')
            for _ in range(64):
                pad.run(0,1)
                if g.read(bell+72,1)==b'\0':break
            assert g.read(bell+72,1)==b'\0'
        ring(0);assert word(g,q+28)==word(g,q+32)==0
        for n in quest['sequence']:ring(n)
        assert word(g,q+32)==1 and pad.return_calls==0
        log('SOURCE BELL COMPLETION PASS',digest,quest['sequence'],'original CPU wrote completion; wrong first note reset; no completion fixture')
        position([pos[0],pos[1],pos[2]+1200],'different height outside source ellipsoid');pad.run(0,4)
        assert pad.return_calls==0 and pad.state()[:2]==(0,ordinal)
        position([pos[0]+1400,pos[1],pos[2]+320],'outside source ellipsoid');pad.run(0,4)
        assert pad.return_calls==0 and pad.state()[:2]==(0,ordinal)
        log('SOURCE RETURN CONTACT BOUNDS PASS',digest,'open visual alone does not travel; vertical/far approach fixtures outside source contact remain in Mesa')
        # The original S2 camera basis has hand1 (also calibrated by the full
        # Summer route). A sideways calibration walk here leaves the narrow
        # source terrace; it is unrelated to testing its return contact.
        driver=Driver(pad,log);track=[]
        log('SOURCE RETURN CONTROLLER BASIS',digest,'original S2 hand1; no lateral calibration walk on the narrow source terrace')
        for tick in range(240):
            if pad.return_calls or pad.state()[:2]!=(0,ordinal):break
            pad.run(private.d.buttons(driver.dpad_for(pos)),1)
            if tick%10==0:track.append((tick,pad.state()))
        assert pad.return_calls==1,('controller did not reach source contact',track,pad.state())
        if a.mode=='full':
            for _ in range(30):
                pad.run(0,60)
                if pad.state()[:2]==(0,0):break
            assert pad.state()[:2]==(0,0),pad.state()
            p=pad.state()[2];assert abs(p[0]-131700)<256 and abs(p[1]-17500)<256,(p,track)
            log('SOURCE QUEST FULL RETURN PASS',digest,'source bell completion -> finite controller contact -> original Return Home -> new Summer portal',p,'approach/mailbox fixtures named; no destination write')
        else:
            pad.run(0,90);assert pad.state()[:2]==(0,1)
            assert tuple(struct.unpack('<H',g.read(p,2))[0] for p in (0x80064682,0x80064686,0x8006468a))==(2,2,2)
            log('SOURCE QUEST COMPACT CONTAINMENT PASS',digest,'source bell completion -> finite controller contact -> actual resident exit guard -> Mesa retained; default learned skills2')
        log('SOURCE RETURN CONTROLLER TRACK',digest,json.dumps(track))
    assert private.sha(a.cue.with_suffix('.bin'))==digest
    log('SOURCE QUEST RETURN CHECK COMPLETE',digest,a.mode,'automated only; manual gameplay acceptance remains user-owned')


if __name__=='__main__':main()
