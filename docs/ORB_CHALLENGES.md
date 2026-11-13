# In-game orb challenges

Started 3 November 2026 (third agent). The orb challenges that speakers give
(Colossus hockey, Glimmer's gem lamps, Metropolis' sheep saucers, ...), driven by
the original code: the giver's class code and the challenge objects' cases run in
the overlay interpreter (`RetailScripts`), and the orb comes from the giver's own
conversation (`8003a0d8` orb commands 2..9, `80016328` state 3), as in the retail
game. Companion to [Level completion](LEVEL_COMPLETION.md).

## How a challenge runs on the PC

- **The giver runs its own class code.** `challenge_giver(slot, class)`
  (`src/retail_scripts.cpp`) lists the givers handed to the interpreter instead of
  the native speaker system; their code starts the challenge after the talk,
  spawns and referees it, and opens the reward line (by itself, or when Spyro
  comes back and presses Triangle).
- **The challenge HUD is the level's own.** Every level overlay installs a HUD
  update hook (`80067154`) and a draw hook (`80067158`). Both now run each script
  tick with the resident counter code (`8005199c` ease, `80052498` entry bounce,
  `8005251c` icon + number, `80052690` icon + number / total, `80052960` m:ss.hh);
  only the sprite primitive `800520cc` is replaced, recording each sprite
  (descriptor, position, size type, CLUT and colour the caller left in the
  primitive). The workbench draws them from the level's HUD page (scene-local
  sprites decoded on first use, `scene_sprite`). Crush, Gulp, the speedways and
  Breeze Harbor keep their native HUDs.
- **Checks.** `spyro_challenges <disc> NAME --trace` (headless) and
  `spyro_workbench --game <disc> --challenge-check NAME --smoke N --capture X.bmp --hidden`
  run the same autopilot (`tools/challenge_pilot.hpp`): pad-equivalent inputs only
  (stick, X, Square, Circle, Triangle), plus counted, named fixtures (the first
  approach to the giver is a teleport before him). `spyro_challenges <disc> list`
  lists them; `lines SLOT OBJECT` prints a speaker's conversation; `hooks` the HUD
  hooks of every level.
- **Trying a giver.** `SPYRO_QUEST_SPEAKERS=19,24` hands those records to the
  interpreter for one run (diagnostic), before adding the class to `challenge_giver`.
  Ghidra exports of Colossus' 113 / 117 / 10F cases: `data/analysis/moby-cases/overlay_020/`.
- **Finding a class's case.** `python tools/find_class_case.py SLOT CLASS[,CLASS]`
  scans the overlay for the dispatcher's `li v0,CLASS` + `beq` and prints the case
  range (to the next case of the dispatcher's tree);
  `tools/export-case-ranges.ps1 -Slot S -Ranges start,end,label,...` decompiles them
  (Ghidra, minutes per case); `tools/mips_listing.py SLOT start end` lists them.

## Engine fixes found by the challenges

- **Spawned mobys after a hole were never mirrored.** `RetailScripts::sync_out`
  stopped at `80066fd4`, which is the lowest *free* slot (`80041290` allocates
  there and moves it to the next hole), not the end of the list (the state -1
  marker). Colossus' puck (Kipp's class 153 spawn) and the hockey players' 113 /
  117 children sat after a hole and were neither drawn nor solid.
- **`8006a104`, the floor's surface flags.** `8002cc88` copies the floor
  triangle's flag byte there (FF when none); scripts read its surface type with
  `8002b078`. The puck only lets Spyro pick it up on ice (type 4). The bridge now
  writes it each script tick (`Triangle::surface_flags`).
- **Powered shots never reached script mobys.** The superflame fireball and the
  green bomb tested only native objects (`container_contact`), so Metropolis'
  sheep saucers (252, script owned) could not be shot down. Each shot now also
  makes the retail moby-sphere query `8004055c` at its centre with its attack word
  (`RetailScripts::attack_query`), which ORs the bits into the touched mobys' +18
  (the saucer falls on `+18 & 10000` or `20000`) and stops the shot on a hit.

- **Superflame shots and explosions reach script mobys as the retail shot does.**
  The superflame shot (class 1E3, Sunny Beach `8007de40`) queries the moby spheres
  (`8004055c`, attack 970000) at birth with radius 600 and every flying tick with
  radius 400, after its terrain test, whether or not that test stopped it; its
  explosion queries again (`8007e620`). The engine skipped the query when the shot
  hit any triangle, and a solid script moby's collision bank counts as one, so
  Sunny Beach's turtle boxes (BA, solid) were never hit. `powerups.cpp` now queries
  after a terrain hit too, with the birth radius, and in the explosion.
- **A checkpoint restart reloads the level's mobys.** `8001722c`'s reload stage
  reads the gameplay section again (`80013918`) and runs the gameplay loader
  `8001c734` on it, which also applies the checkpoint's persistent list
  (`8004c31c`). The engine kept the mobys as they were, so a script that called
  `8004c1ac` from a state it never left (the Master Chef's bell, state 10) asked
  again every tick: game state 3 for ever. `RetailScripts::reload_mobys` now loads
  the section and runs the loader again in the interpreter, re-mirrors the owned
  mobys, retires the spawned ones and hides the native ones from the queries as at
  boot (diagnostic counter `reloads`).

- **The camera's heading is mirrored for the scripts** (`80067ecc`, 12-bit, Spyro's
  heading convention). Scripts test whether they are in front of the camera
  (Skelos Badlands' lava toads, `800777cc`, come out only then); the engine
  wrote the camera's eye but not its heading, so the test failed.

- **Spyro's pitch is mirrored for the scripts** (`8006a058`, byte `80069ffd`). `8002a1f0`
  copies Spyro's rotation words `8006a054 / 58 / 5c` into the bytes `80069ffc / fd / fe`
  (>> 4) every frame; the engine wrote only the heading. A carried thing's spit aims
  with the pitch byte (Magma Cone's lava bubbles, overlay 48 `8007a628`; the other
  carry objects read it too). `RetailScripts::sync_in`.
- **A giver's placement ends a superfly flight.** Camera mode 11 (`8001fa58` case B,
  `80021240`) posts request `10010480`; bit 80 asks Spyro's update (`80030ed4`) for
  the state `8001f524` left in `8006a22c`, which the givers pass as 0 (standing). The
  engine placed a flying Spyro and he flew on, away from Chedda, who never talked
  (Party Crashers had no reward). `RetailScripts::advance_place` ends the flight when
  the requested state is 0.

- **The flame queries the mobys before its terrain stop.** Per flame point, `8002f4f4`
  queries the moby spheres (radius A0, attack 10000) at the advanced point and only
  then tests the terrain (`8003ce38`). The engine's ray test (which includes solid
  script mobys' collision) stopped the stream first and dropped that point, so a
  solid script moby was never reached. `ObjectInteractions::flame_tick` now hands the
  point to the retail query before stopping.

- **The superfreeze fires the retail freeze shot** (agent 8). With the freeze timer
  `8006a1cc` set, Circle spawns class 270 (`80030714`: the green bomb 2C2 first, then 270,
  then 1F8 / 1E3), not a flame. Cloud Temples' spawn (`8007fa6c`) starts it at the head
  joint with lifetime 180 (+0C, down 2 a tick); its case (`8007d714`) flies 340 units a
  tick, stops on the terrain (`8003ce38`) or a moby (`8004055c`, radius 700, attack
  01000000), then bursts (four 2B8 chunks, forty type-5 particles) and queries radius 1000
  for ten updates. The engine had a blue flame stream that froze on flame + ice, so no
  script moby ever got 01000000. `Powerups` now fires `PowerShot::Freeze`; native
  susceptible objects freeze on `freeze_attack`.
- **A script can change a native whirlwind's kind** (agent 8). The class-400 case reads
  its kind from header +08 every update (0 blows, 2 only while its state is set). Cloud
  Temples' Murgen zeroes the kind of the whirlwind behind him (#10) after the first bell;
  the engine read the kind once at load, so it never blew. `RetailScripts::sync_in` now
  re-reads it (`whirlwind_kinds` counter).
- **Classes native elsewhere can run their case in one level** (`level_script_class`):
  Cloud Temples' trolls (1EE) and their ice blocks (271), whose collision bank makes the
  frozen troll a step (`build_solids`).

## Challenges

### Release verification, 5 November 2026

The 20 implemented pilots, including Cloud Temples' bells, all completed on the
current game code: `build/dialogue-20261105/logs/challenges-headless.log` line 84
(`0 failed`). The main application also completed all 20; its per-challenge logs
and JSON are in `build/dialogue-20261105/`, with `logs/desktop-summary.log` line 2
reporting 38/38 (20 challenges + 18 exits). Logs bind the exact main SHA-256
`2baa0e554edce23b10b1ed5445dfe9ca24b82968bae2a0dcb6a2e1a9c9938e8e`.
These are automated pilots with counted fixtures; no manual play or visual inspection
was performed. The older Cloud Temples row and next-step list below describe the
historical seventh-agent state; the eighth-agent route at the end is implemented.

Dialogue regression sweep: `spyro_challenges DISC talk all [--revisit] [--no]` scanned
261 candidate records and opened 127 conversations on each of four runs, with zero
locks (`build/dialogue-20261105/logs/talk-{first,revisit}-{yes,no}.log` line 392).
The other records were gone, had no suitable approach floor, or did not open from
their starting state; this is not proof of every quest-dependent dialogue branch.
Idol Springs' separate Triangle runs opened 5/9 candidates on both first and revisit
fixtures, zero locks (`logs/idol-triangle-{first,revisit}.log` line 18).

The sweep's revisit fixture owns the level's rewards but clears the visit-completion
flag. Twitchy's repeated talisman line is fixed in the common retail dialogue bridge
(see [Level completion](LEVEL_COMPLETION.md)). Icy Speedway #27's apparent lock was a
movement-test artifact: the chosen stick direction pushed Spyro into geometry. The
sweep now tries other stick directions without teleporting him, and also checks for
reopened dialogue, an active reward, and interpreter faults. On both first/revisit
runs, #27 closes its one line and moves 0.55 units; Twitchy moves 2.85 units.

Reproduce using private executable copies only:
`python tools/run_dialogue_release_checks.py stage`, then `headless`, `desktop`, or
`ctest --ctest PATH_TO_LOCAL_CTEST`. Build the main and test targets before staging.

Implemented: the giver's and the challenge objects' original code run (or a
faithful port). Completable: a hidden run completes it with pad-equivalent inputs
and the orb is awarded through the giver's own conversation. Fixtures: what the run
does besides input (teleports; see each row). Headless: `spyro_challenges NAME`;
workbench: `tools/run_challenge_checks.py NAME` (hidden `--challenge-check`, photos
of the composited view with the HUD). Evidence folder:
`data/validation/challenges-20261103/` (`NAME.log`, `NAME.json`, `NAME-*.png`,
`NAME-sheet.png`, `summary.json`).

| Level | Challenge (orb) | Giver | Implemented | Completable | Tested (hidden run, how) | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| Colossus (21) | Hockey vs. the goal keeper (0) | Brother Kipp, 10F #19 | yes: 10F kind 5, puck 153, keeper 117 #22; HUD: timer and Spyro's goals (overlay 20 hooks) | yes | headless PASS (7476 frames); workbench PASS (14150 frames: lost the first game 3-5, won the second, as a player retries); 1 teleport (the first approach), all else input | `colossus-hockey-goalie*` |
| Colossus (21) | Hockey one on one (1) | Brother Kipp, 10F #19 | yes: kind 8, red skater 113 #21; HUD: both scores, no timer | yes | headless PASS (15218); workbench PASS (24924: the keeper game first, then 5-3 one on one); 1 teleport | `colossus-hockey-1v1*` |
| Colossus (21) | Ten green statues (2) | Professor, 193 #24 | yes: statues 1E4 | yes | headless PASS (1639); workbench PASS (2191); 11 teleports (the giver, each statue); flames and the walk back by input | `colossus-statues*` |
| Glimmer (17) | Gem lamps by the pyramid (1) | Kanga, 22A #179 | yes: lamps 21C, big lamp 40C #25, superfly gate 1CC #15 | yes | headless PASS (5112); workbench PASS (4556); 16 teleports (beside 14 enemies for the gate, the giver, the gate); take-off, flight and flames by input | `glimmer-lamps-outdoor*` |
| Glimmer (17) | Gem lamps in the cave (2) | Roo, 22A #180 | yes: hanging lamps 21C, big lamp 40C #28, gate 1CC #23 | yes | headless PASS (4720); workbench PASS (4692); 16 teleports as above | `glimmer-lamps-cave*` |
| Metropolis (67) | The attempted coup (0) | Inventor Droid, 251 #29 | yes (script owned already) | yes | headless PASS (1136); workbench PASS (1164); 5 teleports (beside 4 enemies, before her) | `metropolis-coup*` |
| Metropolis (67) | Sheep saucers, first wave (1) | Inventor Droid, 251 #29 | yes: saucers 252 #36 #44 #45, superfly + superflame gate 1CC #26; HUD: saucers left / total | yes | headless PASS (3639); workbench PASS (3789); 6 teleports (4 enemies, her, the gate); flight and superflame shots by input | `metropolis-saucers-1*` |
| Metropolis (67) | Sheep saucers, second wave (2) | Inventor Droid, 251 #29 | yes: saucers 252 #19 #25 #31 #34 #35 | yes | headless PASS (6491 since the shot-query fix; 6947 before); workbench PASS (6751); the first wave in the same run; 8 teleports (the gate again when the power runs out) | `metropolis-saucers-2*` |
| Glimmer (17) | Lizards and rocks (0) | Bounsa, 242 #37 | no | no | - | - |
| Idol Springs (19) | Foreman Bud's puzzles (0) | Foreman Bud, 124 #268 | no | no | - | - |
| Idol Springs (19) | Hula girls (1) | Hulagirl Stella, 232 #247 | no | no | - | - |
| Hurricos (23) | The factory switch (0) | Spike, 31 #43 | no | no | - | - |
| Hurricos (23) | Lightning stones / thieves (1) | Amper, 31 #42 | no | no | - | - |
| Hurricos (23) | Glides over the windmill (2) | Watt, 31 #246 | no | no | - | - |
| Aquaria Towers (25) | Seahorse rescue (0) | King Flippy, A4 #189 | no | no | - | - |
| Aquaria Towers (25) | Manta ride I, II (1, 2) | Hunter, 14E #215 | no | no | - | - |
| Sunny Beach (27) | Turtle boxes (0) | Advisor Gromit, B6 #41 (kind 3) | yes: Gromit counts group 1 (80039218(1): boxes BA #9 #12 #13 #14 #34 #37 #38); a box breaks only on attack bit 100000 (superflame) and spawns its turtles (123); superflame gate 1CE #15; HUD: boxes opened / 7 | yes | headless PASS (4948); workbench PASS (4940); 29 teleports (17 enemies for the gate, the giver, the gate, each box); the shots by input | `sunny-beach-turtle-boxes*` |
| Sunny Beach (27) | Turtle soup I (1) | Master Chef, BB #31 | yes: the chef (phase +94) starts a round when Spyro enters his start box (+4C) and spawns turtles B3 that walk to the pot (125 #39); a charged turtle (20000) slides (state 4), one reaching the water box (+64) in the air swims away (3, D); a turtle in the pot (F, 70) or Spyro in the water / kitchen strip (+64, +98) rings the bell: a checkpoint restart with the phase stepped back | yes | headless PASS (3896: lost the first round to the bell, the restart reloaded the level, won the second); workbench PASS (3956); 1 teleport (the first approach), all else input | `sunny-beach-soup-1*` |
| Sunny Beach (27) | Turtle soup II (2) | Master Chef, BB #31 | yes: as above, round two (schedule +41: 10 more turtles) | yes | headless PASS (8770); workbench PASS (8860); round one in the same run; 1 teleport | `sunny-beach-soup-2*` |
| Skelos Badlands (35) | Lava lizards I (0) | Lumpy, 62 #52 (kind 1) | yes: Lumpy sets task bit 0 once group 4 (lizards 49 #165 .. #172, eggs 39 #157 .. #164) is gone, places Spyro and talks by himself; HUD: lizards / 8 | yes | headless PASS (2678); workbench PASS (2706); 9 teleports (the giver, each lizard); flames by input | `skelos-lizards-1*` |
| Skelos Badlands (35) | Lava lizards II (1) | Lumpy, 62 #52 | yes: talking again drops the eggs of group 3 (+4C, z + 4000); group 2 (#29 .. #44) gone sets task bit 1 | yes | headless PASS (5400); workbench PASS (5442); round one in the same run; 17 teleports | `skelos-lizards-2*` |
| Skelos Badlands (35) | Dem bones (2) | Ooga, 62 #56 (kind 2) | yes: Ooga waits for 80066fec == 8 (`8007db68`); the eight bone holders (59, case `8007cec8`: #8 #12 #14 #20 #21 #45 #46 #49) throw a bone (65) on any hit at their header +0C point and go; a bone (case `8007e7dc`) is taken within 600 units (2D and height): 80066fec + 1. Ooga then places Spyro and talks by himself. The class 26 "lava toads" are not the targets (see "Dem bones" below) | yes | headless PASS (3983); workbench PASS (3941; the bone counter 0/8 .. 8/8 and Ooga's friend put together in the photos); 9 teleports (the giver, beside each holder); the attacks and the walk to each bone by input | `skelos-dem-bones*` |
| Crystal Glacier (37) | Draclets (0) | Shaman Tik, C6 #64 (kind 7, already script owned) | yes: the flight gate (1D0 #23, 15 enemies, 2400 frames of superfly); a flame knocks a draclet (50, group 7, 18) down (3, 32); Tik counts the standing ones (8007e114) and talks by himself at none | yes | headless PASS (11456); workbench PASS (11442; the draclet counter 18 .. 2 in the photos); agent 5's `sweep_targets` ring tour (the targets in sight order through the tunnels, flaming whatever is ahead); 41 teleports (38 enemies for the gate, the giver, the gate twice) | `crystal-glacier-draclets*` |
| Crystal Glacier (37) | Snow leopard (1) | Shaman Tok, C6 #65 (kind 8) | probed: George (5E #34, case `800798c0`, 102 states) waits in state 0; with Spyro within about 2.3 units he runs along his route (state 6, `80037fcc`) to a waypoint and stops there (64, 7, then 0), sometimes on a ledge 2 units up; state 0 compares header +10 (a stage, under 4) and Spyro within 7000 units (2D) and 1400 in height. Tok's kind-8 branch not read | no | headless probe: `chase_route` on George loops near (25..32, 88..95) without progress | - |
| Breeze Harbor (39) | Gear grab (0), Mine blast (1) | Fisher / Gobble, 180 #99 / #100 | native trolley and cannon systems (earlier work) | not checked here | - | - |
| Zephyr (41) | (0, 1), (2), (3) | Bo Peep C3 #28, Juliet 138 #48, Professor C5 #53 | no | no | - | - |
| Scorch (45) | Monkeys (0) | Hunter, 1B9 #45 | no: an escort ("charge the tree before I get hit... follow me"); 1B9 not read | no | - | - |
| Scorch (45) | Flags (1) | Handel, 1AF #48 | no | no | - | - |
| Shady Oasis (47) | Thieves (0) | Bruno, 188 #12 (kind 1) | yes: Bruno counts the lamp thieves (108, group 5: #13 #34 #35) in 80066fec; at 3 (after a 180-tick wait) he sets task bit 0, places Spyro and talks by himself | yes | headless PASS (2309); workbench PASS (2371; the lamp counter 0/3 .. 3/3 in the photos); 4 teleports (Bruno, near each thief); the chase by input | `shady-oasis-thieves*` |
| Shady Oasis (47) | Petrified hippos (1) | Grundy, 188 #31 (kind 4; script owned since the second batch) | no: the rocks are not class 194 (pushable boxes, charge); the kind-4 branch of 188 not read | no | - | - |
| Magma Cone (49) | Crystal popcorn I, II (0, 1) | Hunter, 14A #34 | no | no | - | - |
| Magma Cone (49) | Party crashers (2) | Chedda, 13B #43 (kind 3) | yes: a lava monster (128 #13 .. #24) dies only on attack bit 10000000 or 100000 (`8007b5a4`), never on a flame; the lava bubbles (91 children of the fountains 91 #25 .. #32, #45) are carried and spat at them (see "Party Crashers" below); HUD: monsters left | yes | headless PASS (21144, about 900 ticks a monster); workbench PASS (18398; the monster counter 0/12 .. 3/12 and Chedda's orb in the photos); 7 teleports (4 enemies for the gate, Chedda, the gate twice), the flight refilled through the gate by input | `magma-cone-party-crashers*` |
| Fracture Hills (51) | (0, 2) | Hunter, 89 #45 | no | no | - | - |
| Fracture Hills (51) | Alchemist escort / free the faun (1) | Krista, 85 #60 | no | no | - | - |
| Mystic Marsh (59) | Thieves (1) | Basil, 1FE #40 (kind 3) | yes: the four spark plug thieves (1A #29 #39 #43 #49) count into 80066fec (overlay 58 `8007b16c`); at 4 Basil sets his task bit, places Spyro and talks by himself | yes | headless PASS (4520, was 34486); workbench PASS (4550); agent 5's `chase_route` (charge after the thief along its own route, straight at it once near and level, held jumps when stuck); 14 teleports | `mystic-marsh-thieves*` |
| Mystic Marsh (59) | Fountain (0), the pencil (2) | Snoozle 1FB #37, Professor 25C #63 | no | no | - | - |
| Cloud Temples (61) | Hideout (0), bells (1), doors (2) | Agent Zero 1E7 #22, Murgen 1C7 #32 (kind 3), Bartle 1C7 #56 | bells: yes for the bells (14F, case `80073e98`: a charge, +18 & 20000, rings one; the first ring sets state 1, 80066fec + 1 and Murgen's +49 + 1; at +49 == 5 he talks by himself); 1C7 is not handed to the interpreter (handing the whole class over stopped Bartle, kind 4, the completion orb, from talking in the level-exit check), so run it with `SPYRO_QUEST_SPEAKERS=32` (Murgen's record only); not for the trolls (1EE, native) whose ice block the third bell needs (see "Cloud Temples' bells" below) | no: bells #23 and #27 ring (a jump, then a charge in the air), #25 hangs 0.6 units higher over its floor and needs the frozen troll under it as a step | headless `SPYRO_QUEST_SPEAKERS=32 spyro_challenges ... cloud-temples-bells`: 2 of 3 (FAIL kept in the catalogue on purpose) | - |
| Robotica Farms (65) | Bug light (0), tractor path (1), crow bugs (2) | Farmer John / Barleycorn / Applebee, 10D #50 / #49 / #48 (handed to the interpreter) | crow bugs: yes (17A #4 .. #7, overlay 64 `8007b908`; Applebee, kind 2, `8007a00c`, waits for all four in state 63 at once; see "Robotica Farms' crow bugs" below) | yes | headless PASS (3146); workbench PASS (3176; the bug counter 0/4 .. 2/4 in the photos, then Applebee's orb); 1 teleport (the giver), all else input | `robotica-crow-bugs*` |
| Metropolis (67) | Ox bombing (3) | Logistics Droid, 250 #24 | no | no | - | - |
| Summer Forest (15) | Hunter's jumps (0) | Hunter, 1BC #200 | no | no | - | - |
| Speedways (29, 43, 53, 63) | one orb each | Queen Finny, the Mayor, Hunter | native speedway system (earlier work) | not checked here | - | - |

The remaining orbs have no giver (free orbs in Summer Forest, Autumn Plains and
Winter Tundra).

## Fourth agent (3 November 2026)

Five more challenges complete through the original code (Sunny Beach's turtle
boxes and both turtle soups, Skelos Badlands' two lava lizard rounds), with two
engine fixes (see above). All thirteen pass headless and in hidden workbench runs.

**Spyro's red-brown colour in the Metropolis saucer photos is retail.** The
resident power palette `80031eac` writes Spyro's tint (`8006a010..13`) from the
power timers in order: invincibility `8006a1c4` (90 20 10, flashing), superflame
`8006a1c8` (R 90, G 20 + |cos(t * 2 + (180 - t) * 8 under 180)| >> 5, B 10, code D8),
then `8006a1cc`, `8006a1d8`, `8006a1d4`. The superfly timer (`8006a1b4`, set by the
gate) has no tint. In Metropolis the Inventor Droid (251, kind 2; dispatcher
`80071e44` -> `8007ca90`) copies the superfly timer into the superflame timer every
tick while her power moby lives (`8007cad4`: `8006a1c8 = 8006a1b4`): the combination
power is a superflame, so retail tints Spyro for the whole flight. `Powerups::tint`
already reproduces that formula and the D8 blend; no change. The Sunny Beach box
photos (superflame) show the same tint.

Tools added:
- `spyro_challenges <disc> objects SLOT [CLASS,...]`: every object after boot
  (class, position, RAM state, owned flag, spirit, header words; `HDR=a0` for a
  longer header) and the moby groups of `800670e0` (what `80039218(n)` counts).
- Pilot steps: `walk_to`, `in_box` / `box_centre` (`8003873c`'s rotated box),
  `turtle_round`, `attack_class`; `defeat_for_gate(gate, class)` limits the enemies;
  `cross_gate` jumps when blocked short of the plane (Crystal Glacier's gate);
  photos at each target done (`hit-N`), turtle saved (`saved-N`) and the bell.

Photos: the turtle soup's `saved-N` photos are taken when a turtle starts swimming
away, but the follow camera faces Spyro's way and the turtle is out of frame; the
soup's in-game proof is the chef's own reward (orb 1, then orb 2 in the photos) and
the traces. The box photos (`hit-N`) show the box at the moment it breaks and the
level's box counter; the lizard photos the eggs and the level's lizard counter.

Fixtures (teleports) per challenge are in the table; no autopilot was moved to
`tools/level_walk.hpp` yet (item 2 of the brief).

## Handoff (fourth agent, second batch)

Second batch: Shady Oasis' thieves complete (headless and workbench) and Mystic
Marsh's thieves headless only (the workbench run hits its frame cap); the camera heading fix; Dem bones, the draclets and Party Crashers' shooting
were tried again and are still open; Robotica's bugs were tried. Tools added:
`chase_each` (thieves), `lure_and_attack`, `until`, `attack_class`.

Next, in order:
1. **The three half-done ones, and Mystic Marsh's thieves** (speed up `chase_each`:
   thief #39 near Basil is the slow one).
   - Dem bones: the pool toads (#23 .. #25) come out (state 4) and go back (1, A)
     without ever being hit. Find where a toad in state 4 / 1 can be hit: read the
     26 case states 4 (`80077448`) and 1 (`8007724c`) for its position and any
     immunity (+47), and whether the flame query reaches it (the pilot stands
     2.4 units in front).
   - Draclets and Party Crashers need a better flight pilot: both lose most of
     their power orbiting moving ground-height targets. Approach on a straight
     line from 6+ units, hold the flame from about 3 units, then fly on rather
     than turning tight; or order the targets by a route (nearest neighbour)
     before take-off.
2. **Tried, not finished:**
   - Robotica's bugs (Farmer Applebee 10D #48): the four flyers (17A #4 .. #7)
     perch two units above the ground on the scarecrows and move between perches;
     ground flames and charges never hit them (80066fec stayed 0). Try jumping
     flames, and check what arms the challenge (80066fec was 0 after the talk).
   - Shady Oasis' petrified hippos (Grundy 188 #31, kind 4, now script owned):
     the rocks are not class 194 (pushable boxes); read 188's kind-4 branch for
     its targets.
   - Scorch's monkeys (Hunter 1B9 #45): "charge the tree before I get hit...
     follow me": an escort sequence; read the 1B9 case.
3. **Likely quick next ones (read, not tried):**
   - Cloud Temples' bells (Murgen 1C7 #32, a native speaker today: add (61, 1C7)
     to `challenge_giver`, which also hands over Bartle): the three bells are 14F
     #23 #25 #27 up the towers (y 30 .. 34); 14F writes 80066fec (overlay 60
     `80073f00`, `80073fbc`); "they'll ring if you charge into them"; the trolls
     (1C9) take the superfreeze. Try `attack_each({23, 25, 27}, rung, true)`.
   - Cloud Temples' hideout (Agent Zero 1E7 #22, already script owned): "hide
     behind the tree" (case `8007a8d4`).
4. **Not started:** Crystal Glacier's snow leopard, Zephyr (Bo Peep's cowleks,
   Juliet's Romeo, the Professor's seeds), Scorch's flags, Magma Cone's popcorn,
   Fracture Hills, Mystic Marsh's fountain and pencil, Cloud Temples, Robotica's
   other two, Metropolis' ox bombing, Summer Forest; Breeze Harbor and the
   speedways re-check; fewer fixtures and the whole-level walks.
5. Starting data: `data/analysis/challenge-surveys/objects-SLOT.txt` holds the
   `objects` dump (all objects, RAM states, headers to +A0, moby groups) of the 16
   levels with challenges left; `data/analysis/moby-cases/overlay_026/` the Sunny
   Beach decompiles (B3, B5, B6, B7, BB, BC). Who writes a counter: `python tools/find_writers.py SLOT ADDRESS` lists the
   stores to it and the dispatcher case holding each (Mystic Marsh's thieves, 1A,
   were found that way for 80066fec).

## Fifth and sixth agents (3 November 2026)

Agent 5 was cut off without a handoff. Recovered from its transcript and the files:
its only source change was `tools/challenge_pilot.hpp`: `chase_route` rewritten
(charge after a thief along its own route, straight at it once near and level, held
jumps and a placement behind it when stuck or lost), the `sweep_targets` ring tour
(Crystal Glacier: the draclets flown in sight order, every one a waypoint), and the
draclets and Party Crashers catalogue entries. The headless runner built, but the
workbench did not: `near` (a variable in `chase_route`) is a `windows.h` macro.
Agent 6 renamed it (`gap`) and kept everything else.

### Party Crashers (Magma Cone, Chedda 13B #43)

The lava monsters (128) take no harm from a flame: their case (`8007b5a4`) kills on
`+18 & 10100000` only (state 5, `80067088` down by one), and Magma Cone's gate (1CC #33,
kind 2, `80078320`) gives superfly alone. The retail way is the lava bubbles:

- The nine fountains (91 #25 .. #32, #45, state 0) blow a bubble (a 91 child) every
  240 .. 300 ticks once the challenge runs: state 4 rising, 5 floating two units over
  the fountain.
- A floating bubble asks to be carried (`8007aad8`: within 512 units of Spyro's head
  joint (`80032a90`, 2D `8001bf6c`) and 512 of its height, his head free `8006a15c`
  = 0): `8006a1fc = 10000001`, itself in `8006a208`. The engine's carry (head states 2,
  3) takes it in his mouth, in flight too.
- Circle spits it (state 2, `8007a59c`): it scans the 128s that are awake and solid
  (+8) for `|dx| + |dy| + |dz|` under 24576 units, the heading within 12/256 of a turn
  of Spyro's (`80069ffe`) and the pitch to 768 units over the monster within 16/256 of
  his (`80069ffd`), keeps the best, and flies at 1024 units over it on a ballistic arc
  (`80038e6c`, 600 units a tick, gravity 8). Without one it flies straight on.
- Falling (state 3, `8007a91c`), it queries the mobys (`8004055c`, radius 256, attack
  10000000) and Spyro; a hit ends it (state 63). The monster it touches dies.
- The volcano lid (401 #12) closes when Spyro is over its box (header +20, 2.5 units a
  side) above its height less 768 with floor under him (`8006a0b0` = 0), flying or not;
  Bippo (13B #44, kind 2) then ends the level with a checkpoint restart. The pilot
  stays under it.

Pilot steps added: `fly_to` (superfly steering: straight on while the point is inside
the turning circle on its side, then in, so every approach ends on a straight line;
the nearest free direction around walls and rocks; floor clearance), `carry_shots`
(nearest floating bubble by distance and turn, a straight pass through it; then the
monster cheapest to face, a straight run from 9 units, the spit when the retail cone
holds and the line is clear; extend and come again when close but off the line; a
target not lined up in 15 s or a bubble not reached in 14 s is left for 30 s; misses
weigh against a monster; back through the gate for more power below 900 ticks).

### Robotica Farms' crow bugs (Farmer Applebee 10D #48)

Traced, not finished: see the table. Next: read states 2 .. 4 (`8007bcd4`, `8007be28`,
`8007c050`) for where the bug flies and lands, and whether it can be met there (a
flame at the landing perch, or a flame from a glide / jump at a low pass).

### Next (agent 6's order)

1. Dem bones (Skelos Badlands, Ooga 62 #56): see the table. The flame fix above did
   not change the result: find where the flame streams stop in front of the pool
   toads (terrain at the pool rim, or the toad's own collision bank) against the
   sphere the retail query tests.
2. Cloud Temples' bells, then the rest in the table's order; Robotica's bugs as above.
3. Fewer fixtures: `carry_shots` refills at the gate by input; `cross_gate` and
   `defeat_for_gate` still teleport.


## Seventh agent (3 November 2026)

### Dem bones (Skelos Badlands, Ooga 62 #56): the targets were the wrong class

Agents 4 to 6 chased the class 26 "lava toads". They are ordinary enemies; the
challenge is the bones:

- Ooga (kind 2, `8007da68`..`8007dba0`) waits for 80066fec == 8, then sets task
  bit 2, +5 = 1 and +3A = 40: he places Spyro and talks by himself (orb bit 2).
- The bone holders are class 59 (#8 #12 #14 #20 #21 #45 #46 #49; case
  `8007cec8`). Any hit (+18 != 0, state not 4) spawns a bone (class 65) thrown
  on a ballistic arc (`80038e6c`) at the point their header +0C names, and the
  holder goes (state 4, removed).
- A bone (case `8007e7dc`, dispatcher `8006f244`) is taken when Spyro is within
  600 units of it in 2D and in height: 80066fec + 1, persisted (`8004c214`).
- Why the class 26 toads looked unhittable: variant 4 (#23 .. #26, group 9)
  hovers at floor + header +30 (500), or + 1400 more while Spyro is within 5500
  units in 2D (`80076a24`); its two spheres (radius 450 and 200) are joints 0-1
  and 2-3 of the frame, 0.4 .. 1.7 units over its origin, so 2.3 .. 3.5 units
  over the sand when Spyro is near. A full jump (X held 13+ VBlanks: apex 1.65)
  with Circle at the top reaches them: all four fell with that timing (now the
  default of `lure_and_attack`'s jump). Killing all four of group 9 drops a D3,
  not a bone.

Pilot step `hit_and_collect` (attack each holder, then walk to its bone once it
has landed).

### Robotica Farms' crow bugs (Farmer Applebee 10D #48)

- A bug's header lists its perches (+00 + level x 10: three scarecrows, class 56
  #0 #1 #2), +30 the current one, +34 its joint, +38 the level (never written in
  overlay 64: 0). Perched (state 0) it eases onto the joint and flies off once
  Spyro is within +28 + 4 x level in 2D: 6, 7, 4 and 5 units for #4 .. #7.
- State 2 takes off toward the next perch ((+30 + 1) mod 3), rising 20 a tick;
  state 3 flies there (speed header +20: 150 .. 210 units a tick) and eases to
  the joint height + 512 within 8192; within 1024 it picks a free joint and
  lands (4), then perches again. Nothing checks Spyro while it takes off, flies
  or lands.
- A flame or charge (+18 & 30000) in any state but 5 / 5F / 61 / 63 (and 62)
  catches it: 80066fec + 1, state 5, then 63 underground with a 1800-frame timer
  (`80037ef0` counts frames); when it runs out the bug comes back (60) and
  80066fec drops again.
- Applebee (kind 2, `8007a00c`) needs all four in state 63 at the same time
  (within 30 s of the first catch): he sets them to 5F, task bit 2, places Spyro
  and talks by himself.
- The bug's spheres are three of radius 200 (joints 4-5, 2-3, 0-1).

Pilot step `ambush_perchers`: pick the scarecrow with the most free perched bugs,
come round to the side of the next scarecrow beyond every flee radius, step in
on the bug's own line (its joint, not the scarecrow's centre) inside its radius,
and as the bugs take off over Spyro flame them standing (up to 1.1 units over
his feet) or from the top of a jump (1.1 .. 2.7). Four catches inside 30 s in the
headless run; `challenge_giver` now hands Robotica's 10D farmers to the
interpreter.

### Cloud Temples' bells (Murgen 1C7 #32): two of three

- A bell (14F, case `80073e98`) rings on a charge (+18 & 20000). Its first ring
  sets state 1, 80066fec + 1 and adds 1 to the +49 of the moby its header +00
  names (Murgen); at +49 == 5 Murgen (kind 3, `80077f14`) sets task bit 1,
  places Spyro and talks by himself (orb bit 1).
- The bell's hit sphere is kind 0 (an unrotated offset): radius 550, 1100 units
  under its origin. Bells #23 and #27 hang 2.73 units over their floors, #25
  3.33. Spyro's query (`8002c0f4` -> `8004055c`) is his origin (0x164 over his
  feet) with radius 0x164 (`80035268`), so the charge must bring his origin
  within 906 units of the sphere.
- A charge jump from the floor stays low (1.16). A jump with X held, then Square
  at frame 9 (800351d8 state 15 keeps the vertical speed and the charge holds it
  for 13 frames) rings #23 and #27 (`charge_jump_each`). #25 stays 1.0 short at
  best (start 2.5 units out, Square at frame 12 or 13; a scan of 48 timings).
- Murgen is not in `challenge_giver`: handing class 1C7 over made the level-exit
  check fail (Bartle, kind 4, gives the completion orb and no longer talked after
  the check set the task flag). Hand over Murgen's record only (`SPYRO_QUEST_SPEAKERS=32`
  does that for a run) or read Bartle's kind-4 branch first.
- The way up is the trolls (1EE): one stands under each bell (#31, #67, #30), and
  the troll case (`800750ac`) turns a troll hit by ice (+18 & 01000000) into an
  ice block, spawning class 271 (`8007520c`). The engine keeps 1EE native (an ice
  shell drawn for 300 frames, not solid), so there is no block to stand on.
  Next: hand 1EE (and 271) to the interpreter in Cloud Temples, deliver the
  freeze breath's 01000000 to the script mobys (find the retail query that sets
  it), let the 271 block be solid through its collision bank (`build_solids`), then
  freeze #67, hop on its block and charge #25.

### Snow leopard (probe only)

Tok (C6 #65, kind 8) at (19.79, 10, 76.41); George (5E #34, case `800798c0`,
states 0, 2, 6 .. 9, B, 14, 15, 1E .. 20, 64, 65) at (38.13, 10.05, 97.24). Within
about 2.3 units George runs along his route (state 6, `80037fcc`), stops at the
next waypoint (64, 7, then 0), sometimes on a ledge 2 units up; state 0 reads a
stage at header +10 (under 4) and Spyro within 7000 units in 2D and 1400 in
height. A `chase_route` run looped near (25 .. 32, 88 .. 95). Next: read the
stage handling (states 8, 14, 15, 1E .. 20) and Tok's kind-8 branch to see what
brings George home.

### Handoff (seventh agent)

Done this batch: Dem bones and Robotica's crow bugs complete headless and in the
workbench; Cloud Temples' bells 2 of 3; the snow leopard probed. Engine change:
`challenge_giver` (65, 10D). Tried and reverted: (61, 1C7).

Next, in order:
1. Cloud Temples' bell #25: the frozen troll step (see "Cloud Temples' bells").
2. The snow leopard (above), then the rest of the table in the brief's order:
   Zephyr, Scorch, Shady Oasis' hippos, Magma Cone's popcorn, Fracture Hills,
   Mystic Marsh, the rest of Cloud Temples and of Robotica (Barleycorn kind 3,
   John kind 4: 10D now runs its class code, so their challenges can be
   probed directly), Metropolis' ox bombing, Summer Forest.
3. Fewer teleports (gate crossings, gate enemies), whole-level walks.

Tools: env-gated diagnostics in `tools/challenge_pilot.hpp`: `LURE_DEBUG`
(flame points against a lure target), `AMBUSH_DEBUG` (crow bug flame distances),
`ATTACK_DEBUG` (attack_each target state, `charge_jump_each` closest approach
to the first hit sphere), `ATTACK_TERRAIN` (floor layers around a target);
`watch_objects(approach, objects, limit)` logs RAM states while walking at one.

## Eighth agent (4 November 2026)

### Cloud Temples' bells (Murgen 1C7 #32): the retail route

Murgen's own lines give it away ("There's still one bell left. You can get to it by
using the whirlwind behind me"; the hints "Freeze the trolls", "Charge the bells"):

- Murgen (kind 3, `80077f14`; his +49 counts the talk and every first ring): at +49 == 1
  he sends group 6 (troll #30) to its route node 8 (`800392a8(6, 43)`, troll state 43);
  at +49 == 3 (after the first bell) he zeroes the kind of the moby his header +3C names,
  the whirlwind #10 at (99.0, 22.0, 24.7) behind him, places Spyro and talks (line 4); at
  +49 == 5 with task bit 2 clear he sets it and talks (line 2, orb bit 1).
- Bell #25 hangs in the top room of the tower behind him: floor 30 (x 95.5 .. 102.5,
  z 33.5 .. 41.5), the bell's sphere at 33.3. Whirlwind #10 (height 8.79) lifts Spyro to
  30.4 and lets him glide +z onto the walkway round the tower (27.5, z 31 .. 33), 2.5
  below the room's floor.
- The trolls (1EE, case `800750ac`) walk routes (header +04, `80037fcc`) and appear only
  once the moby at header +2C is in a state (the freeze gate #68). Attack bit 01000000
  (the freeze shot) turns one into a block: state 4, a 271 spawned at `8007520c` at its
  feet (header +00) that follows it and is solid; header +08 counts 300 updates, then it
  thaws (a 0xAD puff); a flame (10000) thaws it at once; a charge (220000) while Spyro is in
  state D (a charge bonk) or with 80000 shatters it into sixteen 2A5 shards. The block
  top is about 1.6 over the troll's floor.
- So: the troll on the walkway (#180) frozen is the step into the room (block top 29.1,
  then a held jump to 30); a troll in the room (#66 / #67) frozen under the bell is the
  step to it (block top 31.6, then a held jump with the charge at frame 9).

Gate #68 (kind 7, superfreeze, 2000 frames) opens on the level's enemies. Pilot steps:
`ride_whirlwind` (walk in, ride, glide to a landing point), `freeze_climb` (the troll of a
list nearest the goal at Spyro's height; walk to it, shoot it until it is a block, jump on,
then jump to the goal floor or charge the bell from it). The engine changes are in
"Engine fixes" above (the freeze shot, the whirlwind kind, `level_script_class`);
`challenge_giver` now takes the kind byte and hands over Cloud Temples' 1C7 kind 3 only
(Murgen), so Bartle (kind 4) stays a native speaker. Tool: `spyro_challenges <disc>
heights SLOT X0 Z0 X1 Z1 STEP` prints the floor layers along a line.

Still open in this level: the hideout (Agent Zero 1E7 #22) and the doors (Bartle).
Troll #66 bobs between 30.3 and 31.7 in state 2 near #67; not looked into.
