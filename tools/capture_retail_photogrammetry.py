"""Capture retail Summer Forest, never a custom world, with recorded cameras."""
from pathlib import Path
import json, math, subprocess, sys
import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'data/validation/retail-photogrammetry-20260927'
SOURCE=ROOT/'data/custom-levels/springtide-castle/assets/reference-15.json'
OUT.mkdir(parents=True,exist_ok=True)
for name in ('shots','retail-images','references'):(OUT/name).mkdir(exist_ok=True)
source=json.loads(SOURCE.read_text())
spawn=source['spawn']
shots=[
    ('overview-south',(87,139,-52),(87,35,60),55),
    ('overview-north',(82,123,182),(86,35,56),55),
    ('overview-east',(235,130,58),(85,36,58),55),
    ('overview-west',(-65,130,58),(85,36,58),55),
    ('overview-overhead',(85,200,57),(85,35,58),55),
]
mode=sys.argv[1] if len(sys.argv)>1 else 'overview'
width,height=1200,900
if mode=='sequence':
    shots=json.loads((OUT/'sequence-shots.json').read_text())
elif mode=='dense':
    width,height=1600,1200
    shots=[]
    for i in range(36):
        a=math.radians(i*10)
        shots.append([f'dense-orbit-{i+1:02d}',[85+155*math.sin(a),145,58-155*math.cos(a)],[85,36,58],55])
metadata=[]
for name,eye,target,fov in shots:
    framepath=OUT/'retail-images'/f'{name}.bmp'
    shot=OUT/'shots'/f'{name}.s2shot'
    shot.write_text(f'S2SHOT 1\n{width} {height} 6\n'+' '.join(map(str,[*spawn,0,0,fov,1]))+'\n0\n0\n1\n0 '+' '.join(map(str,[*eye,*target]))+'\n')
    if not framepath.exists():
        with (OUT/'shots'/f'{name}.log').open('w') as log:
            subprocess.run([str(ROOT/'build/Release/spyro_workbench.exe'),'--game',str(ROOT/'data/disc'),'--slot','15',
                '--cinematic-shot',str(shot),'--capture',str(framepath)],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    png=framepath.with_suffix('.png');Image.open(framepath).convert('RGB').save(png)
    pos=np.asarray(eye)[[0,2,1]];focus=np.asarray(target)[[0,2,1]]
    forward=focus-pos;forward=forward/np.linalg.norm(forward)
    right=np.cross(forward,[0,0,1]);right=right/np.linalg.norm(right)
    up=np.cross(right,forward)
    matrix=np.eye(4);matrix[:3,:3]=np.column_stack([right,up,-forward]);matrix[:3,3]=pos
    metadata.append({'id':name,'image':png.name,'position':pos.tolist(),'target':focus.tolist(),
        'native_eye':eye,'native_target':target,'vertical_fov_degrees':fov,
        'horizontal_fov_degrees':math.degrees(2*math.atan(math.tan(math.radians(fov)/2)*4/3)),
        'camera_to_world_blender':matrix.tolist(),'image_dimensions':[width,height],
        'source':'Mounted retail WAD 15, Summer Forest; native workbench renderer; no custom-level argument'})
    print(name,flush=True)
(OUT/f'{mode}-cameras.json').write_text(json.dumps(metadata,indent=2))
