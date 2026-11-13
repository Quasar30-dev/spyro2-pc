"""Run through Blender MCP. Only writes this experiment's new scene."""
import bpy, json, math
from pathlib import Path
from mathutils import Vector
ROOT=Path(r'C:/Users/Shadow/Documents/Spyro 2_PC')
OUT=ROOT/'data/custom-levels/lanternfall'

def build():
    name='Lanternfall | MoGe geometry studies r2'
    if bpy.data.scenes.get(name):raise RuntimeError('Preserve existing study; create a named revision')
    scene=bpy.data.scenes.new(name)
    scene.render.engine='CYCLES';scene.cycles.samples=8
    scene.render.resolution_x=1024;scene.render.resolution_y=1024;scene.render.resolution_percentage=100
    scene.view_settings.view_transform='Standard'
    scene.world=bpy.data.worlds.new('MoGe study neutral background');scene.world.use_nodes=True
    scene.world.node_tree.nodes['Background'].inputs['Color'].default_value=(.035,.045,.07,1)
    scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value=.3
    modules=[]
    with bpy.context.temp_override(scene=scene,view_layer=scene.view_layers[0]):
        for index,path in enumerate(sorted((OUT/'moge').glob('*/*/mesh.glb'))):
            report=json.loads((path.parent/'report.json').read_text())
            label=path.parent.parent.name+' | '+path.parent.name
            offset=Vector((index*600,0,0));before=set(bpy.data.objects)
            bpy.ops.import_scene.gltf(filepath=str(path))
            imported=set(bpy.data.objects)-before
            for obj in imported:
                if not obj.parent:obj.location+=offset
                if obj.type!='MESH':continue
                obj.name=label+' | visible surface';obj['s2_export']='mesh';obj['moge_report']=str(path.parent/'report.json')
                obj['reconstruction_status']='Visible surface only; incomplete behind occluders.'
                # Source images already contain lighting. Emission matches native unlit
                # texture inspection without adding a second set of shadows.
                for mat in obj.data.materials:
                    if not mat or not mat.use_nodes:continue
                    tree=mat.node_tree;tex=next(n for n in tree.nodes if n.type=='TEX_IMAGE')
                    output=next(n for n in tree.nodes if n.type=='OUTPUT_MATERIAL')
                    emission=tree.nodes.new('ShaderNodeEmission')
                    tree.links.new(tex.outputs['Color'],emission.inputs['Color'])
                    tree.links.new(emission.outputs[0],output.inputs['Surface'])
                    tex.image.pack()
            for camera_name,local_eye,local_target in [('source',(0,0,1.6),(0,8,1.6)),('offset',(3,-1,2.5),(0,8,1.6))]:
                data=bpy.data.cameras.new(label+' | '+camera_name);cam=bpy.data.objects.new(data.name,data);scene.collection.objects.link(cam)
                cam.location=offset+Vector(local_eye);cam.rotation_euler=(offset+Vector(local_target)-cam.location).to_track_quat('-Z','Y').to_euler()
                data.type='PERSP';data.sensor_fit='HORIZONTAL';data.angle=math.radians(report['fov_x']);data.clip_end=400
                if scene.camera is None:scene.camera=cam
            modules.append({'label':label,'triangles':report['triangles'],'fov':report['fov_x']})
        scene['status']='MoGe-2 GPU comparison: estimated vs known perspective. These are not finished volumetric assets.'
        dest=OUT/'Lanternfall - MoGe.blend'
        bpy.data.libraries.write(str(dest),{scene},path_remap='RELATIVE',fake_user=True,compress=True)
    return {'scene':scene.name,'modules':modules,'saved':str(dest)}

result=build()
