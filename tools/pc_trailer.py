"""Spyro 2 PC trailer (October 2026): author, capture, music and edit.

Native 1080p60 workbench footage only, cut to the July 1999 prototype's own
soundtrack, with titles set in the retail Spyro 2 font.

  python tools/pc_trailer.py author     write the shot list and S2SHOT files
  python tools/pc_trailer.py capture [names...]
  python tools/pc_trailer.py music      decode the July build's level music
  python tools/pc_trailer.py edit       titles, cut and mix
"""
from pathlib import Path
import json, subprocess, sys, time, math
from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'output/spyro2-pc-trailer'
for folder in ('source', 'gameplay', 'review', 'titles', 'edit'):
    (OUT / folder).mkdir(parents=True, exist_ok=True)
EXE = ROOT / 'build/Release/spyro_workbench.exe'
SHOTS = OUT / 'source/shots.json'

BASE = ['--headless', '--capture-size', '1920', '1080', '--capture-music-volume', '0']
GLIMMER_SPOT = ['--game', 'data/disc', '--slot', '17', '--interaction-record', '0']
# The Furies need room on every side: the castle courtyard (spyro_open_spots data/disc 17).
FURY_SPOT = ['--game', 'data/disc', '--slot', '17', '--interaction-record', '12']
ELEMENTS = ['fire', 'water', 'ice', 'electricity', 'convexity']


def s2shot(name, frames, spawn, yaw, keys, route=(), actions=(), mode=1, fov=55):
    """Version-1 S2SHOT: keys are (seconds, eye, target) in engine Y-up units."""
    lines = ['S2SHOT 1', f'1920 1080 {frames}', ' '.join(map(str, (*spawn, yaw, 1, fov, mode))), str(len(route))]
    lines += [' '.join(map(str, p)) for p in route]
    lines += [str(len(actions))] + [' '.join(map(str, a)) for a in actions]
    lines += [str(len(keys))] + [' '.join(map(str, (round(t * 60), *e, *p))) for t, e, p in keys]
    path = OUT / 'source' / f'{name}.s2shot'
    path.write_text('\n'.join(lines) + '\n')
    return path.relative_to(ROOT).as_posix()


def blender(p):
    """Spring Realm authoring coordinates (x, y, z-up) to engine (x, z, y)."""
    return (p[0], p[2], p[1])


def author():
    shots = []
    add = lambda name, args: shots.append(dict(name=name, args=[*args, *BASE]))
    for e in ELEMENTS:
        add(f'breath-{e}', [*GLIMMER_SPOT, '--frames', '300', '--element-replay', 'breath', '--element', e, '--camera-orbit', '60'])
        # Furies film themselves: the gear-up cuts between three angles.
        add(f'fury-{e}', [*FURY_SPOT, '--frames', '600', '--element-replay', 'fury', '--element', e])
    # Every projectile: the five elemental shots, then the retail powers.
    for e, orbit in (('fire', 40), ('water', 50), ('ice', 35), ('electricity', 60), ('convexity', 45)):
        add(f'shot-{e}', [*GLIMMER_SPOT, '--frames', '300', '--element-replay', 'shot', '--element', e, '--camera-orbit', str(orbit)])
    add('superflame', ['--game', 'data/disc', '--slot', '65', '--frames', '240', '--superflame', '--interaction-record', '52', '--attack', 'flame', '--camera-orbit', '35'])
    add('superflame-underwater', ['--game', 'data/disc', '--slot', '15', '--frames', '480', '--swim-replay', '--superflame'])
    add('green-bomb', ['--game', 'data/disc', '--slot', '65', '--frames', '240', '--green-firebomb', '--interaction-record', '52', '--attack', 'flame', '--camera-orbit', '35'])
    add('headbash-water', [*GLIMMER_SPOT, '--frames', '280', '--element-replay', 'headbash', '--element', 'water', '--camera-height', '2.5', '--camera-orbit', '30'])
    add('headbash-ice', [*GLIMMER_SPOT, '--frames', '280', '--element-replay', 'headbash', '--element', 'ice', '--camera-height', '2.5', '--camera-orbit', '30'])
    add('slice-convexity', [*GLIMMER_SPOT, '--frames', '420', '--element-replay', 'slice', '--camera-distance', '6', '--camera-height', '2', '--camera-orbit', '120'])
    add('dark-run', [*GLIMMER_SPOT, '--frames', '300', '--element-replay', 'dark-run', '--dark-spyro'])
    add('demons', [*GLIMMER_SPOT, '--frames', '420', '--element-replay', 'demons', '--camera-distance', '6', '--camera-height', '2.5', '--camera-orbit', '120'])
    # Spirits: a club lizard 7.5 units from Glimmer's superfly gate.
    add('spirit-spark', ['--game', 'data/disc', '--slot', '17', '--play', '--interaction-record', '176', '--attack', 'flame', '--frames', '330'])
    gate = (83.52, 10.6, 96.63)
    add('gate-counter', ['--game', 'data/disc', '--slot', '17', '--cinematic-shot',
        s2shot('gate-counter', 200, (83.5, 10.5, 92.5), 0, [(0, (85.2, 11.0, 90.6), gate), (3.2, (84.1, 11.2, 93.4), gate)])])
    add('gate-open', ['--game', 'data/disc', '--slot', '17', '--open-gates', '--cinematic-shot',
        s2shot('gate-open', 170, (84.67, 10.0, 93.86), -0.39, [(0, (86.6, 12.0, 89.4), gate), (2.7, (85.6, 11.6, 90.6), gate)],
               route=[(84.67, 10.0, 93.86), (82.37, 10.0, 99.40)])])
    # Superfly from the courtyard along its longest clear heading (+z, about 28 units):
    # jump, take off with a second press, flap; a side-behind camera tracks him
    # so the thin retail wing-tip trails read.
    add('superfly', ['--game', 'data/disc', '--slot', '17', '--superfly', '--cinematic-shot',
        s2shot('superfly', 330, (69.98, 9.05, 101.26), 0,
               [(0, (-2.2, 1.1, -2.6), (0, 0.45, 0.8)), (2.5, (-1.9, 0.8, -1.9), (0, 0.4, 0.9)), (5.5, (-2.3, 0.5, -1.0), (0, 0.4, 1.1))],
               actions=[(12, 13, 0, 1, 0), (30, 31, 0, 1, 0), (110, 111, 0, 1, 0), (190, 191, 0, 1, 0)], mode=2)])
    # Cloud Temples: a green fire bomb blasts a golden dog door open (record 51).
    door = (62.93, 23.0, 35.14)
    add('cloud-door', ['--game', 'data/disc', '--slot', '61', '--green-firebomb', '--cinematic-shot',
        s2shot('cloud-door', 300, (59.07, 22.6, 38.31), 2.258, [(0, (57.6, 24.4, 41.6), door), (4.5, (58.3, 24.0, 40.4), door)],
               actions=[(40, 41, 0, 0, 1), (130, 131, 0, 0, 1)])])
    # Boss fights: flame Crush and Gulp; their original health HUD shows.
    add('crush-fight', ['--game', 'data/disc', '--slot', '31', '--play', '--interaction-record', '0', '--attack', 'flame', '--frames', '1500'])
    add('gulp-fight', ['--game', 'data/disc', '--slot', '55', '--play', '--interaction-record', '0', '--attack', 'flame', '--frames', '720'])
    # Spring Realm: a sweep from Hamlet Green over the courtyard to the castle.
    spring = ['--game', 'data/disc', '--custom-level', 'data/custom-levels/spring-realm/pc/spring-realm.s2world']
    keys = [(0, blender((150, -6, 44)), blender((131, 34, 18))), (4, blender((104, 6, 34)), blender((70, 36, 13))),
            (8, blender((70, 20, 26)), blender((58.5, 84, 14))), (11, blender((58.5, 58, 19)), blender((58.5, 100, 14)))]
    add('spring-flyover', [*spring, '--cinematic-shot', s2shot('spring-flyover', 700, blender((57.5, 20.0, 10.9)), 0, keys)])
    keys = [(0, blender((58.5, 124, 23)), blender((58.5, 86, 14))), (5, blender((58.5, 112, 18)), blender((58.5, 86, 13)))]
    add('spring-castle', [*spring, '--cinematic-shot', s2shot('spring-castle', 330, blender((58.5, 100.0, 11.1)), 0, keys)])
    keys = [(0, blender((60.0, 8.0, 18.0)), blender((49.0, 43.0, 18.5))), (5, blender((51.0, 35.5, 11.8)), blender((46.5, 43.0, 17.0)))]
    add('spring-tree', [*spring, '--cinematic-shot', s2shot('spring-tree', 330, blender((57.5, 20.0, 10.9)), 0, keys)])
    # Crownworks: Cyborg Ripto's arena, with the real boss stages.
    crown = ['--game', 'data/disc', '--custom-level', 'data/custom-levels/cyborg-ripto-arena/pc/crownworks.s2world', '--play', '--night', '--backrealms', '--upgrade-tier', '2']
    add('crownworks-foot', [*crown, '--replay', '--hide-hud', '--frames', '480'])
    add('crownworks-mechs', [*crown, '--capture-boss-stage', 'mecha-gulp', '--replay', '--hide-hud', '--frames', '480'])
    # Prototype builds, running from their own discs.
    add('proto-july', ['--game', 'data/builds/july-1999/source-disc/SPYRO2EXE.cue', '--play', '--replay', '--frames', '480'])
    add('proto-august', ['--game', 'data/builds/august-1999/source-disc/PSX - Spyro 2 8-17 Previewable.cue', '--play', '--replay', '--frames', '480'])
    SHOTS.write_text(json.dumps(shots, indent=2))
    print('Authored', len(shots), 'shots')


def capture(names):
    shots = json.loads(SHOTS.read_text())
    for shot in shots:
        name = shot['name']
        if names and name not in names:
            continue
        video = OUT / 'gameplay' / f'{name}.mp4'
        still = OUT / 'review' / f'{name}.bmp'
        cmd = [str(EXE), *shot['args'], '--video', video.relative_to(ROOT).as_posix(), '--capture', still.relative_to(ROOT).as_posix()]
        start = time.monotonic()
        with (OUT / 'review' / f'{name}.log').open('w') as log:
            result = subprocess.run(cmd, cwd=ROOT, stdout=log, stderr=log, timeout=900, creationflags=subprocess.CREATE_NO_WINDOW)
        ok = result.returncode == 0 and video.exists()
        error = (ROOT / 'data/validation/workbench-error.txt')
        why = error.read_text().strip() if not ok and error.exists() else ''
        print(('ok  ' if ok else 'FAIL'), name, round(time.monotonic() - start, 1), 's', why, flush=True)


def music():
    """The July 1999 prototype's own level music (Glimmer), decoded from its disc."""
    probe = ROOT / 'build/Release/spyro_audio_probe.exe'
    disc = 'data/builds/july-1999/source-disc/SPYRO2EXE.cue'
    for slot, name in ((17, 'july-glimmer'), (15, 'july-summer-forest')):
        dest = OUT / 'source' / f'{name}.wav'
        subprocess.run([str(probe), disc, '--slot', str(slot), '--wave', dest.relative_to(ROOT).as_posix()], cwd=ROOT, check=True,
                       creationflags=subprocess.CREATE_NO_WINDOW)
        print('Decoded', dest.name, dest.stat().st_size, 'bytes')


# ---- Retail Spyro 2 font: the executable's glyph table over Glimmer's VRAM.
class RetailFont:
    def __init__(self):
        import struct
        sys.path.insert(0, str(ROOT / 'tools'))
        from spyrodisc import wad_index
        self.struct = struct
        self.exe = (ROOT / 'data/disc/SCUS_944.25').read_bytes()
        with (ROOT / 'data/disc/WAD.WAD').open('rb') as f:
            entries = wad_index(f.read(0x10000), f.seek(0, 2)); e = entries[17]; f.seek(e.offset); archive = f.read(e.size)
        child = wad_index(archive, len(archive))[0]; self.vram = archive[child.offset:child.offset + 0x80000]
        self.cache = {}

    @staticmethod
    def off(a): return a - 0x8000f800

    def word(self, x, y): return self.struct.unpack_from('<H', self.vram, (y * 512 + x - 512) * 2)[0]

    def texture(self, page, u, v, clut, w, h, tint=0x808080):
        im = Image.new('RGBA', (w, h)); depth = (page >> 7) & 3; per = 2 if depth else 4; bits = 8 if depth else 4
        for y in range(h):
            for x in range(w):
                xx = (u + x) & 255; yy = (v + y) & 255; p = self.word((page & 15) * 64 + xx // per, ((page >> 4) & 1) * 256 + yy)
                c = self.word((clut & 63) * 16 + (p >> ((xx % per) * bits) & ((1 << bits) - 1)), clut >> 6)
                rgb = [min(255, ((c >> (k * 5) & 31) * 255 // 31) * (tint >> (k * 8) & 255) // 128) for k in range(3)]
                im.putpixel((x, y), (*rgb, 255 if c else 0))
        return im

    def glyph(self, c, color):
        key = (c, color)
        if key not in self.cache:
            d = self.struct.unpack_from('<H', self.exe, self.off(0x800636a4) + ord(c) * 2)[0]; index = d & 255
            if index == 255:
                self.cache[key] = (None, 10, 0)
            else:
                tint = self.struct.unpack_from('<I', self.exe, self.off(0x800637d4) + color * 4)[0]
                im = self.texture(0x618, (index & 31) * 12, index // 32 * 9, 0x4922, 12, 9, tint)
                dy = (d >> 12) & 15; dy = dy - 16 if dy >= 8 else dy
                self.cache[key] = (im, (d >> 8) & 15, dy)
        return self.cache[key]

    def text(self, text, color=1, scale=5):
        ps = [self.glyph(c, color) for c in text]
        im = Image.new('RGBA', (sum(a for g, a, d in ps) + 12, 24)); x = 0
        for g, a, d in ps:
            if g is not None: im.alpha_composite(g, (x, d + 8))
            x += a
        bbox = im.getbbox(); im = im.crop(bbox) if bbox else im
        # PS1 horizontal aspect (512-wide display): 0.625 per original pixel.
        return im.resize((round(im.width * scale * .625), round(im.height * scale)), Image.Resampling.NEAREST)


def caption_png(font, path, lines, y, color=1, scale=5, shade=True):
    """A transparent 1920x1080 overlay with centred retail-font lines."""
    card = Image.new('RGBA', (1920, 1080))
    for k, (text, size) in enumerate(lines):
        im = font.text(text, color, size)
        x = (1920 - im.width) // 2; yy = y + k * round(size * 13)
        if shade:
            shadow = Image.new('RGBA', im.size, (0, 0, 0, 0)); shadow.putalpha(im.getchannel('A').point(lambda a: min(255, a)))
            black = Image.new('RGBA', im.size, (0, 0, 0, 200)); black.putalpha(im.getchannel('A').point(lambda a: a * 200 // 255))
            card.alpha_composite(black.filter(ImageFilter.GaussianBlur(3)), (x + 5, yy + 6))
        card.alpha_composite(im, (x, yy))
    card.save(path)


# name, source clip (gameplay/<name>.mp4, or 'card'/'logo'/'still:<file>'), in-point, duration, caption lines
EDL = [
    ('open', 'spring-flyover', 0.3, 6.5, [('SPRING HAS COME TO AVALAR', 4)]),
    ('logo', 'logo', 0, 4.5, []),
    ('ch-breath', 'card', 0, 1.6, [('ELEMENTAL BREATH', 6)]),
    ('b-fire', 'breath-fire', 0.9, 2.4, [('FIRE', 4)]),
    ('b-water', 'breath-water', 0.9, 2.4, [('WATER', 4)]),
    ('b-ice', 'breath-ice', 0.9, 2.4, [('ICE', 4)]),
    ('b-elec', 'breath-electricity', 0.9, 2.4, [('ELECTRICITY', 4)]),
    ('b-convex', 'breath-convexity', 0.9, 2.4, [('CONVEXITY', 4)]),
    ('ch-shots', 'card', 0, 1.6, [('PROJECTILES', 6)]),
    ('s-fire', 'shot-fire', 0.5, 2.0, [('EMBER', 4)]),
    ('s-water', 'shot-water', 0.5, 2.2, [('BUBBLE BURST', 4)]),
    ('s-ice', 'shot-ice', 0.5, 2.0, [('ICE SHARDS', 4)]),
    ('s-elec', 'shot-electricity', 0.5, 2.6, [('SPARK ORB', 4)]),
    ('s-convex', 'shot-convexity', 0.5, 2.4, [('RIFT ORB', 4)]),
    ('s-super', 'superflame', 0.4, 2.6, [('SUPERFLAME', 4)]),
    ('s-under', 'superflame-underwater', 3.0, 2.6, [('EVEN UNDERWATER', 3)]),
    ('s-bomb', 'green-bomb', 0.4, 2.6, [('GREEN FIRE BOMB', 4)]),
    ('pocket', 'headbash-water', 0.6, 2.8, [('SWIM IN YOUR OWN WAVES', 3)]),
    ('slice', 'slice-convexity', 0.8, 3.6, [('CUT THE WORLD IN TWO', 3)]),
    ('ch-fury', 'card', 0, 1.6, [('FURY', 7)]),
    ('f-fire', 'fury-fire', 0.6, 5.2, [('INFERNO', 4)]),
    ('f-water', 'fury-water', 0.6, 6.0, [('DELUGE', 4)]),
    ('f-ice', 'fury-ice', 0.6, 5.2, [('GLACIER', 4)]),
    ('f-elec', 'fury-electricity', 0.6, 5.2, [('STORM', 4)]),
    ('f-convex', 'fury-convexity', 0.6, 5.2, [('RIFT', 4)]),
    ('ch-dark', 'card', 0, 1.6, [('DARK SPYRO', 6)]),
    ('dark', 'dark-run', 0.8, 3.0, []),
    ('demons', 'demons', 1.0, 3.6, [('THE CONVEXITY ANSWERS', 3)]),
    ('ch-world', 'card', 0, 1.6, [('AVALAR, RESTORED', 6)]),
    ('spark', 'spirit-spark', 2.6, 2.8, [('SPIRITS FLY HOME', 3)]),
    ('counter', 'gate-counter', 0.4, 2.4, []),
    ('gate', 'gate-open', 0.3, 2.4, [('POWER GATES REAWAKENED', 3)]),
    ('fly', 'superfly', 1.0, 3.4, [('SUPERFLY', 4)]),
    ('door', 'cloud-door', 1.0, 3.0, [('GREEN FIRE BOMBS BREAK THE GOLDEN DOORS', 3)]),
    ('crush', 'crush-fight', 2.0, 4.2, [('CRUSH', 4)]),
    ('gulp', 'gulp-fight', 2.0, 4.2, [('GULP', 4)]),
    ('ch-spring', 'card', 0, 1.6, [('THE SPRING REALM', 6)]),
    ('tree', 'spring-tree', 0.3, 3.4, []),
    ('castle', 'spring-castle', 0.3, 4.0, [('A CASTLE TO EXPLORE', 3)]),
    ('crown-a', 'crownworks-foot', 1.0, 3.0, [('CYBORG RIPTO AWAITS', 3)]),
    ('crown-b', 'crownworks-mechs', 1.0, 3.0, []),
    ('ch-archive', 'card', 0, 1.6, [('FROM THE ARCHIVES', 6)]),
    ('july', 'proto-july', 1.0, 3.2, [('JULY 1999 PROTOTYPE', 3)]),
    ('august', 'proto-august', 1.0, 3.2, [('AUGUST 1999 PROTOTYPE', 3)]),
    ('ch-beyond', 'card', 0, 1.6, [('AND BEYOND...', 6)]),
    ('yotd', 'still:spyro3-sunny.png', 0, 4.0, [('YEAR OF THE DRAGON', 4), ('EARLY PC PREVIEW', 2)]),
    ('end', 'logo-end', 0, 6.0, [('COMING SOON', 4)]),
]
TITLE_MUSIC = OUT / 'source/july-glimmer.wav'
FINAL = OUT / 'Spyro 2 PC - Trailer (October 2026).mp4'


def ffmpeg(args, log):
    with (OUT / 'edit' / log).open('w') as f:
        subprocess.run(['ffmpeg', '-hide_banner', '-nostdin', '-y', '-loglevel', 'warning', *map(str, args)], stdout=f, stderr=f,
                       creationflags=subprocess.CREATE_NO_WINDOW, check=True)


def graphics(font):
    logo = Image.open(OUT / 'logo/spyro-2-pc-logo.png').convert('RGBA'); logo = logo.crop(logo.getbbox())
    for name, y, width, end in (('logo', 190, 1300, False), ('logo-end', 150, 1100, True)):
        frames = OUT / 'edit' / f'{name}-frames'; frames.mkdir(exist_ok=True)
        seconds = 4.5 if not end else 6.0
        for i in range(round(seconds * 60)):
            t = i / 60
            ease = lambda v: 0 if v <= 0 else 1 if v >= 1 else v * v * (3 - 2 * v)
            card = Image.new('RGBA', (1920, 1080), (8, 5, 18, 255))
            # A soft violet glow behind the logo, blurred at quarter size.
            glow = Image.new('RGBA', (480, 270)); d = ImageDraw.Draw(glow)
            r = 130 + 10 * math.sin(t * 1.3); d.ellipse((240 - r, 135 - r * .62, 240 + r, 135 + r * .62), fill=(70, 40, 120, 90))
            card.alpha_composite(glow.filter(ImageFilter.GaussianBlur(30)).resize((1920, 1080), Image.Resampling.BILINEAR))
            w = round(width * (1 + .04 * t / seconds)); im = logo.resize((w, round(logo.height * w / logo.width)), Image.Resampling.LANCZOS)
            a = ease(t / .8) * (1 - ease((t - seconds + .5) / .5))
            im.putalpha(im.getchannel('A').point(lambda v: round(v * a)))
            card.alpha_composite(im, ((1920 - im.width) // 2, y + (0 if not end else -10)))
            card.convert('RGB').save(frames / f'{i:04d}.png')
        ffmpeg(['-framerate', 60, '-i', frames / '%04d.png', '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-crf', 16, OUT / 'edit' / f'{name}.mp4'], f'{name}.log')
    for name, source, start, duration, lines in EDL:
        caption_png(font, OUT / 'titles' / f'{name}.png', lines,
                    480 if source == 'card' else 860 if source not in ('logo-end', 'still:spyro3-sunny.png') else 780 if source == 'logo-end' else 820,
                    color=1 if source == 'card' else 0)
    print('Rendered logo cards and retail-font titles', flush=True)


def edit():
    font = RetailFont(); graphics(font)
    segments = []
    for k, (name, source, start, duration, lines) in enumerate(EDL):
        dest = OUT / 'edit' / f'{k:02d}-{name}.mp4'; title = OUT / 'titles' / f'{name}.png'
        fade = f"fade=t=in:st=0:d=0.25:alpha=1,fade=t=out:st={duration - .35}:d=0.3:alpha=1"
        if source == 'card':
            inputs = ['-f', 'lavfi', '-i', f'color=c=0x080512:s=1920x1080:r=60:d={duration}', '-f', 'lavfi', '-i', f'anullsrc=r=48000:cl=stereo:d={duration}']
            vf = f"[0:v][2:v]overlay=0:0:format=auto[v]"
            ffmpeg([*inputs, '-loop', 1, '-t', duration, '-i', title, '-filter_complex', f"[2:v]format=rgba,{fade}[t];[0:v][t]overlay=0:0[v]",
                    '-map', '[v]', '-map', '1:a', '-t', duration, '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-crf', 16, '-c:a', 'aac', '-ar', 48000, dest], f'{name}.log')
        elif source.startswith('logo'):
            ffmpeg(['-i', OUT / 'edit' / f'{source}.mp4', '-f', 'lavfi', '-i', f'anullsrc=r=48000:cl=stereo:d={duration}', '-loop', 1, '-t', duration, '-i', title,
                    '-filter_complex', f"[2:v]format=rgba,{fade}[t];[0:v][t]overlay=0:0[v]", '-map', '[v]', '-map', '1:a', '-t', duration,
                    '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-crf', 16, '-c:a', 'aac', '-ar', 48000, dest], f'{name}.log')
        elif source.startswith('still:'):
            still = OUT / 'source' / source.split(':', 1)[1]
            ffmpeg(['-loop', 1, '-framerate', 60, '-t', duration, '-i', still, '-f', 'lavfi', '-i', f'anullsrc=r=48000:cl=stereo:d={duration}', '-loop', 1, '-t', duration, '-i', title,
                    '-filter_complex', f"[0:v]scale=2112:1188,zoompan=z='1+0.0006*on':x='iw/2-(iw/zoom/2)':y='ih/2-(ih/zoom/2)':d=1:s=1920x1080:fps=60[s];[2:v]format=rgba,{fade}[t];[s][t]overlay=0:0[v]",
                    '-map', '[v]', '-map', '1:a', '-t', duration, '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-crf', 16, '-c:a', 'aac', '-ar', 48000, dest], f'{name}.log')
        else:
            clip = OUT / 'gameplay' / f'{source}.mp4'
            ffmpeg(['-ss', start, '-t', duration, '-i', clip, '-loop', 1, '-t', duration, '-i', title,
                    '-filter_complex', f"[0:v]scale=1920:1080,fps=60,setsar=1[g];[1:v]format=rgba,{fade}[t];[g][t]overlay=0:0[v];[0:a]aresample=48000,apad[a]",
                    '-map', '[v]', '-map', '[a]', '-t', duration, '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-crf', 16, '-c:a', 'aac', '-ar', 48000, dest], f'{name}.log')
        segments.append(dest); print('Cut', dest.name, flush=True)
    listing = OUT / 'edit/segments.txt'; listing.write_text(''.join(f"file '{s.as_posix()}'\n" for s in segments))
    joined = OUT / 'edit/joined.mp4'
    ffmpeg(['-f', 'concat', '-safe', 0, '-i', listing, '-c', 'copy', joined], 'concat.log')
    total = sum(row[3] for row in EDL)
    mix = (f"[1:a]aresample=48000,atrim=duration={total},asetpts=PTS-STARTPTS,volume=0.9,afade=t=in:d=1.2,afade=t=out:st={total - 3}:d=3[m];"
           f"[0:a]volume=0.55[fx];[m][fx]amix=inputs=2:duration=first:normalize=0,alimiter=limit=0.95[a]")
    ffmpeg(['-i', joined, '-stream_loop', -1, '-i', TITLE_MUSIC, '-filter_complex', mix, '-map', '0:v', '-map', '[a]', '-c:v', 'copy', '-c:a', 'aac', '-b:a', '256k',
            '-metadata', 'title=Spyro 2 PC - Trailer', '-metadata', 'comment=Native gameplay; July 1999 prototype music; fan-made work in progress', FINAL], 'final.log')
    print('Wrote', FINAL, round(total, 1), 's', flush=True)


if __name__ == '__main__':
    command = sys.argv[1] if len(sys.argv) > 1 else 'author'
    if command == 'author':
        author()
    elif command == 'capture':
        capture(sys.argv[2:])
    elif command == 'music':
        music()
    elif command == 'edit':
        edit()
