# Containers, attack collision and released gems

Evidence profile: USA `SCUS_944.25`, CRC32 `cdc0917c`. Ghidra instruction evidence comes from resident code and Summer Forest's WAD-14 overlay at `8006d264`. Source game files are mounted read-only.

## Records and collision

There are **652 containers** in the mapped object tables: 352 baskets (class `c8`), 250 vases (`c9`) and 50 bottles (`d1`). Their stored gem values total **3,575**. Summer Forest has 33 containers worth 235; together with its 165 loose-gem value, these account for 400 gems.

The parser preserves the container parameters. Baskets and bottles use a four-byte record; vases have 28 bytes including a gameplay-relative pointer to a 64-byte signed wobble table. Bottles retain their linked object ID, including the `ffffffff` sentinel. Runtime objects have separate stable IDs and retain their source container ID for future save-data work.

`80041438` supplies collision data from static model `+8` or animated model `+20`. All 43 container model-bank occurrences use the same class-specific type-3 primitive from `8004055c`: an upright cylinder with rounded endpoints. The dimensions are original world units, independent of visual instance scale.

| Class | Broad radius | Cylinder radius | Bottom / top | Cap radius |
| --- | ---: | ---: | ---: | ---: |
| Basket `c8` | 672 | 320 | 128 / 320 | 320 |
| Vase `c9` | 864 | 256 | 128 / 600 | 256 |
| Bottle `d1` | 992 | 320 | 80 / 640 | 320 |

The original query uses strict squared-distance comparisons and a vertical margin `(radius>>1)+(radius>>3)+(radius>>4)` when choosing the cap. Native player collision uses this shape instead of a guessed visual bounding box. Walking resolves against intact containers. Charging supplies attack bit `20000`, as recovered from `800351d8` and `8002c0f4`.

## Container handlers

| Handler | Recovered behavior |
| --- | --- |
| `80073464`, basket | Any nonzero attack breaks it; twelve `132` fragments, first five using mesh 0 and variant 3, remaining seven mesh 1 |
| `800735d4`, vase | Break mask `160000`, which includes charge and excludes ordinary flame; twelve `133` fragments, mesh/variant pairs 0/5 and 1/4 |
| `8007396c`, bottle | Break mask `a0000`; ordinary flame also works when the linked object has state zero. A missing/sentinel link uses the protected branch. Twelve `136` fragments, with the original mesh and variant choices |
| `8003912c` | Vase heat increases by 16 on a flame hit, clamps at 63 and decays after hits stop; feeds the original material word |
| `80073604..800736e0` | Vase rejection starts a 64-VBlank wobble using the authored signed table and restores the saved position/rotation |
| `8007b974` | Shared removal path calls `8004132c` |

Flame collision follows the eight trajectory tables at `80062ad0` / `80062d90`, emission ordering at `80063130`, and attachment offsets at `80062a70`. `8002f08c` uses radius 160 and attack bit `10000`. Native trajectories follow the player attachment and stop at terrain or container hits. The [flame renderer](FLAME_RENDERING.md) now displays the original solid tips and textured ribbons with those cutoffs. Smoke, sparks, impact particles and the full secondary-action state machine remain pending, including alignment with the provisional head gesture.

## Fragments and gem release

`8007c180` initializes the real fragment models with random launch angles, a four-step starting displacement, original angular velocities and a 50–80 script-frame lifetime. `8007491c` supplies gravity, terminal descent, bounce flags and spin. The native renderer uses the decoded fragment meshes and textures. Terrain response uses native closest-point queries; it does not yet reproduce all original collision rounding. Offscreen fragments finish their lifetime rather than using the original renderer's “drawn” flag to remove them.

`80039334` releases the stored gem denomination using executable variant, mesh and material tables:

- Spawn at container Z+256 and initialize state 2, launch speed 140 and three remaining bounces.
- Choose a nearby landing point with up to four floor probes; solve flight time with gravity 10, then divide the horizontal displacement toward zero.
- When Spyro is charging within 2,560 original units, enter state 3 immediately and use the recovered gem flight toward Spyro. Awarding still happens when flight completes.
- State 2 at `80070c40` limits the movement step to 216, applies gravity, probes a radius-240 terrain sphere, reflects incoming velocity through `8001bbac`, damps it and consumes a bounce. When suitable ground is reached with no remaining bounces, settle into the resting gem state.
- `80071138` allows Sparx to target state-2 gems only after the remaining bounce count falls below three. The direct-touch age/height/range gate and state-3 delivery remain shared with loose gems.

Container, fragment and airborne-gem scripts advance at the recovered normal two-VBlank cadence. Transform interpolation and character animation continue at 60 Hz. Play owns the mutations: respawning cannot duplicate a collected reward; stopping and restarting play restores the authored level. Save-file persistence remains open.

## Validation and limits

The interaction test executable has 41 checks, including flame geometry/presentation and decoded collision bounds, attack masks, linked bottle state, walking obstruction, fragment count/lifetime, vase heat/wobble, ballistic settling, terrain-blocked flame, one-time gem awards, source IDs, authoring isolation and 30/60/144 Hz presentation independence.

`spyro_inspect --all-levels --spyro --verify-pickups --verify-breakables` passed all 41 mapped banks, 1,669 loose pickups and 652 container charge/release/delivery checks. This validates the native common-container implementation and all corresponding asset banks; it does not prove every level-specific overlay branch is reconstructed.

Native paced captures exercise basket charge/flame, released-gem bouncing and Sparx delivery, vase flame resistance/charge, and bottle charge. `tools/validate-retail.ps1` checks the reported outcomes. The workbench's object inspector provides **Play beside container** without changing the saved spawn. CLI example:

```powershell
./build/Release/spyro_workbench.exe --game 'D:/Games/Spyro 2/game.cue' --slot 15 `
  --interaction-record 122 --attack flame --smoke 180 --paced --capture data/validation/basket-delivery.bmp
```

The [retail flame renderer](FLAME_RENDERING.md) now displays the attack trajectories and their impact cutoffs. Remaining work includes impact particles, sounds, enemy attacks, complete dynamic-object collision, special surfaces and water motion, the two vases with nonzero surface-follow parameters, exact charge collision reactions, full secondary-action transitions, global RNG synchronization, save bits and overlay-specific behavior. Local random streams use the retail generator but do not yet consume all missing sound/particle/script draws. No complete gameplay equivalence is claimed.

Focused local evidence: `data/analysis/gems_014/breakables_disassembly.txt`, `containers_disassembly.txt` (including the gem state-2 range), `fragments_disassembly.txt`, `fragments_tail.txt`, and resident decompilations of `80039334`, `8003912c`, `8004055c`, `8001bbac`, `8002f08c`, `80041438` and overlay `8007bcb8`.
