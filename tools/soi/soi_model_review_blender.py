"""Import actual local donor geometry into editable Blender and render review views.

Run blender --background --python THIS -- EXPORT_DIRECTORY. Original numeric
clip IDs are retained without invented semantics. This is not in-game proof.
"""
import bpy
import json
import math
from pathlib import Path
import sys
from mathutils import Vector


def main():
    source = Path(sys.argv[sys.argv.index('--') + 1]).resolve()
    data = json.loads((source/'models.json').read_text())
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    atlas = bpy.data.images.load(str(source/'atlas.png')); atlas.pack()
    materials = []
    for textured in (False, True):
        mat = bpy.data.materials.new('Original atlas x source corner colours' if textured else 'Original corner colours')
        mat.use_nodes = True; tree = mat.node_tree; tree.nodes.clear()
        colour = tree.nodes.new('ShaderNodeVertexColor'); colour.layer_name = 'SourceColours'
        output = tree.nodes.new('ShaderNodeOutputMaterial'); emit = tree.nodes.new('ShaderNodeEmission')
        tree.links.new(emit.outputs[0], output.inputs['Surface'])
        if textured:
            tex = tree.nodes.new('ShaderNodeTexImage'); tex.image = atlas; tex.interpolation = 'Closest'
            mix = tree.nodes.new('ShaderNodeMixRGB'); mix.blend_type = 'MULTIPLY'; mix.inputs[0].default_value = 1
            tree.links.new(colour.outputs['Color'], mix.inputs[1]); tree.links.new(tex.outputs['Color'], mix.inputs[2])
            tree.links.new(mix.outputs[0], emit.inputs[0])
        else:
            tree.links.new(colour.outputs['Color'], emit.inputs[0])
        materials.append(mat)
    scene = bpy.context.scene; scene.render.engine = 'BLENDER_EEVEE'
    scene.render.resolution_x = 480; scene.render.resolution_y = 480; scene.render.resolution_percentage = 100
    scene.world.color = (.025, .03, .045)
    scene.view_settings.view_transform = 'Standard'
    camera_data = bpy.data.cameras.new('Review orthographic'); camera_data.type = 'ORTHO'
    camera = bpy.data.objects.new('Review camera', camera_data); scene.collection.objects.link(camera); scene.camera = camera
    objects = []
    for model in data['models']:
        clip = model['clips'][0] if model['clips'] else None
        frame = clip['frames'][0] if clip else None
        mesh = model['meshes'][frame['mesh'] if frame else 0]
        vertices = frame['vertices'] if frame else mesh['vertices']
        faces = mesh['faces']
        geometry = bpy.data.meshes.new(f"S{data['slot']}_{model['class']:03x}")
        order = lambda face: [0, 1, 3, 2] if len(face['vertices']) == 4 else [0, 1, 2]
        geometry.from_pydata([(x, -z, y) for x, y, z in vertices], [],
                             [[face['vertices'][i] for i in order(face)] for face in faces])
        geometry.update()
        obj = bpy.data.objects.new(geometry.name, geometry); scene.collection.objects.link(obj); objects.append(obj)
        obj['source_slot'] = data['slot']; obj['source_class'] = model['class']; obj['source_clips'] = len(model['clips'])
        obj['source_data'] = str(source/'models.json'); obj['limits'] = 'Original source geometry, first numeric clip/frame only; no semantic or engine integration claim'
        for mat in materials: geometry.materials.append(mat)
        uv = geometry.uv_layers.new(name='Original atlas coordinates')
        colour = geometry.color_attributes.new(name='SourceColours', type='FLOAT_COLOR', domain='CORNER')
        for poly, face in zip(geometry.polygons, faces):
            poly.material_index = int(face['textured']); poly.use_smooth = False
            for loop, corner in zip(poly.loop_indices, order(face)):
                u, v = face['uv'][corner]; uv.data[loop].uv = (u, 1 - v)
                rgb = face['colours'][corner]
                # Use linear values for an sRGB-like source colour swatch.
                lin = lambda x: x/12.92 if x <= .04045 else ((x + .055)/1.055)**2.4
                colour.data[loop].color = tuple(lin(max(0., min(1., c))) for c in rgb) + (1.,)
        bounds = [Vector(v) for v in geometry.vertices.foreach_get.__self__.vertices] if False else [Vector(v.co) for v in geometry.vertices]
        low = Vector(tuple(min(v[i] for v in bounds) for i in range(3)))
        high = Vector(tuple(max(v[i] for v in bounds) for i in range(3)))
        centre = (low + high)/2; size = max(high - low)
        for other in objects: other.hide_render = other != obj
        for label, offset in [('front', (0, -3, 1)), ('oblique', (2, -3, 1.1)), ('reverse', (0, 3, 1))]:
            camera.location = centre + Vector(offset)*max(size, .1)
            camera.rotation_euler = (centre - camera.location).to_track_quat('-Z', 'Y').to_euler()
            camera_data.ortho_scale = max(size*1.3, .1)
            scene.render.filepath = str(source/f"class-{model['class']:03x}-{label}.png")
            bpy.ops.render.render(write_still=True)
    for obj in objects: obj.hide_render = False; obj.location.x += objects.index(obj)*5
    bpy.ops.wm.save_as_mainfile(filepath=str(source/'original-donors-review.blend'), compress=True)
    print('ARTIFACT original source mesh review', source, 'numeric first clips only; no in-game/animation completion claim', flush=True)


if __name__ == '__main__':
    main()
