# PC saves, Glimmer enemies and the retail HUD

Implementation profile: USA SCUS_944.25, CRC32 `cdc0917c`.

## Using it

- **Game → Save game** / **F5** saves a playing session. The first save opens a Windows file picker, initially in **Saved Games / Spyro 2 PC**. **Save game as…** chooses another folder or filename.
- **Game → Load game…** / **F9** opens a `.s2save` from a PC location. Mount a supported retail copy first. A save can be resumed with an equivalent CUE/BIN, ISO or extracted copy; the original asset path is not embedded in it.
- **Game → New game** starts fresh without deleting existing save files. Stop/Play and scene selection retain the current campaign in memory; writing a save is necessary to retain it after closing the executable.
- In Glimmer (**WAD 017**), the nine small lizards and five large club wielders react to Spyro. Small lizards accept charge or flame; large ones require flame or supercharge. The object inspector's **Play beside enemy** button provides a quick way to try them; record **8** is a large lizard.
- HUD counters normally stay hidden. Collecting a gem reveals the gem counter; completing an enemy's death reveals the spirit counter; a changed life total reveals the life display. **Tab** briefly recalls all three; holding it keeps them visible. **Game → Show gameplay HUD** enables/disables rendering of these counters.

## Save contents and guarantees

`.s2save` is a versioned progression checkpoint, separate from an editor `.s2scene`. It stores:

- Current scene, position, facing and respawn point.
- Campaign gems, lives, Sparx health, existing orb/talisman fields, camera preference and the implemented supercharge cheat/appearance.
- Removed authored object IDs/types for each visited scene; collected gems, broken containers and defeated enemies stay cleared.
- Released but uncollected gems, their source container, denomination and position. They resume under gravity, including gems saved in mid-bounce or mid-delivery.
- Supported NPC conversation progress and editor test entities, including their collected flags.

Alive enemies restart from their authored state. Current S2SAVE8 retains held/thrown bodies, released butterflies, active power timers and flying-player velocity/orientation. Ordinary ground velocity, active attacks, particles and open conversations remain transient. Paid Glimmer bridge progress restores the completed mesh/collision, without replaying its camera or deducting gems again. Saving during a death commits that enemy's defeat; loading finishes its removal and spirit award. Ordinary gameplay awards the spirit when the death clip finishes. HUD presentation resets to the saved totals without recounting previously collected items.

The reader checks version, profile, payload size, CRC32, numeric bounds, duplicate identifiers and current-scene object/parent/conversation references before applying them. Writing uses a temporary file beside the destination, then replaces it with `MoveFileExW`. Retail assets are opened through the existing read-only asset store. The save writer accepts only `.s2save` destinations. This is not a memory-card emulator or a PS1 save importer.

Sparx's saved runtime index is revalidated against the object's type before reuse. This prevents a restored pending gem at the old index being overwritten when Sparx is recreated.

## Enemy evidence and implementation limits

Glimmer overlay 16 loads at `8006d264`; instruction evidence is in `data/analysis/overlay_016/object_dispatch.txt`.

| Evidence | Native use |
| --- | --- |
| `8007c300…8007d008`, class `1F0` | Small purple pickaxe lizards; alert distance in parameters +4, original clips 0/1 idle, 2 escaping, 4 taunt and 6 death. |
| `8007c654…8007c808`, main `80037fcc`, `80038290` | Routes have a 12-byte header and 16-byte XYZ/action nodes. Ground movement uses 135 original units per two VBlanks, 155-unit waypoint tolerance and packed-angle turn limits. Action 99 identifies the circular escape route. |
| `8007d008…8007d5f8`, class `22B` | Large club wielders: notice distance 8192, attack range 3072; clips 1 alert, 2 ready, 3 swing, 4 recovery, 5 settle, 6 death. Ordinary charge is rejected. |
| `8007d304…8007d450`, main `800416a0` | Swing damage at animation phase 34–36, with angle/range/height checks. Phase comes from frame metadata bits 0–8 plus half-VBlank blend age. |
| Main `8004c3d4` | Authored persistence bit and `record[83] & 127` spirit value. These Glimmer enemies carry no gem loot. |
| Animated bank collision pointer +20 | Both enemy classes have type-3 cylinder/cap collision, decoded alongside container collision. |

These were the first two mapped enemy scripts; later coverage includes Hurricos, metal idols, Breeze Harbor and Mystic Marsh. See [the completion ledger](REMAINING_WORK.md) and [later level evidence](SKIES_AND_LEVEL_BEHAVIOURS.md). Friendly class `22A` Gemcutters and class `400` triggers are not treated as enemies. Linked challenge sequences, authored airborne route transitions, exact taunt decisions, full death particle effects and remaining enemy families (the spirit flight to the power-up gate is now native; see [POWERUPS_AND_CAPTURE.md](POWERUPS_AND_CAPTURE.md)) still need reconstruction. The first native version follows ground routes conservatively at cliffs and walls. Spyro's damage knockback/invulnerability and enemy death displacement are native approximations; the retail death clips, hit window and attack immunity are used. Health exhaustion consumes a life and respawns; a full retail game-over sequence remains future work.

Ghidra's full overlay dispatch is too large for the exploratory case decompilations attempted here; those timed out. The implementation uses the existing instruction listing, focused raw instruction export for `800416a0`, readable main helper decompilations and the retail parameter/model data. The temporary case boundaries are not asserted to be original functions.

## HUD evidence and behaviour

Sprites come from each scene's VRAM, with the executable's UV/CLUT descriptors at `8006351c`, dimensions at `800634b8` / `800634d0`, and HUD TPAGE `38`. This supplies the gold digits, animated gem and spirit symbols, Spyro life portrait and separate blinking eyes. The right-hand star symbol counts spirits, not quest orbs.

`8005199c` controls each counter independently. The native controller retains its five phases, 48-VBlank entry/exit states, 120-VBlank hold, accelerated count-up/down thresholds, and reappearance when a new event arrives during exit. `80052498` reads the original 24-byte entry/exit curves at `80063430` / `80063448`; digits use the original three-VBlank stagger. These curves are decoded from the mounted executable and interpolated at 60 Hz. `800159c8` supplies the 90-VBlank manual recall timer. The native Tab input uses that timer. `80052b88` supplies the portrait's blinking-eye selection and offsets.

Dialogue requests a slide out; pause freezes presentation with simulation. Fresh scene/load HUD counters start hidden at the correct totals. Counter events do not expose unrelated HUD parts. Unsupported challenge timers, boss meters, orb/talisman award displays, pause inventory screens and full cutscene overrides remain future work.

## Checks

`spyro_progression_tests` covers save round trips/overwrite/rejection, multiple scenes, pending gems, Sparx recreation, NPC progress, combat, invulnerability, death persistence, cliffs/walls, independent HUD triggers, count animation, hiding/recall/dialogue, and 30/60/144 Hz enemy pacing. It runs with the ordinary build tests without retail assets.

For retail-backed verification:

```powershell
./build/Release/spyro_progression_tests.exe data/disc data/validation/glimmer-progress.s2save
```

This also decodes HUD/model banks across all 41 scenes and runs the real Glimmer club animation through damage, defeat and save/load. The workbench accepts `--load-save PATH`, and a bounded `--smoke N` run can write through the same save action with `--save-game PATH`. Screenshots with `--capture-ui` include the HUD; plain `--capture` captures the world render alone.
