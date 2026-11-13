# Reverse-engineering coverage audit — 30 September 2026

This is an audit of the local Ghidra projects and implementation evidence, not a percentage of the game that has been ported. Generated spring-world work remains shelved and its files have been retained.

## What the projects actually contain

`tools/audit-ghidra-mapping.ps1` ran `tools/ghidra/AuditMapping.java` read-only against the 15 existing projects. The CSV-style evidence is in `data/analysis/mapping-audit-20260930/`; `summary.json` preserves every memory block. Counts below exclude uninitialized RAM and avoid counting the resident executable again for every overlay project.

| Resident executable | Loaded bytes | Discovered functions | Named / candidate / automatic |
| --- | ---: | ---: | --- |
| US retail, canonical `Spyro2_US` | 356,352 | 616 | 1 / 19 / 596 |
| July 13 prototype | 362,496 | 603 | 1 / 0 / 602 |
| August 11 prototype | 374,784 | 640 | 1 / 0 / 639 |
| Winter Jampack demo, correctly based at `80018000` | 346,112 | 579 | 1 / 0 / 578 |

The canonical retail project has 314,456 bytes defined as instructions and 311,928 bytes covered by discovered function bodies. Those are disassembly measurements. They do **not** measure behavioural understanding or native implementation. The older 642-function export includes other address contexts; it is not the resident-only denominator used here.

Automatic names also do not mean nobody has analysed a function. Many verified addresses are documented in source/comments and exported C while retaining `FUN_...` names in Ghidra. Conversely, successful decompilation does not establish that the port implements a function. A defensible completion percentage needs a verified function/behaviour ledger that joins these three forms of evidence.

## Gameplay overlays

There are projects for **12 distinct gameplay overlay contexts out of 29**. Seventeen have no dedicated local project. This counts the Summer Forest/gems context once and excludes overlay 1, the loader, from the gameplay denominator.

| Overlay | Context | Discovered overlay functions |
| --- | --- | ---: |
| 014 | Summer Forest | 43; separate gems project has 27 |
| 016 | Glimmer | 39 |
| 018 | Idol Springs | 45 |
| 022 | Hurricos | 1 |
| 038 | Breeze Harbor | 48 |
| 044 | Scorch | 41 |
| 046 | Fracture Hills | 47 |
| 056 | Winter Tundra/portals | 1 |
| 058 | Mystic Marsh | 50 |
| 064 | Robotica Farms | 40 |
| 066 | Metropolis | 42 |
| 070 | Ripto | 40 |

Hurricos and Winter Tundra need their discovery boundaries repaired: having a project with only one discovered function is not thorough overlay coverage. Existing exported code and implemented Hurricos combat must be retained while improving those projects. Loader overlay 001 has 110 discovered functions and is a separate audit unit.

The 17 missing dedicated contexts are:

| Overlays | Levels |
| --- | --- |
| 020, 024, 026 | Colossus, Aquaria Towers, Sunny Beach |
| 028, 030, 032 | Ocean Speedway, Crush, Autumn Plains |
| 034, 036, 040 | Skelos Badlands, Crystal Glacier, Zephyr |
| 042, 048, 050 | Metro Speedway, Magma Cone, Shady Oasis |
| 052, 054, 060 | Icy Speedway, Gulp, Cloud Temples |
| 062, 068 | Canyon Speedway, Dragon Shores |

## What still needs mapping

1. **Actor and objective callbacks.** Import the missing overlays and index each class's spawn/update/hit/death callbacks, linked objects and reward conditions. Start with Sunny Beach/Skelos to cover both demo scenes, then Crystal Glacier, Zephyr and the unimplemented enemy families. A rendered or idle Moby is not a completed behaviour.
2. **Full campaign transitions.** Shared portal traversal exists, but story gates, automatic outro triggers, demonstrations, reward cameras and all quest/purchase callers still need a per-level proof trail.
3. **Shared simulation fidelity.** Dynamic collision callbacks, shared random-number consumption, complete camera/body/head matrices and remaining surface rules are separate from animation decoding.
4. **Rendering and audio completeness.** Far terrain/models, subdivision/LOD, remaining animated materials and transparency intersections, all sound callers and SPU envelope/voice policy remain audit areas. Fixed 60 Hz simulation does not by itself establish sustained 60 fps throughout every level/transition.
5. **Build-specific behaviour.** July/Jampack share animation bytes, but their executables, scene directories, audio containers, UI and scripts differ. Each profile needs its own evidence; applying retail tables to another executable is invalid.

The 23 September [all-level audit](ALL_LEVEL_AUDIT.md) counted 326 enemy instances without their native scripts. That is dated implementation evidence, not a fresh recount or a count of unknown Ghidra functions. Later Ripto/Crownworks boss and VFX work supersedes the old ledger's blanket statement that boss meters/loops are absent. It does not close other enemy/quest families. The [completion ledger](REMAINING_WORK.md) remains useful for its other explicitly open items, including authoritative multiplayer and persistent editor work.

## Reproduction and preservation

New prototype projects are isolated from the existing retail work. The demo's correct project is `Spyro2_winter_jampack_1999_native`: its PS-X EXE header loads at `80018000`, unlike July/August/retail at `80010000`. An earlier analysis import at the retail base is retained as an unsuccessful research artifact and must not be used for addresses or coverage counts. The import script now derives its base from the actual executable header.

`tools/analyze-prerelease.ps1 -Build <july-1999|august-1999|winter-jampack-1999>` creates these isolated projects. The detailed format/animation evidence is documented in [Prerelease builds](PRERELEASE_BUILDS.md).
