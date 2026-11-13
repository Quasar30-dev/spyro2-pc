"""GPU MoGe-2 geometry experiment, with lossless textures and explicit sky removal.

Inputs must be perspective images, not untreated equirectangular panoramas.
Outputs are visible-surface meshes, not completed volumetric game assets.
"""
from pathlib import Path
import argparse, json, struct, sys, time
import numpy as np
from PIL import Image
from scipy.ndimage import binary_dilation, label as component_labels

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'research/local/moge'))
MODEL = 'Ruicheng/moge-2-vitl-normal'

def reconstruct(model, path, dest, fov, grid, source_rect=None, scale_to_ground=True, white_background=False):
    import torch, trimesh, utils3d_moge as u3
    from moge.utils.vis import colorize_depth, colorize_normal
    source=Image.open(path).convert('RGBA')
    if source_rect is not None:
        w,h=source.size
        box=tuple(round(v*(w if i%2==0 else h)) for i,v in enumerate(source_rect))
        source=source.crop(box)
    rgba_source=np.asarray(source)
    rgb=rgba_source[:,:,:3].copy()
    source_alpha=rgba_source[:,:,3]
    # Some generated PNGs have slightly translucent (e.g. alpha 246) surfaces.
    # Treat those as geometry; reserve the mask for actually transparent areas.
    rgb[source_alpha<128]=255
    tensor = torch.from_numpy(rgb.copy()).to('cuda', torch.float32).permute(2, 0, 1) / 255
    torch.cuda.reset_peak_memory_stats()
    torch.cuda.synchronize(); begin = time.perf_counter()
    with torch.inference_mode():
        prediction = model.infer(tensor, fov_x=fov, resolution_level=9, use_fp16=True)
    torch.cuda.synchronize(); elapsed = time.perf_counter()-begin
    data = {k:v.float().cpu().numpy() for k,v in prediction.items()}
    points, depth, normals = data['points'], data['depth'], data['normal']
    key = ((rgb[...,0]>180)&(rgb[...,2]>170)&(rgb[...,1]<90))|(source_alpha<128)
    if white_background:
        labels,count=component_labels((rgb>=248).all(axis=-1))
        counts=np.bincount(labels.ravel());large=counts>=64;large[0]=False
        key|=large[labels]
    key = binary_dilation(key, iterations=2)
    valid = (data['mask']>.5)&~key&np.isfinite(points).all(axis=-1)&(depth>0)
    dest.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(dest/'geometry.npz', **data, valid=valid)
    Image.fromarray(rgb).save(dest/'texture.png')
    Image.fromarray((valid*255).astype(np.uint8)).save(dest/'mask.png')
    Image.fromarray(colorize_depth(depth, mask=valid)).save(dest/'depth.png')
    Image.fromarray(colorize_normal(normals)).save(dest/'normals.png')

    # Sample a shared pixel grid so topology, point positions, normals and UVs agree.
    # Keep the original texture resolution; grid resolution controls geometry only.
    h,w = rgb.shape[:2]
    yy=np.unique(np.linspace(0,h-1,max(2,round(grid*h/max(h,w)))).astype(int))
    xx=np.unique(np.linspace(0,w-1,max(2,round(grid*w/max(h,w)))).astype(int))
    iy,ix=np.meshgrid(yy,xx,indexing='ij')
    p=points[iy,ix]; d=depth[iy,ix]; n=normals[iy,ix]
    uv=np.stack([(ix+.5)/w,(iy+.5)/h],axis=-1)
    # Do not erase bands of vertices at depth/normal edges: that produces black
    # cracks around arches even from the original camera. Keep valid samples and
    # handle true occlusion discontinuities per triangle instead.
    keep=valid[iy,ix]
    faces,vertices,uvs,vnormals=u3.np.build_mesh_from_map(p,uv,n,mask=keep,tri=True)
    if not len(faces):raise RuntimeError(f'No usable geometry: {path}')
    tri=vertices[faces];tri_normals=vnormals[faces]
    z=tri[:,:,2];far_index=z.argmax(axis=1);row=np.arange(len(faces))
    anchor=tri[row,far_index];plane_normal=tri_normals[row,far_index]
    plane_error=np.abs(((tri-anchor[:,None,:])*plane_normal[:,None,:]).sum(-1)).max(axis=1)
    jump=(z.max(axis=1)-z.min(axis=1))/np.maximum(z.min(axis=1),1e-6)
    # A receding planar floor can have a large depth gradient, but has little
    # distance from the estimated tangent plane. Never disconnect it for that.
    split=(jump>.12)&(plane_error>.035*z.max(axis=1))
    changed=np.flatnonzero(split)
    if len(changed):
        # Continue the *background* tangent plane through the tiny source-pixel
        # triangle at an occlusion edge. This avoids both an empty strip and a
        # stretched triangle joining a foreground lantern to the distant wall.
        # It is a bounded inferred seam patch, not recovered hidden geometry.
        rays=tri[changed]/z[changed,:,None]
        numerator=(anchor[changed]*plane_normal[changed]).sum(-1)
        denominator=(rays*plane_normal[changed,None,:]).sum(-1)
        projected_z=np.divide(numerator[:,None],denominator,out=np.full_like(denominator,np.nan),where=np.abs(denominator)>1e-5)
        far_z=z[changed].max(axis=1)
        reliable=np.isfinite(projected_z).all(axis=1)&(projected_z>.7*far_z[:,None]).all(axis=1)&(projected_z<1.3*far_z[:,None]).all(axis=1)
        projected_z[~reliable]=far_z[~reliable,None]
        patched=rays*projected_z[:,:,None]
        # Use a background sample, avoiding a stretched foreground-colored fringe.
        patch_uv=np.repeat(uvs[faces[changed,far_index[changed]]][:,None,:],3,axis=1)
        patch_n=np.repeat(plane_normal[changed,None,:],3,axis=1)
        base=len(vertices);vertices=np.concatenate([vertices,patched.reshape(-1,3)])
        uvs=np.concatenate([uvs,patch_uv.reshape(-1,2)])
        vnormals=np.concatenate([vnormals,patch_n.reshape(-1,3)])
        faces[changed]=np.arange(base,base+len(changed)*3).reshape(-1,3)
    # Estimate a useful authoring scale from the visible central ground region.
    # MoGe's metric output on stylized art is a prediction, not a measurement.
    region=valid.copy();region[:int(h*.8)]=False
    region[:,:int(w*.25)]=False;region[:,int(w*.75):]=False
    ys=points[...,1][region];ys=ys[ys>.1]
    ground_distance=float(np.median(ys)) if len(ys)>20 else None
    scale=1.6/ground_distance if ground_distance and scale_to_ground else 1.0
    native=vertices*np.array([scale,-scale,scale]);native[:,1]+=1.6
    glverts=vertices*np.array([scale,-scale,-scale]);glverts[:,1]+=1.6
    material=trimesh.visual.material.PBRMaterial(baseColorTexture=Image.fromarray(rgb),metallicFactor=0,roughnessFactor=1,doubleSided=True)
    mesh=trimesh.Trimesh(vertices=glverts,faces=faces,vertex_normals=vnormals*np.array([1,-1,-1]),
        visual=trimesh.visual.texture.TextureVisuals(uv=uvs*np.array([1,-1])+np.array([0,1]),material=material),process=False)
    mesh.export(dest/'mesh.glb')
    # Native D3D surface packet (no Gaussian substitute). Separate collision later.
    packed=np.concatenate([native,uvs],axis=1)[faces].reshape(-1,5).astype('<f4')
    if len(packed)>1500000:raise ValueError('Mesh exceeds native packet vertex limit; lower --grid')
    rgba=np.dstack([rgb,np.where(key,0,255).astype(np.uint8)])
    with (dest/'mesh.s2scene').open('wb') as out:
        out.write(b'S2GS0002');out.write(struct.pack('<IIII',0,len(packed),w,h))
        out.write(packed.tobytes());out.write(rgba.tobytes())
    fx,fy=u3.np.intrinsics_to_fov(data['intrinsics'])
    lengths=np.linalg.norm(normals[valid],axis=-1)
    report={
        'source':str(path.relative_to(ROOT) if path.is_relative_to(ROOT) else path),
        'source_rectangle':source_rect, 'model':MODEL, 'device':torch.cuda.get_device_name(),
        'inference_seconds':elapsed, 'peak_cuda_allocated_gib':torch.cuda.max_memory_allocated()/2**30,
        'input_size':[w,h], 'input_fov_x':fov, 'fov_x':float(np.degrees(fx)), 'fov_y':float(np.degrees(fy)),
        'normalized_intrinsics':data['intrinsics'].tolist(), 'valid_fraction':float(valid.mean()),
        'normal_length_median':float(np.median(lengths)), 'vertices':len(vertices),'triangles':len(faces),
        'estimated_ground_camera_height':ground_distance,'authoring_scale':scale,
        'depth_percentiles':np.percentile(depth[valid],[5,50,95]).tolist(),
        'watertight':bool(mesh.is_watertight), 'mesh_bounds':mesh.bounds.tolist(),
        'camera_glb':{'position':[0,1.6,0],'look_direction':[0,0,-1]},
        'camera_native':{'position':[0,1.6,0],'look_direction':[0,0,1]},
        'edge_filter':'Continuous valid triangles; discontinuities receive bounded background tangent-plane seam patches',
        'inferred_seam_triangles':int(len(changed)),
        'inferred_seam_fraction':float(len(changed)/len(faces)),
        'status':'Visible-surface study. Hidden surfaces, wall thickness, collision and complete interiors still require reconstruction/polish.'
    }
    (dest/'report.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report),flush=True)
    return report

def main(args):
    import torch
    from moge.model.v2 import MoGeModel
    torch.set_num_threads(8)
    if not torch.cuda.is_available():raise RuntimeError('This experiment requires a CUDA GPU')
    print('Loading '+MODEL,flush=True)
    model=MoGeModel.from_pretrained(MODEL).to('cuda').eval()
    reports=[]
    for source in args.images:
        path=Path(source).resolve()
        label=path.parent.name+'-'+path.stem
        modes=[('estimated',None)]+([('known',args.fov)] if args.fov is not None else [])
        for mode,fov in modes:
            reports.append(reconstruct(model,path,Path(args.output)/label/mode,fov,args.grid,white_background=args.white_background))
    if args.viewset:
        manifest=Path(args.viewset).resolve()
        for view in json.loads(manifest.read_text())['views']:
            # Generated views have uncalibrated poses/FOV. Keep independent meshes
            # until registration; never fuse them using nominal prompt angles.
            path=(manifest.parent/view['image']).resolve()
            reports.append(reconstruct(model,path,Path(args.output)/view['id'],None,args.grid,
                source_rect=view['source_rectangle'],scale_to_ground=view['kind']=='interior'))
    (Path(args.output)/'comparison.json').write_text(json.dumps(reports,indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('images',nargs='*');p.add_argument('--viewset');p.add_argument('--output',required=True)
    p.add_argument('--fov',type=float,help='Also compare against this known horizontal FOV in degrees')
    p.add_argument('--grid',type=int,default=384)
    p.add_argument('--white-background',action='store_true',help='Mask explicitly blank pure-white connected regions in an authoring reference')
    args=p.parse_args()
    if not args.images and not args.viewset:p.error('Provide perspective images or --viewset')
    if args.fov is not None and not 1<args.fov<175:p.error('FOV must be between 1 and 175 degrees')
    if not 16<=args.grid<=500:p.error('Grid must be between 16 and 500 for native mesh limits')
    main(args)
