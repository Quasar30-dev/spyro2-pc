"""Run through Blender MCP on the active Hollowbloom scene.

No retail model or texture payload is embedded in .s2world. The PC loader
resolves references against the mounted copy. PS1 output is a reduction study,
not a game-ready WAD, executable overlay, or hardware-certified asset.
"""
import bpy, json, math
from pathlib import Path
from collections import Counter

ROOT=Path(r'C:/Users/Shadow/Documents/Spyro 2_PC/data/custom-levels/hollowbloom-springs')
scene=bpy.context.scene
assert scene.get('level_id')=='hollowbloom-springs'
materials=sorted({m for o in scene.objects if o.type=='MESH' and o.get('s2_export') in ('geometry','collision') for m in o.data.materials},key=lambda m:m['s2_material'])
mat_ids={m:i for i,m in enumerate(materials)}
spawn=next(o for o in scene.objects if o.get('kind')=='spawn')
route=sorted((o for o in scene.objects if o.get('kind')=='route'),key=lambda o:o['index'])
actors=sorted((o for o in scene.objects if o.get('s2_export')=='moby'),key=lambda o:o['stable_id'])
def xyz(v):return (v[0],v[2],v[1])
def line(v):return ' '.join(f'{n:.7g}' for n in v)

def export(profile):
    reduced=profile=='ps1-study';output=ROOT/profile;output.mkdir(exist_ok=True)
    path=output/'hollowbloom.s2world';triangles=[];sectors=Counter();render_count=0;collision_count=0;degenerates=0;object_count=0
    for o in scene.objects:
        kind=o.get('s2_export')
        if o.type!='MESH' or kind not in ('geometry','collision') or (reduced and o.get('pc_only')):continue
        object_count+=1;md=o.data;md.calc_loop_triangles();uv=md.uv_layers.active;colors=md.color_attributes.get('Color')
        for tri in md.loop_triangles:
            m=md.materials[tri.material_index];flags=(1 if kind=='geometry' else 0)|(2 if o.get('collision') else 0)|(4 if m['runtime_alpha']<1 else 0)
            vertices=[]
            for li in reversed(tri.loops):
                p=o.matrix_world@md.vertices[md.loops[li].vertex_index].co
                if reduced:p=type(p)(round(x*64)/64 for x in p)
                c=colors.data[li].color[:3] if colors else (1,1,1)
                if reduced:c=tuple(round(x*31)/31 for x in c)
                vertices.append((*xyz(p),*c,*uv.data[li].uv))
            from mathutils import Vector
            a,b,c=[Vector(v[:3]) for v in vertices]
            if (b-a).cross(c-a).length<1e-7:degenerates+=1;continue
            triangles.append(f'{flags} {mat_ids[m]} '+ ' '.join(line(v) for v in vertices))
            if flags&1:render_count+=1;sectors[o.get('sector','0,0')]+=1
            if flags&6:collision_count+=1
    with path.open('w',encoding='utf-8',newline='\n') as f:
        f.write('S2WORLD 1\n"Hollowbloom Springs"\n')
        f.write(line((*xyz(spawn.location),spawn['yaw'],.075,.115,.165))+'\n')
        f.write(str(len(materials))+'\n')
        for m in materials:f.write(f'{m["source_slot"]} {m["source_tile"]} {int(m["runtime_alpha"]<1)}\n')
        f.write(str(len(triangles))+'\n');f.write('\n'.join(triangles)+'\n')
        f.write(str(len(actors))+'\n')
        for o in actors:f.write(f'{o["class_id"]} {o["source_record"]} '+line((*xyz(o.location),-o.rotation_euler.z,o['gem_value']))+'\n')
        f.write(str(len(route))+'\n')
        for o in route:f.write(line(xyz(o.location))+'\n')
    report={'profile':profile,'file':str(path),'environment_objects':object_count,'render_triangles':render_count,'collision_triangles':collision_count,'actor_instances':len(actors),'retail_classes':dict(Counter(hex(o['class_id']) for o in actors)),
        'materials':len(materials),'tile_payload_4bpp_bytes_estimate':sum(64*64//2+32 for m in materials if m['source_slot']>=0),'discarded_quantization_degenerates':degenerates,'render_triangles_by_authored_sector':dict(sorted(sectors.items())),
        'ps1_status':'Geometry reduction study. Actor animation RAM, CLUT packing, ordering table, visibility, overlay code and hardware timing remain to be budgeted.'}
    (output/'budget.json').write_text(json.dumps(report,indent=2))
    return report

reports=[export('pc'),export('ps1-study')]
expanded=scene.get('revision',1)>=2
views=[('Expansion 01 /','expanded overview'),('Expansion 02 /','Mooncrown')] if expanded else [('01 /','overview'),('02 /','orchard')]
if not globals().get('SKIP_PREVIEWS',False):
    for prefix,label in views:
        scene.camera=next(o for o in scene.objects if o.type=='CAMERA' and o.name.startswith(prefix))
        scene.render.filepath=str(ROOT/f'Hollowbloom Springs - {label}.png')
        bpy.ops.render.render(write_still=True,scene=scene.name)
scene.camera=next(o for o in scene.objects if o.type=='CAMERA' and o.name.startswith(views[0][0]))
# Write only the final scene and its dependencies. Earlier procedural drafts
# and the untouched startup scene remain in the running Blender session.
for im in bpy.data.images:
    if im.source=='FILE' and im.filepath:im.pack()
scene['pc_world']='//pc/hollowbloom.s2world';scene['reduced_world']='//ps1-study/hollowbloom.s2world'
scene.render.filepath=str(ROOT/f'Hollowbloom Springs - {views[0][1]}.png')
if len(bpy.data.scenes)==1:
    bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'Hollowbloom Springs.blend'),check_existing=False,compress=True)
else:
    bpy.data.libraries.write(str(ROOT/'Hollowbloom Springs.blend'),{scene},path_remap='RELATIVE',fake_user=True,compress=True)
result={'blend':str(ROOT/'Hollowbloom Springs.blend'),'profiles':reports}
