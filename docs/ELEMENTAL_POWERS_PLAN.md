# Elemental powers, Convexity demons and Dark Spyro — design plan

Planned 9 October 2026. Status: **implemented on 9 October 2026**, with the
follow-up tweaks of the same day and these 10 October changes:

- **Fast recharge.** Breath energy refills at 48/s after an 18-tick pause.
  Fury fills at 5/s, plus 30 per defeat and 1 per gem.
- **Furies.** Each is longer and staged for its own element:
  - Fire: rings and meteors;
  - Water: geysers, then a level flood that drains;
  - Ice: frost and spikes, and Glacier freezes every body of water;
  - Electricity: blackout and a lightning storm;
  - Convexity: Dark Spyro with demons.
- **Swimmable water.** Water headbash pockets drain.
- **Crates.** Every breath breaks every container. Upgraded Fire (tier 2+) also
  breaks the C9 metal-capped vases that the tier-1 retail flame only heats.
- **Convexity cuts.** Slices fall with gravity and keep their collision.
  - **Shattering (later on 10 October).** A cut breaks into 4–8 shards along
    Voronoi planes. Each shard is made solid: a back face plus dark fracture walls
    on its open edges.
    - Pieces crumble off one by one, nearest the beam first.
    - Friction and rolling resistance settle them instead of letting them slide.
    - They lift straight up out of floors and keep apart from each other.
    - A sweep stops them passing through walls.
    - A piece still airborne at the 10-second limit drops to the floor rather
      than freezing in mid-air.
  - **Packed ground.** When a cut is mostly walkable floor, the hole is filled
    with a recessed copy of the removed surface plus rim walls, darker like
    packed earth. The level never opens to the void, and a depression as deep
    as the shards are thick marks the cut.
  - **Walls and structures** (towers, cliffs) are capped as before and get no
    infill, so they keep a real opening.
  - **Ground struck below the mouth** is lifted out as a disc: a level cut just
    under the surface.
  - **Saves.** Each shard's landed pose is saved (S2SAVE 19). Older saves
    replay their single-slab cuts unchanged.
  - **Checks:** `tests/shockwave_tests.cpp` (`convexity_slice`,
    `convexity_ground`).
  - **Captures:** `data/validation/elements/shatter-20261010/`
    (`--element-replay slice` and `slice-ground`).
- **Ice** freezes any body of water for Winter Tundra-style skating.
- **Captures.** The element captures' headbash replays now learn the headbash
  (purchase 6) first.

The 10 October captures are in `data/validation/elements/*-20261010.png`. The
tests are in `tests/element_tests.cpp` (931 checks).
Inspired by The Legend of Spyro trilogy (element breaths, shots, Furies,
Convexity and Dark Spyro), presented in Spyro 2's PlayStation art language.
This is new PC gameplay. It does not claim to recreate Legend of Spyro's
combat or any retail Spyro 2 ability.

## 1. Ground rules

- **Opt-in, like every existing upgrade.** With upgrade progression disabled,
  gameplay, attack masks, saves and replays must stay byte-for-byte equivalent
  to the current build. Retail gate powers (Superflame, Superfreeze,
  Invincibility, Superfly, Supercharge, the green bomb) keep priority while their
  timers or cheats are active.
- **PS1 presentation only.** Use low-poly meshes, Gouraud vertex colours and
  4bpp/8bpp CLUT textures from 256×256 tiles. Use point sampling, the four
  retail blend equations (half, additive, **subtractive**, quarter-additive),
  and 30 Hz script updates interpolated at 60 Hz. No bloom, shader lighting or
  post-processing. Glows are additive triangle fans and lines, as used by Sparx's
  glow and particles.
- **Retail content stays read-only.** New art ships as PC assets alongside the
  executable, like `data/expansions/backrealms/armor-steel.rgba` and the
  embedded upgrade atlas. Retail colours, palettes and models are read from the
  mounted copy and recoloured at load time, never redistributed.
- **Budgets are hard caps**, enforced in code and covered by tests (section 9).
- Install the rebuilt main `build/Release/spyro_workbench.exe` before every
  gameplay test or capture (AGENTS.md).

## 2. Current baseline this builds on

| Area | What exists now | Where |
| --- | --- | --- |
| Fire | Retail eight-stream flame (`flame_attack` 0x10000, 30 Hz collision script); Superflame / green bomb projectiles, impacts and particles; Seek / Split upgrade tiers | `object_interaction.cpp`, `flame.cpp`, `powerups.cpp:165-529` |
| Ice | **Retail Superfreeze already exists** (gate kind 7, Cloud Temples): `Powerups::ice` timer, `frozen_ticks=300`, blue flame recolour, `IceShell` visual using model `0x271` in slot 61 only | `powerups.cpp:133,325-336,523-528`, `renderer.cpp:610-613` |
| Damage | Per-object `attack_flags` mailbox written through `ObjectInteractions::attack_sphere`; `enemy_accepts_attack` applies a fixed mask per `EnemyKind`. A hit defeats an ordinary enemy; there is no HP. Frozen enemies skip AI. **Knockback is computed away from Spyro's origin, not from the attacker.** Ripto uses HP values per flag. | `enemy.cpp:240-310`, `ripto.cpp:267`, `ripto_crush.inl:51` |
| Headbash | Landing applies a **0.35-unit** `headbash|charge` sphere, then `HeadbashTerrain::impact`. Crater/Lava carving has a visual-only shock ring; lava contact damages. There are 32 crater stamps; worst carve is 10.16 ms. `impact` returns early on protected/water/budget cases. | `aerial.cpp:111-122`, `headbash.cpp:270-314`, `HEADBASH_APOCALYPSE.md` |
| Upgrades | `Upgrade` enum (8, including Roll); `UpgradeProgress` tiers; 8 HUD icons; 384×64 sprite atlas (6 drawn sprites); purchase page | `extensions.hpp/.cpp`, `upgrade_hud.hpp`, `upgrade_art.cpp`, `desktop_upgrades.inl` |
| Spyro look | Per-face vertex colours plus a paletted atlas built from (page, CLUT) tiles; `spyro_tint` lerp; Backrealms armour precedent: landmarks by vertex index (horns 204–217), separate material pass after posing; Ripto's optic follows a specific eye quad | `spyro_model.cpp:95-150`, `renderer.cpp:552-598`, `renderer_spyro_armor.inl`, `RIPTO_BOSS.md` |
| Actors | **No custom animated model format.** Every actor, including Cyborg Ripto, uses retail models. `import_object_models` can already copy banks and atlas tiles across scenes. Runtime effects use `RetailObject` with `runtime_spawned`. | `objects.hpp`, `objects.cpp:352`, `powerup_import.cpp` |
| Sparx | Follow, gem flight and butterfly feeding (class `10`, extra-life fly `D3`) with retail sounds | `companion.hpp`, `SPARX_COMPANION.md` |
| Saves / online | `S2SAVE 15`; online `appearance` is validated as `<3` in both client and relay | `savegame.cpp:263,275`, `multiplayer.cpp:13`, `tools/online_relay.py:30` |
| Input | Used: WASD/arrows, Shift, Space/X, F, B, H, Z/V, G, J, E, R, C, T, Tab, 1–3 (dialogue only), Enter, Esc. **Free during play: Q, Y, U, I, O, P, K, L, N, M, 4–9.** The gamepad has no breath button (only F) and leaves D-pad, Square and L2 unused in play. | `desktop.cpp:1063-1098` |

## 3. Core element framework (shared by everything below)

### 3.1 Data

```text
enum class Element { Fire, Water, Ice, Electric, Convexity };   // Earth reserved
struct ElementKit { unsigned tier{}; };    // 0 locked, 1 breath, 2 shot, 3 shockwave+fury
struct ElementProgress {
    std::array<ElementKit,5> kits; Element equipped=Element::Fire;
    float energy=100, fury=0; bool dark_form_cosmetic=false;
};
```

`ElementProgress` lives in `UpgradeProgress` and is saved. Fire tier 1 is the
retail flame, so it is always owned.

### 3.2 Abilities per element (Legend of Spyro structure)

| Slot | Trigger (proposed) | Cost |
| --- | --- | --- |
| Breath | **F** / pad **Circle (B)**, the retail flame button, held (Square is the retail charge; see [CONTROLLER.md](CONTROLLER.md)) | Energy per second; Fire is free (retail parity) |
| Shot | **Q** / pad **R2** | Fixed energy |
| Headbash shockwave | Existing headbash controls (section 5) | Free; per-wave cooldown |
| Fury | **L** / pad **L2**, when the Fury meter is full | Whole Fury meter |
| Select element | **1–5** outside dialogue (dialogue keeps 1–3) / pad **D-pad left/right** | — |

Melee and supercharge take on the equipped element as an optional tier-3 bonus.
For example, the third melee strike adds a small elemental burst, and supercharge
leaves a frost, spray, spark or rift trail.

### 3.3 Energy and Fury

- Energy is 0–100 and regenerates after a short idle delay. Breath drains it;
  a shot spends a fixed amount. Starting values are 25/s breath, 20 per shot,
  15/s regeneration after 1 s, all tunable.
- Fury fills from defeats, with a smaller gain from gem collection. Decide
  whether gems count; campaign gem totals are never spent.
- The HUD reuses the retail power-meter construction from resident sprites 22–27,
  recoloured per element, plus a purple Fury strip and five 32×32 element icons
  next to the existing upgrade strip. It follows the current Upgrade HUD policy
  (Once / On use / Manual / Always / Off).

**Fury rework (14 October 2026).** A Fury is now a Legend-of-Spyro style release.
- **Gear-up.** It lasts up to three 50-tick stages (`fury_stage_ticks`). Holding the Fury input (**L** / L2) carries it into the next stage; a tap releases after the first.
  - Spyro levitates while the element spirals in, faster each stage.
  - Each stage cuts the camera to a new angle that drifts while it holds: a low front three-quarter, a tracking side profile, then a high orbit. This is ordinary gameplay, not a trailer mode.
  - Each new stage flares and shakes the screen.
  - The shots keep clear of walls and objects. On each cut the angle turns about Spyro, in steps up to half a turn, until nothing stands between the camera and Spyro and no terrain or drawn object (its bounding sphere) is within a unit of the lens. The angle keeps that turn while it drifts. Anything still in the way or crowding the lens eases the camera in toward Spyro. Pickups and gems do not count.
- **Release.** Up to three blast waves (one per stage reached), 18 ticks apart.
  - Each is a full sphere radiating through the air from Spyro's chest, drawn as a bright leading shell over a fainter one. Convexity's blasts use a violet front with its void dome just inside it; before this change they drew only the faint dome and an equator ring.
  - Radius 7.5 / 11.25 / 15, each slower to spread and harder-hitting.
  - A wide shot from behind the release point films them, then the gameplay camera returns.
- **Element sequences.** These now run longer:
  - Inferno: 8 s of meteors and a third ring.
  - Deluge: a 9 s flood rain.
  - Glacier: three spike rings, then the shatter at 6 s.
  - Storm: rings at 5 s and 7.5 s.
  - Rift: 5 s, with a second imp wave and a void sphere.
- **Interruptions.** An interrupted Fury clears its scripted camera.

### 3.4 Damage translation layer

The same refactor supports elements, shockwaves and demons.

1. Add a PC mailbox beside `attack_flags`:
   `ElementHit { Element element; unsigned strength; Vec3 source; unsigned status; }`.
   **`source` drives knockback**, replacing the Spyro-origin assumption.
2. One table maps (`Element`, strength) → the **retail-compatible mask** that
   each `EnemyKind`, container and `PowerTarget` already understands. Enemy AI,
   container rules and `power_target_accepts` stay unchanged. Starting table:

   | Element | Organic enemies | Metal (Robot, MetalIdol, GearGrinder) | Containers | Notes |
   | --- | --- | --- | --- | --- |
   | Water | Charge-equivalent knockback; defeats Fire Blubbers / Elephant Snails as their flame weakness | Push only | Wooden / vase | Soaks |
   | Ice | Freezes anything flame-vulnerable (existing `frozen_ticks` path) | Freezes | Shatters vases only | Frozen + charge / headbash / shockwave **shatters** |
   | Electric | Flame-equivalent | **Charge-equivalent: electricity beats machines** | Not containers | Stuns and chains |
   | Convexity | Strength 3, superflame-equivalent | Strength 3 | All except heavy chests | Bosses use HP table |

3. Status effects are new per-object timers with fixed caps: `soak_ticks`,
   `stun_ticks` (like frozen: AI paused, jitter pose), existing `frozen_ticks`,
   and `void_mark`. A defeat while marked spawns a demon (section 6).
4. Combos are deterministic and unit-tested:
   - Wet + Electric doubles chain radius.
   - Wet + Ice freezes instantly for 1.5× duration.
   - Fire thaws frozen enemies and dries soaked ones.
   - Water cools lava (section 5.3).
   - Frozen + any impact shatters into ice chips.
5. Ripto bosses receive an explicit per-element HP value, so a Fury cannot
   one-shot a boss phase. Damage can never bypass scripted boss invulnerability.

## 4. Element kits

All values are starting points for tuning.

### Fire (extends the current baseline)
- **Breath:** retail flame, unchanged.
- **Shot — Ember:** a short-range strength-1 fireball using the retail `1E3`
  projectile at a smaller scale. It stays deliberately weaker than retail
  Superflame, so Superflame gates remain special.
- **Shockwave:** a ring of flame tongues using the retail flame texture and
  `21D` spokes.
- **Fury — Inferno:** a widening fire ring plus a guaranteed Lava stamp when
  Lava mode is owned. It obeys the crater budget.

### Water
- **Breath — Torrent:** reuse the retail flame stream trajectories with a blue
  palette, longer reach and retail `27` bubble particles. Pushes enemies back,
  soaks them, extinguishes burning targets and cools lava.
- **Shot — Bubble Burst:** reuse the existing underwater Superflame projectile
  (`1F8`) and `1E1` / `211` impact meshes on land, using the same import path as
  the any-level cheats.
- **Shockwave — Tidal ring:** strong outward push; a splash ring from the
  water-burst meshes and bubbles; soaks everything in range.
- **Fury — Geyser field:** a ring of geysers lifts enemies and drops them.
  The geysers are short-lived additive columns, not real water volumes.
- Underwater, Water breath becomes a faster swim burst. This is optional.

### Ice
- **Breath — Frost:** generalise retail Superfreeze. Use the existing
  `frozen_ticks` path for every flame-susceptible enemy, the blue flame recolour
  already in `renderer.cpp`, and the retail `271` ice shell imported into every
  scene, rather than slot 61 only.
- **Shot — Ice Shards:** a fan of three low-poly PC crystal shards that pierce
  one target.
- **Shockwave — Frost bloom:** freezes in range; a ring of 8–12 low-poly ice
  spikes rises from the floor, then sinks; a temporary frost tint on the floor.
  The tint uses a vertex-colour patch, not a carve.
- **Fury — Glacier:** freezes everything on screen, then shatters frozen targets
  on release.
- **Stretch:** freeze water surfaces into temporary walkable floes, using the
  append-and-disable triangle approach already proven by destruction.

### Electricity
- **Breath — Arc:** a jagged bolt drawn as procedural additive Gouraud line
  strips, regenerated every 30 Hz step. It chains to at most 3 extra targets
  within range and line of sight.
- **Shot — Spark Orb:** a slow homing orb that zaps nearby enemies as it passes.
  It reuses the Seek homing code from the Superflame upgrade.
- **Shockwave — Thunder ring:** stuns everything; a vertical bolt strikes every
  enemy within the radius.
- **Fury — Storm:** repeated bolt strikes across the visible area for 4 s.
- Optional: power Breeze Harbor or Robotica machinery, only where a mapped
  retail script already exposes an activation path.

### Convexity (dark aether)
- **Breath — Void Beam:** a straight beam with a **subtractive** outer ribbon
  and additive white-violet core. It pierces and disintegrates defeated enemies
  into black flakes, then marks survivors with `void_mark`.
- **Shot — Rift Orb:** pulls nearby enemies inward, then collapses.
- **Shockwave — Rift eruption:** an implosion pull followed by an outward blast,
  with **demons erupting** around the ring (section 6).
- **Fury — Dark Spyro** (section 7).
- **Unlock:** proposed to require all four other elements at tier ≥1, plus a
  campaign milestone. This needs your decision.

## 5. Headbash shockwave and “headbashpocalypse” compatibility

### 5.1 New system, separate from terrain carving

Add `HeadbashShockwave` beside `HeadbashTerrain`. `Game::finish_aerial`
(`aerial.cpp:115-120`) always emits a wave after the base 0.35-unit hit, so the
wave works on every surface, including where carving is refused.

- **Wave model:** the radius expands from 0 to the element radius over 24
  ticks. Each tick tests a **band** `[r_prev, r]`, so each target is hit **once
  per wave**. Targets need a vertical tolerance of ±0.8 units and line of sight
  from 0.3 units above the impact, so waves stop at walls and do not hit lower
  floors.
- **Knockback** uses the impact centre as `ElementHit::source`.
- **Element radii:** None 3.0, Fire 3.6, Ice 4.0, Water / Convexity 4.5,
  Electric 5.0.
- **Graded strength:** full damage inside 50% radius, status-only outside. This
  keeps a basic headbash from becoming a room clear.
- **Concurrency:** at most 2 waves. A new wave refreshes the oldest; repeated
  headbashes never stack unbounded particles.

### 5.2 Graceful interaction with Crater / Lava modes

1. **Terrain mode and element are independent.** Normal/Crater/Lava controls
   carving only. The element controls the wave. Every combination is valid.
2. **Carving refusal never suppresses the wave.** This includes protected
   supports, special surfaces, water edges, custom mechanism ranges, walls and
   the 32-stamp budget. The wave still fires and the player gets feedback.
3. **No second carve per headbash.** Element effects on a crater are cheap
   recolour/flag passes over existing `HeadbashLava` patches, never another
   10 ms carve.
4. **Lava + element:**
   - Water cools the lava patches in range into safe dark stone, with steam
     puffs and burning disabled.
   - Ice cools the lava as well and adds frost.
   - Electric makes the lava pulse brighter for the wave only.
   - Convexity tints the lava void-violet; demons may rise from it.
   - Fire refreshes the lava reveal.
5. **Saves:** `HeadbashStamp` gains `element` and `cooled`, so a reload rebuilds
   the same tint. Waves, spikes and steam are transient and never saved. Older
   stamps load as Fire / uncooled.
6. **Water, flight and dialogue:** behaviour is unchanged.
   `finish_aerial` already exits when swimming or flying, and dialogue or scene
   locks suppress the wave. An underwater **Water** “depth charge” is an
   optional extra.
7. **Bosses:** waves use the boss HP table; a wave cannot damage Ripto through
   a scripted invulnerable phase.
8. **Online:** local, matching the current crater policy. Remote peers see the
   wave only if an effect event is added later.

## 6. Convexity demons

### 6.1 Roster

Every demon is brand new, PS1-styled and PC-authored. All of them are summoned
allies. They never damage Spyro, Sparx, NPCs, gates, portals or scripted
objects.

| Demon | Model budget | Behaviour | Help it gives |
| --- | --- | --- | --- |
| **Rift Imp** (swarm of 3) | ≤140 tris, one 64×64 4bpp tile, clips: erupt / run / claw / sink | Runs at the nearest valid enemy; claws apply strength-2 Convexity | Breaks nearby baskets and vases; Sparx collects the gems with his existing flight |
| **Gloom Hound** (×1) | ≤220 tris | Pounces and pins an enemy, then drags it under | Fetches a loose gem out of reach and drops it beside Spyro for Sparx |
| **Hollow Wraith** (×1) | ≤160 tris; subtractive cloak faces | Hovers by Spyro and fires void bolts | **Intercepts enemy projectiles** and absorbs one hit (one `Game::hurt` call), protecting Spyro and Sparx's health |
| **Abyssal Maw** (Fury / headbash only) | ≤260 tris, no AI | A huge clawed jaw rises from the rift, closes on every valid enemy in radius, then sinks | Clears crowds, with boss exclusions |

Enemies defeated by demons have a capped chance to release a retail butterfly
(class `10`, imported if the scene lacks it). Sparx eats it through his **existing
retail feeding path**, restoring health. This gives demons a way to support
Sparx without inventing new healing rules.

### 6.2 Eruption sequence: “erupt from the surface”

1. Choose a spawn point: raycast down to a **static, non-special, non-water,
   non-portal floor** outside protected supports, using the same eligibility
   rules as headbash carving. If none exists, use a floating rift ring above the
   nearest valid point, or refund the summon.
2. **Crack** (8 ticks): an additive/subtractive star decal laid on sampled floor
   heights, plus textured chips from the destruction debris emitter that keep
   the floor's atlas UVs and colours. **No terrain is removed or carved.**
3. **Rise** (12 ticks): the demon plays its erupt clip, starting below the floor
   and clipped by a dark subtractive pool disc.
4. **Act** for its lifetime: Imp 12 s, Hound 15 s, Wraith 20 s.
5. **Sink** back through a new pool, with black flakes.

### 6.3 Rules and budgets

- Run AI at 30 Hz with the existing straight-line `walk()` / terrain-follow
  helpers in `enemy.cpp`. There is no navmesh. Demons stay within a 10-unit
  leash and re-erupt beside Spyro if stuck for 2 s.
- Target only objects with live `enemy` state or non-scenery `power_target`.
  Never target `scripted_contact`, NPC, gate, portal or boss-invulnerable objects.
- Caps: **6 active demons**, **2 eruptions per second**, **1 Maw** at a time.
  Summons beyond the cap refresh the oldest demon.
- Demons sink immediately on level change, portal travel, dialogue, cinematics,
  save and load. They are never saved. `runtime_spawned` IDs are recycled,
  following the projectile approach.
- Water contact makes a demon sink with steam. Demons never swim.

## 7. Dark Spyro

### 7.1 Appearance: PS1-authentic recolour of the real model
- **Palette swap, as the PS1 would do it:** for every Spyro atlas tile
  (page, CLUT), build an alternate CLUT at load time from the mounted colours.
  Indices, layout, STP and transparency bits stay unchanged.
  - Purple ramps become a near-black violet ramp.
  - Gold horns, spines, wing membranes and belly become ash grey or bone.
  - Luminance order is preserved so shading still reads.
- The same ramp remaps every `SpyroFace` vertex colour. Upload the result as a
  second Spyro texture beside `spyro_texture_`; the renderer selects it by form.
- **Glowing white eyes:**
  - Locate the eye faces with a probe that dumps head-partition textured faces
    and their tiles. Then fix the landmarks by vertex/face index, as the armour
    pass does for horns 204–217 and the Ripto optic does for its eye quad.
  - Validate the eye landmarks in all 41 banks and in both supercharge mesh
    variants. The main-model supercharge pose has more than 230 vertices.
  - Eye texels/vertex colours become flat near-white.
  - An **additive fan glow** at each eye centre follows the Sparx gold-glow
    construction.
  - **Eye-light trails:** short additive Gouraud ribbons, eight samples, the
    same deque approach as Superfly wing trails.
- **Aura:** sparse retail charge-smoke particles using the **subtractive**
  blend equation, so they darken the scene behind him, plus occasional violet
  additive sparks.

### 7.2 Gameplay
- **Convexity Fury** turns Spyro into Dark Spyro for 15 s:
  - Convexity breath costs no energy.
  - Headbash raises an Abyssal Maw.
  - Supercharge leaves a rift trail; defeats on it raise Imps.
  - Damage knocks Spyro back without removing health.
  - Afterward, 2 s of exhaustion stops element use and slows movement.
  - The timer pauses during dialogue and cinematics.
- **Cosmetic toggle:** *Cheats → Appearance → Dark Spyro*, independent of the
  supercharge appearance choice. It changes looks only.
- **Online:** add a separate `form` field (and `element`, for remote breath
  tint) to `OnlineState`. Do not overload `appearance`, which the client and
  relay both validate as `<3`. Bump the relay protocol and update
  `tests/test_online_relay.py`.

## 8. New PS1 asset pipeline (required for the demons)

1. **Format `.s2actor`:** keyframed vertex animation compatible with
   `ObjectModel` / `ObjectAnimation`.
   - At most **256 vertices per mesh**, matching the retail 8-bit animated face
     indices.
   - Triangles/quads with per-corner Gouraud colours and UVs into one 4bpp or
     8bpp CLUT tile.
   - Frame durations in VBlanks; damage spheres; attachment vertices for eyes
     and mouths.
2. **Blender exporter** (`tools/export_s2actor.py`) enforces those limits,
   quantises textures to 16-colour CLUTs and rejects non-finite or degenerate
   data.
3. **Loader** merges actors into `ObjectModels` under **PC-reserved type IDs
   `0x8000+`**. Retail types never exceed `0x2ff`. Atlas tiles are appended in
   the same way as `import_object_models`. Existing object rendering, animation
   sampling and interpolation then work unchanged.
4. **Art:**
   - Generate a reference and texture sheet for each demon, following the
     project's mandatory texture-sheet rule.
   - Model in Blender at the triangle budgets above.
   - Bake to the CLUT tile and review against retail moby screenshots before
     acceptance.
   - Element icons extend the `assets/ui/upgrades` pipeline, including prompts
     and the manifest.
   - If generation goes through Codex, run the desktop-bundled CLI with
     `-m gpt-6.1-sol -c model_reasoning_effort="max"` and verify the header.
5. **Procedural effect meshes**, generated in code with no asset file: ice
   spikes, ice shards, bolts, beams, crack decals and geyser columns.
6. **Audio:**
   - Default: reuse mounted retail effects with pitch/volume variation, such as
     bubbles, explosions and the Cloud Temples freeze sound.
   - Optional: new PS1-style 22 kHz samples converted to the native mixer path
     for demon voices and lightning.

## 9. Persistence, performance and testing

- **Save `S2SAVE 16`:** element tiers, equipped element, energy, Fury, the
  cosmetic Dark Spyro choice, and stamp `element`/`cooled`. Versions 1–15 stay
  readable; older saves load with elements locked. Active waves, demons, status
  timers and Dark Spyro remaining time are transient. Decide whether Dark Spyro
  time should persist, matching the precedent for power timers.
- **Per-tick caps:**
  - 64 projectiles and 2048 particles (existing caps)
  - 6 demons, 2 waves and 4 chain targets
  - 12 ice spikes per wave
  - 512 decal triangles in total
- **Cost targets:**
  - Wave and status updates below 0.3 ms
  - Demon AI below 0.5 ms for 6 demons
  - No carving outside the existing headbash path
- **Paced capture gates:** mean CPU work below 6 ms and p95 below 12 ms during
  a full Convexity Fury scene, reporting frames over 16.67 ms. The project's
  existing measurement limits still apply; no claim of locked 60 fps.
- **New suites:**
  - `spyro_element_tests`:
    - Translation table: every `EnemyKind` × element × strength, plus
      containers and power targets
    - Status combos and retail-gate precedence
    - A replay-hash check that elements-disabled play is unchanged
    - Energy/Fury maths and save round trips for v16 and v1–15
  - `spyro_shockwave_tests`:
    - Once-per-wave hits, line of sight and vertical band
    - Wave emitted on refused carves, including the full budget
    - Lava cooling without recarve, and a 41-bank sweep
  - `spyro_demon_tests`:
    - `.s2actor` validation and malformed rejection; spawn-surface eligibility
    - Never-target rules, caps, leash/despawn and level-change cleanup
  - D3D11 renderer checks:
    - Dark palette preserves indices and STP bits
    - Eye landmarks resolve in every bank and mesh variant
    - Subtractive/additive passes
  - HUD/menu integration checks, extending `--upgrade-hud-check`, plus relay
    tests for `form`/`element`
- **Native evidence for each element:**
  - Headless captures from front, oblique, overhead and reverse views, as
    required by AGENTS.md
  - A 60 fps video with sound from the installed executable
  - Paced performance reports under `data/validation/elements-*`

## 10. Phased roadmap

| Phase | Deliverable | Exit criteria |
| --- | --- | --- |
| **0 Foundations** | `Element`/`ElementProgress`, damage translation + attacker-source knockback, status timers, input remap including pad breath, element selector + energy HUD, S2SAVE 16 | Elements-disabled replay hash unchanged; translation table tests; save v1–16 round trips |
| **1 Shockwave** | `HeadbashShockwave` with all five elements (placeholder visuals) and the section 5.2 rules | Shockwave suite + 41-bank sweep; refused-carve and full-budget cases still wave |
| **2 Ice + Water** | Generalised Superfreeze, shards, frost bloom, torrent, bubble burst, tidal ring, lava cooling, combos | Element tests; captures in Glimmer and Cloud Temples, including Cloud Temples retail parity |
| **3 Electricity** | Arc chains, spark orb, thunder ring, stun, metal weakness | Robot/idol/grinder behaviour verified in Hurricos / Idol Springs |
| **4 Actor pipeline + Rift Imp** | `.s2actor` format, exporter, loader, first demon, eruption sequence | Loader/exporter tests; Imp art review against retail mobys; demon caps/perf |
| **5 Convexity + full roster** | Void beam, rift orb, rift shockwave, Hound / Wraith / Maw, Sparx aid | Demon suite; projectile interception; butterfly → Sparx healing via retail path |
| **6 Dark Spyro** | Palette swap, eye glow/trails, subtractive aura, Convexity Fury, cosmetic toggle, online `form` | Renderer checks across 41 banks and supercharge variants; relay tests; native captures |
| **7 Furies + polish** | Fire / Water / Ice / Electric Furies, audio, icons, element-tinted melee/supercharge, docs, trailer captures | Full paced performance pass; doc updates (this file → implementation record) |

Phases 2 and 3 can swap. Phase 4 can start in parallel with phase 2 because it
only touches the asset path.

## 11. Optional extras (not scheduled)

- **Earth:** boulder shot, quake shockwave that topples enemies, and crystal
  armour. This would complete the Legend of Spyro four.
- **Comet Dash:** a fire-element supercharge with a burning trail.
- **Wing Shield:** a short block on a tap of the glide button while grounded.
- **Dragon Time:** slow motion that halves non-player 30 Hz script updates; the
  60 Hz simulation stays fixed.
- **Elemental melee finisher:** combo hit three takes the element's shot
  behaviour.

## 12. Decisions needed before Phase 0

1. **Controls:** the proposed Q / L / 1–5 and pad Square / R2 / L2 / D-pad
   scheme, or your own.
2. **Progression:** element costs and tiers, and how Convexity unlocks. Options:
   all four elements, a campaign milestone such as Ripto defeated, or a points
   price.
3. **Dark Spyro:** Fury-only, cosmetic toggle as well, or both (proposed).
4. **Fury meter:** fill from defeats only, or from gems too.
5. **Demons in boss arenas:** allowed with chip damage, or disabled.
6. **Audio:** reused retail effects only, or new PS1-style samples.
7. **Earth:** include it as a sixth kit now, or leave it as an extra.
