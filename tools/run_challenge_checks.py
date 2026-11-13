"""Run the workbench's --challenge-check (hidden, bounded) for orb challenges and
collect the photos and results.

    python tools/run_challenge_checks.py [--work DIR] [name ...]   (default: every challenge in spyro_challenges list)

Each run: the challenge's autopilot (tools/challenge_pilot.hpp) talks to the giver,
plays the challenge with pad-equivalent inputs and collects the orb in the real
workbench (src/desktop_challenge_check.inl). BMPs are written to the work folder
(default: build/challenge-checks); PNG copies, a contact sheet per challenge, the
JSON reports, the logs and summary.json go to data/validation/challenges-<date>/.
"""
import json, subprocess, sys, time
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
FRAMES = {'colossus-hockey-goalie': 30000, 'colossus-hockey-1v1': 40000, 'metropolis-saucers-2': 16000, 'mystic-marsh-thieves': 40000, 'crystal-glacier-draclets': 24000, 'magma-cone-party-crashers': 40000}   # 40000: the workbench's cap for a challenge check


def names():
    out = subprocess.run([str(ROOT / 'build/Release/spyro_challenges.exe'), str(ROOT / 'data/disc'), 'list'],
                         cwd=ROOT, capture_output=True, text=True, timeout=120).stdout
    return [line.split()[0] for line in out.splitlines() if line.strip()]


def sheet(out, name, photos):
    if not photos:
        return None
    tw, th = 320, 265; cols = min(4, len(photos)); rows = (len(photos) + cols - 1) // cols
    image = Image.new('RGB', (tw * cols, th * rows), (16, 16, 16)); draw = ImageDraw.Draw(image)
    for k, p in enumerate(photos):
        x, y = (k % cols) * tw, (k // cols) * th
        image.paste(Image.open(out / p).convert('RGB').resize((tw, th)), (x, y))
        draw.text((x + 4, y + 4), p.replace(name + '-', ''), fill=(255, 255, 0))
    path = out / f'{name}-sheet.png'; image.save(path); return path.name


def main(args):
    work = ROOT / 'build/challenge-checks'
    if '--work' in args:
        i = args.index('--work'); work = Path(args[i + 1]); args = args[:i] + args[i + 2:]
    out = ROOT / f'data/validation/challenges-{time.strftime("%Y%m%d")}'
    work.mkdir(parents=True, exist_ok=True); out.mkdir(parents=True, exist_ok=True)
    summary_path = out / 'summary.json'
    summary = json.loads(summary_path.read_text()) if summary_path.exists() else {}
    for name in args or names():
        capture = work / f'{name}.bmp'
        log = out / f'{name}.log'
        started = time.time()
        with log.open('w') as f:
            r = subprocess.run([str(ROOT / 'build/Release/spyro_workbench.exe'), '--game', str(ROOT / 'data/disc'),
                                '--challenge-check', name, '--smoke', str(FRAMES.get(name, 12000)), '--capture', str(capture)],
                               cwd=ROOT, stdout=f, stderr=subprocess.STDOUT, timeout=1500)
        result = {'exit_code': r.returncode, 'seconds': round(time.time() - started, 1)}
        report = capture.with_suffix('.json')
        if r.returncode == 0 and report.exists():
            data = json.loads(report.read_text())
            result.update(data.get('challenge', {}))
            (out / f'{name}.json').write_text(json.dumps(data, indent=1))
        else:
            result['error'] = log.read_text(errors='replace').strip()[-400:]
        photos = []
        # Only this run's photos (older ones from earlier runs are left alone).
        for bmp in sorted([b for b in work.glob(f'{name}-*.bmp') if b.stat().st_mtime>=started-1], key=lambda p: int(p.stem[len(name) + 1:].split('-')[0]) if p.stem[len(name) + 1:].split('-')[0].isdigit() else 99):
            png = out / (bmp.stem + '.png'); Image.open(bmp).save(png); photos.append(png.name)
        result['photos'] = photos
        result['sheet'] = sheet(out, name, photos)
        summary[name] = result
        summary_path.write_text(json.dumps(summary, indent=1))
        print(name, 'OK' if r.returncode == 0 and result.get('awarded') else 'FAIL', result.get('error', '')[-300:], flush=True)


if __name__ == '__main__':
    main(sys.argv[1:])
