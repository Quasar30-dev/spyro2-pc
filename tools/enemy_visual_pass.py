"""Visual engagement pass: one enemy of every class in every campaign level.

For each (slot, class) the first spirit-carrying record of that class is
filmed twice on the real runtime, Spyro parked beside it:
  none  - Spyro idles: does the enemy notice, approach or hurt him?
  flame - Spyro breathes fire: does it react and die?
Each clip gets an 8-frame contact sheet; a summary line per clip goes to
summary.txt (workbench JSON: script hits/state, Spyro health, spirits).

  python tools/enemy_visual_pass.py [out-dir] [slot ...]
"""
from pathlib import Path
import json, re, subprocess, sys

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'build/Release/spyro_workbench.exe'
SPOTS = ROOT / 'build/Release/spyro_open_spots.exe'
SLOTS = [17, 19, 21, 23, 25, 27, 35, 37, 39, 41, 45, 47, 49, 51, 59, 61, 65, 67]


def enemies(slot):
    out = subprocess.run([str(SPOTS), 'data/disc', str(slot), '--enemies'], cwd=ROOT, capture_output=True, text=True, check=True).stdout
    first = {}
    for line in out.splitlines():
        parts = line.split()
        record, cls = int(parts[1]), parts[3]
        first.setdefault(cls, record)
    return first


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'data/validation/allpass-20261014/enemies'
    slots = [int(s) for s in sys.argv[2:]] or SLOTS
    out.mkdir(parents=True, exist_ok=True)
    summary = (out / 'summary.txt').open('a')
    for slot in slots:
        for cls, record in enemies(slot).items():
            for mode in ('none', 'flame'):
                name = f'{slot}-{cls}-{record}-{mode}'
                video, still = out / f'{name}.mp4', out / f'{name}.bmp'
                cmd = [str(EXE), '--game', 'data/disc', '--slot', str(slot), '--interaction-record', str(record), '--attack', mode,
                       '--smoke', '360', '--capture-size', '640', '360', '--capture-music-volume', '0', '--video', str(video), '--capture', str(still)]
                result = subprocess.run(cmd, cwd=ROOT, capture_output=True, timeout=900, creationflags=subprocess.CREATE_NO_WINDOW)
                fields = {}
                report = still.with_suffix('.json')
                if result.returncode == 0 and report.exists():
                    text = report.read_text()
                    for key in ('retail_script_hits', 'retail_script_target_state', 'health', 'spirits', 'retail_script_faults', 'mapped_enemies'):
                        m = re.search(rf'"{key}": (-?\d+)', text)
                        if m:
                            fields[key] = int(m.group(1))
                    subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-i', str(video), '-vf',
                                    "select='not(mod(n\\,45))',scale=320:-1,tile=4x2", '-frames:v', '1', str(out / f'{name}-sheet.png')],
                                   creationflags=subprocess.CREATE_NO_WINDOW)
                line = f'{name} exit {result.returncode} ' + ' '.join(f'{k}={v}' for k, v in fields.items())
                print(line, flush=True)
                summary.write(line + '\n')
                summary.flush()


if __name__ == '__main__':
    main()
