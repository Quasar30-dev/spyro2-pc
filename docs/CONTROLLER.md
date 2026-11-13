# Controller (XInput)

The controller plays the game and nothing else. It never navigates or changes
the workbench's ImGui panels, whichever panel has focus.

## How input flows

- **Reading the controller.** `Workbench::poll_pad` (`src/desktop.cpp`) reads
  XInput once per presented frame, before `ImGui::NewFrame`.
  - XInput is loaded at run time: `xinput1_4`, then `1_3`, then `9_1_0`.
  - The first connected controller in any of the four slots plays.
  - Empty slots are rescanned only after `WM_DEVICECHANGE`, or every 2 s while
    none is found, because polling an empty slot is slow.
  - Nothing is read while the window is in the background, or in automated
    `--frames` runs, unless a fixture scripts the samples.
- **ImGui never polls the controller.** `spyro_imgui` is built with
  `IMGUI_IMPL_WIN32_DISABLE_GAMEPAD`, and `NavEnableGamepad` is off.
  - Before this change, ImGui read slot 0 itself. Its navigation moved focus
    across workbench panels, and A (Cross) activated checkboxes and buttons.
    Holding X (Square) opened ImGui's window switcher.
- **Mapping to game input.** `GamePad` (`include/spyro/gamepad.hpp`,
  `src/gamepad.cpp`, in `spyro_core`) turns each sample into `InputState`.
  - A press is the edge between two polls. `Game::advance` holds it until the
    next 60 Hz tick, so each press reaches the simulation exactly once at any
    frame rate.
  - Holds are sampled every frame.
- **The left stick.** It uses XInput's radial dead zone (7849). Past the dead
  zone the direction is exact and Spyro runs at full speed, as with the keys.
  The retail game has no walk.
- **Changing owner.** A button held while control passes between the game, its
  menus and a cinematic counts only once it is released
  (`GamePad::hold_back`). This covers:
  - the Cross press that closes a menu, which does not also jump;
  - a held Cross when the pause menu opens, which does not pick a menu item;
  - a controller that connects, or a window that regains focus, with buttons
    already down.
- **The game's own menus** (title, files, pause, guidebook, upgrades) are ImGui
  widgets.
  - While one of them is open, it is the only window drawn. Only then does
    `poll_pad` pass the D-pad, the left stick and A to ImGui navigation.
  - B (back) and Start (resume) go through the game's own menu handling.

## Mapping

The original PlayStation layout comes first. Xbox positions map directly:
A = Cross, X = Square, B = Circle, Y = Triangle.

**Original controls**

| Control | Action |
| --- | --- |
| Left stick | Move. Underwater and in flight it steers, as W/A/S/D do. |
| A (Cross) | Jump. Press again in the air to glide. Advances dialogue. |
| X (Square) | Charge (hold). |
| B (Circle) | Flame. Hold to keep up elemental breath. Leaves dialogue. |
| Y (Triangle) | Talk. In the air: hover while gliding, otherwise headbash (after a hover too). Held while standing: look mode. |
| Start | Pause and resume. |

**PC additions**

| Control | Action |
| --- | --- |
| LB / RB | Dodge roll left / right (July prototype; needs the upgrade). |
| RT / LT | Element shot / Fury. Hold LT to carry the Fury into later stages. |
| D-pad left / right | Change element. |
| D-pad up | Melee (upgrade). |
| D-pad down | Carry. With X held it throws, as Shift+G does. |
| Right stick | Turn and tilt the camera (up looks up). In look mode it steers the view. |
| Right stick click | Centre the camera behind Spyro, and reset its height. |
| Back | Recall the HUD (hold). |
| LB + RB + RT | Sparx's gem hint (hold). The roll and the shot are held back meanwhile. |

**In dialogue, menus and cinematics**

| Where | Controls |
| --- | --- |
| Dialogue | D-pad or stick up / down picks an answer. A or Y continues. B leaves. |
| Game menus | D-pad or stick moves. A selects. B goes back. Start resumes from pause. |
| Cinematic | A, B or Start skips. |

When a controller is connected:

- retail dialogue shows A / X / B / Y in place of Space / Shift / F / H;
- the talk prompt reads "Y  Talk to …".

## Headbash is a learned move

In the retail campaign, Triangle in the air only headbashes once Moneybags has
taught it in Winter Tundra (purchase 6, `Game::ability_learned`). Before that,
a glide then a hover works, but the next Triangle does nothing, as in the
original. Custom levels, the procedural universe and the session progression
override allow it everywhere.

## Look mode (retail)

Holding Triangle (Y, or H on the keyboard) while standing enters the original
look mode: camera mode 7.

- **When it is allowed (`80030c94`).** Only in the standing states, and only
  while no moby claims Triangle; next to someone it talks instead. After a
  headbash, Triangle has to be released before looking (`8006a1b2`).
- **Entering (`80021dc0`).** Holding Triangle switches the camera to mode 7, and
  `800216d8(7)` clears the offsets. Releasing Triangle, or leaving the
  standing state by jumping, charging or being hurt, switches back.
- **The camera (`8001fa58` case 7).** It draws in to the table at `800615b0`:
  - azimuth behind Spyro, elevation 240, radius 768, pitch bias -128;
  - springs 256 (`80061790`), damping 12 (`8006181c`).
- **Steering (`8002b694`).** The D-pad or left stick moves a steering value
  0x10 a tick toward ±0x7f, and it falls back by 0x20 when released. Once the
  camera has drawn in (within 0x200 of its radius), half the steering value
  is added each tick:
  - yaw within ±0x400;
  - pitch within -0x3d0..0x500.
- **Pitch is flight-style, as in the original.** Up raises the camera, so the
  view looks down. Below -0x140, the camera stops dropping and the view tilts
  up instead.
- **Spyro's head turns with the view.**
  - The targets are `8006a17c = -yaw` (within 0x300) and
    `8006a178 = -pitch` (within 0x200).
  - They spring into the drawn angles (`800308d4`).
  - The head part (vertices from the clip's split) turns about the neck
    attachment, as `80041dbc` / `8001b6c8` do.
  - Six retail neck faces join head and body, so the seam stretches at large
    turns, as it does in the original.
- **Spyro stays put.** Flaming still works.

PC deviations:

- The camera is held short of straight overhead (elevation 1000). Retail allows
  0x500 more pitch, which would carry it over the head.
- The right stick also steers the view in look mode, with up looking up.

## Right-stick camera (PC)

- Turning moves the camera directly.
- The camera then stays where it was put:
  - while the stick has been used in the last 1.5 s;
  - while Spyro stands still.
- Once Spyro runs, the retail follow spring swings it back behind him.
- Tilting moves the camera height directly, within -140..+420 of the retail
  elevation. The height stays until the camera is centred (RS click / C).
- It doesn't act in conversations (camera mode 12) or look mode.

## Verification

- **`controller_input` (CTest, `tests/gamepad_tests.cpp`).** Checks the mapping,
  trigger threshold, radial dead zone, unit stick direction, stick-flick
  hysteresis, press edges, hold-back across context changes, and release on
  disconnect or focus loss.
- **`--pad-check` (`src/desktop_pad_check.inl`).** Scripted samples go through
  the real workbench loop while the **Session** panel, not the viewport, has
  focus.
  - The stick moves Spyro and does not move ImGui focus.
  - A jumps, X charges, and B flames without pausing.
  - Start pauses. In the pause menu the D-pad moves the selection, and B
    backs out.
  - A held through opening the menu activates nothing. Start resumes.
  - No workbench setting changes. ImGui gamepad navigation stays off outside
    the game's menus.
  - In the air (with headbash learned), A glides, Y hovers, and Y again
    headbashes.
  - Back on the ground, holding Y enters look mode. The stick turns the view
    and Spyro's head while he stays put, and releasing Y leaves look mode.
  - The right stick turns the camera.
  - Photos are written to the capture folder: `look-mode-centre.bmp`,
    `look-mode-right.bmp` and `right-stick-turned.bmp`.
- **`look_mode_and_camera` (CTest, `tests/look_tests.cpp`).** 21 checks:
  - look entry and draw-in;
  - Spyro staying put;
  - the yaw and pitch limits and the head turn;
  - the release behaviour, jump exit and headbash release rule;
  - the right-stick hold, swing-back, tilt and centring;
  - head-part rotation.

  Given the disc path, it also reports the six neck faces that join the head
  and body.
- **Aerial tests (`tests/aerial_tests.cpp`).** Triangle after a hover
  headbashes: during the lift, mid-hover and after the hover ends.

```
build\Release\spyro_workbench.exe --game data/disc --slot 15 --play --workbench --smoke 520 --pad-check --mute --capture data/validation/controller/pad-check.bmp
```

On 2026-10-25 this passed. No physical controller was
connected to the build machine (all four XInput slots were empty), so
hardware feel still needs a hands-on check.
