"""Run the workbench's --level-exit-check (hidden, bounded) for campaign levels and
collect the photos and results.

    python tools/run_level_exit_checks.py [--work DIR] [slot ...]     (default: every level with Return Home)
    python tools/run_level_exit_checks.py --sheets                    (contact sheets of the latest photos only)

Each run: talisman / completion speaker -> conversation -> reward -> Return Home ->
outro -> home world (src/desktop_level_exit_check.inl). BMPs are written to the work
folder (default: build/level-exit-checks); PNG copies, the JSON reports and summary.json
go to data/validation/level-exits-<date>/.
"""
import json, subprocess, sys, time
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
LEVELS = {17: 'Glimmer', 19: 'Idol Springs', 21: 'Colossus', 23: 'Hurricos', 25: 'Aquaria Towers', 27: 'Sunny Beach',
          35: 'Skelos Badlands', 37: 'Crystal Glacier', 39: 'Breeze Harbor', 41: 'Zephyr', 45: 'Scorch', 47: 'Shady Oasis',
          49: 'Magma Cone', 51: 'Fracture Hills', 59: 'Mystic Marsh', 61: 'Cloud Temples', 65: 'Robotica Farms', 67: 'Metropolis'}


def sheets(out):
    """Contact sheets: one row per level (end trigger, reward, exit, outro, home), six levels per sheet."""
    from PIL import ImageDraw
    summary = json.loads((out / 'summary.json').read_text())
    rows = [(k, v) for k, v in sorted(summary.items(), key=lambda kv: int(kv[0])) if v.get('photos')]
    names = ('0-end', '1-reward', '2-exit', '3-outro', '4-home')
    tw, th = 320, 265
    for n in range(0, len(rows), 6):
        group = rows[n:n + 6]
        sheet = Image.new('RGB', (tw * len(names), th * len(group)), (16, 16, 16))
        draw = ImageDraw.Draw(sheet)
        for r, (slot, v) in enumerate(group):
            stem = f'{slot}-{v["name"].lower().replace(" ", "-")}'
            for c, name in enumerate(names):
                f = out / f'{stem}-{name}.png'
                if f.exists():
                    sheet.paste(Image.open(f).convert('RGB').resize((tw, th)), (c * tw, r * th))
                draw.text((c * tw + 4, r * th + 4), f'{slot} {v["name"]} {name}', fill=(255, 255, 0))
        sheet.save(out / f'sheet-{n // 6 + 1}.png')
        print('sheet', out / f'sheet-{n // 6 + 1}.png')


def main(args):
    work = ROOT / 'build/level-exit-checks'
    if '--work' in args:
        i = args.index('--work'); work = Path(args[i + 1]); args = args[:i] + args[i + 2:]
    out = ROOT / f'data/validation/level-exits-{time.strftime("%Y%m%d")}'
    work.mkdir(parents=True, exist_ok=True); out.mkdir(parents=True, exist_ok=True)
    if '--sheets' in args:
        sheets(out); return
    summary_path = out / 'summary.json'
    summary = json.loads(summary_path.read_text()) if summary_path.exists() else {}
    for slot in [int(a) for a in args] or list(LEVELS):
        stem = f'{slot}-{LEVELS.get(slot, "level").lower().replace(" ", "-")}'
        capture = work / f'{stem}.bmp'
        log = out / f'{stem}.log'
        started = time.time()
        with log.open('w') as f:
            r = subprocess.run([str(ROOT / 'build/Release/spyro_workbench.exe'), '--game', str(ROOT / 'data/disc'),
                                '--level-exit-check', str(slot), '--smoke', '10000' if slot == 51 else '6000', '--capture', str(capture)],
                               cwd=ROOT, stdout=f, stderr=subprocess.STDOUT, timeout=600)
        result = {'name': LEVELS.get(slot), 'exit_code': r.returncode, 'seconds': round(time.time() - started, 1)}
        report = capture.with_suffix('.json')
        if r.returncode == 0 and report.exists():
            data = json.loads(report.read_text())
            result.update(data.get('level_exit', {}))
            (out / f'{stem}.json').write_text(json.dumps(data, indent=1))
        else:
            result['error'] = log.read_text(errors='replace').strip()[-400:]
        for name in ('0-end', '1-reward', '2-exit', '3-outro', '4-home'):
            bmp = work / f'{stem}-{name}.bmp'
            if bmp.exists() and r.returncode == 0:
                Image.open(bmp).save(out / f'{stem}-{name}.png')
                result.setdefault('photos', []).append(f'{stem}-{name}.png')
        summary[str(slot)] = result
        summary_path.write_text(json.dumps(summary, indent=1))
        print(slot, LEVELS.get(slot), 'OK' if r.returncode == 0 else 'FAIL', result.get('error', '')[-200:], flush=True)


if __name__ == '__main__':
    main(sys.argv[1:])
