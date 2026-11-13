"""Blender MCP: complete architectural module to anchor consistent scene views.

This explicit structural mesh replaces the open monocular shell. GPT material
atlases are reapplied to real volumes. Photo refinement must preserve this layout.
"""
import bpy,math,json
from pathlib import Path
from mathutils import Vector
ROOT=Path(r'C:/Users/Shadow/Documents/Spyro 2_PC');WORLD=ROOT/'data/custom-levels/lanternfall'
OUT=WORLD/'complete-hall';OUT.mkdir(exist_ok=True)
name='Lanternfall | Complete hall'
if bpy.data.scenes.get(name):raise RuntimeError('Preserve existing complete hall; use a revision name')
scene=bpy.data.scenes.new(name);bpy.context.window.scene=scene
ns={};exec(compile((ROOT/'tools/lanternfall_materials.py').read_text(),'lanternfall_materials.py','exec'),ns)
materials=ns['material_library']();objects=[]

def mesh(name,vertices,faces,material,collision=True,uvs=None,color=None):
    md=bpy.data.meshes.new(name);md.from_pydata(vertices,[],faces);md.update()
    o=bpy.data.objects.new(name,md);scene.collection.objects.link(o);objects.append(o)
    md.materials.append(materials[material]);o['s2_material']=material;o['s2_collision']=collision
    o['s2_geometry']='Complete structural volume';o['s2_color']=color or [1,1,1]
    layer=md.uv_layers.new(name='S2Materials')
    for p in md.polygons:
        normal=p.normal;axis=max(range(3),key=lambda i:abs(normal[i]));axes={0:(1,2),1:(0,2),2:(0,1)}[axis]
        values=[(md.vertices[md.loops[i].vertex_index].co[axes[0]],md.vertices[md.loops[i].vertex_index].co[axes[1]]) for i in p.loop_indices]
        lo=[min(v[i] for v in values) for i in range(2)];hi=[max(v[i] for v in values) for i in range(2)]
        for li,v in zip(p.loop_indices,values):layer.data[li].uv=[(v[i]-lo[i])/max(hi[i]-lo[i],1e-6) for i in range(2)]
    return o

BOX=[(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)]
def box(name,center,size,mat,collision=True):
    cx,cy,cz=center;sx,sy,sz=[s/2 for s in size]
    vs=[(cx+x*sx,cy+y*sy,cz+z*sz) for x,y,z in [(-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]]
    return mesh(name,vs,BOX,mat,collision)

def prism(name,polygon,axis,lo,hi,mat,collision=True):
    # Polygon is counterclockwise in its two local axes; reverse orientation is
    # repaired consistently during export. Thickness remains explicit.
    axes=[i for i in range(3) if i!=axis];vs=[]
    for value in [lo,hi]:
        for p in polygon:
            q=[0,0,0];q[axis]=value;q[axes[0]]=p[0];q[axes[1]]=p[1];vs.append(q)
    n=len(polygon);faces=[tuple(reversed(range(n))),tuple(range(n,2*n))]
    faces.extend((i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n))
    return mesh(name,vs,faces,mat,collision)

def lathe(name,center,profile,mat,sides=16,collision=False):
    vs=[(center[0]+r*math.cos(2*math.pi*i/sides),center[1]+r*math.sin(2*math.pi*i/sides),center[2]+z) for z,r in profile for i in range(sides)]
    faces=[]
    for j in range(len(profile)-1):
        for i in range(sides):a=j*sides+i;b=j*sides+(i+1)%sides;faces.append((a,b,b+sides,a+sides))
    faces.extend([tuple(reversed(range(sides))),tuple((len(profile)-1)*sides+i for i in range(sides))])
    return mesh(name,vs,faces,mat,collision)

def arch(u,width=1.2,spring=3.6,rise=1.5):return spring+rise*max(0,1-abs(u)/width)**.65
def roof(x):return 5.5+3.0*max(0,1-abs(x)/4.5)**.68

# Full floor, with separate stone tiles and real thickness.
for i in range(5):
    for j in range(12):box(f'Paving {i} {j}',(-3.6+i*1.8,-2+j*2,-.18),(1.8,2,.36),'interior.paving')

# Six genuinely open windows with deep jambs and closed wall masses around them.
for side in [-1,1]:
    x=side*4.7
    box(f'Wall sill {side}',(x,9,.7),(.5,18,1.4),'interior.wall')
    for bay in range(3):
        center=3+6*bay
        for y in [center-2.1,center+2.1]:box(f'Wall pier {side} {bay} {y}',(x,y,3.45),(.5,1.8,4.1),'interior.wall')
        for j in range(18):
            a=-1.2+j*2.4/18;b=-1.2+(j+1)*2.4/18
            poly=[(center+a,arch(a)),(center+b,arch(b)),(center+b,5.5),(center+a,5.5)]
            prism(f'Wall above window {side} {bay} {j}',poly,0,x-.25,x+.25,'interior.wall')
            outer=[(center+a,arch(a)),(center+b,arch(b)),(center+b,arch(b)+.17),(center+a,arch(a)+.17)]
            prism(f'Window archivolt {side} {bay} {j}',outer,0,x-.34,x+.34,'interior.carved_trim')
        # A small overlap avoids coplanar jamb/pier faces at exactly +/-1.2.
        for y in [center-1.27,center+1.27]:box(f'Window jamb {side} {bay} {y}',(x,y,2.52),(.78,.18,2.24),'interior.carved_trim')
        box(f'Window ledge {side} {bay}',(x,center,1.38),(.9,2.8,.18),'interior.carved_trim')
    # Wall column joints/bases and projecting carved capitals.
    for y in [0,6,12,18]:
        lathe(f'Column {side} {y}',(side*4.28,y,0),[(0,.49),(.12,.55),(.32,.55),(.42,.43),(.58,.34),(4.98,.34),(5.1,.44),(5.28,.5),(5.5,.5)],'interior.wall',16,True)
        box(f'Column capital {side} {y}',(side*4.28,y,5.36),(1.04,1.04,.25),'interior.carved_trim')

# Vault spans the entire room: no missing rear/side surface or camera-dependent shell.
for j in range(9):
    ya,yb=j*2,(j+1)*2
    for i in range(24):
        xa,xb=-4.5+i*9/24,-4.5+(i+1)*9/24
        polygon=[(xa,roof(xa)),(xb,roof(xb)),(xb,roof(xb)+.22),(xa,roof(xa)+.22)]
        prism(f'Vault panel {j} {i}',polygon,1,ya,yb,'interior.vault')
for y in [0,6,12,18]:
    for i in range(32):
        xa,xb=-4.5+i*9/32,-4.5+(i+1)*9/32
        polygon=[(xa,roof(xa)-.14),(xb,roof(xb)-.14),(xb,roof(xb)+.18),(xa,roof(xa)+.18)]
        prism(f'Transverse rib {y} {i}',polygon,1,y-.16,y+.16,'interior.carved_trim')

# Both ends have full walls, vaulted gables, entrance arches and short vestibules.
for end in [0,18]:
    for side in [-1,1]:box(f'End wall {end} {side}',(side*3.03,end,2.75),(2.94,.5,5.5),'interior.wall')
    for i in range(24):
        xa,xb=-4.5+i*9/24,-4.5+(i+1)*9/24
        prism(f'End gable {end} {i}',[(xa,5.5),(xb,5.5),(xb,roof(xb)),(xa,roof(xa))],1,end-.25,end+.25,'interior.wall')
    for i in range(20):
        a=-1.56+i*3.12/20;b=-1.56+(i+1)*3.12/20
        h1=arch(a,1.56,3.1,1.65);h2=arch(b,1.56,3.1,1.65)
        prism(f'Gateway infill {end} {i}',[(a,h1),(b,h2),(b,5.5),(a,5.5)],1,end-.25,end+.25,'interior.wall')
        prism(f'Gateway arch {end} {i}',[(a,h1-.04),(b,h2-.04),(b,h2+.2),(a,h1+.2)],1,end-.4,end+.4,'interior.carved_trim')
    direction=-1 if end==0 else 1;mid=end+direction*1.4;far=end+direction*2.8
    for side in [-1,1]:box(f'Vestibule wall {end} {side}',(side*1.8,mid,2.8),(.5,2.8,5.6),'interior.wall')
    box(f'Vestibule roof {end}',(0,mid,5.52),(4.1,2.8,.3),'interior.vault')
    box(f'Solid double door {end}',(0,far,2.7),(4.1,.3,5.4),'interior.oak')
    for x in [-1.25,0,1.25]:box(f'Door metal brace {end} {x}',(x,far-direction*.18,2.7),(.08,.06,5.15),'interior.brass')
    for z in [.5,2.6,4.7]:box(f'Door horizontal brace {end} {z}',(0,far-direction*.18,z),(3.6,.06,.08),'interior.brass')

# Benches, topiary planters and banners are complete objects, visible from behind.
for side in [-1,1]:
    for bay in range(3):
        y=3+bay*6
        box(f'Bench top {side} {bay}',(side*3.72,y,.77),(.75,2.15,.2),'interior.carved_trim')
        for dy in [-.72,.72]:box(f'Bench foot {side} {bay} {dy}',(side*3.72,y+dy,.36),(.55,.2,.72),'interior.wall')
    for y in [1,7,13,17]:
        x=side*3.55
        lathe(f'Planter {side} {y}',(x,y,0),[(0,.27),(.12,.27),(.55,.42),(.62,.46),(.73,.46)],'interior.wall')
        # Layered evergreen volume, not alpha cards; reusable closed meshes.
        for k,(z,r) in enumerate([(.68,.49),(1.02,.45),(1.36,.39),(1.7,.31),(2.0,.23),(2.27,.14)]):
            lathe(f'Topiary foliage {side} {y} {k}',(x,y,z),[(0,r*.8),(.1,r),(.44,r*.12)],'exterior.ivy',10)
    for y in [5.65,11.65]:
        box(f'Purple hanging banner {side} {y}',(side*3.89,y,3.78),(.065,.77,2.15),'interior.banner',False)
        box(f'Banner brass rod {side} {y}',(side*3.87,y,4.88),(.13,.96,.09),'interior.brass',False)
box('Compass floor inlay',(0,9,.005),(3,3,.01),'interior.compass',False)

# Actual full-volume lamps from the independently verified prop asset.
lantern=json.loads((WORLD/'props/hanging-lantern-mesh.json').read_text())
for index,y in enumerate([3,9,15]):
    for part in lantern:
        verts=[(v[0]*1.15,v[1]*1.15+y,v[2]*1.15+8.45-1.67*1.15) for v in part['vertices']]
        if 'glass' in part['name'].lower() or 'core' in part['name'].lower():
            mat='interior.brass';color=[1,.96,.72]
        else:mat='interior.brass';color=[1,1,1]
        mesh(f'Lantern {index} | '+part['name'],verts,part['faces'],mat,False,color=color)
    d=bpy.data.lights.new(f'Lantern glow {index}','POINT');d.energy=280;d.color=(1,.65,.23);d.shadow_soft_size=.35
    o=bpy.data.objects.new(d.name,d);scene.collection.objects.link(o);o.location=(0,y,7.12)

scene.render.engine='CYCLES';scene.cycles.samples=24
scene.render.resolution_x=1200;scene.render.resolution_y=900;scene.render.resolution_percentage=100
scene.view_settings.view_transform='AgX';scene.world=bpy.data.worlds.new('Blank sky for scene references');scene.world.use_nodes=True
scene.world.node_tree.nodes['Background'].inputs['Color'].default_value=(1,1,1,1);scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value=.55
for side in [-1,1]:
    for y in [3,9,15]:
        d=bpy.data.lights.new(f'Window fill {side} {y}','AREA');d.energy=350;d.size=4;d.color=(1,.86,.65)
        o=bpy.data.objects.new(d.name,d);scene.collection.objects.link(o);o.location=(side*5.5,y,3.8);o.rotation_euler=(Vector((0,y,1))-o.location).to_track_quat('-Z','Y').to_euler()
cameras=[('01 South to north',(0,1,1.8),(0,15,3)),('02 North to south',(0,17,1.8),(0,3,3)),
 ('03 Southwest',( -3,2,1.8),(2,14,3)),('04 Southeast',(3,2,1.8),(-2,14,3)),
 ('05 Northwest',(-3,16,1.8),(2,4,3)),('06 Northeast',(3,16,1.8),(-2,4,3)),
 ('07 East wall',(-2.4,9,2),(4.5,9,3.5)),('08 West wall',(2.4,9,2),(-4.5,9,3.5)),
 ('09 Ceiling',(0,9,2),(0,9,8.5)),('10 Floor',(1.6,9,6.5),(.6,9,0))]
for label,pos,target in cameras:
    d=bpy.data.cameras.new(label);cam=bpy.data.objects.new(label,d);scene.collection.objects.link(cam);cam.location=pos
    cam.rotation_euler=(Vector(target)-cam.location).to_track_quat('-Z','Y').to_euler();d.lens=20
    if scene.camera is None:scene.camera=cam
scene['status']='Complete structural hall; all-angle reference/refinement base. No panorama/depth-shell backdrop.'
scene['dimensions']='9 m clear width, 18 m hall, two 2.8 m vestibules, 8.5 m vaulted apex'
scene.view_layers[0].update()
# Raw mesh export retains materials/UVs and collision independently of render state.
records=[]
for o in objects:
    md=o.data;md.calc_loop_triangles();uv=md.uv_layers['S2Materials']
    records.append({'name':o.name,'vertices':[list(v.co) for v in md.vertices],
        'faces':[list(t.vertices) for t in md.loop_triangles],
        'triangle_uvs':[[list(uv.data[li].uv) for li in t.loops] for t in md.loop_triangles],
        'material':o['s2_material'],'color':list(o['s2_color']),'collision':bool(o['s2_collision'])})
(OUT/'geometry.json').write_text(json.dumps(records))
(OUT/'cameras.json').write_text(json.dumps([{'id':label,'position':list(pos),'target':list(target),'projection':'perspective','horizontal_fov_degrees':math.degrees(scene.objects[label].data.angle_x),'blender_camera':label} for label,pos,target in cameras],indent=2))
bpy.ops.wm.save_as_mainfile(filepath=str(WORLD/'Lanternfall - Complete hall.blend'),compress=True)
result={'scene':name,'objects':len(objects),'triangles':sum(len(o['faces']) for o in records),'cameras':len(cameras),'saved':str(WORLD/'Lanternfall - Complete hall.blend')}
