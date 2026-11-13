"""Stone material/UV polish, preserving every v2g position and polygon.

Uses a clean crop of original Summer Forest masonry instead of repeating the
arch/gold-trim tile on narrow bridge rails. Floors share a world-aligned
mirrored flagstone field; physical kerbs and rails retain the edge treatment.
"""
import hashlib
import json
import math
from collections import Counter
from pathlib import Path

import numpy as np
from PIL import Image


ROOT=Path(__file__).resolve().parents[2]
TEXTURES=ROOT/'data/soi/pc/textures'
NAME='stone_bridge_ashlar'


def prepare_texture():
    import mesa_ps1
    source=mesa_ps1.tile_rgba((15,53))
    # Original tile 52 includes a gold arch quadrant. Tile 53's middle is the
    # matching plain masonry. Exclude both architectural trim bands.
    crop=source[8:44,:48].copy()
    rgb=crop[...,:3].astype(float)
    assert not np.any((rgb[...,0]>1.1*rgb[...,2])&(rgb[...,1]>1.1*rgb[...,2])), 'architectural gold trim escaped crop'
    patch=np.asarray(Image.fromarray(crop).resize((64,64),Image.Resampling.NEAREST))
    atlas=np.concatenate([np.concatenate([patch[::(-1 if y%2 else 1),::(-1 if x%2 else 1)]
                                        for x in range(6)],axis=1) for y in range(6)],axis=0)
    atlas=np.concatenate((atlas,atlas[:1]),axis=0)
    atlas=np.concatenate((atlas,atlas[:,:1]),axis=1)
    image=Image.fromarray(atlas)
    raw=np.asarray(image).tobytes();rgba=TEXTURES/(NAME+'.rgba');png=TEXTURES/(NAME+'.png')
    if not rgba.exists() or rgba.read_bytes()!=raw:
        rgba.write_bytes(raw);image.save(png)
    return {'source_scene':15,'source_material':53,'source_rgba_sha256':hashlib.sha256(source.tobytes()).hexdigest(),
            'crop_pixels':[0,8,48,44],'repeats':[6,6],'mirrored_repeat':True,'size':[385,385],
            'rgba_sha256':hashlib.sha256(raw).hexdigest(),
            'physical_repeat_horizontal_soi_units':16,'physical_repeat_vertical_horizontal_units':12,
            'method':'Original pixels only; nearest resampling; no generated/painted replacement art.'}


def wave(value,origin=0,period=16):
    return 1-abs(((value-origin)%(2*period))/period-1)


def apply(model):
    import mesa_build as mb
    mb.MAT['bridge_ashlar']=NAME
    texture=prepare_texture();m=model['M'];before=np.asarray(model['P'],dtype='<f8').tobytes()
    polygon_before=[tuple(f['vs']) for f in m.faces];flags_before=[f.get('flags',3) for f in m.faces]
    changed=Counter();details=[];ox,oy=model['L']['ox'],model['L']['oy']
    groups={};group_keys={};wall_groups={};wall_panels={}
    for i,f in enumerate(m.faces):
        if f['kind']!='wall':continue
        key=(json.dumps(f['frame']),tuple(f['n']),f.get('flags',3))
        wall_groups.setdefault(key,[]).append(i)
    mixed_panels=0;clipped_feature_panels=0
    for indices in wall_groups.values():
        faces=[m.faces[i] for i in indices]
        materials={f['matname'] for f in faces}
        mixed_panels+=len(materials)>1
        points=np.array([m.pos[v] for f in faces for v in f['vs']],float)
        org,ua,va,*_=faces[0]['frame']
        chart=np.column_stack(((points-org)@ua,(points-org)@va))
        base=chart.min(0);extent=np.ptp(chart,axis=0)
        # The old per-triangle first-vertex hash can put half a window beside
        # plain plaster. Choose once per original panel. Narrow/clipped strips
        # carry plaster rather than a cropped fragment of a window or door.
        material=('door' if 'door' in materials else 'window' if 'window' in materials else 'wall')
        if extent[0]<12 or extent[1]<24:material='wall'
        if material!='wall':
            import mesa_ps1
            subgroups={}
            for f in faces:subgroups.setdefault((f['matname'],f.get('rot',0)),[]).append(f)
            regular=True
            for subset in subgroups.values():
                verts=sorted({v for f in subset for v in f['vs']})
                corners=mesa_ps1.polygon_corners(np.asarray([m.pos[v] for v in verts],float),np.asarray(faces[0]['n'],float)) if len(verts)>3 else list(range(len(verts)))
                outlines=[[verts[k] for k in corners]] if 3<=len(corners)<=4 else [f['vs'] for f in subset]
                for outline in outlines:
                    ps=np.asarray([m.pos[v] for v in outline],float)-org
                    uv=(np.column_stack((ps@ua,ps@va))-base)/extent
                    regular &= bool(np.all(np.isclose(uv,0,atol=1e-7)|np.isclose(uv,1,atol=1e-7)))
            if not regular:
                # Keep recognizable art on panels whose existing serialized
                # polygons use the rectangle corners. Extra fanned corners
                # require scarce additional charts; use plaster on those
                # panels to retain room for the bridges and connected paving.
                material='wall';clipped_feature_panels+=1
        for i in indices:
            wall_panels[i]=(material,(org,ua,va,base.tolist(),extent.tolist()))
    for i,f in enumerate(m.faces):
        if f['matname'] not in ('bridge_side','bridge_rail'):continue
        key=(json.dumps(f['frame']),f['kind'],f['matname'],f.get('rot',0),tuple(f['n']),f.get('flags',3),
             i if 'uvs' in f or f['kind'] in ('decor','under') else -1)
        group_keys[i]=key
        world=np.array([m.pos[v] for v in f['vs']],float)*[1,1,mb.V/mb.H]
        groups.setdefault(key,[]).extend(world)
    for index,f in enumerate(m.faces):
        material=f['matname'];points=np.array([m.pos[v] for v in f['vs']],float)
        old=[list(f['uvs'][i]) if 'uvs' in f else list(mb.uv_of(f,p)) for i,p in enumerate(points)]
        if f['kind']=='top' and material in ('plaza','plaza_var','plaza_edge','bridge','bridge_edge'):
            # Consistent texture phase, including 8-unit partial cells. The
            # mirrored field has identical source texels on every tile join.
            uv=[(wave(p[0],ox),wave(p[1],oy)) for p in points]
            f['stone_group_material']=material;f['stone_group_rotation']=f.get('rot',0)
            f['matname']='bridge' if material.startswith('bridge') else 'plaza'
            f['stone_uv_kind']='floor';f['stone_floor_origin']=(ox,oy);f['rot']=0
        elif material in ('bridge_side','bridge_rail'):
            normal=np.abs(np.asarray(f['n']))
            world=points*np.array([1,1,mb.V/mb.H])
            if normal[2]>.7:
                axes=(0,1)
            elif normal[1]>=normal[0]:axes=(0,2)
            else:axes=(1,2)
            chart=world[:,axes]
            # The repeated source atlas permits the unchanged long/mitered
            # quads to retain the same stone scale without UV clamping or cuts.
            bounds=np.asarray(groups[group_keys[index]])
            base=bounds[:,axes].min(0)
            uv=(chart-base)/[96,72]
            assert uv.min()>=-1e-7 and uv.max()<=1+1e-7,(index,chart.tolist(),uv.tolist())
            f['stone_group_material']=material;f['stone_group_rotation']=f.get('rot',0)
            f['matname']='bridge_ashlar';f['stone_uv_kind']='masonry';f['stone_chart']=(axes,base.tolist(),[96,72]);f['rot']=0
            f.pop('uvs',None)
        elif index in wall_panels:
            chosen,chart=wall_panels[index]
            f['stone_group_material']=material;f['stone_group_rotation']=f.get('rot',0)
            f['matname']=chosen;f['rot']=0
            if chosen in ('window','door'):
                f['stone_uv_kind']='panel';f['stone_panel']=chart
                f.pop('uvs',None)
            uv=[mb.uv_of(f,p) for p in points]
        elif material in ('cliff','cliff2'):
            # Existing square-scale cliff frames already form the stone rows;
            # remove per-face quarter turns that scramble their joints.
            f['stone_group_material']=material;f['stone_group_rotation']=f.get('rot',0);f['rot']=0
            uv=[mb.uv_of(f,p) for p in points]
        else:continue
        changed[material]+=1
        details.append({'face':index,'previous_material':material,'material':f['matname'],
                        'old_uv':old,'uv':[list(mb.uv_of(f,p)) for p in points]})
    assert np.asarray(model['P'],dtype='<f8').tobytes()==before
    assert [tuple(f['vs']) for f in m.faces]==polygon_before and [f.get('flags',3) for f in m.faces]==flags_before
    proof={'version':1,'geometry_positions_sha256':hashlib.sha256(before).hexdigest(),
           'polygon_indices_sha256':hashlib.sha256(json.dumps(polygon_before,separators=(',',':')).encode()).hexdigest(),
           'polygons':len(m.faces),'changed_by_previous_material':dict(changed),'texture':texture,
           'layout_heights_polygon_indices_flags_unchanged':True,'uv_faces_changed':len(details),
           'floor_policy':'One world-aligned mirrored original flagstone field; physical kerbs/rails provide the borders.',
           'bridge_policy':'Clean original masonry crop with physical-scale charts; original ornamental arch tile removed from rails.',
           'wall_panel_policy':'One material per original panel; complete upright windows/doors on wide/tall panels representable with existing rectangle-corner polygons; narrow or complex split panels are plaster.',
           'wall_panels':len(wall_groups),'mixed_wall_panels_repaired':mixed_panels,
           'irregular_feature_panels_using_plaster':clipped_feature_panels,
           'limits':['Material/UV adaptation; user visual acceptance and final disc runtime checks are separate.']}
    model['stone_polish']=proof
    return proof,details
