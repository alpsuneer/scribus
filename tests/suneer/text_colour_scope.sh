#!/bin/bash
# The Text colour popup follows the selection (suneertextcolour.cpp, runScopeTestIfAsked).
# Builds a page in Malayalam: Story (headline + body in one frame), Multi1, Multi2, table Cells; the self-test
# drives the widget's own buttons in real edit / table edit mode, reads the character styles, grabs pictures.
# usage: tests/suneer/text_colour_scope.sh <work dir> [scribus]
set -u
W=${1:?work dir}; SCRIBUS=${2:-/usr/local/bin/scribus}
mkdir -p "$W/out" "$W/home" "$W/prefs"; W=$(cd "$W" && pwd); rm -f "$W"/out/* "$W/base.sla"
cat > "$W/make.py" <<PY
import scribus
scribus.newDocument(scribus.PAPER_A4, (15, 15, 15, 15), scribus.PORTRAIT, 1, scribus.UNIT_MILLIMETERS, scribus.NOFACINGPAGES, scribus.FIRSTPAGERIGHT, 1)
fonts = scribus.getFontNames()
font = next((f for f in ["DZ DB Display 5 Bold", "DZ DB Display 1 Roman"] if f in fonts), None)
def frame(name, x, y, w, h, text, size):
    f = scribus.createText(x, y, w, h, name)
    scribus.setText(text, f)
    if font: scribus.setFont(font, f)
    scribus.setFontSize(size, f)
    scribus.setLineSpacing(size * 1.15, f)
    scribus.setTextColor("Black", f)
frame("Story", 15, 15, 180, 100, "മിസ്റ്റർ ട്രംപ് ഇന്ത്യയിലേക്ക്\rപ്രധാനമന്ത്രിയുമായി കൂടിക്കാഴ്ച നടത്തുമെന്ന് വിദേശകാര്യ മന്ത്രാലയം അറിയിച്ചു. വ്യാപാര കരാർ ചർച്ചയാകും.", 22)
frame("Multi1", 15, 125, 85, 40, "ഒന്നാം ഫ്രെയിം വാർത്ത", 22)
frame("Multi2", 110, 125, 85, 40, "രണ്ടാം ഫ്രെയിം വാർത്ത", 22)
t = scribus.createTable(15, 175, 180, 50, 2, 2, "Cells")
for r, c, x in [(0, 0, "കേരളം"), (0, 1, "തമിഴ്നാട്"), (1, 0, "കർണാടകം"), (1, 1, "ഗോവ")]:
    scribus.setCellText(r, c, x, t)
scribus.saveDocAs("$W/base.sla")
PY
ENVV=(HOME="$W/home" XDG_CONFIG_HOME="$W/home/.config" QT_IM_MODULE=)
env "${ENVV[@]}" timeout 120 xvfb-run -a "$SCRIBUS" -g -ns -pr "$W/prefs" -py "$W/make.py" > "$W/make.log" 2>&1
[ -s "$W/base.sla" ] || { echo "FAIL no base.sla"; tail -5 "$W/make.log"; exit 1; }
rm -f "$W/.base.sla.lock"
env "${ENVV[@]}" SUNEER_TEXTCOLOUR_SCOPETEST="$W/out/s" timeout 180 xvfb-run -a -s '-screen 0 1600x1000x24' "$SCRIBUS" -ns -pr "$W/prefs" "$W/base.sla" > "$W/out/s.log" 2>&1
echo "scribus exit $?"
cat "$W/out/s-report.txt" 2>/dev/null || { echo "FAIL no report"; tail -5 "$W/out/s.log"; exit 1; }
