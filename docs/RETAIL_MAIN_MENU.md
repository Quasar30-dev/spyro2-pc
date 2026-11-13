# Retail main menu and PC saves

Normal launch now opens the original US **Ripto's Rage** title scene. The logo,
Press Start sprite, ornate three-file frame, homeworld thumbnails, highlights,
New Game and Load Game labels are decoded from the mounted game. Spyro uses
the title cinematic's own mesh and animation. The menu preserves the authored
4:3 framing, with bars on wider screens, while gameplay retains its wide view.

Press **Enter**, controller **Start / A**, or click **Press Start**. Select a
recent file and **Load Game**, or use **Choose a PC save...** to browse to any
`.s2save`. **New Game** starts Glimmer. Starting fresh clears the live retail
campaign; it does not delete or overwrite saves on disk.

**Esc / controller Start** pauses play. **Save game** writes to the current
PC save location, opening the Windows save picker on the first save.
**Save game as...** chooses a different location. **Load game** opens the
retail file selector. **Resume game** resumes the same live instance; it does
not recreate Spyro, enemies, carried objects, or the level. Escape closes a
conversation before it opens pause. **F5 / F9** remain save/load shortcuts.

**Exit level** returns to the mounted executable's home-world destination and
retains campaign progress. Ordinary levels arrive beside their matching portal;
the three boss arenas currently return to the home's starting point. It is
disabled in home worlds, procedural worlds and for online guests. This native
grounded arrival does not yet reproduce the retail fly-in/out sequence.

**PC options / Workbench** at the bottom of the title/file screen provides
access to the running game, saving, the game-copy picker, workbench and quit.
The workbench's **Game → Main menu** returns here. Direct workbench launch is
available as `tools/run.ps1 -Workbench` or `spyro_workbench --workbench`.
The existing `-Play` / `--play` options still skip the title.

The source picker accepts ISO, CUE/BIN and extracted retail folders through
the existing `AssetStore`. With no chosen source, the title first asks for the
player's copy. The launcher remembers the source and eight recent successful
save/load locations in `%LOCALAPPDATA%/Spyro 2 PC/frontend.cfg`. On this project's
first launch it also recognizes `data/disc`. Progress remains exclusively in
the existing portable, checksummed, atomic `.s2save` files. Retail and universe
saves retain their separate paths. Damaged or missing files remain visible
and report an error; browsing to a replacement works without losing live play.

## Recovered retail data

| Source | Native implementation |
| --- | --- |
| WAD 95 | Title sky, arena terrain, VRAM, sound bank and music mapping |
| WAD 96 | 14 cinematic segments, 9 animated actors, packed translations, 12-byte camera keys |
| `800739e0`, `80073c64` | Mixed model/chunk-relative cinematic relocation |
| `8007490c` | Frame durations, actor translation interpolation, camera sampling |
| `80079df0` | View adjustment, title loop at VBlank 1620 → 1032, menu sound IDs 0/1 |
| `80079878` | Six-byte sprite descriptors at `80063f44` / `80063f2c`, 8-bit and 4-bit VRAM pages/CLUTs |
| `8007bfc4` | Original logo, file-frame, thumbnail and action-label positions |
| `8001e270`, `8001a2fc` | Camera heading and pitch signs, converted from original Z-up coordinates |

The relevant functions were re-exported with Ghidra into
`data/analysis/title/decompiled/`; `tools/probe_title.py` produces the decoded
sprite contact sheet for inspection. Generated retail data stays under `data/`.
`TitleAssets` relocates bounded byte offsets and reuses the existing Moby
vertex/face decoder. A separate renderer preserves the live game's GPU assets
while the title plays. Rendering samples the cinematic at 60 Hz and interpolates
its original pose durations. The desktop uses native ImGui hit targets over the
retail art. PC file paths/messages retain ImGui text; dialogue and pause/book
controls now use the decoded retail bitmap font. See [Retail interface](RETAIL_INTERFACE.md)
for the original pause inventory, guidebook pages and current scope.

## Verification

- All 19 CTest suites pass. The title/preferences retail run passes **2,417,047
  checks**, sampling 3,240 half-VBlank times across the entrance data and loop,
  checking finite geometry/cameras, loop wrap, bounded preferences and Unicode
  path round trips. These checks do not claim pixel-perfect PS1 rendering.
- `--menu-check` drives real ImGui keyboard/mouse events. Its **56 assertions**
  cover Press Start, Glimmer New Game, menu pause/input isolation, Save, Save As,
  cancellation, original file selector, progress/upgrades reload, damaged save
  rejection, invalid source rejection, same-instance resume, New Game cancel,
  universe saves, and persisted recent/source locations. Native file-picker
  results are injected only in this explicit test mode.
- A second process resumes the saved menu preferences without `--game`.
  A separate run checks the unmounted first-use screen. The existing workbench
  upgrade replay still triggers all three J melee strikes.
- `data/validation/retail-main-menu-60fps.mp4`: native 12-second title capture,
  720 frames, H.264 **60/1 fps**, 1418×904, non-silent 48 kHz stereo AAC. Audio and
  video durations match. `retail-save-selector.bmp` shows the original file UI
  populated with actual PC saves, and `menu-pause-saved.bmp` shows saving in play.

The default starts at the retail title's Press Start phase (VBlank 840), skipping
the boot logos and entrance movie. Console memory-card access, formatting and
attract-mode demos are replaced by PC files, not emulated. Cutscene-specific
intro sound sequencing, PS1 dithering and exact transition/fade timings remain
separate reconstruction work. Current game/level behaviour coverage is unchanged
by this front end; see the other RE documents.

```powershell
./build/Release/spyro_frontend_tests.exe data/disc
./build/Release/spyro_workbench.exe --game data/disc --menu-check --headless --frames 240 --capture data/validation/menu-integration.bmp
./build/Release/spyro_workbench.exe --game data/disc --main-menu --headless --frames 720 --video data/validation/retail-main-menu-60fps.mp4 --capture data/validation/retail-main-menu.bmp
./build/Release/spyro_workbench.exe --game data/disc --menu-files --headless --frames 30 --capture data/validation/menu-files.bmp
```

`--menu-profile <path>` isolates remembered locations for a portable/test launch;
`--menu-time <VBlank>` selects a cinematic inspection time. Bounded captures do
not read or change the user's normal menu preferences unless a profile is explicitly
provided. `--main-menu` captures the rendered UI, including the retail sprites.
