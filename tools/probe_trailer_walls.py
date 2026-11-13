"""Read-only mesh/ray survey for a real building breach capture."""
from pathlib import Path
import json,numpy as np
root=Path(__file__).resolve().parents[1]
folder=root/'output/spyro-powers-trailer/source/autumn-reference'
d=json.loads((folder/'reference-33.json').read_text())
t=np.array(d['vertices'])[:,:3].reshape(-1,3,3)
e1=t[:,1]-t[:,0];e2=t[:,2]-t[:,0]
n=np.cross(e1,e2);area=np.linalg.norm(n,axis=1);n/=np.maximum(area,1e-10)[:,None]
def ray(origin,direction,distance):
    v=np.array(direction);h=np.cross(v,e2);a=np.einsum('ij,ij->i',e1,h)
    inv=np.divide(1,a,out=np.zeros_like(a),where=abs(a)>1e-8)
    s=np.array(origin)-t[:,0];u=inv*np.einsum('ij,ij->i',s,h);q=np.cross(s,e1)
    w=inv*(q@v);r=inv*np.einsum('ij,ij->i',e2,q)
    ok=(abs(a)>1e-8)&(u>=-1e-6)&(w>=-1e-6)&(u+w<=1.000001)&(r>.001)&(r<distance)
    ids=np.where(ok)[0]
    if not len(ids):return None
    i=ids[np.argmin(r[ids])];return np.array(origin)+v*r[i],int(i)
rows=[]
for i in np.argsort(area)[::-1]:
    if abs(n[i,1])>.05 or area[i]<3:continue
    c=t[i].mean(0)
    for direction in (n[i],-n[i]):
        outer=ray(c+direction*3+[0,.2,0],[0,-1,0],15)
        inner=ray(c-direction*3+[0,.2,0],[0,-1,0],15)
        if outer is None or inner is None or abs(outer[0][1]-inner[0][1])>.5:continue
        floor=max(outer[0][1],inner[0][1]);hit=c.copy();hit[1]=floor+.9
        # The intended opening must be within the sampled wall and have floor
        # behind it. This rejects dents in cliff masses and shots into sky.
        obstruction=ray(outer[0]+[0,.9,0],-direction,6)
        if obstruction is None or abs(np.dot(obstruction[0]-hit,direction))>.1:continue
        roof=ray(inner[0]+[0,.1,0],[0,1,0],12)
        if roof is None:continue
        if any(np.linalg.norm(np.array(d['objects'][o['record']]['position'])-hit)<5 for o in d['npcs']):continue
        rows.append(dict(triangle=int(i),area=round(float(area[i]),2),spawn=outer[0].tolist(),normal=direction.tolist(),hit=obstruction[0].tolist(),inside=inner[0].tolist(),roof=roof[0].tolist()))
    if len(rows)>=25:break
(folder/'wall-candidates.json').write_text(json.dumps(rows,indent=2))
for row in rows:print(row)
