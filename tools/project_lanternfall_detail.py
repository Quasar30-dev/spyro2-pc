"""Project generated detail onto real mesh while retaining sheet UVs separately.

This is a detailed appearance layer, not geometry reconstruction. Full-resolution
views are packed without resampling; visibility and semantic masks reject sky
and foreground spills. Unseen surfaces retain reusable atlas materials.
"""
from pathlib import Path
import json,math,struct
import numpy as np
from PIL import Image
from scipy.ndimage import minimum_filter

ROOT=Path(__file__).resolve().parents[1];WORLD=ROOT/'data/custom-levels/lanternfall';OUT=WORLD/'complete-hall'
records=json.loads((OUT/'geometry-refined.json').read_text())
cameras=json.loads((OUT/'cameras.json').read_text());source=WORLD/'source/multiview'
materials={m['id']:m for m in json.loads((source/'materials.json').read_text())['materials']}
names=json.loads((OUT/'depth-reference/objects.json').read_text());ids={name:i for i,name in enumerate(names)}
views=[];padding=16
for camera in cameras:
    pixels=np.asarray(Image.open(OUT/'generated-views'/camera.get('generated_image',camera['id']+'.png')).convert('RGBA'))
    ref=np.load(OUT/'depth-reference'/(camera['id']+'.npz'))
    matrix=np.asarray(camera['camera_to_world_blender'])
    views.append({'camera':camera,'pixels':pixels,'depth':ref['depth'],'ids':ref['object_ids'],
        'origin':matrix[:3,3],'rot':matrix[:3,:3],'tan_x':math.tan(math.radians(camera['horizontal_fov_degrees']/2))})
tilew=max(v['pixels'].shape[1] for v in views)+2*padding;tileh=max(v['pixels'].shape[0] for v in views)+2*padding
sheets=[np.asarray(Image.open(source/(cat+'-material-atlas.png')).convert('RGBA')) for cat in ['exterior','interior']]
width=max(tilew*3,max(p.shape[1]+2*padding for p in sheets));height=tileh*4+sum(p.shape[0]+2*padding for p in sheets)
assert width<=8192 and height<=8192 and width*height<=32*1024*1024
atlas=np.zeros((height,width,4),np.uint8);atlas[:,:,3]=255
def insert(pixels,x,y):
    h,w=pixels.shape[:2];atlas[y-padding:y+h+padding,x-padding:x+w+padding]=np.pad(pixels,((padding,padding),(padding,padding),(0,0)),mode='edge')
for i,view in enumerate(views):
    x=(i%3)*tilew+padding;y=(i//3)*tileh+padding
    insert(view['pixels'],x,y);view['offset']=(x,y)
sheet_offsets=[];y=tileh*4+padding
for pixels in sheets:
    insert(pixels,padding,y);sheet_offsets.append((padding,y));y+=pixels.shape[0]+2*padding

allvertices=[];assignments=[];statistics=[];photo_area=total_area=0
for obj in records:
    points=np.asarray(obj['vertices']);faces=np.asarray(obj['faces']);tri=points[faces];uv=np.asarray(obj['triangle_uvs'])
    cross=np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0]);length=np.linalg.norm(cross,axis=1);keep=length>1e-9
    tri=tri[keep];uv=uv[keep];cross=cross[keep];length=length[keep]
    normal=cross/length[:,None];center=tri.mean(1)
    inward=((center-points.mean(0))*normal).sum(-1)<0;normal[inward]*=-1
    scores=[];projections=[]
    for view in views:
        local=(tri-view['origin'])@view['rot'];z=-local[:,:,2];safe=np.maximum(z,.001)
        h,w=view['pixels'].shape[:2];tan_y=view['tan_x']*h/w
        pu=.5+local[:,:,0]/safe/(2*view['tan_x']);pv=.5-local[:,:,1]/safe/(2*tan_y)
        visible=(z>.1).all(1)&(pu>.004).all(1)&(pu<.996).all(1)&(pv>.004).all(1)&(pv<.996).all(1)
        sample=np.concatenate([tri,center[:,None,:]],axis=1);cam=(sample-view['origin'])@view['rot'];depth=-cam[:,:,2]
        su=.5+cam[:,:,0]/np.maximum(depth,.001)/(2*view['tan_x']);sv=.5-cam[:,:,1]/np.maximum(depth,.001)/(2*tan_y)
        rh,rw=view['depth'].shape;sx=np.clip((su*rw).astype(int),0,rw-1);sy=np.clip((sv*rh).astype(int),0,rh-1)
        # Near object boundaries allow a depth match even if low-res object IDs
        # fall on the adjoining member. Foreground depth still blocks projection.
        matches=np.abs(view['depth'][sy,sx]-depth)<(.13+.014*depth)
        visible&=matches.sum(1)>=3
        px=np.clip((su*w).astype(int),0,w-1);py=np.clip((sv*h).astype(int),0,h-1);rgb=view['pixels'][py,px,:3].astype(float)
        sky=(rgb.min(-1)>247);visible&=~sky.any(1)
        foliage=obj['name'].startswith('Topiary foliage')
        green=(rgb[:,:,1]>rgb[:,:,0]*1.08)&(rgb[:,:,1]>rgb[:,:,2]*1.08)
        if foliage:visible&=green.sum(1)>=3
        else:visible&=green.sum(1)==0
        toward=view['origin']-center;distance=np.linalg.norm(toward,axis=1);facing=np.sum(normal*toward/np.maximum(distance[:,None],.001),axis=1)
        visible&=facing>.12
        # Resolution density and angle; favour dedicated floor/ceiling references.
        score=facing**2/np.maximum(distance,1)**2
        if obj['material'] in ['interior.paving','interior.compass'] and view['camera']['id']=='10 Floor':score*=2
        if obj['material']=='interior.vault' and view['camera']['id']=='09 Ceiling':score*=2
        scores.append(np.where(visible,score,0));projections.append(np.stack([pu,pv],-1))
    scores=np.asarray(scores)
    # Prefer one source for each component to reduce seams between nearby faces.
    dominant=int(np.argmax((scores*length[None,:]).sum(1))) if len(tri) else 0;scores[dominant]*=1.8
    choice=scores.argmax(0);use=scores.max(0)>0
    item=materials[obj['material']];col,row=item['cell'];category=0 if obj['material'].startswith('exterior') else 1
    sh,sw=sheets[category].shape[:2];sx,sy=sheet_offsets[category]
    final_uv=uv.copy();final_uv[:,:,0]=(sx+col*sw/4+2+uv[:,:,0]*(sw/4-4))/width
    final_uv[:,:,1]=(sy+row*sh/2+2+(1-uv[:,:,1])*(sh/2-4))/height
    shade=np.clip(.76+.22*(normal@np.array([-.351,-.281,.895])),.52,1)
    colors=shade[:,None]*np.asarray(obj['color'])[None,:]
    for i,view in enumerate(views):
        chosen=use&(choice==i);h,w=view['pixels'].shape[:2];x,y=view['offset']
        final_uv[chosen,:,0]=(x+projections[i][chosen,:,0]*w)/width
        final_uv[chosen,:,1]=(y+projections[i][chosen,:,1]*h)/height
        colors[chosen]=1 # Source views already contain their authored lighting.
    packed=np.concatenate([tri[:,:,[0,2,1]],final_uv,np.repeat(colors[:,None,:],3,axis=1)],axis=-1)
    allvertices.append(packed.reshape(-1,8));photo_area+=float(length[use].sum());total_area+=float(length.sum())
    assignments.append({'name':obj['name'],'triangle_uvs':final_uv.tolist(),'source_view':np.where(use,choice,-1).tolist(),'colors':colors.tolist()})
    statistics.append({'object':obj['name'],'projected_triangles':int(use.sum()),'triangles':len(tri),'dominant_view':cameras[dominant]['id']})
mesh=np.concatenate(allvertices).astype('<f4');assert len(mesh)<=1500000
with (OUT/'hall-detail.s2scene').open('wb') as f:
    f.write(b'S2GS0003');f.write(struct.pack('<IIII',0,len(mesh),width,height));f.write(mesh.tobytes());f.write(atlas.tobytes())
(OUT/'hall-detail.s2world').write_text((OUT/'hall-refined.s2world').read_text().replace('hall-refined.s2scene','hall-detail.s2scene'))
Image.fromarray(atlas).save(OUT/'projected-detail-atlas.png')
(OUT/'projected-detail-uv.json').write_text(json.dumps(assignments,separators=(',',':')))
report={'status':'Generated appearance projected onto complete mesh; reusable sheet UVs remain in geometry-refined.json',
    'atlas_dimensions':[width,height],'source_views':len(views),'resampled_source_images':False,'mesh_triangles':len(mesh)//3,
    'projected_area_fraction_including_hidden_faces':photo_area/total_area,'objects':statistics}
(OUT/'projected-detail.json').write_text(json.dumps(report,indent=2));print(json.dumps({k:v for k,v in report.items() if k!='objects'}),flush=True)
