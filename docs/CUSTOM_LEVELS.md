# Blender custom worlds

The first authored world is [Hollowbloom Springs](../data/custom-levels/hollowbloom-springs/README.md). Its current `.blend` is the expanded PC master: two castle compounds, village rooftops, a cloister and aqueduct surround the original orchard. Separate PC and reduced `.s2world` profiles are exported from this scene. `Play Hollowbloom Springs.cmd` launches the installed workbench, and **Game → Open custom level…** accepts other supported `.s2world` files. The compact first revision is preserved in `revisions/compact-v1`.

## S2WORLD 1

This bounded text format stores geometry and references into a mounted retail copy. It is an authoring/runtime interchange format, not a retail WAD replacement.

1. `S2WORLD 1`, followed by a quoted UTF-8 title (maximum 128 bytes).
2. Spawn XYZ, yaw in radians, sky RGB. Runtime coordinates are Y-up, with 1 world unit = 1,024 retail units.
3. Material count; each entry is `source_slot tile_index translucent`. Slot -1 means a white tile for vertex colours. The initial loader accepts slots 17, 33 and 59. Tiles are 64×64, indexed in a 16-column decoded terrain atlas.
4. Triangle count; each entry starts with `flags material_index`, followed by three vertices, each `x y z r g b u v`. Flags: 1 visible, 2 collision, 4 water. UVs are local to the chosen tile, in Blender's bottom-up convention. The loader assembles a private atlas and converts V for D3D. Colours and UVs are bounded to [0,1]; input geometry must be finite and non-degenerate.
5. Actor count; each entry is `class_id source_record x y z yaw gem_value`. The initial curated bank is Glimmer: gem 0x1, basket 0xC8, small lizard 0x1F0, club lizard 0x22B and Return Home 0x1AA. Instances use decoded retail models, textures, animation and existing runtime behaviours. Original remote route/link data is reset. Sparx is instantiated separately.
6. Main route point count, followed by XYZ coordinates used by validation.

Limits: 64 MiB input, 64 materials, 200,000 triangles, 10,000 actor placements and 4,096 route points. A supporting spawn floor is required. The loader rejects unsupported banks, invalid counts, non-finite positions, out-of-range material references and trailing input.

## Original tiles (S2WORLD 4, 10 October 2026)

Worlds may ship their own 64×64 textures, painted with the retail tiles as visual and technical reference. Retail pixels are never shipped.

- **Authored material.** A material entry `-2 "textures/name.rgba" 0` loads a raw 64×64 RGBA file (exactly 16,384 bytes) stored beside the world. Rules:
  - The path must be relative, end in `.rgba`, and contain no `.` or `..` parts.
  - Authored tiles are opaque and cannot be water.
  - UVs are tile-local, as for retail tiles.
- **`TILE_OVERRIDES 1`.** This optional last section has a count, then lines of `slot tile "textures/name.rgba"`. Every material of the world that uses that retail (scene, tile), including tiles merged from `RETAIL_TERRAIN`, shows the authored file instead. An override that matches no material is rejected.

The Spring Realm uses both:
- `blossom-meadow` patches under its trees;
- `blossom-canopy` on the great cherry tree;
- overrides that repaint the intro's autumn-yellow canopies (WAD 73 tiles 11 and 12) spring green, so the retail scene's own trees are green too.

The painters are in `data/custom-levels/spring-realm/source/paint_*.py`. `spyro_spring_realm_tests` checks that the tile reaches the atlas unchanged, and that escaping paths and non-`.rgba` files are refused.

## Integration boundaries

`Level::custom_name` distinguishes this world from its source actor bank. The workbench skips source-level campaign restoration and automatic cinematics. Custom progress cannot be captured into or restored from retail save slots. Online rooms reject custom worlds until a shared world identity and asset handshake are implemented. The original return-portal artwork is positioned at the authored arch; walking through it loads Summer Forest through the normal transition flow.

The existing runtime supplies movement, swimming, HUD, animation, interaction and audio. This first world has no new dialogue, cutscene or persistent quest script. Source-level Glimmer music plays. The reduced profile is testable on PC, but PS1 archive packing, visibility compilation, palette allocation, memory accounting and hardware validation remain separate work.

The inspector caches retail cinematic availability at level load. The old per-frame queries each recalculated the executable CRC; eliminating those scans removes approximately 8 ms of redundant UI work on the checked machine. Custom worlds hide source-level cinematic controls and the incompatible retail `.s2scene` project exporter.

## Validation

After rebuilding, run `tools/install-workbench.ps1` **before** tests. The custom-world check accepts `spyro_custom_level_tests <retail-copy> <world.s2world> [platforming.s2courses]`. It checks route support across a player-width corridor, every actor's floor, full traversal through the real controller, gem collection, all enemies' defeat, basket destruction, actual exit entry/loading request and retail-save isolation. When given the expansion's course manifest, it also traverses each optional platforming branch with normal jump inputs and checks every landing. Core and progression regressions should accompany loader/save changes.

See `data/validation/hollowbloom-expansion/` for the current expansion checks and native screenshots, and `data/validation/hollowbloom/` for the original compact revision. Blender design images are labelled separately from native captures.
