# Breeze Harbor, power gates and speedways

23 September 2026. USA retail profile SCUS_944.25, CRC CDC0917C. Game data is read from the mounted copy. Main executable installation precedes tests; no captures were made for this pass.

## Powerup gates and meter

All 25 authored gates decode their own spirit threshold and parameter values. Crossing the aperture grants the corresponding power; remaining in the aperture does not refill it continuously.

| Kind | Response | Gate count |
| --- | --- | ---: |
| 0 | Invincibility and damage protection | 3 |
| 1 | Supercharge | 4 |
| 2 | Timed flight; Metropolis also enables flame | 5 |
| 3 | Timed superflame | 4 |
| 5 | Superjump; +58 is launch strength, not duration | 6 |
| 7 | Superfreeze | 2 |
| 10 | Permanent superflame, requiring 10,000 gems and 64 orbs | 1 |

The HUD now uses resident sprites 22–27: crystal, inset, caps and six cells. Its 47-VBlank entrance/exit follows the decoded curves; the last cell flashes and dialogue suppresses animation. Warning sounds use global sound 1D. Gate symbols use each gate's held-class model. Flight, flame, ice and invincibility timers survive PC saving/loading.

Cloud Temples freeze breath stops susceptible actors; class 1EE uses pose 4, the original 271 ice-shell model and a 300-VBlank thaw. The broader troll movement, pushing, stacking and bell objective are not yet complete. Ordinary enemy scripts are still needed in Cloud Temples and other levels before their spirit thresholds can all be earned through faithful retail combat.

## Breeze Harbor

Six burners accept flame and activate their linked terrain colour groups. Fan 17D accelerates and enables whirlwind 400. Machinery 405 drives the original near-vertex/collision tracks and rigid region translations for the rising platform and ship. Colour changes propagate to both opaque and transparent renderer buffers.

Use **E** near a cannon, arrows/WASD to aim, **F** to fire, and **E or Space** to leave. Shots use the original C2 model. The eight 16F mines respond to cannon/powered hits and award the mine objective once. Burning both ship engines permits Squawk's talisman reward through dialogue.

Use **E** near the trolley or finish Fisher's instructions to ride. **A/D** selects branches, **Space** jumps, **F** fires, and **E** leaves. The controller reads seven authored waypoint routes, their directions, forced/optional branch actions and terminal derail markers. Track switches modify those route actions. Horizontal travel uses the native 0x240 target speed per 30 Hz step; 0x80 is the separate jump impulse. Collecting 50 gears awards the trolley orb once; collision failures expose retry/leave controls. Retries restore gears, switches and obstacles.

Saved mechanisms are matched by original scene object ID and class. Challenge targets and projectiles are transient; completed rewards persist. The trolley still needs exact native vehicle poses, gap/obstacle spawning and collision timing. The pressure launcher, machinery cameras, linked moving props and precise ship collision ownership also need further reconstruction; the ship's collision selection currently uses authored region bounds.

## Speedways

Ocean, Metro, Icy and Canyon now start timed trials with four groups of eight targets. Each starts at 1,800 VBlanks. The resident table supplies target-dependent bonuses:

| Course | Four bonuses, in table order |
| --- | --- |
| Ocean | 60, 120, 180, 120 |
| Metro | 120, 180, 60, 120 |
| Icy | 180, 120, 120, 180 |
| Canyon | 60, 60, 180, 120 |

Completed groups award 80 gems each; all-in-one awards another 80. Flags prevent repeat awards. Best times, guidebook counts and under-time skill points persist. Timeout/water failure, retry, free flight and return-home requests are connected. Results use the retail font: **1/Enter** retries, **2** selects free flight, **3** exits. Saved games restart the attempt while retaining completion and rewards.

Hunter modes now have their own counters and results. Ocean rings shrink after Hunter passes; Metro changes knocked burglars into class-237 falling loot; Icy rings use their lap count, activation point and wrapped deadline; Canyon balloons use route deadlines and the original 2CA projectile while riding Hunter's route. Actor travel currently interpolates the authored polylines with approximated speeds. Exact spline following, remaining spawn schedules, actor presentations and the Icy carrier still need work. These modes are a functional implementation pass, **not a claim of complete retail-equivalent speedways**.

## Validation and performance

The installed `build/Release/spyro_workbench.exe` matches the candidate executable: SHA256 `BBFB25C92DF87CB52AC0B4D71D1C01F34A305056C85232CB40A3A50EB7E8A180`. It was replaced before tests. All 24 regression suites passed; the renderer image-output test was excluded to honor the request to skip captures.

The retail-data suite passed 179 checks across all 29 campaign scenes (5,200 records, 25 gates). It checks locked gates, crossing/cooldown responses, meter animation, freezing/thawing, mechanism persistence, a real aimed cannon projectile hitting a mine, trolley controls/50-gear reward/retry, and all four course/Hunter reward and failure transitions. Target completion tests place the pilot at reachable targets or inject attack contacts; these are controller/progression tests, not manual playthroughs or proof that every original obstacle schedule is reconstructed.

A separate installed-executable run used normal flame at Breeze burner record 2: one mechanism activation and 174 terrain revisions after 240 ticks. Six scenes also completed hardware-rendered, JSON-only smoke checks. No photos or videos were written.

Profiling found repeated discard uploads of tiny sorted translucent batches taking 18.7 ms in Ocean Speedway. The renderer now uploads each sorted chunk once, then draws its existing material runs in the same order. In isolated 600-frame paced runs:

| Scene | Mean work | P95 work | Wall time | Frames over 16.67 ms |
| --- | ---: | ---: | ---: | ---: |
| Ocean | 10.53 ms | 11.74 ms | 10.010 s | 2 |
| Metro | 10.62 ms | 13.16 ms | 10.013 s | 3 |

These are CPU/submission timings, not GPU timestamp measurements or a guarantee of sustained 60 fps throughout every level. Breeze, Icy and Canyon also completed earlier isolated 600-frame runs in approximately 10 seconds. The JSON reports retain simulation, render-stage, audio and presentation timings for further investigation.

## Reverse-engineering evidence

- Resident `80061920`: four 0x58-byte flight profiles; +20 bonuses, +44 best time, +48 starting time, +4C reward, +54 skill threshold.
- Resident `80052D84` / `80051D94`: power meter drawing and sliding. `data/analysis/level-systems/hud-mips.txt` contains the Ghidra listing.
- Ghidra loader `8006FA98` and translator `8003C310`: region translation records and group IDs. `8003873C`: oriented trigger-box containment.
- Breeze `8008031C` burner, `80081660` machinery, `8007A414` cannon, `8007E554` trolley, `8007F65C` route actions, `800797D8` gears, `80080A10` worker rewards. Native orb bits are trolley bit 0 and mines bit 1.
- Cloud `800751D0`: ice attack mask 01000000; `80075874`: thaw threshold 012C.
- Bounded case listings for overlays 28/38/42/52/62 and the full record census are under `data/analysis/level-systems`. One exploratory Ghidra decompilation timed out; its output is not treated as evidence of successful decompilation.

The [all-level audit](ALL_LEVEL_AUDIT.md) distinguishes decoded records from implemented gameplay. Validation logs are in `data/validation/level-systems-20260923/`.
