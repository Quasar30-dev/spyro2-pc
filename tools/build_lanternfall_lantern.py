"""Blender MCP: complete volumetric hanging lantern, matching the hall reference.

No camera-facing cards: cage, solid panes, roof, chain and inner light are meshes.
"""
import bpy,math,json
from pathlib import Path
from mathutils import Vector
ROOT=Path(r'C:/Users/Shadow/Documents/Spyro 2_PC')
OUT=ROOT/'data/custom-levels/lanternfall/props';OUT.mkdir(exist_ok=True)
name='Lanternfall | Hanging lantern mesh'
if bpy.data.scenes.get(name):raise RuntimeError('Preserve the existing lantern asset')
scene=bpy.data.scenes.new(name)
bpy.context.window.scene=scene
parts=[]

def material(name,color,metal=0,rough=.4,emission=0,transmission=0):
    m=bpy.data.materials.new(name);m.diffuse_color=(*color,1);m.use_nodes=True
    p=m.node_tree.nodes['Principled BSDF'];p.inputs['Base Color'].default_value=(*color,1)
    p.inputs['Metallic'].default_value=metal;p.inputs['Roughness'].default_value=rough
    p.inputs['Emission Color'].default_value=(*color,1);p.inputs['Emission Strength'].default_value=emission
    p.inputs['Transmission Weight'].default_value=transmission
    return m

gold=material('Lantern brass',(.55,.265,.055),.72,.27)
dark=material('Lantern recessed brass',(.12,.055,.012),.65,.38)
glass=material('Lantern amber solid glass',(.95,.58,.15),0,.19,.25,.55)
glow=material('Lantern warm light core',(1,.52,.11),0,.35,4)

def mesh_part(name,vertices,faces,mat):
    m=bpy.data.meshes.new(name);m.from_pydata(vertices,[],faces);m.update()
    o=bpy.data.objects.new(name,m);scene.collection.objects.link(o);m.materials.append(mat)
    o['s2_part']=name;o['s2_geometry']='Full 3D mesh, not a billboard';parts.append(o)
    return o

def lathe(name,profile,mat,sides=6):
    verts=[(r*math.cos(2*math.pi*j/sides),r*math.sin(2*math.pi*j/sides),z) for z,r in profile for j in range(sides)]
    faces=[]
    for ring in range(len(profile)-1):
        for j in range(sides):
            a=ring*sides+j;b=ring*sides+(j+1)%sides
            faces.append((a,b,b+sides,a+sides))
    faces.extend([tuple(reversed(range(sides))),tuple((len(profile)-1)*sides+j for j in range(sides))])
    return mesh_part(name,verts,faces,mat)

def rod(name,a,b,r,mat,sides=10):
    a,b=Vector(a),Vector(b);q=(b-a).to_track_quat('Z','Y')
    vs=[tuple(q@Vector((r*math.cos(2*math.pi*j/sides),r*math.sin(2*math.pi*j/sides),z))+a) for z in [0,(b-a).length] for j in range(sides)]
    faces=[(j,(j+1)%sides,(j+1)%sides+sides,j+sides) for j in range(sides)]
    faces.extend([tuple(reversed(range(sides))),tuple(range(sides,sides*2))])
    return mesh_part(name,vs,faces,mat)

lathe('Bottom finial',[(0,.025),(.035,.06),(.07,.075),(.12,.045),(.15,.19)],gold,12)
lathe('Lower structural rim',[(.135,.18),(.155,.21),(.185,.21),(.202,.188)],gold)
lathe('Upper structural rim',[(.58,.281),(.605,.305),(.635,.305),(.65,.285)],gold)
lathe('Hexagonal roof cap',[(.63,.32),(.665,.32),(.705,.28),(.855,.105),(.89,.083)],gold)
lathe('Roof recessed band',[(.665,.322),(.68,.322)],dark)
lathe('Roof finial',[(.86,.082),(.89,.075),(.92,.045),(.97,.045),(.995,.02)],gold,12)
for i in range(6):
    a=2*math.pi*i/6;b=2*math.pi*(i+1)/6
    rod('Cage rib '+str(i),(math.cos(a)*.188,math.sin(a)*.188,.19),(math.cos(a)*.282,math.sin(a)*.282,.6),.018,gold)
    # Each pane is a closed eight-vertex solid with nonzero thickness.
    verts=[]
    for inset in [0,.009]:
        for radius,z,ang in [(.183,.203,a),(.183,.203,b),(.275,.579,b),(.275,.579,a)]:
            verts.append(((radius-inset)*math.cos(ang),(radius-inset)*math.sin(ang),z))
    mesh_part('Solid glass pane '+str(i),verts,[(0,1,2,3),(7,6,5,4),(0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0)],glass)
    lathe('Brass glass rivet '+str(i),[(.56,.013),(.581,.013)],gold,8).location=(math.cos(a)*.272,math.sin(a)*.272,0)
lathe('Internal light core',[(.23,.038),(.27,.075),(.36,.073),(.47,.033),(.535,.007)],glow,12)
for i in range(8):
    vertices=[];faces=[]
    for a in range(16):
        u=2*math.pi*a/16
        for b in range(6):
            v=2*math.pi*b/6;r=.043+.009*math.cos(v)
            x,y,z=r*math.cos(u),.009*math.sin(v),r*math.sin(u)*1.4+1.02+i*.075
            vertices.append((x,y,z) if i%2==0 else (y,x,z))
    for a in range(16):
        for b in range(6):faces.append((a*6+b,((a+1)%16)*6+b,((a+1)%16)*6+(b+1)%6,a*6+(b+1)%6))
    mesh_part('Suspension chain link '+str(i),vertices,faces,gold)
lathe('Ceiling mounting boss',[(1.59,.025),(1.62,.065),(1.645,.12),(1.67,.12)],gold,12)
root=bpy.data.objects.new('Lantern | reusable complete prop',None);scene.collection.objects.link(root)
for o in parts:o.parent=root
root['reference']='source/multiview/interior-multiview.png';root['status']='Complete volumetric prop; source-image reconstruction replacement still requires per-view masking/placement.'
scene.view_layers[0].update()
# Export direct evaluated-free mesh data. The Blender 5.2 glTF operator crashes
# in BKE_base_eval_flags in this background MCP session; a separate trimesh pack
# preserves the same geometry/material values without that dependency-graph path.
raw=[]
for o in parts:
    o.data.calc_loop_triangles();m=o.data.materials[0];p=m.node_tree.nodes['Principled BSDF']
    raw.append({'name':o.name,'vertices':[list(o.matrix_basis@v.co) for v in o.data.vertices],
        'faces':[list(t.vertices) for t in o.data.loop_triangles],
        'color':list(p.inputs['Base Color'].default_value),
        'metallic':p.inputs['Metallic'].default_value,'roughness':p.inputs['Roughness'].default_value,
        'emission':p.inputs['Emission Strength'].default_value})
(OUT/'hanging-lantern-mesh.json').write_text(json.dumps(raw))

scene.render.engine='CYCLES';scene.cycles.samples=32
scene.render.resolution_x=900;scene.render.resolution_y=1000;scene.render.resolution_percentage=100
scene.world=bpy.data.worlds.new('Lantern inspection world');scene.world.use_nodes=True
scene.world.node_tree.nodes['Background'].inputs['Color'].default_value=(.16,.19,.25,1)
scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value=.55
for label,pos,energy,size in [('Key',(2,-3,4),400,3),('Rim',(-2,1,2.2),250,2)]:
    light=bpy.data.lights.new(label,'AREA');light.energy=energy;light.shape='DISK';light.size=size
    o=bpy.data.objects.new(label,light);scene.collection.objects.link(o);o.location=pos;o.rotation_euler=(Vector((0,0,.6))-o.location).to_track_quat('-Z','Y').to_euler()
for label,pos,target in [('Front',(1.9,-3,1.8),(0,0,.8)),('Rear',(-1.9,3,1.8),(0,0,.8)),('Underside',(1.7,-2,-.7),(0,0,.5))]:
    data=bpy.data.cameras.new(label);cam=bpy.data.objects.new(label,data);scene.collection.objects.link(cam)
    cam.location=pos;cam.rotation_euler=(Vector(target)-cam.location).to_track_quat('-Z','Y').to_euler();data.type='ORTHO';data.ortho_scale=2
    if label=='Front':scene.camera=cam
scene.view_layers[0].update()
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'Hanging lantern.blend'),compress=True)
report={'parts':len(parts),'vertices':sum(len(o.data.vertices) for o in parts),'geometry':'Closed volumetric components with solid panes and closed chain links','billboards':0,'reference':root['reference'],'runtime_integration':'Reusable asset exported; image-derived lights have not yet been removed/replaced in every view.'}
(OUT/'hanging-lantern.json').write_text(json.dumps(report,indent=2));result=report
