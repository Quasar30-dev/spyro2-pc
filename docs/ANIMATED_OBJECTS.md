# Animated retail objects — USA SCUS_944.25

The native decoder reads animated object banks from the mounted retail copy. It decodes **2,430 clips / 24,196 frames** across the 41 scene banks; these totals include repeated models. Summer Forest supplies 26 animated classes, 55 clips and 577 frames. Glimmer supplies 28 classes, 80 clips and 937 frames.

Sparx is class `0x78` / decimal 120. His Summer Forest bank has two clips with 20 and 15 frames, 76 vertices and 115 faces. The model studio renders both clips, supports frame scrubbing and plays their recovered timing. Class `0x1bc` / decimal 444 renders Hunter. Sparx also uses this bank in the native companion runtime; Hunter now has native interactions in the supported scenes.

## Format and instruction evidence

| Field | Interpretation / evidence |
| --- | --- |
| Bank `+0` | Positive animation count before relocation |
| Bank `+52` | Frame-dependent face patch table |
| Bank `+56` | Common data offset |
| Bank `+60 + 4*clip` | Descriptor offsets; relocation in loader `80070018` |
| Descriptor `+0/+1/+2` | Frame count, palette count, vertex shift |
| Descriptor `+4/+5` | Near / far vertex counts; native rendering currently uses near vertices |
| Descriptor `+12/+16/+20` | Common-relative base vertices, near faces, near palette |
| Descriptor `+24/+28` | Far face/palette offsets; preserved in source, far rendering pending |
| Descriptor `+32` | Common-relative compressed stream |
| Descriptor `+36 + 8*frame` | Frame control word, then metadata word |

`80044684..80044870` expands the animated object's interleaved halfword and byte streams. The frame-control low 16 bits select a **halfword** offset; bits 16..24 locate the byte stream after the halfwords, and bit 25 selects the first decoding mode. Compact codes use the executable's 125-entry table at `80062674`. Wide codes either modify the base delta or reset the accumulator with an absolute vertex.

This format differs from Spyro's two-part codec. In particular, the absolute Z field is signed 12-bit with its two low control bits cleared. Coordinates retain the GTE ADD borrow when packing a negative VX into the low halfword. Model shift and the shared `1/1024` world conversion preserve size. `80044034..8004406c` scales projection translation by instance size/32; the equivalent native model scale is `32/size`, or one when the byte is zero.

Animated polygons use four **8-bit** vertex indices, unlike the static object's 7-bit indices. `80044e14..800451fc` supplies triangle/quad selection and winding. The fourth colour index occupies bits that serve as flags only on triangles; the renderer keeps those interpretations separate. Triangle bit 3 enables both sides, including Sparx's wings.

When the frame-control high bit is clear, `8004408c..800440a8` selects a patch list and `80044618..80044670` replaces runs of face words. The native decoder applies bounded patches to private face variants. It never relocates host pointers into retail data or modifies the source stream. Face variants share an atlas decoded from the scene VRAM image.

## Timing and initial state

`80041438` initializes the animation blend byte to `0x72` for a multi-frame clip. It preserves the record's authored current/next clips and frames. **68 source records** have nonzero starting frames; the import retains these offsets.

`80041068` advances animation in VBlank units. Frame metadata bits 9..12 encode the transition duration minus one. The blend byte's upper nibble holds that duration code and the lower nibble selects the executable's interpolation table at `80062868`. Frame and loop event bits are retained; sound-event metadata is preserved but not played yet. A zero duration code holds the resulting state as in the original routine.

The original caller normally advances two VBlanks per update. Native animation advances one per 60 Hz simulation step and interpolates presentation. Synthetic tests check 30/60/144 Hz presentation cadence, pause behavior, loop boundaries and authored starting frames. Gameplay code that changes clips in response to events remains separate work.

## Materials and limitations

Near palette colours and CLUT textures render natively. `80045620` and the special triangle path apply a DPCS body tint. Sparx's full-health tint comes from executable table `80061adc`; his untagged wing faces retain their palette colours.

Textured Moby faces now preserve TPAGE bit 15 and CLUT STP texels. Solid pixels retain depth writes; translucent pixels use the four retail blend equations without depth writes. This restores the glow textures used by powered projectiles and explosion meshes. [Powered effects](POWERUPS_AND_CAPTURE.md) records the reconstructed scripts and validation.

Some special triangles store normals where ordinary faces store colour indices. These now use the recovered normal-lighting mode instead of disappearing. Reflection materials use the original scene environment tile and projection tables. Translucent terrain, object and Spyro triangles share one depth-sorted queue, including non-additive blend modes. [Object rendering](OBJECT_RENDERING.md) documents these fixes and the all-scene audit. Far models, general object fog, exact PSX rounding and most class-specific behavior still need work; a successful decode does not imply every object's final appearance is matched.

Most level objects currently play their imported clips without class-specific initialization, AI or movement. Sparx now spawns as Spyro's gameplay companion, follows him and collects resting loose gems. [Companion evidence](SPARX_COMPANION.md) covers the recovered paths and remaining behaviors. His follow/collect dispatcher at Summer Forest `800720b4..80073410` is recorded in `data/analysis/gems_014/sparx_disassembly.txt`.

## Reproduce

```powershell
./tools/build.ps1
./tools/validate-retail.ps1 -Game 'D:/Games/Spyro 2/game.cue' -NoBuild
./build/Release/spyro_workbench.exe --game 'D:/Games/Spyro 2/game.cue' --object-model 120
./build/Release/spyro_workbench.exe --game 'D:/Games/Spyro 2/game.cue' --object-model 120 --object-clip 1 --object-pose 7
```

ImGui's **Animated object models** panel selects a class and clip. **Retail objects → Inspect model clips** opens the model used by a level instance. The existing scene and play paths render animated objects using the same decoder.

Local Ghidra evidence includes `data/analysis/main/animated_objects_disassembly.txt`, `animated_objects_timing.txt`, resident decompilations in the WAD-14 project, and the loader's `80070018` export. Validation captures include `sparx-frame0.bmp`, `sparx-frame1.bmp`, `sparx-clip1.bmp`, `sparx-studio.bmp` and `object-444.bmp` under `data/validation/`.
