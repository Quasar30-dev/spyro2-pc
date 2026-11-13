# Optional dynamic terrain destruction

Enable **Pause → Options → Terrain damage → Dynamic destruction**, or the
**Terrain destruction** panel in the workbench. The feature starts disabled.
Supercharge, superflame and green bombs have separate switches; impact strength
has three levels. Options and damage history are stored in PC saves and retained
when returning to a level. Turning the feature off stops new damage. **Restore
current level terrain** removes this layer while retaining headbash craters.

Supercharge collisions tessellate the local wall, push it inward, darken the
impact, project branching cracks onto the resulting surface, and release textured
chips. Superflame and green bombs cut through building shells and leave textured,
collidable rims; green bombs remove a larger volume. Split flame fragments make
smaller holes. Solid cliffs and ground receive textured cavity surfaces so their
missing original interiors do not expose bare sky; openings into rooms remain
passable. Gems, containers and enemies on changed floors follow the new support
height. Detached pieces retain the source atlas UVs and colours and have
fixed 60 Hz motion, gravity, rotation, collision, bounce and a timed fade. The
normal retail projectile and explosion effects remain active. Debris shares a
local collision broad phase and uses substeps when falling quickly.

Normal charge and supercharge also have their missing solid-wall recovery:
Spyro reverses to one quarter of incoming velocity, plays retail primary clip
13, sounds global effect 2, and returns to control after 41 simulation ticks.
This reaction does not remove health and works with destruction disabled.
The resident exports `800351d8_FUN_800351d8.c` (case 0x0D) and
`80035d38_FUN_80035d38.c` (cases 0x0C/0x0D) provide the velocity, sound and
recovery timing. These are in `data/analysis/main/decompiled`.

Visible geometry and collision use the same clipping volume and deformation
field. Source triangle indices stay stable; removed collision faces are disabled.
New vertices interpolate the original UVs and colours instead of reprojecting the
surrounding textures. Shared grids keep wall dents continuous across triangle
boundaries. Authored wall/floor/corner joins keep an undeformed attachment band,
including T-junctions where a floor edge meets the middle of a wall triangle.
Shared grid-corner weights are cached during deformation. Topology changes only
replace geometry buffers; loaded terrain atlases, sky and splat resources remain
resident. Retail archives, source assets and the editor's original scene remain
unchanged; gameplay owns the modified copy.

Gameplay protection preserves spawn supports, Spyro's immediate footing,
scripted/NPC supports, power gates, portal mouths, objective checkpoints and
headbash floor footprints. Water, portal collision and indexed moving or morphing
terrain remain protected. The gameplay protection switch permits experimentation
with ordinary special surfaces. Terrain is reconstructed in a consistent order:
headbash first, then the saved destruction impacts.

With gameplay protection enabled, horizontal wall blasts retain the adjacent
floor beneath their opening in both collision and visible geometry. This avoids
floating ledges and missing floor triangles when breaching a room. Direct floor
impacts and the separate headbash deformation layer retain their behaviour.

CLI options for bounded captures and checks:

```
--terrain-destruction
--destruction-strength 1|2|3
--unprotected-destruction
--interface-page destruction
```

Save format 15 adds settings and deterministic impact records; formats 1–14
remain readable. Transient chips and dust restart empty on load. Per-level impact,
triangle and debris budgets bound memory and simulation work. This implements
local mesh destruction; it does not simulate whole-building structural collapse
or replicate terrain edits to remote multiplayer peers.

Implementation: `destruction.hpp/.cpp`, charge collision in `runtime.cpp`,
powered projectile contacts in `powerups.cpp`, dynamic uploads and debris in
`renderer.cpp`, and the terrain page in `desktop_upgrades.inl`.
Validation lives in `tests/destruction_tests.cpp` and the D3D11 destruction/UV
checks in `tests/renderer_tests.cpp`.

## Installed-build validation — 2 October 2026

The main executable was installed before validation; its SHA256 matches the
candidate: `64226836DD768F384B47383BF09B9F9FDBF7908FC0F0481283B10500C43EB8A7`.
All ten selected suites pass: destruction, headbash, D3D11 terrain rendering,
traversal, terrain materials, prerelease powers, portals, jump rules, powerups
and campaign commands. The destruction suite performs 503,548 assertions;
the renderer performs 60 checks. The retail sweep finds a damageable sampled
ordinary wall in 40 of 41 banks and preserves their water, portal and special
surface geometry. Prototype/demo powers and recoil remain tested across 59 banks.

New captures inspect Glimmer (slot 17) from front, overhead and both oblique
sides. Summer Forest (slot 15) supplies a tall building wall whose blast opening
is clear in both visible and collision meshes. The recorded controller run jumps
through that opening, crossing the wall plane at X=43 and landing outside at
X=38.70 with full health. Original floors/tiles remain aligned across the joins.

`data/analysis/destruction-20261002/dynamic-terrain-destruction-60fps.mp4` is a
15-second demonstration from the installed executable: supercharge/recoil,
superflame, green bomb and traversal through the Summer Forest wall. FFprobe
confirms 60/1 video and 48 kHz stereo audio; the audio is non-silent.

An isolated, paced 600-tick bomb run measured 6.58 ms mean CPU work and
16.07 ms P95, with 28 frames exceeding 16.67 ms and a 71.81 ms maximum.
The corresponding destruction-disabled baseline also had frame hitches.
Physics and debris run at fixed 60 Hz; these results do not establish locked
60 fps across the port. Full reports and camera/action sources are retained in
the capture directory. Build/test logs are in `build/destruction-*.log`.
