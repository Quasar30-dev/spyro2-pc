"""Compare actual exported texture samples across paving joins and the inlay.

This checks the baked UVs/atlas independently of the material generator, so a
per-object reset, photo fallback or mismatched compass background fails the gate.
"""
from pathlib import Path
import json
import numpy as np
import cv2
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'data/custom-levels/lanternfall/complete-hall'
records=json.loads((OUT/'geometry-polished.json').read_text())
bakes={o['name']:o for o in json.loads((OUT/'hall-polished-uv.json').read_text())}
atlas=np.asarray(Image.open(OUT/'hall-polished-atlas.png').convert('RGB'),dtype=np.float32)
h,w=atlas.shape[:2];surfaces=[]
for obj in records:
    if obj['material'] not in ['interior.paving','interior.compass']:continue
    tri=np.asarray(obj['vertices'])[obj['faces']]
    cross=np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0])
    center=np.asarray(obj['vertices']).mean(0)
    cross[np.sum((tri.mean(1)-center)*cross,axis=1)<0]*=-1
    top=cross[:,2]>np.abs(cross[:,:2]).sum(1)
    t=tri[top,:,:2]
    surfaces.append({'name':obj['name'],'tri':t,'uv':np.asarray(bakes[obj['name']]['triangle_uvs'])[top],'rgb':np.asarray(bakes[obj['name']]['colors'])[top],
                     'lo':t.min((0,1)),'hi':t.max((0,1))})

def sample(surface,points):
    t=surface['tri'];a=t[:,1]-t[:,0];b=t[:,2]-t[:,0]
    d=a[:,0]*b[:,1]-a[:,1]*b[:,0]
    r=points[:,None,:]-t[None,:,0,:]
    v=(r[:,:,0]*b[:,1]-r[:,:,1]*b[:,0])/d
    q=(a[:,0]*r[:,:,1]-a[:,1]*r[:,:,0])/d
    bary=np.stack([1-v-q,v,q],-1)
    inside=(bary>=-1e-5).all(-1)
    assert inside.any(1).all(),surface['name']+' sample outside geometry'
    face=inside.argmax(1)
    uv=np.einsum('pi,pij->pj',bary[np.arange(len(points)),face],surface['uv'][face])
    return cv2.remap(atlas,(uv[:,0,None]*w-.5).astype(np.float32),(uv[:,1,None]*h-.5).astype(np.float32),cv2.INTER_LINEAR)[:,0,:]*surface['rgb'][face]

paving=[s for s in surfaces if s['name'].startswith('Paving')]
inlay=next(s for s in surfaces if s['name']=='Compass floor inlay')
checks=[];all_errors=[]
def compare(a,b,points,kind):
    error=np.abs(sample(a,points)-sample(b,points)).mean(1)
    all_errors.extend(error.tolist())
    checks.append({'a':a['name'],'b':b['name'],'kind':kind,'samples':len(points),'mean_rgb_error_255':float(error.mean()),'maximum_rgb_error_255':float(error.max())})

for ai,a in enumerate(paving):
    for b in paving[ai+1:]:
        for axis in [0,1]:
            other=1-axis
            boundary=a['hi'][axis] if abs(a['hi'][axis]-b['lo'][axis])<1e-5 else b['hi'][axis] if abs(b['hi'][axis]-a['lo'][axis])<1e-5 else None
            if boundary is None:continue
            lo=max(a['lo'][other],b['lo'][other],0 if other==1 else -4.4)
            hi=min(a['hi'][other],b['hi'][other],18 if other==1 else 4.4)
            if hi-lo<.1 or (axis==1 and not 0<boundary<18):continue
            p=np.zeros((37,2));p[:,axis]=boundary;p[:,other]=np.linspace(lo+.03,hi-.03,len(p))
            compare(a,b,p,'mesh join')
for a in paving:
    lo=np.maximum(a['lo'],inlay['lo'])+.01;hi=np.minimum(a['hi'],inlay['hi'])-.01
    if (hi<=lo).any():continue
    xx,yy=np.meshgrid(np.linspace(lo[0],hi[0],17),np.linspace(lo[1],hi[1],17))
    compare(a,inlay,np.stack([xx.ravel(),yy.ravel()],-1),'inlay overlap')

e=np.asarray(all_errors);assert len(e)>1000
mean=float(e.mean());p95=float(np.percentile(e,95));maximum=float(e.max())
passed=mean<3 and p95<8 and maximum<32
report={'status':'pass' if passed else 'fail','samples':len(e),'joins_and_overlaps':len(checks),
        'mean_rgb_error_255':mean,'p95_rgb_error_255':p95,'maximum_rgb_error_255':maximum,
        'limits':{'mean':3,'p95':8,'maximum':32},'checks':checks,
        'scope':'Actual atlas samples at common world positions; small filtering differences allowed. Source motif quality also requires overhead and oblique visual review.'}
(OUT/'floor-continuity.json').write_text(json.dumps(report,indent=2))
print(json.dumps({k:v for k,v in report.items() if k!='checks'}))
assert passed,'Floor material continuity regression'
