#!/bin/bash
# Check repetition on page (ui/suneer_news_repeat.cpp): SUNEER_REPEAT_SELFTEST on a copy of a REAL page.
#   tests/suneer/repeat_on_page.sh <page.sla> <work dir> [scribus]          planted repeats on an obituary page:
#       one obituary again with another id, one placed twice with the same id, a photo again (resized copy in
#       a frame of its own), a paragraph / sentence / word repeated; click, yellow mark not in a page render,
#       Ignore, Delete + Undo, no Unused mark for a story still on the page, page scope, pasteboard.
#   SUNEER_REPEAT_MODE=scan tests/suneer/repeat_on_page.sh <page.sla> <work dir>   whole document once,
#       every entry listed (false positives on real pages).
# Fixture mode (SCRIBUS_NEWS_FIXTURE): no server request is ever made; marks go to news-sync.log only.
set -u
SLA=${1:?page.sla}; W=${2:?work dir}; SCRIBUS=${3:-/usr/local/bin/scribus}
mkdir -p "$W/out" "$W/home" "$W/run" "$W/page"; W="$(cd "$W" && pwd)"; chmod 700 "$W/run"
rm -rf "$W/out"/* "$W/page"/* "$W/home/.cache" "$W/home/.config" "$W/home/scribus-crashlogs"
base=$(basename "$SLA" .sla); dir=$(dirname "$SLA")
cp "$SLA" "$W/page/$base.sla"
[ -d "$dir/${base}_photos" ] && cp -r "$dir/${base}_photos" "$W/page/"
echo '{"data": []}' > "$W/out/fixture.json"
D=${REPEAT_DISPLAY:-:93}
Xvfb $D -screen 0 1600x1000x24 -nolisten tcp >/dev/null 2>&1 & XVFB=$!
trap 'kill $XVFB 2>/dev/null' EXIT
sleep 1
HOME="$W/home" XDG_CONFIG_HOME="$W/home/.config" XDG_CACHE_HOME="$W/home/.cache" XDG_RUNTIME_DIR="$W/run" DISPLAY=$D QT_IM_MODULE= \
SCRIBUS_NEWS_FIXTURE="$W/out/fixture.json" SUNEER_REPEAT_SELFTEST="$W/out/r" SUNEER_REPEAT_MODE="${SUNEER_REPEAT_MODE:-}" SUNEER_REPEAT_DEBUG="${SUNEER_REPEAT_DEBUG:-}" \
	timeout 600 "$SCRIBUS" "$W/page/$base.sla" > "$W/out/scribus.log" 2>&1
echo "scribus exit $?" | tee "$W/out/exit.txt"
cat "$W/out/r-report.txt" 2>/dev/null || echo "FAIL no report"
