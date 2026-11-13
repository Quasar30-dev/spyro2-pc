"""Edit native and GPT-still editions, preserving 1080p60 and gameplay audio."""
from pathlib import Path
import argparse, json, subprocess, wave

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'output/hollowbloom-trailers'
WORK=OUT/'source/edit';WORK.mkdir(exist_ok=True)

def run(args,log):
    with (WORK/log).open('w') as f:
        subprocess.run(['ffmpeg','-hide_banner','-nostdin','-y','-loglevel','warning',*map(str,args)],stdout=f,stderr=f,check=True)

# (name, native source, duration, source in, overlays, fade-in/out duration)
EDL=[
 ('opening','01-vista',6,.2,['01-opening'],(.8,.25)),
 ('orchard','03-moonwell',5,.6,['02-discover'],(.25,.25)),
 ('run','02-orchard-run',6,1.1,['03-play'],(.15,0)),
 ('basket','04-basket',3.5,.35,[],(0,0)),
 ('enemy','05-enemy',3.5,.15,[],(0,0)),
 ('charge','06-charge-jump',4.5,.15,[],(0,0)),
 ('bridge','09-bridge',4,1.4,[],(0,.25)),
 ('moonwell','08-spyro',4.5,.7,[],(.25,.25)),
 ('conservatory','07-conservatory',5,1,[],(.25,.3)),
 ('title','01-vista',7,0,['04-title','05-end'],(.4,1.1)),
]
ILLUSTRATIONS={'orchard':'01-drowned-orchard','moonwell':'02-moonwell'}

def segment(row, edition):
    name,native,duration,start,overlays,fades=row
    illustrated=edition=='gpt-image' and name in ILLUSTRATIONS
    key=f'{name}-{edition}' if illustrated or name=='title' else name
    output=WORK/f'{key}.mp4'
    if output.exists():return output
    args=['-threads','4','-filter_complex_threads','2']
    if illustrated:
        src=OUT/'stills'/f'{ILLUSTRATIONS[name]}.png'
        args+=['-loop','1','-framerate','60','-i',src,'-f','lavfi','-i','anullsrc=r=48000:cl=stereo']
        audio='1:a';next_input=2
        base=f"scale=3840:2160:force_original_aspect_ratio=increase,crop=3840:2160,zoompan=z='1.025+0.035*min(on/{round(duration*60)},1)':x='(iw-iw/zoom)*0.45':y='(ih-ih/zoom)*0.5':d=1:s=1920x1080:fps=60"
    else:
        src=OUT/'gameplay'/f'{native}.mp4';args+=['-ss',start,'-i',src];audio='0:a';next_input=1
        base='fps=60,scale=1920:1080,setsar=1'
    base+=f',trim=duration={duration},setpts=PTS-STARTPTS'
    if name=='title':base+=',eq=brightness=-0.075:saturation=0.75:contrast=1.04'
    elif name=='opening':base+=',eq=brightness=-0.035:saturation=0.9:contrast=1.04'
    elif not illustrated:base+=',eq=contrast=1.04:brightness=0.005:saturation=1.08:gamma=1.02'
    base+=',vignette=PI/7,drawbox=x=0:y=0:w=iw:h=132:color=black:t=fill,drawbox=x=0:y=948:w=iw:h=132:color=black:t=fill,format=yuv420p'
    filters=[f'[0:v]{base}[base]'];last='base'
    if name=='title':overlays=[*overlays,'06-art-credit' if edition=='gpt-image' else '07-native-credit']
    for i,overlay in enumerate(overlays):
        args+=['-loop','1','-framerate','60','-i',OUT/'titles'/f'{overlay}.png']
        fade_start=.5 if name!='title' else .65+i*.5
        fade_end=duration-.7
        if name=='opening':fade_end=4.4
        if name=='run':fade_end=3.2
        filters.append(f'[{next_input+i}:v]format=rgba,fade=t=in:st={fade_start}:d=0.65:alpha=1,fade=t=out:st={fade_end}:d=0.5:alpha=1[ov{i}]')
        filters.append(f'[{last}][ov{i}]overlay=0:0:shortest=1[v{i}]');last=f'v{i}'
    finish='null'
    if fades[0]:finish+=f',fade=t=in:st=0:d={fades[0]}'
    if fades[1]:finish+=f',fade=t=out:st={duration-fades[1]}:d={fades[1]}'
    filters.append(f'[{last}]{finish},format=yuv420p[outv]')
    filters.append(f'[{audio}]atrim=duration={duration},asetpts=PTS-STARTPTS,aresample=48000,apad,atrim=duration={duration},afade=t=in:d=0.015,afade=t=out:st={duration-.03}:d=0.03[outa]')
    args+=['-filter_complex',';'.join(filters),'-map','[outv]','-map','[outa]','-t',duration,
           '-c:v','libx264','-preset','fast','-crf','18','-threads','4','-r','60','-video_track_timescale','60000',
           '-c:a','aac','-b:a','192k','-ar','48000','-ac','2','-movflags','+faststart',output]
    print('Rendering',key,flush=True);run(args,f'{key}.log');return output

def edit(edition):
    clips=[segment(row,edition) for row in EDL]
    listing=WORK/f'{edition}-concat.txt'
    listing.write_text(''.join("file '"+p.as_posix()+"'\n" for p in clips))
    intermediate=WORK/f'{edition}-picture-and-effects.mp4'
    run(['-f','concat','-safe','0','-i',listing,'-map','0:v:0','-an','-c:v','copy',intermediate],f'{edition}-concat.log')
    # Concatenating AAC packets retains each clip's encoder delay and causes
    # overlapping timestamps. Join decoded effects at exact sample boundaries.
    effects=WORK/f'{edition}-effects.wav'
    with wave.open(str(effects),'wb') as wav:
        wav.setnchannels(2);wav.setsampwidth(2);wav.setframerate(48000)
        for clip,row in zip(clips,EDL):
            pcm=subprocess.check_output(['ffmpeg','-hide_banner','-loglevel','error','-i',str(clip),'-vn','-f','s16le','-ar','48000','-ac','2','pipe:1'])
            size=round(row[2]*48000)*4;wav.writeframes(pcm[:size].ljust(size,b'\0'))
    duration=sum(r[2] for r in EDL)
    title='Hollowbloom Springs - GPT Image Edition' if edition=='gpt-image' else 'Hollowbloom Springs - Gameplay Edition'
    dest=OUT/f'{title}.mp4'
    # A continuous retail score underneath synchronous native footsteps,
    # gems, flame and break sounds. Soften the opening and resolve the ending.
    music=(f'[1:a]aresample=48000,atrim=duration={duration},asetpts=PTS-STARTPTS,highpass=f=35,'
           f"volume='if(lt(t,6),0.20+0.035*t,if(lt(t,11),0.41,0.50))':eval=frame,afade=t=in:d=1.5,afade=t=out:st={duration-3}:d=3[m];"
           f'[2:a]volume=0.95[fx];[m][fx]amix=inputs=2:duration=first:normalize=0,alimiter=limit=0.89:level=false[outa]')
    print('Mixing',title,flush=True)
    run(['-i',intermediate,'-i',OUT/'source/glimmer-music.wav','-i',effects,'-filter_complex',music,'-map','0:v:0','-map','[outa]',
         '-c:v','copy','-c:a','aac','-b:a','320k','-ar','48000','-ac','2','-t',duration,'-movflags','+faststart',
         '-metadata',f'title={title}','-metadata','comment=Fan-made Spyro 2 PC level trailer. Retail font, music and gameplay assets from the mounted copy.',dest],f'{edition}-mix.log')
    timeline=[];at=0
    for name,native,length,start,overlays,fades in EDL:
        src=f'stills/{ILLUSTRATIONS[name]}.png' if edition=='gpt-image' and name in ILLUSTRATIONS else f'gameplay/{native}.mp4'
        timeline.append(dict(start=at,duration=length,source=src,source_in=0 if src.startswith('stills') else start));at+=length
    (OUT/'source'/f'{edition}-timeline.json').write_text(json.dumps(dict(duration=duration,fps=60,width=1920,height=1080,timeline=timeline),indent=2)+'\n')
    print('FINISHED',dest,flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('edition',choices=['gameplay','gpt-image','both']);a=p.parse_args()
    for e in (['gameplay','gpt-image'] if a.edition=='both' else [a.edition]):edit(e)
