"""Render every calibrated source camera with a clay material override."""
import bpy,sys
from pathlib import Path
ROOT=Path(r'C:/Users/Shadow/Documents/Spyro 2_PC');WORLD=ROOT/'data/custom-levels/lanternfall'
out=WORLD/'complete-hall/blockout-views';out.mkdir(exist_ok=True)
scene=bpy.data.scenes['Lanternfall | Complete hall'];bpy.context.window.scene=scene
# Lights must not appear as black rectangles through the open windows.
for obj in scene.objects:
    if obj.type=='LIGHT':obj.visible_camera=False
prefs=bpy.context.preferences.addons['cycles'].preferences
try:
    prefs.compute_device_type='CUDA';prefs.get_devices()
    for device in prefs.devices:device.use=device.type=='CUDA'
    if any(d.use for d in prefs.devices):scene.cycles.device='GPU'
except Exception as error:print('Using CPU rendering:',error)
clay=bpy.data.materials.new('Untextured blockout clay');clay.use_nodes=True
clay.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value=(.48,.48,.48,1)
clay.node_tree.nodes['Principled BSDF'].inputs['Roughness'].default_value=.85
scene.view_layers[0].material_override=clay;scene.cycles.samples=12
for camera in sorted((o for o in scene.objects if o.type=='CAMERA'),key=lambda o:o.name):
    if '--floor-only' in sys.argv and camera.name!='10 Floor':continue
    scene.camera=camera;scene.render.filepath=str(out/(camera.name+'.png'));bpy.ops.render.render(write_still=True)
scene.view_layers[0].material_override=None
if '--floor-only' not in sys.argv:
    scene.camera=scene.objects['01 South to north'];scene.render.filepath=str(WORLD/'complete-hall/textured-base.png');bpy.ops.render.render(write_still=True)
