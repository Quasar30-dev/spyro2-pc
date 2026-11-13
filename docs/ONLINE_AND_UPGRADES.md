# Optional online exploration and upgrades

Both features are off by default. Each player mounts their own US retail copy; no game assets pass through the relay.

## Internet play

1. Run **Start Online Relay.cmd** on the host PC. It starts the local relay and an outbound temporary Cloudflare tunnel. The displayed HTTPS address is also written to `data/online/relay-url.txt` and loaded by the workbench.
2. Enter Play, then open **Online**, check the relay address and choose **Host**. Copy the room code.
3. On another PC, enter the same HTTPS address and room code in **Online**, then choose **Join**.
4. Use **Disconnect** to leave. **Stop Online Relay.cmd** closes the relay and tunnel created by the start script. A new tunnel usually has a new address.

Up to four players can explore together. The relay shares world addresses, position, orientation, animation, appearance and melee pose. Remote Spyros interpolate between updates; local movement remains on the fixed 60 Hz simulation. Guests follow the host into retail levels and procedural worlds. A host disconnect expires the room after 12 seconds.

This is the initial exploration implementation: enemies, damage, pickups, objectives, carried objects and saves are still local. It does not yet provide authoritative shared combat, shared progression, matchmaking, host migration or prediction/reconciliation. The room code is the invitation; give it only to intended players.

The Windows client uses a background WinHTTP worker, validated HTTPS, bounded responses and at most 20 polls per second. The Python relay accepts only bounded player snapshots, binds to loopback by default and has no file-serving endpoint. Plain HTTP is accepted by the client only for local testing. The Cloudflare helper is downloaded from the official release and its SHA-256 checked; `tools/bin/cloudflared-version.json` records the version and digest.

Temporary tunnels are for testing and their address lasts only while the tunnel runs. [Cloudflare's Quick Tunnel documentation](https://developers.cloudflare.com/cloudflare-one/networks/connectors/cloudflare-tunnel/do-more-with-tunnels/trycloudflare/) describes the service limitations.

## Upgrades and controls

Enable **Upgrades → Enable upgrade progression** to earn points and buy abilities. This checkbox alone does not purchase them. Collecting a gem earns its value in upgrade points; buying an upgrade spends those points and preserves the campaign gem total. The menu shows each current tier and whether melee/carry are unlocked.

For immediate testing, choose **Unlock all and activate powers**. This grants all tiers, carry and melee, and enables the Supercharge, Superflame and Superfly cheats; it selects Superflame instead of the green bomb. **Unlock all for testing** grants the upgrades without changing power cheats. Power upgrades require their corresponding base power through a level gate or Cheats. To test ordinary glide and its wing boosts, turn off the Superfly cheat. Superfly now allows a grounded jump before flight activation.

Selections made before Play or while stopped are retained when Play constructs the game, and when changing levels. Purchases and cheat changes return keyboard focus to the play viewport after menus close. Previously, Play could replace those selections with a fresh or stale saved configuration. PC saves retain the selection across application launches.

| Upgrade | First tier | Second tier | Point costs |
| --- | --- | --- | --- |
| Supercharge | +30% speed, yellow flames | +60% speed, red flames | 25, then 50 |
| Superflame | Steers toward visible enemies | Splits into three smaller flames on impact | 25, then 50 |
| Superfly | +25% speed | +50% speed | 25, then 50 |
| Glide | One wing boost per airtime | Two wing boosts per airtime | 25, then 50 |
| Carry | **G** picks up / drops nearby baskets, vases, bottles and heavy chests while flying; **Shift+G** throws | — | 30 |
| Melee | **J** starts a swipe even without a target; press again during it to chain three strikes | — | 30 |

Homing respects walls and a forward targeting cone. Split flames have shorter lives, smaller effects and never split again. Carrying slows flight according to weight and checks the whole model's enclosing volume against terrain. Four foot targets are placed on the object's upper mesh; Spyro's leg pose blends onto them while his wings keep their original flap. Dropping releases the object under gravity, while throwing adds forward/upward velocity. The model bounds are swept to the actual impact; containers then smash and release their contents once. Heavy chests emit the original sixteen class-134 metal shards and break sound. A stored gem is released once; chests whose contents are separate authored pickups do not invent an additional gem. Reset, scene change or disabling upgrades restores any suspended intact object. Current saves also retain intact carried/thrown bodies, their transforms, velocity, grip/throw age and active flight state. Loading rebuilds model bounds and the current flight pose before the first render.

Carrying uses separate shoulder/hip, elbow/knee and ankle joints. Each paw stays rigid and aligns its sole to its own supporting plane on the lid; the full paw, including the heel, remains outside the carried mesh. The container follows Spyro's pitch and bank so those contacts remain within natural limb reach. Knee volumes move together instead of folding around different pivots. Head and wing vertices retain their original animation.

Melee uses new native gestures over the original Spyro 2 mesh: a coiled cross swipe, rising counterstrike and leaping tail-spin finisher. Both swipes move into the strike through normal collision checks, with shoulder/hip turns, articulated paws, tail follow-through and an audible exertion even on a miss. The finisher crouches, hops and rotates through a complete turn. **J works with no target nearby, during airborne movement, while Shift is held, and after closing another workbench panel.** Text entry, an open popup, pause, dialogue or a hurt reaction still own their respective inputs. Melee remains an optional unlock. Attacking while carrying drops the object to free Spyro's limbs. It is groundwork inspired by Legend of Spyro, not a recreation of that game's combat animations or complete combat system. Only the three hit windows query nearby enemies/containers for damage; missed attacks still animate normally.

Tap Space/X during a glide to spend a wing boost, using the original Superfly-up animation. Holding the button does not consume further boosts. The maximum is two before landing; hover or a midair toggle does not refill them. [Movement evidence and controls](AERIAL_AND_SCORCH.md).

Save format **S2SAVE8** retains upgrade enablement, tiers, unlocks, points and the glide tier introduced in S2SAVE5, plus butterfly state (v6), carried/thrown bodies and active flight/power state (v7), and campaign purchase flags (v8). All earlier formats remain readable. Existing S2SAVE1–3 files load with upgrades disabled; S2SAVE4 retains its previous upgrades with glide locked. The upgrade master switch restores the ordinary movement multipliers and removes homing/splitting/extra charge flames and glide boosts. It leaves separately selected base power cheats under the Cheats controls.

## Repeatable checks

`spyro_extension_tests.exe data/disc` exercises the actual retail models, homing, wall impacts, split cleanup, combos, carrying, throwing and save round trips. `tests/test_online_relay.py` covers isolated rooms, host authority, expiry, replay rejection, capacity and malformed packets.

`spyro_workbench.exe --game data/disc --headless --frames 180 --upgrade-workbench-check --capture data/validation/upgrade-workbench-check.bmp` exercises pre-play unlocks, edits while stopped, purchases, level changes and the actual J-key route. It requires three melee swings after those lifecycle operations. `spyro_aerial_tests.exe data/disc` checks capped wing boosts, hover, headbash, sand, old-save compatibility and relevant animation transitions across all 41 banks.

The workbench supports `--online-host URL` / `--online-join URL` with `--online-room` followed by a 32-character hexadecimal room code. Use `--paced` for network tests. `--upgrade-tier 1|2` and `--ability-replay carry|carry-throw|melee --interaction-record N` drive repeatable native ability captures; `--video` records 60 fps with game audio.

For a wider view of a drop, bounded captures also accept `--camera-distance 7 --camera-height 3 --camera-orbit 150`. Distance and extra height are in world units; these affect the recording viewpoint, leaving the simulation and ordinary gameplay camera unchanged.

`--capture-fullscreen` uses the full capture window for the level. `--camera-orbit-rate -15` rotates the recording viewpoint by 15 degrees per second. `--ability-replay carry-inspect` holds the object for a close inspection, while `--ability-replay melee-empty` drives actual ImGui J-key events at an ordinary level spawn without needing `--interaction-record`.

Metal fragmentation is grounded in Summer Forest `8007374c..800737dc`: sixteen class-134 fragments, eight of each mesh, launch amplitude 144, material 1 and effect flags 5/4. The existing `80039334` gem-release reconstruction supplies the contents. The optional aerial pickup/drop and limb fitting are new PC gameplay, not a retail Spyro 2 ability.
