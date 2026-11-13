# Season of Ice in the Spyro 2 engine: project plan (prep)

Status: **phase 2 in progress, 3 November 2026** (Season of Ice agents 1, 2 and 3). Phase 1 (prep) is complete;
see §10 for phase 2; **the current handoff is §10.10** (agent 3), **paused state §10.11** (agent 4). Nothing in `src/` or the main
executable was changed. Goal, in the user's words: Spyro: Season of Ice's levels running in the
Spyro 2 engine, on the PC reimplementation and as the first Spyro 2 ROM mod of the retail PS1
disc, with SoI's characters and objects replaced by their Spyro 2 equivalents and the levels rebuilt
as 3D equivalents in Blender.

Work lives in `tools/soi/`, `data/soi/` and this file. Large or ROM-bearing outputs are on D:
(`D:/Theia-staging/spyro2-mod/`). The SoI ROM and the retail disc are read-only inputs and are never
copied into the tree.

## 1. Inputs

| Input | Identity |
| --- | --- |
| SoI ROM `D:/Spyro - Season of Ice (USA)/Spyro - Season of Ice (USA).gba` | 8,388,608 B, title `SPYRO`, code `ASYE`, maker `7D`, SHA-256 `d331abe1…8d024b0`, CRC32 `2f2389d2` |
| Spyro 2 retail BIN/CUE (`Spyro 2 - Riptos Rage (USA)/`) | MODE2/2352, 240,379 sectors, SHA-256 `b3fd9fdb…dec64551`; exe SCUS_944.25 CRC32 `cdc0917c` |

## 2. Season of Ice formats (proven by the ROM's code)

Oracle: the ROM's own THUMB code, analysed with headless Ghidra 12.2 (`tools/soi/ghidra/`,
project on D:, 1,605 functions, all decompiled; export in `data/soi/analysis/ghidra/`). Every
format below names the function that reads it. Fan documentation was not used.

### 2.1 ROM header and code layout
- Standard GBA header; ARM entry `08000000 → 080000c0` sets the IRQ/system stacks, installs the IRQ
  handler pointer at `03007ffc` (handler `03001070`, IWRAM) and jumps to THUMB `main` at `080035d0`
  (a state-object loop: current state pointer at `30010ac`, pending state at `30010a8`, state
  function pointers at +8/+c/+18/+1c/+20/+24).
- Code occupies `08000000–0805ffff`; data follows. Free padding starts at `087cfe94`.
- BIOS wrappers: `0805d230` SWI 0x11 LZ77UnCompWram, `0805d22c` SWI 0x12, `0805d228` SWI 0x13
  Huffman, `0805d238` SWI 0x14 RL, `0805d218` CpuSet. `tools/soi/gba.py` implements the BIOS formats.

### 2.2 Asset tables
- **Resource cache** `08056340(id)`: table `081adce0`, 83 entries of `{ptr, size, codec}` (codec 0 raw,
  1 Huffman, 2 LZ77, 3 RL). All 83 are LZ77 and decompress to their recorded sizes (UI/sprite data).
- **Sprite banks** `08004e3c(src, dst, slot)`: 0x18-byte slot records; per level, `0801584c` loads
  slots 0x00–0x22 (shared) and 0x23–0x29 (the level's own enemies/props). Not decoded yet (§9).
- **Speedway textures**: `KTX` blocks (`"KTX\0"`, 64×64, name `Z000`–`Z003`, 64 row offsets) at
  `08068fa4…`, four zones × four copies: the four "Age" speedways' affine ground textures. Not used by
  the isometric levels.

### 2.3 Level list (17 isometric maps)
`08016d38(L)` loads isometric level `L` (0–16) by calling every per-level loader with the same index.
The byte table `081ac190` (literal at `08027b58`, read by `08027b3c`) maps level → **game order** 0–23 (24 entries, the speedways
and Sparx levels are 17–23), and the atlas table `081aace0` (four rows of 6 string ids + 6 game-order
ids) gives each one its name and world. Names come from the ROM's text (§2.8).

| L | Name | World / slot | L | Name | World / slot |
| ---: | --- | --- | ---: | --- | --- |
| 2 | Autumn Fairy Home | Autumn 0 (game start) | 3 | Winter Fairy Home | Winter 0 |
| 6 | Market Mesa | Autumn 1 | 8 | Hummingbird Fort | Winter 1 |
| 4 | Lava Prairie | Autumn 2 | 9 | Panda Gardens | Winter 2 |
| 5 | Mermaid Coast | Autumn 3 | 7 | Honey Marsh | Winter 3 |
| 0 | Spring Fairy Home | Spring 0 | 1 | Summer Fairy Home | Summer 0 |
| 12 | Time Machine Lab | Spring 1 | 10 | Dusty Trails | Summer 1 |
| 14 | Roman City | Spring 2 | 11 | Star Park | Summer 2 |
| 13 | Twilight Bulb Factory | Spring 3 | 15, 16 | Grendor's Lair (two arenas) | Summer 5 |

Non-isometric entries (other game modes, not extracted yet): Stone/Ice/Aqua/Space Age Speedway
(flying, mode 7), Ant Farm, Wasp City, Beetle Burrows, Caterpillar Gardens (Sparx top-down shooter).

### 2.4 Background graphics (the iso "terrain" art)
`08016780(L)`: palette `[081ab680+4L]` (0x200 B, 16 banks), tiles `[081ab6c4+4L]` raw 4bpp DMA'd to
VRAM, size `[08061b24+4L]`; four layers `[081ab570+16L+4k]` LZ77, buffer sizes `[08061a14+16L+4k]`.
`08005970(k, buf)` reads each layer: 12-byte header `u16 flags, u16 priority, u16 width/8,
u16 height/8, u8 metatile w, u8 metatile h, u16 count`; when `flags & 3`, `count` metatiles of
(w/8 × h/8) u16 screen entries follow, then the metatile map; char base `(flags >> 9) & 3`.
Layers 0–2 are 2400×1600 px built from 16×32 px metatiles (150×50 map); layer 3 is a 32×32 backdrop.
Every layer of all 17 levels decodes; one buffer (L13 layer 1) is allocated larger than its LZ77
output, which the loader allows.

### 2.5 Collision and heights (solid prisms)
`08014e30 → 08013cd0(P)`, `P = [081ab49c+4L]`: `u32 n, u32 n_occ`, n × 12-byte records, the edge
lists (16 B each: `s16 nx, ny` (1.0 = 1024), `s32 d`, `s16 x0, y0, x1, y1`), then the broadphase grid
read by `08013bd4` (100×100 cells, cell 14 units, per-cell counts and s16 record lists).
- The floor/wall code `080137e0 → 08013528 / 08013380` walks the grid. **The grid lists only records
  0…n−n_occ−1** (checked on all 17 levels), so those are the solids.
- A solid is a vertical prism from the bottom of the world up to **top z = (u32 & 0xffffff) >> 14**.
  `byte3 >> 3` is its edge count; 0 means an axis-aligned box whose four edges the code synthesises
  from the bbox. In practice **every solid in every level is an axis-aligned, flat-topped box.**
  Heights step in 24-unit terraces (25, 49, 73, …).
- The last `n_occ` records are **sprite occluders**, not collision: `08013e54(pos, margin)` tests the
  point against them (45° planes: kind 1 normal (−514,−514,728), kind 3 axis normals) and returns the
  OAM priority a spawner (e.g. gems, `0801c474`) gives the sprite, so the pre-drawn BG foreground can
  cover it. They are kept for reference only.

### 2.6 Objects, start point and projection
`08016594(L)`: list `[081ab74c+4L]` = `u32 n`, n × `{u16 type, s16 param, s32 x, y, z}` (world units
<< 10), spawned through the switch `080170a4(type, pos, param)`; player start `[08061b68+12L]`.
`0801ec60` gives the projection: **screen x = 2(x − y), screen y = x + y − z** (BG pixels). Overlays of
solids and objects on the maps line up (`overlay_half.png` per level).

| Types | Spawner | What is proven |
| --- | --- | --- |
| 1 | `08018ad0` | the player, spawned at the start table entry |
| 0, 2, 3, 15, 21 | various | spawned by the loader itself at fixed points (system objects) |
| 117–120 | `0801c474` | **gems**, value byte from `081ab8f0` = 1, 2, 5, 10; each takes a save id |
| 40–81 | `0801d428` | static sprite props, 0x2c-byte records, two behaviours (jump table `0801d538`) |
| 10, 11, 16, 90, 126–129, 245 | own spawners | per-level families (3–15 per level); not identified yet |
| 130–134, 135–139 | `0801fbc4` | in every level, ~10 and ~5 per level; not identified yet |
| 140–244 in fives | one spawner per family | level-specific families (two per level), almost certainly enemies |
| 115, 116 | `08033b80` | only in the two Grendor arenas (the boss) |

Warps: homeworld portals and gem-priced doors are not decoded yet; the text has a gem-price prompt
(`Pay … gems to enter`) and "Return to …" menus, so the portals are among the unidentified families.
Goals: the HUD texts are "Collected:", "Total Items", "Treasure found:" and per-character rescue
lines; loose gem totals per level are in the inventory below.

### 2.7 Text
`08002c50 / 08002b20 / 08002e88`: u16 offsets at `08270434` relative to the table; each string is a
length byte then characters (A–Z = 25–50, a–z = 51–76, 0 = space, a few punctuation codes).
461 strings; `data/soi/text/strings.json` holds them for internal reference.

## 3. Level inventory

`python tools/soi/soi_extract.py` writes, per level, `data/soi/levels/NN-name/`: `map.png` (2400×1600),
`map_half.png`, `thumb.png`, `overlay_half.png` (solids red, gems cyan, others magenta, start white),
`heights_topdown.png` and the 16-bit `heights_topdown_x64.png` (z × 64, 1 px = 1 unit),
`bg3_backdrop.png`, and `level.json` (layers, every prism with its edges, every object, start).
`data/soi/inventory.json` and `data/soi/levels/contact_sheet.png` summarise them; the maps,
overlays, height maps and the contact sheet were opened and inspected.

| L | Name | XY extent (units) | Height range (terraces) | Solids / occluders | Loose gems (value) | Families |
| ---: | --- | --- | --- | --- | --- | --- |
| 0 | Spring Fairy Home | 1082×962 | 25–433 (26) | 261/100 | 113 (254) | 7 props, 4×t16, 129 |
| 1 | Summer Fairy Home | 986×1130 | 25–377 (23) | 253/93 | 120 (277) | 7 props, 3×t11, 2×t90 |
| 2 | Autumn Fairy Home | 1250×1034 | 25–329 (18) | 274/119 | 121 (265) | 6 props, 5×t10, 2×t90 |
| 3 | Winter Fairy Home | 1226×1058 | 25–329 (17) | 284/102 | 122 (285) | 7 props, 18×t245 |
| 4 | Lava Prairie | 1226×1154 | 1–169 (9) | 308/81 | 121 (241) | enemies 140s, 145s |
| 5 | Mermaid Coast | 1250×1226 | 1–185 (10) | 297/68 | 116 (236) | enemies 150s, 155s |
| 6 | **Market Mesa** | 964×868 | 73–561 (27) | 283/114 | 110 (240) | enemies 180s, 185s; 3×t16 |
| 7 | Honey Marsh | 1198×1242 | 1–153 (9) | 175/143 | 108 (229) | enemies 170s, 175s; 14×t16 |
| 8 | Hummingbird Fort | 1218×1194 | 25–185 (14) | 224/154 | 124 (268) | enemies 165s, 180s |
| 9 | Panda Gardens | 1202×1190 | 1–177 (15) | 244/83 | 95 (184) | enemies 190s, 195s |
| 10 | Dusty Trails | 1250×1214 | 1–153 (11) | 267/64 | 112 (283) | enemies 200s, 205s |
| 11 | Star Park | 1274×1082 | 1–113 (6) | 199/122 | 121 (297) | enemies 225s, 230s |
| 12 | Time Machine Lab | 1178×1178 | 1–193 (9) | 230/141 | 96 (273) | enemies 220s, 240s |
| 13 | Twilight Bulb Factory | 1232×1178 | 1–121 (5) | 136/120 | 117 (329) | enemies 215s, 220s |
| 14 | Roman City | 1274×1254 | 1–193 (10) | 351/101 | 107 (227) | enemies 180s, 210s |
| 15, 16 | Grendor's Lair | 402×522 | 1–65 (2) | 13/10 | 0 | boss 115 / 116 |

Every regular level also has the 130–139 families (≈15) and one 40–81 prop.

## 4. Character / object mapping, SoI → Spyro 2

S2 classes come from the retail census `tools/soi/s2_class_census.py` (5,200 records; it reproduces
the documented 5,200 records and 1,669 gems) and the project's docs. "Native" means the PC runtime
already has a reconstruction.

| SoI | SoI type | Spyro 2 equivalent (class, where) | Behaviour needed | Status |
| --- | --- | --- | --- | --- |
| Spyro | 1 | global Spyro model/banks | movement, glide, charge, flame | native |
| Sparx | system | Sparx, class 0x78 (resident) | follow, gem fetch, health | native |
| Gems 1/2/5/10 | 117–120 | gem, class 0x001 (21 levels; 1/2/5/10/25 denominations) | pickup, Sparx fetch | native; 1:1 values |
| Zoe | — (story) | Zoe, class 0x110 (20 levels) | checkpoint/talk | native script runs |
| Hunter | text speaker | Hunter, class 0x1BC (Summer Forest) / 0x2AC (Ripto's Arena) | conversation | native |
| Moneybags, gem-priced doors | text "Pay … gems" | Moneybags 0x077 (home worlds) / 0x220 (Glimmer bridge) | payment command 25 (`8003a568`) | native for Glimmer; extend |
| Fairies to rescue | candidate 135–139 or 10/11/16 | fairy speaker: Zoe model 0x110 + talk trigger 0x110 | rescue on hit, counter | **identify first**; reuse |
| Frozen fairies / ice | candidate 245 | Cloud Temples ice shell (class 0x1EE pose 4, model 271) | break on flame/charge | partly native |
| Containers / treasure | candidates 126–134 | basket 0x0C8, vase 0x0C9, chests 0x0CA/0x0CB/0x0CC | break, release gems | native |
| Sheep (charge tutorial) | in text | sheep 0x12C + butterflies (Summer Forest) | flee, feed Sparx | native |
| Portals home ↔ level | 40–81 / 90 (candidates) | level portal (home world portal records), Return Home 0x1AA | load scene, arrival | native |
| Whirlwinds / lifts | none seen | whirlwind 0x400 | lift | native (if wanted) |
| Rhynocs (Grendor's minions) | 140–244 families | S2 has no Rhynocs: Riptoc/lizard enemies with native scripts (Glimmer club lizard 0x22B, small lizard 0x1F0) | patrol, attack, die | **gap**: closer match = Spyro 3 Rhynocs (YotD disc, provisional support) |
| Bianca | text speaker | not in S2 | dialogue | **gap**: Spyro 3 donor or reuse Elora/Hunter |
| Grendor (boss) | 115, 116 | no S2 equivalent; arena/boss-script donors: Crush's Dungeon, Gulp's Overlook, Ripto's Arena (their original scripts already run in the project's interpreter) | boss script | **gap**: new boss on S2 combat |
| Speedways (4) | game modes 17–20 | Ocean/Metro/Icy/Canyon Speedway rules | flight trial | native rules; new terrain |
| Sparx levels (4) | game modes 21–24 | none | top-down shooter | **gap** (skip or new mode) |

## 5. What a Spyro 2 level needs

### 5.1 Data, per the project's decoders
A scene is a root WAD odd slot (campaign index i → slot 15 + 2i; names at `800649b4`) with four
children (VRAM image + level data, scene (terrain regions, near geometry, colours, materials, sky,
collision), model bank, gameplay (object records, params, cameras, portals)) and its gameplay
overlay in the preceding even slot. Collision is triangle-based (12-byte packed triangles with
surface tags), not prisms; terrain is regioned near geometry with far LODs; mobys are 88-byte
records with per-class parameters; music and speech are XA streams in SPEECH.STR; sounds are SPU
banks. See LEVEL_SYSTEMS, OBJECTS_AND_GEMS, CAMPAIGN_AND_TERRAIN, CUSTOM_LEVELS.

### 5.2 Retail budgets (measured, 29 campaign levels)
`tools/soi/s2_budgets.py` (from the WAD TOC and `spyro_inspect --all-levels`), in
`data/soi/ps1/s2_budgets.json`:

| Measure | Min | Median | Max (level) |
| --- | ---: | ---: | ---: |
| Scene slot bytes | 1,476,608 | 2,064,384 | 2,181,120 (Mystic Marsh) |
| Gameplay overlay bytes | 75,776 | 92,160 | 126,976 (Ripto's Arena) |
| Terrain faces | 1,707 | 6,610 | 8,160 (Summer Forest) |
| Terrain triangles (native) | 3,268 | 12,146 | 14,929 (Metro Speedway) |
| Terrain regions | 36 | 128 | 182 (Colossus) |
| Terrain materials | 25 | 73 | 94 (Aquaria Towers) |
| Collision triangles | 3,874 | 12,140 | 22,283 (Ocean Speedway) |
| Sky triangles | 247 | 2,897 | 3,396 |
| Object records | 7 | 205 | 301 (Hurricos) |
| Model classes | 19 | 48 | 58 (Idol Springs) |
| Texture palettes | 34 | 85 | 119 (Shady Oasis) |
| Loose gems | 0 | 70 | 112 (Idol Springs) |

Scene children: child 0 757–1,030 KB (larger than a 512×512 16-bit VRAM image, so it carries more
than VRAM; to be split), scene 195–844 KB, model bank 229–553 KB, gameplay 29–254 KB.

### 5.3 How the retail executable finds a level (SCUS_944.25)
- `80011b70`: reads **LBA 500**, 0x800 bytes (WAD.WAD's first sector), stores the base LBA (500) at
  `800682d8`; `8001b40c` copies **0x628 bytes = 197 slot pairs** to `800676d8`. The file name is never
  searched: WAD.WAD must stay at LBA 500 and the TOC holds exactly 197 slots.
- `80013810(base, dst, size, offset)`: CdlSetloc to `base + offset >> 11` and reads ⌈size/2048⌉
  sectors, so **slot offsets must be 2048-aligned** and the destination must hold the rounded size.
  Level loads call it with the TOC entry of the scene and overlay slots (`80011ee0`, `80017340`,
  `800193d0`, `80019564`, …).
- SPEECH.STR's base LBA 90,000 is a data word at `80066d44`.

## 6. ROM-mod pipeline (first proof)

`python tools/soi/ps1disc.py roundtrip` decomposes the disc into a model (ISO files and their tail
padding, WAD.WAD **rebuilt from its own 197-slot TOC and payloads**, every non-file sector as raw user
data, both XA subheader copies of all 240,379 sectors), then re-encodes sync, MSF header, EDC (Form 1
and Form 2) and Form 1 P/Q ECC with vectorised ECMA-130 code.

**Result (3 Nov 2026): byte-exact.** In memory and as a written file, SHA-256
`b3fd9fdb24b115fee4583dbe3c36ab788fd8679fb4592dbec8736392dec64551` = retail, 0 differing sectors,
42 s. Facts recorded in `data/soi/ps1/retail-disc-model.json`: 133,317 Form 2 sectors (none with a
zero EDC), 128,365 subheader runs, 152 used WAD slots, no unaligned slot offsets, **3,116 free
sectors (6.38 MB) between WAD.WAD's end and SPEECH.STR (LBA 90,000)**: a WAD that grows by less than
that needs no other file moved.

The written copy was checked and is deleted (the coordinator moved staging inside safe_delete's
roots: **`D:/Theia-staging/spyro2-mod/`**; write mod images only there).

Mod rules derived: keep WAD.WAD at LBA 500; replace or grow slots but keep 197; keep offsets
2048-aligned; grow into the 3,116-sector gap, or move SPEECH.STR and patch `80066d44`; update the ISO
directory record sizes; re-encode sectors with `ps1disc.encode`.

### Emulators (none installed; the user decides)
| Candidate | Gives us | Hidden / batch route |
| --- | --- | --- |
| DuckStation | accurate PS1, fast boot, save states, memory cards | `duckstation-regtest` runs headless for N frames and dumps frames; Qt build `-batch -nogui` |
| PCSX-Redux | debugger, GDB server, Lua scripting, memory/VRAM viewers | `-no-ui -run -exec`, Lua automation; best for RE traces |
| Mednafen (Beetle PSX) | very accurate core, CLI | command line, no native hidden mode (needs a hidden-window wrapper) |
| mGBA (for SoI runtime evidence) | GBA debugger, Lua scripting | `mgba` headless / scripting; would let us check RE findings in the running ROM |
Recommendation: DuckStation (hidden regression boots of the mod) plus PCSX-Redux (traces); mGBA if
SoI runtime checks are wanted. PCSX2 is excluded by the user's rule.

## 7. Pilot level: Market Mesa (L6)

**Choice (the user delegated it):** Market Mesa, the first level of the game (Autumn world, slot 1).
- Small-to-medium: the smallest regular level by footprint (307k unit², 964×868 units; the others
  are 345k–550k).
- The clearest height structure: a stepped mesa town rising from the plaza (73) to 561, **27 terrace
  levels**, the most of any level, plus bridges to three outlying blocks: it reads well in 3D.
- Clean mapping: gems, two enemy families (180s, 185s), 3 × t16, one prop; no boss, no special mode.
- Recognisable: it is the first level SoI players enter.

**Blockout** (`tools/soi/blender_blockout.py`, Blender 5.2 headless, `-b --factory-startup`):
`data/soi/pilot/06-market-mesa_blockout.blend` with the 283 solids as one mesh (3,396 triangles,
per-terrace materials, prism index face attribute), the 114 occluders as hidden wireframes, 151
markers (gems by value, enemies, props, player start) carrying their SoI type/param/handler, and four
cameras. Renders in `data/soi/pilot/renders/`: `iso_match.png`, `top_down.png`, `overview.png`,
`start_view.png`, and `iso_match_vs_map.png` (50 % blend with the original map). All were inspected.

**Scale.** Blender X = x·H, Y = −y·H (SoI axes are left-handed), Z = z·H/√6. With that vertical
factor the game's projection is exactly an orthographic camera at 30° elevation and 45° azimuth;
the `soi_iso_camera` render lines up with the ROM map (silhouette IoU 0.90 at 1200×800; the rest is
the blockout's extruded underside versus the map's cliff art). H is provisional at **0.15 m per SoI
unit** (1 m = 1 Spyro 2 world unit = 1,024 retail units): Market Mesa becomes 144 × 130 × 30 m and a
24-unit terrace 1.47 m. Calibrate H against SoI's jump height and run speed (player code `08018ad0`)
and Spyro 2's before authoring.

**From blockout to a Spyro 2 level, both targets** (Theia "Image-to-3D asset authoring" and this
project's image-to-level standard apply):
1. Lock the route: keep the blockout as the authored layout (landing at the start marker, terrace
   routes, bridges); turn sheer 24-unit steps that the S2 jump cannot clear into ramps or stairs and
   record each change.
2. Look: Spyro 2 terrain is vertex-coloured, textured polygons (64×64 tiles, palette textures). Use
   the SoI map art as the colour/material reference: generate reference views from the
   `soi_iso_camera` plus forward/reverse route views, then material and trim sheets (stone plaza,
   white walls, blue tiled roofs, awnings), applied by region with shared texel density.
3. PC target: export S2WORLD (custom_level.cpp) with authored 64×64 tiles; mobys from the mapping
   table; collision = the terrain triangles.
4. PS1 target: within the medians above (≈6,600 faces / 12,000 triangles, ≈73 materials, ≈12,000
   collision triangles, ≈200 objects, scene ≤ ~2.1 MB, overlay ≤ ~92 KB) and a 512×512 VRAM page:
   the blockout's 3,396 triangles leave room for detail; reuse a retail level's overlay (Glimmer or
   Summer Forest) and its classes so no new MIPS code is needed for the first proof.
5. Keep the master `.blend` and three LOD candidates; the user picks the in-game version.

## 8. Risks and decisions for the user
1. **Emulator** (§6): approve DuckStation (+ PCSX-Redux, + mGBA?) or none. Without one, the ROM mod
   can only be proven structurally (round trip, TOC, loader rules), not booted.
2. **Pilot**: Market Mesa chosen (delegated); say if another level is preferred.
3. **Gaps**: Rhynocs, Bianca and Grendor have no Spyro 2 equivalent; use Spyro 3 (YotD) donors
   (the project already reads that disc), substitute S2 characters, or author new ones. The Sparx
   shooter levels have no S2 mode.
4. **Scale**: confirm the 0.15 m/unit start point or ask for a gameplay calibration first.
5. **Overlay code for the PS1 mod**: a new SoI level needs gameplay code; the first mod should reuse
   one retail overlay and only retail classes. New behaviours (fairy rescue, ice blocks) mean MIPS
   patches or a new overlay.
6. **Disk**: C: had 3.6 GB free at the end of this pass (other work used it; this pass added 71 MB on
   C:). The 565 MB round-trip copy on D: and the empty `C:/data/soi/pilot/renders` folders (made by
   the first Blender run before its paths were made absolute) are left for the user because
   safe_delete refused them.

## 9. Next steps
1. Decode the sprite banks (`08004e3c` records, slots 0x23–0x29) and render one frame per object type
   to name every family (fairies, chests, enemies, portals).
2. Decode the homeworld portal/gem-door objects and the save-id usage (`08027b70`, `0800a334`) for
   goals per level.
3. Calibrate H from SoI's player physics.
4. First S2WORLD export of the Market Mesa blockout on PC (custom level loader, no runtime change).
5. First structural PS1 mod: replace one scene slot with a rebuilt copy, re-run `ps1disc`, then boot
   it hidden once an emulator is approved.

## Tools
| Tool | Purpose |
| --- | --- |
| `tools/soi/gba.py` | ROM access, BIOS LZ77/RL/Huffman |
| `tools/soi/soi_extract.py` | all 17 levels: maps, overlays, heights, JSON, inventory, text |
| `tools/soi/ghidra/run_ghidra.sh`, `PrepareSoI.java`, `ExportSoI.java` | headless Ghidra import/export (project on D:, ROM via a hard link without parentheses) |
| `tools/soi/ps1disc.py` | PS1 disc model, EDC/ECC encoder, byte-exact round trip |
| `tools/soi/s2_budgets.py`, `s2_class_census.py` | Spyro 2 budgets and class census |
| `tools/soi/blender_blockout.py` | headless Blender blockout + renders |

## 10. Phase 2 (started 3 Nov 2026) and handoff

User decisions received: (1) Rhynocs and Bianca come from Spyro 3 (YotD disc
`Spyro - Year of the Dragon (USA).zip`, project support in SPYRO3_FORMATS.md, `data-yotd/`); Grendor is
a new character (route below); both targets. (2) The PS1 mod is to be played in the user's existing
DuckStation-class emulator (path to come from the coordinator): hidden runs only, a private portable
profile, never the user's settings, never kill a process this agent did not start. (3) Scale is
calibrated from both games' jump code. (4) The first PS1 mod reuses one retail level's gameplay code.
Staging: `D:/Theia-staging/spyro2-mod/` (D: had 1.5 GB free on 3 Nov: one 565 MB image at a time).

### 10.1 Sprite banks: decoded, every family named (done)
`tools/soi/soi_sprites.py` (sheets in `data/soi/sprites/`, inspected):
- `0801584c(L)` loads 35 shared slots (0x00–0x22) and 5–7 per-level slots (0x23–0x29) through
  `08004e3c(src, size, slot)`. A bank is a 12-byte header (`flags, placement, codec (bits 2–4: raw /
  LZ77 / Huffman; bit 1 = 8bpp), "CRS", u16 frames, u16 tiles, u16 bits`), 5-byte frame records,
  `bits` presence bits per frame, then 4bpp tiles. Frame record: `u16 first tile, u8 palette<<4 |
  OAM shape<<2 | size, s8 x, s8 y`. The presence bits form a grid 2/4/8 tiles wide (by `bits`),
  row-major, bit 0 first; each set bit consumes the next tile. **Checked on all 125 banks: the
  presence bits add up exactly to the stored tile count.**
- `0800540c` draws objects: slot = `(u16 obj+8 & 0xfff) >> 6`, frame = `u16 obj+4`.
- OBJ palettes: `08015134(L)` (banks 0–7, 14, 15 shared; 8–13 per level).
- Family → slot, traced from each spawner and the callbacks it installs (`slot << 6` constants):
  `tools/soi/soi_families.py` → `data/soi/families.json`; every object in `level.json` now carries
  `family`, `family_name`, `s2` and `s2_placeholder`.

| Types | Family | Sprite slots | Spyro 2 stand-in (scene, class) |
| --- | --- | --- | --- |
| 117–120 | gems 1/2/5/10 | 0x10 | Glimmer gem 0x001 |
| 126–129 | treasure chest (1 per level) | 0x13 break + 0x10 gems | basket 0x0C8 (placeholder) |
| 130–134, 135–139 | gem containers drawn into the background (10 + 5 per map, homes too) | gems only | basket 0x0C8 (placeholder) |
| 10, 11, 16, 245 | level-themed critters that drop gems (fodder) | per-level 0x23 + gems | Summer Forest sheep 0x12C (placeholder) |
| 40–81 | ice barriers, one unique save id each (1 per level, 6–7 per home) | 0x16 ice block | none yet |
| 90 | warp / vortex (swirl 0x14, urn 0x1f), 0–4 per map | 0x14, 0x1e–0x20 | unconfirmed |
| 140–244 in fives | enemies: first family per level on slot 0x25, second on 0x26 | 0x25 / 0x26 | Rhynocs → Glimmer club lizard 0x22B / small lizard 0x1F0 (placeholders until the YotD Rhynocs load) |
| 115, 116 | Grendor | arena 0x25 | new character |
| 1, 15, 21, 0, 2, 3 | Spyro, Sparx, loader system objects | 0x05/0x06/0x18/0x19 | native |
Moneybags appears in the homeworld slot 0x27 (spawned by the per-home init code, not the list).
Enemy looks per level are in `soi_families.ENEMY_LOOK` (e.g. Market Mesa: 180s spear guards,
185s green-robed Rhynocs).

### 10.2 Market Mesa on PC (first build loads)
**Status 3 Nov:** `tools/soi/s2world_export.py` writes `data/soi/pc/market-mesa.s2world` (+ `.json`
summary): 11,662 triangles (2,083 top quads, 3,748 wall quads; budget median 12,146 triangles),
7 retail materials (Shady Oasis 47 tiles 1, 2, 4, 9, 12, 25; Glimmer 17 tile 0), 150 actors (110
gems, 15 baskets for the containers, 21 lizard stand-ins for the two Rhynoc families, 3 sheep
imported from Summer Forest for the critters, the Return Home arch), the ice barrier skipped (no
stand-in). A hidden run with a private copy of the exe (`build-soi/spyro_workbench.exe`, copied from
`build/Release`, not rebuilt) loads it: `data/soi/pc/shots/smoke-start.png` (inspected: Spyro and
Sparx on the plaza, the arch, gems, a lizard, terraces textured by surface type).

Scale calibration (provisional, geometry-based): Spyro 2's held normal-jump peak is 1.2807 world
units (retail launch 0xdc0 Q6 and held window, MISC_MOVEMENT_AND_LEVELS); one SoI terrace (24 z) is
mapped to 1.0 unit (V = 1/24), so one terrace is jumpable and two are not, as SoI's level design
assumes; H = V·√6 = 0.102. The SoI-side jump peak from its own player code (`08018ad0` and its
callbacks) is still to be read to confirm this; the blockout `.blend` still uses the older 0.15.

Remaining PC proof (next agent): a scripted hidden run (the challenge pilot in
`tools/challenge_pilot.hpp` or a `--replay`-style input script) walking the terraces, jumping a
gap, collecting a gem and meeting an enemy; and an orthographic capture at SoI's angle beside the
ROM map (the `soi_iso_camera` maths in `blender_blockout.py`). No runtime change has been needed so
far; any hook goes in new files, built into `build-soi/`, listed in the CHANGELOG.
Route without runtime changes: an `S2WORLD 4` file (`src/custom_level.cpp`), launched with
`--custom-level`. Format notes from the loader: bank 17 (Glimmer) gives gem 0x001, basket 0x0C8,
club lizard 0x22B, small lizard 0x1F0 and Return Home 0x1AA natively; `IMPORTS` adds classes from any
retail scene 15–73 (sheep 0x12C, vase 0x0C9 from 15); each actor names a real donor record of its
class; materials are `slot tile water` (any retail scene's 64×64 tile) or authored `.rgba`;
triangles carry flags (1 visual, 2 collision, 4 water); trailing sections are optional.
Plan: heightfield mesh from the solids (greedy-merged tops + wall quads, target ≤ 6,600 faces),
surface type per top/wall sampled from the SoI map colour at its projected point (paving, roof
tile, plaster wall, rock, grass), mapped to retail tiles (Shady Oasis, slot 47, is the closest
retail look: export its atlas with `spyro_inspect <disc> --level 47 --obj <file>`), objects from
`level.json` via `families.json`, Return Home arch near the start. Hidden runs: copy the main exe
into `build-soi/` first (never lock or rebuild `build/Release`).

### 10.3 Market Mesa as a PS1 WAD (planned)
Slot choice: **Glimmer (scene slot 17, overlay 16)**: its overlay already runs every class the PC
placeholder set uses (gem, basket, both lizards, Return Home), it is reached from Summer Forest at
the start of a new game, and its overlay is one of the smallest. Risk to check first: Glimmer's
scripted bridge (Moneybags, animation group 1) expects terrain animation data; drop that record or
keep an empty group. Build with `ps1disc.py` (TOC, 2048 alignment, the 3,116-sector gap).

### 10.4 Spyro 3 characters and Grendor (planned)
- Rhynocs / Bianca: decode their YotD model banks with the provisional Spyro 3 readers, map them into
  the PC object path; for PS1, convert to Spyro 2's animated moby format (ANIMATED_OBJECTS.md)
  within the scene's VRAM and model-bank budget, proven against `80070018` / `8004753c` loaders.
- Grendor route proposal (for approval): an original low-poly boss authored in Blender in Spyro 2's
  style (vertex colours + palette textures, a few hundred faces, vertex-animation clips), designed
  from the user's concept brief rather than traced from the GBA sprite; exported to the PC object
  path and to the S2 animated moby format; behaviour from an S2 boss script (Crush or Gulp).

### 10.5 Handoff state (context limit reached, 3 Nov 2026)
Done: phase 1; sprite banks decoded and every family named (§10.1); re-extraction with families;
the Market Mesa S2WORLD export and a first hidden PC load (§10.2); a provisional jump-based scale.
Next, in order:
1. PC proof shots (§10.2 "Remaining PC proof"); rebuild the blockout `.blend` at H = 0.102.
2. SoI jump peak from `08018ad0` to confirm the scale.
3. PS1: Market Mesa into Glimmer's slot 17 (§10.3): build the scene children with the project's
   decoders as the format oracle, rebuild WAD.WAD and the image with `ps1disc.py` into
   `D:/Theia-staging/spyro2-mod/` (D: is nearly full: delete the previous image first), static
   checks (TOC vs `80011b70`/`80013810`, read back with the PC reader), then the user's emulator
   when the coordinator sends its path (private portable profile, hidden).
4. YotD Rhynocs and Bianca for both targets (§10.4), then swap the lizard and basket stand-ins.
5. Grendor: route proposal in §10.4 awaits the user.

### 10.6 Agent 2 (3 Nov 2026): Market Mesa v2 to the retail bar, handoff

**User decisions received this session (binding):**
1. The PS1 version must be **played** in DuckStation (portable: `D:/Theia-tools/duckstation/duckstation-qt-x64-ReleaseLTCG.exe`,
   portable.txt present). BIOS: try the user's PS2 dumps in `C:/Users/Shadow/Documents/PCSX2/bios/ps2 bios usa/`
   (SCPH-39001 / 70012 / 77001 / 90001); **copy** one into DuckStation's own bios folder; never download one.
   Hidden runs only (no window; hide it as `Theia/tools/claude_guard/pcsx2_window_guard.py` does), audio on, never
   kill an emulator process this agent did not start. Not started yet.
2. Rhynocs and Bianca are imported from Spyro 3 (YotD) on both targets. Not started yet.
3. **Grendor is in the game, adapted from SoI like the levels** (not a stand-in): a reference sheet of every frame
   and direction of SoI types 115 / 116 (idle, walk, attacks, hurt, defeat), opened and reported; a hand-made
   Blender model (headless bpy, **no TRELLIS, no image-to-3D generator**) at Spyro 2 PS1 quality, master kept, three
   LODs with a shared pivot and scale; the PS1 version inside Spyro 2's boss moby budgets, proven against the S2
   model loader and renderer; animations matching SoI's frames; behaviour read from SoI's arena code (L15 / L16),
   ported or mapped to the closest S2 boss (say which). Order: after the Market Mesa PC proof and the PS1
   play-through, alongside the YotD imports. Report the reference sheet and model renders before putting him in game.
4. **Market Mesa must reach typical Spyro 2 PS1 level quality** (not a box blockout), inside the PS1 limits, with
   SoI's walkable layout and heights authoritative; textures mapped like retail (world-aligned floors, edge and trim
   tiles on terrace, cliff and building edges, walls running along walls, consistent texel density, varied floors,
   baked vertex lighting with corner AO); set dressing (trees, stalls, banners, crates); sky and distant scenery.
   Proof per iteration: a before / after sheet (ground, elevated, SoI angle beside the map), a sheet next to a
   retail level, a 360-degree gap hunt, face and texture counts against the budget; **send a sheet after each
   meaningful improvement**. User feedback on the first v2 shots: "not terrible, but textures look slightly
   janky/sub-optimally placed. Also need to keep working on the look/design of the level".

**Retail bar, measured** (`tools/soi/s2_terrain_metrics.py` -> `data/soi/ps1/terrain_metrics.json`, 23 scenes). The
reader `tools/soi/s2scene.py` decodes every region byte: a 28-byte header, the far mesh (verts*4 + colours*4 +
faces*8, `lp_words` words) and the near mesh (verts*4 + colours*8 + faces*16). Six scenes (21, 25, 27, 45, 67, 69)
have region records whose +0x17 byte is not the far-mesh size; they are not read yet. Medians: 5,311 near quads +
1,017 triangles = **11,776 render triangles** (max 14,929, Metro Speedway), 2,655 far faces, 69 materials, quad edge
1.87 world units; surface area by slope 35 % flat / 9 % gentle / 22 % steep / 31 % vertical; creases 35 % soft
(2-20 deg), 24 % bevel (20-60), 31 % hard; vertex-colour luma p5 / p50 / p95 about 40 / 90 / 170 (128 neutral).
Each near face maps one whole 64 x 64 tile (four 32 x 32 quadrant descriptors), so faces are tile-sized quads.

**Market Mesa v2** (`tools/soi/mesa_build.py` + `tools/soi/mesa_decor.py` -> `data/soi/pc/market-mesa.s2world`,
`.json` counts, `.mesh.npz` topology, final positions and per-face kind / material / UV frame for the PS1 encoder;
the box export is kept as `market-mesa-blockout.s2world` for the before / after sheets):
- Layout: SoI's solids sampled on an 8-unit lattice (cell centres; at most 4 units = 0.4 world units of shift; 3.7 %
  of the top area changes height, all at borders; thin 1-2 unit rail and post boxes are dropped). Tops are 16 x 16
  faces (1.63 world units) or 8 x 8 where the layout needs it.
- Watertight: integer shared vertices; every vertex lying on another face's edge is inserted (1,328) and those
  polygons are split by a zipper (quads where aligned) or a centre fan. The gap hunt shows no holes.
- Solids are intervals: islands from the cloud line (z 25) up, bridges as 12-z slabs; walls are each side's interval
  minus the neighbour's. Building walls (two cells around roof-coloured tops) are plaster down to the nearest open
  ground level, cliff below; cliff tops get a 12-z brick kerb row; risers are brick.
- Surfaces come from SoI's art projected top-down (`art_class`): plaza brick, roof tiles, roof lips, hatches,
  bridges; plaza-coloured cells inside roofs become roof. Decor from the map: 18 trees, 14 stalls (awning stripes),
  43 banners (wall top rows), 25 flagpoles (SoI's pole-sized solids, removed from the walk layout).
- Sculpt: cliff vertices get +-4 units of noise relief and a flare at the cloud line, top edges are rounded (cliffs
  drop 4 z and pull out 1.2), each island hangs a tapering rock underside from a distance transform (down to 150 z).
- Light: sun with heightfield shadows, sky ambient by normal, heightfield AO, dark undersides, per-material tints.
- Framing: Glimmer's own sky (bank 17, which is also the PS1 slot), a cloud sea below (visual only), nine distant mesas.
- Tiles (20 retail tiles): plaza 39:72 brick, risers 39:72 and kerb 39:73, roof 39:74, wall tops 39:76 (fringe),
  walls 15:28, windows 33:0, hatches 61:78, cliffs 47:21 (random quarter turns), undersides 17:14, bridges 15:68 with
  the 15:67 edge tile turned to the void, trees 33:18 and 33:2, stalls 39:44 and 47:59, banners 61:5, clouds 15:55.
  Atlases with tile numbers: `data/soi/retail-atlas/atlas-<slot>.png`.
- **Counts (v2c): 14,084 render triangles** = terrain 11,782 (tops 4,261, walls 2,097, cliffs 2,570, risers 659,
  bridge sides 276, undersides 1,919) + decor 1,654 + far scenery 648; 20 tiles. Above the retail median, below the
  max. For PS1: coarsen the undersides (1,919, mostly hidden); the far scenery belongs in the sky, not the terrain.

**Proof sheets (opened):** `data/soi/pc/shots/sheet-v2{a,b,c}-before-after.png` (ground, elevated, SoI angle beside
the original map), `sheet-v2{a,b,c}-vs-retail.png` (Glimmer and Shady Oasis from their start spots; Shady Oasis'
elevated spot looks into a wall), `sheet-v2c-gap-hunt.png` (8 headings x 4 spots, no holes). Tool:
`python tools/soi/soi_pc_proof.py sheets <tag>` (hidden runs of `build-soi/spyro_workbench.exe`, a copy; S2SHOT
cameras; `--sequence` frames go to the session scratchpad); `views` renders named views (`v2-*.png`).

**Honest state vs retail:** coherent retail textures, closed islands, trims and framing, but still rectilinear: a
flat plaza with one brick tile, box buildings, pyramid trees, flatter lighting than Glimmer. Next iteration, in order:
1. Texture placement (user feedback): plaza variation (2-3 brick or paving variants, rotations where the tile
   allows), edge tiles on plaza borders and terrace lips (find a paving tile with a border, or use a PS1 descriptor
   sub-rectangle of a retail tile), wall u running continuously around corners and mirrored per facing, wall texel
   density (rows are 48 z = 2.0 world units against 1.63 wide: 23 % vertical stretch), a lighter roof-edge trim.
2. Shapes: bevelled building corners and roof parapets (visual-only rim), fillets on convex plaza corners, stairs
   where the art shows them (the plaza's left side), arches under bridges and rails, rounder trees, crates, doors.
3. Lighting: softer gradients and stronger corner AO; compare luma percentiles with the retail 40 / 90 / 170.
4. Then the PC proof run (below), the PS1 encoder, the disc and the DuckStation play-through.

**PC proof run (started, not finished):** `soi_pc_proof.py` `probe` / `shoot` with route "mesa" (spawn on the 481
terrace, the Rhynoc stand-in at (690, 71, 480), the staircase 481 -> 457 -> 433 -> 409 with a gem on each step, the
16-unit gap from the 433 step over the 385 trench to the 433 block). A probe takes 5 s and reports the player
position, gems and health. The route was tuned on the blockout only: re-probe it on v2, then add the jump and flame
actions at the probed ticks.

**Not started:** the SoI jump peak from `08018ad0` (scale check); Glimmer's bridge / terrain-animation dependency
check; the PS1 scene encoder (near and far meshes per region, collision with its spatial index, a VRAM repack of the
20 tiles into Glimmer's page, objects); DuckStation set-up and play-through; the YotD Rhynocs and Bianca; Grendor.

### 10.7 Agent 2, iterations v2d / v2e (coordinator review of v2c), handoff

Coordinator priorities after v2c, in order: (1) palette and material fidelity to SoI (pale pink / lavender paving,
white plaster, pale-blue roofs, sandstone cliffs, snow / ice fringes under every island instead of a cloud sea);
(2) paving with border tiles, inset panels and a variation set, one tile per face; (3) real building forms:
bevelled corners, parapets, roofs with ridges and overhangs, framed windows and doors, pink banners and awnings,
fix the 23 % wall stretch; (4) rounder trees, stalls, crates (reuse retail tree / prop mobys where one fits);
(5) soft gradients, stronger corner darkening, a warmer key light; (6) fix the Shady Oasis comparison spot. After
each iteration send the before / after, the retail comparison and the SoI-angle sheet. Keep cutting hidden
undersides. When the look is right: the PC proof run, then the PS1 build in Glimmer's slot played hidden in
DuckStation (`D:/Theia-tools/duckstation/`, portable; the BIOS is already a copy of the user's dump in its bios folder).

**Done in v2d / v2e** (`mesa_build.py`, `mesa_decor.py`; sheets `data/soi/pc/shots/sheet-v2{d,e}-*.png`, opened):
- Palette: paving 15:68 / 33:7 flagstones tinted pink (1.32, 1.0, 1.1) with the 15:67 border tile turned to every
  drop (edge rotation order +x, -y, -x, +y; the earlier bridge order was wrong and is fixed); quarter turns and two
  variants inside. Walls 15:28 tinted white-lavender, roofs 39:74 tinted pale blue (1.5, 1.6, 1.85), hatches 33:83
  pale wood, risers 47:4 pink, cliff-top kerb 57:26 (white band), pole flags 47:16 tinted pink.
- Snow: the cliff band up to the cloud line + 16 z and the underside's outer ring use Winter Tundra snow 57:4, with
  a 10-unit drift flare at the cloud line; the cloud sea is gone (distant mesas stay, with snow caps).
- Doors 15:70 and arched windows 33:0 on building walls; convex building corners bevelled 2.4 units inward.
- Wall rows every 40 z (one tile = 16 units = 39.2 z): about 2 % stretch instead of 23 %.
- Light: warm key (1.06, 1.0, 0.88), cooler sky fill, AO weight 0.75, walls darken toward their base.
- Trees: two rounded six-sided tiers; one or two crates per stall; undersides 64 x 64 deep inside.
- Shady Oasis' comparison spot looks back from its start (yaw + pi); its start is an enclosed courtyard, so pick a
  hand-placed spot next time.
- **Counts (v2e): 15,004 render triangles** including about 430 of distant mesas (sky material on PS1); terrain +
  props about 14,570, against the retail max of 14,929 and median 11,776; 22 tiles. Undersides are still about
  1,700 triangles, mostly in the fans where block sizes meet: the next cut.

**Judgement:** the SoI-angle view now reads like SoI's map (pink paving, white walls, pale-blue roofs, snow rims).
Still below retail in form: box buildings without eaves or parapets, flat plaza levels, simple props.

**Next, in order:**
1. Eaves / overhangs on roof edges (double-sided sloped strip; at convex corners extend one strip and add a fill
   triangle so no notch shows), a visual-only parapet rim, ridged roofs only where they are not walkable.
2. Plaza stairs where SoI draws them (left side), bridge rails and arches, rounder stall awnings.
3. Undersides: one ring of 16-cells at the outline, then 64-blocks with zipper transitions (fewer fans); target
   terrain + props <= 13,500 so detail fits.
4. Retail-like gradients: compare the vertex-colour luma percentiles with the retail 40 / 90 / 170.
5. Then the PC proof run (route "mesa", re-probe on the new geometry), and the PS1 build and DuckStation play-through.

### 10.8 Agent 3 (3 Nov 2026): v2f look pass, PC proof run, Glimmer dependency check

**v2f** (coordinator review of v2e; sheets `data/soi/pc/shots/sheet-v2f-{before-after,vs-retail,soi-angle,gap-hunt}.png`,
props `props-v2f.png`, all opened):
- **Authored tiles** (`tools/soi/mesa_textures.py`, 20 tiles in `data/soi/pc/textures/`, S2WORLD 4 slot -2): no retail tile
  matched SoI's dusky sandstone cobbles, slate roofs or mint pines, so they are painted after SoI's map art (colours
  sampled from `map.png`): cobbles (2 variants) and a cobble-with-puffy-snow foot, slate courses, pale-cyan coping
  (side / top), plaster / window / door, framed wooden hatches, pine fronds, bark, pink cloth, pole banner, orange
  wall banner with purple tassels, stall stripes, wood, far rock. Retail tiles kept: plaza 15:68 / 33:7 / 15:67,
  bridges 15:68 / 15:67 / 15:52, counter 39:44, crate 61:78.
- **Cliffs and risers**: 48-z rows aligned to the terraces (25 + 48k): no slivers, 2,870 -> 2,043 cliff triangles.
  Snow only in the foot row (texture + a 14-unit drift flare + bright base vertices). Building walls over a drop with
  no open ground within 13 cells are plaster for at most 120 z, cobble below (they used to run to the cloud line).
- **Undersides**: `island_caps` triangulates each island's cliff-bottom vertices (shared, so the island stays closed)
  with a coarse interior grid (Delaunay, parity-classified across the outline; closure check: 0 open outline edges,
  0 open interior edges). 1,725 -> 555 triangles. Bridges keep their flat slab bottoms.
- **Buildings**: a mitred coping wedge on every roof edge (`merge_runs` + `profile_strips`: overhang + parapet, convex /
  concave mitres, capped free ends, visual only), orange banners hung under the coping, doors only on the floor in
  front of a wall. Bridges: parapets on the void edges (collision), quarter arches under the bridge ends.
- **Props modelled in Blender** (`tools/soi/mesa_props_blender.py`, headless bmesh; master
  `data/soi/pilot/mesa_props.blend`, export `data/soi/pc/mesa_props.json`): pagoda pine big / small (68 / 43
  triangles), stall (44), pink cloth rack (46, 5 placed from SoI's magenta cloth), flagpole (17), crate (10).
- **Counts (v2f): 13,797 render triangles** = terrain + props 13,437 + distant scenery 360; 26 tiles. Undersides 555,
  copings 674, rails 324, racks 230.
- **Judgement**: at SoI's angle the town reads like the map (blue roofs, white walls with pale copings and orange
  banners, pink racks, salmon plaza, mint pines, stalls). Still weaker than SoI: the snow rim is thinner, cliffs a
  little darker, hatch patches larger (SoI's are separate small panels). Against retail: textures are busier and the
  forms still rectilinear (SoI's layout); lighting flatter than Glimmer.
- **Not done from the review**: stairs (SoI's plaza staircase on the left is 24-z steps; real stairs would change the
  walk layout: decide), ridged roofs (SoI's roofs are flat and walkable; only the slate courses suggest ridges).

**PC proof run** (`soi_pc_proof.py shoot mesa --seq`, hidden, private exe copy; sheet
`data/soi/pc/shots/proof-v2f-route.png`, report `proof-v2f-route.json`): spawn on the 481 terrace, two flames defeat
the Rhynoc stand-in (hit at f40, knocked away f50, gone f62), down the terraces collecting the 10-, 5- and 1-gems
(counter 0 -> 16, health 3), a tap jump across the 16-unit trench onto the 433 block (airborne f153-f210), a held
jump up the 24-z riser onto the 409 step. Notes: the shot controller has no auto-jump; a jump pressed while pinned
against a riser rises only ~10 z (pressing it 10 frames earlier clears the step); the game camera clipped into a
plaster wall at f124.

**Glimmer slot (17) dependency check** (for the PS1 build):
- Terrain animation lives in the gameplay child (child 3) at +0x2C: seven length-prefixed sections `{u32 size, u32
  count, count x u32 offset, records}`. Retail `8001c734` walks them by size and guards each relocation loop with
  `0 < count`; the timeline `800225f8` loops pointer-to-`base + count`. **Empty sections (size 8, count 0) are safe.**
- Glimmer's group 1 (the bridge: near-vertex tracks of 66 + 19 source vertices by region index, a 76-triangle
  collision track) would write into Market Mesa's regions and collision if left in: **empty all seven sections**.
- The only callers of `8002365c` / `80023710` in overlay 16 are inside Moneybags' handler (class 0x220,
  `8007ded0`): at level start when the purchase halfword `8006467e` is set (node 0x3A) and in the purchase sequence
  (`8007e0ac`). **Drop Moneybags' record** so nothing indexes the empty group directory.
- The scene's 28-byte moving-region records before the visible-region directory (`8006fa98`, `8003c310`): write none.

**PS1 encoding constraints found** (from `s2scene.py` / `terrain.cpp`): a near face maps one whole material (four
32 x 32 quadrant descriptors, 2-bit rotation, quad or triangle); materials <= 128 per scene. v2f's 8-unit half-tile
cells, partial wall rows, half-tile prop panels and the caps' planar UVs need sub-rectangle materials (descriptors
covering a part of a tile) or re-authoring; plan about 60-90 materials (retail median 69, max 94).

**Next, in order:** (1) a scene-child round trip for slot 17 (decode every section -> re-encode byte-exact, as
`ps1disc.py` did for the disc); (2) the Market Mesa encoder: regions (near + far meshes, 11/11/10-bit packing per
region origin), materials and descriptors with the tiles packed into Glimmer's VRAM page, collision (12-byte
triangles + its grid), gameplay child (spawn, empty animation sections, gem / enemy records without Moneybags);
(3) the disc via `ps1disc.py` into `D:/Theia-staging/spyro2-mod/`; (4) DuckStation hidden play-through; (5) YotD
Rhynocs / Bianca; (6) Grendor.

### 10.9 Agent 3, continued (4 Nov 2026): v2g polish, the step-jump finding, PS1 codecs; handoff

**Coordinator decisions (binding):** keep SoI's 24-z jump steps (no stairs) and flat walkable roofs (slate rows
only). Polish before / alongside the PS1 work; the step-jump note to be checked against retail.

**v2g** (sheets `data/soi/pc/shots/sheet-v2g-{before-after,vs-retail,soi-angle,gap-hunt}.png`, before = v2e, which
is kept as `data/soi/pc/market-mesa-v2e.s2world`): a 24-z snow row at every cliff base (cliff rows 25-49, then
49 + 48k, still on the terraces; risers stay on 25 + 48k; the snow row is lit near-white, its vertex colours keyed
apart from the cobble row above), cliffs ~8 % lighter, `separate_hatches` turns each hatch blob into one or more
16 x 16 framed panels a block apart (the rest of the blob takes roof or paving): 807 -> 215 hatch triangles.
**14,435 render triangles** (360 distant scenery): above the 13,500 soft target, under the retail max 14,929.

**Step jump: Market Mesa's geometry, not the engine.** In the PC build on retail Glimmer a jump pressed while pinned
against a 1.1-unit ledge (collision wall 2962, 10.25 -> 11.34, near the start) rises 1.6 units and lands on top. At
Market Mesa's 409 step it rose 9 z: the convex-corner bevel (agent 2, v2d) pulled the corner vertex in, and the
T-junction fan joined it across a 14-unit wall span into a tilted triangle (normal ~0.54 vertical) that the sphere
resolver treats as a ceiling. The bevel is now 0 in `sculpt()`; the same pinned jump clears the step and the proof
route still passes (16 gems, health 3). Not an engine issue, so nothing for the challenges agent. Not yet checked in
the retail game itself (DuckStation); a 1-unit ledge hop is ordinary retail play.

**PS1, done (all byte-exact against every retail scene 15-73, 30 / 30):**
- `tools/soi/s2collision.py`: collision chunk decode / encode. Header (9 words): count, special count, second-array
  count, tree, lists, triangles, special bytes, second bytes (4-byte aligned), optional trailing block (+0x20;
  slots 25, 39, 47, 71 only). Grid (8003ce38 / 8003dc64): a three-level u16 tree indexed by z >> 12, y >> 12,
  x >> 12 (4096 retail units = 4 world units per cell); tables laid out L1, every L2 (z order), every L3, 4-byte pad;
  lists in traversal order, unshared, first entry | 0x8000, closed by 0x8000 + pad. Retail's cell *assignment*
  is not reproduced (plain bounds: 223 / 993 Glimmer cells equal; exact triangle-box test: 787 / 993); new levels use
  `cells_conservative` (bounds + 256-unit margin, a superset, lists high to low as retail). The queries guard both
  byte-array counts, so a new level may set them to 0.
- `tools/soi/s2regions.py`: region chunk decode / encode (directory, moving-region records, records rebuilt from
  their header and six arrays). Glimmer: 119 records, contiguous, directory order, no padding, no moving records.
- `tools/soi/s2scene_build.py`: the whole scene child split into its sections and rebuilt with regions and
  collision re-encoded (not copied). Run each module directly for its 30-scene check.

**PS1, next (in order), with the open questions:**
1. Region header semantics for new regions: +0..+3 (two u16), +4 flags (0x1000 = fine vertical scale, 0x4000 = not
   drawn), +6, +8 y0 / +10 x0 / +14 z0 (origin / 16), +12, +0x18 (0xffffffff in Glimmer). Read the loader
   `8006fa98`, the culling and the near / far renderers (`80024664`, `80028be4`, `80024a00`, `80024c60`) in
   `data/analysis/main/decompiled/`. Also HP face bytes 8-11, 14, 15, the second half of each 8-byte HP colour, the
   LP (far) face and colour records.
2. Part 2 (region visibility): find whether size 8 (disabled) is safe in the retail code; otherwise build it.
3. Materials (part 0, 6 x 8-byte descriptors each: two far, four 32 x 32 near quadrants) and the VRAM in child 0:
   pack Market Mesa's tiles into Glimmer's terrain texture areas (62 materials' space), sub-rectangle materials for
   half-tile faces (<= 128 materials).
4. Gameplay child (child 3): spawn at +0, header words +0x10..+0x28, the seven animation sections at +0x2C (write
   each as size 8, count 0), then the object list (Glimmer: from 0x3160) to decode before writing Market Mesa's
   gems / enemies without Moneybags (class 0x220).
5. Market Mesa encoder from `data/soi/pc/market-mesa.mesh.npz` (+ decor), regions of <= 32 world units (11 / 11 / 10
   bit packing per origin), collision from the walkable triangles.
6. Disc: rebuild WAD.WAD with the new slot 17 via `ps1disc.py` into `D:/Theia-staging/spyro2-mod/` (grow into the
   3,116-sector gap), static checks, then read it back with the PC reader.
7. DuckStation (portable, BIOS already copied): hidden, unmuted, private profile. Input for a hidden play-through has
   no route yet (no movie playback in DuckStation): try its GDB stub (memory and pad state) or key messages posted to
   its hidden window; record which works.
8. YotD Rhynocs / Bianca; Grendor.

**§10.9 addendum (agent 3, 4 Nov): region header fields and DuckStation driving.**
- Region header (28 bytes), fitted on all of Glimmer's 119 regions (and checked on slots 15 and 47): u16 +0 = bounding
  centre y / 16, +2 = centre x / 16, +6 = centre z / 16 (all within ~15 of the box centre of the near + far
  vertices: a fitted sphere, not the box centre); +4 = radius / 16 in its low 12 bits (~17 mean error against the
  far mesh's box half-diagonal) with flags above (0x1000 fine vertical scale, 0x2000 seen in slot 47, 0x4000 not
  drawn); +8 / +10 / +14 = origin y / x / z (/ 16); +12 = unknown, 1..8,126, uncorrelated with radius, counts, centre or
  index; +0x18 = 0xffffffff in every record of slots 15, 17, 47.
- Near colours are two RGBX words per entry (usually equal; the second differs on some). Near-face bytes 8-11 are not a
  packed normal (10/10/10, 11/11/10, 10/11/11, 8/8/8 all correlate < 0.2 with the face normal). Far faces are 8 bytes
  of packed indices (not decoded). None of these are documented in the project, and the renderers named by the
  decoders (`80024664`, `80028be4`, `80024a00`, `80024c60`) are inside functions Ghidra has not split
  (`ExportSelected` reports no function there; `8006fa98` lies outside the imported main.bin). Next: disassemble
  those ranges (`tools/ghidra/DumpMipsRanges.java`) and read how +12, the face bytes 8-11 and the far records are used.
- DuckStation (`D:/Theia-tools/duckstation/`): **no PINE server in this build** (no "PINE" string); it has a **GDB
  server** (`gdb_server.cpp`; settings `EnableGDBServer`, `GDBServerPort`), and `-batch`, `-nogui`, `-fastboot`,
  `-settings`. There is no `bios` folder and no settings file yet (it has never run here). Theia's
  `tools/claude_guard/pcsx2_window_guard.py` takes its process names, mutex and log from `THEIA_GUARD_NAMES`,
  `THEIA_GUARD_MUTEX` and `THEIA_GUARD_LOG`, so a second instance can hide DuckStation's `-batch` windows.

### 10.10 Agent 3 handoff (context limit, 4 Nov 2026): DuckStation driving, state of the PS1 work

**DuckStation, hidden** (`tools/soi/ds_harness.py`, portable copy `D:/Theia-tools/duckstation/`):
- This build (0.1-12442) has **no PINE server**, no `-settings` option and no command-line screenshot. It has a **GDB
  server** (`[Debug] EnableGDBServer = true`, `GDBServerPort = 2345` in the portable `settings.ini`, which is the
  private profile; `[Main] SetupWizardIncomplete = false` skips the first-run wizard; `[AutoUpdater] CheckAtStartup =
  false`; audio left on). BIOS: a **copy** of the user's SCPH-39001 dump in `D:/Theia-tools/duckstation/bios/`
  (DuckStation accepts the PS2 image: "PS2, SCPH-39001 (v5.0)").
- Qt's offscreen platform cannot host a Vulkan swap chain (VK_ERROR_SURFACE_LOST_KHR). **Starting DuckStation on a
  separate Win32 desktop** (`CreateDesktopW` + `STARTUPINFO.lpDesktop`, SW_HIDE) works: real windows that never
  reach the user's desktop. The **first** such run booted retail Spyro 2 (Vulkan device, "GDB server is now
  listening", client connected, RAM read over GDB).
- **Open problem:** every later run stops at a Qt dialog titled **"Error"** (class `Qt6120QWindowIcon`) on the hidden
  desktop before the log is written. Not the cause: the desktop name (a fresh one per run), the Vulkan shader cache
  (removed with safe_delete), the update check (disabled). Next: read the dialog's text (UI Automation from a helper
  started on that desktop, or `GetWindowText` on its children), and diff the portable folder against its state
  before the first run (DuckStation rewrote `settings.ini` with `[UI] MainWindow*` and created `cache/`,
  `memcards/`, `gamesettings/` and others; the first run was ended with TerminateProcess).
- Input route (TEST FIXTURE, not yet run): break at `8001271c` (the retail controller logic) each frame and write
  the 16-byte VBlank pad samples at `0x8006a99c + 16k` (held, pressed, released, analog bytes). Button bits, from
  `8001271c`'s own masks: L2 1, R2 2, L1 4, R1 8, Triangle 0x10, Circle 0x20, Cross 0x40, Square 0x80, Select 0x100,
  Start 0x800, Up 0x1000, Right 0x2000, Down 0x4000, Left 0x8000. Spyro's position: `0x80069ff0`.
- Screenshots: only the "Save Screenshot" hotkey / menu action exists; `probe()` posts F10 to the hidden windows
  (untested, the boot failed). The fallback is a save state (it carries VRAM) decoded offline.

**PS1 encoder state:** collision and region chunks and the whole scene child round-trip byte-exact (§10.9). Region
header fields mostly identified (§10.9 addendum). Still unknown before a new level can be encoded: region header
+12, near-face bytes 8-11, the far-face records (renderers inside functions Ghidra has not split: disassemble
around `80024664` / `80024a00` / `80028be4` with `tools/ghidra/DumpMipsRanges.java`), part 2 (visibility), the
material descriptors and VRAM placement, the gameplay object list. A low-risk alternative for the first boot: keep
Glimmer's far meshes and unknown face bytes as copied templates and only move geometry, then fix what DuckStation
shows wrong.

**Files this agent added:** `tools/soi/{mesa_textures,mesa_props_blender,s2collision,s2regions,s2scene_build,
ds_harness}.py`, `data/soi/pc/textures/`, `data/soi/pc/mesa_props.json`, `data/soi/pilot/mesa_props.blend`,
`data/soi/pc/market-mesa-v2e.s2world`, sheets `data/soi/pc/shots/{sheet-v2f-*,sheet-v2g-*,props-v2f,proof-v2f-route*}`.
On D:, `D:/Theia-tools/duckstation/` gained `bios/`, `settings.ini` and DuckStation's own folders (`cache`,
`memcards`, ...). Nothing in `src/`, `build/Release` or the main exe was touched.

### 10.11 Agent 4 (4 Nov 2026): PAUSED by the coordinator (user's order)
- **Done:** `ds_harness.py diag` + `ds_uia_dump.ps1` read any dialog on the hidden desktop through UI Automation (windows shown on that desktop only); agent 3's "Error" dialog did not recur in six hidden retail boots (two in a row, GDB read, two concurrent), cause unknown.
- **In progress:** the frame capture for the two-boots proof (F10 hotkey or a save state's VRAM), then the pad fixture at `8001271c`.
- **Next:** pad input on retail Glimmer with shots, then the Market Mesa quick-route encode into Glimmer's slot (§10.10).

### 10.12 Agent 4, resumed (4 Nov 2026): DuckStation proven, the PS1 formats read from the renderer

**DuckStation (hidden, separate desktop), proven on retail:**
- Boots twice with frames: PASS, `data/soi/ps1/ds/logs/boot2-20261104-014344.log` line `PROOF boot-twice PASS (2/2)`.
  DuckStation's GDB server **pauses the emulator when a client attaches**; the harness resumes it (`$c`). Frames:
  the private profile binds `[Hotkeys] Screenshot = Keyboard/F10`, posted to the hidden display window; a frame is
  only presented while the emulator runs freely (not while stepping at a breakpoint).
- Pad input: PASS (TEST FIXTURE), `data/soi/ps1/ds/logs/pad-glimmer-20261104-020157.log` lines `WALK PASS`,
  `JUMP PASS`, `PROOF pad PASS`; sheet `data/soi/ps1/ds/shots/pad-glimmer-sheet.png`. **A new game starts in
  Glimmer**, so the mod in Glimmer's slot is reached by pad input alone: title START, memory-card prompt CROSS,
  START / START / CROSS through the opening cutscenes (levels 29, 30), Glimmer = level 1 (`0x80066f54`), game state
  `0x800681c8` = 0 (11 title, 6 cutscene, 1 talk). Menus need the press reported on every tick.

**Retail facts read from SCUS_944.25 and overlay slot 1 (capstone, `tools/soi/mipsdis.py`):**
- Region header **+12 = 1 + the near faces of the regions before it** (checked exact on slots 15 / 17 / 47): each near
  face owns one byte of a per-frame buffer at `[0x80067034] - 0x3000 + 1 ...`, and the near region list starts at
  +0x2000, so a level has **at most 8,191 near faces** (retail max 8,160).
- Near-face **bytes 8-15 hold four 13-bit neighbour face indices** (into that buffer; 0 = none), read by the close-up
  subdivision path (`80025d0c`, `800262b4`): quads' edges (v3,v2) (v2,v1) (v1,v0) (v0,v3), with slot 2 split as w2 bits
  0-5 + w3 bits 12-18; triangles keep slot 2's high 7 bits in byte 4 >> 1 (the unused colour index) and their UV
  quarter turn in w3 bits 12-13. Checked on Glimmer: 23,018 of 24,163 non-zero slots share the expected edge. w3 bits
  0-6 material, 7-9 depth bias (ordering table), 10 flips the cull sign, 11 two-sided.
- Near colours are **two arrays**: A (light, 128 = 1.0) then B (a flat colour, about the tile average x light), used
  as the face fades from textured to flat with distance (`80024c84`).
- Far faces (`80028e2c`): w0 = a<<24 | b<<17 | c<<10 | d<<3 (7-bit vertex indices, c == d = triangle, bit 31
  two-sided), w1 = colour indices a<<25 | b<<18 | c<<11 | d<<4 | 4-bit depth bias; untextured POLY_G4 (command 0x38).
- Region classification (`80023c9c`): near when distance - radius < 0x7c0 (GTE units), far beyond; header flags
  0x1000 fine vertical scale (separate list), 0x4000 no near draw, 0x8000 widens the far test, 0x2000 set at run time;
  **+0x18 = four animation-group bytes (0xff = none)**, written by `800231f4` from the gameplay sections.
- Visibility (scene part 2, loader `8006fb84`): size <= 8 turns it off (`800673e4` = 0): `80048d18` then marks every
  region visible (max 256 regions, scratchpad bytes) and the far draw distance drops from 0x2c000 to 0x18000.
  Part 3 size <= 8 also fine. Part 5 (water vertex tables, `80023778` / `80023994` / `80023a9c`) is empty in Glimmer.
- Winding: front faces have cross(B - A, C - A) . outward < 0 in retail coordinates for near (v3, v2, v0), far
  (a, b, c) and collision (p0, p1, p2) alike (Glimmer: ~85 % of flat faces point that way).
- Gameplay child (`8001c734`): the seven terrain-animation sections are walked by size and every loop is guarded by
  its count, so **count 0 with the size kept** empties them without moving any offset. Section 8 (284 B in Glimmer)
  is not relocated. The object table then holds 88-byte records followed by a pool of 0x70-byte slots.
- Glimmer's archive header lists the model bank's absolute archive offsets after byte 64: child 2 must not move,
  so the new scene child must fit Glimmer's 505,856 bytes.
- Glimmer's 62 terrain materials have exclusive VRAM (near 8 bpp 32 x 32 quadrants with a 256-entry CLUT each, far
  4 bpp 32 x 32 with a 16-entry CLUT); their union holds 54 blocks of 64 x 64 at 8 bpp.

**Encoder** `tools/soi/mesa_ps1.py` (in progress): Glimmer's archive as the template; Market Mesa's faces regrouped
one per tile frame (the PC build's T-junction splits merged back), materials = (tile, half / whole UV range,
orientation), tiles repacked into Glimmer's terrain VRAM, regions of <= 127 vertices / faces / colours with the far
mesh a flat-coloured copy of the near mesh, collision from the PC build's colliding faces, visibility off, objects
moved, Moneybags dropped. `mesa_build.py` main() was split into `build_model()` (the PC export is byte-identical).

**The mod disc** (`tools/soi/mesa_disc.py`, `D:/Theia-staging/spyro2-mod/spyro2-soi-mesa.bin` + `.cue`, newest only):
the new slot 17 (1,972,224 B, 49,152 more than Glimmer's) goes after WAD.WAD's last slot at offset 176,914,432 (LBA
86,884). The 3,116 sectors before SPEECH.STR were empty form-2 sectors (subheader 00 00 20 00); the ones WAD.WAD now
covers become form-1 data sectors, the end-of-file submode 0x89 moves to the new last sector, and WAD.WAD's ISO size
is updated. 966 sectors differ from retail. The game reads WAD slots by LBA 500 + TOC offset, so nothing else moves.

**First boot (play run 1, `data/soi/ps1/ds/logs/mesa-play-20261104-131854.log`): Market Mesa loads in Glimmer's slot
and renders** (`ARRIVE PASS`; sheet `data/soi/ps1/ds/shots/mesa-play1-sheet.png`): Spyro and Sparx on the pink
paving, border tiles, hatches, cobble cliffs, gems, Glimmer's sky, camera turning; walking works. Found and fixed:
the retail tiles came from the labelled atlas PNGs (yellow tile numbers on the paving; now decoded from the disc);
far meshes smeared trees into green island tops (props are now left out of the far mesh); the jump after a shot
was lost (the fixture now re-arms at the breakpoint before the next press).

**User test image test01 and the PC build (4 Nov, user requests).**
- `D:/Theia-staging/spyro2-mod/Spyro2-SoI-MarketMesa-test01.bin` + `.cue` (newest only; no fixture in the image:
  the pad fixture and shots work over DuckStation's GDB server at play time). To keep TOC offsets increasing (the PC
  build's `parse_wad` refuses an offset below the previous slot's end: "Invalid WAD extent at slot 18"), slot 17 now
  grows in place and slots 18 onward move by 49,152 B; WAD.WAD grows by 24 sectors into the gap. 78,272 sectors
  differ from retail (the moved slots' sector headers and ECC); the xdelta patch is still ~0.5 MB.
- Reaching Market Mesa: a **new game** (title START; without a memory card "Start Game"); after the two opening
  cutscenes Spyro arrives in Glimmer's slot, which is Market Mesa.
- **PC build**: `spyro_workbench.exe --game <test cue> --slot 17 --play` loads Market Mesa from the disc through the
  PC's own retail loader (no code change needed once the offsets increase). Launcher:
  `Play Market Mesa (Season of Ice).cmd` (newest `Spyro2-SoI-MarketMesa-test*.cue`). Proof: `tools/soi/mesa_compare.py`
  renders the PS1 run's cameras in the PC build from the same disc (`build-soi` copy, byte-identical to the main exe).
- Patcher: `tools/soi/package_patcher.py` -> Theia `dist/spyro2-soi-marketmesa-<label>-<date>.zip`, with BIN/CUE copies
  of the house GUI (`tools/soi/patcher/main.cpp`, built in `build-soi/patcher-gui`) and backend
  (`tools/soi/patcher/Patch-Spyro2.ps1`: source .cue or .bin, writes the output .bin and its .cue).

**test01 freeze, found and fixed in test02 (4 Nov).** A DuckStation course froze ~150 ticks into the level, wherever
Spyro was (`test01-ds2/3/5/6/7` logs, `STALL` lines). The tick timer `0x8006700c` stopped; the CPU sat in the BIOS
exception handler (cause `0xc000001c`: data bus error in a branch delay slot); the kernel's saved context gave
EPC `0x80028d64`, the far renderer's vertex loop, with the store address register at `0x1f800400`, the end of the
scratchpad. The loop (`80028c2c`, software-pipelined, exit test `t7 == s0`) never ends for a region with **0 far
vertices**. Retail never puts such a region on the far list: **header +4 flag 0x2000 = no far mesh** (checked: all
500 retail regions without a far mesh carry 0x2000; the classifier `80023ddc` skips the far test for it). test01 had
6 decor-only regions with empty far meshes and no flag. The encoder now sets 0x2000 on them, and
`mesa_ps1_check.py` enforces the retail rule (it fails on test01's archive). Also in test02: Market Mesa's own fog
colour (scene part 5 +0x10, the GTE far colour that `80023778` loads; Glimmer's khaki `c0 c0 80` turned distant
islands green) = (240, 176, 236), the horizon's pink.

**State at the end of this pass (agent 4).** Current user image **test02** (`Spyro2-SoI-MarketMesa-test02.bin/.cue`,
`2e0ceecf…71f5fb`), patcher `Theia/dist/spyro2-soi-marketmesa-test02-20261104.zip`, PC launcher `Play Market Mesa
(Season of Ice).cmd`. Proven: boots and plays in DuckStation for a two-minute course without freezing (terraces,
step jump, gems), and in the PC build from the same disc. Open, in order: (1) distant islands on the PS1 draw as flat
grey-green blocks (far meshes / far textures: not the fog, not B; next: dump VRAM from a DuckStation save state and
compare the far texture and CLUT areas, and render the LP meshes offline); (2) region visibility (part 2) for the
longer draw distance; (3) gem values and SoI's containers / ice barrier; (4) the YotD Rhynocs and Bianca; (5) Grendor.


### 10.13 Market Mesa test05 release review (5 Nov 2026)

Current clean image: `D:/Theia-staging/spyro2-mod/Spyro2-SoI-MarketMesa-test05.bin` and matching CUE. BIN SHA-256
`b114be656add36c57601a0078aab0e6505cb108320b1fd7e2b7ecf42c031e309`; CUE `2c31e56d63fb1e27d05e9532f42b4ff385d855ed7e02700de234d8c8245985d7`.
Slot-17 archive `D:/Theia-staging/spyro2-mod/work/sol-retry-20261105/test05/slot17-mesa.bin`, SHA-256 `83c6803e67de80e2a35f5f02f61d4c0078c4001fdff9dabf611578dd7b335ed6`.
Patcher: `C:/Users/Shadow/Documents/Theia/dist/spyro2-soi-marketmesa-test05-20261105.zip`, 2,386,859 bytes, SHA-256
`bd4452ef7e57a6c08aec60493fb56ecc3272ca4b26840a83d4fe11bb1d3ef048`. Launcher is pinned to test05 and retains the optional CUE argument.
Disc readback and untouched other WAD slots: `data/soi/ps1/disc-test05.log:27` (image/archive identities at lines 4/6).
Package GUI/script exact-output checks: `data/soi/ps1/patcher-test05.log:10`, `data/soi/ps1/patcher-test05.log:13`; package identity: `data/soi/ps1/patcher-test05.log:17`.

**Distant rendering and visibility inherited from the reviewed test04 implementation.**
Far terrain contains exactly 5,246 selected near terrain faces, preserving their positions, winding and material B
colours; decor stays near-only. Removed spatial averaging and material-blind colour sharing. There are 7,364 near
faces, 142 regions, 62 materials/30 texture slots, and 10,320 collision triangles. Per-region far words peak at 254,
within the original 255-word limit. Full 32x32 texture descriptors and all 16 depth CLUT rows now follow the original
consumer layout. Static evidence: `data/soi/ps1/static-test05.log:26` (identity line 1; far equality line 17, limits line 13).
The original visibility consumer accepts the conservative all-visible tree at 266 tested region/cell positions
(static log line 15); the real loader enables distance 0x2c000 (`data/soi/ps1/ds/logs/sol-test05-review-20261105-151120.log:12`).
Retail comparison: `data/soi/ps1/oracle-test05.log:15` (candidate identity line 1); unchanged near polygons and no displaced far vertices
at line 5. Runtime original far-record selection and POLY_G4 emission: `data/soi/ps1/ds/logs/sol-test05-review-20261105-151120.log:17`, `data/soi/ps1/ds/logs/sol-test05-review-20261105-151120.log:19`.

**Moneybags safety correction (the only gameplay-byte change from final test04).**
Parking class 0x220 was unsafe: original Glimmer `8007ded0` checks timer `80067008 < 10` and purchase halfword
`8006467e != 0` before its state switch, calling terrain-animation group 1 at `8007df00/8007df10` even far from Spyro.
The seven animation sections remain empty, so distance is insufficient. Keep Moneybags at original index 9/type 0x220,
but set object +0x48 to **0xfd**, the original initial-table removed state written by `80041384/8004138c`.
`80040ef0/80040ef8` skips negative states and continues past 0xfd; only 0xff ends the table. Glimmer's dispatcher
`8007043c/80070444/80070448` also skips states >=0x80 before any class handler. Never put 0xff inside the table.
The loader loop and original helpers `80041438`, `80041524`, `8003ee3c`, `80041ab8` were inspected; 80041ab8 only
stores object +0x1c. Original byte signatures and disassembly: `data/soi/ps1/moneybags-oracle-test05.log:5` (oracle hashes at lines 2/3).
Static enforcement of state 0xfd and no internal sentinel: `data/soi/ps1/static-test05.log:20`.
All 201 original indices/types and parameter pointers remain; 19 appended gems give 220 records with separate
24-byte parameters. All 110 SoI loose gems use their source denominations: 56x1 + 37x2 + 12x5 + 5x10 = 240
(static lines 19, 21-25; runtime `data/soi/ps1/ds/logs/sol-test05-review-20261105-151120.log:13`).

The real loader was stopped at `8001d2d0`: Moneybags was still 0xfd, index 219 had been initialized, and the terminal
0xff followed record 220 (`data/soi/ps1/ds/logs/sol-test05-review-20261105-151120.log:7`, full runtime records retained). The purchased flag was then set in private
RAM **before the first actor update**, with timer zero. A further 12 finite ticks reset that timer to zero each tick;
the breakpoint at `8007ded0` was never reached and the exact record stayed 0xfd (`data/soi/ps1/ds/logs/sol-test05-review-20261105-151120.log:15`).
The same private run completed 1,800 further logic ticks (`data/soi/ps1/ds/logs/sol-test05-review-20261105-151120.log:26`); the final record remained inactive
(`data/soi/ps1/ds/logs/sol-test05-review-20261105-151120.log:27`); complete run: `data/soi/ps1/ds/logs/sol-test05-review-20261105-151120.log:30`. This covers the original purchased-bridge trigger with a
RAM fixture, not every possible old-save quest combination or a complete saved-game playthrough.
An initial attempt checked volatile +0x4d after gameplay changed it and stopped on a harness assertion; its retained
`data/soi/ps1/runtime-test05-console.log` is not release evidence. The corrected check observes the actual loader boundary.

**Restored and protected baselines.** The earlier cleanup incorrectly removed explicitly preserved test02/test03.
test02 was rebuilt from local `work/slot17-mesa-test02.bin` (archive SHA-256
`b7150b290b88ff15a9efe555f12261bced9e77a64f6f872606d5ab7640e4df8b`); test03 was extracted from the retained local RAR.
Exact restored identities: `data/soi/ps1/baselines-test05.log:30`, `data/soi/ps1/baselines-test05.log:31`:

| Baseline | BIN SHA-256 | CUE SHA-256 |
|---|---|---|
| test02 | `2e0ceecf28bfe64b65dabe3bade12c707c89f7f25c93a7d37968a75e8171f5fb` | `478e39bf4639a6750953970c56759e621fccb9791fa9f0aad9f3f0fc983a1da4` |
| test03 WIP | `60680946e3be8fe3546c4173909ae8a31b54e9316935a0a3e105496d7e68308c` | `1074a4ebc2b4d475c027e204a7b1ca01ea499019aef52a60d591b6678f39aba5` |

The pinned CUEs use CRLF bytes; they were restored to the pinned hashes. Both recovery snapshot directories and
the test03 RAR remain. test01 stays absent. The old test02 patcher ZIP remains retired; it was not restored.
`retire_mesa.py` now requires explicit paths, rejects test02/test03 (including patchers), validates the newest
image/package receipt, and retains in-use groups using Win32 sharing guards. Only `safe_delete.py` performs deletion.
Superseded test04 BIN/CUE/ZIP cleanup is recorded in `data/soi/ps1/retirement-test05.log`.

**Release integrity and remaining limits.** Patcher verification reads slot 17 from the exact target BIN, regenerates
hash-bound static evidence, requires completed matching runtime checks, and embeds the original-consumer and PC logs.
No stale PASS text is accepted. It was packaged with `--no-retire` and both extracted GUI/script paths rebuilt the
exact final image before cleanup. Preservation/baseline/package invariants: `data/soi/ps1/final-integrity-test05.log:13`; its line 2 proves
test05 differs from final test04's archive by only byte 0x1e9de4 (0 -> 0xfd).
PC disc-load evidence is `data/soi/ps1/pc-test05.log:5`: four automatic cameras on a separate hidden desktop, 110 gems/220 objects,
10,320 collision triangles and zero script faults. It uses a task-private copy of `build-soi/spyro_workbench.exe`
SHA-256 `5055ed71f26bee91fd4d6b56c14ecc27d3a8596ef53e21b4146a9c1b855fbf8d`, not a rebuilt executable.
The current main executable, retail executable and v2g source mesh/world hashes are unchanged (integrity log lines 3-7).
No new character imports, downloads, commit, push, manual gameplay/UI testing, or operation of a user-owned running app.

PS1 LP terrain is inherently untextured vertex-colour POLY_G4, so a flat-looking distant surface is expected.
Decor remains absent at far range; separate background mesas are still omitted. CLUT layout is corrected, but the fade
formula is a **fit**, not exact recovered generation: 98.8995% of measured RGB5 channels match on rows 2..14 across
443 retail materials, maximum channel error 14 (`data/soi/ps1/oracle-test05.log:14`). 641 partial-rectangle faces still
fall back to whole tiles. Containers retain retail basket contents; 14 of 21 source enemy positions have retail
lizard stand-ins. Ice barrier/critters, Bianca, Grendor and new Rhynoc models remain unfinished. Manual visual and
gameplay acceptance belongs to the user; automated captures do not establish it.

### 10.14 UV/content continuation, 5 Nov 2026 — unfinished, no replacement release

The current delivered image, patcher and launcher remain **test05** in 10.13. This continuation
worked solo, without another CLI/agent/model/generator, downloads, native executable changes,
manual application/game testing, or control of a user-owned process. No artifacts were retired.
The source snapshot and 626-file initial inventory are under
`D:/Theia-staging/spyro2-mod/work/soi-content-20261105/snapshot-20261105-155508/`.
Task outputs/logs below are relative to `D:/Theia-staging/spyro2-mod/work/soi-content-20261105/`.

**Latest experimental image, explicitly not release eligible:**

- BIN: `D:/Theia-staging/spyro2-mod/Spyro2-SoI-MarketMesa-uv06-reserved-wip.bin`, SHA-256
  `b66da4ddb2055c951eab68517e38ce1e997d292d5bf998920bc039bdb654aee5`.
- Slot 17: `uv/candidate-reserved/slot17-mesa.bin`, SHA-256
  `576b6225a88a9b4456c6bd0b78f986805de578a22864874199e9d2dfe34dad9d`.
- Matching CUE, adjacent build report, clean-disc receipt `uv/disc-reserved-report.json`,
  and feature/evidence index `continuation.json`. No fixture is baked into the disc.
- `uv/static-reserved.log:26`, `uv/disc-reserved.log:27`, original Moneybags consumer
  `uv/moby-reserved.log:5`, original engine `uv/runtime-reserved.log:32` and PC load/capture
  `uv/pc-reserved.log:5` all identify this exact archive/image. The engine's 1,800 ticks are
  at runtime line 28, purchased-flag fixture at line 15, 24 original near packets at line 17,
  12 far-record stops at line 19 and six far POLY_G4 packets at line 21. This is automated
  coverage; it is not a manual route/interaction or old-save playthrough.

**UV changes and honest accuracy:** opt in with `mesa_ps1.py build --uv-polish --work TASKDIR`.
`mesa_uv.py` bakes measured source UV mappings into full 32x32 cells, uses original descriptor
orientation flags (table `80062040`, consumer `8002654c..800265a4`), deduplicates cells and uses
per-cell 4/8bpp palettes. No undersized descriptors or executable/overlay patches were introduced.
The source contains 1,011 distinct four-corner templates after current winding normalization;
the actual material ceiling is 128. The selected pack uses **123** materials and reports every
approximated face rather than relabelling zero whole-tile fallback as perfect mapping.

- Whole-tile fallback selection: 641 -> 0. Source-exact mappings: **5,561/7,364**; **1,803**
  mappings remain approximate. The old 2,229 corner-category errors and new source-distance
  metric are different measurements and must not be subtracted as if equivalent.
- Every **2,041 top face** preserves its source UV corners. Independent serialized polygon,
  texture and UV binding is `uv/coverage-final.log:13`; complete shared edges at line 14;
  302 split edges / **1,412** continuous atomic joins (998 repeat boundaries), zero new
  coordinate discontinuities at line 15. The all-material texel check executes original MIPS
  instructions and covers 503,808 near texels, including all orientation codes and both depths
  (`uv/coverage-final.log:9`). This does not prove arbitrary non-floor mappings are exact.
- Colour quantization is a real tradeoff: shared 16-colour floor mappings keep the colour
  transform consistent across charts. The final pack has 374 unique near cells (8 at 8bpp,
  366 at 4bpp) and 123 far cells. Full-material source RGB5 RMS is **0.723004**, maximum channel
  error **26** (`uv/coverage-final.log:10`). Do not call the images exact source colour copies.
- Terrain positions/heights/winding remain fixed: 7,364 near faces and 5,246 far terrain faces.
  Colour sharing retains identical quantized A/B pairs within a material, with no spatial
  averaging; near colour entries fell from 22,696 to 9,811. There are 136 regions, and the
  static checker enforces original buffers/counts and exact selected-near/far B-colour equality.
  The archive is 1,968,128 bytes. `uv/coverage-final.log:6` proves every serialized near
  position/winding/material/rotation/A/B byte and every far position/winding/colour byte
  matches the pre-compaction UV candidate. Region boundaries/duplicate vertex counts can differ.

**VRAM accounting:** `mesa_vram.py` parses original static poses, animated near/far streams and
frame patches, shared/scene Spyro geometry, reserved resident/font pages and flame. It reserves
the complete pause/guidebook upload destination, which overlaps 24,576 original terrain words.
The original terrain union is 174,592 halfwords; 150,016 remain permitted, of which this pack
allocates 142,592. Palettes prefer the original palette footprints and the partial 32-row band.
All unclassified space is reserved. The JSON/NPZ/map at `data/soi/ps1/uv06-review/vram-ownership.*`
include the ownership and overlaps. **Exhaustive symbolic analysis of every global/overlay
consumer and all UI lifetimes is still incomplete**; do not claim this conservative map is that proof.

**Failed experiments retained:** uncompact archive `3e11da87e5dec051e15b82791eef458a564821dc6d96e8339d8e50505a2c5345`
/ BIN `de0360bfe5403bf3d07e7a02792877e2e2964d72c67bebcf2eb1fe575e544f17` reached the loader,
near and far renderer, then timed out. `uv/runtime-trace.log:15` records the stopped CPU in BIOS
and line 16 its control registers; the adjacent task-private failure RAM is retained. The short test05
control succeeded. Exact colour-pair compaction was followed by successful bounded runs on the
compact and reserved candidates. This establishes successful smaller inputs; the precise original
failure mechanism is not yet proven. `uv/build-reserved-attempt1.log` also retains the initial
palette-fragmentation allocation failure. Neither failure log is release evidence.

**Automated captures for user review:**
`data/soi/ps1/uv06-review/uv06-before-after.png` and `uv06-vs-retail.png`, metadata `sheets.json`.
These include ground, overhead, downward, oblique and reverse views from private PC copies;
retail Glimmer is shown from its own spawn. They are not manual visual acceptance. The previously
reported uncompact sheet was superseded by captures of the reserved candidate. Original v2g
source mesh/world and installed executables remain untouched.

**Content source preparation, not implementation:** `soi_content_sources.py` exports all original
frames and original Ghidra C/ROM-literal/THUMB evidence under `data/soi/content-20261105/`.
`source-manifest.json` retains all 21 enemy placements (including index 141), 15 background
containers, three critters and the barrier. Factory/recreated-gem paths read from original code
select 15 container gems and 26 enemy gems via `081ab8f0`; actual death/collection/state integration
still needs checking. The neighboring fodder damage continuation
`08022958 -> 08022980 -> 080374e8` uses the health/butterfly path; its exact type-16 dispatch
binding remains unproven, and the old generic "drops gems" label is insufficient evidence.
Barrier type 41 selects original init branch `0801d5fa`; rescue/save/dialogue integration remains
unfinished and must not be replaced by a generic breakable shell.

`soi_yotd_sources.py` inventories 1,334 banks in original main scenes 97..169, bounds the clip
descriptors/frame tables, and records source hashes. Original S2's complete 500-byte Moby codebook
is byte-identical at YotD `80066274`; the 256-byte blend table matches at `80066530`
(`yotd-sources.log:3`). This is a source compatibility observation, not a completed model import.
Sunny FD has 14 clips / 207 near vertices / 87,020 bytes; EE has 11 clips / 229 near vertices /
102,844 bytes. Best source-matching donors and Bianca's identity/placement are not decided.

**All remaining requested work stays active:**

1. Finish original-consumer/UI/VRAM ownership and UV approximation review; preserve budget and
   source-coordinate/continuity checks if improving the remaining 1,803 mappings.
2. Implement the functional ice barrier, all three critters, all 21 enemy placements and correct
   container contents with source save/interaction behavior. Current delivered/candidate gameplay
   is still test05's 14 lizard stand-ins and retail container contents; no new actors were silently added.
3. Import actual animated YotD Rhynocs/Bianca through Blender and proven S2 model/CLUT/LOD/clip/sound
   integration on the same PS1 image and PC path, with no `src` changes. No static OBJ or raw bank
   census constitutes completion.
4. Author Grendor in Blender with master/three LODs and source animations/arena behavior. The
   exports retain **248 frames per original arena**, but semantic direction/state labels, model,
   animations and renders are unfinished. **The Grendor model-review checkpoint has not been reached**;
   no `ARTIFACT_READY` claim or in-game integration occurred.
5. Complete the content feature manifest, full regression checks, new clean-image packaging and
   exact extracted GUI/script reproduction before updating the launcher or retiring any eligible
   previous current artifact. `package_patcher.py` rejects `release_eligible: false` experiments
   (`uv/package-guard-reserved.log:2`). Protected test02/test03/RAR/recovery snapshots remain protected.

Default legacy packing was also rebuilt after refactoring and remained byte-exact test05
(`uv/legacy-regression.log:2`); this is refactor regression evidence, not a new delivery. Canonical
test05 report files were restored to their original bytes. Exact changed-file inventory and full
continuation handoff are `changed-files.json` and `handoff-full.txt` in the task folder.

### 10.15 Phase2c recovery: converted model banks and functional containers (private WIP)

The delivered launcher/package remains test05. This checkpoint continues the retained phase2b
work without repeating its sprite or family export. The 646-frame source manifest remains
`4f372fcb202e333b9f6d2339c2a2dce763337e2c459c6b43de0a5f13f1e96f5f`.
Type16's bound health/butterfly chain is retained separately in
`data/soi/content-20261105/source-type16-binding-phase2c.json`; the three critters are not installed.
Source type41/index148 is the return portal, not breakable ice. The parent's Grendor authoring
checkpoint and approved music/stone work remain separate and were not overwritten.

The immutable private candidate is
`C:/Users/Shadow/Documents/Spyro 2_PC/build/soi-content-sol-phase2/disc-content04/Spyro2-SoI-MarketMesa-content04-assets-wip.bin`,
SHA-256 `3ffa5a1673c5f4a60767f7cb82ce557b2ed443b90b311f5c079141dc1c5b976c`.
Its slot17 archive is
`data/soi/content-20261105/phase2-containers/content04/slot17-content.bin`, SHA-256
`a39b76c1b726e5d2ea6a2ee80f4da337b5e510d08e6bc6bae9586b7e330d6bc0`.
**Slot16 must also be included**: generated `slot16-overlay.bin`, SHA-256
`c3e06f3b15d36fd557826f8b899767585be0e1187a79c6968058f6c9b1839534`.
The stock single-slot `mesa_disc.py` cannot preserve this extension when rebuilding from retail.
`soi_content_disc.py` assembles both slots privately and checks every other WAD slot byte-exact.
The candidate has no approved score applied and is not release eligible.

Local original YotD banks now converted into S2 classes are Bianca (Sunrise97/1A9 -> 2E0),
armoured spear Rhynoc (Charmed Ridge141/19A -> 2E1), and a provisional green-robed magic Rhynoc
(Bamboo123/5 -> 2E2). The last donor has a staff; the SoI reference has a fan. All 24 numeric clips,
300 frames, original near/far streams, face topology, palettes and frame metadata are retained.
Material references and inline sound IDs are relocated. Original loader/vertex execution and
253,210 face-rectangle texels are checked against source; native sound decode matches 104,748
signed16 samples across 16 references. This does not establish hardware SFX playback or AI.

The editable near-pose archive is
`data/soi/content-20261105/phase2-models/content04-editable/all-numeric-clips-editable-v2.blend`,
SHA-256 `6dd388c8b20c087bfc6622786a17d5405a8a1ef162393fbc03acc64e7cf832c3`.
It retains all 300 poses and unclipped source RGB multipliers. Its linear authoring preview is
not advertised as the game's interpolation/loop/event implementation. The original runtime
banks remain authoritative for both LODs and all metadata.

The three converted models emit actual textured packets in the original PS1 renderer on this
exact BIN, using temporary RAM presentation fixtures; native presentation also renders numeric
clip1/frame5. They are not placed as functioning source enemies/NPCs. The original 14 lizard
stand-ins remain. All21 Rhynoc placements, source AI/attack/death, the three critters, Bianca's
story/dialogue placement, and the source portal gate remain unfinished. No full character-runtime
or complete-content claim is made.

All15 source containers now hold value1. Five type136 containers append C9 records220..224 with
independent 28-byte parameters and the original common wobble curve; their old C8 records90/91/92/93/102
are 0xFD. Every original index/type/parameter pointer remains. A 52-byte slot16 extension at80081A64
retains the original C8 continuation and applies C9's 0x160000 hit mask. Normal flame is rejected;
charge breaks the box and returns a value1 gem. The PC uses its existing native C9 consumer.
Basket art and C9 heat/wobble are donor approximations. There are225 records and8128 bytes of dynamic
reserve; the original loader's terminal0xFF is checked after225 before dynamic allocation.

An earlier larger asset pack reached the loader but failed when GPU commands overlapped the
particle area. The retained remedy removes155648 bytes from three already-parked Glimmer-only
banks22A/242/22F, marks their12 existing records0xFD and retains small valid static banks for
loader safety. No visible SoI actor uses those compact banks. Moneybags index9 remains class220/0xFD.
No native executable or `src` file was modified. All prototypes/failure profiles are retained.

All new evidence below is under the absolute directory
`C:/Users/Shadow/Documents/Spyro 2_PC/build/soi-content-sol-phase2/` and was read after execution:

- BIN `3ffa5a1673c5f4a60767f7cb82ce557b2ed443b90b311f5c079141dc1c5b976c`:
  `content-model-gpu04-console.log:18` original model packets;
  `container-all15-content04.log:62` all15 named native break/releases;
  `container224-flame01.log:6` isolated native flame rejection;
  `container-original06-console.log:41` all5 original hit/release consumers plus600 ticks;
  `content-pc04.log:6` four native cameras,225 objects/110 loose gems/10320 collision triangles, no script faults.
  `content-render04-console.log:18` additionally checks1800 original logic ticks,24 actual near UV packets,
  12 valid far records and6 far POLY_G4 commands on the exact same image.
- Archive `a39b76c1b726e5d2ea6a2ee80f4da337b5e510d08e6bc6bae9586b7e330d6bc0`:
  `content-static04.log:29` structural/source guards (identity at line1);
  `content-uv04.log:17` exact near geometry/selected far colours/source UV continuity;
  `content-model-abi04.log:33` converted near/far bank checks;
  `content-sound04.log:21` native PCM equality (identity at line1).
- Blender `6dd388c8b20c087bfc6622786a17d5405a8a1ef162393fbc03acc64e7cf832c3`:
  `content-editable-check-v2.log:4`,300 poses/65462 pose vertices/topology/UV/RGB round trip; identity at line2.

The parent stone pack must reserve the7328 halfwords in
`phase2-models/converted03/model-vram-reservation.npz` before allocating terrain, then copy these
reserved words from the candidate VRAM. Its SHA-256 is
`f72383eaccf1483e7267528a2906f08160d9613e687a8b2eb6aba59289342487`.
Both slots16/17, the full SFX table/payload, model directory and225-record gameplay must survive
the combined build. The parent reapplies the approved score and runs exact combined release,
GUI/script/packaging gates. Launcher, packager and global disc report were not promoted.

### 10.16 Parent ROM targets and dedicated portal checkpoint (2026-11-05, private WIP)

The user requested two distinct deliverables: a physically stripped Mesa-only ROM
with direct boot, learned swimming/climbing/headbash and no ordinary exit; and an
expanded full-game ROM/patcher preserving Glimmer, adding a new Summer Forest
portal and allowing normal return travel. The standalone restrictions must not
apply to the expanded game. Test05 remains the delivered launcher/package; none
of the candidates below is a new release. New Codex invocations remain explicitly
`gpt-6.1-sol`/`xhigh`; no Astra worker was launched during this recovery.

Parent stone/content composition14 is
`build/soi-stone-sol-20261105/composed14/slot17-mesa.bin`, SHA-256
`d4825a6ce55a174e47df8a27a4c5c0d0560fcdbcdfee3f8beb73112998124c41`.
It retains content04's models, gameplay, SFX and nonterrain sections, repacking
terrain around its 7328 reserved VRAM words. The composition uses116 materials,
5274/7364 source-exact UV faces and2090 approximations; all2041 tops and90 selected
window/door faces remain exact. The new ROM prototypes still use this immutable
content04 composition, not the unfinished phase3 actor/quest candidates.

The first stripped disc (`compact01`, SHA-256
`cbc964b26f28ab13f91a1eeda068abeca5f1163804b6841e91ee8f830fa2fb19`)
retained WAD0/1/16/17 only. On that exact image the original CPU completed zero-input
boot, learned-flag2 checks, Exit(-1)/(0) rejection, original title-reset reentry and
600 later ticks. Evidence is
`D:/Theia-staging/spyro2-mod/work/soi-content-20261105/coordinator-standalone-compact01-runtime.log:14`.
Its relocated approved score also reaches the original CD end/reset/reseek/playback
events (`coordinator-standalone-compact01-music02.log:17`, same absolute directory).
Captured score identity and restarted intro correlation are recorded at
`coordinator-standalone-compact01-score.log:4` and `:5` on that same BIN.

Native startup checks exposed two independent dependencies. Workbench initially
decoded default Summer Forest before applying CLI `--slot`; the lead authorized
the narrow bootstrap fix in `src/desktop.cpp`. It now applies the explicit slot
before decoding the initial world and skips absent optional donor model banks
while retaining strict checks for a present donor. Original interface/guidebook
WAD4 must also be retained: original `8004de1c` reads its cached offset at
`800676f8`. The corrected stripped `compact02` contains only shared0/1/4 plus
Mesa16/17, with192 other WAD payloads and bundled demos physically absent. Shared
speech is deliberately retained to avoid silently losing active voice consumers.
It is308377776 bytes, SHA-256
`d7c1fad96ac735cfae94a9c1c52c0e7ddf7868dd2778953c3ba911115e9b618e`, at
`build/soi-standalone-sol-20261105/compact02/Spyro2-SoI-MarketMesa-standalone-compact02.bin`.
Its structure/whole-sector reencode evidence is
`coordinator-standalone-compact02.log:3`; its private PC load/four automated cameras
are `coordinator-standalone-compact02-cli-fix02-pc.log:5`.
The compiled main candidate SHA-256 is
`9a798a70060a14452a73acf117d72cc420b3f13c80124d83de7eba41d3ea5c8c`;
retail default startup on the same candidate is
`coordinator-retail-default-cli-fix02.log:3`, and explicit original Glimmer is
`coordinator-retail-glimmer-cli-fix02-pc.log:5`. These are private copied executable
runs. The user-owned running main image was left untouched; deployment is not
claimed. Native application ability/travel restrictions are not established by
the PS1 flags or these load/camera checks.

The extra scene is **14 decimal (`0x0e`)**, absent from both the original29 campaign
IDs and original `8006d960` callback cases. It uses new campaign progress ordinal29
and physical WAD198/199. Original lookup `80064940`, inverse IDs `80064994`, names
`800649b4`, homes `80064b84`, per-level music and GPU/DMA buffer selectors are
recorded in the generated loader receipt. The unchanged resident EXE is still
SHA-256 `7b54002789ab379e0d3a6b49d7b50078b9c9c2fed589f41ff9878ebb68fe0bc1`.
Loader1 installs the extra metadata and temporarily aliases cached entries72/73
to198/199 for Mesa, restoring their original final-disc offsets on other loads.
Original cinematic70 also uses streaming ordinal29; its original row29 metadata
is restored while that cinematic runs, then Mesa metadata is reinstalled on normal
campaign loads. The latest cinematic safeguard still needs exact-image CPU proof.

The new portal's closed editable Blender source is
`data/soi/summer-portal-20261105/author03/market-mesa-special-portal.blend`, SHA-256
`301d36c397fbbe02d9e161b55a773050468f2ec2900f0f767929e0ce03e64113`.
Summer Forest has8160 original near faces; the runtime arch/crown costs23, for8183
within the original13-bit face scratch limit. Its original160 terrain records,
original triangle coordinates, actors and model/VRAM payloads remain. Three new
regions, a portal collision surface, record7 and a class40A name controller are
appended. Original surface/actor indices remain stable. The latest encoder uses
the original7-bit far index bitfields and copies packed near positions to far.
The prior byte-index far prototype is superseded, not release evidence.

On private expanded candidate05, SHA-256
`26b5155ae44705bdf0aab55e7b01b8f80f58c0e9a05b7bfd9c16d4d3f0b02779`,
controller input moved Spyro from Summer's original spawn through the new portal
into scene0x0e/ordinal29. The original Return Home function then completed a return
to `(131700,17500,37736)`. Entry is actual controller/collision/portal flow after an
explicit original load-call fixture to Summer; return uses an explicitly named
original-call register fixture, not manual menu/gameplay acceptance. Evidence:
`coordinator-expanded-portal05-runtime.log:11` and `:14`. The first return failed
because unused `80064a34[29]` was0xff; original `80072aa4` then indexed an invalid
fly-in endpoint. The corrected loader gives Mesa a conditional endpoint0 alias,
substitutes the new approach only for Summer-from29, and restores original
`80064a54`/`80064b3c` endpoint bytes on all other loads.

Latest full private candidate08 with corrected far records, a Mesa-specific
Blender-derived panorama and appended `MESA.XA` is
`build/soi-expanded-sol-20261105/disc-extra08-music/Spyro2-SoI-MarketMesa-expanded-extra08-music.bin`,
636987456 bytes, SHA-256
`6059b3b4f9e59c20c883df237c1315248ac16469ae03c375381dad3c8f77de51`.
The source-derived panorama retains its own editable Blender file and220-vertex/
380-triangle original-format artwork; it is a portal image, not replacement level
geometry. The approved XA is the original source-enhanced mix, SHA-256
`1a0dc5b27f7c04c553da1d40d6ae2072cb0ce2309e7d20a5d948340f61ba0a0e`.
The new extent is appended instead of replacing Glimmer's original music. Existing
retail file sectors are exact relative to the explicitly extended input except
the ISO PVD/root-directory records; evidence is
`coordinator-expanded-extra08-music-build.log:2`. On the exact candidate08,
`coordinator-expanded-extra08-static.log:3` verifies195 undeclared original WAD
payloads including Glimmer16/17; `:4` checks the unused scene ID, retained original
campaign indices, unchanged resident code and absence of standalone ability/exit
writes; `:5` checks new near/far fields and the8183-face budget; `:6` is the static
receipt. These are structural checks, not completed runtime/release gates.

Candidate08 portal/return/cinematic and original CD music checks are running in
private copied DuckStation profiles. Phase3 actor/critters/quest work remains a
separate active Sol worker and is not yet promoted. Full native extra-scene travel,
progress/guidebook and music mapping still encounter the host's original29-level/
slot15..71 bounds; only the narrow CLI bootstrap source fix is authorized so far.
Final integration, original/native exact-image checks, standalone pause/game-over/
ability behavior, final clean BIN/CUE promotion, launcher, feature manifest and
GUI/script patcher hash proof remain. No new patcher has been shipped, no original
retail/source image was modified, and no manual application/gameplay test is claimed.

### 10.17 Sol resumption: bounded standalone, added-level patcher and CD transport correction (2026-11-05)

The lead's installed shadow/jar executable is SHA-256
`3ae3a4e0c33f7769e18c25bcf740d2719a9de3ab2cbdac2a456f167dac9b36e7`.
The user explicitly confirmed that it includes the narrow desktop bootstrap fix
and is installed/packaged. Mesa has made no subsequent `src` edit, main build or
main-executable replacement. Further desktop work stays private pending lead
coordination. The native extra-scene repro below used an immutable copied3ae
executable, not the user's running game. The Sol actor worker was resumed after
all prior worker/emulator PIDs disappeared; `run-phase3r.log` verifies the actual
`gpt-6.1-sol` / `xhigh` header. No other model was used.

**Original CD transport bug found before promotion.** Candidate08's complete
near-GPU check failed; its earlier static/native-decoder and portal/audio results
must not be called complete PS1 rendering validation. The unaligned content04
code bank was84020 bytes, loaded at8006d264 and ending80081a98. The first scene
texture descriptor was80081aa0. Captured original RAM showed1988 descriptor bytes
zero through80082263, with the first surviving byte at80082264: exactly the end
of the code bank's rounded physical CD transfer. Original loader70498 places the
scene after the declared code size (`8006774c + ordinal*16`); resident13918 rounds
the transfer to complete2048-byte sectors; resident1d658 case8 loads the overlay
late, after scene descriptors. All197 original WAD lengths are sector aligned.
The generated raw C9 extension was not. Evidence and arithmetic are retained in
`data/soi/rom-targets-20261105/original-overlay-sector-overrun.json` and
`D:/Theia-staging/spyro2-mod/work/soi-content-20261105/coordinator-expanded-renderer08-runtime.log`.
No UV or terrain-geometry guess was used to conceal this failure.

The standalone/full builders now append zero padding to generated code banks and
declare the aligned sizes. Original code addresses stay fixed. The private
emulator harness refuses unaligned WAD lengths before launching, and current
renderer gates require every loaded texture descriptor to equal the source bytes
both after loading and after the finite camera/GPU/gameplay run. The patcher
requires those exact-image checks for new releases. Candidate10 renderer/portal
checks are still running at this checkpoint; neither08 nor09 is a release.

**Latest compact prototype06** is
`build/soi-standalone-sol-20261105/compact06/Spyro2-SoI-MarketMesa-standalone-compact06.bin`,
308377776 bytes, SHA-256
`88935e3ad321bc39a429eb38bef4f6412c38766f954a22cedf546e50bd88f9e1`.
Only WAD0/1/shared-interface4 and Mesa16/17 are nonempty;192 other payloads and the
demos are physically absent, with only SYSTEM.CNF/SCUS/WAD/SPEECH ISO files.
Shared speech is retained for original voice consumers. The resident executable
on disc is original. Structure/whole raw-sector EDC/ECC and approved XA readback
are logged at
`D:/Theia-staging/spyro2-mod/work/soi-content-20261105/coordinator-standalone-compact06-build.log:3` and`:4`.

On that exact compact06, automated original-CPU evidence is in
`D:/Theia-staging/spyro2-mod/work/soi-content-20261105/coordinator-standalone-compact06-runtime.log`:
no-input boot`:8`, default swim/climb/headbash flags2`:9`, resident Exit gates`:10`,
pause Return Home resumes Mesa`:13`, original shared guidebook open/cancel`:15`,
physical jump/Triangle headbash reaching state2e`:16`, last-life death through
original35d38/32e74 and the standalone1a6f8 reset gate`:19`, removed-scene request
fixtures10/63/70 return only to Mesa`:21`/`:23`/`:25`, title-reset reentry`:27`,
600 further finite ticks`:28`, completed bootstrap check`:30`.
The pause test explicitly clears retail's tutorial Return Home lock in private
RAM after original pause initialization; the on-disc image contains no fixture.
The last-life and foreign-request probes are named RAM/register-call fixtures.
No manual gameplay or move execution in water/on ladders is claimed.

The standalone exit guard mirrors original pause's normal resume stores, rather
than leaving pause substate2 stuck. The original last-life Game Over screen73510
would wait for input and read stripped WAD6 artwork, so standalone alone routes
1a6f8 through its resident Mesa reset. The transition19364 detour forces Mesa
before cinematic branches and skips only the removed loading-art stage; original
asynchronous loading and level-entry consumers remain. The loader70498 gate also
forces the sole scene. These rules are not installed by the expanded loader.

**Latest expanded prototype10** is
`build/soi-expanded-sol-20261105/disc-extra10-music/Spyro2-SoI-MarketMesa-expanded-extra10-music.bin`,
636987456 bytes, SHA-256
`990da170f6dfb3edd67729ccaa09e0ad708cddb5558104668b3d6453c3f7735d`.
It uses aligned added code198 and Mesa archive199, separate scene14/ordinal29,
Summer portal7 and its new return position. Original Glimmer16/17 remain exact.
`D:/Theia-staging/spyro2-mod/work/soi-content-20261105/coordinator-expanded-extra10-static.log:3`
verifies195 undeclared original WAD payloads;`:4` verifies original29 campaign
IDs, original resident executable and no standalone ability/exit/title/game-over
writes;`:5` verifies all160 original Summer records plus23 new faces, total8183
within8191, and valid matching far records. The new Summer07 archive is
`06e609fbe0277cb979fd6c4634989fb4b2f9869a1c356d17572b2bf8a8ed0577`.
Its220-colour/380-triangle Mesa panorama now uses the exact stored Blender PNG
RGB values rather than applying a second transfer curve; all220 samples match
at `D:/Theia-staging/spyro2-mod/work/soi-content-20261105/coordinator-portal-art05-colour-check.log:1`.
Original CPU frame acceptance is separate from that byte check.

The approved XA remains
`1a0dc5b27f7c04c553da1d40d6ae2072cb0ce2309e7d20a5d948340f61ba0a0e`,
in a new MESA.XA file at LBA240379; original SPEECH/Glimmer music stays exact.
Candidate10's fresh presence proof is
`D:/Theia-staging/spyro2-mod/work/soi-content-20261105/coordinator-expanded-extra10-presence.log:2`.
On earlier exact candidate08, real controller portal entry/Return Home/original
Glimmer return completed at
`D:/Theia-staging/spyro2-mod/work/soi-content-20261105/coordinator-expanded-portal08r-runtime.log:14`/`:17`/`:18`.
Its original CD capture/end/reset/reseek completed in
`coordinator-expanded-extra08s-music-runtime.log`; waveform score and restarted
intro identity are at the absolute same task root's
`coordinator-expanded-extra08s-score02.log:4`/`:5` (correlations0.9687/0.9477).
These older audio/runtime proofs are not transferred to candidate10's hash.

`package_patcher.py` now supports the separate physical199 archive, fresh full
original-payload/portal checks, completed new-entry/Mesa-return/Glimmer-return/
cinematic evidence and expansion wording in GUI/README. The original xdelta,
ZIP extraction, hidden GUI and script roundtrip/hash checks remain. No new ZIP,
launcher promotion or retirement has happened. Release eligibility is still
false until final content and exact-artifact gates complete.

Native extra199 geometry loads under copied3ae, but its original script bridge
is inactive and audio/mapped enemies are zero. Exact repro:
`D:/Theia-staging/spyro2-mod/work/soi-content-20261105/coordinator-expanded-extra08-native-repro.log:4`;
concrete seams/scope are saved in
`data/soi/rom-targets-20261105/native-expanded-integration-repro.json`.
The generic load/capture marker is not proof of native expanded gameplay, music,
travel, independent progress or guidebook integration. The actor worker's final
immutable model/AI/critter/fan/bell/quest pair is not yet composed into either
prototype. Test02, test03 BIN/CUE/RAR, current user test05, original retail and
prior WIP remain preserved. Manual application/listening/gameplay acceptance
belongs to the user. No download, git push or user-game process operation occurred.


### 10.18 Combined stone/content variants and source return binding (Sol, 2026-11-05)

The actor worker selected immutable `data/soi/content-20261105/phase3-actors/final02`.
Its payloads equal final01: archive `838a90d38ae2ec540e62934d638a4100009468a52880ae6ccbb6989238c5ed92`,
code `be1e4a8851fd924c59fc39bd908395239ca65e01bed29f961cf2f384715909f6`,
7328-word model reservation `f72383eaccf1483e7267528a2906f08160d9613e687a8b2eb6aba59289342487`.
The selected manifest is `d55d17cabe4f58b0688972b1088a7f231844fb3303cfa48b018d711954c0cc65`.
Its copied immutable evidence records original CPU damage for both enemy families
and health2->3 feeding for all three critters, at `final02/evidence/actor-original-features16.log:12,15,16,17,18`;
line37 completes that bounded fixture run. All21 source enemy adapters and26 total
reward value are covered at `final02/evidence/actor-original16-console.log:76` on
its development image `35658649ae50a73bef2439a3bab67a31a3a93b99e509e3707596021528f531cd`.
These are explicit approaches/hit/health fixtures, not manual gameplay acceptance.
Bianca is an authored ambient greeter without fabricated source dialogue/reward.
The fans, rabbit and bells retain editable Blender sources; the quest uses the
source2/1/0/2/1/0 sequence and source-derived bell sounds. Source148 is a return
portal; the inspected source does not establish a separate breakable ice actor.

`mesa_stone_compose.py` now rebinds actor and container receipts through verified
ancestry, including exact existing container records/parameters, while preserving
all content models/gameplay/SPU/nonterrain sections and7328 reserved VRAM words.
Composition15 archive is `0091fd248e77c2b17fd4439d623a6c9553764a8a661faadb4344da768b2ef539`.
The unchanged terrain is composition14's116-material stone polish, including all
v2g positions/heights/winding/collision. Evidence is the absolute task root
`D:/Theia-staging/spyro2-mod/work/soi-content-20261105/coordinator-compose15.log:4`.

Private combined full11 is `44a21d205900a2441cbc93dc4f6beb23e4577a379bb0064592d59ad7da740eeb`;
private combined compact07 is `15e2f3af541cf2a2b9fb87c70d0f48bdafe2af60fdd96b5379948019dc447e39`.
Both retain the approved XA. Full11's195 undeclared original WAD payloads, original
Glimmer16/17, resident executable, original29 IDs, lack of standalone restrictions,
and8183 Summer faces are checked at the task root's `coordinator-expanded-extra11-static.log:3,4,5,6`.
Full11 renderer completion is `coordinator-expanded-renderer11-runtime.log:33`;
compact07 renderer completion is `coordinator-standalone-renderer07-runtime.log:31`.
The complete compact07 bootstrap/pause/guidebook/default flags/headbash/game-over/
removed-scene/title-restart checks are in `coordinator-standalone-compact07b-runtime.log:30`.
The renderer checks retain every loaded descriptor byte and actual original near/
far GPU packet evidence. The first compact06 renderer harness missed the loader
because menu-free boot finished before its observer was armed; a copied paused
emulator now arms the observer before any CPU execution. The original failure
log/profile is retained; `coordinator-standalone-renderer06b-runtime.log` is the
successful corrected-harness run, not a changed ROM.

Full11's actual original CD capture/end/reset/reseek run completed. Fresh waveform
identity and post-restart intro identity are at
`D:/Theia-staging/spyro2-mod/work/soi-content-20261105/coordinator-expanded-extra11-score.log:4,5`,
correlations0.9177899 and0.9565052. Lossless WAV is `952c00c889abcf8861d07512d105738a9de320b15211d6d51b1fa39465244692`.
These proofs apply to full11 only. They are not transferred to a later code/ROM hash.
Actual line excerpts/log hashes are saved in `data/soi/rom-targets-20261105/combined-content-verified.json`.

**Source portal travel is being bound separately.** Original SoI19410 emits the
player sphere with radius1000hex; original1d8a4 adds the return portal's1400hex;
16e10/011a4/0122c calculate three-dimensional squared distance and16dec compares
strictly against the radius squared. With the existing horizontal sqrt(6)/24 and
vertical1/24 coordinate transforms, the PS1 integer metric reduces exactly to
`dx*dx + dy*dy + 6*dz*dz <884736`. New owned `mesa_quest_return.py` hooks only the
already-completed quest portal, preserves the archive and all other actor/C9 code,
and invokes original80019928(-1). Full home metadata selects Summer; standalone's
resident exit guard keeps Mesa. Return overlay is `58a72ab7c69983ee812530fce53e60182b59a7fac042e81992207446293071c6`,44 sectors.
Its original source exports and MIPS listing live in `build/soi-quest-return-sol-20261105/return01`.
Full12 `4602bc855adce282ff521adfb087b9ecfb8bcb2c35d713aee530495aaadc01b2` and
compact08 `d28ff030bef8a7b63c29436db07e87e26ead289ba160bdfba6cc4b34a7ffd2a8`
are private new candidates requiring fresh exact-artifact gates. The first compact
contact fixture failed because its sideways calibration walk left the narrow
source terrace. The failed logs remain; the revised fixture resets motion through
original351d8(0)/2e2bc and avoids that unrelated sideways walk. No contact/travel
completion is claimed yet. Coordinate quantization and PS1 player-origin conventions
remain explicit port limitations.

**Native release blockers remain.** Copied3ae's added199 scripts/progress/music/
travel mapping is incomplete. The actor VM also assigns UINT32_MAX to uncollected
spawned gems, which valid GameSave rejects. Exact failure is
`data/soi/content-20261105/phase3-actors/final02/evidence/actor-native-sim-final16.log:24,25`,
with the same archive/code hashes above; the simulation completion marker does not
mean that save failure passed. A stable parent/release identity proposal and tests
are saved in `final02/native-pending-script-gem.json`. No native source edit, main
build/install/replacement or launcher promotion was made. Patcher release remains
ineligible. Test02, test03 BIN/CUE/RAR, good test05, originals and prior WIP remain.


### 10.19 Exact PS1 review milestone: full14 and standalone09 (Sol, 2026-11-05)

The latest bounded originals are full14, SHA-256
`c4d1e29ffea3384a1e618a43be547a7bb4b33dd891208a303b150b719d7397b2`,
`build/soi-expanded-sol-20261105/disc-extra14-music/Spyro2-SoI-MarketMesa-extra14-music.cue`,
and compact09, SHA-256
`2f1c3a1d611adaed715e1428ae38674d324faf13b50e28789e381ae69753222e`,
`build/soi-standalone-sol-20261105/compact09/Spyro2-SoI-MarketMesa-standalone-compact09.cue`.
Both are clean, fixture-free on disc and remain private review candidates. The
native PC promotion/release gate stays false. Good test05 and the default launcher
remain unchanged; no src edit or main build/install/replacement was made.

**Source return coordinate correction.** SoI19410's source position is clamped
directly to floor height. Original S2 stores Spyro's origin164hex above his feet:
resident1722c and1a2fc subtract/add that exact offset around persistence consumers.
The first return01 adapter's standing contact was therefore too high despite
using the correct radius. Return02 converts originZ to feetZ before comparing
`dx^2+dy^2+6*dz^2 <884736`, retaining source19410 radius1000hex plus source1d8a4
portal1400hex. Its overlay is `db321532a334fb368f4b772cd8ed70dfcdeb89e77b02cd427ac6b01c96db0108`,
44 complete CD sectors. All source/actor/C9 bytes except the eight-byte portal-open
trampoline are retained. Failed return01 fixtures and their artifacts remain.

At the absolute task root
`D:/Theia-staging/spyro2-mod/work/soi-content-20261105/`, compact09 closed/open/contact
and containment evidence is `coordinator-compact-source-return09-runtime.log:9,24,27,30,33`.
Its source bell state machine writes completion after2/1/0/2/1/0; no completion or
travel destination fixture is used. The approach/original idle initializer and
bell flame mailboxes are explicitly named RAM fixtures; final contact uses pad
input. Full14's current exact-image original actor feature run completes at
`coordinator-full14-features-runtime.log:39`, and physical contact from that
completed quest reaches the new Summer portal at`:44`, position131700/17500/37736.
The earlier commentary's line43 is the original Return Home call; line44 is the
final position confirmation. No original memory-card roundtrip is claimed.

Full14 retains195 undeclared original WAD payloads including Glimmer16/17, has no
standalone ability/exit restrictions and keeps the original resident executable;
`coordinator-expanded-extra14-static.log:3,4,5,6` is the fresh proof. Actual new
Summer controller crossing, new Mesa return and original Glimmer return are
`coordinator-expanded-portal14-runtime.log:15,18,19`. Exact loaded texture descriptors
remain unchanged and original near/far packets render at
`coordinator-expanded-renderer14-runtime.log:29,33`.

Compact09's192 other WAD payloads/demos are physically absent; shared0/1/interface4
and Mesa16/17 remain. Structure/whole raw-sector EDC/ECC and approved XA are checked
in `coordinator-standalone-compact09-build.log:3,4`. Its complete no-menu/default
learned flags/pause/guidebook/headbash/game-over/removed-scene/title-reset suite ends
at `coordinator-standalone-compact09-runtime.log:30`; exact renderer evidence ends
at `coordinator-standalone-renderer09-runtime.log:31`. Current original enemy-family
damage, all3 critter health2->3 feedings, bells/SPU/persistence/open visual and source141
physical flame/reward are `coordinator-compact09-features-runtime.log:37`.

The combined terrain archive remains `0091fd248e77c2b17fd4439d623a6c9553764a8a661faadb4344da768b2ef539`.
`coordinator-composed15-uv.log:15,16,18` checks1817 direct continuous joins and2685
atomic/T-junction/periodic joins, with0 new UV errors. This fresh run reads a
hash-verified clean test05 archive and preserves every near position/winding plus
all7364 near/5246 far colour/material/rotation attributes relative to composition14.
All2041 top faces keep exact source corners. Other approximated UVs and colour
quantization remain explicit limits. Original collision stays10320 triangles.

Summer08 (`4ec68f0d01c135eb01d2eaa1bd9e7d366fd7eb05e19d60c7b9ddf76ac88bd5a7`)
replaces the blurred20x11 Gouraud panorama with an editable Blender Delaunay flat
picture:380 vertices/734 triangles/359 colours, derived from the retained actual
Mesa render. Original Autumn artwork demonstrates440 vertices/767 triangles;
original296d0's fog palette has512 words before68bf0. Flat colour words use the
original high-bit flag. Exact160 original Summer regions/8183 total faces remain.
`panorama08/market-mesa-portal-panorama.blend` retains the unchanged source far mesh,
calibrated camera and separate editable picture. Automated settled portal frames
are saved by portal14; this best-effort artwork has no manual acceptance claim.
Failed derived mesh06/07 budget experiments and the earlier grid remain WIP.

Compact09's approved score is verified by actual original CD restart/capture and
`coordinator-standalone-compact09b-score.log:4,5` (correlations0.925086/0.981775).
The first shorter capture's restart identity failed and remains recorded; the
longer finite capture satisfies the unchanged >0.9 identity gate. Copied3ae also
decodes15345792 signed16 samples exactly and captures360 frames with music at
`coordinator-standalone-compact09-native-music.log:4,6`. This is audio evidence,
not native standalone profile/default-skill/travel acceptance.

The shared changelog now records lead shadow qualityR2. Mesa copied only its
immutable packaged executable from ZIP `86a9f5f24e760ec3b47cecadb95c7e3afc61751f51bbd4aceff3832c9e349418`,
member hash `c3aa993f399a34aa8530f820fc88fed53cc019e82bbff0ba78e30ed238e103cc`.
Fresh full14 repro `coordinator-expanded14-native-r2-repro.log:4` still shows266
geometry objects but VM inactive, mapped enemies0 and music0. The load/capture
marker is not native gameplay success. Concrete receipt
`data/soi/rom-targets-20261105/native-expanded14-r2-blocker.json` joins the retained
pending spawned-gem save repro. Lead native integration remains required.

At this checkpoint full14 CD end/reset/reseek was observed, but the first long
capture's synchronous three-second stop hotkey timed out before WAV finalization.
The failed log/profile are preserved. A corrected owned-window asynchronous hotkey
run14b is active; its final audio/packaging proof is not claimed here. An8,473,268-byte
private xdelta is encoded (`e8afaa8f8736bfaaafa9b36ce6ef38cdfcfa1941ba9ee1485dcc7be0f09d2b46`).
`mesa_private_patcher_review.py` prepares a private PS1-only review ZIP, explicitly
naming incomplete PC expansion in GUI/output/manifest; it cannot write dist or mark
release eligibility. It still requires completed exact-image original evidence,
full streamed decode and extracted GUI/script roundtrips. Production
`package_patcher.py` and its original/native release gate are unchanged by this
review path. No new public ZIP/launcher promotion or WIP retirement has occurred.


### 10.20 Selected private PS1 review artifacts and patcher reproduction complete (2026-11-05)

The selected clean ROMs remain full14 `c4d1e29ffea3384a1e618a43be547a7bb4b33dd891208a303b150b719d7397b2`
(636987456 bytes) and compact09 `2f1c3a1d611adaed715e1428ae38674d324faf13b50e28789e381ae69753222e`
(308413056 bytes). Their CUE identities are respectively
`f602333f79a2e983c9d767ac6d90833957b3a492ab62e5d8404f4d56f9811cfd`
and `35c05ed82567cfa9c765e95c71ea6412e842a8307c3bdfa9e8177d6ceaedfda2`.
Final paths, fixture-free status, current native limitations, log hashes/actual lines
and the selected private ZIP are in `data/soi/rom-targets-20261105/selected-PS1-review-roms.json`
and its PS1-REVIEW.md ROM-index entries. Source test05 was freshly read/hash checked
as b114be65...e309 for the geometry oracle; the existing default launcher is unchanged.

The corrected asynchronous capture completes on exact full14:
`D:/Theia-staging/spyro2-mod/work/soi-content-20261105/coordinator-expanded-extra14b-music-runtime.log:19,20`
records lossless PCM capture and original extent restart. WAV is
`1d450940a3a41b4996abdb964651c3915986ff4feee90ad4b2f030f1df31a507`;
original score and post-restart intro identity are
`D:/Theia-staging/spyro2-mod/work/soi-content-20261105/coordinator-expanded-extra14-score.log:4,5`,
correlations0.9542254/0.9606633. Failed earlier capture/UI timeout evidence remains.
No score, original sound bank, user mix approval or acceptance threshold was changed.

Selected **private PS1 review patcher14b**:
`D:\Theia-staging\spyro2-mod\work\soi-content-20261105\patcher-review14b\spyro2-soi-marketmesa-expanded-PS1-REVIEW-20261105.zip`,
10224258 bytes, SHA-256 `1672ab725f85db6c869d64bc2824a32dc2cff0e659c85e23ec6f3685dfb1da28`.
Stream decode, extracted GUI and script use the original pinned retail BIN. Actual
GUI and script exact-target/CUE confirmations are
`D:\Theia-staging\spyro2-mod\work\soi-content-20261105\coordinator-expanded14b-review-package.log:6` and`:9`; selected ZIP binding is`:11`.
`D:/Theia-staging/spyro2-mod/work/soi-content-20261105/coordinator-expanded14b-zip-member-check.log:1`
independently verifies all52 manifested member hashes. The generated success PNG
was read to check the review/platform labels. Review14 exposed a leftover backend
PC-play footer; selected14b uses the new manifest play_instructions text and was
repacked/retested with a new ZIP hash. Older private review14 is retained WIP.
The review GUI, output filename, README and feature manifest explicitly say PS1
review / PC expansion incomplete. It includes no source ROM, performs no native
main install or launcher change and is not a public dist/release promotion.

The normal production packager still refuses release-ineligible archives and
requires original/native music evidence. No gate was weakened to ship around the
native blocker. Current copied lead shadowR2 c3aa... full14 repro has VM inactive,
mapped enemies0/music0 at coordinator-expanded14-native-r2-repro.log:4. Stable
pending script-gem save ownership, native extra-scene/profile/progress/music/travel,
original PS1 memory-card roundtrip and manual gameplay/art acceptance remain
unfinished. A separately reversed breakable ice barrier / full source story is
not claimed. All changes stay in owned tools/soi, data/soi and Mesa doc/changelog
entries; no new src edit, main build/install, download, git commit/push or user-game
process operation occurred. No own emulator/worker remains running at this checkpoint.
