# Upgrade artwork and interface

Open **Pause > Options > Upgrades**, or **Workbench > Upgrades > Open upgrade
page**. Mouse, keyboard and gamepad navigation use the existing menu controls.
Enable progression, collect gems for upgrade points, and select a card to buy
its next tier. Purchases use the existing price, cap and save data rules.
Unavailable cards remain focusable to explain their effects and cost.

The six GPT Image sprites cover supercharge, superflame, superfly, glide boost,
aerial carry and melee. Source art and the exact reference-based prompts live
under `assets/ui/upgrades/`. The atlas is embedded in the main executable.

The default is **Once per power**: each owned ability is announced for three
seconds when first equipped or used during this application session. Repeated
uses, points, tier changes and Tab do not replay it. This history survives
level changes, restart/Play, save loading and toggling progression or HUD modes.
After all owned abilities have appeared, the strip stays hidden for the rest
of the session. Buying a different ability allows its own first announcement.

**Options > Upgrade HUD** offers Once per power, On each use, Manual (hold Tab),
Always and Off, with a 1–8 second reminder duration for the timed modes.
These settings are included in saves (v12 onward); older saves use the quiet
default. Reminder history is session state, separate from saved preferences.
Disabled progression or Off hides the entire upgrade HUD, even with purchased tiers.
Dialogue, scene cameras, portal travel, challenge UI and hidden-HUD settings
suppress it. Pausing shows the native upgrade page rather than the game strip.

- Charge: 130% / 160% speed, with an active sparkle during supercharge.
- Flame: SEEK / SPLIT; POWER means its base power is unavailable. Green
  firebomb does not claim to have the superflame upgrades.
- Flight: 125% / 150%, active during actual superflight.
- Glide: remaining / purchased boosts; spent lights stay empty until landing.
- Carry: FLY before flight; READY, SEEK or HELD according to the real carry state.
- Melee: READY or the current hit number in the three-strike combo.
- Headbash: CRATER / LAVA, or NORMAL when terrain effects are deselected. Its
  seventh marker uses a purple impact arrow and gold ring. See
  [Headbash terrain upgrades](HEADBASH_APOCALYPSE.md).

Tier lights show purchased tiers; glide lights show remaining boosts. Dimmed
sprites distinguish locked, dormant and spent abilities. HUD settings do not
change gameplay powers, the retail HUD or the multiplayer protocol.

Validation entry points:

```text
spyro_upgrade_hud_tests
spyro_workbench --game data/disc --headless --frames 1000 --upgrade-hud-check --capture-fullscreen --capture <path.bmp>
spyro_workbench --game data/disc --headless --frames 30 --play --upgrade-tier 2 --interface-page upgrades --capture-ui <path.bmp>
```

The integration check clicks through pause options, enables progression, buys
both glide tiers, rejects an unaffordable purchase, resumes without recreating
the game, verifies once-only use across level changes, exercises manual Tab
recall and Off, triggers melee/charge with real inputs, and loads saved mode
and duration choices without resetting reminder history. Captures are
written alongside its report for visual inspection.
