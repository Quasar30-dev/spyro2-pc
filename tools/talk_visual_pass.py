"""Visual pass of scripted conversations across the campaign (current build).

For every level with speakers in the quest census (spyro_script_quests
output), Spyro walks up to one speaker from about 7 units away and presses
triangle (workbench --attack approach). The conversation runs on the original
level logic. The frame with the dialogue open is kept, along with the report's
conversation counters. One labelled frame per speaker goes into the sheet.

    python tools/talk_visual_pass.py data/validation/allpass-20261015/quests.txt OUT_DIR [slot ...]
"""
import json, re, subprocess, sys
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'build/Release/spyro_workbench.exe'
NAMES = {15: 'Summer Forest', 17: 'Glimmer', 19: 'Idol Springs', 21: 'Colossus', 23: 'Hurricos', 25: 'Aquaria Towers',
         27: 'Sunny Beach', 29: 'Ocean Speedway', 31: "Crush's Dungeon", 33: 'Autumn Plains', 35: 'Skelos Badlands',
         37: 'Crystal Glacier', 39: 'Breeze Harbor', 41: 'Zephyr', 43: 'Metro Speedway', 45: 'Scorch', 47: 'Shady Oasis',
         49: 'Magma Cone', 51: 'Fracture Hills', 53: 'Icy Speedway', 55: "Gulp's Overlook", 57: 'Winter Tundra',
         59: 'Mystic Marsh', 61: 'Cloud Temples', 63: 'Canyon Speedway', 65: 'Robotica Farms', 67: 'Metropolis',
         69: 'Dragon Shores', 71: "Ripto's Arena"}


def speakers(census):
    slot, out = None, {}
    for line in Path(census).read_text().splitlines():
        m = re.match(r'slot (\d+)', line)
        if m:
            slot = int(m.group(1))
            continue
        m = re.match(r"\s+talk\s+object (\d+) type (\w+) '([^']*)'(?: mode \d+)? lines (\d+)", line)
        if m and slot is not None and int(m.group(4)) > 0:
            out.setdefault(slot, []).append((int(m.group(1)), m.group(3)))
    # Per level, quest givers before checkpoint fairies (the first that talks is filmed).
    return {s: sorted(v, key=lambda p: p[1] == 'Zoe') for s, v in out.items()}


def film(slot, record, still):
    """Walk up from ~7 units where there is room. The approach spot may be a
    lower floor (a speaker up on a ledge or walkway) or against a wall; then
    stand beside the speaker, on its own floor, instead."""
    for mode in ('approach', 'talk'):
        cmd = [str(EXE), '--game', 'data/disc', '--slot', str(slot), '--interaction-record', str(record), '--attack', mode,
               '--smoke', '260', '--capture-size', '640', '360', '--capture-music-volume', '0', '--capture', str(still)]
        ran = subprocess.run(cmd, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=600).returncode == 0
        report = json.loads(still.with_suffix('.json').read_text()) if ran and still.with_suffix('.json').exists() else {}
        talked = report.get('npc_conversations', 0) + report.get('retail_script_dialogues', 0)
        if talked:
            break
    return mode, report, talked


def main(census, out_dir, only=None):
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    previous = json.loads((out / 'talk-pass.json').read_text()) if only and (out / 'talk-pass.json').exists() else []
    rows = [r for r in previous if r['slot'] not in only] if only else []
    for slot, candidates in sorted(speakers(census).items()):
        if only and slot not in only:
            continue
        for record, name in candidates:
            still = out / f'{slot}-{record}.bmp'
            mode, report, talked = film(slot, record, still)
            if talked:
                break
        faults = report.get('retail_script_faults', 0)
        if still.exists():
            Image.open(still).save(still.with_suffix('.png'))
            still.unlink()
        rows.append({'slot': slot, 'level': NAMES.get(slot, str(slot)), 'record': record, 'speaker': name, 'mode': mode,
                     'conversations': talked, 'dialogue_open': report.get('npc_dialogue_active', False), 'faults': faults})
        print(json.dumps(rows[-1]), flush=True)
    rows.sort(key=lambda r: r['slot'])
    (out / 'talk-pass.json').write_text(json.dumps(rows, indent=1))
    cols = 5
    sheet = Image.new('RGB', (cols * 384, ((len(rows) + cols - 1) // cols) * 216), 'black')
    draw = ImageDraw.Draw(sheet)
    for i, r in enumerate(rows):
        png = out / f'{r["slot"]}-{r["record"]}.png'
        if png.exists():
            sheet.paste(Image.open(png).resize((384, 216)), ((i % cols) * 384, (i // cols) * 216))
        x, y = (i % cols) * 384, (i // cols) * 216
        draw.rectangle((x, y, x + 383, y + 15), fill=(0, 0, 0))
        draw.text((x + 4, y + 2), f'{r["level"]} {r["speaker"]} ({r["record"]}): talked {r["conversations"]}', fill=(255, 230, 120))
    sheet.save(out / 'talk-sheet.png')
    print('sheet', len(rows), 'levels; all talked:', all(r['conversations'] > 0 for r in rows), '; faults:', sum(r['faults'] for r in rows))


if __name__ == '__main__':
    # Optional trailing slots re-film just those levels into an existing pass.
    main(sys.argv[1], sys.argv[2], {int(s) for s in sys.argv[3:]} or None)
