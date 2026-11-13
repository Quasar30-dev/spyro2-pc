# Headbash terrain upgrades

Reference investigated on 2026-09-23: [LXShades' SpyroEdit guide](https://lxshades.github.io/spyroedit/spyroedit_guide.html), [Powers.cpp](https://github.com/LXShades/spyroedit/blob/master/Source/Powers.cpp), and [Scene::GenerateCrater](https://github.com/LXShades/spyroedit/blob/master/Source/SpyroScene.cpp). The original triggers its crater from the landing animation, reshapes nearby scene vertices, darkens their colours and relocates nearby objects. Its collision-regeneration documentation specifically warns about losing portal/surface associations. This port implements its own deformation layer and retains those associations.

## Playing

Enable optional progression in **Pause → Options → Upgrades** (or the workbench Upgrades menu). Buy **Headbash** for 25 points, then the second tier for 50 points. The first equips **Crater shockwave**; the second equips **Lava apocalypse**. The **Headbash mode** card selects Normal, Crater or Lava, with Lava requiring tier two. The workbench's existing unlock-all actions include both tiers.

Jump, then press **B**, or use **H** while airborne outside a glide. These are the existing headbash controls. Ordinary landing hits and object attack flags run before the optional terrain effect, including on protected terrain. Disabling progression or selecting Normal restores ordinary headbash behaviour without erasing already created terrain.

Crater mode excavates an annular bowl with an expanding ground flash. Lava mode deepens/widens the bowl and reveals animated molten ground behind the expanding wave. The circular patch immediately below Spyro remains unchanged: a radius of at least 1.05 world units, plus a tessellation margin. Lava damages grounded Spyro/enemies; jumping above it avoids contact. Repeated strikes at the same location do not dig through the island. Using Lava on an existing first-tier crater replaces that crater with the second-tier version.

## Geometry and gameplay protections

- Static floors alone are eligible. Water, tagged special surfaces (including sand), portal contacts, walls, decoded morphs and translated platforms retain their authored geometry/IDs.
- Known NPCs, scripts, portals, whirlwinds, transports, props, spawn points and custom objective checkpoints have protected supports. Custom homeworld and arena mechanism mesh ranges are also preserved.
- Neighbouring ordinary floor can still deform when the impact itself lands on a protected contact. The base headbash always runs. Protection does not invent missing retail script behaviour.
- Local clipping to a 0.42-unit grid adds actual floor geometry while interpolating the original textures and colours. Collision and visible surfaces use the same deformation field. Original array indices remain stable; new faces append after them, preserving script references.
- Lava follows the visible crater surface and clips to a continuous deformation contour. The wave does not regenerate collision tags or alter the mounted retail files.
- Existing craters reserve their footprints and islands against later neighbouring blasts. A scene has a budget of 32 distinct craters and bounded geometry growth. At the budget, ordinary headbash remains usable.

## Persistence and presentation

PC saves now use **S2SAVE 13**, retaining readers for versions 1–12. They store the headbash tier, selected mode and per-level crater stamps, including procedural-world records. Loading rebuilds geometry from the mounted assets; saves contain no arbitrary mesh data. Reapplying the same save does not compound excavation. Existing custom-level playtests retain their pre-existing restriction on retail save slots.

Headbash has a seventh upgrade indicator and obeys the upgrade HUD policy. The default announces each ability once per application session; changing level, loading a save, gaining points or repeatedly attacking does not reset that history. **Options → Upgrade HUD** selects Once per power / On each use / Manual (Tab) / Always / Off, plus reminder duration. Those choices persist in saves; session reminder history intentionally resets when the application exits.

Terrain and hazards belong to the local Game instance, as do the current multiplayer prototype's combat and pickups; crater replication is not part of this implementation.

## Verification

`spyro_headbash_tests` exercises real aerial input through impact, collision/visual agreement, dense seam sampling, safe-island boundaries, disabled/normal modes, lava contact height, upgrades, protected surfaces, repeat strikes and save reconstruction. Its retail pass checks all 41 scene banks and verifies tagged geometry remains unchanged. Existing aerial, progression, upgrade HUD and D3D11 material tests are retained.

Native checks and captures live in `data/validation/headbash-upgrade`. `S2SHOT 2` extends capture actions with a final headbash boolean; version 1 files remain accepted. Clips use the real controller and existing 60 Hz capture/audio pipeline.

Final validation on 2026-09-23:

- 503,074 terrain/headbash assertions passed, including unchanged special geometry in all 41 retail banks. The sampled ordinary floors allowed deformation in 40 banks; the sampled Breeze Harbor floors remained protected. Worst measured carve CPU cost was 10.16 ms after sample caching/local clipping improvements.
- 86 upgrade HUD, 28 aerial/sand, 53 progression and 49 D3D11 material assertions passed. The installed executable passed 71 native menu/input assertions, including cycling Normal/Crater/Lava.
- A native Glimmer save/reload reconstructed the same 996-triangle lava patch and retained Spyro's island position. Both upgrade tiers ran through the actual jump/headbash controller.
- `performance-final.json`: 600 paced frames in 9.99871 seconds, mean CPU work 3.958 ms, p95 5.172 ms. Only startup frame 1 exceeded 16.67 ms. An earlier measurement made while compiler processes were active was slower and is retained separately as `performance.json`.
- `headbash-lava-60fps.mp4`: 360 H.264 frames at 60/1 fps, six seconds, AAC stereo at 48 kHz. Geometry and sound come from the running executable.
- Installed executable SHA256: `A0B184E428B4CB93CB9180F7DE009BD0E5443E8BBBA0455D4CC49812EA77EBC7`.
