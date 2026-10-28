#!/bin/bash
# Delete-from-page = mark unused: the scenarios on a page placed from the fixture (no server).
#   tests/suneer/mark_unused_on_delete.sh page.sla fixture.json
set -u
[ $# -ge 2 ] || { sed -n 2,4p "$0"; exit 2; }
sla=$1; fixture=$2
SCRIBUS=${SCRIBUS:-/usr/local/bin/scribus}
work=$(mktemp -d /tmp/unused-test.XXXXXX); mkdir -p "$work/home" "$work/prefs"
name=$(basename "$sla" .sla); cp "$sla" "$work/$name.sla"
env HOME=$work/home QT_IM_MODULE= SCRIBUS_NEWS_FIXTURE=$fixture SUNEER_UNUSED_SELFTEST=$work/result.txt \
	timeout 600 xvfb-run -a -s "-screen 0 1600x1000x24" "$SCRIBUS" -ns -pr "$work/prefs" "$work/$name.sla" > "$work/scribus.log" 2>&1
rc=$?
[ -f "$work/result.txt" ] && sed 's/^/   /' "$work/result.txt"
echo "work folder: $work (exit $rc)"
grep -q "^RESULT PASS" "$work/result.txt" 2>/dev/null && [ $rc -eq 0 ]
