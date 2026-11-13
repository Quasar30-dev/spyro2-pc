"""Give all SHARP cube faces a single DA360 depth reference.

This avoids independent monocular depth scales producing six overlapping rooms.
The keyed sky stays absent. Floor is constrained to the authored level plane.
"""
import sys,json,argparse
from pathlib import Path
import numpy as np
from PIL import Image
from scipy.ndimage import map_coordinates,gaussian_filter
from scipy.spatial.transform import Rotation
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'research/local/da360'))
from splat_reconstruct import panorama_uv,FACES

def depth(image,cache):
    if cache.exists():return np.load(cache)
    import torch
    import torch.nn.functional as F
    from networks import DA360
    torch.set_num_threads(8)
    state=torch.hub.load_state_dict_from_url('https://huggingface.co/hljiang/DA360/resolve/main/DA360_large.pth',map_location='cpu',progress=False,weights_only=True)
    h,w=state.get('height',518),state.get('width',1036)
    model=DA360(h,w,dinov2_encoder=state.get('dinov2_encoder','vitl'))
    keys=model.state_dict();model.load_state_dict({k:v for k,v in state.items() if k in keys},strict=True);del state
    model.eval().cuda()
    rgb=np.array(Image.open(image).convert('RGB'));t=torch.from_numpy(rgb.copy()).float().permute(2,0,1)[None].cuda()/255
    t=F.interpolate(t,(h,w),mode='bilinear',align_corners=False)
    mean=torch.tensor([.485,.456,.406],device='cuda')[None,:,None,None];std=torch.tensor([.229,.224,.225],device='cuda')[None,:,None,None]
    with torch.no_grad():disp=model((t-mean)/std)['pred_disp'];disp=F.interpolate(disp,rgb.shape[:2],mode='bilinear',align_corners=False)
    d=1/disp[0,0].clamp_min(1e-5).cpu().numpy();np.save(cache,d);return d

def main(args):
    root=Path(args.module);rgb=np.array(Image.open(args.image).convert('RGB'));h,w=rgb.shape[:2]
    d=depth(args.image,root/'panorama-depth.npy')
    yy,xx=np.mgrid[:h,:w];pitch=(.5-(yy+.5)/h)*np.pi;y=np.sin(pitch)
    floor=(yy>h*.67)&(yy<h*.91)
    metric=1.6/np.median((d*-y)[floor]);d*=metric
    # A continuous authored floor avoids pitch-to-pitch depth drift and allows
    # correct contact/collision. Do not flatten architecture above the pavement.
    floor_depth=1.6/np.maximum(-y,.01);weight=np.clip((yy/h-.55)/.055,0,1)
    d=d*(1-weight)+floor_depth*weight
    d=np.clip(d,.3,180)
    np.save(root/'aligned-depth.npy',d)
    merged=dict(np.load(root/'module.npz'));p=merged['positions'].copy();p[:,1]-=1.6
    old=np.linalg.norm(p,axis=1);uv=panorama_uv(p,rgb.shape)
    reference=map_coordinates(d,uv,order=1,mode='grid-wrap')
    ratio=reference/np.maximum(old,1e-5);merged['positions']=p*ratio[:,None];merged['positions'][:,1]+=1.6
    merged['positions'][:,1]=np.maximum(merged['positions'][:,1],0)
    merged['covariance']*=ratio[:,None,None]**2
    # Bound prediction floaters to the local angular sample spacing. Keep their
    # anisotropy and orientation; do not enlarge entire splats to hide holes.
    eigen,rot=np.linalg.eigh(merged['covariance'])
    max_sigma=reference*.009
    eigen=np.minimum(eigen,max_sigma[:,None]**2)
    merged['covariance']=(rot*eigen[:,None,:])@rot.transpose(0,2,1)
    # After depth alignment retain tangent detail without old per-face blobs
    # poking through the unified surface and obscuring the player.
    yaw=((xx+.5)/w-.5)*2*np.pi
    rays=np.stack([np.sin(yaw)*np.cos(pitch),np.sin(pitch),np.cos(yaw)*np.cos(pitch)],axis=-1)
    surface=rays*d[...,None];surface[...,1]=np.maximum(surface[...,1],-1.6)
    dy,dx=np.gradient(surface,axis=(0,1));normal=np.cross(dx,dy);normal/=np.maximum(np.linalg.norm(normal,axis=-1,keepdims=True),1e-9)
    n=np.stack([map_coordinates(normal[...,i],uv,order=1,mode='grid-wrap') for i in range(3)],axis=1);n/=np.maximum(np.linalg.norm(n,axis=1,keepdims=True),1e-8)
    nn=n[:,:,None]*n[:,None,:];tangent=np.eye(3)-nn
    merged['covariance']=tangent@merged['covariance']@tangent+nn*.000025
    np.savez_compressed(root/'aligned.npz',**merged)
    # Standard PLY with Blender Z-up coordinate system.
    from plyfile import PlyData,PlyElement
    b=np.array([[1,0,0],[0,0,1],[0,1,0]],dtype=float);pc=merged['positions']@b.T
    c=b@merged['covariance']@b.T;eigen,rot=np.linalg.eigh(c);rot[:,:,0]*=np.linalg.det(rot)[:,None]
    q=Rotation.from_matrix(rot).as_quat(scalar_first=True);op=np.clip(merged['opacity'],1e-5,1-1e-5)
    names=['x','y','z','f_dc_0','f_dc_1','f_dc_2','opacity','scale_0','scale_1','scale_2','rot_0','rot_1','rot_2','rot_3']
    values=np.column_stack([pc,(merged['colors']-.5)/.28209479177387814,np.log(op/(1-op)),.5*np.log(np.maximum(eigen,1e-12)),q])
    a=np.empty(len(pc),dtype=[(n,'<f4') for n in names])
    for i,n in enumerate(names):a[n]=values[:,i]
    PlyData([PlyElement.describe(a,'vertex')],text=False,byte_order='<').write(root/'aligned.ply')
    print(json.dumps({'gaussians':len(pc),'depth_scale':float(metric),'bounds':[pc.min(0).tolist(),pc.max(0).tolist()]}),flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('image');p.add_argument('module');main(p.parse_args())
