#!/bin/bash
# Embed All regression test: every picture goes into the .sla, and the .sla alone shows them all.
#
#   tests/suneer/embed_all_images.sh page.sla
#
# The installed Scribus runs on a private X display with a throw-away HOME and profile;
# with SUNEER_EMBED_SELFTEST set, ScribusMainWindow::suneerEmbedSelfTest() does the work:
#   1. ref     - open page.sla as it is (linked), list every image frame, render page(s) to PNG
#   2. embed   - Embed All (News Browser / _photos files included), one Undo + Redo check,
#                Save As <work>/embedded/<name>.sla
#   3. moved   - copy ONLY that .sla to <work>/moved/ (no _photos folder), open it, list,
#                render: no frame may be linked, everything not missing must show, and the
#                render must match step 1 pixel for pixel (transparency, feather)
#   4. extract - Unembed / Extract on a copy in <work>/extract/: files written to
#                <name>_photos/edited/, frames linked to them; open again and render
# page.sla is only read (Save As goes to the work folder). Exit 0 = all passed.
set -u
[ $# -eq 1 ] || { sed -n 2,17p "$0"; exit 2; }
SCRIBUS=${SCRIBUS:-/usr/local/bin/scribus}
sla=$(readlink -f "$1"); name=$(basename "$sla" .sla)
work=$(mktemp -d /tmp/embed-all.XXXXXX); chmod 755 "$work"
lock="$(dirname "$sla")/.$(basename "$sla").lock"; hadlock=0; [ -e "$lock" ] && hadlock=1
status=0
run() {   # run <step> <mode> <sla> [VAR=value ...]
	local step=$1 mode=$2 doc=$3; shift 3
	mkdir -p "$work/$step/home" "$work/$step/prefs"
	env HOME="$work/$step/home" QT_IM_MODULE= SUNEER_EMBED_SELFTEST="$work/$step.txt" SUNEER_EMBED_MODE=$mode "$@" \
		timeout 600 xvfb-run -a -s "-screen 0 1600x1000x24" "$SCRIBUS" -ns -pr "$work/$step/prefs" "$doc" > "$work/$step.log" 2>&1
	local rc=$?
	echo "== $step ($mode) exit $rc"
	[ -f "$work/$step.txt" ] && grep -E "^(COUNT|EMBED|EXTRACT|PASS|FAIL|RESULT|  SKIP|PNG)" "$work/$step.txt" | sed 's/^/   /'
	if [ $rc -ne 0 ] || ! grep -q "^RESULT PASS" "$work/$step.txt" 2>/dev/null; then echo "   FAILED - log: $work/$step.log"; status=1; fi
}
count() { grep "^COUNT $2:" "$work/$1.txt" | sed -E "s/.* $3 ([0-9]+).*/\1/"; }
compare() {   # compare <a.png> <b.png>
	python3 - "$1" "$2" <<'PY'
import sys
from PIL import Image, ImageChops
a, b = (Image.open(p).convert("RGBA") for p in sys.argv[1:3])
if a.size != b.size:
    print("   FAIL render size %s vs %s" % (a.size, b.size)); sys.exit(1)
d = ImageChops.difference(a, b).convert("L")
hist = d.histogram(); n = sum(hist[3:])
print("   %s render vs ref: %d of %d pixels differ by more than 2 levels (max %d)" % ("PASS" if n == 0 else "FAIL", n, a.size[0] * a.size[1], max(i for i, c in enumerate(hist) if c)))
sys.exit(0 if n == 0 else 1)
PY
}

run ref check "$sla" SUNEER_EMBED_PNG="$work/ref.png"
mkdir -p "$work/embedded"
run embed embed "$sla" SUNEER_EMBED_SAVEAS="$work/embedded/$name.sla"
[ $hadlock -eq 0 ] && rm -f "$lock"
mkdir -p "$work/moved"; cp "$work/embedded/$name.sla" "$work/moved/"
run moved check "$work/moved/$name.sla" SUNEER_EMBED_PNG="$work/moved.png"
missing=$(grep -c "^  SKIP" "$work/embed.txt")
linked=$(count moved before linked); showing=$(count moved before showing); total=$(grep -c "^  " <(sed -n '/^== before/,/^COUNT before/p' "$work/moved.txt"))
if [ "$linked" = "0" ]; then echo "   PASS moved copy: no frame is linked"; else echo "   FAIL moved copy: $linked frame(s) still linked"; status=1; fi
if [ "$showing" = "$((total - missing))" ]; then echo "   PASS moved copy: $showing of $total frames show a picture ($missing skipped as missing)"; else echo "   FAIL moved copy: $showing of $total show, $missing skipped"; status=1; fi
for ref in "$work"/ref*.png; do compare "$ref" "${ref/ref/moved}" || status=1; done
mkdir -p "$work/extract"; cp "$work/moved/$name.sla" "$work/extract/"
run extract extract "$work/extract/$name.sla" SUNEER_EMBED_SAVEAS="$work/extract/$name.sla"
run extracted check "$work/extract/$name.sla" SUNEER_EMBED_PNG="$work/extracted.png"
echo "   files in $name""_photos/edited: $(find "$work/extract/${name}_photos/edited" -type f 2>/dev/null | wc -l)"
for ref in "$work"/ref*.png; do compare "$ref" "${ref/ref/extracted}" || status=1; done
echo "work folder: $work"
[ $status -eq 0 ] && echo "ALL PASSED" || echo "SOME FAILED"
exit $status
