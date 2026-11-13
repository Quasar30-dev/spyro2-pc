# Procedural universe preview

Launch **Run Universe.cmd**, or mount your retail copy in the workbench and choose **Universe → Land on this world**. The seed selects a universe; the world number selects a repeatable destination. **Fly the Starways** starts in space. The same CUE/BIN, ISO and extracted-copy importer is used; no bundled retail assets are required.

On each ground world, Spyro arrives on a clear landing area facing a connected gem trail. The trail passes optional encounters and breakable baskets and ends at an exit. Collecting all trail gems is an optional objective; it never locks the exit. Walk through the **original white-and-gold Return Home portal**, or press **E** near it, to enter the Starways. A second portal behind the landing area provides an immediate return.

In space, Spyro flies automatically with his original Superfly animation family and wing trails. **A/D** turn, **W/S** dive/climb, **Space** flaps and **Shift** boosts. Fly through a destination portal to land on that world. The further-stars portal leads to the next group of destinations. The universe menu also accepts any world number directly. Portals have an arrival cooldown to prevent immediate return trips. Ground portals retain the retail model's scale; space portals use the same model at four times scale for flight approaches.

**F5 / Game → Save game** saves a universe to a PC location; **F9 / Load game** restores it. Retail campaign and universe progress have separate in-memory checkpoints and default save paths. Each visited world's gems, defeated enemies and broken baskets persist independently, even when worlds use the same theme. Loading resumes the saved position; ordinary travel lands at the destination's safe arrival point. Changing universe seed starts a separate universe checkpoint, so save before replacing a universe you want to retain.

## Generation and validation

The route is generated before scenery. It joins five to seven rooms with gentle slopes and a broad, continuous corridor. The landing area and portal approaches are reserved first. Structures have flat foundations, matched visible/collision geometry and an exclusion zone around the route. Placement uses the complete rotating animation envelope for enemies, including the extended club, and reserves the small lizard's retreat segment. Basket origins and retreat waypoints are projected onto supporting ground.

Before a world is accepted, the runtime checks floor continuity at intervals along the route and both shoulders, floor slope, headroom, landing clearance, actor support, model availability, overlapping footprints, material bounds and return-portal connections. Space destinations reserve a clear flight approach past the decorative planets; the onward gate sits above them. Invalid output is rejected before replacing the current scene. Terrain shares coordinates along cell edges, uses fully opaque retail materials, and has skirts around its boundary. The portal opening and travel test use the same five-sided aperture; swept crossing prevents fast flight from skipping a gate.

The first four themes use terrain textures from **Glimmer, Autumn Plains, Skelos Badlands and Crystal Glacier**. Known Glimmer lizard scripts supply the encounters; original gem and basket models, Spyro's global/level animation banks, Sparx, HUD and sound are reused. Ruin structures and island geometry are generated. Unknown Moby scripts and arbitrary level buildings are deliberately not placed until their movement bounds, dependencies and collision can be validated.

## Retail exit portal evidence

- **Glimmer record 185, class `1AA`** is the Return Home arch. Its existing animated-model decoder supplies the original geometry, colours and textures; the portal has one clip (0). The universe initializes it in its open state.
- The original Glimmer dispatcher selects class `1AA` at **`80070478`**, with the case at **`80075024..80075550`**. Proximity opens the aperture; the original code ramps its extent in steps of 32/256. The prototype uses the fully open extent so travel remains available.
- The five local aperture points are read from the mounted executable at **`800631F0`**. The case transforms these at **`80075318`**. They define the polygon for both rendering and native travel detection.
- The arch's original visible faces provide collision. Its flat opening mask is excluded from solid collision, allowing Spyro to walk or fly through the aperture.
- Loader **`8006FA98`** locates the scene's portal section after collision and sky. Its **68-byte records** store a section-relative picture pointer at **`+3C`**. The picture contains two coloured mesh layers: background and destination panorama. Renderer **`8004C66C` / `800296D0`** clips these against the aperture. `portal.cpp` decodes the original packed vertices, colours and faces, then clips them to the native opening. The first preview retains Glimmer's original Return Home artwork inside all universe portals; a live destination preview is future work.
- The Ghidra case export timed out on this large switch; the existing instruction dump and resident/loader decompilations supply the evidence above. No guessed decompiler output is treated as verified code.

## Addressing, saving and editor groundwork

Addresses consist of a **64-bit universe seed**, **64-bit world number**, and ground/space flag. Generation uses a fixed integer random stream and stable traversal order. This provides a practically unbounded address space, not a claim of mathematically infinite unique environments; themes and encounter types currently repeat. Addresses wrap at the unsigned 64-bit boundary.

Generator version **1** is stored in save format **3**, alongside world-specific progress. Previous retail save versions 1 and 2 remain readable. Unsupported generator versions, mixed universe seeds and applying universe records to retail scenes are rejected. Saves use checksums and atomic replacement. The initial format explicitly limits stored progress to **10,000 visited worlds / 64 MiB**; it does not silently discard older progress.

`UniverseLayout` exposes the address, route, landing radius, portals, planets, structures, object footprints, objective IDs and material choices. These are the groundwork for editor tools to inspect, edit and revalidate generation rules. The existing scene editor is not yet a persistent editor for generated terrain; saving a universe checkpoint preserves progression, not arbitrary changes to generated geometry.

## Scope and validation commands

This is a playable foundation with compact island layouts and transitions between loaded scenes. Seamless streaming, broader architecture, procedural quests, biome-specific enemy scripts, live views through portals, per-world music generation and a full procedural level editor remain future work. Original music follows the source theme, with Glimmer's mapped effect bank. Simulation and deterministic video capture run at **60 Hz / 60 fps**; loading a destination may cause a visible pause.

```powershell
./build/Release/spyro_universe_tests.exe data/disc
./build/Release/spyro_workbench.exe --game data/disc --universe 2026 --world 0 --headless --universe-replay --frames 2100 --capture data/validation/universe-travel.bmp
./build/Release/spyro_workbench.exe --game data/disc --universe 2026 --space --headless --video data/validation/universe-space-60fps.mp4 --frames 480 --capture data/validation/universe-space.bmp
```

The universe replay steers the actual controller along the route and toward the next world, without teleporting between capture frames. The normal portal controller performs the transitions. Headless clips retain the existing retail audio mix when the mounted copy includes complete XA sectors.
