"""Author Mesa's small pink rabbit in Blender; closed components and two LODs.

No generated images or downloaded assets. The reference is the retained
original type16 sprite sheet. Runtime material is flat vertex colour; UVs and
the reusable swatch sheet remain editable in the Blender source.
"""
import bpy
import bmesh
import json
import math
import sys
from pathlib import Path
from mathutils import Vector

out=Path(sys.argv[sys.argv.index('--out')+1]).resolve();assert not out.exists();out.mkdir(parents=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
colours=[(238,137,232),(255,204,255),(182,69,178),(25,13,32),(255,246,255)]
names=['pink fur','pale muzzle','inner ears','eyes and mouth','teeth and eye highlights']
materials=[]
for name,rgb in zip(names,colours):
    m=bpy.data.materials.new(name);m.diffuse_color=tuple(v/255 for v in rgb)+(1,);materials.append(m)
sheet=bpy.data.images.new('Mesa rabbit reusable swatches',width=80,height=16,alpha=True)
sheet.pixels=[v for y in range(16) for x in range(80) for v in tuple(c/255 for c in colours[x//16])+(1,)]
sheet.filepath_raw=str(out/'rabbit-swatches.png');sheet.file_format='PNG';sheet.save();sheet.pack()

# Every far vertex is also a near vertex. Components share the same origin,
# scale, UV convention, material and silhouette landmarks across both LODs.
parts=[('body',(0,0,.31),(.21,.17,.29),0),('head',(0,-.025,.64),(.245,.19,.21),0),
       ('muzzle',(0,-.192,.595),(.15,.065,.095),1),('left ear',(-.115,0,.94),(.055,.06,.25),0),
       ('right ear',(.115,0,.94),(.055,.06,.25),0),('left inner ear',(-.115,-.052,.95),(.024,.014,.17),2),
       ('right inner ear',(.115,-.052,.95),(.024,.014,.17),2),
       ('left foot',(-.145,-.06,.065),(.12,.18,.065),0),('right foot',(.145,-.06,.065),(.12,.18,.065),0),
       ('left paw',(-.235,-.015,.35),(.075,.075,.15),0),('right paw',(.235,-.015,.35),(.075,.075,.15),0),
       ('tail',(0,.175,.25),(.105,.10,.10),1),('left eye',(-.085,-.179,.685),(.04,.023,.052),3),
       ('right eye',(.085,-.179,.685),(.04,.023,.052),3),('smile',(0,-.252,.575),(.08,.018,.035),3),
       ('front teeth',(0,-.270,.582),(.037,.012,.030),4)]


def ellipsoid(name,c,r,mat,sides):
    # One equatorial ring and two poles; near adds alternate vertices on the
    # same ring. All components are closed convex volumes, including details.
    verts=[(c[0],c[1],c[2]+r[2]),(c[0],c[1],c[2]-r[2])]
    verts.extend((c[0]+r[0]*math.cos(k*2*math.pi/sides),c[1]+r[1]*math.sin(k*2*math.pi/sides),c[2]) for k in range(sides))
    faces=[]
    for k in range(sides):
        p,q=k+2,(k+1)%sides+2;faces.extend([(0,p,q),(1,q,p)])
    return verts,faces,[mat]*len(faces)


lods=[]
for label,sides in [('near',8),('far',4)]:
    verts=[];faces=[];mats=[];keys=[]
    for name,c,r,mat in parts:
        vv,ff,mm=ellipsoid(name,c,r,mat,sides);offset=len(verts);verts.extend(vv);faces.extend(tuple(i+offset for i in f) for f in ff);mats.extend(mm)
        keys.extend((name,tuple(round(x,7) for x in v)) for v in vv)
    lods.append(dict(label=label,vertices=verts,faces=faces,materials=mats,keys=keys))
near,far=lods
order=[near['keys'].index(k) for k in far['keys']];order.extend(i for i in range(len(near['keys'])) if i not in order)
assert len(set(order))==len(near['vertices']);reindex={old:new for new,old in enumerate(order)}
near['vertices']=[near['vertices'][i] for i in order];near['faces']=[tuple(reindex[i] for i in f) for f in near['faces']]
assert all(tuple(near['vertices'][i])==tuple(v) for i,v in enumerate(far['vertices']))
for lod in lods:
    me=bpy.data.meshes.new('Mesa rabbit '+lod['label']);me.from_pydata(lod['vertices'],[],lod['faces']);me.update()
    ob=bpy.data.objects.new(me.name,me);bpy.context.collection.objects.link(ob)
    for m in materials:me.materials.append(m)
    uv=me.uv_layers.new(name='Reusable swatch UV')
    for poly,material in zip(me.polygons,lod['materials']):
        poly.material_index=material
        for li in poly.loop_indices:
            v=me.vertices[me.loops[li].vertex_index].co
            # A bounded chart inside the matching 16x16 swatch, with padding.
            uv.data[li].uv=((material*16+3+10*(v.x+.5))/80,(3+10*(v.z/1.25))/16)
    bm=bmesh.new();bm.from_mesh(me);assert all(e.is_manifold for e in bm.edges);bm.free()
    ob['reference']='Retained original SoI Mesa type16 pink rabbit sprites; authored 3D interpretation'
    ob['runtime_material']='Untextured RGB vertex colours; editable swatch UV retained'
    ob['closed_components']=len(parts);ob['lod_pair']='same vertices at far prefix; same component/material landmarks'
    if lod['label']=='far':ob.hide_render=True;ob.hide_set(True)
    ob.shape_key_add(name='Basis')
    for f in range(8):
        key=ob.shape_key_add(name=f'hop_{f:02}')
        height=(0,8,16,24,24,16,8,0)[f]/1024
        for v in key.data:v.co.z+=height
    del lod['keys']
bpy.ops.wm.save_as_mainfile(filepath=str(out/'mesa-type16-rabbit.blend'))
payload=dict(authorship='Blender-authored closed pink rabbit, source sprite informed',lods=lods,colours=colours,
             frames=8,hop_units=[0,8,16,24,24,16,8,0],closed_components=len(parts),runtime_material='vertex colours',
             limits=['Best-effort 3D interpretation of original sprites; no image/model generator.',
                     'UVs and reusable swatches remain in Blender; flat RGB colour packets in PS1.'])
(out/'rabbit.json').write_text(json.dumps(payload,separators=(',',':'))+'\n')
print('AUTHORED RABBIT',len(near['vertices']),'near',len(far['vertices']),'far',len(parts),'closed components; editable UVs and swatches')
