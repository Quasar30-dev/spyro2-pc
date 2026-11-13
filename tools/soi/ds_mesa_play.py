"""Play the Season of Ice mod disc (Market Mesa in Glimmer's slot) in DuckStation, hidden (TEST FIXTURE input).

Route: pad input only (ds_pad_proof.MENU: new game, the opening cutscenes), then level 1 (Glimmer's slot) loads
Market Mesa. Then: shots at the start and with the camera turned (L1 / R1), a walk, a gem collected (the gem total
0x800670cc rises) and a jump up a terrace step, each with its log line:
    ARRIVE PASS / FAIL, LOOK shots, WALK PASS / FAIL, GEM PASS / FAIL, JUMP PASS / FAIL, PROOF mesa-play PASS / FAIL

    python tools/soi/ds_mesa_play.py [CUE] [NAME]
"""
import json
import math
import socket
import struct
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import ds_harness as d  # noqa: E402
import ds_pad_proof as pp  # noqa: E402

CUE = "D:/Theia-staging/spyro2-mod/spyro2-soi-mesa.cue"
GEMS = 0x800670CC
CAMERA = 0x80067EAC          # camera x, y, z (80019928 reads 80067eac / 80067eb0)


def rd(pad, addr, fmt):
    return struct.unpack(fmt, pad.g.read(addr, struct.calcsize(fmt)))


class Driver:
    def __init__(self, pad, log):
        self.pad, self.log = pad, log
        self.hand = 1

    def pos(self):
        return rd(self.pad, d.SPYRO, "<iii")

    def cam(self):
        return rd(self.pad, CAMERA, "<iii")

    def dpad_for(self, target):
        """D-pad buttons that move Spyro towards target (camera-relative: up = from the camera to Spyro)."""
        p, c = self.pos(), self.cam()
        fx, fy = p[0] - c[0], p[1] - c[1]
        n = math.hypot(fx, fy) or 1.0
        fx, fy = fx / n, fy / n
        rx, ry = fy * self.hand, -fx * self.hand
        tx, ty = target[0] - p[0], target[1] - p[1]
        f, r = tx * fx + ty * fy, tx * rx + ty * ry
        ang = math.atan2(r, f)
        k = int(round(ang / (math.pi / 4))) % 8
        return ["UP", "UP+RIGHT", "RIGHT", "DOWN+RIGHT", "DOWN", "DOWN+LEFT", "LEFT", "UP+LEFT"][k]

    def calibrate(self):
        """Which way is screen-right in world terms: press RIGHT and see."""
        p0, c = self.pos(), self.cam()
        self.pad.run(d.buttons("RIGHT"), 10)
        p1 = self.pos()
        fx, fy = p0[0] - c[0], p0[1] - c[1]
        mx, my = p1[0] - p0[0], p1[1] - p0[1]
        cross = fx * my - fy * mx
        self.hand = 1 if cross < 0 else -1
        self.log(f"calibrate: RIGHT moved ({mx}, {my}) with forward ({fx}, {fy}); hand {self.hand}")
        self.pad.run(0, 10)

    def goto(self, target, ticks=240, tol=600, extra=0):
        for t in range(ticks):
            p = self.pos()
            if math.hypot(target[0] - p[0], target[1] - p[1]) < tol:
                return True, t
            b = self.dpad_for(target)
            self.pad.run(d.buttons(b) | extra, 1)
        return False, ticks


def stall_report(s, log, name):
    """The game stopped calling 8001271c: pause, read where it is, take a frame, keep going if it resumes."""
    try:
        s.gdb.s.sendall(bytes([3]))
        s.gdb.s.settimeout(1.0)
        junk = b""
        try:
            while True:                                   # drain stop replies / late answers
                junk += s.gdb.s.recv(65536)
        except socket.timeout:
            pass
        s.gdb.s.sendall(b"+")
        log("drained", junk[:200])
        s.gdb.s.settimeout(5)
        pc = s.gdb._send("p25")
        g = s.gdb._send("g")
        log("STALL g reply:", g[:400])
        regs = {}
        for n, r in (("sr", 0x20), ("lo", 0x21), ("hi", 0x22), ("badvaddr", 0x23), ("cause", 0x24), ("pc", 0x25),
                     ("ra", 0x1f), ("k0", 0x1a), ("k1", 0x1b), ("sp", 0x1d), ("a0", 4), ("v0", 2)):
            regs[n] = s.gdb._send(f"p{r:x}")
        log("STALL registers (raw little-endian hex): " + " ".join(f"{n}={v}" for n, v in regs.items()))
        st = [struct.unpack("<I", s.gdb.read(a, 4))[0] for a in (d.GAME_STATE, d.LEVEL)]
        log(f"STALL: no logic tick for 30 s; pc {pc} game state {st[0]} level {st[1]} spyro {rd_s(s, d.SPYRO)}")
        for a, n in ((0x80000100, 0x40), (0xA000E200, 0x200), (0x80008400, 0x200)):
            log(f"STALL mem {a:08x}: " + s.gdb.read(a, n).hex())
        s.resume()
        time.sleep(1.0)
        log("stall frame", s.screenshot(d.OUT / "shots" / f"{name}-stall.png"))
    except Exception as e:  # noqa: BLE001
        log("stall report failed", repr(e))


def rd_s(s, addr):
    return struct.unpack("<iii", s.gdb.read(addr, 12))


def main(cue=CUE, name="mesa-play"):
    log = d.Log(name)
    shots = d.OUT / "shots"
    res = {}
    views = []

    def shot(pad, tag, free_run):
        cam, spy = rd(pad, CAMERA, "<iii"), rd(pad, d.SPYRO, "<iii")
        path = pad.shot(shots / f"{name}-{len(views) + 1:02d}-{tag}.png", free_run)
        cam2, spy2 = rd(pad, CAMERA, "<iii"), rd(pad, d.SPYRO, "<iii")
        views.append(dict(tag=tag, path=str(path), camera=cam2, spyro=spy2))
        log(f"shot {tag} {path} camera {cam2} spyro {spy2}")
        return path
    rep = json.loads((HERE.parents[1] / "data/soi/ps1/mesa-ps1-report.json").read_text())
    with d.Session(cue, log) as s:
        t0 = time.time()
        while time.time() - t0 < 12:
            time.sleep(0.25)
        pad = d.Pad(s)
        arrived = pp.to_glimmer(pad, log, limit=1500)
        gs, lv, p = pad.state()
        spawn = rep["spawn"]
        near = math.dist(p[:2], spawn[:2]) < 4096 if p != (0, 0, 0) else False
        res["arrive"] = arrived and near
        log(f"ARRIVE {'PASS' if res['arrive'] else 'FAIL'}: state {gs} level {lv} spyro {p} spawn {spawn}")
        pad.run(0, 20)
        shot(pad, "start", 0.5)
        for k, b in enumerate(("R1", "R1", "R1")):
            pad.run(d.buttons(b), 18)
            pad.run(0, 10)
            shot(pad, f"look{k}", 0.4)
        if not res["arrive"]:
            log("PROOF mesa-play FAIL (arrival)")
            return
        drv = Driver(pad, log)
        drv.calibrate()
        course0 = (rd(pad, GEMS, "<i")[0], drv.pos())
        try:
            play_course(pad, drv, s, log, shot, res, name, rep)
        except (TimeoutError, OSError) as e:
            log("run stopped:", repr(e), "tick timer 0x8006700c while waiting:", getattr(pad, "stall", None))
            stall_report(s, log, name)
        try:
            g_end, p_end = rd(pad, GEMS, "<i")[0], drv.pos()
            moved = math.dist(course0[1][:2], p_end[:2]) / 1024
            res["course_walk"] = moved > 5.0
            res["course_gems"] = g_end > course0[0]
            log(f"COURSE WALK {'PASS' if res['course_walk'] else 'FAIL'}: {moved:.1f} world units from the course start "
                f"{course0[1]} to {p_end}")
            log(f"COURSE GEMS {'PASS' if res['course_gems'] else 'FAIL'}: gem total {course0[0]} -> {g_end} over the course")
        except Exception as e:  # noqa: BLE001
            log("course totals not read:", repr(e))
    ok = res.get("arrive") and res.get("terrace") and res.get("jump") and res.get("course_walk") and res.get("course_gems")
    log(f"PROOF mesa-play {'PASS' if ok else 'FAIL'} {res}")
    (d.OUT / f"{name}-views.json").write_text(json.dumps(views, indent=1))


def play_course(pad, drv, s, log, shot, res, name, rep):
        # the terraces: SoI's start terrace, a terrace down, and a jump back up one 24-z step (mesa_route.py)
        route = json.loads((HERE.parents[1] / "data/soi/ps1/mesa-route.json").read_text())
        first = 0
        import os
        if os.environ.get("MESA_TELEPORT"):          # diagnostics: start the course at a waypoint (Spyro moved there)
            first = int(os.environ["MESA_TELEPORT"])
            wp = route["waypoints"][first - 1]
            pad.g.write(d.SPYRO, struct.pack("<iii", wp[0], wp[1], wp[2] + 300))
            pad.run(0, 30)
            log(f"teleported to waypoint {first - 1}: {drv.pos()}")
        for k, wp in enumerate(route["waypoints"]):
            if k < first:
                continue
            ok, t = drv.goto(wp, ticks=160, tol=500)
            log(f"waypoint {k} {wp}: {'reached' if ok else 'not reached'} in {t} ticks at {drv.pos()}")
            if k == len(route["waypoints"]) // 2:
                shot(pad, "terraces", 0.3)
        lo, up = route["step_lower"], route["step_upper"]
        ok, t = drv.goto(lo, ticks=160, tol=300)
        pad.run(0, 15)
        zl = drv.pos()[2]
        log(f"at the step: {'reached' if ok else 'not reached'} {drv.pos()} (lower {lo}, upper {up})")
        zs = []
        b = d.buttons(drv.dpad_for(up))
        pad.run(b | d.buttons("CROSS"), 16, on_tick=lambda _t: zs.append(drv.pos()[2]))
        pad.run(b, 12, on_tick=lambda _t: zs.append(drv.pos()[2]))
        pad.run(0, 25)
        zf = drv.pos()[2]
        res["terrace"] = zf - zl > 700
        log(f"TERRACE {'PASS' if res['terrace'] else 'FAIL'}: z {zl} -> {zf} ({(zf - zl) / 1024:.2f} world units; "
            f"one terrace = 1.0), peak {max(zs) if zs else None}")
        shot(pad, "on-top", 0.5)
        g0 = rd(pad, GEMS, "<i")[0]
        p0 = drv.pos()
        # the nearest gem from the encoder's placement (objects 'placed' are gems from the SoI list in value order)
        gems = rep.get("gem_positions") or []
        target = min(gems, key=lambda q: math.dist(q[:2], p0[:2])) if gems else (p0[0] + 3000, p0[1], p0[2])
        ok, t = drv.goto(target)
        p1 = drv.pos()
        pad.run(0, 30)
        g1 = rd(pad, GEMS, "<i")[0]
        walked = math.dist(p0[:2], p1[:2]) / 1024
        res["walk"] = walked > 1.0
        log(f"WALK {'PASS' if res['walk'] else 'FAIL'}: {walked:.2f} world units in {t} ticks, {p0} -> {p1}")
        res["gem"] = g1 > g0
        log(f"GEM {'PASS' if res['gem'] else 'FAIL'}: gem total {g0} -> {g1} (target {target})")
        shot(pad, "gem", 0.4)
        # a jump: hold CROSS while walking forward; z must rise above the start height and Spyro must land
        z0 = drv.pos()[2]
        zs = []
        pad.run(d.buttons("UP+CROSS"), 12, on_tick=lambda _t: zs.append(drv.pos()[2]))
        shot(pad, "jump", 0.0)
        pad.run(0, 40, on_tick=lambda _t: zs.append(drv.pos()[2]))
        rise = (max(zs) - z0) / 1024
        res["jump"] = rise > 0.4
        log(f"JUMP {'PASS' if res['jump'] else 'FAIL'}: rose {rise:.2f} world units, z {z0} -> {drv.pos()[2]}")
        log("state after", pad.state())
        shot(pad, "end", 0.5)


if __name__ == "__main__":
    main(*sys.argv[1:])
