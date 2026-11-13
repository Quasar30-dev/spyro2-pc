# Shared object material reconstruction

The hollow metal chests were caused by a shared material-decoding error. Some animated triangle packets store a signed normal in the same word used by ordinary packets for palette indices. The parser preserved those faces but marked their palette invalid, and the renderer then skipped them.

The renderer now dispatches the original instance material mode for every class:

- **Mode 0:** per-face palette tint for tagged triangles, including Sparx.
- **Mode 1:** the scene's original reflection image, with spherical coordinates generated from the world-rotated local vertices and the executable's angle/radius/sine tables. This also handles packets that do not carry their own texture.
- **Mode 2:** packed face normals, original material colour and strength, and the recovered GTE colour matrix. Metal faces no longer disappear, even when the packed normal happens to resemble valid palette indices.
- **Mode 3:** whole-palette tint used by heat and power effects.

Ordinary palette quads, texture modulation, transparent texels and all four PSX blend equations remain separate material cases. Model studio picks up the authored instance material, so inspection uses the same metal/reflection settings as the level. Transparent object triangles, Spyro's translucent faces and terrain now share a back-to-front depth order; drawing each blend mode in a separate global batch could previously reverse overlapping surfaces.

## Primary evidence

From the mounted USA executable SCUS_944.25, CRC32 `cdc0917c`, inspected with Ghidra:

- `8001c734`: scene gameplay header +`0x10` supplies the environment TPAGE/CLUT through `80066ed8`.
- `80044d8c`, `800454f8`: dispatch instance material mode.
- `80045124`, `80045318..80045380`: tagged triangle dispatch; quads retain palette semantics.
- `800455b0..80045614`: mode-2 RGB, strength and repeated `[-2848,-2458,-2517]` colour-matrix rows.
- `80045ccc..80045d50`: signed normal extraction, MVMVA → GPF → CC.
- `800458e0..80045cc4`: reflected texture packet and per-vertex spherical mapping; executable lookup tables at `800625ac`, `80062630`, `80061bd8` and `80061c58`.

Focused instruction exports are `data/analysis/animated-materials-mips.txt` and `animated-materials-tail-mips.txt`. Native floating-point transforms smooth the retail equations; this is not PSX integer/pixel emulation. General Moby distance fog and intersecting translucent polygons still need separate reconstruction.

## Validation

`spyro_object_material_tests.exe data/disc` loads all 41 mapped scene archives and checks the material path across every mesh variant of each placed object. The first full audit covered **4,418 rendered-model instances**, **1,802,978 face/instance combinations**, **22,368 normal-lit faces** and **2,160 reflective faces**, with **95,959 checks passing**. Normal-lit placed classes include `30`, `CA`, `CB`, `CC`, `159`, `15B`, `16F`; reflective classes include `116`, `1F3`, `291`.

The D3D11 regression suite also measures actual rendered pixels for formerly omitted normal faces and reflection packets, alongside existing opaque/transparent texture and blend-equation checks. Reports and native captures are under `data/validation/`.
