"""Compare generated views with coherent renders under the same COLMAP settings.

This is an isolated experiment. It never replaces production geometry/materials.
Known cameras measure compatibility with the blockout, not truth in generated art.
"""
from pathlib import Path
import json,math,sys,time,shutil
import numpy as np
from PIL import Image
from scipy.spatial.transform import Rotation
import pycolmap as pc

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'data/validation/photogrammetry-20260927'
HALL=ROOT/'data/custom-levels/lanternfall/complete-hall'
case=sys.argv[1] if len(sys.argv)>1 else 'generated10'
assert case in ['generated10','control10','control20','sequence6','sequence-control6']
work=OUT/case;images=work/'images';images.mkdir(parents=True,exist_ok=True)
sequence=case in ['sequence6','sequence-control6']
cameras=json.loads((OUT/'sequence6/cameras.json' if sequence else HALL/'cameras.json' if case=='generated10' else OUT/'control/cameras.json').read_text())
if not sequence:cameras=cameras[:20 if case=='control20' else 10]
for i,c in enumerate(cameras):
    name=f'{i+1:02d}.png'
    if case=='sequence6':source=OUT/'sequence6/generated-frames'/name
    elif case=='sequence-control6':source=OUT/'control/images'/c['source_control_image']
    else:source=HALL/'generated-views'/c.get('generated_image',c['id']+'.png') if case=='generated10' else OUT/'control/images'/name
    Image.open(source).convert('RGB').resize((960,720),Image.Resampling.LANCZOS).save(images/name)
    c['image']=name;c['image_dimensions']=[960,720]
(work/'cameras.json').write_text(json.dumps(cameras,indent=2))
width,height=960,720;f=width/(2*math.tan(math.radians(cameras[0]['horizontal_fov_degrees']/2)))
dbpath=work/'database.db';started=time.time()
if not dbpath.exists():
    pc.extract_features(dbpath,images,camera_mode=pc.CameraMode.SINGLE,
        reader_options=pc.ImageReaderOptions(camera_model='PINHOLE',camera_params=f'{f},{f},{width/2},{height/2}'),
        extraction_options=pc.FeatureExtractionOptions(num_threads=4,max_image_size=960,use_gpu=False),device=pc.Device.cpu)
    pc.match_exhaustive(dbpath,matching_options=pc.FeatureMatchingOptions(num_threads=4,use_gpu=False),device=pc.Device.cpu)

known=work/'known';known.mkdir(exist_ok=True)
(known/'cameras.txt').write_text(f'1 PINHOLE {width} {height} {f} {f} {width/2} {height/2}\n')
pose={};lines=[]
with pc.Database.open(dbpath) as db:
    entries={i.name:i for i in db.read_all_images()}
    for c in cameras:
        matrix=np.asarray(c['camera_to_world_blender']);r=np.diag([1.,-1.,-1.])@matrix[:3,:3].T;t=-r@matrix[:3,3]
        q=Rotation.from_matrix(r).as_quat()[[3,0,1,2]];item=entries[c['image']]
        lines.append(f'{item.image_id} '+ ' '.join(map(str,[*q,*t]))+f' 1 {c["image"]}\n\n');pose[item.image_id]=(r,t)
    (known/'images.txt').write_text(''.join(lines));(known/'points3D.txt').write_text('')
    k=np.array([[f,0,width/2],[0,f,height/2],[0,0,1]]);ki=np.linalg.inv(k);pairs=[];errs=[];matches=0
    ids=sorted(pose)
    for ia,a in enumerate(ids):
        for b in ids[ia+1:]:
            geo=db.read_two_view_geometry(a,b);m=geo.inlier_matches
            if not len(m):continue
            xa=db.read_keypoints(a)[m[:,0],:2];xb=db.read_keypoints(b)[m[:,1],:2]
            ra,ta=pose[a];rb,tb=pose[b];r=rb@ra.T;t=tb-r@ta
            cross=np.array([[0,-t[2],t[1]],[t[2],0,-t[0]],[-t[1],t[0],0]])
            fundamental=ki.T@cross@r@ki
            x=np.column_stack([xa,np.ones(len(xa))]);y=np.column_stack([xb,np.ones(len(xb))])
            fx=x@fundamental.T;fty=y@fundamental
            error=np.abs(np.sum(y*fx,axis=1))/np.sqrt(np.maximum((fx[:,:2]**2).sum(1)+(fty[:,:2]**2).sum(1),1e-12))
            errs.extend(error.tolist());matches+=len(m)
            pairs.append({'images':[a,b],'verified_matches':len(m),'known_pose_inliers_2px':int((error<=2).sum()),'median_known_pose_sampson_px':float(np.median(error))})

options=pc.IncrementalPipelineOptions(num_threads=4,min_model_size=3,max_num_models=10,random_seed=27,max_runtime_seconds=120,
    ba_refine_focal_length=False,ba_refine_principal_point=False,ba_refine_extra_params=False,fix_existing_frames=True)
options.mapper.init_min_num_inliers=50;options.mapper.init_min_tri_angle=8
options.mapper.abs_pose_refine_focal_length=False;options.mapper.abs_pose_refine_extra_params=False
options.triangulation.ignore_two_view_tracks=False
fixed=work/'triangulated';fixed.mkdir(exist_ok=True)
reconstruction=pc.triangulate_points(pc.Reconstruction(known),dbpath,images,fixed,options=options,refine_intrinsics=False)
reconstruction.export_PLY(work/'known-camera-points.ply')

def stats(model):
    points=list(model.points3D.values());tracks=[p.track.length() for p in points]
    return {'registered_images':model.num_reg_images(),'points':model.num_points3D(),'points_seen_by_3plus':sum(n>=3 for n in tracks),
            'mean_track_length':float(np.mean(tracks)) if tracks else 0,'mean_reprojection_px':model.compute_mean_reprojection_error()}
free=work/'free';free.mkdir(exist_ok=True);options.fix_existing_frames=False
models=pc.incremental_mapping(dbpath,images,free,options=options)
errors=np.asarray(errs)
report={'case':case,'images':len(cameras),'image_dimensions':[width,height],'pycolmap':pc.__version__,'cpu_sparse':True,
        'verified_pairs':len(pairs),'verified_matches':matches,'known_pose_match_fraction_2px':float((errors<=2).mean()) if len(errors) else 0,
        'median_known_pose_sampson_px':float(np.median(errors)) if len(errors) else None,'fixed_cameras':stats(reconstruction),
        'free_models':{str(key):stats(model) for key,model in models.items()},'elapsed_seconds':time.time()-started,'pairs':pairs,
        'limits':'Generated image poses are intended blockout cameras, not measured truth. Registration/point count alone is not evidence of a complete or better-looking mesh.'}
(work/'report.json').write_text(json.dumps(report,indent=2));print(json.dumps({k:v for k,v in report.items() if k!='pairs'}),flush=True)
