"""Apply the actual Autumn Plains green roof tile to every pointed turret."""
import bpy
from pathlib import Path
ROOT=Path(r'C:/Users/Shadow/Documents/Spyro 2_PC/data/custom-levels/springtide-castle')
scene=bpy.context.scene
assert scene.get('level_id')=='springtide-castle'
atlas=bpy.data.images.load(str(ROOT/'assets'/'terrain-33.png'),check_existing=True)
pixels=list(atlas.pixels);aw,ah=atlas.size;tile=18;tx=tile%16*64;ty=ah-(tile//16+1)*64
im=bpy.data.images.new('Autumn Plains green roof / tile 18',width=64,height=64,alpha=True)
im.pixels=[c for y in range(64) for c in pixels[((ty+y)*aw+tx)*4:((ty+y)*aw+tx+64)*4]];im.pack()
m=bpy.data.materials.new('Autumn Plains green castle roof');m.use_nodes=True
m['source_slot']=33;m['source_tile']=18;m['water']=False;m['s2_material']=100
n=m.node_tree.nodes;l=m.node_tree.links;bs=n.get('Principled BSDF');bs.inputs['Roughness'].default_value=.9
tx=n.new('ShaderNodeTexImage');tx.image=im;tx.interpolation='Closest'
vc=n.new('ShaderNodeVertexColor');vc.layer_name='Color';mix=n.new('ShaderNodeMixRGB');mix.blend_type='MULTIPLY';mix.inputs[0].default_value=1
l.new(tx.outputs['Color'],mix.inputs[1]);l.new(vc.outputs['Color'],mix.inputs[2]);l.new(mix.outputs[0],bs.inputs['Base Color']);l.new(mix.outputs[0],bs.inputs['Emission Color']);bs.inputs['Emission Strength'].default_value=.45
changed=[]
for o in scene.objects:
    if o.type!='MESH' or 'pointed roof' not in o.name:continue
    o.data.materials.clear();o.data.materials.append(m)
    colors=o.data.color_attributes.get('Color')
    for p in o.data.polygons:
        shade=.85+.15*max(0,p.normal.z)
        for li in p.loop_indices:colors.data[li].color=(shade,shade,shade,1)
    changed.append(o.name)
result={'pointed_roofs_updated':len(changed),'source':'Autumn Plains terrain tile 18','blue_tint_removed':True}
