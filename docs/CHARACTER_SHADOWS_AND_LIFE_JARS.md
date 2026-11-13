# Character shadows and captive life butterflies — 5 November 2026

Implemented and installed in the main workbench. The compiled candidate and
`build/Release/spyro_workbench.exe` both have SHA-256
`3ae3a4e0c33f7769e18c25bcf740d2719a9de3ab2cbdac2a456f167dac9b36e7`.
The main includes Carver's completed desktop startup/bootstrap changes. This
pass did not edit custom-level, SOI, camera, FOV, dialogue or trailer sources.
The project installer preserved the already loaded user executable; no Spyro
process was stopped. No download, settings change, worker or packaging step was
used in this pass.

## Original executable evidence

The oracle is the mounted US `SCUS_944.25`, with its original WAD overlays and
model/animation assets. Fresh, read-only Ghidra exports are retained under
`build/shadows-jars-20261105/oracle/`:

- `8004c534.c`: the original entity render call order, including Spyro's shadow
  at `800495e8` and moby shadows at `800499d4`.
- `800495e8.c`: Spyro's per-frame contour bytes, animation blending, sixteen
  perimeter segments and subtractive ground shadow. The original supports a
  richer shape than one generic ellipse.
- `800499d4.c`: moby shadow ground height/orientation from the packed `+1C`
  word. `80043858` queues eligible animated mobys when that word is negative.
- `jar-233-mips.txt`: original Summer Forest class `233`,
  `80075040..8007540C`. `80075054/64` creates class `D3`; `80075078..9C`
  places it at jar Z + `E0` (224 original units), writes lifecycle state `2`
  and clears its shadow word. The jar's parameter block keeps its live pointer.
  On an attack, `800751A0` changes that same child to state `0` and
  `800751AC` restores its shadow word before the jar's break/removal sequence.
- `jar-mips.txt`: `800713B0..D0` is the original captive-state branch: state
  `2` turns the butterfly without entering roaming/feeding.

The focused `jar_233.c` export follows shared dispatcher branches and contains
Ghidra warnings; the bounded instruction dump is the authority for the state
and pointer claims above. Ghidra projects were opened read-only and discarded.
`tools/ghidra/ExportShadowJarEvidence.java` exports known functions without
deleting output files or saving the analysis project.

## Rendering

`src/renderer_shadows.inl` renders the actual interpolated Spyro and character
mesh triangles into a vertical 2048-square depth map. It includes remote Spyros,
authored animated mobys with the original shadow flag, native enemies/NPCs and
native boss/PC character visuals. It excludes hidden, retired and undrawn
objects, Sparx, butterflies, fragments and power gates. The pass uses the same
position, pose, scale, rotation and boss deformation as visible rendering.

The terrain pixel shader projects each real receiving fragment into this map.
Shadows therefore follow slopes and stop at the visual floor's edges and holes;
there is no floating horizontal decal. The depth union prevents overlapping
mesh triangles from accumulating blackness. A 3x3 filter softens the edge;
height and map-edge fades avoid hard range boundaries. Objects above the caster
do not receive its shadow. The map covers a texel-snapped 96-world-unit window
around the player (48 either side); projection spans 192 vertical units.
Height fade begins at 3 world units and finishes at 12. Shadow darkening is
capped at 46 percent. Character silhouettes are measured independently by
`rendered_character_shadows` and `rendered_shadow_triangles` in capture reports.

This is a PC implementation using the original animated geometry, rather than
a byte-for-byte PS1 radial-shadow renderer. Opaque terrain receives the pass;
transparent water and separate splat materials are not additional receivers.
No camera, FOV or dialogue behavior is changed.

## Jar ownership and release

Three bugs interacted:

1. `RetailScripts::sync_out` handed every new butterfly to native feeding with
   a default free state, then removed its original RAM moby. For a jar child,
   this destroyed the pointer's target while the jar remained intact.
2. The native controller accepted state `2` as food and could replace it with
   pursuit state `1`.
3. Keeping the RAM pointer alone was insufficient: when the jar ran before its
   child on the break frame, the original free-butterfly case could consume it
   into VM-only health/lives before the PC feeding handoff.

Captive butterflies now remain script-owned and mirror the original transform
and animation. Native roaming, feeding and reset cancellation exclude them.
The filtered active list processes captive butterflies before jars, retaining
the original captive spin/animation before a possible release that tick. Free
food is handed over at `sync_out`, before its retail feeding case can execute.
The handoff reuses the existing engine object ID and records its source; it
removes the RAM moby only after the original jar has released it. The native
Sparx controller then handles its ordinary healing/life award exactly once.

No jar movement constants or release attack conditions are replaced. Both
normal charge and normal flame inputs were captured from the installed main.

## Automated evidence

Readiness receipt: `build/character-shadows-20261105/ready.json`. It records the
main/candidate identity, individual log paths with one-based result lines and
hashes, main-executable captures and the validation limits.

All following logs are under `build/shadows-jars-20261105/`:

- `build-main.log`: the successful main `spyro_workbench` target.
- `install-main.log`: installation and the SHA-256 above. Installation preceded
  the tests and was repeated after the implementation fix.
- `shadow-jar-tests.log:1..4`: D3D11 WARP shadow tests. Flat floor: 4,530 darker
  receiver pixels; slope: 3,739; edge/gap: 3,535. Each has zero receiver leaks
  and zero repeatedly darkened pixels. Spyro/NPC caster inclusion, original
  flag, hidden exclusion and triangle counts are checked.
- `shadow-jar-tests.log:5..27`: every one of the 23 retail class-233 jars,
  across 16 scenes. Each child's state and position survive 1,908 VBlanks
  intact (longer than a free butterfly's 1,800-VBlank lifetime), then an original
  attack query carrying the charge bit releases it. Its native consumption
  restores health 3, awards exactly one life and does not repeat; no interpreter
  faults. The award portion holds the probe player near the freed butterfly to
  isolate feeding from unrelated falling deaths on lava ledges. This is an
  automated integration fixture, not a manually played route.
- `shadow-jar-tests.log:28`: `226 character shadow / life jar checks passed`.
- `companion-tests.log:1..2`: 40 retail Sparx banks, 11 underwater bubble banks,
  then `46894 companion checks passed`.
- `interaction-tests.log:1`: `41 interaction checks passed`.
- `retail-script-tests.log`: final line `1156 retail overlay script checks
  passed`, including the existing reward/conversation and Party Crashers paths.

Installed-main captures under `evidence/`:

- `main-glimmer-shadows.png/.json`: 14 casters, 4,806 silhouette triangles,
  zero interpreter faults. The screenshot shows Spyro's wing silhouette and
  nearby characters on the retail terrain.
- `main-jar-intact.png/.json`: ordinary passive run beside Summer Forest jar
  62, 240 VBlanks, health 0, no feeding or consumption, zero interpreter faults.
- `main-jar-charge.png/.json`: the existing normal charge replay beside that
  same jar; health 3, exactly one consumed butterfly/feeding start, zero faults.
- `jar-close-c.png/.json/.s2shot`: a fixed capture view that clearly shows the
  butterfly inside the glass jar, with Spyro and his shadow beside it.
- `jar-close-released.png/.json/.s2shot`: the same fixed view with a normal
  flame press at tick 20. Jar removed, health 3, one consumption, zero faults.
- `gpu-flat-on.bmp` / `gpu-flat-off.bmp`, `gpu-slope-on.bmp` /
  `gpu-slope-off.bmp`, and `gpu-edge-gap-on.bmp` / `gpu-edge-gap-off.bmp`:
  retained WARP A/B fixtures.

These are automated hidden runs and inspection of their captured output.
Hands-on gameplay and subjective visual approval remain with the user. The
parent owns the original-shot trailer reshoot and portable package refresh;
`tools/reshoot_release_trailer.py` was not edited.

## Repeating the checks

Build/install the main first, as required by `AGENTS.md`, then build and run
`spyro_character_shadow_jar_tests`, `spyro_companion_tests`,
`spyro_interaction_tests` and `spyro_retail_script_tests` from the same build
tree. The new test accepts the project root as its only argument and retains
its GPU screenshots under the evidence directory. The mounted retail data is
required. Existing companion tests accept `data/disc`; the retail-script suite
does too. Keep tests' resulting logs bound to the exact installed main hash.
