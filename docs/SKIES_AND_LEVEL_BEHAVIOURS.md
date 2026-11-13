# Retail skies and additional level behaviours

The mounted USA SCUS_944.25 copy supplies all geometry, palettes, animation clips, collision data, routes and fragments. This pass adds the skies from every scene bank, fixes inverted plain-object quads (including the gem basket), and expands native combat beyond Glimmer. It does not complete every level's quests, enemies or progression scripts.

## Skies

All **41 scene banks**, slots 15–95, decode **1,590 regions / 112,385 triangles**. The decoder reads the original sky directory, packed coordinates, vertex palettes and triangle fan/strip commands. It validates extents and indices, including scenes with more than 128 vertices per region. Seventeen scenes also contain sky visibility trees.

Evidence comes from main routines `80029170` (draw), `8001c5ec` (relocation) and `80048d18` (visibility), plus the overlay-1 scene loader. The renderer centers this geometry on the camera, keeps it behind terrain without writing depth, and interpolates the original colours in screen space. Camera movement changes the visibility mask but does not translate the sky. Generated planets reuse their donor sky without copying a visibility tree tied to a different world layout. Space keeps its existing backdrop.

`tools/verify_sky.py` executes the mounted executable's original `80048d18` code in the bounded MIPS test oracle. Native masks match **20,992 camera positions** across all scenes, executing **4,580,266 retail instructions**. This caught an important detail: a missing octree child means draw the complete sky, rather than retain the parent's partial mask.

Native D3D11 tests cover sky colours, camera translation/rotation, terrain occlusion, visibility selection and exclusion from the isolated model studio. `data/validation/sky-scenes/` contains a native capture and JSON report for every scene, plus `audit.json`.

## Basket and common containers

Plain static quads previously followed triangle winding. Main `80046fd8`, particularly `800479c0..80047ad0`, uses the opposite winding for quads and evaluates both halves to handle folded faces. The corrected shared face selection closes the basket's outside walls and applies to other plain static quad objects. Synthetic geometry and a rendered pixel test cover front/back visibility and the folded case. Compare `basket-before.bmp` with `basket-after.bmp`.

Both heavy-container classes now enter the common container update path. The **25 authored heavy containers** receive their wobble tables, heat response, attack rules, fragments and gem release. Class `CA` additionally accepts headbash; class `CB` requires a powered attack. Ordinary flame and charge do not break either. Both release sixteen original class-134 fragments and their authored gem. Pending gems, broken-container saves, HUD accounting and carrying/drop behaviour share the existing implementation.

Held and thrown objects retain ownership of their own interpolation: the container script must not advance them again. The complete retail carry/paw-clearance suite passes after this correction.

The attack-mask evidence is Summer Forest `800736e4..80073708`; heavy fragments are `8007374c..800737dc`. Heavy containers currently use imported mesh contact where their collision primitive format is not yet mapped.

## Enemy coverage

| Scene | Mapped native enemies | Behaviour added / retained |
| --- | ---: | --- |
| Glimmer | 9 small lizards + 5 club lizards | Existing escape routes, attacks and deaths |
| Hurricos | 14 gear grinders + 8 robots | Commands through authored record links, robot patrols, pursuit, attacks, return home, metal heat/immunity, deaths and robot debris |
| Idol Springs | 6 metal idols | Turn/notice, attack/recovery clips, retail hit window, charge vulnerability and death |

Hurricos evidence is overlay 22 `800765fc..80077270` and `80078ef8..80079dec`; Idol Springs is overlay 18 `80079bcc..8007a124`. `tools/ghidra/DisassembleRanges.java` forces decoding of jump-table case bodies that automatic analysis missed. Raw instruction exports are retained under `data/analysis/overlay_018/` and `overlay_022/`; the attempted Hurricos decompilations timed out and are not treated as evidence.

Hurricos damage uses the original collision-bank selector in animation metadata, switching at the blend midpoint (`80041538`). Type-2 spheres use the separate packed collision vertices, including the midpoint-of-two-bones rule from `8004055c`; they do not use arbitrary visible mesh vertices. The mapped damage cylinders preserve their original dimensions. Thus attack poses and active collision data advance together at the fixed simulation rate. The robot's ten debris pieces use its five original meshes; particle movement uses the native fragment solver.

Enemy scripts update at 30 Hz with 60 Hz animation/presentation. Original patrol speeds, detection distances, clip IDs, command delays, timers and attack rules are used where mapped. Native raycasts handle ground following, walls and ledges. Defeat persistence and spirit accounting use the existing PC save/HUD system.

Remaining work includes the other enemy classes, bosses, friendly NPC challenges/rewards, gear-grinder challenge triggers and linked-worker scripts, complete original movement/collision equivalence, exact debris trajectories and additional death effects. The Metal Idol mapping does not include Idol Springs' shield enemies. These scenes must not be described as fully playable or script-complete yet.

## Reproduce

```powershell
./build/Release/spyro_sky_tests.exe data/disc
python tools/verify_sky.py --game data/disc
./build/Release/spyro_level_behaviour_tests.exe data/disc
./build/Release/spyro_progression_tests.exe data/disc
```

The workbench's object inspector offers **Play beside enemy** for mapped enemies. Hurricos record 125 is a grinder linked to robot 126. Bounded capture accepts `--interaction-record 125 --attack none` to observe its AI without automatically attacking. `flame` and `charge` remain available for combat captures. F10 saves a photo and F11 records 60 fps video with audio.
