"""Blender MCP: calibrated consistent views for the photogrammetry control.

Renders a copy of the existing hall scene without modifying the production file.
CONTROL_START and CONTROL_END allow bounded batches through the MCP server.
"""
import bpy,json,math
from pathlib import Path
from mathutils import Matrix,Vector
ROOT=Path(r'C:/Users/Shadow/Documents/Spyro 2_PC')
OUT=ROOT/'data/validation/photogrammetry-20260927';DEST=OUT/'control/images';DEST.mkdir(parents=True,exist_ok=True)
name='COLMAP | Consistent hall control'
scene=bpy.data.scenes.get(name)
if scene is None:
    scene=bpy.data.scenes['Lanternfall | Detail polish'].copy();scene.name=name
    scene.world=scene.world.copy()
    bg=scene.world.node_tree.nodes.get('Background')
    if bg:bg.inputs['Color'].default_value=(1,1,1,1);bg.inputs['Strength'].default_value=1
scene.render.engine='CYCLES';scene.cycles.samples=8;scene.cycles.use_denoising=False
scene.render.resolution_x=960;scene.render.resolution_y=720;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.render.image_settings.color_mode='RGB'
scene.view_settings.view_transform='Standard';scene.render.film_transparent=False
prefs=bpy.context.preferences.addons['cycles'].preferences;prefs.compute_device_type='CUDA';prefs.get_devices()
for d in prefs.devices:d.use=d.type=='CUDA'
scene.cycles.device='GPU';bpy.context.window.scene=scene
cameras=json.loads((ROOT/'data/custom-levels/lanternfall/complete-hall/cameras.json').read_text())
for y in [3,5,7,9,11]:
    for reverse in [False,True]:
        pos=Vector((.7 if reverse else -.7,18-y if reverse else y,1.8))
        target=Vector((0,1 if reverse else 17,3.0))
        matrix=(target-pos).to_track_quat('-Z','Y').to_matrix().to_4x4();matrix.translation=pos
        cameras.append({'id':f'{len(cameras)+1:02d} overlap','position':list(pos),'target':list(target),'horizontal_fov_degrees':83.97442327907126,'camera_to_world_blender':[list(r) for r in matrix]})
for i,c in enumerate(cameras):c['image']=f'{i+1:02d}.png';c['image_dimensions']=[960,720]
(OUT/'control/cameras.json').write_text(json.dumps(cameras,indent=2))
rendered=[]
for index in range(globals().get('CONTROL_START',0),min(globals().get('CONTROL_END',20),len(cameras))):
    c=cameras[index];target=DEST/c['image']
    if target.exists():continue
    data=bpy.data.cameras.new('COLMAP calibration');data.type='PERSP';data.lens=20;data.sensor_width=36;data.sensor_fit='HORIZONTAL'
    camera=bpy.data.objects.new('COLMAP view '+c['image'],data);scene.collection.objects.link(camera);camera.matrix_world=Matrix(c['camera_to_world_blender'])
    scene.camera=camera;scene.view_layers[0].update();scene.render.filepath=str(target);bpy.ops.render.render(write_still=True);rendered.append(c['image'])
result={'images':rendered,'output':str(DEST),'saved_production_file':False}
