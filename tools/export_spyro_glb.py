"""Spyro's retail idle mesh (with texture and vertex colours) as a GLB, for
Tripo Studio's auto-rigging and text-to-motion.
blender --background --factory-startup --python tools/export_spyro_glb.py -- <retail-meshes.json> <spyro.png> <out.glb>
The vertex order follows the retail model, so motion can be sampled back onto
it (see assets/actors/tripo/README.md)."""
import bpy, json, sys
args = sys.argv[sys.argv.index('--') + 1:]
data = json.load(open(args[0]))['spyro']
bpy.ops.wm.read_factory_settings(use_empty=True)
verts = [(v[0], v[2], v[1]) for v in data['vertices']]
faces = [tuple(reversed(f['indices'])) for f in data['faces']]   # Y/Z swap flips handedness
md = bpy.data.meshes.new('Spyro'); md.from_pydata(verts, [], faces); md.update()
uv = md.uv_layers.new(name='UVMap'); col = md.color_attributes.new(name='Color', type='BYTE_COLOR', domain='CORNER')
mat = bpy.data.materials.new('Spyro'); mat.use_nodes = True
n = mat.node_tree.nodes; links = mat.node_tree.links; bs = n.get('Principled BSDF'); bs.inputs['Roughness'].default_value = .9
im = n.new('ShaderNodeTexImage'); im.image = bpy.data.images.load(args[1]); im.interpolation = 'Closest'
links.new(im.outputs['Color'], bs.inputs['Base Color'])
md.materials.append(mat)
for face, p in zip(data['faces'], md.polygons):
    for j, li in enumerate(p.loop_indices):
        k = len(p.loop_indices) - 1 - j; u, v = face['uv'][k]; uv.data[li].uv = (u, 1 - v)
        col.data[li].color = (*[min(1, c) for c in face['colors'][k]], 1)
md.update()
o = bpy.data.objects.new('Spyro', md); bpy.context.scene.collection.objects.link(o)
bpy.ops.export_scene.gltf(filepath=args[2], export_format='GLB', export_vertex_color='ACTIVE', export_yup=True)
print('SPYRO VERTICES', len(verts), 'FACES', len(faces))
