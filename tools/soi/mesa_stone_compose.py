"""Repack stone terrain around explicit model reservations, preserving content.

The input is an immutable, hash-bound content archive/report. This produces a
private candidate only. It never updates a disc, launcher or release eligibility.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from types import SimpleNamespace
import numpy as np
import mesa_ps1
import mesa_vram
import s2scene_build
from spyrodisc import wad_index
from mesa_oracle_check import near_polygons
from soi_model_convert import terrain_mask


def sha(b):return hashlib.sha256(b).hexdigest()
def aligned(n):return (n+2047)&~2047


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--content-report',type=Path,required=True)
    ap.add_argument('--reservation',type=Path,required=True)
    ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--terrain-report',type=Path,help='reuse a hash-bound terrain build; reservation intersections are checked again')
    ap.add_argument('--material-limit',type=int,default=120,choices=range(1,129),metavar='1..128')
    a=ap.parse_args();assert not a.out.exists(),'refusing to reuse composition directory'
    content=json.loads(a.content_report.read_text());path=Path(content['archive']);base=path.read_bytes()
    assert sha(base)==content['archive_sha256'],'content archive differs from report'
    reserved=np.load(a.reservation,allow_pickle=False)['reserved']
    assert reserved.shape==(512,512) and reserved.dtype==np.bool_ and reserved.any()
    original_inventory=mesa_vram.inventory
    original,own,terrain,safe=original_inventory()
    assert not np.any(reserved&~safe),'model reservation outside proven original terrain ownership'
    a.out.mkdir(parents=True)
    def inventory():
        data,usage,area,available=original_inventory()
        return data,usage,area,available&~reserved
    if a.terrain_report:
        generated=json.loads(a.terrain_report.read_text())
        assert generated.get('stone_polish') and generated['uv_material_limit']==a.material_limit
    else:
        mesa_vram.inventory=inventory
        try:
            generated=mesa_ps1.build(SimpleNamespace(work=a.out/'terrain',uv_polish=True,stone_polish=True,uv_material_limit=a.material_limit))
        finally:mesa_vram.inventory=original_inventory
    stone=Path(generated['archive']).read_bytes()
    assert sha(stone)==generated['archive_sha256']
    ch=wad_index(base[:65536],len(base));st=wad_index(stone[:65536],len(stone))
    section=lambda b,c,i:b[c[i].offset:c[i].offset+c[i].size]
    assert near_polygons(base)==near_polygons(stone),'content terrain differs from v2g stone source'
    current=s2scene_build.split(section(base,ch,1));replacement=s2scene_build.split(section(stone,st,1))
    assert current['collision']==replacement['collision'],'content collision changed; review before composition'
    assert not np.any(terrain_mask(stone)&reserved),'terrain descriptor intersects a model reservation'
    # Keep content's sound/sample payload and every nonterrain scene section.
    vram=bytearray(section(base,ch,0));before_vram=np.frombuffer(vram,'<u2',512*512).reshape(512,512).copy()
    pixels=np.frombuffer(stone,'<u2',512*512,st[0].offset).reshape(512,512).copy()
    pixels[reserved]=before_vram[reserved]
    assert not np.any((pixels!=before_vram)&~(safe&~reserved))
    vram[:0x80000]=pixels.astype('<u2').tobytes()
    scene=dict(current);scene['parts']=replacement['parts'][:4]+current['parts'][4:]
    scene_bytes=s2scene_build.join(scene)
    children=[bytes(vram),scene_bytes,section(base,ch,2),section(base,ch,3)]
    output=bytearray(base[:ch[0].offset]);offsets=[]
    for i,payload in enumerate(children):
        output.extend(bytes(aligned(len(output))-len(output)));offsets.append(len(output))
        struct.pack_into('<II',output,8*i,len(output),aligned(len(payload)))
        output.extend(payload);output.extend(bytes(aligned(len(output))-len(output)))
    moved=0
    for at in range(64,0x140,4):
        pointer=struct.unpack_from('<I',base,at)[0]
        if not pointer:break
        assert ch[2].offset<=pointer<=ch[2].offset+ch[2].size,'model directory extent'
        struct.pack_into('<I',output,at,pointer+offsets[2]-ch[2].offset);moved+=1
    outch=wad_index(output[:65536],len(output));outscene=s2scene_build.split(section(output,outch,1))
    assert near_polygons(output)==near_polygons(base)
    assert section(output,outch,2)==children[2] and section(output,outch,3)==children[3]
    assert section(output,outch,0)[0x80000:]==children[0][0x80000:]
    for key in ('words','collision','sky'):assert outscene[key]==current[key],key
    assert outscene['tail'].startswith(current['tail'])
    assert not any(outscene['tail'][len(current['tail']):]),'nonzero extra scene alignment padding'
    assert outscene['parts'][4:]==current['parts'][4:]
    actual=np.frombuffer(output,'<u2',512*512,outch[0].offset).reshape(512,512)
    assert np.array_equal(actual[reserved],before_vram[reserved])
    destination=a.out/'slot17-mesa.bin';destination.write_bytes(output)
    report=dict(content)
    gameplay_keys={'spawn','gem_table_expansion','moneybags_inert','gem_records','container_oracle','container_records',
                   'gem_positions','objects','loose_gem_total','archive','archive_sha256','archive_layout','release_blockers','release_eligible','seconds'}
    report.update({k:v for k,v in generated.items() if k not in gameplay_keys})
    blockers=list(dict.fromkeys(content.get('release_blockers',[])+generated.get('release_blockers',[])+[
        'Combined content/stone candidate requires exact-image static, native and original-engine verification']))
    proof=dict(content_archive=str(path.resolve()),content_sha256=sha(base),
               content_report_sha256=sha(a.content_report.read_bytes()),terrain_archive=generated['archive'],terrain_sha256=sha(stone),
               reservation=str(a.reservation.resolve()),reservation_sha256=sha(a.reservation.read_bytes()),reserved_words=int(reserved.sum()),
               reserved_pixels_sha256=sha(before_vram[reserved].tobytes()),model_directory_entries_relocated=moved,
               extra_scene_zero_padding=len(outscene['tail'])-len(current['tail']),
               models_gameplay_samples_nonterrain_sections_exact=True,
               geometry_collision_positions_winding_exact=True)
    report.update(archive=str(destination.resolve()),archive_sha256=sha(output),release_eligible=False,release_blockers=blockers,
                  stone_composition=proof,archive_layout=dict(offsets=offsets,sizes=[x.size for x in outch],total=len(output)))
    if content.get('container_conversion'):
        cp=Path(content['container_conversion']);conversion=json.loads(cp.read_text())
        if conversion['archive_sha256']!=sha(base):
            # Actor append preserves the existing container records/parameters.
            # Prove that ancestry and those bytes before rebinding its receipt.
            actor=json.loads(Path(content['actor_conversion']).read_text())
            assert actor['archive_sha256']==sha(base)
            ancestor=Path(conversion['archive']).read_bytes()
            assert sha(ancestor)==conversion['archive_sha256']==actor['base_archive_sha256']
            ancestor_ch=wad_index(ancestor[:65536],len(ancestor))
            old_game=section(ancestor,ancestor_ch,3);new_game=section(base,ch,3)
            for entry in conversion['containers']:
                for index in (entry['index'],entry['old_index']):
                    at=0x327c+8+88*index
                    assert old_game[at:at+88]==new_game[at:at+88],('container record changed',index)
                for at,size in ((entry['parameter'],28),(entry['curve'],64)):
                    assert old_game[at:at+size]==new_game[at:at+size],('container data changed',at)
            conversion['actor_ancestry']=dict(archive_sha256=sha(base),actor_conversion=content['actor_conversion'],
                                               preserved_container_records_and_parameters_exact=True)
        conversion.update(parent_content_archive=str(path.resolve()),parent_content_archive_sha256=sha(base),
                          parent_conversion=str(cp.resolve()),parent_conversion_sha256=sha(cp.read_bytes()),
                          archive=str(destination.resolve()),archive_sha256=sha(output),stone_composition=proof)
        target=a.out/'content-conversion.json';target.write_text(json.dumps(conversion,indent=2)+'\n')
        report['container_conversion']=str(target.resolve())
    if content.get('actor_conversion'):
        cp=Path(content['actor_conversion']);conversion=json.loads(cp.read_text())
        assert conversion['archive_sha256']==sha(base)
        overlay=Path(conversion['overlay'])
        assert sha(overlay.read_bytes())==conversion['overlay_sha256']
        assert overlay.stat().st_size%2048==0,'actor overlay must declare complete CD sectors'
        conversion.update(parent_content_archive=str(path.resolve()),parent_content_archive_sha256=sha(base),
                          parent_conversion=str(cp.resolve()),parent_conversion_sha256=sha(cp.read_bytes()),
                          archive=str(destination.resolve()),archive_sha256=sha(output),stone_composition=proof,
                          immutable=False,release_eligible=False)
        target=a.out/'actor-conversion.json';target.write_text(json.dumps(conversion,indent=2)+'\n')
        report['actor_conversion']=str(target.resolve())
    (a.out/'mesa-ps1-report.json').write_text(json.dumps(report,indent=1)+'\n')
    print('CONTENT INPUT',path.resolve(),'sha256',sha(base),flush=True)
    print('MODEL RESERVATION',a.reservation.resolve(),'sha256',proof['reservation_sha256'],'words',proof['reserved_words'],flush=True)
    print('COMPOSED ARCHIVE',destination.resolve(),'sha256',sha(output),flush=True)
    print('STONE/CONTENT COMPOSITION PASS',sha(output),'models/gameplay/SPU payload/nonterrain sections exact; reserved model VRAM exact; terrain geometry/collision/winding exact',flush=True)
    print('LIMIT private candidate; original/native runtime and release gates remain required',flush=True)


if __name__=='__main__':main()
