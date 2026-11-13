"""Register generated view detail against known blockout depth/semantic edges.

The camera pose is a prior, not proof that an image edit preserved each edge.
A bounded smooth warp aligns depth, normals, windows, foliage and violet areas.
Original full-resolution images stay unchanged; only texture sampling changes.
"""
from pathlib import Path
import json,sys,math
import numpy as np
import cv2
import torch
import torch.nn.functional as F
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'data/custom-levels/lanternfall/complete-hall'
names=json.loads((OUT/'depth-reference/objects.json').read_text());cameras=json.loads((OUT/'cameras.json').read_text())
fits={v['view']:v for v in json.loads((OUT/'refinement.json').read_text())['views']}
dest=OUT/'registration';dest.mkdir(exist_ok=True);reports=[]
torch.set_num_threads(8)
for camera in cameras:
    ref=np.load(OUT/'depth-reference'/(camera['id']+'.npz'));pred=np.load(OUT/'moge-detail'/(camera['id']+'.npz'))
    base=ref['depth'];h,w=base.shape;ids=ref['object_ids'];valid=base>.1
    rgb=cv2.imread(str(OUT/'generated-views'/camera.get('generated_image',camera['id']+'.png')))[...,::-1].astype(float)/255
    rgb=cv2.resize(rgb,(w,h),interpolation=cv2.INTER_AREA)
    depth=cv2.resize(pred['depth'],(w,h));a=fits[camera['id']]['inverse_depth_scale'];b=fits[camera['id']]['inverse_depth_offset']
    inverse=np.clip(a/np.maximum(depth,.01)+b,0,1);bi=np.where(valid,1/np.maximum(base,.1),0)
    sky=(rgb.min(-1)>.97)|~np.isfinite(depth);inverse[sky]=0
    rot=np.asarray(camera['camera_to_world_blender'])[:3,:3];bn=ref['normal']@rot;bn[:,:,1:]*=-1
    pn=np.nan_to_num(cv2.resize(pred['normal'],(w,h)),nan=0,posinf=0,neginf=0);same=np.sum(bn*pn,axis=-1)[valid&~sky]
    if np.median(same)<0:pn=-pn
    bn[~valid]=0;pn[sky]=0
    categories=[]
    for predicate in [lambda n:n.startswith('Topiary foliage'),lambda n:n.startswith('Vault panel') or n.startswith('Purple hanging banner')]:
        lut=np.array([predicate(n) for n in names]+[False]);categories.append(lut[np.where(ids>=0,ids,len(names))].astype(float))
    green=(rgb[:,:,1]>rgb[:,:,0]*1.08)&(rgb[:,:,1]>rgb[:,:,2]*1.08)
    purple=(rgb[:,:,2]>rgb[:,:,0]*1.03)&(rgb[:,:,2]>rgb[:,:,1]*1.3)
    target=np.concatenate([bi[:,:,None]*2,bn*.22,(~valid)[:,:,None]*.8,categories[0][:,:,None]*.65,categories[1][:,:,None]*.4],-1)
    source=np.concatenate([inverse[:,:,None]*2,pn*.22,sky[:,:,None]*.8,green[:,:,None]*.65,purple[:,:,None]*.4],-1)
    assert np.isfinite(source).all() and np.isfinite(target).all(),camera['id']
    target=torch.tensor(target,dtype=torch.float32,device='cuda').permute(2,0,1)[None]
    source=torch.tensor(source,dtype=torch.float32,device='cuda').permute(2,0,1)[None]
    flow=None
    for width,steps in [(64,100),(128,100),(256,120)]:
        height=round(width*h/w);t=F.interpolate(target,size=(height,width),mode='bilinear',align_corners=False);s=F.interpolate(source,size=(height,width),mode='bilinear',align_corners=False)
        yy,xx=torch.meshgrid((torch.arange(height,device='cuda')+.5)*2/height-1,(torch.arange(width,device='cuda')+.5)*2/width-1,indexing='ij');grid=torch.stack([xx,yy],-1)[None]
        flow=torch.zeros((1,2,height,width),device='cuda') if flow is None else F.interpolate(flow.detach(),size=(height,width),mode='bilinear',align_corners=False)
        flow.requires_grad_(True);optimizer=torch.optim.Adam([flow],lr=.002)
        for step in range(steps):
            optimizer.zero_grad();warp=F.grid_sample(s,grid+flow.permute(0,2,3,1),align_corners=False,padding_mode='border')
            photo=F.smooth_l1_loss(warp,t,beta=.04)
            dx=flow[:,:,:,1:]-flow[:,:,:,:-1];dy=flow[:,:,1:]-flow[:,:,:-1]
            smooth=(dx.square().mean()*width**2+dy.square().mean()*height**2)*.018
            loss=photo+smooth+flow.square().mean()*.025;loss.backward();optimizer.step()
            with torch.no_grad():flow.clamp_(-.12,.12)
    flow=flow.detach();warped=F.grid_sample(source,grid+flow.permute(0,2,3,1),align_corners=False,padding_mode='border')
    before=float(F.l1_loss(source,target));after=float(F.l1_loss(warped,target));offset=flow[0].permute(1,2,0).cpu().numpy()/2
    if not math.isfinite(after) or after>=before or not np.isfinite(offset).all():offset[:]=0
    np.savez_compressed(dest/(camera['id']+'.npz'),uv_offset=offset)
    reports.append({'view':camera['id'],'feature_error_before':before,'feature_error_after':after,'used':after<before,'maximum_offset_fraction':float(np.abs(offset).max()),'mean_offset_pixels_at_1448':float(np.linalg.norm(offset*np.array([1448,1086]),axis=-1).mean())})
    print(json.dumps(reports[-1]),flush=True)
(dest/'report.json').write_text(json.dumps({'method':'Bounded smooth depth/normal/semantic registration; source images unchanged','views':reports},indent=2))
