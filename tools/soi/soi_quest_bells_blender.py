"""Blender-authored gold bells for Mesa's actual 2,1,0,2,1,0 source quest.

The retained original slot28 sprite bank supplies the gold bell/pink beam
reference. Source coordinates and sequence are read separately from the ROM.
Closed inner/outer bell walls, closed beams, editable UVs, matched LOD prefix.
"""
import bpy
import bmesh
import json
import math
import sys
from pathlib import Path

out=Path(sys.argv[sys.argv.index('--out')+1]).resolve();assert not out.exists();out.mkdir(parents=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
palette=[(255,202,52),(221,135,21),(255,230,115),(227,150,218)]
materials=[]
for i,c in enumerate(palette):
    m=bpy.data.materials.new('Bell source colour '+str(i));m.diffuse_color=tuple(x/255 for x in c)+(1,);materials.append(m)


def lod(sides):
    vertices=[];faces=[];mats=[]
    # Closed hollow bell shell: outside -> inside -> rim; no missing back.
    profiles=[(.31,.24),(.29,.30),(.23,.52),(.13,.66),(.055,.70),(.025,.68),(.10,.63),(.20,.49),(.255,.29),(.275,.24)]
    for radius,z in profiles:
        vertices.extend((radius*math.cos(2*math.pi*k/sides),radius*math.sin(2*math.pi*k/sides),z) for k in range(sides))
    for ring in range(len(profiles)):
        nxt=(ring+1)%len(profiles)
        for k in range(sides):faces.append((ring*sides+k,ring*sides+(k+1)%sides,nxt*sides+(k+1)%sides,nxt*sides+k));mats.append(2 if k%3==0 else 0 if ring<5 else 1)
    moving=len(vertices)
    def box(center,size):
        start=len(vertices)
        vertices.extend(tuple(center[j]+size[j]*v[j]/2 for j in range(3)) for v in [(-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)])
        for f in [(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)]:faces.append(tuple(start+i for i in f));mats.append(3)
    box((-.40,0,.43),(.055,.055,.86));box((.40,0,.43),(.055,.055,.86));box((0,0,.87),(.92,.055,.055))
    return dict(vertices=vertices,faces=faces,materials=mats,moving=moving)

near,far=lod(8),lod(4)
keys=lambda verts:[tuple(round(v,7) for v in p) for p in verts]
nk,fk=keys(near['vertices']),keys(far['vertices']);order=[nk.index(k) for k in fk];order.extend(i for i in range(len(nk)) if i not in order)
mapping={old:new for new,old in enumerate(order)};moving={mapping[i] for i in range(near['moving'])}
near['vertices']=[near['vertices'][i] for i in order];near['faces']=[tuple(mapping[i] for i in f) for f in near['faces']]
clips=[]
for clip,angles in [(0,[0]),(1,[0,18,10,-15,-8,9,-4,0])]:
    frames=[]
    for angle in angles:
        a=math.radians(angle);vv=[]
        for i,(x,y,z) in enumerate(near['vertices']):
            if i in moving:y,z=y*math.cos(a)-(z-.72)*math.sin(a),.72+y*math.sin(a)+(z-.72)*math.cos(a)
            vv.append((x,y,z))
        frames.append(dict(vertices=vv,metadata=3<<9,ticks=4))
    clips.append(dict(clip=clip,frames=frames))
for label,mesh in [('near',near),('far',far)]:
    me=bpy.data.meshes.new('Mesa quest bell '+label);me.from_pydata(mesh['vertices'],[],mesh['faces']);me.update();ob=bpy.data.objects.new(me.name,me);bpy.context.collection.objects.link(ob)
    for m in materials:me.materials.append(m)
    uv=me.uv_layers.new(name='Bell circumferential UV')
    for poly,mat in zip(me.polygons,mesh['materials']):
        poly.material_index=mat
        for li in poly.loop_indices:
            x,y,z=me.vertices[me.loops[li].vertex_index].co;uv.data[li].uv=((math.atan2(y,x)/(2*math.pi))%1,z)
    bm=bmesh.new();bm.from_mesh(me);assert all(e.is_manifold for e in bm.edges);bm.free()
    ob.shape_key_add(name='Basis')
    for fi,f in enumerate(clips[1]['frames']):
        key=ob.shape_key_add(name=f'ring_{fi:02}')
        for v,p in zip(key.data,f['vertices'][:len(mesh['vertices'])]):v.co=p
    ob['reference']='Original Mesa slot28 gold bell and pink crossbar; closed 3D interpretation';ob['closed_components']=4
    if label=='far':ob.hide_render=True;ob.hide_set(True)
bpy.ops.wm.save_as_mainfile(filepath=str(out/'mesa-source-quest-bells.blend'))
(out/'bells.json').write_text(json.dumps(dict(near=near,far=far,colours=palette,clips=clips,authorship='Original sprite-informed bells authored in Blender; source coordinates/sequence not invented'),separators=(',',':'))+'\n')
print('AUTHORED BELLS',len(near['vertices']),'near',len(far['vertices']),'far; four closed components; editable UVs; idle/ring poses')
