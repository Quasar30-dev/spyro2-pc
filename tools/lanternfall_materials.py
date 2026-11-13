"""Blender material library and repeatable UV reapplication for textureless meshes.

Exec this file in Blender, then use apply_material(obj, 'exterior.roof',
selected_only=True, meters_per_tile=2). Select only roof faces first. This does
not guess semantic material assignments from an untextured mesh.
"""
import bpy, json
from pathlib import Path
from mathutils import Vector
ROOT=Path(r'C:/Users/Shadow/Documents/Spyro 2_PC')
WORLD=ROOT/'data/custom-levels/lanternfall'
SOURCE=WORLD/'source/multiview'
KINDS={
    'exterior':['wall','roof','paving','carved_trim','banner','brass','oak','ivy'],
    'interior':['wall','paving','vault','carved_trim','banner','compass','brass','oak'],
}

def material_library():
    materials={};entries=[]
    for category,names in KINDS.items():
        path=SOURCE/(category+'-material-atlas.png')
        img=bpy.data.images.load(str(path),check_existing=True);img.pack()
        for cell,name in enumerate(names):
            ident=category+'.'+name;mat=bpy.data.materials.get('Lanternfall | '+ident)
            if mat is None:
                mat=bpy.data.materials.new('Lanternfall | '+ident);mat.use_nodes=True
                tree=mat.node_tree;bsdf=tree.nodes.get('Principled BSDF')
                bsdf.inputs['Roughness'].default_value=.8
                bsdf.inputs['Metallic'].default_value=.55 if name=='brass' else 0
                uv=tree.nodes.new('ShaderNodeUVMap');uv.uv_map='S2Materials'
                repeat=tree.nodes.new('ShaderNodeVectorMath');repeat.operation='FRACTION'
                scale=tree.nodes.new('ShaderNodeVectorMath');scale.operation='MULTIPLY'
                # Inset by two source pixels on each boundary to avoid neighbors.
                px,py=2/img.size[0],2/img.size[1]
                scale.inputs[1].default_value=(.25-2*px,.5-2*py,1)
                offset=tree.nodes.new('ShaderNodeVectorMath');offset.operation='ADD'
                col,row=cell%4,cell//4
                offset.inputs[1].default_value=(col*.25+px,(1-row)*.5+py,0)
                tex=tree.nodes.new('ShaderNodeTexImage');tex.image=img;tex.extension='EXTEND'
                for a,b in [(uv.outputs['UV'],repeat.inputs[0]),(repeat.outputs[0],scale.inputs[0]),(scale.outputs[0],offset.inputs[0]),(offset.outputs[0],tex.inputs['Vector']),(tex.outputs['Color'],bsdf.inputs['Base Color'])]:tree.links.new(a,b)
                mat['s2_material_id']=ident;mat['s2_atlas']=path.name
                mat['s2_cell']=cell;mat['s2_source']='GPT Image base-color candidate; seams/relighting require validation.'
            materials[ident]=mat
            entries.append({'id':ident,'atlas':path.name,'cell':[cell%4,cell//4],'uv_rectangle_top_left':[cell%4/4,cell//4/2,(cell%4+1)/4,(cell//4+1)/2],'normal_map':None})
    (SOURCE/'materials.json').write_text(json.dumps({'grid':[4,2],'uv_layer':'S2Materials','inset_source_pixels':2,'materials':entries},indent=2))
    return materials

def apply_material(obj, ident, selected_only=True, meters_per_tile=2):
    if obj.type!='MESH':raise ValueError('Select a mesh')
    if obj.mode!='OBJECT':raise ValueError('Switch to Object mode after selecting the target faces')
    if meters_per_tile<=0:raise ValueError('Tile size must be positive')
    polygons=[p for p in obj.data.polygons if p.select or not selected_only]
    if not polygons:raise ValueError('No selected faces; refusing to replace the whole mesh')
    mat=material_library()[ident]
    slot=next((i for i,m in enumerate(obj.data.materials) if m==mat),None)
    if slot is None:slot=len(obj.data.materials);obj.data.materials.append(mat)
    uv=obj.data.uv_layers.get('S2Materials') or obj.data.uv_layers.new(name='S2Materials')
    transform=obj.matrix_world.to_3x3()
    normal_transform=transform.inverted().transposed()
    for p in polygons:
        normal=normal_transform@p.normal
        axis=max(range(3),key=lambda i:abs(normal[i]))
        axes={0:(1,2),1:(0,2),2:(0,1)}[axis]
        for loop in p.loop_indices:
            co=transform@obj.data.vertices[obj.data.loops[loop].vertex_index].co
            uv.data[loop].uv=(co[axes[0]]/meters_per_tile,co[axes[1]]/meters_per_tile)
        p.material_index=slot
    obj.data.update()
    return {'object':obj.name,'material':ident,'assigned_faces':len(polygons),'uv_layer':uv.name}

def build_library_scene():
    name='Lanternfall | Reusable materials'
    if bpy.data.scenes.get(name):raise RuntimeError('Material preview scene already exists; preserve it')
    materials=material_library();scene=bpy.data.scenes.new(name)
    scene.render.engine='CYCLES';scene.cycles.samples=16
    scene.render.resolution_x=1500;scene.render.resolution_y=1500;scene.render.resolution_percentage=100
    scene.view_settings.view_transform='Standard';scene.world=bpy.data.worlds.new('Material studio');scene.world.use_nodes=True
    scene.world.node_tree.nodes['Background'].inputs['Color'].default_value=(.7,.7,.7,1)
    scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value=.8
    corners=[(-1,-1,-.12),(1,-1,-.12),(1,1,-.12),(-1,1,-.12),(-1,-1,.12),(1,-1,.12),(1,1,.12),(-1,1,.12)]
    faces=[(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)]
    ink=bpy.data.materials.new('Material labels ink');ink.use_nodes=True
    ink.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value=(.025,.025,.025,1)
    for i,(ident,mat) in enumerate(materials.items()):
        mesh=bpy.data.meshes.new(ident+' sample');mesh.from_pydata(corners,[],faces);mesh.update()
        obj=bpy.data.objects.new(ident,mesh);scene.collection.objects.link(obj)
        obj.location=((i%4-1.5)*3,(1.5-i//4)*3,0)
        apply_material(obj,ident,selected_only=False,meters_per_tile=2)
        # Offset sample UVs into one full tile; no repeated ornament on samples.
        for loop in mesh.uv_layers['S2Materials'].data:loop.uv+=Vector((.5,.5))
        font=bpy.data.curves.new(ident+' label','FONT');font.body=ident;font.size=.18;font.align_x='CENTER'
        font.materials.append(ink)
        text=bpy.data.objects.new(font.name,font);scene.collection.objects.link(text);text.location=(obj.location.x,obj.location.y-1.35,.15)
    camdata=bpy.data.cameras.new('Material sheet camera');cam=bpy.data.objects.new(camdata.name,camdata);scene.collection.objects.link(cam)
    cam.location=(0,-.5,20);cam.rotation_euler=(Vector((0,-.5,0))-cam.location).to_track_quat('-Z','Y').to_euler()
    camdata.type='ORTHO';camdata.ortho_scale=12.7;scene.camera=cam
    scene['status']='16 reusable atlas materials; assign selected faces and validate UV scale/seams.'
    dest=WORLD/'Lanternfall - Materials.blend'
    scene.view_layers[0].update()
    with bpy.context.temp_override(scene=scene,view_layer=scene.view_layers[0]):
        bpy.data.libraries.write(str(dest),{scene},path_remap='RELATIVE',fake_user=True,compress=True)
    return {'saved':str(dest),'materials':len(materials),'sample_meshes':16}
