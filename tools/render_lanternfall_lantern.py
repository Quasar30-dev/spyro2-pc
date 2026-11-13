import bpy
from pathlib import Path
out=Path(r'C:/Users/Shadow/Documents/Spyro 2_PC/data/validation/lanternfall/lantern')
out.mkdir(parents=True,exist_ok=True)
scene=bpy.data.scenes['Lanternfall | Hanging lantern mesh']
for label in ['Front','Rear','Underside']:
    scene.camera=scene.objects[label];scene.render.filepath=str(out/(label.lower()+'.png'))
    bpy.ops.render.render(write_still=True,scene=scene.name)
