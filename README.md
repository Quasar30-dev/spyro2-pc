# Spyro 2 PC

A native C++20 / D3D11 reconstruction of Spyro 2, with an ImGui workbench and a fixed 60 Hz simulation. The executable reads the player's own retail disc image or extracted files. It renders the actual textured, animated Spyro model, retail terrain, all 41 scene skies, static and animated objects, and gems.

This is an active reverse-engineering project, **not yet a complete playable port**. The current game view has running, charging, jumping, gliding, swimming, container collision, gem collection, flame, retail music/effects, voiced Hunter conversations, PC saves, Glimmer lizards, Hurricos robots/gear grinders, Idol Springs metal idols and an event-driven retail HUD. Remaining movement equations, object scripts and level progression still need reconstruction. The full project scope and evidence are recorded in [RE notes](docs/RE_NOTES.md).

## Play as Crush, Gulp, or Ripto

Use **Game → Playable character** in the PC Workbench, or press **F6** while playing. **Play Boss Training Ground.cmd** opens the practice arena and its character switch pad. The Workbench menu includes 1/3, 2/3, and full retail scale. [Controls, animation inventory, and PSX development status](docs/PLAYABLE_BOSSES.md).

## Build and run

This source snapshot requires your own supported game data. Build the executable without disc-backed tests with:

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=OFF
cmake --build build --config Release --target spyro_workbench
```


Conversations now use the game's bitmap font and rounded dialogue box. **Esc →
Guidebook** opens the original parchment pages, homeworld pictures and level
inventory. PC save/load locations and workbench access remain available from
pause. See [retail interface controls and scope](docs/RETAIL_INTERFACE.md).

Windows prerequisites: Visual Studio with Desktop development with C++, CMake, Git, and Python 3 for the extraction/research tools. The build script locates the installed Visual Studio CMake. Dear ImGui is fetched at a pinned commit during configuration.

```powershell
./tools/build.ps1
./tools/run.ps1 -Game 'D:/Games/Spyro 2/game.cue' -Play
```

`Run Workbench.cmd` now opens the **original retail title screen**. Press **Enter / Start**, then choose **New Game** (Glimmer) or **Load Game**. The original three-file selector opens PC saves; **Choose a PC save...** browses other locations. Your retail source and recent saves are remembered. **PC options / Workbench** opens the editor, or launch with `tools/run.ps1 -Workbench`. [Retail main menu and PC saves](docs/RETAIL_MAIN_MENU.md) documents the recovered title animation/artwork and controls. In the workbench, **Play movement test**, **Model studio**, and **View spawn** retain their existing functions.

Supported inputs are a 2048-byte-sector ISO, raw BIN, a CUE with one data track, or an extracted retail directory. The current executable/data profile is **USA SCUS_944.25, CRC32 `cdc0917c`**. Other revisions need their own verified address maps. Mounting a game is read-only; extraction is optional for running the desktop executable.

Music and speech need complete XA sectors: use CUE/BIN or an extraction retaining `SPEECH.STR.raw` / full-sector `SPEECH.STR`. The importer includes that stream by default. A data-only 2048-byte ISO lacks the additional audio bytes; geometry and sound effects can still load. [Audio source formats](docs/AUDIO_AND_NPC_INTERACTIONS.md) explains the supported extractions.

In play, WASD or the arrow keys move, Shift charges, and Space or X jumps; tap again to glide, then release. Gliding carries Spyro forward without W/Up; A/D or Left/Right steers and S/Down slows his approach. H hovers out of a glide; B headbashes in the air (H also headbashes when not gliding). F plays the flame action and R respawns. With the optional glide upgrade, further jump taps give one or two wing boosts per airtime, replenished on landing. Scorch's tagged sand slopes automatically slide downhill. [Aerial movement and sand evidence](docs/AERIAL_AND_SCORCH.md). In the editor, right mouse + WASD flies, Q/E moves vertically, Shift speeds up flight, and left click selects collision geometry. In the model studio, right mouse orbits and the wheel zooms.

Near Hunter, **E** talks, **Enter** continues, **1/2** answer a question and **Esc** leaves. **C** recenters the camera. For an immediate conversation test in Summer Forest, select object record **200 / class 1BC** and choose **Play beside Hunter**. His tutorial demonstration and rewards remain unimplemented.

**Esc → Save game / F5** writes a `.s2save`; **Save game as…** chooses any PC location. **Load game / F9** opens the main menu's file selector. The workbench Game menu uses the same save backend. The initial save folder is **Saved Games / Spyro 2 PC**. These saves retain campaign inventory, visited-scene pickups/defeats, released gems/butterflies, held/thrown containers, active flight/power state, supported conversations and the paid Glimmer bridge. **Tab** recalls the HUD; otherwise its counters appear for pickups, spirit awards and life changes, then slide away. [Saving, enemies and HUD](docs/SAVING_ENEMIES_AND_HUD.md) explains the implemented behaviour and remaining work.

**F3** (or **Game → Show FPS counter**) toggles a frame-rate counter drawn top right in the retail font: green at 55+, yellow at 30+, red below. The choice is remembered; `--fps` shows it for one session.

Enemies, bosses (Crush, Gulp) and scripted objects without a native reconstruction run their **original level scripts** in a built-in R3000/GTE interpreter, so every campaign level's enemies now move, attack, take hits and die as authored. [Retail scripts](docs/RETAIL_SCRIPTS.md).

**Cheats** includes Superflame, Superfly and Ripto's green fire bomb for use in any level. The bomb keeps Spyro purple and can break Robotica Farms' heavy doors. **Capture → Take photo / F10** saves a photo; **Record video / F11** records a 60 fps MP4 with sound. Video needs FFmpeg on PATH. [Powered attacks, flight and capture](docs/POWERUPS_AND_CAPTURE.md) covers the retail effects, native gates, controls and headless recording commands.

## Current native features

- Breeze Harbor burners, moving machinery, cannons and trolley controls now run; all four speedways have trial/Hunter progression, rewards and retries. All 25 power gates have mapped responses and the retail power meter. [Controls, checks and remaining gaps](docs/LEVEL_SYSTEMS.md), [all-level audit](docs/ALL_LEVEL_AUDIT.md).
- Regular charge jumps now use their retail clip and velocity-driven body pitch, including the jump-then-charge height gain. Superfly allows a normal first jump; G assists flying pickups. Six new combat profiles cover 52 enemies in Breeze Harbor and Mystic Marsh, shared dialogue expands to 162 NPC records, and original level cinematic cameras/actors/sound can play in the workbench. [Evidence, footage and remaining quest/cinematic work](docs/MISC_MOVEMENT_AND_LEVELS.md).
- Original skies load for all 41 scenes, with recovered geometry, colours and visibility trees. Basket and other plain static quad faces use the corrected retail winding. Hurricos gains linked grinder/robot combat, patrols and debris; six metal idols in Idol Springs gain attacks and deaths. Heavy chests use their proper attack rules and release their original fragments/gems. [Sky and level behaviour evidence, coverage and remaining scripts](docs/SKIES_AND_LEVEL_BEHAVIOURS.md).
- **Optional online exploration and upgrades:** **Online** hosts/joins an internet room through the temporary Cloudflare relay; **Upgrades** enables speed tiers, homing/splitting Superflame, flying carry/throw, melee and up to two glide boosts. These features are off by default. **Upgrades → Unlock all and activate powers** makes the abilities immediately available for testing. Choices survive Play, Stop and scene changes, and are stored in PC game saves. [Controls, setup and prototype scope](docs/ONLINE_AND_UPGRADES.md).
- Metal objects use their packed normals and original material shading; reflective objects use the retail environment texture. [Shared material fixes and all-scene audit](docs/OBJECT_RENDERING.md).

- **Procedural universe preview:** use **Run Universe.cmd** or **Universe → Land on this world**. Seeded islands have clear landing areas, connected gem trails, optional encounters, baskets and original Return Home portals into a space flight area. World progress saves separately from the campaign. [Controls, generation rules and scope](docs/PROCEDURAL_UNIVERSE.md).

- Glimmer's nine small lizards follow escape routes/taunt and can be charged or flamed. Its five club wielders turn, swing, damage Spyro and die to flame or supercharge. Choose **Play beside enemy** in the object inspector. Further enemy families still need their individual scripts.
- The HUD uses the original digits, animated gem/spirit sprites and blinking Spyro life portrait. Its independent counters use retail entry/exit curves, counting and hold timings, with 60 Hz interpolation.
- All 41 mapped scene archives load terrain, collision and Spyro's global / level-specific animation banks.
- Water uses the retail palette transparency and additive blend settings, keeping the pool bed, objects and submerged Spyro visible. **Play in water** starts beside a suitable swimming surface. [Water rendering evidence](docs/WATER_RENDERING.md) records the material flags, GPU checks and remaining animation/atmosphere work.
- Spyro uses the original model, face colors, palette textures, frame durations, loops and an independently animated secondary track. [The head controller](docs/SECONDARY_ANIMATION.md) reads the original transition table and passes 279,000 state comparisons against the retail MIPS code.
- Ordinary traversal uses the full-speed run clip. The native animation state reads the retail transition matrix, blends between movement clips and preserves fall exit frames. Glide and its braking animation use the recovered forward-speed and descent constants. [Traversal evidence](docs/TRAVERSAL_AND_TRANSITIONS.md) records remaining physics work.
- Ground contact now follows floor seams without repeatedly entering the fall animation. Regression replays cover walking, charge and supercharge on flat and uneven terrain, real ledges and 30/60/144 Hz presentation.
- **Cheats** offers two Spyro 2 main-model supercharge appearances: the existing random wings and a new regular-charge flap variant. Both preserve full-size wings, the corrected neck, smoke and leg blur; saves and multiplayer retain the selection. [Supercharge wing options](docs/POWERUPS_AND_CAPTURE.md#supercharge-wing-options) describes their timing and command-line controls.
- Retail music maps all 41 scenes to 29 referenced tracks. Native audio plays movement, flame, pickup/container and supported Moby frame sounds, with background music/speech decoding and separate volume controls. Summer Forest Hunter uses retail text, voices, talking animations and camera choices. [Audio and conversation evidence](docs/AUDIO_AND_NPC_INTERACTIONS.md) records coverage and remaining scripts/mixer work.
- Retail object import preserves 5,200 records across the scene banks. Static meshes, materials and 1,669 loose gems are decoded; gems use recovered colors, rocking animation and direct-touch collection. [Object and gem evidence](docs/OBJECTS_AND_GEMS.md) records the remaining behaviors.
- Animated object banks decode 2,430 clips / 24,196 frames, including Sparx and Hunter. The ImGui model viewer offers class/clip selection and frame scrubbing; level instances retain their authored animation phases. [Animated object evidence](docs/ANIMATED_OBJECTS.md) covers timing, scale, materials and remaining character behavior.
- Sparx uses his original model, wing/eating animations, idle substates, swimming transform/bubbles, health colours, sparkle trail and attached gold glow. Hold **T** for his gem hint; collected gems display their original value digits. Sheep release butterflies that he eats to restore health, with both dedicated retail sounds; gem attraction and delivery have separate sounds. [Companion evidence](docs/SPARX_COMPANION.md) records the recovered effects, 40-bank checks and remaining controller work.
- Baskets, vases and bottles use their retail collision shapes, attack masks and break fragments. Released gems bounce, settle and become available to Sparx; charge releases can fly directly to Spyro. Select **Play beside container** in the object inspector to try an interaction. [Container evidence](docs/CONTAINERS_AND_RELEASED_GEMS.md) records the remaining effects, water/surface behavior and save work.
- F breathes the retail flame: eight curved streams with colored tips, textured additive trails, impact cutoffs and interpolated fading. Geometry, palette and texture come from the mounted copy. [Flame evidence](docs/FLAME_RENDERING.md) records the recovered renderer and remaining action/effect work.
- Powered shots use the retail projectile, trail and explosion meshes, particle textures, sparks, colour/size envelopes and blend modes. Underwater Superflame uses its blue burst and bubbles. Original Moby transparency preserves solid and translucent pixels within one texture.
- Model, terrain and collision use one shared unit conversion, preserving retail relative scale. The model origin and collision sphere radius are both 356 retail units.
- Normal, front-facing and charge camera profiles are read from the mounted executable. The original spherical placement, fixed-point spring integration and projection correction are implemented. [Camera and scale evidence](docs/CAMERA_AND_SCALE.md) distinguishes recovered behavior from remaining work.
- The renderer targets 60 fps, with animation interpolation and a fixed 60 Hz simulation independent of presentation cadence.
- ImGui scene projects support a spawn point, test entities, save/load, undo/redo, and isolated play sessions. A retail object inspector exposes record IDs and models with camera focus. Projects use `.s2scene`; geometry editing and retail object authoring remain future work.

## Validation

Campaign portals now use the mounted game's panorama meshes and destinations; home-world transporters have paired arrivals, and all 26 whirlwind elevators have native lift/glide movement. Walk through an open doorway or step into a lift. **Cheats → Unlock portals and whirlwind elevators (session)** allows exploration while quest scripts are being completed. [Portal evidence, controls and current limits](docs/PORTALS_AND_WHIRLWINDS.md).

Glimmer's Moneybags now opens the original bridge for **100 gems**, including
its camera view, moving mesh/collision and saved payment. **Esc → Exit level**
returns to the corresponding home world while retaining progress. Dialogue
answers use **1/2** in their displayed order. [Campaign evidence and current
scope](docs/CAMPAIGN_AND_TERRAIN.md); [whole-port completion ledger](docs/REMAINING_WORK.md).

`tools/build.ps1` builds Release and runs the C++ and Python suites. Retail files are not needed for these synthetic tests.

```powershell
./tools/validate-retail.ps1 -Game 'D:/Games/Spyro 2/game.cue' -NoBuild
./tools/validate-audio.ps1 -Game 'D:/Games/Spyro 2/game.cue' -NoBuild
```

This decodes every mapped scene/model bank, exercises movement transitions and object animation clocks, verifies every loose pickup and container charge/release, runs a paced 360-frame replay with free glide, braking and Sparx delivery, and captures container interactions and the original character models. Captures, timing and inspection reports go to `data/validation/`. The timing report measures CPU submission work and wall-clock pacing, not GPU timestamp queries or complete-game performance.

The latest checked results are recorded in [VALIDATION.md](docs/VALIDATION.md).

For archive inspection:

```powershell
./build/Release/spyro_inspect.exe 'D:/Games/Spyro 2/game.cue' --all-levels --spyro
```

## Ghidra workflow

```powershell
python ./tools/spyrodisc.py import 'D:/Games/Spyro 2/game.cue' --output data
./tools/analyze.ps1 -GhidraHome 'D:/Tools/Ghidra' -JavaHome 'D:/Tools/jdk' -AllFunctions
./tools/analyze_overlay.ps1 -Slot 1 -GhidraHome 'D:/Tools/Ghidra' -JavaHome 'D:/Tools/jdk'
```

Alternatively put the local `ghidra` and `java` paths in `tools/local.json`. `analyze.ps1 -ExportOnly -AllFunctions` exports an existing project without reimporting it. Normal analysis reimports the generated `main.bin`; keep manually annotated research projects separately.

The original game files, extracted data, Ghidra projects and local research clones are excluded by `.gitignore`. They are not dependencies to distribute with the engine. See [third-party references](docs/REFERENCES.md).

## Provisional Spyro 3

The Year of the Dragon US profile recognizes `SCUS_944.67` CRC32 `02c69f63`.
It exposes 37 main scenes and 38 extra areas. Use your own extracted disc
and a separate original Spyro 2 retail donor for PC powers, effects and UI:

Launch it with **Play Spyro 3.cmd** (front end and level select) or **Spyro 3 Workbench.cmd**. These start the workbench's Spyro 3 edition (`--spyro3`), with its own title, menus, settings and saves. The workbench's *Retail data* panel also has a Spyro 2 / Spyro 3 game switch.

```powershell
./build/Release/spyro_workbench.exe --spyro3
./build/Release/spyro_workbench.exe --spyro3 --workbench --game data-yotd --slot 99 --yotd-area 1 --play
```

Choose **Upgrades > Unlock all and activate powers** in the workbench to enable
the provisional all-level powers and PC abilities. Genuine Dodge Roll clips
require the existing full July donor. The area selector also exposes extra
areas; original portal transitions and campaign progression remain incomplete.

See [format evidence](docs/SPYRO3_FORMATS.md), the
[implementation and merge report](SPYRO3_PROVISIONAL.md), and
[repeatable native captures](tools/capture-yotd.ps1).
