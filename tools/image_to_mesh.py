"""Generate a volumetric mesh from a keyed GPT Image asset using TripoSG.

The mesh, rather than a panoramic depth shell, is the authoring source of truth.
This runner avoids background segmentation weights because our source has an
explicit chroma key. Game runtime has no Python or ML dependencies.
"""
from pathlib import Path
import sys,argparse,json
import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'research/local/triposg'))

def main(args):
    import torch,trimesh
    from huggingface_hub import snapshot_download
    from triposg.pipelines.pipeline_triposg import TripoSGPipeline
    torch.set_num_threads(8)
    path=Path(args.output);path.parent.mkdir(parents=True,exist_ok=True)
    image=np.array(Image.open(args.image).convert('RGB'))
    mask=(image[:,:,0]>180)&(image[:,:,2]>170)&(image[:,:,1]<90)
    image[mask]=255
    image=Image.fromarray(image)
    weights=snapshot_download('VAST-AI/TripoSG',local_dir=ROOT/'research/models/TripoSG')
    print('Loading volumetric reconstruction model',flush=True)
    pipe=TripoSGPipeline.from_pretrained(weights).to('cuda',torch.float16)
    print('Generating full 3D shape',flush=True)
    with torch.inference_mode():
        output=pipe(image=image,generator=torch.Generator(device='cuda').manual_seed(args.seed),num_inference_steps=50,guidance_scale=7.0,use_flash_decoder=False,dense_octree_depth=7,hierarchical_octree_depth=9).samples[0]
    if output[0] is None:raise RuntimeError('Mesh extraction failed')
    mesh=trimesh.Trimesh(output[0].astype(np.float32),np.ascontiguousarray(output[1]),process=True)
    mesh.export(path)
    report={'source':str(Path(args.image)),'generator':'VAST-AI/TripoSG','seed':args.seed,'vertices':len(mesh.vertices),'triangles':len(mesh.faces),'bounds':mesh.bounds.tolist(),'watertight':bool(mesh.is_watertight),'volume':float(mesh.volume),'note':'Generated shape; unseen surfaces inferred. Blender inspection and doorway/interior polish required before gameplay.'}
    path.with_suffix('.json').write_text(json.dumps(report,indent=2));print(report,flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('image');p.add_argument('output');p.add_argument('--seed',type=int,default=42);main(p.parse_args())
