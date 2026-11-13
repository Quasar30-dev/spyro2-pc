"""Validate layout/UV preservation and the runtime packet, not visual similarity."""
from pathlib import Path
import json,struct
import numpy as np
from refine_lanternfall_moge import subdivided_barycentrics
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'data/custom-levels/lanternfall/complete-hall'
source={o['name']:o for o in json.loads((OUT/'geometry.json').read_text())}
refined=json.loads((OUT/'geometry-refined.json').read_text());bary=subdivided_barycentrics()
assert len(refined)==len(source)
checked=0;changed=0
for obj in refined:
    original=source[obj['name']];assert obj['material']==original['material'];assert obj['collision']==original['collision']
    p=np.asarray(obj['vertices']);f=np.asarray(obj['faces']);uv=np.asarray(obj['triangle_uvs']);assert np.isfinite(p).all() and np.isfinite(uv).all()
    assert uv.min()>=-1e-6 and uv.max()<=1+1e-6
    if 'refinement' not in obj:
        assert obj==original;continue
    tri=np.asarray(original['vertices'])[original['faces']];valid=np.linalg.norm(np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0]),axis=1)>1e-9
    expected=np.einsum('kij,tjc->tkic',bary,tri[valid]).reshape(-1,3)
    expected_uv=np.einsum('kij,tjc->tkic',bary,np.asarray(original['triangle_uvs'])[valid]).reshape(-1,3,2)
    assert np.allclose(uv,expected_uv,atol=1e-7)
    edge=np.tile(bary.min(-1).reshape(-1)==0,valid.sum())
    assert np.array_equal(p[edge],expected[edge]),obj['name']+' opened an edge'
    displacement=np.linalg.norm(p-expected,axis=1);assert displacement.max(initial=0)<=.030001
    assert len(f)==valid.sum()*16
    checked+=1;changed+=int((displacement>1e-5).sum())
packet=(OUT/'hall-refined.s2scene').read_bytes();assert packet[:8]==b'S2GS0003'
_,count,width,height=struct.unpack_from('<IIII',packet,8)
assert len(packet)==24+count*32+width*height*4
entries=np.frombuffer(packet,dtype='<f4',count=count*8,offset=24).reshape(-1,8)
assert np.isfinite(entries).all() and (entries[:,3:]>=0).all() and (entries[:,3:]<=1).all()
report={'status':'pass','objects':len(refined),'refined_objects':checked,'displaced_vertex_entries':changed,'mesh_triangles':count//3,
        'checks':['Semantic material IDs retained','Atlas UVs exactly barycentrically interpolated','Original edges pinned exactly','No triangles removed by refinement except source zero-area faces','Maximum relief 3 cm','Native packet length, finite coordinates, UVs and RGB valid']}
(OUT/'refinement-validation.json').write_text(json.dumps(report,indent=2));print(json.dumps(report))
