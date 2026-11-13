# Cinematic shot capture

`--cinematic-shot path.s2shot` runs an offline, bounded 60 Hz shot using the real player controller and a smooth presentation camera. It selects a clean full-screen render area with the requested native resolution. The ordinary gameplay camera and interactive workbench are unaffected when this option is absent.

Example:

```powershell
./build/Release/spyro_workbench.exe --game data/disc --custom-level data/custom-levels/hollowbloom-springs/pc/hollowbloom.s2world --cinematic-shot output/hollowbloom-trailers/source/02-orchard-run.s2shot --capture-music-volume 0 --video output/hollowbloom-trailers/gameplay/02-orchard-run.mp4 --capture output/hollowbloom-trailers/review/02-orchard-run.bmp
```

`S2SHOT 1` is a whitespace-separated authoring format:

1. Magic and version: `S2SHOT 1`.
2. Width, height, simulation frame count. Bounds: 320–3840 by 240–2160, even dimensions, 6–36000 frames.
3. Spawn XYZ, yaw in radians, HUD boolean, vertical FOV in degrees (20–100), camera mode.
4. Route point count, then XYZ points. Point zero is the starting point; the controller approaches points one onward. Ground movement uses normalized world directions; charging uses heading-relative steering after charge entry.
5. Action count, then rows: begin tick, exclusive end tick, charge boolean, jump boolean, flame boolean. Jump/flame are pressed at the first tick; jump/charge remain held through the window.
6. Camera key count, then rows: tick, eye XYZ, target XYZ. Keys are strictly increasing. Camera mode 0 uses the ordinary follow camera, 1 uses absolute coordinates, and 2 adds interpolated player position to each camera key. Smoothstep interpolation eases between keys.

Coordinates are the engine's Y-up world units. Collision, animation, enemies, gem collection, and effects remain live. Camera moves do not move Spyro or bypass collisions. Routes are a filming tool, not an AI navigation replacement. The author must inspect shots for camera occlusion and route hazards.

The video recorder warms up for five frames. Shot sources deliberately include a small editing handle. The edit trims them to exact durations and 60 fps. `--capture-music-volume 0` leaves gameplay sound effects in the source clips while the final edit uses a continuous retail music track.

Hollowbloom's repeatable pipeline is `tools/author_hollowbloom_trailer.py`, `tools/capture_hollowbloom_trailer.ps1`, then `tools/edit_hollowbloom_trailers.py both`. Captures run the installed main executable. The title renderer decodes the mounted retail font's glyphs, advances, vertical offsets, colours and PS1 horizontal aspect correction; it does not substitute a desktop font.

Versions 2–4 preserve the same header, routes and camera keys:

- `S2SHOT 2` appends the headbash boolean to each action.
- `S2SHOT 3` appends melee, carry, throw, special, interact, world-steering enabled, world X and world Z.
- `S2SHOT 4` appends roll-left and roll-right booleans. Each roll is pressed at the first tick and held only for the authored interval.

A full version-4 action is `begin end charge jump flame headbash melee carry throw special interact world_steer world_x world_z roll_left roll_right`. Boolean fields must be written as integers, not decimal strings. `--upgrade-tier 1|2` also equips the available prototype roll donor.

Bounded replay captures support `--capture-size 1920 1080` to set the actual render dimensions, and `--hide-hud` to suppress gameplay overlays after replay setup. These switches preserve normal interactive defaults. The powers trailer pipeline is `tools/author_powers_trailer.py`, `tools/capture_powers_trailer.py`, `tools/graphics_powers_trailer.py`, then `tools/edit_powers_trailer.py`. Source clips retain gameplay audio; the edit uses July's Glimmer music continuously.

The revised powers edit opens with a GPT Image edit of the retail Spyro 2 logo,
with PC integrated beneath it, then three ordinary flame-breath gameplay cuts.
July Glimmer music starts after those cuts. The guidebook chapters follow; the
closing assistance-logo stinger and yellow-font plant joke remain at the end.
Run `python tools/graphics_powers_trailer.py all` after the selected native
captures are available, then `python tools/edit_powers_trailer.py`. These graphics
are asset-only edits and do not require an unchanged executable to be rebuilt.

Guidebook text is measured using retail glyph advances and fitted to each paper page, with at least 38 original UI units of horizontal margin. Pictures must show the feature described by the page: the adventure spread uses a clear underwater shot, rather than an obstructing rock. Inspect every rendered spread and the page-turn frames before delivery.

The closing teaser is introduced by "Coming soon" and the existing Spyro 2.5
Backrealms logo. Crownworks takes require `--backrealms`. Bounded offline captures
can select `--capture-boss-stage foot|mecha-gulp|bird`; this runs the real boss
transformations before the capture clock starts. Movement, attacks and damage
then remain live. Phase selection cannot combine with saved-game input/output or
the boss validation fixture. It is filming setup, not an unassisted playthrough.

Use `--require-visible-footing` for gameplay captures starting on dry ground.
It rejects a spawn whose collision support has no matching rendered floor. The
report also counts `unsupported_grounded_frames` throughout the take, rebuilding
the visible support mesh when terrain changes. Accept a grounded feature take
only after inspecting its footage and this count. An authored airborne action
can legitimately have ungrounded frames; a collision-only invisible ledge cannot
be used as a standing position. Mode 0 plus no orbit/distance/height overrides
uses the ordinary gameplay camera.

## Finding open ground

`spyro_open_spots <data> <slot> [count]` ranks a level's objects by how open the floor around them is: the nearest terrain over 32 headings at three heights, and the nearest large object. `--headings <record>` lists each heading's clear flying distance from one record. The October trailer films its Furies beside Glimmer record 12 (the castle courtyard) and its Superfly along that record's longest heading, +z.
