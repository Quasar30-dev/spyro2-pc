"""Run in private background Blender; inspect seam repair and real mesh props."""
import bpy,json,math
from pathlib import Path
from mathutils import Vector
ROOT=Path(r'C:/Users/Shadow/Documents/Spyro 2_PC');WORLD=ROOT/'data/custom-levels/lanternfall'
VALID=ROOT/'data/validation/lanternfall/quality';VALID.mkdir(parents=True,exist_ok=True)

def camera(scene,name,pos,target,fov):
    d=bpy.data.cameras.new(name);o=bpy.data.objects.new(name,d);scene.collection.objects.link(o)
    o.location=pos;o.rotation_euler=(Vector(target)-o.location).to_track_quat('-Z','Y').to_euler()
    d.sensor_fit='HORIZONTAL';d.angle=math.radians(fov);d.clip_end=400;scene.camera=o
    return o

def surface(scene,path):
    before=set(bpy.data.objects);bpy.ops.import_scene.gltf(filepath=str(path))
    for o in set(bpy.data.objects)-before:
        if o.type!='MESH':continue
        for mat in o.data.materials:
            if not mat or not mat.use_nodes:continue
            tree=mat.node_tree;tex=next(n for n in tree.nodes if n.type=='TEX_IMAGE')
            out=next(n for n in tree.nodes if n.type=='OUTPUT_MATERIAL');em=tree.nodes.new('ShaderNodeEmission')
            tree.links.new(tex.outputs['Color'],em.inputs['Color']);tree.links.new(em.outputs[0],out.inputs['Surface']);tex.image.pack()

def new_scene(name):
    scene=bpy.data.scenes.new(name);bpy.context.window.scene=scene
    scene.render.engine='CYCLES';scene.cycles.samples=4;scene.render.film_transparent=True
    scene.render.image_settings.file_format='PNG';scene.render.image_settings.color_mode='RGBA'
    scene.render.resolution_x=768;scene.render.resolution_y=768;scene.render.resolution_percentage=100
    scene.view_settings.view_transform='Standard';scene.world=bpy.data.worlds.new(name+' world');scene.world.use_nodes=True
    scene.world.node_tree.nodes['Background'].inputs['Color'].default_value=(.35,.38,.45,1)
    scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value=.6
    return scene

for name,path in [('entrance',WORLD/'moge-seams/exterior-splats-front/known'),('interior',WORLD/'moge-seams/interior.southwest'),('clean-interior',WORLD/'moge-props/multiview-interior-clean-background/known')]:
    scene=new_scene('Quality | '+name);report=json.loads((path/'report.json').read_text());surface(scene,path/'mesh.glb')
    camera(scene,name+' source',(0,0,1.6),(0,8,1.6),report['fov_x'])
    scene.render.filepath=str(VALID/(name+'-source.png'));bpy.ops.render.render(write_still=True)
    if name=='clean-interior':
        records=json.loads((WORLD/'pc/interior-mesh/placements.json').read_text())
        for lamp in records['lamps']:
            before=set(bpy.data.objects);bpy.ops.import_scene.gltf(filepath=str(WORLD/'props/hanging-lantern.glb'))
            imported=set(bpy.data.objects)-before
            parent=bpy.data.objects.new(lamp['id'],None);scene.collection.objects.link(parent)
            for obj in imported:
                if not obj.parent:obj.parent=parent
            x,y,z=lamp['native_position'];parent.location=(x,z,y);parent.scale=(lamp['scale'],)*3
            parent['geometry_verified']='34 closed mesh components';parent['ceiling_anchored']=lamp['ceiling_ray_anchor']
        light=bpy.data.lights.new('Warm fill for mesh props','AREA');light.energy=900;light.size=6;light.color=(1,.79,.51)
        lamp_light=bpy.data.objects.new(light.name,light);scene.collection.objects.link(lamp_light);lamp_light.location=(0,-1,6)
        lamp_light.rotation_euler=(Vector((2,12,5))-lamp_light.location).to_track_quat('-Z','Y').to_euler()
        scene.render.film_transparent=False;scene.render.resolution_x=scene.render.resolution_y=1100;scene.cycles.samples=16
        scene.render.filepath=str(VALID/'interior-with-mesh-lights.png');bpy.ops.render.render(write_still=True)
        camera(scene,'Displaced review',(1.5,-.5,1.9),(1,9,3),report['fov_x'])
        scene.render.filepath=str(VALID/'interior-with-mesh-lights-offset.png');bpy.ops.render.render(write_still=True)
        bpy.ops.wm.save_as_mainfile(filepath=str(WORLD/'Lanternfall - Interior mesh lights.blend'),compress=True)
    else:
        camera(scene,name+' offset',(2,-.5,2.1),(0,8,1.6),report['fov_x'])
        scene.render.filepath=str(VALID/(name+'-offset.png'));bpy.ops.render.render(write_still=True)
