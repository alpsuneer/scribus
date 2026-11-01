#!/bin/bash
# The "Text colour" popup (suneertextcolour.cpp) on the gradient engine:
#   1. builds an A3 page with four 80 pt Malayalam headlines (fixtures/text_gradient_make.py),
#   2. SUNEER_TEXTCOLOUR_SELFTEST drives the widget itself: preset thumbnails "Gradient" /
#      "Red headline" / "Fire", direction buttons, Apply to, a stop colour edit (own copy),
#      undo / redo, Remove gradient, the preset store (save / rename / delete / built-in hide),
#      grabs the popup (plain, More..., Solid) and the canvas, saves grad.sla,
#   3. checks the saved SLA: Malayalam, the three gradients, the own copy, CMYK colours.
# usage: tests/suneer/text_colour_ui.sh <work dir> [scribus]   (HOME / PREFS for an isolated profile)
set -u
W=${1:?work dir}; SCRIBUS=${2:-/usr/local/bin/scribus}; HERE="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$W/out"; PR=(); [ -n "${PREFS:-}" ] && PR=(-pr "$PREFS")
sed "s#/home/s1/tmp-textgrad#$W#g" "$HERE/fixtures/text_gradient_make.py" > "$W/make.py"
if [ ! -s "$W/base.sla" ]; then
  timeout 300 xvfb-run -a "$SCRIBUS" -g -ns "${PR[@]}" -py "$W/make.py" > "$W/make.log" 2>&1
fi
[ -s "$W/base.sla" ] || { echo "FAIL: no base.sla"; exit 1; }
rm -f "$W/.base.sla.lock"
SUNEER_TEXTCOLOUR_SELFTEST="$W/out/t" SUNEER_TEXTCOLOUR_SAVEAS="$W/grad.sla" QT_IM_MODULE= timeout 300 \
  xvfb-run -a -s '-screen 0 1600x1000x24' "$SCRIBUS" "${PR[@]}" "$W/base.sla" > "$W/out/t.log" 2>&1
echo "scribus exit $?"
cat "$W/out/t-report.txt" 2>/dev/null || { echo "FAIL: no report"; exit 1; }
python3 - "$W" <<'PY'
import sys,re
W=sys.argv[1]; fails=0
def check(what,ok):
    global fails
    print(("PASS " if ok else "FAIL ")+what); fails+=0 if ok else 1
try:
    sla=open(W+'/grad.sla',encoding='utf-8').read()
except FileNotFoundError:
    print('FAIL grad.sla missing'); sys.exit(1)
ml=sum(0xD00<=ord(c)<=0xD7F for c in sla)
check('Malayalam in grad.sla (%d codepoints)'%ml, ml>50)
check('GradLine uses preset "Gradient" at 45 deg per line', 'TextFillGradient="Gradient"' in sla and 'TextFillGradientAngle="45"' in sla)
check('GradFrame uses "Red headline" per frame', 'TextFillGradient="Red headline"' in sla and 'TextFillGradientMapping="1"' in sla)
m=re.search(r'TextFillGradient="(Text gradient \d+)"',sla)
check('GradRadial has its own copy (%s), radial'%(m.group(1) if m else '-'), bool(m) and 'TextFillGradientType="1"' in sla)
check('own copy saved as a document gradient', bool(m) and ('<Gradient Name="%s"'%m.group(1)) in sla)
check('preset gradients saved', '<Gradient Name="Gradient"' in sla and '<Gradient Name="Fire"' in sla)
check('preset colours are CMYK', re.search(r"<Color Name=\"Grad Pink\" Space=\"CMYK\"",sla) is not None)
r=open(W+'/out/t-report.txt').read()
check('selftest ALL PASS', 'ALL PASS' in r)
print('ALL PASS' if fails==0 else 'FAILED: %d'%fails); sys.exit(1 if fails else 0)
PY
