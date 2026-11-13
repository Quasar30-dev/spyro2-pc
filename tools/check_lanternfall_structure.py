"""Blender: calibrated depth references and displaced, full-sphere gap checks."""
import bpy,json,math,sys
import numpy as np
from pathlib import Path
from mathutils import Vector
ROOT=Path(r'C:/Users/Shadow/Documents/Spyro 2_PC')
OUT=ROOT/'data/custom-levels/lanternfall/complete-hall'
refined='--refined' in sys.argv
polished='--polished' in sys.argv
scene=bpy.data.scenes['Lanternfall | Detail polish' if polished else 'Lanternfall | MoGe fitted hall' if refined else 'Lanternfall | Complete hall'];bpy.context.window.scene=scene
scene.view_layers[0].update();deps=bpy.context.evaluated_depsgraph_get()
names=sorted(o.name for o in scene.objects if o.type=='MESH');lookup={name:i for i,name in enumerate(names)}
(OUT/'depth-reference').mkdir(exist_ok=True)
for c in json.loads((OUT/'cameras.json').read_text()):
    if '--coverage-only' in sys.argv:continue
    if '--floor-only' in sys.argv and c['id']!='10 Floor':continue
    cam=scene.objects[c['id']];rot=cam.matrix_world.to_3x3();origin=cam.matrix_world.translation
    width,height=256,192;fx=math.tan(math.radians(c['horizontal_fov_degrees']/2));fy=fx*height/width
    depth=np.zeros((height,width),np.float32);normal=np.zeros((height,width,3),np.float32);ids=np.full((height,width),-1,np.int32)
    for y in range(height):
        for x in range(width):
            local=Vector(((2*(x+.5)/width-1)*fx,(1-2*(y+.5)/height)*fy,-1))
            hit,p,n,face,obj,matrix=scene.ray_cast(deps,origin,(rot@local).normalized(),distance=100)
            if hit:
                depth[y,x]=-(rot.transposed()@(p-origin)).z
                normal[y,x]=n;ids[y,x]=lookup[obj.name]
    np.savez_compressed(OUT/'depth-reference'/(c['id']+'.npz'),depth=depth,normal=normal,object_ids=ids)
    print('Depth:',c['id'],flush=True)
if '--coverage-only' not in sys.argv:
    (OUT/'depth-reference/objects.json').write_text(json.dumps(names))

def through_window(origin,direction):
    if abs(direction.x)<1e-8:return False
    t=((4.7 if direction.x>0 else -4.7)-origin.x)/direction.x
    if t<=0:return False
    p=origin+direction*t
    for center in [3,9,15]:
        u=abs(p.y-center)
        if u<1.2 and 1.4<=p.z<=3.6+1.5*(1-u/1.2)**.65:return True
    return False

checks=[];unexpected=[]
for x in [-2,0,2]:
    for y in [2,6,12,16]:
        origin=Vector((x,y,1.8));hits=windows=misses=0
        for pitch in range(-85,86,5):
            p=math.radians(pitch)
            for yaw in range(0,360,5):
                a=math.radians(yaw)
                # Exact cardinal rays: tiny sin(pi) components can make Blender's
                # BVH miss a coplanar tile boundary through reciprocal overflow.
                d=Vector(tuple(0 if abs(v)<1e-12 else v for v in (math.cos(p)*math.sin(a),math.cos(p)*math.cos(a),math.sin(p))))
                hit,*_=scene.ray_cast(deps,origin,d,distance=100)
                if hit:hits+=1
                elif through_window(origin,d):windows+=1
                else:
                    misses+=1;unexpected.append({'position':list(origin),'yaw':yaw,'pitch':pitch})
        checks.append({'position':list(origin),'hits':hits,'open_window_rays':windows,'unexpected_misses':misses})
report={'status':'pass' if not unexpected else 'fail','sampled_positions':len(checks),'rays':sum(c['hits']+c['open_window_rays']+c['unexpected_misses'] for c in checks),'checks':checks,'unexpected':unexpected,
        'scope':'Discrete whole-sphere samples inside this hall; intentional open windows allowed. Not proof of arbitrary exterior geometry or pixel-perfect coverage.'}
(OUT/('structural-coverage-polished.json' if polished else 'structural-coverage-refined.json' if refined else 'structural-coverage.json')).write_text(json.dumps(report,indent=2))
print(json.dumps({'status':report['status'],'rays':report['rays'],'unexpected':len(unexpected)}),flush=True)
