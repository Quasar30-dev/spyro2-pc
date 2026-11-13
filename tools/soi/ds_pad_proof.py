"""Pad-input proof on retail Spyro 2 in DuckStation, hidden (TEST FIXTURE input, see ds_harness.Pad).

Route (pure pad input, no state writes): boot -> title START -> memory-card prompt CROSS (new game) -> skip the two
opening cutscenes (START, START, CROSS) -> Glimmer (level 1, game state 0). Then Spyro walks back towards the camera (DOWN; UP walks into Pogo's talk) and jumps
(CROSS), with Spyro's position (0x80069ff0) logged every tick and shots at the start, after the walk and mid-jump.

    python tools/soi/ds_pad_proof.py CUE [NAME]
PASS lines in the log: "WALK PASS" (moved > 1.5 world units), "JUMP PASS" (rose > 0.4 world units), "PROOF pad PASS".
"""
import math
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ds_harness as d  # noqa: E402

MENU = [("idle", 380), ("START!", 8), ("idle", 40), ("CROSS!", 8), ("idle", 150), ("START!", 5), ("idle", 100),
        ("START!", 5), ("idle", 100), ("CROSS!", 5)]


def to_glimmer(pad: d.Pad, log, limit=900):
    for b, t in MENU:
        pad.run(0 if b == "idle" else d.buttons(b.rstrip("!")), t, repeat=b.endswith("!"))
    for k in range(limit):
        gs, lv, pos = pad.state()
        if gs == 0 and lv == 1 and pos != (0, 0, 0):
            log(f"in Glimmer after {k} extra ticks: state {gs} level {lv} spyro {pos}")
            return True
        pad.run(0, 1)
    log("did not reach Glimmer:", pad.state())
    return False


def main(cue, name="pad-glimmer"):
    log = d.Log(name)
    shots = d.OUT / "shots"
    ok_walk = ok_jump = False
    with d.Session(cue, log) as s:
        t0 = time.time()
        while time.time() - t0 < 12:
            time.sleep(0.25)
        pad = d.Pad(s)
        if not to_glimmer(pad, log):
            log("PROOF pad FAIL (route)")
            return
        pad.run(0, 30)
        p0 = pad.state()[2]
        log("start", p0, "shot", pad.shot(shots / f"{name}-1-start.png", 0.3))
        track = []
        pad.run(d.buttons("DOWN"), 40, on_tick=lambda t: track.append(pad.state()[2]))
        p1 = pad.state()[2]
        log("walk track (every 5th tick):", track[::5])
        dist = math.dist(p0[:2], p1[:2]) / 1024
        ok_walk = dist > 1.5
        log(f"WALK {'PASS' if ok_walk else 'FAIL'}: moved {dist:.2f} world units, {p0} -> {p1}")
        log("after walk shot", pad.shot(shots / f"{name}-2-walked.png", 0.3))
        pad.run(0, 20)
        g0 = pad.state()[2]
        ztrack = []
        pad.run(d.buttons("CROSS"), 9, on_tick=lambda t: ztrack.append(pad.state()[2][2]))
        shot = pad.shot(shots / f"{name}-3-jump.png", 0.0)      # captured within a few frames: still airborne
        zshot = pad.state()[2][2]
        pad.run(0, 40, on_tick=lambda t: ztrack.append(pad.state()[2][2]))
        rise = (max(ztrack + [zshot]) - g0[2]) / 1024
        ok_jump = rise > 0.4
        log("jump z track:", ztrack, "z at shot", zshot)
        log(f"JUMP {'PASS' if ok_jump else 'FAIL'}: rose {rise:.2f} world units from z {g0[2]} (shot {shot}, "
            f"{(zshot - g0[2]) / 1024:.2f} up at the shot)")
        log("landed", pad.state(), "shot", pad.shot(shots / f"{name}-4-landed.png", 0.3))
    log(f"PROOF pad {'PASS' if ok_walk and ok_jump else 'FAIL'}")


if __name__ == "__main__":
    main(*sys.argv[1:])
