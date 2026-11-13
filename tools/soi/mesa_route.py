"""A walk route on Market Mesa for the PS1 play run: from SoI's start down the terraces to the nearest step that
can be jumped up (one 24-z terrace), plus the nearest gems on the way. Breadth-first over SoI's 8-unit height
lattice (walking level or dropping <= 30 z), waypoints every few cells, converted to retail units with the
encoder's offset. Output: data/soi/ps1/mesa-route.json.

    python tools/soi/mesa_route.py
"""
import json
import sys
from collections import deque
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import mesa_build as MB  # noqa: E402

PROJECT = HERE.parents[1]


def main():
    lv = PROJECT / "data/soi/levels/06-market-mesa/level.json"
    level = json.loads(lv.read_text())
    L = MB.build_layout(level, Image.open(lv.parent / "map.png"))
    hc, ox, oy, G = L["hc"], L["ox"], L["oy"], MB.G
    off = np.array(json.loads((PROJECT / "data/soi/ps1/mesa-ps1-report.json").read_text())["frame_offset"])

    def retail(x, y, z):
        return [int(x * MB.H * 1024 + off[0]), int(-y * MB.H * 1024 + off[1]), int(z * MB.V * 1024 + off[2])]

    sx, sy, _ = level["objects"]["start"]["pos"]
    s0 = (int((sx - ox) // G), int((sy - oy) // G))
    H, W = hc.shape
    prev = {s0: None}
    q = deque([s0])
    while q:
        i, j = q.popleft()
        for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            a, b = i + di, j + dj
            # stay two cells away from drops of more than a terrace when walking level (no edge-hugging)
            if 0 <= a < W and 0 <= b < H and (a, b) not in prev and hc[b, a] > 0 and 0 <= hc[j, i] - hc[b, a] <= 30:
                prev[(a, b)] = (i, j)
                q.append((a, b))
    best = None
    for (i, j) in prev:
        h = hc[j, i]
        for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            a3, b3 = i + 3 * di, j + 3 * dj
            if not (0 <= a3 < W and 0 <= b3 < H):
                continue
            if hc[j + dj, i + di] == h + 24 and hc[b3, a3] == h + 24 and hc[j - dj, i - di] == h and hc[j - 2 * dj, i - 2 * di] == h:
                n, c = 0, (i, j)
                while prev[c]:
                    c, n = prev[c], n + 1
                if best is None or n < best[0]:
                    best = (n, (i, j), (di, dj), h)
    n, cell, (di, dj), h = best
    path, c = [], (cell[0] - 2 * di, cell[1] - 2 * dj)
    if c not in prev:
        c = cell
    while c:
        path.append(c)
        c = prev[c]
    path = path[::-1]
    way = [path[k] for k in range(3, len(path), 3)] + [path[-1]]
    pts = [retail(ox + i * G + 4, oy + j * G + 4, hc[j, i]) for i, j in way]
    lower = retail(ox + cell[0] * G + 4, oy + cell[1] * G + 4, h)
    upper = retail(ox + (cell[0] + 3 * di) * G + 4, oy + (cell[1] + 3 * dj) * G + 4, h + 24)
    out = dict(waypoints=pts, step_lower=lower, step_upper=upper, step_height_soi=[int(h), int(h + 24)],
               path_cells=len(path))
    (PROJECT / "data/soi/ps1/mesa-route.json").write_text(json.dumps(out, indent=1))
    print(json.dumps(out))


if __name__ == "__main__":
    main()
