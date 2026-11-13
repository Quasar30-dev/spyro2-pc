"""Add closed leaf volumes and shallow carved mouldings to the fitted hall."""
from pathlib import Path
import json,math,random
import numpy as np
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'data/custom-levels/lanternfall/complete-hall'
records=json.loads((OUT/'geometry-refined.json').read_text());added=[]

def record(name,vertices,faces,material,color=(1,1,1)):
    p=np.asarray(vertices);f=np.asarray(faces);uv=[]
    for triangle in p[f]:
        n=np.cross(triangle[1]-triangle[0],triangle[2]-triangle[0]);axis=np.argmax(abs(n));axes=[i for i in range(3) if i!=axis]
        q=triangle[:,axes];lo=q.min(0);extent=np.maximum(q.max(0)-lo,1e-6);uv.append(((q-lo)/extent).tolist())
    added.append({'name':name,'vertices':p.tolist(),'faces':f.tolist(),'triangle_uvs':uv,'material':material,'color':list(color),'collision':False,'detail_geometry':'Closed volumetric detail on preserved structural mesh'})

def tube(path,radius=.012,sides=6):
    path=np.asarray(path);vertices=[];faces=[]
    for i,p in enumerate(path):
        tangent=path[(i+1)%len(path)]-path[(i-1)%len(path)];tangent/=np.linalg.norm(tangent)
        axis=np.array([0.,0,1]) if abs(tangent[2])<.85 else np.array([1.,0,0])
        a=np.cross(tangent,axis);a/=np.linalg.norm(a);b=np.cross(tangent,a)
        vertices.extend(p+radius*(math.cos(j*2*math.pi/sides)*a+math.sin(j*2*math.pi/sides)*b) for j in range(sides))
    for i in range(len(path)):
        for j in range(sides):
            a=i*sides+j;b=i*sides+(j+1)%sides;c=((i+1)%len(path))*sides+(j+1)%sides;d=((i+1)%len(path))*sides+j
            faces.extend([(a,b,c),(a,c,d)])
    return vertices,faces

rng=random.Random(2719)
for side in [-1,1]:
    for y in [1,7,13,17]:
        cx=side*3.55;verts=[];faces=[]
        for k,(z,r) in enumerate([(.68,.49),(1.02,.45),(1.36,.39),(1.7,.31),(2.,.23),(2.27,.14)]):
            for ring in range(3):
                fraction=.14+ring*.26;radius=r*(1-fraction*.88);count=max(10,round(2*math.pi*radius/.085))
                for i in range(count):
                    angle=2*math.pi*(i+rng.random()*.45+ring*.4)/count
                    radial=np.array([math.cos(angle),math.sin(angle),0]);tangent=np.array([-math.sin(angle),math.cos(angle),0]);up=np.array([0.,0,1.])
                    center=np.array([cx,y,z+.10+.34*fraction])+radial*(radius+.015)
                    scale=.8+rng.random()*.4;length=.10*scale;width=.043*scale;start=len(verts)
                    outline=[center+up*length-radial*.01,center+tangent*width,center-up*length+radial*.06,center-tangent*width]
                    verts.extend(outline+[center+radial*.038,center-radial*.01])
                    for j in range(4):faces.extend([(start+j,start+(j+1)%4,start+4),(start+(j+1)%4,start+j,start+5)])
        record(f'Topiary leaf volumes {side} {y}',verts,faces,'exterior.ivy',(.78,.87,.69))
        verts=[];faces=[]
        # Eight actual raised quatrefoil mouldings around each planter.
        for motif in range(8):
            theta=motif*2*math.pi/8;path=[]
            for i in range(48):
                t=i*2*math.pi/48;u=.12*math.sin(t)*(1+.23*math.cos(4*t));v=.18*math.cos(t)*(1+.12*math.cos(4*t))
                z=.35+v;radius=.27+np.clip((z-.12)/.43,0,1)*.15+.009
                a=theta+u/radius;path.append((cx+radius*math.cos(a),y+radius*math.sin(a),z))
            pv,pf=tube(path,.009);offset=len(verts);verts.extend(pv);faces.extend(tuple(i+offset for i in f) for f in pf)
        record(f'Planter carved relief {side} {y}',verts,faces,'interior.wall',(1,.94,.80))
    for bay in range(3):
        for dy in [-.72,.72]:
            y=3+bay*6+dy;x=side*(3.72-.275-.006);path=[]
            for i in range(48):
                t=i*2*math.pi/48;r=1+.22*math.cos(4*t);path.append((x,y+.072*r*math.sin(t),.38+.19*r*math.cos(t)))
            v,f=tube(path,.012);record(f'Bench carved relief {side} {bay} {dy}',v,f,'interior.wall',(1,.95,.84))
records+=added
(OUT/'geometry-polished.json').write_text(json.dumps(records,separators=(',',':')))
report={'new_objects':len(added),'added_triangles':sum(len(o['faces']) for o in added),'triangles':sum(len(o['faces']) for o in records),'leaf_geometry':'Closed folded diamonds; no alpha cards','carvings':'Closed shallow quatrefoil mouldings, inferred from the generated ornamental style'}
(OUT/'geometry-polish.json').write_text(json.dumps(report,indent=2));print(json.dumps(report))
