"""Hidden DuckStation runs for the Season of Ice PS1 mod (TEST FIXTURE: input and inspection over GDB).

DuckStation (portable copy, D:/Theia-tools/duckstation) is started on a separate Win32 desktop, so its windows
exist (Vulkan needs a real surface) but are never on the user's desktop. Its GDB server (settings.ini [Debug]
EnableGDBServer / GDBServerPort 2345; this build has no PINE server) gives RAM read / write. Only processes this
module started are stopped. Audio stays on (user rule).

    python tools/soi/ds_harness.py boot CUE [SECONDS]     # boot hidden, report the GDB link and a RAM probe
    python tools/soi/ds_harness.py probe CUE              # + a breakpoint stop at 8001271c and an F10 screenshot try

Pad fixture plan (TEST FIXTURE, not yet run): break at 8001271c (the controller logic) every frame and write the
16-byte VBlank samples at 0x8006a99c + 16 * k (k = 1..ticks; held, pressed, released, analog bytes 7f 7f centred);
button bits in the game's order: L2 1, R2 2, L1 4, R1 8, Triangle 0x10, Circle 0x20, Cross 0x40, Square 0x80,
Select 0x100, Start 0x800, Up 0x1000, Right 0x2000, Down 0x4000, Left 0x8000 (8001271c tests 0xf000 / 0xf0ff).
Spyro's position: 0x80069ff0 (x, y, z words).
"""
from __future__ import annotations

import ctypes
import ctypes.wintypes as wt
import socket
import struct
import sys
import time
from pathlib import Path

EXE = Path(r"D:\Theia-tools\duckstation\duckstation-qt-x64-ReleaseLTCG.exe")
DESKTOP = f"TheiaDuckHidden{int(time.time())}"   # a fresh desktop per run
PORT = 2345

k32, u32 = ctypes.windll.kernel32, ctypes.windll.user32


class STARTUPINFOW(ctypes.Structure):
    _fields_ = [("cb", wt.DWORD), ("lpReserved", wt.LPWSTR), ("lpDesktop", wt.LPWSTR), ("lpTitle", wt.LPWSTR),
                ("dwX", wt.DWORD), ("dwY", wt.DWORD), ("dwXSize", wt.DWORD), ("dwYSize", wt.DWORD),
                ("dwXCountChars", wt.DWORD), ("dwYCountChars", wt.DWORD), ("dwFillAttribute", wt.DWORD),
                ("dwFlags", wt.DWORD), ("wShowWindow", wt.WORD), ("cbReserved2", wt.WORD),
                ("lpReserved2", ctypes.c_void_p), ("hStdInput", wt.HANDLE), ("hStdOutput", wt.HANDLE),
                ("hStdError", wt.HANDLE)]


class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [("hProcess", wt.HANDLE), ("hThread", wt.HANDLE), ("dwProcessId", wt.DWORD), ("dwThreadId", wt.DWORD)]


SHOW = 4   # SW_SHOWNOACTIVATE: visible on the hidden desktop only (UIA and the renderer see it; the user never does)


def start(cue: str, extra=()):
    """Start DuckStation (-batch -nogui) on the hidden desktop; returns (process handle, pid)."""
    u32.CreateDesktopW.restype = wt.HANDLE
    desk = u32.CreateDesktopW(DESKTOP, None, None, 0, 0x10000000, None)     # GENERIC_ALL
    if not desk:
        raise OSError(ctypes.get_last_error())
    si = STARTUPINFOW()
    si.cb = ctypes.sizeof(si)
    si.lpDesktop = DESKTOP
    si.dwFlags = 1          # STARTF_USESHOWWINDOW
    si.wShowWindow = SHOW   # the desktop itself is never shown
    pi = PROCESS_INFORMATION()
    cmd = " ".join([f'"{EXE}"', "-batch", "-nogui", *extra, "--", f'"{cue}"'])
    if not k32.CreateProcessW(None, ctypes.c_wchar_p(cmd), None, None, False, 0x08000000, None, str(EXE.parent),
                              ctypes.byref(si), ctypes.byref(pi)):
        raise OSError(ctypes.get_last_error())
    return pi.hProcess, pi.dwProcessId, desk


def stop(handle):
    if k32.WaitForSingleObject(handle, 0) != 0:
        k32.TerminateProcess(handle, 0)
        k32.WaitForSingleObject(handle, 10000)


def spawn_on_desktop(cmd: str, cwd: str | None = None):
    """Start any helper process on the hidden desktop (it sees only that desktop's windows)."""
    si = STARTUPINFOW()
    si.cb = ctypes.sizeof(si)
    si.lpDesktop = DESKTOP
    si.dwFlags = 1
    si.wShowWindow = 0
    pi = PROCESS_INFORMATION()
    if not k32.CreateProcessW(None, ctypes.c_wchar_p(cmd), None, None, False, 0x08000000, None, cwd,
                              ctypes.byref(si), ctypes.byref(pi)):
        raise OSError(ctypes.get_last_error())
    return pi.hProcess


def uia_dump(timeout_ms: int = 30000) -> str:
    """Read every window on the hidden desktop through UI Automation (Qt dialogs have no native children)."""
    out = Path(__file__).resolve().parents[2] / "tmp" / "soi4" / f"uia-{int(time.time() * 1000)}.txt"
    out.parent.mkdir(parents=True, exist_ok=True)
    ps1 = Path(__file__).with_name("ds_uia_dump.ps1")
    h = spawn_on_desktop(f'powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "{ps1}" '
                         f'-Out "{out}"')
    k32.WaitForSingleObject(h, timeout_ms)
    stop(h)
    return out.read_text(encoding="utf-8", errors="replace") if out.exists() else "(no UIA output)"


def window_titles():
    res = []
    for w in desktop_windows():
        buf = ctypes.create_unicode_buffer(256)
        u32.GetWindowTextW(w, buf, 256)
        cls = ctypes.create_unicode_buffer(256)
        u32.GetClassNameW(w, cls, 256)
        res.append((w, buf.value, cls.value, bool(u32.IsWindowVisible(w))))
    return res


def diag(cue: str, seconds: float = 25.0):
    """Boot hidden, watch the desktop's windows; if an 'Error' dialog appears, read it through UIA."""
    h, pid, _ = start(cue)
    print("pid", pid, "desktop", DESKTOP)
    try:
        t0 = time.time()
        seen = set()
        while time.time() - t0 < seconds:
            if k32.WaitForSingleObject(h, 0) == 0:
                code = wt.DWORD()
                k32.GetExitCodeProcess(h, ctypes.byref(code))
                print("process exited, code", code.value)
                break
            ts = [(t, c, v) for _, t, c, v in window_titles() if t]
            for x in ts:
                if x not in seen:
                    seen.add(x)
                    print(f"{time.time() - t0:5.1f}s window", x)
            if any(t in ("Error", "DuckStation") for t, _, _ in ts) and time.time() - t0 > 3:
                time.sleep(0.5)
                print(uia_dump())
                break
            time.sleep(0.5)
        try:
            s = socket.create_connection(("127.0.0.1", PORT), timeout=1)
            print("gdb port open")
            s.close()
        except OSError:
            print("gdb port closed")
    finally:
        stop(h)


class Gdb:
    """Minimal GDB remote protocol client (memory read / write, continue, interrupt)."""

    def __init__(self, port=PORT, timeout=60.0):
        t = time.time()
        while True:
            try:
                self.s = socket.create_connection(("127.0.0.1", port), timeout=2)
                break
            except OSError:
                if time.time() - t > timeout:
                    raise
                time.sleep(0.5)
        self.s.settimeout(5)

    def _send(self, body: str) -> str:
        pkt = f"${body}#{sum(body.encode()) & 255:02x}".encode()
        self.s.sendall(pkt)
        return self._recv()

    def _recv(self) -> str:
        buf = b""
        while True:
            buf += self.s.recv(65536)
            i = buf.find(b"$")
            j = buf.find(b"#", i + 1)
            if i >= 0 and j >= 0 and len(buf) >= j + 3:
                self.s.sendall(b"+")
                return buf[i + 1: j].decode(errors="replace")

    def read(self, addr: int, size: int) -> bytes:
        out = b""
        while size:
            n = min(size, 0x800)
            r = self._send(f"m{addr:x},{n:x}")
            out += bytes.fromhex(r)
            addr += n
            size -= n
        return out

    def write(self, addr: int, data: bytes) -> str:
        return self._send(f"M{addr:x},{len(data):x}:{data.hex()}")

    def close(self):
        try:
            self.s.close()
        except OSError:
            pass


def boot(cue: str, seconds: float = 20.0):
    h, pid, _ = start(cue)
    print("pid", pid, "(hidden desktop)")
    try:
        g = Gdb(timeout=seconds)
        print("gdb connected")
        print("exe header at 0x80010000:", g.read(0x80010000, 16).hex())
        g.close()
    finally:
        stop(h)


# ------------------------------------------------------------------ breakpoints, pad fixture, screenshots
def gdb_break(g: Gdb, addr: int):
    return g._send(f"Z0,{addr:x},4")


def gdb_cont_until_stop(g: Gdb, timeout=10.0) -> str:
    g.s.sendall(f"$c#{ord('c') & 255:02x}".encode())
    g.s.settimeout(timeout)
    return g._recv()


def desktop_windows():
    out = []
    u32.OpenDesktopW.restype = wt.HANDLE
    d = u32.OpenDesktopW(DESKTOP, 0, False, 0x0001 | 0x0040)    # DESKTOP_READOBJECTS | DESKTOP_ENUMERATE
    cb = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)(lambda h, l: out.append(h) or True)
    u32.EnumDesktopWindows(d, cb, 0)
    return out


def post_key(hwnd, vk: int):
    WM_KEYDOWN, WM_KEYUP = 0x100, 0x101
    sc = u32.MapVirtualKeyW(vk, 0)
    u32.PostMessageW(hwnd, WM_KEYDOWN, vk, 1 | (sc << 16))
    time.sleep(0.08)
    u32.PostMessageW(hwnd, WM_KEYUP, vk, 1 | (sc << 16) | 0xC0000000)


def probe(cue: str):
    """Boot retail hidden, stop at the controller logic once, read state, try an F10 screenshot."""
    shots = EXE.parent / "screenshots"
    before = set(shots.glob("*")) if shots.exists() else set()
    h, pid, _ = start(cue)
    try:
        g = Gdb(timeout=30)
        time.sleep(1)
        print("break:", gdb_break(g, 0x8001271C))
        for k in range(3):
            r = gdb_cont_until_stop(g, 30)
            print("stop", k, r[:40], "scene byte 80066f90:", g.read(0x80066F90, 4).hex(),
                  "spyro 80069ff0:", g.read(0x80069FF0, 12).hex())
        print("unbreak:", g._send("z0,8001271c,4"))
        g.s.sendall(b"$c#63")
        time.sleep(8)
        wins = desktop_windows()
        titles = []
        for w in wins:
            buf = ctypes.create_unicode_buffer(256)
            u32.GetWindowTextW(w, buf, 256)
            titles.append((w, buf.value, bool(u32.IsWindowVisible(w))))
        print("windows on the hidden desktop:", [(t, v) for _, t, v in titles if t][:6])
        for w, t, v in titles:
            if t:
                post_key(w, 0x79)      # VK_F10
        time.sleep(3)
        after = set(shots.glob("*")) if shots.exists() else set()
        print("new screenshots:", [str(p) for p in after - before])
        g.close()
    finally:
        stop(h)


# ------------------------------------------------------------------ sessions, screenshots, logged proofs
SHOTS = EXE.parent / "screenshots"
OUT = Path(__file__).resolve().parents[2] / "data" / "soi" / "ps1" / "ds"


class Log:
    """Every proof writes a log file; PASS / FAIL lines are cited from it (release-integrity rule)."""

    def __init__(self, name: str):
        (OUT / "logs").mkdir(parents=True, exist_ok=True)
        self.path = OUT / "logs" / f"{name}-{time.strftime('%Y%m%d-%H%M%S')}.log"
        self.f = open(self.path, "w", encoding="utf-8")
        self("log", self.path)

    def __call__(self, *a):
        line = " ".join(str(x) for x in a)
        print(line, flush=True)
        self.f.write(time.strftime("%H:%M:%S ") + line + "\n")
        self.f.flush()


def game_window():
    """The emulator's display window (titled with the game name) on the hidden desktop."""
    for w, t, c, v in window_titles():
        if c.startswith("Qt") and t and not t.startswith("DuckStation") and t != "Error":
            return w, t
    return None, None


class Session:
    """One hidden DuckStation run; always stopped on exit (only this process)."""

    def __init__(self, cue: str, log: Log, boot_timeout=40.0, extra=()):
        global DESKTOP
        DESKTOP = f"TheiaDuckHidden{int(time.time() * 1000)}"
        self.log = log
        self.h, self.pid, self.desk = start(cue, extra)
        log("started pid", self.pid, "desktop", DESKTOP, "cue", cue)
        t0 = time.time()
        self.win = None
        while time.time() - t0 < boot_timeout:
            if k32.WaitForSingleObject(self.h, 0) == 0:
                raise RuntimeError("DuckStation exited during startup")
            ts = window_titles()
            if any(t in ("Error", "DuckStation") for _, t, _, _ in ts) and time.time() - t0 > 3:
                log("startup dialog:\n" + uia_dump())
                stop(self.h)
                raise RuntimeError("startup dialog")
            w, t = game_window()
            if w:
                self.win, self.title = w, t
                log(f"game window '{t}' after {time.time() - t0:.1f}s")
                break
            time.sleep(0.25)
        if not self.win:
            stop(self.h)
            raise RuntimeError("no game window")
        self.gdb = Gdb(timeout=20)
        log("gdb connected")
        # DuckStation's GDB server pauses the system when a client attaches: resume at once. Memory reads
        # are answered while running; pause() / resume() bracket anything that must see one frame.
        self.resume()

    def resume(self):
        self.gdb.s.sendall(b"$c#63")
        self.running = True

    def pause(self):
        self.gdb.s.sendall(bytes([3]))      # GDB interrupt
        self.gdb.s.settimeout(5)
        r = self.gdb._recv()
        self.running = False
        return r

    def alive(self):
        return k32.WaitForSingleObject(self.h, 0) != 0

    def screenshot(self, dest: Path, timeout=10.0):
        """F10 (the private profile's Screenshot hotkey) posted to the hidden display window; the PNG is copied."""
        SHOTS.mkdir(exist_ok=True)
        before = {p: p.stat().st_mtime for p in SHOTS.glob("*")}
        post_key(self.win, 0x79)
        t0 = time.time()
        while time.time() - t0 < timeout:
            new = [p for p in SHOTS.glob("*") if p not in before or p.stat().st_mtime != before[p]]
            if new:
                p = max(new, key=lambda q: q.stat().st_mtime)
                try:                                    # DuckStation may still hold the file open
                    data = p.read_bytes()
                except PermissionError:
                    time.sleep(0.2)
                    continue
                if not data.endswith(b"IEND\xaeB`\x82"):
                    time.sleep(0.2)
                    continue
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes(data)
                return dest
            time.sleep(0.2)
        return None

    def close(self):
        try:
            self.gdb.close()
        except Exception:
            pass
        stop(self.h)
        self.log("stopped pid", self.pid)

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()


def luma_stats(png: Path):
    from PIL import Image
    import numpy as np
    a = np.asarray(Image.open(png).convert("L"), dtype=np.float32)
    return a.shape, float(a.mean()), float(a.std())


def proof_boot2(cue: str, settle: float = 20.0, runs: int = 2):
    """Boots-twice proof: each run reaches the game window and GDB, then a frame is captured and checked non-blank."""
    log = Log("boot2")
    ok = 0
    for r in range(1, runs + 1):
        try:
            with Session(cue, log) as s:
                t0 = time.time()
                while time.time() - t0 < settle and s.alive():
                    time.sleep(0.5)
                pc = s.gdb.read(0x80069FF0, 12).hex()
                shot = s.screenshot(OUT / "shots" / f"boot2-run{r}.png")
                if shot:
                    shape, m, sd = luma_stats(shot)
                    good = sd > 4.0
                    log(f"run {r}: shot {shot} size {shape} mean {m:.1f} std {sd:.1f} spyro-pos {pc}")
                    log(f"BOOT RUN {r} {'PASS' if good else 'FAIL (blank frame)'}")
                    ok += good
                else:
                    log(f"BOOT RUN {r} FAIL (no screenshot)")
        except Exception as e:
            log(f"BOOT RUN {r} FAIL ({e})")
    log(f"PROOF boot-twice {'PASS' if ok == runs else 'FAIL'} ({ok}/{runs})")


# ------------------------------------------------------------------ pad input (TEST FIXTURE)
BUTTONS = dict(L2=1, R2=2, L1=4, R1=8, TRIANGLE=0x10, CIRCLE=0x20, CROSS=0x40, SQUARE=0x80, SELECT=0x100,
               START=0x800, UP=0x1000, RIGHT=0x2000, DOWN=0x4000, LEFT=0x8000)
LOGIC = 0x8001271C          # retail controller logic (candidate_LogicController), once per game tick
TICKS = 0x80067160          # VBlanks since the last logic tick (written by the VBlank handler)
SAMPLES = 0x8006A99C        # VBlank pad samples, 16 bytes each at +16k (k = 1 newest .. 4 oldest)
GAME_STATE, LEVEL, DEST, SPYRO = 0x800681C8, 0x80066F54, 0x8006715C, 0x80069FF0


def buttons(spec: str) -> int:
    v = 0
    for b in spec.replace("+", " ").split():
        v |= BUTTONS[b.upper()]
    return v


class Pad:
    """TEST FIXTURE: the game's own pad samples are overwritten at every logic tick.

    A GDB breakpoint at 8001271c stops the game once per tick; the fixture writes `held` (and the pressed /
    released edges against the previous tick) into every VBlank sample the tick will read, then continues.
    """

    def __init__(self, s: Session):
        self.s, self.g = s, s.gdb
        self.prev = 0
        self.tick = 0
        if s.running:
            s.pause()
        r = self.g._send(f"Z0,{LOGIC:x},4")
        s.log("pad: breakpoint at 8001271c:", r)

    def _wait_stop(self, limit=30.0, probe=5.0):
        """Wait for the breakpoint. Every `probe` seconds without a stop, read the game's tick timer (0x8006700c)
        and Spyro while it runs (DuckStation answers reads while running), so a stall says whether the game is
        still ticking (the breakpoint is not reached) or frozen."""
        t0 = time.time()
        self.stall = []
        while True:
            self.g.s.settimeout(probe)
            try:
                return self.g._recv()
            except socket.timeout:
                if time.time() - t0 >= limit:
                    raise
                try:
                    pkt = lambda body: f"${body}#{sum(body.encode()) & 255:02x}".encode()  # noqa: E731
                    self.g.s.sendall(pkt(f"m{0x8006700C:x},4"))
                    self.g.s.settimeout(2)
                    r = self.g._recv()
                    self.stall.append((round(time.time() - t0, 1), r))
                except Exception as e:  # noqa: BLE001
                    self.stall.append((round(time.time() - t0, 1), repr(e)))

    def run(self, held: int, ticks: int, on_tick=None, repeat: bool = False):
        """Hold `held` for `ticks` logic ticks (`repeat`: report it as newly pressed on every tick, for menus)."""
        for _ in range(ticks):
            n = struct.unpack("<I", self.g.read(TICKS, 4))[0]
            n = max(1, min(n, 4))
            pressed, released = held & ~self.prev, self.prev & ~held
            if repeat:
                pressed = held
            for k in range(1, n + 1):
                first = k == n                      # the oldest sample carries the edge
                samp = struct.pack("<III", held, pressed if first else 0, released if first else 0) + b"\x7f" * 4
                self.g.write(SAMPLES + 16 * k, samp)
            self.prev = held
            self.tick += 1
            if on_tick:
                on_tick(self.tick)
            self.g.s.sendall(b"$c#63")
            self._wait_stop()

    def release(self):
        """Remove the breakpoint and let the game run freely (the emulated pad then reads no buttons)."""
        self.g._send(f"z0,{LOGIC:x},4")
        self.g.s.sendall(b"$c#63")
        self.s.running = True

    def rearm(self):
        """Pause, set the breakpoint again and run to it, so the next tick's samples are written at 8001271c."""
        self.s.pause()
        self.g._send(f"Z0,{LOGIC:x},4")
        self.g.s.sendall(b"$c#63")
        self._wait_stop()
        self.prev = 0

    def shot(self, dest: Path, free_run: float = 0.6):
        """A frame needs the emulator running: release for `free_run` seconds, capture, re-arm."""
        self.release()
        t0 = time.time()
        while time.time() - t0 < free_run:
            time.sleep(0.05)
        p = self.s.screenshot(dest)
        self.rearm()
        return p

    def state(self):
        gs = struct.unpack("<I", self.g.read(GAME_STATE, 4))[0]
        lv = struct.unpack("<I", self.g.read(LEVEL, 4))[0]
        pos = struct.unpack("<iii", self.g.read(SPYRO, 12))
        return gs, lv, pos


if __name__ == "__main__":
    if sys.argv[1] == "probe":
        probe(sys.argv[2])
    elif sys.argv[1] == "boot2":
        proof_boot2(sys.argv[2])
    elif sys.argv[1] == "diag":
        diag(sys.argv[2], float(sys.argv[3]) if len(sys.argv) > 3 else 25.0)
    elif sys.argv[1] == "boot":
        boot(sys.argv[2], float(sys.argv[3]) if len(sys.argv) > 3 else 20.0)
