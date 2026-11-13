"""Capture the installed quality build in the retained, reproducible views."""
from pathlib import Path
import hashlib,json,subprocess
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/shadow-quality-20261105'

def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()

def main():
 exe=ROOT/'build/Release/spyro_workbench.exe';h=sha(exe)
 assert h==sha(ROOT/'build/soi-cli-candidate/Release/spyro_workbench.exe')
 tests=OUT/'tests.log';assert '226 character shadow / life jar checks passed' in tests.read_text()
 baseline=ROOT/'build/shadows-jars-20261105/evidence'
 jobs=[('glimmer',['--slot','17','--frames','90','--capture-size','1280','720','--capture-fullscreen','--hide-hud']),
 ('jar',['--slot','15','--cinematic-shot',str(baseline/'jar-close-c.s2shot'),'--capture-health','0','--capture-fullscreen']),
 ('jar-release',['--slot','15','--cinematic-shot',str(baseline/'jar-close-released.s2shot'),'--capture-health','0','--capture-fullscreen'])]
 results=[]
 for name,args in jobs:
  cap=OUT/(name+'-after.bmp');log=OUT/(name+'.log')
  command=[str(exe),'--headless','--game',str(ROOT/'data/disc'),*args,'--menu-profile',str(OUT/(name+'-profile.json')),'--capture',str(cap)]
  with log.open('w') as f:
   f.write('EXE SHA256 '+h+'\nCOMMAND '+json.dumps(command)+'\n');f.flush()
   r=subprocess.run(command,cwd=ROOT,stdout=f,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW);assert r.returncode==0,(name,r.returncode)
   report=json.loads(cap.with_suffix('.json').read_text());assert report['retail_script_faults']==0
   assert report['rendered_character_shadows']>0 and report['rendered_shadow_triangles']>0
   if name=='jar':assert report['health']==0 and report['sparx_butterflies_eaten']==0
   if name=='jar-release':assert report['health']==3 and report['sparx_butterflies_eaten']==1
   Image.open(cap).save(cap.with_suffix('.png'));f.write('QUALITY CAPTURE PASS '+name+'\n')
  results.append(dict(name=name,path=str(cap.with_suffix('.png')),report=str(cap.with_suffix('.json')),mean_render_ms=report.get('mean_render_ms'),shadow_count=report['rendered_character_shadows'],shadow_triangles=report['rendered_shadow_triangles']))
  print('QUALITY CAPTURE PASS',name,flush=True)
 for name in ('glimmer','jar'):
  before=Image.open(OUT/(name+'-before.png')).convert('RGB');after=Image.open(OUT/(name+'-after.png')).convert('RGB');assert before.size==after.size
  sheet=Image.new('RGB',(before.width*2,before.height+36),(12,12,16));sheet.paste(before,(0,36));sheet.paste(after,(before.width,36));d=ImageDraw.Draw(sheet)
  d.text((14,10),'Before: 2048 / nearest comparison steps',fill='white');d.text((before.width+14,10),'After: 4096 / bilinear comparison + weighted filtering',fill='white');sheet.save(OUT/(name+'-comparison.png'))
  crop=(490,350,830,560) if name=='glimmer' else (690,410,1010,650)
  a=before.crop(crop).resize((680,420),Image.Resampling.NEAREST);b=after.crop(crop).resize((680,420),Image.Resampling.NEAREST)
  detail=Image.new('RGB',(1360,456),(12,12,16));detail.paste(a,(0,36));detail.paste(b,(680,36));d=ImageDraw.Draw(detail);d.text((12,10),'Before / identical crop, enlarged without filtering',fill='white');d.text((692,10),'After / identical crop, enlarged without filtering',fill='white');detail.save(OUT/(name+'-detail.png'))
 ready=dict(ready=True,package_suffix='-shadows-r2',main_exe=str(exe),sha256=h,shadow_quality=dict(map_size=4096,coverage=96,texel_metres=96/4096,filter='hardware bilinear depth comparison, 1/2/1 weighted PCF',map_memory_mib=64),captures=results,logs=[],limitations=['Automated hidden captures and GPU receiver checks; manual gameplay remains for the user.','The approved trailer edit is retained; this quality update applies to the executable and portable ZIP.','Shadows receive on opaque terrain.'])
 for p,needle in ((OUT/'build.log','spyro_workbench.vcxproj ->'),(OUT/'install.log','SHA256:'),(tests,'226 character shadow / life jar checks passed')):
  lines=p.read_text().splitlines();i=next(i for i,line in enumerate(lines,1) if needle in line);ready['logs'].append(dict(path=str(p),line=i,text=lines[i-1],sha256=sha(p),result='PASS'))
 (OUT/'ready.json').write_text(json.dumps(ready,indent=2));print('SHADOW QUALITY PASS '+h,flush=True)

if __name__=='__main__':main()
