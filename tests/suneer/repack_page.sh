#!/bin/bash
# Layout > Re-pack Page / Re-pack Automatically regression (ui/suneer_news_repack.cpp, runRepackSelfTest).
# usage: tests/suneer/repack_page.sh page.sla [report.txt]
#   SUNEER_REPACK_AUTO=<story ids, comma separated>  test Re-pack Automatically by deleting those stories instead
#   SUNEER_REPACK_SAVEAS=<sla>  SUNEER_REPACK_SHOT=<png prefix>  SCRIBUS=<binary>  PREFS=<prefs dir>
# The page is opened on a throw-away Xvfb; the report ends with ALL PASS or FAILED: n (exit 0 / 1).
set -u
sla=$1; rep=${2:-${sla%.sla}-repack.txt}
SCRIBUS=${SCRIBUS:-/usr/local/bin/scribus}
args=()
[ -n "${PREFS:-}" ] && args=(-pr "$PREFS")
rm -f "$(dirname "$sla")/.$(basename "$sla").lock"
SUNEER_REPACK_SELFTEST=$rep QT_IM_MODULE= timeout 300 xvfb-run -a -s '-screen 0 1600x1000x24' "$SCRIBUS" "${args[@]}" "$sla" > "${rep%.txt}.log" 2>&1
code=$?
cat "$rep"
exit $code
