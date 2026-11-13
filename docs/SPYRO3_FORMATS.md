# Spyro 3 NTSC-U provisional format support

This survey concerns the supplied US Year of the Dragon disc, identified by
`SCUS_944.67` CRC32 `02c69f63`. Addresses are PS1 virtual addresses; archive
offsets are relative to their named extent. Evidence consists of bounded
inspection of the user's files, original MIPS instructions, native probes and
workbench captures. Game payloads are not embedded in source or this document.

## Identity and extraction

The single MODE2/2352 track contains an ISO 9660 filesystem. The new
`spyro_disc_extract` utility uses the project's existing `AssetStore` disc
reader, streams files into `data-yotd`, and retains original sectors for STR
sidecars. `data-yotd/disc-extents.tsv` records every extracted extent.

| File | LBA | Bytes | CRC32 | SHA-256 |
|---|---:|---:|---|---|
| Original BIN | — | 597,250,416 | — | `a313c9b1306a08e950edddc152280a443796e68052c8e0454b5d76d69354d157` |
| SCUS_944.67 | 24 | 380,928 | `02c69f63` | `cb819ee78c556d403779309859cb08a7111331f624759bc1bc380946261bb26e` |
| WAD.WAD | 500 | 166,295,552 | `f81ffa32` | `9ed8407f939d5c5962a38629fdcab48b18ac67e3ddf67c4f44fb1aeea9d5d6e7` |
| SYSTEM.CNF | 23 | 68 | `584bf5be` | `704676f6f71adf178103271643a3d43dc68d0722d1f8a341e1872edf2b52dc90` |
| SPEECH.STR | 90,000 | 258,889,728 | `73fbdf2c` | See local extraction manifest |
| 3MN_BLNK.DAT | 238,279 | 31,752,000 | `464b6ce9` | See local extraction manifest |
| CRASHBSH/CRASHBSH.DAT | 216,608 | 44,382,208 | `bb1f3cda` | See local extraction manifest |
| CRASHBSH/SHELL.EXE | 216,412 | 401,408 | `1b4bf9b6` | See local extraction manifest |

The PS-X EXE loads at `80010000`, has payload size `5c800`, entry `80059444`,
initial stack `801ffff0`, and initializes GP to `8006c3b0`. Entry clears BSS
`8006c4f4..800742d0`. A resident VA maps to EXE file offset
`VA - 80010000 + 800`.

WAD root directory size is `800`, with **195 physical entries**. Slot 0 is
the common Spyro bank, file offset `800`, size `22800`, SHA-256
`43d516cd2e766cd13d1dc0dad76c281a8c5f559bb21cd5bac6e65e183f650282`.
Slot 96 is a structured common model bank, not Sunrise Spring code.

## Level directory

Main scene slots are odd **97..169**. Their following even slot holds the
corresponding overlay, with overlay IDs **12..48**. This reverses Spyro 2's
preceding-overlay arrangement. Names are read at runtime from the original
37-entry name-pointer table at `80069cf8`, rather than copied into a source
array. The labels below identify the requested directory entries.

| Scene | Overlay | Semantic ID | Level | Extra areas | Category |
|---:|---:|---:|---|---:|---|
| 97 | 98 | 10 | Sunrise Spring | 0 | Home |
| 99 | 100 | 11 | Sunny Villa | 2 | Regular |
| 101 | 102 | 12 | Cloud Spires | 2 | Regular |
| 103 | 104 | 13 | Molten Crater | 2 | Regular |
| 105 | 106 | 14 | Seashell Shore | 3 | Regular |
| 107 | 108 | 15 | Mushroom Speedway | 0 | Speedway |
| 109 | 110 | 16 | Sheila's Alp | 0 | Sheila |
| 111 | 112 | 17 | Buzz's Dungeon | 0 | Boss |
| 113 | 114 | 18 | Crawdad Farm | 0 | Sparx |
| 115 | 116 | 20 | Midday Gardens | 0 | Home |
| 117 | 118 | 21 | Icy Peak | 2 | Regular |
| 119 | 120 | 22 | Enchanted Towers | 2 | Regular |
| 121 | 122 | 23 | Spooky Swamp | 2 | Regular |
| 123 | 124 | 24 | Bamboo Terrace | 2 | Regular |
| 125 | 126 | 25 | Country Speedway | 0 | Speedway |
| 127 | 128 | 26 | Sgt. Byrd's Base | 0 | Sgt. Byrd |
| 129 | 130 | 27 | Spike's Arena | 0 | Boss |
| 131 | 132 | 28 | Spider Town | 0 | Sparx |
| 133 | 134 | 30 | Evening Lake | 0 | Home |
| 135 | 136 | 31 | Frozen Altars | 2 | Regular |
| 137 | 138 | 32 | Lost Fleet | 2 | Regular |
| 139 | 140 | 33 | Fireworks Factory | 3 | Regular |
| 141 | 142 | 34 | Charmed Ridge | 2 | Regular |
| 143 | 144 | 35 | Honey Speedway | 0 | Speedway |
| 145 | 146 | 36 | Bentley's Outpost | 0 | Bentley |
| 147 | 148 | 37 | Scorch's Pit | 0 | Boss |
| 149 | 150 | 38 | Starfish Reef | 0 | Sparx |
| 151 | 152 | 40 | Midnight Mountain | 0 | Home |
| 153 | 154 | 41 | Crystal Islands | 2 | Regular |
| 155 | 156 | 42 | Desert Ruins | 2 | Regular |
| 157 | 158 | 43 | Haunted Tomb | 2 | Regular |
| 159 | 160 | 44 | Dino Mines | 3 | Regular |
| 161 | 162 | 45 | Harbor Speedway | 0 | Speedway |
| 163 | 164 | 46 | Agent 9's Lab | 0 | Agent 9 |
| 165 | 166 | 47 | Sorceress' Lair | 0 | Boss |
| 167 | 168 | 48 | Bugbot Factory | 0 | Sparx |
| 169 | 170 | 50 | Super Bonus Round | 3 | Bonus |

There are **37 main scenes and 38 extra areas**, 75 selectable areas. Extra
area identifiers are local 1/2, not reconstructed retail portal destinations.
Playable-character, speedway, boss and quest rules are not implied by loading
their terrain with the native Spyro controller.

The original plain menu overlay69 block `800749e8..80074a10` proves the
semantic conversion: IDs below60 index the resident byte table`80066fcc`;
IDs60 and above use `ID-60`. The result is stored at`8006c58c`. All37
original-block VM trials match the directory indices. Sunny is11/index1;
Super Bonus Round is50/index36.

## Scene, terrain, VRAM and collision

The first four extent pairs still mean VRAM, base scene, base model bank and
base gameplay. The main model directory moves from S2 archive `+40` to
**`+a0`**; uint16 class IDs begin at **`+1a0`**. Extra area i uses extent pairs
`+20+(i-1)*10` and `+28+(i-1)*10` (hex). Four candidate pairs are
reserved; the supplied disc has zero, two or three actual extra areas per
level. Only bounded matching scene/model and gameplay pairs are accepted.

Each extra scene/model blob starts with a bank-byte length L. Model offsets
are at `blob+L`, class IDs at `blob+L+100`, offsets relative to `blob+4`.
Terrain begins at **`blob+L+180`**. Extras reuse their parent's texture
descriptors and sky. Their VRAM starts with the parent image, with original
area-specific rectangle uploads applied as described below. Sunny area 1 starts at archive `1e6800`, L=`42660`;
area 2 starts at `270000`, L=`358d8`.

The base scene has four length-prefixed chunks: texture descriptors
(`8 + count*48`), terrain regions, special surfaces, and unimplemented
atmosphere/visibility data. Three raw grid words follow, then collision and
sky chunks. S2 has six chunks before its grid/collision chain. Extra areas
have four chunks too: regions, visibility, special surfaces, atmosphere.
They proceed directly to collision; only base scenes have the three grid words. Region
directories use their stored pointer; Icy Peak area 1 uses `+20`, so assuming
all directories begin at `+4` would reject a valid scene.

Near vertices, colors, faces, material descriptors, VRAM CLUT/TPAGE sampling
and atlas mapping retain their S2 encoding. The shared region decoder and
atlas mapping are unchanged. The bounded 37-scene survey found no invalid
used vertex/color/material index, texture rectangle or quadrant-blend
disagreement. An unused triangle color byte is metadata and is not validated
as an emitted corner. Texture scale/alignment remains shared across region
joins; no new texture projection or baking is introduced.

| Base scene metadata | Sunrise, 97 | Sunny Villa, 99 |
|---|---:|---:|
| Archive scene offset / bytes | `c7800` / 569,344 | `e1000` / 507,904 |
| Materials / regions | 94 / 138 | 98 / 119 |
| Near faces / render triangles | 6,475 / 11,726 | 5,791 / 10,633 |
| Collision chunk relative offset | `46f1c` | `3d050` |
| Collision bytes | 172,228 | 173,184 |
| Packed / special collision triangles | 10,409 / 34 | 10,445 / 1,269 |
| Sky relative offset / bytes | `70fe0` / 20,012 | `674d0` / 19,908 |
| Sky regions / triangles | 37 / 2,775 | 37 / 2,871 |

Nineteen extra areas contain a native VRAM rectangle immediately after their
collision chunk. Its four u32 values are x,y,width,height; the next u32 is an
alignment gap. Pixel bytes begin at `collision_end + 16 + alignment_gap`,
are aligned to 800, and end exactly at the area blob end. Each payload is
`width*height*2` bytes. The other nineteen extras have a zero header. Uploads
address the right half of PS1 VRAM (x>=512), so the shared helper subtracts
512 when indexing the stored 512x512 image. It validates all ranges and keeps
every pixel outside the rectangle unchanged. Terrain, Moby and Spyro texture
paths consume the same image; the padding before it is never treated as pixels.

Packed collision retains its nine-word header, 12-byte triangle records,
14-bit base coordinates, packed deltas and special-triangle flags. The
relation `triangle_offset + count*12 == flag_offset` fits 30 main scenes and
all 38 extras. Exact collision/near-mesh vertex matches are 92.73% in Sunrise
and 86.34% in Sunny, independently supporting the shared coordinate scale.
The two meshes serve different purposes and are not expected to be equal.

Main slots **105,119,137,139,155,159,169** have an explicitly empty 4-byte
collision chunk at this location. Their provisional collision comes from
non-degenerate **opaque** near triangles, reported by
`Level::collision_from_terrain`. The retail path providing their collision is
unresolved. Translucent surfaces are excluded from the fallback.

All 37 skies fit the existing packed palette/vertex fan/strip decoder.
YOTD's fourth scene chunk does not fit the S2 sky octree; all sky regions are
drawn with culling disabled. Fog, dynamic terrain/morph controllers, texture
animation and exact special-surface behavior remain open. Surface IDs are
retained without assigning S2 behavior by numeric coincidence.

Gameplay header XYZ is retained as `authored_spawn`. Extra areas often repeat
their parent's coordinates, so retail transition-specific entry positions
remain unresolved. `spawn_projected` identifies a provisional native ground
selection when needed; native spawn validation is reported below.

## Objects, models and animation

Gameplay's fixed header grows from `2c` to **`30`** bytes. Skip **12**
length-prefixed chunks to reach the Moby table (S2 skips eight). Records
remain **88 bytes** with the existing position/class/rotation/animation/
material/size/state/persistence/held-gem fields. Table indices remain object
IDs. Sunrise table: gameplay `+c64`, chunk `6fe0`, 209 records. Sunny:
`+808`, chunk `5b40`, 149 records. Both include `2800` bytes of reserved tail.
Reflection TPAGE/CLUT moves from gameplay `+10` to **`+14`**.

The 75-area inventory contains **7,877 records and 506 raw classes**.
There are 1,300 records without a matching area model, across 70 raw classes.
All have raw draw flags zero. Class3F3 accounts for 494 deferred gems; the
remaining 69 classes are not identified solely from their absent models.
WAD96 common models1/202/204/28B match native level geometry and do not fill
these gaps (class1 differs only in its inline sound word).
Static and animated object geometry, palettes, normals and textures retain
their S2 packet encodings after selecting the changed directory. Records without
models are preserved without fabricated visible meshes or assumed identities.

The common Spyro bank retains 62 slots and 20-byte animation/frame records.
Its normal model has **230 vertices**, two 115-vertex tracks, 82 colors and
**296 face packets** (S2 has 295). Model starts at common slot `+800`, shared
geometry at `+216c`. Native vertex compression, skin, UVs, colors and timing
are preserved. Controller-facing clip IDs are normalized while `source_id`
retains the raw ID:

| Raw YOTD clips | PC semantic clips |
|---|---|
| 0,1 | unchanged |
| 2 | 22 |
| 3..9 | raw minus 1 |
| 10 | 10 |
| 11 | 9 |
| 12 | 11 |
| 13..22 | raw minus 1 |
| 23..61 | IDs retained; this alone does not establish semantic identity |

Decoded pose/timing comparisons independently confirm native24,31,40-44,46,
49 and57 against their S2 motion families; native46 is the global 27-frame
headbash. Unused raw27,36,37,38,48,60 remain semantically unverified. Native
raw10/12 are not advertised as PC dodge rolls; genuine July donor rolls are
required and tracked by explicit import provenance.

Loop metadata uses original IDs; both transition-matrix axes are permuted.
YOTD has ten secondary clips and a 10x10 matrix. The existing eight PC
secondary actions use their actual stride of ten and normalized clip IDs.

S2's landmark indices differ from YOTD's reordered vertices. Donor import
derives a partition-preserving bijection from face agreement and neutral
22-frame poses, with Hungarian assignment to resolve ties. It rewrites native
vertex order and face indices into the PC landmark convention while retaining
actual positions and material/texture data. `source_vertices` records the
mapping. This preserves the existing flame, eyes, melee, carry and Dark Spyro
landmarks. Repeated import is idempotent.

Missing PC motions use the user's S2 donor: semantic families 9,18,25,33–35,
40–44,49,57 and supercharge 61 as needed. Clip 61 retains the original donor's
separate 255-vertex model; main-model charge variants are retargeted too.
Donor effects use object IDs `8000 | canonical_id`, avoiding YOTD raw-class
collisions. No artwork is invented.

Verified common container fallback is limited to **C8/C9** (463/221 records).
Their type-3 cylinders and bounded 4/28-byte parameter layouts fit the common
handler. Every placed instance has native fragment models 132/133. CA/CB
collision/contents layouts differ; they are not initialized as S2 heavy
containers. D1's conditional flame behavior remains unverified.

Raw **3F3** is a hidden, deferred gem record. Its parameter word zero names a
parent record; all **494** links are in bounds: CA183, CB192, C8 20, CD21,
9A21, CE11 and CF46. The original callback waits for the parent's negative
state, delays, spawns class1, and removes the placeholder. Native fallback
releases once after parent destruction, without exposing live-parent gems.
Original delay/CE follow-position behavior is not reconstructed.

Sunny main contains **12 FD small shield Rhynocs and 9 EE large club Rhynocs**,
identified from original models and handler behavior. FD/EE have 14/11 clips
and type-3 cylinders: FD broad1216/radius500/bottom0/top700/cap500; EE
broad2112/radius700/bottom0/top1400/cap700. F9 is the friendly lion, F1 frog
fodder, FC potted tree and 1BB a cinematic Rhynoc copy; those are excluded from
enemy fallback. The native fallback is explicitly restricted to scene99,
area0, uses short collision-safe pursuit/contact damage, and retains authored
animation. It does not claim original attack/death choreography.

## Resident decoder tables

These are verified bounded matches against the user's S2 executable. The
profile throws on unmapped table requests instead of reading S2 addresses.

| Purpose | S2 VA | YOTD VA |
|---|---|---|
| Vertex codebook | `80062674` | `80066274` |
| Blend weights | `80062868` | `80066530` |
| Sine / cosine | `80061bd8` / `80061c58` | `800658a0` / `80065920` |
| Atan / square root | `80061e5c` / `80066478` | `80065b24` / `80069f40` |
| Environment radii / angles | `800625ac` / `80062630` | `80066468` / `800664ec` |
| Gem denominations / values | `8006440c` / `80064428` | `80066964` / `80066980` |
| Gem meshes / colors / shiny colors | `80064430` / `80064438` / `80064440` | `80066988` / `80066990` / `80066998` |
| Sparx colors / glow geometry | `80061adc` / `8006430c` | `8006585c` / `80066864` |
| Primary loops / transitions | `80060334` / `8006042c` | `80067aa8` / `80067bac` |
| Secondary clip IDs / 10x10 transitions | different layout | `80067ba0` / `80068ab0` |

The PC follow camera uses provisional native defaults plus YOTD trig tables.
Unverified raw YOTD surface IDs4/10 do not activate S2 ice/sand controllers.
The complete YOTD authored camera-state array is not mapped.

## Sound banks

YOTD retains 20-byte sample descriptors, 256 global event bytes and 32 surface
sound bytes. After collision and sky, four further chunks precede the surface
map and descriptor chunk. Sunrise surface map is scene `+8a530`, 54 samples,
descriptor chunk `540`; Sunny is `+7b040`, 100 samples, chunk `8d8`. All 37
main banks have valid descriptors (2,808 total). ADPCM sample bytes start at
VRAM child **`+80000`**, rather than S2's usual `+86000`.

Moby inline sound headers retain four bytes for static models and twelve for
animated models, with main/area bank directories selected correctly. Native
object sounds are retained. Nineteen semantic global cues are proved by exact
waveform hashes, including flame, jump and water. Changed semantic-to-native
global IDs include 21→22,22→23,23→24,25→26,28→29,30→31. Unmapped PC cues use
deduplicated donor samples, and donor powered-object sound lists use reserved
IDs. Charge's native waveform identity remains unproved and uses its donor.

Footsteps use native **37x4** matrix `80067968`, indexed by level-array index
and surface category. Entries select the scene's 32-byte surface sound map,
which yields direct descriptor indices. Resident `8004cfb8..8004cfe4` and
event wrapper `8003bb10` establish this distinction from the global event map.
Original YOTD music/XA mapping is not implemented; it is not replaced with
unrelated S2 level music.

## Overlay VM and original code

The CPU/GTE VM is reusable, but the S2 bridge's resident ABI is not. YOTD
overlays load at **`800742d0`**. Original resident decoder
`8002cf58..8002cf94` reads loaded-base pointer `800722b8`, takes header+4 as
both key and end address, and XORs each word in `[base+1000,key)` with the key.
The header+8 check skips this for plain loader1. `8005e0bc` is the following
cache/hardware operation. Only that exact operation is guarded in the bounded
decoder trial.

Decoded header+8 points to a **1024-entry class-update callback array**;
header+c points immediately after it. This replaces S2's giant branch-tree
dispatch convention. The validated `find_yotd_moby_handlers` inspector accepts
only a fully bounded native table. Sunny overlay100 table is `80091ed4`:
FD=`8007fc04`, EE=`8007d8b8`, 3F3=`8007ae4c`, C8=`800779f8`, C9/CA=`80077cb8`,
F9=`8007e800`. FD accepts any nonzero attack; EE requires mask `00090000`
before its death state. Ordinary charge `20000` is resisted and flame `10000`
is accepted. Other attack meanings are not assumed from that mask alone.

| Native service/global | YOTD VA | Evidence/role |
|---|---|---|
| Moby array pointer / stride | `8006c550` / 88 | Resident allocator and original handlers |
| Frame dispatcher | `8003038c` | Builds active list, clocks animation, calls class callback |
| Active-list builder / storage | `80055680` / `8006e788` | Uses group flags and player origin |
| Spyro origin / group flags | `80070328` / `8006e450` | Active-list inputs |
| Class-update table global | `800742d8` | Overlay header+8 |
| Spawn callback global | `800742e0` | Header+10; Sunrise callback `8008368c` |
| Particle callback global | `800742e4` | Header+14; resident effect calls |
| Free-Moby allocator | `80055a7c` | Clears/allocates 88-byte records |
| Alloc cursor / end / params cursor | `8006c610` / `8006c5ec` / `8006c728` | Allocator globals |
| Spawn wrapper / remove Moby | `80036a68` / `80055b18` | Original deferred-gem handler |
| Semantic level ID / array index | `8006c5bc` / `8006c58c` | Distinct from overlay ID and WAD slot |
| Scene relocator in loader1 | `80079744` | Native scene relocation structure |
| Candidate gameplay loader | `8002b810` | Requires runtime boot validation |
| Camera look | `80013ae4` | Mode9 writes |
| Camera state / request | `8006e044` / `8006e160` | Resident camera fields |
| Camera shot fields | `8006e174` / `8006e178` | Resident shot inputs |

The original 3F3 callback at Sunny `8007ae4c` computes parent address from
`moby_array + 88*parent_index`. State1 waits for parent+48's sign bit, states
2/3 delay and spawn class1. The already-dead-parent branch explicitly copies
held-gem byte+50. This independently confirms the object-table interpretation.

The workbench uses explicit native fallback until full relocation, services,
animation ownership, collision bridge and progression semantics are verified.
`make_retail_script_image` and `boot_retail_level` accept only S2 RetailUS;
YOTD is rejected before loading hardcoded S2 services. The original S2
dispatcher detector is unchanged.

## Native validation and visual evidence

The native scene census validates all75 areas: 512,780 visual triangles,
515,397 collision triangles, seven marked opaque-terrain collision fallbacks,
and38 marked extra-area spawn projections. All37 main spawns remain authored.
The native model/audio probe validates 2,197 model banks, 5,286 object clips
and45,216 object frames across7,877 records. Tests have exercised54 real loose
gem cases,76 real container cases (73 direct/3 linked), and21 Sunny fallback
enemies. Final controller/VM/VRAM results, full CTest outcomes and capture
inspection are recorded in the root [provisional report](../SPYRO3_PROVISIONAL.md).

Research evidence remains under `analysis-yotd`: extraction/identity manifests,
WAD directory and level names, resident-table matches, scene/area inventories,
model/object inventory, audio validation, and retained VM trial logs/RAM.
Research RAM and transformed overlay images contain private game data and
must not be merged or distributed with source.

The supplied Ghidra launcher/JDK failed with sandbox-native path access errors
before useful analysis. REA target analysis was unavailable/rejected; no REA
evidence IDs or Ghidra decompilation results are claimed. Static claims above
come from the named local byte/code inspections and bounded decoders.
