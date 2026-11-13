"""Read-only structural check of the saved Blender authoring artifact."""
import bpy
import bmesh
import hashlib
import json
import math
from pathlib import Path
from mathutils.bvhtree import BVHTree

path=Path(bpy.data.filepath)
print('ARTIFACT',path,'sha256',hashlib.sha256(path.read_bytes()).hexdigest(),flush=True)
meshes=[bpy.data.objects[n] for n in ('Grendor_MASTER','Grendor_LOD0','Grendor_LOD1','Grendor_LOD2')]
limits=(None,244,160,100)
stats=[]
for obj,limit in zip(meshes,limits):
    assert tuple(obj.location)==(0,0,0) and tuple(obj.scale)==(1,1,1)
    assert obj.parent.name=='Grendor_RIG'
    assert len(obj.data.uv_layers)==1 and len(obj.data.uv_layers.active.data)==len(obj.data.loops)
    assert all(all(math.isfinite(x) for x in v.co) and abs(sum(g.weight for g in v.groups)-1)<1e-5 for v in obj.data.vertices)
    assert all(0<=x<=1 for u in obj.data.uv_layers.active.data for x in u.uv)
    if limit:assert len(obj.data.vertices)<=limit
    group_sets={}
    for group in ('body','cape'):
        gi=obj.vertex_groups[group].index
        group_sets[group]={v.index for v in obj.data.vertices if any(g.group==gi and g.weight>.5 for g in v.groups)}
    co=[v.co for v in obj.data.vertices]
    trees={group:BVHTree.FromPolygons(co,[list(p.vertices) for p in obj.data.polygons
              if all(v in group_sets[group] for v in p.vertices)]) for group in group_sets}
    assert not trees['body'].overlap(trees['cape']),(obj.name,'cape intersects torso')
    bm=bmesh.new();bm.from_mesh(obj.data);unseen=set(bm.verts);closed=0;decals=0;volumes=[]
    while unseen:
        pending=[unseen.pop()];component=set(pending)
        while pending:
            v=pending.pop()
            for e in v.link_edges:
                q=e.other_vert(v)
                if q in unseen:unseen.remove(q);component.add(q);pending.append(q)
        faces={f for v in component for f in v.link_faces};edges={e for v in component for e in v.link_edges}
        if all(e.is_manifold for e in edges):
            volume=0
            for f in faces:
                p=f.verts[0].co
                for i in range(1,len(f.verts)-1):volume+=p.dot(f.verts[i].co.cross(f.verts[i+1].co))/6
            assert volume>1e-8,(obj.name,'inward or degenerate closed shell',volume)
            closed+=1;volumes.append(volume)
        else:
            # Only eye/mouth surface patches may be open; no open solids.
            assert all(f.material_index in (11,15) for f in faces),(obj.name,'unclosed solid')
            decals+=1
    bm.free();stats.append({'name':obj.name,'vertices':len(obj.data.vertices),'closed_shells':closed,'open_eye_mouth_decals':decals,'cape_torso_triangle_intersections':0,'volumes':volumes})
print('AUTHORING STRUCTURE PASS',json.dumps(stats),flush=True)
print('LIMIT neutral Blender authoring asset only; no PS1 encoder, game import, source-animation or user visual acceptance claimed',flush=True)
