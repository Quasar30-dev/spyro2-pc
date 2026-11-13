"""Replace the three pictured hanging lights with complete mesh props."""
from pathlib import Path
import json,struct,math
import numpy as np
import trimesh
from PIL import Image
ROOT=Path(__file__).resolve().parents[1];WORLD=ROOT/'data/custom-levels/lanternfall'
BG=WORLD/'moge-props/multiview-interior-clean-background/known'
OLD=WORLD/'moge-seams/interior.southwest'
OUT=WORLD/'pc/interior-mesh';OUT.mkdir(parents=True,exist_ok=True)
data=np.load(OLD/'geometry.npz');old=json.loads((OLD/'report.json').read_text())
clean=np.load(BG/'geometry.npz');clean_report=json.loads((BG/'report.json').read_text())
bg=trimesh.load(BG/'mesh.glb',force='mesh',process=False)
lamp=json.loads((WORLD/'props/hanging-lantern-mesh.json').read_text())
placements=[];lamp_triangles=[];colors=[]
for index,(cx,cy,top,bottom,hook_y) in enumerate([(393,93,53,138,10),(445,171,143,197,108),(472,229,213,245,178)]):
    raw=data['points'][cy-2:cy+3,cx-2:cx+3]
    d=float(np.median(raw[:,:,2][np.isfinite(raw[:,:,2])]))
    fx=old['normalized_intrinsics'][0][0]*old['input_size'][0]
    scale=old['authoring_scale'];h=old['input_size'][1]
    body_height=(bottom-top)*d/fx*scale;s=body_height/.995
    x=(cx-h*.5)*d/fx*scale;z=d*scale
    bottom_y=1.6-(bottom-h*.5)*d/fx*scale
    origin=np.array([x,.2,-z])
    hits,_,_=bg.ray.intersects_location([origin],[[0,1,0]],multiple_hits=True)
    ys=hits[:,1] if len(hits) else np.array([])
    ys=ys[ys>max(2,body_height*1.8)]
    anchored=bool(len(ys))
    if anchored:bottom_y=float(ys.min())-1.67*s
    # Use the newly reconstructed ceiling surface itself as the mounting point.
    # Old single-view metric depth can shift after foreground removal; reusing
    # its world position left the nearest lamp floating outside the new surface.
    bh,bw=clean['depth'].shape;px=round(cx/old['input_size'][0]*bw);py=round(hook_y/old['input_size'][1]*bh)
    hook=clean['points'][py,px]*clean_report['authoring_scale']
    assert clean['valid'][py,px] and np.isfinite(hook).all(),'Ceiling hook must land on reconstructed geometry'
    fx=clean_report['normalized_intrinsics'][0][0]*old['input_size'][0]
    s=(bottom-hook_y)*hook[2]/fx/1.67
    native=np.array([hook[0],1.6-hook[1]-1.67*s,hook[2]])
    placements.append({'id':f'hanging-lantern-{index+1}','native_position':native.tolist(),'scale':float(s),'ceiling_ray_anchor':True,'anchor_method':'Point on the clean ceiling surface at the source hook','parts':len(lamp)})
    for part in lamp:
        local=np.asarray(part['vertices'])[:,[0,2,1]] # Blender xyz -> native x,z,y
        outward=trimesh.Trimesh(vertices=local,faces=part['faces'],process=True);outward.fix_normals()
        tri=(outward.vertices*s+native)[outward.faces]
        normal=np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0]);normal/=np.maximum(np.linalg.norm(normal,axis=1,keepdims=True),1e-9)
        # Native scene textures are unlit. Bake a small directional value range
        # into per-face palette swatches, retaining actual silhouette/depth.
        light=np.array([-.35,.8,-.48]);light/=np.linalg.norm(light)
        shade=np.clip(.68+.3*(normal@light),.4,1)
        if part['emission']>0:shade=np.maximum(shade,.95)
        shade=np.round(shade*24)/24
        linear=np.asarray(part['color'][:3])[None,:]*shade[:,None]
        rgb=np.clip(np.where(linear<=.0031308,linear*12.92,1.055*np.power(linear,1/2.4)-.055)*255,0,255).astype(np.uint8)
        lamp_triangles.extend(tri);colors.extend(map(tuple,rgb.tolist()))
blob=(BG/'mesh.s2scene').read_bytes();_,count,w,h=struct.unpack_from('<IIII',blob,8)
surface=np.frombuffer(blob,dtype='<f4',offset=24,count=count*5).reshape(-1,5).copy()
image=np.frombuffer(blob,dtype=np.uint8,offset=24+count*20).reshape(h,w,4)
palette=list(dict.fromkeys(colors));cols=w//8;rows=math.ceil(len(palette)/cols);height=h+rows*8
atlas=np.zeros((height,w,4),dtype=np.uint8);atlas[:h]=image
for i,c in enumerate(palette):atlas[h+(i//cols)*8:h+(i//cols+1)*8,(i%cols)*8:(i%cols+1)*8]=(*c,255)
surface[:,4]*=h/height
lookup={c:i for i,c in enumerate(palette)};prop=[]
for tri,c in zip(lamp_triangles,colors):
    i=lookup[c];uv=((i%cols*8+4)/w,(h+i//cols*8+4)/height)
    prop.append(np.concatenate([tri,np.tile(uv,(3,1))],axis=1))
mesh=np.concatenate([surface,np.concatenate(prop)]).astype('<f4')
assert len(mesh)<=1500000 and np.isfinite(mesh).all()
with (OUT/'interior.s2scene').open('wb') as out:
    out.write(b'S2GS0002');out.write(struct.pack('<IIII',0,len(mesh),w,height));out.write(mesh.tobytes());out.write(atlas.tobytes())
template=(WORLD/'pc/exterior.s2world').read_text().replace('Lanternfall Court - Gaussian test','Lanternfall interior - full mesh lanterns')
template=template.replace('SPLATS 1\n"exterior.s2gs"','SCENERY 1\n"interior.s2scene"')
lines=template.splitlines();actor_index=7+int(lines[6])*4
actor_count=int(lines[actor_index]);lines[actor_index:actor_index+actor_count+1]=['0']
template='\n'.join(lines)+'\n'
(OUT/'interior.s2world').write_text(template)
(OUT/'placements.json').write_text(json.dumps({'source_background':str(BG.relative_to(ROOT)),'lamps':placements,'triangles':len(mesh)//3,'lamp_triangles':len(lamp_triangles),'image_hanging_lights_removed':True},indent=2))
print(json.dumps({'placements':placements,'triangles':len(mesh)//3,'lamp_triangles':len(lamp_triangles)}))
