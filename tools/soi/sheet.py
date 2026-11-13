"""Contact sheet of PNG shots: python tools/soi/sheet.py OUT.png COLS W H shot1.png shot2.png ... (labels = file stems)."""
import sys
from PIL import Image, ImageDraw

out, cols, w, h = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
files = sys.argv[5:]
rows = (len(files) + cols - 1) // cols
W = Image.new("RGB", (cols * w, rows * h), (20, 20, 20))
dr = ImageDraw.Draw(W)
for k, f in enumerate(files):
    im = Image.open(f).convert("RGB").resize((w, h))
    x, y = (k % cols) * w, (k // cols) * h
    W.paste(im, (x, y))
    lab = f.replace("\\", "/").split("/")[-1].rsplit(".", 1)[0]
    dr.rectangle([x, y, x + 7 * len(lab) + 6, y + 14], fill=(0, 0, 0))
    dr.text((x + 3, y + 1), lab, fill=(255, 255, 0))
W.save(out)
