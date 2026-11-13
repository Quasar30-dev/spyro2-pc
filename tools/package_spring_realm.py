"""Package the Spring Realm with the Spyro 2 PC workbench.

    python tools/package_spring_realm.py [--date YYYYMMDD]

Writes dist/spring-realm-<date>/ and dist/spring-realm-<date>.zip (+ .sha256):
the installed release build/Release/spyro_workbench.exe, the world
(levels/spring-realm/pc/spring-realm.s2world and its 64x64 tiles), a launcher
that asks once for the player's disc image (remembered in disc_path.txt) and
starts straight into the Spring Realm with the ImGui workbench open, a README
and the Dear ImGui notice. No disc files, debug builds or PDBs: the check
below refuses the package if any would ship. The Blender source with the
fused retail scene (tools/fuse_spring_realm_retail.py) goes beside it, not in
it, because it carries data extracted from the disc.
"""
import hashlib, shutil, sys, time, zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DATE = sys.argv[sys.argv.index('--date') + 1] if '--date' in sys.argv else time.strftime('%Y%m%d')
NAME = f'spring-realm-{DATE}'
OUT = ROOT / 'dist'
WORLD = ROOT / 'data/custom-levels/spring-realm/pc'
EXE = ROOT / 'build/Release/spyro_workbench.exe'
IMGUI = ROOT / 'build/_deps/imgui-src/LICENSE.txt'
VCREDIST = 'https://aka.ms/vs/17/release/vc_redist.x64.exe'
WITH_BLENDER = '--with-blender' in sys.argv
BLENDER_NOTE = {
    False: '''The Blender source is NOT in this package. It is kept beside it as
"Spring Realm + Dragon Realm intro (fused).blend" (folder spring-realm-{date}-blender). That
file contains geometry and textures extracted from your own disc (the Dragon Realm intro
scene, WAD 73, and its texture atlas) together with the Spring Realm additions. It is for
personal use only; do not redistribute it.''',
    True: '''This zip also carries the Blender source beside the game folder, in
spring-realm-{date}-blender: "Spring Realm + Dragon Realm intro (fused).blend". The game
folder has no disc data, but that .blend does: it contains geometry and textures extracted
from the disc (the Dragon Realm intro scene, WAD 73, and its texture atlas) together with
the Spring Realm additions. It is for personal use with your own copy of Spyro 2.'''}
FORBIDDEN = {'.iso', '.bin', '.cue', '.img', '.wad', '.str', '.raw', '.pdb', '.ilk', '.lib', '.exp', '.blend', '.blend1'}

LAUNCHER = r'''@echo off
setlocal
rem Spring Realm (Spyro 2 PC). Asks once for your own Spyro 2 disc image, remembers it
rem in disc_path.txt, then starts straight into the Spring Realm with the workbench open.
rem Drag a different disc image onto this file to change it. Further arguments go to the game.
set "HERE=%~dp0"
set "GAME=%~1"
if "%GAME%"=="" if exist "%HERE%disc_path.txt" set /p GAME=<"%HERE%disc_path.txt"
if not "%GAME%"=="" if not exist "%GAME%" set "GAME="
if "%GAME%"=="" for /f "usebackq delims=" %%F in (`powershell -NoProfile -STA -Command "Add-Type -AssemblyName System.Windows.Forms; $d=New-Object System.Windows.Forms.OpenFileDialog; $d.Filter='Spyro 2 disc image (*.cue;*.bin;*.iso)|*.cue;*.bin;*.iso|All files|*.*'; $d.Title='Choose your Spyro 2: Ripto''s Rage! (USA) disc image (CUE/BIN recommended)'; if($d.ShowDialog() -eq 'OK'){$d.FileName}"`) do set "GAME=%%F"
if "%GAME%"=="" exit /b 1
>"%HERE%disc_path.txt" echo %GAME%
set "EXTRA="
:more
shift
if "%~1"=="" goto run
set EXTRA=%EXTRA% %1
goto more
:run
pushd "%HERE%"
start "Spring Realm" "%HERE%spyro_workbench.exe" --game "%GAME%" --custom-level "%HERE%levels\spring-realm\pc\spring-realm.s2world" --play --workbench%EXTRA%
popd
'''

README = '''SPRING REALM - Spyro 2 PC ({date})
==================================================

The Spring Realm is a "Spyro 2.5" expansion hub built around Spyro 2's own Dragon Realm
intro scene. It runs in the Spyro 2 PC workbench: a native C++ / Direct3D 11 reconstruction
of Spyro 2: Ripto's Rage! (USA, SCUS-94425) that reads your own disc image. This is a
development build, not a finished game.


STARTING
--------
Double-click "Play Spring Realm.bat". The first time, choose your Spyro 2: Ripto's Rage!
(USA) disc image. CUE/BIN is recommended: music and speech need the full-sector BIN; a
data-only .iso still loads the level and sound effects. Your choice is remembered in
disc_path.txt; drag a different image onto the .bat to change it.

The game starts straight in the Spring Realm, in front of the castle, with the workbench
open around the game view.


THE WORKBENCH (SHOW / HIDE)
---------------------------
At start the ImGui workbench is visible: the menu bar, "Retail data" (left), "Inspector"
(right), "Session" (bottom) and the game in the "Viewport" panel.

  F1                          hide the workbench (full-window game) / show it again
  Game > Hide workbench       the same, from the menu bar
  Esc > Workbench             in the pause menu: show the workbench again
  Esc > Options > Workbench   the same

The game keeps running while the workbench is hidden. Minimising the whole window pauses it.


CONTROLS (keyboard)
-------------------
  WASD / arrow keys   move                 Shift        charge
  Space or X          jump; tap again in the air to glide; release to drop
  H                   hover out of a glide  B            headbash (in the air)
  F                   flame                 E            talk
  C                   recentre the camera   R            respawn
  Tab                 show the HUD          Esc          pause menu
  F3                  FPS counter           F10 / F11    photo / video (video needs FFmpeg on PATH)

CONTROLS (XInput controller; it plays the game whichever panel has focus)
  left stick move, A jump / glide, X charge, B flame, Y talk / hover / headbash (hold while
  standing to look), right stick camera, RS click recentre, Back HUD, Start pause.

In the workbench: right mouse + WASD flies the editor camera, Q/E moves it up/down.


WHAT IS IN THE SPRING REALM
---------------------------
- The retail Dragon Realm intro scene, loaded from your disc, completed and turned green:
  the courtyard closed on its south side, the pond completed, the castle completed with
  open gates, a walkway round the walls and two ramps.
- West and East Halls and the Orrery Hall (Autumn Plains style), whirlwind lifts to the
  roof terraces, vases, baskets, gems and a life jar.
- The great cherry tree on the north lawn, fallen blossoms, the open east meadow, the
  grotto and root tunnel under the lake.
- Three level portals in the Dragon Realm's arches. Their expansion levels do not exist
  yet: they are stand-ins that lead into retail Summer Forest levels. To come back, start
  the launcher again (or Game > Open custom level... and pick levels\\spring-realm\\pc\\
  spring-realm.s2world).
- The crown gate beyond the castle's north gate stays sealed: it needs four expansion
  emblems, which cannot be earned yet. Its world, the Crownworks, is not in this package.
- Progress in a custom world is not saved to Spyro 2 save files.


WHAT IS IN THIS PACKAGE
-----------------------
  Play Spring Realm.bat                        the launcher
  spyro_workbench.exe                          Spyro 2 PC workbench (release build)
  levels\\spring-realm\\pc\\spring-realm.s2world   the Spring Realm world
  levels\\spring-realm\\pc\\textures\\*.rgba        its four 64x64 tiles
  README.txt, THIRD-PARTY-NOTICES.txt

No disc files are included: no WADs, game executable, audio or texture atlases. The program
reads the Dragon Realm intro scene, Spyro, the objects, sounds and music from your disc at
run time. The world file holds the Spring Realm's own geometry. Some of it was modelled by
copying and mirroring shapes of the retail scene (wall bays, the castle's north half, trees),
and three of its four tiles are repaints of retail tiles (the blossom meadow and the two
spring-green leaf tiles), so the world is derived from the original game's art.

{blender_note}


REQUIREMENTS
------------
Windows 10/11 x64 with Direct3D 11, and the Microsoft Visual C++ 2015-2022 x64 runtime
({vcredist}) if it is not already installed. Your own Spyro 2: Ripto's Rage! (USA) disc image.

Executable SHA-256: {sha}
'''


def sha256(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def main():
    folder = OUT / NAME
    # A refresh overwrites the package in place (nothing is deleted); a file
    # that is not part of the package stops it, so the zip never picks it up.
    expected = {'Play Spring Realm.bat', 'README.txt', 'THIRD-PARTY-NOTICES.txt', EXE.name, 'disc_path.txt',
                'levels/spring-realm/pc/spring-realm.s2world'} | {f'levels/spring-realm/pc/textures/{t.name}' for t in (WORLD / 'textures').glob('*.rgba')}
    if folder.exists():
        extra = [p.relative_to(folder).as_posix() for p in folder.rglob('*') if p.is_file() and p.relative_to(folder).as_posix() not in expected]
        if extra or (folder / 'disc_path.txt').exists():
            raise SystemExit(f'{folder} holds files that are not part of the package: {extra or ["disc_path.txt"]}')
    (folder / 'levels/spring-realm/pc/textures').mkdir(parents=True, exist_ok=True)
    shutil.copy2(EXE, folder / EXE.name)
    digest = sha256(folder / EXE.name)
    assert digest == sha256(EXE)
    shutil.copy2(WORLD / 'spring-realm.s2world', folder / 'levels/spring-realm/pc/spring-realm.s2world')
    for t in sorted((WORLD / 'textures').glob('*.rgba')):
        shutil.copy2(t, folder / 'levels/spring-realm/pc/textures' / t.name)
    (folder / 'Play Spring Realm.bat').write_bytes(LAUNCHER.replace('\n', '\r\n').encode('ascii'))
    note = BLENDER_NOTE[WITH_BLENDER].format(date=DATE)
    (folder / 'README.txt').write_bytes(README.format(date=DATE, sha=digest, vcredist=VCREDIST, blender_note=note)
                                        .replace('\n', '\r\n').encode('utf-8'))
    notice = ['THIRD-PARTY SOFTWARE NOTICES', 'This product includes the following third-party component, under its own licence.',
              '', '=' * 92, 'Dear ImGui (https://github.com/ocornut/imgui)', '=' * 92, '', IMGUI.read_text(encoding='utf-8', errors='replace').strip(), '']
    (folder / 'THIRD-PARTY-NOTICES.txt').write_bytes('\n'.join(notice).replace('\n', '\r\n').encode('utf-8'))
    # Every tile the world names must ship; nothing from the disc may.
    world = (folder / 'levels/spring-realm/pc/spring-realm.s2world').read_text(encoding='utf-8')
    for t in (folder / 'levels/spring-realm/pc/textures').glob('*.rgba'):
        assert f'"textures/{t.name}"' in world, t.name
        assert t.stat().st_size == 64 * 64 * 4, t.name
    bad = [p.name for p in folder.rglob('*') if p.suffix.lower() in FORBIDDEN]
    if bad:
        raise SystemExit(f'refusing to ship {bad}')
    zpath = OUT / f'{NAME}.zip'
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for p in sorted(folder.rglob('*')):
            z.write(p, f'{NAME}/{p.relative_to(folder).as_posix()}')
        if WITH_BLENDER:   # the user's choice: the fused Blender source travels in the zip, beside the game folder
            blender = OUT / f'{NAME}-blender'
            if not (blender / 'Spring Realm + Dragon Realm intro (fused).blend').exists():
                raise SystemExit(f'--with-blender: missing {blender}')
            for p in sorted(blender.rglob('*')):
                if p.suffix.lower() != '.blend1':
                    z.write(p, f'{blender.name}/{p.relative_to(blender).as_posix()}')
    with zipfile.ZipFile(zpath) as z:
        if z.testzip() is not None:
            raise SystemExit(f'{zpath}: CRC check failed')
        names = z.namelist()
    zsha = sha256(zpath)
    (OUT / f'{zpath.name}.sha256').write_text(f'{zsha}  {zpath.name}\n', encoding='ascii')
    print(f'{zpath}  {zpath.stat().st_size:,} B  sha256 {zsha}\n  exe sha256 {digest}')
    for n in names:
        print('  ', n)


if __name__ == '__main__':
    main()
