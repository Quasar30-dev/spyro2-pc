# Original level scripts in the overlay interpreter

Since 9 October 2026, every retail campaign scene except Ripto's arena runs its
**original moby scripts**. Enemies, bosses and scripted objects that the
engine does not reconstruct natively are no longer static. They now run
inside an R3000 + GTE interpreter (`OverlayVm`, `src/overlay_vm.cpp`,
`src/gte.cpp`) against a faithful RAM image of the level.
`RetailScripts` (`src/retail_scripts.cpp`) bridges that RAM to the engine.

## Boot

`boot_retail_level` follows the retail loader:

1. Load the executable's text and set the level id (`80066f90`, from
   `retail_level_id`).
2. Load overlay 1 (WAD 1) at `8006d264` and run its hook installer `8006d960`.
   This sets the dispatcher (`800670f0`), particle (`800670fc`) and other hook
   pointers.
3. Load the scene child at `80200000` and run `8006fa98`. This relocates it
   and publishes the region, special-surface and collision tables
   (`800673d4`, `800673f4`, `800673fc`).
4. Load the model child at `80300000` and run `80070018` for every class in
   the `+40`/`+140` directory. This fills the class table `80068c94`, as
   `80070398` would.
5. Load the level overlay over overlay 1, then the gameplay child at
   `80400000`, and run the gameplay loader `8001c734`. It relocates the
   gameplay tables, runs the collision init `800231f4`, builds the 32×32 moby
   grid `80067174`, starts every moby (`80041438`), floors them (`8003ee3c`)
   and calls the dispatcher once.

The interpreter has 8 MB of RAM so level data never overlaps the 2 MB
retail map. GPU/SPU/CD/pad library code (`80050000–8005f000`) is guarded and
never runs, except the C-library `rand()` at `80059d2c`. Every campaign scene,
slots 15–69, boots and runs its dispatcher without faults
(`spyro_retail_script_tests`).

## Ownership

A moby is *script owned* unless the engine reconstructs it natively. The
native set (`native_retail_class`) covers gems, containers and fragments,
Sparx and his effects, butterflies, power gates, portal labels and level
systems. It also excludes any record referenced by an NPC, portal,
whirlwind, home transporter or `ScriptDefinition`, plus any record with a
native `enemy`, `boss_body`, `container`, `butterfly` or `powerup_gate`.
Native classes are unlinked from the moby grid and lose their collision bank
(`+08`), so the scripts' collision queries never see stale native positions.

Census at an idle spawn (`spyro_script_census data/disc 600`): 3–153 owned
mobys per scene, zero interpreter faults, about 0.4k–184k instructions per
30 Hz tick (Aquaria Towers is the heaviest).

## Each 30 Hz script tick

*Engine → RAM*
- Spyro's origin (`80069ff0`), heading (`8006a05c`, byte `80069ffe`), health
  (`8006a248`), invulnerability (`8006a24c`) and elapsed VBlanks (`80067018`, always 2).
- Movement mode `8006a048` and state `8006a040`, mapped from the engine's motion
  with the values set by the resident state entry `800351d8`: 0 standing,
  1 on foot, 2 jump, 3 fall, 4 charge, 5 glide, 6 flight, 7 hurt, 10/11/12 swim,
  0F headbash. Crush, for example, starts his fight once this mode leaves 0.
  Flight (6) is set by the level overlays' own state entries; their 9 is the
  attacking flight boost, which the PC flight lacks.
- Frames since Spyro last had floor under him (`8006a0b0`, from `8002cc88`).
- The moby Spyro collided with since the last tick (`8006a130`).
- Engine attack bits on owned objects are ORed into the moby's `+18` attack
  word. Engine flame points also query the moby spheres with radius `A0` and
  bit `10000`, as `8002f08c` does. This catches breath from inside an
  enemy's body, which the engine's mesh test misses.
- Objects the engine removed (elemental, demon or melee defeats) award their
  spirit through `8004c3d4` and retire through `8004132c`.

*Run* the overlay dispatcher. Its own `80040e94` call is replaced by a filtered
version that emits only owned, unfrozen mobys, with the retail activation
radius (`+4A`), the drawn flag (`+4D`, see below) and group lists (`800670e0`). The
animation clock `80041068` and all terrain, movement and collision helpers run
natively.

*RAM → engine*: class, position (interpolated over two 60 Hz frames),
rotation, state, size, material word `+54` and the animation bytes
`+3C..+42` (validated against the native bank). Spawned mobys after the
authored array become runtime objects. Scripted gems (class 1) and butterflies
(0x10/0xD3) are handed to the native gem and Sparx systems instead.

*Hurting Spyro*: the hurt word `8006a014` is cleared, scripts may OR bits
into it directly (Idol Springs 11D/126, Colossus 111/118, Scorch 1A3, …), and
Spyro's own sphere query (`8004055c` with radius `0x164`, as `8002c0f4`)
adds damage spheres. Mask `1F9` hurts, as `8002b0b8` does.

*Solid mobys*: during movement, the same query in mode 0 pushes Spyro out of
solid script mobys (bodies and other sphere-solid mobys). The push never goes
through terrain or straight down. Each query carries Spyro's live attack bits
(`8006a128`), so a charge or headbash registers on the moby he runs into.
Mobys whose bank has a triangle section are solid as part of the level's
collision (see "Doors, gates and terrain animation" below).

*Terrain animation*: after the dispatcher, the main loop's timeline `800225f8`
runs with 2 elapsed VBlanks. Scripted terrain groups follow their RAM records.

## Script cameras

The retail camera update (`800210e8` …) is not run. The scripts' requests run
natively, so their RAM writes stay exact. The interpreter observes each request,
then executes the original code (`OverlayVm::passthrough`):

- `8001f24c(eye, target)`: a mode-9 "look" shot;
- `8001f4a4(moby, …)`: a mode-10 focus on a moby;
- `8001f2e4(blend)`: ends a shot;
- `8001f524(origin, heading, …)`: mode 11, which places Spyro;
- `8001f1e4(param, mode)`: any other mode (arenas, fixed views: 12–27).

After each dispatcher tick, `RetailScripts::direct_camera` advances the camera
struct `80067ed0` the way that update does:

- the request is adopted with substate 1, which becomes 2 once the camera has
  "arrived" (12 script ticks);
- `0x80` (ending) blends back to the previous mode;
- a mode-11 placement moves Spyro to the requested origin and heading.

Look and focus shots are presented through the engine camera's
`scripted_view`. Magma Cone's Party Crashers runs end-to-end on this:
1. Accepting starts the host's look shot and wakes the 12 crashers.
2. The last defeat ends the shot (substate 2 → `8001f2e4`).
3. The host places Spyro in front of itself and opens its reward line.
4. The orb is awarded and saved.

## Robustness fixes (10 October 2026)

- **Reused spawn slots.** When a spawned moby retires, its engine mapping is
  released. Previously a stale mapping made the engine retire the next moby
  spawned into that slot, behind its owner's back. The owner's chain then became
  circular (Shady Oasis 186 → 187), the dispatcher spun until the instruction
  budget ran out, and after 9 faults the whole level's scripts were disabled.
  Dragon Shores (2a0) failed the same way.
- **`8004132c` guard.** The resident removal walks the moby's grid cell list
  until it finds the moby. An observer first checks, with a bounded walk, that
  the moby is in that list. If it is not, the observer unlinks it wherever it is
  and clears the cell index, so the original skips the endless walk.
- **Fault reports** include the PC and the last eight PCs.
- **`80041538` guard (12 October).** When a moby changes grid cell, the
  update walks its old cell's list (`+34`) to unlink it, and never ends if the
  moby is not there. Prodding every Metropolis moby in turn (the audit's
  interactables pass) left one out of its list; 9 budget faults followed, and
  the level's scripts were disabled. Before a cell change, an observer checks
  that the moby is in the list. If it is missing, the observer unlinks it
  wherever it is and pushes it onto that list, so the original code runs
  unchanged (`grid_repairs` counts these).
- **Text-less lines.** A conversation line without text (Cloud Temples'
  Chronos, the bridge's empty line 2) is finished through `8003a568` instead of
  aborted, so a next line, Zoe, rewards and the close still happen.
- **Script-owned speakers.** Speakers whose own script moves them and opens the
  conversation are script owned: Magma Cone's host 13b, and now Scorch's Bombo
  1a6. Bombo stays hidden until a powered hit (bit `100000`) arms his flag-tower
  bomb chase.

## Zoe and checkpoints (10 October 2026)

Class 110 is Zoe, the checkpoint fairy. The same class also covers the silent
"sparkle", "Talk" and "Fairy" checkpoints. Ghidra's decompile of Glimmer's case
(`overlay_016/class_110.c`, `80073d40`) shows that the class drives everything
itself:

- **State 0** waits for Spyro.
  - It requires a distance under `0x1000`, a height within `0x800` and a
    walking or swimming movement mode.
  - It ignores a Spyro who was already that close when it woke (`+49`) until he
    has moved more than `0x1800` away.
  - Within `0x6000` it spins in from size `7f` down to `20` (`+4f` is the
    moby's size).
- **Its state block** is at `param+18`. Byte 2 is the talk mode:
  - **0:** silent checkpoint;
  - **1:** talks when Spyro faces her and presses triangle (`80037da8`, then
    `8003a98c` with flag `8`, which accepts Spyro up to 800 units below her
    feet);
  - **2:** talks automatically.

  Talking calls `80015be4` and sets her "talked" bit.
- **State 1** is the zap. `8004c0c8(80067414, position, heading)` stores the
  checkpoint:
  - `+0` = 1;
  - `+24` = origin (floor height plus `164`);
  - `+30` = heading;
  - `+40` = a snapshot of the persistent list.

The engine used to treat class 110 as a native speaker. Its proximity test
needs Spyro at or above the speaker's feet, but Zoe floats about 1.5 above her
perch with no feet offset (`+38` = 0). So no Zoe in the game could be talked to
or set a checkpoint.

Class 110 is now script owned:
- it is removed from `native_retail_class` and `dialogue_class`;
- the existing bridge presents her conversations;
- after every dispatcher tick, `RetailScripts` watches `80067414`. When a Zoe
  stores a new checkpoint, `play_scene.spawn` and `Game::respawn_yaw` take it, so
  a lost life returns Spyro there;
- `respawn()` (level entry) clears it.

**Results** (`spyro_script_quests`, all 28 scenes):
- 121 of 153 speakers open (112 before).
- Of the 33 class-110 objects:
  - all 9 with talk mode 1 talk;
  - the 24 with mode 0 are silent checkpoints by design, and 22 of them stored
    their checkpoint during the visit.
- `tests/retail_script_tests.cpp` section 6 checks Glimmer's first Zoe end to
  end: script ownership, her conversation, the checkpoint, and the respawn
  after a lost life.
- `data/validation/retail-scripts/zoe-20261010/` has the native capture: Spyro
  walks in, she spins in and speaks, zaps, then says her second line.

The workbench replay `--attack approach` starts Spyro about 7 units away and
walks him up to the speaker, as a player would.

## Camera shakes, shot endings and spirit awards (10 October 2026)

Colossus 11a's sequence exposed three places where the engine did not do what
the retail camera update or game loop does. All three are general fixes:

- **Shakes.** Scripts request a shake with a strength (`80068058`) and a frame
  count (`8006805c`), and some wait for the count to reach zero.
  - The retail camera update counts it down; we do not run that update, so it
    never reached zero.
  - `direct_camera` now counts it down once per script tick and plays the shake
    on the engine camera.
- **Shot endings.** `8001f2e4(1)` writes substate `80`, and the camera update
  (`80020920`) then blends back in its arriving phase (`81`). The normal mode
  re-enters arriving (substate 1, `800213e8`) and settles (2).
  - The director used to write substate 0 when a shot ended, so scripts waiting
    on bit 0 hung.
  - It now follows `81`, then 1, then 2. Party Crashers still passes.
- **Spirit awards.** `8004c3d4` is observed (passthrough).
  - A moby whose own script awards its spirit counts in `Game::spirits()` even
    if it stays in the scene (`RetailObject::spirit_awarded`).
  - Before, only retired mobys counted.

Diagnostics:
- `SPYRO_TRACE_PC=addr[,addr…]` prints the interpreter registers (plus the
  class and state of the moby in `s4`/`a0`, and the return address) every time
  those instructions run, then runs them normally. It chains to any guard
  already on that address.
- `SPYRO_WATCH=address` reports every interpreter write to that byte, with the
  writing instruction and return address.

### Shady Oasis 187 list cycle (fixed 10 October 2026)

The 10 October engagement census showed Shady Oasis (slot 47) faulting in an
endless walk at `8007caf4` / `80081634`. Nine faults then disable the level's
scripts.

- **The list.** Class 187 mobys (thrown by 186) join a list on object 0 at
  `8007ca78`:
  - own `param+14` = the owner's `header+8` (the old head);
  - the owner's `header+8` = self;
  - the owner's count `+4` goes up by one.

  In state 99 a 187 unlinks itself, then retires through the shared removal at
  `80082d3c`.
- **How it broke.** The `SPYRO_WATCH` watchpoint on a 187's state byte shows
  that a hit class 233 resets the 187 it holds to state 0 at `80076f4c`, without
  that unlink. The 187 then joins again while still in the list, closing it into
  a cycle. Both walks follow that same `+14` chain.
- **The fix.** A guard on `8007ca78` (slot 47 only) unlinks a 187 that is
  already in the list before the original join runs. Nothing changes unless the
  list would otherwise become a cycle.

`tests/retail_script_tests.cpp` section 8 makes a joined 187 join again, as a hit
233 does. It faults without the guard and passes with it. Shady Oasis's census
now shows 0 faults.

## Chest gems and the level gem count (fixed 10 October 2026)

The new end-to-end audit (`spyro_level_completion`, below) found most levels
short of their gems. Idol Springs had 358 of 400 and Zephyr 277 of 400. Three
problems in how the engine and the scripts hand gems to each other caused it.

- **Gem riders.** Class 80 is a gem riding a host moby (`param[0]`), such as a
  chest (cb) or Zephyr's hopping ce (Ghidra `overlay_018/class_80.c`,
  `overlay_040/class_ce.c`):
  - it follows the host;
  - when the host's state byte gets bit `80`, it bursts. `80039334` launches a
    class-1 gem from the host to the rider's placed spot, and the rider removes
    itself.

  Native hosts never showed their removal to the scripts, because a negative
  state was never mirrored. Now a native moby the engine has removed reads `FE`,
  as `8004132c` leaves it.
- **Gems in flight.** A launched gem flies in state 2 with only its variant at
  `+49`; its value byte `+50` is written when it lands. The native hand-off
  read 0 there and dropped the gem. It now takes the value from the variant.
- **The level gem count.** `Game::level_gems()` credits released gems through
  a broken container's held gem, so script gems were never counted. Script gems
  (`RetailObject::script_gem`) now count once taken and are not "pending" in
  flight. Quest reward gems benefit too.

`tests/retail_script_tests.cpp` section 9 breaks Idol Springs' cb chest. The
scripts see `FE`, 7 gems burst out, and taking them adds the full 42.

## Doors, gates and terrain animation (fixed 12 October 2026)

Doors across the game ignored their switches, defeated enemies and payments.
Spyro could often walk straight through them. Original doors come in two
kinds, and the port handled neither.

**Moby doors.** A moby is solid when its collision bank (`+08`) has a
triangle section:
- header bits 16..22 hold the vertex count, and bits 24..31 the section's word
  count;
- each triangle word packs three 6-bit vertex indices (bits 24, 16 and 8) and a
  flag byte;
- bit 31 ends the list.

Spyro's terrain test `8003dc64` walks the moby grid as well as the scene's
collision. It transforms each solid moby's vertices with these steps:
1. take the current animation frame (`+3C` clip, `+3E` frame);
2. blend it with the next frame (`+3D`/`+3F`) by `80062868[+40]`, with GTE INTPL;
3. rotate by the matrix at `+20..+33` (MVMVA);
4. shift by the clip's `+02` and place at `+0C`.

The sphere query `8004055c` skips these mobys unless header bit 23 is set, so
the port, which only ran that query, let Spyro through 118 solid script mobys
in 18 scenes (`spyro_solid_census`).

`RetailScripts::build_solids` now repeats that transform after every script
tick into `Level::object_triangles`. `Level::raycast`, `ground_support` and
`resolve_sphere` test them, so Spyro, the camera and floor checks all collide
with them. Hits report `Level::object_triangle | index`. Contacts are fed back
as `8003dc64` and `8002cc88` would:
- Spyro's attack bits are ORed into the moby's `+18`;
- flagged triangles (flag 1..7F) hurt through `80064668`;
- the moby he stands on is written to `8006a134` (switch plates, lifts, scales);
- standing on a moving, turning or animating solid carries him with the
  touched point.

**Terrain doors.** Most levels move parts of the scene itself: gates, doors,
bridges, lifts and lights. The level's seven animation sections hold
keyframed near vertices, colours and collision. `8001c734` stores them in the
RAM directories at `800681d4` out of order: section *k* goes to directory
`{0,1,4,2,6,5,3}[k]`.

Scripts drive the groups:
- `8002365c(group, keep, set)` changes a group's flags; `0xFC, 0` starts it.
- `80023710(group, node, skip-mask)` sets its timeline node.
- The main loop's `800225f8` advances every record.

Only Glimmer's bridge and Breeze Harbor were decoded. Elsewhere the animated
collision never got its authored pose, and nothing ever moved.

All retail scenes now decode their groups: 453 morph tracks, 0 records
skipped. Two step forms that only scripted scenes use are now accepted:
- **Zero-length steps** are passed through.
- **Step byte 7** sets the region's `0x4000` "not drawn" flag at that node
  (`80024168`). An opened door's region is hidden and its collision disabled.

Each script tick runs `800225f8(2)` in the interpreter and adopts each
record's flags, node and frames left, then poses the vertices and collision.

Debugging switches:
- `SPYRO_STATIC_MORPHS=1` keeps the collision table's static pose, the
  pre-fix behaviour, for comparison.
- `SPYRO_MORPH_DEBUG=1` reports records the decoder skips.

**Doors that native classes open.** Some handlers never run in the
interpreter, so their terrain commands are issued for them (`native_doors`):
- **Moneybags (77).** `+28` group, `+2C` camera record, `+3E` open node,
  `+48` purchase.
  - Paid at load: the group is set to its open node; subtype 4 also sets
    group 9 to node 1C.
  - Paid during the visit: the look shot plays, the group starts after
    30 ticks, and the shot ends 90 ticks later.
- **Orb doors 16D (`+3C`) and 1B6 (`+38`).** The group starts when the engine
  opens the gated portal, including at load. In Winter Tundra the Professor's
  arches fill with their portal panel.
- **Boss doors 16D with no portal of their own.** The original handler opens
  them after a conversation, by checking `+02`, which its talk logic sets from
  the talisman count `80067108` or orb count `8006702c`. The engine presents
  that NPC natively, so the door opens when the same requirement holds:
  - Summer Forest subtype 5 (Elora, on to Crush): 6 talismans.
  - Autumn Plains subtype 13 (on to Gulp): 14 talismans.
  - Winter Tundra subtype 55: 40 orbs.

  Before today, the static collision table left these doorways passable. With
  their authored closed pose and nothing to open them, they would have blocked
  progression.

The overlay scan's other native hits, Skelos C9 and Fracture Hills CA, are not
real. Those classes' dispatch ranges span other classes' code, and the callers
are scripts.

**Switch plates and charges.** Before this fix, nothing ever wrote the moby
Spyro stands on (`8006a134`), so every stand-on mechanism was dead:
- Idol Springs' 1E5 tile puzzle;
- Hurricos 4D;
- Sunny Beach BC and 1D8;
- Magma Cone class 7, which moves a terrain group;
- Breeze Harbor 176;
- Robotica Farms 25A/25D;
- Dragon Shores 293/2CF.

Charges also missed switches. Each movement query passed attack bits 0, and by
the next script tick Spyro had already bonked into ChargeRecoil. The queries
now carry his live attack word (`8006a128`), as `8002c0f4` does.

**Example.** Summer Forest's corridor gates are a toggle. Charging switch 145
(record 166) raises gate 2 (z 59) and lowers gate 3 (z 51); the next hit swaps
them back. `data/validation/doors-20261012/15-switch-gates-sheet.png` shows the
gate rising. `retail_script_tests`
sections 10–12 check:
- the gates block and pass Spyro before and after the switch;
- paid Moneybags doors open at load;
- Elora's door stays shut with 5 talismans and opens with 6;
- winning Colossus' 11A sequence opens the gate beside its arena (group 2,
  section 7);
- Colossus 21 and Hurricos 4D moby doors stop Spyro.

## The drawn flag `+4D` (fixed 12 October 2026)

The bridge used to treat a moby whose `+4D` was set at load as drawn for the
rest of the visit. The retail renderer does not keep it. `80043944` clears
`+4D` on every live moby each frame, then sets it on the ones it draws. It
draws a moby when all of these hold:
- the moby's draw distance `+4C` (signed, in world units) is positive;
- the moby is inside that cube around the camera (`+4D & 3` adds a unit of
  hysteresis);
- it lies in front of the camera, within the view.

Scripts hide a moby by zeroing `+4C`. The bridge now evaluates exactly that
each tick against the engine camera, including scripted shots.

What this fixed:
- **NPCs that move between places.** They swap only while undrawn, so they
  never did.
  - Idol Springs' Foreman Bud (class 124, subtype 3, record 268) sends Spyro to
    the pond once the first tile puzzle is solved.
  - He hides and reveals his pond copy (record 267) once he is undrawn while
    Spyro swims underwater (movement mode 11/12): `8007f2c0`.
  - That copy opens the second challenge, "This idol says he's hungry…". It
    is the pond idol (404, record 224) eating the class 19F fish, and terrain
    group 8 is the idol's mouth, not a door.
- **Magma Cone's class CF gem block.** It now spawns its gem: 400/400 (was
  385).
- **Mobys stay inactive until seen or approached,** as in retail. A Cloud
  Temples class 1F lift no longer consumes the shared one-shot flag `8006711C`
  while Spyro is elsewhere, and a moby's script still initialises its own
  collision bank on its first update.

**Idol Springs' tile puzzle.** Standing on a 1E5 tile (`8006a134`) toggles
its lit bit (`param+0`) and its two linked tiles' (`param+4`/`+8`). Each tile
counts `+49` up from its neighbour while all three are lit; the ring is solved
at 99. The census probe `LIGHTS_OUT=1` solves it by breadth-first search in 4
steps (166, 167, 176, 185). Bud's script then reads the solved ring and sets
the progress byte `8006abf8`. Unsolved, he gives the brief: "The first puzzle
is to turn all these blocks yellow."

Tests that teleport Spyro now move the camera with him, as walking would:
- Zoe materialises only when approached from beyond six units.
- Colossus' class 21 doors rise into place when they first activate.

`retail_script_tests` section 14 checks the Idol Springs chain:
1. the four steps light all eight tiles;
2. Bud acknowledges the solve and sets `8006abf8`;
3. standing by the pond leaves him in place;
4. diving into the pond moves him there.

### Hurricos' diodes, the power bolt and the frame counter (fixed 12 October 2026)

**The quest.** Eight class 35 sockets (records 180, 188–190, 192–195) sit at
the foot of Hurricos' generator towers. Eight class 34 diodes (178, 179,
182–186, 191) lie around them. The class 2F robots of moby group 1 juggle the
diodes and pull them back out of powered sockets.
- **A powered socket** (`params+0x10` = the diode) lights its tower: a colour
  group (11, 12, 17–22), its class 40 glow and four class 3B arcs (`+4C` 0 → 50).
- **The counter.** Class 5C (record 223) counts the powered sockets in group
  list 0 into `params+0x14`.
- **The power bolt.** Class 8B (record 257) is hidden (`+4C` = 0) until record
  223 counts 8 and `80039218(1)` finds no group 1 robot alive. It then:
  1. looks through its camera record (`8001f24c`);
  2. starts terrain group 13, the looping electricity (nodes 122–136);
  3. tells record 42 (`+49` = 1);
  4. stays lit (`+4C` = 0x20) with a looping buzz (state 3).
- **Robots.** A hit knocks a robot down: a charge sets state 2, a flame state
  3, and either drops its diode. It explodes and is removed (`800391f8`,
  `8004132c`) when its knockdown clip ends (`8006711c`). A robot the scripts
  keep hidden (`+4C` = 0) is never drawn, so it never animates. Robots
  249/250 stay hidden until their tower's socket is powered.

**Carrying (Spyro's head state).** `8002fc20` runs Spyro's head through the
jump table at `80010b0c`:

| `8006a15c` | Meaning | Code |
| --- | --- | --- |
| 0 | free; a flame press starts state 1 | `8002fcbc` |
| 1 | flaming | `80030120` |
| 2 | taking an object | `800302f0` |
| 3 | holding it | `80030410` |
| 4 | spitting it | `800305dc` |
| 5 | holding paused, in a state that cannot carry | `80030660` |
| 6, 7 | powered flame, underwater | |

- **Pickup.** A diode in state 0 posts `8006a1fc = 0x10000001` with itself in
  `8006a208`. It does this while Spyro faces it (within 16/256 of his heading),
  600–850 units away, no more than 256 units above it, with the head state 0.
  Spyro's next frame (`8002ffd4`) takes the request if his state allows a pickup
  (`80061371`, four bytes per state): head state 2, the moby in `8006a138`.
- **Holding.** After 16 frames the diode gets state 1, which hides it (`+4C` =
  0), and the head state is 3. Circle (pad `0x20`, or request bit `0x200`) in a
  state that may spit (`80061373`) starts state 4.
- **Spitting.** Four frames later the diode gets state 2. It leaves the mouth
  on an arc, simulates its landing (30 steps of `8003dcc0`) and homes in on the
  nearest socket within 1000/1000/1500 units of the landing point.
- **The head clips** come from `80066d54` by head state: 53 take, 45 hold and
  54 spit. The transitions come from the 8×8 table at `80061330`. The port's
  `SpyroSecondaryAnimator` already plays this table; the runtime now passes it
  the carry head state when the level's Spyro bank has those clips.
- **The port** (`RetailScripts::carry`, `src/retail_scripts.cpp`):
  - answers the request before each dispatcher tick, as Spyro's update runs
    before the mobys';
  - clears `8006a1fc` (copying it to `8006a200`, as `80031450` does);
  - holds the engine's flame and breath while Spyro carries (`carrying()`);
  - draws the carried moby at Spyro's posed mouth. The PlayStation draws
    nothing there; the PC shows what he holds.

**Draw distance `+4C` (all levels).** The retail moby renderer (`80043944`)
skips a moby whose `+4C` is not positive, and scripts hide mobys that way. The
port drew every script moby regardless. `RetailObject::script_undrawn` now
follows `+4C`; it only affects rendering, so collision and attacks still reach
the moby. The model-bearing classes this hides, each only while its script says
so:
- spare copies of moving NPCs: Zoe (110), Foreman Bud (124);
- Hurricos' power bolt (8B);
- Aquaria Towers A1;
- Sunny Beach B3 (21 of them);
- Scorch 1A6;
- Magma Cone 128 and 14B;
- Fracture Hills 6A;
- Gulp 199;
- Mystic Marsh 15, 1A and 179;
- Metropolis 252;
- Dragon Shores' finale mobys (292, 294, 2A0, 2A2, 2A3, 2BA, 2CF).

`spyro_solid_census UNDRAWN=60` lists them per level.

**Frames since load, `80067008` (all levels).** The gameplay loader
(`8001c734`) zeroes it and the main loop (`800159fc`) adds one per game frame.
Every level overlay reads it, 13 to 44 times each. Scripts compare it with 60
to finish an event silently while the level is still loading, instead of
filming it. The port never advanced it, so every such event took the silent
branch: Hurricos' bolt lit up without its shot or its electricity. The bridge
now adds one per script tick.

**Validation.**
- `retail_script_tests` section 15:
  - the counter runs and the unpowered bolt is undrawn;
  - robots 254/255 are defeated, and Spyro takes diode 178 (head 3, diode
    state 1, head clip 45, shown at his mouth);
  - Circle spits instead of flaming, and socket 188 takes it.
- `spyro_solid_census FETCH=auto` plays the whole quest:
  `tools/carry_quest.hpp`, shared with the workbench and the level audit.
- Workbench: `--attack fetch --interaction-record <diode> --socket-record
  <socket>` walks up to a diode, carries it and spits it. `--carry-quest` plays
  the rest of the quest unfilmed first.
- Footage: `data/validation/hurricos-diodes-20261012/`.

## Boss health HUD and Cloud Temples' dog doors (14 October 2026)

**`80066eec`, the level-entry counter.**
- The main loop (`8001d7b4`) counts it up during the load fade and sets it to -1 once gameplay runs.
- The port never ran that code, so it stayed non-negative, and every hook gated on it stayed off.
- The bridge now holds it at -1 each script tick.

**Crush (31) and Gulp (55) health HUD.**
- The loader installs two hooks: update at `80067154` and draw at `80067158`.
  - Crush: `8006fdd8` / `8006fe80`.
  - Gulp: `8006f0f0` / `8006f198`.
- Both hooks run only while `80066eec` is negative.
- The update sets the counter's `+1` to 3, which keeps it shown, and eases `80067674` toward the boss's health `80066fec` with `8005199c`.
- The draw shows:
  - the scene-local portrait frame (`80063660`, size 21) at (38,198);
  - its blinking eye frame (`80063690`+4i, size 18; over 5 frames for Crush, 3 for Gulp);
  - 7 (Crush) or 10 (Gulp) pips from x=40 every 20 at y=236, using sprites 13/14/15 of `800635f8`, with the empty ones drawn as 9/10/11 through palette 0x5960;
  - all of it lifted 40 by the counter's entry curve.
- The port mirrors this natively: `HudController::boss`, `BossHudAssets` and `draw_retail_boss_hud`.
- Damage:
  - Crush loses health to flame (bit 0x10000), as in the original.
  - Gulp's health lives at data `+0x4A`. Flame does not hurt him; his state 15 takes one health per hit from the items his feeder (199 → 19E) drops, chosen by phase (`8007792c`): bombs (196) that Spyro charges into him (`800745ec`, attack 0x20000) and items (197) he picks up and spits.

**Defeat and the level's end.**
- When Crush's death clip ends, overlay 30 sets the level's own talisman-table byte (`8006b264`+8, `80073060`) and calls `80019230(7)`. Gulp's overlay 54 calls `80019230(11)` (`800744e0`).
- `80019230(n)` is the resident "start cutscene" routine: game state 5, cutscene `800698f4`=n, level id `8006323c[n]`. It swaps 7 for 9 and 11 for 13 when the home was already visited (`8006b084`+level index). Scenes 74/77 are the outro cinematics; 9 and 13 continue in Autumn Plains (30) and Winter Tundra (50).
- The bridge observes `80019230` for 7 and 11 only. It does not run it, because the rewritten level id would change the checks of the scripts still running (class 1A4 tests level ids 26/46). The engine then:
  - records the talisman-table byte as the level's boss award (PC save `awards.boss`; the byte is restored on later visits);
  - starts a scripted departure (`start_scripted_travel`) to the home the original continues in, slot 33 or 57, arriving at the home's starting point.
- The cinematic itself is not played. Homes' own requests (transporters 5/9/13, 14) still run the original code, as before.
- `spyro_boss_fights data/disc` plays both fights with ordinary inputs and loads the destination as the workbench does:
  - Crush: flame to 0 health in 4074 ticks; outro 7; departure 240 ticks later; grounded in Autumn Plains.
  - Gulp: 10 bomb charges, 1 pickup and 2 spits bring him to 0 in 32624 ticks; outro 11; departure 316 ticks later; grounded in Winter Tundra.

**Cloud Temples' golden dog doors (class 1E).**
- A door ignores attacks. Its wizard (class 1F) opens it: the wizard's case sets the paired door's `+49` (`80076434`), and the door's own case then bursts it into nine class-255 fragments.
- PC addition: a green fire bomb's heavy-door bit (0x10000000) reaching a dog door sets that same `+49`, so the door opens through its original code.
- Footage: `output/spyro2-pc-trailer/gameplay/cloud-door.mp4`.

## End-to-end level audit

`spyro_level_completion <disc> [slot …]` (`tools/level_completion.cpp`) runs
every campaign level on the real runtime. Teleports stand in for walking. For
each level it:
1. defeats every spirit-carrying enemy, native or script owned, escalating
   flame, charge, headbash, superflame, supercharge and green bomb;
2. breaks every container;
3. opens every script gem holder (mobys holding a gem, and gem-rider hosts),
   following hosts that move between hits;
4. collects every gem;
5. checks the powerup gates.

It reports:
- both gem counters (the level's and Spyro's), unopened holders, undefeated
  classes and faults;
- since 12 October, which terrain door groups moved and which solid mobys
  moved or opened.

It plays as a completing player: paid services, 64 orbs, every talisman.
`INTERACT=1` also tries every other script moby: it flames, charges and
headbashes each one, and stands on solid ones. Results are in
`data/validation/level-completion/`.

## All-level pass (14 October 2026)

Fresh runs over every campaign scene on the current build. Results are in `data/validation/allpass-20261014/`.

- **Engagement census** (`engagement.txt`): no spirit carrier that never reacts to Spyro (no `ENEMY!`) and no faults. The three `NODIE!` classes are the documented retail behaviours below: Colossus 11A's area sequence, Crystal Glacier's group-defeated 50, and Robotica's respawning flyers 17A.
- **Completion audit** (`completion.txt`): every level's enemies, containers, gem holders and gates complete with zero faults, except:
  - the same Colossus 11A and Robotica 17A;
  - Dragon Shores' 10,000-gem gate;
  - Summer Forest, where Spyro's gem counter read 395 against the level's 400 (unchanged since the 10 October audit). **Traced and fixed:**
    - The bridge writes Spyro's counter into the scripts' gem total `800670cc` before each script tick. It reads back any drop as a payment (`8003a568` deducts a service's price there).
    - A conversation pauses the script ticks, so the total isn't rewritten during it. Spyro's first gem (5) was picked up just before one; when the conversation ended, the progress sync found the stale total 0 below his counter of 5 and took the gems as a payment.
    - Payments now count only what a script took off the value last written (`gems_written_`).
    - Summer Forest completes 400/400. The Moneybags records 211 and 163 there still charge their 500 and 400.
    - `SPYRO_GEM_TRACE=1` prints counter changes and payments.
- **Visual pass** (`tools/enemy_visual_pass.py`, `enemies/`): one enemy of every class in every level with enemies (53 classes, 19 levels). Each is filmed idling beside Spyro and being flamed, with a contact sheet per clip.
  - `enemies-idle-engagement.png` and `enemies-flame.png` collect one frame per class.
  - Every class engages: it faces, approaches or attacks. The few that look still at a sampled frame are dormant until attacked, hide (Sunny Beach AF) or hang overhead (Robotica 175).
  - Classes that survive a single flame do so as in the original: Idol Springs 116 blocks with its shield; Aquaria 9C and other underwater enemies take only charges. Others need a charge or headbash (Magma Cone 128/148, Fracture Hills 69, Robotica 175). The audit's escalating attacks defeat them all.
- **Boss levels:** see "Defeat and the level's end" above. Crush and Gulp now run their whole loop.
- **Scripted events beyond enemies** (`quests.txt`, `quests-after.txt`): the quest census talked to every speaker in every scene (`spyro_script_quests`) and compared each level against the previous census.
  - Two speakers no longer opened, and both turned out to be census artefacts, now fixed in the tool.
  - **Idol Springs' first Zoe (138):** her original rule (`80073d40`) ignores a Spyro who was already within `0x1000` when she woke, until he has moved `0x1800` away. The census teleported Spyro beside her before she woke. It now stands 8–14 units off first, as a player arrives, and she talks and sets her checkpoint.
  - **Zephyr's Sergeant Tub:** the census stood Spyro on the cannon's step, 0.54 above Tub's feet. That is outside the original 800-unit talk band. It now prefers floors no higher than the speaker's, and he talks; talking to him from the ground always worked in the workbench.
  - The remaining silent speakers are the documented ones: mode-0 checkpoints, speedway challenge-givers (started by the speedway system), 3T and Chronos (empty retail lines), and the cutscene Hunter.
  - Footage: Summer Forest's Moneybags 211 charges 500 to learn diving through his original conversation (`scripted/moneybags-211-sheet.png`).
- **Re-run on the final build** (`data/validation/allpass-20261014-final/`): the same three audits after the gem fix. Every level now matches its gem counter (Summer Forest 400/400). There are 0 `ENEMY!` flags and 0 faults, and the quest census is unchanged.

## Validation

- `tests/retail_script_tests.cpp` (ctest `retail_overlay_scripts`):
  - all 28 campaign scenes boot and run 30 dispatcher ticks;
  - Colossus 111, Scorch 1A3, Crystal Glacier 4E and Cloud Temples 1C9 are
    owned, move or animate, die to the retail flame bit and count spirits;
  - a Colossus ram hurts an idle Spyro;
  - Hurricos native enemies, gems and containers stay native;
  - sections 10–12 cover the doors: Summer Forest's switch gates, Moneybags'
    and Elora's doors, and Colossus/Hurricos solid mobys. Section 7 checks
    Colossus' gate opening after 11A. 1104 checks in total.
- `tools/solid_census.cpp` (`spyro_solid_census`) lists, per scene, solid
  script and native mobys (collision-bank triangles) and terrain animation
  records with their RAM timelines (`MORPHS=1`). Its probes are environment
  variables:
  - `WALK=1` walks into every solid;
  - `WALK_FROM`, `PROBE_AT`, `STAND` and `CHARGE`;
  - `CLASS` and `PARAM_PEEK` read parameters; `DUMP` and `FIND_PARAMS` read
    RAM;
  - `PURCHASES`, `ORBS` and `TALISMANS` set progression.
- `tools/script_census.cpp`: per-class idle census (removed, moved, animated, spawned).
- `tools/vm_probe.cpp`: boots one level and runs chosen classes. It can inject
  repeated attack masks (`attack=10000@20 attack=20000@150`) and run any length
  (`ticks=900`). It also prints the boot camera mode (1).
- `tools/script_engagement.cpp` (`spyro_script_engagement`): for every owned
  moby in every scene, it takes a far baseline (Spyro at the spawn), then parks
  Spyro beside the moby. It records new RAM states, movement, clip changes and
  hits on Spyro, then tries flame, charge, headbash, superflame and green bomb.
  Spirit carriers that never react are flagged `ENEMY!`; those that react but
  never die are flagged `NODIE!`. Results:
  [engagement-census.txt](../data/validation/retail-scripts/engagement-census.txt).
- `tools/script_quests.cpp` (`spyro_script_quests`): walks up to every speaker,
  presses triangle, answers "yes" throughout, then runs on. It reports
  conversations that never open, conversations not driven by the original
  logic, camera shots, placements, rewards and faults (`QUEST_TRACE=1` for
  details). Results:
  [quest-census.txt](../data/validation/retail-scripts/quest-census.txt).
- Workbench validation options:
  - `--attack talk`: plays a conversation through (triangle, then "yes"),
    letting each line type out for 90 ticks;
  - `--attack approach`: the same, after walking up from about 7 units away;
  - `--attack-class HEX@FRAME`: applies the superflame bit to every live moby
    of a class.
- `data/validation/retail-scripts/`: per-level idle / charge / flame contact
  sheets (`run-level-checks.sh`, `level-checks.txt`, `flame-checks.txt`) and
  the Crush and Gulp fights (`crush-fight-sheet.png`, `gulp-fight-sheet.png`).

Workbench reports include `retail_script_*` fields (owned, ticks,
instructions, hits, spawned, attack bits, faults, classes). The session panel
shows the live counts.

## Known gaps

- Ripto's arena keeps the native encounter (`ripto.cpp`).
- Script particles (`800670fc`) and the sound wrappers other than `80050648`
  are not rendered or played. Animation frame sounds come from the engine's
  generic event path.
- Native speakers' conversations run through the original logic
  (`80015be4`/`8003a568`), so the quest flags they set reach the scripts. The
  speakers' own idle behaviour stays native.
- Camera modes other than 9/10/11 are adopted in RAM but presented by the
  engine's follow camera.

### 10 October engagement census: remaining flags, all retail-consistent

- **Crystal Glacier 50 (18 instances): corrected 12 October.** The handler has
  no death path of its own, but the group is defeated together.
  - All 18 form group list 7. Controller c6 (record 64, subtype 7,
    `8007e070`) awards every member's spirit (`8004c3d4`) and removes them once
    none is awake: all in states 50/51 and none in state 3.
  - It arms only while the flight gate (record 23, kind 2) is open and Spyro
    flies in movement mode 6. That is the mode the level overlay's flight
    states 33–35 set (`8006ed58`); the speedway overlays set it the same way,
    and use 9 for the attacking boost.
  - The port reported flight as mode 9, so the challenge never armed. Flight
    now reports 6, and 8002cc88's airborne-frame count `8006a0b0` is mirrored,
    because c6 cancels the challenge when it reads "grounded" with Spyro far
    below.
  - `retail_script_tests` section 13 knocks all 18 down while flying and gets
    18 spirits; the audit now finishes Crystal Glacier at 38/38.

  The original note follows.

  Its handler has no death path: no `8004c3d4` and no `8004132c`. Flame knocks
  it into states 3 and `33`, and it recovers about 7 s later.
  - **Ghidra cross-check:** `tools/export-moby-cases.ps1` decompiles 50, 17a, 11a
    and 1a6 to `data/analysis/moby-cases/overlay_NNN/class_*.c`. The case
    decompiles also pull in the dispatcher's shared code.
  - Class 50's own branch (`code_r0x800787a8`) only handles a flame hit:
    knockback, dropping its carried moby, removing its spawned `fb` throwable,
    and state 3.
  - The file's spirit-award and removal calls sit under other classes'
    branches of the type switch (for example `sVar11 == 0x233`).
  - The raw case range confirms it (942 instructions,
    `tools/mips_listing.py 37 800787a8 800795a4`). Its only `8004132c` call
    (`800788a8`) removes `param+2c`, the spawned `fb` throwable, and there is no
    `8004c3d4`.
- **Robotica Farms 17a (4, the flyers).** The first flame or charge hit sends it
  to state 5, then to the wreck state `63`. It is never removed.
  - Ghidra (`overlay_064/class_17a.c`, `8007b908`) shows a perching flyer: it
    cycles between three perch slots on another moby (states 0, 2, 3, 4).
  - A hit drops it out of sight (state 5, `z −0x2800`) with an 1800-tick
    timer.
  - In state 61 or 63 it then decrements `80066fec` and flies back in from its
    spawner (state 60).
  - Its raw case range (708 instructions, `tools/mips_listing.py 65 8007b908 8007c3c0`)
    calls neither `8004c3d4` nor `8004132c`, only the reset `8003986c`: it
    respawns by design.
- **Colossus 11a (1): fixed, not retail behaviour.** It is an area-triggered
  sequence (Ghidra `overlay_020/class_11a.c`):
  - Spyro inside its oriented box (`8003873c`, centre at `param+4`) starts a
    camera look (`8001f24c`).
  - States 2–5 play its animation and wait on a camera shake (`80068058`/`8006805c`).
  - States 61/62 wait for camera substate bit 0, set `80066f44` and place Spyro
    (`8001f524`).
  - The next state 0 awards its spirit (`8004c3d4`) and the moby stays in state 100.

  The census parks Spyro beside the moby rather than in the box, so it never
  started there. Three engine gaps stalled it once started; all three are now
  fixed (see "Camera shakes, shot endings and spirit awards").
  `tests/retail_script_tests.cpp` section 7 runs it to the spirit.
- **Speakers without a triangle conversation:**
  - class-110 checkpoints with talk mode 0 are silent by design (see "Zoe and
    checkpoints");
  - speedway challenge-givers (Queen Finny, the Mayor) start through the native
    speedway system (E within 4 units);
  - 3T (Hurricos) and Chronos (Cloud Temples) have empty retail lines;
  - Hunter #201 (Summer Forest) has no floor (a cutscene actor).
