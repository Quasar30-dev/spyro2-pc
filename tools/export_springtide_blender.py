"""Export the active Springtide scene through Blender MCP to S2WORLD 2."""
import bpy,json,math
from pathlib import Path
from collections import Counter
from mathutils import Vector
ROOT=Path(r'C:/Users/Shadow/Documents/Spyro 2_PC/data/custom-levels/springtide-castle')
scene=bpy.context.scene
assert scene.get('level_id')=='springtide-castle'
design=json.loads((ROOT/'source'/'design.json').read_text())
materials=sorted({m for o in scene.objects if o.type=='MESH' and o.get('s2_export') in ('geometry','collision','retail_prefab') for m in o.data.materials},key=lambda m:m['s2_material'])
mat_ids={m:i for i,m in enumerate(materials)}
actors=sorted((o for o in scene.objects if o.get('s2_export')=='moby'),key=lambda o:o['stable_id'])
trees=[o for o in scene.objects if o.get('s2_export')=='retail_prefab']
spawn=next(o for o in scene.objects if o.get('kind')=='spawn')
route=sorted((o for o in scene.objects if o.get('kind')=='route'),key=lambda o:o['index'])
portals=sorted((o for o in scene.objects if o.get('kind')=='portal'),key=lambda o:o['reference'])
def xyz(v):return (v[0],v[2],v[1])
def line(v):return ' '.join(f'{x:.7g}' for x in v)
def quote(v):return json.dumps(v,ensure_ascii=True)

def export(profile):
    reduced=profile=='ps1-study';folder=ROOT/profile;folder.mkdir(exist_ok=True)
    path=folder/'springtide.s2world';triangles=[];sectors=Counter();render=collision=skipped=0;groups={}
    for o in scene.objects:
        kind=o.get('s2_export')
        if o.type!='MESH' or kind not in ('geometry','collision') or (reduced and o.get('pc_only')):continue
        first_v=render*3;first_t=collision
        md=o.data;md.calc_loop_triangles();uv=md.uv_layers.active;colors=md.color_attributes.get('Color')
        for t in md.loop_triangles:
            m=md.materials[t.material_index];flags=(1 if kind=='geometry' else 0)|(2 if o.get('collision') else 0)|(4 if m['water'] else 0)
            vertices=[]
            for li in reversed(t.loops):
                p=o.matrix_world@md.vertices[md.loops[li].vertex_index].co
                if reduced:p=Vector(round(x*64)/64 for x in p)
                c=colors.data[li].color[:3] if colors else (1,1,1)
                if reduced:c=tuple(round(x*31)/31 for x in c)
                vertices.append((*xyz(p),*c,*uv.data[li].uv))
            a,b,c=[Vector(v[:3]) for v in vertices]
            if (b-a).cross(c-a).length<1e-7:skipped+=1;continue
            triangles.append(f'{flags} {mat_ids[m]} '+' '.join(line(v) for v in vertices))
            if flags&1:render+=1;sectors[o.get('sector','0,0')]+=1
            if flags&6:collision+=1
        if 'gameplay_group' in o:groups.setdefault(o['gameplay_group'],[]).append((first_v,render*3-first_v,first_t,collision-first_t))
    with path.open('w',encoding='utf-8',newline='\n') as f:
        f.write('S2WORLD 2\n15\n"Springtide Castle"\n')
        f.write(line((*xyz(spawn.location),spawn['yaw'],.3,.45,.62))+'\n')
        f.write(str(len(materials))+'\n')
        for m in materials:f.write(f'{m["source_slot"]} {m["source_tile"]} {int(m["water"])}\n')
        f.write(str(len(triangles))+'\n'+'\n'.join(triangles)+'\n')
        f.write(str(len(actors))+'\n')
        for o in actors:f.write(f'{o["class_id"]} {o["source_record"]} '+line((*xyz(o.location),-o.rotation_euler.z,o['gem_value']))+'\n')
        f.write(str(len(route))+'\n')
        for o in route:f.write(line(xyz(o.location))+'\n')
        f.write(str(len(trees))+'\n')
        for o in trees:f.write(f'15 {o["source_region"]} '+line((*xyz(o.location),-o.rotation_euler.z,o.scale.x))+'\n')
        f.write(str(len(portals))+'\n')
        for o in portals:f.write(f'{o["reference"]} '+line((*xyz(o.location),o['yaw'],o['scale']))+'\n')
        f.write(str(len(design['npcs']))+'\n')
        for n in design['npcs']:f.write(str(n['actor'])+' '+quote(n['name'])+' '+quote(n['initial'])+' '+quote(n['complete'])+'\n')
        f.write(str(len(design['objectives']))+'\n')
        for g in design['objectives']:
            f.write(f'{g["bit"]} {g["npc"]} {g["reward"]} {int(g["underwater"])} '+quote(g['title'])+' '+str(len(g['checkpoints']))+'\n')
            for p in g['checkpoints']:f.write(line(xyz(p))+'\n')
        if design.get('mechanisms'):
            f.write('GAMEPLAY 1\n'+str(len(design['mechanisms']))+'\n')
            for p in design['mechanisms']:
                ranges=groups[p['id']]
                f.write(f'{p["kind"]} {p["objective"]} '+quote(p['name'])+' '+line((*xyz(p['center']),*xyz(p['extent']),*xyz(p['axis']),p['travel'],p['period']))+' '+str(len(ranges))+'\n')
                for r in ranges:f.write(line(r)+'\n')
            f.write(str(len(design['trials']))+'\n')
            for t in design['trials']:f.write(f'{t["objective"]} {t["limit"]} '+line(xyz(t['start']))+' '+quote(t['name'])+'\n')
    tree_tri=sum(len(o.data.polygons) for o in trees)
    report={'profile':profile,'file':str(path),'environment_triangles':render,'referenced_tree_triangles':tree_tri,'total_environment_triangles':render+tree_tri,'collision_triangles':collision,'actors':len(actors),'trees':len(trees),'portals':len(portals),'objectives':len(design['objectives']),'materials':len(materials),'texture_4bpp_estimate_bytes':sum(2080 for m in materials if m['source_slot']>=0),'discarded_degenerate_triangles':skipped,'sectors':dict(sorted(sectors.items())),'ps1_status':'Authoring/reduction study only. Retail archive packing, per-room visibility, CLUT allocation, actor RAM and hardware timing are not implemented.'}
    (folder/'budget.json').write_text(json.dumps(report,indent=2))
    return report

reports=[export('pc'),export('ps1-study')]
with (ROOT/'source'/'walking.s2routes').open('w',encoding='utf-8') as f:
    routes=dict(design.get('extra_routes',{}));routes['Library stairs']=design['stairs']
    f.write('S2ROUTES 1\n'+str(len(routes))+'\n')
    for name,points in routes.items():
        f.write(quote(name)+' '+str(len(points))+'\n')
        for p in points:f.write(line(xyz(p))+'\n')
# Physics regression courses include explicit edge takeoffs rather than asking
# a jump controller to cross entire platforms from centre to centre.
courses=[]
for course in design['platform_courses']:
    points=course['points'];steps=[(points[0],False)]
    for i,(a,b) in enumerate(zip(points,points[1:])):
        av,bv=Vector(a),Vector(b);d=bv-av;horizontal=Vector((d.x,d.y,0)).normalized()
        jump=(course['name']=='Across the Great Hall' or i<6)
        if jump:
            edge=av+horizontal*(.85 if course['name']=='Across the Great Hall' else 1.1);steps.append((list(edge),False))
        steps.append((b,jump))
    courses.append((course['name'],steps))
with (ROOT/'source'/'platforming.s2courses').open('w',encoding='utf-8') as f:
    f.write('S2COURSES 1\n'+str(len(courses))+'\n')
    for name,steps in courses:
        f.write(quote(name)+' '+str(len(steps))+'\n')
        for p,jump in steps:f.write(line((*xyz(p),int(jump)))+'\n')
with (ROOT/'source'/'swimming.s2route').open('w') as f:
    f.write(str(len(design['swim_route']))+'\n')
    for p in design['swim_route']:f.write(line(xyz(p))+'\n')
scene['pc_world']='//pc/springtide.s2world';scene['study_world']='//ps1-study/springtide.s2world'
bpy.data.libraries.write(str(ROOT/'Springtide Castle.blend'),{scene},path_remap='RELATIVE',fake_user=True,compress=True)
result={'blend':str(ROOT/'Springtide Castle.blend'),'exports':reports}
