# Changelog

Project development history.

## 2026-11-13 - Playable boss training

- Added Crush, Gulp and Ripto as selectable characters with mounted retail models, character-specific movement clips and close and ranged attacks.
- Added Boss Training Ground and a local switch pad. The PC Workbench provides selection in retail Spyro 2 levels and 1/3, 2/3 and 3/3 scale options.
- The PSX development builder packs the arena and donor banks into local WAD replacements. See docs/PLAYABLE_BOSSES.md for controls and clip evidence.
## 2026-11-05 - Shadow quality R2

- Responded to quality feedback with a 4096-square depth map (previously2048),
  retaining96-unit coverage and the animated meshes. A dedicated bilinear comparison
  sampler plus1/2/1 weighted PCF smooths the old hard sampling steps. Retail texture
  sampling, receiver separation, height fade and depth union are preserved.
- Built and installed the main; SHA-256 `c3aa993f399a34aa8530f820fc88fed53cc019e82bbff0ba78e30ed238e103cc`. No running user process was stopped.
- `build/shadow-quality-20261105/tests.log:28` records all226 focused GPU/character/jar checks passing.
  Matched installed-main views and charge/flame release captures pass with zero script faults.
  Before/after: `build/shadow-quality-20261105/glimmer-comparison.png` and `jar-comparison.png`.
- Latest packages: `dist/spyro2-pc-20261105-shadows-r2.zip` and
  `dist/spring-realm-20261105-shadows-r2.zip`, with checksums, validation and build manifests.
  Spring Realm retains its fused Blender source/previews. Every member was re-extracted and
  hash checked; four hidden launches pass (title, Summer Forest, Glimmer, Spring Realm),
  recorded in `build/shadow-quality-20261105/package.log:7`.
- Kept the approved trailer edit. The completed same-shot shadow reshoot is deployed as
  `D:/Theia-Promotional-Videos/05 - Spyro 2 PC - Release Trailer.mp4`,79.221s,
  SHA-256 `0e26d84c13d0e5e70e950185f277e2453572bbdfd40707e4712a44e7ce830cc0`.
  It uses the earlier validated shadow build; this subsequent quality-only update does not
  trigger another edit/reshoot.44 encoded frames reviewed, full decode clean; native
  effects/music, original thumbnail and description link gap retained.

## 2026-11-05 - Portable shadow/jar snapshots

- Packaged the installed shadow/jar main `3ae3a4e0c33f7769e18c25bcf740d2719a9de3ab2cbdac2a456f167dac9b36e7`
  as `dist/spyro2-pc-20261105-shadows.zip` (4,553,924 B, SHA-256
  `e83a4abba747cf3de5e4a2f8bc2e9e47e4ff3b265dccf50613d7353e7fc75bf6`).
- Refreshed `dist/spring-realm-20261105-shadows.zip` with the same executable
  (27,819,462 B, `4db2722160609c43da1124d6678339f4d38d47adf44cceffad3f1d398fa1ee46`).
  Each ZIP has a checksum, build manifest and validation notes. No retail disc images included;
  the Spring Realm archive retains the fused Blender source and previews beside the game folder.
- Verified every extracted member against the package manifest. Hidden 300-frame
  launches from the extracted packages pass: title, Summer Forest, Glimmer, Spring Realm.
  The three world runs report character-shadow geometry, decoded music and no script faults.
  `build/character-shadows-20261105/package-smoke.log:5` records both packages/four smokes PASS.
  Baseline 42-test/20-challenge/18-exit results are explicitly historical, not claimed as new reruns.
- `tools/package_shadow_snapshot.py` records packaging and extraction evidence.
  The same-shot trailer reshoot is running separately; it has not replaced the published trailer yet.

## 2026-11-05 - Character shadows and captive life butterflies

- Added animated mesh silhouette shadows for Spyro, NPCs, enemies and native
  character visuals, projected onto actual opaque terrain with filtered edges,
  slope/edge handling and a depth union that prevents repeated darkening.
  Original shadow semantics were checked in read-only Ghidra exports of
  `800495E8`, `800499D4` and their renderer call order.
- Retail life jars (class `233`) retain their original state-2 `D3` butterfly
  and live RAM pointer until their own attack handler releases it. The native
  controller excludes captive food. The filtered script order prevents a
  break-frame VM-only life award; released food transfers once to native Sparx.
- Built and installed the main with Carver's completed bootstrap edits intact.
  Compiled and deployed SHA-256 both
  `3ae3a4e0c33f7769e18c25bcf740d2719a9de3ab2cbdac2a456f167dac9b36e7`.
  The project installer preserved the running image; no user Spyro process was
  stopped. No camera/FOV/dialogue, custom-level, SOI or trailer source changes.
- Automated evidence: `build/shadows-jars-20261105/shadow-jar-tests.log:28`
  records 226 checks passing, including all 23 retail life jars held intact for
  1,908 VBlanks then released for one life award. Companion 46,894, interaction
  41 and retail overlay script 1,156 checks also pass in that directory's logs.
  Installed-main normal charge and flame captures restore health 3 with one
  consumption and zero interpreter faults; passive intact capture cannot feed.
  `build/character-shadows-20261105/ready.json` binds paths, hashes and result
  lines. Details/limits: `docs/CHARACTER_SHADOWS_AND_LIFE_JARS.md`. Manual play
  remains with the user; parent owns the trailer reshoot and portable package.

## 2026-11-05 - Market Mesa phase2c private content checkpoint (unfinished)

- Reused the completed phase2b sprite/family evidence. Converted three original YotD animated
  banks (Bianca, armoured spear Rhynoc, provisional green-robed staff Rhynoc), preserving24 clips,
  300 frames and both LOD streams. Added original-loader/vertex/material checks, native PCM equality,
  real PS1 textured-packet fixtures, native clip/frame renders, and an editable300-pose Blender archive.
  Enemy AI/placements, critters and Bianca story integration remain unfinished.
- All15 source containers now release value1 in the private candidate. Five charge-only boxes use
  appended C9 records and a generated slot16 dispatcher extension; native all15 interactions and
  original PS1 flame rejection/charge/released-gem checks are retained. Basket art/C9 heat/wobble
  remain donor approximations. Original indices/types/parameter pointers, source placements,
  Moneybags safety,7364 near/5246 far/10320 collision geometry and the existing source UV audit remain.
- Private BIN `3ffa5a1673c5f4a60767f7cb82ce557b2ed443b90b311f5c079141dc1c5b976c`,
  archive `a39b76c1b726e5d2ea6a2ee80f4da337b5e510d08e6bc6bae9586b7e330d6bc0`, and mandatory
  slot16 overlay `c3e06f3b15d36fd557826f8b899767585be0e1187a79c6968058f6c9b1839534` are WIP only.
  Details and exact artifact-bound evidence are in SEASON_OF_ICE_PORT.md10.15 and the phase2 handoff.
  No native executable/src changes, download, generator, subagent, release/launcher promotion or deletion.
  The parent retains stone/music/release ownership and must reserve the new model texture footprint.

## 2026-11-05 - Release trailer revised opening

- Removed only the opening 5.5-second Spring Realm flyover from the finished November
  release trailer. The film now opens on its existing Spyro 2 PC logo; subsequent cuts
  remain in order, including the later Spring Realm castle shot.
- Added `tools/release_trailer.py edit-no-opening` to reuse the finished picture cuts
  and render a continuous music/game-audio mix with new opening and ending fades.
- Current project trailer and `D:/Theia-Promotional-Videos/05 - Spyro 2 PC - Release Trailer.mp4`
  match SHA-256 `093c50974d37f13b4e78de86abd6c535f2e1c0ea0caeacbaf00a4f5825a82e13`.
  Duration 79.221 seconds; full FFmpeg decode passes. Receipt:
  `output/spyro2-pc-release-trailer/review/no-spring-opening.json`. Opening frame
  reviewed in `review/revised-opening.png`; the original trailer is retained in `archive/`.

## 2026-11-05 - Market Mesa UV experiment and content source preparation (solo, unfinished)

- Added an opt-in `mesa_ps1.py build --uv-polish` path, with original 32x32 descriptor cells,
  source UV baking, original descriptor orientation flags, per-cell 4/8bpp allocation and shared
  floor colour mappings. It removes whole-tile fallback selection while explicitly retaining
  1,803 approximate face mappings. 5,561/7,364 face mappings, including all 2,041 top faces,
  match source UV corners. This is an experiment, not a completed content release.
- Reused identical quantized A/B colour pairs **within the same material**, with no geometry
  averaging. The original near/far colours and positions are checked byte-for-byte against
  the preceding UV candidate. Near colour entries fell from 22,696 to 9,811; regions are 136.
- Added `mesa_uv_check.py`, `mesa_vram.py` and `mesa_uv_sheets.py`: original MIPS UV-consumer
  execution, every material texel, independent serialized source-face/UV binding, complete-edge,
  split-edge and repeat-boundary checks; conservative VRAM ownership, including all parsed object
  frames/LODs/patches, Spyro clips, font/sprites, flame and reserved guidebook uploads. Unclassified
  space is reserved; exhaustive symbolic coverage of all overlay/global consumers remains unfinished.
- Latest private candidate: `D:/Theia-staging/spyro2-mod/Spyro2-SoI-MarketMesa-uv06-reserved-wip.bin`,
  SHA-256 `b66da4ddb2055c951eab68517e38ce1e997d292d5bf998920bc039bdb654aee5`.
  Archive SHA-256 `576b6225a88a9b4456c6bd0b78f986805de578a22864874199e9d2dfe34dad9d`.
  Actual evidence under `D:/Theia-staging/spyro2-mod/work/soi-content-20261105/`:
  `uv/static-reserved.log:26`, `uv/coverage-final.log:17`, `uv/disc-reserved.log:27`,
  `uv/runtime-reserved.log:32` (1,800 ticks at line 28), `uv/pc-reserved.log:5`.
- Extended private checks with a task-root option, exact near GPU descriptor checks, first-frame
  diagnostic breakpoints and failure RAM capture. Added optional PC camera/reference inputs.
  Original uncompact candidates failed after loading; failures are retained, not release evidence.
  Compacted candidates completed bounded engine checks. The exact crash mechanism remains unproven.
- Added original ROM frame/code exports with `soi_content_sources.py`, and bounded original YotD
  bank/table census with `soi_yotd_sources.py`. All 248 Grendor frames per arena are retained;
  no Grendor model, animations or in-game integration is claimed. New Market Mesa actors and
  YotD character import/clip/sound integration remain unfinished.
- `package_patcher.py` rejects explicitly ineligible experiments. Default legacy output remains
  byte-exact test05 (`uv/legacy-regression.log:2`). Launcher, current test05 receipts, main/native
  executables and v2g mesh/world remain unchanged. No new patcher/release or retirement occurred.
- Review sheets and ownership map: `data/soi/ps1/uv06-review/`. Source references/census:
  `data/soi/content-20261105/`. Exact changed/generated paths and hashes are recorded in the
  task's `changed-files.json`; continuation details are in `handoff-full.txt` and doc section 10.14.

## 2026-11-05 - Market Mesa test05 release review (Codex, solo)

- Fixed the purchased-bridge regression by retaining Moneybags at original index 9 with retail's inactive state
  0xfd. Original consumer evidence: `data/soi/ps1/moneybags-oracle-test05.log:5`; real loader and targeted purchase fixture:
  `data/soi/ps1/ds/logs/sol-test05-review-20261105-151120.log:7`, `data/soi/ps1/ds/logs/sol-test05-review-20261105-151120.log:15`; final inactive record after 1,800 ticks: `data/soi/ps1/ds/logs/sol-test05-review-20261105-151120.log:27`.
- Retained exact far terrain (5,246 faces), enabled retail 0x2c000 visibility, and kept all 110 SoI loose gems/240 value.
  Static/archive evidence: `data/soi/ps1/static-test05.log:26`; clean disc readback: `data/soi/ps1/disc-test05.log:27`; private runtime: `data/soi/ps1/ds/logs/sol-test05-review-20261105-151120.log:30`.
- Restored clean test02 and test03 WIP to their exact BIN/CUE hashes (`data/soi/ps1/baselines-test05.log:30`, `data/soi/ps1/baselines-test05.log:31`).
  Cleanup requires explicit paths and protects those baselines; recovery snapshots and the user's RAR remain.
  test01 and the old test02 ZIP remain retired. Superseded test04 cleanup: `data/soi/ps1/retirement-test05.log`.
- Shipped `spyro2-soi-marketmesa-test05-20261105.zip`, SHA-256 `bd4452ef7e57a6c08aec60493fb56ecc3272ca4b26840a83d4fe11bb1d3ef048`; target BIN `b114be656add36c57601a0078aab0e6505cb108320b1fd7e2b7ecf42c031e309`.
  Extracted hidden-desktop GUI/script exact-output checks: `data/soi/ps1/patcher-test05.log:10`, `data/soi/ps1/patcher-test05.log:13`; package `data/soi/ps1/patcher-test05.log:17`.
  Launcher pinned to test05. Exact unchanged source/executable and baseline evidence: `data/soi/ps1/final-integrity-test05.log:13`.
- Automated PC load/captures only: `data/soi/ps1/pc-test05.log:5`. No main executable/source edits or manual acceptance.
  Far props/background mesas remain absent; LP is untextured; CLUT fading is fitted, 641 faces retain whole-tile
  fallback, containers retain retail contents, and 14/21 enemy positions use retail stand-ins. Full limits in §10.13.

## 2026-11-05 - Dialogue first/revisit correction and refreshed PC packages (Codex)

- Fixed Glimmer Twitchy's repeated talisman dialogue on first visits and revisits.
  The first-visit mirror of `80066ff4` was already in the saved source; the remaining
  cause was counting an already-owned reward as no completion this visit. Retail
  `80016328` state 4 update 150 sets the visit flag unconditionally, then conditionally
  increments the talisman count. The bridge now follows that distinction (including
  Winter Tundra Perfect orbs) and plays selected reward presentations on revisits
  without incrementing the count again. No blanket dialogue timeout was added.
- Reused the existing Ghidra exports of original SCUS_944.25 (`8003a0d8`, `8003a568`,
  `80016328`) and added direct execution of the original reward update to the retail
  script regression test. `build/dialogue-20261105/logs/retail-regression.log` lines 1-7:
  original first/owned reward writes, Twitchy's first/revisit release, 1,156 checks.
- Rebuilt the main `build/Release/spyro_workbench.exe`, SHA-256
  `2baa0e554edce23b10b1ed5445dfe9ca24b82968bae2a0dcb6a2e1a9c9938e8e`, then built test/probe
  targets and staged private executable copies. Exact artifact hashes:
  `build/dialogue-20261105/artifacts.json`; build logs under its `logs/` directory.
  Preserved Zoe's sparkles, Cloud Temples bells/freezing/whirlwind, camera recentre
  default 4 s with saved 0/1-10 s options, PS1 FOV and PS1 HUD options.
- Automated validation on this build (no manual gameplay or visual inspection):
  - CTest 42/42: `build/dialogue-20261105/logs/ctest.log` line 90.
  - 20 headless orb pilots: `logs/challenges-headless.log` line 84 (`0 failed`).
  - 20 main-application orb pilots and 18 main-application exits: `logs/desktop-summary.log`
    line 2 (38/38). Per-job logs record executable hashes; JSON reports record fixtures,
    awards and exit completion. Several exits simulate prerequisite end conditions.
  - Four dialogue sweeps: `logs/talk-{first,revisit}-{yes,no}.log` line 392, each 261
    candidates / 127 opened / 0 locks. Other candidates did not open from the starting
    fixture; quest-dependent branches remain outside this coverage. Separate Idol
    Springs Triangle sweeps: `logs/idol-triangle-{first,revisit}.log` line 18, each 9
    candidates / 5 opened / 0 locks. Fixed Icy Speedway #27's movement-test false
    positive by trying other input directions, without moving Spyro by fixture.
- **Failed diagnostic retained:** `logs/exits-headless.log` line 244, FAIL; initial
  `logs/headless-summary.log` line 2, **7/8 FAIL**. The old standalone exit probe does
  not complete several required end conditions. It is now opt-in with
  `--legacy-exit-probe`; the 18 main-application exits are the release evidence.
  Explanation and original addresses are in `docs/LEVEL_COMPLETION.md`.
- Shipped refreshed packages with README, validation notes, build-info manifest,
  app-local VC++ runtime and the same main executable:
  - `dist/spyro2-pc-20261105.zip`, 4,546,431 B, SHA-256
    `dd30cbea513a88a53d9f65a297e429230438ca59d294e1362fee308f9a84fa68`.
  - `dist/spring-realm-20261105.zip`, 27,811,253 B, SHA-256
    `fd1b104913962b7e6be12df46c41baf1cef96f8e7c8fa4dc8f9f301dfff63d0a`.
    Retains the previously shipped Blender source and previews alongside the game folder.
  - ZIP CRC and executable identity: `logs/package-build.log` lines 1-2.
  - Unzipped-copy smokes with the original retail CUE: title, Summer Forest, Glimmer,
    Idol Springs, Spring Realm; `logs/package-smoke-summary.log` line 2, 5/5. Each
    package-specific log binds ZIP and executable hashes. Automated render/report,
    music and fault checks only; screenshots are retained for the user.
- Changed source/tool/doc paths: `src/retail_scripts.cpp`, `tests/retail_script_tests.cpp`,
  `tools/challenges.cpp`, `tools/run_dialogue_release_checks.py`,
  `tools/package_dialogue_release.py`, `tools/smoke_dialogue_packages.py`,
  `docs/LEVEL_COMPLETION.md`, `docs/ORB_CHALLENGES.md`, `CHANGELOG.md`.
  New build evidence and dated package folders/ZIPs are listed above. No SOI paths
  edited, no downloads, no deletion, no user-owned running game manipulated, no git push.

## 2026-11-04 - Release trailer (coordinator)

- `tools/release_trailer.py` (capture / sheets / music / edit; reuses `pc_trailer.py`'s retail font, captions and
  ffmpeg helpers): `output/spyro2-pc-release-trailer/Spyro 2 PC - Release Trailer (November 2026).mp4`, 84.7 s,
  1920x1080 60 fps, retail Glimmer music (decoded with spyro_audio_probe). New hidden 1080p60 captures from the
  20261104b package exe (copied to D:/Theia-staging/s2-trailer/exe so build/Release was never locked): talisman
  hand-overs (Idol Springs, Aquaria Towers), six orb challenges via the challenge pilots, Market Mesa (build-soi exe);
  October clips reused (Spring Realm, superfly, power gate, Crush, Gulp). EDL in `source/edl.json`.

## 2026-11-04

### Season of Ice (agent 4): test02 - the freeze fixed, the current user image and patcher

- **Freeze in test01 found and fixed.** DuckStation courses stopped ~150 ticks into the level: a data bus error in
  the far renderer (EPC `0x80028d64`, store at `0x1f800400`, past the scratchpad). Its vertex loop never ends for a
  region with 0 far vertices; retail flags every such region 0x2000 ("no far mesh", skipped by the classifier).
  test01 had 6 decor-only regions without it. `mesa_ps1.py` sets the flag; `mesa_ps1_check.py` enforces it (it fails
  on test01's archive: `data/soi/ps1/ds/logs/static-check-test01-archive-fails-0x2000.log`). Also: Market Mesa's fog
  colour (scene part 5 +0x10) instead of Glimmer's khaki; near colour array B = A (the flat tile colours belong to
  the far mesh only).
- **Current user image:** `D:/Theia-staging/spyro2-mod/Spyro2-SoI-MarketMesa-test02.bin` + `.cue`, SHA-256
  `2e0ceecf28bfe64b65dabe3bade12c707c89f7f25c93a7d37968a75e8171f5fb` (`data/soi/ps1/ds/logs/disc-test02.log` line 24
  `DISC BUILD PASS`; `static-check-test02.log` line 15 `STATIC CHECK PASS`). No fixture in the image.
- **DuckStation proof (exact image):** `data/soi/ps1/ds/logs/test02-final-20261104-141024.log`: line 7 `ARRIVE PASS`,
  27 `TERRACE PASS` (a jump up one terrace, z +1.07), 33 `JUMP PASS`, 36 `COURSE WALK PASS` (18.3 world units across
  several terraces), 37 `COURSE GEMS PASS` (0 -> 4), 39 `PROOF mesa-play PASS`; no freeze. Sheet (opened):
  `data/soi/ps1/ds/shots/test02-final-sheet.png`.
- **PC proof (same image):** `data/soi/ps1/ds/logs/pc-test02b-ds-20261104-140958.log` `PC RUN PASS`; paired sheet
  (opened) `data/soi/ps1/ds/shots/test02b-ds-vs-pc.png` (PS1 left, PC right, the same cameras).
- **Patcher:** Theia `dist/spyro2-soi-marketmesa-test02-20261104.zip` (2,228,875 B, SHA-256
  `7a7f2b8733394c9aafc47d2d8c59ad8fd01eb0f0de9542c9496348c4a3471083`, patch 512,020 B);
  `data/soi/ps1/ds/logs/patcher-test02-20261104-1414.log` lines 8 `END TO END GUI PASS`, 11 `END TO END SCRIPT PASS`,
  16 `PATCHER PASS`. The test01 zip is retired (its `.sha256` stays).
- test01's BIN is retired but still on D: (`Spyro2-SoI-MarketMesa-test01.bin`): a `build/Release/spyro_workbench.exe
  --slot 15` session started 14:01 (not this agent's) holds it open; delete it with safe_delete once that closes.
- Known (TEST): on the PS1 far-away islands draw as flat grey-green blocks (the PC draws them fully); cause not found
  yet (not the fog colour, not the near B colours). The README says so.

### Season of Ice (agent 4): user test image test01, PC build support, patcher (user requests)

- **Test image (no fixtures):** `D:/Theia-staging/spyro2-mod/Spyro2-SoI-MarketMesa-test01.bin` + `.cue`,
  565,371,408 B, SHA-256 `7ac5ca692c084f6c2cbf4e7c0c4500977b181574ff7c5e501e86d3bb0bf4ccc7`
  (`data/soi/ps1/ds/logs/disc-test01-20261104-1330.log`: `readback: slot 17 equal True, TOC increasing True, other
  slots differing 0`, `DISC BUILD PASS`). Reach Market Mesa with a new game (title START, "Start Game" without a card,
  START through the two opening cutscenes): the first level is Glimmer's slot.
- **PC build:** `parse_wad` (src/assets.cpp) refuses decreasing WAD offsets ("Invalid WAD extent at slot 18" with
  slot 17 moved to the end), so `tools/soi/mesa_disc.py` now grows slot 17 in place and moves the later slots;
  no PC code change. `spyro_workbench.exe --game <test01 cue> --slot 17 --play` loads Market Mesa from the disc.
  New launcher `Play Market Mesa (Season of Ice).cmd` (newest test cue; optional .cue argument).
- **Proofs on the exact test01 image:** DuckStation `data/soi/ps1/ds/logs/test01-ds-20261104-133046.log`: `ARRIVE
  PASS`, `WALK PASS`, `GEM PASS` (0 -> 1), `JUMP PASS` (1.19); PC build (the `build-soi` copy, byte-identical to
  `build/Release/spyro_workbench.exe`, SHA-256 5055ed71...) `data/soi/ps1/ds/logs/pc-test01-ds2-20261104-134334.log`:
  `PC RUN PASS`; paired sheet (opened) `data/soi/ps1/ds/shots/test01-ds2-vs-pc.png`.
- **Patcher:** Theia `dist/spyro2-soi-marketmesa-test01-20261104.zip` (2,250,650 B, SHA-256
  `d204b717ea5463f885af9db5a6a6aef0e6877c75d92138ce685ea79638173054`; patch 533,820 B). Built by
  `tools/soi/package_patcher.py` with BIN/CUE copies of the house GUI and backend (`tools/soi/patcher/main.cpp`, built in
  `build-soi/patcher-gui`; `tools/soi/patcher/Patch-Spyro2.ps1`). End to end from the zip on D:
  (`data/soi/ps1/ds/logs/patcher-test01-20261104-1340.log` lines 10, 13, 17): `END TO END GUI PASS` (source = retail
  .cue), `END TO END SCRIPT PASS` (source = retail .bin), both = the test01 SHA-256 with a matching .cue;
  `PATCHER PASS`. GUI screenshot (opened): `D:/Theia-staging/spyro2-mod/patcher/spyro2-soi-marketmesa-test01-20261104-gui-success.png`.
- Not proven yet: the longer terrace course in DuckStation stops at a 30-s gap without a logic tick after Spyro drops
  to a lower terrace (`test01-ds2-20261104-133944.log`, `STALL` line); under investigation (fixture or game hang).

### Season of Ice (agent 4): Market Mesa PS1 encoder and the first mod disc

- `tools/soi/mesa_ps1.py` (new): Market Mesa -> Glimmer's slot 17 archive (`D:/Theia-staging/spyro2-mod/work/
  slot17-mesa.bin`, report `data/soi/ps1/mesa-ps1-report.json`). 7,364 near faces (limit 8,191) in 117 regions,
  125 materials, 30 texture slots repacked into Glimmer's terrain VRAM, clustered far meshes, 10,320 collision
  triangles, visibility off, seven animation sections emptied, Moneybags dropped (record 200 moved into his slot),
  objects moved (91 gems, 14 lizards on the Rhynoc spots, 15 baskets on the containers, 2 Zoes and Return Home near
  the start, the rest parked off the map). The scene child is 555,008 B (Glimmer's 505,856), so child 2 / 3 move
  by 49,152 and the header's 38 model-directory offsets with them. Formats in `docs/SEASON_OF_ICE_PORT.md` §10.12.
- `tools/soi/mesa_ps1_check.py` (new): static checks of the archive. **STATIC CHECK PASS**, log
  `tmp/soi4/static-check.log` (14 CHECK PASS lines).
- `tools/soi/mesa_disc.py` (new): the disc. Slot 17 is appended after WAD.WAD's last slot (LBA 86,884; the
  3,116-sector gap before SPEECH.STR held empty form-2 sectors, which become form-1 WAD sectors), the TOC entry and
  WAD.WAD's ISO size are updated, every other sector is retail's. `D:/Theia-staging/spyro2-mod/spyro2-soi-mesa.bin`
  (+ `.cue`), 966 sectors differ from retail; **DISC BUILD PASS** (slot read back equal), log `tmp/soi4/disc.log`.
- `tools/soi/mesa_build.py`: `main()` split into `build_model()` (shared with the PS1 encoder); the PC export is
  byte-identical (`tmp/soi4/refactor-check.log`: `S2WORLD IDENTICAL`).
- `tools/soi/ds_mesa_play.py` (new): hidden play run of the mod disc (route by pad, shots, walk / gem / jump).
- **Market Mesa boots and plays in retail Spyro 2 (DuckStation, hidden), reached from a new game by pad input.**
  Disc `spyro2-soi-mesa.bin` SHA-256 `1137e199cba84eada06131921d411d22222485120839f2008dd638e0904b5dde`
  (`data/soi/ps1/ds/logs/disc-20261104-1320.log` line 23 `DISC BUILD PASS`; static checks
  `data/soi/ps1/ds/logs/static-check-20261104-1320.log` line 15 `STATIC CHECK PASS`). Play run
  `data/soi/ps1/ds/logs/mesa-play-20261104-132232.log`: line 7 `ARRIVE PASS`, 13 `WALK PASS` (3.92 world units),
  14 `GEM PASS` (gem total 0 -> 1), 17 `JUMP PASS` (rose 1.19), 21 `PROOF mesa-play PASS`. Sheet (opened):
  `data/soi/ps1/ds/shots/mesa-play2-sheet.png`. The first run (`mesa-play-20261104-131854.log`) showed tile-number
  labels on the paving (the retail tiles had been taken from the labelled atlas PNGs) and green smears on distant
  island tops (trees in the far meshes); both fixed. Input route and fixture: TEST FIXTURE (`ds_harness.Pad`).

### Season of Ice (agent 4, resumed): hidden DuckStation boots, frames and pad input proven on retail

- **Boots twice with frames: PASS.** Log `data/soi/ps1/ds/logs/boot2-20261104-014344.log`, line
  `PROOF boot-twice PASS (2/2)`; shots `data/soi/ps1/ds/shots/boot2-run{1,2}.png` (Insomniac intro, opened).
  Fixes: DuckStation's GDB server pauses the emulator when a client attaches (the earlier "booted" runs sat
  paused at frame 0 and gave black frames), so the harness resumes it; screenshots use the private profile's
  `[Hotkeys] Screenshot = Keyboard/F10` posted to the hidden window, read once DuckStation closes the PNG.
  Host pads are off in the private profile (`[InputSources] SDL = false, XInput = false`).
- **Pad input on retail Glimmer: PASS** (TEST FIXTURE). Log `data/soi/ps1/ds/logs/pad-glimmer-20261104-020157.log`,
  lines `WALK PASS: moved 3.46 world units`, `JUMP PASS: rose 0.78 world units`, `PROOF pad PASS`; sheet
  `data/soi/ps1/ds/shots/pad-glimmer-sheet.png` (start, walked, mid-jump with shadow, landed; opened). Route by
  pad only: title START, memory-card prompt CROSS (new game), START / START / CROSS through the opening
  cutscenes, Glimmer (level 1, game state 0x800681c8 = 0). Menus need the press reported on every tick
  (`repeat`); a frame is only presented while the emulator runs freely, so shots release the breakpoint briefly.
- `tools/soi/ds_harness.py`: `Session` (hidden run, resume after attach, screenshot), `Pad` (breakpoint at
  8001271c, writes the 16-byte VBlank samples at 0x8006a99c + 16k with edges), `Log` (every proof writes a log),
  `boot2`. New: `tools/soi/ds_explore.py` (step lists with shots), `tools/soi/ds_pad_proof.py`,
  `tools/soi/mipsdis.py` (capstone disassembly of SCUS_944.25 / WAD overlays), `tools/soi/sheet.py`.

### Season of Ice (agent 4): DuckStation blocker, PAUSED (coordinator's order)

- `tools/soi/ds_harness.py`: new `diag CUE [SECONDS]` (boots hidden, lists the hidden desktop's windows, reads any
  startup dialog), `spawn_on_desktop`, `uia_dump`, `window_titles`. DuckStation's windows are now shown
  (SW_SHOWNOACTIVATE) **on the hidden desktop only**, because UI Automation skips hidden windows; that desktop is
  never switched to, so nothing reaches the user's desktop.
- `tools/soi/ds_uia_dump.ps1` (new): dumps the UIA tree of every window on the desktop it runs on (Qt dialogs have no
  native child controls, so GetWindowText cannot read them). Proven: it read DuckStation's
  "Unknown parameter: -settings" box.
- The "Error" dialog agent 3 hit (00:18-00:23) did **not** recur: six hidden boots of retail Spyro 2 since 00:26
  all reached the game window and the GDB port (including two in a row, a GDB-read boot, and two concurrent
  instances). Ruled out: a second instance, the GDB port, settings.ini as it stands. Its text was never read, so
  the cause is unknown; from agent 3's transcript the dialog had no main window and the log was not rewritten, so it
  failed very early in startup. Not tested: a transiently full D: or another transient host condition at that time.
  The harness now reads such a dialog automatically if it comes back.
- No frame captured yet; the pad fixture is not run. Scratch (for the user to clear): `tmp/soi4/` (test scripts,
  `agent3_ds_excerpt.txt`, `settings-after-run1.ini`, `uia-*.txt`). No DuckStation process left running. Nothing
  in `src/`, `build/Release` or `build-soi/` was touched.

### General Spyro 2 PC package 20261104c (agent 8): the PC options

- `dist/spyro2-pc-20261104c.zip` (4,545,006 B, SHA-256
  `18635a5f61b3ef6d8818b5ef5072b1640cfe21c8f27093aacfb41c6e0bece939`), exe
  `274aab5e4ecc853eee1e95e51f2d4736539500a2b277d580f3a5f2f1f228f067` (the options below, the Zoe fix, the
  Cloud Temples changes). README lists the options and the dialogue soft-lock as a known issue.
- ctest `tmp/agent8/ctest3.log` line 87 "100% tests passed, 0 tests failed out of 42"; the 18 exits
  `tmp/agent8/level-exits3.log` (18 OK lines, rc=0; reports in `data/validation/level-exits-20261104/`).
- Smokes of the unzipped package (hidden, the retail CUE): `data/validation/spyro2-pc-package-20261104/
  c-smoke-15-summer-forest-rerun.png` / `.json` and `c-smoke-17-glimmer.png` / `.json` (opened). The
  first Summer Forest smoke reported 0 music frames; two reruns reported 5,787,936 (the usual figure).
- Spring Realm refreshed with the same exe: `dist/spring-realm-20261103.zip` 24,928,360 B,
  `6217ae0cbeffef43e1400af6f36bc67123c22a7cbd3d5dd3dc91ebc8bc0cb586`; smoke
  `data/validation/spring-realm-package-20261103/package-refresh-smoke-8.png` (opened).
- Package 20261104b retired: zip and folder moved to `tmp/agent8/retired/` for deletion; its
  `.sha256` stays in `dist/`.
- The user's own workbench (started 13:31 and 14:01 from `build/Release`) was left running; its
  images were moved aside to `build/Release/spyro_workbench-running-20261104-*.exe` so the build
  could relink (the `tools/install-workbench.ps1` convention).

### PC options: camera recentre delay, PS1 FOV, PS1 HUD size (agent 8, the user's request)

- **Right-stick camera recentre** (`src/camera.cpp`, `include/spyro/camera.hpp`): `recentre_delay`
  ticks after the stick is let go (default 240 = 4 s; 0 = never) the stick's angle and height are
  dropped and the camera's own follow spring eases it back to Spyro's default angle (no cut). The
  old rule stays: running after the 1.5 s hold makes it follow. `tests/look_tests.cpp`:
  `tmp/agent8/look-tests.log` lines 1-2 "camera recentre 4 s: ... held 239 ticks after release
  (3.98333 s); returning from tick 241; largest step 12 per tick" and the same for 2 s (119 / 121);
  line 3 "32 look-mode and camera checks passed".
- **PS1 FOV** (default off; `view_fov` in `src/desktop.cpp`): the picture the PS1 draws, 512 columns
  by the 216-line draw region (80011bbc) at H 341 with the 320/512 vertical correction: 73.7 x 53.8
  degrees, fitted to the window (wider windows keep the vertical, narrower ones the horizontal).
  The PC default stays the full 240-line display (58.8 degrees vertical).
- **PS1 HUD size** (default off; `draw_hud`): one retail line per 1/240 of the height, the 4:3 pixel
  width (0.625), the retail x positions in a centred 4:3 frame (gems 40, 8005251c from 80053f5c; the
  life portrait 208, 80052b88; spirits 408, 80053fa8); the y positions were already retail.
- Settings: `FrontendPreferences` (S2MENU 5: delay, PS1 FOV, PS1 HUD), the pause menu's Options page
  (a "PC options" box: Camera recentre, PS1 FOV, PS1 HUD size; the Upgrade HUD / Terrain damage /
  Upgrades entries moved into it) and the workbench Game menu (a slider and two toggles). Session
  flags `--camera-recentre N`, `--ps1-fov` / `--pc-fov`, `--ps1-hud` / `--pc-hud`, `--recall-hud`
  (holds the HUD recall for photos).
- Photos (opened): `data/validation/pc-view-options-20261104/fov-before-after.png`,
  `hud-before-after.png` (Summer Forest, 1280 x 720, hidden runs). The pause-menu page itself was
  not photographed.

### Zoe's wand sparkles, and the zap (agent 8, the user's report)

- **The type-0C sparkle grew to several times its size.** Its update (Glimmer `8007f588`, overlay 70
  `80089a68`, the same in every overlay) runs with its base register at the record + 1, so the
  "+0A += 8" agent 2 read as size growth is the spin byte +0B (8 / 256 of a turn per update); the size
  (+0A, 32 + rand & 7, four units per size) never changes; the colour channels fade 8 per update.
  The renderer grew the sprite from about 0.14 to 0.9 units over its 48 frames; it now keeps it and
  spins it (`src/renderer.cpp`). The particle hook spawns them as `80080720` does: the offset table
  `800644f0` turned by the moby's matrix (8001b9a4), variant 5 / 6 jitter, drift = offset / 4 - 4 +
  rand & 7, colour `800644d0[variant]` (`src/retail_scripts.cpp`). This also applies to the reward
  and spirit sparkles (the same type).
- The zap bolt (8004b120 table) is drawn from her wand point (80041730, attachment 0) to Spyro, as
  before. Diagnostic `SPYRO_BOLT_DEBUG=1` prints the bolt's ends against the Zoes.
- Photos (opened): `data/validation/zoe-sparkle-20261104/zoe-before-after.png` (agent 2's sheet
  against this build, the same 6.00 .. 7.33 s), `zoe-after-sheet.png`, `zoe-zap-6.50s.png`. The user
  confirmed the fix.

### General Spyro 2 PC package 20261104b (agent 8)

- `dist/spyro2-pc-20261104b.zip` (4,543,221 B, SHA-256
  `0be454322a5038d92238385c6d6c4bcee0c0a82ed456bb067742229eccfaa9b8`), exe `f657c959...b906` (the Zoe
  fix and the Cloud Temples changes). ctest `tmp/agent8/ctest2.log` line 87 "100% tests passed, 0
  tests failed out of 42"; the 18 exits `tmp/agent8/level-exits2.log` (18 OK lines, rc=0). Smokes of
  the unzipped package: `data/validation/spyro2-pc-package-20261104/b-smoke-15-summer-forest.png`,
  `b-smoke-17-glimmer.png` (opened). Spring Realm refreshed with the same exe:
  `dist/spring-realm-20261103.zip` `d981f5c7...305f`, smoke `package-refresh-smoke-7.png` (opened).
- The 20261104 zip is retired: moved to `tmp/agent8/retired/` (with its folder) for deletion; its
  `.sha256` stays in `dist/`.

### General Spyro 2 PC package (agent 8, coordinator's request)

- `dist/spyro2-pc-20261104.zip` (4,534,672 bytes, SHA-256
  `89a29a13e94a7f13add710710e8e773000d8436058569c75ada374d4170dca17`, also in the `.sha256` beside it)
  from the last fully verified main build (`spyro_workbench.exe` `5055ed71...fbf8d`, agent 7's).
  Folder `dist/spyro2-pc-20261104/`: the exe, the app-local VC++ 14.51.36231 runtime (msvcp140,
  msvcp140_atomic_wait, vcruntime140, vcruntime140_1, from Visual Studio's redist folder), launchers
  `Play Spyro 2.bat` (title menu), `Run Workbench.bat` (Summer Forest, workbench open), `Play Spring
  Realm.bat`, `Play Retail Ripto.bat` (all ask once for the user's own disc image and share
  disc_path.txt, as the Spring Realm package does), the Spring Realm world and tiles, README.txt
  (disc path, controls, what works / what is not finished), THIRD-PARTY-NOTICES.txt. No disc data.
- Hidden smokes from the unzipped package (a temp folder), against the retail CUE: Summer Forest home
  (slot 15) and Glimmer (slot 17), 300 frames each, scripts active, no faults, music playing; photos
  and reports in `data/validation/spyro2-pc-package-20261104/` (opened: both scenes drawn correctly).
- Tools: `tmp/agent8/make_zip.py` (zip + .sha256).

### In-game orb challenges (agent 8): Cloud Temples' bells, PAUSED mid-batch (coordinator's order)

- **State at the pause: source changed, only `spyro_challenges` rebuilt; the workbench, ctest,
  the other challenges, the level exits and the package are NOT rebuilt or re-run.** The main
  `build/Release/spyro_workbench.exe` is still agent 7's (`5055ed71...fbf8d`); `dist/` untouched.
- **Cloud Temples' bells PASS headless** (`spyro_challenges data/disc cloud-temples-bells`: 6540 ticks,
  orb bit 1 from Murgen's own talk; no `SPYRO_QUEST_SPEAKERS` needed). Not yet run in the workbench.
  The retail route (traced in overlay 60): after the first bell Murgen (1C7 kind 3, `80078134`)
  zeroes the kind (header +08) of the whirlwind behind him (#10) and says "use the whirlwind behind
  me"; it lifts Spyro to the bell tower's walkway (27.5); the troll there (#180), frozen, is the step
  into the bell room (floor 30); a troll in the room (#67), frozen, is the step to bell #25.
- Engine changes (all in the source, not yet in a workbench build):
  - `src/powerups.cpp`, `include/spyro/powerups.hpp`: the superfreeze (gate kind 7, `8006a1cc`) now
    fires the retail freeze shot (class 270, Cloud Temples `8007d714` / spawn `8007fa6c`: 340 units a
    tick, 180-frame life, moby query radius 700 and burst radius 1000 with attack 01000000) instead of
    a blue flame stream (`80030738` picks 270 over 1E3). Native susceptible objects freeze on the
    01000000 bit (`freeze_attack`), not on flame + ice.
  - `src/runtime.cpp`: the flame stream is suppressed while the freeze shot is available.
  - `src/retail_scripts.cpp`, `include/spyro/retail_scripts.hpp`: `level_script_class` hands Cloud
    Temples' trolls (1EE) and their ice blocks (271, solid through their collision bank) to the
    interpreter; `challenge_giver(slot, class, kind)` hands over Cloud Temples' 1C7 kind 3 only
    (Murgen; Bartle, kind 4, stays native); a native whirlwind's kind is re-read from RAM each tick
    (`whirlwind_kinds` counter).
  - `tests/level_system_tests.cpp`: the troll freeze test uses the freeze shot's bit.
- Tools: `tools/challenge_pilot.hpp` (`ride_whirlwind`, `freeze_climb`, `cross_gate` reads dialogue
  and accepts the superfreeze, the bells entry rewritten, env `FREEZE_DEBUG`), `tools/challenges.cpp`
  (`heights SLOT X0 Z0 X1 Z1 STEP`: floor layers along a line).
- Scratch files: `tmp/agent8/` (backups of the edited files in `tmp/agent8/backup/`, run logs,
  listings, `fix_eol.py`, `freeze_step.txt`).

## 2026-11-03

### In-game orb challenges (agent 7): Dem bones, Robotica's crow bugs, Cloud Temples' bells (2 of 3)

- **Skelos Badlands' Dem bones complete** (Ooga, 62). The targets were never the class 26 lava
  toads: Ooga waits for 80066fec == 8, the bones. Eight bone holders (class 59) throw a bone
  (class 65) on any hit; Spyro picks each up within 600 units. New pilot step `hit_and_collect`.
  Why the toads looked unhittable is in `docs/ORB_CHALLENGES.md` (variant 4 hovers 1900 units over
  the floor while Spyro is near; a full held jump with Circle at the top reaches them, now the
  default of `lure_and_attack`'s jump).
- **Robotica Farms' crow bugs complete** (Farmer Applebee, 10D). `challenge_giver` hands Robotica's
  10D farmers to the interpreter. The bugs flee a perched approach (4 .. 7 units) but check nothing
  while taking off, flying or landing; all four must be caught within 1800 frames of each other.
  New pilot step `ambush_perchers` (round to the far side of the scarecrow, step in on the bug's
  line, flame it standing or from a jump as it takes off over Spyro). 1 fixture (the giver).
- **Cloud Temples' bells: two of three** (Murgen, 1C7; run with `SPYRO_QUEST_SPEAKERS=32`: handing the
  whole class over broke the level-exit check, Bartle's completion talk, so it was reverted). A jump, then a
  charge in the air, rings bells #23 and #27 (new pilot step `charge_jump_each`); #25 hangs 0.6 units
  higher and needs the frozen troll under it as a step (class 1EE is still native: no ice block).
  Catalogue entry `cloud-temples-bells` FAILs on purpose until then. Next steps in the doc.
- Probed, not finished: Crystal Glacier's snow leopard (George, 5E, runs his route when Spyro is near).
- Removed nothing; all changes: `src/retail_scripts.cpp` (`challenge_giver`: (65, 10D)),
  `tools/challenge_pilot.hpp` (the steps above, `watch_objects` diagnostic, env-gated debug prints
  `LURE_DEBUG`, `AMBUSH_DEBUG`, `ATTACK_DEBUG`, `ATTACK_TERRAIN`), `docs/ORB_CHALLENGES.md`.
- Verification: 42/42 ctest; 19/20 challenges PASS headless (`spyro_challenges data/disc all`; the
  bells entry FAILs on purpose); the same 19 PASS in hidden workbench runs (sheets in
  `data/validation/challenges-20261103/`, including `skelos-dem-bones-sheet.png` and
  `robotica-crow-bugs-sheet.png`); the 18 level exits PASS (Cloud Temples and Robotica rerun after the
  1C7 revert; `data/validation/level-exits-20261103/`). Workbench `build/Release/spyro_workbench.exe`
  SHA-256 `5055ed71f26bee91fd4d6b56c14ecc27d3a8596ef53e21b4146a9c1b855fbf8d`.
- Spring Realm package refreshed: `dist/spring-realm-20261103.zip` SHA-256
  `61ba8901dcb0ae95b00bbcf493d80d23aa9a5c8b1ade4cd15a62e386f1201a1f` (exe equals the main build);
  hidden smoke from the unzipped package:
  `data/validation/spring-realm-package-20261103/package-refresh-smoke-6.png` / `.json`.

### Season of Ice phase 2 (part 3, agent 3): Market Mesa v2f look pass

- `tools/soi/mesa_textures.py` (new): 20 authored 64 x 64 tiles painted after SoI's map art (sandstone
  cobbles, cobbles with a puffy snow foot, slate roof courses, pale-cyan coping, plaster / window / door,
  framed hatches, mint pine fronds, pink cloth, pale pole banner, orange wall banner, stall stripes, far
  rock) -> `data/soi/pc/textures/*.rgba` (+ `png/`, `preview.png`). Loaded through S2WORLD 4 slot -2.
- `tools/soi/mesa_props_blender.py` (new): props modelled in headless Blender 5.2 (bmesh): SoI's pagoda
  pines (68 / 43 triangles), market stall (44), pink cloth rack (46), flagpole (17), crate (10). Master
  `data/soi/pilot/mesa_props.blend`, export `data/soi/pc/mesa_props.json`, review render
  `data/soi/pc/shots/props-v2f.png`.
- `tools/soi/mesa_build.py`: cliffs and risers in 48-z rows aligned to the terraces (no slivers), SoI
  sandstone with snow only in the foot row; building walls over a drop are plaster for at most 120 z;
  island undersides replaced by one Delaunay cap per island on the cliffs' own bottom vertices
  (parity-classified, closure-checked: 0 open edges; 1,725 -> 555 triangles); mitred coping wedges on
  every roof edge (overhang + parapet), hanging orange banners, bridge parapets, quarter arches at the
  bridge ends; decor keeps its own vertices; thin cloth lit from both sides. `mesa_decor.py`: rack
  detection from SoI's magenta cloth, Blender prop instancing.
- `tools/soi/soi_pc_proof.py`: `sheets3` (before / after vs the previous iteration, retail with
  hand-placed Shady Oasis courtyard cameras, SoI angle beside the map), `views` prefix / out_dir.
- v2f: 13,797 render triangles (360 of them distant scenery), 26 tiles. Sheets
  `data/soi/pc/shots/sheet-v2f-{before-after,vs-retail,soi-angle,gap-hunt}.png`. v2e kept as
  `data/soi/pc/market-mesa-v2e.s2world` (retail tiles only) for before / after sheets.
- PC proof run: route "mesa" re-probed and rescripted on v2f (two flames, a tap jump across the trench, a held
  jump up a riser): enemy defeated, gem counter 0 -> 16, health 3. Sheet `data/soi/pc/shots/proof-v2f-route.png`,
  report `proof-v2f-route.json`.
- Glimmer slot dependency check for the PS1 build (`docs/SEASON_OF_ICE_PORT.md` §10.8): empty the gameplay
  child's seven terrain-animation sections (retail `8001c734` / `800225f8` accept count 0) and drop Moneybags
  (class 0x220, the only caller of `8002365c` / `80023710` in overlay 16).
- No runtime change, no `src/` change, nothing built.
- **v2g polish** (coordinator decisions: keep SoI's 24-z jump steps and flat walkable roofs): a 24-z snow row at
  every cliff base (rows now 25-49 then 49 + 48k, still on the terraces; lit near-white), cliffs ~8 % lighter,
  hatch blobs become separate 16 x 16 framed panels a block apart (`separate_hatches`; the rest of each blob
  takes roof or paving). 14,435 render triangles (360 distant scenery). Sheets
  `data/soi/pc/shots/sheet-v2g-{before-after,vs-retail,soi-angle,gap-hunt}.png` (before = v2e).
- **The stunted step jump was Market Mesa's geometry, not the engine.** In the PC build on retail Glimmer, a jump
  pressed while pinned against a 1.1-unit ledge (collision wall 2962 near the start) rises 1.6 units and lands on
  top. At Market Mesa's 409 step it rose 9 z: agent 2's convex-corner bevel pulled the corner vertex in, and the
  T-junction fan joined it across a 14-unit wall span into a tilted triangle (normal ~0.54 vertical) that acted as a
  ceiling. Bevel removed; the same pinned jump now clears the step; the proof route still passes (16 gems, health 3).
  Not yet checked in the retail game itself (DuckStation).
- PS1 codecs, byte-exact on all 30 retail scenes (15-73): `tools/soi/s2collision.py` (collision chunk: header,
  three-level u16 grid tree, lists, triangles, both surface byte arrays, the optional trailing block; plus a
  conservative cell builder for new levels), `tools/soi/s2regions.py` (terrain region chunk),
  `tools/soi/s2scene_build.py` (the scene child rebuilt with regions and collision re-encoded). Handoff:
  `docs/SEASON_OF_ICE_PORT.md` §10.9.
- `tools/soi/ds_harness.py` (new, TEST FIXTURE): hidden DuckStation runs on a separate Win32 desktop with its GDB
  server (this build has no PINE); the first run booted retail Spyro 2 and read RAM over GDB, later runs stop at an
  unread "Error" dialog on the hidden desktop (open). Portable DuckStation: BIOS copied from the user's SCPH-39001
  dump into its own `bios/`, `settings.ini` with the GDB server on. Region header fields fitted (§10.9 addendum).
  Handoff: `docs/SEASON_OF_ICE_PORT.md` §10.10.

### Season of Ice phase 2 (part 2, agent 2): Market Mesa v2 to the retail bar

- `tools/soi/s2scene.py`: Spyro 2 scene reader (every region byte: header, far mesh, near mesh; the terrain atlas
  from the scene VRAM). `tools/soi/s2_terrain_metrics.py` -> `data/soi/ps1/terrain_metrics.json`: the retail bar for
  23 scenes (faces, slopes, creases, edge lengths, materials, vertex-colour lighting). Atlases with tile numbers:
  `data/soi/retail-atlas/atlas-{15,17,19,33,39,41,47,61}.png`.
- `tools/soi/mesa_build.py` + `tools/soi/mesa_decor.py` -> `data/soi/pc/market-mesa.s2world` (v2, overwritten; the
  box export is kept as `market-mesa-blockout.s2world` / `.json`) and `market-mesa.mesh.npz`: watertight lattice
  terrain (T-junctions repaired), bridges as slabs, plaster buildings, sculpted cliffs and closed island undersides,
  retail tiles chosen per surface from SoI's art, baked vertex lighting, trees / stalls / banners / flagpoles placed
  from the SoI map, a cloud sea and distant mesas. 14,084 render triangles, 20 tiles.
- `tools/soi/soi_pc_proof.py`: hidden S2SHOT runs of the private `build-soi/spyro_workbench.exe` (a copy, not
  rebuilt; `build/Release` is never run): route probes, named views, before / after, vs-retail and 360-degree
  gap-hunt sheets in `data/soi/pc/shots/` (`sheet-v2{a,b,c}-*.png`, `v2-*.png`).
- Iterations v2d / v2e after the coordinator's review: SoI palette (pink flagstone paving with border tiles,
  white plaster, pale-blue roofs, snow rims under every island), doors and windows, bevelled building corners,
  2 % wall stretch (was 23 %), warmer light with stronger AO, rounder trees, crates, coarser undersides; sheets
  `sheet-v2{d,e}-*.png`. 15,004 render triangles (about 430 of them distant scenery).
- No runtime change, no `src/` change, nothing built. Handoff: `docs/SEASON_OF_ICE_PORT.md` §10.6 and §10.7.

### In-game orb challenges (agents 5 and 6): flight pilot, Party Crashers, the thieves in the workbench

- **Agent 5 (cut off, no handoff), recovered:** its only change was `tools/challenge_pilot.hpp`:
  `chase_route` rewritten (Mystic Marsh's thieves now pass in 4520 ticks headless, was 34486),
  the `sweep_targets` ring tour (Crystal Glacier's draclets complete), and the draclets and Party
  Crashers catalogue entries. The workbench did not compile it (`near`, a variable, is a
  `windows.h` macro); agent 6 renamed it and kept the rest.
- **Magma Cone's Party Crashers complete** (Chedda, 13B). The lava monsters (128) die only on
  attack bits 10000000 / 100000, never on a flame: the retail way is to carry a lava bubble (91,
  blown by the fountains) and spit it at them (it aims itself, `8007a59c`). New pilot steps
  `fly_to` (superfly steering that minds the turning circle and flies round walls) and
  `carry_shots` (bubbles, a straight run at each monster, the spit inside the retail cone, the
  flight refilled by flying back through the gate; the volcano lid is avoided: over it the level
  ends).
- **Engine fixes found by the challenges** (`docs/ORB_CHALLENGES.md`):
  - Spyro's pitch is mirrored for the scripts (`8006a058`, `80069ffd`; `8002a1f0`): carried
    things aim their spit with it.
  - A giver's placement ends a superfly flight (mode 11 posts request 10010480; bit 80 sets the
    state `8006a22c`, 0 = standing). Chedda never talked to a still-flying Spyro.
  - The flame queries the mobys at each advanced point before its terrain stop (`8002f4f4`
    before `8003ce38`), on a separate list (`flame_query_points`) so the native systems are
    unchanged.
- Investigated, not finished: Skelos' Dem bones (catalogue entry `skelos-dem-bones`, 2 of 5
  toads), Robotica's crow bugs (flee radius traced). Details and the next steps in
  `docs/ORB_CHALLENGES.md`.
- Verification: 17/17 challenges PASS headless and in hidden workbench runs (sheets in
  `data/validation/challenges-20261103/`); the new `skelos-dem-bones` entry FAILs as expected
  (2 of 5 toads); 42/42 ctest; the 18 level exits PASS. Workbench
  `build/Release/spyro_workbench.exe` SHA-256
  `58c4ba33b6d87ff37c6481e33bc7432f42d2a010f29fbe51bdce16efb8e0925d`.
- Spring Realm package refreshed: `dist/spring-realm-20261103.zip` SHA-256
  `60b53e2946e92868afe751d3f9dc8317c8f550f272a4cbecef58f76cf15e6d76` (exe equals the main
  build); hidden smoke from the unzipped package:
  `data/validation/spring-realm-package-20261103/package-refresh-smoke-5.png` / `.json`.


### Season of Ice phase 2 (part 1): sprite families and Market Mesa on PC

- `tools/soi/soi_sprites.py`: SoI sprite banks decoded (header "CRS", 5-byte frames, presence-bit
  tile grids; the bits match every bank's tile count) and rendered per level in `data/soi/sprites/`.
- `tools/soi/soi_families.py` -> `data/soi/families.json`: every object family named from the sprite
  slot its spawner uses; `level.json` / `inventory.json` regenerated with families and Spyro 2
  stand-ins (placeholders marked).
- `tools/soi/s2world_export.py` -> `data/soi/pc/market-mesa.s2world`: heightfield terrain (11,662
  triangles) textured with retail Shady Oasis tiles by surface type, gems, stand-in enemies, baskets,
  sheep and a Return Home arch. Loads in a hidden run of a private exe copy (`build-soi/`); shot
  `data/soi/pc/shots/smoke-start.png`. **No runtime change and no hooks yet**; the main exe was not
  rebuilt or run.
- `docs/SEASON_OF_ICE_PORT.md` §10 holds the phase 2 status and the handoff.

### Season of Ice port: prep (new project; no runtime change)

- Plan and findings: `docs/SEASON_OF_ICE_PORT.md`. Nothing in `src/` changed; the main exe was not
  rebuilt.
- SoI ROM (ASYE) reverse engineered from its own code (headless Ghidra, project on D:): BIOS LZ77
  backgrounds (`08016780`, `08005970`), solid-prism collision and heights plus sprite occluders
  (`08013cd0`, `080137e0`, `08013e54`; the broadphase grid lists only solids), object lists and
  start points (`08016594`, spawn switch `080170a4`), projection (`0801ec60`: x' = 2(x - y),
  y' = x + y - z), text (`08270434`), level names and worlds (`081ac190`, atlas `081aace0`).
- `tools/soi/soi_extract.py`: all 17 isometric levels to `data/soi/levels/` (map, overlay, top-down
  heights, JSON), `data/soi/inventory.json`, contact sheet, text JSON.
- `tools/soi/ps1disc.py`: PS1 disc model and EDC/ECC encoder; **byte-exact round trip** of the retail
  BIN (SHA-256 `b3fd9fdb...dec64551`), WAD.WAD rebuilt from its 197-slot TOC. The staging folder moved
  from `D:/Spyro2-mod-staging` to `D:/Theia-staging/spyro2-mod` (inside safe_delete's roots); the
  round-trip copy there is deleted. Write future mod images under `D:/Theia-staging/spyro2-mod/`.
- `tools/soi/s2_budgets.py`, `tools/soi/s2_class_census.py`: retail budgets and class census
  (5,200 records / 1,669 gems, matching the existing docs) in `data/soi/ps1/`.
- Pilot: **Market Mesa**. `tools/soi/blender_blockout.py` (Blender 5.2 headless):
  `data/soi/pilot/06-market-mesa_blockout.blend` and renders; the SoI-camera render lines up with
  the ROM map (silhouette IoU 0.90).
- Note: the first Blender run wrote its renders to `C:/data/...` (relative paths); they were moved
  into `data/soi/pilot/renders/`, the script now uses absolute paths, and the empty `C:/data` folders
  are left for the user (safe_delete refused them).

### In-game orb challenges (fourth agent, second batch): the thieves

- **Shady Oasis' lamp thieves complete** (Bruno, 188; headless and workbench). Mystic
  Marsh's spark plug thieves (Basil, 1FE) complete headless only: the workbench run
  reaches its 40000-frame cap with one thief left.
  `challenge_giver` adds Shady Oasis 188 and Mystic Marsh 1FE.
- **The camera's heading is mirrored for the scripts** (`80067ecc`): Skelos Badlands'
  lava toads come out only in front of the camera.
- Tried again, still open (findings in `docs/ORB_CHALLENGES.md`): Dem bones (the pool
  toads never take a hit), the draclets and Party Crashers (the flight pilot is too
  slow on moving targets); tried: Robotica's bugs, Shady Oasis' hippos, Scorch's
  monkeys.
- Tools: pilot steps `chase_each`, `lure_and_attack`, `until`; `tools/find_writers.py`
  (which class writes a RAM word); `run_challenge_checks.py` gives the marsh thieves
  40000 frames (the workbench's cap for a challenge check).
- Verification: 15/15 challenges PASS headless; 42/42 ctest; 14/15 workbench checks PASS
  (the marsh thieves hit the frame cap); the 18 level exits PASS (Shady Oasis and
  Mystic Marsh with their givers now script owned). Workbench SHA-256
  `f2b10bbe5cd4879ccb991ea2fdbed537e9812f68b6d43f96fc07a02e874fe52d`.
- Spring Realm package refreshed again: `dist/spring-realm-20261103.zip` SHA-256
  `dbef585821fa5c4d42714e5842f452078f472b379de4155800ce7dbd43acf402` (exe equals the
  main build); hidden smoke from the unzipped package:
  `data/validation/spring-realm-package-20261103/package-refresh-smoke-4.png` / `.json`.

### In-game orb challenges (fourth agent): Sunny Beach, Skelos Badlands' lizards

- **Five more speaker challenges complete through the original code** (hidden
  headless and workbench runs): Sunny Beach's turtle boxes (Advisor Gromit, B6),
  turtle soup I and II (Master Chef, BB), Skelos Badlands' lava lizards I and II
  (Lumpy, 62). Table, evidence and handoff: `docs/ORB_CHALLENGES.md`,
  `data/validation/challenges-20261103/`.
- **Superflame shots reach script mobys the retail way** (`src/powerups.cpp`): the
  moby-sphere query (`8004055c`, attack 970000) runs at birth with radius 600, every
  flying tick after the terrain test even when that test stopped the shot (a solid
  script moby's collision bank is a terrain hit), and in the explosion. Sunny Beach's
  boxes (BA) were unbreakable before.
- **Checkpoint restarts reload the mobys** (`RetailScripts::reload_mobys`): `8001722c`
  reads the gameplay section again and runs the gameplay loader `8001c734` (which
  applies the checkpoint's persistent list). A script calling `8004c1ac` from a state
  it never left (the Master Chef's bell) used to hold game state 3 for ever.
- `challenge_giver` adds Sunny Beach B6 and BB and Skelos Badlands 62.
- Checked, no change: Spyro's red-brown colour in the Metropolis saucer photos is the
  retail superflame tint (`80031eac`, timer `8006a1c8`, which the Inventor Droid
  fills from the superfly timer at overlay 66 `8007cad4`).
- Tools: `spyro_challenges <disc> objects SLOT [CLASS,...]`; pilot steps `walk_to`,
  `in_box`, `turtle_round`, `attack_class`, class filter for `defeat_for_gate`, a jump in
  `cross_gate`, in-play photos.
- Investigated, not finished: Skelos' Dem bones, Crystal Glacier's draclets, Party
  Crashers in the challenge runner (see the table).
- Handoff data: `data/analysis/challenge-surveys/objects-SLOT.txt` (object and group
  dumps of the 16 levels with challenges left), `data/analysis/moby-cases/overlay_026/`
  (Sunny Beach decompiles: B3, B5, B6, B7, BB, BC).
- Verification: 13/13 challenges PASS headless (`spyro_challenges <disc> all`) and in
  hidden workbench runs (`data/validation/challenges-20261103/summary.json`, sheets);
  42/42 ctest; the 18 level exits pass (Magma Cone through one checkpoint restart, now a
  reload). Workbench `build/Release/spyro_workbench.exe` SHA-256
  `6216a6ec2c7832d4ae6b46334cf39bd351d2a98581eaf226dd69805e9f3801ec`.
- **Spring Realm package refreshed** (`tools/package_spring_realm.py --date 20261103
  --with-blender`): `dist/spring-realm-20261103.zip`, SHA-256
  `80c99fd8281aa12b68f1e589636cc1ff4daccaa3769303c1844d92e7751d3f6c`; its exe equals the
  main build. Hidden smoke from the unzipped package (300 frames, the Spring Realm):
  `data/validation/spring-realm-package-20261103/package-refresh-smoke-3.png` / `.json`.
  (The workbench has no `--hidden` flag; `--smoke N` runs without showing a window.)

### In-game orb challenges (third agent): hockey, gem lamps, sheep saucers, statues

- **Eight speaker challenges complete through the original code**, each with its own
  autopilot and a hidden workbench run: Colossus' hockey against the keeper and one
  on one (Brother Kipp, 10F), Colossus' ten statues (the Professor, 193), Glimmer's gem
  lamps by the pyramid and in the cave (Kanga and Roo, 22A, with the superfly gate),
  and Metropolis' coup, first and second wave of sheep saucers (the Inventor Droid,
  251, with the superfly + superflame gate). Table, method and evidence:
  `docs/ORB_CHALLENGES.md`, `data/validation/challenges-20261103/`.
- **Challenge givers run their class code** (`challenge_giver`, `src/retail_scripts.cpp`):
  Colossus 10F and 193, Glimmer 22A. Their code starts the challenge, referees it and
  opens the reward line itself.
- **The level's own challenge HUD.** Every level overlay's HUD hooks (`80067154` update,
  `80067158` draw) run each script tick with the resident counter code (`8005199c`,
  `80052498`, `8005251c`, `80052690`, `80052960`); the sprite primitive `800520cc` is
  recorded (`RetailScripts::hud_draws`) and the workbench draws the sprites from the
  level's HUD page, decoded on first use (`scene_sprite`, `InterfaceAssets::scene_vram`).
  Hockey shows its timer and both scores this way. Crush, Gulp, the speedways and Breeze
  Harbor keep their native HUDs.
- **Engine fixes the challenges needed (all general):**
  - script-spawned mobys after a free slot were never mirrored or drawn
    (`sync_out` stopped at `80066fd4`, the lowest free slot, instead of the list's -1
    marker): the hockey puck and players' children;
  - `8006a104`, the floor's surface flag byte, is written for the scripts
    (`Triangle::surface_flags`): the puck is only picked up on ice;
  - superflame shots and green bombs reach script mobys through the retail moby-sphere
    query (`RetailScripts::attack_query`): the saucers.
- Tools: `spyro_challenges` (`tools/challenges.cpp`; `list`, `all`, `hooks`, `lines SLOT
  OBJECT`), the shared autopilots `tools/challenge_pilot.hpp`, the workbench's
  `--challenge-check NAME` (`src/desktop_challenge_check.inl`; photos of the composited
  view, with the HUD), `tools/run_challenge_checks.py`, `tools/find_class_case.py`
  (a class's dispatcher case) and `tools/export-case-ranges.ps1` (Ghidra, explicit ranges).
- Verification: all eight challenges PASS headless (`spyro_challenges <disc> all`) and in
  hidden workbench runs (`data/validation/challenges-20261103/summary.json`, contact sheets
  `*-sheet.png`); 42/42 ctest; the 18 level exits re-run with this workbench
  (`data/validation/level-exits-20261103/`). Workbench `build/Release/spyro_workbench.exe`
  SHA-256 `d1775f56b010f86b919d79b3686fe44bc00a3fb5bce33b12d53601a73b5e6bb4`.
- **Spring Realm package refreshed** (`tools/package_spring_realm.py --with-blender`):
  `dist/spring-realm-20261103.zip`, SHA-256
  `c42b874b971cc1a68a37001f6082c6dd57762335f6b42b13eead48fc6c400d77`; its exe equals the
  main build. Hidden smoke from the unzipped package (300 frames, the Spring Realm):
  `data/validation/spring-realm-package-20261103/package-refresh-smoke-2.png` / `.json`.

### Level completion, second pass: Magma Cone, checkpoint restarts, camera paths

- **Magma Cone completes.** Its host (13B, kind 2) waits for the crater trigger (401) and then
  restarts the level at a checkpoint before itself (`8004c1ac`) before talking by itself; three
  engine gaps blocked it (Ghidra: `data/analysis/moby-cases/overlay_048/`). Details in
  `docs/LEVEL_COMPLETION.md`, "Second pass".
- **Checkpoint restart (game state 3).** `RetailScripts::advance_restart` plays `8001722c`: fade out,
  Spyro at the stored checkpoint without losing a life, the camera back behind him, fade in, game
  state 0 (it used to stay 3 and block every later script conversation). Used by many quest givers
  (Colossus 10F, Sunny Beach BB, Zephyr C5, Scorch 1B9, Mystic Marsh 25C, ...).
- **Scripts start native whirlwinds.** A state a script writes into a native whirlwind (class 400)
  is adopted (Magma Cone's 401 raises its whirlwind this way).
- **Camera mode 10 plays the level's camera path** (`8001fa58` case 10): keys, blend-in, fades,
  hold / loop / end flags, `80066f68` and `80067100`; it used to aim at the path as if it were a moby.
  `RetailScripts::screen_fade()` (restart and path fades) is drawn by the workbench.
- **Breeze Harbor's talisman presentation.** Squawk's native burner quest now starts the state 4
  reward (talisman rising, sparkles, Zoe) like every talisman speaker; Return Home waits for it.
- **The follow camera stops at portal apertures** instead of passing into a doorway's panorama.
- Workbench `--level-exit-check`: Magma Cone uses the level's own end trigger (Spyro in the 401
  rectangle, report `end_trigger`, `checkpoint_restarts`, photo `-0-end`); the home photo waits for
  150 game ticks in the home world; a `photo` log line per photo (camera, doorway sides, reward item);
  Return Home photo from 3 units.
- `tools/run_level_exit_checks.py --sheets`: contact sheets (one row per level).
- `spyro_level_exits`: `DUMP_HEADER=1`, `TRIGGER_401=1`. New probe `spyro_level_walk`
  (`tools/level_walk.cpp`, `tools/level_walk.hpp`): a navigation graph over a level's floors and an
  input-only autopilot (work in progress), plus `--portals`, `--arrival SCENE`, `--triangles`,
  `OBJECTS=`, `FIND_TYPE=` diagnostics.
- Hidden runs: all 18 talisman / Winter Tundra levels PASS (`data/validation/level-exits-20261103/`,
  `sheet-1.png` .. `sheet-3.png`).
- **Fracture Hills: no more standing in mid-air at the end.** The six satyrs (class 8A) run their
  own code: freeing them raises the end plateau and shows its floor (region 82). Walk evidence in
  `data/validation/level-exits-20261103/fracture-hills/`; the check frees them first.
- An object animation transition between clips of different meshes shows the target frame instead
  of throwing (freeing a satyr ended the workbench).
- **Camera mode 11** fades out, places Spyro, fades in (scripts keep running; fixes Colossus 11A's
  spirit). `retail_overlay_scripts` updated for the fade; all 42 ctest suites pass.
- **The reward camera**: views 3 / 4 of `8006163c` during the talisman / orb reward (80016328).
- **Zoe's zap**: the `8004b120` bolt table drawn natively (her wand-to-Spyro bolt) and type-0C
  sparkles emitted from the scripts' particle requests. Evidence `zoe-zap/`.
- **Spring Realm package refreshed** with this workbench (`tools/package_spring_realm.py --with-blender`):
  `dist/spring-realm-20261103.zip`, SHA-256 `d973f39bdf3b54703f3fc13fdd60a4d7a0bab5a055044e58dcc94bf793031981`
  (`.sha256` beside it); the packaged exe equals `build/Release/spyro_workbench.exe`. Hidden smoke
  from the unzipped package (300 frames, the Spring Realm loaded):
  `data/validation/spring-realm-package-20261103/package-refresh-smoke.png` / `.json`.

### Spring Realm package and fused Blender source

- **Workbench: hide / show (F1).** While playing, F1 (or *Game > Hide workbench*) minimises
  the workbench panels to a full-window game and brings them back; the pause menu's
  *Workbench* entry also shows them again. A notice says how to get them back.
  `--hide-workbench N` presses the real F1 key at frame N of a bounded run and fails the run
  unless the workbench was hidden (`src/desktop.cpp`).
- **`spyro_scene_export` writes the scene's sky** (`sky.obj`, vertex-coloured triangles per
  region, camera-centred) beside the terrain (`tools/scene_export.cpp`).
- **`tools/fuse_spring_realm_retail.py`** (Blender, headless): a copy of the Spring Realm
  master with WAD 73 freshly imported from the disc (terrain, collision, sky, atlas), the
  world's exclusion boxes, liquid boxes and tile overrides applied as the game does, saved
  under a new name, plus textured and false-colour preview renders. The master is not
  written.
- **`tools/package_spring_realm.py`**: `dist/spring-realm-<date>/` + `.zip` (+ `.sha256`):
  the release workbench, the world and its tiles, a launcher that asks once for the disc
  (`disc_path.txt`) and starts straight into the Spring Realm with the workbench open, a
  README and the Dear ImGui notice. Refuses disc files, PDBs and .blend files.
- Built: `dist/spring-realm-20261103.zip`, `dist/spring-realm-20261103-blender/`.
  Evidence: `data/validation/spring-realm-package-20261103/`.

### Spring Realm: Hamlet Green removed, east meadow closed (user request)

- The user removed the Tripo area Hamlet Green (in a copy of the fused .blend). In the
  source: `stage_tripo_areas.AREAS` is empty (the entry moved to `REMOVED_AREAS`), so the
  area's objects go and `stage_ground` no longer cuts its footprint; the master was rebuilt
  with `rebuild_spring_realm.py` (226 -> 221 objects: only the five Hamlet objects gone) and
  re-exported (117,080 triangles, 46 materials). `spyro_spring_realm_tests`: 0 failures.
- Backups: `Spring Realm.before-hamlet-removal-20261103.blend`,
  `pc/spring-realm.before-hamlet-removal-20261103.s2world`, `pc/budget.before-...json`,
  `source/stage_tripo_areas.before-hamlet-removal-20261103.py`, and the user's edited copy
  `dist/Spring Realm + Dragon Realm intro (fused).user-edit-20261103-1342.blend`.
- `tools/capture_spring_realm.py`: `--out DIR`, `--world FILE`, east-meadow views.
- `tools/package_spring_realm.py --with-blender`: the zip also carries the fused .blend
  folder (as the user packed it); a refresh overwrites in place and deletes nothing.
- Evidence: `data/validation/spring-realm-hamlet-patch-20261103/`.

### Level completion: talisman / orb, Return Home, outro (task 2)

- Reverse engineered the retail end-of-level flow; see `docs/LEVEL_COMPLETION.md`.
- `RetailScripts`: talisman speakers without a native reconstruction run their own class
  code (they arm `80066f44` on their own condition and talk when Spyro arrives); a native
  kind-1 speaker is armed as Glimmer's 22A does (not Breeze Harbor's Squawk).
- `npc`: lines record command 1 (`talisman`) and commands 2..9 (`orb_bits`).
- `Game::RewardShow`: the state 3 / 4 reward presentation after the conversation (talisman
  rises and turns with sparkles and its sound; orb flies to Spyro); Spyro is held still.
- Winter Tundra completion (Perfect-mask orb) sets the level's talisman byte (saved, not
  counted); Return Home tests the Perfect mask (Cloud Temples is bit 2, not bit 0).
- `Game::reward_this_visit` (`80066ff4`): Return Home and the pause menu's Exit Level play
  the level's outro before the home world loads.
- Workbench `--level-exit-check SLOT` and `tools/run_level_exit_checks.py`; probe
  `spyro_level_exits` (tools/level_exits.cpp).
- Retail timing: Return Home waits for update 150 of the reward; Zoe arrives at update 140
  (the original `8003cafc`); the first Glimmer exit before Summer Forest is visited plays
  story cutscene 4 (scene WAD 77, movie WAD 142).
- Hidden runs (`tools/run_level_exit_checks.py`): 17 of 18 talisman / Winter Tundra levels
  PASS; Magma Cone fails (its host never opens the conversation). Table in
  `docs/LEVEL_COMPLETION.md`; evidence in `data/validation/level-exits-20261103/`.


## 2026-11-05 - Market Mesa private PS1 review candidates

- Added the standalone compact09 and expanded full14 private candidates described
  in SEASON_OF_ICE_PORT10.19; the default good test05 launcher remains unchanged.
- Bound source bell completion to original Return Home with the source contact
  metric and original PS1 feet/origin conversion. Full14 physical contact returns
  to the new Summer portal; compact09 retains Mesa. Exact hashes/log lines are in
  data/soi/rom-targets-20261105/source-quest-return-verified.json and
  full14-original-verified.json; no manual game test is claimed.
- Combined source actors/critters/fans/bells and approved music with stone polish;
  retained v2g geometry/collision/model VRAM and0 new discontinuities over2685
  floor joins. Kept editable Blender portal artwork and source sheets.
- Original compact09 boot/skill/exit/restart/content/render/music checks complete;
  full14 original content/render/portal checks complete. Full14 final audio capture
  and private review GUI/script patcher checks are in progress at this checkpoint.
- Native full14 scripts/music/travel remain incomplete on a copied lead shadowR2
  package. No native source edits, main build/install, user process operation,
  public patcher promotion, downloads or git push. This folder is not a Git repo.


### Market Mesa PS1 review checkpoint completed

- Full14 final original score/restart identity is complete on its exact BIN;
  log coordinator-expanded-extra14-score.log:4,5 at the absolute Mesa task root.
- Selected private patcher14b ZIP `1672ab725f85db6c869d64bc2824a32dc2cff0e659c85e23ec6f3685dfb1da28` reproduces full14 byte for byte
  through GUI and script; coordinator-expanded14b-review-package.log:6,9,11.
  All52 member hashes checked. Compatibility footer now explicitly names the
  incomplete native expansion. No public dist/main/launcher promotion.
- Selected clean standalone/full ROMs, exact hashes, proof lines and remaining
  native/save/story/manual acceptance limits are indexed in
  data/soi/rom-targets-20261105/PS1-REVIEW.md and SEASON_OF_ICE_PORT10.20.

