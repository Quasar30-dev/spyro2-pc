"""Keep every decoded numeric donor clip/frame editable as Blender shape keys.

This is an authoring archive of source poses and UVs. Its linear preview uses
the retained per-frame VBlank durations; game interpolation, loop flags, AI and
sound events remain authoritative in the unchanged bank/frame metadata.
"""
import bpy
import json
from pathlib import Path
import sys


def main():
    root=Path(sys.argv[sys.argv.index('--')+1]).resolve()
    following=sys.argv[sys.argv.index('--')+2:]
    output_name=following[0] if following else 'all-numeric-clips-editable.blend'
    data=json.loads((root/'models.json').read_text())
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    atlas=bpy.data.images.load(str(root/'atlas.png'));atlas.pack()
    mats=[]
    for textured in (False,True):
        mat=bpy.data.materials.new('Original textured corner colours' if textured else 'Original corner colours')
        mat.use_nodes=True;nodes=mat.node_tree.nodes;nodes.clear();links=mat.node_tree.links
        vertex=nodes.new('ShaderNodeVertexColor');vertex.layer_name='SourceColours'
        emit=nodes.new('ShaderNodeEmission');output=nodes.new('ShaderNodeOutputMaterial');links.new(emit.outputs[0],output.inputs['Surface'])
        if textured:
            tex=nodes.new('ShaderNodeTexImage');tex.image=atlas;tex.interpolation='Closest'
            multiply=nodes.new('ShaderNodeMixRGB');multiply.blend_type='MULTIPLY';multiply.inputs[0].default_value=1
            links.new(vertex.outputs['Color'],multiply.inputs[1]);links.new(tex.outputs['Color'],multiply.inputs[2]);links.new(multiply.outputs[0],emit.inputs[0])
        else:links.new(vertex.outputs['Color'],emit.inputs[0])
        mats.append(mat)
    pose_count=0;clip_count=0;vertex_count=0
    scene=bpy.context.scene;scene.render.fps=60
    xyz=lambda p:(p[0],-p[2],p[1])
    lin=lambda x:x/12.92 if x<=.04045 else ((x+.055)/1.055)**2.4
    for m,model in enumerate(data['models']):
        collection=bpy.data.collections.new(f"Converted_{model['class']:03x}_numeric_clips");scene.collection.children.link(collection)
        for clip in model['clips']:
            frames=clip['frames'];assert len({f['mesh'] for f in frames})==1
            source=model['meshes'][frames[0]['mesh']];faces=source['faces']
            order=lambda f:[0,1,3,2] if len(f['vertices'])==4 else [0,1,2]
            mesh=bpy.data.meshes.new(f"Class_{model['class']:03x}_clip_{clip['clip']:02d}")
            mesh.from_pydata([xyz(p) for p in frames[0]['vertices']],[],[[f['vertices'][i] for i in order(f)] for f in faces]);mesh.update()
            obj=bpy.data.objects.new(mesh.name,mesh);collection.objects.link(obj);obj.location.x=m*8
            obj.hide_render=clip['clip']!=0;obj.hide_viewport=clip['clip']!=0
            obj['target_class']=model['class'];obj['numeric_clip']=clip['clip'];obj['source_data']=str(root/'models.json')
            obj['limits']='Exact decoded poses; linear authoring preview only. Runtime blends/loops/events retained in source bank.'
            obj['frame_control_metadata_ticks']=json.dumps([[f['control'],f['metadata'],f['ticks']] for f in frames])
            for material in mats:mesh.materials.append(material)
            uv=mesh.uv_layers.new(name='SourceAtlasUV');colours=mesh.color_attributes.new(name='SourceColours',type='FLOAT_COLOR',domain='CORNER')
            original_rgb=mesh.color_attributes.new(name='SourceRGBMultipliers',type='FLOAT_COLOR',domain='CORNER')
            for poly,face in zip(mesh.polygons,faces):
                poly.material_index=int(face['textured'])
                for loop,corner in zip(poly.loop_indices,order(face)):
                    u,v=face['uv'][corner];uv.data[loop].uv=(u,1-v)
                    colours.data[loop].color=tuple(lin(max(0.,x)) for x in face['colours'][corner])+(1.,)
                    original_rgb.data[loop].color=tuple(face['colours'][corner])+(1.,)
            obj.shape_key_add(name='Basis',from_mix=False)
            keys=[]
            for f,frame in enumerate(frames[1:],1):
                key=obj.shape_key_add(name=f'SourceFrame_{f:03d}',from_mix=False)
                for vertex,p in zip(key.data,frame['vertices']):vertex.co=xyz(p)
                keys.append(key)
            tick=1
            for f,frame in enumerate(frames):
                for k,key in enumerate(keys,1):key.value=float(k==f);key.keyframe_insert(data_path='value',frame=tick)
                tick+=frame['ticks']
            if keys:
                action=mesh.shape_keys.animation_data.action
                action.name=f"RawClip_{model['class']:03x}_{clip['clip']:02d}_preview"
                try:curves=list(action.fcurves)
                except AttributeError:curves=[f for layer in action.layers for strip in layer.strips for bag in strip.channelbags for f in bag.fcurves]
                for curve in curves:
                    for point in curve.keyframe_points:point.interpolation='LINEAR'
            scene.frame_end=max(scene.frame_end,tick)
            pose_count+=len(frames);clip_count+=1;vertex_count+=sum(len(f['vertices']) for f in frames)
    text=bpy.data.texts.new('Original frame metadata and provenance');text.write(json.dumps(dict(source=str(root/'models.json'),
         warning='Authoring preview only; actual runtime interpolation/events are preserved in converted bank.',
         clips=clip_count,frames=pose_count,pose_vertices=vertex_count),indent=2))
    scene.frame_set(1)
    output=root/output_name;assert not output.exists();bpy.ops.wm.save_as_mainfile(filepath=str(output),compress=True)
    print('EDITABLE SOURCE CLIPS',clip_count,'frames',pose_count,'pose vertices',vertex_count,'file',output,flush=True)
    print('LIMIT near authoring pose archive; runtime bank retains near/far streams; no semantic labels or source-accurate Blender interpolation claim',flush=True)


if __name__=='__main__':main()
