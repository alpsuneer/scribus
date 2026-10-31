# tests/suneer/fit_gap.sh: space between the frame (or, with a border, the box's inner edge) and the ink
# above the first line and below the last one, per frame, from the 600 dpi render of the exported PDF.
import sys, glob, os
from PIL import Image
Image.MAX_IMAGE_PIXELS = None
W = sys.argv[1]
TOL = float(os.environ.get("FITGAP_TOL", "0.2"))
dpi = 600; px_mm = dpi / 25.4
pages = sorted(glob.glob(W + "/out/p-*.png"))
imgs = {}
fails = 0; rows = []
sla = open(W + "/fitted.sla", encoding="utf-8").read()
print("Malayalam codepoints in fitted.sla: %d" % sum(0xD00 <= ord(c) <= 0xD7F for c in sla))
for line in open(W + "/out/t-cases.txt"):
    p = line.split()
    if len(p) < 10:
        continue
    path, name, pg = p[0], p[1], int(p[2])
    x, y, w, h, over, lw, dtop = map(float, p[3:10])
    if pg not in imgs:
        imgs[pg] = Image.open(pages[pg - 1]).convert("L")
    im = imgs[pg]; px = im.load()
    # the box's inner edge: half the stroke inside the frame edge
    inner = lw / 2.0 if lw > 0 else 0.0
    x0, x1 = int((x + inner) * px_mm) + 2, int((x + w - inner) * px_mm) - 2
    y0, y1 = (y + inner) * px_mm, (y + h - inner) * px_mm
    # with a border the stroke itself is ink: look only well inside it
    pad = int((lw * px_mm) + 3) if lw > 0 else 0
    top = bottom = None
    for yy in range(int(y0) + pad, int(y1) - pad):
        if any(px[xx, yy] < 128 for xx in range(x0 + pad, x1 - pad, 2)):
            if top is None:
                top = yy
            bottom = yy
    if top is None:
        print("FAIL %-8s %-26s no ink found" % (path, name)); fails += 1; continue
    gt = (top - y0) / px_mm
    gb = (y1 - (bottom + 1)) / px_mm
    d = gb - gt
    ok = abs(d) <= TOL and not over
    fails += 0 if ok else 1
    rows.append((path, name, gt, gb, d, over))
    print("%s %-8s %-26s top %5.2f mm  bottom %5.2f mm  bottom-top %+5.2f mm  overflow %d" % ("PASS" if ok else "FAIL", path, name, gt, gb, d, over))
by = {}
for r in rows:
    by.setdefault(r[0], []).append(r[4])
for k, v in by.items():
    print("SUMMARY %-9s bottom-top: min %+5.2f  max %+5.2f  mean %+5.2f mm (%d frames)" % (k, min(v), max(v), sum(v) / len(v), len(v)))
try:
    fails += sum(1 for l in open(W + "/out/t-checks.txt") if l.startswith("FAIL"))
except OSError:
    fails += 1
print("ALL PASS" if fails == 0 else "FAILED: %d" % fails)
