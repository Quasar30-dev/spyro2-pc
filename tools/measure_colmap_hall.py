"""Measure the reconstruction probe against the retained hall and plot evidence."""
from pathlib import Path
import json
import numpy as np
import trimesh
import pycolmap as pc
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'data/validation/photogrammetry-20260927'
records=json.loads((ROOT/'data/custom-levels/lanternfall/complete-hall/geometry-polished.json').read_text())
tri=np.concatenate([np.asarray(o['vertices'])[o['faces']] for o in records])
surface=trimesh.Trimesh(vertices=tri.reshape(-1,3),faces=np.arange(len(tri)*3).reshape(-1,3),process=False)
random=np.random.default_rng(27);reports=[];clouds=[]

def distance_stats(points):
    if not len(points):return None
    chosen=points[random.choice(len(points),min(3000,len(points)),replace=False)]
    distances=[]
    for batch in np.array_split(chosen,max(1,len(chosen)//100)):
        _,d,_=trimesh.proximity.closest_point(surface,batch);distances.extend(d.tolist())
    d=np.asarray(distances)
    return {'samples':len(d),'median_m':float(np.median(d)),'p95_m':float(np.percentile(d,95)),
            'within_5cm_fraction':float((d<=.05).mean()),'within_15cm_fraction':float((d<=.15).mean())}

for case in ['generated10','control10','control20']:
    work=OUT/case;report=json.loads((work/'report.json').read_text());reconstruction=pc.Reconstruction(work/'triangulated')
    xyz=np.array([p.xyz for p in reconstruction.points3D.values()]);track=np.array([p.track.length() for p in reconstruction.points3D.values()])
    report['sparse_layout_distance']=distance_stats(xyz);report['three_view_layout_distance']=distance_stats(xyz[track>=3])
    fused=work/'dense-3.11.1/fused.ply'
    if fused.exists():
        mesh=trimesh.load(fused,process=False);points=np.asarray(mesh.vertices)
        report['dense_points']=len(points);report['dense_layout_distance']=distance_stats(points)
    report.pop('pairs',None);reports.append(report);clouds.append((xyz,track))
    print(case,report['sparse_layout_distance'],report.get('dense_points'),flush=True)
result={'experiments':reports,'scope':'For coherent renders, distance is reconstruction error against known geometry. For generated art, it measures compatibility with intended layout, not unknown true geometry. Surface completeness and texture quality are not inferred from point count.'}
(OUT/'comparison.json').write_text(json.dumps(result,indent=2))
fig=plt.figure(figsize=(15,5),facecolor='#f5f3ef')
for i,((points,tracks),report) in enumerate(zip(clouds,reports)):
    ax=fig.add_subplot(1,3,i+1,projection='3d');ax.set_facecolor('#f5f3ef')
    inside=(abs(points[:,0])<8)&(points[:,1]>-5)&(points[:,1]<23)&(points[:,2]>-2)&(points[:,2]<12)
    weak=inside&(tracks<3);strong=inside&(tracks>=3)
    ax.scatter(*points[weak].T,s=1,color='#9fa6b0',alpha=.3)
    ax.scatter(*points[strong].T,s=3,color='#be593d' if i==0 else '#217f87',alpha=.8)
    for x in [-4.5,4.5]:ax.plot([x,x],[0,18],[0,0],color='#555555',lw=.8)
    for y in [0,18]:ax.plot([-4.5,4.5],[y,y],[0,0],color='#555555',lw=.8)
    ax.set_xlim(-7,7);ax.set_ylim(-3,21);ax.set_zlim(-1,10);ax.set_box_aspect([14,24,11]);ax.view_init(24,-60)
    ax.set_xlabel('X (m)');ax.set_ylabel('Y (m)');ax.set_zlabel('Z (m)')
    title=['10 generated views','10 coherent Blender views','20 coherent Blender views'][i]
    ax.set_title(title+'\n'+f'{int((tracks>=3).sum()):,} points supported by 3+ views',fontsize=11,pad=12)
fig.suptitle('COLMAP: consistency and overlap determine recoverable geometry',fontsize=16)
fig.text(.5,.015,'Known blockout cameras supplied. Coloured points have 3+ observations; grey points have 2. This is a sparse reconstruction test, not a finished level.',ha='center',fontsize=9)
fig.tight_layout(rect=[0,.055,1,.94]);fig.savefig(OUT/'sparse-comparison.png',dpi=140);plt.close(fig)
