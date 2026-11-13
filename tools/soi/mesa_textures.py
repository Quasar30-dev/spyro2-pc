"""Authored 64 x 64 tiles for Market Mesa, painted after Season of Ice's own art (Spyro 2 PS1 style).

  python tools/soi/mesa_textures.py      # writes data/soi/pc/textures/*.rgba and preview.png

No retail Spyro 2 tile matches SoI's dusky sandstone cobbles, slate roofs, pale-cyan copings or mint
pines, so these are painted here (the loader's authored-tile route, S2WORLD 4 slot -2). Colours are
sampled from SoI's map art (data/soi/levels/06-market-mesa/map.png); shapes are drawn as a PS1 artist
would: rounded stones with a top-left light, dark joints, a little noise. Every tile wraps in x and y
except where noted (snow foot: x only; banners and windows: not tiled).
"""
from __future__ import annotations

import math
from pathlib import Path

import numpy as np
from PIL import Image

PROJECT = Path(__file__).resolve().parents[2]
OUT = PROJECT / "data/soi/pc/textures"
N = 64
LIGHT = np.array([-0.55, -0.6, 0.58])
LIGHT = LIGHT / np.linalg.norm(LIGHT)
yy, xx = np.mgrid[0:N, 0:N].astype(float)


def vnoise(seed: int, freq: int, octaves: int = 3) -> np.ndarray:
    """Tileable value noise in [-1, 1]."""
    rng = np.random.default_rng(seed)
    out = np.zeros((N, N))
    amp, tot = 1.0, 0.0
    for o in range(octaves):
        f = freq * 2 ** o
        g = rng.uniform(-1, 1, (f, f))
        u, v = xx * f / N, yy * f / N
        i0, j0 = np.floor(u).astype(int), np.floor(v).astype(int)
        fu, fv = u - i0, v - j0
        fu, fv = fu * fu * (3 - 2 * fu), fv * fv * (3 - 2 * fv)
        a = g[j0 % f, i0 % f]
        b = g[j0 % f, (i0 + 1) % f]
        c = g[(j0 + 1) % f, i0 % f]
        d = g[(j0 + 1) % f, (i0 + 1) % f]
        out += amp * ((a * (1 - fu) + b * fu) * (1 - fv) + (c * (1 - fu) + d * fu) * fv)
        tot += amp
        amp *= 0.5
    return out / tot


def voronoi(seeds: np.ndarray, sx=1.0, sy=1.0):
    """F1, F2 (pixels, torus) and the nearest seed index; sx/sy stretch the metric."""
    d = []
    for px, py in seeds:
        dx = np.abs(xx - px)
        dy = np.abs(yy - py)
        dx = np.minimum(dx, N - dx) * sx
        dy = np.minimum(dy, N - dy) * sy
        d.append(np.hypot(dx, dy))
    d = np.stack(d)
    order = np.argsort(d, 0)
    f1 = np.take_along_axis(d, order[:1], 0)[0]
    f2 = np.take_along_axis(d, order[1:2], 0)[0]
    return f1, f2, order[0]


def relaxed_seeds(n: int, seed: int, iters: int = 3) -> np.ndarray:
    """Roughly even stone centres (Lloyd relaxation on the torus)."""
    rng = np.random.default_rng(seed)
    pts = rng.uniform(0, N, (n, 2))
    for _ in range(iters):
        _, _, idx = voronoi(pts)
        for k in range(n):
            m = idx == k
            if m.any():
                # circular mean on the torus
                ax = np.angle(np.exp(2j * np.pi * xx[m] / N).mean()) * N / (2 * np.pi)
                ay = np.angle(np.exp(2j * np.pi * yy[m] / N).mean()) * N / (2 * np.pi)
                pts[k] = (ax % N, ay % N)
    return pts


def shade(h: np.ndarray, strength: float = 1.0) -> np.ndarray:
    """Lambert term from a wrapped height field (top-left light)."""
    gx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * 0.5 * strength
    gy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * 0.5 * strength
    n = np.stack([-gx, -gy, np.ones_like(h)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return np.clip(n @ LIGHT, 0, 1)


def col(*c):
    return np.array(c, float)


def cobbles(seed: int, stones: int, palette, mortar, joint=2.0, sx=1.0, sy=1.0):
    pts = relaxed_seeds(stones, seed)
    f1, f2, idx = voronoi(pts, sx, sy)
    edge = f2 - f1
    h = np.clip(edge / 7.0, 0, 1) ** 0.6 * 6.0 + vnoise(seed + 1, 8, 2) * 0.35
    lam = shade(h)
    rng = np.random.default_rng(seed + 2)
    base = np.array(palette, float)[rng.integers(0, len(palette), stones)]
    stone = base[idx] * (0.52 + 0.62 * lam)[..., None] * (1 + 0.06 * vnoise(seed + 3, 16, 2))[..., None]
    m = np.clip((joint + 0.8 - edge) / 1.6, 0, 1)[..., None]
    return stone * (1 - m) + np.asarray(mortar, float) * m, idx, edge


SAND = [col(244, 222, 192), col(236, 206, 182), col(248, 230, 204), col(230, 196, 178), col(240, 214, 196)]
SAND_MORTAR = col(150, 106, 112)


def cobble_sand(seed=21):
    # small rounded cobbles: a 64 px tile covers 16 x 48 SoI units (1.6 x 2.0 world), so ~30 stones
    im, _, _ = cobbles(seed, 30, SAND, SAND_MORTAR, 1.15, sy=1.25)
    return im


def snow_band(im: np.ndarray, base: float, seed: int, puffs: int = 5) -> np.ndarray:
    """Puffy SoI snow along the bottom edge (wraps in x): white tops, blue shading and outline."""
    rng = np.random.default_rng(seed)
    h = np.full((N, N), -1.0)
    cx = (np.arange(puffs) + rng.uniform(-0.2, 0.2, puffs)) * N / puffs
    for k in range(puffs):
        r = rng.uniform(8.5, 11.5)
        cy = N - base + rng.uniform(-1.5, 2.5)
        dx = np.abs(xx - cx[k])
        dx = np.minimum(dx, N - dx)
        d = np.hypot(dx, yy - cy)
        h = np.maximum(h, np.where(d < r, np.sqrt(np.clip(r * r - d * d, 0, None)) / r, -1))
    h = np.where(yy > N - base + 6, np.maximum(h, 0.35), h)
    inside = h >= 0
    lam = shade(np.where(inside, h * 6, 0), 1.0)
    snow = col(150, 186, 232) + (col(252, 254, 255) - col(150, 186, 232)) * np.clip(lam * 1.15, 0, 1)[..., None]
    out = np.where(inside[..., None], snow, im)
    # blue outline as SoI draws it
    ring = inside & ~(np.roll(inside, 1, 0) & np.roll(inside, -1, 0) & np.roll(inside, 1, 1) & np.roll(inside, -1, 1))
    out[ring & (yy < N - 2)] = col(96, 132, 206)
    return out


def cobble_snowfoot(seed=21):
    # the 24-z base row: a strip of cobbles, then SoI's thick puffy snow rim
    return snow_band(cobble_sand(seed), 48, seed + 40, puffs=3)


def snow_puff(seed=5):
    rng = np.random.default_rng(seed)
    h = np.zeros((N, N))
    for _ in range(26):
        cx, cy, r = rng.uniform(0, N), rng.uniform(0, N), rng.uniform(7, 13)
        dx, dy = np.abs(xx - cx), np.abs(yy - cy)
        dx, dy = np.minimum(dx, N - dx), np.minimum(dy, N - dy)
        d = np.hypot(dx, dy)
        h = np.maximum(h, np.sqrt(np.clip(r * r - d * d, 0, None)))
    lam = shade(h, 0.6)
    return col(132, 170, 226) + (col(250, 253, 255) - col(132, 170, 226)) * np.clip(lam * 1.2, 0, 1)[..., None]


def under_rock(seed=9):
    im, _, _ = cobbles(seed, 22, [col(150, 120, 132), col(138, 108, 124), col(160, 130, 140)], col(76, 54, 70), 1.4)
    return im


def slate(seed=31):
    """Blue slate rows (SoI's roofs): 4 courses, staggered slates, light top / dark foot per slate."""
    rng = np.random.default_rng(seed)
    out = np.zeros((N, N, 3))
    rows = 4
    rh = N / rows
    for r in range(rows):
        y0 = r * rh
        cuts = np.sort((np.arange(4) * 16 + (8 if r % 2 else 0) + rng.uniform(-2, 2, 4)) % N)
        t = (yy - y0) / rh
        inrow = (yy >= y0) & (yy < y0 + rh)
        # slate index by x
        k = np.searchsorted(cuts, xx[inrow], side="right") % len(cuts)
        tone = rng.uniform(0.9, 1.08, len(cuts))[k]
        tt = t[inrow]
        top, bot = col(128, 140, 206), col(78, 88, 160)
        c = top + (bot - top) * np.clip(tt ** 0.8, 0, 1)[:, None]
        c *= tone[:, None]
        # slate edges: darker joints between slates and a dark course line
        xr = xx[inrow]
        dj = np.min(np.stack([np.minimum(np.abs(xr - cc), N - np.abs(xr - cc)) for cc in cuts]), 0)
        c[dj < 1.0] *= 0.72
        c[tt > 0.92] *= 0.7
        c[tt < 0.05] = col(150, 162, 220)
        out[inrow] = c
    out *= (1 + 0.07 * vnoise(seed + 1, 16, 2))[..., None]
    # a few green tufts as SoI sprinkles on roofs
    for _ in range(3):
        x, y = rng.integers(0, N, 2)
        out[y % N, x % N] = col(90, 190, 120)
        out[(y + 1) % N, x % N] = col(60, 150, 100)
    return out


def coping_side(seed=41):
    """Pale-cyan coping bricks, one course of four (the face is stretched along the wall)."""
    rng = np.random.default_rng(seed)
    out = np.zeros((N, N, 3))
    for k in range(4):
        x0 = k * 16
        tone = rng.uniform(0.94, 1.04)
        m = (xx >= x0) & (xx < x0 + 16)
        t = yy[m] / N
        c = col(236, 248, 255) + (col(150, 192, 236) - col(236, 248, 255)) * np.clip(t * 1.1, 0, 1)[:, None]
        out[m] = c * tone
        out[(xx == x0) | (xx == x0 + 1)] = col(108, 140, 206)
    out[yy >= N - 6] = col(96, 124, 196)
    out *= (1 + 0.05 * vnoise(seed + 2, 16, 2))[..., None]
    return out


def coping_top(seed=43):
    """The coping's snowy top, seen from above: white-cyan with brick joints."""
    out = np.zeros((N, N, 3))
    t = yy / N
    out[:] = (col(250, 253, 255) + (col(196, 226, 250) - col(250, 253, 255)) * t[..., None])
    for k in range(4):
        out[(xx == k * 16) | (xx == k * 16 + 1)] = col(150, 186, 232)
    out *= (1 + 0.04 * vnoise(seed, 16, 2))[..., None]
    return out


def plaster(seed=51, brick=True):
    """SoI's white plaster walls: faint brick courses, lavender toward the foot (v = 0 is the
    tile bottom, the wall's foot)."""
    out = np.zeros((N, N, 3)) + col(246, 244, 252)
    out = out * (1 + 0.035 * vnoise(seed, 8, 3))[..., None]
    if brick:
        for r in range(5):
            y = int(r * 12.8)
            out[y] *= 0.86
            off = 0 if r % 2 else 10
            for x in range(off, N, 21):
                out[y:y + 13, x % N] *= 0.9
    foot = np.clip((yy - 40) / 24, 0, 1)
    out = out * (1 - 0.28 * foot[..., None]) + col(170, 150, 214) * 0.28 * foot[..., None]
    return out


def window(seed=53):
    """A small recessed window in a dark wooden frame on the plaster (not tiled)."""
    out = plaster(seed)
    x0, x1, y0, y1 = 22, 42, 16, 42
    m = (xx >= x0) & (xx < x1) & (yy >= y0) & (yy < y1)
    out[m] = col(132, 84, 60)
    inner = (xx >= x0 + 3) & (xx < x1 - 3) & (yy >= y0 + 3) & (yy < y1 - 3)
    out[inner] = col(54, 40, 72) + col(30, 26, 40) * ((yy[inner] - y0) / (y1 - y0))[:, None]
    out[inner & (xx == (x0 + x1) // 2)] = col(120, 76, 56)
    out[(xx >= x0 + 3) & (xx < x1 - 3) & (yy == y0 + 3)] = col(40, 28, 52)
    out[(xx >= x0 - 1) & (xx < x1 + 1) & (yy >= y1) & (yy < y1 + 3)] = col(196, 180, 214)    # sill
    out[(xx >= x0) & (xx < x1) & (yy == y0)] = col(168, 112, 80)
    return out


def door(seed=55):
    out = plaster(seed)
    x0, x1, y0 = 18, 46, 14
    cy = y0 + 14
    r = (x1 - x0) / 2
    cx = (x0 + x1) / 2
    arch = (yy >= cy) | (np.hypot(xx - cx, yy - cy) < r)
    m = (xx >= x0) & (xx < x1) & (yy >= y0) & arch
    out[m] = col(136, 86, 58)
    inner = (xx >= x0 + 3) & (xx < x1 - 3) & (yy >= y0 + 3) & ((yy >= cy) | (np.hypot(xx - cx, yy - cy) < r - 3))
    planks = (((xx - x0) // 6) % 2 == 0)
    out[inner] = np.where(planks[inner][:, None], col(112, 70, 48), col(98, 60, 42))
    out[inner & (np.abs(yy - 40) < 1)] = col(70, 46, 34)
    return out


def pine(seed=61):
    """Mint-green SoI pine fronds with snow on the upper edges (v = 1 at a tier's top)."""
    rng = np.random.default_rng(seed)
    out = np.zeros((N, N, 3)) + col(40, 132, 100)
    for r in range(4):
        yb = r * 16 + 16
        for k in range(6):
            cx = (k + 0.5 * (r % 2)) * N / 6 + rng.uniform(-2, 2)
            dx = np.abs(xx - cx)
            dx = np.minimum(dx, N - dx)
            # a drooping frond: an arch from its top centre down to both sides
            top = yb - 14 + (dx / 8.0) ** 2 * 6
            m = (yy >= top) & (yy < yb) & (dx < 9)
            t = np.clip((yy - top) / 12, 0, 1)
            c = col(184, 250, 190) + (col(60, 176, 120) - col(184, 250, 190)) * t[..., None]
            out[m] = c[m]
            snow = m & (yy < top + 2.2)
            out[snow] = col(244, 252, 255)
    out *= (1 + 0.06 * vnoise(seed + 1, 16, 2))[..., None]
    return out          # image row 0 is the tile's top (v = 1): fronds droop downward


def bark(seed=63):
    out = np.zeros((N, N, 3)) + col(196, 116, 52)
    out *= (1 + 0.18 * np.sin(xx / N * 2 * np.pi * 5 + 2.5 * vnoise(seed, 4, 2)))[..., None]
    out *= (1 + 0.06 * vnoise(seed + 1, 16, 2))[..., None]
    return out


def cloth_pink(seed=71):
    """SoI's magenta market cloth: soft folds, a light hem at the bottom (v = 0)."""
    f = np.sin(xx / N * 2 * np.pi * 3 + 0.6 * vnoise(seed, 4, 2))
    out = col(226, 92, 176) * (0.78 + 0.22 * f)[..., None]
    hem = yy >= N - 8
    out[hem] = col(250, 170, 220)
    out[yy == N - 9] = col(150, 40, 120)
    out[yy < 3] = col(170, 56, 140)
    return out


def flag_cyan(seed=73):
    """SoI's pole banners: white cloth, pale-cyan stripes, a blue fringe at the bottom."""
    out = np.zeros((N, N, 3)) + col(244, 252, 255)
    stripe = ((xx // 8) % 2 == 1)
    out[stripe] = col(170, 226, 244)
    out *= (0.86 + 0.14 * np.sin(yy / N * 2 * np.pi * 2 + xx / 12))[..., None]
    out[yy >= N - 6] = col(84, 150, 214)
    return out


def banner_orange(seed=75):
    """SoI's wall banners: orange cloth, yellow border, purple tassels (bottom, on plaster)."""
    out = plaster(seed, brick=False)
    body = (xx >= 6) & (xx < 58) & (yy < 50)
    out[body] = col(250, 214, 60)
    inner = (xx >= 11) & (xx < 53) & (yy >= 4) & (yy < 45)
    out[inner] = col(238, 128, 30)
    out[inner & (yy >= 40)] = col(214, 96, 26)
    out[(xx >= 6) & (xx < 58) & (yy < 3)] = col(150, 90, 40)       # the rod
    for cx in (18, 32, 46):
        d = np.hypot(xx - cx, yy - 56)
        out[(np.abs(xx - cx) < 1.5) & (yy >= 50) & (yy < 54)] = col(110, 40, 130)
        out[d < 4.2] = col(130, 52, 156)
        out[(d < 2) & (yy < 56)] = col(190, 120, 210)
    return out


def awning(seed=77):
    """Red / cream stripes (SoI's stall awnings), the stripes run down the slope (v)."""
    out = np.where(((xx // 8) % 2 == 0)[..., None], col(214, 52, 52), col(248, 226, 180))
    out = out * (0.9 + 0.1 * np.cos(yy / N * np.pi))[..., None]
    out[yy >= N - 5] = col(160, 40, 60)
    return out


def wood(seed=79):
    out = np.zeros((N, N, 3)) + col(150, 98, 70)
    out *= (1 + 0.16 * np.sin(yy / N * 2 * np.pi * 3 + 3 * vnoise(seed, 4, 2)))[..., None]
    out[(yy % 16) == 0] *= 0.6
    return out


def hatch(seed=81):
    """SoI's wooden trapdoors: pale planks in a darker frame, corner studs (one per face)."""
    out = np.zeros((N, N, 3)) + col(214, 176, 118)
    out *= (1 + 0.10 * np.sin(xx / N * 2 * np.pi * 9 + 2.0 * vnoise(seed, 4, 2)))[..., None]
    out[((yy - 4) % 12) < 1] *= 0.62                       # plank joints
    frame = (xx < 5) | (xx >= N - 5) | (yy < 5) | (yy >= N - 5)
    out[frame] = col(150, 104, 66)
    out[(xx == 5) | (xx == N - 6) | (yy == 5) | (yy == N - 6)] = col(110, 74, 48)
    for cx in (2, N - 3):
        for cy in (2, N - 3):
            out[(np.abs(xx - cx) <= 1) & (np.abs(yy - cy) <= 1)] = col(90, 86, 96)
    return out


def far_rock(seed=83):
    """Distant mesas: sandstone strata hazed toward the sky's lavender."""
    base = col(226, 196, 176) * (1 + 0.12 * np.sin(yy / N * 2 * np.pi * 4 + 1.5 * vnoise(seed, 4, 2)))[..., None]
    base *= (1 + 0.06 * vnoise(seed + 1, 8, 3))[..., None]
    return base * 0.5 + col(206, 180, 232) * 0.5


TEXTURES = {
    "cobble_sand": cobble_sand, "cobble_sand_b": lambda: cobble_sand(27), "cobble_snowfoot": cobble_snowfoot,
    "snow_puff": snow_puff, "under_rock": under_rock, "slate": slate, "coping_side": coping_side,
    "coping_top": coping_top, "plaster": plaster, "window_soi": window, "door_soi": door, "pine": pine,
    "bark_soi": bark, "cloth_pink": cloth_pink, "flag_cyan": flag_cyan, "banner_orange": banner_orange,
    "awning_soi": awning, "wood_soi": wood, "hatch_soi": hatch, "far_rock": far_rock,
}


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    tiles = []
    for name, fn in TEXTURES.items():
        rgb = np.clip(fn(), 0, 255).astype(np.uint8)
        rgba = np.concatenate([rgb, np.full((N, N, 1), 255, np.uint8)], -1)
        (OUT / f"{name}.rgba").write_bytes(rgba.tobytes())
        tiles.append((name, Image.fromarray(rgb)))
        (OUT / "png").mkdir(exist_ok=True)
        Image.fromarray(rgb).save(OUT / "png" / f"{name}.png")    # for the Blender prop previews
    from PIL import ImageDraw
    cols = 6
    sheet = Image.new("RGB", (cols * 136, math.ceil(len(tiles) / cols) * 150), (20, 20, 24))
    d = ImageDraw.Draw(sheet)
    for k, (name, im) in enumerate(tiles):
        x, y = (k % cols) * 136, (k // cols) * 150
        # 2 x 2 repeats show the wrap
        rep = Image.new("RGB", (128, 128))
        for a in (0, 64):
            for b in (0, 64):
                rep.paste(im, (a, b))
        sheet.paste(rep, (x + 4, y + 4))
        d.text((x + 4, y + 134), name, fill=(255, 255, 0))
    sheet.save(OUT / "preview.png")
    print(f"{len(tiles)} tiles -> {OUT}")


if __name__ == "__main__":
    main()
