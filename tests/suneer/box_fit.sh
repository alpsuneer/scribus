#!/bin/bash
# Frame Border box fitted to the text (suneerboxfit.cpp): single frames 8-12 pt, auto / fixed line
# spacing, left / justified, too tall with empty paragraphs at the end; a welded, a linked and a grouped
# story (one story box each); text edited -> fitted again on leaving edit mode, one undo step.
# Then the saved file is exported to PDF and the ink-to-box spaces of every box are measured (600 dpi).
# usage: tests/suneer/box_fit.sh <work dir> [scribus]   (PREFS for an isolated profile)
set -u
W=${1:?work dir}; SCRIBUS=${2:-/usr/local/bin/scribus}; HERE="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$W/out"; W="$(cd "$W" && pwd)"; PR=(); [ -n "${PREFS:-}" ] && PR=(-pr "$PREFS")
sed "s#/tmp/box_fit_base.sla#$W/base.sla#" "$HERE/fixtures/box_fit_make.py" > "$W/make.py"
QT_IM_MODULE= timeout 300 xvfb-run -a "$SCRIBUS" -g -ns "${PR[@]}" -py "$W/make.py" > "$W/make.log" 2>&1
[ -s "$W/base.sla" ] || { echo "FAIL: no base.sla"; tail -5 "$W/make.log"; exit 1; }
rm -f "$W/.base.sla.lock"
SUNEER_BOXFIT_SELFTEST="$W/out/t" SUNEER_BOXFIT_SAVEAS="$W/boxed.sla" QT_IM_MODULE= timeout 300 \
	xvfb-run -a -s '-screen 0 1600x1000x24' "$SCRIBUS" "${PR[@]}" "$W/base.sla" > "$W/out/t.log" 2>&1
cat "$W/out/t-report.txt" 2>/dev/null || { echo "FAIL: no self-test report"; tail -20 "$W/out/t.log"; }
cat > "$W/export.py" <<PY
import scribus
scribus.openDoc("$W/boxed.sla")
pdf = scribus.PDFfile(); pdf.file = "$W/out/boxed.pdf"; pdf.save()
PY
rm -f "$W/.boxed.sla.lock"
QT_IM_MODULE= timeout 300 xvfb-run -a "$SCRIBUS" -g -ns "${PR[@]}" -py "$W/export.py" > "$W/out/export.log" 2>&1
pdftoppm -r 600 -png -singlefile "$W/out/boxed.pdf" "$W/out/boxed" 2>/dev/null
pdftoppm -r 110 -png -singlefile "$W/out/boxed.pdf" "$W/out/boxed-page" 2>/dev/null
python3 - "$W" <<'PY'
import sys, re
from PIL import Image
Image.MAX_IMAGE_PIXELS = None
W = sys.argv[1]; fails = 0
def check(what, ok):
    global fails
    print(("PASS " if ok else "FAIL ") + what); fails += 0 if ok else 1
sla = open(W + "/boxed.sla", encoding="utf-8").read()
ml = sum(0xD00 <= ord(c) <= 0xD7F for c in sla)
check("Malayalam in boxed.sla (%d codepoints)" % ml, ml > 500)
im = Image.open(W + "/out/boxed.png").convert("L"); px = im.load(); dpi = 600; mm = dpi / 25.4
half = 0.5 * 25.4 / 72 / 2   # half the 0.5 pt stroke: the box's inner edge is that far inside
rows = []
for line in open(W + "/out/t-cases.txt"):
    if not line.strip():
        continue
    name, x, y, w, h = line.split(); x, y, w, h = map(float, (x, y, w, h))
    X0, Y0, X1, Y1 = int((x + 0.25) * mm), int((y + 0.25) * mm), int((x + w - 0.25) * mm), int((y + h - 0.25) * mm)
    xs = []; ys = []
    for yy in range(Y0, Y1):
        for xx in range(X0, X1):
            if px[xx, yy] < 128:
                xs.append(xx); ys.append(yy)
    if not xs:
        check("%s: ink found" % name, False); continue
    top = min(ys) / mm - y - half; bot = y + h - half - (max(ys) + 1) / mm
    left = min(xs) / mm - x - half; right = x + w - half - (max(xs) + 1) / mm
    rows.append((name, h, top, bot, left, right))
print("%-24s %7s %6s %6s %6s %6s   (mm: ink to the inner edge of the box)" % ("box", "height", "top", "bottom", "left", "right"))
for name, h, top, bot, left, right in rows:
    print("%-24s %7.2f %6.2f %6.2f %6.2f %6.2f" % (name, h, top, bot, left, right))
open(W + "/out/gaps.txt", "w").write("\n".join("%s %.3f %.3f %.3f %.3f %.3f" % r for r in rows) + "\n")
for name, h, top, bot, left, right in rows:
    check("%s: top %.2f = bottom %.2f (within 0.3 mm)" % (name, top, bot), abs(top - bot) <= 0.3)
    check("%s: left %.2f near the inner space (1.6-2.3 mm)" % (name, left), 1.6 <= left <= 2.3)
    if "justify" in name or name.startswith("STORY"):
        check("%s: right %.2f, text not touching the border (>= 1.5 mm)" % (name, right), right >= 1.5)
print("ALL PASS" if fails == 0 else "FAILED: %d" % fails)
sys.exit(1 if fails else 0)
PY
