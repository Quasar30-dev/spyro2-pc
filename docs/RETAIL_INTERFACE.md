# Retail dialogue, pause and guidebook

The interface is decoded from the player's mounted USA SCUS_944.25 copy.
No font files, screenshots or extracted artwork are shipped with the engine.
ISO, CUE/BIN and extracted folders use the same AssetStore path.

## Controls

- **E** starts a conversation. **Enter** reveals the passage, then advances to
  the next passage or Spyro's answer prompt. Long passages scroll in the box.
- **Up / Down**, then **Enter**, selects an answer. **1 / 2**, mouse clicks and
  controller confirm/cancel/D-pad are also supported.
- **Esc** dismisses a conversation; another press opens pause.
- **Esc / controller Start → Guidebook** opens the parchment book. Select a
  visited level using mouse, keyboard or controller navigation. **Left / Right**
  turns pages; **Esc** returns through level page, contents and pause.
- **Continue**, **Save Game**, **Exit Level**, **Options** and **Sound** remain
  available. PC Save As, Load, game-copy selection, main menu, workbench and quit
  stay on the pause page. Native Windows file pickers and F5/F9 are retained.
- Pause Options controls active/passive camera and HUD visibility. Sound adjusts
  music, voice and effect volumes and muting.

## Recovered data

| Retail source | Implementation |
| --- | --- |
| `8004a374`, `8004a59c`, `8004a7dc` | 12 × 9 bitmap glyphs, character advances, signed baseline offsets, colours and combining accents |
| `800636a4`, `800637a4`, `800637d4` | Character descriptors, escape mapping and 14 modulation colours |
| `80013bc4`, `80015be4`, `8004d3fc` | Rounded dialogue panel, speaker tab, five-line clipping, answer prompt and price footer |
| `800633f4`, `80063418` | Original 18-sample entrance and 12-sample exit curves, interpolated at 60 Hz |
| Level child 0, `+0x80000..+0x86000` | Pause artwork, originally cached in SPU RAM at `0x7a000` and uploaded to VRAM `(512,128,192,64)` |
| WAD 4, 32 × `0xc000` | Contents, homeworld pictures, level headings, objective/reward artwork and additional book banks |
| `8004e238`, `8006387c`, `80063e38`, `80063ec0` | 4-bit/8-bit sprite descriptors, CLUTs and transparency |
| `80063918..80063e38`, `8004ecbc`, `8004f200..8004fb48` | Original panel, page border, inventory, contents and level layouts |
| `800647f4..800649b4` | Level maxima, objective-bit ordering, scene IDs and names |
| `8004bf94`, `8004c068`, `80061bd8`, `80063870` | Page-turn sine table, frame angles, spine `(256,120)` and perspective depth 512 |
| `8005004c`, `800501c0`, `80064720`, `80064770` | Skill-point headings, earned descriptions and associated levels |

`tools/probe_interface.py` produces inspection sheets in
`data/analysis/interface/`. Fresh Ghidra instruction evidence for the page turn
is in that directory's `page-turn-mips.txt`. Atlas gutters duplicate each sprite's
edge pixels to prevent lines between parchment tiles.

An ImGui draw list transports the decoded sprites, and invisible hit regions
provide input. Dialogue, pause, inventory and book text use the retail font.
Editor controls and PC path/error dialogs retain their normal UI. Spoken control
instructions use PC key labels; original controller-labelled artwork remains
inside the book. The original contents table excludes Dragon Shores from its
seven Winter Tundra entries. Unvisited levels remain unnamed.

## Progress and compatibility

Level gem totals are independent of the spendable wallet. Visited levels,
removed objects, uncollected released gems and editor gem entities feed saved
counts. Completion follows the retail weighting: gem value, 200 per orb and
500 for a talisman/boss, with a minimum 1% after initial progress.

S2SAVE9 adds per-level orb masks, talisman/boss flags, speedway task results and
best times, plus global skill-point flags. Formats 1–8 remain readable. Reward
APIs are idempotent; reading a page or loading a save cannot grant a reward.
Orb badges follow the executable's objective-bit ordering. Earned skill points
show their original descriptions. That page appears after a skill point is
earned or Ripto is defeated.

Quest, boss, speedway and skill-point script callers remain part of the port's
gameplay backlog; the book does not invent completion. Epilogue playback and
movie-theatre navigation are not implemented here. The original map/vibration/
screen-position options are also pending; the working PC camera/HUD/audio
settings are exposed instead. Reveal/scroll timing and page transition ordering
are native approximations; PS1 pixel/dither equivalence is not claimed.

## Verification

The main `build/Release/spyro_workbench.exe` was replaced before validation.
All **23 CTest suites** pass. The final retail-data run passes **82,215 checks**,
covering 514 dialogue passages, 70 choice prompts, all 32 book banks, atlas
bounds/gutters, colour and accent controls, wrapping, reward idempotence and
save round trips, including two actual S2SAVE8 files upgraded to S2SAVE9 without
losing the checked totals, purchase flags, level count or current scene. The
earlier UI replay passed 46 dialogue/book assertions;
the retained PC menu replay passed 69 assertions.

Additional captures were stopped at the user's request. The attempted final
video replay exposed a test-input timing issue during encoder warm-up; the
script now allows Spyro to land before pressing E. No completed final video
verification is claimed. Existing images and logs are under
`data/validation/retail-interface-20260922/`.

For later manual reproduction, `--interface-page pause|contents|level` opens a
bounded capture of the requested interface. `--interface-check --frames 1300`
drives dialogue, contents navigation and every level page. Neither option runs
during normal gameplay.
