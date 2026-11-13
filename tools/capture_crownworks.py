"""Native workbench captures of The Crownworks (visual check).

Each view is a fixed S2SHOT camera; Blender coordinates (x, y, z-up) are
converted to engine (x, z, y). The fight runs as Play Cyborg Ripto.cmd
launches it (--night --backrealms). Output: previews/engine/<name>.png.

    python tools/capture_crownworks.py [view ...] [--tag NAME] [--frames N]
"""
import subprocess, sys
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
WORLD = ROOT / 'data/custom-levels/cyborg-ripto-arena/pc/crownworks.s2world'
OUT = ROOT / 'data/custom-levels/cyborg-ripto-arena/previews/engine'
SPAWN = (0, -36, 7.06)
VIEWS = {   # eye, target, spawn (Blender coordinates)
    'overview': ((0, -66, 42), (0, 0, 0), SPAWN),
    'overview-reverse': ((0, 66, 42), (0, 0, 0), SPAWN),
    'overview-east': ((66, 0, 42), (0, 0, 0), SPAWN),
    'lower-ring': ((0, -15, 3.2), (0, 12, 0.5), (0, -12, 0.06)),
    'crossing': ((15, -15, 4.5), (5, -5, 0), (12, -12, 3.6)),
    'terrace': ((-27, -15, 6.5), (-18, 12, 3.5), (-25, -10, 3.56)),
    'terrace-pool': ((22, 18, 7.5), (18, 10, 3.5), (21, 16, 3.56)),
    'upper-walk': ((0, -38, 9.5), (26, -24, 7), SPAWN),
    'from-walk': ((0, -37.5, 10.5), (0, 0, 1), SPAWN),
    'plan': ((0, -0.5, 95), (0, 0, 0), SPAWN),
    # The widened north/south dry crossings across their joins with the
    # island, the lava and the outer floor (overhead, oblique, grazing, reverse).
    'crossing-overhead': ((0.0, 13.5, 13.0), (0.0, 14.0, 0.0), (0, 9, 0.06)),
    'crossing-oblique': ((6.0, 8.5, 4.2), (0.0, 14.0, 0.0), (0, 9, 0.06)),
    'crossing-grazing': ((4.6, 10.2, 0.9), (-1.0, 16.5, 0.0), (0, 9, 0.06)),
    'crossing-reverse': ((-3.0, -18.2, 2.6), (0.5, -11.0, 0.0), (0, -9, 0.06)),
    'pylon': ((9.5, 6.0, 4.0), (12.3, 12.3, 3.5), (8.5, 8.5, 0.06)),
    # Machinery in the authored Crownworks steel (TILE_OVERRIDES 33/11).
    'steel-relay': ((3.0, -32.5, 9.6), (7.0, -37.5, 8.6), SPAWN),
    'steel-pedestal': ((-22.0, -4.5, 5.8), (-26.0, -8.0, 4.4), (-23, -6, 3.56)),
    'steel-service': ((10.0, 20.0, 6.2), (14.4, 24.9, 4.6), (11, 21, 3.56)),
}


def engine(p):
    return (p[0], p[2], p[1])


def main(args):
    tag, frames, names = '', 8, []
    i = 0
    while i < len(args):
        if args[i] == '--tag':
            tag = '-' + args[i + 1]; i += 2
        elif args[i] == '--frames':
            frames = int(args[i + 1]); i += 2
        else:
            names.append(args[i]); i += 1
    OUT.mkdir(parents=True, exist_ok=True)
    for name in names or VIEWS:
        eye, target, spawn = VIEWS[name]
        shot = OUT / f'{name}.s2shot'
        shot.write_text(f'S2SHOT 1\n1280 720 {frames}\n' + ' '.join(map(str, [*engine(spawn), 0, 0, 55, 1])) + '\n0\n0\n1\n0 '
                        + ' '.join(map(str, [*engine(eye), *engine(target)])) + '\n')
        bmp = OUT / f'{name}{tag}.bmp'
        with (OUT / f'{name}{tag}.log').open('w') as log:
            subprocess.run([str(ROOT / 'build/Release/spyro_workbench.exe'), '--game', str(ROOT / 'data/disc'),
                            '--custom-level', str(WORLD), '--night', '--backrealms', '--upgrade-tier', '2',
                            '--cinematic-shot', str(shot), '--capture', str(bmp)],
                           cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=300)
        Image.open(bmp).save(OUT / f'{name}{tag}.png')
        bmp.unlink()
        print(name, 'ok')


if __name__ == '__main__':
    main(sys.argv[1:])
