# Campaign purchases and Glimmer bridge

Implemented against USA `SCUS_944.25`, CRC32 `cdc0917c`, on 22 September 2026. This closes one mapped service and its world change; the remaining campaign scripts are tracked in [REMAINING_WORK.md](REMAINING_WORK.md).

In Glimmer, approach Moneybags (record 9 / class `220`) and press **E**. With at least 100 gems, his original offer shows its price and authored answers. **1/2** choose the corresponding displayed answer; **Enter** continues and **Esc** dismisses. Accepting deducts exactly 100 gems once. After the reply, the camera moves to the authored bridge view, the original bridge geometry and collision extend, then control returns. Insufficient funds, refusal and cancellation do not charge or open the bridge. Revisiting a paid service selects its post-purchase reply.

## Resident commands and overlay evidence

- `8003a568` implements command `25`: two one-based reply targets, a purchase index and an insufficient-funds target. Bit 7 of each decoded reply controls dismissal. The price/paid records begin at `80064670`, four bytes per entry; Glimmer uses index 3 at a cost of 100.
- `8003a0d8` selects the unaffordable branch before presenting the offer. `80015be4` substitutes the `^^^` price field and separates answer labels at `&` delimiters. Hunter's camera question can display **No**, then **Yes**; input follows the authored order.
- Glimmer overlay 16, `8007ded0..8007e1ac`, reacts to the paid flag. Moneybags parameter `+2C` points to the camera record; eye at `+0C` and look-at at `+1C` feed resident `8001f24c`. Once the view is ready, it enables animation group 1 and requests Moneybags sound 2.
- `8001c734` relocates the seven gameplay animation directories. `8002365c` changes group flags and `80023710` controls the timeline node. `800225f8` advances the timeline; `800231f4` initializes collision. The near-vertex morph path is at `80024168..80024310`.

`tools/probe_dialogue.py` inventories the original command bytes for 162 speaking records across 40 levels. Other payments and rewards are retained as unsupported until their corresponding state changes are implemented; reading a price does not authorize deducting it for a missing service.

## Bridge geometry and timing

Glimmer group 1 contains two near-vertex tracks (66 and 19 source vertices) and one 76-triangle collision track, each with seven keyframes and 70 timeline nodes. It stops at node **59**, whose blend is not the final keyframe. The native decoder retains region/source-vertex indices and maps the moving near vertices back to 213 expanded rendering vertices. Animated collision has signed nine-bit Z deltas, unlike the repacked static collision format.

The game owns a copy of the level so playback and restored progress leave the editor/source unchanged. Position revisions update both GPU buffers and translucent geometry/sort centres. The native 60 Hz controller interpolates between the authored two-VBlank samples, including collision; original fixed-point rounding is not reproduced exactly. Texture animation, far-only geometry and other deformation groups remain open.

Camera coordinates are authored. Its native easing and the 40-tick setup / 30-tick finishing hold are approximations of the original camera-ready/completion gates. This is not a claim of exact retail camera timing. Input is held and ordinary HUD parts hide during the scene.

## Save policy

S2SAVE8 adds a bounded 19-bit purchase mask to the version-7 payload. Versions 1–7 remain readable. Loading a paid Glimmer service applies the terminal bridge mesh/collision immediately; it does not replay the camera or deduct gems again. Saving midway through construction commits the completed paid service on load. Loading an unpaid save into an already-open session resets the bridge. Prices and geometry always come from the mounted retail copy.

Version 6 retained released butterflies and the extra-life drop counter. Version 7 added held/thrown bodies, their transforms and velocities, grip/throw age, active power timers and flight velocity/orientation. The saved flying pose is restored before rendering. Object bounds are rebuilt from decoded models and all save input is validated before applying it.

## Verification and footage

All 22 CTest suites pass. `spyro_campaign_tests data/disc` passes **11,960 checks**, covering payment, cancellation, insufficient funds, duplicate prevention, source isolation, scene input lock, actual retail tracks and terminal collision, save restoration and all mapped Exit Level arrivals. A genuine version-7 fixture verifies compatibility. D3D11 WARP checks exercise changed opaque geometry, collision buffers, transparent depth ordering and switching between gameplay/editor copies.

`tools/verify_scene_morphs.py` runs the mounted executable's `800231f4` and near-track portion of `800225f8` in the bounded RE interpreter: **76 initial collision triangles and 282 timeline states match, over 16,088 instructions**. This oracle excludes GTE collision interpolation and camera behavior.

```powershell
python tools/verify_scene_morphs.py data/disc --output data/validation/limitations-20260921/bridge-oracle.json
./build/Release/spyro_workbench.exe --game data/disc --bridge-replay --headless --frames 660 --capture-fullscreen --capture-music-volume 0.3 --video data/validation/limitations-20260921/glimmer-bridge.mp4 --capture data/validation/limitations-20260921/glimmer-bridge.bmp --save-game data/validation/limitations-20260921/glimmer-bridge.s2save
```

The replay uses the real dialogue/payment path, starting with 150 gems and ending with 50, purchase mask 8 and control released. `glimmer-bridge-offer.png`, `glimmer-bridge-moving.png` and `glimmer-bridge-open.png` were visually inspected. The video contains 660 H.264 frames at 60/1 fps with 48 kHz stereo AAC. Offline recording cadence does not establish sustained live 60 fps.

## Pause-menu Exit Level

Resident `80019928(-1)` selects the home-world scene ID from `80064B84`. The 29 campaign indices map to scene IDs through `80064994`; these include three home worlds and 26 destinations with a return route. The native pause menu now exposes **Exit level** for these routes. Home worlds, procedural worlds and online guests cannot request this campaign transition; online hosts retain destination ownership.

The home loader at `80071BD4` matches the departed scene ID to byte `+42` of a 68-byte portal record. The native arrival uses that doorway's authored geometry and orientation, then projects a standing point onto its approach floor. All **23 ordinary return doorways** have supported arrival floors. The three boss arenas have separate gate behavior; their current fallback is the home world's authored starting point. Native arrivals are grounded, with the full fly-in animation/camera, loading presentation and talisman-gated Return Home portal scripts still open.

The same campaign checkpoint path runs before departure and after arrival. Gems, purchases, removed actors, released contents and supported conversation progress remain associated with their original scenes. The real ImGui `--menu-check` replay now passes **69 assertions**, including two Exit Level button clicks, correct Glimmer-to-Summer-Forest placement, retained inventory, a still-collected gem and the restored paid bridge on revisiting Glimmer. It also checks that guests cannot override the host. The arrival screenshot at `menu-check-2659631600200/campaign-return.bmp` was visually inspected.
