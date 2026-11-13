"""Run inside Blender. Keep the volumetric model separate from panorama studies."""
import bpy,math
from pathlib import Path
from mathutils import Vector
ROOT=Path(r'C:/Users/Shadow/Documents/Spyro 2_PC')
OUT=ROOT/'data/custom-levels/lanternfall'

def build():
    name='Lanternfall | Volumetric castle'
    if bpy.data.scenes.get(name):raise RuntimeError('Volumetric scene already exists; preserve it and create a named revision')
    scene=bpy.data.scenes.new(name)
    with bpy.context.temp_override(scene=scene,view_layer=scene.view_layers[0]):
        before=set(bpy.data.objects)
        bpy.ops.import_scene.gltf(filepath=str(OUT/'source/castle-editable.glb'))
        meshes=[o for o in set(bpy.data.objects)-before if o.type=='MESH']
        if len(meshes)!=1:raise RuntimeError('Expected one generated castle mesh')
        obj=meshes[0];obj.name='Castle | generated structural mesh';obj['s2_export']='mesh';obj['source_image']='source/castle-mesh-reference.png'
        bounds=[obj.matrix_world@Vector(c) for c in obj.bound_box]
        lo=Vector(tuple(min(v[i] for v in bounds) for i in range(3)));hi=Vector(tuple(max(v[i] for v in bounds) for i in range(3)))
        factor=32/max(hi.x-lo.x,hi.y-lo.y);obj.scale*=factor
        obj.location-=Vector(((lo.x+hi.x)/2,(lo.y+hi.y)/2,lo.z))*factor
        scene.view_layers[0].update()
        mat=bpy.data.materials.new('Castle clay | geometry inspection');mat.diffuse_color=(.58,.46,.3,1);obj.data.materials.clear();obj.data.materials.append(mat)
        scene.render.engine='BLENDER_WORKBENCH';scene.display.shading.light='STUDIO';scene.display.shading.color_type='MATERIAL';scene.display.shading.show_shadows=True;scene.display.shading.show_cavity=True
        scene.display.shading.cavity_type='BOTH';scene.display.shading.background_type='WORLD';scene.world=bpy.data.worlds.new('Lanternfall neutral studio');scene.world.color=(.035,.042,.055)
        scene.render.resolution_x=1200;scene.render.resolution_y=1000;scene.render.resolution_percentage=100
        for label,pos in [('Front',(32,-48,31)),('Back',(-32,48,31)),('Left',(-48,-32,25)),('Right',(48,32,25))]:
            camera=bpy.data.cameras.new(label);cam=bpy.data.objects.new(label,camera);scene.collection.objects.link(cam);cam.location=pos
            cam.rotation_euler=(Vector((0,0,12))-cam.location).to_track_quat('-Z','Y').to_euler();camera.type='ORTHO';camera.ortho_scale=46;camera.clip_end=500
            if label=='Front':scene.camera=cam
        scene['authoring_standard']='Everything depicted is editable mesh; no panorama shell in this scene.'
        scene['status']='Generated geometry inspection. Materials, interiors, collision and gameplay require validation.'
        bpy.data.libraries.write(str(OUT/'Lanternfall - Mesh.blend'),{scene},path_remap='RELATIVE',fake_user=True,compress=True)
        return {'scene':scene.name,'vertices':len(obj.data.vertices),'polygons':len(obj.data.polygons),'dimensions':list(obj.dimensions),'saved':str(OUT/'Lanternfall - Mesh.blend')}

result=build()
