#!/bin/bash
# Layout model regression test: every layout model is applied to each page and undone.
#
#   tests/suneer/layout_models_apply.sh [--models DIR] page.sla [page2.sla ...]
#
# For each .sla (a copy; the original is only read) the installed Scribus starts on a
# private X display with a throw-away HOME and profile, opens the copy, and with
# SUNEER_LAYOUT_SELFTEST set runs SuneerNewsPanel::runLayoutSelfTest(): for every
# model the Layout Gallery card (scratch copy), Apply on the real page (compared with
# the card), Undo (the page must be exactly as before). Without --models the profile
# has the built-in models only. Exit status 0 = every model on every page passed;
# a crash, a hang (timeout) or any FAIL line fails the run.
set -u
MODELS=""
if [ "${1:-}" = "--models" ]; then MODELS=$2; shift 2; fi
[ $# -ge 1 ] || { sed -n 2,13p "$0"; exit 2; }
SCRIBUS=${SCRIBUS:-/usr/local/bin/scribus}
work=$(mktemp -d /tmp/layout-apply.XXXXXX)
status=0
for sla in "$@"; do
	name=$(basename "$sla" .sla)
	run=$work/$name; mkdir -p "$run/home" "$run/prefs"
	if [ -n "$MODELS" ]; then mkdir -p "$run/prefs/layout-models"; cp "$MODELS"/*.json "$run/prefs/layout-models/" 2>/dev/null; fi
	cp "$sla" "$run/$name.sla"
	[ -d "${sla%.sla}_images" ] && cp -r "${sla%.sla}_images" "$run/"
	echo "== $sla"
	env HOME=$run/home QT_IM_MODULE= ${EXTRA_ENV:-} SUNEER_LAYOUT_SELFTEST=$run/result.txt SUNEER_LAYOUT_DEBUG=1 \
		timeout 900 xvfb-run -a -s "-screen 0 1600x1000x24" "$SCRIBUS" -ns -pr "$run/prefs" "$run/$name.sla" > "$run/scribus.log" 2>&1
	rc=$?
	if [ -f "$run/result.txt" ]; then grep -v "^    undo:" "$run/result.txt" | sed 's/^/   /'; fi
	if [ $rc -ne 0 ] || ! grep -q "^RESULT PASS" "$run/result.txt" 2>/dev/null; then
		echo "   FAILED (exit $rc$( [ $rc -eq 124 ] && echo ', timeout')$(grep -q 'Scribus crashed' "$run/scribus.log" && echo ', CRASH')) - log: $run/scribus.log"
		status=1
	fi
done
echo "work folder: $work"
[ $status -eq 0 ] && echo "ALL PASSED" || echo "SOME FAILED"
exit $status
