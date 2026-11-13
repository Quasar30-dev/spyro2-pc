"""COLMAP probe of retail whole-level shots and one coordinated generated sequence.

Generated concepts have unknown cameras: do not assign the retail camera poses
to the new imagined layout. All source photos and generated sheets are retained.
"""
from pathlib import Path
import json, math, sys, time
import numpy as np
from PIL import Image
from scipy.spatial.transform import Rotation
import pycolmap as pc

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'data/validation/retail-photogrammetry-20260927'
case=sys.argv[1]
assert case in ('retail12','retail36','spring6','spring6v2')
work=OUT/case;images=work/'images';images.mkdir(parents=True,exist_ok=True)
generated=case.startswith('spring')
width,height=(1280,960) if case=='retail36' else (512,512) if case=='spring6v2' else (592,444)
focal=height/(2*math.tan(math.radians(55)/2))
if generated:
    source=OUT/'generated'/('spring-world-six-view-concept-v2.png' if case=='spring6v2' else 'spring-world-six-view-concept.png')
    sheet=Image.open(source).convert('RGB');w,h=sheet.size
    crops=[]
    for i in range(6):
        x,y=i%3,i//3
        box=[round(x*w/3),round(y*h/2),round((x+1)*w/3),round((y+1)*h/2)]
        name=f'{i+1:02d}.png'
        sheet.crop(box).resize((width,height),Image.Resampling.LANCZOS).save(images/name)
        crops.append({'image':name,'source_box':box})
    (work/'source.json').write_text(json.dumps({'sheet':str(source),'source_size':[w,h],
        'crops':crops,'output_size':[width,height],'tool':'built-in image_gen',
        'model_id':'Not exposed by the built-in tool; no specific model version asserted',
        'prompt':'references/generation-prompt-v2.txt' if case=='spring6v2' else 'references/generation-prompt.txt',
        'cameras':'Unknown; free SfM only'},indent=2))
else:
    cameras=json.loads((OUT/('dense-cameras.json' if case=='retail36' else 'sequence-cameras.json')).read_text())
    for i,c in enumerate(cameras):
        Image.open(OUT/'retail-images'/c['image']).convert('RGB').resize((width,height),Image.Resampling.LANCZOS).save(images/f'{i+1:02d}.png')
dbpath=work/'database.db';start=time.time()
if not dbpath.exists():
    pc.extract_features(dbpath,images,camera_mode=pc.CameraMode.SINGLE,
        reader_options=pc.ImageReaderOptions(camera_model='SIMPLE_PINHOLE',camera_params=f'{focal},{width/2},{height/2}'),
        extraction_options=pc.FeatureExtractionOptions(num_threads=4,max_image_size=width,use_gpu=False),device=pc.Device.cpu)
    pc.match_exhaustive(dbpath,matching_options=pc.FeatureMatchingOptions(num_threads=4,use_gpu=False),device=pc.Device.cpu)

options=pc.IncrementalPipelineOptions(num_threads=4,min_model_size=3,max_num_models=6,random_seed=27,max_runtime_seconds=120,
    ba_refine_focal_length=generated,ba_refine_principal_point=False,ba_refine_extra_params=False)
options.mapper.init_min_num_inliers=30;options.mapper.init_min_tri_angle=6
options.mapper.abs_pose_refine_focal_length=generated;options.mapper.abs_pose_refine_extra_params=False
options.triangulation.ignore_two_view_tracks=False
free=work/'free';free.mkdir(exist_ok=True)
models=pc.incremental_mapping(dbpath,images,free,options=options)

def stats(model):
    tracks=[p.track.length() for p in model.points3D.values()]
    return {'registered_images':model.num_reg_images(),'points':model.num_points3D(),
        'points_seen_by_3plus':sum(n>=3 for n in tracks),'mean_track_length':float(np.mean(tracks)) if tracks else 0,
        'mean_reprojection_px':model.compute_mean_reprojection_error(),
        'registered_names':[model.images[i].name for i in model.reg_image_ids()],
        'camera_parameters':[list(c.params) for c in model.cameras.values()]}
report={'case':case,'pycolmap':pc.__version__,'image_size':[width,height],
    'source_images':6 if generated else len(cameras),'initial_vertical_fov':55,
    'refined_focal_length':generated,'free_models':{str(k):stats(m) for k,m in models.items()}}
if models:
    best_key=max(models,key=lambda k:(models[k].num_reg_images(),models[k].num_points3D()))
    best=models[best_key];bestdir=work/'best';bestdir.mkdir(exist_ok=True)
    best.write(bestdir);best.export_PLY(work/'sparse.ply');report['best_model']=str(best_key)
    report['best']=stats(best)

if not generated:
    known=work/'known';known.mkdir(exist_ok=True);lines=[]
    (known/'cameras.txt').write_text(f'1 SIMPLE_PINHOLE {width} {height} {focal} {width/2} {height/2}\n')
    with pc.Database.open(dbpath) as db:
        entries={im.name:im for im in db.read_all_images()}
    for i,c in enumerate(cameras):
        m=np.asarray(c['camera_to_world_blender']);r=np.diag([1.,-1.,-1.])@m[:3,:3].T;t=-r@m[:3,3]
        q=Rotation.from_matrix(r).as_quat()[[3,0,1,2]];name=f'{i+1:02d}.png'
        lines.append(f'{entries[name].image_id} '+' '.join(map(str,[*q,*t]))+f' 1 {name}\n\n')
    (known/'images.txt').write_text(''.join(lines));(known/'points3D.txt').write_text('')
    fixed=work/'triangulated';fixed.mkdir(exist_ok=True);options.fix_existing_frames=True
    triangulated=pc.triangulate_points(pc.Reconstruction(known),dbpath,images,fixed,options=options,refine_intrinsics=False)
    report['known_cameras']=stats(triangulated);triangulated.export_PLY(work/'known-points.ply')

with pc.Database.open(dbpath) as db:
    ids=[i.image_id for i in db.read_all_images()];pairs=[]
    for ia,a in enumerate(ids):
        for b in ids[ia+1:]:
            geo=db.read_two_view_geometry(a,b)
            if len(geo.inlier_matches):pairs.append({'images':[a,b],'matches':len(geo.inlier_matches)})
report['verified_pairs']=pairs;report['seconds']=time.time()-start
report['scope']='Sparse points/camera recovery do not establish a complete mesh or better renders. The new spring concept has no geometric ground truth.'
(work/'report.json').write_text(json.dumps(report,indent=2))
print(json.dumps({k:v for k,v in report.items() if k!='verified_pairs'},indent=2),flush=True)
