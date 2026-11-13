"""Build a private loader1 bootstrap for a Mesa-only original PS1 ROM.

The mounted resident executable stays byte-exact retail. Loader1 installs a
resident restart routine after its original startup load. Retired Return Home
code supplies the resident space; portals/Exit Level resume the current level. The
original title tick starts the real level-loading flow without menu input.
This emits a bootstrap candidate, not a compact ROM or a release claim.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from s2scene import read_archive
import mipsdis

BASE=0x8006d264
EXIT=0x80019928
EXIT_END=0x80019c40
NEW_GAME=0x8007644c
NEW_GAME_BYTES=456
TITLE_TICK=0x8001ac20
GAME_OVER=0x8001a6f8
LOAD_SCREEN=0x80019364
MENU_INIT=0x80077374
LOAD_TICK=0x80070498
FLUSH=0x8005ff74


def sha(data):return hashlib.sha256(data).hexdigest()
def word(value):return struct.pack('<I',value&0xffffffff)
def jump(address,link=False):return (3 if link else 2)<<26|((address>>2)&0x3ffffff)
def addiu(rt,rs,n):return 9<<26|rs<<21|rt<<16|(n&65535)
def sw(rt,n,rs):return 43<<26|rs<<21|rt<<16|(n&65535)
def lw(rt,n,rs):return 35<<26|rs<<21|rt<<16|(n&65535)
def sh(rt,n,rs):return 41<<26|rs<<21|rt<<16|(n&65535)
def load(rt,n):return [15<<26|rt<<16|(n>>16),13<<26|rt<<21|rt<<16|(n&65535)]
def code(words):return b''.join(word(w) for w in words)


def build(speech_lba=90000,minimal_menu=False):
    exe=mipsdis.EXE.read_bytes();base,body=mipsdis.exe_image()
    assert sha(exe)=='7b54002789ab379e0d3a6b49d7b50078b9c9c2fed589f41ff9878ebb68fe0bc1'
    loader=read_archive(mipsdis.EXE.parent,1);original=bytes(loader)
    reset=bytearray(loader[NEW_GAME-BASE:NEW_GAME-BASE+NEW_GAME_BYTES])
    assert body[FLUSH-base:FLUSH-base+12]==bytes.fromhex('a0000a240800400144000924'),'original BIOS A0/44 stub'
    assert body[TITLE_TICK-base:TITLE_TICK-base+4]==word(addiu(29,29,-24)),'original title-tick entry'
    # Original pause tick800176a4 case2 normally resumes by clearing the game
    # state and CD pause request. Returning without these stores strands its
    # Return Home selection in substate2, calling this gate every frame.
    exit_gate=code(load(8,0x800681c8)+[sw(0,0,8)]+load(8,0x80068304)+[0x03e00008,sw(0,0,8)])
    resident_reset=EXIT+len(exit_gate);relocated=[];calls=[]
    for at in range(0,len(reset),4):
        w=struct.unpack_from('<I',reset,at)[0];op=w>>26
        if op not in (2,3):continue
        target=0x80000000|((w&0x3ffffff)<<2)
        if NEW_GAME<=target<NEW_GAME+len(reset):
            new=resident_reset+target-NEW_GAME;struct.pack_into('<I',reset,at,jump(new,op==3));relocated.append([hex(NEW_GAME+at),hex(target),hex(new)])
        else:
            assert op==3 and target==0x8001b3c8,('unexpected nonresident reset dependency',hex(target))
            calls.append(hex(target))
    restart=resident_reset+len(reset)
    # Both reset functions are original game code. The three learned flags are
    # the exact fields set to 2 by original 80054518 case 0 (swim/climb/headbash).
    cleanup_instruction=restart+8
    words=[addiu(29,29,-24),sw(31,20,29)]
    # Menu initialization temporarily replaces the class/Spyro tables. Its
    # original 800741a0 cleanup must run once before leaving that menu bank.
    # A title reset without reloading the menu must not restore a stale backup.
    # Every real 80077374 menu load reinstalls this payload and re-arms cleanup.
    if not minimal_menu:
        words+=[jump(0x800741a0,True),0]+load(8,cleanup_instruction)+[sw(0,0,8),jump(FLUSH,True),0]
    words+=[jump(0x8001aaa4,True),0,jump(resident_reset,True),0]
    words+=load(8,0x80060000)+[addiu(9,0,2),sh(9,0x4682,8),sh(9,0x4686,8),sh(9,0x468a,8)]
    words+=[jump(0x80019230,True),addiu(4,0,0)]
    # Original restart 8001a9b0 calls 80019230(0), then sets 80066eec=0
    # so loader1 is fetched again. The ordinary saved-game start value 2 only
    # works while the menu/loader overlay is still present (first cold boot).
    words+=load(8,0x80066eec)+[sw(0,0,8),lw(31,20,29),0,0x03e00008,addiu(29,29,24)]
    resident=exit_gate+reset+code(words)
    transition_gate=EXIT+len(resident)
    screen_entry=body[LOAD_SCREEN-base:LOAD_SCREEN-base+8]
    assert screen_entry==bytes.fromhex('e0ffbd270780033c'),'original transition-screen stack/global prologue'
    screen_words=load(8,0x80066f90)+[addiu(9,0,11),sw(9,0,8)]
    screen_words+=load(8,0x800698ec)+[sw(0,8,8),lw(9,4,8),0]
    branch=len(screen_words);screen_words+=[0,0]
    screen_words+=[addiu(9,0,1),sw(9,4,8),sw(0,0,8)]
    target=len(screen_words);screen_words[branch]=5<<26|9<<21|((target-branch-1)&65535)
    # Skip retail loading-art case0 (a stripped shared bank); case1 retains
    # the actual original asynchronous level loader and case2 enters gameplay.
    resident+=code(screen_words)+screen_entry+code([jump(LOAD_SCREEN+8),0])
    assert EXIT+len(resident)<=EXIT_END,'resident restart payload exceeds retired exit function'
    payload=original+bytes((-len(original))%4);payload_address=BASE+len(payload)
    payload+=resident;payload+=bytes((-len(payload))%4)
    wrapper_address=BASE+len(payload)
    # The detour preserves the original a0/ra, then replays both overwritten
    # original prologue instructions before rejoining the real menu initializer.
    wrapper=[addiu(29,29,-32),sw(31,28,29),sw(4,24,29)]
    wrapper+=load(4,EXIT)+load(5,payload_address)+load(6,len(resident))+[jump(0x8001b40c,True),0]
    wrapper+=load(8,TITLE_TICK)+load(9,jump(restart))+[sw(9,0,8),sw(0,4,8)]
    # Original last-life32e74 calls1a6f8. Retail then waits at loader1's
    # Game Over screen73510 and reads shared WAD6. This Mesa-only build
    # bypasses that screen through the same resident reset, so no stripped
    # background/menu dependency can be read and no further input is needed.
    wrapper+=load(8,GAME_OVER)+[sw(9,0,8),sw(0,4,8)]
    wrapper+=load(8,LOAD_SCREEN)+load(9,jump(transition_gate))+[sw(9,0,8),sw(0,4,8)]
    wrapper+=load(8,0x80066d44)+load(9,speech_lba)+[sw(9,0,8),jump(FLUSH,True),0]
    original_entry=original[MENU_INIT-BASE:MENU_INIT-BASE+8]
    assert original_entry==bytes.fromhex('c0ffbd273400b3af'),'original menu-initializer prologue'
    if minimal_menu:
        wrapper+=[lw(8,24,29),0];branch=len(wrapper);wrapper+=[0,0]
        # Exact essential startup calls from original 80077374(1), omitting
        # logos, the title scene/model substitution and memory-card menu flow.
        # Global0 supplies shared Spyro/font/SFX data through original 11f30.
        wrapper+=load(8,0x80060000)+[addiu(9,0,10),sw(9,0x6d48,8),sw(9,0x6d4c,8)]
        wrapper+=load(8,0x80068314)+[addiu(9,0,0x3fff),sw(9,0,8)]
        wrapper+=load(8,0x80067e74)+load(9,0x80180000)+[sw(9,0,8)]
        wrapper+=load(4,0x1c000)+[jump(0x8004d158,True),0]
        wrapper+=load(4,0x80067e00)+[addiu(5,0,0),jump(0x8001b3c8,True),addiu(6,0,0x84)]
        for function in (0x80011d58,0x80011d78,0x80011f30):wrapper+=[jump(function,True),0]
        target=len(wrapper);wrapper[branch]=4<<26|8<<21|((target-branch-1)&65535)
        wrapper+=[lw(31,28,29),0,0x03e00008,addiu(29,29,32)]
    else:
        wrapper+=[lw(4,24,29),lw(31,28,29),0,addiu(29,29,32)]
        wrapper+=list(struct.unpack('<2I',original_entry))+[jump(MENU_INIT+8),0]
    payload+=code(wrapper)
    # Original70498 case2 reads scene66f90 to select the cached WAD pair.
    # Enforce the sole scene before every original loader stage, including a
    # guidebook/replay request that may bypass the ordinary Return Home call.
    # Replay its two position-independent original instructions, then rejoin.
    load_entry=original[LOAD_TICK-BASE:LOAD_TICK-BASE+8]
    assert load_entry==bytes.fromhex('0680023cec6e428c'),'original loader reads66eec before its stack prologue'
    load_gate=BASE+len(payload)
    payload+=code(load(8,0x80066f90)+[addiu(9,0,11),sw(9,0,8)])+load_entry+code([jump(LOAD_TICK+8),0])
    payload=bytearray(payload)
    payload[MENU_INIT-BASE:MENU_INIT-BASE+8]=code([jump(wrapper_address),0])
    payload[LOAD_TICK-BASE:LOAD_TICK-BASE+8]=code([jump(load_gate),0])
    changes=[i for i,(a,b) in enumerate(zip(original,payload)) if a!=b]
    assert set(changes)<=set(range(MENU_INIT-BASE,MENU_INIT-BASE+8))|set(range(LOAD_TICK-BASE,LOAD_TICK-BASE+8))
    payload+=bytes((-len(payload))%2048)
    proof=dict(original_executable_sha256=sha(exe),original_loader_sha256=sha(original),loader_sha256=sha(payload),
               original_loader_bytes=len(original),loader_bytes=len(payload),wrapper=hex(wrapper_address),
               resident_payload=hex(EXIT),resident_payload_bytes=len(resident),resident_payload_sha256=sha(resident),
               exit_gate_bytes=len(exit_gate),exit_gate='Resume current level: game state800681c8=0 and CD pause80068304=0, as original pause tick800176a4 case2',
               resident_reset=hex(resident_reset),restart=hex(restart),speech_lba=speech_lba,
               minimal_menu=minimal_menu,required_wad_slots=([0,1,4,16,17] if minimal_menu else None),
               game_over_entry=hex(GAME_OVER),game_over_policy='Original last-life death32e74 -> resident Mesa reset; skip retail Game Over73510/WAD6 display',
               transition_gate=hex(transition_gate),transition_gate_entry=hex(LOAD_SCREEN),
               transition_gate_policy='Force scene11/screen0 before original transition branches; skip stripped loading artwork only; original loader/entry consumers retained',
               loader_gate=hex(load_gate),loader_gate_entry=hex(LOAD_TICK),sole_scene_id=11,
               loader_gate_policy='Force scene11 before original70498 reads64940/cached WAD pair; all guidebook/replay load requests remain Mesa',
               shared_interface_dependency='Original8004de1c reads cachedWAD4 offset800676f8 for guidebook pages; preserve shared bank4',
               original_menu_cleanup=('Title model substitution is omitted; original global model tables remain installed.' if minimal_menu else '800741a0 restores original class/Spyro tables once per menu-bank initialization'),
               retained_original_reset_sha256=sha(original[NEW_GAME-BASE:NEW_GAME-BASE+NEW_GAME_BYTES]),
               reset_absolute_branches_relocated=relocated,reset_external_calls=calls,
               learned_flags={'swimming':'80064682=2','climbing':'80064686=2','headbash':'8006468a=2'},
               original_flow=['80011e9c loads loader1',('80077374 detour performs original shared startup11d58/11d78/11f30; no title scene' if minimal_menu else '80077374 detour installs restart/exit gate; original initializer continues'),
                              '8001aaa4 original title reset','8001ac20 detour calls original title reset + relocated original 8007644c',
                              '80019230(0),80066eec=0 as original restart8001a9b0 -> reload loader1 ->80070498 ->800171bc ->8001722c'],
               original_executable_on_disc_unchanged=True,source_files_modified=False,
               release_eligible=False,limits=['Private bootstrap only; compact single-level content build remains required.',
                  'Cold boot without input, learned-move behavior, return/Exit Level/restart/game-over bounds need original-engine checks.',
                  'Not applied to the full-game patcher. No native main executable deployment.'])
    if not minimal_menu:proof['one_shot_cleanup_instruction']=hex(cleanup_instruction)
    return bytes(payload),resident,proof


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--speech-lba',type=int,default=90000)
    ap.add_argument('--minimal-menu',action='store_true',help='use original essential startup calls, omitting title/logo data and temporary title models')
    a=ap.parse_args()
    assert not a.out.exists();a.out.mkdir(parents=True)
    loader,resident,proof=build(a.speech_lba,minimal_menu=a.minimal_menu)
    path=a.out/'slot01-standalone-bootstrap.bin';path.write_bytes(loader)
    (a.out/'resident-restart.bin').write_bytes(resident);proof['loader']=str(path.resolve())
    (a.out/'bootstrap.json').write_text(json.dumps(proof,indent=2)+'\n')
    print('ORIGINAL EXE',mipsdis.EXE,'sha256',proof['original_executable_sha256'])
    print('LOADER BOOTSTRAP',path.resolve(),'sha256',proof['loader_sha256'],'bytes',len(loader))
    print('RECLAIMED EXIT FUNCTION',hex(EXIT),'..',hex(EXIT_END),'payload bytes',len(resident),'restart',proof['restart'])
    print('ORIGINAL RESET RELOCATION',json.dumps(proof['reset_absolute_branches_relocated']),'resident external calls',len(proof['reset_external_calls']))
    print('LIMIT',json.dumps(proof['limits']))


if __name__=='__main__':main()
