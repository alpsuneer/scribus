#!/bin/bash
# Fit frame to text: is the space below the last line the same as the space above the first line?
# Every path (menu Adjust Frame Height to Text, toolbar Fit, News Browser placement, Fit Caption Frame,
# Frame Border with inner space) on Malayalam / English frames 8-18 pt, auto / fixed line spacing; the
# result is exported to PDF and the gaps between the frame (box inner edge) and the INK are measured at 600 dpi.
# usage: tests/suneer/fit_gap.sh <work dir> [scribus]   (PREFS for an isolated profile; FITGAP_TEXT=clean: no
#        empty paragraphs; FITGAP_TEXT=columns: 1-4 columns + a linked chain, Malayalam only, 5 pages)
#        empty paragraphs at the end of the text, to see the fit alone)
set -u
W=${1:?work dir}; SCRIBUS=${2:-/usr/local/bin/scribus}; HERE="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$W/out"; W="$(cd "$W" && pwd)"; rm -f "$W/out"/* "$W"/*.sla; PR=(); [ -n "${PREFS:-}" ] && PR=(-pr "$PREFS")
QT_IM_MODULE= timeout 300 xvfb-run -a "$SCRIBUS" -g -ns "${PR[@]}" -py "$HERE/fixtures/fit_gap_make.py" "$W/base.sla" ${FITGAP_TEXT:-} > "$W/make.log" 2>&1
[ -s "$W/base.sla" ] || { echo "FAIL: no base.sla"; tail -5 "$W/make.log"; exit 1; }
SUNEER_FITGAP_SELFTEST="$W/out/t" SUNEER_FITGAP_SAVEAS="$W/fitted.sla" SUNEER_FIT_DEBUG="${SUNEER_FIT_DEBUG:-}" SUNEER_FIT_TRACE=1 SUNEER_FITGAP_MODE="$([ "${FITGAP_TEXT:-}" = app ] && echo actions)" QT_IM_MODULE= timeout 300 \
	xvfb-run -a -s '-screen 0 1600x1000x24' "$SCRIBUS" "${PR[@]}" "$W/base.sla" > "$W/out/t.log" 2>&1
[ -s "$W/out/t-cases.txt" ] || { echo "FAIL: no cases"; tail -20 "$W/out/t.log"; exit 1; }
cat > "$W/export.py" <<PY
import scribus
scribus.openDoc("$W/fitted.sla")
pdf = scribus.PDFfile(); pdf.file = "$W/out/fitted.pdf"; pdf.save()
PY
rm -f "$W/.fitted.sla.lock"
QT_IM_MODULE= timeout 300 xvfb-run -a "$SCRIBUS" -g -ns "${PR[@]}" -py "$W/export.py" > "$W/out/export.log" 2>&1
pdftoppm -r 600 -png "$W/out/fitted.pdf" "$W/out/p" 2>/dev/null
pdftoppm -r 100 -png "$W/out/fitted.pdf" "$W/out/view" 2>/dev/null
[ "${FITGAP_TEXT:-}" = app ] && grep "FIT-TRACE" "$W/out/t.log"
cat "$W/out/t-checks.txt" 2>/dev/null || [ "${FITGAP_TEXT:-}" = app ] || echo "FAIL no paste checks"
python3 "$HERE/fixtures/fit_gap_measure.py" "$W"
