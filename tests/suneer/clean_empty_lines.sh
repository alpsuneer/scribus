#!/bin/bash
# "Clean Empty Lines & Fit Frames" regression test.
#
#   tests/suneer/clean_empty_lines.sh page.sla [page2.sla ...]
#   SELECT=1 tests/suneer/clean_empty_lines.sh page.sla   (every text frame selected: the "selected frames" path)
#
# For each .sla (a copy; the original is only read) the installed Scribus starts on a private
# X display with a throw-away HOME and profile, opens the copy, and with SUNEER_CLEAN_SELFTEST set
# runs ScribusMainWindow::suneerCleanSelfTest() on every page: the News Browser text frames before
# and after the command - no blank line / blank end left, no new overflow, every non-blank
# character kept, soft hyphens and single-column line breaks unchanged, no frame taller -
# then one Undo (text and heights exactly as before) and Redo. The cleaned copy is saved as
# <work>/<name>/cleaned.sla.
# Exit status 0 = every check on every page passed.
set -u
[ $# -ge 1 ] || { sed -n 2,13p "$0"; exit 2; }
SCRIBUS=${SCRIBUS:-/usr/local/bin/scribus}
work=$(mktemp -d /tmp/clean-lines.XXXXXX); chmod 755 "$work"
status=0
for sla in "$@"; do
	name=$(basename "$sla" .sla)
	run=$work/$name; mkdir -p "$run/home" "$run/prefs"
	cp "$sla" "$run/$name.sla"
	for f in $( { echo "$(basename "$sla" .sla)_photos"; grep -o 'ImageFileName="[^"/]*_photos/' "$sla" | sed 's/ImageFileName="//; s#/$##'; } | sort -u ); do
		[ -d "$(dirname "$sla")/$f" ] && cp -r "$(dirname "$sla")/$f" "$run/"
	done
	chown -R s1:s1 "$work" 2>/dev/null
	echo "== $sla"
	env HOME=$run/home QT_IM_MODULE= ${EXTRA_ENV:-} SUNEER_CLEAN_SELFTEST=$run/result.txt SUNEER_CLEAN_SAVEAS=$run/cleaned.sla \
		SUNEER_CLEAN_SELECT=${SELECT:-0} \
		timeout 900 xvfb-run -a -s "-screen 0 1600x1000x24" "$SCRIBUS" -ns -pr "$run/prefs" "$run/$name.sla" > "$run/scribus.log" 2>&1
	rc=$?
	[ -f "$run/result.txt" ] && sed 's/^/   /' "$run/result.txt"
	if [ $rc -ne 0 ] || ! grep -q "^RESULT ALL PASSED" "$run/result.txt" 2>/dev/null; then
		echo "   FAILED (exit $rc$( [ $rc -eq 124 ] && echo ', timeout')$(grep -q 'Scribus crashed' "$run/scribus.log" && echo ', CRASH')) - log: $run/scribus.log"
		status=1
	fi
done
echo "work folder: $work"
[ $status -eq 0 ] && echo "ALL PASSED" || echo "SOME FAILED"
exit $status
