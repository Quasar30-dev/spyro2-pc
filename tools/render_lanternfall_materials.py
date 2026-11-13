import bpy
from pathlib import Path
scene=bpy.data.scenes['Lanternfall | Reusable materials']
bpy.context.window.scene=scene
out=Path(r'C:/Users/Shadow/Documents/Spyro 2_PC/data/validation/lanternfall/materials.png')
scene.render.filepath=str(out)
bpy.ops.render.render(write_still=True,scene=scene.name)
bpy.ops.wm.save_as_mainfile(filepath=bpy.data.filepath,compress=True)
