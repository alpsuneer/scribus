#!/bin/bash
# Page Models bulk import test: old pages (.sla) are read by tools/autolayout/page_models.py through the
# import dialog, every page's suggested group is listed, the pages are saved as models in a throw-away profile.
#
#   tests/suneer/page_models_import.sh <folder or file.sla> [more files...]
#
# The installed Scribus starts on a private X display (xvfb-run) with a throw-away HOME / profile on a
# blank document (tests/suneer/fixtures or the first file itself) and SUNEER_PAGEMODEL_IMPORT_REPORT set;
# SuneerNewsPanel::runPageModelImportTest() runs the dialog unattended. Exit 0 = RESULT PASS.
set -u
[ $# -ge 1 ] || { sed -n 2,9p "$0"; exit 2; }
SCRIBUS=${SCRIBUS:-/usr/local/bin/scribus}
work=$(mktemp -d /tmp/page-models-import.XXXXXX); mkdir -p "$work/home" "$work/prefs"
list=$(IFS=';'; echo "$*")
first=$1; [ -d "$first" ] && first=$(ls "$first"/*.sla | head -1)
cp "$first" "$work/doc.sla"
env HOME=$work/home QT_IM_MODULE= SUNEER_PAGEMODEL_IMPORT_REPORT=$work/result.txt SUNEER_PAGEMODEL_IMPORT="$list" \
	timeout 900 xvfb-run -a -s "-screen 0 1600x1000x24" "$SCRIBUS" -ns -pr "$work/prefs" "$work/doc.sla" > "$work/scribus.log" 2>&1
rc=$?
[ -f "$work/result.txt" ] && sed 's/^/   /' "$work/result.txt"
echo "models: $work/prefs/layout-models/pages  log: $work/scribus.log"
grep -q "^RESULT PASS" "$work/result.txt" 2>/dev/null && [ $rc -eq 0 ] && { echo PASSED; exit 0; }
echo "FAILED (exit $rc)"; exit 1
