# Image-to-level authoring standard

User decision, 27 September 2026: depicted scene elements must become actual
editable mesh geometry. A panorama on a depth shell is not a completed level.

For new spring-world concepts, use **whole-level shots of the retail homeworlds**
and their shipped texture assets. Include opposing, side and overhead views so
the references convey layout and density. Player-centred screenshots and earlier
custom levels do not satisfy this reference requirement. Generate coordinated
views of the same new design. Match retail scale, density and rendering language
while creating a distinct architectural silhouette and route layout; copying
Summer Forest's gold domes and circular terraces was explicitly rejected.

1. Build a **complete, untextured structural blockout in Blender first**. This is
   the required starting point, not a fallback after image reconstruction fails.
   Establish dimensions, floor levels, traversable paths, wall thickness, roofs,
   openings, rooms and connections. Keep the structure throughout refinement.
   Save named cameras, their transforms, projection, image dimensions and FOV.
2. Render coordinated **multiple camera positions for every exterior and interior
   module**, then use those individual renders as GPT Image edit references.
   Preserve the blockout's projection, silhouettes and opening positions.
   Exteriors need front/rear and both sides with roof
   coverage; interiors need opposing corner views exposing floors, ceilings,
   doorways and occluded corners. Keep the same dimensions, openings, materials
   and landmarks across views. Use blank skies, including sky through openings;
   the engine supplies its skybox. A rotated panorama from one optical center is
   not a substitute for cameras at different positions.
   Generate matching **reusable texture sheets** with each view set: stone,
   paving, roofs, plaster, wood, metal, cloth, trim and unique decorative elements
   as appropriate. Keep these separate from perspective images so textureless
   models can be UV mapped again after topology changes. Store material names,
   atlas bounds and intended scale alongside the images.
3. Use MoGe to estimate geometry, normals and camera perspective from each
   generated view. Fit reliable detail **onto the complete structural mesh**:
   register scale/depth against the known Blender view, check occlusion and view
   agreement, then subdivide/refine the appropriate surfaces. Keep structural
   geometry wherever evidence is absent or inconsistent. Do not replace the
   blockout with disconnected single-view point-map shells.
   Reconstruct actual three-dimensional meshes from the images. Buildings need
   side and rear surfaces, roof volume, openings and usable interior connections.
   Treat hidden surfaces as generated interpretations, not recovered evidence.
4. Import and assemble the refined meshes in Blender. Retain separate editable modules,
   local origins, scale, materials and connection points. Polish intersections,
   topology and unsupported areas here.
   Preserve generated appearance as well as structure: maintain a separate,
   registered detail bake and UV layer alongside the reusable sheet material.
   Generic tiled sheets alone do not preserve the view's unique ornament, wear,
   masonry, colour variation or painted detail. Inventory those features and
   compare close views after baking; triangle count is not a fidelity measure.
   Silhouette-changing features (foliage, lamps, mouldings, openings) need real
   geometry. Texture detail cannot stand in for their volume or occlusion.
5. Give every playable surface appropriate collision. Doors must be openings in
   geometry; liquids, characters, portals and collectibles remain interactive
   runtime elements instead of baked imagery.
6. Validate the entire playable route from normal third-person camera positions,
   including lateral views, the rear of buildings and interior transitions.
   Reject image planes, missing backs, stretched projection surfaces and gaps.
   Include a full turn at several displaced camera positions and a walkthrough;
   source-image coverage alone is not evidence of a complete 3D environment.
7. Budget and measure the assembled scene against the project's 60 Hz target.
   Preserve source meshes and create separate optimized runtime exports.

Gaussian splats may supplement visual detail. They do not replace the structural
mesh or provide an excuse for a building that only looks correct from one view.

Generated camera views are hypotheses, not calibrated photographs. Check tower
counts, doors, window spacing and room layout before reconstruction. Do not merge
independent MoGe point maps at their camera origins: estimate/register poses,
resolve depth/scale disagreement, reject inconsistent regions, then fuse. Use a
Blender blockout's known cameras and reject layout drift in generated edits. Preserve
per-view masks and confidence. A contact sheet must be treated as separate views,
never passed to a monocular geometry model as one scene.

Texture sheets are base-color candidates, not automatically complete PBR
materials. Check seams, color consistency and baked shadows. Derive normals from
actual geometry where possible; camera-space MoGe normals are not tangent-space
material normal maps. UV seams, texel density, atlas padding, unique ornaments
and material assignments must survive an untextured-mesh round trip.

Detail bakes must reject source foregrounds using both structural visibility and
registered generated depth. Use semantic masks to keep foliage off stone and
photographed lamps/ribs off ceilings. Keep view ownership coherent across
neighbouring surfaces; feather genuine visibility boundaries, not every packed
chart edge. Otherwise narrow wall/arch pieces acquire repeated stripes. Preserve
original images, registration fields, sheet UVs and bake provenance. Check mipmaps
and oblique views in the native renderer, not only a matching source camera.

### Continuous floors and other connected surfaces

The Lanternfall floor exposed a specific failure: individually plausible photo
projections had inconsistent tile scale, colour and grout alignment when viewed
as one floor. Treat this as a regression to prevent in every future module.

- Establish one world-space material scale and origin for a connected floor.
  Object, triangle and UV-chart boundaries must not reset the texture grid or
  trigger a fade into an unrelated fallback texture.
  Apply it to every upward floor surface, including doorway and furniture-covered
  pieces. Visibility at an object's centre must not decide whether the whole
  object receives the material; partially hidden surfaces can remain visible.
- Reuse registered material samples at that common scale. Reject image areas
  containing another object's silhouette, strong local perspective distortion or
  incompatible lighting. Preserve fine source detail without mixing incompatible
  tile grids.
- Compass inlays and other unique ornaments must share the paving coordinates
  and appear once. Their supporting meshes must not introduce a second tile grid
  or mismatched square of background material.
- Validate texture continuity across actual mesh joins and the inlay boundary.
  Inspect downward, overhead, oblique and reverse native views, then traverse the
  surface. A flattering frontal screenshot alone is insufficient.
- Keep the source sheets and image references separate and editable. Record
  material scale, source samples and any deliberate mapping overrides with the
  asset. Automated continuity checks supplement, rather than replace, visual QA.

**Texture sheets remain mandatory alongside generated camera views.** They are
the reusable material source for both the blockout and the refined prefab.
Preserve material IDs and UV assignments across subdivision; provide a reapply
operation for replacement/textureless meshes. Image projections can guide detail
or unique textures but must not be the only way to texture the asset. Ship the
separate source sheets, cell/trim manifest and scale conventions with the Blender
prefab, even when the runtime packs them into one atlas.

The initial Lanternfall panorama/SHARP/DA360 files are an experimental rendering
test. They do not meet this standard and must not be presented as the completed
mesh level. MoGe visible-surface studies are subject to the same limitation.

### Photogrammetry / COLMAP gate

COLMAP is an optional measured reconstruction/refinement stage, not a substitute
for coherent source imagery or the complete structural mesh. See
`docs/photogrammetry-colmap-study.md` for the controlled local experiment.

- Whole-level concept views establish design and visual references. They are not
  calibrated reconstruction photographs. If concepts precede a blockout, lock the
  layout in Blender next and render the calibrated cameras used for detail work.
- Capture overlapping views from translated camera positions. Aim to observe
  each reconstructable detail in at least three views; camera rotation at one
  point supplies no triangulation baseline. Keep object shapes, ornament,
  textures and lighting consistent. More inconsistent generated pictures do not
  resolve contradictory geometry.
- Reuse known Blender intrinsics/poses. Check coordinate conversion and image
  resizing. A known input camera is only a prior for a generated edit: the edit
  can still shift content. Measure feature compatibility with that intended
  camera, track support and reconstruction errors before accepting its detail.
- Treat sparse matches, successful camera registration and dense point counts as
  diagnostic evidence, not a completed mesh or a guarantee of better rendering.
  Check false matches on repeated paving, banners, foliage and symmetric rooms.
- Use reliable multi-view reconstruction to refine bounded regions of the
  complete mesh. Retain the structural mesh where evidence is absent; evaluate
  MoGe predictions as a separate prior rather than merging unrelated depth shells.
  Preserve reusable texture sheets and the continuous-surface material rules.
- Compare known-geometry control renders with generated views using the same
  image resolution and reconstruction settings. Record versions, matching and
  fusion settings, rejected regions, calibration and visual/completeness limits.
