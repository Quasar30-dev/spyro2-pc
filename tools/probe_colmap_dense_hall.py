"""Bounded CUDA MVS probe using a compatible portable COLMAP 3.11.1 build.

Sparse statistics use pycolmap 4.2.0. The 4.2.0 Windows CUDA binary reports zero
devices on this machine; no driver/system configuration is changed for the probe.
"""
from pathlib import Path
import sys,json,subprocess,time
import numpy as np
import pycolmap as pc
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'data/validation/photogrammetry-20260927'
case=sys.argv[1];assert case in ['control10','control20','generated10','sequence6','sequence-control6']
work=OUT/case;dense=work/'dense-3.11.1';legacy=work/'legacy-text';legacy.mkdir(exist_ok=True)
exe=ROOT/'research/local/colmap-3.11.1/bin/bin/colmap.exe'
rec=pc.Reconstruction(work/'triangulated');rec.write_text(legacy)
reference_case='sequence-control6' if case.startswith('sequence') else 'control20' if case=='control20' else 'control10'
reference=pc.Reconstruction(OUT/reference_case/'triangulated')
counts={i:{} for i in reference.reg_image_ids()}
for p in reference.points3D.values():
    ids=[e.image_id for e in p.track.elements]
    for a in ids:
        for b in ids:
            if a!=b:counts[a][b]=counts[a].get(b,0)+1

def run(stage,args):
    log=work/(stage+'-3.11.1.log');print(case,stage,flush=True)
    with log.open('w') as f:
        r=subprocess.run([str(exe),*map(str,args)],stdout=f,stderr=subprocess.STDOUT)
    if r.returncode:raise RuntimeError(f'{stage} failed ({r.returncode}); {log}')

started=time.time()
run('undistort',['image_undistorter','--image_path',work/'images','--input_path',legacy,'--output_path',dense,'--max_image_size',640,'--num_patch_match_src_images',6])
lines=[]
for a in sorted(counts):
    # Both 10-view cases receive the same source-view graph. Do not let the
    # generated case's sparse matching failure alone remove its MVS neighbours.
    others=sorted([b for b in counts if b!=a],key=lambda b:(-counts[a].get(b,0),np.linalg.norm(reference.images[a].projection_center()-reference.images[b].projection_center())))[:6]
    lines.extend([reference.images[a].name,', '.join(reference.images[b].name for b in others)])
(dense/'stereo/patch-match.cfg').write_text('\n'.join(lines)+'\n')
run('patch-match',['patch_match_stereo','--workspace_path',dense,'--PatchMatchStereo.max_image_size',640,'--PatchMatchStereo.num_iterations',3,'--PatchMatchStereo.num_samples',10,'--PatchMatchStereo.cache_size',2,'--PatchMatchStereo.depth_min',.3,'--PatchMatchStereo.depth_max',30])
run('fusion',['stereo_fusion','--workspace_path',dense,'--output_path',dense/'fused.ply','--StereoFusion.num_threads',4,'--StereoFusion.min_num_pixels',3,'--StereoFusion.cache_size',2])
report={'case':case,'dense_version':'3.11.1','max_image_size':640,'iterations':3,'samples':10,'fusion_min_views':3,'depth_range':[.3,30],'source_graph':'Control sparse covisibility with pose-distance fallback; identical for both 10-view cases','seconds':time.time()-started,'fused_cloud':str(dense/'fused.ply')}
(work/'dense-report.json').write_text(json.dumps(report,indent=2));print(json.dumps(report),flush=True)
