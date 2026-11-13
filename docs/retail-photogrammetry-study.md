# Retail-referenced spring-world / COLMAP exploration

27 September 2026. Active source directory:
`data/validation/retail-photogrammetry-20260927`.

## User corrections carried into the workflow

- Reference the **whole shipped retail levels**, including their layout, density,
  connections, water and reverse sides. Player-centred gameplay captures and
  the previous custom Lanternfall castle are not the requested references.
- Generate multiple coordinated frames of one scene. The earlier independent
  hall-image experiment does not evaluate that approach.
- Create an original spring homeworld. The first retail-referenced concept was
  rejected because it looked too much like Summer Forest. Do not carry its
  gold-domed rotundas or stacked circular terraces into the next design.
- Retain Summer Forest's broadleaf trees; no cherry blossom.

## Reference capture

`tools/capture_retail_photogrammetry.py` captures Summer Forest (WAD 15) from
the mounted `data/disc` copy using the installed main workbench and absolute
cinematic cameras. The executable is unchanged. No custom-level argument is
passed. `overview-cameras.json`, `sequence-cameras.json` and `dense-cameras.json`
record the actual cameras, projection and output dimensions. Shots show the
entire level, with the player neither the subject nor a foreground obstruction.

`tools/capture_homeworld_design_references.py` adds whole-level views of Autumn
Plains (WAD 33) and Winter Tundra (WAD 57). WAD 59 is Mystic Marsh; its collision
export is retained under that correct name and was not used as Winter Tundra.
The retail texture catalogs are decoded shipped assets, even where their
original export is stored under an older custom world's assets directory.

These captures expose the retail mesh as shipped. Surfaces hidden during normal
gameplay can be absent when seen from outside the level; the images are reference
and measurement inputs, not claims that retail out-of-bounds geometry is closed.

## Generated design revisions

Built-in `image_gen` was used. Its exact backend model ID is not exposed by the
tool; no specific model version is asserted in the provenance.

1. `generated/spring-world-six-view-concept.png`: six views generated together
   from whole Summer Forest captures and its texture catalog. **Rejected by the
   user as too similar to Summer Forest. Preserved, not deleted.** Exact prompt:
   `references/generation-prompt.txt`.
2. `generated/spring-world-six-view-concept-v2.png`: new asymmetric fortress
   across a ravine, square bell-keep, pitched slate roofs, connected courtyards,
   stream crossings and battlement/platform routes. References include all three
   retail homeworlds and Autumn Plains materials. This is the current reviewable
   concept revision, not an accepted final level or a completed mesh. Exact
   prompt: `references/generation-prompt-v2.txt`.

Version 2's actual output is 1536 x 1024, with six square 512 x 512 panels,
despite the requested 4:3 panels. Analysis uses those actual dimensions; it does
not assume that prompt camera angles, projection or physical dimensions were
obeyed. All original images are preserved. Splitting a contact sheet into frames
is deterministic preprocessing, not additional image generation.

## Reconstruction probe

`tools/probe_retail_photogrammetry.py` runs CPU SIFT/exhaustive matching and
incremental SfM with official pycolmap 4.2.0. It records settings, image sizes,
camera recovery, point support and reports under each case directory.

Generated views use free camera recovery with focal-length refinement. The retail
camera poses are **not** assigned to a different generated world. Retail controls
also have a separate known-camera triangulation pass, whose supplied cameras are
not counted as successful camera recovery.

The initial low-resolution pilot used 12 retail views 30 degrees apart, reduced to
592 x 444. It recovered a two-camera / 52-point free model; supplied-camera
triangulation produced 405 points, 94 supported by three or more views. Neither
six-frame generated concept yielded an initialized free model with these SIFT
settings. This small pilot does not establish why: view spacing, texture detail,
projection uncertainty, matching limitations and correspondence quality are all
possible contributors. It cannot support a blanket claim about a model's
multi-frame consistency or about photogrammetry's maximum quality.

A denser control captures 36 full-level retail views 10 degrees apart at
1600 x 1200, analyzed at 1280 x 960. Its measurements and any dense output remain
separate from concept approval. Reconstruction of a known retail mesh validates
the pipeline; it cannot invent higher-quality source geometry.

## Production boundary

The earlier custom-hall experiment is preserved in
`docs/photogrammetry-colmap-study.md` as a historical engineering control. It is
not the design source or acceptance evidence for this world.

Concepts establish the new layout. A complete editable Blender structure,
calibrated detail views, reusable texture sheets, MoGe/COLMAP fitting and route /
material continuity validation are still required before claiming a playable
spring level. No runtime world or launcher has been replaced by this exploration.

Primary method references: [COLMAP tutorial](https://colmap.github.io/tutorial)
and [known-camera reconstruction](https://colmap.github.io/faq.html#reconstruct-sparse-dense-model-from-known-camera-poses).
