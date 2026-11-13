"""Finite original-PS1 damage, critter feeding and source bell execution.

Source approaches, health, Sparx origins and named hit mailboxes are explicit
RAM fixtures. State transitions, actual resident damage/feeding, original SPU
calls and persistence registration execute on the copied emulator CPU.
"""
import argparse
import json
from pathlib import Path
import struct

import ds_private_check as private
import ds_pad_proof as route
from soi_actor_runtime_check import ActorPad
from soi_container_runtime_check import word


class FeaturePad(ActorPad):
    def _wait_stop(self,limit=30.,probe=None):
        for _ in range(32):
            stop=super()._wait_stop(limit,probe)
            if self.g.pc()!=0x80019928 or not getattr(self,'observe_return',False):return stop
            assert self.g.regs()[4]==0xffffffff
            self.source_return_seen=True
            self.s.log('FINAL CONTENT ORIGINAL RETURN CALL',self.digest,'actual CPU80019928(-1) from completed source portal')
            assert self.g._send('z0,80019928,4')=='OK'
            self.g.s.sendall(b'$c#63')
        raise AssertionError('finite source-return observation bound')


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('cue',type=Path);ap.add_argument('proof',type=Path);ap.add_argument('--work',type=Path,required=True)
    ap.add_argument('--entry',choices=('new-game','standalone','expanded'),default='new-game')
    ap.add_argument('--return-test',action='store_true',help='After the feature fixtures, walk into the completed source portal through original Return Home')
    a=ap.parse_args();cue=a.cue.resolve();proof=json.loads(a.proof.read_text());digest=private.sha(cue.with_suffix('.bin'))
    private.TASK=a.work.resolve();private.d.OUT=a.work.resolve()/'evidence';private.d.OUT.mkdir(parents=True,exist_ok=True)
    log=private.d.Log('actors-features');log('ARTIFACT',cue,'BIN SHA256',digest);log('SLOT17 SHA256',proof['archive_sha256'],'SLOT16 SHA256',proof['overlay_sha256'])
    cold=a.entry=='standalone';ordinal=29 if a.entry=='expanded' else 1
    with private.Session(cue,log,'actors-features',start_paused=cold) as s:
        pad=FeaturePad(s,defer_start=cold);pad.sounds=set();g=s.gdb;loaded={}
        def observe():
            if word(g,private.d.LEVEL)!=ordinal:return False
            base=word(g,0x80066f14);assert g.read(base+88*proof['count']+72,1)==b'\xff'
            loaded['base']=base;assert g._send('Z0,80000080,4')=='OK';return True
        pad.loader_observer=observe;assert g._send('Z0,8001d2d0,4')=='OK'
        if cold:pad.start()
        if a.entry=='standalone':
            from mesa_standalone_check import arrive
            arrive(pad,log,'final content feature gate')
        else:
            assert route.to_glimmer(pad,log,limit=1200)
            if a.entry=='expanded':
                from mesa_portal_check import enter_from_glimmer
                enter_from_glimmer(pad,log,digest)
        pad.run(0,20);assert pad.state()[:2]==(0,ordinal)
        base=loaded['base'];origin=g.read(private.d.SPYRO,12)
        assert g._send('Z0,8007ded0,4')=='OK';assert g._send('Z0,80050648,4')=='OK';assert g._send('Z0,80050720,4')=='OK'
        def put(addr,value):assert g.write(addr,struct.pack('<I',value))=='OK'
        def approach(e,health=3,distance=900):
            from mesa_standalone_check import call
            call(pad,0x800351d8,0)
            pos=e['position'];put(0x8006a248,health);put(0x8006a24c,600)
            assert g.write(private.d.SPYRO,struct.pack('<3i',pos[0]+distance,pos[1],pos[2]+512))=='OK'
            return base+88*e['index']
        # Actual resident damage, with starting health and approach fixtures.
        for source in [0,9]:
            e=next(e for e in proof['actors'] if e['kind'] in [1,2] and e['source_index']==source);m=approach(e,distance=600);pad.run(0,2);put(0x8006a24c,0)
            before=word(g,0x8006a248);pad.run(0,24);after=word(g,0x8006a248)
            log('ORIGINAL SOURCE DAMAGE',digest,'source',source,'index',e['index'],'health',before,'->',after,'resident consumer8002b0b8; no health result writes')
            assert before==3 and 0<=after<before,(source,before,after)
        for e in [e for e in proof['actors'] if e['kind']==3]:
            m=approach(e,health=2);pad.run(0,4)
            # A remote teleport must move Sparx's origin too; this is the
            # original-CPU equivalent of the native scene-start fixture.
            end=word(g,0x80066fb4);companions=[]
            for at in range(base,end,88):
                if struct.unpack('<H',g.read(at+54,2))[0]==0x78 and g.read(at+72,1)[0]<128:companions.append(at)
            pos=list(struct.unpack('<3i',g.read(private.d.SPYRO,12)));pos[2]+=512
            for at in companions:assert g.write(at+12,struct.pack('<3i',*pos))=='OK'
            put(m+24,0x10000);pad.run(0,2)
            food=[]
            for at in range(base,word(g,0x80066fb4),88):
                if struct.unpack('<H',g.read(at+54,2))[0]==0x10 and g.read(at+72,1)[0]<128:food.append(at)
            assert food and g.read(m+72,1)==b'\x07',('No source butterfly release',e['index'])
            for _ in range(240):
                pad.run(0,1)
                if word(g,0x8006a248)>2:break
            final=word(g,0x8006a248)
            log('ORIGINAL CRITTER FEEDING',digest,'source',e['source_index'],'index',e['index'],'class10',list(map(hex,food)),'Sparx-origin fixtures',list(map(hex,companions)),'health2 ->',final,'state',g.read(m+72,1).hex())
            assert final==3,(e['source_index'],final,[(hex(at),g.read(at,88).hex()) for at in food])
        quest=proof['quest'];controller=base+88*quest['controller_index'];q=word(g,controller)
        def ring(n):
            e=next(e for e in proof['actors'] if e['index']==quest['bell_indices'][n]);m=approach(e);pad.run(0,4);put(m+24,0x10000)
            for _ in range(64):
                pad.run(0,1)
                if g.read(m+72,1)==b'\0':break
            assert g.read(m+72,1)==b'\0','Bell did not complete its ring animation'
            log('ORIGINAL SOURCE BELL',digest,'ordinal',n,'progress',word(g,q+28),'complete',word(g,q+32),'source sound observed',(0x2e6,n) in pad.sounds)
            assert (0x2e6,n) in pad.sounds
        ring(0);assert word(g,q+28)==0 and word(g,q+32)==0
        for n in quest['sequence']:ring(n)
        assert word(g,q+32)==1
        assert all((0x2e6,n) in pad.spu_plays for n in proof['bell_sounds']['sample_indices']),'Original source bell sound allocator not reached'
        persistence=0x8006718c;registered=[]
        for i in range(word(g,persistence+0x240)):
            p=word(g,persistence+0x40+8*i)
            if p&0xffffff==(q+32)&0xffffff:registered.append(p)
        assert registered,'Original quest completion not in resident persistence list'
        portal=next(e for e in proof['actors'] if e['index']==quest['portal_index']);m=approach(portal,distance=1600);pad.run(0,8);assert g.read(m+76,1)==b'\x20'
        log('ORIGINAL SOURCE QUEST COMPLETE',digest,'sequence',quest['sequence'],'saved completion address',hex(q+32),'resident records',list(map(hex,registered)),'source-position portal visual open from outside contact; travel has separate exact-image gate')
        # A distinct live source enemy, using a real controller breath. The
        # approach/facing/health are fixtures; no hit mailbox is written.
        e=next(e for e in proof['actors'] if e['kind']==1 and e['source_index']==141)
        m=approach(e);pad.run(0,48);m=approach(e);put(0x8006a05c,0x800);pad.run(0,4)
        pad.index=e['index'];pad.release_target=m;pad.release_return=None;pad.released=None
        assert g._send('Z0,80039334,4')=='OK'
        pad.run(private.d.buttons('CIRCLE'),4);pad.run(0,160)
        log('ORIGINAL PHYSICAL FLAME RESULT',digest,'source141 index',e['index'],'state',g.read(m+72,1).hex(),'released',pad.released,'Spyro mode',word(g,0x8006a048),'position',pad.state()[2],'approach/facing/health fixtures; no enemy mailbox writes')
        assert g.read(m+72,1)==b'\xfd' and pad.released and pad.released['value']==e['value'],'Original controller flame did not kill named source141'
        pad.release_target=None
        assert g.write(private.d.SPYRO,origin)=='OK';pad.run(0,600)
        assert pad.state()[:2]==(0,ordinal),'content fixtures escaped their exact target'
        (private.d.OUT/'final-ram.bin').write_bytes(g.read(0x80000000,0x200000))
        log('ORIGINAL FEATURE EXECUTION COMPLETE',digest,'both enemy-family damage; all3 source class10 feeding; source bell sequence/SPU calls/resident persistence/open visual; source141 physical controller flame death/reward; 600 later samples; explicit fixture boundaries above; no manual acceptance')
        if a.return_test:
            assert proof.get('quest_return') and a.entry in ('expanded','standalone')
            from mesa_standalone_check import call
            from ds_mesa_play import Driver
            call(pad,0x800351d8,0)
            pos=portal['position'];start=[pos[0]+1400,pos[1],pos[2]+320]
            assert g.write(private.d.SPYRO,struct.pack('<3i',*start))=='OK'
            log('NAMED FINAL RETURN APPROACH',digest,start,'original idle/motion reset and origin fixture only; completed source quest retained')
            pad.observe_return=True;pad.source_return_seen=False;pad.digest=digest
            assert g._send('Z0,80019928,4')=='OK';driver=Driver(pad,log)
            for _ in range(240):
                if pad.source_return_seen:break
                pad.run(private.d.buttons(driver.dpad_for(pos)),1)
            assert pad.source_return_seen,'completed source portal did not consume physical controller contact'
            if a.entry=='expanded':
                for _ in range(30):
                    pad.run(0,60)
                    if pad.state()[:2]==(0,0):break
                assert pad.state()[:2]==(0,0)
                p=pad.state()[2];assert abs(p[0]-131700)<256 and abs(p[1]-17500)<256,p
                log('FINAL CONTENT SOURCE FULL RETURN PASS',digest,p,'source bell completion/physical controller contact/original Return Home; no destination write')
            else:
                pad.run(0,90);assert pad.state()[:2]==(0,1)
                log('FINAL CONTENT SOURCE COMPACT CONTAINMENT PASS',digest,'physical controller contact reaches resident exit guard and stays in Mesa')
    assert private.sha(cue.with_suffix('.bin'))==digest


if __name__=='__main__':main()
