#!/bin/bash
# An edited photo stays edited (suneerimageresample.cpp originalPathOf / resample): SUNEER_IMGKEEP_SELFTEST
# on a news photo after Edit photo > Apply (background removed) and an obituary photo after the
# obituary pipeline - resize by handle with auto-resample, W/H, the image size toolbar's resample,
# enlarge again, Adjust Frame to Image, embedded; phase 2 reopens the saved file. The self-test reads the
# frame's loaded PIXELS (a background corner): blue / grey = the edit, green = the original came back.
# usage: tests/suneer/image_keep_edit.sh <work dir> [scribus]
set -u
W=${1:?work dir}; SCRIBUS=${2:-/usr/local/bin/scribus}
HERE="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$W/out" "$W/home" "$W/run" "$W/fix" "$W/doc"; W="$(cd "$W" && pwd)"; chmod 700 "$W/run"
rm -rf "$W/out"/* "$W/doc"/* "$W/home/.config" "$W/home/.cache" "$W/home/scribus-crashlogs"
convert -size 1600x1200 xc:'rgb(30,200,40)' -fill 'rgb(220,30,30)' -draw "circle 800,600 800,250" "$W/fix/photo.jpg"
convert -size 900x1100 xc:'rgb(30,200,40)' -fill 'rgb(220,30,30)' -draw "circle 450,500 450,250" "$W/fix/obit_orig.jpg"
convert -size 600x720 xc:'rgb(190,196,200)' -fill 'rgb(220,30,30)' -draw "circle 300,330 300,150" "$W/fix/obit_out.png"
cp "$HERE/fixtures/news_open_page.sla" "$W/doc/keep.sla"
Xvfb :93 -screen 0 1600x1000x24 -nolisten tcp >/dev/null 2>&1 & XVFB=$!
trap 'kill $XVFB 2>/dev/null' EXIT
sleep 1
# phases 1-2: keep.sla; phases 3-5: restore.sla (damaged as the old build did, then opened twice)
cp "$HERE/fixtures/news_open_page.sla" "$W/doc/restore.sla"
rm -rf "$W/home/scribus-crashlogs"
for ph in 1 2 3 4 5; do
	doc="$W/doc/keep.sla"; [ $ph -ge 3 ] && doc="$W/doc/restore.sla"
	rm -f "$W/doc/.keep.sla.lock" "$W/doc/.restore.sla.lock"
	HOME="$W/home" XDG_CONFIG_HOME="$W/home/.config" XDG_CACHE_HOME="$W/home/.cache" XDG_RUNTIME_DIR="$W/run" DISPLAY=:93 QT_IM_MODULE= \
	SUNEER_IMGKEEP_SELFTEST="$W/out/p$ph" SUNEER_IMGKEEP_DIR="$W/fix" SUNEER_IMGKEEP_PHASE=$ph \
		timeout 300 "$SCRIBUS" "$doc" > "$W/out/scribus-p$ph.log" 2>&1
	echo "phase $ph: scribus exit $?" >> "$W/out/exit.txt"
	echo "== phase $ph"; cat "$W/out/p$ph-report.txt" 2>/dev/null || echo "FAIL no report"
done
cat "$W/out/exit.txt"
ok=1; for ph in 1 2 3 4 5; do grep -q "ALL PASS" "$W/out/p$ph-report.txt" 2>/dev/null || ok=0; done
[ $ok = 1 ] && [ "$(grep -c 'exit 0' "$W/out/exit.txt")" = 5 ] && echo "ALL PASS" || echo "FAILED"
