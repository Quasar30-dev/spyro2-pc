# Spyro's secondary animation controller

Evidence: USA `SCUS_944.25`, CRC32 `cdc0917c`. The native controller now follows the original head/secondary-partition state machine. It replaces the earlier short, timed flame fade.

## Retail paths

`8002e304` advances both animation partitions once per VBlank. The primary partition uses `8002e440`; the secondary uses `8002eb00`, `8002ebc8` and `80030a14`. The eight-by-eight transition table is at `80061330`; action-to-clip mapping at `80066d54` is `[0,7,53,45,54,0,7,7]`. All tables and frames are read from the mounted copy.

Mode 6 copies the body's endpoints. Mode 1 blends into the requested secondary clip over eight VBlanks; mode 9 selects the action's stable table operation. Mode 0 loops, mode 4 finishes an outgoing clip, mode 7 plays a one-shot, and mode 8 blends back to the body's current destination before returning to mode 6. An interrupted transition promotes its incoming endpoint when the original blend weight exceeds 95. The frame durations, loop limits and blend weights remain retail data.

`SpyroSecondaryAnimator` keeps previous/current states for presentation interpolation. `SpyroModel::secondary_track` replaces vertices starting at the primary clip's split index and reapplies the primary attachment translation. Ordinary movement therefore continues while the flame head animation plays.

## Flame action

`8002fc20` selects secondary action 1 for ordinary flame. The first permission byte in each four-byte record at `80061370` controls activation. Firing from idle requests primary clip 7 for the initial VBlank; the primary controller then uses its original exit transition while the head continues independently. Entering charge can preserve an existing flame, but starting a new ordinary flame while already charging is rejected.

The action stops following the live attachment at age 20 and cancels after the old age exceeds 47, or when a disallowed non-charge state interrupts it. `FlameAction` implements that lifetime; `ObjectInteractions` owns the separately fading streams. The flame attachment uses the primary source frame's root, matching `8002f2a4`. The former 0.18/0.32-second gesture constants are removed.

## Verification

`spyro_animation_probe` emits native endpoints for available pairs of secondary actions while the primary body changes between idle, run, glide, brake and charge. `tools/verify_secondary.py` independently reads raw retail frame records and executes the original MIPS instructions in a bounded test interpreter. It models branch and load delay slots. Only the primary animation update is supplied as input; no secondary result is hooked or replaced.

All 41 scene banks passed **279,000 state comparisons**, checking both clip/frame endpoints, packed blend, controller mode and observed action. This includes the additional secondary clips in levels that contain them. Three interpreter tests cover delay slots, load/write conflicts and invalid instructions/accesses. Seventeen native checks cover transitions, body partition preservation, action timing, permissions, interruption and reset. The full movement, object, container and flame retail validation also passed after integration.

Reproduce with `python tools/verify_secondary.py <ISO|CUE|BIN|directory> --all-levels`, or `tools/validate-retail.ps1 -NoBuild`. Local evidence is in `data/analysis/main/secondary_animation_disassembly.txt` and `data/validation/secondary-oracle-all.log`. The oracle is a research test, not a runtime emulator. R3000-family delay behavior is described in the [IDT R3081 hardware manual](https://www.bitsavers.org/components/idt/risc/1991_IDT_79R3081_Hardware_Users_Manual.pdf).

## Limits

The generic secondary transition controller is implemented; the gameplay logic that requests mouth-carrying, spitting and powered projectile actions is separate work. Full body pitch/bank and the original three-axis head spring/matrix composition remain incomplete. The state oracle does not establish those transforms, full movement equivalence, sound, enemy damage or pixel-matched rendering.
