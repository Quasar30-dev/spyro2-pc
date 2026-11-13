"""Import independent MoGe views for registration; do not pretend they are fused."""
import bpy,json,math
from pathlib import Path
from mathutils import Vector
ROOT=Path(r'C:/Users/Shadow/Documents/Spyro 2_PC')
WORLD=ROOT/'data/custom-levels/lanternfall'
name='Lanternfall | Multiview registration'
if bpy.data.scenes.get(name):raise RuntimeError('Preserve existing registration work')
scene=bpy.data.scenes.new(name)
scene.render.engine='BLENDER_WORKBENCH';scene.display.shading.color_type='TEXTURE'
scene['registration_status']='Unregistered independent camera-space meshes. Use landmarks/pose estimation before fusion.'
with bpy.context.temp_override(scene=scene,view_layer=scene.view_layers[0]):
    for i,path in enumerate(sorted((WORLD/'moge-multiview').glob('*/mesh.glb'))):
        report=json.loads((path.parent/'report.json').read_text());before=set(bpy.data.objects)
        bpy.ops.import_scene.gltf(filepath=str(path))
        offset=Vector((i*200,0,0))
        for obj in set(bpy.data.objects)-before:
            if not obj.parent:obj.location+=offset
            if obj.type=='MESH':
                obj.name=path.parent.name;obj['registration_status']='UNREGISTERED';obj['moge_report']=str(path.parent/'report.json')
                for mat in obj.data.materials:
                    if mat and mat.use_nodes:
                        for node in mat.node_tree.nodes:
                            if node.type=='TEX_IMAGE' and node.image:node.image.pack()
        camera=bpy.data.cameras.new(path.parent.name+' camera');cam=bpy.data.objects.new(camera.name,camera);scene.collection.objects.link(cam)
        cam.location=offset+Vector((0,0,1.6));cam.rotation_euler=Vector((0,1,0)).to_track_quat('-Z','Y').to_euler()
        camera.sensor_fit='HORIZONTAL';camera.angle=math.radians(report['fov_x'])
        if scene.camera is None:scene.camera=cam
    scene.view_layers[0].update()
    bpy.data.libraries.write(str(WORLD/'Lanternfall - Multiview.blend'),{scene},path_remap='RELATIVE',fake_user=True,compress=True)
result={'scene':scene.name,'views':sum(o.type=='MESH' for o in scene.objects)}
