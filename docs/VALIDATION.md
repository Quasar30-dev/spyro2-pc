# Validation snapshots

## Campaign portals and whirlwind elevators — 2026-09-23

Main executable rebuilt and installed **before** tests; installed/candidate SHA-256 both `2170D6BEF1233D4C6B34C76D0A1E35DEAA6C0698CD4EC89299A75B3A0C060B84`. **23 non-capture CTest suites passed**, including the new portal suite. The existing `terrain_renderer` suite was excluded because its fixtures write temporary BMPs. No photographs, videos or audio captures were generated.

The final retail test passed **34,277 checks**: all **42 portal apertures**, both viewing sides/side flags, **26 whirlwind routes** using actual level collision, **12 paired home transporters**, all three paid portal services, orb thresholds, gate/entry protections, checkpoint positions and fixed-step presentation independence. The lower count than the intermediate panorama test reflects rejecting one-sided back views instead of generating triangles there.

The installed executable completed a **740-frame**, hardware-rendered, non-recording `--portal-check --no-capture` run, with **36 assertions and five actual loader transitions**: Summer Forest → Glimmer → Summer Forest → Autumn Plains → Winter Tundra → Summer Forest. Health, gems, purchase bits, upgrades, arrival protection and remembered scenes survived. Installed/candidate hashes were compared again after the complete build.

Logs/report: `data/validation/portals-20260923/{portal-tests-final.log,ctest.log,integration-final.json}`. The final integration ran alongside compilation and is not a performance acceptance run: mean/P95 CPU submission 19.034/40.961 ms, maximum 1216.09 ms, 132 frames above 16.667 ms. The earlier functional run had mean/P95 12.261/11.901 ms with eight over-budget frames and a 465.614 ms maximum. Synchronous level loading still hitches; neither result establishes uninterrupted 60 fps or GPU frame time. See [implementation, RE evidence and remaining boundaries](PORTALS_AND_WHIRLWINDS.md).

## Retail dialogue, pause inventory and guidebook — 2026-09-23

The main `build/Release/spyro_workbench.exe` was rebuilt and installed before validation. Installed and candidate SHA-256 match: `6A6EE8F71B0671511A1169A3BB2ECE46983DB8DAC0B88681675DB6F91BBA5B5E`. All **23 CTest suites passed**. The final retail interface executable passed **82,215 checks**, including 514 dialogue passages, 70 choice passages, 32 book banks, font metrics/colours/accents, atlas bounds and gutters, page projection, reward idempotence and save round trips. Two actual S2SAVE8 files were read, written as S2SAVE9 and read again, preserving the checked totals, purchase flags, level count and current scene.

Dialogue, answer pages, pause inventory, book contents, level details and skill-point artwork now use decoded retail assets. PC file selection, save/load, camera/HUD/audio controls and workbench access remain available. S2SAVE9 stores level reward masks, boss/talisman flags, speedway results/best times and global skill points; older formats remain readable.

Earlier UI replays passed **46 dialogue/book assertions** and **69 PC-menu assertions**. These predate the final presentation/save refinements. Further captures were stopped at the user's request. A final video attempt exposed an encoder warm-up input timing issue; its script was corrected but not rerun. No successful final video is claimed. Logs are in `data/validation/retail-interface-20260922/`, including `ctest.log` and `interface-tests-final.log`.

Quest/boss/speedway reward callers remain incomplete, as do epilogue playback and movie-theatre navigation. Dialogue timing and page ordering are native approximations. [Retail interface evidence](RETAIL_INTERFACE.md) records recovered tables, controls and the remaining scope; this does not close the whole campaign backlog.

## Companion controllers, persistent bodies and campaign services — 2026-09-22

The main `build/Release/spyro_workbench.exe` was rebuilt and replaced **before** each gameplay test/capture. Final installed SHA-256: `58614D92B5CAD94CBB07F43924FCEC55EA1313DF70000EA9134DA9EC21B98190`. All **22 CTest suites pass**; the actual ImGui menu replay passes **69 assertions**, including repeated Exit Level travel with retained inventory, collected objects and the paid bridge.

Retail-backed suites pass **46,887 companion**, **13,060 PC extension**, **26,482 aerial/sand**, **19,396 universe**, **102 progression** and **11,960 campaign** checks. Companion coverage includes 40 Sparx banks and 11 underwater-bubble banks; universe coverage includes 256 generated worlds, 84,942 route samples and 8,362 actors. Reports are `data/validation/limitations-20260921/final-*-tests.log` and `all-ctest.log`.

Sparx has the recovered idle substates, gem-hint input, swimming transforms and original bubbles, butterfly roaming/feeding timers and extra-life intervals. Collected gems spawn their original value meshes. Current S2SAVE8 retains v6 butterflies, v7 held/thrown bodies and active flight/power state, and v8 campaign purchase flags. A loaded flying player receives its correct pose before the first render. Genuine v4–v7 fixtures guard earlier layouts.

Glimmer Moneybags' 100-gem transaction opens three authored bridge tracks: 213 expanded render vertices and 76 collision triangles move under the scripted camera. Cancellation, insufficient funds, duplicate charging, camera input lock, unpaid/paid reload and source/editor isolation are covered. The bounded MIPS oracle matches **76 initial collision triangles and 282 near-track states over 16,088 instructions**. GPU tests cover moved opaque and translucent geometry, collision buffers and switching back to the editor copy.

Exit Level follows the executable's 26 return routes and finds all 23 ordinary home-world doorways. The three boss returns use home spawns. Exact camera easing, retail portal fly-in/out sequences and quest-controlled portals remain open; see [campaign sources and scope](CAMPAIGN_AND_TERRAIN.md).

Inspected native footage in `data/validation/limitations-20260921/`:

| File | Duration / frames | Streams |
| --- | --- | --- |
| `glimmer-bridge.mp4` | 11 s / 660 | H.264 60/1 fps; 48 kHz stereo AAC |
| `feeding-and-pursuit.mp4` | 8 s / 480 | H.264 60/1 fps; 48 kHz stereo AAC |
| `sparx-swimming.mp4` | 5.5 s / 330 | H.264 60/1 fps; 48 kHz stereo AAC |

The bridge offer, extending/open bridge and Summer Forest arrival were visually inspected. `final-build-and-media.json` records matching candidate/installed hashes and checked media streams. An isolated paced bridge run completed **660 ticks in 11.011 seconds**, with **2.007 ms mean / 2.678 ms P95 CPU work** and one **30.051 ms** frame. These measurements cover CPU submission and pacing, not GPU timestamps or sustained performance across all scenes. The broader open backlog remains explicit in [REMAINING_WORK.md](REMAINING_WORK.md); this snapshot does not close the whole port.

## Retail main menu and PC file flow — 2026-09-19

All **19 CTest suites pass**. The new retail title/preferences suite passes **2,417,047 checks**; the native `--menu-check` replay passes **56 assertions** through real ImGui inputs, covering New Game, pause/input isolation, PC Save/Save As/Load, cancellation, invalid saves/sources, retained live state, and universe saves. A fresh process also reopens the remembered source and recent files. Workbench upgrade regression still produces all three J melee strikes.

The original title assets are decoded from WAD 95/96: 9 actors, 14 cinematic segments, logo, Press Start and file-menu sprites. The inspected `retail-main-menu-60fps.mp4` contains 720 H.264 frames at 60/1 fps and 12 seconds of non-silent 48 kHz stereo AAC. Native screenshots show populated retail file panels and saving from the pause menu. [Retail menu evidence](RETAIL_MAIN_MENU.md) records addresses, reproduction commands and remaining intro/transition work.

## Original skies, basket winding and level behaviours — 2026-09-19

All **18 CTest suites pass**. The retail sky suite passes **338,843 checks**, covering 41 banks, 1,590 regions and 112,385 triangles. `sky-oracle.json` reports exact agreement with the original MIPS visibility routine at **20,992 camera positions**, using 4,580,266 instructions. Every scene was also loaded and rendered by the native workbench; `sky-scenes/audit.json` records geometry and draw counts beside its 41 captures.

The additional level-behaviour suite passes **193 checks** with retail assets: all 22 Hurricos enemies and six Idol Springs metal idols reach attacks, connect hits and complete defeat. All eight robots additionally pass an actual player-charge collision test. Linked commands, patrols, chase/return, frame-selected damage shapes, debris, attack immunity, heavy-container save/load and gem accounting have regression coverage. The scene audit finds 25 heavy containers. Existing retail progression passes 102 checks; materials pass 95,962, aerial/sand 26,482, and PC extensions 12,994. The extension suite caught and verified a fix for double advancement of carried-container interpolation.

The basket's closed exterior and the new skies were visually inspected. Three final native recordings have matching video/audio durations, **1418×904 H.264 at 60/1 fps**, and non-silent **48 kHz stereo AAC**:

| File in `data/validation/` | Duration / frames | Evidence |
| --- | --- | --- |
| `winter-tundra-sky-60fps.mp4` | 6 s / 360 | Original star field, sky colours and clouds behind the mountains; camera pans clear of the castle |
| `hurricos-robot-charge-60fps.mp4` | 4 s / 240 | Actual charge defeat, ten original debris pieces and one spirit |
| `hurricos-command-combat-60fps.mp4` | 8 s / 480 | Grinder commands its linked robot; subsequent combat damages Spyro |

`sky-levels-media.json` contains stream and audio checks. These are fixed-rate offline recordings, not a sustained live-performance benchmark. Other enemy classes, bosses, NPC quests and complete level scripts remain unfinished; [coverage and RE evidence](SKIES_AND_LEVEL_BEHAVIOURS.md) states the limits.

Windows held the user's running standard executable during the final link. The final native build was linked successfully as `spyro_workbench_candidate.exe`, captured and verified, then copied with matching SHA-256 to **`build/Release/spyro_workbench_ready.exe`**. The existing `Run Workbench.cmd` launcher selects this newer verified build; the running session was left untouched. Evidence: `sky-levels-candidate-build-final.log`, `sky-levels-core-final.log`, `sky-levels-ctest.log`, `level-behaviour-retail.log` and `sky-levels-build.json`.

## Workbench upgrade persistence, aerial moves and Scorch sand — 2026-09-19

Release rebuilt successfully. **16/16 CTest suites**, **26,482 retail aerial/sand checks across all 41 banks** and **12,994 retail extension checks** pass. New checks cover latched glide after releasing jump, hover's height cap, one/two wing boosts and rejection of a third, held-input behavior, disabling upgrades, recharge on landing, headbash collision/recovery and a retail basket hit, sand-only downhill response, jumping out of the slide, and format-4/5 save compatibility. The final hover/boost timing refinement was linked as `spyro_workbench_candidate.exe` while Windows held the user's open workbench, then verified with the native regression and aerial recording before installing `spyro_workbench_ready.exe`. `Run Workbench.cmd` selects that verified update while it is newer than the normal executable. The original process remains untouched; `workbench-candidate-build.log` is the final successful link log.

The workbench previously reconstructed the game on Play without retaining upgrades selected in preview or after Stop. The same menu action, Play/Stop and scene-switch paths now retain those choices. `upgrade-workbench-check.json` confirms enabled progression, purchased glide tier 2, retained points/tiers and **three target-independent melee swings from actual ImGui J events**. A separate process reloaded the format-5 save and retained all tiers, unlocks, enablement and points (`upgrade-reloaded.json`). The menu now distinguishes progression from unlocks and provides **Unlock all and activate powers** for immediate testing, including required base powers. Play and menu purchases return keyboard focus to the viewport.

The requested chest drop was recorded first; a separate melee recording shows the more forceful swipes and leaping spin. New movement clips then verify the Scorch work. All four clips are native 1418×904 H.264 at **60/1 fps**, with **48 kHz stereo AAC** and matching audio/video durations:

| Recording in `data/validation/` | Duration / frames | Observed result |
| --- | --- | --- |
| `chest-drop-close-60fps.mp4` | 4 s / 240 | Sunny Beach chest: one pickup, one drop, one smash; visible release, falling chest and metal shards |
| `melee-ferocious-60fps.mp4` | 3 s / 180 | Three swings, zero hits; articulated swipes, forward lunges and leaping tail spin |
| `scorch-sand-slide-60fps.mp4` | 6 s / 360 | 220 ticks sliding on Scorch's authored sand, followed by ordinary ground motion |
| `scorch-aerial-moves-60fps.mp4` | 8 s / 480 | Two boosts, rejected third request, one hover and one headbash impact, with jump released |

Frames were inspected through each action. These deterministic recordings preserve 60 simulation frames per second and are not a claim of sustained stall-free live performance. Logs: `aerial-build.log`, `aerial-test-build.log`, `aerial-ctest.log`, `aerial-retail-tests.log`, `aerial-extension-tests.log` and the capture JSON / stream reports. [Aerial/Scorch evidence](AERIAL_AND_SCORCH.md) and [upgrade controls](ONLINE_AND_UPGRADES.md) record the recovered constants and remaining fidelity work.

## Carry joint fitting and target-independent J attacks — 2026-09-19

Release passes **15/15 CTest suites** and **12,746 retail extension checks**. The grip regression now checks every paw vertex against an exterior supporting plane, rigid paw dimensions, both limb segment lengths, separate foot spacing, and unchanged head/wing geometry. Eight complete flight poses and a 300-tick turning/climbing sequence are checked at three presentation fractions, including heading wrap. The carried object follows pitch/roll; shoulders/hips, knees/elbows and ankles have separate transforms, with each knee volume kept together. Earlier checks that only put a tip midpoint on the lid were insufficient and have been replaced.

The melee checks exercise a buffered J press between simulation ticks, holding charge, empty-space attacks, airborne starts and disabling the optional feature. The native `melee-empty` replay injects ImGui J-key down/up events through the ordinary workbench input path. It records **three swings and zero hits**, confirming that all three attacks play without a nearby target. Only actual damage remains conditional on contact.

Close native captures, visually inspected at several points through their motion:

| Recording in `data/validation/` | Video | Audio | Evidence |
| --- | --- | --- | --- |
| `carry-joints-close-60fps.mp4` | 1418×904, 360 frames / 6 s, 60/1 fps | 48 kHz stereo AAC | Close orbit around Spyro's separated paws, joint bends and sloped-lid contacts in Sunny Beach |
| `melee-j-empty-space-60fps.mp4` | 1418×904, 180 frames / 3 s, 60/1 fps | 48 kHz stereo AAC | J-triggered left/right swipes and spin with no enemy or container hit |

FFprobe verifies matching audio/video durations. Logs: `carry-melee-final-build.log`, `carry-melee-final-tests.log`, `carry-joints-retail.log`, each capture JSON and its `-streams.json` / `-audio.log`. The captures are deterministic native renders with sound; their encoded 60 fps is not a new sustained-performance benchmark.

## Object materials, optional abilities and internet exploration — 2026-09-19

The final Release rebuild passes **15/15 CTest suites**. The retail extension test completes **470 checks**: disabled upgrades preserve base movement, tier speeds and purchases work, homing respects walls, splitting stops after one generation, melee has bounded hit windows, and carried baskets and heavy chests follow, drop, throw and smash. Four claw contacts stay on the upper mesh through three flight poses and interpolated pitch, roll and wrapped headings. Paws retain their width; the original head and wing vertices remain unchanged. A heavy impact creates sixteen original class-134 shards, releases a stored gem exactly once, and preserves destruction/contents through save/load. Empty authored chests do not invent gems, and fragments expire.

The material audit passes **95,959 checks across all 41 scene banks**, covering **4,418 placed model instances / 1,802,978 face-instance combinations**, including **22,368 normal-lit faces and 2,160 reflective faces**. All 41 scenes also rendered in separate native workbench runs without a rendering error. **24 D3D11 WARP pixel checks** verify the recovered lighting, reflection sampling, blend modes and shared transparency ordering across objects, Spyro and water. Corrected heavy/bullseye chest bodies and the reflective shell were visually inspected. [Object rendering](OBJECT_RENDERING.md) records the original instruction evidence and remaining limitations.

Native carry captures in `data/validation/`:

| Recording | Frames / duration | Verified action |
| --- | --- | --- |
| `heavy-grip-drop-wide-60fps.mp4` | 245 / 4.083 s | Sunny Beach heavy chest, grip → drop → seabed impact and visible shards |
| `heavy-chest-grip-and-smash-60fps.mp4` | 220 / 3.667 s | Shady Oasis bullseye chest, drop → visible floor impact and shards; the early wide-camera view is obstructed by the overhead walkway |

FFprobe confirms **60/1 fps H.264 and 48 kHz stereo AAC**, with audio/video durations matching within 1 ms. The audio peaks are -5.0 and -9.1 dB respectively. `heavy-grip-side.bmp` and `heavy-grip-front.bmp` show the close carrying pose. The workbench saved the destroyed Sunny Beach chest (record 62 / class CB) to `heavy-grip-drop.s2save`; a separate process loaded it successfully, retaining the removal and original gem totals. A 300-tick paced run measured CPU mean **4.86 ms**, P95 **9.43 ms**, maximum **49.37 ms**, with **11 frames above 16.67 ms** and 5.21 seconds elapsed. These short CPU/submission measurements include scheduling and driver waits; sustained stall-free 60 fps is not established by this run. Recording retains every simulation frame.

Other native clips verify homing/splitting (`upgraded-superflame-60fps.mp4`, 37 homing ticks / three child flames) and melee container destruction (`melee-container-60fps.mp4`, two containers). Four relay unit tests pass. Two separate native clients exchanged snapshots through the public temporary Cloudflare HTTPS tunnel and rendered both Spyros together; `online-two-spyros-60fps.mp4` contains 420 frames / 7 seconds with game audio. A longer host/guest journey completed two portal transitions and brought both clients to universe seed 2026, world 1, with **730 host / 669 guest exchanges**. This is a two-process public-internet test on one PC, not a two-physical-PC test. Shared enemies, combat, carried objects and progression remain outside the initial online exploration scope.

Primary logs: `carry-grip-final-build.log`, `carry-grip-retail.log`, `object-material-retail-final.log`, `object-renderer-pixels.log`, `object-scenes/result.json`, `online-journey-host.json`, `online-journey-guest.json`, the clip `-streams.json` / `-audio.log` files and `carry-grip-performance.json`. [Online and upgrades](ONLINE_AND_UPGRADES.md) documents setup, controls, save compatibility and scope.

## Procedural universe and original exit portals — 2026-09-19

The Release build passes **13/13 CTest suites**. The retail universe executable completes **19,398 checks**, including 1,000 repeatable layout plans and **256 generated retail-asset worlds**, with **84,942 route samples** and **8,362 grounded actors** checked. Every accepted layout reserves the landing area, a continuous route and portal approaches, checks actor animation envelopes and scenery overlap, and rejects obstructing output. Eight worlds were traversed end to end by the actual player controller, including walking through the original Return Home arch. Four generated encounters also verify enemy reactions, flame defeat, spirit awards and saved defeat state.

The exit uses **Glimmer's original class `1AA` model**, the five-point aperture from the mounted executable, and the original coloured destination artwork from the scene archive. The arch retains solid collision while its opening mask remains passable. Space uses the same portal at four times scale. Separate checks cover fast swept crossings, arrival cooldown, automatic flight without enabling the Superfly cheat, boosting, wing streak length, and unobstructed approaches past the decorative planets.

Save checks cover 64-bit addresses, independent world pickups, revisits, seed/campaign isolation, generator-version rejection and version-2 retail compatibility. The workbench journey saves a two-transition trip and is reopened in a separate process: **world 1, 35 gems and position `[-3.34133, 8.8049, 32.9288]`** are retained. A separate process also reloads the existing version-1 `glimmer-progress.s2save`, retaining 0 gems, 1 spirit and health 2. Universe progress and retail campaign saves remain separate.

Fresh native recordings were checked with FFprobe:

| Recording in `data/validation/` | Video frames | Duration | Travel |
| --- | ---: | ---: | --- |
| `universe-journey-60fps.mp4` | 2,100 | 35.000 s | Ground world 0 → Starways → ground world 1 |
| `universe-space-60fps.mp4` | 360 | 6.000 s | Fly through the original portal to ground world 1 |

Both are **60/1 fps H.264 with 48 kHz stereo AAC** and matching audio/video durations. The journey audio measures -22.7 dB mean / -6.4 dB peak. `universe-original-portal.png` and `universe-starways.png` are frames extracted directly from these clips. The deterministic replay steers the actual controller; travel occurs through the normal portal logic.

A separate paced ground run completed **600 frames / 600 ticks in 9.999 seconds**, measuring 0.98 ms mean / 1.37 ms P95 CPU work and one 18.69 ms startup frame. The offline journey recording measured 9.75 ms mean / 25.49 ms P95, with a 1,118 ms destination-loading spike; recording retains every simulation frame despite encoding and loading delays. These CPU/submission samples and the encoded 60 fps cadence do not establish sustained full-game GPU performance. Scene loading currently pauses play.

The four theme captures and the original portal were inspected in actual workbench output. Initial scope is compact generated islands using four retail terrain banks and curated Glimmer actors. Original level buildings, broader enemy scripts, seamless streaming and a full procedural editor are future work. See [procedural universe controls and evidence](PROCEDURAL_UNIVERSE.md). Build and retail-check logs are `data/validation/universe-build.log` and `universe-retail-final.log`.

## Powered attacks, retail VFX and 60 fps capture — 2026-09-19

Release rebuilt and **12/12 CTest suites passed**. The retail power-up executable passed **146 checks**, including imported effect banks/materials across all 41 mapped scenes, native gate inventory and requirements, flight, shot windup/cooldown, green-bomb colour override, actual projectile-to-terrain impacts, each explosion's component effects, growth, pause and eventual cleanup. Both Robotica Farms heavy doors reject Superflame, accept the green bomb, lose their collision and stay destroyed after a save/load round trip. The D3D11 WARP renderer passed **20 pixel checks**, including all four original Moby blend equations, transparent pixels and the opaque part of a translucent texture.

The earlier dark solid effects were caused by discarded Moby TPAGE/STP transparency flags. Fresh native captures were inspected after restoring the material passes and implementing the original explosion scripts. Green bomb now emits its repeated `146` bursts, green `3A` particles and `46` sparks, with the original ground offset and camera shake. Superflame has its trail, impact dome, five spokes, growing clouds and fire/spark particles. Underwater Superflame now uses its spiralling shot, trail copies, blue expanding meshes and rising `27` bubbles.

| Final recorded run | Encoded frames | Duration | Observed result |
| --- | ---: | ---: | --- |
| `green-bomb-door-60fps.mp4` | 240 | 4.000 s | 1 shot, 1 blast, 60 burst meshes, 1 heavy door destroyed; Spyro stays purple |
| `superflame-impact-60fps.mp4` | 240 | 4.000 s | 1 shot, 1 blast, 12 trail/impact meshes; heavy door survives |
| `underwater-superflame-60fps.mp4` | 480 | 8.000 s | 2 shots, 2 underwater blasts, 61 trail/impact meshes |

FFprobe verified **60/1 fps H.264** and **48 kHz stereo AAC**, with matching audio/video durations in all three files. Audio peaks were -10.1, -8.9 and -9.0 dB respectively. Every run finished after the visible effects expired. Each video has five unrecorded setup renders; simulation starts at time zero, so a 240-frame clip reports 239 simulation ticks and 245 total renders.

The final paced green, orange and underwater blast runs measured CPU P95 **8.19 ms**, **5.84 ms** and **7.85 ms**. Starting the encoder/audio mix caused a 203 ms / 124 ms / 55 ms initialization spike; the green run had two further CPU frames above 16.67 ms. Recording retains every deterministic simulation frame, so these stalls do not shorten the clip or drop encoded frames. These are short CPU/submission measurements, not a guarantee of sustained full-game GPU performance.

Logs and artifacts are in `data/validation/`: `power-vfx-final-build.log`, `powerup-retail-final.log`, each clip's `-streams.json`, the `*-finished.json` run reports, and `green-bomb-retail-vfx-*.png`, `superflame-retail-vfx-*.png`, `underwater-retail-vfx-*.png` extracted from the actual recordings. [Powered attacks, flight and capture](POWERUPS_AND_CAPTURE.md) records controls, commands, original instruction evidence and remaining PSX rendering/mission-script limitations.

## Saving, Glimmer enemies and event-driven HUD — 2026-09-19

Release rebuilt successfully with no reported warnings; **11/11 CTest suites passed**. The progression executable completed **101 checks** with the retail copy: save/load/overwrite and invalid-file handling, cross-scene progression, pending gems and Sparx recreation, conversation restoration, enemy attack rules/damage/death, HUD events/timings/recall and all **41 HUD/model banks**. Existing traversal, swimming, secondary animation, audio, NPC and terrain tests also passed.

A 240-frame workbench run flamed Glimmer's large lizard (record 8), collected a nearby gem and wrote a PC save through the workbench save action. A separate workbench process mounted the CUE/BIN copy and loaded that file: **2 gems, 1 spirit and health 3** were retained. The loaded HUD remained hidden at the restored totals. The retail gameplay probe separately verified actual club damage and saving during an enemy's death animation.

HUD captures were visually inspected at the event, after loading and after the counters had timed out: `data/validation/hud-spirit.bmp`, `hud-loaded.bmp`, and `hud-hidden.bmp`. At 480 simulation ticks the two event counters had disappeared without losing the totals. That unpaced run measured 2.78 ms mean / 4.96 ms P95 CPU frame work; this measures CPU submission rather than GPU timing or a full-game performance guarantee. The native simulation remains fixed at 60 Hz, with independent 30/60/144 Hz presentation checks.

See [saving, enemies and HUD](SAVING_ENEMIES_AND_HUD.md) for scope and remaining work. Enemy coverage is the **14 Glimmer lizards**, not every level's scripts. Raw evidence and logs: `data/validation/progression-build.log`, `progression-retail.log`, and the capture JSON files.

## Water materials — 2026-09-16

Release rebuild and all **7/7 CTest suites passed**, including 22 new terrain decoder checks and 13 D3D11 WARP renderer checks. The GPU checks compare actual pixels for the recovered blend equations, STP/opaque separation, submerged Spyro visibility, foreground depth and overlapping transparent surfaces.

All 41 scene archives decoded with their material references retained. A separate audit directly from CUE/BIN checked 2,624 terrain materials, including 44 additive materials, and confirmed agreement between each material's near quadrants. Native water captures in slots 15, 19 and 33 were visually inspected. An opaque Glimmer capture is byte-identical to the pre-change supercharge control.

| Water run | Frames / ticks | Elapsed | Mean CPU | P95 CPU | Maximum CPU | CPU frames >16.67ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Summer Forest swimming | 800 / 800 | 13.324 s | 1.597 ms | 2.108 ms | 10.910 ms | 0 |
| Slot 43, 5,108 transparent triangles | 108 / 108 | 1.795 s | 3.287 ms | 4.310 ms | 20.225 ms | 1, at startup |

The swimming run visits all five water motion states and returns to surface idle. These are short CPU/submission and wall-clock pacing measurements. See [WATER_RENDERING.md](WATER_RENDERING.md) for evidence, commands, artifacts and remaining visual limitations. The full historical retail validation script was not rerun for this material-only change.

## Earlier snapshot — 2026-09-14

Release x64 build with MSVC 14.51, Windows SDK 10.0.26100.0 and the local D3D11 hardware renderer. Reproduce with `tools/build.ps1`, then `tools/validate-retail.ps1 -NoBuild` with the retail CUE or an explicit `-Game` path.

- CTest: 3/3 suites passed. The core executable reports 156 checks, including primary animation transitions and fall exits, forward glide without input, glide braking and steering, Sparx following / collection / interpolation, camera fixed-point steps, presentation cadence, model/world scale, projection, collision, animation decoding, object bounds, gem pickup/material rules and editor persistence. The interaction executable adds 41 checks for container collision and attack rules, fragment lifecycle, vase heat/wobble, gem bounce/delivery, flame geometry/materials/expiry and presentation independence. Python's five disc tests also passed.
- Retail CUE/BIN mounted directly: 20 files, 197 WAD slots, expected executable CRC.
- All 41 mapped level archives and Spyro animation banks decoded successfully: 404,576 collision triangles and 16,690 animation frames across repeated common / level banks.
- The native primary animation controller completed the idle, run, jump, fall, charge, glide and glide-brake transition sequence in every mapped bank, including the fall exit. The workbench uses this controller during gameplay.
- Animated object banks decoded 2,430 clips / 24,196 frames across repeated scene banks. Each clip's native playback state was exercised for 600 VBlanks. Distinct Sparx poses, both Sparx clips, Hunter and the ImGui model inspector were captured and visually inspected.
- All scene banks contain 5,200 decoded object records and 1,669 loose gems. Native direct-pickup validation passed for all 1,669, checking player collision, gem accounting and authoring isolation.
- The inspector verified 652 retail container charge/release/delivery cases, containing gem value 3,575. Each produced twelve valid retail fragment meshes and the expected gem denomination while preserving the authored records. Six native container captures checked basket charge/flame, bounce and Sparx delivery, vase flame resistance/charge, and bottle charge. Original textured fragments, the released gem and the post-collection scene were visually inspected.
- Flame rendering loads eight trajectories, two age envelopes, four tip colors and the original 32x128 texture with each of the 40 ordinary scene palettes. The inspector checked 16,896 integer/half-age and impact-cutoff poses. Six native captures cover emission, trails, side view, fading and expiry; the vase-resistance check now also requires actual heat. [Flame evidence](FLAME_RENDERING.md) distinguishes recovered data from native presentation choices.
- Flame captures were visually inspected after correcting the optional capture camera's pivot. The retail tips and trails are visible and expire. The side view also exposes unfinished head-gesture timing / mouth alignment; that remaining action-state work is not counted as validated.
- Viewport captures visually inspected in Summer Forest and Glimmer: actual textured Spyro, recovered spawn heading, retail terrain, static and animated objects and camera framing. Hunter, sheep and Glimmer's characters now appear in their imported positions. Separate red, green and blue gem captures verify cap winding, bright facets and the recovered additive material pass.
- Gameplay captures confirmed run clip 2 at replay frame 110, free glide clip 16 at frame 238 and brake clip 24 at frame 253. The replay releases forward input during glide, then applies Down. Sparx spawned in both tested levels and delivered one gem during the movement replay, awarding value 2 exactly once.

The final instrumented paced runs produced:

| Run | Frames / simulation ticks | Elapsed | Mean CPU work | P95 CPU work | Maximum CPU work | CPU frames >16.67ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Summer Forest idle | 120 / 120 | 1.992 s | 3.598 ms | 5.212 ms | 16.740 ms | 1 |
| Summer Forest movement replay | 360 / 360 | 5.993 s | 2.784 ms | 4.396 ms | 12.516 ms | 0 |
| Glimmer idle | 90 / 90 | 1.490 s | 2.839 ms | 4.123 ms | 13.004 ms | 0 |
| Basket charge | 90 / 90 | 1.489 s | 2.096 ms | 3.874 ms | 10.118 ms | 0 |
| Basket flame / bouncing gem | 90 / 90 | 1.490 s | 2.335 ms | 5.038 ms | 10.882 ms | 0 |
| Basket gem delivered by Sparx | 180 / 180 | 2.994 s | 2.720 ms | 5.103 ms | 11.273 ms | 0 |
| Vase resists flame | 90 / 90 | 1.489 s | 1.758 ms | 2.896 ms | 11.390 ms | 0 |
| Vase charge | 90 / 90 | 1.489 s | 1.810 ms | 3.567 ms | 10.370 ms | 0 |
| Bottle charge | 90 / 90 | 1.489 s | 2.325 ms | 4.476 ms | 12.332 ms | 0 |

The replay observed all seven native motion states (`127` bitmask). The Summer Forest idle run had one over-budget frame at startup: 16.740 ms on frame 1. The movement replay and six container runs had no CPU frames over 16.67 ms. Earlier builds recorded a 30.584 ms vase-charge spike at frame 7, before the attack, and an unexplained 152.184 ms idle spike at frame 41. These outliers remain unresolved; stable frame delivery has not been established across long sessions.

CPU work is the wall time spent in the native update, ImGui, render submission and `Present`; it includes driver waits and scheduling delays, and is not a GPU timestamp measurement. Elapsed time includes the final capture and has no post-final-frame pacing sleep. These short runs do not establish performance of the still-unimplemented complete game. The three pose captures are unpaced and are visual checks, not 60 fps benchmarks.

Local detailed reports and captures:

- `data/validation/retail-inspection.txt`
- `data/validation/retail-camera-idle.json` / `.bmp`
- `data/validation/retail-camera-motion.json` / `.bmp`
- `data/validation/retail-camera-glimmer.json` / `.bmp`
- `data/validation/retail-gem-green-final.bmp`, `retail-gem-red-final.bmp`, `retail-gem-blue-final.bmp`
- `data/validation/sparx-frame0.bmp`, `sparx-frame1.bmp`, `sparx-clip1.bmp`, `sparx-studio.bmp`, `object-444.bmp`
- `data/validation/animated-objects-build.log`, `animated-objects-validation.log`, `animated-objects-inspection.txt`
- `data/validation/traversal-build.log`, `traversal-retail-validation.log`
- `data/validation/retail-run.bmp`, `retail-free-glide.bmp`, `retail-glide-brake.bmp` and their JSON reports
- `data/validation/breakables-build.log`, `breakables-retail-inspection.txt`, `breakables-retail-validation.log`
- `data/validation/basket-charge.bmp`, `basket-flame.bmp`, `basket-delivery.bmp`, `vase-resists-flame.bmp`, `vase-charge.bmp`, `bottle-charge.bmp` and their JSON reports

Full camera equivalence, remaining ground / jump movement equations, scripted cameras, collision flags, special object materials, most object behavior, additional Sparx scripts and gem feedback, enemy AI, audio, progression and editor geometry authoring remain open in [RE_NOTES.md](RE_NOTES.md). [Traversal evidence](TRAVERSAL_AND_TRANSITIONS.md), [companion evidence](SPARX_COMPANION.md), [object and gem evidence](OBJECTS_AND_GEMS.md) and [animated object evidence](ANIMATED_OBJECTS.md) distinguish recovered behavior from remaining work.

- `data/validation/flame-build.log`, `flame-retail-validation.log`
- `data/validation/flame-before.bmp`, `flame-emission.bmp`, `flame-trails.bmp`, `flame-side.bmp`, `flame-fading.bmp`, `flame-expired.bmp` and their JSON reports
