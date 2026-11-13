# All-level gameplay audit - 23 September 2026

> **Update, 9 October 2026:** the "still without their native script" column
> below is historical. Those 326 enemies, Crush, Gulp and the other scripted
> mobys now run their **original overlay scripts** in the interpreter. See
> [RETAIL_SCRIPTS.md](RETAIL_SCRIPTS.md) for the per-level census and
> validation captures.

All 29 campaign scenes were loaded through the native decoder: 5,200 authored records and 25 powerup gates. Counts are runtime classifications, not claims that a level is complete. Bosses, quests, demonstrations, hazards and props without a spirit-bearing enemy record are not counted as missing combat below.

| Level | Records | Mapped enemy instances | New level-system records | Enemy instances still without their native script |
| --- | ---: | ---: | ---: | ---: |
| Summer Forest | 273 | 0 | 0 | 0 |
| Glimmer | 201 | 14 | 0 | 0 |
| Idol Springs | 278 | 6 | 0 | 5 |
| Colossus | 213 | 0 | 0 | 13 |
| Hurricos | 301 | 22 | 0 | 0 |
| Aquaria Towers | 271 | 0 | 0 | 29 |
| Sunny Beach | 202 | 0 | 0 | 17 |
| Ocean Speedway | 68 | 0 | 54 | 0 |
| Crush's Dungeon | 31 | 0 | 0 | 0 |
| Autumn Plains | 164 | 0 | 0 | 0 |
| Skelos Badlands | 230 | 0 | 0 | 28 |
| Crystal Glacier | 233 | 0 | 0 | 38 |
| Breeze Harbor | 253 | 16 | 94 | 0 |
| Zephyr | 237 | 0 | 0 | 30 |
| Metro Speedway | 50 | 0 | 46 | 0 |
| Scorch | 213 | 0 | 0 | 28 |
| Shady Oasis | 205 | 0 | 0 | 21 |
| Magma Cone | 199 | 0 | 0 | 19 |
| Fracture Hills | 252 | 0 | 0 | 29 |
| Icy Speedway | 86 | 0 | 83 | 0 |
| Gulp's Overlook | 7 | 0 | 0 | 0 |
| Winter Tundra | 156 | 0 | 0 | 0 |
| Mystic Marsh | 238 | 36 | 0 | 0 |
| Cloud Temples | 206 | 0 | 0 | 23 |
| Canyon Speedway | 61 | 0 | 59 | 0 |
| Robotica Farms | 248 | 0 | 0 | 24 |
| Metropolis | 198 | 0 | 0 | 22 |
| Dragon Shores | 118 | 0 | 0 | 0 |
| Ripto's Arena | 8 | 0 | 0 | 0 |

The exact class IDs, record inventory and bounded overlay disassembly are in `data/analysis/level-systems/coverage.json` and `inventory.json`. The remaining enemy count is 326. Those actors still need individual attack, movement, activation and death scripts. A generic powered-hit fallback is not counted as implemented combat.

The existing five combat areas remain Glimmer, Hurricos, the metal idols in Idol Springs, Breeze Harbor, and Mystic Marsh. This pass adds authored power-gate responses across scenes and level-specific systems for Breeze Harbor and the four speedways. It does **not** establish complete progression in the other levels. See [level systems](LEVEL_SYSTEMS.md) and the [completion ledger](REMAINING_WORK.md).
