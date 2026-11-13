# Ripto: retail arena and Crownworks combat

Use **Play Retail Ripto.cmd** for the original Ripto arena (WAD slot 71), or **Play Cyborg Ripto.cmd** for Crownworks. Both launch the installed main executable. The original arena also activates the fight when entered normally or selected in the workbench; no Backrealms flag is required.

Launch Crownworks with **Play Cyborg Ripto.cmd** in the project root. The launcher accepts an ISO, CUE, BIN or extracted retail folder by drag and drop. Enter the lower arena to engage Ripto. The separate environment-only Crownworks launcher is preserved.

## Playable encounter

Ripto pursues Spyro and Hunter's power orbs, casts scepter volleys, sends expanding shockwaves across the floor, spins out fireballs, and rushes behind fourteen orbiting shield orbs. Jump over waves, dodge rushes, and use the arena's movable cover to block shots. All damaging projectiles use swept collision, wall occlusion and Spyro's usual damage/invulnerability rules.

His second phase mounts the original Mecha-Gulp model: charges, alternating laser sweeps, flame streams and a pursuing explosive fire train. Lure the train back into Mecha-Gulp to damage him. The final phase mounts the original bird, flies around the arena and fires homing volleys. Spyro receives temporary Superfly and Superflame throughout that phase. In the retail arena the phase transition places him at the authored aerial start and launches him into flight; Crownworks retains jump-then-jump takeoff. The powers have a ten-second grace period after victory.

Ripto and his mounts are solid. Normal flame and charge, melee, headbash and powered attacks damage him through the existing combat paths. The on-foot phase has 60 HP; Mecha-Gulp and the bird each have 80 HP, as initialized at `8007ef20`. In Crownworks, unshielded damage is 1 for normal flame, 2 for charge, 5 for headbash, 10 for Superflame/Supercharge, and 15 for a green bomb. The relays act in the two-mech phase only (since 10 October 2026). There, the shield relay halves damage, rounded up, the repair relay slowly restores HP after ten seconds without a hit, and overdrive reduces his attack delay. Disabling relays weakens the boss; it no longer wins the encounter by itself.

In the retail arena ordinary flame and charge cause clip-15 flinches without reducing HP. Powered attacks deal 10 HP. Ground powered attacks require Ripto to collect three orbs, with the acquisition animation and return to the centre for waves/spinning fire. Crownworks retains its broader attack rotation and relay rules.

Hunter drops red, green and blue orbs. Both combatants can collect them. Three grant a power determined by the third orb's colour. Spyro receives five seconds of Superflame, six seconds of green bombs or eight seconds of Supercharge: 300/360/480 VBlanks, now decremented once per 60 Hz tick. Blue-orb charge no longer depends on a Crownworks station timer. Orbs bounce with the original 5/8 restitution and 150-unit resting offset. The retail supply is capped at three. A new partial set does not replace an active power. Ripto's original music and object animation sound events accompany combat.

The retail arena uses the original HUD from overlay function `80070b30`: vertical yellow health at `(42,74)..(50,162)`, Ripto portrait at `(32,166)`, Spyro portrait at `(430,167)`, and three gold-rimmed orb slots at x=40/80/120 and 356/396/436, y=196 (512x240 logical coordinates). Scene-local portraits and orb sprites are decoded from descriptors `80063688..800636a0`, including the powered portrait's `5c20` palette. Filled slots all take the last collected orb's colour; at three, their intensity pulses using the resident sine table. The original entry curve animates the display. The aerial phase hides the orb rows and Spyro portrait. The timed power meter remains on the right when active. Custom captions, phase titles, general counters and upgrade icons are suppressed in the retail arena; Crownworks keeps its existing custom boss HUD.

Defeating the final phase in Crownworks awards the arena orb and opens Return Home. Retail victory instead sets the saved boss-completion flag, awards no ordinary orb and returns through the existing travel system to Winter Tundra. Loading an unfinished attempt restarts safely at the arena entrance; loading a completed encounter retains victory. No transient boss/projectile indices are serialized. Dying in Crownworks restarts the phase Spyro had reached from its own start (both mechs at full health in phase 2), and clears hazards. Disabled relays remain disabled. The retail arena keeps its full restart. Pausing freezes the encounter. Custom-world progress remains session-local and does not overwrite retail campaign progress.

## Retail evidence

### Crownworks: Mecha-Crush and coordinated combat

The custom second phase now fields Mecha-Gulp (80 HP) and Mecha-Crush (60 HP) simultaneously. Each has independent health, movement, solid collision, hit reactions and defeat. Both must fall before the bird phase starts; the survivor keeps fighting. The custom HUD shows both health bars. The retail encounter and its original HUD do not use these additions.

Crush uses the mounted scene-31 animation bank and GPT Image metal textures generated directly from his retail texture patches and model view. See `data/analysis/mecha-crush/README.md` and its saved prompt. His club pulses and aimed blast have warnings and recovery windows. Unshielded flame/charge/headbash/Superflame/green-bomb damage is 1/3/8/10/15; the shield relay halves it, rounded up.

The **Hammer and Anvil** team sequence combines Gulp's gun raise/fire/lower clips with Crush's raised club, angry chase and multi-slam poses. It tracks Spyro for 45 ticks, locks the path, starts the charge at tick 60, fires two flanking bolts at 82, winds up the club at 110 and releases a wave at 160. Sideways dodging and heavy-hit interruption remain available. The sequence ends at 240 ticks. Ordinary heavy attacks take turns while both bosses continue moving.

Crownworks also adds a three-target ballistic bombardment to Gulp and a locked-path climb/dive/recovery sequence to the bird. Ripto's optic lance and scepter nova now display charging energy at their actual animated eye/staff sockets. All timings use the 60 Hz simulation.

`data/validation/ripto-duo/duo.s2shot` is the 10,000-frame custom renderer fixture. Like other `--ripto-check` recordings it injects damage and protects/repositions Spyro; it is validation footage, not unassisted gameplay.

The shared controller is based on the US retail overlay 70, loaded at `8006d264`, with level assets from WAD slot 71. Ghidra exports are in `data/analysis/ripto-boss/isolated/`; decoded model/animation metadata is in `data/analysis/ripto-boss/retail-banks.json`.

| Entry point | Evidence used |
|---|---|
| `8007ef20` | Ripto class `2af`; pursuit/evasion, scepter casts, three powered ground attacks, hit responses, and progression to mounts. |
| `80077478` | Mecha-Gulp class `2b0`; movement, laser sweep families, fire stream, explosive attacks and mount sockets. |
| `800759c8` | Bird class `2ad`; flight/turn poses and aerial movement. |
| `80075520` | Flying Hunter class `2ac` and power-orb supply. |
| `8007b1e4` | Orb class `2b9`; three-orb counters, last-colour power selection, pickup tolerances and shiny material indices 1/2/8 at instance `+54`. |
| `8007a634` | Scepter bolt `2b7`, aerial homing bolt `2eb`, 22-point trail, yaw/pitch turn limits. |
| `8007be8c` | Expanding shockwave `2d2`; radial growth and vertical/radial hit tolerances. |
| `80070b30` | Retail health meter, blinking portraits, paired orb rows, last-colour modulation and powered pulses. Raw instructions in `data/analysis/ripto-boss/boss-hud-mips.txt`. |

Ripto's cast is clip 2, releasing after 50 VBlanks; spin uses clip 5, powered hurt clip 6, transformation/death clip 7, evade clip 13 and power acquisition clip 14. Mecha-Gulp's two laser groups use clips 7–12; flame stream uses 14–16 and fire train 17. Mount animation prefixes decode Mecha-Gulp mouth 12/13, saddle 14/15, lasers 16/17 and 18/19, and flame emitters 20/21 and 22/23. The bird mount and Ripto scepter each use socket 0. Emitters and the rider follow these original interpolated sockets. The fire-train head (class 2e6, entry 8007e2c0) has six following 2e8 bodies (8007e7ec); contact with Mecha-Gulp applies retail mask 100000.

`tools/ghidra/ExportRiptoBoss.java` isolates cases from the enormous shared Moby dispatcher. For decompilation only, it replaces the common loop epilogue at `80088164` with `jr ra; nop` in a read-only analysis session. The retail overlay and Ghidra database are not rewritten. Raw dispatch disassembly is retained under `data/analysis/overlay_070/` for cross-checking branch and delay-slot behaviour.

`decode_ripto_arena` reads original gameplay routes: Ripto parameter +18 centre, +20 Spyro aerial start, +24 Mecha-Gulp spawn and +28 bird spawn; bird +34 flight route and Hunter +4 route. All movement limits, floor/wave heights and emitter locations are relative to the active arena. The bird continues moving while casting and reacting to hits.

This is a native reconstruction, not execution of the original MIPS overlay. Full parity is not claimed: adaptive-difficulty formulas, the original platform-destruction choreography and scripted in-level transition/ending cinematics are not yet fully reproduced. The normal retail cinematic directory has no intro/outro entry for slot 71; its in-level scripted sequences require separate decoding. Crownworks relay modifiers, its normal-attack HP damage and new cyborg attacks are PC rules.

## New cyborg attack choreography

Crownworks adds **Optic Lance**: a braced charging pose, red warning sightline, locked aim, damaging eye beam and recoil/recovery. Rendering and collision use the same deformed eye socket. **Scepter Nova** winds up, twists/spins, then sends three spaced floor pulses with impact flashes and camera shake. Both layer smooth torso motion over the shipped animation banks and retain the original leg stance. They are absent from the retail encounter. Expanding waves now render as undulating bands instead of narrow rings.

## Cyborg face

The old red optic was attached to eyebrow vertices 18/19/77. It now follows the actual right-eye quad 168/165/161/157, including its local plane, across all sixteen retail clips. The optic has a recessed red lens, emissive core, bevelled steel ring and fasteners. Head panels sample the existing Backrealms steel texture with stable local UVs; attached panel seams and screws follow the animated skin. These are runtime material/mesh additions; the source retail textures are unchanged.

## Elements in Crownworks (10 October 2026)

Element hits reach the bosses as retail masks (`element_boss_flags`): breath as flame or charge, shots and waves as headbash, and a Fury hit as `0x140000` (10 HP). A 10-second Storm Fury could land about eight of those and end a phase by itself. Fury hits are now marked (`RetailObject::fury_hit`). Each phase takes at most a third of its maximum health from Furies: 20 of Cyborg Ripto's 60, 26 of Gulp's and the bird's 80, and 20 of Crush's 60. Ordinary attacks keep counting after the cap. Bosses are never frozen.

**Fixed 25 October:** element hits never reached a boss in play. Every boss
body is a scripted-contact actor, and `element_target` rejected those before
it tested `boss_body`. Breath, shots, waves and Furies passed straight through
Ripto, the two mechs and the bird; only the damage table was tested. Boss
bodies now count first. `spyro_ripto_tests` fires a real Fire shot at Ripto
through ordinary input and requires it to land.

## Cutscenes, phase light and phase audio (15 October 2026)

Crownworks presentation from `EXPANSION_LEVEL_PLANS.md` 6.5.2, 6.5.4 and 6.5.7. Code: `src/ripto_director.inl`. The retail arena is unchanged.

- **Cutscenes.** Four keyed camera sequences play on the scripted camera, with letterbox bars and captions:

  | Cutscene | When | Length | Shots |
  | --- | --- | --- | --- |
  | Intro | Spyro first comes within range | 8 s | over the crown walk; past the shield, repair and overdrive relays; Ripto turning to face Spyro |
  | Summon | phase one ends | 4 s | Ripto calling his machines; a low orbit as Mecha-Gulp and Mecha-Crush arrive |
  | Flight | the mechs fall | 4 s | from the lower ring; up at the bird against the sky |
  | Outro | a Fury breaks the crown core | 6 s | the core shattering; the arena from above; the exit gate rising to Return Home |

  - A cutscene holds the arena and the controls (`Game::scene_locked`). Only the transformation or death sequence it shows keeps running; the mechs and hazards wait.
  - Each cutscene can be skipped with jump once it has been seen this session ("JUMP: SKIP").
  - Phase restarts after a lost life never replay them.
  - They can be turned off in the settings panel ("Crownworks cutscenes") or with `--no-boss-cinematics`.
  - The power meters and the boss HUD are hidden while one plays.
- **Phase light.** A shader grade (`Light` buffer, b1) is applied to the Crownworks terrain and sky only. It eases over 2 s between phases:
  - dormant before the fight;
  - the lower ring lit blue with Ripto on foot;
  - the terrace amber for the mechs;
  - a violet sky for the bird;
  - a pulsing white light at the crown core in the finale;
  - calm and silent once he is defeated.

  Each tier has a multiplier and a colour cast. The cast is needed because the night palette is mostly blue: multiplying alone cannot make it warm or violet.
- **Audio.**
  - Music follows the phase: Ripto's arena theme on foot, in the sky and at the core; Gulp's Overlook (WAD 55) for the two mechs; silence after the outro's shattering.
  - Each live mech holds Mecha-Gulp's retail looping servo hum (class 2b0, sound 0 = sample 23 of Ripto's bank) at its body.

## Validation and reproduction

Build `spyro_workbench` in `build/misc-candidate`, then run `tools/install-workbench.ps1` **before** running tests. The root launcher always uses `build/Release/spyro_workbench.exe`.

`spyro_ripto_tests` loads the actual retail arena and Crownworks. It additionally checks real orb pickups/all three powers, retail flinch-only normal hits, 60/80/80 HP, authored aerial movement, automatic flight, save restore and victory travel. It checks all eye poses and mount clips, every attack family, collision during ordinary movement/charge, HP/phase progression, real flame input, projectile damage, pause, lethal-hit reset safety, one-time rewards and relay gating. Related core, object interaction, power-up, companion, audio and level-system regressions are also run.

`--ripto-reel` is the same fixture with two seconds per phase (at least 2400 frames). One recording then covers all four cutscenes, the phase lights and the music changes; the report counts `crown_cutscenes_played`, `crown_light_changes`, `crown_lit_frames` and `music_changes`. `spyro_ripto_tests` checks the cutscenes and light in code: the intro holds the fight and drives the camera, skipping works only once seen, the mechs hold during the summon, each phase's light, the outro's gate and Return Home, and no cutscene during phase setup.

`--ripto-tells DIR` runs the same fixture with the camera held on one view over the south side of the arena, and saves each attack once, 24 ticks into its warning, as `DIR/<phase>-<attack>.bmp`. It stops when all eight are saved. `data/validation/crownworks-polish/tells/tells-sheet.png` shows them in their phase light. The lava pit keeps its own glow: the phase grade stops below the lowest floor.

`--ripto-check` is a bounded native renderer fixture requiring either the retail arena or Backrealms Crownworks and at least 4200 frames. It injects attack flags and protects/repositions Spyro to exercise all three phase render paths and victory. Recordings from this fixture are **phase validation**, not unassisted gameplay. `data/validation/ripto-boss/combat.s2shot` records ordinary live boss behaviour without that fixture. Headless recordings use the existing 60 fps / 48 kHz stereo pipeline. Reports and inspection images are under `data/validation/ripto-boss/`.
