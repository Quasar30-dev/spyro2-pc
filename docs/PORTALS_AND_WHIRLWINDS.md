# Campaign portals and whirlwind elevators

23 September 2026. Profile: US `SCUS_944.25`, CRC32 `cdc0917c`. Assets are read from the user's mounted disc/extraction; no replacement portal art is bundled.

## Playing

Walk, charge or glide through an open portal. Departure locks movement, moves Spyro through the opening, fades out, loads its destination and returns control after arrival. Ordinary exits return beside the corresponding home-world doorway. Home-world transporters use their paired arrival pads, with a spin departure and protection against immediately travelling back.

Step into an active whirlwind to ascend in Spyro's original clip 18, spiral towards the authored exit heading and leave in a latched glide. The camera follows the exit direction rather than spinning with Spyro. Superfly bypasses the lift. Each lift uses its authored height, radius and activation mode, the bank's type-06 particle image, blue/white type-52 streaks and global sound 1C.

Progress gates remain meaningful. Moneybags' Zephyr, Icy Speedway and Canyon Speedway purchases deduct the executable's price once and persist in PC saves. Ocean Speedway requires 3 orbs; the Autumn Plains whirlwind/Metro Speedway route requires 6; Cloud Temples and Metropolis require 15 and 25. Dragon Shores uses saved Ripto completion. Home transporters require a previously visited destination. Return Home uses level completion/reward state; an unrelated challenge orb does not open a talisman level's exit.

**Workbench → Cheats → Unlock portals and whirlwind elevators (session)** bypasses travel gates for exploration. It survives Play/Stop and level changes for that workbench session, does not award progress, and is deliberately not written into game saves. It is useful while the separate quest/boss scripts remain incomplete. Turn it off to restore progress checks.

## What the retail code/data establishes

`tools/probe_portals.py` inventories 42 apertures, 26 whirlwind objects and 12 home transporters across the 29 campaign banks. The evidence JSON is `data/analysis/portals/inventory.json`.

| System | Evidence |
| --- | --- |
| Apertures | Scene child 1, following collision and sky: count plus 68-byte records. Five signed XYZ points, section-relative picture at +3C, visibility at +40, two-sided flag at +41 and destination scene ID at +42. |
| Portal picture | Resident `8004C66C` projects the aperture; `800296D0` clips two 60-byte panorama headers and their packed vertex/colour faces in screen space. Native rendering keeps that screen-space clipping, side visibility, foreground depth occlusion and screen-linear colour gradients, with near-plane clipping for close approaches. |
| Portal crossing | Special collision surface type 6, `8002AAFC` → `80019928`. The latter selects +42 for homeworld doors, or the resident return table `80064B84` for ordinary levels. Native swept segment/aperture intersection also catches high-speed charge crossings. |
| Departure/load | `80019C40`, steering helper `80031B94`, loader overlay 1 `80071BD4`. The original has a 32-VBlank departure camera and a staged flying/gem-tally loading presentation. |
| Return Home | Class 1AA, Glimmer `80075024..80075550`; aperture from resident `800631F0`, arch rises 2,000 units over 40 script updates, opening uses 7/8-unit distance hysteresis and 32/256 scale increments per script update. |
| Home transporters | Class 23B, Summer Forest `8007540C..80075CA0`; parameters +4 destination index and +8 paired arrival ID. Resident `8001AC40` searches the destination pad by this ID. Native departure preserves the 88-VBlank sequence boundary. |
| Purchases | Shared Moneybags class 77, `80077008` / `8007728C`: +48 purchase, +4C portal index, +44 barrier helper. Purchase bits 10, 12 and 18. |
| Orb portals | Summer Elora `80078234` (3); Autumn overlay 32 `80077A18` (6); Winter Professor `80076E64` / `80076EA4` (15/25), portal/barrier fields +40/+48. Fresh Winter Ghidra instruction export: `data/analysis/portals/winter-gates.txt`. |
| Whirlwind activation | Class 400, Glimmer `8007A530..8007A8A4`: +0 height, +4 radius, +8 activation kind, +C timer. Kinds 0/1/2/3 are permanent/proximity/externally controlled/distance-ring. |
| Whirlwind motion | Overlay 16 `8006EE18` state 12 initialisation, `8006DB88` movement, `8006F1D0` exit checks. Native conversion preserves the 128-unit velocity acceleration, 4800 cap, quadratic entry/exit turns, radius+1024 release target and automatic glide. Camera mode 17 hex comes from `8006FB10`. |
| Effects | Shared particle construction `8007E100` and update `8007CC38`: type 06 sprites use independent random starting phases and rise 64 units per script update; type 52 streaks rise 192 units and have a 384-unit endpoint offset. |

Fresh resident Ghidra output is in `data/analysis/portals/resident-mips.txt`. Two attempted selected decompilations timed out; their failure files are not used as evidence. A broad Winter dump reached the end of the mapped overlay; the subsequent bounded gate export completed successfully. Existing full instruction exports supply the large dispatch cases.

## Verification and current boundaries

`spyro_portal_tests data/disc` checks the complete inventory, picture clipping on both sides, high-speed aperture crossing, disabled gates, all 26 lift routes against actual terrain, every paired home transporter, payment/threshold gates, save positions and 30/60/144 Hz presentation independence. The lift approach is projected onto its floor: Zephyr's emitter starts below the walkable surface.

`--portal-check --headless --frames 740 --no-capture` exercises the installed workbench loader through Summer Forest → Glimmer → Summer Forest → Autumn Plains → Winter Tundra → Summer Forest. It checks gems, health, purchases, upgrades, arrivals and remembered scenes. `--capture <base>.bmp` selects the JSON report basename with `--no-capture`; no image or video is written. Logs are under `data/validation/portals-20260923/`.

The simulation runs at 60 Hz and rendering interpolates it. Loading still runs synchronously and can hitch; uninterrupted 60 fps across loading is not established. The original flying gem-tally loading screen, exact steering/camera trajectories, gate-opening cinematic commands and original shared particle RNG/positional loop attenuation remain unfinished. Current arrival steering/camera easing and widescreen panorama projection are native adaptations. Portal transporters do not replace the separately scripted boss-door/story transitions. Generic quest-controlled lifts accept their object's state, but their individual mission callers remain part of the campaign backlog; use the explicit session override to explore them now.

No footage or review captures were made for this pass, as requested. Nonvisual tests do not establish visual parity with the PlayStation renderer.

## 2 October particle correction

Large stars now have independent birth phases and use the original 64-unit ascent and alternate-update emission cadence. They no longer form a shared height-driven spiral. Thin streaks use the original randomly chosen half-to-full radius, 192-unit ascent and 384-unit length. Deterministic per-birth seeds and interpolated presentation preserve 60 Hz rendering. See `data/analysis/portals/whirlwind-particles.md` for construction/update addresses and validation. Original shared RNG remains a documented difference.
