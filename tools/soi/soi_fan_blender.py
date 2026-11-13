"""Author a closed cyan folding fan, bound to retained source weapon motion.

The source robed actor keeps its compressed body motion and all clips. Its old
staff's untextured near faces are removed separately. This new editable fan is
a distinct class with copied parent animation state, including an exact far
prefix, so it follows the actual 117 retained donor poses on both runtimes.
"""
import bpy
import bmesh
import json
import math
import sys
from pathlib import Path
from mathutils import Vector

root=Path(__file__).resolve().parents[2];out=Path(sys.argv[sys.argv.index('--out')+1]).resolve();assert not out.exists();out.mkdir(parents=True)
source=root/'data/soi/content-20261105/phase2-models/content04-editable/models.json'
model=next(m for m in json.loads(source.read_text())['models'] if m['class']==0x2e2)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
palette=[(73,183,221),(113,219,244),(30,124,169),(180,237,246)]
mats=[]
for i,c in enumerate(palette):
    m=bpy.data.materials.new('Cyan fan fold '+str(i));m.diffuse_color=tuple(v/255 for v in c)+(1,);mats.append(m)


def topology(steps):
    angles=[math.radians(v) for v in steps];verts=[]
    for side in [-1,1]:
        verts.append((0,0,side*.009))
        for i,angle in enumerate(angles):verts.append((.215*math.sin(angle),.215*math.cos(angle),side*(.009+(i%2)*.006)))
    n=len(angles)+1;faces=[];materials=[]
    for i in range(1,n-1):faces.extend([(0,i+1,i),(n,n+i,n+i+1)]);materials.extend([i%3,i%3])
    for a,b in [(0,1)]+[(i,i+1) for i in range(1,n-1)]+[(n-1,0)]:faces.append((a,b,b+n,a+n));materials.append(2)
    return dict(vertices=verts,faces=faces,materials=materials)


near=topology([-90,-60,-30,0,30,60,90]);far=topology([-90,-30,30,90])
# Far folds use the same physical ridge heights as the near source vertices.
for side in [0,1]:
    for fi,ni in enumerate([0,1,3,5,7]):far['vertices'][side*5+fi]=near['vertices'][side*8+ni]
order=[near['vertices'].index(v) for v in far['vertices']];order.extend(i for i in range(len(near['vertices'])) if i not in order)
mapping={old:new for new,old in enumerate(order)};near['vertices']=[near['vertices'][i] for i in order];near['faces']=[tuple(mapping[i] for i in f) for f in near['faces']]


def pose(frame):
    v=[Vector(p) for p in frame['vertices']];origin=v[3];axis=(v[140]-origin).normalized();across=v[136]-v[134];across=(across-axis*across.dot(axis)).normalized();normal=across.cross(axis).normalized()
    # Engine Y-up -> Blender Z-up; hinge rests inside the original hand.
    points=[]
    for x,y,z in near['vertices']:
        p=origin+across*x+axis*(y+.025)+normal*z;points.append((p.x,p.z,p.y))
    return points

clips=[]
for clip in model['clips']:
    frames=[]
    for f in clip['frames']:frames.append(dict(vertices=pose(f),metadata=f['metadata'],ticks=f['ticks']))
    clips.append(dict(clip=clip['clip'],frames=frames))
for label,lod,n in [('near',near,len(near['vertices'])),('far',far,len(far['vertices']))]:
    positions=clips[0]['frames'][0]['vertices'][:n];me=bpy.data.meshes.new('Mesa cyan fan '+label);me.from_pydata(positions,[],lod['faces']);me.update()
    ob=bpy.data.objects.new(me.name,me);bpy.context.collection.objects.link(ob)
    for m in mats:me.materials.append(m)
    uv=me.uv_layers.new(name='Fan paper planar UV')
    for poly,mat in zip(me.polygons,lod['materials']):
        poly.material_index=mat
        for li in poly.loop_indices:
            local=near['vertices'][me.loops[li].vertex_index];uv.data[li].uv=(.5+local[0]/.5,.05+local[1]/.25)
    bm=bmesh.new();bm.from_mesh(me);assert all(e.is_manifold for e in bm.edges);bm.free()
    ob.shape_key_add(name='Basis')
    for clip in clips:
        for fi,f in enumerate(clip['frames']):
            key=ob.shape_key_add(name=f'clip_{clip["clip"]:02}_pose_{fi:03}')
            for vertex,p in zip(key.data,f['vertices'][:n]):vertex.co=p
    ob['source_anchors']='Original retained robed donor vertices 3, 134, 136, 140; source clipping/timing retained'
    ob['closed_components']=1;ob['runtime_material']='Cyan RGB folds; editable planar UV retained'
    if label=='far':ob.hide_render=True;ob.hide_set(True)
bpy.ops.wm.save_as_mainfile(filepath=str(out/'mesa-cyan-folding-fan.blend'))
(out/'fan.json').write_text(json.dumps(dict(near=near,far=far,colours=palette,clips=clips,source=str(source),source_class=0x2e2,target_class=0x2e5,
    source_staff_near_vertices=[0,3]+list(range(133,143)),source_staff_near_faces=list(range(24,35)),authorship='Closed cyan folding fan authored in Blender; 117 poses from retained donor weapon motion'),separators=(',',':'))+'\n')
print('AUTHORED CLOSED FAN',len(near['vertices']),'near',len(far['vertices']),'far',sum(len(c['frames']) for c in clips),'source-bound poses; planar UVs retained')
