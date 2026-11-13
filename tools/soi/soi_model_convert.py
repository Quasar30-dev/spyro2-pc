"""Convert retained local YotD animated banks into a separate S2 asset archive.

All numeric clips, compressed streams, palettes, collision and LOD data stay
authored. Only face UV/TPAGE/CLUT and inline sound IDs change. No actor is
silently assigned a donor's behavior. The output remains a non-release asset
candidate until placement, story, AI and original-engine evidence are complete.
"""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import struct

import numpy as np

import mesa_vram
import s2scene_build
from s2scene import read_archive
from spyrodisc import wad_index

ROOT = Path(__file__).resolve().parents[2]


def sha(b): return hashlib.sha256(b).hexdigest()
def u32(b, o): return struct.unpack_from('<I', b, o)[0]
def put32(b, o, v): struct.pack_into('<I', b, o, v)
def align(n, a=2048): return (n+a-1)//a*a


def faces(bank):
    """Unique near/far packets, rejecting unsupported frame patch banks."""
    common = u32(bank, 56)
    streams = set()
    for clip in range(u32(bank, 0)):
        d = u32(bank, 60+4*clip)
        assert d+36+8*bank[d] <= len(bank)
        for frame in range(bank[d]):
            assert u32(bank,d+36+8*frame)&0x80000000, 'patched donor needs explicit variant conversion'
        for field in (16, 24):
            if field == 24 and not bank[d+5]: continue
            start = common+u32(bank,d+field)
            if start in streams: continue
            streams.add(start)
            p, end = start+4, start+4+u32(bank,start)
            assert end <= len(bank)
            while p < end:
                assert p+8 <= end
                textured = bool(u32(bank,p+4)&0x80000000)
                n = 20 if textured else 8
                assert p+n <= end
                yield p, textured
                p += n
            assert p == end


def sound_table(arc, yotd):
    ch = wad_index(arc[:65536],len(arc))
    scene = arc[ch[1].offset:ch[1].offset+ch[1].size]
    at = 0
    for _ in range(4 if yotd else 6): at += u32(scene,at)
    at += 12
    for _ in range(6 if yotd else 5): at += u32(scene,at)
    start = at+32
    count, size = u32(scene,start+260),u32(scene,start)
    assert size == 264+20*count and count <= 255
    descriptors = [scene[start+264+i*20:start+284+i*20] for i in range(count)]
    sample_start = ch[0].offset+(0x80000 if yotd else 0x86000)
    samples = arc[sample_start:ch[0].offset+ch[0].size]
    return ch, scene, start, descriptors, samples


def sample_bytes(samples, offset):
    assert offset%16 == 0 and offset+16 <= len(samples)
    for at in range(offset,len(samples),16):
        assert samples[at]>>4 <= 4, 'SPU predictor'
        if samples[at+1]&1: return samples[offset:at+16]
    raise AssertionError('unterminated SPU sample')


def terrain_mask(arc):
    ch = wad_index(arc[:65536],len(arc))
    scene = s2scene_build.split(arc[ch[1].offset:ch[1].offset+ch[1].size])
    own = mesa_vram.Ownership()
    for m in range(u32(scene['parts'][0],4)):
        for k in range(6):
            d = scene['parts'][0][8+m*48+k*8:16+m*48+k*8]
            clut = struct.unpack_from('<H',d,2)[0]
            own.texture('used',d[6],clut,[(d[0],d[1]),(d[4],d[5])],False)
            own.palette('used',clut,256 if d[6]&128 else 16,1 if k>=2 else 16)
    return own.masks['used']


class Pack:
    def __init__(self, available, pixels, reserve_first=False, palette_area=None):
        self.available, self.pixels = available.copy(), pixels
        self.records = []
        self.palettes = {}
        self.reserve_first,self.palette_area=reserve_first,palette_area

    def alloc(self, w, h, alignment=1, ppw=1):
        # The original GPU requires one texture page, with packed X aligned to
        # its pixels/word and CLUT X aligned to 16 halfwords.
        aw,ah=w,h
        mask=self.available
        if self.reserve_first:
            if ppw>1: aw,ah=align(w,8),align(h,32)
            else: mask=mask&self.palette_area
        summed=np.pad((~mask).astype(np.int32),((1,0),(1,0))).cumsum(0).cumsum(1)
        counts=summed[ah:,aw:]-summed[:-ah,aw:]-summed[ah:,:-aw]+summed[:-ah,:-aw]
        ys,xs=np.nonzero(counts==0)
        for y,x in zip(ys,xs):
            if x%alignment or y//256 != (y+h-1)//256: continue
            if self.reserve_first and ppw>1 and (x%8 or y%32): continue
            if ppw > 1 and x//64 != (x+w-1)//64: continue
            self.available[y:y+ah,x:x+aw] = False
            return int(x),int(y)
        raise ValueError(f'No conservative VRAM rectangle {w}x{h}; retain existing archive')


def convert_materials(bank, source, pack, identity):
    groups = defaultdict(list)
    for p,textured in faces(bank):
        if not textured: continue
        page,clut = struct.unpack_from('<H',bank,p+14)[0],struct.unpack_from('<H',bank,p+10)[0]
        # Whole-page material groups contain widely separated eyes/body UVs.
        # Keep exact packet rectangles so unused gaps do not force a huge box.
        uv=np.array([(bank[p+k],bank[p+k+1]) for k in (8,12,16,18)])
        lo,hi=uv.min(0),uv.max(0)
        key=(page,clut,(),()) if pack.reserve_first else (page,clut,tuple(lo),tuple(hi))
        groups[key].append(p)
    # Place large rectangles first. Preserve full bounding rectangles, including
    # transparent/STP texels, so oblique/interior samples retain original data.
    regions = []
    for (page,clut,_,_),packets in groups.items():
        depth = (page>>7)&3; assert depth <= 1
        ppw = 2 if depth else 4
        uv = np.array([(bank[p+k],bank[p+k+1]) for p in packets for k in (8,12,16,18)])
        lo,hi = uv.min(0),uv.max(0)
        x0,x1 = int(lo[0])//ppw,int(hi[0])//ppw
        y0,y1 = int(lo[1]),int(hi[1])
        regions.append((-(x1-x0+1)*(y1-y0+1),page,clut,packets,ppw,x0,x1,y0,y1))
    modified = bytearray(bank); checks = 0
    for _,page,clut,packets,ppw,x0,x1,y0,y1 in sorted(regions):
        w,h = x1-x0+1,y1-y0+1
        x,y = pack.alloc(w,h,ppw=ppw)
        sx,sy = (page&15)*64-512+x0,((page>>4)&1)*256+y0
        assert 0 <= sx < sx+w <= 512 and 0 <= sy < sy+h <= 512
        pack.pixels[y:y+h,x:x+w] = source[sy:sy+h,sx:sx+w]
        n = 256 if ppw==2 else 16
        scx,scy = (clut&63)*16-512,(clut>>6)&511
        assert 0 <= scx < scx+n <= 512
        palette=source[scy,scx:scx+n]
        palette_key=palette.tobytes()
        if palette_key not in pack.palettes:
            cx,cy=pack.alloc(n,1,16)
            pack.pixels[cy,cx:cx+n]=palette
            pack.palettes[palette_key]=(cx,cy)
        cx,cy=pack.palettes[palette_key]
        newpage = (page&~31)|((x+512)//64)|((y//256)<<4)
        newclut = ((cx+512)//16)|(cy<<6)
        du,dv = ((x+512)%64-x0)*ppw,y%256-y0
        for p in packets:
            struct.pack_into('<H',modified,p+10,newclut)
            struct.pack_into('<H',modified,p+14,newpage)
            for k in (8,12,16,18):
                u,v = bank[p+k],bank[p+k+1]
                assert 0 <= u+du <= 255 and 0 <= v+dv <= 255
                modified[p+k],modified[p+k+1] = u+du,v+dv
            checks += 1
        # Exact packed pixels + palette guarantees every texel (not just UV
        # corners) has identical RGB5, zero transparency and STP semantics.
        assert np.array_equal(pack.pixels[y:y+h,x:x+w],source[sy:sy+h,sx:sx+w])
        assert np.array_equal(pack.pixels[cy,cx:cx+n],source[scy,scx:scx+n])
        pack.records.append(dict(model=identity,source_page=page,source_clut=clut,
             source_rect=[sx+512,sy,w,h],target_rect=[x+512,y,w,h],
             source_palette=[scx+512,scy,n,1],target_palette=[cx+512,cy,n,1],
             target_page=newpage,target_clut=newclut,uv_delta=[du,dv],packets=len(packets)))
    return modified, checks


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--archive',type=Path,required=True)
    ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--reserve-before-terrain',action='store_true',help='repack UV terrain around exact donor texture reservations using existing builder')
    a = ap.parse_args(); a.out=a.out.resolve()
    assert not a.out.exists(), 'retain earlier conversion artifacts'
    base = a.archive.read_bytes(); input_digest=sha(base)
    _,original_owners,_,safe = mesa_vram.inventory()
    available = safe & ~terrain_mask(base)
    if a.reserve_before_terrain: available=safe.copy()
    ch,scene,sound_at,descriptors,samples = sound_table(base,False)
    pixels = np.frombuffer(base,'<u2',512*512,ch[0].offset).reshape(512,512).copy()
    original_pixels = pixels.copy()
    palette_area=original_owners.masks['terrain near palettes']|original_owners.masks['terrain fade palettes']
    pack = Pack(available,pixels,a.reserve_before_terrain,palette_area)
    sources = json.loads((ROOT/'data/soi/content-20261105/yotd-model-census.json').read_text())
    donor_wad = Path(sources['source_wad']); donor_exe = Path(sources['source_exe'])
    assert hashlib.file_digest(donor_wad.open('rb'),'sha256').hexdigest() == sources['source_wad_sha256']
    assert sha(donor_exe.read_bytes()) == sources['source_exe_sha256']
    selected = [(97,0x1a9,0x2e0,'Bianca'),(141,0x19a,0x2e1,'armoured spear Rhynoc'),
                (123,5,0x2e2,'green-robed magic Rhynoc; source fan geometry differs')]
    newbanks = []; sound_proof=[]; sample_payload=bytearray(samples)
    # Retain every original sample. Identical compressed waveform + descriptor
    # metadata is reused; native sample IDs are never guessed by event number.
    existing = {}
    for i,d in enumerate(descriptors): existing[(sample_bytes(samples,u32(d,0)),d[4:])] = i
    all_new = []
    for slot,cls,target,label in selected:
        arc=read_archive(donor_wad.parent,slot)
        row=next(m for m in sources['models'] if m['slot']==slot and m['class']==cls)
        original=arc[row['bank_begin']:row['bank_begin']+row['bytes']]
        assert sha(original)==row['bank_sha256']
        source_pixels=np.frombuffer(arc,'<u2',512*512,u32(arc,0)).reshape(512,512)
        # Texture allocation is done after sorting all donor materials below;
        # independent bank identities remain attached to every source region.
        all_new.append((slot,cls,target,label,arc,row,original,source_pixels))
    # Biggest donor first limits fragmentation without changing material scale.
    all_new.sort(key=lambda x:-len(x[6]))
    for slot,cls,target,label,arc,row,original,source_pixels in all_new:
        bank,checks=convert_materials(original,source_pixels,pack,label)
        _,_,_,source_descriptors,source_samples=sound_table(arc,True)
        mapping={}
        for at in range(4,16):
            old=original[at]
            if old==255: continue
            assert old<len(source_descriptors)
            descriptor=source_descriptors[old]; wave=sample_bytes(source_samples,u32(descriptor,0))
            key=(wave,descriptor[4:])
            if key not in existing:
                assert len(descriptors)<255
                new=bytearray(descriptor); put32(new,0,len(sample_payload))
                descriptors.append(bytes(new));existing[key]=len(descriptors)-1
                sample_payload.extend(wave)
            new_id=existing[key];mapping[old]=new_id;bank[at]=new_id
            sound_proof.append(dict(slot=slot,source_class=cls,target_class=target,
                 source_id=old,target_id=new_id,adpcm_sha256=sha(wave),bytes=len(wave),
                 descriptor_metadata_sha256=sha(descriptor[4:])))
        # Only declared material-reference bytes and sound IDs may differ.
        allowed=set(range(4,16))
        for p,textured in faces(original):
            if textured: allowed.update(range(p+8,p+20))
        changed=[i for i,(x,y) in enumerate(zip(original,bank)) if x!=y]
        assert set(changed)<=allowed and len(original)==len(bank)
        newbanks.append((target,bytes(bank)))
        all_new_row=dict(slot=slot,source_class=cls,target_class=target,label=label,
             source_bank_sha256=sha(original),bank_sha256=sha(bank),bytes=len(bank),
             source_clips=u32(bank,0),material_packets=checks,sound_map=mapping,
             changed_bytes=len(changed),original_bank=original)
        row.update(conversion=all_new_row)
    assert len(sample_payload) <= 0x7e000, 'SPU address space budget (before runtime loader proof)'
    assert not np.any((pixels!=original_pixels)&~available), 'VRAM outside proven unused terrain words'
    reserved=available&~pack.available
    if a.reserve_before_terrain:
        import mesa_ps1
        from types import SimpleNamespace
        a.out.mkdir(parents=True)
        np.savez_compressed(a.out/'model-vram-reservation.npz',reserved=reserved)
        original_inventory=mesa_vram.inventory
        def with_reservation():
            arc,own,terrain,permitted=original_inventory()
            return arc,own,terrain,permitted&~reserved
        mesa_vram.inventory=with_reservation
        try:
            terrain_report=mesa_ps1.build(SimpleNamespace(work=a.out/'terrain-reserved',uv_polish=True,stone_polish=False))
        finally:
            mesa_vram.inventory=original_inventory
        terrain_base=Path(terrain_report['archive']).read_bytes()
        nch=wad_index(terrain_base[:65536],len(terrain_base))
        assert terrain_base[nch[3].offset:nch[3].offset+nch[3].size]==base[ch[3].offset:ch[3].offset+ch[3].size], 'gameplay changed during reserved build'
        old=s2scene_build.split(base[ch[1].offset:ch[1].offset+ch[1].size])
        new=s2scene_build.split(terrain_base[nch[1].offset:nch[1].offset+nch[1].size])
        assert old['parts'][1:]==new['parts'][1:] and old['collision']==new['collision'] and old['words']==new['words'] and old['sky']==new['sky']
        terrain_pixels=np.frombuffer(terrain_base,'<u2',512*512,nch[0].offset).reshape(512,512).copy()
        assert not np.any(terrain_mask(terrain_base)&reserved)
        terrain_pixels[reserved]=pixels[reserved];pixels=terrain_pixels
        # Sound tables and samples in the terrain rebuild must remain identical.
        _,nscene,nsound,ndescriptors,nsamples=sound_table(terrain_base,False)
        assert ndescriptors==sound_table(base,False)[3] and nsamples==samples
        base=terrain_base;ch=nch;scene=nscene;sound_at=nsound
    # Rebuild the archive with original level/Spyro banks retained, and three
    # distinct new classes. No loader descriptor aliases or internal sentinels.
    model=bytearray(base[ch[2].offset:ch[2].offset+ch[2].size])
    indices=[]
    for i in range(1,63):
        if not u32(base,64+(i+1)*4): break
        indices.append(i)
    last=len(indices)+1
    assert last+len(newbanks)<63
    header=bytearray(base[:ch[0].offset])
    for target,bank in sorted(newbanks):
        while len(model)%4: model.append(0)
        put32(header,64+last*4,len(model))  # relative until final child offsets known
        struct.pack_into('<H',header,0x140+last*2,target)
        model.extend(bank);last+=1
    put32(header,64+last*4,len(model));put32(header,64+(last+1)*4,0)
    original_sample_count=u32(scene,sound_at+260)
    sound_chunk=bytearray(scene[sound_at:sound_at+264])
    put32(sound_chunk,0,264+20*len(descriptors));put32(sound_chunk,260,len(descriptors))
    sound_chunk.extend(b''.join(descriptors))
    scene=scene[:sound_at]+sound_chunk+scene[sound_at+264+20*original_sample_count:]
    # Drop only known zero alignment padding introduced by the larger descriptor
    # table if available; preserve all original nonzero bytes and section order.
    vram_child=bytearray(base[ch[0].offset:ch[0].offset+0x86000]);vram_child[:0x80000]=pixels.tobytes()
    vram_child.extend(sample_payload)
    children=[bytes(vram_child),bytes(scene),bytes(model),base[ch[3].offset:ch[3].offset+ch[3].size]]
    output=bytearray(header);offsets=[]
    for i,payload in enumerate(children):
        output.extend(bytes(align(len(output))-len(output)));offsets.append(len(output))
        put32(output,i*8,len(output));put32(output,i*8+4,align(len(payload)))
        output.extend(payload);output.extend(bytes(align(len(output))-len(output)))
    for i in range(len(indices)+1): put32(output,64+4*i,u32(base,64+4*i)+offsets[2]-ch[2].offset)
    for i in range(len(indices)+1,last+1): put32(output,64+4*i,u32(header,64+4*i)+offsets[2])
    # Table + material data are the only archive changes besides audio/model
    # children and their offsets. Geometry, UV descriptors and gameplay exact.
    outch=wad_index(output[:65536],len(output))
    newscene=s2scene_build.split(output[outch[1].offset:outch[1].offset+outch[1].size])
    oldscene=s2scene_build.split(base[ch[1].offset:ch[1].offset+ch[1].size])
    for key in ('parts','words','collision','sky'): assert newscene[key]==oldscene[key], key
    assert output[outch[3].offset:outch[3].offset+ch[3].size]==children[3]
    a.out.mkdir(parents=True,exist_ok=True)
    models=[]
    for slot,cls,target,label,arc,row,original,source_pixels in all_new:
        details=dict(row['conversion']);details.pop('original_bank')
        (a.out/f'{target:03x}-source.bank').write_bytes(original)
        bank=next(b for t,b in newbanks if t==target)
        (a.out/f'{target:03x}-s2.bank').write_bytes(bank);models.append(details)
    (a.out/'slot17-model-assets.bin').write_bytes(output)
    report=dict(base_archive=str(a.archive.resolve()),base_archive_sha256=input_digest,
         terrain_base_sha256=sha(base),terrain_repacked=a.reserve_before_terrain,
         archive=str(a.out/'slot17-model-assets.bin'),archive_sha256=sha(output),release_eligible=False,
         source_wad_sha256=sources['source_wad_sha256'],source_exe_sha256=sources['source_exe_sha256'],
         models=models,materials=pack.records,sounds=sound_proof,
         vram_words=int((pixels!=original_pixels).sum()),vram_allocated_words=int((available&~pack.available).sum()),
         available_before=int(available.sum()),available_after=int(pack.available.sum()),
         original_sample_count=original_sample_count,sample_count=len(descriptors),spu_payload_bytes=len(sample_payload),
         unchanged=['terrain geometry and authored source UV mapping','collision','sky','gameplay including all placements'],
         limits=['Asset archive only. No new actors or behavior installed.',
                 'Bianca story placement and dialogue are not inferred.',
                 'Green-robed donor has a staff; SoI reference has a fan.',
                 'VRAM allocation must be reserved when parent repacks stone.',
                 'Original loader/renderer/clip/audio playback proof required.'])
    path=a.out/'model-conversion.json';path.write_text(json.dumps(report,indent=2)+'\n')
    print('BASE ARCHIVE',a.archive.resolve(),'sha256',input_digest)
    print('TERRAIN BASE sha256',sha(base),'repacked',a.reserve_before_terrain)
    print('CONVERTED ARCHIVE',report['archive'],'sha256',sha(output))
    print('CONVERSION REPORT',path,'sha256',sha(path.read_bytes()))
    print('EXACT MATERIAL RELOCATION',sum(x['material_packets'] for x in models),'near/far textured packets;',len(pack.records),'pixel rectangles and CLUTs; RGB5/transparent/STP data exact')
    print('EXACT SOUND RELOCATION',len(sound_proof),'inline references;',original_sample_count,'->',len(descriptors),'samples; compressed waveform and descriptor metadata exact')
    print('UNCHANGED TERRAIN/GAMEPLAY',sha(output),'all geometry, collision, sky and authored placements byte-exact base; terrain descriptors repacked when requested')
    print('LIMIT',json.dumps(report['limits']))


if __name__=='__main__': main()
