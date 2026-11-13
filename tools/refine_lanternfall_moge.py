"""Conservative MoGe relief fitted onto a complete, independently textured mesh.

Generated views are not calibrated measurements. Reject inconsistent evidence,
pin original triangle boundaries, retain all surfaces and keep the atlas UVs.
This experiment adds bounded shallow relief, not invented missing architecture.
"""
from pathlib import Path
import json,sys,math,time,hashlib
import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter,minimum_filter,maximum_filter
from scipy.optimize import least_squares

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'data/custom-levels/lanternfall/complete-hall'
sys.path.insert(0,str(ROOT/'research/local/moge'))

def infer_views():
    import torch
    from moge.model.v2 import MoGeModel
    torch.set_num_threads(8)
    model=MoGeModel.from_pretrained('Ruicheng/moge-2-vitl-normal').to('cuda').eval()
    dest=OUT/'moge-detail';dest.mkdir(exist_ok=True)
    for camera in json.loads((OUT/'cameras.json').read_text()):
        target=dest/(camera['id']+'.npz')
        source=OUT/'generated-views'/camera.get('generated_image',camera['id']+'.png')
        digest=hashlib.sha256(source.read_bytes()).hexdigest()
        if target.exists():
            with np.load(target) as cached:
                if 'source_sha256' in cached and str(cached['source_sha256'])==digest:continue
        rgb=np.asarray(Image.open(source).convert('RGB'))
        tensor=torch.from_numpy(rgb.copy()).to('cuda',torch.float32).permute(2,0,1)/255
        start=time.perf_counter()
        with torch.inference_mode():
            prediction=model.infer(tensor,fov_x=camera['horizontal_fov_degrees'],resolution_level=9,use_fp16=True)
        arrays={k:v.float().cpu().numpy() for k,v in prediction.items()}
        np.savez_compressed(target,**arrays,source_sha256=digest)
        print('MoGe',camera['id'],round(time.perf_counter()-start,2),'s',flush=True)

def fitted_views():
    import cv2
    output=[];reports=[]
    for camera in json.loads((OUT/'cameras.json').read_text()):
        ref=np.load(OUT/'depth-reference'/(camera['id']+'.npz'))
        inferred=np.load(OUT/'moge-detail'/(camera['id']+'.npz'))
        base=ref['depth'];h,w=base.shape
        depth=cv2.resize(inferred['depth'],(w,h),interpolation=cv2.INTER_LINEAR)
        mask=cv2.resize(inferred['mask'],(w,h),interpolation=cv2.INTER_NEAREST)>.5
        # The blockout supplies sky/occlusion masks: glowing lamps are not sky.
        valid=(base>.1)&np.isfinite(depth)&(depth>.1)&mask
        stable=valid&(maximum_filter(base,3)-minimum_filter(base,3)<.15)
        if stable.sum()<100:raise RuntimeError('Insufficient stable samples: '+camera['id'])
        x=1/depth[stable];y=1/base[stable]
        step=max(1,len(x)//12000);x=x[::step];y=y[::step]
        scale=float(np.median(y/x))
        fit=least_squares(lambda ab:ab[0]*x+ab[1]-y,[scale,0],loss='soft_l1',f_scale=.01,bounds=([.01,-2],[100,2]))
        inv=fit.x[0]/np.maximum(depth,.01)+fit.x[1]
        aligned=np.divide(1,inv,out=np.zeros_like(inv),where=inv>.001)
        error=aligned-base
        accepted=stable&(np.abs(error)<np.maximum(.15,base*.08))
        # Remove broad layout/depth drift. Only local residual relief is eligible.
        weight=gaussian_filter(accepted.astype(float),3)
        drift=gaussian_filter(np.where(accepted,error,0),3)/np.maximum(weight,1e-8)
        relief=np.where(accepted& (weight>.7),np.clip(error-drift,-.04,.04),0)
        eligibility=minimum_filter((accepted&(weight>.7)).astype(np.uint8),3)>0
        matrix=np.asarray(camera['camera_to_world_blender']);rot=matrix[:3,:3]
        reports.append({'view':camera['id'],'inverse_depth_scale':float(fit.x[0]),'inverse_depth_offset':float(fit.x[1]),
            'stable_samples':int(stable.sum()),'accepted_samples':int(eligibility.sum()),'median_absolute_aligned_error_m':float(np.median(np.abs(error[stable]))),
            'eligible_relief_p95_m':float(np.percentile(np.abs(relief[eligibility]),95)) if eligibility.any() else 0})
        output.append({'depth':base,'relief':relief,'valid':eligibility,'ids':ref['object_ids'],'rot':rot,'origin':matrix[:3,3],
            'tan_x':math.tan(math.radians(camera['horizontal_fov_degrees']/2))})
    return output,reports

def subdivided_barycentrics(levels=2):
    triangles=np.eye(3)[None,:,:]
    for _ in range(levels):
        a,b,c=triangles[:,0],triangles[:,1],triangles[:,2]
        ab=(a+b)/2;bc=(b+c)/2;ca=(c+a)/2
        triangles=np.concatenate([np.stack(t,1) for t in [(a,ab,ca),(ab,b,bc),(ca,bc,c),(ab,bc,ca)]])
    return triangles

def refine():
    views,reports=fitted_views();records=json.loads((OUT/'geometry.json').read_text())
    object_ids={name:i for i,name in enumerate(json.loads((OUT/'depth-reference/objects.json').read_text()))}
    bary=subdivided_barycentrics();results=[];stats=[];before=after=changed=0;maximum=0
    for obj in records:
        vertices=np.asarray(obj['vertices']);faces=np.asarray(obj['faces']);uv=np.asarray(obj['triangle_uvs']);tri=vertices[faces]
        before+=len(faces)
        if obj['material'] not in ['interior.wall','interior.carved_trim','interior.vault','interior.paving']:
            results.append(obj);after+=len(faces);continue
        normal=np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0]);area=np.linalg.norm(normal,axis=1)
        keep=area>1e-9;tri=tri[keep];normal=normal[keep]/area[keep,None];uv=uv[keep]
        inward=((tri.mean(1)-vertices.mean(0))*normal).sum(-1)<0;normal[inward]*=-1
        dense=np.einsum('kij,tjc->tkic',bary,tri).reshape(-1,3)
        dense_uv=np.einsum('kij,tjc->tkic',bary,uv).reshape(-1,3,2)
        normals=np.repeat(normal,len(bary)*3,axis=0)
        # Exactly zero on every original edge: no displacement cracks at UV seams,
        # sharp corners, separate module joins, doors, windows or floor boundaries.
        taper=np.tile(np.clip(bary.min(-1).reshape(-1)*4,0,1),len(tri))
        evidence=[]
        for view in views:
            local=(dense-view['origin'])@view['rot'];z=-local[:,2];h,w=view['depth'].shape
            safe=np.maximum(z,.001);nx=local[:,0]/safe/view['tan_x'];ny=local[:,1]/safe/(view['tan_x']*h/w)
            px=np.clip(((nx+1)*w/2).astype(int),0,w-1);py=np.clip(((1-ny)*h/2).astype(int),0,h-1)
            dot=normals@(-view['rot'][:,2])
            visible=(z>.1)&(np.abs(nx)<.98)&(np.abs(ny)<.98)&(dot<-.2)&(taper>0)
            visible&=view['valid'][py,px]&(view['ids'][py,px]==object_ids[obj['name']])
            visible&=np.abs(view['depth'][py,px]-z)<.1+.015*z
            displacement=view['relief'][py,px]/np.minimum(dot,-.2)
            evidence.append(np.where(visible,displacement,np.nan))
        values=np.stack(evidence);count=np.isfinite(values).sum(0)
        # Two agreeing, independently positioned views are required for relief.
        sums=np.nansum(values,axis=0);mean=sums/np.maximum(count,1)
        deviation=np.sqrt(np.nansum((values-mean)**2,axis=0)/np.maximum(count,1))
        accepted=(count>=2)&(deviation<.02)
        shift=np.where(accepted,np.clip(mean,-.03,.03)*taper,0)
        dense+=normals*shift[:,None]
        if not np.isfinite(dense).all():raise RuntimeError('Nonfinite refined geometry')
        n=int((np.abs(shift)>1e-5).sum());changed+=n;maximum=max(maximum,float(np.abs(shift).max(initial=0)))
        result=dict(obj);result['vertices']=dense.tolist();result['faces']=np.arange(len(dense)).reshape(-1,3).tolist();result['triangle_uvs']=dense_uv.tolist()
        result['refinement']='MoGe residual, two agreeing views, edges pinned, atlas UVs preserved'
        results.append(result);after+=len(dense)//3
        stats.append({'object':obj['name'],'triangles':len(dense)//3,'displaced_vertex_entries':n})
    if after>500000:raise RuntimeError('Native mesh budget exceeded')
    (OUT/'geometry-refined.json').write_text(json.dumps(results,separators=(',',':')))
    report={'status':'bounded relief prototype; not a reconstruction of all generated ornament',
        'source_triangles':before,'refined_triangles':after,'displaced_vertex_entries':changed,'maximum_displacement_m':maximum,
        'material_source':'Separate original GPT Image texture sheets; all semantic IDs and interpolated UVs retained',
        'collision':'Original structural mesh; relief is at most 3 cm and pins every source edge',
        'views':reports,'objects':stats}
    (OUT/'refinement.json').write_text(json.dumps(report,indent=2));print(json.dumps({k:v for k,v in report.items() if k not in ['objects','views']}),flush=True)

if __name__=='__main__':
    if '--fit-only' not in sys.argv:infer_views()
    refine()
