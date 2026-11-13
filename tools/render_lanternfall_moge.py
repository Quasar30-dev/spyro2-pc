import bpy
from pathlib import Path
out=Path(r'C:/Users/Shadow/Documents/Spyro 2_PC/data/validation/lanternfall/moge')
out.mkdir(parents=True,exist_ok=True)
scene=bpy.data.scenes['Lanternfall | MoGe geometry studies r2']
bpy.context.window.scene=scene
for module in ['exterior-splats-front','exterior-splats-left','interior-splats-front']:
    for view in ['source','offset']:
        scene.camera=next(o for o in scene.objects if o.type=='CAMERA' and o.name.startswith(module+' | known | '+view))
        scene.render.filepath=str(out/(module+'-'+view+'.png'))
        bpy.ops.render.render(write_still=True,scene=scene.name)
scene.camera=next(o for o in scene.objects if o.type=='CAMERA' and o.name.startswith('exterior-splats-front | known | source'))
bpy.ops.wm.save_as_mainfile(filepath=bpy.data.filepath,compress=True)
