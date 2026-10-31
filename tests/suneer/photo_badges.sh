#!/bin/bash
# Photo badges at 25 / 50 / 100 / 200 % zoom (suneerphotobadges.cpp, SUNEER_BADGE_SELFTEST).
# usage: tests/suneer/photo_badges.sh page.sla [png prefix]   SCRIBUS=<binary>  PREFS=<prefs dir>
# The page needs badged photos (linked, obit.photoStatus lowres / bg / noface). Writes <prefix>-*.png and
# <prefix>-report.txt, then checks: dots only at 25 %, labels at 100 %, full label when selected, none when off.
set -u
sla=$1; pre=${2:-${sla%.sla}-badges}
SCRIBUS=${SCRIBUS:-/usr/local/bin/scribus}
args=()
[ -n "${PREFS:-}" ] && args=(-pr "$PREFS")
rm -f "$(dirname "$sla")/.$(basename "$sla").lock" "$pre-report.txt"
SUNEER_BADGE_SELFTEST=$pre QT_IM_MODULE= timeout 300 xvfb-run -a -s '-screen 0 1600x1000x24' "$SCRIBUS" "${args[@]}" "$sla" > "$pre.log" 2>&1
rep=$pre-report.txt
[ -f "$rep" ] || { echo "FAILED: no report ($pre.log)"; exit 1; }
cat "$rep"
fail=0
section() { awk -v s="== $1 " 'index($0,s)==1{on=1;next} /^== /{on=0} on&&/^  /' "$rep"; }
check() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fail=$((fail+1)); fi; }
check "25 %: dots only"          '[ -n "$(section z025)" ] && ! section z025 | grep -qv ": dot "'
check "100 %: labels"            'section z100 | grep -q "link\[LINK\]"'
check "100 %: no badge > 20 %"   '! section z100 | grep -oE "=[0-9.]+%" | tr -d "=%" | awk "\$1>20{f=1} END{exit !f}"'
check "selected: full label"     'section z050-selected | grep "sel=1" | grep -q "check\[low res: "'
check "nothing mode"             '! section z025-nothing | grep -qv ": nothing"'
check "badges off"               '! section z100-off | grep -qv ": off"'
check "shortcut"                 'grep -q "key '"'"'Ctrl+Alt+B'"'"'" "$rep"'
[ $fail -eq 0 ] && echo "ALL PASS" || echo "FAILED: $fail"
exit $((fail > 0))
