"""Finite no-input cold boot and resident exit/restart checks on a private PS1.

Only the separately owned emulator/profile/hidden desktop is used. Register-call
probes are explicit test fixtures; installed code comes from the ROM's loader1.
No user game process or executable is opened for mutation.
"""
import argparse
import json
import struct
from pathlib import Path
import ds_private_check as private
import ds_harness as d


def register(g,index,value):
    assert g._send(f'P{index:x}='+struct.pack('<I',value&0xffffffff).hex())=='OK','private GDB register write'


def call(pad,address,arg):
    g=pad.g;saved=g.regs();assert g.pc()==d.LOGIC
    register(g,4,arg);register(g,31,d.LOGIC);register(g,37,address)
    g.s.sendall(b'$c#63');pad._wait_stop();assert g.pc()==d.LOGIC
    for i,value in enumerate(saved):
        if i:register(g,i,value)


def arrive(pad,log,label,limit=1800):
    for tick in range(0,limit,60):
        pad.run(0,60)
        row=pad.state();log('FINITE ZERO INPUT',label,tick+60,'/',limit,'state',row)
        if row[:2]==(0,1):return tick+60
    raise AssertionError(label+' did not enter Mesa within finite zero-input ticks')


def menu(pad):
    return struct.unpack('<4I',pad.g.read(0x800698ec,16))


def press(pad,button,settle=16):
    pad.run(d.buttons(button),1);pad.run(0,settle)


def pause_checks(pad,log,digest):
    # Original800176a4: primary menu selection5 is Return Home, selection1
    # enters the guidebook through8004de1c and retained shared WAD4.
    g=pad.g;initial_lock=g.read(0x800698fc,4)
    # The original first-Glimmer tutorial hides Return Home (five rows rather
    # than six) until progression clears698fc. Exercise the future unlocked
    # pause route as an explicit RAM progression fixture, never an on-disc edit.
    press(pad,'START',40)
    pause_lock=g.read(0x800698fc,4)
    if pause_lock!=bytes(4):
        assert g.write(0x800698fc,bytes(4))=='OK'
        log('NAMED PAUSE PROGRESSION FIXTURE',digest,'original pause init800175a4 computes Return Home lock800698fc',pause_lock.hex(),'->0 only in private RAM')
    assert pad.state()[0]==4 and menu(pad)[1]==1,(pad.state(),menu(pad))
    log('FINITE PAUSE INITIAL',digest,pad.state(),menu(pad))
    for _ in range(5):press(pad,'DOWN')
    assert menu(pad)[2:]==(5,0),menu(pad)
    press(pad,'CROSS',60)
    assert pad.state()[:2]==(0,1),(pad.state(),menu(pad))
    assert pad.g.read(0x80068304,4)==bytes(4)
    log('ORIGINAL PAUSE RETURN HOME BOUNDED PASS',digest,'finite START/DOWNx5/CROSS samples; original pause handler -> ROM exit guard -> normal Mesa gameplay and CD resumed')
    assert g.write(0x800698fc,initial_lock)=='OK'
    press(pad,'START',40);press(pad,'DOWN');press(pad,'CROSS',90)
    log('FINITE GUIDEBOOK',digest,pad.state(),menu(pad))
    assert pad.state()[0]==4 and menu(pad)[1] in (3,4),(pad.state(),menu(pad))
    assert pad.g.read(0x80069910,4)!=bytes(4),'original guidebook did not finish its shared-bank load'
    for _ in range(6):
        if pad.state()[0]==0:break
        press(pad,'TRIANGLE',45)
    assert pad.state()[:2]==(0,1),(pad.state(),menu(pad))
    log('ORIGINAL SHARED GUIDEBOOK PASS',digest,'finite controller open/cancel returns to Mesa; retained WAD4 original consumer; no travel destination writes')


def move_check(pad,log,digest):
    # Original80035d38 reads learned headbash6468a and Triangle edge683a4.
    pad.run(d.buttons('CROSS'),12);pad.run(0,1);pad.run(d.buttons('TRIANGLE'),1)
    states=[]
    for _ in range(48):
        states.append(struct.unpack('<I',pad.g.read(0x8006a040,4))[0]);pad.run(0,1)
    assert 0x2e in states,states
    pad.run(0,90);assert pad.state()[:2]==(0,1)
    log('ORIGINAL HEADBASH INPUT PASS',digest,'finite CROSS then TRIANGLE reaches original state2e with default learned flag; no state/position writes',states)


def game_over_check(pad,log,digest):
    g=pad.g
    assert g.read(0x80069894,4)==b'\xff'*4,'original normal campaign challenge index required'
    assert g.write(0x8006712c,bytes(4))=='OK'
    pad.trace_points={0x8001a6f8};assert g._send('Z0,8001a6f8,4')=='OK'
    call(pad,0x800351d8,0x1f)
    log('NAMED LAST-LIFE DEATH FIXTURE',digest,'lives6712c=0 in private RAM; actual original state setter800351d8(1f); original35d38 ->32e74 must reach1a6f8')
    states=[]
    for tick in range(0,1800,30):
        pad.run(0,30);states.append((tick+30,pad.state()[:2]))
        if not pad.trace_points and pad.state()[:2]==(0,1):break
    assert not pad.trace_points,'original game-over entry never reached'
    assert pad.state()[:2]==(0,1),states
    lives=struct.unpack('<i',g.read(0x8006712c,4))[0];assert lives>=0,lives
    assert tuple(struct.unpack('<H',g.read(p,2))[0] for p in (0x80064682,0x80064686,0x8006468a))==(2,2,2)
    log('ORIGINAL GAME OVER REENTRY PASS',digest,'original death32e74 -> ROM gate1a6f8 -> resident reset with zero further input -> only Mesa; retail Game Over screen intentionally bypassed; learned flags restored; reset lives',lives,'finite states',states)


def foreign_load_check(pad,log,digest):
    g=pad.g
    for scene in (10,63,70):
        assert g.write(0x80066f90,struct.pack('<I',scene))=='OK'
        call(pad,0x80019230,0);assert g.write(0x80066eec,bytes(4))=='OK'
        arrive(pad,log,'bounded foreign scene '+str(scene))
        assert g.read(0x80066f90,4)==struct.pack('<I',11)
        log('ORIGINAL FOREIGN TRAVEL BOUNDED PASS',digest,'named removed-scene request fixture',scene,'original loader gate routes to Mesa11/ordinal1; no absent level is loaded')


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('cue',type=Path)
    ap.add_argument('--bootstrap',type=Path,required=True);ap.add_argument('--name',required=True)
    ap.add_argument('--task-root',type=Path,required=True);ap.add_argument('--build-report',type=Path,required=True)
    ap.add_argument('--pause',action='store_true',help='finite physical pause Return Home and guidebook checks')
    ap.add_argument('--moves',action='store_true',help='finite headbash controller input with default learned flag')
    ap.add_argument('--game-over',action='store_true',help='named last-life/death-state fixture through original game-over flow')
    ap.add_argument('--foreign-loads',action='store_true',help='named Summer/boss/cinematic load requests must all stay Mesa')
    a=ap.parse_args();cue=a.cue.resolve();private.TASK=a.task_root.resolve()
    proof=json.loads(a.bootstrap.read_text());build=json.loads(a.build_report.read_text())
    resident=a.bootstrap.with_name('resident-restart.bin').read_bytes()
    assert private.sha(a.bootstrap.with_name('resident-restart.bin'))==proof['resident_payload_sha256']
    expected=bytearray(resident)
    if proof.get('one_shot_cleanup_instruction'):
        at=int(proof['one_shot_cleanup_instruction'],16)-0x80019928;expected[at:at+4]=bytes(4)
    log=d.Log(a.name);digest=private.sha(cue.with_suffix('.bin'));log('ARTIFACT',cue,'BIN sha256',digest)
    log('BOOTSTRAP',a.bootstrap.resolve(),'sha256',private.sha(a.bootstrap))
    with private.Session(cue,log,a.name) as s:
        pad=private.Pad(s);g=s.gdb
        ticks=arrive(pad,log,'cold boot')
        assert g.read(0x80019928,len(resident))==expected,'ROM-installed resident restart bytes including declared one-shot cleanup'
        assert g.read(0x8001ac20,8)==struct.pack('<II',0x08000000|((int(proof['restart'],16)>>2)&0x3ffffff),0)
        if proof.get('game_over_entry'):
            assert g.read(int(proof['game_over_entry'],16),8)==g.read(0x8001ac20,8),'standalone game-over reset detour'
        skills=struct.unpack('<3H',b''.join(g.read(p,2) for p in (0x80064682,0x80064686,0x8006468a)))
        assert skills==(2,2,2),skills
        objects=struct.unpack('<I',g.read(0x80066f14,4))[0];money=objects+88*build['moneybags_inert']['index']
        assert g.read(money+72,1)==b'\xfd'
        assert struct.unpack('<I',g.read(0x800673d8,4))[0]==build['regions']
        log('ORIGINAL NO-INPUT BOOT PASS',digest,'zero nonzero-button samples;',ticks,'ticks; scene index1; installed code exact; Moneybags FD; regions',build['regions'])
        log('ORIGINAL LEARNED FLAGS PASS',digest,'swim80064682/climb80064686/headbash8006468a =',skills,'source original80054518 case0; movement execution remains separate')
        before=g.read(0x8006715c,4)+g.read(0x800681c8,4)
        for argument in (-1,0):
            call(pad,0x80019928,argument)
            assert g.read(0x8006715c,4)+g.read(0x800681c8,4)==before
        log('ORIGINAL EXIT GATE PASS',digest,'actual CPU calls 80019928(-1) / (0) return without changing destination/state; private register-call fixtures')
        if a.pause:pause_checks(pad,log,digest)
        if a.moves:move_check(pad,log,digest)
        if a.game_over:game_over_check(pad,log,digest)
        if a.foreign_loads:
            assert proof.get('loader_gate'),'ROM has no general sole-scene loader gate'
            foreign_load_check(pad,log,digest)
        call(pad,0x8001aaa4,0)
        assert pad.state()[0]==11
        restarted=arrive(pad,log,'title/quit restart')
        assert tuple(struct.unpack('<H',g.read(p,2))[0] for p in (0x80064682,0x80064686,0x8006468a))==(2,2,2)
        log('ORIGINAL TITLE RESTART PASS',digest,restarted,'zero-input ticks; returns to scene index1 with learned flags retained; original title-reset call fixture')
        pad.run(0,600);assert pad.state()[:2]==(0,1)
        log('ORIGINAL STANDALONE FINITE TICKS PASS',digest,600,'further ticks; no user session/manual gameplay validation')
        (s.home/'ram.bin').write_bytes(g.read(0x80000000,0x200000))
    assert private.sha(cue.with_suffix('.bin'))==digest
    log('STANDALONE BOOT CHECK PASS',digest,'bootstrap prototype only; compact-level content and full-target isolation still required')


if __name__=='__main__':main()
