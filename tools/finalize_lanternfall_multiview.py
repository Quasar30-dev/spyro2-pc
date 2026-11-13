import bpy
scene=bpy.data.scenes['Lanternfall | Multiview registration']
bpy.context.window.scene=scene
scene.render.resolution_x=1000;scene.render.resolution_y=1000;scene.render.resolution_percentage=100
scene.display.shading.color_type='TEXTURE';scene.display.shading.light='FLAT'
scene.camera=scene.objects['interior.southwest camera']
scene.render.filepath=r'C:/Users/Shadow/Documents/Spyro 2_PC/data/validation/lanternfall/multiview-interior.png'
bpy.ops.render.render(write_still=True,scene=scene.name)
bpy.ops.wm.save_as_mainfile(filepath=bpy.data.filepath,compress=True)
