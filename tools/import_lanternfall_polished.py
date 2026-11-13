"""Blender MCP: preserve base sheets and add a separate baked-detail UV/material."""
import bpy,bmesh,json
from pathlib import Path
ROOT=Path(r'C:/Users/Shadow/Documents/Spyro 2_PC');WORLD=ROOT/'data/custom-levels/lanternfall';OUT=WORLD/'complete-hall'
name='Lanternfall | Detail polish'
if bpy.data.scenes.get(name):raise RuntimeError('Preserve existing polish scene; choose a revision name')
base=bpy.data.scenes['Lanternfall | Complete hall'];scene=bpy.data.scenes.new(name);bpy.context.window.scene=scene
scene.world=base.world;scene.render.engine='CYCLES';scene.cycles.samples=16
scene.render.resolution_x=1200;scene.render.resolution_y=900;scene.render.resolution_percentage=100
scene.view_settings.view_transform='Standard'
atlas=bpy.data.images.load(str(OUT/'hall-polished-atlas.png'),check_existing=False);atlas.pack()
mat=bpy.data.materials.new('Lanternfall | Registered image detail');mat.use_nodes=True
tree=mat.node_tree;tree.nodes.clear();uvnode=tree.nodes.new('ShaderNodeUVMap');uvnode.uv_map='S2PhotoDetail'
tex=tree.nodes.new('ShaderNodeTexImage');tex.image=atlas;tex.extension='EXTEND'
color=tree.nodes.new('ShaderNodeVertexColor');color.layer_name='S2Lighting'
multiply=tree.nodes.new('ShaderNodeMixRGB');multiply.blend_type='MULTIPLY';multiply.inputs[0].default_value=1
emission=tree.nodes.new('ShaderNodeEmission');output=tree.nodes.new('ShaderNodeOutputMaterial')
for a,b in [(uvnode.outputs['UV'],tex.inputs['Vector']),(tex.outputs['Color'],multiply.inputs[1]),(color.outputs['Color'],multiply.inputs[2]),(multiply.outputs[0],emission.inputs['Color']),(emission.outputs[0],output.inputs['Surface'])]:tree.links.new(a,b)
materials={m['s2_material_id']:m for m in bpy.data.materials if 's2_material_id' in m}
records=json.loads((OUT/'geometry-polished.json').read_text());bakes={r['name']:r for r in json.loads((OUT/'hall-polished-uv.json').read_text())}
for record in records:
    bake=bakes[record['name']];md=bpy.data.meshes.new('Detail | '+record['name']);md.from_pydata(record['vertices'],[],record['faces']);md.update()
    assert len(md.polygons)==len(record['triangle_uvs'])==len(bake['triangle_uvs'])==len(bake['colors']),record['name']
    base_uv=md.uv_layers.new(name='S2Materials');detail_uv=md.uv_layers.new(name='S2PhotoDetail');light=md.color_attributes.new(name='S2Lighting',type='FLOAT_COLOR',domain='CORNER')
    for polygon,original,detail,rgb in zip(md.polygons,record['triangle_uvs'],bake['triangle_uvs'],bake['colors']):
        for li,a,b in zip(polygon.loop_indices,original,detail):
            base_uv.data[li].uv=a;detail_uv.data[li].uv=(b[0],1-b[1]);light.data[li].color=(*rgb,1)
        polygon.material_index=1
    md.materials.append(materials[record['material']]);md.materials.append(mat)
    bm=bmesh.new();bm.from_mesh(md);bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-6);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(md);bm.free()
    obj=bpy.data.objects.new(record['name'],md);scene.collection.objects.link(obj)
    obj['s2_material']=record['material'];obj['s2_collision']=record['collision'];obj['s2_color']=record['color'];obj['s2_source_uv']='S2Materials';obj['s2_detail_uv']='S2PhotoDetail'
    obj['s2_source_object']=record['name']
    obj['s2_detail_geometry']=record.get('detail_geometry',record.get('refinement','Original complete prop'))
for obj in base.objects:
    if obj.type in ['LIGHT','CAMERA']:
        copy=obj.copy();copy.data=obj.data.copy();scene.collection.objects.link(copy)
        if obj==base.camera:scene.camera=copy
scene['status']='Registered multi-view detail bake with independent reusable sheets, closed leaves and carved mouldings'
scene['texture_editing']='Material slot 0 + S2Materials retain reusable sheet mappings; slot 1 + S2PhotoDetail contain the separate image-detail bake.'
scene.view_layers[0].update();bpy.ops.wm.save_as_mainfile(filepath=str(WORLD/'Lanternfall - Detail polish.blend'),compress=True)
result={'scene':scene.name,'objects':len(records),'file':str(WORLD/'Lanternfall - Detail polish.blend')}
