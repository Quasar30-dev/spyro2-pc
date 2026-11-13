"""Author the Crownworks steel tile (EXPANSION_LEVEL_PLANS 6.5.2 retexture).

An original 64x64 PS1-style texture for the arena's machinery (relay and
service housings, control pedestals, crusher heads, jet grates and ladder
rails), which used Autumn Plains' ribbed conduit (bank 33 tile 11). The
world's TILE_OVERRIDES section swaps it in for that tile in the Crownworks
only.

It keeps the conduit's ribbed character as steel: horizontal ribs with dark
grooves and lit lips, two plates split by a seam, a rivet at each plate
corner, and a bevelled frame. Its colours are sampled from the Backrealms
armour steel (data/expansions/backrealms/armor-steel.rgba), the Cyborg Ripto
material, so the arena's machines match his armour. Like a 4bpp PS1 tile it is
quantised to 16 colours with no filtering.

    python tools/author_crown_steel.py
"""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
STEEL = ROOT / 'data/expansions/backrealms/armor-steel.rgba'
OUT = ROOT / 'data/custom-levels/cyborg-ripto-arena/pc/textures/crown-steel.rgba'


def reference_tones():
    """Dark, mid and light steel tones from the armour steel (by luminance)."""
    raw = STEEL.read_bytes()
    px = [tuple(raw[i:i + 3]) for i in range(0, len(raw), 4)]
    px.sort(key=lambda c: c[0] * .3 + c[1] * .59 + c[2] * .11)
    pick = lambda f: px[int(f * (len(px) - 1))]
    return pick(.08), pick(.5), pick(.94)


def mix(a, b, t):
    return tuple(round(a[k] + (b[k] - a[k]) * t) for k in range(3))


def main():
    dark, mid, light = reference_tones()
    shadow = tuple(max(0, round(c * .38)) for c in dark)
    img = Image.new('RGB', (64, 64))
    p = img.load()
    for y in range(64):
        for x in range(64):
            # Ribs every 8 texels: a groove, a lit lip, then a gently shaded face.
            r = y % 8
            if r == 0:
                c = shadow
            elif r == 1:
                c = light
            else:
                c = mix(mid, dark, (r - 2) / 7)
            # Brushed grain, like the armour steel (a coarse PS1-scale streak).
            if (x * 7 + y * 3) % 11 == 0 and r > 1:
                c = mix(c, light, .25)
            # Two plates: a seam down the middle, a bevelled frame round the tile.
            if x in (31, 63) or y == 63:
                c = shadow
            elif x in (0, 32) or y == 0:
                c = mix(light, mid, .3)
            p[x, y] = c
    # A rivet near each plate corner: a lit cap with a shadow below-right.
    for px0 in (4, 27, 36, 59):
        for py0 in (4, 59):
            for dx, dy, col in ((0, 0, light), (1, 0, light), (0, 1, mix(light, mid, .5)), (1, 1, dark), (2, 1, shadow), (1, 2, shadow), (2, 2, shadow)):
                p[px0 + dx, py0 + dy] = col
    tile = img.quantize(colors=16, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).convert('RGBA')
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(tile.tobytes())
    tile.resize((256, 256), Image.NEAREST).save(OUT.with_suffix('.png'))
    print('wrote', OUT.relative_to(ROOT), 'palette', len(tile.getcolors()), 'colours; tones', dark, mid, light)


if __name__ == '__main__':
    main()
