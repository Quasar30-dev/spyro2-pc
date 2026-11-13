# Sparx rendering, feeding, sound and gem collection

Sparx now spawns as a runtime object using class `78`'s decoded model, original instance scale, wing animation and health-dependent body tint. His ID is allocated after the authored object IDs. Runtime state belongs to the play copy; respawning reuses the companion's ID and preserves collected gems without changing the retail records or editor scene.

## Recovered behavior

Summer Forest's dynamic initializer `8007bcb8` uses the common object defaults at `800375c8` and `StartMoby` at `80041438`. With no parent, Sparx appears 1000 original units behind Spyro's origin and 1000 units above it. The angle computes the spawn position; it does not initialize the object's zeroed yaw. The first animation pair is frames 0 and 1.

The companion path in `800720b4..80073410` supplies:

- Ordinary following at `80072ec8`: a local offset chosen with the original RNG equation, a timer in VBlanks, and one quarter of the target-position error per script update. `8001bd48` divides negative components toward zero. Terrain sphere radius is 256 original units. Yaw approaches Spyro's heading by four byte-angle units per normal game frame.
- Gem approach at `8007282c`: normalize the gem delta using the executable's integer length / atan tables, move by 310 original units per normal game frame, and start the gem's flight when Sparx is within 512 units.
- Health tint at `800733a0`, using the five words at executable `80061adc`. Zero health hides Sparx and clears his target.

`8001271c` enforces at least two VBlanks per retail game frame. These object scripts run at the normal 30 Hz interval in the native 60 Hz simulation. Their logical positions remain integer-valued; separate transform snapshots interpolate movement across the two native ticks. Wing animation advances every VBlank.

## Gem flight and accounting

The loose-gem path `80071078..800711b4` selects targets in object order, with horizontal range `80061a90` (2062), vertical range `80061a98` (640), Spyro's 356-unit origin offset and a line-of-sight test from Sparx to 100 units above the gem. Falling extends the vertical window by 512. Native collection currently covers resting loose gems.

Contact starts gem state 3, resets its age, records its starting position and chooses its spin. `80070ec8..80070ff0` divides the current Spyro-origin delta by 30, multiplies by age, adds the original sine arc, then tracks Spyro's origin after age 30. It includes the original long-distance fallback. Awarding happens at the direct pickup gate or the age-65 fallback, not when Sparx first touches the gem. A delivered gem increments the total exactly once.

The integer math uses executable tables at `80061e5c` (atan) and `80066478` (square-root seed), followed by the recovered refinement and Q15 rescaling. The local deterministic RNG implements `80059d2c`; it is not yet synchronized to the entire original game's RNG stream.

## Trail and attached glow

`80072148..8007220c` emits four type-42 particles per normal 30 Hz script update, two at either side of Sparx's pre-move position. The side offset is 100 original units. `8007e100` gives each particle quarter-resolution position jitter, an initial age of 0..15 and zero velocity. `8007d8b0` advances its random walk per VBlank and expires it at age 24. Blue Sparx uses the recovered eight-colour palette; gold Sparx uses yellow. Green/absent Sparx does not emit. Existing particles finish their short lifetimes after damage.

The renderer at `80048128` draws a tiny opaque untextured line, not a textured star sprite. The PC renderer preserves its reference screen size at higher resolutions and tests terrain depth without writing particle depth. Particle positions interpolate at presentation rate; effect randomness is separate from following randomness. Unlike the original renderer's visibility feedback, offscreen native particles expire by age.

Gold Sparx's light comes from `80072224` / `8004a87c`, with size `0x40` and centre RGB `(144,144,72)`. `800733f8` / `80041730` attaches it to packed animation attachment vertex zero. The decoder now reads that vertex from the collision/attachment prefix for every Sparx frame; it does not guess the location from the render mesh. The additive shaded triangle fan uses the resident outlines/connectivity at `8006430c`, including the four-bit vertex indices and rotation. Damage below gold removes the glow. Respawn clears previous particles; pause freezes the trail.

## Feeding and sound

All 40 gameplay banks contain two dedicated Sparx samples. In Summer Forest they are samples 67 and 68; each level resolves its own object sound mapping. Sound 0 begins feeding/revival, retaining its retail volume and pitch variation. Clip 1's frame 5 triggers sound 1 through the shared animation metadata path. Clip 0 has no sound events: no continuous wing buzz has been invented. Audio follows the companion's position and enters both live playback and the 48 kHz stereo offline capture mixer.

The native feeding path targets butterfly class `10` and extra-life fly `D3` within the original 3000-horizontal/2800-vertical-unit range. Living Sparx approaches, blends into clip 1, closes the mouth-to-target distance and consumes at the next-frame-7 gate (`8007259c`). Health is awarded at consumption, with the original maximum; the extra-life fly also awards one life up to 99. Sparx then blends back to the wing loop. When Sparx is absent, the butterfly recreates him and plays sound 0 (`80071434`). Cancelled targets are released on respawn or obstruction. End-of-animation handling retains the intervening 60 Hz frame so the 30 Hz controller cannot miss it and replay the eating sound.

Sheep class `12C` accepts flame/charge hits, plays its own sound 0 and releases a butterfly half a world unit above it, following `80077578` / `8007bed8`. The recovered 7/9/10-drop interval selects extra-life flies when that bank is present; the counter resets after each extra-life drop. **Play beside animal** provides a convenient starting point. Butterflies roam around their release location, circle Spyro during pursuit, bounce off terrain and expire after 1800 VBlanks. Feeding retains the original randomized timer and next-frame-7 consumption gate. Other fodder controllers and the sheep wool burst remain open.

Gem attraction also plays class 1 sound slot 1 at the start of flight (`800728cc`); the existing slot-0 delivery sound remains separate. Both occur once, at their respective gameplay events.

## Limits and checks

Sparx also targets [released container gems](CONTAINERS_AND_RELEASED_GEMS.md) after their first bounce, using the original state-2 remaining-bounce gate. His seven recovered idle substates use the original selection weights/timers and interrupt on movement. Hold **T** (gamepad L1+R1+R2) to point toward the nearest remaining loose gem or gem-bearing container. Swimming follows Spyro's pitch and roll and emits the original animated class-AA underwater bubble; its bank exists in 11 gameplay scenes.

Gem deliveries spawn the original class-104 value digits with recovered 30 Hz motion interpolated at 60 Hz and a 96-update lifetime. Saving records inventory, not the transient number presentation, so loading cannot replay an award. Terminal number sparkles, pickup sparkles and spirit-flight effects remain open.

S2SAVE6 introduced persistent released butterflies, source/home/position/heading/age and the extra-life drop counter; current S2SAVE8 retains these fields. Loading and consuming a saved butterfly cannot duplicate healing or a life. Transient wander phase and the entire original shared RNG stream are not serialized. Following and line of sight still use native terrain queries; complete dynamic-object collision callbacks and visibility feedback remain open. Exact whole-game companion trajectory equivalence is not claimed.

Core checks cover the recovered spawn, integer following step, intermediate 60 Hz transform, health tint, strict target boundaries, blocked line of sight, deferred flight award, single collection, pause, respawn, authoring isolation and presentation cadence. The native Summer Forest movement replay exercises an actual Sparx gem delivery using the mounted CUE/BIN.

`spyro_companion_tests [retail-copy]` additionally checks all health states, particle bounds/expiry, 30/60/144 Hz presentation, deferred healing, revival, capped extra lives, terrain obstruction, released targets, feeding sound counts, return to the wing loop and sheep release. With a retail copy it validates all 40 Sparx attachment/animation/sound banks and runs a decoded sheep-to-butterfly-to-Sparx integration check.

Gameplay captures and test reports are in `data/validation/sparx-20260921/`. For example, run the installed executable with `--game data/disc --slot 15 --headless --frames 420 --interaction-record 26 --attack flame --capture-fullscreen --camera-distance 2.7 --camera-orbit 65 --video data/validation/sparx-20260921/sparx-feeding.mp4`. Bounded captures also accept `--capture-health 0..3` to set the initial health and `--capture-music-volume 0..1` to isolate effects. These are capture setup controls, not changes to normal gameplay health or saved preferences.

Evidence: `data/analysis/gems_014/sparx_disassembly.txt`, `disassembly.txt`, the `8007bcb8` export in the WAD-14 project and `data/analysis/main/companion_math_disassembly.txt`.

Additional evidence: `data/analysis/sparx-effects.json`, `data/analysis/sparx/butterflies.txt`, `sheep.txt`, and the `8007e100`, `8007cab8`, `80048128` decompilations in `data/analysis/overlay_014/decompiled/`. The implementation uses assets from the mounted retail copy.
