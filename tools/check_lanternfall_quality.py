"""Measured source-view seam coverage, distinct from full 3D completeness."""
from pathlib import Path
import json
import numpy as np
from PIL import Image
from scipy.ndimage import binary_erosion,label
ROOT=Path(__file__).resolve().parents[1];WORLD=ROOT/'data/custom-levels/lanternfall'
VALID=ROOT/'data/validation/lanternfall/quality'
results=[]
for name,path in [('entrance',WORLD/'moge-seams/exterior-splats-front/known'),('interior',WORLD/'moge-seams/interior.southwest'),('clean-interior',WORLD/'moge-props/multiview-interior-clean-background/known')]:
    valid=np.load(path/'geometry.npz')['valid']
    render=Image.open(VALID/(name+'-source.png')).convert('RGBA');alpha=np.asarray(render)[:,:,3]
    resized=np.asarray(Image.fromarray(valid.astype(np.uint8)*255).resize(render.size,Image.Resampling.NEAREST))>0
    # Four render pixels exclude expected silhouette antialiasing and mask edges.
    # Interior cracks remain in the measurement; only the valid-mask boundary is excluded.
    core=binary_erosion(resized,iterations=4)
    holes=core&(alpha<250);components,count=label(holes)
    sizes=np.bincount(components.ravel())[1:]
    coverage=100*(1-holes.sum()/max(1,core.sum()))
    report=json.loads((path/'report.json').read_text())
    results.append({'view':name,'valid_core_pixels':int(core.sum()),'missing_pixels':int(holes.sum()),'source_projection_coverage_percent':coverage,'largest_gap_pixels':int(sizes.max()) if len(sizes) else 0,'inferred_seam_triangles':report['inferred_seam_triangles'],'inferred_seam_fraction':report['inferred_seam_fraction'],'passes_99_percent_source_coverage':bool(coverage>=99)})
out={'metric_scope':'Source camera coverage of valid mask interior only. This is not a claim of 99% 3D reconstruction accuracy or hidden-surface completeness. Offset views must also be inspected.','views':results,'passes':all(x['passes_99_percent_source_coverage'] for x in results)}
(VALID/'seam-coverage.json').write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2))
if not out['passes']:raise SystemExit(1)
