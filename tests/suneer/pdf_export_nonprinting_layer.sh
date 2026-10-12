#!/bin/bash
# Regression test: PDF export of a page whose content sits on a layer with the
# "print" flag off (fixtures/chara1.sla, layer "Advt", 105 objects).
#   1. Exported as-is, the file must still come out within the timeout (no
#      hang, no crash) and be a readable one-page PDF — it is blank by design.
#   2. With every layer made printable, the same export must carry the page's
#      images and text.
# The GUI warning (ScribusMainWindow::suneerNonPrintingLayersCheck) is not
# exercised here: scripts never see modal dialogs.
# Usage: tests/suneer/pdf_export_nonprinting_layer.sh [source.sla] [scribus binary]
# Needs: xvfb-run, python3, pdfinfo, pdfimages, pdftotext. Exit 0 = pass.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="${1:-$HERE/fixtures/chara1.sla}"; SCRIBUS="${2:-/usr/local/bin/scribus}"
TIMEOUT=120
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
cp "$SRC" "$WORK/in.sla"
cat > "$WORK/export.py" <<PY
import scribus, sys
scribus.openDoc("$WORK/in.sla")
if "@MODE@" == "printable":
    for name in scribus.getLayers():
        scribus.setLayerPrintable(name, True)
pdf = scribus.PDFfile()
pdf.file = "$WORK/out_@MODE@.pdf"
pdf.save()
print("EXPORT DONE", file=sys.stderr)
PY
fail=0
for mode in asis printable; do
    sed "s/@MODE@/$mode/g" "$WORK/export.py" > "$WORK/export_$mode.py"
    start=$(date +%s)
    ( cd "$WORK" && timeout "$TIMEOUT" xvfb-run -a "$SCRIBUS" -g -ns -py "$WORK/export_$mode.py" >"$WORK/run_$mode.log" 2>&1 )
    rc=$?; secs=$(( $(date +%s) - start ))
    out="$WORK/out_$mode.pdf"
    if [ $rc -eq 124 ]; then echo "FAIL ($mode): export did not finish within ${TIMEOUT}s"; fail=1; continue; fi
    grep -q "EXPORT DONE" "$WORK/run_$mode.log" || { echo "FAIL ($mode): script did not finish (exit $rc)"; grep -v "^qt\.\|DBus\|^Qt:" "$WORK/run_$mode.log" | tail -5; fail=1; continue; }
    pages=$(pdfinfo "$out" 2>/dev/null | awk '/^Pages:/{print $2}')
    images=$(pdfimages -list "$out" 2>/dev/null | tail -n +3 | wc -l)
    chars=$(pdftotext "$out" - 2>/dev/null | tr -d '[:space:]' | wc -c)
    echo "$mode: ${secs}s, pages=$pages images=$images text_chars=$chars size=$(stat -c %s "$out" 2>/dev/null)"
    [ "$pages" = "1" ] || { echo "FAIL ($mode): expected 1 page"; fail=1; }
    if [ "$mode" = printable ]; then
        [ "$images" -ge 30 ] || { echo "FAIL (printable): expected the page's images in the PDF"; fail=1; }
        [ "$chars" -ge 1000 ] || { echo "FAIL (printable): expected the page's text in the PDF"; fail=1; }
    fi
done
echo "RESULT: $([ $fail = 0 ] && echo PASS || echo FAIL)"; exit $fail
