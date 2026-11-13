"""Native workbench captures of the exported Spring Realm (visual check).

Each view is a fixed S2SHOT camera; Blender coordinates (x, y, z-up) are
converted to engine (x, z, y). Output: previews/engine/<name>.png.
"""
import subprocess, sys
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
WORLD = ROOT / 'data/custom-levels/spring-realm/pc/spring-realm.s2world'
OUT = ROOT / 'data/custom-levels/spring-realm/previews/engine'
VIEWS = {
    'overview': ((58, -40, 70), (58, 70, 10), (60.9, 50.0, 10.9)),
    'courtyard': ((57.5, -12, 18), (58.5, 40, 11), (57.5, 20.0, 10.9)),
    'castle-approach': ((58.5, 50, 15), (58.5, 75, 13), (58.5, 60.0, 10.3)),
    'bailey': ((58.5, 121, 22), (58.5, 86, 14), (58.5, 100.0, 11.1)),
    'west-hall': ((52.0, 103.2, 13.2), (44.5, 114, 12.5), (47.0, 108.0, 11.1)),
    'walkway': ((36.0, 104, 18.6), (40, 80, 17), (35.4, 98.0, 16.9)),
    'terrace': ((37.5, 101, 20.5), (47, 110, 16.5), (47.0, 112.0, 16.9)),
    'orrery-hall': ((57.0, 81.4, 13.0), (61.5, 85.5, 12.6), (58.5, 84.0, 11.2)),
    'lake-well': ((57.5, -0.5, 15.5), (57.5, 15.0, 6.0), (57.5, 30.0, 10.4)),
    'cave-mouth': ((100.0, -6.0, 24.0), (106.0, -21.0, 21.5), (103.0, -11.0, 19.6)),
    'root-tunnel': ((106.2, -21.0, 21.6), (107.0, -30.0, 19.0), (106.3, -22.0, 20.0)),
    'grotto-ledge': ((91.5, -27.5, 11.4), (62.0, -32.0, 3.0), (91.0, -28.0, 9.65)),
    'grotto-floor': ((50.0, -41.0, 2.4), (80.0, -25.0, 7.0), (52.0, -38.0, 1.1)),
    'grotto-pool': ((57.5, -31.0, 3.5), (57.5, -15.0, -1.0), (57.5, -29.0, 1.1)),
    'grotto-gallery': ((41.2, -24.5, 10.8), (70.0, -30.0, 3.0), (42.5, -23.0, 9.05)),
    'portal-west-arch': ((44.0, 41.2, 12.8), (37.7, 41.2, 12.4), (43.0, 41.0, 11.2)),
    'portal-east-gateway': ((69.7, 32.5, 12.9), (69.7, 39.6, 12.6), (69.7, 34.0, 11.3)),
    'portal-south-west-arch': ((41.0, 26.8, 11.2), (34.5, 26.8, 10.8), (40.0, 26.8, 9.8)),
    'courtyard-plan': ((58.0, 23.0, 75.0), (58.0, 25.0, 10.0), (57.5, 20.0, 10.9)),   # top-down: placing the blossom tree
    # The crown gate to the Crownworks, beyond the castle's north gate.
    'crown-gate': ((58.5, 130.2, 14.6), (58.5, 137.0, 15.6), (58.5, 131.0, 11.9)),
    'crown-gate-oblique': ((64.5, 131.5, 15.8), (58.5, 137.0, 15.2), (58.5, 131.0, 11.9)),
    'crown-gate-reverse': ((54.0, 146.0, 19.5), (58.5, 136.0, 14.5), (58.5, 131.0, 11.9)),
    # The great cherry blossom tree on the north lawn (46.5, 43), framing the castle.
    'great-tree': ((60.0, 8.0, 18.0), (49.0, 43.0, 18.5), (57.5, 20.0, 10.9)),
    'great-tree-trunk': ((51.0, 35.5, 11.8), (46.5, 43.0, 17.0), (50.5, 36.5, 10.9)),
    'great-tree-reverse': ((38.0, 54.0, 21.0), (48.0, 38.0, 15.0), (44.0, 47.0, 10.9)),
    # Original "Blossom meadow" tile under a tree (105.9, -3.9): the patch
    # border against the retail meadow from above, obliquely, grazing and reversed.
    'blossom-overhead': ((102.6, -7.4, 25.5), (104.2, -5.6, 19.4), (100.5, -9.5, 20.0)),
    'blossom-oblique': ((100.5, -9.5, 22.5), (105.9, -3.9, 19.4), (102.5, -8.0, 20.0)),
    'blossom-grazing': ((101.8, -7.4, 20.3), (106.5, -3.0, 19.5), (102.5, -8.0, 20.0)),
    'blossom-reverse': ((111.5, 1.5, 22.5), (105.9, -3.9, 19.4), (102.5, -8.0, 20.0)),
    # Hamlet Green: the first Tripo-built area (stage_tripo_areas), east meadow (106..156, 10.5..57.5).
    'hamlet-overview': ((98.0, -2.0, 52.0), (131.0, 34.0, 18.0), (112.0, 30.0, 21.0)),
    'hamlet-lane': ((110.0, 26.0, 23.5), (134.0, 38.0, 19.5), (112.0, 30.0, 21.0)),
    'hamlet-from-courtyard': ((88.0, 30.0, 26.0), (131.0, 34.0, 19.0), (95.0, 30.0, 16.0)),
    'hamlet-reverse': ((160.0, 66.0, 36.0), (128.0, 32.0, 18.0), (150.0, 55.0, 24.0)),
    # The east meadow where Hamlet Green stood (removed 3 November 2026): straight
    # down, obliquely and at Spyro's eye height across the former footprint.
    'east-meadow-overhead': ((131.0, 33.9, 95.0), (131.0, 34.0, 18.0), (131.0, 34.0, 26.0)),
    'east-meadow-oblique': ((96.0, -4.0, 46.0), (131.0, 34.0, 19.0), (112.0, 30.0, 25.0)),
    'east-meadow-ground': ((104.0, 34.0, 22.6), (150.0, 34.0, 20.0), (106.0, 34.0, 21.0)),
}


def engine(p):
    return (p[0], p[2], p[1])


def main(names):
    global OUT, WORLD
    if '--out' in names:      # --out DIR: write the captures elsewhere (validation runs)
        i = names.index('--out'); OUT = Path(names[i + 1]); names = names[:i] + names[i + 2:]
    if '--world' in names:    # --world FILE: capture another .s2world (a backup, a candidate)
        i = names.index('--world'); WORLD = Path(names[i + 1]); names = names[:i] + names[i + 2:]
    OUT.mkdir(parents=True, exist_ok=True)
    extra, suffix = [], ''
    if '--unlock-travel' in names:   # open sealed doors (the crown gate's picture)
        names = [n for n in names if n != '--unlock-travel']
        extra, suffix = ['--unlock-travel'], '-open'
    for name in names or VIEWS:
        eye, target, spawn = VIEWS[name]
        shot = OUT / f'{name}.s2shot'
        shot.write_text('S2SHOT 1\n1280 800 8\n' + ' '.join(map(str, [*engine(spawn), 0, 0, 55, 1])) + '\n0\n0\n1\n0 '
                        + ' '.join(map(str, [*engine(eye), *engine(target)])) + '\n')
        bmp = OUT / f'{name}{suffix}.bmp'
        with (OUT / f'{name}{suffix}.log').open('w') as log:
            subprocess.run([str(ROOT / 'build/Release/spyro_workbench.exe'), '--game', str(ROOT / 'data/disc'),
                            '--custom-level', str(WORLD), '--cinematic-shot', str(shot), '--capture', str(bmp), *extra],
                           cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=300)
        Image.open(bmp).save(OUT / f'{name}{suffix}.png')
        bmp.unlink()
        print(name, 'ok')


if __name__ == '__main__':
    main(sys.argv[1:])
