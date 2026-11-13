# Movement, Moby and cinematic update — 21 September 2026

The playable executable is `build/Release/spyro_workbench.exe`. Install each compiled change there **before running validation**, as requested. `tools/install-workbench.ps1` publishes a candidate immediately. If Windows holds the executable open, it retains the running image under a dated filename and installs the replacement at the normal path. Reopening the game loads the new image. Do not leave playable changes only in the candidate directory.

## Movement and carrying

- Ordinary Shift + Space now enters retail charge-jump animation **21**, with the original animation transitions. Supercharge is not required. It also works while the Superfly cheat is enabled.
- Body orientation follows the jump velocity: upward on ascent, then downward during descent. Resident `800350d0`, state `15`, calls `8002ded0`: the target is the 12-bit velocity angle plus `0x8e`, followed by the original quarter-force/quarter-damping angular spring. The mounted executable supplies the angle and square-root tables. Rendering interpolates body rotation at the presentation rate.
- Normal launch is `0xdc0` Q6 velocity. Normal jumps add `0x17` for the first 13 held VBlanks, then subtract `0xc0`; charge jumps retain their upward velocity for the initial held window. Entering charge in the air preserves vertical speed and restarts that window. Releasing charge retains the retail nine-tick exit delay.
- Flat-ground held peaks in the integration fixture: normal jump **1.28073**, ground charge jump **1.16406**, jump followed ten ticks later by charge **1.83083** world units. These verify the requested jump-then-charge height gain. Collision-assisted enemy/scenery proxies are **not fully reconstructed** by this vertical-controller work.
- Grounded Superfly activation permits a normal first jump. A second airborne press starts flight. Native airborne power gates and speedway starts retain their flight behavior.
- Supercharge uses the requested PC height adjustment: launch **10.5**, gravity **22**; fixture peak **2.41889**. This is a deliberate tuning change, not a claim of an exact retail supercharge trajectory.
- With the optional carrying upgrade enabled, flying past a visible heavy chest gently reduces speed. **G** selects a visible nearby carryable object within 5.5 units and flies toward a safe pickup position. The approach uses normal swept collision, times out after two seconds and can be cancelled with G. It leaves clearance for the carried object's collision sphere. G while carrying still drops it; impact still smashes it.

## Moby coverage

Six new combat profiles cover **52 authored enemies**:

| Level | Classes | Count | Added behavior |
|---|---|---:|---|
| Breeze Harbor | `169`, `16a` | 16 | Bucket/fire Blubbers: facing, pursuit, attack/recovery, original projectile models, armor rules and death clips |
| Mystic Marsh | `1f2` | 4 | Elephant snails: emerge, wind up, roll, recover, authored route data and flame defeat |
| Mystic Marsh | `1f3` | 15 | Rhino snails: emerge, watch, horn strike and charge defeat |
| Mystic Marsh | `1fc` | 8 | Hanging monkeys: face Spyro, two spits at the recovered animation times, projectile collision and defeat |
| Mystic Marsh | `1fd` | 9 | Platypuses: aquatic routes, surface/dive animation cycle, contact damage and charge defeat |

The profiles use the supplied retail models and clips. Evidence is in the class cases under `data/analysis/level-scripts/` for overlays 38 and 58. Some movement speeds, Blubber attack choreography and launch offsets remain native approximations; full quest-linked behavior has not been translated. Ghidra's selected-function decompilation timed out for these two overlays; their raw MIPS disassembly remains available. Do not describe those cases as matching decompilations.

Platypus playback follows the recovered clip sequence `0 → 1 → 5 → 6 → 7 → 2 → 0`. The switches from 1 to 5 and 7 to 2 reset the mesh and animation endpoints, as at `8007f928`, `8007fb3c` and `800801b8`; these clips have incompatible vertex layouts and cannot be blended. Submerged platypuses reject attacks until they resurface. Pose sampling now runs throughout every tested enemy's combat and defeat sequence, including both endpoints used by the renderer.

The shared dialogue parser now reads **162 NPC conversation records across the 41 scene records**, including the Breeze Harbor workers, Mystic Marsh water wizards, Basil and the Professor. E starts available conversations, plays the mapped speech and presents supported response/repeat links. Inspector → **Play beside NPC** works for mapped speakers. The dialogue camera uses the three resident views at `8006163c`, with terrain obstruction fallback. Beaky and Basil were captured speaking in their levels.

**Coverage limits:** parsing a conversation does not implement its associated quest. Purchases, conditional rewards, minigames, trade chains, level machinery, many other enemy families and quest-triggered camera events still need their individual scripts. New NPCs do not force automatic greetings where those conditions are unknown. Prop occlusion can still affect a dialogue view.

## Cinematic playback

The native player now reads the retail movie directory at `80064914`, chunked actor animations, authored camera positions/rotations/cuts and the embedded mono SPU soundtrack. It supports pause/skip and 60 fps capture with sound.

- **35 standard-level intro/outro sequences** are mapped, including Breeze Harbor and Mystic Marsh. Glimmer has only an outro.
- First entry into an unvisited standard level automatically plays its mapped intro. Inspector → **Play retail intro / Play retail outro** allows review. Enter/Escape skips.
- `--cinematic intro` and `--cinematic outro` work with headless captures.
- **11 story streams** and the existing title stream also decode. The title retains its separate looping/audio behavior.

All **47 mapped streams / 92,178 timeline frames** passed camera, pose and soundtrack bounds checks. This is decoder coverage, not proof that every scene is visually finished. Standard-level actor/camera playback was inspected in Breeze Harbor and Mystic Marsh. Story-specific static props, text, particles and other scripted effects are incomplete; automatic outro/story triggers also await campaign quest progression. Those omissions must not be described as complete cutscene support.

## Validation and local footage

Artifacts are in `data/validation/misc-20260920/`:

- `charge-jump-winter.mp4`: clear side view of ordinary charge-jump ascent/descent, from the installed executable; 60 fps with sound.
- `charge-jump-arc.mp4`: the same fix in Glimmer. Earlier `regular-charge-jump.mp4` predates the body-pitch correction and is superseded.
- `charge-animation-tests.log`: actual retail clip/mesh integration at 30/60/144 Hz, both charge-input orders, with and without Superfly; 4,761 checks. Ordinary charge-jump pitch reaches approximately +48 to −26 degrees in the fixture.
- `jump-oracle.json`: 504 comparisons / 64,002 executed MIPS instructions, zero mismatches for the isolated vertical equations. Horizontal helpers and collision are outside this oracle's scope.
- `levels-expanded.log`: 53,785 checks including all 52 added enemy instances, their rendered animation endpoints and all six surface/dive clips for each platypus. Combat fixtures isolate their actual models, animation metadata, parameters and defeat behavior from unimplemented quest activation. Existing Hurricos/Idol Springs encounters are checked in their actual terrain.
- `extensions.log`: 13,039 checks, including distant assisted pickup, proximity slowing, wall rejection, existing grip geometry, dropping, smashing and saved rewards.
- `aerial.log`: 26,482 checks, all 41 retail banks and Scorch sand coverage.
- `cinematics.log`: all 47 movie decoder checks.
- `breeze-dialogue.mp4`, `mystic-dialogue.mp4`, `mystic-intro.mp4`, `breeze-outro.mp4`: installed-build footage with sound. Raw BMP viewport captures omit the ImGui dialogue panel; the videos capture it.
- `breeze-combat.mp4`, `mystic-combat.mp4`: actual-level charge encounters from the main executable; both award one spirit. Mystic Marsh completes 600 captured frames after the platypus transition fix. All seven listed jump/combat/dialogue/cinematic videos were checked for 60 fps H.264 and 48 kHz AAC audio.
- `ctest.log`: the 20 standard test suites.

Final installed executable SHA-256: `6a262029ac36ac52af181d934092cea4153c0707aae73cad29563f72f1d85b19`. It was installed before the final enemy tests and Mystic Marsh combat capture. The 20-suite run predates that final, platypus-specific correction; the expanded enemy suite was rebuilt and rerun afterwards.

The cut-content investigation remains paused.
