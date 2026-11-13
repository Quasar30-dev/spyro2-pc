# Spyro 2.5 Backrealms — new level plans and Crownworks rework

Status: design plans, revised 10 October 2026 against the current engine. None
of the four levels is built yet. The Spring Realm hub (the intro's Dragon Realm
scene, WAD 73, expanded; see the
[Spring Realm README](../data/custom-levels/spring-realm/README.md)) and the
Crownworks arena (see [RIPTO_BOSS.md](RIPTO_BOSS.md)) exist and are playable.

Every number quoted as "current" below is taken from the code on 10 October
(file named in brackets), so a plan never depends on a power that works
differently from how it is described.

---

## 0. Starting point

### 0.1 Where each level hangs off the hub

| Level | Hub entrance (Blender x, y, z) | State today |
| --- | --- | --- |
| Tidewell Cisterns | Retail west arch, (37.75, 41.16, 11.09), facing +x | Live portal; Summer Forest stand-in picture and destination |
| Stormglass Spires | Retail east gateway, (69.71, 39.57, 11.19), facing −y | Live portal; stand-in |
| Cinderfrost Fault | Retail south-west arch, (34.53, 26.76, 9.38), facing +x | Live portal; stand-in |
| The Hollow Orrery | Orrery Hall (the keep), east wall | Room, lining and vases built; **no portal yet** |
| Crownworks | Crown gate beyond the castle's north gate, (58.5, 137, 13.6), facing −y | World door (mode 2), sealed until four emblems; also launched separately (`Play Cyborg Ripto.cmd`) |

The three arch portals fill the Dragon Realm's own unused blank arches
(`stage_dressing.py`, `ARCH_PORTALS`). The West and East Halls each have an
empty portal dais at their north end, kept free for later content.

### 0.2 What a new level can already use (S2WORLD 4)

- **Terrain and materials:** up to 128 materials from any retail scene, plus
  authored 64×64 tiles (`-2 "textures/x.rgba"`) and `TILE_OVERRIDES`.
  `RETAIL_TERRAIN` can reference a whole retail scene from the disc.
- **Actors:** up to 10,000. Each names its source scene through `IMPORTS`; spirit
  values and gate requirements can be overridden. Using **Summer Forest (15)** as
  the source bank gives orbs (`0x71`), speakers (`0x77`, `0x1bc`, `0x16d`), talk
  triggers (`0x110`) and vases (`0xc9`) without imports.
- **Native enemies** that can be borrowed, with the retail AI the engine already
  runs (`objects.cpp`):
  - small and club lizards (Glimmer, 17);
  - gear grinders and robots (Hurricos, 23);
  - metal idols (Idol Springs, 19);
  - bucket and fire blubbers (Breeze Harbor, 39);
  - elephant and rhino snails, spitting monkeys and the platypus (Mystic Marsh, 59).

  Borrowed enemies keep their kind, notice and reach; their donor route is dropped.
- **Speech and goals:**
  - up to 32 two-line conversations;
  - up to 8 objectives, each with an orb reward, checkpoints, and an optional
    speaker or underwater flag.
- **Mechanisms** (`GAMEPLAY 1`, up to 128):
  - kinds: shuttles, pressure lifts, sinking slabs and flame relays, all with
    moving collision;
  - timed trials (start point, tick limit, objective).
- **Other sections:**
  - `WATER_VOLUMES` (up to 512 bounded swimmable volumes);
  - `LIFTS` (whirlwinds);
  - `SCENERY`/`SPLATS`;
  - an optional `ARENA` (the Crownworks module).

*Checked against the loader on 14 October 2026:*
- every limit above matches `custom_level.cpp` and `custom_homeworld.inl`: 128 materials (S2WORLD 4), 10,000 actors, 32 doors and 32 conversations, 8 objectives, 128 `GAMEPLAY 1` mechanisms of exactly the four listed kinds, and 512 water volumes;
- the borrowable native enemies are exactly the eleven `EnemyKind`s;
- none of H1–H6 below existed yet. Since 15 October, H1 (door mode 2, world-to-world, with Return Home back through the door) and the *Sliceable* part of H5 (`SLICEABLE 1` volumes) exist; S2SAVE 19, `FLOOD_REGIONS`, `ROUTES`, the rest of `GAMEPLAY 2` and element trials do not.

The measured mechanics in 0.3 come from `spyro_element_tests` and `spyro_shockwave_tests`, which print them:
- recharge from empty: 2.2 s;
- headbash pocket: 1.6 deep in 0.4 s, 2.7 radius, gone after 16.4 s;
- Water Fury flood: 4 deep in 3 s, covering at least 39 units, drained after 57 s;
- Void Beam cut of a 6-unit tower: 6 pieces, landed in 2.6 s, solid stump lid, 450 collision triangles.

### 0.3 Current element behaviour the puzzles rely on

| Power | Current behaviour (source) |
| --- | --- |
| Tiers | 1 breath, 2 shot, 3 headbash element + Fury. Costs 30/50/80 points; Convexity 60/90/120 (`extensions.cpp`). |
| Water Fury flood | Rises to **3.4 units above the floor under Spyro**: 3 s rise, 12 s full, 45 s slow drain. One flood at a time; it covers the whole level below its surface (`element_water.cpp`). |
| Water headbash pocket | A cylinder of radius **2.7** that rises **1.6** above the floor in 24 ticks, holds 10 s, then drains over 6 s. At most four; a fifth replaces the oldest. |
| Ice freezing | Frost breath, Ice Shards and the Ice headbash freeze a whole body of water. Spyro skates on it (Winter Tundra state-9 physics). It lasts **75 s**, then melts over 2.5 s, sooner when flame or an Ember shot touches it (`element_ice.cpp`). |
| Furies (reworked 14 October) | Holding Fury gears up through up to three 50-tick stages (0.8 s each), each with its own camera angle. Release fires one sphere blast per stage reached, 18 ticks apart: radius 7.5 / 11.25 / 15, all counted as Fury hits. Then the element's sequence runs: Inferno 8 s of meteors, Deluge 9 s of rain (plus the flood), Glacier with three spike rings and the shatter at 6 s, Storm rings at 5 s and 7.5 s, Rift 5 s and then **25 s of Dark Spyro** (`element_powers.cpp`). The Fury camera keeps clear of walls and large objects, so tight rooms are safe to fire in. |
| Recharge (14 October) | Breath energy refills at 48/s after a 0.3 s pause, about 2 s from empty. The Fury meter fills passively at 5/s, plus 30 per defeat and 1 per gem. Puzzles can expect a Fury roughly every 20 s of play, sooner in fights. |
| Containers | Every elemental breath, shot and wave sends `0x160000` and breaks every container. Fire tier 2+ breath also breaks heavy containers and metal vases (`elements.cpp`). |
| Arc chaining | At most 3 extra targets. Wet + Electric doubles the radius. |
| Void Beam | Slices terrain into falling slabs with collision, and erodes thin geometry. Retail terrain damage is saved per level (S2SAVE 18). |

### 0.4 Engine work the levels need (hub prerequisites)

These come first; each is small and testable on its own.

Already in place (14 October): a power gate placed in a custom world opens on spirits that fly to it, as in retail, and flies them straight in. The donor gate's own spirit routes are waypoints in its retail level, so they are dropped (`custom_level.cpp`). The Spring Realm's grotto Superfly gate opens about 300 ticks after its six lizards fall, and a new level's gates need no authored routes.

- **H1 — World-to-world portals.**
  - *Problem:* a door can only borrow a retail homeworld portal's destination,
    and the custom world is loaded once at startup (`desktop.cpp`). Return Home
    (`0x1aa`) always goes to Summer Forest.
  - *Fix:* add door mode 2, `2 "worlds/tidewell.s2world"`, loaded through the
    normal portal transition. Its Return Home goes back to the hub arch it came
    from. Paths use the same rules as authored tiles: relative, no `..`.
  - *Done (15 October):*
    - The door line is a mode-1 aperture, then the world path (relative to `data/custom-levels`), the emblems that open it and presentation flags (1 night, 2 the boss encounter).
    - The workbench keeps a stack of where each door was left. A world's Return Home pops it and comes back out in front of that door, facing away from it.
    - The crown gate to the Crownworks is the first one (`--world-door-check`).
- **H2 — Custom-world progress.**
  - *Problem:* custom-world progress is session-local.
  - *Fix:* save it per world, keyed by title and content hash, in a new save
    block (S2SAVE 19). It covers gems, objectives and orbs, the level emblem,
    trial completions, granted element tiers and terrain damage (slices,
    craters), so a solved slicing puzzle stays solved. Retail save slots stay
    isolated.
- **H3 — Flood regions.**
  - *Problem:* a Fury flood covers everything below its surface.
  - *Fix:* add `FLOOD_REGIONS 1`: boxes with an authored peak height and an
    optional drain-time multiplier. A Fury cast inside a region floods only that
    region to its peak. A Fury cast elsewhere keeps today's behaviour.
- **H4 — Authored routes.** `ROUTES 1` gives a borrowed enemy a patrol route,
  using the retail waypoint format the native AI already reads.
- **H5 — Element mechanisms.** `GAMEPLAY 2` adds the kinds below. Each one
  listens to the same `ElementHit` mailbox as enemies, so no new damage path is
  needed:
  - **ElementRelay:** any element, or one named element, lights it, generalising
    today's FlameRelay;
  - **Conductor:** Electricity charges it for N ticks;
  - **SluiceGate:** opens while the dynamic water at its sill is above a height;
  - **WeakCrust:** breaks to a headbash shockwave of a named element;
  - **Sliceable:** an authored mesh the Void Beam may cut. Everything else
    stays unsliceable in custom worlds.
    - *Done (15 October)* as `SLICEABLE 1` volumes: boxes, each with an optional fall direction.
    - A world with the section is cut only inside them.
    - A volume with a fall direction is cut level, comes away whole and topples about its stump edge.
    - Worlds without the section keep cutting anywhere, so the Spring Realm's castle can still be sliced.
- **H6 — Element trials.**
  - A trial is a timed `GAMEPLAY` trial whose reward is "grant tier N of
    element E" instead of an orb.
  - With the upgrade master switch off, the level's own element is granted for
    the visit only, and its trials become orb challenges.

---

## 1. Shared rules for every new level

**Structure.**
- 400 gems.
- Three orbs, each an objective: one quest from a speaker, one timed trial, one
  found or mastery challenge.
- One level emblem for finishing the critical path (the custom-world equivalent
  of a talisman, stored by H2).
- One element trial chain: the level grants its own element's tiers as you go,
  so its puzzles can rely on them. Puzzles for *other* elements stay optional.

**Critical path vs. mastery.**
- The critical path uses only the level's own element (granted in the level),
  Spyro's retail moves and the upgrades bought with points.
- Other elements open optional gem rooms and orbs, or shortcuts.
- Every element-gated gem also has a slower route without that element, except
  where a table below says otherwise.

**Construction.**
- Follow [image-to-level-standard.md](image-to-level-standard.md):
  1. complete Blender blockout;
  2. calibrated reference renders;
  3. generated detail views;
  4. MoGe fitting;
  5. editable mesh polish;
  6. separate reusable texture sheets;
  7. floor-continuity checks: overhead, downward, oblique and reverse views
     across real joins, then a walk.
- Retail textures come from the player's disc at load time and are never
  shipped.
- New tiles are authored 64×64 RGBA, quantised to PS1 15-bit colour, and painted
  from the retail tiles. Each has its own `paint_*.py`, like the Spring Realm's
  blossom tiles.
- When a level extends a retail scene, follow the hub rules: extend the scene's
  own UV layout and vertex tint, and never lay planar-mapped bordered tiles over
  it.

**Budgets per level.**
- About 9–12k visible collision triangles.
- At most 24 sectors visible at once.
- 180–260 placed objects.
- At most 24 enemies in any one zone.
- Water is always bounded `WATER_VOLUMES`, never an infinite plane.

**Enemies.**
- Custom worlds have no retail overlay, so `RetailScripts` cannot run their
  enemies. Use the borrowed native kinds (0.2) plus new PC actors, built with
  the demon asset pipeline:
  - a mesh within the PS1 budget;
  - 2–4 clips;
  - GPT-image texture sheets painted from retail enemy references.
- Every new actor gets a row in the element damage translation table, so the
  choice of element matters.

**Tests and captures.** Each level ships with:
- a `spyro_custom_level_tests` run: spawn, every actor's floor, the S2SHOT
  critical route on the real controller, every gem reachable, every enemy
  defeatable, the exit entered;
- an `s2world` round trip;
- one native capture per element puzzle;
- a floor-continuity report;
- deterministic 60 Hz unit tests for every timer puzzle (flood heights at the
  sills, freeze life, conductor charge, crust re-melt);
- a quest pass with `--attack talk` for each speaker.

**Milestones per level.**
- **M0 Blockout:** grey-box geometry, spawn, portal in and out, the critical
  route walkable.
- **M1 Gameplay:** all mechanisms, gems, enemies, objectives and the trial
  chain; tests green.
- **M2 Detail:** reference renders, generated detail, authored tiles, MoGe
  polish.
- **M3 Polish:** lighting tints, audio, captures, continuity sign-off.

---

## 2. Tidewell Cisterns — Water

**Pitch.**
- A drowned aqueduct city behind the hub's west arch.
- The sluice keepers have lost control of the water, and levels rise and fall
  across the map.
- Here the player learns that Water Fury *floods the room* and the Water headbash
  *makes swim pockets*, and that both drain.

**Look.**
- Pale limestone, verdigris bronze, moss in the joints, green-blue water.
- Retail tiles:
  - Aquaria Towers (25): stone, tile, seaweed;
  - Ocean Speedway (29): water;
  - Summer Forest (15): moss, grass.
- Authored tiles:
  - `aqueduct-arch` (cracked limestone, painted from Aquaria's stone);
  - `sluice-bronze` (from Aquaria's gold trim, shifted to verdigris);
  - `algae-stone` (wet streaks, from Summer Forest's wall moss).

**Hub link.** The west arch portal (H1). Return Home stands at the Spillway
Gate.

### 2.1 Layout and gems

| Zone | Size (units) | Gems | Content |
| --- | --- | --- | --- |
| Spillway Gate (start) | 30 × 24 | 40 | Return Home, the sluice keeper (`0x77`), shallow training cistern 1.2 deep |
| Low Market | 60 × 40 | 90 | Half-flooded streets at floor 0; walkway ring at **+3.0** |
| Great Cistern | round, Ø 44, 18 deep | 100 | Four aqueduct mouths at +3.2, +6.4, +9.6 and +12.8 above the floor |
| Bellows Works | 36 × 30 | 70 | Pump room, the level emblem, the Bellows trial |
| Reflecting Gardens (optional) | 40 × 40 terraces | 60 | Three terraced pools, the orb rooms |
| Flood-run bonus | — | 40 | Walkway gems only reachable at full flood |
| **Total** | | **400** | |

The walkway ring sits at +3.0 so it is reachable from a flood. A flood cast on
the market floor peaks at +3.4, and Spyro jumps out of the surface onto the
walkway. Without the flood, the walkways are reached by the slower bell-tower
climb on the north side.

### 2.2 Element trial chain (Water)

1. **Spillway Gate:** the keeper grants Water tier 1 (Torrent breath) after the
   swim tutorial.
2. **Low Market:** the Torrent pushes three stuck sluice wheels. Doing so grants
   tier 2 (Bubble Burst shot).
3. **Great Cistern** entrance trial: shoot six floating targets in 40 s, which
   grants tier 3 (Tidal headbash + Fury).

### 2.3 Element puzzles

- **Flood the market** (critical).
  - Cast Water Fury on the market floor: the water reaches +3.4 for 12 s, then
    drains over 45 s.
  - Swim to the walkways, then ride the drain down to the submerged
    counting-house door, a **SluiceGate** that is open only while the water is
    between +1.0 and +2.0 at its sill.
  - Needs **H3**, with the market's flood region capped at +3.4.
- **Pocket stepping-stones** (critical, short).
  - Three gaps of 5–6 units over a culvert are too wide to glide.
  - Headbash on each ledge to raise a pocket (radius 2.7, 1.6 deep), swim
    across, and jump from its rim onto the next ledge, set at +1.4.
  - There are five ledges and a cap of four pockets, so the first pocket drains
    before the fifth is needed. The order matters.
- **Great Cistern** (critical).
  - Each aqueduct mouth needs the water at its own height.
  - The cistern is split into four stacked flood regions. A Fury from the
    floor raises the water to the first mouth (+3.2); Furies from each higher
    landing reach the next.
  - The 12 s hold is the puzzle clock.
  - The non-element route uses the Bellows pumps: three valve carries (carry
    upgrade or Spyro's mouth) under enemy waves. It is slower but always works.
- **Wet circuits** (optional, Electricity): soaked enemies chain with double
  radius. A flooded pump hall conducts one Arc breath to three **Conductors** at
  once; that opens the Reflecting Gardens orb room.
- **Ice floes** (optional, Ice): freeze the cistern while it is flooded. The
  ice lasts 75 s at the high waterline, a walkable floe to a gem shelf at +6.4.

### 2.4 Enemies

| Enemy | Source | Rule |
| --- | --- | --- |
| Bucket blubber | Breeze Harbor 39 (native) | Torrent soaks; charge finishes |
| Fire blubber | Breeze Harbor 39 (native) | Water defeats it in one hit (flame weakness) |
| Sluice crab | New PC actor | Charge-proof shell; Torrent pushes it off ledges |
| Eel lantern | New PC actor | Underwater only; Electricity stuns it |
| The Sunken Bell (mini-boss) | New PC actor | Its bell can only be hit at full flood |

### 2.5 Orbs

1. **Fix the bellows** (keeper quest): carry three valves to the Works.
2. **Flood run** (timed trial): 25 walkway gems during one flood's hold and
   drain.
3. **Wet circuit** (found): the Reflecting Gardens orb room.

### 2.6 Engine and tests

- **Engine work:** H1–H3, H5 (SluiceGate, Conductor), plus the three new actors.
- **Unit tests:**
  - flood height at each sill at ticks 180, 900 and 3600;
  - pocket replacement order;
  - the SluiceGate's open window.
- **Captures:** market at full flood, the pocket chain, each cistern mouth.

---

## 3. Stormglass Spires — Electricity

**Pitch.**
- Glass-capped spires above the clouds, behind the hub's east gateway, linked
  by copper cables and wind bridges.
- A storm keeps striking the spires. Whoever controls the lightning controls
  the lifts.

**Look.**
- Pale blue glass, oxidised copper, white marble, a deep violet storm sky.
- Retail tiles:
  - Cloud Temples (61): marble, gold trim;
  - Zephyr (41): sky, wood;
  - Metropolis (67): metal panels.
- Authored tiles:
  - `storm-glass` (cracked panes);
  - `copper-coil`;
  - `cloud-floor`.

**Hub link.** The east gateway portal (H1). The gateway already has its
invisible collision panel made liquid.

### 3.1 Layout and gems

| Zone | Gems | Content |
| --- | --- | --- |
| Anchor Platform (start) | 30 | Return Home, the conductor keeper (`0x1bc`) |
| Cable Run | 90 | Six spires 14–18 apart, each linked by a whirlwind `LIFT` and a glide |
| Coil Garden | 80 | An open plateau of coils; combat arena |
| The Eye | 100 | Central spire, 60 tall, with a rotating lens and the emblem at the top |
| Leeward Terraces (optional) | 70 | Wind tunnels and glide rings |
| Conductor bonus | 30 | Gems released by charged conductors |
| **Total** | **400** | |

### 3.2 Element trial chain (Electricity)

1. The keeper grants tier 1 (Arc breath) on the Anchor Platform.
2. **Cable Run:** charge the first express lift's three conductors, which grants
   tier 2 (Spark Orb).
3. **Coil Garden:** clear the arena wave, which grants tier 3 (Thunder ring +
   Storm Fury).

### 3.3 Element puzzles

- **Power the lift** (critical).
  - Arc breath charges a **Conductor** for 8 s.
  - A **PressureLift** rises only while its conductor is charged.
  - The express lift needs three conductors charged at once. One Arc chains to
    all three when they stand within the chain range (at most 3 extra targets).
- **Storm Fury switchboard** (critical at the Eye). The Storm runs 10 s. A
  spire's crown opens only when all of its 8 conductors are charged during one
  Fury. They are spread so that the player must move during the Fury.
- **Spark Orb couriers** (critical, once).
  - A homing Spark Orb follows a copper rail to a switch behind glass.
  - Aim it at the rail's start.
  - Engine: rail-following homing.
- **Glide and boost** (optional): wind bridges need the two-boost glide tier.
  Dodge-roll through timed arcs (`Shuttle` arcs with a damage volume).
- **Insulated crates** (optional): carry rubber-sealed crates onto charged plates
  to ground them, opening optional rooms.

### 3.4 Enemies

| Enemy | Source | Rule |
| --- | --- | --- |
| Robot | Hurricos 23 (native) | Electricity counts as charge |
| Gear grinder | Hurricos 23 (native) | Patrols a `ROUTES` loop (H4) |
| Storm imp | New PC actor | Flies and dodges Fire; Electricity knocks it down |
| Glass bat | New PC actor | Ice shatters it |
| The Lens Warden (mini-boss) | New PC actor | A turret whose lens reflects breath. Divert its beam with a Spark Orb, then hit it from behind. |

### 3.5 Orbs

1. **Twelve conductors** (trial): charge all twelve before the storm passes, in
   120 s.
2. **Leeward rings** (trial): glide through 10 rings.
3. **Lost lightning** (keeper quest): bring three escaped spark wisps back.

**Engine work:** H1, H2, H4, H5 (Conductor), rail homing, and wind volumes that
lift a glide.

---

## 4. Cinderfrost Fault — Fire and Ice

**Pitch.**
- A volcano split by a glacier, behind the hub's south-west arch.
- One side burns, the other is frozen solid, and the fault between them is
  crumbling.
- The player alternates thawing and freezing to cross it.

**Look.**
- Basalt and ember-cracked rock against blue ice and snow.
- Retail tiles:
  - Magma Cone (49): lava, basalt;
  - Crystal Glacier (37) and Winter Tundra (57): ice, snow.
- Authored tiles:
  - `ember-crust`, which blends into `rime-crust`;
  - `steam-rock`.

### 4.1 Layout and gems

| Zone | Gems | Content |
| --- | --- | --- |
| Steam Gate (start) | 35 | Hot springs, Return Home, the fault surveyor (`0x16d`) |
| Ashen Terraces (fire side) | 85 | Lava rivers, basalt columns |
| Rime Halls (ice side) | 85 | Frozen caverns, enemies frozen in walls |
| Meltwater Lake | 60 | Skating run under the frozen falls |
| The Fault | 90 | A long canyon where the two sides meet; the emblem at the far end |
| Caldera Rim (optional) | 45 | The orb challenges |
| **Total** | **400** | |

### 4.2 Element trial chain

- **Fire:** tier 1 is retail.
  - Clearing the Ashen Terraces grants Fire tier 2 (Ember shot, and breath that
    breaks heavy containers and metal vases).
  - The Fault's first crust trial grants tier 3.
- **Ice:**
  - The surveyor grants Ice tier 1 (Frost breath).
  - The Rime Halls grant tier 2 (Ice Shards).
  - The lake trial grants tier 3 (Frost bloom + Glacier Fury).

### 4.3 Element puzzles

- **Cool the lava** (critical).
  - Water and Ice cool lava into walkable crust. This uses the existing
    cooled-lava path and is compatible with headbash Lava mode.
  - Crust re-melts after 12 s, so each crossing is a sprint.
  - Engine: re-melt timers on cooled-lava stamps.
- **Thaw the walls** (critical).
  - Fire thaws ice walls and frees the surveyors trapped inside: six NPCs, one
    per orb step.
  - It also frees enemies, some of which drop keys.
- **Freeze the falls** (critical).
  - Ice breath freezes a lava-heated waterfall into a ladder of icicles.
  - Fire melts the icicles that block the way down.
  - Engine: freezable waterfall geometry, using the same
    append-and-disable triangles as destruction.
- **Skate the meltwater** (critical).
  - Frost breath, Ice Shards or the Ice headbash freeze the whole lake.
  - Spyro skates on Winter Tundra's state-9 physics and clip 9.
  - A 75 s skating run crosses gems and ramps, and ends at the Fault.
  - Flame or an Ember shot melts the ice early, so the player chooses the order
    of freezing and burning. Two gem islands are only reachable by melting a
    channel and swimming it.
- **Fault collapse** (critical).
  - Headbash shockwaves (Fire ring or Ice bloom) break **WeakCrust** sections
    over the canyon.
  - Breaking the right sections drops a bridge; breaking the wrong ones drops
    Spyro into the lava, which costs a life and restores the crust.
  - Craters respect the crater budget.
- **Inferno vs. Glacier** (optional finale route). The last canyon has two
  routes: the Inferno Fury burns a path through the ice side, and the Glacier
  Fury freezes the lava side.

### 4.4 Enemies

| Enemy | Source | Rule |
| --- | --- | --- |
| Ember lizard | Glimmer 17 lizards, re-tinted | Water or Ice defeats it in one hit |
| Club lizard | Glimmer 17 (native) | Frozen in the walls; freed by thawing |
| Frost troll | New PC actor | Fire only |
| Steam bat | New PC actor | Electricity |
| The Fault Wyrm (mini-boss) | New PC actor | Alternates fire and frost breath; only vulnerable to the opposite element |

### 4.5 Orbs

1. **Crust walker** (trial): cross the Ashen Terraces on crust alone.
2. **Six surveyors** (quest).
3. **Wyrm without a scratch** (mastery).

**Engine work:**
- H1, H2, H5 (WeakCrust);
- crust re-melt timers;
- freezable waterfalls;
- a boss weakness that alternates by element.

---

## 5. The Hollow Orrery — Convexity

**Pitch.**
- A shattered astronomical machine floating in the dark between realms, where
  the Convexity leaks through.
- Here Spyro learns to *cut the world*: the Void Beam slices pillars and towers,
  and the pieces fall with physics and keep full collision.

**Look.**
- Black stone with violet veins, brass orrery rings, a star-filled void.
- Retail tiles:
  - Ripto's Arena (71): dark stone;
  - Metropolis (67): brass;
  - Dragon Shores (69): star sky.
- Authored tiles:
  - `veined-obsidian`;
  - `brass-gear`;
  - `void-fringe`.

**Hub link.**
- A portal on the Orrery Hall's east wall, in the keep. It is the last level,
  so it opens only after the other three emblems.
- Convexity's purchase also requires all four other elements at tier ≥ 1 and a
  campaign milestone. The level's first trial satisfies the milestone.

### 5.1 Layout and gems

| Zone | Gems | Content |
| --- | --- | --- |
| Threshold Ring (start) | 30 | Floating brass ring, Return Home |
| Pillar Field | 90 | Tall columns over the void |
| The Gearworks | 90 | A turning machine room |
| Heart of the Orrery | 80 | The central sphere, the emblem |
| Inverted Garden (optional) | 70 | A garden hanging upside-down below the Orrery, reached by rifts |
| Void bridge | 40 | The Dark Spyro gauntlet |
| **Total** | **400** | |

### 5.2 Element puzzles

- **Slice a bridge** (critical).
  - Cut the top off a **Sliceable** pillar so it falls across a gap.
  - The cut angle decides where it lands; the slice-size setting decides the
    piece size.
  - H2 keeps the fallen slab after a reload.
- **Erode to reveal** (critical). Void erosion opens thin walls and false floors.
- **Gearworks timing** (critical). Slice gear teeth to stop a turning ring
  (`Shuttle` rotation) at the right angle.
- **Demon allies** (optional). The Rift eruption summons demons that hold
  pressure plates while Spyro goes elsewhere. Engine: demon plate-holding.
- **Dark Spyro gauntlet** (orb). A Convexity Fury gives 25 s of Dark Spyro, just
  enough to cross the void bridge before it dissolves.

### 5.3 Enemies

| Enemy | Source | Rule |
| --- | --- | --- |
| Metal idol | Idol Springs 19 (native) | Electricity or charge |
| Void wisp | New PC actor | Hurt only by Convexity, or by a charge while Dark |
| Brass automaton | New PC actor | Metal; Electricity |
| Orrery sentinel | New PC actor | Shield is Sliceable |
| The Astrarch (boss) | New PC actor | A giant rotating armillary; slice its rings to expose its core |

### 5.4 Orbs

1. **Pillar staircase:** drop three pillars into a staircase.
2. **Void bridge:** finish the Dark Spyro gauntlet.
3. **Astrarch with allies:** defeat the Astrarch with demons alive.

**Engine work:**
- H1, H2, H5 (Sliceable);
- slabs resting on moving geometry;
- demon plate-holding;
- a gravity volume for the upside-down garden (stretch goal).

---

## 6. Crownworks — rework and major polish pass

### 6.1 What it is today

- `data/custom-levels/cyborg-ripto-arena`, S2WORLD 3 with an `ARENA` section.
- The tiers are a lower floor at 0, a middle circuit at 3.5 and an upper crown
  walk at 7, joined by four ramps and a lower lava ring.
- **Arena systems:**
  - three relays (shield, repair, overdrive);
  - a cooling valve;
  - four cover walls;
  - Superflame, Superfly, Supercharge and healing stations;
  - two launch pads;
  - four pressure trapdoors and six vents;
  - two crushers and four jets in the swim galleries;
  - four pools linked by flooded galleries.
- **Fight:**
  - Cyborg Ripto on foot (60 HP);
  - Mecha-Gulp (80) and Mecha-Crush (60) together;
  - the bird (80).
- **Damage** (`ripto.cpp`, `take_hits`): 1 flame, 2 charge, 5 headbash,
  10 powered, 15 green bomb, with a 35/70-tick cooldown. The shield relay halves
  damage.
- Elements reach Ripto only through the attack masks they translate to. Disabled
  relays stay disabled.
- **Done on 10 October** (see 6.7): phase restart, the Fury cap and relays
  limited to phase 2.

### 6.2 Problems to fix

- **Too many systems at once.** Relays, the cooling valve, cover walls, three
  stations, trapdoors, vents, crushers and the molten floor all compete with
  three boss phases.
- **Readability.**
  - Warnings are amber everywhere, so the boss's own tells (Optic Lance
    sightline, Scepter Nova pulses, Hammer and Anvil) get lost.
- **Arena flow.** The middle terrace and its pools are mostly ignored by the
  fight.
- **Elements.** The fight predates the element kit.
  - A Storm Fury's ten seconds of strikes or a Convexity breath only meet the
    generic cooldown.
  - Slicing, floods and demons have no rules in the arena.
- **Presentation.**
  - No intro or outro.
  - Phase changes are state changes only.
  - Flat lighting; stretched texture joins on the terrace floors.
- **Hub.** It is not reachable from the Spring Realm.

### 6.3 Fight structure after the rework

| Phase | Arena state | Boss kit | What the player learns |
| --- | --- | --- | --- |
| Intro | Cinematic: Ripto wakes the Crownworks; the lower ring lights up; the camera passes the three relays | — | Where the relays are |
| 1. Cyborg on foot (60 HP) | Lower ring only; upper walk sealed | Scepter casts, Optic Lance, Scepter Nova, evades | Dodge-roll the lance, jump the nova rings, punish recovery windows |
| Interlude | Floor segments drop away; Spyro is pushed up to the middle terrace | Ripto summons the mechs | The terrace and its pools |
| 2. Gulp + Crush (80 / 60 HP) | Middle terrace and pools; relays now reachable | Gulp lasers, flame, fire train; Crush club pulses; Hammer and Anvil team attack | Pools break line of sight; lure the train into Gulp; relays weaken the duo |
| 3. Bird (80 HP) | Upper walk and open sky | Climb and dive sequences, homing volleys | Superfly and Superflame grant (existing), glide-boost dodges |
| Finale | Crown core exposed; 20 s window | Ripto clings to the core | Any one Fury ends it, each with its own finishing animation |
| Outro | Cinematic: the Crownworks falls silent; Return Home opens to the hub | — | — |

- The relays act in phase 2 only, where their effects (shield, repair,
  overdrive) are clearest.
- The cooling valve and cover walls belong to phase 1, and the stations to
  phase 3.
- The trapdoors and crushers move to the optional swim galleries, so the arena
  is about the boss.

### 6.4 Elements in the fight (code rules)

- **Per-hit damage by element**, applied in `take_hits` before the shield, and
  read from `ElementHit` rather than guessed from masks:

  | Hit | Damage |
  | --- | --- |
  | Breath | 1 (Fire, Water, Ice); 2 (Electric, which chains to the mechs); 3 (Convexity) |
  | Shot | 3 |
  | Headbash shockwave | 4 |
  | Fury | 20 in total, capped at **33% of the current phase's maximum**, so no Fury ends a phase |

- **Slicing:** the Void Beam may cut the four arena pylons only. These are
  **Sliceable** (H5). A pylon cut in phase 2 falls across the molten ring as a
  new crossing.
- **Floods:**
  - A Water Fury in phase 2 floods only the middle terrace, as one H3 region
    capped at the pool rims.
  - Gulp's fire train hisses out at the waterline.
  - The lower ring never floods.
- **Demons** fight the mechs but cannot hit Ripto during his evade window.
- **Dark Spyro** cannot be entered during the interlude, the finale cinematic
  or the outro.
- **Ice:** Ripto cannot be frozen. The mechs freeze for at most 1.5 s, once per
  10 s.

### 6.5 Polish list

1. **Arena geometry.**
   - Merge the middle terrace into one readable ring.
   - Widen the four dry crossings.
   - Remove dead corners.
   - Re-run the floor-continuity checks on every paving join, including the
     trapdoor lids.
2. **Lighting and materials.**
   - Per-phase colour: blue lower ring, amber terrace, violet sky.
   - Retexture the steel with authored PS1-style sheets, referenced from the
     Backrealms steel and Metropolis (67) panels.
3. **Telegraphs.**
   - One warning colour per source: red for Ripto, amber for the mechs, white
     for the arena.
   - Matching audio cues.
   - A tell of at least 30 ticks.
4. **Cinematics.**
   - Intro, interlude and outro on the existing cinematic camera-key system.
   - Script-camera style shots (look, focus, placement), as the retail scripts
     now use them.
   - Skippable after the first viewing.
5. **HUD.** Phase-aware boss bars; relay icons in phase 2 only.
6. **Difficulty.** An assist option (longer tells, more health pickups) and a
   hard option (overdrive always on).
7. **Audio.** Retail Ripto music stems per phase; mech motor loops.
8. **Performance.** Keep 60 Hz with every hazard active; cap particles.
9. **Failure flow.** Dying restarts the current phase from its own start, not
   the whole fight. Disabled relays stay disabled (existing).
10. **Hub.**
    - A sealed crown gate on the castle's north side opens once all four
      emblems are held.
    - H1 carries Spyro in, and the outro's Return Home brings him back to the
      gate.

### 6.6 Validation

Extend `spyro_ripto_tests` with:
- the per-element damage table and the Fury cap;
- the 20 s finale window;
- the pylon-slice crossing;
- terrace-only flooding;
- the phase-restart rule;
- the hub gate.

New captures:
- intro, interlude and outro;
- each phase's tells from a fixed camera;
- a full unassisted run recorded without the `--ripto-check` fixture.

*As of 15 October:*
- Every test above is in `spyro_ripto_tests`, which now has 25,923 checks. The hub gate is checked by `spyro_spring_realm_tests` and the workbench's `--world-door-check`.
- The cutscenes and phase lights are recorded with `--ripto-reel` (`data/validation/crownworks-polish/reel2-sheet.png`).
- Each phase's tells are filmed from one fixed camera with `--ripto-tells DIR`, which saves each attack 24 ticks into its warning (`data/validation/crownworks-polish/tells/tells-sheet.png`). It covers optic lance, scepter nova and shield rush; Mecha-Gulp charge, sweeping lasers, crown bombardment, and hammer and anvil; and sky talon.
- Still to film: an unassisted run. It needs a player, or a bot playing through real input, not the damage-injecting fixture.

*As of 25 October:*
- The bot (`spyro_ripto_autoplay`, `src/ripto_autoplay.cpp`) plays through ordinary input. It walks from the arrival to the duel island: route progress only moves forward, which fixed a dither at the south ramp.
- It leads its shots against Ripto's motion.
- Its zero hits exposed the element-targeting bug noted in `RIPTO_BOSS.md`.
- With that fixed, it beats Cyborg Ripto on foot and reaches Mecha-Gulp, landing 98 hits in six minutes. It does not yet survive the two-mech phase, where it loses its lives and the phase restarts.

---

### 6.7 Progress

| Item | State |
| --- | --- |
| 6.5.9 Failure flow: a lost life restarts the current phase | **Done.** `RiptoBoss::reset` advances back to the reached phase through the real transformations (`advance_to`). The retail arena keeps its full restart. |
| 6.4 Fury cap | **Done (12 October).** Fury damage is capped at 20 HP, and never more than a third of the phase's health. Every Fury source now counts toward the cap: rings, storm strikes, geysers, meteors and the shatter (`HitSource::Fury`). Before, the strength-2 rings and strikes escaped it. |
| 6.3 Relays in phase 2 only | **Done** for the boss modifiers (shield, repair, overdrive). Arena hazards still follow the relays in every phase. |
| 6.4 Per-element damage table | **Done (12 October).** Element resolution records each boss hit's damage from `boss_element_damage`, and Crownworks applies it in place of the mask table. The retail arena keeps its rules. Damage per hit: <ul><li>breath: 1 for Fire, Water and Ice, 2 for Electric, 3 for Convexity;</li><li>shot: 3;</li><li>headbash wave: 4;</li><li>Fury: 4.</li></ul> |
| 6.4 Electric chains to the other mech | **Done.** An Electric breath on Gulp chips Crush by 1, and one on Crush chips Gulp. |
| 6.4 Ice and the mechs | **Done.** Ice freezes a mech for at most 1.5 s (90 ticks), once per 10 s. A frozen mech holds its attack and does not animate; Gulp's rider holds with him. Ripto and the bird are never frozen. |
| 6.4 Demons and Dark Spyro | **Done.** Demon hits miss Ripto while he evades. A Convexity Fury does not enter Dark Spyro, or summon its demons, during the interlude, the finale or the outro (`RiptoBoss::blocks_dark`). |
| 6.3 Fury finale | **Done.** Knocking the bird down exposes the crown core for 20 s, with Ripto clinging to it. Ordinary hits do nothing there. Any one Fury ends the fight, with a burst in that element's colour. If the player owns no tier-3 element, a powered hit ends it instead. A missed window sends Ripto back into the air at a quarter health. |
| 6.5.3 Telegraphs | **Done** for colour: red for Ripto (optic lance, sky-talon path), amber for the mechs (lasers, hammer and anvil, bombardment, mortars), white for the arena (trapdoor, vent and jet warnings, and the crown core). Every tell lasts at least 30 ticks. Audio cues are still to come. |
| 6.5.5 HUD | **Done.** Bars and labels follow the phase: <ul><li>phase number;</li><li>a red Ripto bar, then amber Gulp and blue Crush, then a violet bird;</li><li>a white crown-core countdown in the finale;</li><li>"(FROZEN)" labels on frozen mechs;</li><li>relay icons in the two-mech phase only, lit while active, "BROKEN" once destroyed.</li></ul> |
| 6.5.6 Difficulty | **Done.** Set in the settings panel (`Crownworks difficulty`) or with `--boss-difficulty normal\|assist\|hard`; saved with the cheats. <ul><li>Assist stretches every attack's warning by half and refills Sparx at each phase.</li><li>Hard keeps the overdrive attack rhythm and vent cycle in every phase.</li></ul> |
| Fury rework (14 October) | **Compatible.** The reworked Fury fires up to three sphere blasts; each is tagged `HitSource::Fury`, so the 20 HP / one-third cap above still bounds its damage. Its gear-up camera now avoids terrain and drawn objects, which matters among the arena's pylons and mech bodies. `ripto_boss` passes on the reworked build. |
| 6.4 Terrace flood | **Done.** A Water Fury in the Crownworks floods only the middle terrace and its pools, to 4.9, and never the lower ring or the crown walk (`element_water.cpp`, `crownworks_terrace`). Gulp's fire shots hiss out at its waterline (`RiptoBoss::doused`). `spyro_ripto_tests` checks all three tiers. |
| 6.5.4 Cinematics | **Done (15 October).** Intro, mech summon, flight and outro on the scripted camera, with letterbox and captions. Each holds the arena (only the transformation or death it shows runs on) and is skippable once seen. Phase restarts never replay them. Settings option and `--no-boss-cinematics`. Code: `src/ripto_director.inl`; see [RIPTO_BOSS.md](RIPTO_BOSS.md). |
| 6.5.2 Lighting | **Done (15 October)** for per-phase colour: a shader grade on the Crownworks terrain and sky only. Blue lower ring on foot, amber terrace for the mechs, violet sky for the bird, a white crown-core light in the finale, calm once he falls. It eases over 2 s. **Steel retexture done:**
<ul><li>the machinery used Autumn Plains' ribbed conduit (33/11): relay and service housings, control pedestals, crusher heads, jet grates and ladder rails;</li><li>it now uses an original 64×64, 16-colour PS1-style ribbed steel tile, with riveted plates and its palette sampled from the Backrealms armour steel (`tools/author_crown_steel.py`);</li><li>it is swapped in through the world's `TILE_OVERRIDES`, so other levels keep the retail tile;</li><li>captures are in `previews/engine/steel-sheet.png`.</li></ul> |
| 6.5.7 Audio | **Done (15 October).** Music follows the phase (Ripto's arena theme; Gulp's Overlook for the mechs; silence after the outro's shattering), and each live mech holds Mecha-Gulp's retail servo loop. Retail Spyro 2 has no separate stems, so whole retail boss tracks stand in for them. |
| 6.4 Pylon slicing | **Done (15 October).** Four tall panels on the outer lower floor's diagonals are the only geometry the Void Beam may cut in the Crownworks. The world's `SLICEABLE 1` section is the new H5 *Sliceable* volume: one box per pylon, with a fall direction. A cut is level and comes away whole. It turns about the stump edge under gravity until it meets the floor, so it lies across the molten ring from its stump (about 1.7 high) to the island (about 0.6). `spyro_ripto_tests` checks: the floor and walls refuse the beam; a cut pylon lands across radius 12.6 to 15.8; Spyro jumps onto it from the island, runs over the lava and reaches the outer floor. Worlds without a `SLICEABLE` section still cut anywhere. |
| 6.5.1 Geometry | **Crossings done (15 October).** The north and south dry crossings were 4.8 × 4.8 boxes with dead lava corners against the ring's curves. They are now ring sectors (radius 11.6 to 16.4) about 7 units wide, flush with both curves, with six bordered floor slabs each and gold edging on both sides. They are checked by walking across at the centre and 2.9 off the axis, and filmed overhead, oblique, grazing and reversed (`previews/engine/crossings-sheet.png`). `tools/crownworks_polish.py` applies the crossings and pylons to the pre-polish world; the `ARENA` mesh ranges are unchanged. The middle terrace was already one continuous ring, so it is unchanged. The Blender master carries the same changes (`source/polish_master.py`, plan render `previews/plan-polish.png`). |
| 6.5.10 Hub | **Done (15 October)** with H1. The Spring Realm's crown gate (`stage_crown_gate.py`), on the castle's north side, is a world door (custom door mode 2) to the Crownworks, with its night and boss flags. It is sealed until four expansion emblems are held (unlock-travel opens it). The Crownworks' Return Home goes back out in front of the gate. `--world-door-check` makes the round trip in the workbench (17 assertions), and `spyro_spring_realm_tests` checks the gate sealed and opened. Emblems are session-local until H2. |
| 6.5.8 Performance | **Measured (15 October)** with a real-time (`--paced`) 2700-frame `--ripto-reel`: all three phases, every arena hazard, the four cutscenes, the phase light and music changes. The fight itself costs a mean 3.6 ms of simulation and 5.4 ms of rendering per frame, inside the 16.7 ms budget. On this machine the workbench's own ImGui panels (9.7 ms) and present (7.6 ms) bring the total to a 33 ms mean. Idle Summer Forest measures the same 33 ms mean (p95 164 ms) under the same settings, so the overhead is the workbench's, not the arena's. Boss effects are capped: 96 shots, 96 flashes that age out after 24 ticks, a 28-point dive trail. Reports: `data/validation/crownworks-polish/perf-*.json`. |

`spyro_ripto_tests` checks each done item:
- a lethal hit in the two-mech phase restarts it with Gulp at 80 and Crush at 60;
- 20 Fury hits take exactly 20 of Cyborg Ripto's 60 HP;
- the shield relay leaves phase-one damage whole.
- the element table: each source's damage, and elemental hits of 1-4 HP landing as recorded;
- the Ice freeze, its 1.5 s length and 10 s cooldown, and the Electric chain from Gulp to Crush;
- the finale:
  - an ordinary hit does not end it;
  - a missed window returns the bird at 20 HP;
  - a Fury ends the fight;
  - Dark Spyro is blocked throughout;
- difficulty: assist stretches the optic lance's warning by half; hard reaches Ripto's first attack sooner.

On 12 October 2026: 25,881 checks passed.

## 7. Build order

1. **Hub prerequisites:**
   - H1, then H2 (one save block);
   - the Orrery Hall portal;
   - the Crownworks gate in the Spring Realm.
2. **Crownworks:** gameplay rework (6.3–6.4), then polish (6.5). It proves H3
   and H5's Sliceable on a level that already exists.
3. **Tidewell Cisterns:** Water has the most finished systems; it needs H3 and
   the SluiceGate.
4. **Cinderfrost Fault:** freezing and skating already work; it needs WeakCrust
   and the re-melt timers.
5. **Stormglass Spires:** needs the Conductor, rail homing and wind volumes.
6. **The Hollow Orrery last:** after slicing on authored meshes is proven on the
   Crownworks pylons.

Each level goes through M0–M3 (section 1) before the next level's M1 starts.
Blockouts (M0) may run ahead.
