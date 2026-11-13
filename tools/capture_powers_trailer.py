"""Capture and catalogue footage from the installed main workbench."""
from pathlib import Path
import argparse,subprocess,json,hashlib,time
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'output/spyro-powers-trailer'
p=argparse.ArgumentParser();p.add_argument('--names',nargs='*');p.add_argument('--sheet-only',action='store_true');a=p.parse_args()
shots=json.loads((OUT/'source/shots.json').read_text())
if not a.sheet_only:
    exe=ROOT/'build/Release/spyro_workbench.exe';digest=hashlib.sha256(exe.read_bytes()).hexdigest()
    for shot in shots:
        name=shot['name']
        if a.names and name not in a.names:continue
        video=OUT/'gameplay'/f'{name}.mp4';capture=OUT/'review'/f'{name}.bmp'
        if video.exists() and video.stat().st_size>1024:continue
        cmd=[str(exe),*shot['args'],'--video',video.relative_to(ROOT).as_posix(),'--capture',capture.relative_to(ROOT).as_posix()]
        print('Recording',name,flush=True);start=time.monotonic()
        with (OUT/'review'/f'{name}.log').open('w') as log:
            subprocess.run(cmd,cwd=ROOT,creationflags=subprocess.CREATE_NO_WINDOW,stdout=log,stderr=log,timeout=300,check=True)
        assert video.exists() and capture.exists(),name
        report=json.loads(capture.with_suffix('.json').read_text())
        # Keep failed exploratory takes reviewable, but do not approve them
        # solely because the collision engine calls Spyro grounded.
        if '--require-visible-footing' in shot['args'] and report.get('unsupported_grounded_frames',0):
            print('REJECTED visible footing:',name,report['unsupported_grounded_frames'],'unsupported frames',flush=True)
        (OUT/'review'/f'{name}-provenance.json').write_text(json.dumps(dict(executable_sha256=digest,command=cmd,elapsed=time.monotonic()-start),indent=2))
        print('Captured',name,round(time.monotonic()-start,1),'seconds',flush=True)
# Contact frames include the action, rather than only the final resting pose.
files=[]
for shot in shots:
    name=shot['name'];video=OUT/'gameplay'/f'{name}.mp4'
    if not video.exists():continue
    for sec in [.65,1.3,2.1]:
        dest=OUT/'review'/f'{name}-{sec:.2f}.jpg'
        if not dest.exists():
            subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-y','-ss',str(sec),'-i',str(video),'-frames:v','1','-vf','scale=480:270',str(dest)],creationflags=subprocess.CREATE_NO_WINDOW,check=True)
        files.append(dest)
sheet=Image.new('RGB',(1440,300*((len(files)+2)//3)),(12,9,18));d=ImageDraw.Draw(sheet)
for i,f in enumerate(files):
    x=i%3*480;y=i//3*300;sheet.paste(Image.open(f).convert('RGB'),(x,y));d.text((x+8,y+276),f.stem,fill='white')
sheet.save(OUT/'review/gameplay-contact.jpg');print('Saved contact sheet',flush=True)
