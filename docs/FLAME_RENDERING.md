# Retail flame rendering

Profile: USA `SCUS_944.25`, CRC32 `cdc0917c`. The native renderer reads its flame geometry, animation tables, colors and texture from the mounted retail executable and scene archive. No replacement fire texture or particle model is supplied with the port.

## Executable evidence

`8002f08c` updates the eight flame streams, their emission order, attachment transform, age and collision cutoff. `80048f04` submits solid tips and textured trails. The focused instruction export is `data/analysis/main/flame_render_disassembly.txt`; the existing resident decompilations provide surrounding control flow. Instruction operands and delay slots were checked where the decompiler merged reused registers.

| Address | Contents / use |
| --- | --- |
| `80062968` | 30 packed envelope words for the four short streams |
| `800629e0` | 36 envelope words for the four long streams |
| `80062a70` | Eight attachment offsets |
| `80062ad0` | Four trajectories, each containing 22 eight-byte records |
| `80062d90` | Four trajectories, each containing 28 eight-byte records |
| `80063110` | Four vertex colors for the solid tips |
| `80063130` | Two eight-entry emission orders |
| `80061bd8` / `80061c58` | Sine / cosine samples used for each cross section |
| `8001c734` | Reads the flame UV, page and palette references from the language gameplay header |

Trajectory records contain signed local forward/side/up coordinates and a packed final halfword. Its low byte supplies the cross-section twist. Geometry uses the same `1/1024` unit conversion and local-axis transform as Spyro. The envelope bytes specify the head sample, trail length in samples, and fade offset. Rendering visits streams seven through zero.

A head has four vertices: the leading trajectory sample and three cross-section vertices two samples behind it. The recovered trig multiplier gives an 80-retail-unit maximum extent. The four triangles retain the original corner colors and winding. They use an opaque Gouraud material.

The textured trail begins two samples behind the head. Cross sections normally advance backward by two samples, with a final one-sample step when required. The trig multiplier is 47 (a 94-unit maximum extent) in the body and 8 (16 units) at the tail end or at a clipped leading edge. Each section produces a pair of vertices. Brightness starts at `128 - fade * 5`; each backward trajectory step advances the texture vertically by five texels and lowers brightness by five. The renderer interpolates these values for presentation between script updates.

The long envelope at age 24 reaches sample `-1`. In the executable's contiguous data bank this is the previous eight-byte record. The parser preserves that record explicitly as `FlameCurve::prefix`; the native code never reads before a vector. Other out-of-range trajectory requests produce a format error.

## Texture and material

The standard gameplay header's words at `+0x14` and `+0x18` provide the packed CLUT / UV and page / UV fields. All 40 ordinary scenes use the same 32-by-128 crop, while palette addresses vary. The mapped page selects four-bit pixels and additive blending. Every nontransparent palette texel in these scenes carries the semitransparency flag. The decoder validates these assumptions, the VRAM window and crop, then uploads RGBA pixels from that scene's own VRAM image. Slot 95 has a separately recognized shorter header and no flame texture in this implementation.

GPU command `0x30` identifies the opaque Gouraud tips; `0x3e` identifies the semitransparent Gouraud textured quads. Native trails multiply texture RGB by vertex RGB divided by 128 and add the result to the background. Zero-color texels are discarded. These packet and modulation rules follow the [PlayStation GPU register documentation](https://psx-spx.consoledev.net/graphicsprocessingunitgpu/); the particular values and geometry above come from the retail executable.

## Native integration and limits

`ObjectInteractions` keeps previous/current visual ages, attachment bases, headings and per-stream cutoff samples. Its existing 30 Hz collision script remains responsible for attacks. Terrain and container hits shorten the visible path; the remaining tail continues fading. Births on successive VBlanks and the final lifetime interval interpolate across 60 Hz rendering. Respawn clears both visual states. The flame texture has a separate D3D11 resource, so loading another scene also replaces its palette.

The geometry follows the recovered primary source-frame attachment plus flame offsets and the current yaw. [The secondary action controller](SECONDARY_ANIMATION.md) now preserves the retail head gesture and its original entry/exit timing, including firing from idle and continuing into charge. Full body pitch/bank and the head spring matrices still need reconstruction, so mouth alignment under those rotations is not final. Native projection, depth buffering and interpolation replace the original GTE rounding, ordering-table buckets and near-camera rejection; pixel-for-pixel equivalence is not claimed. Sounds, impact particles, enemy damage and water interactions remain unfinished. Flame trails do not implement a new lighting system or screen glow.

## Verification

Synthetic interaction tests check the original tip extents/colors, ribbon taper, UV/modulation values, a half-age pose, collision trimming, the explicit prefix record, malformed sample rejection, emission spacing, expiry and reset. Existing attack tests still exercise container results and terrain occlusion at 30, 60 and 144 presentation updates per second.

The retail inspector checks all 40 texture/palette mappings and sweeps 16,896 geometry poses: all eight streams, every integer and half-integer active age, and cutoff samples zero through 32. Native capture cases cover emission, trails, side view, fade-out and expiry. The flame-resistant vase case also requires nonzero heat, proving that the attack reached it.

`tools/validate-retail.ps1` reproduces these checks alongside the movement, object and container regressions. `--camera-orbit 90` is an optional smoke-capture view offset for inspecting the flame from the side; it does not alter the simulated follow camera. Flame pose captures are unpaced visual checks; the normal movement and container runs separately record paced timing.
