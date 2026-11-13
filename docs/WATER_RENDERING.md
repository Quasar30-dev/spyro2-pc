# Retail water materials

Profile: USA `SCUS_944.25`, CRC32 `cdc0917c`. Implemented and checked on 2026-09-16.

The black/tarry water came from two lost pieces of material data: the terrain decoder discarded palette bit 15, and the renderer submitted every terrain face as opaque with depth writes. Black texels therefore obscured the pool bed and submerged objects.

## Executable and archive evidence

The scene's first section has a length/count header followed by 48-byte material records. Each record contains six eight-byte descriptors: two distant textures followed by four 32-by-32 near quadrants. The descriptor's final halfword contains TPAGE and engine flags.

- `80024f20` loads the descriptor's second word. At `80024f38..80024f44`, a negative value replaces the quad command byte with `3e`, enabling textured semi-transparency. The triangle path does the same with command `36` at `80025384..80025390`. Thus descriptor byte 7 bit 7 is the primitive's transparency flag, separate from rotation/reflection.
- TPAGE bits 5..6, in descriptor byte 6, select the PSX blend equation. The near quadrant settings agree in every material in the mounted 41 scene banks.
- Palette value `0000` is invisible. Other palette values retain bit 15 as the texel's STP flag. An opaque primitive ignores STP; a semi-transparent primitive blends only its STP texels and still draws its other nonzero texels opaquely. The hardware equations and palette behaviour are described in the [PSX GPU reference](https://psx-spx.consoledev.net/graphicsprocessingunitgpu/).

The read-only Ghidra dump is `data/analysis/main/water_terrain_disassembly.txt`; the existing terrain setup evidence is `data/analysis/main/terrain_disassembly.txt`. The large GTE-heavy renderer still cannot be decompiled reliably, so these claims use instructions rather than inferred pseudocode.

`tools/probe_terrain_materials.py` checks the mounted copy and writes metadata only. The current CUE/BIN audit covers **2,624 material records across 41 scene banks**, of which **44 have semi-transparency enabled**. All 44 select mode 1 (`background + foreground`). Their near textures contain only STP or invisible texels. Counts include repeated/cutscene banks and materials not referenced by the currently rendered near mesh; they are not a count of unique water surfaces.

Summer Forest material 87 is its water surface: all 4,096 texels have STP set and use additive mode 1. It appears in 173 near faces / 296 emitted triangles. Consequently black water texels preserve the scene underneath, while coloured ripple texels add the original palette colour. This is not an arbitrary uniform opacity adjustment.

## Native implementation

`terrain.cpp` preserves the descriptor blend settings in `Level::terrain_materials` and attaches each emitted vertex to its material. The RGBA atlas keeps palette entries as alpha 0 (invisible), 128 (STP), or 255 (opaque), with original RGB conversion and vertex modulation intact.

`renderer.cpp` uses two terrain passes:

1. Opaque terrain and the opaque texels of semi-transparent materials write depth normally.
2. STP texels of enabled materials draw after terrain, Spyro and objects. They test opaque depth but do not write depth, so submerged geometry remains visible and foreground objects occlude the water. Triangles are sorted from back to front by view depth and batched by blend mode.

The shared retail blend states implement all four equations: half background plus half foreground, additive, subtractive, and background plus quarter foreground. The hardware pixel tests exercise each equation even though all currently audited terrain selects additive blending. Collision view and model studio omit the water pass.

## Reproduction and checks

```powershell
./tools/build.ps1
python ./tools/probe_terrain_materials.py 'D:/Games/Spyro 2/game.cue' --out data/validation/terrain-materials.json
./build/Release/spyro_inspect.exe 'D:/Games/Spyro 2/game.cue' --all-levels
./build/Release/spyro_workbench.exe --game 'D:/Games/Spyro 2/game.cue' --slot 15 --swim-replay --smoke 800 --paced --capture data/validation/water-swim-paced.bmp
```

The ImGui **Play in water** button gives an immediate in-level view without replay arguments. The normal retail source options still work: ISO, CUE/BIN, raw BIN, or an extracted copy.

- All seven CTest suites pass. The new decoder suite has 22 checks; the D3D11 WARP suite has 13 checks, including actual captured pixels for black STP, opaque STP, invisible texels, all blend equations, submerged/foreground Spyro geometry, and ordered overlapping surfaces. These tests use synthetic assets.
- The C++ inspector decodes all 41 scenes successfully with material references retained. The independent Python audit was run directly on the retail CUE/BIN.
- Native hardware captures were inspected above and below the surface in Summer Forest, with additional surface captures in scene slots 19 and 33. The opaque Glimmer supercharge control is byte-identical to its pre-change BMP.
- The 800-frame Summer Forest swimming replay completes 800 simulation ticks in 13.324 seconds, visiting all five swimming states. Mean CPU work is 1.597 ms, p95 2.108 ms, maximum 10.910 ms, with no CPU frame over 16.67 ms.
- Slot 43 submits the largest decoded transparent near mesh, 5,108 triangles. Its 108-frame paced run has mean CPU work 3.287 ms, p95 4.310 ms, and one 20.225 ms startup frame. Subsequent frames meet the CPU budget. These are short submission/pacing checks, not GPU timestamps or a guarantee of complete-game performance.

Reports and photos are under `data/validation/`: `water-build.log`, `water-retail-inspection.txt`, `terrain-materials.json`, `water-swimming-after.bmp`, `water-underwater-after.bmp`, `water-slot19-after.bmp`, `water-slot33-after.bmp`, `water-swim-paced.json`, and `water-largest-surface-paced.json`.

## Remaining accuracy work

This recovers material transparency and compositing. Original animated texture/UV updates, water deformation, underwater fog/tint, distance shading, sky geometry, and full terrain subdivision/LOD remain to be reconstructed. The D3D11 pass uses a depth buffer and triangle sorting rather than reproducing the PSX ordering table or its five-bit framebuffer arithmetic. General translucent objects still have separate, incomplete material handling. Output should not be called pixel-matched.
