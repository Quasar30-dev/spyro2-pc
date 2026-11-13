# Camera and world-scale reconstruction

Verified against the locally supplied USA `SCUS_944.25`, CRC32 `cdc0917c`. Addresses below are PSX virtual addresses. For this executable, file offset = `0x800 + VA - 0x80010000`. Ghidra exports are under `data/analysis/main/`; the focused instruction dump is `camera_disassembly.txt`.

## One world scale

`include/spyro/coordinates.hpp` centralizes these conversions:

| Data | Native conversion |
| --- | --- |
| Original world `(X,Y,Z)` | `(X,Z,Y) / 1024` |
| Original model-local `(X,Y,Z)` | `(-Y,Z,X) / 1024` |
| Original yaw, one turn = 4096 | Native yaw = pi/2 - yaw * 2pi/4096 |
| Player origin | Feet + `(0,356/1024,0)` |

1024 is a shared unit conversion, not an enlargement or shrink factor for Spyro. Collision coordinates contain a separate encoded factor of 16, which is applied before this conversion. Terrain region origins and packed vertices similarly expand their encoded scale before conversion. Spyro's decoded vertices are already in original world units.

The minus sign in model-local Y makes its +Z-forward basis agree with the native world basis. With original yaw zero, rotating the model into the world produces exactly `(X,Z,Y)/1024`. The previous local basis reflected the model across this axis.

### Evidence

- `80041dbc`: `RenderSpyro` subtracts the camera position from Spyro's original position without a model-specific scale. Decoded local vertices pass through the model/camera matrix in the same units.
- `8001b6c8`: model rotation uses Q12 identity `0x1000` and sine/cosine values. There is no normal-render model enlargement.
- `8001c214`: collision triangle coordinate unpacking supplies the original factor of 16.
- `8001c734`: the fourth level archive child begins with the original spawn XYZ; heading byte `+0x0e` becomes a 12-bit angle.
- `8001722c`: floor height + `0x164` becomes Spyro's vertical origin. `8001a2fc` temporarily subtracts that offset to save the floor-relative position.
- `800351d8`: initializes collision radius at `8006a260` to `0x164`. `8002c0f4` passes that radius and Spyro's origin into the terrain sphere collision routine. The native runtime now uses this sphere in place of the temporary tall capsule. The complete original collision response/flags are still pending.

The player is rendered with the unscaled decoded vertices, current heading and origin offset. Body banking / slope orientation remain to be reconstructed; their absence must not be compensated with a size adjustment.

## Camera profiles

Five signed 32-bit values per profile: orbit azimuth relative to Spyro, orbit elevation, radius, view-pitch bias and view-yaw bias. Angles use 4096 units per turn. `decode_camera_settings` reads these directly from the mounted executable.

| Address | Use currently implemented | Azimuth | Elevation | Radius | Pitch bias | Yaw bias |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `80061560` | Normal follow | 2048 | 160 | 2560 | 0 | 0 |
| `80061574` | Running towards camera | 0 | 160 | 2560 | 96 | 0 |
| `80061588` | Charge | 2048 | 224 | 1632 | -64 | 0 |

The normal camera radius is 2.5 native units, with 14.0625 degrees orbit elevation. The charge radius is 1.59375 native units, with 19.6875 degrees elevation and -5.625 degrees view-pitch bias. The separate view-pitch bias matters: always looking at a fixed point above Spyro loses this behavior.

`8001ed20` converts spherical parameters to original XYZ with two Q12 products. The implementation preserves their integer rounding and uses the executable's sine/cosine tables (`80061bd8`, `80061c58`). The normal profile at original heading zero has original offset `(-2483,0,621)`. With feet at zero, its native camera height is `(621+356)/1024`.

`8001e270` derives view angles from the resulting orbit and the separate biases. The native view reconstructs its direction after collision displacement, then interpolates both camera position and direction for display.

### Follow integration at 60 Hz

`8001d8a4` calls `80021558` once per elapsed VBlank. `8001db58` forms signed wrapped angle errors and the unwrapped radius error. `8001dc6c` integrates five components:

```text
velocity += (spring * error - damping * velocity) >> 6
if abs(velocity) < minimum_velocity:
    velocity = clamp(error * 128, -256, 256)
velocity = clamp(velocity, -speed_limit * 256, speed_limit * 256)
value += velocity >> 8
wrap angle components to [-2047,2048]
```

The unobstructed spring, damping, minimum-velocity and speed-limit parameters come from `8006172c`, `800617b8`, `80061858`, and `80061844`. Charge enforces the recovered minimum yaw spring of 100. State mapping comes from `80061468`. Native simulation executes one integration step at each 1/60-second tick; it does not apply a stock 30 Hz frame step sixty times a second.

Tests include three independently calculated MIPS-equivalent steps of a 90-degree turn, charge-profile transition values, angle conversion, and identical simulation/camera results with 30, 60 and 144 Hz presentation inputs.

### Projection

- `80011d24` sets GTE screen center `(256,120)` and projection distance `H=0x155` (341).
- `8001c2f8` multiplies the camera matrix's vertical row by `0x140 >> 9`, i.e. 320/512.
- `80011bbc` establishes a 512x240 display and a 216-line draw region inset by 12 lines.

For a native square-pixel image, the full 4:3 display has:

```text
vertical_fov = 2 * atan(240 / (2 * 341 * (320/512)))
             = 58.7634 degrees
```

The native window preserves this vertical framing and extends horizontal coverage with aspect ratio. It renders the old safe-area margins too, rather than inserting the original black strips. At 4:3, tests verify projected coordinates against GTE H=341 with the original vertical correction. Editor picking and the level renderer use the same projection.

### Obstruction and remaining camera work

The native camera uses the original sphere radius 256 and maximum sweep slice 240 from `8001e454`. Its terrain sphere resolver and line-of-sight pull-in are native approximations. Object collision callbacks, the original orbit search in `8001e808`, obstruction-dependent spring-table selection, turning-velocity lead, active/passive camera modes, swimming/flying modes and scripted cameras are not yet fully reconstructed. Look mode (camera mode 7) and the PC right-stick orbit are described in [CONTROLLER.md](CONTROLLER.md). The current normal/front/charge profiles and base integration are evidence-backed; full camera equivalence is not claimed.

**Backed against a wall (10 October 2026).** The line-of-sight pull-in used to press the camera into Spyro when a wall stood right behind him. It now searches for a clearer arm if the result would sit closer than 1.1 units:
- arms lifted 0.35, 0.7 and 1.05 rad over Spyro;
- each swung 0, ±0.5 and ±1 rad around him.

It takes the clearest and eases towards it (30% per tick). This is a native stand-in until `8001e808`'s orbit search is reconstructed. The check is in `core_tests` (the 10-unit wall behind Spyro). `data/validation/retail-scripts/sweep-20261010/metro-camera.png` and `metro-camera-fixed.png` show it before and after, beside Metropolis's shielded robot.

## Validation outputs

`tools/validate-retail.ps1` writes inspection reports, native viewport captures and paced replay metrics to `data/validation/`. The source-independent tests cover scale, model handedness, projected coordinates, camera spring steps, wall clearance and a 0.8-unit passage that fits the original 712-unit-diameter player sphere.

Screenshots alone cannot establish original timing or exact camera equivalence. A later milestone should compare synchronized camera/Spyro traces against the retail executable, especially around slopes, obstructions and gameplay-state transitions.
