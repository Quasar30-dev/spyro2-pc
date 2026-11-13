# Spyro traversal and animation transitions

Evidence is from USA `SCUS_944.25`, CRC32 `cdc0917c`. The native runtime selects clip **2 (run)** for ordinary traversal; the old workbench selected clip 1 (walk) at every ground speed. Charge remains clip 12. Glide is clip 16 and glide braking is clip 24.

## Primary animation state

`8002e304` advances animation once per elapsed VBlank. `8002e440` advances the primary track; `8002e64c` chooses transitions using the 62-by-62 byte matrix at `8006042c`. The four-byte entries at `80060334` contain loop start, exclusive loop end, transition end and flags. `80062868` maps packed blend counters to vertex weights.

The decoder reads all these tables from the mounted executable. `SpyroAnimator` keeps the current and next clip/frame endpoints, packed blend counter and exit state in the fixed-step game, rather than restarting a time sample whenever the workbench draws a new motion.

- Opcode 1 selects the outgoing endpoint using the original weight threshold (`>95`) and blends to incoming frame 0. Counter `71` through `77` produces weights 32 through 224 out of 256; the eighth VBlank completes the transition.
- Opcode 2 switches directly to the target's initial frame pair.
- Opcode 3 preserves frame phase for the walk / intermediate locomotion pairing with a four-VBlank blend.
- Opcodes 4 / 5 use the outgoing clip's exit window through `8002e568`. In particular, a longer fall landing into idle plays the fall exit frames; a fall shorter than 24 VBlanks takes the short transition.
- Opcode 10 enters at the target's loop start.

Only those recovered opcodes are implemented. The current traversal, swimming, power and aerial motion paths use supported transitions. Unimplemented opcodes produce an explicit error if future movement work requests them. The model studio still permits raw frame scrubbing independently of the gameplay state machine.

Presentation interpolates the previous and current fixed-step poses and yaw. [The secondary controller](SECONDARY_ANIMATION.md) now uses the original action transition table and independently timed head clip, including its return to the body's current movement. Full body/head rotation matrices remain separate work.

## Glide movement and controls

`80033300`, states `10` / `18`, uses a forward target of `1900` even with zero directional input. Negative forward input reduces the target by `1180 * input / 128`; speed approaches it by `80` per VBlank and stays at least `780`. Glide vertical acceleration is `-80`, clamped to `[-780,0]`. `8002c0f4` establishes velocity as Q6 original units per VBlank: accumulated velocity is divided by 64 before world-position integration.

The native glide uses these speed, acceleration and descent constants in the shared 1/1024 world scale. Full forward glide is 5.859375 native world units per second. Digital Down is -127, producing a target slightly above the `780` minimum. A/D or Left/Right steers around Spyro's heading; S/Down reduces forward approach speed without turning him around. W/Up is unnecessary during glide. Space or X jumps; a second tap latches glide. A tap during ascent queues it for the apex, and releasing the button does not cancel glide. H starts hover; B starts headbash. [Aerial movement and Scorch sand](AERIAL_AND_SCORCH.md) records their constants, clips, upgrade behavior and remaining limits.

`80035d38` and `800351d8` confirm clip 24 as the slower glide state. Native selection currently responds to the brake input; retail speed-threshold hysteresis, camera-relative stick projection, banking, exact jump-to-glide thresholds and remaining special-surface responses remain under reconstruction. Hover, headbash and the type-10 sand response are implemented. Ground maximum speeds use recovered Q6 values: run 4.21875, charge 7.1484375 and supercharge 13.0078125 world units per second. Remaining acceleration/contact response and jump physics do not yet establish complete movement equivalence.

## Ground contact and animation stutter — 2026-09-19

The previous sphere solver could carry an upward slope-projection velocity into the next frame, briefly launching Spyro off adjoining triangles. Descending seams also lost contact for a frame. These physical contact losses repeatedly requested fall/landing transitions even while charging on a continuous floor.

Grounded movement now clears the previous step's projected vertical velocity and checks nearby walkable support after collision resolution. Support includes triangle-edge contacts so a shared seam does not behave like a ledge. Snapping is limited by travel distance and sphere radius; a deliberate jump bypasses it, and real drops remain airborne.

The state switch at `80035d38` retains ground animation while `8006a0b0` is below four ticks. The native motion selector now preserves this four-tick delay separately from physical grounded state. A deliberate jump requests its animation immediately. The support solver is a native implementation, not a claim of identical retail slope physics.

`spyro_traversal_tests` runs six 360-tick motion/terrain cases. Before this fix, uneven charge entered the wrong animation for 52 ticks and uneven supercharge for 257. All six cases now report zero incorrect motion ticks. Additional cases verify ledge exits, immediate jumping and matching outcomes across 30/60/144 Hz presentation. Native audio replays also exercise traversal and swimming with the 60 Hz simulation active.

## Validation

Core tests cover run/brake clip selection, forward glide without directional input, slowing without reversing, resuming after brake release, steering, transition timing and interruption, fall exit completion, pause behavior and 30/60/144 Hz presentation independence. Retail inspection exercises the movement transition sequence in all 41 mapped scene banks. The native replay releases forward input during glide and then brakes; captures `retail-run.bmp`, `retail-free-glide.bmp` and `retail-glide-brake.bmp` check the displayed clip IDs.

Ghidra exports are under `data/analysis/main/decompiled/`; the focused instruction dump is `data/analysis/main/traversal_disassembly.txt`. The existing `80033300` and `8002c0f4` decompilations supply the complete glide / position-integration context.
