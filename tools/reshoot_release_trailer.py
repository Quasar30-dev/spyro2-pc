"""Reshoot the released edit with one explicit, hash-bound executable.

Keeps every published cut/in-point/caption and the removed opening removed.
No old character footage enters the new trailer. The old release stays intact
until the new encode and its contact sheets have been accepted.
"""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,sys,time
import release_trailer as T
from PIL import Image,ImageDraw

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'output/spyro2-pc-release-trailer-shadows-20261105'
PUBLIC=Path('D:/Theia-Promotional-Videos/05 - Spyro 2 PC - Release Trailer.mp4')
ORIGINAL=ROOT/'output/spyro2-pc-release-trailer'

def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()

def configure():
 for d in ('source','gameplay','review','edit','titles','logo','exe'):(OUT/d).mkdir(parents=True,exist_ok=True)
 receipt=OUT/'source/original-edit.json'
 if not receipt.exists():shutil.copy2(PUBLIC.with_suffix('.json'),receipt)
 original=json.loads(receipt.read_text());edl=original['edl']
 assert all(r[0]!='open' and r[1]!='old:spring-flyover' for r in edl)
 T.OUT=OUT;T.STAGE=OUT/'source';T.EXE=T.EXE_SOI=OUT/'exe/spyro_workbench.exe'
 T.OLD=OUT/'gameplay';T.EDL=edl;T.MUSIC=ORIGINAL/'source/retail-glimmer.wav'
 T.FINAL=OUT/'Spyro 2 PC - Release Trailer (character shadows).mp4'
 # Identical existing graphical cards and logos, never an old gameplay cut.
 for name in ('logo.mp4','logo-end.mp4'):shutil.copy2(ORIGINAL/'edit'/name,OUT/'edit'/name)
 return original

def plan():
 selected={r[1] for r in T.EDL if r[1] not in ('card','logo','logo-end')}
 new=T.shots();old={r['name']:r['args'] for r in json.loads(T.P.SHOTS.read_text())}
 result={}
 for source in selected:
  key=source[4:] if source.startswith('old:') else source
  args=old[key] if source.startswith('old:') else new[key][1]+T.BASE
  if '--headless' not in args:args=[*args,'--headless']
  result[key]=args
 return result

def capture(exe,names):
 configure();target=OUT/'exe/spyro_workbench.exe'
 h=sha(exe)
 if target.exists():assert sha(target)==h,'Use a fresh output directory for a different executable'
 else:shutil.copy2(exe,target)
 for p in Path(exe).parent.glob('*.dll'):shutil.copy2(p,target.parent/p.name)
 (OUT/'source/executable.json').write_text(json.dumps(dict(source=str(exe),sha256=h,copy=str(target)),indent=2))
 for name,args in sorted(plan().items()):
  if names and name not in names:continue
  video=OUT/'gameplay'/f'{name}.mp4';log=OUT/'review'/f'{name}.log'
  receipt=OUT/'review'/f'{name}-capture.json'
  if receipt.exists():
   saved=json.loads(receipt.read_text());assert saved['executable_sha256']==h and sha(video)==saved['sha256']
   print('CAPTURE REUSED',name,flush=True);continue
  command=[str(target),*args,'--video',str(video),'--capture',str(OUT/'review'/f'{name}.bmp')]
  print('CAPTURE START',name,flush=True);start=time.monotonic()
  with log.open('w') as f:r=subprocess.run(command,cwd=ROOT,stdout=f,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
  accept_capture(name,args,r.returncode,h,log)
  print('CAPTURE READY',name,round(time.monotonic()-start,2),flush=True)

def accept_capture(name,args,returncode,h,log):
 video=OUT/'gameplay'/f'{name}.mp4';assert video.is_file(),(name,returncode,log)
 note='Completed source run'
 if returncode:
  assert returncode==1 and '--challenge-check' in args,(name,returncode,log)
  error=(ROOT/'data/validation/workbench-error.txt').read_text().strip()
  assert error.startswith(f'Challenge check ({name}): the challenge did not finish in the bounded frames (step '),error
  (OUT/'review'/f'{name}-bounded-error.txt').write_text(error+'\n')
  note='Bounded gameplay capture; challenge completion NOT established. '+error
 p=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_format','-of','json',str(video)]))
 duration=float(p['format']['duration'])
 selected=[r for r in T.EDL if r[1].removeprefix('old:')==name]
 assert selected and duration>=max(r[2]+r[3] for r in selected)
 result=dict(name=name,sha256=sha(video),executable_sha256=h,seconds=duration,source_returncode=returncode,note=note,args=args)
 (OUT/'review'/f'{name}-capture.json').write_text(json.dumps(result,indent=2))

def render():
 original=configure();assert all((OUT/'gameplay'/f'{n}.mp4').exists() for n in plan())
 # These cards/captions are unchanged by the requested gameplay reshoot.
 # Preserve the already approved pixels instead of regenerating hundreds of logo frames.
 for row in T.EDL:
  title=ORIGINAL/'titles'/f'{row[0]}.png';assert title.exists(),title
  shutil.copy2(title,OUT/'titles'/title.name)
 graphics=T.P.graphics;T.P.graphics=lambda font:None
 try:T.edit()
 finally:T.P.graphics=graphics
 log=OUT/'review/full-decode.log'
 with log.open('w') as f:r=subprocess.run(['ffmpeg','-v','error','-xerror','-i',str(T.FINAL),'-f','null','-'],stdout=f,stderr=f)
 assert r.returncode==0 and not log.read_text().strip(),log
 p=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_format','-of','json',str(T.FINAL)]))
 duration=float(p['format']['duration']);assert abs(duration-original['seconds'])<.15
 receipt=dict(file=str(T.FINAL),public_file=str(PUBLIC),seconds=duration,sha256=sha(T.FINAL),
  edl=T.EDL,original_edit=original['sha256'],executable=json.loads((OUT/'source/executable.json').read_text()),
  decode='PASS',picture='All gameplay recaptured with character shadows; same shots, cut order, timing and captions. Opening Spring Realm flyover remains removed.',
  audio='Same retail Glimmer music and native captured game effects; same mix and fades.')
 (OUT/'review/rendered.json').write_text(json.dumps(receipt,indent=2))
 review_frames(receipt)
 print('TRAILER RENDERED',duration,receipt['sha256'],flush=True)

def review_frames(receipt):
 """Review the actual finished encode at both quarters of every picture cut."""
 rows=[];at=0
 for row in T.EDL:
  name,source,start,duration,lines=row
  for fraction in (.25,.75):rows.append((name,at+duration*fraction))
  at+=duration
 for page in range((len(rows)+15)//16):
  selected=rows[page*16:page*16+16];sheet=Image.new('RGB',(1920,4*296),(12,12,16));draw=ImageDraw.Draw(sheet)
  for i,(name,second) in enumerate(selected):
   p=OUT/'review'/f'finished-{page*16+i:02}.png'
   subprocess.run(['ffmpeg','-v','error','-y','-ss',str(second),'-i',str(T.FINAL),'-frames:v','1','-vf','scale=480:270',str(p)],check=True)
   x=i%4*480;y=i//4*296;sheet.paste(Image.open(p),(x,y));draw.text((x+8,y+275),f'{second:.2f}s / {name}',fill='white')
  sheet.save(OUT/'review'/f'finished-sheet-{page+1}.png')
 with (OUT/'review/audio-levels.log').open('w') as f:
  subprocess.run(['ffmpeg','-hide_banner','-i',str(T.FINAL),'-af','volumedetect','-vn','-f','null','NUL'],stdout=f,stderr=f,check=True)

def deploy():
 configure();r=json.loads((OUT/'review/rendered.json').read_text());v=json.loads((OUT/'review/final-review.json').read_text())
 assert v['accepted'] and v['sha256']==r['sha256'] and sha(T.FINAL)==r['sha256']
 shutil.copy2(T.FINAL,PUBLIC);PUBLIC.with_suffix('.json').write_text(json.dumps(r,indent=2))
 PUBLIC.with_suffix('.mp4.sha256').write_text(r['sha256']+'  '+PUBLIC.name+'\n')
 shutil.copy2(T.FINAL,ORIGINAL/'Spyro 2 PC - Release Trailer (November 2026).mp4')
 update_public_metadata(r)
 assert sha(PUBLIC)==r['sha256'];print('TRAILER DELIVERED',PUBLIC,flush=True)

def update_public_metadata(r):
 root=PUBLIC.parent;key=PUBLIC.stem;thumb=root/(key+' - Thumbnail.png');assert thumb.exists()
 sums=root/'SHA256SUMS.txt';rows=sums.read_text().splitlines();rows=[x for x in rows if not x.endswith('  '+PUBLIC.name)]
 rows.append(r['sha256']+'  '+PUBLIC.name);sums.write_text('\n'.join(rows)+'\n')
 review=root/'Review';review.mkdir(exist_ok=True)
 for p in (OUT/'review').glob('finished-sheet-*.png'):shutil.copy2(p,review/(key+' - '+p.name))
 status=root/'editorial-status.json';d=json.loads(status.read_text());d[key]=dict(status='ready',reason=r['picture'],duration=r['seconds'],sha256=r['sha256'],receipt=str(PUBLIC.with_suffix('.json')),review=str(OUT/'review/final-review.json'));status.write_text(json.dumps(d,indent=2))
 media=root/'media-verification.json';d=json.loads(media.read_text());d['videos']=[x for x in d['videos'] if x['file']!=PUBLIC.name]
 d['videos'].append(dict(file=PUBLIC.name,bytes=PUBLIC.stat().st_size,seconds=r['seconds'],sha256=r['sha256'],video='1920x1080 / 60fps / H.264',audio='AAC / 48kHz / stereo',decode='PASS',audio_measurements=[x.strip() for x in (OUT/'review/audio-levels.log').read_text().splitlines() if 'mean_volume:' in x or 'max_volume:' in x],poster=thumb.name,storyboard='Review/'+key+' - finished-sheet-1.png'))
 media.write_text(json.dumps(d,indent=2))
 title='Spyro 2 PC - Release Trailer | The Adventure on PC'
 description="Spyro 2 PC: explore Avalar, collect talismans and take on the orb challenges. This trailer includes ice hockey, gem lamps, sheep saucers, turtle soup, Dem Bones, crow bugs, power-ups and boss encounters.\n\nReshot with the updated PC build: Spyro and other characters now cast animated shadows. Life butterflies also remain inside their jars until released.\n\nAlso previewed: Market Mesa from the Season of Ice adaptation, which is still in development.\n\nDownload / project link:\n\n\nFan-made PC project. Same shots and edit timing; the opening Spring Realm flyover remains removed.\n\n#Spyro #Spyro2 #PCGaming"
 (root/(key+' - YouTube.txt')).write_text(title+'\n\n'+description+'\n')
 manifest=root/'youtube-upload-manifest.json';d=json.loads(manifest.read_text());d['long_videos']=[x for x in d['long_videos'] if Path(x['file']).name!=PUBLIC.name]
 d['long_videos'].append(dict(file=str(PUBLIC),title=title,description=description,thumbnail=str(thumb),thumbnail_sha256=sha(thumb),visibility='public',instant_premiere=True,made_for_kids=False,status='NOT UPLOADED BY THIS SCRIPT',editorial_status='ready',source_notes=r['picture'],sha256=r['sha256']))
 manifest.write_text(json.dumps(d,indent=2))
 text=root/'YouTube descriptions.txt';s=text.read_text();marker='FILE: '+str(PUBLIC)
 if marker not in s:text.write_text(s+'\nSTATUS: READY\n'+marker+'\nTHUMBNAIL: '+str(thumb)+'\nTITLE: '+title+'\n\nDESCRIPTION:\n'+description+'\n\n'+'-'*72+'\n')
 gallery=root/'Open Media Gallery.html';s=gallery.read_text();s=s.replace('79.2 seconds · revised release trailer','79.2 seconds · character shadows reshoot');gallery.write_text(s)

if __name__=='__main__':
 a=argparse.ArgumentParser();a.add_argument('command',choices=('capture','render','deploy'));a.add_argument('--exe',type=Path);a.add_argument('--render-after',action='store_true');a.add_argument('names',nargs='*');o=a.parse_args()
 if o.command=='capture':
  assert o.exe;capture(o.exe,o.names)
  if o.render_after:render()
 elif o.command=='render':render()
 else:deploy()
