# Level completion: talisman / orb, Return Home and the outro

Reconstructed 3 November 2026 from the retail executable (SCUS_944.25) and the level
overlays (Ghidra exports in `data/analysis/main`, `data/analysis/overlay_001`,
`data/analysis/moby-cases`; raw listings with `tools/mips_listing.py`).

## The retail flow

1. **The talisman speaker.** Each talisman level has one speaker whose conversation holds a
   line with command 1 (`8003a0d8`). The speaker's class code, after `8003a98c` says Spyro
   talked (Triangle, or by itself when its header byte +5 is set), sets **`80066f44`** on its
   own condition and only then opens the conversation (`80015be4`):

   | Level | Speaker class | Arms `80066f44` when |
   | --- | --- | --- |
   | Glimmer | 22A (`8007d944`) | header kind byte = 1 |
   | Idol Springs | 124 | its own script |
   | Colossus | 10F | the end event 11A (`800825c0`) arms it and makes 10F talk by itself (+5 = 2) |
   | Hurricos | 31 (`8007a564`) | kind = 3 (kind 4 sets task flag 4) |
   | Aquaria Towers | A1 (`80078e18`) | kind = 3 and its linked moby (header +3C) has left state 0 |
   | Sunny Beach | B6 (`8007c5a8`) | kind = 2 |
   | Skelos Badlands | 62 (`8007dbdc`) | kind = 3 |
   | Crystal Glacier | C6 | kind = 3 |
   | Breeze Harbor | 180 (`80080acc`) | kind = 1 (the PC keeps its native burner quest) |
   | Zephyr | BE (`800735c8`) | kind = 3 |
   | Scorch | 1B0 (`8007d3e8`) | kind = 5 (also sets +49) |
   | Shady Oasis | 189/188 (`8007d68c`) | kind = 3 |
   | Magma Cone | 13B (script owned, `8007bb80`) | kind 2: its end trigger 401 (header +44) reached state 2; it restarts the level at a checkpoint before itself and talks by itself (see "Second pass") |
   | Fracture Hills | 85 (script owned) | its own script |

2. **The line.** `8003a0d8` command 1 picks its line when `80066f44` is set and `80066ff4`
   is clear, and stores the reward class (`80066f94`, the level's talisman object, e.g.
   Glimmer DB). Commands 2..9 are orb lines: orb bit (command - 2) once task flag
   `80067148` has that bit.
2. **The reward** (`80016328`, after the conversation's closing state 2, 0x18 updates):
   - *State 4 (talisman):* updates 19..71 spawn class-81 sparkles that gather on a point
     7/16 of the way from the speaker to Spyro, 0x1C0 above Spyro; update 20 plays the
     speaker's sound; update 80 spawns the talisman object there. Its class (Glimmer DB,
     `80071e74`) plays sound 0, turns 8 and rises 4 units per update with sparkles every
     fourth update for 80 updates, then vanishes in a four-sparkle burst (`8003ca00`) with
     sound 0. Update 140 calls Zoe (`8003cafc`); update 150 sets the talisman byte
     (`8006b264` + level), the talisman count (`80067108`) and **`80066ff4`**; update 200
     closes the conversation (`800161c8`).
   - *State 3 (orb, class 71):* update 30 sends the orb from the speaker to Spyro; 100,
     140 and 210 follow. For levels after index 0x15 (Winter Tundra), an orb in the level's
     "Perfect" mask (`80066e72` + level: Mystic Marsh 1, Cloud Temples 4, Robotica Farms 1,
     Metropolis 1) also sets the talisman byte and `80066ff4`: that is how a Winter Tundra
     level is completed.
3. **Return Home** (class 1AA; Glimmer `80075024`, Mystic Marsh `800773f4`) stays down until
   the level's talisman byte is set, then rises. Crossing it calls `80019928(portal)`:
   game state 7, destination `80064b84[level]` (the level's home world).
4. **The exit** (`80071bd4`, overlay 1): after the 0x10-update fade, if Spyro is leaving a
   level (`80066ee4` = 0) and `80066ff4` is set, it plays the level's outro,
   `80073e48(1)` (movie `80064914[level] + 1`; Glimmer with Summer Forest not yet visited
   plays story cutscene 4 instead). The pause menu's Exit Level takes the same path
   (`800698f8` = 1). The home world then loads and Spyro arrives at the level's portal.

## What the PC does now

- Talisman speakers whose class has no native reconstruction run their own class code
  (`RetailScripts::boot` hands them to the overlay interpreter). They arm the talisman line
  on their own conditions and, being automatic, talk when Spyro arrives. A native speaker
  of kind 1 is armed as 22A does (`begin_dialogue`), except Breeze Harbor's Squawk.
- `NpcDefinition` lines record command 1 (`talisman`, the reward class) and commands 2..9
  (`orb_bits`).
- The reward plays out natively after the conversation (`Game::RewardShow`): sparkles,
  the level's own talisman model rising and turning, its sound and the vanishing burst;
  the orb flight for orbs. Spyro is held still until it ends (update 200 / 210).
- Winter Tundra: the completion byte is saved like a talisman (not counted), and Return
  Home tests the level's Perfect mask.
- `Game::reward_this_visit` (`80066ff4`): leaving through Return Home or the pause menu then
  plays the level's outro before the home world loads.

Orb challenges (the speakers' orbs, as opposed to the end-of-level talisman / completion
orb) have their own table and checks: [Orb challenges](ORB_CHALLENGES.md).

## Dialogue completion correction (5 November 2026)

`80066ff4` is a visit flag independent of whether the save already owns the reward.
The saved Ghidra exports of `8003a0d8`, `8003a568` and `80016328` establish the order:
command 1 selects the talisman line while `80066f44 != 0 && 80066ff4 == 0`; an
already-owned reward sets `80067164`; command 2B leaves speaker header +5 = 2
(automatic talk); state 4 update 150 sets `80066ff4` unconditionally and only then
conditionally increments `80067108` / sets `8006b264 + level`. State 3 update 150
also sets the visit flag for Winter Tundra's Perfect completion orb.

The initial PC correction mirrored `Game::reward_this_visit` to `80066ff4`, which
fixed the first award. A revisit still looped: `award_talisman()` returns false
when the save owns it, so the visit flag stayed clear and Twitchy reselected the
talisman line whenever his automatic talk ran. `advance_dialogue` now records the
completion visit independently of adding a reward. `end_dialogue` plays any selected
reward presentation on revisits too, while `sync_progress_out` counts only new awards.
Owning a talisman on entry alone does not mark this visit complete.

`tests/retail_script_tests.cpp` executes unmodified SCUS_944.25 `80016328` at state 4
update 150, with the talisman table byte both 0 and 1. Only the final camera and
outer game-loop calls are stubbed. Both executions set the visit flag and leave
the count at exactly 1. Runtime tests then read Twitchy's first/revisit conversation,
observe the reward, drain the follow-up talk, and walk away without a reopening.
Evidence: `build/dialogue-20261105/logs/retail-regression.log` lines 1-7 (1,156 checks);
the exact test and main hashes are in `build/dialogue-20261105/artifacts.json`.

The private main executable SHA-256 is
`2baa0e554edce23b10b1ed5445dfe9ca24b82968bae2a0dcb6a2e1a9c9938e8e`.
All 18 main-application exit checks completed: `build/dialogue-20261105/logs/desktop-summary.log`
line 2 (38/38 includes 20 orb challenges). Each `exit-SLOT.log` names the exact binary;
the matching JSON records `simulated_end`, the outro, home world and assertion count.
These are automated fixture-driven checks; no manual play or visual inspection is claimed.

**Failed diagnostic retained:** `build/dialogue-20261105/logs/exits-headless.log` line 244
reports FAIL. The older standalone `spyro_level_exits` teleports to speakers without
satisfying several quest/end prerequisites; e.g. Colossus never arms `80066f44`, and
Winter Tundra's task bits stay clear. The main application's check explicitly records
and applies those fixtures (`desktop_level_exit_check.inl`), or runs the original end
trigger / quest for Magma Cone and Fracture Hills. Its 18 complete exits are the release
evidence. The initial combined headless summary remains **7/8 FAIL**, not a passing
exit gate. `run_dialogue_release_checks.py` now runs that older diagnostic only with
`--legacy-exit-probe`.

## Checks

- `spyro_level_exits <disc> [slot...]` (tools/level_exits.cpp): per level, the talisman /
  completion speaker, the talk, the award and Return Home.
- `spyro_workbench --game <disc> --level-exit-check SLOT --smoke 6000 --capture <file.bmp>`
  plays the whole flow in the real workbench (hidden) and saves `-1-reward`, `-2-exit`,
  `-3-outro` and `-4-home` photos beside the capture; the report's `level_exit` block says
  whether the end condition was simulated.

## Status by level

Hidden runs of `--level-exit-check` on 3 November 2026; all 18 PASS after the second pass below (photos and reports: `data/validation/level-exits-20261103/`; contact sheets `sheet-1.png` .. `sheet-3.png`, one row per level: end trigger, reward, exit, outro, home; the first agent's `sheet-a.png` / `sheet-b.png` show the earlier photos). Each PASS: the award, Return Home raised (after update 150), the walk through it, the outro, arrival in the home world within 9 units of the level's portal and the award kept in the campaign record. "End simulated": the teleport skipped the level's own end event or task, so the check armed it as that event does.

| WAD | Level | World | Talisman / orb | Exit portal | Outro | Tested (hidden run) | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 17 | Glimmer | SF | yes (talisman) | yes | yes | PASS | First exit with Summer Forest unvisited plays story cutscene 4 (WAD 77 / 142); Zoe arrives at update 140. |
| 19 | Idol Springs | SF | yes (talisman) | yes | yes | PASS |  |
| 21 | Colossus | SF | yes (talisman) | yes | yes | PASS (end simulated) | End event 11A skipped by the teleport: armed by the check. The talisman (class DA) is spawned but behind the doorway: the monk 10F walked off after his first, ordinary talk and the check made him talk again from there. A check artifact (11A itself places Spyro beside him). |
| 23 | Hurricos | SF | yes (talisman) | yes | yes | PASS |  |
| 25 | Aquaria Towers | SF | yes (talisman) | yes | yes | PASS (end simulated) | Task (linked moby) skipped by the teleport: armed by the check. |
| 27 | Sunny Beach | SF | yes (talisman) | yes | yes | PASS |  |
| 35 | Skelos Badlands | AP | yes (talisman) | yes | yes | PASS |  |
| 37 | Crystal Glacier | AP | yes (talisman) | yes | yes | PASS | Exit photo: the check stands Spyro before Return Home in a narrow ice passage and the native wall fallback (P04) lifts the camera over him onto the floor. Test placement, not an exit fault. |
| 39 | Breeze Harbor | AP | yes (talisman) | yes | yes | PASS | Talisman by the native burner quest (engines set by the check). The black photo was a missing one: the quest gave the talisman without the state 4 presentation. Fixed; it now plays. |
| 41 | Zephyr | AP | yes (talisman) | yes | yes | PASS |  |
| 45 | Scorch | AP | yes (talisman) | yes | yes | PASS |  |
| 47 | Shady Oasis | AP | yes (talisman) | yes | yes | PASS (end simulated) | End condition armed by the check. |
| 49 | Magma Cone | AP | yes (talisman) | yes | yes | PASS (own end trigger) | Spyro is put in the end trigger (401, the crater); the eruption shot, the whirlwind, the checkpoint restart and the host's own talk follow without simulation. |
| 51 | Fracture Hills | AP | yes (talisman) | yes | yes | PASS (own quest) | The check frees the six satyrs first (flame, charge, talk); the plateau rises and its floor is drawn; the faun talks by himself once they are all there. |
| 59 | Mystic Marsh | WT | yes (completion orb) | yes | yes | PASS (end simulated) | Completion orb (Perfect mask 1); task flag set by the check. |
| 61 | Cloud Temples | WT | yes (completion orb) | yes | yes | PASS (end simulated) | Completion orb (Perfect mask 4); task flag set by the check. |
| 65 | Robotica Farms | WT | yes (completion orb) | yes | yes | PASS (end simulated) | Completion orb (Perfect mask 1); task flag set by the check. |
| 67 | Metropolis | WT | yes (completion orb) | yes | yes | PASS (end simulated) | Completion orb (Perfect mask 1); task flag set by the check. |

Not covered: speedways, bosses and Dragon Shores / Ripto (no talisman speaker; their rewards are handled by existing systems), and home worlds.

## Second pass (3 November 2026): Magma Cone, checkpoint restarts, camera paths, the photos

**Magma Cone** (Ghidra: `data/analysis/moby-cases/overlay_048/class_13b.c`, `class_401.c`).
The host 13B (`8007bb80`, state table `8006d6f0`, kind table `8006d728`) is kind 2:
- with +49 = 0 it waits until the moby at header +44 (record 12, class 401) is in state 2, then
  counts header +2C down from 120, turns round (+46 - 0x80), sets header +5 = 1 (talk by itself)
  and +49 = 1, persists the three, stores a checkpoint in front of itself (`8004c0c8`) and calls
  `8004c1ac`;
- with +49 = 1 it waits for `8003a98c`, sets `80066f44` and opens the conversation.

401 (`8007f744`) is the crater trigger. Spyro inside its rectangle (`8003873c`, header +14: centre,
half sizes +20 / +24, angle +28), above its height - 0x300 and on the ground (or mode 13): it writes
1 into the state of the whirlwind at header +30 (record 175, class 400, kind 2) and persists it,
requests `10000002` and plays camera path 5 (`8001f4a4(path, 3)`); once the shot has arrived it
starts its terrain group (`8002365c(+2C, FC)`) and enters state 2.

Three engine gaps kept it from finishing; all three are general:
1. **`8004c1ac`, the checkpoint restart.** It copies the checkpoint into the level record and calls
   `80017164`: game state 3 (`8001722c`: fade out 0x10 per frame, reload the level at the
   checkpoint with its persistent bytes, fade in, then game state 0). Nothing handled game state 3,
   so it stayed 3 and `8003a98c` refused every later talk (it requires game state 0). The engine
   now plays it (`RetailScripts::advance_restart`): fade out, Spyro at the checkpoint without
   losing a life, the camera back to normal, fade in, game state 0. The level is not reloaded: the
   bytes the scripts persisted for the reload are already in RAM. `8004c1ac` is also called by
   Colossus 10F, Aquaria Towers 14E, Sunny Beach BB, Zephyr C5, Scorch 1B9, Mystic Marsh 25C,
   Breeze Harbor 180, Skelos Badlands, Fracture Hills, the speedways and Dragon Shores; the report's
   `checkpoint_restarts` counts them.
2. **A script starting a native whirlwind.** Scripts write the state of native mobys (401 writes 1
   into its whirlwind); the engine overwrote it every tick. A whirlwind (class 400) now adopts a
   state a script wrote (kind-2 whirlwinds are controlled that way).
3. **Camera mode 10 is a camera path, not a moby focus** (`8001fa58` case 10, `800216d8`).
   `8001f4a4(path, flags)` stores the path (a count, then 12-byte keys: u16 x, y, z times 4 and s16
   roll, pitch, yaw - the cinematic camera layout). Substate 3 (flag 2) blends from the current view
   over 0x40 timer units; 0 / 1 fade out and in; 2 shows key `8006802c`, advancing unless flag 1
   (4 loops, 8 stays); 80 / 81 fade back to Spyro. The timer grows by the elapsed VBlanks. The
   director used to read the path as a moby and point the camera at nothing; it now plays the path
   with the retail fades (`RetailScripts::play_path`, `80066f68`) and sets `80067100` as the update
   does.

With these, `--level-exit-check 49` puts Spyro in the 401 rectangle (the check's "end trigger", not
a simulation): the eruption shot plays (`49-magma-cone-0-end.png`), the whirlwind rises, the level
restarts at the host, he talks by himself, and the talisman, Return Home, the outro and Autumn
Plains follow (`checkpoint_restarts` 1, `simulated_end` false).

**The photos.**
- Breeze Harbor's black reward photo: no photo was taken, because Squawk's native burner quest gave
  the talisman with `award_talisman()` only. It now starts the state 4 presentation as every
  talisman speaker does (`LevelSystems::after_move`), and Return Home waits for it. Game bug, fixed.
- Home-world arrivals inside walls or portal frames: the check photographed 200 frames after the
  load, before the home world had run (40 game ticks; the arrival glide was not finished). It now
  waits for 150 game ticks, and all 18 home photos show Spyro in the home world. Separately, the
  camera arm could pass through a doorway's aperture (portal surfaces are not terrain to the camera
  queries) into the panorama behind it; the follow camera now stops at apertures
  (`FollowCamera::place`; a native rule, the retail `8003dcc0` handling of surface 6 is not
  reconstructed). Mostly the test camera, plus that game fix.
- Crystal Glacier's exit photo and Cloud Temples' reward photo: the check's placement in a narrow
  spot; the native wall fallback (P04) frames a wall or the floor. Test camera.
- Colossus' missing talisman: see the table (a check artifact).

Each run's log (`<slot>-<name>.log`) has a `photo ...` line per photo: Spyro, the camera eye, which
side of the nearest doorway each is on, and the reward's speaker, point and item.

## Handoff (agent, 3 November 2026)

Files changed for this work: `src/retail_scripts.cpp` (talisman speaker hand-over in
`boot`, kind-1 arming in `begin_dialogue`, reward start in `end_dialogue`, `zoe_reward`),
`include/spyro/retail_scripts.hpp`, `src/npc.cpp` / `include/spyro/npc.hpp` (`talisman`,
`orb_bits`), `src/runtime.cpp` / `include/spyro/runtime.hpp` (`award_talisman` for Winter
Tundra completion, `reward_this_visit`, `RewardShow`), `src/travel.cpp` (Return Home: Perfect
mask, waits for update 150), `src/powerups.cpp` / `include/spyro/powerups.hpp`
(`emit_sparkle`), `src/desktop.cpp`, `src/desktop_menu.inl`, `src/desktop_cinematic.inl`
(outro / story cutscene 4 before the home world, `--level-exit-check`),
`src/desktop_level_exit_check.inl`, `tools/level_exits.cpp`, `tools/run_level_exit_checks.py`,
`CMakeLists.txt` (`spyro_level_exits`). The main `build/Release/spyro_workbench.exe` is built
in place (`cmake --build build --config Release --target spyro_workbench`).

Run one level: `python tools/run_level_exit_checks.py 17` (results and photos in
`data/validation/level-exits-<date>/`, `summary.json`). Look at the photos: a passing run
only proves the flags and the trip.

Open points:
- Story cutscene 4 (first Glimmer exit) uses scene WAD 77 + movie WAD 142, derived from
  `8006323c[4]` = 72 and `80064914[31]` = 46 (US language offset `80067118` = 0); confirm
  it is the "Spyro meets Elora" scene in the photo.
- Zoe at update 140 calls the original `8003cafc` with `80066f78` / `80066f5c` restored;
  she is then a script moby. Her checkpoint conversation was not exercised.
- Speakers that the check had to arm (report `simulated_end`) depend on a level event or
  task the teleport skipped; walking the level should arm them, but that was not tested.

## Third pass (3 November 2026): Fracture Hills, mode 11, the reward camera, Zoe's zap

**Fracture Hills, "Spyro standing in mid-air" (user report): a game bug, fixed.** The end area
(Return Home, the faun 85, the baskets) stands on collision at y 34.36 whose drawn floor is
terrain region 82: a scripted morph (group 6) whose node 0 is hidden (byte 7, the region's 0x4000
flag). The six satyrs (class 8A, `80079484`) build it: each waits encased until it is attacked
(+18 & 30000: flame or charge), is freed, counts itself in the word at header +20 (80066fec =
6 - count) and raises the plateau a group at a time (`80023710(count)`); at six, `8002365c(6, FC)`
shows the floor. They were native talkers, so their code never ran and the floor never appeared.
They now run their own code (`RetailScripts::boot`, quest speakers). Evidence:
- `spyro_level_walk` (headless, `FLAME=1 CHARGE=1 TALK_TO=... WALK_Y=35.5 WALK_END=...`): before,
  187 grounded ticks on the end floor and none over a drawn face; after freeing the six with real
  inputs, the counter reaches 6, region 82 spans y 34.34..38.03 and 611 of 611 grounded ticks are
  on a drawn face (gap 0.016). Logs: `fracture-hills/fh-before.log`, `fh-after.log`.
- `--level-exit-check 51` now frees them first (report `quest_freed` 6); the faun talks by himself
  once the band is there (not simulated). Photos `51-fracture-hills-*.png`: Spyro on the tiled
  plateau floor.
- Freeing a satyr switches its model to a clip of another mesh; the animation blend threw
  ("Object animation transition has incompatible vertices") and ended the workbench. A transition
  between meshes now shows the target frame (`blend_frames`).

**Camera mode 11 (`8001f524`, 8001fa58 case B).** The placement is no longer instant: the screen
fades out (0x10 per timer unit), Spyro is placed with the camera behind him, then it fades in and
the normal mode returns; Spyro's controls are held. The scripts run during the fade, so Colossus
11A awards its spirit (state 99) before Spyro arrives beside the monk, who then talks (the
instant warp let the monk talk first and skip it: `retail_overlay_scripts` check 1079). The fade
also completes when a conversation opens during it (Party Crashers' host).

**The reward camera.** `80016328`, when the talk closes into a reward: 80066f5c becomes 3 if the
talk ended in view 0, otherwise 4; camera mode 12 then springs from the current eye to that view of
the table at `8006163c` (five 20-byte views; 3 = 832, 400, 2304, -96, 256 and 4 = 3264, 400, 2304,
-96, -256: yaw, elevation, radius, view pitch, view yaw), facing the speaker, until the reward
closes the conversation. The engine read only views 0..2 and left the conversation camera at the
reward. It now keeps mode 12 with view 3 / 4 for the whole reward, and Zoe's reward arrival uses
the same side (`8003cafc`). Evidence: the decoded views (`CAMERA_PROFILES=1 spyro_level_walk`), and
the reward photos of the 18 levels (sheets). No PS1 emulator is installed (only PCSX2), so there
is no retail capture A/B yet.

**Zoe's zap (class 110, `80073d40`).** In her state 1 she takes a bolt from `8004b120(4, 1,
608080)` (the table at `8006b400`: eight 0x30-byte records, +0 from, +0C to, +18 colour, +1C
segments, +20 a random word per bend), writes her wand (attachment 0) and Spyro's origin into it
every frame, spawns a type-0C sparkle on Spyro (`800670fc(1, C, moby, 3)` with her position set to
his) and plays her sound 0; when it ends she frees the record (+1C = 0). The engine now draws every
live record of that table as `8004b194` draws the gates' bolts (four segments, colour 608080,
bends re-rolled every other frame at half strength) and emits type-0C sparkles natively (count
a0). Evidence: `zoe-zap/zap-sheet.png` (Glimmer's first Zoe, 5.0-7.8 s of `zoe-glimmer.mp4`): the
bolt from her wand to Spyro at 6.50-6.67 s. The sparkle sprite size is the engine's type-0C size;
it is not compared with a retail capture.

## Handoff (second agent, 3 November 2026)

Done and proven in hidden runs:
- Magma Cone;
- the checkpoint restart (game state 3);
- scripts raising native whirlwinds;
- camera paths (mode 10) and placement (mode 11);
- Breeze Harbor's reward presentation;
- the camera at portal apertures;
- Fracture Hills' satyr quest (floor and walk);
- the reward camera views;
- Zoe's zap bolt and sparkles;
- the satyr mesh-transition crash.

All 18 level exits pass, and all 42 ctest suites pass.

Code touched:
- `src/retail_scripts.cpp`, `include/spyro/retail_scripts.hpp`: restart, `play_path`, `advance_place`, `present_bolts`, the particle hook, quest speakers, native whirlwind states;
- `src/camera.cpp`, `include/spyro/camera.hpp`: portal apertures, views 3 / 4;
- `src/runtime.cpp`, `include/spyro/runtime.hpp`: reward camera, `restart_at_checkpoint`;
- `src/speedway.cpp`: Breeze Harbor reward;
- `src/object_animation.cpp`: mesh transitions;
- `src/powerups.cpp` / `.hpp`: script bolts;
- `src/renderer.cpp`: script bolts;
- `src/desktop.cpp`, `src/desktop_level_exit_check.inl`: fade, check end triggers, the satyr quest, photo log;
- `tests/retail_script_tests.cpp`: mode 11 fade;
- `tools/level_exits.cpp`, `tools/level_walk.cpp` / `.hpp`, `tools/run_level_exit_checks.py` (`--sheets`);
- `CMakeLists.txt` (`spyro_level_walk`).

Not started, next in the coordinator's order:
1. **In-game orb challenges** (user priority): Colossus hockey, Glimmer's gem lamps and Metropolis' superfly first, then the rest, with a per-challenge table (implemented / completable / tested) beside this file.
   - Starting points: `spyro_level_exits` prints every orb speaker (commands 2..9) per level.
   - `data/analysis/moby-dispatch/dispatch.json` ranges are imprecise. Find a class's case the way Magma Cone's was found: scan the overlay for `li v0,CLASS` + `beq s0,v0,TARGET`.
   - Then decompile it: run `ExportCaseRanges.java` with explicit ranges, as in `data/analysis/moby-cases/overlay_048/`.
   - Many givers end their challenge with `8004c1ac`, which now works.
2. **Whole-level walks.**
   - `tools/level_walk.hpp` builds a floor graph and drives Spyro with inputs only. On Colossus the start area's graph does not yet reach the rest of the level (steep ramps / steps and 2-4 unit rises east of x 47 are not linked), and there is no swim support (Aquaria Towers).
   - The end conditions the check still arms itself:
     - Colossus: 11A's box at header +4 (`tests/retail_script_tests.cpp` section 7 enters it);
     - Aquaria Towers: A1's linked moby at header +3C is record 205, class 141, which waits in state 0 until another moby sets its header +16 timer (not traced yet);
     - Shady Oasis: speaker 189 talks with the talisman only once its kind byte becomes 3 (set by its quest, not traced);
     - the Winter Tundra levels: the task flag `80067148`.
3. **The Spring Realm package refresh**: see CHANGELOG.

Diagnostics added to `spyro_level_walk` (env):
- `OBJECTS`, `FIND_TYPE`, `NEAR`, `UNDER`, `TRANSLATIONS`, `CAMERA_PROFILES`;
- `BOX=index,offset` (stand in a script's trigger box and trace);
- `TALK_TO` with `FLAME` / `CHARGE`, `WALK_END` / `WALK_Y`;
- `--portals`, `--arrival SCENE`, `--triangles`.

## Handoff (third agent, 3 November 2026): orb challenges

Done (details, table and evidence in [Orb challenges](ORB_CHALLENGES.md)):
- eight speaker challenges complete through the original code in hidden runs, headless
  (`spyro_challenges <disc> all`) and in the workbench (`python tools/run_challenge_checks.py`):
  Colossus hockey (keeper, one on one) and statues; Glimmer gem lamps (outdoors, cave);
  Metropolis coup and sheep saucers (two waves);
- the level overlays' own challenge HUD (timers, scores) drawn by the workbench;
- three general engine fixes (spawned mobys after a hole, `8006a104`, powered shots on
  script mobys).

Next, in order:
1. The other speaker challenges in the table (each: hand the giver to the interpreter in
   `challenge_giver`, find the challenge objects' cases with `tools/find_class_case.py`,
   write the autopilot in `tools/challenge_pilot.hpp`, prove it headless, then in the
   workbench, look at the photos). Sunny Beach's turtle boxes were started: the boxes (B7)
   take a charge, or a flame when their header +28 is set; Gromit's own count is not traced.
2. Whole-level walks (the second agent's item 2) are untouched by this pass.
3. The autopilots use teleports for the first approach and for reaching scattered targets
   (enemies for a power-up gate, statues); every one is counted in the run's `fixtures`.
