# Reverse-engineering status

The objective is a native 60 fps Spyro 2 gameplay reconstruction and a shared ImGui level editor. The current workbench proves the retail-data pipeline, animated player rendering, terrain rendering, fixed simulation and authoring foundation. It does not yet reproduce the complete game loop.

Latest rendering/level pass: all 41 original sky banks and their 17 visibility trees are supported; sky masks match the original MIPS routine at 20,992 positions. Plain static quad winding fixes the inverted gem basket. Native enemy coverage now includes Glimmer, Hurricos's 22 robots/gear grinders and six metal idols in Idol Springs; 25 heavy containers gain their common scripts. [Detailed evidence and remaining level work](SKIES_AND_LEVEL_BEHAVIOURS.md).

## Retail identity and analysis

USA executable: `SCUS_944.25`, CRC32 `cdc0917c`, SHA-256 `7b54002789ab379e0d3a6b49d7b50078b9c9c2fed589f41ff9878ebb68fe0bc1`. Load address `80010000`, payload size `57000`, entry `8005478c`, stack `801ffff0`.

The supplied CUE describes one MODE2/2352 data track. The disc has 20 directory files. `WAD.WAD` is 176,914,432 bytes at LBA 500. Its root index has 197 slots, including 45 zero-size slots; these must retain their IDs. Root odd slots 15 through 95 contain 41 mapped four-child scene archives.

The current Ghidra main export contains 642 identified functions, 615 successful decompilations, 26 unloaded targets and one failure. `80023bb4` has an overlapping-instruction decompilation failure; near-terrain decode evidence uses actual disassembly instead. Automatically inferred functions and `candidate_` symbols are research leads, not proof of matching source.

Overlay slots 1 (loader) and 14 (Summer Forest gameplay) have separate projects at load address `8006d264`, with resident main-code bytes and uninitialized globals. Overlay exports include resident functions, so their counts must not be reported as unique overlay functions. Other overlays reuse RAM and need separate contexts. The large Summer Forest object dispatcher still times out during decompilation; its gem path was recovered from disassembly. These two contexts do not provide complete coverage of all overlay gameplay code.

## Implemented data paths

| Component | Evidence and native implementation |
| --- | --- |
| Disc images | ISO9660 records, endian mirrors, bounded reads; raw Mode1/Mode2 Form1 and single-data-track CUE. `assets.cpp`, `spyrodisc.py` |
| WAD | Offset/size pairs; keep nonzero-offset zero-size slots, terminate zero/zero. `assets.cpp` |
| Collision | Six length-prefixed scene sections, 12 bytes, then collision section; packed 14-bit bases and signed/unsigned deltas. `8001c214`, `level.cpp` |
| Terrain | Region relocation relative to section+4 (`8006fa98`); near geometry vertices, color banks, polygons and material records. `80024664`, `80024a00`, `80024c60`, `terrain.cpp` |
| Textures | Level VRAM image, PSX CLUT / TPAGE addressing, model palette tiles and terrain mip quadrants. Native RGBA atlases |
| Water materials | Original terrain primitive transparency flag, palette STP and TPAGE blend modes, with opaque/transparent depth passes. [Evidence and validation](WATER_RENDERING.md) |
| Spyro | Global WAD slot 0 and level-dependent frame banks; two vertex partitions, delta streams, palette face colors and UVs. `80041dbc`, `spyro_model.cpp` |
| Animation clocks | Frame-control duration, loop table `80060334`, original VBlank timing and secondary-track composition. `8002e304`, `8002e440`, `8002eb00`, `80030a14` |
| Camera / scale | Mounted executable profiles and trig tables, common units, origin and sphere radius. [Detailed evidence](CAMERA_AND_SCALE.md) |
| Objects / gems | Stable 88-byte records, static mesh banks/materials, gem initialization, rocking and direct pickups. [Detailed evidence](OBJECTS_AND_GEMS.md) |
| Animated objects | 36-byte descriptors, 8-byte frame records, compressed vertices, face patches, authored phases, retail blend table and ImGui clip viewer. [Detailed evidence](ANIMATED_OBJECTS.md) |
| Audio | Scene music table, XA music/speech streams, SPU banks and sound events; native Windows playback. [Detailed evidence](AUDIO_AND_NPC_INTERACTIONS.md) |
| Hunter conversations | Retail text/voice records, proximity, idle/talk transitions, repeat/choice controls and camera preference. [Implemented scope and remaining scripts](AUDIO_AND_NPC_INTERACTIONS.md) |

All 41 mapped archives decode 404,576 collision triangles and 16,690 Spyro animation frames across their banks. Those frame counts include repeated common clips. Summer Forest (slot 15) has 160 terrain regions, 8,160 near faces, 12,472 collision triangles, and 31 available Spyro clips. Slot 17 is Glimmer.

### Spyro model details

Global bank: slot 0; container-relative offset at +4; 62 animation offsets beginning at container+60. The common model-data offset is at container+56. Absent clips use `ffffffff`. Nineteen common clips are present; levels add their own banks.

A descriptor is 20 bytes. It supplies frame/color/partition/vertex counts and pointers to base vertices, faces, colors and dynamic streams. Frames have 20-byte records. The primary partition has 115 of 230 vertices for the mounted model. The secondary partition is attached with the frame's decoded attachment translation. Faces have four packed vertex bytes, packed color indices and PSX texture metadata; triangles repeat the final vertex index.

The 125-entry delta lookup at `80062674` is read from the user's executable. Each partition maintains its own accumulator and byte/word coding state. The primary and secondary parts can use different animation clips. Idle, walk, run, jump, fall, charge and glide are identified; unverified IDs stay numbered in the workbench.

Frame durations use control bits 18..21 plus one VBlank. The loop table's start, exclusive end and transition end preserve intros, loops and exit poses. The native primary controller uses the recovered transition matrix and VBlank blend table for current movement states, including the run and glide-brake clips. [Traversal evidence](TRAVERSAL_AND_TRANSITIONS.md) covers implemented transition opcodes and remaining control / secondary-track behavior.

### Terrain limitations

Near visual geometry and [retail water transparency](WATER_RENDERING.md) are present. Far-only regions, subdivision/LOD behavior, material rotations for all polygon combinations, animated water/terrain textures, underwater atmosphere, sky geometry, fog and dynamic object behavior need more reconstruction. Animated object model clips now render. Background clear color comes from the retail sky header. Visual output must not yet be labeled pixel-matched.

## Runtime and editor

`FixedClock` runs at 60 Hz and interpolates presentation. Catch-up is bounded and reports dropped time. Ground speed limits and glide Q6 constants are recovered; remaining ground response and jump physics are provisional. Swimming uses the retail surface/underwater clips and motion constants. Ground support follows floor seams, and the original four-tick ground-to-fall delay prevents spurious animation changes. Collision tags now resolve through the scene's special-surface directory, including Scorch's 1,276 type-10 sand triangles. [Hover, headbash, latched glide and sand sliding](AERIAL_AND_SCORCH.md) use the recovered clips and action constants. Remaining special-surface responses, slope alignment and animation-driven body orientation are incomplete.

The flame action uses [the retail secondary state machine](SECONDARY_ANIMATION.md), checked against 279,000 original MIPS states across all scene banks, and the executable's trajectories for container hits. [The visible flame renderer](FLAME_RENDERING.md) uses the original colored tips and textured trails. Full head/body rotation matrices and enemy interaction remain pending. The native game renders static and animated retail models and implements direct-touch collection for 1,669 parsed loose gems. Their materials retain the recovered shiny-face and additive-blend rules. Sparx follows Spyro and approaches resting and eligible bouncing gems; their recovered flight path awards value on completion. [Container evidence](CONTAINERS_AND_RELEASED_GEMS.md) covers 652 parsed baskets/vases/bottles, original collision shapes, break fragments and released gems. [Companion evidence](SPARX_COMPANION.md) covers the remaining idle / swimming behavior, feedback and save bits. Editor test entities remain separate fixtures. Do not replace Spyro with proxy geometry while reconstructing these systems.

The editor saves separate versioned `.s2scene` projects with retail profile/slot, spawn and entities. It supports undo/redo, selection, test entities, play copies and read-only retail-object inspection with camera focus. Geometry editing, retail-object overrides, object scripts and a richer serialized scene schema remain future milestones.

## Next executable-driven milestones

1. Add class-specific initialization and dynamic transforms to decoded static/animated objects. Complete special normal/environment materials and transparent object passes. Preserve record IDs and unhandled parameters.
2. Extend Sparx's ordinary follow / collection path to idle stunts, swimming, butterflies, hinting and feedback. Complete container surface/water behavior, remaining particles and flame action transforms, enemy gem release and save bits. Direct-touch collection and container charge/release are validated against all corresponding parsed records.
3. Recover superflame, supercharge and superfly actions, their animations, wing trails and supporting level overlays. Make their retail assets available across levels for the requested cheats menu. Recover remaining movement equations, input thresholds, slope alignment, glide/jump activation and camera cases; compare native state traces with retail execution. Primary movement transitions, secondary animation transitions and ordinary glide velocity constants are implemented.
4. Analyze each relevant overlay in its own load context and recover level-specific object/enemy/script dispatch.
5. Implement enemies, remaining NPC/challenge scripts, portals, level progression, UI, save data and end-to-end level transitions. Music, speech and general effect playback are present; remaining sound callers, surface categories, environmental loops and original mixer effects need reconstruction.
6. Build the editor on the same parsed scene/asset/runtime representations; then add PC-specific controls, graphics and authoring enhancements with measured 60 fps performance.

Keep evidence addresses, revisions, unresolved questions and validation artifacts alongside each implementation. Successful rendering or decompilation alone does not establish faithful gameplay.
