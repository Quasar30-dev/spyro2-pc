"""Bind the source bell-opened portal to the original Return Home consumer.

The GBA's actual contact metric is transformed through the existing anisotropic
Mesa port coordinates. The full disc's home metadata selects Summer Forest;
the standalone's resident exit guard deliberately keeps the player in Mesa.
No level ID, completion fixture, resident EXE change or guessed ice is added.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

from gba import Rom, ROM_SHA256
import soi_sprites
from soi_actor_overlay import Asm, BASE, REG
import mipsdis


def sha(b):return hashlib.sha256(b).hexdigest()


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--content-report',type=Path,required=True)
    ap.add_argument('--out',type=Path,required=True)
    a=ap.parse_args();assert not a.out.exists()
    report=json.loads(a.content_report.read_text());cp=Path(report['actor_conversion'])
    actor=json.loads(cp.read_text());archive=Path(actor['archive']);overlay=Path(actor['overlay'])
    assert sha(archive.read_bytes())==actor['archive_sha256']==report['archive_sha256']
    original=overlay.read_bytes();assert sha(original)==actor['overlay_sha256'] and len(original)%2048==0
    rom=Rom();assert sha(rom.data)==ROM_SHA256
    functions=soi_sprites.functions()
    assert 'local_c = 0x1000;' in functions['08019410']
    assert 'iVar21 + 0x1400' in functions['0801d8a4']
    assert 'param_1 < iVar1' in functions['08016dec']
    assert 'param_3[2]' in functions['080011a4'] and 'param_1[2]' in functions['0800122c']
    labels=actor['extension']['labels'];hook=labels['portal_open'];at=hook-BASE
    assert original[at:at+8]==bytes.fromhex('200008244c0008a2')
    # This hook is reached only after the source quest controller reports done.
    asm=Asm(BASE+len(original));entry=asm.pc
    asm.li('t0',32);asm.sb('t0',76,'s0');asm.li('a1',1);asm.jal(labels['clip'])
    asm.li('t0',0x800681c8);asm.lw('t0',0,'t0');asm.bne('t0','zero','done')
    asm.li('t6',0x80069ff0);asm.move('t7','zero')
    for axis,(offset,bound) in enumerate(((0,1024),(4,1024),(8,512))):
        asm.lw('t0',offset,'t6');asm.lw('t1',12+offset,'s0');asm.subu('t0','t0','t1')
        if axis==2:
            # GBA19410 clamps its position directly to floor height; S2's
            # original1722c/1a2fc add164hex to the feet when storing Spyro's
            # origin. Convert that origin back to feet before the source metric.
            asm.addiu('t0','t0',-0x164)
        asm.addiu('t2','t0',bound);asm.imm(11,'t2','t2',2*bound);asm.beq('t2','zero','done')
        asm.emit(REG['t0']<<21|REG['t0']<<16|0x18)  # signed mult
        asm.emit(REG['t1']<<11|0x12)               # mflo
        if axis==2:
            asm.emit(REG['t1']<<16|REG['t2']<<11|1<<6)
            asm.emit(REG['t1']<<16|REG['t1']<<11|2<<6)
            asm.addu('t1','t1','t2')
        asm.addu('t7','t7','t1')
    # Source Q10: horizontal maps by sqrt(6)/24, vertical by 1/24.
    # 96*(dx^2+dy^2)+576*dz^2 < (0x1000+0x1400)^2;
    # divide exactly by96: dx^2+dy^2+6*dz^2 <884736.
    asm.li('t0',884736);asm.r(0x2b,'t0','t7','t0');asm.beq('t0','zero','done')
    asm.li('a0',-1);asm.jal(0x80019928)
    asm.label('done');asm.jump(labels['out'])
    code=asm.finish();result=bytearray(original+code);result+=bytes((-len(result))%2048)
    struct.pack_into('<II',result,at,2<<26|((entry>>2)&0x3ffffff),0)
    assert result[:at]==original[:at] and result[at+8:len(original)]==original[at+8:]
    a.out.mkdir(parents=True);target=a.out/'slot16-quest-return.bin';target.write_bytes(result)
    evidence={}
    for name in ['08019410','0801d8a4','08016e10','08016dec','080011a4','0800122c']:
        text=functions[name];p=a.out/(name+'.c');p.write_text(text)
        evidence[name]=dict(ghidra_export=str(p.resolve()),sha256=sha(p.read_bytes()))
    dis=a.out/'return-adapter.mips.txt';dis.write_text('\n'.join(mipsdis.dis(bytes(result),BASE,entry,entry+len(code)))+'\n')
    proof=dict(source_rom_sha256=ROM_SHA256,source_functions=evidence,source_player_radius=0x1000,
        source_portal_radius=0x1400,source_contact_radius=0x2400,metric='dx*dx+dy*dy+6*dz*dz <884736',
        strict_inequality=True,ps1_origin_above_feet=0x164,
        ps1_coordinate_oracle=['8001722c','8001a2fc'],source_floor_clamp='08019410 positionZ=max(positionZ,floorZ)',
        quest_completion_gate_preserved=True,original_consumer='80019928(-1)',
        hook=hex(hook),entry=hex(entry),code_bytes=len(code),overlay=str(target.resolve()),overlay_sha256=sha(result),
        parent_overlay=str(overlay.resolve()),parent_overlay_sha256=sha(original),
        limits=['Source-informed PS1 adapter; coordinate quantization and PS1 player-origin convention remain port limitations.',
                'Exact-artifact original CPU closed/open/contact/full return/standalone containment gates required.',
                'Native extra-scene and Return Home bridge remain lead-owned integration.'])
    proofpath=a.out/'quest-return.json';proofpath.write_text(json.dumps(proof,indent=2)+'\n')
    actor.update(parent_actor_conversion=str(cp.resolve()),parent_actor_conversion_sha256=sha(cp.read_bytes()),
        overlay=str(target.resolve()),overlay_sha256=sha(result),quest_return=str(proofpath.resolve()),immutable=False,release_eligible=False)
    actor['quest']=dict(actor['quest'],travel_binding='Source contact metric invokes original Return Home; full home metadata/standalone exit guard select behavior.')
    actorpath=a.out/'actor-conversion.json';actorpath.write_text(json.dumps(actor,indent=2)+'\n')
    report.update(actor_conversion=str(actorpath.resolve()),quest_return=str(proofpath.resolve()),release_eligible=False)
    (a.out/'mesa-ps1-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print('QUEST RETURN OVERLAY',target.resolve(),'sha256',sha(result),flush=True)
    print('SOURCE CONTACT METRIC',ROM_SHA256,proof['metric'],'source19410 radius1000; source1d8a4 radius1400; strict original16dec compare',flush=True)
    print('PRESERVED',sha(result),'all original/actor/C9 overlay bytes except8-byte open-portal trampoline; all archive/model/gameplay/SPU/terrain bytes unchanged; declared CD sectors',len(result)//2048,flush=True)
    print('LIMIT private candidate; exact original CPU full/compact travel tests and native integration still required',flush=True)


if __name__=='__main__':main()
