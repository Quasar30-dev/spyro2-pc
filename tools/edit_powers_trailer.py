"""Edit verified native clips into a CFR 1080p60 trailer with July Glimmer music."""
from pathlib import Path
import argparse, json, subprocess, wave, hashlib
from PIL import Image, ImageDraw

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'output/spyro-powers-trailer'
WORK=OUT/'edit/segments';WORK.mkdir(parents=True,exist_ok=True)
# name, source, source in, duration, optional retail-font title
EDL=[
 ('logo','opening-logo',0,3,None),
 ('flame-intro-a','intro-glimmer-lizard-v9',.32,.5,None),
 ('flame-intro-b','intro-idol-basket-v9',.32,.5,None),
 ('flame-intro-c','intro-colossus-basket-v9',.32,.5,None),
 ('adventure','book-adventure',0,3.5,None),
 ('portal','portal-gold',.2,3,None),
 ('run','run-pov-v2',1.15,3.5,'regular-run'),
 ('jump','jump-pov-v2',.65,3,'charge-jump'),
 ('whirlwind','whirlwind-close',2.1,3,'whirlwind'),
 ('swim','underwater-close',2.0,2.5,'underwater'),
 ('speed','book-speed',0,3.5,None),
 ('yellow','yellow-pov-v2',.3,2.5,'yellow-charge'),
 ('red','red-pov-v2',.6,3,'red-charge'),
 ('roll','roll-clear-pov-v7',.1,2.5,'dodge-roll'),
 ('glide','glide-pov-v4',.45,3,'glide-boost'),
 ('fly','fly-pov-v4',.6,2,'superfly'),
 ('carry','carry-close',.3,2,'carry'),
 ('drop','drop-final',.3,3,'drop-smash'),
 ('fire','book-fire',0,3.5,None),
 ('homing','homing-pov-v2',.25,3.5,'homing-flame'),
 ('split','superflame-close',.3,2.5,None),
 ('crack','temple-flame-pov-v4',.1,2.5,'wall-flame'),
 ('bomb','temple-breach-pov-v4',.4,3,'green-bomb'),
 ('breach','farm-breach-pov-v6',.15,5.5,'building-breach'),
 ('combat','book-combat',0,3.5,None),
 ('combo','melee-pov-v2',.3,3,'melee'),
 ('crater','headbash-crater',1.1,3,'headbash-crater'),
 ('lava','headbash-lava',1.15,3.5,'headbash-lava'),
 ('recoil','temple-charge-pov-v4',.1,2.5,'wall-charge'),
 ('gems','gem-gold',.4,2.5,None),
 ('end','end-card',0,6,None),
 ('coming','coming-soon',0,3.5,None),
 ('cyborg','crownworks-cyborg-pov-v6',.4,.8,None),
 ('mechs','crownworks-superflame-gulp-v6',1.7,2.35,None),
 ('crush','crownworks-greenbomb-crush-v6',.35,2.35,None),
 ('stinger','stinger',0,6,None),
 ('plants','plants-sting',0,10,None),
]
GRAPHICS=('opening-logo','end-card','stinger','plants-sting','coming-soon')
MUSIC_START=sum(row[3] for row in EDL[:4])
FINAL=OUT/'Spyro 2 PC - Powers Trailer v3.mp4'

def run(args,log):
    with (WORK/log).open('w') as f:
        subprocess.run(['ffmpeg','-hide_banner','-nostdin','-y','-loglevel','warning',*map(str,args)],
                       stdout=f,stderr=f,creationflags=subprocess.CREATE_NO_WINDOW,check=True)

def src_for(name):
    return OUT/('edit' if name in GRAPHICS or name.startswith('book-') else 'gameplay')/f'{name}.mp4'

def segment(row):
    name,source,start,duration,title=row;src=src_for(source);dest=WORK/f'{name}.mp4'
    key=hashlib.sha256(json.dumps([row,src.stat().st_mtime_ns, (OUT/'titles'/f'{title}.png').stat().st_mtime_ns if title else 0,5]).encode()).hexdigest()
    stamp=WORK/f'{name}.sha256'
    if dest.exists() and stamp.exists() and stamp.read_text()==key:return dest
    args=['-threads','4','-filter_complex_threads','2','-ss',start,'-i',src]
    crop=''
    filters=[f'[0:v]{crop}fps=60,scale=1920:1080,setsar=1,trim=end_frame={round(duration*60)},setpts=N/(60*TB),tpad=stop_mode=clone:stop_duration=0.1[base]']
    last='base'
    if source not in GRAPHICS and not source.startswith('book-'):
        grade='eq=contrast=1.02:saturation=1.035'
        if name=='end':grade='gblur=sigma=5,eq=brightness=-.18:saturation=.72'
        filters.append(f'[{last}]{grade},drawbox=x=0:y=0:w=iw:h=84:color=black:t=fill,drawbox=x=0:y=996:w=iw:h=84:color=black:t=fill[film]');last='film'
    if title:
        args+=['-loop','1','-framerate','60','-i',OUT/'titles'/f'{title}.png']
        filters.append(f'[1:v]format=rgba,fade=t=in:st=0.12:d=0.35:alpha=1,fade=t=out:st={duration-.4}:d=0.3:alpha=1[text]')
        filters.append(f'[{last}][text]overlay=0:0:shortest=1[titled]');last='titled'
    finish='format=yuv420p'
    if name=='adventure':finish+=',fade=t=in:st=0:d=0.25'
    if name=='end':finish+=',fade=t=out:st=4.6:d=1.4'
    filters.append(f'[{last}]{finish}[outv]')
    args+=['-filter_complex',';'.join(filters),'-map','[outv]','-an','-frames:v',round(duration*60),
           '-c:v','libx264','-preset','fast','-crf','17','-threads','4','-r','60','-pix_fmt','yuv420p',
           '-video_track_timescale','60000','-movflags','+faststart',dest]
    print('Editing',name,flush=True);run(args,f'{name}.log');stamp.write_text(key);return dest

def audio():
    # Decode each original take and cut at exact PCM boundaries. Joining AAC
    # packets would retain encoder padding and gradually slip sound from film.
    dest=OUT/'edit/gameplay-effects.wav';rate=48000
    with wave.open(str(dest),'wb') as w:
        w.setnchannels(2);w.setsampwidth(2);w.setframerate(rate)
        for name,source,start,duration,title in EDL:
            size=round(duration*rate)*4
            if source in GRAPHICS or source.startswith('book-') or name=='end':pcm=bytes(size)
            else:
                pcm=subprocess.check_output(['ffmpeg','-hide_banner','-loglevel','error','-ss',str(start),'-i',str(src_for(source)),
                    '-vn','-t',str(duration),'-af',f'afade=t=in:d=0.012,afade=t=out:st={duration-.025}:d=0.025',
                    '-f','s16le','-ar',str(rate),'-ac','2','pipe:1'],creationflags=subprocess.CREATE_NO_WINDOW)
                pcm=pcm[:size].ljust(size,b'\0')
            w.writeframesraw(pcm)
    return dest

def edit():
    clips=[segment(row) for row in EDL];duration=sum(row[3] for row in EDL)
    listing=WORK/'concat.txt';listing.write_text(''.join("file '"+p.as_posix()+"'\n" for p in clips))
    picture=OUT/'edit/picture.mp4';run(['-f','concat','-safe','0','-i',listing,'-map','0:v:0','-an','-c:v','copy',picture],'concat.log')
    effects=audio();dest=FINAL;music_duration=duration-MUSIC_START
    roll_start=sum(row[3] for row in EDL[:next(i for i,row in enumerate(EDL) if row[0]=='roll')])-MUSIC_START
    # Let the actual native roll cue read above the score. Its original PCM
    # and pitch are in the take; no added or substituted sound is overlaid.
    duck=f'0.55*(1-0.7*min(1,max(0,(t-{roll_start-.15})/.15))*min(1,max(0,({roll_start+2.8}-t)/.3)))'
    mix=(f'[1:a]aresample=48000,atrim=duration={music_duration},asetpts=PTS-STARTPTS,'
         f"volume='{duck}':eval=frame,"
         f'afade=t=in:d=0.6,afade=t=out:st={music_duration-2.8}:d=2.8,adelay={round(MUSIC_START*1000)}:all=1[music];'
         '[2:a]volume=.78[fx];[music][fx]amix=inputs=2:duration=longest:normalize=0,'
         'alimiter=limit=.94:level=false:latency=true[outa]')
    print('Mixing July Glimmer music and native gameplay effects',flush=True)
    run(['-i',picture,'-i',OUT/'source/july-glimmer.wav','-i',effects,'-filter_complex',mix,'-map','0:v:0','-map','[outa]',
         '-c:v','copy','-c:a','aac','-b:a','320k','-ar','48000','-ac','2','-t',duration,
         '-metadata','title=Spyro 2 PC - Powers Trailer','-metadata','comment=Native gameplay; July 1999 Glimmer music; fan-made work in progress',
         '-movflags','+faststart',dest],'mix.log')
    timeline=[];at=0
    for name,source,start,length,title in EDL:
        timeline.append(dict(name=name,source=source,start=at,source_in=start,duration=length,title=title));at+=length
    (OUT/'source/edit-decision-list-v3.json').write_text(json.dumps(dict(fps=60,duration=duration,music_start=MUSIC_START,shots=timeline),indent=2))
    print('Saved',dest,'duration',duration,flush=True);review(dest,timeline)

def review(dest,timeline=None):
    if timeline is None:timeline=json.loads((OUT/'source/edit-decision-list-v3.json').read_text())['shots']
    sheet=Image.new('RGB',(1440,300*((len(timeline)+2)//3)),(12,9,18));draw=ImageDraw.Draw(sheet)
    for i,shot in enumerate(timeline):
        sample={'gems':.6,'stinger':4.0,'coming':1.75}.get(shot['name'],min(1.1,shot['duration']/2))
        t=shot['start']+sample;still=OUT/'review'/f'edit-{i:02d}-{shot["name"]}.jpg'
        subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-y','-ss',str(t),'-i',str(dest),'-frames:v','1','-vf','scale=480:270',str(still)],
                       creationflags=subprocess.CREATE_NO_WINDOW,check=True)
        x=i%3*480;y=i//3*300;sheet.paste(Image.open(still).convert('RGB'),(x,y));draw.text((x+8,y+276),f'{shot["start"]:05.1f}s {shot["name"]}',fill='white')
    sheet.save(OUT/'review/final-contact.jpg')
    probe=subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(dest)],creationflags=subprocess.CREATE_NO_WINDOW)
    (OUT/'review/final-probe.json').write_bytes(probe)
    p=json.loads(probe);v=next(s for s in p['streams'] if s['codec_type']=='video');a=next(s for s in p['streams'] if s['codec_type']=='audio')
    assert (v['width'],v['height'],v['avg_frame_rate'])==(1920,1080,'60/1')
    assert (a['sample_rate'],a['channels'])==('48000',2)
    assert int(v['nb_frames'])==round(sum(s['duration'] for s in timeline)*60)
    print('Verified native 1920 x 1080, 60 fps, stereo 48 kHz audio',flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--review-only',action='store_true');args=p.parse_args()
    if args.review_only:review(FINAL)
    else:edit()
