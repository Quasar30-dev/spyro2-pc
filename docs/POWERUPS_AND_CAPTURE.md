# Powered attacks, flight and capture

The workbench reads these models, animation banks, textures and sounds from the mounted retail copy. The new effects are native renderers and controllers recovered from the USA executable and level overlays; they do not run an emulated game underneath the viewport.

July, August and Winter Jampack can import the complete retail power family from the user's separate retail copy. The local `data/disc` extraction is detected automatically; **Cheats → Choose retail power-up source…** selects another copy. This supplies Supercharge, Superflame, green bombs and any missing Superfly clips while retaining the early build's levels and normal Spyro skin. See [prototype power imports](PRERELEASE_BUILDS.md#retail-powers-in-the-prototypes-and-demo).

## Playing

Open **Cheats** and enable **Superflame**, **Superfly** or **Green fire bomb**. **F** fires. The green bomb takes priority when both attack cheats are enabled and always leaves Spyro his usual purple colour. Superflame uses the retail pulsing body tint.

With Superfly enabled, the first **Space/X** press jumps normally; a second press in the air starts flight. Further presses flap. **A/D** steers, **W** dives and **S** climbs. Flight maintains forward thrust without holding W. **Shift** leaves powered flight. The retail level, climb and dive animations and wing-tip trails are used.

`--superfly --replay` exercises flight: it jumps, takes off with a second press in the air and flaps every second. Before 14 October it pressed only once and never left the ground.

Native gates use their authored duration, power type, spirit requirement and crossing bounds.

**Spirits and the gate counter.**
- A locked gate shows its requirement as class-104 gold digits:
  - one digit under ten, otherwise tens and units;
  - 0x520 above the gate, facing the camera with a 64-frame sway (overlay 16, gate case `80075600`).
  - The digits go once the gate opens.
- Each defeated enemy launches a class-68 spirit (`80037608`), which picks its gate:
  - **Direct:** the first gate within 0x2800 in clear sight.
  - **By route:** otherwise the closest waypoint of the authored spirit routes (gate parameter +0, up to 20 routes, linked). Their final waypoint is moved to 0x520 above the gate.
- The spirit's flight (overlay 16, class-68 case):
  - it plays its class sound 0;
  - it spirals up for 0x34 ticks;
  - it leaves when its tangent faces the target;
  - it follows the route with `8003ad84`'s eased segments, or flies straight at 300 units per update;
  - it trails six positions (`800502fc`);
  - it ends in twelve gold type-0C sparkles.
- The HUD counts a spirit at once (`80066fb0`), but a gate opens only on the spirits that have **arrived** (`80066efc`).
- Spirits already counted when a level loads (or Spyro respawns) do not fly.

**Trails (`800502fc` / `8004b72c`).**
- While flying (mode 6), `80032678` and `800326a8` give each wing tip a trail: six positions, width byte -3, colour 0x303030.
- The width byte spreads |w|·2048/SZ pixels: screen-horizontal when negative, vertical when positive. That is ±36 retail units for the wings.
- The trail tapers linearly to nothing and depth-cues to black.
- The 8×32 trail texture's 32-texel glow profile runs across the trail, with its 8 texels along each segment.
- The port previously had the texture axes swapped (a solid band), drew at twice the colour and did not taper. It now draws exactly this.
- Spirit trails use width 3 and colour 0x808080. The gate bolts use the same texture orientation.

**Open gates** (overlay 16, gate state 1):
- **Icon.** The gate holds its icon class (record +0x50) 0x580 above it.
  - Only classes under 0D take material 0x2a606060.
  - 0B and 0E turn about Z and 0C about X; the other icons keep the gate's heading and play their own clip.
- **Ambient sound.** The gate's class sound 0 is a looping sample. It is held while the gate is open: `80050b74` restarts it when it ends. The port keeps such sounds through `Powerups::held_sounds()`, which the audio output and the video capture mixer both honour.
- **Sparkles.** While drawn and within 0xA000, the gate emits one particle 04 every fourth update (every second update within 0x5000):
  - it starts on the line between the pillars;
  - it grows 4 per update for 4–7 updates, turns and rises;
  - once above gate height + 0x640 it shrinks away.
- **Crossing.** Crossing the gate grants the power, plays the **icon** class's sound 0, and starts eight updates of lightning while Spyro stays within 0x600:
  - two four-segment bolts run from the pillar tips (gate-local 0, ±0x400, 0x6cc) to Spyro (`8004b120` / `8004b194`, colour 0x608080, trail texture, bends re-rolled every other frame);
  - four type-47 crackle streaks appear on Spyro each update.
- **Filming.** `--open-gates` sets every gate's spirit requirement to zero, as a filming setup. Evidence is in `data/validation/gates-20261014`. There are four ordinary Superflame gates and five Superfly gates in the mapped archives. Metropolis grants the combined fire/flight power; speedways enable flight automatically. Dragon Shores' permanent Superflame gate requires 10,000 gems and 64 orbs. Unimplemented mission scripts can still prevent normal progression to a gate's requirements; enabling a cheat bypasses the requirement for that power.

The green bomb's PC damage mask adds the heavy-door bit to its original blast capabilities. It can destroy Robotica Farms' class `282` doors (records 52 and 59); Superflame cannot. Door removal includes collision and persists through PC saves. The original door fragments and mission-completion script have not been ported. Other unported enemies with authored spirit rewards accept powered attacks through the shared combat path; this does not implement their individual AI.

PC saves retain the cheat settings and permanent Superflame unlock. Temporary gate timers and active projectiles are not restored on loading.

## Supercharge wing options

The **Cheats** appearance selector offers **Original supercharge mesh**, **Spyro 2 main model / random wings**, and **Spyro 2 main model / regular charge flaps**. The third option uses the mounted regular-charge animation (clip 12) for the wing strokes. Both main-model options keep Spyro 2's full wing length, the corrected neck, clip 61's body motion and leg blur, and retail smoke. This is an appearance choice and also applies when a level's supercharge gate supplies the power.

The existing random-wing retarget remains unchanged at saved/network value 1; the new flap variant uses value 2. Current defaults remain unchanged. The planned prototype/demo default selection is a future pass. Saves restore all three choices; multiplayer snapshots and the bundled relay accept all three (players and relay need the updated build).

The flap variant transfers all 16 regular-charge wing vertices in torso space, including the roots, fingers and folding membrane. It evaluates the mounted clip after body interpolation, including fractional presentation frames, instead of interpolating twelve rigid wing approximations. Its roots no longer inherit clip 61's random wing movement. Clip 12's 44-VBlank loop is fitted to clip 61's 48-VBlank loop. Body, neck, blur, frame durations, transition rules and the 60 Hz simulation are unchanged; no independently drifting animation timer is introduced. Fractional last-frame poses wrap correctly in the studio/network sampling path.

For automated playback, use `--supercharge main-flap`. Existing `--supercharge main` retains random wings; `--supercharge retail` retains the original mesh. `spyro_supercharge_tests` checks every wing vertex against the mounted regular-charge pose, including subframe membrane/root positions, unchanged body/neck/blur/materials, loop continuity and entry/jump/exit transitions in three scene banks. Save and online checks cover old and new values.

The desktop loader also fills missing movement clips from the mounted Summer Forest (18, 40–44) and Scorch (49) banks before packing Spyro's textures. Existing scene-local clips take precedence. This prevents the fatal missing-animation path when PC movement reaches an action absent from a selected retail bank; it does not change which retail water surfaces permit swimming. `spyro_movement_bank_tests data/disc` exercises all movement clips and supercharge interruptions across 41 banks, plus actual swimming/whirlwind controller runs with imported clips.

## Retail effect reconstruction

The earlier opaque-looking explosions had a material bug: the Moby renderer discarded the texture-page transparency flag and the palette's per-texel STP bit. The decoder now preserves both. Solid texels write depth; translucent texels use the original half, additive, subtractive or quarter-additive blend equation without writing depth. A black texel in an additive glow contributes zero light. D3D11 pixel tests cover all four equations and mixed solid/translucent texture behaviour.

| Effect | Recovered components |
| --- | --- |
| Green bomb | Animated `2C2` projectile; `3A` trail particles; 60-VBlank expanding damage region; two `146` burst meshes per original script update; radial green particles; two-colour `46` sparks; original ground offset and camera shake |
| Superflame | Animated `1E3` projectile; clip-1 trail meshes; five-sided orange trail; `146` impact dome; five `21D` radial spokes; repeated `1E2` clouds; `0D` fire particles and `46` sparks; 20-VBlank impact damage region |
| Underwater Superflame | Spiralling `1F8` projectile; clip-1 trail meshes; expanding `1E1` and blue `211` impact meshes; rising/wobbling `27` bubbles; 40-VBlank impact damage region |
| Superfly | Spyro clips 33/34/35, recovered speed/pitch/roll parameters, original wing-tip vertices and trail texture |

Effect objects are separate from saved level objects. They have bounded lifetimes, interpolate at 60 Hz, stop during pause and are cleared on scene reset. Any-level cheats import missing model/material banks from Ripto's arena or Summer Forest. The referenced palette tiles accompany imported models; launch and blast sounds have the same fallback treatment.

Primary local evidence, for SCUS_944.25 CRC32 `cdc0917c`:

- Ripto overlay 70: `8007b7d8..8007bca8` green bomb; `80073708..8007385c` burst mesh; `80073d78..80074b44` Superflame; `800881b0` effect creation; `800896d0` particle update; `8008ab40` particle creation.
- Summer Forest overlay 14: `8007a088..8007a670` underwater shot/trail/blast; `80079208` and `8007a674` expanding water effects; `8007bcb8` model creation; `8007cab8` / `8007e100` bubble update/creation.
- Resident `80031eac` selects Spyro's power palette; `80041dbc` applies its depth-cue tint. `80044504` and its textured-face paths recover Moby transparency. `8001d8a4` supplies the camera shake.
- `data/analysis/powerups.json` inventories gates, clips, overlay callbacks and textures. Focused assembly/decompilation exports live under `data/analysis/overlay_070`, `overlay_014` and `main`.

These recover the retail effect components, colours, timing and blend rules. Native floating-point transforms/interpolation do not reproduce PSX integer rounding or every ordering-table artefact. Environment mapping and shared object/terrain transparency ordering are now implemented; see [object materials](OBJECT_RENDERING.md). General Moby fog and the full Ripto fight remain separate work; this is not a claim of pixel-identical emulation.

## Photos and video

**Capture → Take photo / F10** opens a PC save picker for a BMP. **Capture → Record video / F11** starts an MP4; press F11 again to stop and save. Capture includes the game viewport and HUD. It excludes the workbench panels. FFmpeg must be available on PATH for video recording; photos do not need it.

Video is H.264 at 60 fps with 48 kHz stereo AAC sound. Live recording retains the initial viewport size and repeats the most recent image if presentation falls behind. The headless path advances a deterministic 60 Hz run and mixes the mounted game's decoded music/effects directly into the clip, without recording the desktop or playing sound through the speakers. The frame count is the number of encoded frames; five setup renders precede recording.

Music requires full XA sectors: CUE/BIN or an extracted copy retaining full-sector `SPEECH.STR.raw` / `SPEECH.STR`. A data-only ISO still supports geometry and SPU sound effects but cannot supply the missing XA audio bytes.

Examples, from the project folder:

```powershell
# One green bomb into a Robotica Farms heavy door; 4 seconds, 60 fps, sound.
./build/Release/spyro_workbench.exe --game data/disc --slot 65 --headless --frames 240 --green-firebomb --interaction-record 52 --attack flame --video data/validation/green-bomb-door-60fps.mp4 --capture data/validation/green-bomb-door-finished.bmp

# The same scene with the complete Superflame trail and impact.
./build/Release/spyro_workbench.exe --game data/disc --slot 65 --headless --frames 240 --superflame --interaction-record 52 --attack flame --video data/validation/superflame-impact-60fps.mp4

# Swimming replay; Superflame fires twice while underwater.
./build/Release/spyro_workbench.exe --game data/disc --slot 15 --headless --frames 480 --swim-replay --superflame --video data/validation/underwater-superflame-60fps.mp4

# Asset, attack, gate and effect-lifecycle checks against the mounted copy.
./build/Release/spyro_powerup_tests.exe data/disc
```

`--camera-orbit <degrees>` changes the capture angle around Spyro. `--paced` runs the bounded replay in real time for pacing measurements. Capture JSON reports encoded frames, shot/blast counts, effect counts, target destruction and CPU timing. It distinguishes an actual impact from a screenshot showing only a projectile.
