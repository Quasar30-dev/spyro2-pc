"""Validate the final trailer files, including decoded frame cadence and sound."""
from pathlib import Path
import json, subprocess

base=Path(__file__).resolve().parents[1]/'output/hollowbloom-trailers'
report={}
for edition in ['Gameplay','GPT Image']:
    p=base/f'Hollowbloom Springs - {edition} Edition.mp4'
    metadata=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(p)]))
    video=next(s for s in metadata['streams'] if s['codec_type']=='video')
    audio=next(s for s in metadata['streams'] if s['codec_type']=='audio')
    assert (video['width'],video['height'],video['avg_frame_rate'],video['nb_frames'])==(1920,1080,'60/1','2940')
    assert audio['sample_rate']=='48000' and audio['channels']==2
    assert float(video['duration'])==49 and float(audio['duration'])==49
    r=subprocess.run(['ffmpeg','-hide_banner','-i',str(p),'-af','ebur128=peak=true','-f','null','NUL'],capture_output=True,text=True,check=True)
    (base/'review'/(p.stem+'-decode-audio.log')).write_text(r.stderr)
    tail=r.stderr[r.stderr.rfind('Summary:'):];print(p.name,tail,flush=True)
    frames=json.loads(subprocess.check_output(['ffprobe','-v','error','-select_streams','v:0','-show_entries','frame=best_effort_timestamp_time','-of','json',str(p)]))
    timestamps=[float(x['best_effort_timestamp_time']) for x in frames['frames']]
    gaps=[b-a for a,b in zip(timestamps,timestamps[1:])]
    assert len(timestamps)==2940 and max(abs(x-1/60) for x in gaps)<2e-6
    report[p.name]={'frames':len(timestamps),'max_frame_interval':max(gaps),'min_frame_interval':min(gaps),'duration_seconds':49,
        'width':1920,'height':1080,'fps':60,'audio_hz':48000,'audio_channels':2,'decode_ok':True,'bytes':p.stat().st_size,'loudness':tail}
    (base/'review'/(p.stem+'-probe.json')).write_text(json.dumps(metadata,indent=2)+'\n')
(base/'review'/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
print('Both trailers decoded successfully with 2940 frames and uniform 60 fps.',flush=True)
