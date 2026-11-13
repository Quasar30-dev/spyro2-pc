"""Round-trip every stored source pose/UV/colour in the editable Blender import."""
import bpy
import hashlib
import json
from pathlib import Path
import sys


def main():
    path=Path(sys.argv[sys.argv.index('--')+1]).resolve();bpy.ops.wm.open_mainfile(filepath=str(path))
    source=path.with_name('models.json');data=json.loads(source.read_text());frames=vertices=0
    for model in data['models']:
        for clip in model['clips']:
            obj=bpy.data.objects[f"Class_{model['class']:03x}_clip_{clip['clip']:02d}"]
            keys=obj.data.shape_keys.key_blocks;assert len(keys)==len(clip['frames'])
            assert json.loads(obj['frame_control_metadata_ticks'])==[[f['control'],f['metadata'],f['ticks']] for f in clip['frames']]
            for key,frame in zip(keys,clip['frames']):
                assert len(key.data)==len(frame['vertices'])
                for vertex,p in zip(key.data,frame['vertices']):
                    expected=(p[0],-p[2],p[1]);assert max(abs(x-y) for x,y in zip(vertex.co,expected))<2e-6
                    vertices+=1
                frames+=1
            geometry=model['meshes'][clip['frames'][0]['mesh']]
            assert len(obj.data.polygons)==len(geometry['faces'])
            for poly,face in zip(obj.data.polygons,geometry['faces']):
                order=[0,1,3,2] if len(face['vertices'])==4 else [0,1,2]
                assert list(poly.vertices)==[face['vertices'][i] for i in order]
                for loop,corner in zip(poly.loop_indices,order):
                    u,v=face['uv'][corner];actual=obj.data.uv_layers['SourceAtlasUV'].data[loop].uv
                    assert abs(actual[0]-u)<1e-6 and abs(actual[1]-(1-v))<1e-6
                    rgb=obj.data.color_attributes['SourceRGBMultipliers'].data[loop].color[:3]
                    assert max(abs(a-b) for a,b in zip(rgb,face['colours'][corner]))<2e-6
    print('ARTIFACT',path,'sha256',hashlib.sha256(path.read_bytes()).hexdigest(),flush=True)
    print('SOURCE',source,'sha256',hashlib.sha256(source.read_bytes()).hexdigest(),flush=True)
    print('EDITABLE SOURCE ROUNDTRIP PASS',frames,'poses',vertices,'pose vertices; numeric metadata/topology/UV/RGB multipliers exact within float32 storage',flush=True)
    print('LIMIT near authoring data; preview interpolation is linear; no game AI/story equivalence claim',flush=True)


if __name__=='__main__':main()
