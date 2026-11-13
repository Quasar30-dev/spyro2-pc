"""Exploration runs of retail Spyro 2 under the pad fixture (TEST FIXTURE): a step list, a shot and a state line per step.

    python tools/soi/ds_explore.py CUE NAME "wait:12" "idle:30" "START:3" "idle:60" "shot" ...
Steps: wait:S (free run, seconds), BUTTONS:T (hold for T ticks, e.g. CROSS+UP:20), idle:T, shot, state.
"""
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ds_harness as d  # noqa: E402


def main(cue, name, steps):
    log = d.Log(name)
    n = 0
    with d.Session(cue, log) as s:
        pad = None
        for st in steps:
            if st.startswith("wait:"):
                t0 = time.time()
                while time.time() - t0 < float(st[5:]):
                    time.sleep(0.25)
                log(st)
            elif st == "shot":
                n += 1
                dest = d.OUT / "shots" / f"{name}-{n:02d}.png"
                p = pad.shot(dest) if pad else s.screenshot(dest)
                log("shot", p, pad.state() if pad else "")
            elif st == "state":
                log("state (game state, level, spyro xyz)", pad.state())
            else:
                b, t = st.split(":")
                if pad is None:
                    pad = d.Pad(s)
                rep = b.endswith("!")
                b = b.rstrip("!")
                pad.run(0 if b == "idle" else d.buttons(b), int(t), repeat=rep)
                log(st, "->", pad.state())


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3:])
