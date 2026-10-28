#!/bin/bash
# "Adjust gaps" regression test: the gaps of every placed News Browser story, live checks, undo.
#
#   tests/suneer/layout_gaps.sh page.sla [page2.sla ...]
#
# For each .sla (a copy; the original is only read) the installed Scribus starts on a private
# X display with a throw-away HOME and profile, opens the copy, and with SUNEER_GAPS_SELFTEST set
# runs SuneerNewsPanel::runGapSelfTest():
#   A. each story alone: headline / kicker space, column gutter, wrap, caption gaps changed -
#      the values land on the story, no new overlapping frames, gaps remembered, Undo = page as before
#   B. the whole page with the story-to-story gaps (vertical 8 mm, horizontal 2 mm less):
#      no new overlaps, the stacked gaps, Undo = page as before
#   C. a Re-layout with the story's model keeps the story's own gaps; Undo twice = page as before
# Exit status 0 = every check on every page passed.
set -u
[ $# -ge 1 ] || { sed -n 2,14p "$0"; exit 2; }
SCRIBUS=${SCRIBUS:-/usr/local/bin/scribus}
work=$(mktemp -d /tmp/layout-gaps.XXXXXX); chmod 755 "$work"
status=0
for sla in "$@"; do
	name=$(basename "$sla" .sla)
	run=$work/$name; mkdir -p "$run/home" "$run/prefs"
	cp "$sla" "$run/$name.sla"
	# the photo folders the page links to (its own <name>_photos, or one of an earlier Save As)
	for f in $( { echo "$(basename "$sla" .sla)_photos"; grep -o 'ImageFileName="[^"/]*_photos/' "$sla" | sed 's/ImageFileName="//; s#/$##'; } | sort -u ); do
		[ -d "$(dirname "$sla")/$f" ] && cp -r "$(dirname "$sla")/$f" "$run/"
	done
	echo "== $sla"
	env HOME=$run/home QT_IM_MODULE= ${EXTRA_ENV:-} SUNEER_GAPS_SELFTEST=$run/result.txt \
		timeout 900 xvfb-run -a -s "-screen 0 1600x1000x24" "$SCRIBUS" -ns -pr "$run/prefs" "$run/$name.sla" > "$run/scribus.log" 2>&1
	rc=$?
	[ -f "$run/result.txt" ] && sed 's/^/   /' "$run/result.txt"
	if [ $rc -ne 0 ] || ! grep -q "^RESULT PASS" "$run/result.txt" 2>/dev/null; then
		echo "   FAILED (exit $rc$( [ $rc -eq 124 ] && echo ', timeout')$(grep -q 'Scribus crashed' "$run/scribus.log" && echo ', CRASH')) - log: $run/scribus.log"
		status=1
	fi
done
echo "work folder: $work"
[ $status -eq 0 ] && echo "ALL PASSED" || echo "SOME FAILED"
exit $status
