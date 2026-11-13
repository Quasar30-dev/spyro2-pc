# Photogrammetry / COLMAP study — 27 September 2026

**Historical engineering control, not the current spring-world design study.**
The user corrected the choice of reference: use whole RETAIL levels and shipped
textures, not Lanternfall. No conclusion here establishes the performance of a
coordinated retail-referenced generated sequence. The replacement study and
design revisions are documented in `docs/retail-photogrammetry-study.md`.

The spring-world production pass was paused at the user's request to test whether
photogrammetry could improve the image-to-mesh process. No spring-world concept
images had been generated yet. Production hall geometry and textures were not
replaced by this experiment.

## Finding

COLMAP is a photogrammetry system (structure from motion and multi-view stereo),
not a separate alternative to photogrammetry. It is useful for reconstructing
consistent overlapping images and for checking whether generated views agree.
The current ten independent hall images do not support a coherent reconstruction
comparable with views of one fixed 3D scene. Switching reconstruction software
alone therefore does not establish better final renders.

The appropriate next production experiment is a hybrid: complete calibrated
blockout, coherent overlapping detail views, COLMAP consistency/reconstruction
checks, then supported detail fitted to the retained mesh. MoGe remains a
separate geometric prior; original material sheets and continuous material
coordinates remain mandatory. Unobserved surfaces stay structural geometry.

## Controlled inputs

Artifacts: `data/validation/photogrammetry-20260927`.

- `generated10`: ten accepted individual GPT Image hall references, including
  `09 Ceiling-v2.png`. Source files remain unchanged. Analysis copies are resized
  to 960 x 720, with matching intrinsic scaling.
- `control10`: the polished hall rendered at exactly the ten corresponding
  Blender cameras, 960 x 720. Textured editable geometry supplies a genuinely
  consistent scene. The retained mesh is ground truth for this control.
- `control20`: those ten views plus ten translated positions looking along the
  hall from both directions. More overlap is the intervention here.

Blender rendering uses a separate unsaved scene copy, emission appearance
materials, eight Cycles samples and a blank white background. Production files
remain unchanged. Reconstructing these control renders cannot invent new detail
or improve their original mesh; the control validates the reconstruction method
and the influence of input consistency/overlap.

## Reproducible setup

Sparse reconstruction uses official **pycolmap 4.2.0**, installed in the existing
`build/splat-python` environment. All cases use CPU SIFT, exhaustive matching,
shared fixed PINHOLE intrinsics, 960-pixel inputs, four worker threads and seed 27.
Initial-pair minimum is 50 inliers / 8-degree triangulation angle. Two-view tracks
are retained but reported separately from support in three or more views.

Each case runs both known-camera triangulation and free incremental camera
recovery. Known poses use Blender-to-COLMAP coordinate conversion and the actual
database image IDs. Their presence is not counted as successful camera recovery.
The reported epipolar compatibility is the Sampson error against those supplied
poses, not against a newly estimated two-view model.

Dense reconstruction uses official portable **COLMAP 3.11.1 CUDA**: 640-pixel
maximum dimension, three PatchMatch iterations, ten samples, depth range
0.3–30 metres and fusion support from at least three images. Both ten-view cases
receive the same source-view graph, selected from control covisibility with
camera-distance fallback. Generated views are not penalized merely by missing
automatic neighbours in their weak sparse reconstruction.

The official Windows 4.2.0 CUDA binary started but reported zero available CUDA
devices for PatchMatch on this RTX A4500 / driver 565.90 setup. The 3.11.1 binary
successfully runs the GPU stage. No driver/system update was performed. The two
versions are deliberately recorded so results can be reproduced accurately.

Commands (from repository root):

```powershell
# Render the control through Blender MCP using tools/render_colmap_control.py.
& build/splat-python/Scripts/python.exe tools/probe_colmap_hall.py generated10
& build/splat-python/Scripts/python.exe tools/probe_colmap_hall.py control10
& build/splat-python/Scripts/python.exe tools/probe_colmap_hall.py control20
& build/splat-python/Scripts/python.exe tools/probe_colmap_dense_hall.py control10
& build/splat-python/Scripts/python.exe tools/probe_colmap_dense_hall.py generated10
& build/splat-python/Scripts/python.exe tools/probe_colmap_dense_hall.py control20
& build/splat-python/Scripts/python.exe tools/measure_colmap_hall.py
```

Per-case `report.json`, `dense-report.json`, logs, SQLite feature databases,
calibrated camera models and PLY clouds are preserved. The aggregate
`comparison.json` and `sparse-comparison.png` provide the measured comparison.

## Sparse results

| Input | Free camera recovery, largest model | Fixed-camera points | Supported by 3+ images | Matches compatible with intended cameras at 2 px |
| --- | ---: | ---: | ---: | ---: |
| Ten generated views | 2 / 10 | 77 | 3 | 4.36% |
| Ten coherent views | 8 / 10 | 2,669 | 543 | 74.61% |
| Twenty coherent views | 19 / 20 | 11,236 | 4,468 | 85.65% |

Free mapping leaves one control view out even with twenty images; the sparse
control is not being described as a complete scene reconstruction. Repeated
patterns, symmetry, extreme viewpoints and weak overlap still create ambiguity.
The generated two-camera model has a low reprojection error but covers almost
none of the intended scene: low error on a tiny surviving subset is insufficient.

Dense counts and sampled distances are in `comparison.json`. For the controls,
point-to-mesh distance measures reconstruction error against known geometry. For
generated images it only measures compatibility with the intended blockout:
their actual depicted geometry has no measured ground truth. Distances alone do
not measure completeness, texture quality or appearance at novel viewpoints.

## Production decision

Use COLMAP as a consistency gate and an optional source of multi-view detail.
Do not replace the complete mesh with this generated-image reconstruction.
For the new PS1-style spring world, capture multiple translated close/medium
views of each exterior and interior after locking the design in Blender. Keep
landmarks, object identity and material patterns unchanged across views; accept
reconstructed detail only where several views support it. Whole-level concept
views remain design references. More independently invented pictures are not
automatically more reconstruction evidence.

This is a bounded SIFT/stock-MVS pilot. It does not rule out learned matching,
better controlled generation or later multi-view methods. Those require their
own measured comparison before a claim of better results.

Primary references: [COLMAP tutorial](https://colmap.github.io/tutorial),
[known-camera reconstruction](https://colmap.github.io/faq.html#reconstruct-sparse-dense-model-from-known-camera-poses),
[official 4.2.0 release](https://github.com/colmap/colmap/releases/tag/4.2.0),
[official 3.11.1 release](https://github.com/colmap/colmap/releases/tag/3.11.1).
