"""Ship the Season of Ice Market Mesa test disc as a patcher for the user's own retail Spyro 2 (USA) BIN/CUE:
Theia dist/spyro2-soi-marketmesa-<label>-<date>.zip (+ .sha256).

    python tools/soi/package_patcher.py --label test05 --runtime-log LOG --moby-oracle-log LOG [--pc-log LOG] --no-retire

The house patcher (Theia tools/rac1_mp/package_patcher.py's flow) with Spyro 2 / Season of Ice wording, and two
BIN/CUE-aware copies of its parts in tools/soi/patcher/: the ImGui front end (main.cpp, built privately in
build-soi/patcher-gui: patch.json names the disc type) and the PowerShell backend (Patch-Spyro2.ps1: the source may
be the retail .cue or .bin; the output .bin gets a matching .cue). Steps, each under a wall-clock limit, big files on D:
 1. Inputs: the target disc against its report, slot 17 read from that exact disc and freshly checked, completed
    private runtime evidence bound to its full SHA-256; the retail BIN and house decoder by pinned SHA-256.
    The image has no test fixture
    (the pad fixture runs over DuckStation's GDB server at play time only).
 2. Encode retail -> mod with the house xdelta3 (-e -9 -A -S lzma; slot 17 grows in place
    and the rest of WAD.WAD moves with it, so the default source window sees every match).
 3. Decode it back, streamed through SHA-256 -> verification/backend-roundtrip.json.
 4. Payload: GUI + runtime DLLs, Patch-Spyro2.ps1 / .bat, patch.json, README, licences, GUI source, evidence.
 5. SHA256SUMS.txt; the zip (fixed timestamps, the patch stored).
 6. End to end from the zip on D:: the GUI hidden --auto (source = the retail .cue) and the .bat (source = the .bin);
    both outputs must hash to the mod disc and carry the matching .cue; outputs and the extraction are deleted.
 7. Retire only exact --retire paths after end-to-end verification (their .sha256 stay); no wildcard retirement.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import threading
import time
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[1]
THEIA = Path("C:/Users/Shadow/Documents/Theia")
RETAIL_BIN = PROJECT / "Spyro 2 - Riptos Rage (USA)" / "Spyro 2 - Ripto's Rage! (USA).bin"
RETAIL_CUE = RETAIL_BIN.with_suffix(".cue")
RETAIL_ID = dict(bytes=565371408, sha256="b3fd9fdb24b115fee4583dbe3c36ab788fd8679fb4592dbec8736392dec64551")
FRAMEWORK = THEIA / "dist/uya-hoverboots-imgui-20261029-r5"     # the house decoder, licences, runtime DLLs
DECODER_SHA = "6e812b38484d0c764291779fadee83ffbe2eceaccb4e3a21f8a043a02654a01e"
GUI = PROJECT / "build-soi/patcher-gui/out/Spyro2-SoI-Patcher.exe"
GUI_SRC = PROJECT / "build-soi/patcher-gui/src"
BACKEND = HERE / "patcher/Patch-Spyro2.ps1"
SAFE_DELETE = THEIA / "tools/claude_guard/safe_delete.py"
DIST = THEIA / "dist"
WORK = Path("D:/Theia-staging/spyro2-mod/patcher")
EXE, SCRIPT, BAT = "Spyro2-SoI-Patcher.exe", "Patch-Spyro2.ps1", "Patch-Spyro2.bat"
CREATE_NO_WINDOW = 0x08000000
RUNTIME = ("msvcp140.dll", "msvcp140_atomic_wait.dll", "vcruntime140.dll", "vcruntime140_1.dll", "vcruntime140_threads.dll")


def sha(path):
    with open(path, "rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()


def say(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


def safe_delete(*paths, recursive=False):
    paths = [str(p) for p in paths if Path(p).exists()]
    if paths:
        subprocess.run([sys.executable, str(SAFE_DELETE), *(["-r"] if recursive else []), *paths], check=True)


def run_limited(cmd, limit, **kw):
    return subprocess.run(cmd, timeout=limit, creationflags=CREATE_NO_WINDOW, **kw)


def run_hidden_gui(cmd,limit,cwd):
    """Use a private Win32 desktop even for the GUI's hidden automation mode."""
    import ctypes as c
    import ctypes.wintypes as w
    import uuid
    import ds_harness as desktop
    desktop.k32.WaitForSingleObject.argtypes=[w.HANDLE,w.DWORD]
    desktop.k32.TerminateProcess.argtypes=[w.HANDLE,w.UINT]
    desktop.k32.CloseHandle.argtypes=[w.HANDLE]
    desktop.k32.GetExitCodeProcess.argtypes=[w.HANDLE,c.POINTER(w.DWORD)]
    desktop.u32.CreateDesktopW.restype=w.HANDLE
    desktop.u32.CloseDesktop.argtypes=[w.HANDLE]
    desktop.DESKTOP='SoiPatcherPrivate'+uuid.uuid4().hex
    desk=desktop.u32.CreateDesktopW(desktop.DESKTOP,None,None,0,0x10000000,None)
    if not desk: raise c.WinError()
    handle=None
    try:
        handle=desktop.spawn_on_desktop(subprocess.list2cmdline(cmd),str(cwd))
        say('PRIVATE GUI DESKTOP',desktop.DESKTOP,'exe',cmd[0])
        assert desktop.k32.WaitForSingleObject(handle,int(limit*1000))==0,'private patcher deadline'
        code=w.DWORD(); assert desktop.k32.GetExitCodeProcess(handle,c.byref(code))
        return subprocess.CompletedProcess(cmd,code.value,'','')
    finally:
        if handle:
            desktop.stop(handle); desktop.k32.CloseHandle(handle)
        desktop.u32.CloseDesktop(desk)


def texts(label, mb, expanded=False):
    t = dict(
        window=f"Spyro 2: Season of Ice - Market Mesa patcher ({label}, TEST build)", game="SPYRO 2: SEASON OF ICE",
        heading="MARKET MESA TEST PATCHER",
        tag=f"USA / SCUS-94425 (BIN/CUE)    |    Market Mesa {label} (TEST)",
        summary="Puts Market Mesa, Season of Ice's first level rebuilt in 3D, into your retail Spyro 2: start a new "
                "game and Spyro arrives in Market Mesa instead of Glimmer.",
        scope=f"Creates a separate BIN (about {mb} MB) and its CUE. Your retail BIN/CUE stays unchanged.",
        button="Create Market Mesa BIN/CUE",
        step5="5. Open the new .cue in DuckStation (or your PS1 setup), start a NEW GAME, and Spyro arrives in Market "
              "Mesa. It also runs in the Spyro 2 PC build (see README). Windows PowerShell 5.1 and Direct3D 11 are "
              "required.",
        source_prompt="Select your original Spyro 2 (USA) .cue or .bin to begin.",
        source_hint="USA SCUS-94425 BIN/CUE (MODE2/2352); exact source hash checked before patching.",
        source_dialog="Select your original Spyro 2: Ripto's Rage (USA) .cue or .bin",
        output_name=f"Spyro2-SoI-MarketMesa-{label}.bin",
        step2="2. Select your original USA Spyro 2: Ripto's Rage .cue (or its .bin). The patcher checks the complete "
              "image, not just its filename.",
        step3=f"3. Choose a new .bin filename on a local drive with room for it (about {mb} MB); a matching .cue is "
              "written beside it. Existing files are never replaced. Accented/non-ASCII paths are not supported by "
              "this decoder.",
        building="Building Market Mesa BIN", done="SUCCESS  /  Market Mesa BIN and CUE verified", script=SCRIPT,
        console_title=f"Spyro 2: Season of Ice - Market Mesa {label} (TEST) - retail USA SCUS-94425 patcher",
        source_label="USA Spyro 2: Ripto's Rage (SCUS-94425)",
        source_rule="Use the unmodified USA SCUS-94425 BIN (MODE2/2352), not CHD/PBP/ECM, PAL, or a patched image.",
        temp_prefix=".spyro2-soi-", ascii_example="C:\\Spyro2-Patch",
        output_ext="bin", disc_word="BIN", file_filter="PlayStation disc image (*.cue;*.bin)",
        source_pattern="*.cue;*.bin")
    if expanded:
        t.update(heading='MARKET MESA EXPANSION PATCHER',
                 summary='Adds Market Mesa as a separate level reached through a new special portal in Summer Forest. Original Glimmer and the rest of Spyro 2 remain present.',
                 step5='5. Open the new .cue in your PS1 setup. Start normally in Glimmer; visit Summer Forest and enter the new Market Mesa portal. Read README for current PC compatibility and limits.')
    for k, v in t.items():
        assert v.isascii(), k
    return t


BAT_TEXT = """@echo off
setlocal
title Spyro 2 Season of Ice - Market Mesa patcher
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0{script}" %*
set "patch_exit=%ERRORLEVEL%"
echo.
if not "%patch_exit%"=="0" echo Patching failed. Read the error above.
pause
exit /b %patch_exit%
"""


def readme(label, date, target, patch, build, notes_extra=""):
    return f"""# Spyro 2: Season of Ice - Market Mesa {label} patcher ({date}) - TEST BUILD

This package turns **your own retail Spyro 2: Ripto's Rage (USA)** disc image (BIN/CUE) into a **test build** of the
Season of Ice port: **Market Mesa**, the first level of *Spyro: Season of Ice* (GBA), rebuilt in 3D and played in the
original Spyro 2 engine. It holds a standard xdelta3 / VCDIFF patch and a Windows patcher; your retail image supplies
the game data and is never modified.

**This is a TEST build**: one level, with stand-ins for the characters Spyro 2 does not have (see below).

## What you need

- Your retail **Spyro 2: Ripto's Rage (USA, SCUS-94425)** as an uncompressed BIN/CUE (one MODE2/2352 track):
  the `.bin` is {RETAIL_ID['bytes']:,} bytes, SHA-256 `{RETAIL_ID['sha256']}`. PAL, CHD / PBP / ECM and already
  patched images are refused.
- Windows 10 / 11 x64 (Windows PowerShell 5.1 and Direct3D 11, both built in).
- About {round((target['bytes'] + (128 << 20)) / 2 ** 20)} MB free on the output drive.

## Patch with the app

1. Extract **the complete ZIP** into one folder (plain ASCII path, e.g. `C:\\Spyro2-Patch`).
2. Open **{EXE}**. No installation or administrator access is needed.
3. Click **Browse** to select your retail `.cue` (or `.bin`), or drag it onto the window.
4. Click **Save as** to choose a new `.bin` filename. A `.cue` with the same name is written beside it.
5. Click **Create Market Mesa BIN/CUE** and wait for **SUCCESS / Market Mesa BIN and CUE verified**.

The source is held read-only and existing files are never replaced. The backend checks the full source, patch, decoder
and output SHA-256 values; only a verified output is moved to your filename. Its window stays hidden.

## Patch from the console (fallback)

Double-click **{BAT}** and paste the paths when asked, or run:

    powershell -ExecutionPolicy Bypass -File {SCRIPT} -Source "C:\\path\\to\\Spyro 2 - Ripto's Rage! (USA).cue" -Output "C:\\path\\to\\Spyro2-SoI-MarketMesa-{label}.bin"

A `.patch-receipt.json` with the hashes is written beside the new BIN.

## Play (PS1 emulator)

Open the new **.cue** in DuckStation (or another PS1 emulator / your PS1 setup). At the title press START; with no
memory card choose **Start Game** (with a card, start a **new game**). After the two opening cutscenes (START skips
them) Spyro arrives in **Market Mesa**: the new game's first level is Glimmer's slot, which now holds Market Mesa.
Saves made in it load as "Glimmer" in the save menus.

## Play in the Spyro 2 PC build

The Spyro 2 PC reimplementation reads the same disc. Open the patched `.cue` as the game copy, or start Market Mesa
directly:

    spyro_workbench.exe --game "C:\\path\\to\\Spyro2-SoI-MarketMesa-{label}.cue" --slot 17 --play

(`Play Market Mesa (Season of Ice).cmd` in the PC project is pinned to the verified {label} disc and accepts a CUE argument.)

## What is in it

- **Market Mesa's layout from Season of Ice's own data**: its 283 solids and 27 terrace heights, rebuilt as a 3D
  level: pink paving with border tiles, white plaster buildings with pale-blue slate roofs and copings, framed wooden
  hatches, doors and windows, sandstone cliffs with a snow foot, rock undersides, bridges with rails and arches,
  pagoda pines, market stalls, cloth racks, flagpoles and banners. {build['near_faces']:,} faces in {build['regions']} regions,
  {build['materials']} materials from {build['texture_slots']} textures, {build['collision_triangles']:,} collision triangles.
- **SoI's loose gems**: all 110, with the source denominations (56 x 1, 37 x 2, 12 x 5, 5 x 10 = 240).
  Original retail object indices and data pointers stay intact; 19 added gems have separate parameter blocks.
- **SoI's other objects**: 14 of the two Rhynoc families' 21 positions (Glimmer's available lizards stand in), its 15
  gem containers (Glimmer's baskets), Zoe and the Return Home portal near the start.
- Glimmer's sky, music, sounds and gameplay code. Moneybags retains his original index and type, with retail's
  inactive removed state 0xfd. The original loader preserves it and the activity builder/dispatcher skip it,
  including with the bridge-purchased flag set. Other unsupported actors remain parked out of reach.
- **Distant terrain**: all 5,246 non-decor terrain faces use their exact near positions/winding and material B colours
  instead of averaged clusters. Walls and roofs remain in the far mesh. Full 32 x 32 texture descriptors and 16 far
  palette rows follow the retail renderer's layout. Palette fading is a measured fit, not an exact recovered formula.
- **Visibility**: a conservative all-visible tree restores the original loader's 0x2c000 far distance, without
  executable changes or invented occlusion.

## Still stand-ins or unfinished (TEST)

- Rhynocs: Glimmer's 14 lizards cover 14 of 21 source positions until the Spyro 3 Rhynocs are imported.
  Bianca and Grendor are not in yet.
- Containers are Glimmer's baskets with their retail contents; SoI's ice barrier and critters are not placed.
- Glimmer's other objects (Pogo and the gem-cutters, lamps, gates) are parked out of reach, not removed.
- Decor (trees, stalls, flags) remains near-only on PS1; separate distant scenery mesas remain omitted.
  The existing texture allocation still maps 641 partial-rectangle faces to whole tiles; source v2g geometry is preserved.
- PS1 far terrain uses untextured flat shading; palette fading remains a measured fit. The saved-purchase check
  is an automated RAM fixture, not a complete old-save playthrough.
- Automated structural and private emulator checks were run. Manual gameplay and visual acceptance are left to the user.
- Falling off the islands has no catch net yet; the level's talisman / completion is Glimmer's.
- Only Glimmer's slot is replaced: the rest of Spyro 2 is the retail game.
{notes_extra}
## Identities

| | Bytes | SHA-256 |
|---|---|---|
| Retail source `.bin` (USA SCUS-94425) | {RETAIL_ID['bytes']:,} | `{RETAIL_ID['sha256']}` |
| Patched `.bin` (Market Mesa {label}) | **{target['bytes']:,}** | `{target['sha256']}` |
| Patch `{patch['file']}` | {patch['bytes']:,} | `{patch['sha256']}` |
| Decoder `tools/xdelta3.exe` (xdelta 3.2.0) | | `{DECODER_SHA}` |

`verification/backend-roundtrip.json` records the patch decoded against the retail image to the exact disc;
`verification/disc.json` the disc's build report and static checks. Before release this ZIP was extracted and both
the app and the console script rebuilt the disc from the retail image; both outputs matched the SHA-256 above.
Static evidence is regenerated from slot 17 read from that exact target BIN and is bound to the archive SHA-256.
The backend moves a verified temporary file into place; failed partial files are retained with their exact path
for explicit cleanup. It never deletes files.

## Files

- `{EXE}`: the app (Dear ImGui). Its source is in `source/`; the Microsoft Visual C++ runtime DLLs sit beside it.
- `{SCRIPT}` / `{BAT}`: the patching backend (writes the `.bin` and its `.cue`) and its console launcher.
- `patch.json`: the identities above, read by the app and the script. `{patch['file']}`: the patch.
- `tools/xdelta3.exe`: the unmodified upstream xdelta 3.2.0 decoder.
- `THIRD-PARTY.md` and `licenses/`: the notices (xdelta: Apache 2.0; liblzma: 0BSD; BLAKE3; Dear ImGui: MIT).
- `verification/`, `SHA256SUMS.txt`: the evidence and every file's hash.
"""


def expansion_readme(label,date,target,patch,notes,pc_checked):
    pc=('The exact target also has the native PC load/capture evidence in verification/pc.log. '
        'Read the current feature notes for the supported launch and travel paths.' if pc_checked else
        'Native PC support for the added scene is not established by this package. '
        'This package\'s original-engine evidence applies to the PS1 ROM.')
    return f'''# Spyro 2: Market Mesa expansion {label} ({date}) — TEST BUILD

Adds **Market Mesa as a separate level**, reached through a new special portal
in **Summer Forest**. Original Glimmer and the other original level payloads are
retained. The full-game expansion uses normal menus, learned-ability purchases
and Return Home; the separate Mesa-only ROM's restrictions are absent.

This is a patch for your own **USA Spyro 2: Ripto's Rage (SCUS-94425)** BIN/CUE.
The original image remains unchanged. Use an uncompressed one-track MODE2/2352
BIN of {RETAIL_ID['bytes']:,} bytes, SHA-256 `{RETAIL_ID['sha256']}`.
Windows 10/11 x64, Windows PowerShell 5.1 and Direct3D11 are required for the
included patcher. Allow about {round((target['bytes']+(128<<20))/2**20)} MB free.

## Create the expanded BIN/CUE

1. Extract the entire ZIP to a plain ASCII path such as `C:\\Spyro2-Patch`.
2. Open **{EXE}**, select your retail `.cue` or `.bin`, and choose a new output `.bin`.
3. Click **Create Market Mesa BIN/CUE** and wait for the verified success message.
4. Open the generated `.cue` in your PS1 emulator or setup. Start the game normally
   in Glimmer, reach Summer Forest, and enter the new **Market Mesa** portal near
   the initial Summer Forest arrival area. Return Home brings Spyro back to it.

The script provides the same patch and hash checks:

    powershell -ExecutionPolicy Bypass -File {SCRIPT} -Source "C:\\path\\retail.cue" -Output "C:\\path\\MarketMesa-expanded.bin"

The source, patch, decoder and output identities are checked. Existing outputs
are refused. A matching CUE and `.patch-receipt.json` are written beside the BIN.

## PC compatibility

{pc}

{notes}

## Identities and automated evidence

| Item | Bytes | SHA-256 |
|---|---:|---|
| Retail source BIN | {RETAIL_ID['bytes']:,} | `{RETAIL_ID['sha256']}` |
| Expanded BIN | {target['bytes']:,} | `{target['sha256']}` |
| {patch['file']} | {patch['bytes']:,} | `{patch['sha256']}` |
| xdelta3 decoder | | `{DECODER_SHA}` |

`verification/` contains the feature manifest, exact-disc archive checks,
original-payload/portal checks, private original-engine runtime and music logs,
and streamed backend roundtrip. The ZIP is extracted before release and both
GUI and script rebuild the same target hash and matching CUE from retail.
Manual gameplay, menu and visual acceptance remain with the user.

`patch.json` describes the disc and front-end text; `SHA256SUMS.txt` covers the
package files. `source/`, `licenses/` and `THIRD-PARTY.md` retain the patcher
source and notices. The decoder is xdelta3 3.2.0; its provenance is recorded in
`verification/xdelta-provenance.json`. No game ROM is included in this ZIP.
'''


def make_zip(root, out, when):
    tmp = out.with_name(out.name + ".partial")
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for f in sorted(p for p in root.rglob("*") if p.is_file()):
            info = zipfile.ZipInfo(f"{root.name}/{f.relative_to(root).as_posix()}", when)
            info.compress_type = zipfile.ZIP_STORED if f.suffix == ".xdelta" else zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            with f.open("rb") as src, z.open(info, "w", force_zip64=True) as dst:
                shutil.copyfileobj(src, dst, 1 << 20)
    os.replace(tmp, out)
    d = sha(out)
    out.with_name(out.name + ".sha256").write_text(f"{d}  {out.name}\n")
    return d


def roundtrip(decoder, patch, expected, limit):
    say("roundtrip decode (streamed)")
    proc = subprocess.Popen([str(decoder), "-d", "-c", "-s", str(RETAIL_BIN), str(patch)], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, creationflags=CREATE_NO_WINDOW)
    timer = threading.Timer(limit, proc.kill)
    timer.start()
    h, n = hashlib.sha256(), 0
    try:
        while b := proc.stdout.read(1 << 22):
            h.update(b)
            n += len(b)
    finally:
        timer.cancel()
    err = proc.stderr.read().decode(errors="replace").strip()
    rc = proc.wait()
    got = dict(exit=rc, bytes=n, sha256=h.hexdigest(), streamed_without_output_file=True)
    if rc or n != expected["bytes"] or got["sha256"] != expected["sha256"]:
        raise SystemExit(f"roundtrip mismatch: {got} {err} != {expected}")
    return got


def verify_target(disc, report, work, archive_report=None, expanded_manifest=None, portal_report=None):
    """Read the actual Mesa slot from this BIN and rerun current checks.

    A stale PASS string has no role. Evidence binds target and readback hashes.
    """
    sys.path.insert(0,str(HERE.parent))
    import numpy as np
    import ps1disc
    target=dict(bytes=disc.stat().st_size,sha256=sha(disc))
    expanded=expanded_manifest is not None
    if (report.get('sha256')!=target['sha256'] or report.get('fixture') is not False
            or (not expanded and not report.get('readback_slot_equal'))):
        raise SystemExit('target identity / clean-disc report mismatch')
    if expanded and (archive_report is None or portal_report is None):
        raise SystemExit('expanded target requires its archive report and Summer portal report')
    if archive_report:
        build_report=json.loads(archive_report.read_text());source_archive=Path(build_report['archive'])
    else:
        source_archive=Path(report['archive'])
        build_report=json.loads(source_archive.with_name('mesa-ps1-report.json').read_text())
    if build_report.get('release_eligible') is False:
        raise SystemExit('experimental archive is not release eligible: '+ '; '.join(build_report.get('release_blockers',[])))
    model=ps1disc.DiscModel(disc.with_suffix('.cue'))
    original_exe=PROJECT/'data/disc/SCUS_944.25'
    executable=next(f for f in model.files if f.path=='SCUS_944.25')
    executable_bytes=bytes(model.raw[executable.lba:executable.lba+(executable.size+2047)//2048,
                                     24:24+2048].reshape(-1))[:executable.size]
    executable_sha=hashlib.sha256(executable_bytes).hexdigest()
    if executable_sha!=sha(original_exe):
        raise SystemExit('target executable differs from unmodified retail oracle')
    toc=bytes(model.raw[500,24:24+2048])
    import struct
    slot=199 if expanded else 17
    offset,size=struct.unpack_from('<II',toc,slot*8)
    assert offset%2048==0
    first=500+offset//2048
    payload=bytes(model.raw[first:first+(size+2047)//2048,24:24+2048].reshape(-1))[:size]
    archive_sha=hashlib.sha256(payload).hexdigest()
    expected_archive=(json.loads(expanded_manifest.read_text())['replacements']['199']['sha256']
                      if expanded else report.get('archive_sha256'))
    if archive_sha!=expected_archive or archive_sha!=build_report.get('archive_sha256'):
        raise SystemExit('readback archive does not match hash-bound build evidence')
    checkdir=work/('static-'+target['sha256'][:16])
    checkdir.mkdir(exist_ok=True)
    archive=checkdir/f'slot{slot}-mesa.bin'; archive.write_bytes(payload)
    (checkdir/'mesa-ps1-report.json').write_text(json.dumps(build_report,indent=1))
    result=run_limited([sys.executable,str(HERE/'mesa_ps1_check.py'),str(archive)],120,capture_output=True,text=True)
    checks=result.stdout+result.stderr
    (checkdir/'static-check.log').write_text(checks)
    if result.returncode or checks.splitlines()[-1:]!=['STATIC CHECK PASS'] or f'sha256 {archive_sha}' not in checks:
        raise SystemExit('current target static checks failed: '+str(checkdir/'static-check.log'))
    evidence=dict(target=target,archive_sha256=archive_sha,retail_executable_sha256=executable_sha,
                  static_log_sha256=sha(checkdir/'static-check.log'),
                  cue_sha256=sha(disc.with_suffix('.cue')),fixture=False,checker_sha256=sha(HERE/'mesa_ps1_check.py'))
    evidence['mesa_slot']=slot
    if expanded:
        variant_log=checkdir/'expanded-static-check.log'
        result=run_limited([sys.executable,str(HERE/'mesa_expansion_check.py'),str(disc.with_suffix('.cue')),
                            '--manifest',str(expanded_manifest),'--portal-report',str(portal_report),
                            '--out',str(checkdir/'expanded-static-proof.json')],120,capture_output=True,text=True)
        variant_log.write_text(result.stdout+result.stderr)
        if result.returncode or 'EXPANDED STATIC CHECK PASS '+target['sha256'] not in result.stdout:
            raise SystemExit('expanded original-payload/portal checks failed: '+str(variant_log))
        evidence['expanded_static_log']=str(variant_log)
        evidence['expanded_static_log_sha256']=sha(variant_log)
        evidence['expanded_manifest_sha256']=sha(expanded_manifest)
        evidence['portal_report_sha256']=sha(portal_report)
    say('TARGET STATIC PASS',json.dumps(evidence))
    return target,checks,evidence,build_report


def end_to_end(zip_path, package, work, target, limit, label):
    ext = work / f"verify-{package}"
    if ext.exists():
        raise SystemExit(f"{ext} exists")
    with zipfile.ZipFile(zip_path) as z:
        z.extractall(ext)
    pkg = ext / package
    rep = {}
    want_cue = None
    try:
        for how in ("gui", "script"):
            out = work / f"{package}-{how}.bin"
            cue = out.with_suffix(".cue")
            receipt = Path(str(out) + ".patch-receipt.json")
            safe_delete(out, cue, receipt)
            say(f"end to end: {how}")
            t0 = time.time()
            if how == "gui":
                shot = work / f"{package}-gui-success.png"
                cmd = [str(pkg / EXE), "--hidden", "--auto", "--source", str(RETAIL_CUE), "--output", str(out),
                       "--package", str(pkg), "--screenshot", str(shot)]
                r = run_hidden_gui(cmd,limit,pkg)
                log = ""
            else:
                cmd = ["cmd.exe", "/c", str(pkg / BAT), "-Source", str(RETAIL_BIN), "-Output", str(out), "-NonInteractive"]
                r = run_limited(cmd, limit, capture_output=True, text=True, stdin=subprocess.DEVNULL)
                log = r.stdout[-1500:]
            ok_file = out.is_file()
            want_cue = f'FILE "{out.name}" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n'
            got = dict(exit=r.returncode, seconds=round(time.time() - t0, 1),
                       bytes=out.stat().st_size if ok_file else None, sha256=sha(out) if ok_file else None,
                       cue_ok=cue.is_file() and cue.read_bytes() == want_cue.encode("ascii"),
                       receipt_passed=json.loads(receipt.read_text(encoding="utf-8-sig")).get("passed")
                       if receipt.is_file() else None)
            if log:
                got["log_tail"] = log
            rep[how] = got
            safe_delete(out, cue, receipt)
            if r.returncode or got["sha256"] != target["sha256"] or got["bytes"] != target["bytes"] \
                    or got["receipt_passed"] is not True or not got["cue_ok"]:
                raise SystemExit(f"end to end {how} FAILED: {got}")
            say(f"END TO END {how.upper()} PASS: sha256 {got['sha256']} = mod disc, cue ok ({got['seconds']} s)")
        shot = work / f"{package}-gui-success.png"
        if shot.exists():
            rep["gui_screenshot"] = str(shot)
    finally:
        safe_delete(ext, recursive=True)
    return rep


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--label", default="test01")
    ap.add_argument("--date", default=time.strftime("%Y%m%d"))
    ap.add_argument("--dest", type=Path, default=DIST)
    ap.add_argument("--no-retire", action="store_true")
    ap.add_argument('--retire',type=Path,action='append',default=[],help='Exact superseded ZIP; no wildcard retirement')
    ap.add_argument('--runtime-log',type=Path,required=True,help='Completed private check for the exact target SHA256')
    ap.add_argument('--pc-log',type=Path)
    ap.add_argument('--moby-oracle-log',type=Path,required=True)
    ap.add_argument('--disc-report',type=Path,default=PROJECT/'data/soi/ps1/mesa-disc-report.json')
    ap.add_argument('--disc',type=Path,help='Exact target BIN; otherwise use the labelled staging BIN')
    ap.add_argument('--archive-report',type=Path,help='Exact Mesa archive build report')
    ap.add_argument('--expanded-manifest',type=Path,help='Expanded full-game layout/replacement manifest')
    ap.add_argument('--portal-report',type=Path,help='Hash-bound Summer Forest portal report')
    ap.add_argument('--portal-runtime-log',type=Path,help='Completed exact-image new portal/return/cinematic/Glimmer regression')
    ap.add_argument('--music-evidence',type=Path,help='Hash-bound original/native score evidence for this exact disc')
    ap.add_argument('--release-notes',type=Path,help='Reviewed current feature/limitation notes replacing legacy test05 notes')
    ap.add_argument('--work',type=Path,default=WORK,help='Private packaging workspace; may use the project build tree')
    ap.add_argument("--encode-limit", type=int, default=900)
    ap.add_argument("--decode-limit", type=int, default=900)
    a = ap.parse_args()
    disc = (a.disc or Path(f"D:/Theia-staging/spyro2-mod/Spyro2-SoI-MarketMesa-{a.label}.bin")).resolve()
    expanded=a.expanded_manifest is not None
    name = f"spyro2-soi-marketmesa-{a.label}"
    package = f"{name}-{a.date}"
    zip_path = a.dest.resolve() / f"{package}.zip"
    shipping = zip_path.parent == DIST.resolve()
    if zip_path.exists():
        raise SystemExit(f"{zip_path} exists")
    work = a.work.resolve()
    work.mkdir(parents=True, exist_ok=True)

    # 1. inputs
    rep = json.loads(a.disc_report.read_text())
    target, checks, static_evidence, build_report = verify_target(disc,rep,work,a.archive_report,a.expanded_manifest,a.portal_report)
    portal_runtime=None
    if expanded:
        if not a.portal_runtime_log or not a.release_notes:
            raise SystemExit('expanded patcher requires completed portal runtime evidence and current release notes')
        portal_runtime=a.portal_runtime_log.read_text()
        for marker in ('ORIGINAL CINEMATIC70 METADATA EXACT','ORIGINAL NEW PORTAL CROSSING PASS',
                       'ORIGINAL MESA RETURN POSITION PASS','ORIGINAL GLIMMER RETURN PRESERVED PASS'):
            if not any(marker+' '+target['sha256'] in line for line in portal_runtime.splitlines()):
                raise SystemExit('expanded runtime evidence missing exact target check: '+marker)
    # Historical packages can still be reproduced with their original Glimmer
    # score. Every subsequent release retains the now-finished SoI soundtrack.
    if a.label not in ('test01','test02','test03','test04','test05') and not a.music_evidence:
        raise SystemExit('new Market Mesa releases require exact-disc SoI soundtrack evidence')
    music_checked=music_paths=music_work=None
    if a.music_evidence:
        if not a.release_notes:
            raise SystemExit('music/content releases require reviewed current feature and limitation notes')
        import mesa_music_release_check
        music_checked,music_paths,music_work=mesa_music_release_check.verify(
            a.music_evidence,disc.with_suffix('.cue'),work/('music-'+target['sha256'][:16]))
    runtime=a.runtime_log.read_text()
    if a.label not in ('test01','test02','test03','test04','test05'):
        for marker in ('LOADED TEXTURE DESCRIPTORS PASS','FINAL TEXTURE DESCRIPTORS PASS'):
            if not any(marker+' '+target['sha256'] in line for line in runtime.splitlines()):
                raise SystemExit('runtime evidence missing exact target CD-boundary check: '+marker)
    for marker in ('ARRIVE PASS','VISIBILITY RUNTIME PASS','GEM RUNTIME PASS','FAR RENDERER RUNTIME PASS',
                   'FAR GPU EMISSION PASS','FINITE TICKS PASS','MONEYBAGS LOADER PASS',
                   'MONEYBAGS PURCHASED FLAG PASS','MONEYBAGS FINAL RECORD PASS','PRIVATE CHECK PASS'):
        if not any(marker+' '+target['sha256'] in line for line in runtime.splitlines()):
            raise SystemExit('runtime evidence missing exact target check: '+marker)
    if RETAIL_BIN.stat().st_size != RETAIL_ID["bytes"] or sha(RETAIL_BIN) != RETAIL_ID["sha256"]:
        raise SystemExit("the retail Spyro 2 BIN does not match its pinned identity")
    decoder = FRAMEWORK / "tools/xdelta3.exe"
    if sha(decoder) != DECODER_SHA:
        raise SystemExit("decoder hash changed")
    say(f"inputs ok: {disc.name} {target['sha256'][:16]} {target['bytes']:,} B")

    # 2-3. patch + roundtrip
    patch_file = f"{name}.xdelta"
    encoded = work / f"{package}.xdelta"
    safe_delete(encoded)
    say("encode (xdelta3 -e -9 -A -S lzma)")
    r = run_limited([str(decoder), "-e", "-9", "-A", "-S", "lzma", "-s", str(RETAIL_BIN), str(disc), str(encoded)],
                    a.encode_limit, capture_output=True, text=True)
    if r.returncode:
        safe_delete(encoded)
        raise SystemExit(f"encode failed: {r.stderr[-1500:]}")
    rt = roundtrip(decoder, encoded, target, a.decode_limit)
    say(f"patch {encoded.stat().st_size:,} B, roundtrip ok")

    # 4. payload
    stage = work / "stage" / package
    if stage.exists():
        safe_delete(stage, recursive=True)
    stage.mkdir(parents=True)
    shutil.copyfile(encoded, stage / patch_file)
    shutil.copyfile(BACKEND, stage / SCRIPT)
    (stage / BAT).write_text(BAT_TEXT.format(script=SCRIPT), encoding="ascii", newline="\r\n")
    shutil.copyfile(GUI, stage / EXE)
    for n in RUNTIME:
        shutil.copyfile(FRAMEWORK / n, stage / n)
    notices = (FRAMEWORK / "THIRD-PARTY.md").read_text(encoding="utf-8")
    notices = notices.replace("The official release README is retained in verification.",
                              "The decoder's provenance record is verification/xdelta-provenance.json.")
    (stage / "THIRD-PARTY.md").write_text(notices, encoding="utf-8")
    shutil.copytree(FRAMEWORK / "tools", stage / "tools")
    shutil.copytree(FRAMEWORK / "licenses", stage / "licenses")
    v = stage / "verification"
    v.mkdir()
    shutil.copyfile(FRAMEWORK / "verification/xdelta-provenance.json", v / "xdelta-provenance.json")
    (v / "disc.json").write_text(json.dumps(dict(name=disc.stem, disc=rep, static_checks=checks.splitlines(),
                                                 static_evidence=static_evidence, fixture=False), indent=1) + "\n")
    (v/'static-check.log').write_text(checks)
    (v/'build.json').write_text(json.dumps(build_report,indent=1))
    if expanded:
        shutil.copyfile(a.expanded_manifest,v/'expanded-layout.json')
        shutil.copyfile(a.portal_report,v/'summer-portal.json')
        shutil.copyfile(static_evidence['expanded_static_log'],v/'expanded-static-check.log')
        (v/'portal-runtime.log').write_text(portal_runtime)
    feature_manifest={
        'schema':1,'target_sha256':target['sha256'],'archive_sha256':static_evidence['archive_sha256'],
        'finished_features':{
            'market_mesa_terrain':{'near_faces':build_report['near_faces'],'collision_triangles':build_report['collision_triangles']},
            'distant_terrain':{'source':'near terrain positions/winding/material B','check':'static-check.log and private-runtime.log'},
            'source_loose_gems':{'count':110,'value':240,'check':'static-check.log and private-runtime.log'},
            'retail_visibility_distance':{'distance':'0x2c000','check':'static-check.log and private-runtime.log'},
            'moneybags_inactive_state':{'state':'0xfd','check':'moneybags-oracle.log and private-runtime.log'}},
        'manual_gameplay':'Left to user; automated evidence is named separately.'}
    if expanded:
        feature_manifest['finished_features']['separate_mesa_level']={
            'scene_id':14,'progress_ordinal':29,'overlay_slot':198,'archive_slot':199,
            'original_glimmer_slots_preserved':[16,17],'original_unchanged_wad_payloads':195,
            'check':'expanded-static-check.log and portal-runtime.log'}
        feature_manifest['finished_features']['summer_forest_special_portal']={
            'portal_index':7,'entry':'Original controller collision crossing',
            'return':'Original Return Home call fixture reaches new Summer approach',
            'glimmer_regression':'Original Glimmer Return Home endpoint retained',
            'check':'summer-portal.json and portal-runtime.log'}
    if build_report.get('uv_polish'):
        uv=build_report['uv_polish']
        feature_manifest['finished_features']['source_uv_polish']={k:uv[k] for k in (
            'exact_top_faces','top_faces','source_exact_faces','source_approximated_faces','allocated_words','permitted_words')}
    if music_checked:
        mv=v/'music';mv.mkdir()
        shutil.copyfile(a.music_evidence,mv/'evidence-bundle.json')
        for role,path in music_paths.items():shutil.copyfile(path,mv/(role+path.suffix))
        for filename in ('music-presence.json','music-presence.log','music-release-check.json'):
            shutil.copyfile(music_work/filename,mv/filename)
        feature_manifest['finished_features']['soi_market_mesa_music']=music_checked
    (v/'feature-manifest.json').write_text(json.dumps(feature_manifest,indent=2)+'\n')
    (v/'private-runtime.log').write_text(runtime)
    moby_oracle=a.moby_oracle_log.read_text()
    if 'CONSUMER DATA SAFETY PASS '+static_evidence['archive_sha256'] not in moby_oracle:
        raise SystemExit('Moneybags original-consumer evidence does not match the exact archive')
    (v/'moneybags-oracle.log').write_text(moby_oracle)
    if a.pc_log:
        pc_log=a.pc_log.read_text()
        if 'PC DISC LOAD/CAPTURE PASS '+target['sha256'] not in pc_log:
            raise SystemExit('PC evidence does not match the exact target')
        (v/'pc.log').write_text(pc_log)
    (v / "backend-roundtrip.json").write_text(json.dumps(dict(decoder="tools/xdelta3.exe", source=RETAIL_ID,
                                                              target=target, decoded=rt), indent=1) + "\n")
    patch = dict(file=patch_file, bytes=encoded.stat().st_size, sha256=sha(encoded),
                 format="VCDIFF (RFC 3284), xdelta3 3.2.0 -e -9 -A -S lzma; no application header")
    mb = round(target["bytes"] / 2 ** 20)
    manifest = dict(schema=1, name=f"Spyro 2: Season of Ice - Market Mesa {a.label} (TEST)", game="Spyro 2: Ripto's Rage",
                    edition="USA SCUS-94425 NTSC (BIN/CUE, MODE2/2352)", source=RETAIL_ID, target=target, patch=patch,
                    decoder=dict(file="tools/xdelta3.exe", version="3.2.0", sha256=DECODER_SHA),
                    gui=texts(a.label, mb, expanded))
    manifest['features']=feature_manifest['finished_features']
    (stage / "patch.json").write_text(json.dumps(manifest, indent=2) + "\n")
    readme_text=readme(a.label, f"{a.date[:4]}-{a.date[4:6]}-{a.date[6:]}", target, patch, build_report)
    if a.release_notes:
        notes=a.release_notes.read_text(encoding='utf-8').strip()
        if not notes or '## What is in it' not in notes or 'unfinished' not in notes.lower():
            raise SystemExit('release notes must identify the delivered features and unfinished work')
        begin=readme_text.index('## What is in it');end=readme_text.index('## Identities')
        readme_text=readme_text[:begin]+notes+'\n\n'+readme_text[end:]
        (v/'release-notes.md').write_text(notes+'\n',encoding='utf-8')
    if expanded:
        readme_text=expansion_readme(a.label,f"{a.date[:4]}-{a.date[4:6]}-{a.date[6:]}",target,patch,notes,a.pc_log is not None)
    (stage / "README.md").write_text(readme_text,encoding="utf-8")
    src = stage / "source"
    shutil.copytree(GUI_SRC, src)
    shutil.copyfile(BACKEND, src / SCRIPT)
    (src / "README.md").write_text(
        "# Spyro 2 Season of Ice patcher front end\n\nThe house ImGui patcher front end (Theia tools/uya_patcher/main.cpp)"
        " with the disc type read from patch.json (BIN/CUE). Build with an x64 Visual Studio Developer Command Prompt,"
        " CMake and Ninja:\n\n    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release\n    cmake --build build\n\n"
        "ImGui's MIT license is in imgui/LICENSE.txt. The hidden --auto/--source/--output/--package/--screenshot"
        " switches support automated end-to-end validation; they do not bypass the backend's checks.\n", encoding="utf-8")
    shutil.copyfile(src / "imgui/LICENSE.txt", stage / "licenses/IMGUI-MIT.txt")

    # 5. sums, zip
    sums = [f"{sha(p)} *{p.relative_to(stage).as_posix()}" for p in sorted(stage.rglob("*"))
            if p.is_file() and p.name != "SHA256SUMS.txt"]
    (stage / "SHA256SUMS.txt").write_text("\n".join(sums) + "\n")
    when = (int(a.date[:4]), int(a.date[4:6]), int(a.date[6:]), 0, 0, 0)
    trial = work / zip_path.name
    safe_delete(trial, trial.with_name(trial.name + ".sha256"))
    zip_sha = make_zip(stage, trial, when)
    say(f"zip {trial} {trial.stat().st_size:,} B")

    # 6. end to end
    try:
        e2e = end_to_end(trial, package, work, target, a.decode_limit, a.label)
    except BaseException:
        safe_delete(trial, trial.with_name(trial.name + ".sha256"))
        raise
    a.dest.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(trial, zip_path)
    if sha(zip_path) != zip_sha:
        safe_delete(zip_path)
        raise SystemExit("copy mismatch")
    zip_path.with_name(zip_path.name + ".sha256").write_text(f"{zip_sha}  {zip_path.name}\n")
    safe_delete(trial, trial.with_name(trial.name + ".sha256"), encoded)
    safe_delete(stage, recursive=True)

    # 7. retire
    retired = []
    if shipping and not a.no_retire:
        old = [p.resolve() for p in a.retire if p.resolve() != zip_path]
        if any(p.parent!=DIST.resolve() or not p.name.startswith('spyro2-soi-marketmesa-') or p.suffix!='.zip' for p in old):
            raise SystemExit('retirement paths must name exact Market Mesa ZIPs in dist')
        safe_delete(*old)
        retired = [p.name for p in old]
    out = dict(zip=str(zip_path), zip_bytes=zip_path.stat().st_size, zip_sha256=zip_sha, package=package,
               target=target, patch=patch, roundtrip=rt, end_to_end=e2e, retired=retired)
    (work / f"{package}-verify.json").write_text(json.dumps(out, indent=1) + "\n")
    say("PATCHER PASS", json.dumps(out))


if __name__ == "__main__":
    main()
