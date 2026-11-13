"""Source-UV texture baking into original 32x32 terrain cells.

The material limit is real: 7-bit near face indices. Select 128 measured
source mappings, retaining a separate residual for every approximated face.
Mixed 4/8bpp cells and the original D4 descriptor flags conserve VRAM. Neither
the original source geometry nor an executable is changed.
"""
from __future__ import annotations

import hashlib
import struct
from collections import Counter, defaultdict

import numpy as np
from PIL import Image

import mesa_ps1 as mesa
import mesa_vram
import mipsdis

SQUARE = np.array([(0,0),(1,0),(0,1),(1,1)],float)


def corners(face):
    rect,g,strip,rotation,error = mesa.assign(face)
    if face.get('uv_canonical',False):
        # Cyclic vertex orders preserve the polygon's original front winding.
        # Choose one representative of those equivalent UV charts before
        # spending a 7-bit material index. Triangle rotations are the original
        # near-face bits 12..13, not an extra geometry/texture approximation.
        options=[];vs=face['vs'];source=np.asarray(face['uv'],float)
        for k in range(len(vs)):
            indices=[(k+j)%len(vs) for j in range(len(vs))]
            if len(vs)==4:indices=[indices[j] for j in (0,1,3,2)]
            uv=source[indices]
            for r in range(4 if len(vs)==3 else 1):
                cs=SQUARE[:len(vs)].copy()
                for _ in range(r):cs=np.column_stack((1-cs[:,1],cs[:,0]))
                ids=(cs[:,0]+2*cs[:,1]).astype(int)
                t=np.zeros((4,2));t[ids]=uv
                if len(vs)==3:
                    missing=next(i for i in range(4) if i not in ids)
                    if missing in (0,3):t[missing]=t[1]+t[2]-t[3-missing]
                    else:t[missing]=t[0]+t[3]-t[3-missing]
                t=np.round(t,6)
                outside=float(np.maximum(source.min(0)-t,0).sum()+np.maximum(t-source.max(0),0).sum())
                options.append((round(outside,6),tuple(t.flat),k,r,ids,uv,t,[vs[i] for i in indices]))
        _,_,_,r,ids,uv,t,strip=min(options,key=lambda v:v[:4])
        return ids,uv,t,strip,r,rect,g,error
    cs = SQUARE.copy() if len(strip)==4 else SQUARE[:3].copy()
    if len(strip)==3:
        for _ in range(rotation):
            cs = np.column_stack((1-cs[:,1],cs[:,0]))
    ids = (cs[:,0]+2*cs[:,1]).astype(int)
    uv = np.array([face['uv'][face['vs'].index(v)] for v in strip],float)
    template = np.zeros((4,2),float); template[ids] = uv
    if len(ids)==3:
        missing = next(j for j in range(4) if j not in ids)
        transform = np.linalg.solve(np.column_stack((np.ones(3),cs)),uv)
        template[missing] = np.r_[1,SQUARE[missing]]@transform
    return ids,uv,np.round(template,6),strip,rotation,rect,g,error


def flag_map():
    """Read original 80062040 table consumed at 80026564..80026594.

    Table entries are offsets from top-left/top-right packed UV words. Read
    each result back into (u,v); match it to the mathematical square transform.
    The separate oracle check executes these instruction bytes as well.
    """
    base,code = mipsdis.exe_image()
    result = {}
    for flag in range(8):
        a,b,c,d = struct.unpack_from('<4h',code,0x80062040-base+flag*8)
        words = (a,31+b,c,d)
        uv = [((w&255)/31,((w>>8)&255)/31) for w in words]
        g = next(g for g in range(8) if np.allclose([mesa.D4[g](*p) for p in SQUARE],uv))
        result[g] = flag<<4
    assert len(result)==8
    return result


def sample(image,template,n):
    """Piecewise affine mapping on the renderer's 0,1,2 / 1,3,2 diagonal."""
    s,t = np.meshgrid(np.arange(n)/(n-1),np.arange(n)/(n-1))
    low = template[0]+s[...,None]*(template[1]-template[0])+t[...,None]*(template[2]-template[0])
    high = template[3]+(1-t[...,None])*(template[1]-template[3])+(1-s[...,None])*(template[2]-template[3])
    uv = np.clip(np.where((s+t<=1)[...,None],low,high),0,1)
    return image[np.round(uv[...,1]*(image.shape[0]-1)).astype(int),
                 np.round(uv[...,0]*(image.shape[1]-1)).astype(int)]


def canonical(cell, compatible=False):
    # Existing native terrain.cpp reverses reflected rotations 0x50/0x70.
    # Keep the original descriptor meaning; use only the six orientations that
    # both actual consumers decode identically until the native owner fixes it.
    orientations=(0,1,2,3,4,6) if compatible else range(8)
    options = [(mesa.orient(cell,g).tobytes(),g) for g in orientations]
    data,g = min(options)
    inverse = next(k for k in range(8) if all(np.allclose(mesa.D4[k](*mesa.D4[g](*p)),p) for p in SQUARE))
    return data,g,inverse


def choose(faces,limit=128):
    records = []; candidates = defaultdict(Counter); required = set()
    for face in faces:
        ids,uv,t,strip,rot,rect,g,error = corners(face)
        key = mesa.MB.MAT[face['mat']]
        tk = tuple(t.flat)
        candidates[key][tk] += 1
        if face['kind']=='top' or face.get('uv_exact',False):
            required.add((key,tk))
        records.append((key,ids,uv,t,strip,rot,rect,g,error))
    # Every texture family retains a mapping, then the most frequently authored
    # mappings use the remaining entries. Ties are deterministic. An assigned
    # nearest template is always reported as an approximation unless exact.
    selected = required | {(key,max(count,key=lambda x:(count[x],x))) for key,count in candidates.items()}
    assert len(selected)<=limit, 'exact connected floor mappings exceed material budget'
    ranked = sorted(((n*(8 if key=='stone_bridge_ashlar' else 1),str(key),tk,key) for key,count in candidates.items() for tk,n in count.items()),
                    key=lambda x:(-x[0],x[1],x[2]))
    for _,_,tk,key in ranked:
        if len(selected)>=limit:
            break
        selected.add((key,tk))
    selected = sorted(selected,key=str)
    bykey = defaultdict(list)
    for i,(key,tk) in enumerate(selected):
        bykey[key].append(i)
    templates = np.array([tk for key,tk in selected]).reshape(-1,4,2)
    errors = []; exact = 0; old_snap = 0; details = []; top_exact = 0; top_count = 0
    for i,(face,record) in enumerate(zip(faces,records)):
        key,ids,uv,t,strip,rot,rect,g,error = record
        ids_of = bykey[key]
        costs = ((templates[ids_of][:,ids]-uv)**2).sum((1,2))
        best = ids_of[int(costs.argmin())]
        delta = templates[best][ids]-uv
        maxerr = float(np.abs(delta).max())
        exact += maxerr<=1e-6; old_snap += bool(error)
        if face.get('uv_exact',False):
            assert maxerr<=1e-6, 'recognizable wall panel approximation forbidden'
        if face['kind']=='top':
            assert maxerr<=1e-6, 'connected floor approximation forbidden'
            top_count += 1; top_exact += maxerr<=1e-6
        errors.extend(np.linalg.norm(delta,axis=1))
        face.update(key=key,rect=rect,g=g,strip=strip,trot=rot,uverr=error,material=best)
        details.append({'face':i,'material':best,'source_uv':uv.tolist(),'square_corners':ids.tolist(),
                        'max_uv_error':maxerr,'exact':maxerr<=1e-6,'kind':face['kind']})
    used = sorted({f['material'] for f in faces})
    remap = {old:new for new,old in enumerate(used)}
    selected = [selected[i] for i in used]; templates = templates[used]
    for face,detail in zip(faces,details):
        face['material'] = remap[face['material']]; detail['material'] = face['material']
    summary = {'material_limit':limit,'materials':len(selected),'distinct_source_templates':sum(map(len,candidates.values())),
               'source_exact_faces':exact,'source_approximated_faces':len(faces)-exact,
               'source_corner_rms_uv':float(np.sqrt(np.mean(np.square(errors)))),
               'source_corner_max_uv':float(max(errors)),'legacy_snapped_corner_faces':old_snap,
               'exact_top_faces':top_exact,'top_faces':top_count,
               'whole_tile_fallback_faces':0,
               'warning':'Nearest source template remains approximate; zero whole-tile fallback is not exact UV coverage.'}
    return selected,templates,details,summary


def build(faces,arc,sc,rep):
    selected,templates,details,summary = choose(faces,limit=rep.get('uv_material_limit',128))
    original,own,terrain,available = mesa_vram.inventory()
    permitted = available.copy()
    available = available.copy()
    palette_area = own.masks['terrain near palettes'] | own.masks['terrain fade palettes']
    original_vram = np.frombuffer(original,'<u2',512*512,struct.unpack_from('<I',original)[0]).reshape(512,512)
    vram = original_vram.copy()
    flags = flag_map()
    cells = {}; material_cells = []; far_images = {}; flat = {}; near_uses = Counter()
    images = {key:mesa.tile_rgba(key) for key,_ in selected}
    floor_keys = {mesa.MB.MAT[f['mat']] for f in faces if f['kind']=='top'}
    coherent_images = dict(images)
    floor_quantization = {}
    for key in floor_keys:
        palette,idx = mesa.quantize(images[key],16)
        words = palette[idx]
        rgb = np.stack([((words>>(5*k))&31)*255//31 for k in range(3)],-1).astype(np.uint8)
        coherent_images[key] = np.concatenate((rgb,np.where(words[...,None]==0,0,255).astype(np.uint8)),axis=-1)
        floor_quantization[str(key)] = {'colours':int(len(np.unique(words))),
                                       'policy':'one shared colour mapping across all charts and cells'}
    for material,((key,_),template) in enumerate(zip(selected,templates)):
        image = sample(coherent_images[key],template,64)
        reference = sample(images[key],template,64)
        opaque = image[...,3]>=128
        flat[material] = image[opaque,:3].mean(0) if opaque.any() else np.array([128.,128.,128.])
        refs = []
        for q in range(4):
            cell = image[q//2*32:q//2*32+32,q%2*32:q%2*32+32]
            _,g,inverse = canonical(mesa.to15(cell),compatible=bool(rep.get('stone_polish')))
            canon = mesa.orient(cell,g).copy()
            # Canonicalization uses RGB5: source RGB24 variations that quantize
            # to the same original GPU colour must not consume extra cells.
            words = mesa.to15(canon)
            rgb = np.stack([((words>>(5*k))&31)*255//31 for k in range(3)],-1).astype(np.uint8)
            canon = np.concatenate((rgb,np.where(words[...,None]==0,0,255).astype(np.uint8)),axis=-1)
            ck = words.tobytes()
            reference_cell = mesa.to15(mesa.orient(reference[q//2*32:q//2*32+32,q%2*32:q%2*32+32],g))
            cells.setdefault(ck,{'image':canon,'refs':[],'references':[]})['refs'].append((material,q))
            cells[ck]['references'].append(reference_cell)
            refs.append((ck,flags[inverse]))
        material_cells.append(refs)
    for face in faces:
        for ck,flag in material_cells[face['material']]:
            near_uses[ck] += 1

    def allocate(w,h,xstep=8,ystep=1,palette=False):
        # One-row palettes prefer the partial 32-row band at y32..61: it cannot
        # hold texture cells because y62..63 belong to other original users.
        # Keep palettes in original palette footprints before taking pixel cells.
        rows=list(range(0,512-h+1,ystep))
        if palette and h==1:rows.sort(key=lambda y:(not 32<=y<62,y))
        for y in rows:
            if y//256 != (y+h-1)//256:
                continue
            for x in range(0,512-w+1,xstep):
                if (available[y:y+h,x:x+w].all() and
                        (not palette or palette_area[y:y+h,x:x+w].all())):
                    available[y:y+h,x:x+w] = False
                    return x+512,y
        raise ValueError(f'no proven terrain-owned {w}x{h} allocation remains')

    def put(x,y,words):
        h,w = words.shape
        assert permitted[y:y+h,x-512:x-512+w].all()
        vram[y:y+h,x-512:x-512+w] = words

    def clut_at(x,y):
        return (y<<6)|(x//16)

    def descriptor(x,y,clut,depth,flag=0):
        ppw = 2 if depth else 4
        px = (x//64)*64; u = (x-px)*ppw; v = y%256
        assert u+31<=255 and v+31<=255
        page = px//64+((y//256)<<4)+(depth<<7)
        return bytes((u,v))+struct.pack('<H',clut)+bytes((u+31,v+31,page,flag))

    def pack(idx,depth):
        if depth:
            return (idx[:,::2]|(idx[:,1::2]<<8)).astype('<u2')
        return (idx[:,::4]|(idx[:,1::4]<<4)|(idx[:,2::4]<<8)|(idx[:,3::4]<<12)).astype('<u2')

    far_palettes = {}
    for key in sorted(images,key=str):
        palette,idx = mesa.quantize(images[key],16)
        x,y = allocate(16,16,16,palette=True)
        put(x,y,mesa.far_clut_fade(palette))
        far_palettes[key] = (idx,clut_at(x,y))
    palettes = {}
    for ck,cell in cells.items():
        for depth,n in ((0,16),(1,256)):
            pal,idx = mesa.quantize(cell['image'],n)
            reconstructed = pal[idx]
            expected = np.stack(cell['references'])
            err = np.stack([((reconstructed[None]>>(k*5))&31).astype(float)-((expected>>(k*5))&31) for k in range(3)],-1)
            cell[depth] = {'palette':pal,'idx':idx,'error_rgb5_squared':float(np.square(err).mean())}
        pk = cell[0]['palette'].tobytes()
        if pk not in palettes:
            x,y = allocate(16,1,16,palette=True)
            put(x,y,cell[0]['palette'][None,:]); palettes[pk] = clut_at(x,y)
        cell[0]['clut'] = palettes[pk]
    # Texture cells are 32 rows high in both depth modes. Allocate minimum
    # footprint first, then optional higher-colour cells in the remaining space.
    for ck,cell in cells.items():
        x,y = allocate(8,32,8,32)
        put(x,y,pack(cell[0]['idx'],0))
        cell['descriptor'] = descriptor(x,y,cell[0]['clut'],0)
        cell['depth'] = 0
    far_refs = []
    for (key,_),template in zip(selected,templates):
        idx,clut = far_palettes[key]
        small = sample(idx,template,32)
        fk = (clut,small.tobytes())
        if fk not in far_images:
            x,y = allocate(8,32,8,32)
            put(x,y,pack(small,0)); far_images[fk] = descriptor(x,y,clut,0)
        far_refs.append(far_images[fk])
    ranked = sorted(cells,key=lambda ck:-(cells[ck][0]['error_rgb5_squared']-cells[ck][1]['error_rgb5_squared'])*near_uses[ck])
    upgrades = 0
    for ck in ranked:
        cell = cells[ck]
        if cell[0]['error_rgb5_squared']<=cell[1]['error_rgb5_squared']:
            continue
        # Failed trials must not consume either allocation.
        saved = available.copy()
        try:
            x,y = allocate(16,32,16,32)
            px,py = allocate(256,1,256,palette=True)
        except ValueError:
            available[:] = saved
            continue
        put(px,py,cell[1]['palette'][None,:]); put(x,y,pack(cell[1]['idx'],1))
        cell['descriptor'] = descriptor(x,y,clut_at(px,py),1)
        cell['depth'] = 1; upgrades += 1
    part = bytearray()
    for far,refs in zip(far_refs,material_cells):
        part += far*2
        for ck,flag in refs:
            d = bytearray(cells[ck]['descriptor']); d[7] = flag; part += d
    assert not np.any((vram!=original_vram)&~permitted)
    start = struct.unpack_from('<I',arc)[0]
    arc[start:start+0x80000] = vram.astype('<u2').tobytes()
    weights = np.array([near_uses[ck] for ck in cells])
    errors = np.array([c[c['depth']]['error_rgb5_squared'] for c in cells.values()])
    summary.update(unique_near_cells=len(cells),unique_far_cells=len(far_images),
                   native_compatible_orientations=bool(rep.get('stone_polish')),
                   near_8bpp_cells=upgrades,near_4bpp_cells=len(cells)-upgrades,
                   weighted_colour_rms_rgb5=float(np.sqrt(np.average(errors,weights=weights))),
                   terrain_owned_words=int(terrain.sum()),permitted_words=int(permitted.sum()),
                   allocated_words=int((permitted&~available).sum()),
                   writes_outside_original_terrain_words=0,
                   coherent_floor_palettes=floor_quantization,
                   ownership_limit='Unclassified consumers reserved; full symbolic ownership audit incomplete.',
                   source_textures={str(key):hashlib.sha256(image.tobytes()).hexdigest() for key,image in images.items()},
                   mappings=[{'material':i,'key':key,'uv_corners':templates[i].tolist()} for i,(key,_) in enumerate(selected)],
                   faces=details)
    rep.update(materials=len(selected),texture_slots=len(images),faces_uv_snapped=summary['source_approximated_faces'],
               part_rect_faces=0,exact_part_materials={'count':len(selected),'faces':summary['source_exact_faces'],
                                                    'whole_fallback_faces':0,'own_far_cells':len(far_images)},
               vram_cells_used={'near':len(cells),'far':len(far_images)},uv_polish=summary)
    return {(key,tuple(tk)):i for i,(key,tk) in enumerate(selected)},flat,struct.pack('<II',8+len(selected)*48,len(selected))+part
