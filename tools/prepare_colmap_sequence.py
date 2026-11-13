"""Pack calibrated overlapping frames for one coordinated image-generation edit.

Packing/splitting is lossless asset preparation, not synthetic image generation.
Keep all original renders, generated sheets and cameras for reproduction.
"""
from pathlib import Path
import json, sys
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'data/validation/photogrammetry-20260927'
SEQ = OUT / 'sequence6'
SEQ.mkdir(exist_ok=True)
indices = [0, 10, 12, 14, 16, 18]
all_cameras = json.loads((OUT / 'control/cameras.json').read_text())
cameras = [all_cameras[i] for i in indices]
sheet = Image.new('RGB', (2880, 1440))
for i, camera in enumerate(cameras):
    src = OUT / 'control/images' / camera['image']
    frame = Image.open(src).convert('RGB')
    sheet.paste(frame, ((i % 3) * 960, (i // 3) * 720))
    camera['source_control_image'] = camera['image']
    camera['image'] = f'{i + 1:02d}.png'
    camera['sequence_frame'] = i + 1
sheet.save(SEQ / 'calibrated-reference-sheet.png')
(SEQ / 'cameras.json').write_text(json.dumps(cameras, indent=2))

if len(sys.argv) > 1:
    source = Path(sys.argv[1]).resolve()
    generated = Image.open(source).convert('RGB')
    w, h = generated.size
    if w % 3 or h % 2 or abs((w / 3) / (h / 2) - 4 / 3) > 0.005:
        raise ValueError(f'Expected exact 3 x 2 sheet of 4:3 frames; got {w} x {h}')
    images = SEQ / 'generated-frames'
    images.mkdir(exist_ok=True)
    for i in range(6):
        x, y = (i % 3) * (w // 3), (i // 3) * (h // 2)
        generated.crop((x, y, x + w // 3, y + h // 2)).save(images / f'{i + 1:02d}.png')
    (SEQ / 'generation-source.json').write_text(json.dumps({
        'source': str(source), 'dimensions': [w, h], 'columns': 3, 'rows': 2,
        'order': 'row-major', 'frame_dimensions': [w // 3, h // 2],
        'processing': 'unmodified rectangular crops; no view synthesis or color correction'
    }, indent=2))
print(SEQ / 'calibrated-reference-sheet.png')
