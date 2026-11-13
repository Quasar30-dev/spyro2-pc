# Provisional Spyro 3: Year of the Dragon

Status as of 14 October 2026. This is an early preview, not a port.

## What it is

The workbench recognises the US Year of the Dragon executable (`SCUS_944.67`, CRC32 `02c69f63`) and plays its scenes on the Spyro 2 engine:
- all 37 main scenes and their 38 extra areas;
- YOTD's own terrain, VRAM, models, objects and sound effects, decoded by the profile-selected paths (`src/spyro3.cpp`, `src/yotd_assets.cpp`);
- Spyro 2's movement controller, camera, HUD and PC abilities.

A separate original Spyro 2 copy supplies:
- the PC powers (Superflame, Superfly, Supercharge, green bombs);
- the elemental upgrade system's effects;
- the interface assets.

Codex (`gpt-6.1-sol`, maximum reasoning) built this in a copy of the project (`Documents/Spyro3-provisional`). Its run ended early when its tool connection failed. Its work was then merged into the main tree by hand, file by file, keeping every later Spyro 2 change. Format evidence is in [docs/SPYRO3_FORMATS.md](docs/SPYRO3_FORMATS.md).

## Launching

Spyro 3 has its own edition of the workbench. Launch it with **Play Spyro 3.cmd**, in this folder or in `Documents/Spyro3-provisional`. The edition has:
- its own window title: *Spyro 3: Year of the Dragon PC*;
- its own front end: a Spyro 3 title over a live Sunrise Spring, then New game, Choose level, Load a PC save, Choose game copy, Workbench and Quit;
- its own settings (`%LOCALAPPDATA%/Spyro 3 PC/frontend.cfg`), which remember its copy, saves and donor;
- its own saves folder (`Saved Games/Spyro 3 PC`).

**Spyro 3 Workbench.cmd** opens the same edition straight into the workbench panels. `tools/run.ps1 -Spyro3` and the `--spyro3` option do the same from a shell.

- **Copies:** the edition opens its remembered copy, else `data-yotd`. Drag a YOTD disc image or folder onto either launcher to use another copy. The Spyro 2 copy in `data/disc` supplies the PC powers and interface.
- **Level select:** *Choose level* lists all 37 main scenes under their worlds (Sunrise Spring, Midday Gardens, Evening Lake, Midnight Mountain), plus the Super Bonus Round. The in-game pause menu offers it in place of Spyro 2's guidebook. *Exit Level* returns to the level's home world.
- **Workbench panels:**
  - the *Retail data* panel has a **Game** switch (Spyro 2 / Spyro 3: Year of the Dragon) and a *Spyro 3 front end* button;
  - its level list is grouped by world, shows each level's extra areas, and lists the current level's areas under it;
  - *Game → Switch game* and *Game → Choose level...* do the same from the menu bar;
  - switching games mounts that game's remembered copy and moves to its own settings file;
  - Spyro 2-only tools are disabled while Spyro 3 is mounted (Universe, custom Spyro 2 worlds).
- **Command line:**

```powershell
./build/Release/spyro_workbench.exe --spyro3
./build/Release/spyro_workbench.exe --spyro3 --workbench --game data-yotd --slot 99 --yotd-area 1 --play
```

Main scenes are the odd slots 97–169, for example 97 Sunrise Spring and 99 Sunny Villa. **Upgrades → Unlock all and activate powers** enables the PC abilities. `--menu-levels` (with `--main-menu`) opens the level select, for captures. Captures of the title, level select, pause menu and workbench are in `data/validation/spyro3-launcher`.

## Verified

- `spyro_spyro3_tests data-yotd data/disc` (CTest `spyro3_provisional`) covers all 75 areas. For each, it loads the scene, imports the models and runs the real controller through movement, gems, containers, enemies and the PC powers. That is about 113 million checks.
- Workbench captures of Sunrise Spring and Sunny Villa are in `data/validation/yotd-20261014`.
- The whole suite (40 CTest tests, this one included) passes with the merge.
- Sunny Villa's original Rhynoc scripts run in the interpreter: 21 actors, about 2.2 million instructions in 240 frames, no faults. Before the merge fix, the bridge aborted because the original loader appends five mobys (three gems, two D3s) after the 149 authored records. Those extras now stay VM-only.

The provisional test passes with 0 failures (113 million checks):
- **Sunny Villa's three "charge-accepting" EE Rhynocs** (actors 56, 63, 84) were never hit at all. The original loader removes them at boot.
  - Its per-moby persistence pass (`8002c4b0`..`8002c62c`) checks the level's save bitmaps (`8006d088`/`8006d08c`).
  - Matching records get their reward spawned in their place and are then removed (`80055b18` marks them −3); this is where the three extra gems come from. The bridge mirrored them as removed.
  - The test now identifies loader-removed actors and attacks the 18 live ones. The EE handler (`8007d8b8`) enters its death path only for attack bits `0x90000` (flame or headbash), as Codex found.
  - Still provisional: whether a new campaign keeps these three depends on initialising the save tables as the game does, and their replacement gems stay inside the VM.
- **Icy Peak area 1:** the Superfly sweep dived into rising ground, and landing ends flight. The dive now stops once its clip has played or the ground is near.
- `SPYRO_YOTD_DUMP=file` writes the decoded level overlay (load address `800742d0`) for `tools/mips_listing.py file:FILE@800742d0`. `SPYRO_YOTD_WATCH=address` reports writes to one byte.

## Not yet done

- Campaign progression, portals between scenes, cutscenes, music and speech.
- Original YOTD moby scripts run only for Sunny Villa's Rhynocs (the provisional VM bridge in `src/yotd_scripts.cpp`). Other levels use native fallbacks.
- Raw YOTD surface types (ice, sand) are not yet mapped to Spyro 2's skating and sliding.
- Sub-area saving.
