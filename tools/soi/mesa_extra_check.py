"""Original CPU extra-scene streaming checks, with explicit load-call fixtures.

This is not normal portal travel or manual gameplay validation. Actual portal
crossings and original save/return flow need separate exact-disc checks.
"""
import argparse
import json
import struct
from pathlib import Path
import ds_private_check as private
import ds_pad_proof as route
import ds_harness as d
from mesa_standalone_check import call


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('cue',type=Path)
    ap.add_argument('--loader-report',type=Path,required=True);ap.add_argument('--task-root',type=Path,required=True)
    ap.add_argument('--name',required=True);a=ap.parse_args()
    private.TASK=a.task_root.resolve();proof=json.loads(a.loader_report.read_text())
    log=d.Log(a.name);digest=private.sha(a.cue.with_suffix('.bin'));log('ARTIFACT',a.cue.resolve(),'BIN sha256',digest)
    log('LOADER REPORT',a.loader_report.resolve(),'sha256',private.sha(a.loader_report))
    with private.Session(a.cue.resolve(),log,a.name) as s:
        pad=private.Pad(s);g=s.gdb
        assert route.to_glimmer(pad,log,limit=1200)
        assert g.read(0x8006494e,1)==bytes([29]) and g.read(0x800649b1,1)==bytes([14])
        assert g.read(0x80011e9c,12)==b'Market Mesa\0'
        assert g.read(0x80064682,2)==bytes(2),'full game must not unlock swimming'
        original_alias=struct.pack('<4I',*proof['aliases']['original'])
        mesa_alias=struct.pack('<4I',*proof['aliases']['mesa'])
        assert g.read(0x80067918,16)==original_alias
        original_exit=g.read(0x80019928,16)
        log('EXPANDED ORIGINAL GLIMMER INITIAL',digest,'ordinal1; original intro aliases; normal learned flag0; name metadata installed')
        def load(scene,index):
            # Destination and original load-call registers are explicit RAM/
            # register fixtures. All overlay/archive data is read from this ROM.
            assert g.write(0x80066f90,struct.pack('<I',scene))=='OK'
            call(pad,0x80019230,0)
            assert g.write(0x80066eec,bytes(4))=='OK'
            for ticks in range(0,1800,60):
                pad.run(0,60);state=pad.state();log('FINITE LOAD FIXTURE',scene,ticks+60,state)
                if state[:2]==(0,index):return
            raise AssertionError('original loader did not reach requested extra/retail scene')
        load(14,29)
        assert g.read(0x80066f90,4)==struct.pack('<I',14)
        assert g.read(0x80067918,16)==mesa_alias
        assert struct.unpack('<I',g.read(0x800673d8,4))[0]>100
        assert g.read(0x80019928,16)==original_exit,'standalone exit patch leaked into full game'
        assert g.read(0x80064682,2)==bytes(2)
        pad.run(0,300)
        log('EXPANDED EXTRA SCENE CPU PASS',digest,'scene14 ordinal29 physical198/199; real ROM load; original exit/learned flags retained; 300 ticks; explicit destination fixture')
        load(11,1)
        assert g.read(0x80067918,16)==original_alias
        log('EXPANDED GLIMMER RESTORE CPU PASS',digest,'scene11 ordinal1; real original Glimmer reloaded; cutscene slots72/73 restored')
        load(10,0);assert g.read(0x80067918,16)==original_alias
        pad.run(0,120)
        log('EXPANDED SUMMER FOREST LOAD CPU PASS',digest,'scene10 ordinal0; original Summer Forest loaded; no standalone restrictions')
        (s.home/'ram.bin').write_bytes(g.read(0x80000000,0x200000))
    assert private.sha(a.cue.with_suffix('.bin'))==digest
    log('LIMIT loader-call fixtures only; actual new portal/return/save/cutscene/music/PC gates remain')


if __name__=='__main__':main()
