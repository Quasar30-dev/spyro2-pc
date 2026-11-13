"""Export the complete structural module and its collision, with GPT atlas UVs."""
from pathlib import Path
import json,struct,math,argparse
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[1];WORLD=ROOT/'data/custom-levels/lanternfall'
OUT=WORLD/'complete-hall';SOURCE=WORLD/'source/multiview'
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--refined',action='store_true');args=parser.parse_args()
records=json.loads((OUT/('geometry-refined.json' if args.refined else 'geometry.json')).read_text())
structural={o['name']:o for o in json.loads((OUT/'geometry.json').read_text())}
prefix='hall-refined' if args.refined else 'hall'
material={m['id']:m for m in json.loads((SOURCE/'materials.json').read_text())['materials']}
atlas=np.concatenate([np.asarray(Image.open(SOURCE/(cat+'-material-atlas.png')).convert('RGBA')) for cat in ['exterior','interior']])
h,w=atlas.shape[:2];vertices=[];collision=[];discarded=0
light=np.array([-.35,-.28,.89]);light/=np.linalg.norm(light)
for obj in records:
    p=np.asarray(obj['vertices']);ids=np.asarray(obj['faces']);tri=p[ids];uv=np.asarray(obj['triangle_uvs'])
    cross=np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0]);area=np.linalg.norm(cross,axis=1)
    valid=area>1e-9;discarded+=int((~valid).sum());tri=tri[valid];uv=uv[valid];cross=cross[valid];area=area[valid]
    normal=cross/area[:,None];centroid=p.mean(0)
    inward=((tri.mean(1)-centroid)*normal).sum(-1)<0;normal[inward]*=-1
    shade=np.clip(.76+.22*(normal@light),.52,1)
    if obj['name'].startswith('Lantern') and ('glass' in obj['name'].lower() or 'core' in obj['name'].lower()):shade[:]=1
    colors=shade[:,None]*np.asarray(obj['color'])[None,:]
    item=material[obj['material']];col,row=item['cell'];category=0 if obj['material'].startswith('exterior') else 1
    # Explicit atlas UVs survive outside Blender; no shader-node mapping required.
    uv[:,:,0]=col*.25+(2/w)+uv[:,:,0]*(.25-4/w)
    uv[:,:,1]=(category+row*.5)/2+2/h+(1-uv[:,:,1])*(.25-4/h)
    native=tri[:,:,[0,2,1]]
    packed=np.concatenate([native,uv,np.repeat(colors[:,None,:],3,axis=1)],axis=-1)
    vertices.append(packed.reshape(-1,8))
    if obj['collision']:
        original=structural[obj['name']];points=np.asarray(original['vertices']);indices=np.asarray(original['faces']);ctri=points[indices]
        valid=np.linalg.norm(np.cross(ctri[:,1]-ctri[:,0],ctri[:,2]-ctri[:,0]),axis=1)>1e-9
        collision.extend(ctri[valid][:,:,[0,2,1]])
mesh=np.concatenate(vertices).astype('<f4');assert len(mesh)<=1500000
with (OUT/(prefix+'.s2scene')).open('wb') as f:
    f.write(b'S2GS0003');f.write(struct.pack('<IIII',0,len(mesh),w,h));f.write(mesh.tobytes());f.write(atlas.tobytes())
lines=['S2WORLD 2','17','"Lanternfall - complete structural hall"','0 .08 2 0 .25 .34 .53','1','-1 0 0',str(len(collision))]
for tri in collision:
    lines.append('2 0')
    for p in tri:lines.append(' '.join(f'{v:.7g}' for v in p)+' 1 1 1 0 0')
lines+=['0','5','0 0 2','0 0 6','0 0 10','0 0 16','0 0 2','0','0','0','0','SCENERY 1','"'+prefix+'.s2scene"']
(OUT/(prefix+'.s2world')).write_text('\n'.join(lines)+'\n')
(OUT/('export-refined.json' if args.refined else 'export.json')).write_text(json.dumps({'source':'Complete Blender structure with GPT material atlases'+(' and bounded MoGe relief' if args.refined else ''),'mesh_triangles':len(mesh)//3,'collision_triangles':len(collision),'objects':len(records),'discarded_zero_area_triangles':discarded,'texture_dimensions':[w,h],'packet':'S2GS0003, position/UV/RGB, no Gaussian backdrop'},indent=2))
print({'triangles':len(mesh)//3,'collision':len(collision),'objects':len(records)})
