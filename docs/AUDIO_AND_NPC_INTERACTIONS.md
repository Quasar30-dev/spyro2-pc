# Retail audio and Hunter conversations

Implemented and checked on 2026-09-19 against USA `SCUS_944.25`, CRC32 `cdc0917c`. All audio is decoded from the mounted retail copy. No replacement recordings or bundled game assets are used.

## Trying it

Launch the normal workbench and choose **Play movement test**. The selected scene's soundtrack starts automatically. The **Audio** controls provide mute and separate music, effects and voice volumes. Stopping play or minimizing the workbench pauses playback.

In Summer Forest, approach Hunter and press **E**. Authored automatic greetings also work. **Enter** continues, **1/2** select the displayed answers and **Esc** leaves the conversation. Hunter turns toward Spyro and blends between his original idle and talking clips. The camera conversation changes the native active/passive camera preference; **C** recenters it.

For a direct test, select **record 200 / class 1BC** in the retail object inspector and choose **Play beside Hunter**. This starts beside the jumping-tutorial Hunter. His greeting, voice and repeat conversation work. The jumping demonstration, challenge stages and orb award are not implemented. Record 181 is the camera conversation. The bridge Hunter, record 201, still needs his original placement/path script; the convenience spawn can report that no standing position was found there.

## Music and source formats

The executable's scene-to-track table at `80060140` indexes eight-byte entries at `80064de4`. Each entry contains a sector offset relative to `SPEECH.STR`, a sector count and an XA channel. The music paths at `8001722c`, `80019c40`, `8001a2fc` and `8001ac40` feed these extents to `80012c1c`. The resident CD state machine at `80012cbc` selects file 1 and the requested channel. Despite its name, `SPEECH.STR` contains both music and speech.

All **41 mapped scenes**, **29 distinct referenced tracks**, and **2,517 sound descriptors** decode successfully. Repeated tracks and bank samples are counted separately only where stated. This establishes asset coverage; it does not implement every gameplay event that can request a sound.

| Mounted copy | Music / speech |
| --- | --- |
| Single-track MODE2/2352 CUE or raw BIN | Reads complete sectors directly, with no extraction required |
| Raw 2352-byte-sector image named `.iso` | Same sector detection as BIN |
| Extracted directory | Reads `SPEECH.STR.raw` preferentially, otherwise `SPEECH.STR`; accepts complete 2352-byte sectors, 2336-byte sectors retaining XA subheaders, or RIFF/CDXA |
| Data-only 2048-byte-sector ISO or stripped extraction | Geometry and bank effects can load, but the missing XA headers and extra Form 2 audio bytes prevent music/speech decoding; the Audio panel explains the missing data |

The importer now includes `SPEECH.STR` by default and preserves its raw sectors. Existing extractions lacking that file need it extracted again or can use their original CUE/BIN. It cannot be reconstructed from a data-only ISO.

`XaDecoder` separates channel histories and handles mono/stereo, 18.9/37.8 kHz, and 4/8-bit XA. Extent checks, duplicate subheaders, coding changes, cancellation and RIFF chunk boundaries are checked. A five-second Summer Forest excerpt matches FFmpeg's decoded 37.8 kHz stereo PCM sample for sample, before Windows resampling. This is not a comparison of the PS1's final mixed output.

## Sound effects

The scene loader at `8006fa98` locates the sound metadata after the collision-related section chain. `8001d5fc` exposes the global ID table and 20-byte descriptors: SPU offsets, near/far volumes and distances, pitch range, loop flag and priority. Normal scene samples follow VRAM and a `0x6000` work area in child 0, at `0x86000`. The title loader at `80077374` omits that work area, placing samples at `0x80000`.

SPU decoding retains loop markers and uses signed predictor products with separate truncation. CD-XA uses a rounded combined predictor instead. FFmpeg's generic PSX ADPCM decoder differs in rounding/history treatment and is not treated as a bit-exact SPU oracle. Hand-calculated predictor vectors guard this distinction.

`80050648` resolves object-local sounds; `800506ac` resolves surface sounds; `80050720` allocates a voice and applies the descriptor. `80051350` / `80051548` provide positional attenuation/panning evidence. The native mixer has 32 effect voices, descriptor pitch/priority, distance attenuation and camera-relative stereo panning. Distant events are culled before they can displace nearby sounds.

The current runtime emits:

- Footsteps at the recovered locomotion frame events in `80034e0c`, using the scene footstep table at `800602b4`. Landing currently uses the same default surface category.
- Jump (`0x22`), flame (`3`), charge loop (`0x21`) and underwater movement loop (`6`) through the recovered global IDs. Water follows overlay 14's state entry (`8006f0a0`): landing on the surface or surfacing (states 2B/2C) plays global `7`, and only diving under (28/29/2A) plays global `8`. The underwater hit (state 39, overlay 58 `8006fcd0`) plays `0x18`, pushes Spyro along the full attacker bearing with no lift (twice the land impulse), and holds clip 57 for 25 VBlanks.
- Gem collection and supported container breaks using class-local sound 0, following the corresponding overlay calls.
- Nonnegative sound bytes in animated Moby frame metadata, following `80041068`.

Prototype sound IDs are translated when decoding the bank, before either the
XAudio2 or offline capture path resolves events. August's jump is global `0x23`
and charge is `0x22` (`8002fbf8` / `8002ddb0`); July's equivalents are `1` and `0`
(`8002f19c` / `8002d69c`), also used by the Jampack bank. Passing retail IDs
previously selected a loop for August's one-shot jump (which was rejected) and
an absent charge sample. Prerelease globals `0x15..0x21` also shift down by one
in retail. Object-local sound indices do not undergo that translation.

Ghidra exports are in `data/analysis/prerelease-20260930/{july,august}-global-sounds/`.
`spyro-sound-id-matches.json` records exact compressed-waveform matches across
all 28 July and 29 August gameplay banks. July's charge and underwater movement
retain their earlier source recordings and original pitch settings. The
`prerelease_spyro_audio` regression checks audible PCM and one-shot/loop types;
`spyro_audio_playback_probe <CUE> --spyro-effects` additionally exercises Windows
voices, 60 fps capture energy and loop shutdown without audible test playback.

Looping environmental sources and sounds requested by unimplemented enemy/NPC scripts still need their callers. Collision materials do not yet select all four footstep surface categories. Native attenuation uses ordinary distance rather than reproducing the complete retail approximation and Doppler path. SPU reverb, ADSR, Gaussian interpolation and the CD controller's hardware resampling filter are not emulated.

## Playback and lifetime

Windows playback uses XAudio2 with initialized COM services. Music and speech decode in cancellable background jobs, each with its own asset reader. Rendering performs no disc reads for those streams. Decoded music loops in a source voice; sound effects use their original pitch ratios and loop markers. Level changes stop old jobs and voices before replacing their buffers.

Music, speech and effects have independent volume controls. Speech lowers music/effects while it plays. Keeping music running quietly under dialogue is a native presentation choice: the original CD state machine switches its single stream between music and speech. Pausing stops source advancement; leaving a conversation cancels pending speech and destroys its voice. A missing audio device or unsupported extraction produces an Audio-panel message while the workbench remains usable.

## Conversation reconstruction

The gameplay child contains a speech table of eight-byte channel/count/sector records and 88-byte Moby records. Hunter class `0x1bc` uses a dialogue header pointing to command-prefixed text records. The initial decoder covered three Summer Forest Hunters and 19 voiced lines. The current shared command probe covers 162 speaking records across 40 levels; decoding dialogue does not implement each actor's quest.

Resident evidence: `80015be4` starts text/voice, `8003a98c` checks proximity and input, `8003a568` interprets continuation/choice commands, and `80016328` requests the talking/idle clips. Summer Forest overlay 14, `800785d8`–`80078dec`, contains Hunter's subtype-specific dispatch. Separate Ghidra overlay analysis avoids mixing code that shares the same load address.

Supported controls include first greeting, text/voice association, repeat target, immediate next line, free yes/no questions and dismissal. Ordinary movement/attacks are held during a conversation; presentation and fixed simulation continue. Line-of-sight and height checks prevent talking through walls or from another floor. Camera Hunter's replies update active/passive mode, although the complete original passive-camera algorithm is still pending.

Glimmer Moneybags now accepts the original 100-gem bridge payment, handles insufficient funds and opens the bridge through its authored terrain tracks and camera position. Actual answer labels/order, price placeholders and closing-choice flags are decoded. Other purchase/reward commands remain unsupported. [Campaign evidence](CAMPAIGN_AND_TERRAIN.md). Random repeat ranges currently use their first target. Hunter's jumping tutorial path and physics, bridge placement, challenge state, other NPC quest scripts and their flags remain separate reconstruction work; the Glimmer purchase flag persists in S2SAVE8.

## Reproducing validation

The in-game ImGui dialogue child has been replaced with the original rounded
box, speaker tab and bitmap font. Colour escapes, controller instructions,
prices and the two authored answers are retained. Enter reveals/advances text;
answers support arrows, 1/2, controller and mouse. [Retail interface](RETAIL_INTERFACE.md)
records the recovered artwork, controls, timing scope and validation.

```powershell
./tools/build.ps1
./tools/validate-audio.ps1 -Game 'D:/Games/Spyro 2/game.cue' -NoBuild
./build/Release/spyro_audio_probe.exe 'D:/Games/Spyro 2/game.cue' --slot 15 --seconds 5 --wave data/validation/music.wav
```

The normal build runs synthetic codec/container, conversation and traversal regressions alongside the existing suites. `validate-audio.ps1` fully decodes the retail music/banks and Hunter voices, then runs a muted Windows integration check for real sample consumption, charge-loop exit, pause/resume, speech cancellation and bank replacement. `-SkipPlayback` permits decoding on a machine without a playback device. These checks do not replace an audible mix review.

Native captures and timing are recorded in [VALIDATION.md](VALIDATION.md). Audio references: [PSX-SPX XA format](https://psx-spx.consoledev.net/cdromformat/), [SPU block/loop format](https://psx-spx.consoledev.net/soundprocessingunitspu/), [DuckStation SPU decoding](https://github.com/stenzek/duckstation/blob/master/src/core/spu.cpp), [FFmpeg ADPCM decoder](https://github.com/FFmpeg/FFmpeg/blob/n8.0/libavcodec/adpcm.c), and [Microsoft XAudio2 initialization](https://learn.microsoft.com/en-us/windows/win32/xaudio2/how-to--initialize-xaudio2). No third-party decoder implementation is linked into the engine.
