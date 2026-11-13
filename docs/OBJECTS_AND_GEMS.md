# Retail objects and gems — USA SCUS_944.25

The native object path reads the mounted retail copy. Records, mesh banks, palettes, denominations and gem animation phases come from the WAD/executable. Source bytes remain unchanged.

## Records and model banks

`8001c734` walks the fourth scene child from `+0x2c`. After eight length-prefixed chunks, the ninth contains a byte size, count and 88-byte object records. Original record indices remain stable IDs.

| Record offset | Interpretation used by the native parser |
| --- | --- |
| `00` | Gameplay-relative parameter pointer, bounded to the child |
| `0c/10/14` | Signed original X/Y/Z position |
| `36` | 16-bit object/model class |
| `3a/3b` | Save-source ID / persistence flags; preserved, saving pending |
| `3c` | Model variant or animation ID |
| `44/45/46` | Packed roll, pitch and yaw bytes |
| `48/49` | Signed lifecycle state / object-specific variant |
| `4c/4f` | Draw flags / size byte; preserved |
| `50` | Held gem denomination; this alone does not make an object collectible |
| `54` | Material/color selector |

The 41 scene archives contain **5,200 records**, including **1,669 loose gems**. Summer Forest (WAD 15) contains 273 records and 105 loose gems worth 165; Glimmer (WAD 17) contains 201 records and 91 loose gems worth 195. Enemy/container/reward gems are additional. WAD 95 uses a different short gameplay header; its object path is explicitly left unsupported.

Loader-overlay `80070398` pairs scene-header model offsets at `+0x40` with class IDs at `+0x140`. Index zero is Spyro's extra animation bank; the final nonzero offset marks the last model's end. Runtime class lookup is `80068c94`.

Static banks have a negative count before relocation. `80070018` establishes the common-data offset at bank `+16`, variant offsets at `+20`, and descriptor-relative face/color offsets. Positive-count animated banks now have their own decoder and runtime clock; see [animated object evidence](ANIMATED_OBJECTS.md).

`8004753c..8004755c` expands overlapping signed vertex fields: `word >>21`, `(word <<10)>>21`, `(word <<20)>>20`. Its ADD when packing GTE VX/VY borrows one unit into VY for negative VX. Independent non-overlapping fields produce incorrect geometry. Static vertex units are twice original world units; the common native conversion remains `1/1024`.

Faces contain four 7-bit vertex indices, palette colors or shiny normals, and optional UV/CLUT/TPAGE data. Materials are decoded from the level VRAM image. Native rendering culls complete meshes against the camera frustum and calculates an object rotation basis once per frame.

## Gem behavior

Summer Forest's gameplay overlay is **WAD 14**, loaded at `8006d264`. The loader selects object update callback `80070480`; its class-1 branch begins at `800707f8`.

| Evidence | Recovered behavior |
| --- | --- |
| `80070ac0..80070b60` | Cast down from Z+1024, place on the ground, set state/variant/model/color |
| `80038604` | Derive surface roll/pitch from the collision normal |
| `8006440c/4428/4430/4438` | Tables for denomination-to-variant, value, mesh and material; values 1, 2, 5, 10, 25 |
| `80071008..80071078` | Rock with original sine/cosine tables shifted by seven; phase advances two per VBlank |
| `8001bf6c` | Horizontal distance: max absolute delta plus floor(3*minimum/8) |
| `8007120c` | Direct touch requires horizontal distance <512, age >=30 and Spyro-origin height relative to gem in `[-383,512)` |
| `80039678` | Account for the gem using its decoded variant/value |

The native simulation runs at 60 Hz and interpolates gem rotation for presentation. Play owns object-state copies: collection increments the session count once and hides the gem; restarting play restores the authoring state. Containers are not collected through this path. Retail validation starts Spyro at each loose gem and checks collision, collection, accounting and isolation.

Sparx attraction and state-3 gem flight are implemented in [the companion runtime](SPARX_COMPANION.md). [Container release and state-2 bouncing](CONTAINERS_AND_RELEASED_GEMS.md) now feed that collection path. Remaining behavior includes sparkles, popup numbers, sound, save bits and enemy release. The sparkle timer initializes to 256 rather than consuming the original RNG; source animation phases are retained. Surface alignment uses the recovered equations with native floating-point normals.

## Gem color regression and material passes

The initial dark gems exposed three distinct omissions:

- Triangle fronts follow positive NCLIP, while **shiny quads use a separate two-NCLIP rule** at `80047c4c..80047cac`, including reversed winding and folded-quad triangle selection. Treating a cap quad like a triangle incorrectly shades its center as a back face.
- `80047cac` rotates packed normals into the camera and uses the repeated light row `[-1916,-3065,-3072]`. Base/specular pairs come from `80064440`; the camera's 320/512 Y correction remains in the calculation. Signed light gain is applied before RGB saturation.
- Nearby shiny fronts retain the semi-transparency flag, with opaque back faces. The distance test uses half-unit TRZ and a 2048-unit bias for face bit zero. `80047fe0` emits `E1000620`, selecting additive B+F blending. Native rendering now implements opaque and additive passes with the corresponding depth-write behavior.

The [PSX GPU command reference](https://psx-spx.consoledev.net/graphicsprocessingunitgpu/) documents blend flags and saturation; the [GTE register reference](https://psx-spx.consoledev.net/geometrytransformationenginegte/) documents signed IR0 and MAC behavior. Game-specific addresses above were checked in the local executable's instructions.

The renderer retains these rules using floating-point transforms and a depth buffer. PSX rounding, dithering, polygon ordering, far LODs and complete pixel equivalence are not implemented.

## Ghidra and editor workflow

Local evidence: `data/analysis/gems_014/disassembly.txt` for the level dispatch, and `data/analysis/main/object_disassembly.txt` for vertex expansion and materials. A broad WAD-14 export produced 680 functions, 662 successful decompilations and 18 failures. These include resident code and speculative interior entries, not 680 unique overlay functions.

The large `80070480` dispatch still times out after repairing 14 speculative interior function boundaries. Its gem path was reconstructed from instructions, not a successful C decompilation. `RepairDispatchBody.java` preserves the reviewed boundary in the focused project.

```powershell
./tools/analyze_overlay.ps1 -Slot 14 `
  -EntryPoints '80070480','8007bcb8','8007cab8','8007e100' `
  -ExportFunctions '8007bcb8','8007cab8','8007e100' `
  -DumpRanges '80070480','80071480'

./build/Release/spyro_inspect.exe 'D:/Games/Spyro 2/game.cue' --all-levels --spyro --verify-pickups
./build/Release/spyro_workbench.exe --game 'D:/Games/Spyro 2/game.cue' --slot 15 --object 1
```

ImGui's **Retail objects** inspector exposes stable IDs, classes, model variants and gem values. **View object** searches for an unobstructed inspection angle. Records are read-only; authored overrides and object script editing are future work.
