# Port completion ledger

Scope confirmed on 21 September 2026: the whole port's documented backlog, including gameplay, presentation, online play and authoring. Cut-content exploration remains paused. This ledger is an inventory, not a completion claim. An item closes only with implementation and relevant verification; decoding an asset or playing its idle animation does not close its gameplay script.

The installed workbench is replaced **before** gameplay tests and recordings. Companion/campaign evidence lives in `data/validation/limitations-20260921/`; the dialogue/guidebook pass is recorded in `data/validation/retail-interface-20260922/`. Further captures were skipped at the user's request. The 23 September all-level audit and Breeze Harbor / gates / speedways implementation are recorded in [Level systems](LEVEL_SYSTEMS.md) and [All-level audit](ALL_LEVEL_AUDIT.md).

## Companion and common objects

| ID | Work | Status / evidence |
| --- | --- | --- |
| C01 | Save released butterflies and the extra-life drop counter | Implemented since S2SAVE6, retained in current S2SAVE9; load/consume/no-duplicate and genuine older-format fixtures checked. `final-companion-tests.log`. |
| C02 | Sparx idle substates, camera antics and movement interruption | Implemented from Summer Forest `80072ae0..80073378`: seven states, original weights/timers and movement interruption. Shared RNG/collision differences remain C09. |
| C03 | Swimming companion transform and original underwater bubble | Implemented from `800720b4` / class AA `8007740c`, including pitch/roll and all 11 bubble banks. `sparx-swimming.mp4`. |
| C04 | Gem hint input, target selection and pointing | Implemented: hold T (pad L1+R1+R2), loose gems and gem-bearing containers; `80072304..80072464`, `80072978..80072adc`. |
| C05 | Butterfly wandering, circling, feeding timer and extra-life intervals | Implemented bounded roaming/circling, pursuit, consumption gate, 1800-VBlank lifetime, feeding timer and reset-based 7/9/10 drop intervals. Shared RNG and visibility feedback remain C09. |
| C06 | All fodder scripts and original death fragments | Partial; sheep release works; animal AI and other fodder families remain. |
| C07 | Original gem-value popups, pickup sparkles and spirit flight | Partial. Done: class 104 gem digits (30 Hz motion interpolated at 60 Hz, 96-update expiry); the class-68 spirit flight to the power-up gate, with its sound, trail and type-0C arrival sparkles; the gates' class-104 requirement counters; open-gate icons, ambient sound, type-04 sparkles, crossing lightning, type-47 crackle and icon sound. Open: the terminal type-47 burst, the pickup type-0C sparkles, and a locked gate's occasional class-1CB motes. |
| C08 | Container impact effects, special surfaces/water following and charge response | Partial; common breaks, gems, sounds, fragments and persistence work. Two authored surface-follow vases need their scripts. |
| C09 | Complete dynamic collision callbacks and original shared RNG consumption | Open; local retail generators and terrain queries do not establish complete equivalence. |

## Campaign, levels and interface

| ID | Work | Status / evidence |
| --- | --- | --- |
| G01 | Every enemy family and boss, activation, routes, attacks and deaths | Implemented through the original scripts: every campaign scene except Ripto's arena runs its non-native mobys (enemies, Crush, Gulp, scripted objects) in the overlay interpreter, with engine attacks, retail hurt words and solid-moby pushes bridged. Glimmer, Hurricos, metal idols, Breeze Harbor and Mystic Marsh keep their native combat. 10 October pass: script cameras (look/focus/placement/other modes), the reused-spawn-slot fault that disabled Shady Oasis and Dragon Shores, and script-owned Bombo. Engagement and quest censuses run over all 28 scenes: zero faults, every spirit-bearing class reacts, and the remaining non-dying classes are retail-consistent. Party Crashers plays end-to-end. 14 October: Crush and Gulp play their full loops (health HUD, original damage, death, outro request, boss award, departure and arrival in Autumn Plains / Winter Tundra; `spyro_boss_fights`); the outro cinematics are not played. Script particles remain open. [Retail scripts](RETAIL_SCRIPTS.md). |
| G02 | Friendly actors, machinery, linked workers and challenge triggers | Partial; Breeze Harbor burners, fan activation, platform/ship movement, cannon projectiles, mines and trolley route/gear/retry logic are now connected. Pressure-launcher behavior, machinery cameras and exact trolley props/poses remain open. Dialogue decoding is still broader than implemented quests elsewhere. |
| G03 | Hunter demonstrations, bridge placement, conditional/repeat dialogue | Partial; authored answer labels/order, price substitution, close flags and Glimmer Moneybags bridge camera work. Camera easing and other demonstrations remain. [Campaign evidence](CAMPAIGN_AND_TERRAIN.md). |
| G04 | Purchases, trade chains, challenge rewards, orb/talisman flags | Partial; 3 November: end-of-level talisman / Winter Tundra completion orb, reward presentation (retail reward camera views 3/4), Return Home and outro work in all 18 levels in hidden runs (Magma Cone through its own end trigger, Fracture Hills through its satyr quest); see [Level completion](LEVEL_COMPLETION.md). In-game orb challenges: listed one by one in [Orb challenges](ORB_CHALLENGES.md); eight complete through the original code in hidden runs (Colossus hockey x2 and statues, Glimmer gem lamps x2, Metropolis coup and sheep saucers x2), the rest are open; whole-level walks without teleports (`tools/level_walk.hpp`, work in progress) are not done; Colossus, Aquaria Towers, Shady Oasis and the Winter Tundra levels still need their end conditions armed by the check. Glimmer purchase 3 deducts 100 gems once and opens visual/collision geometry. Idempotent orb/talisman/skill-point award APIs and guidebook persistence are implemented. Zephyr, Icy Speedway and Canyon Speedway portal purchases (bits 10/12/18) also deduct their retail price once and persist. Other services, trade chains and reward-script callers remain unsupported. |
| G05 | Retail portal destinations, campaign traversal and end-to-end progression | Partial; 42 decoded apertures now render clipped retail panoramas and support physical crossings, native departure/arrival, saved progress and online host ownership. All 12 home transporters resolve paired arrival IDs. Payment/orb gates and a session-only exploration override work. Full loader presentation, gate cinematics and boss/story transitions remain. [Portal evidence](PORTALS_AND_WHIRLWINDS.md). |
| G06 | Automatic outro/story triggers; cinematic props, text and effects | Partial; 47 actor/camera/audio streams decode and standard intros play. |
| G07 | Damage response, death sequence and game over | Partial; native knockback, invulnerability and life-consuming respawn exist. |
| G08 | Challenge timers, boss meters, reward presentations and pause inventory | Partial; event-driven ordinary gem/life/spirit HUD and retail pause inventory/guidebook presentation exist. Four speedway timers, category counters, Hunter objectives and retry/free-flight/exit results now work, as does the retail power meter built from resident sprites 22–27. Boss meters and remaining reward presentations are open. [Interface evidence](RETAIL_INTERFACE.md). |
| G09 | Title entrance sound sequencing, fades and transition timings | Partial; retail title/file menu and PC save routing work. |
| G10 | Save every newly implemented quest, challenge and persistent entity | Partial; S2SAVE11 retains prior progress and adds invincibility/freeze timers and validated Breeze Harbor mechanism records. Speedway saves retain earned rewards/best times and restart the current attempt; transient challenge targets are not permanently removed. Legacy formats remain readable. Other quests remain open alongside their scripts. |
| G11 | Retail dialogue/font, guidebook and PC pause controls | Implemented core presentation/navigation: original font, coloured text, answer pages, contents, level/objective artwork, pause inventory and skill-point page, with PC save/load/settings retained. Epilogue/movie-theatre navigation and original map/vibration/screen-position options remain open. Reveal/scroll timing and page ordering are native approximations. [Interface evidence](RETAIL_INTERFACE.md). |

## Movement and presentation

| ID | Work | Status / evidence |
| --- | --- | --- |
| P01 | Ground acceleration/contact, slopes, horizontal jump collision and special surfaces | Partial; speeds, isolated vertical jump equations, charge-jump pitch, hover, headbash and Scorch sand implemented. |
| P02 | Glide thresholds/hysteresis, camera-relative stick projection and banking | Partial; latched glide/brake/optional boosts work. |
| P03 | Full head spring and body/head matrix composition | Partial; secondary transitions and powered requests work. |
| P04 | Camera orbit search, obstruction spring, turn lead and remaining modes | Partial; native follow/charge/swim/conversation and authored movie cameras work. |
| P05 | Far-only terrain, subdivision/LOD and material rotation combinations | Open; near terrain and all skies work. |
| P06 | Animated terrain/water textures, deformation and underwater atmosphere | Partial; Glimmer's three bridge tracks move 213 expanded render vertices and 76 collision triangles. Initial collision and 282 near-track states match MIPS. Breeze burner colour tracks and machinery geometry/region translations are now active. Exact moving-prop collision ownership and other animated terrain/materials remain open. |
| P07 | Moby distance fog, far models and intersecting translucent faces | Open; normal/reflection materials and a shared translucent queue are implemented. |
| P08 | Rendering fidelity controls and PSX rounding/dithering differences | Open; current native transforms/depth buffer are not pixel emulation. Preserve the 60 fps presentation requirement. |
| P09 | Full effects for actions, impacts, death and scripted scenes | Partial; normal/powered fire, charge smoke and wing trails exist; remaining callers/effects require evidence. |
| P10 | All sound callers, environmental loops and surface footsteps | Partial; music, speech, mapped action/object/animation sounds and Sparx sounds work. |
| P11 | SPU envelopes/reverb/interpolation, CD resampling and spatial sound details | Open; native mixer currently differs from the retail audio path. |
| P13 | Whirlwind elevators | Implemented native movement/effects for all 26 emitters, activation kinds, clip 18, exit-facing camera and glide release; real terrain routes checked. Individual quest activation callers and shared RNG/positional audio remain open. [Evidence](PORTALS_AND_WHIRLWINDS.md). |
| P12 | Sustained live 60 fps across scenes and transitions | Open broad performance gate. Sorted transparency now uploads once per chunk while retaining material draw order. Isolated paced Ocean / Metro: 600 frames in 10.010 / 10.013 s, mean 10.53 / 10.62 ms, P95 11.74 / 13.16 ms; 2 / 3 frames over budget. All-scene sustained performance and GPU timing remain unverified. See Level systems for the measurement limits. |

## Optional PC systems and editor

| ID | Work | Status / evidence |
| --- | --- | --- |
| E01 | Authoritative online combat, pickups, objectives and progression | Open; internet relay/rooms and remote player presentation are groundwork. |
| E02 | Online carried objects, reconciliation, reconnection and host migration | Open. |
| E03 | Online discovery/matchmaking and durable hosting | Open; temporary Cloudflare tunnel is the provisionally requested transport. |
| E04 | Preserve carried/airborne bodies in checkpoints | Implemented since S2SAVE7, retained in S2SAVE9: held/thrown transforms, velocities, grip/throw age, flight velocity/orientation, active power timers and immediate flight pose. Bounds are rebuilt and input validated before application. |
| E05 | Persistent geometry editing, retail-object overrides and object scripts | Open; `.s2scene` currently stores spawn and test entities with undo/redo. |
| E06 | Generated-world editing, rule validation and richer project schema | Open. |
| E07 | Broader procedural architecture, quests and biome behaviors | Open; current coherent island routes/landing spots/objectives repeat a limited set. |
| E08 | Live destination portal views and seamless world streaming | Open; correct retail exit portal and static original panorama work. |
| E09 | Scalable world progress beyond the current 10,000-world / 64 MiB file bound | Open; do not silently discard older worlds. |
| E10 | Per-world procedural music and expanded optional combat | Open extension work; existing source-theme music and three-strike melee are foundations. |
| E11 | Spring Realm expansion hub (retail Dragon Realm, WAD 73, expanded) | Blockout playable: `Play Spring Realm.cmd`. Includes the completed courtyard and castle (walkway, gates, keep/towers), two Autumn-style halls, the Orrery Hall, whirlwind lifts (new custom-world `LIFTS 1` section), breakables and stand-in portals. `spyro_spring_realm_tests` passes (tour, 2 rides, 18 breakables, 4 portals). Open: the expansion levels, the image-to-level detail pass, a blossom texture and the Dragon Realm sky. See `data/custom-levels/spring-realm/README.md`. |

## Historical notes reconciled

Several older documents predate later implementations. Their mentions of missing skies, opaque water, special object materials, audio playback, base power cheats, basic saves, ordinary charge-jump pitch or all non-Glimmer combat are not fresh regressions. See `SKIES_AND_LEVEL_BEHAVIOURS.md`, `OBJECT_RENDERING.md`, `MISC_MOVEMENT_AND_LEVELS.md`, `POWERUPS_AND_CAPTURE.md` and `RETAIL_MAIN_MENU.md` for the later evidence. They do not establish completion of the broader open items above.

A stripped 2048-byte ISO cannot supply XA Form 2 audio bytes that are absent from that copy. This is an input-data constraint, not something a decoder can reconstruct. Raw BIN/CUE, raw-sector ISO and preserved-XA extractions are supported. Console memory-card hardware is intentionally replaced with user-selected PC files.
