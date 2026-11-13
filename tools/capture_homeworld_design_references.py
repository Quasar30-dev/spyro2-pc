"""Whole-level retail homeworld references, used for scale/style rather than copying layouts."""
from pathlib import Path
import json, math, re, subprocess
import numpy as np
from PIL import Image, ImageDraw

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'data/validation/retail-photogrammetry-20260927'
sources=[('autumn-plains',33,'build/autumn-reference-inspect.log'),('winter-tundra',57,'build/winter57-reference-inspect.log')]
manifest=[];sheet=Image.new('RGB',(1800,900))
for row,(name,slot,logpath) in enumerate(sources):
    p=OUT/'references'/f'{name}-collision.obj'
    v=np.array([list(map(float,l.split()[1:4])) for l in p.read_text().splitlines() if l.startswith('v ')])
    lo,hi=v.min(0),v.max(0);center=(lo+hi)/2
    radius=max(hi[0]-lo[0],hi[2]-lo[2])*1.25
    spawn=list(map(float,re.search(r'retail spawn ([\d.,-]+)',(ROOT/logpath).read_text()).group(1).split(',')))
    for column,angle in enumerate([0,120,240]):
        a=math.radians(angle);eye=center+[radius*math.sin(a),radius*.75,-radius*math.cos(a)]
        stem=f'{name}-whole-{angle:03d}';shot=OUT/'shots'/f'{stem}.s2shot';bmp=OUT/'retail-images'/f'{stem}.bmp'
        shot.write_text('S2SHOT 1\n1200 900 6\n'+' '.join(map(str,[*spawn,0,0,55,1]))+'\n0\n0\n1\n0 '+' '.join(map(str,[*eye,*center]))+'\n')
        if not bmp.exists():
            with (OUT/'shots'/f'{stem}.log').open('w') as log:
                subprocess.run([str(ROOT/'build/Release/spyro_workbench.exe'),'--game',str(ROOT/'data/disc'),'--slot',str(slot),
                    '--cinematic-shot',str(shot),'--capture',str(bmp)],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        png=bmp.with_suffix('.png');im=Image.open(bmp).convert('RGB');im.save(png)
        sheet.paste(im.resize((600,450),Image.Resampling.LANCZOS),(column*600,row*450))
        manifest.append({'level':name,'slot':slot,'eye':eye.tolist(),'target':center.tolist(),'image':str(png.relative_to(OUT))})
        print(stem,flush=True)
sheet.save(OUT/'references/retail-other-homeworld-overviews.png')
(OUT/'references/other-homeworld-cameras.json').write_text(json.dumps(manifest,indent=2))
