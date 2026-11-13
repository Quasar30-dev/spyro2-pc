# Supplied prototypes, demo and cut-animation investigation

Evidence date: 30 September 2026. All model, texture, audio and animation bytes come from the user's supplied copies. Source archives are preserved. No retail bytes are bundled in the executable or used to disguise a different build's identity.

## Identified copies

| Copy | Executable CRC32 | Load address | Gameplay scenes |
| --- | --- | --- | ---: |
| US retail | `cdc0917c` | `80010000` | 29 |
| Full July 13, 1999 prototype | `2c8235e7` | `80010000` | 28 |
| August 11, 1999 prototype | `f22e71e1` | `80010000` | 29 |
| Winter Jampack '99 Spyro demo | `3da0212b` | `80018000` | 2 |

The Jampack disc boots a compilation menu. Spyro is `SPYRO2/DEMO2.EXE`, with its own `SPYRO2/WAD.WAD`. The playable banks are physical WAD 29 (Sunny Beach) and 45 (Skelos Badlands), exposed as canonical scene IDs 27 and 35. They must not be confused with the unrelated retail banks with physical IDs 29/45. The multi-track CUE's separate CD audio files are retained; only its data track is used for the ISO filesystem.

Full prototype scene archives are four slots apart: July `6 + 4*i`, August `8 + 4*i`. Their executable overlays immediately precede the scene. Logical campaign IDs remain stable in the PC runtime, while `GameProfile` maps every actual archive read. Missing scene banks are absent capabilities, not replacements loaded from a retail copy. Saves retain the real executable CRC and reject a mismatched mounted build.

The build inventories, file extents, executable hashes and archive indexes are under `data/builds/<build>/`. Unknown executable CRCs remain rejected.

The root launchers `Play July Prototype.cmd`, `Play August Prototype.cmd` and `Play Winter Jampack Demo.cmd` mount the supplied CUE files in the installed workbench. The normal source picker also accepts these discs and their extracted folders. Use the CUE for the original audio unless the extraction retains XA sector headers. The demo level selector offers its two actual scenes.

## Early glide: investigation result

**No separate Spyro 1-style glide with Spyro 2 wings has been identified in either supplied July or Jampack copy.** This is a bounded search result, not proof that the animation never existed in any build.

Their entire 167,936-byte global Spyro bank is byte-for-byte identical:

```
SHA-256 fadd61d2863e5f468df23064687785b99e3031d8065beb25456f7216321280d1
```

Both have the same 21 referenced global clips, including rolls 10/11, glide 16 and glide brake 24. Every global slot was decoded and visually inspected. Glide 16 has five frames and 230 vertices. Corresponding face corners establish a complete bijection between the early and retail vertex orders. Across all five frames, the maximum displacement from retail is **7.549835 native units**, with RMS vertex displacement **3.782109 units**. The two early glides are identical to each other; they have the familiar Spyro 2 silhouette. The small retail differences are consistent with vertex compression/quantization and do not establish a different animation.

The scan also examined every level's extra Spyro bank: none supplies an alternate glide 16 or brake 24. A whole-WAD search allowed different vertex counts, partitions and common face banks, checked frame extents and decoded surviving unreferenced candidates. July had one superficial header match inside gameplay data; its supposed frames 3/4 fail the vertex codebook and it is rejected. Jampack had no surviving unreferenced candidate. This does not cover deleted disc sectors, other codec families, missing builds or an animation whose descriptor was removed completely.

Reproducible evidence:

- `tools/render_prerelease_animations.py`: actual decoded meshes and textures, not generated art.
- `tools/scan_prerelease_animation_storage.py`: whole-WAD candidate search and rejection log.
- `data/analysis/prerelease-20260930/animation-storage-scan.json`: 313 referenced July descriptors and 45 Jampack descriptors, including scene banks.
- `data/analysis/prerelease-20260930/animation-comparison/glide-all-frames.png`, `glide-rear.png`, `glide-side.png`, `glide-above.png` and `glide-differences.json`.
- `july-13-all-globals.png` and `winter-jampack-all-globals.png` in that comparison directory.

## Recovered roll upgrade

July global clips **10 (left)** and **11 (right)** each contain 20 authored frames with two NTSC VBlanks per frame. They are also present in Jampack's identical global bank; August and retail omit them.

The retail PC model imports these vertex motions using the verified 230-vertex face correspondence. It retains the mounted model's face order, UVs, colours, textures and atlas tiles. This avoids interpreting July's different vertex order as retail geometry. The July animation-transition rows/columns are imported with the clips.

The optional **July dodge roll** upgrade costs 30 upgrade points. Controls are **Z / V**, or **L1 / R1**. Tap for one grounded side roll; hold to keep rolling, then release to finish the current revolution. The existing gem-hint chord takes priority over the shoulder buttons. Native July/Jampack profiles retain their roll without requiring the PC upgrade. The acquired upgrade is saved in `S2SAVE14`; older saves remain readable and do not silently gain the upgrade. An asset source can be selected through **Upgrades → Import July roll animations**. The already supplied, extracted July folder is detected locally when present; another user's retail-only installation still needs that donor copy.

Ghidra evidence is in `july-state/` and `july-roll/`: `8003091c` selects states 10/11 from shoulder-button bits; `80029c30` supplies sideways motion, `0x1000` speed target, `0x140` acceleration and decaying inherited velocity; `8002f19c` initializes the state. Steering is bounded by `800341ec`/`80034438`. The PC implementation uses the port's normal collision path and fixed 60 Hz clock. These recovered parameters do not establish bit-exact equivalence of all surrounding collision/camera code.

The release check uses the **next** animation frame, matching `8003091c`'s byte at `800698c7`, and the executable's transition-end table. The old current-frame check waited until frame 0 had been queued again, making a single tap play two visible revolutions. Releasing on the loop's final pose now blends directly out of that pose. Holding retains the lateral movement state and animation phase instead of restarting entry transitions.

The roll's original July native sound event `0x14` is retained as PC semantic
event `0xFE`, separate from retail headbash. Clips 10/11 emit it on source frame
1 once per revolution. The donor PCM, SPU pitch and descriptor stay intact; the
PC mixer applies a 4x cue gain so its low-pitched sound remains audible beneath
music. Live Windows playback and offline video use the same gain. The opt-in
`spyro_audio_playback_probe game-copy --roll-effects July-copy` checks voice
delivery, level reload, audible capture energy and clean stopping. Use the
original CUE/BIN for music when an extracted analysis folder omits STR files.

## Confirmed format differences

- Early level headers put the model directory at **+0x20**, followed by class IDs at **+0x120**. Retail inserts 32 reserved bytes, moving these to +0x40/+0x140. Parsing the early model pointers as additional WAD extents was invalid.
- Jampack omits the gameplay speech directory and scene portal-picture section. Object, particle and sound directories must advance over the sections that actually exist.
- Early short/long flame trajectories have **24/32 samples** and **32/40-tick envelopes**, with five emission offsets. Retail uses 22/28 samples, 30/36-tick envelopes and eight offsets.
- July/Jampack SPU samples follow a **0x2800-byte** pause upload after VRAM; August/retail use **0x6000 bytes**. The wrong prefix either clips the last samples or decodes UI pixels as sound.
- July/Jampack fonts use byte glyph codes and an older UV layout; August has 16-bit descriptors with that earlier UV encoding. Palette/width/colour tables are profile-specific. The early pause upload also occupies a different VRAM rectangle and has 19 sprite descriptors rather than retail's 26. Jampack's last level-name pointer is null because that scene is absent.
- Early home transporters store a scene ID rather than a retail scene-array index. July's Elora portal index is at +0x48, and absent Winter Tundra gates retain `ffff` sentinels.
- August has stale one-past-the-end terminal indices for shortened clips 31 and 59. The decoder explicitly holds their last authored frames instead of reading into the next compressed record.
- July music uses absolute catalog addresses into **MUSIC1.STR, SPYRO_1.STR and SPYRO_2.STR**. August/retail use relative offsets into SPEECH.STR.
- Spyro's global SFX IDs differ between builds. August charge/jump are **0x22/0x23**, July/Jampack **0/1**, and retail **0x21/0x22**. Prerelease IDs **0x15..0x21** correspond to retail **0x14..0x20**, including headbash, underwater hurt, power-up warning and whirlwind. The decoder translates these to the runtime's retail semantic IDs while retaining each copy's actual samples, pitches, volumes and object-local indices. See [audio evidence](AUDIO_AND_NPC_INTERACTIONS.md).
- July's Hurricos music descriptor requests 28,779 records from the 28,664-record SPYRO_2 file. Its selected channel has a valid XA EOF marker at record 28,657; the next file is speech with a different coding rate. The reader respects the selected stream's EOF, never reads outside its file and still rejects a truncated stream without an EOF marker. Other channels' markers do not stop playback.
- Jampack bootstrap `8001a584` patches music addresses and lengths after finding **SPYRO2/DEMO2.STR**. That file contains packed **2336-byte XA records inside ordinary ISO file data**; reading only the physical sectors' Form 1 subheaders discards the embedded audio. The reader handles both representations.

## Validation status and remaining work

The July roll's original sound is now retained as a separate semantic event
(`0xfe`), rather than being lost when native global `0x14` is translated to the
retail headbash mapping. July resident `8002e26c` plays that sample on frame 1
of clips 10/11. The runtime emits it once per revolution; a tap emits it once,
and held rolls repeat it with the animation. Retail imports the sample and its
native pitch/volume from the mounted July copy alongside the roll animations.
The donor is mapped with July's physical WAD layout and re-imported after level
changes. The cross-bank tests preserve headbash audio and prevent duplicate
imports. July Glimmer resolves the original cue to sample 6, pitch 1024.

The expanded decoder matrix passes **590/590 checks** across all **59 supplied prototype/demo scenes**: geometry, every decoded Spyro frame, camera, Moby models, HUD, interface sprites, power-effect banks, SPU banks, music-directory entries and travel. The installed workbench also completed a **360-frame traversal replay in every scene** from the original CUE sources, following an initial 90-frame startup pass. The replay sends run, flame, charge, jump, glide and glide-brake inputs. The spawn environment can alter which moves actually occur; these short runs do not demonstrate completion of every quest or enemy behaviour. Per-scene reports are in `runtime/`, with their observed motion masks and audio counters. They are smoke tests, not sustained-performance benchmarks.

The real-asset roll regression checks the retained retail face/UV topology, all imported frames, left/right movement, no forward lurch on exit, identical fixed-clock results at 30/60/144 Hz, upgrade gating, native July access, purchase, save/load and respawn. The expanded checks count animation wraps after a tap and sweep held-button releases over every loop phase in both directions, including the final pose. Evidence is recorded in `data/analysis/prerelease-20260930/`.

The roll/SFX correction is installed as SHA256 `19f99206c64f54aec95de33cb908f6e424901f4abd5c43e3b5f006a77ccd5130`, before validation. Ten targeted suites pass, including 80 held-roll release cases and 2,939 audio assertions across **88** retail/prototype/demo level banks. July's Winter Tundra (scene 57) has four `255` footstep-category sentinels and an entirely `255` surface table in the original files; that native omission remains silent. The first audit flagged this omission; the regression now checks its exact sentinels while requiring audible footsteps in every other bank (`ctest-audio.log`).

August, July and Jampack each pass **39** Windows playback/capture checks for jump, flame, headbash, footsteps, charge, splash, underwater hurt and swim. Captured effects have nonzero PCM energy with music/voice volumes at zero, and both playback and capture loops stop when movement ends. The retail playback/pause/speech/level-change probe passes 14 checks. The installed workbench completes 360-frame replays from all four profiles. Logs and reports are under `roll-sfx-fix/`; these checks do not imply that every possible gameplay sound caller has been implemented.

All **32 CTest suites pass** after building and installing the main executable. Full-length music decoding also passes: July has 11 referenced unique tracks, August 17, and Jampack 2. These counts follow the supplied executable's track choices, including prototype placeholders/reused tracks; they are not a promise that a prototype contains the later retail soundtrack. `*-full-audio.jsonl` records frame counts, rate, channel count and PCM checksums; `ctest.log` records the regression results.

The profile-aware animation probe additionally exercises body/head transitions through run, charge, glide and brake using the original transition tables: 2,880 ticks for July, 4,500 for August, 6,480 for Jampack and 4,500 for retail, with fractional pose sampling and frame-reference/partition checks. Summer Forest speech samples also decode in July (16 referenced lines) and August (55); Jampack's two scenes omit the speech-directory section. See `*-animation-transitions.jsonl`, `*-speech.jsonl` and `validation-summary.json` for the installed executable hash.

The final installed executable also passes 15 workbench checks: saving, loading that save, opening the menu, opening pause and inspecting Sparx in each of the three builds. Loading a Jampack save into retail is rejected before changing gameplay. Retail's menu integration check and main-model charge-wing traversal replay pass too. These final checks include the corrected build-specific Sparx preview colour table.

This is not yet a claim of complete prototype gameplay parity. The old pause composition, guidebook/title/cinematic formats, August's unfinished Ripto actor layout and prototype-specific quest/AI differences still require separate mapping. The prototype front end provides PC new-game/load/source controls while the original title formats are unmapped; guidebook artwork is not substituted from retail. Missing scenes remain absent. The power-up extension below deliberately imports retail assets at the user's request. The main-model supercharge retarget remains tied to the verified retail topology; prototypes use the original purpose-built supercharge mesh.

## Retail powers in the prototypes and demo

Supercharge, Superflame and the green fire bomb can now use a separately supplied US retail copy in all three early builds. The workbench detects this project's extracted `data/disc` copy automatically. **Cheats → Choose retail power-up source…** accepts CUE/BIN/ISO/IMG or the extracted retail `SCUS_944.25` file. The chosen path is retained in PC preferences (version 3, with versions 1/2 still readable). Headless runs accept `--retail-powerups PATH`. The original source files, level selection, music and save CRC remain those of the mounted demo/prototype; no game assets are embedded in the port.

The import brings the complete eight-class retail projectile/impact family, its palette tiles, particle sprites, powered-fire eligibility table and decoded sound samples. It replaces early variants together, avoiding mixtures of unfinished prototype effect models and retail effect controllers. Spyro's powered head action 6 is also translated: July/Jampack's table names clip 54 where retail uses clip 7. Retaining the early action mapping prevented the retail firing sequence from behaving correctly.

The separate Supercharge mesh (clip 61), its own texture atlas, charge-jump clip 25 and smoke are imported from retail. Missing flight clips 33–35 are added for Jampack. Main-mesh motion is remapped through the verified face-corner vertex bijection; existing normal/flight skin and native animations remain attached to the mounted model. Retail animation transition rows accompany the imported clips. **Shift** charges; **Space/X** jumps while charging; **F** fires the selected power. Green bombs take precedence over Superflame and keep Spyro purple.

Superfly trail anchors now follow the actual model's vertex order: July/Jampack tips are **58/57**, while August/retail tips are **63/62**. The previous fixed retail indices selected the wrong early vertices. Each wing has an independent trail attached to its animated tip through pitch, roll and steering.

Return Home actors (`1AA`) are hidden unless bound to an authored Return Home portal. Degenerate Return Home apertures in a home world are discarded. This removes July Summer Forest's unused exit actor and the same stray actors elsewhere while retaining object IDs, saved progress and legitimate standard-level exits.

`spyro_prerelease_power_tests` exercises movement, charge jumps, firing windup, impacts, effects, sounds, both wing attachments, and portal bindings across all 59 early-build gameplay banks. `tools/validate-prerelease-powers.ps1 -AllLevels` validates the installed renderer from the original CUE sources. Reports are written under `data/analysis/prerelease-20260930/power-import-fix/`.

Validated after installing executable SHA256 `d22951a419541081ac57f9b91b7be6354409752b0a5f16637c7a478053a59d31`: all **59** real-asset power cases pass (357,915 assertions), and all **59** installed-workbench runs complete with retail imports active and no visible unbound exit actors. The source audit hides one orphan (July Summer Forest) and retains 32 valid early-build exits. All three builds visibly render two separate wing-tip trails, and Jampack renders the imported 255-vertex charge mesh with smoke. Separate Superflame replays in each build fire and detonate four shots. Seven targeted regression suites pass, as do retail's complete 42-portal/26-whirlwind/12-transporter audit and 146 retail power-effect checks. These short fixed-60-Hz runs verify this feature; they do not establish completion of every prototype quest or a sustained frame-rate benchmark.
