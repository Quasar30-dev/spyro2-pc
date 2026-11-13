# Aerial moves and Scorch sand

Implemented against US `SCUS_944.25`, CRC32 `cdc0917c`, using the mounted original model and animation banks. The optional glide extension is a new PC ability; hover, headbash and sliding use recovered retail action constants.

## Controls

| Action | Keyboard | Behavior |
| --- | --- | --- |
| Jump / glide | Space or X, then a second tap | Queues glide through ascent; glide continues after release and without W |
| Glide braking | S or Down | Slows forward approach; does not turn Spyro around |
| Hover | H while gliding | Cancels glide, gives the retail lift and transitions into falling/landing |
| Headbash | B in the air; H when not gliding | Plays the flip, dives vertically, attacks on contact and recovers on landing |
| Optional wing boost | Further Space/X taps while gliding | One boost at tier 1, two at tier 2; original Superfly-up clip; refills on landing |
| Sand slide | Walk onto authored slippery sand | Downhill movement, original slide animation and looping slide sound; jump escapes |

On a controller, A (Cross) jumps and Y (Triangle) is the contextual hover/headbash action; see [CONTROLLER.md](CONTROLLER.md). Keyboard play and synthetic input are verified; physical controller testing is separate.

Enable and buy the glide upgrade under **Upgrades**, or choose **Unlock all for testing**. The master progression checkbox alone does not buy it. **Unlock all and activate powers** additionally enables the base power cheats; disable Superfly to try glide, as active Superfly makes jump take off into free flight instead. Holding jump never spends another wing boost. An extra tap after the allowance is exhausted leaves Spyro gliding. Hover, charge interruption and toggling upgrades in midair do not refund spent boosts. Water, free flight, damage and dialogue cancel incompatible aerial actions.

## Retail evidence

All listed velocity constants are Q6 original units per VBlank, converted by `60 / (64 * 1024)` to native world units per second. Movement advances at the fixed 60 Hz tick.

- `800351d8` enters hover state `11` with vertical speed `c00`. `80033300` gives horizontal target `640`, response `280`, initial vertical lift `17`, then gravity `c0`. The glide's saved height caps the hover. `80035d38` selects hover from glide and headbash from another special press. Clip 17 supplies the original pose.
- Headbash state `2e` enters at vertical speed `800`, holds its flip phase for 42 ticks with gravity `20`, then uses downward acceleration `800` capped at `3200`. Ground impact uses shake `1000 / 30` and global sound `14`. `80032ab4` identifies attack bit `80000`. The native sweep combines it with the ordinary charge contact path, including retail basket destruction. Clip 46 includes the flip, dive loop and exit frames; normal transition handling plays the exit on landing.
- Scorch scene slot 45 uses overlay 44. `8006dcb8`, `8006e2fc`, `8006e60c` and `8006e9f4` were exported with Ghidra into `data/analysis/overlay_044/selected/`. State `31` drives the downhill response: target `f00`, speed approach `80/c0`, vector approach `100`, heading from the slope normal, and off-surface grace of 16 ticks / four unsupported ticks. Clip 49 and global loop sound `1f` are selected.
- Loader `8006fa98` relocates scene section 4's pointer directory at `800673e8`. `8002b078` resolves each triangle flag's low six bits, with 63 meaning no record. `8002aafc` routes surface kind 10 into state `31`. Scorch has 1,276 matching triangles. Hurricos also contains kind 10 and its clip 49. Several other retail scenes retain tags with no matching record; these retain solid geometry with an unknown surface response rather than dereferencing a nonexistent record or inventing sand.

The optional boost reuses mounted Superfly-up clip 34 and a 12-tick lift / 24-tick release envelope. It preserves ordinary glide steering and braking, raises the allowed hover ceiling only as altitude is gained, then returns to glide. It never enables Superfly itself. Upgrade costs are 25 and 50 points, with a maximum tier of two. Save format 5 appends this tier; formats 1–4 still load, with the new tier locked.

## Validation and recordings

- `spyro_aerial_tests.exe data/disc`: **26,482 checks**, including no-hold/no-forward glide, hover height cap, tier 0/1/2 boost limits, held input, upgrade disabling, landing refill, headbash impact/recovery and a real retail basket hit, slippery versus ordinary ramps, jump exit, old-save compatibility, all 41 mounted banks and their animation transitions.
- Full Release CTest: **16/16 suites**. Retail extension checks: **12,994**.
- `--upgrade-workbench-check`: validates unlocks before Play, edits while stopped, buying while stopped, level changes, required base powers and three real J-key attacks with no target.
- `data/validation/scorch-sand-slide-60fps.mp4`: six seconds, 360 frames; 220 simulation ticks in Slide followed by ordinary ground motion.
- `data/validation/scorch-aerial-moves-60fps.mp4`: eight seconds, 480 frames; two boosts, a rejected third request, one hover and one headbash impact. No held jump or forward input drives the sequence.

Both native captures are 1418×904 H.264 at 60/1 fps with 48 kHz stereo AAC, with matching audio/video durations. They are deterministic recordings of actual gameplay frames, not a sustained frame-time benchmark. JSON sidecars record the action counters. `--movement-replay sand|aerial|headbash` is restricted to bounded captures.

Remaining fidelity work includes the full original jump equations, surface body alignment, the original headbash impact dust callback and remaining special-surface responses. The current sphere solver is shared with ordinary native traversal; recovered action constants alone do not establish identical retail collision behavior.
