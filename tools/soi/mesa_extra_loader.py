"""Asset-loader integration for an additional PS1 Market Mesa scene.

The original resident EXE and Glimmer slot stay exact. Scene14 uses otherwise
unused progress ordinal29. A loader1 callback aliases its two streaming TOC
entries to physical slots198/199, restoring original cutscene entries on every
other scene load. All changes here are generated ROM assets, not host code.
"""
import hashlib
import json
import struct
from pathlib import Path
import mipsdis
from s2scene import read_archive
from mesa_standalone_boot import BASE,load,addiu,lw,sw,jump,code

SCENE=14
ORDINAL=29
CALLBACK_CALL=0x800705f0
MENU=0x80077374
NAME=0x80011e9c
TOC=0x800676d8+72*8


def sha(data):return hashlib.sha256(data).hexdigest()


def build(toc,new_music_start=None):
    """toc is the final packed WAD table; returns loader bytes and a receipt."""
    base,resident=mipsdis.exe_image();exe=mipsdis.EXE.read_bytes()
    assert sha(exe)=='7b54002789ab379e0d3a6b49d7b50078b9c9c2fed589f41ff9878ebb68fe0bc1'
    original=read_archive(mipsdis.EXE.parent,1)
    at=lambda address:address-base
    assert resident[at(0x80064940+SCENE)]==0
    assert resident[at(0x80064994+ORDINAL)]==0
    for table in (0x800647f4,0x80064814,0x80064834,0x80064b84,0x80060140,0x80061bb8):
        assert resident[at(table+ORDINAL)]==0
    caller=jump(NAME,True)
    direct=[base+i for i in range(0,len(resident),4) if struct.unpack_from('<I',resident,i)[0]==caller]
    assert direct==[0x80011aec],'startup function reuse requires its sole original caller'
    assert not any(struct.unpack_from('<I',resident,i)[0]==NAME for i in range(0,len(resident),4))
    campaign=list(resident[at(0x80064994):at(0x80064994)+29])
    assert SCENE not in campaign and len(set(campaign))==29
    lookup=list(resident[at(0x80064940):at(0x80064940)+84])
    assert all(lookup[scene]==i for i,scene in enumerate(campaign))
    legacy_scenes=campaign+list(range(70,82))
    assert SCENE not in legacy_scenes
    legacy_mapping={str(scene):dict(ordinal=lookup[scene],overlay_slot=14+2*lookup[scene],
                                   archive_slot=15+2*lookup[scene]) for scene in legacy_scenes}
    callback_decompile=Path(__file__).resolve().parents[2]/'data/analysis/overlay_001/decompiled/8006d960_FUN_8006d960.c'
    dispatch_text=callback_decompile.read_text()
    import re
    dispatch_cases={int(value,0) for value in re.findall(r'case (0x[0-9a-f]+|[0-9]+):',dispatch_text)}
    assert SCENE not in dispatch_cases
    output=bytearray(original);output+=bytes((-len(output))%4)
    install=BASE+len(output)
    # Metadata is installed at original title initialization, before a saved
    # scene can be selected; then persists across level overlay replacements.
    words=[]
    def store(address,value,op=43):
        words.extend(load(8,address)+load(9,value)+[op<<26|8<<21|9<<16])
    for address,value in ((0x80064940+SCENE,ORDINAL),(0x80064994+ORDINAL,SCENE),
                          (0x80064b84+ORDINAL,10),(0x80060140+ORDINAL,1),
                          # Original72aa4 uses ff for unused ordinal29, then
                          # indexes packed fly-in endpoints64a54. Give Mesa
                          # a conditional alias to original endpoint0.
                          (0x80064a34+ORDINAL,0),
                          # Original1d658 stage8 selects this GPU/DMA buffer
                          # size in KiB. Unused ordinal29 is zero in retail.
                          (0x80061bb8+ORDINAL,resident[at(0x80061bb8+1)])):
        store(address,value,40)
    store(0x800649b4+ORDINAL*4,NAME)
    # Original startup has executed these first12bytes before calling menu
    # initialization. Its only caller is the one-shot hardware startup.
    for i in range(0,12,4):store(NAME+i,struct.unpack_from('<I',b'Market Mesa\0',i)[0])
    words += [0x03e00008,0]
    output+=code(words)
    callback=BASE+len(output)
    words=[addiu(29,29,-32),sw(31,28,29),sw(16,24,29)]
    words += [jump(install,True),0]
    # Retail cinematic scene70 also streams through ordinal29. Preserve its
    # original metadata while that original scene runs; normal campaign loads
    # reinstall Mesa's otherwise unused campaign row before callbacks run.
    words+=load(8,0x80066f90)+[lw(8,0,8),addiu(9,0,70)]
    not_cinematic=len(words);words += [0,0]
    for table in (0x80064994,0x80064b84,0x80060140,0x80064a34,0x80061bb8):
        store(table+ORDINAL,resident[at(table+ORDINAL)],40)
    store(0x800649b4+ORDINAL*4,struct.unpack_from('<I',resident,at(0x800649b4+ORDINAL*4))[0])
    after_cinematic=len(words);words[not_cinematic]=5<<26|8<<21|9<<16|((after_cinematic-not_cinematic-1)&65535)
    # Return-position index29 maps to original entry0. Restore its original
    # bytes on every load; only a Mesa->Summer return substitutes the new
    # portal's arrival point. Original1c734/1c164 consume the packed fields.
    arrival=struct.pack('<4H',131700//4,17500//4,37732//4,0x800)
    for table in (0x80064b3c,0x80064a54):
        for i in range(2):store(table+4*i,struct.unpack_from('<I',resident,at(table+4*i))[0])
    words+=load(8,0x80066f90)+[lw(8,0,8),addiu(9,0,10)]
    not_home=len(words);words += [0,0]
    words+=load(8,0x80066fd0)+[lw(8,0,8),addiu(9,0,ORDINAL)]
    not_mesa=len(words);words += [0,0]
    for table in (0x80064b3c,0x80064a54):
        for i in range(2):store(table+4*i,struct.unpack_from('<I',arrival,4*i)[0])
    after_arrival=len(words)
    for branch_at in (not_home,not_mesa):words[branch_at]=5<<26|8<<21|9<<16|((after_arrival-branch_at-1)&65535)
    words+=load(8,0x80066f90)+[lw(16,0,8),addiu(9,0,SCENE)]
    branch=len(words);words += [0,0]
    # Mesa callback setup uses the original Glimmer layout, then restores
    # actual sceneID14. Progress index29 has already been selected by70498.
    for i,value in enumerate((*toc[198],*toc[199])):store(TOC+4*i,value)
    if new_music_start is not None:
        for i,value in enumerate((new_music_start,(1<<16)|30448)):store(0x80064dec+4*i,value)
    store(0x80066f90,11)
    words += [jump(0x8006d960,True),0]
    store(0x80066f90,SCENE)
    done_jump=len(words);words += [0,0]
    normal=len(words);words[branch]=5<<26|16<<21|9<<16|((normal-branch-1)&65535)
    for i,value in enumerate((*toc[72],*toc[73])):store(TOC+4*i,value)
    if new_music_start is not None:
        for i in range(2):store(0x80064dec+4*i,struct.unpack_from('<I',resident,at(0x80064dec+4*i))[0])
    words += [jump(0x8006d960,True),0]
    done=len(words);words[done_jump]=jump(callback+done*4)
    words += [lw(31,28,29),lw(16,24,29),0,0x03e00008,addiu(29,29,32)]
    output+=code(words)
    menu=BASE+len(output)
    output+=code([addiu(29,29,-24),sw(31,20,29),sw(4,16,29),jump(install,True),0,
                  lw(4,16,29),lw(31,20,29),0,addiu(29,29,24),
                  *struct.unpack_from('<2I',original,MENU-BASE),jump(MENU+8),0])
    assert struct.unpack_from('<I',original,CALLBACK_CALL-BASE)[0]==jump(0x8006d960,True)
    assert original[MENU-BASE:MENU-BASE+8]==bytes.fromhex('c0ffbd273400b3af')
    struct.pack_into('<I',output,CALLBACK_CALL-BASE,jump(callback,True))
    struct.pack_into('<II',output,MENU-BASE,jump(menu),0)
    changed=[i for i,(a,b) in enumerate(zip(original,output)) if a!=b]
    assert set(changed)<=set(range(CALLBACK_CALL-BASE,CALLBACK_CALL-BASE+4))|set(range(MENU-BASE,MENU-BASE+8))
    output+=bytes((-len(output))%2048)
    proof=dict(loader_sha256=sha(output),original_loader_sha256=sha(original),loader_bytes=len(output),
               original_executable_sha256=sha(exe),scene_id=SCENE,scene_id_hex=hex(SCENE),progress_ordinal=ORDINAL,
               original_campaign_scene_ids=campaign,original_defined_scene_mappings=legacy_mapping,
               original_dispatch_case_ids=sorted(dispatch_cases),original_dispatch_decompile_sha256=sha(callback_decompile.read_bytes()),
               new_scene_absent_from_original_campaign_and_dispatch=True,
               original_campaign_progress_indices_retained=list(range(29)),
               cinematic70_shared_ordinal29_metadata_restored=True,
               physical_overlay=198,physical_archive=199,metadata_install=hex(install),
               callback_wrapper=hex(callback),menu_wrapper=hex(menu),
               aliases=dict(resident_toc=hex(TOC),mesa=[*toc[198],*toc[199]],original=[*toc[72],*toc[73]]),
               startup_name_storage=hex(NAME),startup_direct_callers=[hex(x) for x in direct],
               new_music_start=new_music_start,original_glimmer_payloads_untouched=True,
               summer_return_position=[131700,17500,37732],summer_return_heading=0x800,
               conditional_return_table='80064b3c restart and80064a54 fly-in entry0 only while loading Summer from prior ordinal29; original restored otherwise',
               learned_flags_unmodified=True,exit_function_unmodified=True,release_eligible=False,
               limits=['Private implementation; original normal-flow portal/return/save/cutscene checks required.',
                       'Host application has campaign tables/slot limits requiring a separate lead-owned change.',
                       'No final gem/orb/spirit completion target invented.'])
    return bytes(output),proof


if __name__=='__main__':
    import argparse
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('layout',type=Path);ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--music-start',type=int);a=ap.parse_args()
    loader,proof=build(json.loads(a.layout.read_text())['wad_toc'],a.music_start)
    assert not a.out.exists();a.out.mkdir(parents=True)
    (a.out/'slot01-expanded.bin').write_bytes(loader);(a.out/'loader-report.json').write_text(json.dumps(proof,indent=2)+'\n')
    print('EXPANDED LOADER',proof['loader_sha256'],len(loader),'scene14/progress29/physical198+199; original learned flags and exit code untouched')
