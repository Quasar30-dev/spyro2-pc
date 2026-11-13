"""Blender MCP: editable refined prefab, retaining independent atlas materials."""
import bpy,bmesh,json
from pathlib import Path
ROOT=Path(r'C:/Users/Shadow/Documents/Spyro 2_PC');WORLD=ROOT/'data/custom-levels/lanternfall'
base=bpy.data.scenes['Lanternfall | Complete hall']
name='Lanternfall | MoGe fitted hall'
if bpy.data.scenes.get(name):raise RuntimeError('Refined scene already exists; preserve it and choose a revision')
scene=bpy.data.scenes.new(name);bpy.context.window.scene=scene
scene.world=base.world;scene.render.engine='CYCLES';scene.cycles.samples=24
scene.render.resolution_x=1200;scene.render.resolution_y=900;scene.render.resolution_percentage=100
scene.view_settings.view_transform='AgX'
materials={m['s2_material_id']:m for m in bpy.data.materials if 's2_material_id' in m}
records=json.loads((WORLD/'complete-hall/geometry-refined.json').read_text())
for record in records:
    md=bpy.data.meshes.new('Refined | '+record['name']);md.from_pydata(record['vertices'],[],record['faces']);md.update()
    uv=md.uv_layers.new(name='S2Materials')
    for polygon,coords in zip(md.polygons,record['triangle_uvs']):
        for loop,coord in zip(polygon.loop_indices,coords):uv.data[loop].uv=coord
    md.materials.append(materials[record['material']])
    # Geometrically shared subdivision edges become genuinely shared editable
    # mesh vertices. Loop UVs keep their seams and independent atlas assignments.
    bm=bmesh.new();bm.from_mesh(md);bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-6)
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(md);bm.free()
    obj=bpy.data.objects.new(record['name'],md);scene.collection.objects.link(obj)
    obj['s2_material']=record['material'];obj['s2_collision']=record['collision'];obj['s2_color']=record['color']
    obj['s2_refinement']=record.get('refinement','Original complete prop retained')
for obj in base.objects:
    if obj.type not in ['CAMERA','LIGHT']:continue
    copy=obj.copy();copy.data=obj.data.copy();scene.collection.objects.link(copy)
    if obj==base.camera:scene.camera=copy
scene['authoring_workflow']='Complete Blender blockout -> individual GPT Image views -> bounded MoGe relief -> reusable atlas materials'
scene['status']='Complete structural prefab with modest fitted relief. Generated ornament and foliage silhouettes still need mesh refinement.'
scene.view_layers[0].update()
bpy.ops.wm.save_as_mainfile(filepath=str(WORLD/'Lanternfall - MoGe fitted hall.blend'),compress=True)
result={'scene':scene.name,'mesh_objects':len(records),'file':str(WORLD/'Lanternfall - MoGe fitted hall.blend')}
