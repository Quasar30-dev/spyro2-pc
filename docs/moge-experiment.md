# Lanternfall: MoGe and multiview authoring experiment

27 September 2026. This is an authoring study, not a finished playable level.
The [image-to-level standard](image-to-level-standard.md) requires real meshes
and now requires matching exterior/interior view sets and reusable textures.

## What is available

- `data/custom-levels/lanternfall/Lanternfall - MoGe.blend`: six perspective
  reconstructions, comparing estimated FOV with a known 100-degree FOV on three
  cube views of the original exterior/interior panoramas. Packed textures and
  source/offset inspection cameras are included.
- `Lanternfall - Multiview.blend` in the same directory: eight independently
  reconstructed views from the new exterior/interior reference sheets. Their
  positions in this file are a spaced inspection layout, **not registered poses**.
- `Lanternfall - Materials.blend`: 16 reusable atlas materials, packed images,
  and textured sample meshes. These can be appended to other Blender files.
- `Lanternfall - Mesh.blend`: separate TripoSG volumetric castle study. It has
  side/rear volume, but the geometry contains holes and irregular detail. It is
  not accepted as the finished castle and is not part of MoGe's reconstruction.
- `Inspect MoGe Courtyard.cmd`: opens the main workbench with the native textured
  mesh study. Default retail source is `data/disc`; an ISO/CUE/BIN/folder can be
  dropped onto the launcher. Collision is only a flat inspection scaffold.
- `Reconstruct MoGe Image.cmd`: drop a perspective image onto it to estimate
  geometry and camera parameters with the installed GPU environment.

The generated sources, both 4-view sheets and both 8-cell atlases, are under
`data/custom-levels/lanternfall/source/multiview/`. `prompts.json` records the
exact prompts and original reference paths; `views.json` describes panel bounds
and explicitly marks unknown camera poses; `materials.json` names the atlas cells.
All four new image assets were generated with built-in GPT Image.

## MoGe configuration and results

Official source: https://github.com/microsoft/MoGe

This experiment uses `Ruicheng/moge-2-vitl-normal` with CUDA mixed precision and
resolution level 9, from repository commit
`74fbce054ebed49800de42d0ad0e83495065719a`. MoGe-3 is described by the current
upstream repository, but was not tested here. The v2 model provides the depth,
point map, normal and camera estimates needed for this comparison.

Hardware: NVIDIA RTX A4500, 20 GB. Initial model warm-up took about 2 seconds;
subsequent 1024-square inference was roughly 0.2–0.6 seconds per view, with peak
PyTorch allocation about 2.88 GiB. Download, loading and mesh export time are
excluded. This runs offline in authoring; the game does not load PyTorch.

| Perspective source | Estimated horizontal FOV | Known FOV | Final triangles |
| --- | ---: | ---: | ---: |
| Exterior entrance | 94.73° | 100° | 292,268 |
| Exterior left courtyard | 87.10° | 100° | 281,048 |
| Interior forward | 91.65° | 100° | 289,396 |

These are camera-estimation comparisons, not geometric ground-truth accuracy
scores. MoGe's metric scale on generated stylized art is also an estimate.
The scene studies normalize an estimated central ground height to 1.6 units;
isolated castle-object views keep their predicted scale. Raw point/depth maps
are preserved before this authoring normalization.

Each output directory contains `geometry.npz`, `texture.png`, `mask.png`,
`depth.png`, `normals.png`, `report.json`, a textured `mesh.glb` with vertex normals,
and `mesh.s2scene` for the native renderer. GLB is Y-up, camera looking down -Z;
the native packet is Y-up, looking down +Z. MoGe raw arrays use OpenCV coordinates.
Predicted camera-space normals are not tangent-space material normal maps.

Two conversion issues were corrected during validation:

1. Relative-depth filtering on a reduced grid cut away receding floors. The
   exporter now uses full-resolution inverse-depth curvature, preserving planes
   while separating depth discontinuities.
2. Generated PNGs can have alpha around 246 on solid surfaces. Alpha below 128
   denotes masked background; treating every non-255 pixel as empty discarded
   large parts of otherwise valid rooms. Mask borders are conservatively dilated.

## Validation

`data/validation/lanternfall/moge-assets.json` records successful interchange
checks for all 14 GLBs and native mesh packets: finite geometry/normals/UVs,
embedded textures, triangle counts and complete packet payloads.

`material-roundtrip.json` verifies stripping UVs/materials from a sample mesh,
reapplying a material to two selected faces, preserving four other faces, and
handling a scaled object. The generated materials were rendered for inspection
in `data/validation/lanternfall/materials.png`.

The installed main executable was used for source/offset captures and a paced
1920×1080 run of 600 frames (9.985 seconds). The small entrance study had mean CPU
work 0.501 ms, p95 0.741 ms, and zero CPU frames above 16.67 ms. This establishes
the native mesh path works at the target cadence in this study; it is not a
complete-level GPU benchmark or a performance guarantee for a fused world.
Raw results are `data/validation/lanternfall/moge/benchmark.json`.
Its legacy `vertical_fov_degrees` field reports the gameplay preference, not the
cinematic override; this run used the 65° value in `benchmark.shot`.

Source and displaced-camera PNGs in that directory expose the remaining gaps
behind lanterns, arches and foliage. They are intentionally not hidden by
background cards. The old panorama source remains visibly soft when enlarged.

## Reproduce

The existing authoring interpreter is `build/splat-python/Scripts/python.exe`,
using Torch 2.8.0+cu126, torchvision 0.23.0+cu126, numpy, scipy, Pillow,
trimesh, matplotlib, huggingface_hub and utils3d_moge. The latter comes from
`EasternJournalist/utils3d-moge` commit
`62f09d58509485564e24d5d9f6aac9ee9ebc0c37`. The local MoGe checkout is imported
directly; v3-specific dependencies are not needed for this v2 runner.
Model weights are cached by Hugging Face on first use.

From the project directory in PowerShell:

```powershell
& build/splat-python/Scripts/python.exe tools/moge_reconstruct.py `
  data/custom-levels/lanternfall/source/exterior-splats/front.png `
  data/custom-levels/lanternfall/source/exterior-splats/left.png `
  data/custom-levels/lanternfall/source/interior-splats/front.png `
  --fov 100 --output data/custom-levels/lanternfall/moge

& build/splat-python/Scripts/python.exe tools/moge_reconstruct.py `
  --viewset data/custom-levels/lanternfall/source/multiview/views.json `
  --output data/custom-levels/lanternfall/moge-multiview

& build/splat-python/Scripts/python.exe tools/build_moge_study.py
& build/splat-python/Scripts/python.exe tools/validate_moge_assets.py
```

For arbitrary perspective images, omit `--fov` unless it is actually known.
Do not feed a whole contact sheet or equirectangular panorama to the monocular
model. The viewset loader selects each sheet panel independently.

## Reapply the material sheets

In Blender, run `tools/lanternfall_materials.py` in the Text Editor. It defines
the library and helper without rebuilding an existing scene. Select the intended
faces, return to Object mode, then use the Python Console:

```python
apply_material(bpy.context.active_object, 'exterior.roof', selected_only=True,
               meters_per_tile=2)
```

This assigns a named atlas material and adds a separate `S2Materials` UV layer,
with box projection at the requested world scale. Existing materials on other
faces stay assigned. No-selection calls fail instead of replacing the whole
object. Atlas sampling is inset by two source pixels to avoid neighboring cells.
Unique banners/compass ornaments should receive deliberate UV placement rather
than repeated box mapping. Material IDs and cells are in `materials.json`.

## Superseded approach: independent view shells

- Register the camera poses and scales of the additional views, check matching
  architectural landmarks, then fuse compatible geometry. The generated views
  disagree on some details; simply merging them at the origin is invalid.
- Fill hidden surfaces, wall thickness, complete door openings and connected
  interiors. All MoGe outputs remain open visible-surface meshes.
- Use individual high-resolution views for production. The initial contact
  sheets are 1254×1254 overall (about 620 pixels per panel); they are a coverage
  prototype. The atlases are 1774×887 and require seam and relighting review.
- Polish image alpha edges; some generated cutout boundaries have color fringes.
- Assign materials semantically, validate seams and texel density, add collision
  matching the real mesh, and integrate native liquids/actors/objectives.
- Validate lateral/rear views and normal gameplay routes, then profile the whole
  assembled scene at 60 Hz. Splats may add detail but cannot replace these meshes.

## Complete blockout and separate sheets (27 September continuation)

The user approved a stronger default: a complete untextured Blender blockout,
calibrated renders used as GPT Image references, then MoGe detail fitted onto the
retained structure. **Separate reusable texture sheets remain required.** The
canonical requirements are in `docs/image-to-level-standard.md`.

The new test module is `data/custom-levels/lanternfall/complete-hall` (see its
README for every artifact and reproduction command). `Inspect Lanternfall Hall.cmd`
opens it in the installed native workbench. The earlier partial studies are
preserved as research and are not the launcher target.

- A complete 9 x 18 m hall plus vestibules, roof, floor, wall thickness, six true
  window openings and complete hanging lantern meshes: 1,005 editable objects.
- Ten individual clay renders and ten accepted GPT Image detail references,
  covering both ends, four corners, side walls, ceiling and floor. Known Blender
  transforms/FOV are recorded. Generated edits still have local projection drift;
  the known input camera does not make their invented details calibrated evidence.
- Both original GPT Image sheets stay separate, with 16 named atlas materials,
  cell metadata and the Blender reapplication helper. They texture the runtime
  mesh directly; perspective views do not replace them.
- GPU MoGe-2 predictions are fitted to known base depth using robust inverse-depth
  alignment. Occlusion checks, two-view agreement, pinned source edges and a 3 cm
  relief cap retain the original structure. The mesh has 223,488 visual triangles
  and 12,216 structural collision triangles. Only 2,508 vertex entries receive
  accepted relief; this is a **modest refinement prototype**, not an automatic
  recreation of every carved ornament or detailed foliage silhouette.
- The editable result is `Lanternfall - MoGe fitted hall.blend`. The native
  S2GS0003 packet preserves XYZ/UV/RGB. v1/v2 remain readable. The installed
  executable also uses mipmaps and 8x anisotropic sampling for scenery atlases
  to reduce fine-texture shimmer.

Validation: all 12 scenery parser checks passed; the mesh/UV check confirms
material IDs, exact interpolated UVs, pinned edges, finite packet values and
bounded displacement. Both base and refined geometry pass 30,240 directional
samples at twelve displaced interior positions, excluding intentional windows.
This checks discrete room coverage; it is not a claim of 99% visual fidelity.
Native forward/reverse views and traversal reports live under
`data/validation/lanternfall/complete-hall`. Performance results must be read from
those reports: camera turns are lighter than moving-player collision work.

Remaining quality work for this test prefab is detailed ornament reconstruction,
better material scale/UV treatment on narrow curved pieces, lighting and foliage
polish, followed by exterior/adjacent-room modules. Collision/layout completeness
is established for the hall; high-end appearance is still an authoring target.

## Detail preservation and surface continuity pass

The current prefab is `Lanternfall - Detail polish.blend` and the runtime target
is `complete-hall/hall-polished.s2world`. It retains the source scene and material
sheets, with 1,033 editable mesh objects / 286,592 visual triangles. Twenty-eight
new closed objects add leaf volumes and planter/bench mouldings. Three complete
lanterns retain real cages, glass, chains and independent materials.

Generated depth, normals and broad material boundaries are registered to the
blockout with bounded smooth sampling warps. A separate detail UV layer preserves
image ornament, wear and surface variation. Structural visibility alone proved
insufficient: displaced photographic props could still appear on background
surfaces. Generated-depth rejection and expanded semantic masks reduce that spill.
Chart-edge feathering caused repeated stripes on narrow arch/gable pieces; only
actual visibility boundaries are now feathered.

Conflicting ceiling patterns are resolved with a continuous per-bay mapping from
the accepted ceiling reference, excluding the photographed lamp/ribs. Banner
emblems use the matching source sheet. These are deliberate authoring decisions,
not a claim that inconsistent generated views can all be reproduced simultaneously.

The floor review uncovered two distinct failures: independently projected tile
grids disagreed, and centre-point visibility skipped entire paving pieces near
doorways or beneath furniture. Every upward floor face now receives one 1.2 m
world-space material field, with rectified stone samples and a single compass.
The inlay shares the same field. Grout transitions are consistent across material
cells and do not cut the compass ornament. The independent atlas-sampling check
`tools/validate_lanternfall_floor.py` covers mesh joins and inlay overlap; see
`floor-continuity.json` for the actual result, thresholds and scope.

`polish-validation.json` verifies exported mesh/UV/RGB data, original sheet UVs,
closed detail edges and unchanged structural collision. The polished mesh passes
30,240 interior ray samples. Final native overhead, oblique, front/reverse and
walking captures are under `data/validation/lanternfall/complete-hall`.

Source lighting remains baked into appearance, so this is not a fully relightable
PBR set. Some ornamental shapes remain texture detail rather than exact sculpted
reconstructions. Texture registration and sampled coverage checks cannot certify
literal all-detail recovery. Original generated views, prompts and separate
material sheets remain available for further editable refinement.

Final floor continuity check: 4,879 samples across 91 joins/overlaps passed;
mean channel error 0.292/255, p95 1.077/255, maximum 16.385/255. This measures
exported texture continuity, not similarity to every source image. The final
atlas is 8192 x 4064 with no density downscale. Six native inspection views and
the 1,200-frame walking route completed. The route's CPU-work mean was 2.535 ms,
p95 3.296 ms, with 15 frames over the 16.67 ms budget and a 118.77 ms maximum;
elapsed time was 20.372 s. **This does not establish a locked 60 FPS.** Intermittent
frame-time spikes remain a performance task; CPU-work measurements are not GPU
timings. See `final-walk.json` for the unrounded report.
