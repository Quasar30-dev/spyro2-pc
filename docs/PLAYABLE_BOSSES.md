# Playable bosses

Open **Play Boss Training Ground.cmd** to start the practice level. You can pass a US retail CUE, BIN, or extracted directory as the first argument. The cyan pad at spawn cycles Spyro → Crush → Gulp → Ripto → Spyro with **E** or **Triangle**. Targets can be reset by reopening the level.

In the PC Workbench, **Game → Playable character** selects a character in Spyro 2 retail levels. **F6** cycles them during play. The same menu offers **1/3**, **2/3**, and **3/3** scale for all three bosses. The camera follows the selected model scale and muzzle positions adjust with it. All forms retain the retail player's terrain collision size for narrow routes and gates. The selection is local to the current PC session.

| Control | Action |
| --- | --- |
| Move / left stick | Walk |
| Shift / Square + movement | Sprint |
| Space / Cross | Jump |
| F or Q / Circle or R2 | Ranged attack |
| J or B / D-pad Up | Close attack |

Crush launches a straight blast and uses his club to send out shockwaves. Gulp spits an arcing bomb and uses a close body slam. Ripto casts from his animated scepter socket and uses a short-range spin. The PC move set uses retail models, selected animation clips, and event sounds. Windup, recovery, projectile flight and damage are tuned for player input. The bosses share the engine's usual health, pickups and checkpoint handling.

## Retail animation review

Mounted SCUS_944.25, CRC32 `cdc0917c`. Inventory covers all stored clips. Each pair is decoded frame count / nominal VBlanks, using the frame metadata rather than assuming uniform duration. Clip IDs are zero-based in each row.

| Actor | WAD / class | Clips in order |
| --- | --- | --- |
| Crush | 31 / `08f` | 25/100, 23/92, 48/192, 15/60, 27/54, 25/100, 17/68, 55/220, 60/240, 15/60 |
| Gulp | 55 / `191` | 30/240, 14/120, 23/102, 10/80, 9/80, 25/180, 8/80, 16/120, 12/80, 10/40, 20/80, 26/104, 15/120, 33/132, 15/80, 18/140, 71/284 |
| Ripto | 71 / `2af` | 30/240, 12/50, 16/80, 11/60, 8/32, 8/32, 11/52, 17/100, 8/40, 8/40, 11/64, 15/80, 10/50, 10/40, 15/60, 11/52 |

| Actor | Idle | Walk / sprint | Air | Hurt | Close | Ranged |
| --- | --- | --- | --- | --- | --- | --- |
| Crush | 0 | 3 / 9 | 5 | 6 | 7 | 1 |
| Gulp | 0 | 3 / 8 | 2 | 5 | 2 | 10 |
| Ripto | 0 | 1 / 1 | 13 | 6 | 5 | 2 |

Crush's walking, chase, recoil and club sequences are corroborated by overlay 30 and the previous Crownworks mapping. Ripto's cast, spin, evade and hurt roles are documented in [Ripto's encounter](RIPTO_BOSS.md). The player air poses are adapted from existing clips. Gulp's selections are based on decoded poses and timings and remain subject to gameplay review.

## PSX development build

The source builder creates replacement WAD slots for the same arena from a local US retail extraction, then the disc builder applies them to a copy of a local retail CUE. The image is intended for private testing. See [the PSX builder](../tools/boss-training/build_psx.py).
