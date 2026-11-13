"""GPT Image equirectangular module -> SHARP Gaussian cube views (no training).

Run with build/splat-python/Scripts/python.exe. The source remains immutable.
Sky is keyed from SOURCE rays, not merely the predicted Gaussian color.
Intermediate per-face predictions are cached for repeatable alignment/export.
"""
import argparse
import json
from pathlib import Path
import numpy as np
from PIL import Image
from scipy.ndimage import map_coordinates, binary_dilation
from scipy.spatial.transform import Rotation

FACES = [("front", [0,0,1], [0,1,0]), ("right", [1,0,0], [0,1,0]),
         ("back", [0,0,-1], [0,1,0]), ("left", [-1,0,0], [0,1,0]),
         ("up", [0,1,0], [0,0,-1]), ("down", [0,-1,0], [0,0,1])]

def basis(forward, up):
    f,u=np.array(forward,dtype=float),np.array(up,dtype=float)
    return np.stack([np.cross(u,f),-u,f],axis=1)

def panorama_uv(d, shape):
    h,w=shape[:2]
    norm=np.linalg.norm(d,axis=-1)
    x=(np.arctan2(d[...,0],d[...,2])/(2*np.pi)+.5)*w-.5
    y=(.5-np.arcsin(np.clip(d[...,1]/np.maximum(norm,1e-6),-1,1))/np.pi)*h-.5
    return np.stack([np.clip(y,0,h-1),x%w])

def cube_image(rgb, rotation, n=1024, fov=100):
    f=n/(2*np.tan(np.deg2rad(fov)/2))
    y,x=np.mgrid[:n,:n]
    rays=np.stack([(x-(n-1)/2)/f,(y-(n-1)/2)/f,np.ones_like(x)],axis=-1)@rotation.T
    coords=panorama_uv(rays,rgb.shape)
    return np.stack([map_coordinates(rgb[...,c],coords,order=1,mode='grid-wrap') for c in range(3)],-1).astype(np.uint8),f

def reconstruct(args):
    import torch
    from sharp.models import PredictorParams,create_predictor
    from sharp.cli.predict import predict_image,DEFAULT_MODEL_URL
    from sharp.utils.color_space import linearRGB2sRGB
    torch.set_num_threads(8)
    if not torch.cuda.is_available():
        raise RuntimeError('CUDA required for this authoring preset; gameplay has no Python dependency.')
    root=Path(args.output);root.mkdir(parents=True,exist_ok=True)
    rgb=np.array(Image.open(args.image).convert('RGB'))
    if rgb.shape[1]!=2*rgb.shape[0]:raise ValueError('Input must be a 2:1 equirectangular panorama')
    key=(rgb[...,0]>180)&(rgb[...,2]>170)&(rgb[...,1]<90)
    key=binary_dilation(key,iterations=2)
    model=None
    for name,fwd,up in FACES:
        out=root/(name+'.npz')
        if out.exists():continue
        r=basis(fwd,up);img,f=cube_image(rgb,r)
        Image.fromarray(img).save(root/(name+'.png'))
        print('Predicting',name,flush=True)
        if model is None:
            state=torch.hub.load_state_dict_from_url(DEFAULT_MODEL_URL,progress=False,map_location='cpu',weights_only=True)
            model=create_predictor(PredictorParams());model.load_state_dict(state);del state
            model.eval().to('cuda')
        g=predict_image(model,img,f,torch.device('cuda'))
        p=g.mean_vectors.reshape(-1,3).cpu().numpy()@r.T
        q=g.quaternions.reshape(-1,4).cpu().numpy()
        scales=g.singular_values.reshape(-1,3).cpu().numpy()
        color=linearRGB2sRGB(g.colors.reshape(-1,3)).cpu().numpy()
        opacity=g.opacities.reshape(-1).cpu().numpy()
        # Keep cube ownership only at merge time: overlap is needed to align scale.
        coords=panorama_uv(p,rgb.shape)
        masked=map_coordinates(key.astype(float),coords,order=0,mode='grid-wrap')>.5
        valid=(~masked)&np.isfinite(p).all(1)&(opacity>.02)&(np.linalg.norm(p,axis=1)<500)
        valid &= ~((color[:,0]>.7)&(color[:,2]>.65)&(color[:,1]<.3))
        idx=np.flatnonzero(valid)
        # A deterministic uniform sample controls authoring/runtime cost; widen
        # footprint to retain image coverage. Full predictions can be requested.
        stride=max(1,int(np.ceil(len(idx)/args.face_budget)))
        idx=idx[::stride]
        p,q,scales,color,opacity=p[idx],q[idx],scales[idx]*np.sqrt(stride),color[idx],opacity[idx]
        rot=Rotation.from_quat(q,scalar_first=True).as_matrix()
        axes=np.einsum('ij,njk->nik',r,rot)*scales[:,None,:]
        covariance=axes@axes.transpose(0,2,1)
        np.savez_compressed(out,positions=p.astype('f4'),covariance=covariance.astype('f4'),colors=color.astype('f4'),opacity=opacity.astype('f4'))
        print(name,len(idx),'Gaussians, sky rejected',int(masked.sum()),flush=True)
        del g;torch.cuda.empty_cache()

def merge(args):
    from scipy.spatial import cKDTree
    from plyfile import PlyData,PlyElement
    root=Path(args.output);data=[dict(np.load(root/(f[0]+'.npz'))) for f in FACES]
    # Solve relative face scales from shared viewing rays. This corrects scale
    # drift, not arbitrary disagreement about hidden geometry.
    equations=[];targets=[];residuals=[]
    for i,a in enumerate(data):
        pa=a['positions'];da=np.linalg.norm(pa,axis=1);ua=pa/da[:,None]
        sample=np.arange(0,len(pa),max(1,len(pa)//16000));tree=cKDTree(ua[sample])
        for j in range(i+1,len(data)):
            pb=data[j]['positions'][::max(1,len(data[j]['positions'])//16000)]
            db=np.linalg.norm(pb,axis=1);ub=pb/db[:,None]
            dist,nn=tree.query(ub);ok=(dist<.009)&(db<60)&(da[sample[nn]]<60)
            if ok.sum()<40:continue
            ratio=np.log(da[sample[nn]][ok]/db[ok]);row=np.zeros(6);row[j]=1;row[i]=-1
            equations.append(row);targets.append(float(np.median(ratio)))
            residuals.append(dict(a=FACES[i][0],b=FACES[j][0],matches=int(ok.sum()),median_ratio=float(np.exp(np.median(ratio)))))
    equations.append([1,0,0,0,0,0]);targets.append(0)
    scales=np.exp(np.linalg.lstsq(equations,targets,rcond=None)[0])
    floor=[]
    for i,d in enumerate(data):
        p=d['positions']*scales[i];radius=np.linalg.norm(p,axis=1)
        good=(p[:,1]<-.2)&(-p[:,1]/radius>.72)&(d['opacity']>.4)
        floor.extend((-p[good,1]).tolist())
    metric=args.eye_height/np.median(floor)
    pieces=[]
    for i,d in enumerate(data):
        p=d['positions'];directions=np.stack([p@np.array(f[1]) for f in FACES],axis=1)
        own=np.argmax(directions,axis=1)==i
        s=scales[i]*metric
        d={k:v[own].copy() for k,v in d.items()};d['positions']*=s;d['positions'][:,1]+=args.eye_height
        d['covariance']*=s*s
        pieces.append(d)
    merged={k:np.concatenate([d[k] for d in pieces]) for k in pieces[0]}
    np.savez_compressed(root/'module.npz',**merged)
    # Standard 3DGS PLY in Blender Z-up coordinates, preserving full covariance.
    b=np.array([[1,0,0],[0,0,1],[0,1,0]],dtype=float)
    p=merged['positions']@b.T;c=b@merged['covariance']@b.T
    eigen,rot=np.linalg.eigh(c);rot[:,:,0]*=np.linalg.det(rot)[:,None]
    q=Rotation.from_matrix(rot).as_quat(scalar_first=True)
    names=['x','y','z','f_dc_0','f_dc_1','f_dc_2','opacity','scale_0','scale_1','scale_2','rot_0','rot_1','rot_2','rot_3']
    op=np.clip(merged['opacity'],1e-5,1-1e-5)
    values=np.column_stack([p,(merged['colors']-.5)/.28209479177387814,np.log(op/(1-op)),np.log(np.sqrt(np.maximum(eigen,1e-12))),q]).astype('f4')
    records=np.empty(len(p),dtype=[(n,'<f4') for n in names])
    for i,n in enumerate(names):records[n]=values[:,i]
    PlyData([PlyElement.describe(records,'vertex')],text=False,byte_order='<').write(root/'module.ply')
    report=dict(source=str(Path(args.image)),gaussians=len(p),eye_height=args.eye_height,face_scales=scales.tolist(),metric_scale=float(metric),overlaps=residuals,bounds=[p.min(0).tolist(),p.max(0).tolist()],limitations='Single-center reconstruction. Unseen surfaces and translation disocclusions require additional source views or geometry. Overlap fit corrects relative scale only.')
    (root/'reconstruction.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report),flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('image');p.add_argument('output');p.add_argument('--face-budget',type=int,default=180000);p.add_argument('--eye-height',type=float,default=1.6);p.add_argument('--merge-only',action='store_true')
    a=p.parse_args()
    if not a.merge_only:reconstruct(a)
    merge(a)
