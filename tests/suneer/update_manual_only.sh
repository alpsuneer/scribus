#!/bin/bash
# Updates are manual only (2026-11-01): start Scribus against a local mock update server that offers a
# NEWER signed version -> no dialog after 15 s; Help > Check for Updates... shows it with Update enabled;
# the dialog is closed with Later and nothing is installed.
# usage: tests/suneer/update_manual_only.sh <work dir with srv/ cfg/ prefs/> [scribus]
set -u
W=${1:?work dir}; SCRIBUS=${2:-/usr/local/bin/scribus}
mkdir -p "$W/out"; rm -f "$W/out"/*
(cd "$W/srv" && python3 -I -m http.server 18095 --bind 127.0.0.1 > "$W/out/server.log" 2>&1) &
SRV=$!
trap 'kill $SRV 2>/dev/null' EXIT
sleep 1
XDG_CONFIG_HOME="$W/cfg" SUNEER_UPDATE_SELFTEST="$W/out/t" QT_IM_MODULE= timeout 120 \
	xvfb-run -a -s '-screen 0 1400x900x24' "$SCRIBUS" -pr "$W/prefs" > "$W/out/scribus.log" 2>&1
cat "$W/out/t-startup.txt" "$W/out/t-manual.txt" 2>/dev/null
grep -c 'latest.json' "$W/out/server.log" | sed 's/^/manifest requests: /'
ok=1
grep -q 'after 15 s: none' "$W/out/t-startup.txt" 2>/dev/null || { echo "FAIL: a dialog appeared without the user"; ok=0; }
grep -q 'latest 1.7.3-20261231-1 .*Update button enabled 1' "$W/out/t-manual.txt" 2>/dev/null || { echo "FAIL: manual check did not offer the newer version"; ok=0; }
grep -q '_amd64.deb' "$W/out/server.log" && { echo "FAIL: the package was downloaded without Update"; ok=0; }
[ "$(grep -c 'latest.json' "$W/out/server.log")" = 1 ] || { echo "FAIL: manifest fetched other than once (manual)"; ok=0; }
[ $ok = 1 ] && echo "ALL PASS" || exit 1
