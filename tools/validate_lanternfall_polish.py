"""Check the detail bake's mesh, original materials, closed additions and packet."""
from pathlib import Path
from collections import Counter
import json,struct,hashlib
import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'data/custom-levels/lanternfall/complete-hall'
source={o['name']:o for o in json.loads((OUT/'geometry-refined.json').read_text())}
records=json.loads((OUT/'geometry-polished.json').read_text())
bakes=json.loads((OUT/'hall-polished-uv.json').read_text())
assert len(records)==len(bakes)
packet=(OUT/'hall-polished.s2scene').read_bytes()
assert packet[:8]==b'S2GS0003'
gaussians,count,width,height=struct.unpack_from('<IIII',packet,8)
assert gaussians==0 and count%3==0 and count<=1500000
assert width<=8192 and height<=8192 and width*height<=32*1024*1024
assert len(packet)==24+count*32+width*height*4
entries=np.frombuffer(packet,dtype='<f4',count=count*8,offset=24).reshape(-1,8)
assert np.isfinite(entries).all()
assert (entries[:,3:]>=0).all() and (entries[:,3:]<=1).all()
assert np.array_equal(np.asarray(Image.open(OUT/'hall-polished-atlas.png')),np.frombuffer(packet,dtype=np.uint8,offset=24+count*32).reshape(height,width,4))
cursor=closed_additions=0
for record,bake in zip(records,bakes):
    assert record['name']==bake['name']
    points=np.asarray(record['vertices']);faces=np.asarray(record['faces'])
    tri=points[faces];uv=np.asarray(bake['triangle_uvs']);rgb=np.asarray(bake['colors'])
    assert tri.shape== (len(faces),3,3) and uv.shape==(len(faces),3,2) and rgb.shape==(len(faces),3)
    assert np.all(np.linalg.norm(np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0]),axis=1)>1e-9)
    if record['name'] in source:
        assert record==source[record['name']],record['name']+' changed its source geometry or sheet UVs'
    else:
        assert not record['collision']
        edges=Counter(tuple(sorted((int(a),int(b)))) for face in faces for a,b in zip(face,np.roll(face,-1)))
        assert all(n==2 for n in edges.values()),record['name']+' has an open detail edge'
        closed_additions+=1
    size=len(faces)*3;actual=entries[cursor:cursor+size].reshape(-1,3,8)
    assert np.allclose(actual[:,:,:3],tri[:,:,[0,2,1]],atol=2e-6)
    assert np.allclose(actual[:,:,3:5],uv,atol=1e-7)
    assert np.allclose(actual[:,:,5:],rgb[:,None,:],atol=1e-7)
    cursor+=size
assert cursor==count
assert (OUT/'hall-polished.s2world').read_text().replace('hall-polished.s2scene','hall-refined.s2scene')==(OUT/'hall-refined.s2world').read_text()
registration=json.loads((OUT/'registration/report.json').read_text())
assert len(registration['views'])==10
for view in registration['views']:
    flow=np.load(OUT/'registration'/(view['view']+'.npz'))['uv_offset']
    assert np.isfinite(flow).all() and abs(flow).max()<=.060001
report={'status':'pass','objects':len(records),'triangles':count//3,'closed_detail_objects':closed_additions,'atlas':[width,height],
        'packet_sha256':hashlib.sha256(packet).hexdigest(),
        'checks':['Original mesh and reusable sheet UVs unchanged','All added leaf/carving edges closed','Native XYZ/UV/RGB match authoring records','Atlas equals embedded native pixels','Structural collision and gameplay records unchanged','Ten finite bounded registration fields'],
        'scope':'Data integrity and preservation; visual similarity is reviewed in native captures, not inferred from these checks.'}
(OUT/'polish-validation.json').write_text(json.dumps(report,indent=2));print(json.dumps(report))
