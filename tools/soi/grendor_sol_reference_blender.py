"""Hand-authored Grendor reference master/three LODs; run in headless Blender.

Uses corrected original SoI reference palettes, not a generated image/mesh.
This is an editable authoring checkpoint, not a PS1 character import. Neutral
rig only: semantic clips, source-arena scale and animation are not claimed.
"""
import bpy
import bmesh
import math
import json
import hashlib
from pathlib import Path
from mathutils import Vector

ROOT = Path('C:/Users/Shadow/Documents/Spyro 2_PC')
OUT = ROOT/'data/soi/content-20261105/coordinator-grendor'
OUT.mkdir(parents=True,exist_ok=True)
PALETTE = [(255,0,255),(230,246,255),(156,205,246),(115,180,222),
           (90,148,172),(82,115,148),(65,98,106),(41,139,24),
           (41,172,32),(74,238,49),(213,172,0),(255,230,8),
           (255,74,82),(222,57,57),(164,32,32),(74,8,8)]
NAMES = ['transparent','horn_eye','ice_light','ice','ice_shadow','ice_deep','outline',
         'green_dark','green','green_belly','gold_dark','gold','red_light','red','red_dark','mouth']
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
materials=[]
atlas=bpy.data.images.new('Grendor original palette11 authoring atlas',width=128,height=128,alpha=True)
pixels=[]
for y in range(128):
    for x in range(128):
        idx=(y//32)*4+x//32
        pixels.extend([v/255 for v in PALETTE[idx]]+[0 if idx==0 else 1])
atlas.pixels=pixels;atlas.filepath_raw=str(OUT/'grendor-palette11.png');atlas.file_format='PNG';atlas.save()
for i,(name,c) in enumerate(zip(NAMES,PALETTE)):
    mat=bpy.data.materials.new(name)
    linear=lambda v:(v/255/12.92 if v/255<=.04045 else ((v/255+.055)/1.055)**2.4)
    mat.diffuse_color=tuple(linear(v) for v in c)+(1,)
    mat.use_nodes=True;n=mat.node_tree.nodes.get('Principled BSDF');n.inputs['Base Color'].default_value=mat.diffuse_color
    n.inputs['Roughness'].default_value=1
    materials.append(mat)


def mesh(name,verts,faces,material,bone,parts,face_materials=None):
    data=bpy.data.meshes.new(name);data.from_pydata(verts,[],faces);data.update()
    obj=bpy.data.objects.new(name,data);bpy.context.collection.objects.link(obj)
    for m in materials:data.materials.append(m)
    uv=data.uv_layers.new(name='OriginalPaletteAtlas')
    detail=data.attributes.new('authoring_detail','INT','FACE')
    for poly in data.polygons:
        mi=material if face_materials is None else face_materials[poly.index]
        poly.material_index=mi;poly.use_smooth=False
        detail.data[poly.index].value=(3 if name.startswith(('pupil','mouth')) else
                                      2 if name.startswith('eye') else 1 if 'wand' in name else 0)
        for j,li in enumerate(poly.loop_indices):
            angle=2*math.pi*j/len(poly.loop_indices)
            uv.data[li].uv=((mi%4*32+16+10*math.cos(angle))/128,
                            (mi//4*32+16+10*math.sin(angle))/128)
    group=obj.vertex_groups.new(name=bone);group.add(list(range(len(verts))),1,'REPLACE')
    parts.append(obj);return obj


def ellipsoid(name,centre,radius,n,rings,mat,bone,parts,body=False):
    cx,cy,cz=centre;rx,ry,rz=radius
    verts=[(cx,cy,cz-rz)]
    for k in range(1,rings):
        a=math.pi*k/rings
        for j in range(n):
            t=2*math.pi*j/n
            verts.append((cx+rx*math.sin(a)*math.cos(t),cy+ry*math.sin(a)*math.sin(t),cz-rz*math.cos(a)))
    verts.append((cx,cy,cz+rz));top=len(verts)-1
    faces=[]
    for j in range(n):faces.append((0,1+(j+1)%n,1+j))
    for k in range(rings-2):
        for j in range(n):
            a=1+k*n+j;b=1+k*n+(j+1)%n
            faces.append((a,b,b+n,a+n))
    for j in range(n):faces.append((top,1+(rings-2)*n+j,1+(rings-2)*n+(j+1)%n))
    ids=[]
    for f in faces:
        x=sum(verts[i][0] for i in f)/len(f);y=sum(verts[i][1] for i in f)/len(f)
        if body:ids.append((12 if y<-.15 else 13) if x>0 else (9 if y<-.15 else 8))
        else:ids.append(mat)
    obj=mesh(name,verts,faces,mat,bone,parts,ids)
    for p in obj.data.polygons:p.use_smooth=True
    return obj


def rod(name,start,end,radius,n,mat,bone,parts,cone=False):
    a,b=Vector(start),Vector(end);axis=(b-a).normalized()
    u=axis.cross(Vector((0,0,1)))
    if u.length<.1:u=axis.cross(Vector((0,1,0)))
    u.normalize();v=axis.cross(u)
    verts=[tuple(a+radius*(math.cos(2*math.pi*j/n)*u+math.sin(2*math.pi*j/n)*v)) for j in range(n)]
    if cone:
        verts.append(tuple(b));faces=[tuple(reversed(range(n)))]+[(j,(j+1)%n,n) for j in range(n)]
    else:
        verts.extend(tuple(b+radius*(math.cos(2*math.pi*j/n)*u+math.sin(2*math.pi*j/n)*v)) for j in range(n))
        faces=[tuple(reversed(range(n))),tuple(range(n,2*n))]+[(j,(j+1)%n,(j+1)%n+n,j+n) for j in range(n)]
    return mesh(name,verts,faces,mat,bone,parts)


def build(name,master,lod=0):
    parts=[];n=16 if master else (8 if lod==0 else 6 if lod==1 else 5)
    rings=8 if master else (4 if lod==0 else 3)
    limb_n=10 if master else (5 if lod==0 else 4 if lod==1 else 3)
    ellipsoid('two-tone rounded torso',(0,.02,1.3),(.82,.55,1.0),n,rings,8,'body',parts,True)
    for side in (-1,1):
        suffix='L' if side<0 else 'R';mat=8 if side<0 else 13
        ellipsoid('leg '+suffix,(side*.39,.03,.36),(.25,.28,.38),10 if master else (4 if lod<2 else 3),4 if master else 2,mat,'leg_'+suffix,parts)
        ellipsoid('foot '+suffix,(side*.4,-.2,.14),(.31,.43,.15),limb_n,4 if master else 2,mat,'leg_'+suffix,parts)
        ellipsoid('head '+suffix,(side*.43,-.04,2.46),(.47,.4,.5),14 if master else (6 if lod==0 else 4),7 if master else (4 if lod==0 else 3 if lod==1 else 2),mat,'head_'+suffix,parts)
        ellipsoid('projecting muzzle '+suffix,(side*.46,-.43,2.35),(.31,.35,.24),12 if master else (5 if lod==0 else 4 if lod==1 else 3),6 if master else (3 if lod==0 else 2),mat,'head_'+suffix,parts)
        rod('ice horn '+suffix,(side*.43,-.19,2.86),(side*.49,-.55,3.36),.17,10 if master else (4 if lod<2 else 3),1,'head_'+suffix,parts,True)
        ellipsoid('sleeve '+suffix,(side*.89,-.03,1.76),(.25,.25,.43),limb_n,5 if master else 2,mat,'arm_'+suffix,parts)
        ellipsoid('ice glove '+suffix,(side*1.03,-.23,1.46),(.28,.27,.25),limb_n,5 if master else 2,2,'arm_'+suffix,parts)
        for eye in ((-1,1) if lod<2 else ()):
            x=side*.43+eye*.16
            def face_point(xx,z,offset=.012):
                y=-.04-.4*math.sqrt(max(.05,1-((xx-side*.43)/.47)**2-((z-2.46)/.5)**2))-offset
                return xx,y,z
            verts=[face_point(x-.105,2.58),face_point(x+.105,2.58),
                   face_point(x+.095,2.74-eye*.03),face_point(x-.095,2.74+eye*.03)]
            mesh('eye '+suffix+str(eye),verts,[(0,1,2,3)],11,'head_'+suffix,parts)
            verts=[face_point(x-.035,2.61,.028),face_point(x+.045,2.61,.028),face_point(x+.018,2.71,.028)]
            if lod==0:mesh('pupil '+suffix+str(eye),verts,[(0,1,2)],15,'head_'+suffix,parts)
        x=side*.46
        if lod==0:mesh('mouth '+suffix,[(x-.18,-.70,2.24),(x+.18,-.70,2.24),(x+.16,-.69,2.31),(x-.16,-.69,2.31)],[(0,1,2,3)],15,'head_'+suffix,parts)
    # Piecewise planar cloth strips clear the entire rounded torso. A single
    # nonplanar ngon allowed its fan triangulation to cut through the body.
    front=[(-.82,.43,2.38),(.82,.43,2.38),(-1.04,.82,1.35),
           (1.04,.82,1.35),(-.62,.82,.43),(.62,.82,.43)]
    verts=front+[(x,y+.04,z) for x,y,z in front]
    faces=[(0,2,3,1),(2,4,5,3),(6,7,9,8),(8,9,11,10)]
    outline=(0,1,3,5,4,2)
    faces.extend((j,outline[(i+1)%6],outline[(i+1)%6]+6,j+6) for i,j in enumerate(outline))
    mesh('closed ice-blue cape',verts,faces,3,'cape',parts)
    if lod<2:rod('gold wand',(1.05,-.26,1.40),(1.20,-.36,2.48),.035,8 if master else 3,10,'arm_R',parts)
    verts=[]
    for y in (-.39,-.35):
        for j in range(10):
            angle=math.pi/2+2*math.pi*j/10;rad=.27 if j%2==0 else .11
            verts.append((1.20+rad*math.cos(angle),y,2.59+rad*math.sin(angle)))
    faces=[tuple(reversed(range(10))),tuple(range(10,20))]+[(j,(j+1)%10,(j+1)%10+10,j+10) for j in range(10)]
    if lod<2:mesh('solid five-point wand star',verts,faces,11,'arm_R',parts)
    bpy.ops.object.select_all(action='DESELECT')
    for p in parts:p.select_set(True)
    bpy.context.view_layer.objects.active=parts[0];bpy.ops.object.join()
    result=parts[0];result.name=name;result.data.name=name+' editable mesh'
    return result


master=build('Grendor_MASTER',True)
lod0=build('Grendor_LOD0',False)
assert len(lod0.data.vertices)<=244,('original Crush budget',len(lod0.data.vertices))
objects=[master,lod0]
for name,target,lod in [('Grendor_LOD1',160,1),('Grendor_LOD2',100,2)]:
    obj=build(name,False,lod)
    assert len(obj.data.vertices)<=target,(name,len(obj.data.vertices),target)
    objects.append(obj)

arm=bpy.data.armatures.new('Grendor neutral authoring skeleton');rig=bpy.data.objects.new('Grendor_RIG',arm)
bpy.context.collection.objects.link(rig);bpy.context.view_layer.objects.active=rig;rig.select_set(True)
bpy.ops.object.mode_set(mode='EDIT')
bone_specs={'root':((0,0,0),(0,0,.3),None),'body':((0,0,.4),(0,0,2.2),'root'),
            'head_L':((-.43,0,2.1),(-.43,0,2.95),'body'),'head_R':((.43,0,2.1),(.43,0,2.95),'body'),
            'arm_L':((-.63,0,2.0),(-1.03,-.23,1.46),'body'),'arm_R':((.63,0,2.0),(1.03,-.23,1.46),'body'),
            'leg_L':((-.39,0,.65),(-.4,-.2,.14),'root'),'leg_R':((.39,0,.65),(.4,-.2,.14),'root'),
            'cape':((0,.2,2.35),(0,.7,.65),'body')}
for name,(head,tail,parent) in bone_specs.items():
    b=arm.edit_bones.new(name);b.head=head;b.tail=tail
    if parent:b.parent=arm.edit_bones[parent]
bpy.ops.object.mode_set(mode='OBJECT')
rig.show_in_front=True
counts={}
for obj in objects:
    bm=bmesh.new();bm.from_mesh(obj.data)
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    bm.to_mesh(obj.data);bm.free()
    obj.parent=rig;mod=obj.modifiers.new('neutral skeleton','ARMATURE');mod.object=rig
    obj['source']='Original SoI grendor bank08738888; bounded ARM reference frames'
    obj['status']='AUTHORING REVIEW ONLY; no game import or semantic animations'
    counts[obj.name]={'vertices':len(obj.data.vertices),'polygons':len(obj.data.polygons),
                      'triangles':sum(len(p.vertices)-2 for p in obj.data.polygons),'pivot':list(obj.location)}
    data={'name':obj.name,'positions':[list(v.co) for v in obj.data.vertices],
          'faces':[list(p.vertices) for p in obj.data.polygons],
          'materials':[p.material_index for p in obj.data.polygons],
          'uv_per_loop':[list(x.uv) for x in obj.data.uv_layers.active.data],
          'bone_weights':[[[obj.vertex_groups[g.group].name,g.weight] for g in v.groups] for v in obj.data.vertices]}
    (OUT/(obj.name+'.json')).write_text(json.dumps(data,separators=(',',':')))

scene=bpy.context.scene;scene.render.engine='BLENDER_WORKBENCH'
scene.view_settings.view_transform='Standard'
scene.display.shading.light='STUDIO';scene.display.shading.studiolight_rotate_z=.5
scene.display.shading.color_type='MATERIAL';scene.display.shading.show_shadows=True
scene.display.shading.show_cavity=True;scene.display.shading.background_type='WORLD'
scene.world.color=(.055,.065,.085)
scene.render.resolution_x=768;scene.render.resolution_y=768;scene.render.resolution_percentage=100
cameras=[]
for tag,pos in [('front',(0,-7,2.3)),('side',(7,0,2.3)),('rear',(0,7,2.3)),('oblique',(5,-7,4.2))]:
    data=bpy.data.cameras.new('Grendor '+tag);cam=bpy.data.objects.new('Grendor '+tag,data)
    bpy.context.collection.objects.link(cam);cam.location=pos
    cam.rotation_euler=(Vector((0,0,1.7))-cam.location).to_track_quat('-Z','Y').to_euler()
    data.type='ORTHO';data.ortho_scale=4.2
    cameras.append((tag,cam))
    for obj in (master,lod0):
        for other in objects:other.hide_render=other!=obj;other.hide_viewport=other!=obj
        scene.camera=cam;scene.render.filepath=str(OUT/(obj.name+'-'+tag+'.png'))
        bpy.ops.render.render(write_still=True)
for obj in objects:obj.hide_render=obj!=lod0;obj.hide_viewport=obj!=lod0
scene.camera=cameras[-1][1]
manifest={'status':'editable Blender reference master +3 LODs; NOT integrated',
          'authoring_script_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
          'source_reference_manifest':str(ROOT/'data/soi/content-20261105/coordinator-sprite-oracle/grendor-final/original-arm-manifest.json'),
          'source_boss_budgets':str(ROOT/'data/soi/content-20261105/coordinator-boss-budgets.json'),
          'counts':counts,'palette':PALETTE,'bones':list(bone_specs),
          'cameras':[{'name':tag,'position':list(cam.location),'rotation_euler':list(cam.rotation_euler),'orthographic_scale':cam.data.ortho_scale} for tag,cam in cameras],
          'limits':['Best-effort handmade shape adaptation; reference pose and silhouette fidelity await review.',
                    'Neutral rig only; no guessed animation states. Source clip/direction binding, attacks/hurt/defeat, PS1 packing, game load and runtime verification remain unfinished.',
                    'Scale3.36 authoring units is provisional; shared foot pivot and scale retained across all LODs. Grendor belongs in source arenas15/16, not Market Mesa.']}
(OUT/'grendor-authoring-manifest.json').write_text(json.dumps(manifest,indent=2))
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'grendor-master-three-lods.blend'))
print('GRENDOR AUTHORING ARTIFACT',OUT/'grendor-master-three-lods.blend')
print('COUNTS',json.dumps(counts))
print('LIMIT',json.dumps(manifest['limits']))
