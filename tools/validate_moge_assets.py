"""Check exported geometry/material interchange, independently of inference."""
from pathlib import Path
import json,struct
import numpy as np
import trimesh
ROOT=Path(__file__).resolve().parents[1]
WORLD=ROOT/'data/custom-levels/lanternfall'
results=[]
for path in sorted(list((WORLD/'moge').glob('*/*/mesh.glb'))+list((WORLD/'moge-multiview').glob('*/mesh.glb'))):
    mesh=trimesh.load(path,force='mesh',process=False)
    assert len(mesh.faces)>0 and np.isfinite(mesh.vertices).all(),path
    assert np.isfinite(mesh.vertex_normals).all(),path
    assert mesh.visual.uv is not None and np.isfinite(mesh.visual.uv).all(),path
    assert mesh.visual.uv.min()>=0 and mesh.visual.uv.max()<=1,path
    assert mesh.visual.material.baseColorTexture is not None,path
    packet=(path.parent/'mesh.s2scene').read_bytes()
    assert packet[:8]==b'S2GS0002',path
    splats,verts,w,h=struct.unpack_from('<IIII',packet,8)
    assert splats==0 and verts==len(mesh.faces)*3 and 0<verts<=1500000,path
    assert len(packet)==24+verts*20+w*h*4,path
    native=np.frombuffer(packet,dtype='<f4',offset=24,count=verts*5).reshape(-1,5)
    assert np.isfinite(native).all(),path
    results.append({'asset':str(path.relative_to(WORLD)),'vertices':len(mesh.vertices),'triangles':len(mesh.faces),'embedded_texture':list(mesh.visual.material.baseColorTexture.size),'native_packet_valid':True,'watertight':bool(mesh.is_watertight)})
out=ROOT/'data/validation/lanternfall/moge-assets.json'
out.write_text(json.dumps({'passed':True,'meshes':results},indent=2))
print(f'{len(results)} textured GLBs and native mesh packets passed interchange validation')
