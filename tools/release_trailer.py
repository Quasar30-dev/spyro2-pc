"""Spyro 2 PC release trailer (November 2026): capture, music and edit.

Native 1080p60 footage from the shipped package's own exe (dist/spyro2-pc-20261104b.zip, copied to
D:/Theia-staging/s2-trailer/exe so the challenges agent's rebuilds of build/Release are never locked), cut to the retail
Glimmer music, titles in the retail Spyro 2 font, the Spyro 2 PC logo. New footage: orb challenges and a level ending
(the workbench's own --challenge-check / --level-exit-check pilots, by input), Market Mesa (the Season of Ice port, its
build-soi exe). Reused from the October trailer: Spring Realm, superfly, power gate, Crush, Gulp.

  python tools/release_trailer.py capture [names...]
  python tools/release_trailer.py sheets [names...]   contact sheets (a frame every 2 s) to pick in-points
  python tools/release_trailer.py music
  python tools/release_trailer.py edit
"""
from pathlib import Path
import hashlib, json, math, shutil, subprocess, sys, time

from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tools/soi'))
import pc_trailer as P  # noqa: E402  (retail font, captions, ffmpeg helper, S2SHOT v1 writer)

OUT = ROOT / 'output/spyro2-pc-release-trailer'
for folder in ('source', 'gameplay', 'review', 'titles', 'edit'):
    (OUT / folder).mkdir(parents=True, exist_ok=True)
STAGE = Path('D:/Theia-staging/s2-trailer')
EXE = STAGE / 'exe/spyro_workbench.exe'
EXE_SOI = STAGE / 'exe-soi/spyro_workbench.exe'
PROBE = STAGE / 'exe/spyro_audio_probe.exe'
OLD = P.OUT / 'gameplay'          # the October trailer's clips
BASE = ['--capture-size', '1920', '1080', '--capture-music-volume', '0']


def mesa_shots():
    """Market Mesa: a sweep round the mesa, and Spyro's proof route (flame, terraces, gems, a gap jump)."""
    import soi_pc_proof as S
    tgt = (600, 280, 220)
    eyes = [(1250, 900, 640), (1250, 150, 600), (900, -300, 650), (300, -380, 680)]
    keys = [(i * 150, S.eng(e), S.eng(tgt)) for i, e in enumerate(eyes)]
    sweep = STAGE / 'mesa-sweep.s2shot'
    sweep.write_text(S.shot_text(S.ROUTES['mesa'] | {'route': [], 'actions': []}, 470, hud=False, fov=50, mode=1,
                                 cameras=keys, size=(1920, 1080)))
    run = STAGE / 'mesa-run.s2shot'
    run.write_text(S.shot_text(S.ROUTES['mesa'], 420, hud=True, fov=55, mode=0, size=(1920, 1080)))
    world = ROOT / 'data/soi/pc/market-mesa.s2world'
    return {
        'mesa-sweep': (EXE_SOI, ['--game', 'data/disc', '--custom-level', str(world), '--cinematic-shot', str(sweep)]),
        'mesa-run': (EXE_SOI, ['--game', 'data/disc', '--custom-level', str(world), '--cinematic-shot', str(run)]),
    }


def shots():
    s = {}
    for name, frames in (('colossus-hockey-1v1', 16000), ('glimmer-lamps-outdoor', 6000), ('metropolis-saucers-1', 5000),
                         ('sunny-beach-soup-1', 9000), ('skelos-dem-bones', 9000), ('robotica-crow-bugs', 9000)):
        s[name] = (EXE, ['--game', 'data/disc', '--challenge-check', name, '--smoke', str(frames)])
    for slot in (19, 25):        # Idol Springs, Aquaria Towers: talisman hand-over with the retail reward camera
        s[f'exit-{slot}'] = (EXE, ['--game', 'data/disc', '--level-exit-check', str(slot), '--smoke', '9000'])
    s.update(mesa_shots())
    return s


def capture(names):
    for name, (exe, args) in shots().items():
        if names and name not in names:
            continue
        video = OUT / 'gameplay' / f'{name}.mp4'
        still = STAGE / 'stills' / f'{name}.bmp'
        still.parent.mkdir(parents=True, exist_ok=True)
        cmd = [str(exe), *args, *BASE, '--video', str(video), '--capture', str(still)]
        start = time.monotonic()
        with (OUT / 'review' / f'{name}.log').open('w') as log:
            r = subprocess.run(cmd, cwd=ROOT, stdout=log, stderr=log, timeout=1800, creationflags=subprocess.CREATE_NO_WINDOW)
        print('ok  ' if r.returncode == 0 and video.exists() else 'FAIL', name, r.returncode,
              round(time.monotonic() - start, 1), 's', flush=True)


def sheets(names):
    """A frame every 2 s of each new clip, labelled with its time, to choose in-points."""
    for clip in sorted((OUT / 'gameplay').glob('*.mp4')):
        if names and clip.stem not in names:
            continue
        frames = OUT / 'review' / f'{clip.stem}-f'
        frames.mkdir(exist_ok=True)
        subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-i', str(clip), '-vf', 'fps=0.5,scale=320:180',
                        str(frames / '%03d.png')], check=True, creationflags=subprocess.CREATE_NO_WINDOW)
        pics = sorted(frames.glob('*.png'))
        cols = 8
        rows = math.ceil(len(pics) / cols) or 1
        sheet = Image.new('RGB', (cols * 320, rows * 180), (16, 16, 16))
        d = ImageDraw.Draw(sheet)
        for k, p in enumerate(pics):
            x, y = (k % cols) * 320, (k // cols) * 180
            sheet.paste(Image.open(p), (x, y))
            d.text((x + 4, y + 4), f'{k * 2}s', fill=(255, 255, 0))
        sheet.save(OUT / 'review' / f'{clip.stem}-sheet.png')
        print(clip.stem, len(pics) * 2, 's')


def music():
    dest = OUT / 'source/retail-glimmer.wav'
    subprocess.run([str(PROBE), 'data/disc', '--slot', '17', '--wave', str(dest)], cwd=ROOT, check=True,
                   creationflags=subprocess.CREATE_NO_WINDOW)
    print('Decoded', dest.name, dest.stat().st_size, 'bytes')


# name, source (clip name: new clips in gameplay/, 'old:' October clips, 'card', 'logo', 'logo-end'), in, duration, captions
EDL = json.loads((OUT / 'source/edl.json').read_text()) if (OUT / 'source/edl.json').exists() else []
MUSIC = OUT / 'source/retail-glimmer.wav'
FINAL = OUT / 'Spyro 2 PC - Release Trailer (November 2026).mp4'


def clip_path(source):
    return OLD / f'{source[4:]}.mp4' if source.startswith('old:') else OUT / 'gameplay' / f'{source}.mp4'


def edit():
    P.OUT = OUT                    # P.ffmpeg / P.graphics write under this trailer's folders
    font = P.RetailFont()
    logo_src = P.ROOT / 'output/spyro2-pc-trailer/logo/spyro-2-pc-logo.png'
    (OUT / 'logo').mkdir(exist_ok=True)
    if not (OUT / 'logo/spyro-2-pc-logo.png').exists():
        Image.open(logo_src).save(OUT / 'logo/spyro-2-pc-logo.png')
    P.EDL = [(n, s, a, d, [tuple(l) for l in lines]) for n, s, a, d, lines in EDL]
    P.graphics(font)               # the logo cards and every caption overlay, from P.EDL
    segments = []
    for k, (name, source, start, duration, lines) in enumerate(P.EDL):
        dest = OUT / 'edit' / f'{k:02d}-{name}.mp4'
        title = OUT / 'titles' / f'{name}.png'
        fade = f"fade=t=in:st=0:d=0.25:alpha=1,fade=t=out:st={duration - .35}:d=0.3:alpha=1"
        enc = ['-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-crf', 16, '-c:a', 'aac', '-ar', 48000, dest]
        if source == 'card':
            P.ffmpeg(['-f', 'lavfi', '-i', f'color=c=0x080512:s=1920x1080:r=60:d={duration}', '-f', 'lavfi', '-i',
                      f'anullsrc=r=48000:cl=stereo:d={duration}', '-loop', 1, '-t', duration, '-i', title,
                      '-filter_complex', f"[2:v]format=rgba,{fade}[t];[0:v][t]overlay=0:0[v]", '-map', '[v]', '-map', '1:a',
                      '-t', duration, *enc], f'{name}.log')
        elif source.startswith('logo'):
            P.ffmpeg(['-i', OUT / 'edit' / f'{source}.mp4', '-f', 'lavfi', '-i', f'anullsrc=r=48000:cl=stereo:d={duration}',
                      '-loop', 1, '-t', duration, '-i', title, '-filter_complex', f"[2:v]format=rgba,{fade}[t];[0:v][t]overlay=0:0[v]",
                      '-map', '[v]', '-map', '1:a', '-t', duration, *enc], f'{name}.log')
        else:
            P.ffmpeg(['-ss', start, '-t', duration, '-i', clip_path(source), '-loop', 1, '-t', duration, '-i', title,
                      '-filter_complex', f"[0:v]scale=1920:1080,fps=60,setsar=1[g];[1:v]format=rgba,{fade}[t];[g][t]overlay=0:0[v];"
                                         f"[0:a]aresample=48000,apad[a]", '-map', '[v]', '-map', '[a]', '-t', duration, *enc], f'{name}.log')
        segments.append(dest)
        print('Cut', dest.name, flush=True)
    listing = OUT / 'edit/segments.txt'
    listing.write_text(''.join(f"file '{s.as_posix()}'\n" for s in segments))
    joined = OUT / 'edit/joined.mp4'
    P.ffmpeg(['-f', 'concat', '-safe', 0, '-i', listing, '-c', 'copy', joined], 'concat.log')
    total = sum(row[3] for row in P.EDL)
    mix = (f"[1:a]aresample=48000,atrim=duration={total},asetpts=PTS-STARTPTS,volume=0.9,afade=t=in:d=1.2,"
           f"afade=t=out:st={total - 3}:d=3[m];[0:a]volume=0.55[fx];[m][fx]amix=inputs=2:duration=first:normalize=0,"
           f"alimiter=limit=0.95[a]")
    P.ffmpeg(['-i', joined, '-stream_loop', -1, '-i', MUSIC, '-filter_complex', mix, '-map', '0:v', '-map', '[a]', '-c:v', 'copy',
              '-c:a', 'aac', '-b:a', '256k', '-metadata', 'title=Spyro 2 PC - Release Trailer',
              '-metadata', 'comment=Native gameplay; retail music; fan-made', FINAL], 'final.log')
    print('Wrote', FINAL, round(total, 1), 's', flush=True)


def edit_no_opening():
    """Remove only the opening Spring Realm shot; retain all later finished cuts.

    Join the original encoded picture cuts and render a fresh continuous music /
    game-audio mix. This avoids a keyframe trim or an extra video generation.
    """
    assert EDL[0][1] == 'old:spring-flyover' and EDL[0][0] == 'open'
    P.OUT=OUT
    selected=EDL[1:]
    clips=[OUT/'edit'/f'{i:02d}-{row[0]}.mp4' for i,row in enumerate(EDL) if i]
    assert all(p.is_file() for p in clips)
    listing=OUT/'edit/segments-no-spring-opening.txt'
    listing.write_text(''.join(f"file '{p.as_posix()}'\n" for p in clips))
    joined=OUT/'edit/joined-no-spring-opening.mp4'
    P.ffmpeg(['-f','concat','-safe',0,'-i',listing,'-c','copy',joined], 'concat-no-opening.log')
    total=sum(row[3] for row in selected)
    destination=OUT/'Spyro 2 PC - Release Trailer (November 2026, revised).mp4'
    mix=(f'[1:a]aresample=48000,atrim=duration={total},asetpts=PTS-STARTPTS,volume=0.9,'
         f'afade=t=in:d=1.2,afade=t=out:st={total-3}:d=3[m];'
         '[0:a]volume=0.55[fx];[m][fx]amix=inputs=2:duration=first:normalize=0,alimiter=limit=0.95[a]')
    P.ffmpeg(['-i',joined,'-stream_loop',-1,'-i',MUSIC,'-filter_complex',mix,'-map','0:v','-map','[a]',
              '-c:v','copy','-c:a','aac','-b:a','256k','-movflags','+faststart',
              '-metadata','title=Spyro 2 PC - Release Trailer',destination], 'final-no-opening.log')
    decoded=subprocess.run(['ffmpeg','-v','error','-i',str(destination),'-f','null','NUL'],capture_output=True,text=True)
    assert decoded.returncode==0 and not decoded.stderr,decoded.stderr
    probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_format','-show_streams','-of','json',str(destination)]))
    duration=float(probe['format']['duration']);assert abs(duration-total)<.15
    archive=OUT/'archive';archive.mkdir(exist_ok=True)
    archived=archive/'Spyro 2 PC - Release Trailer (November 2026, original opening).mp4'
    if FINAL.exists() and not archived.exists():shutil.move(str(FINAL),str(archived))
    shutil.copy2(destination,FINAL)
    public=Path('D:/Theia-Promotional-Videos/05 - Spyro 2 PC - Release Trailer.mp4')
    public.parent.mkdir(exist_ok=True);shutil.copy2(destination,public)
    digest=hashlib.sha256(destination.read_bytes()).hexdigest()
    assert hashlib.sha256(public.read_bytes()).hexdigest()==digest
    receipt=dict(file=str(destination),current_project_file=str(FINAL),public_file=str(public),seconds=duration,
                 sha256=digest,removed=EDL[0],edl=selected,decode='PASS',
                 picture='Original finished cuts retained; opening Spring Realm flyover removed.',
                 audio='Continuous retail music remixed from the start with fresh opening and closing fades; game effects retained.')
    (OUT/'review/no-spring-opening.json').write_text(json.dumps(receipt,indent=2))
    public.with_suffix('.json').write_text(json.dumps(receipt,indent=2))
    public.with_suffix('.mp4.sha256').write_text(digest+'  '+public.name+'\n')
    print(json.dumps(receipt,indent=2),flush=True)


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'capture'
    {'capture': capture, 'sheets': sheets}.get(cmd, lambda a: None)(sys.argv[2:])
    if cmd == 'music':
        music()
    elif cmd == 'edit':
        edit()
    elif cmd == 'edit-no-opening':
        edit_no_opening()
