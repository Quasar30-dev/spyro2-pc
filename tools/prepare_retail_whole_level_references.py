"""Package real whole-level captures and shipped materials for the generation probe."""
from pathlib import Path
import json, math, hashlib, shutil
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'data/validation/retail-photogrammetry-20260927'
refs=OUT/'references';refs.mkdir(exist_ok=True)
shots=[]
for i in range(12):
    a=math.radians(i*30)
    shots.append([f'orbit-{i+1:02d}',[85+155*math.sin(a),145,58-155*math.cos(a)],[85,36,58],55])
(OUT/'sequence-shots.json').write_text(json.dumps(shots,indent=2))
provenance=[]
for source_name,dest_name in [('terrain-15.png','summer-forest-retail-atlas.png'),('terrain-15-catalog.png','summer-forest-retail-textures.png')]:
    source=ROOT/'data/custom-levels/springtide-castle/assets'/source_name
    destination=refs/dest_name;shutil.copy2(source,destination)
    provenance.append({'source':str(source.relative_to(ROOT)),'destination':str(destination.relative_to(ROOT)),
        'sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'type':'Unmodified decoded retail WAD 15 texture atlas/catalog'})
(refs/'provenance.json').write_text(json.dumps({'level':'Summer Forest','wad_slot':15,'retail_textures':provenance,
    'shots':'Native workbench camera captures from data/disc; no custom level loaded',
    'selection':'Entire level in frame; exterior, reverse, side and overhead coverage. Sky is the existing retail renderer background.',
    'excluded':'Lanternfall and all previous custom castle imagery are excluded from these inputs.'},indent=2))
names=['overview-south','overview-north','overview-east','overview-west','overview-overhead']
sheet=Image.new('RGB',(1800,900),(235,235,235))
for i,name in enumerate(names):
    p=OUT/'retail-images'/f'{name}.png'
    if p.exists():sheet.paste(Image.open(p).convert('RGB').resize((600,450),Image.Resampling.LANCZOS),((i%3)*600,(i//3)*450))
sheet.save(refs/'summer-forest-whole-level-overviews.png')
print(refs)
