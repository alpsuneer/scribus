#!/bin/bash
# Update -> restore point -> Go back -> stays on the old version (SuneerRestorePoint, SuneerRollbackDialog,
# UpdateAvailableDialog). Runs as ROOT on this laptop only (pkexec needs no agent for root; the packages
# really get installed): the laptop ends with <N> installed again.
#
#   sudo tests/suneer/update_rollback.sh <work dir> <N .deb (installed now)> <N+1 .deb> [display]
#
# A local update server (python http.server, 127.0.0.1:8099) serves N+1 with a latest.json signed with the
# laptop's release key (as s1), and N with its .sig (the "package not cached -> from the server" path).
# Steps, each a Scribus start on Xvfb with a throw-away HOME:
#   0  N: first start, office defaults received; then the design conf is edited ("before-update" marker)
#   1  N: Check for Updates -> Update (SUNEER_UPDATE_SELFTEST_ACTION=update, AUTOYES): restore point
#        (settings backup, N fetched from the server and cached), N+1 installed
#   2  N+1: start: office settings applied without a question (marker gone, .bak kept)
#   3  N+1: Check for Updates -> Go back... -> N (SUNEER_ROLLBACK_SELFTEST): N installed from the cache,
#        settings copied back after exit, Scribus restarted by the waiter
#   4  N: Check for Updates -> Later: N+1 offered with the "you went back" note, nothing installed
set -u
W=${1:?work dir}; DEB_N=${2:?N .deb}; DEB_N1=${3:?N+1 .deb}; DISP=${4:-:96}
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(cd "$HERE/../.." && pwd)"
[ "$(id -u)" = 0 ] || { echo "run as root"; exit 2; }
VN=$(dpkg-deb -f "$DEB_N" Version); VN1=$(dpkg-deb -f "$DEB_N1" Version)
[ "$(dpkg-query -W -f='${Version}' scribus)" = "$VN" ] || { echo "installed is $(dpkg-query -W -f='${Version}' scribus), expected $VN"; exit 2; }
BASE="http://127.0.0.1:8099/scribus-updates"
rm -rf "$W/home" "$W/out" "$W/server"; mkdir -p "$W/home/.config/Faircode" "$W/out" "$W/server/scribus-updates"
cp "$DEB_N" "$DEB_N1" "$W/server/scribus-updates/"
chown -R s1:s1 "$W/server"
# sign as s1 (the key is s1's): latest.json for N+1, detached .sig for both
sudo -u s1 -H bash -c "cd '$ROOT' && K=\$HOME/scribus-keys/release-key.pem && \
  tools/sign-update-manifest.sh sign \$K '$VN1' '$BASE/$(basename "$DEB_N1")' '$W/server/scribus-updates/$(basename "$DEB_N1")' '- rollback test' > '$W/server/scribus-updates/latest.json' && \
  for d in '$(basename "$DEB_N")' '$(basename "$DEB_N1")'; do openssl pkeyutl -sign -inkey \$K -rawin -in '$W/server/scribus-updates/'\$d | base64 -w0 > '$W/server/scribus-updates/'\$d.sig; done" || { echo "signing failed"; exit 2; }
printf '[General]\nserverUrl=%s\n' "$BASE" > "$W/home/.config/Faircode/ScribusUpdater.conf"
# the real cache, as on an office PC; emptied so that step 1 has to fetch N from the server
CACHE=/var/cache/scribus-updates; rm -rf "$CACHE"
pkill -f "http.server 8099" 2>/dev/null; sleep 0.5
sudo -u s1 -H python3 -m http.server 8099 --bind 127.0.0.1 --directory "$W/server" > "$W/out/http.log" 2>&1 &
HTTP_PID=$!
pkill -f "Xvfb $DISP" 2>/dev/null; sleep 0.5
Xvfb "$DISP" -screen 0 1280x900x24 > /dev/null 2>&1 &
XVFB_PID=$!
sleep 1
trap 'kill $HTTP_PID $XVFB_PID 2>/dev/null' EXIT
DESIGN="$W/home/.config/Scribus/SuneerDesignStyle.conf"
PREFS="$W/home/.config/scribus"
run() {   # run <name> <timeout> [ENV=... ...]   Scribus with the test HOME, no -pr: the restart has to find the same prefs
	local name=$1 t=$2; shift 2
	env HOME="$W/home" XDG_CONFIG_HOME="$W/home/.config" DISPLAY="$DISP" QT_IM_MODULE= "$@" \
		timeout "$t" /usr/local/bin/scribus -ns > "$W/out/$name.log" 2>&1
	echo "== $name exit $?"
}
# 0 ------------------------------------------------------------------------
run 0-first 120 SUNEER_OFFICEDEF_SELFTEST="$W/out/0-office.txt"
sed -i 's/^1\\tooltip=.*/1\\tooltip=before-update marker/' "$DESIGN"
grep -c "before-update marker" "$DESIGN" | sed 's/^/   marker lines in the design conf: /'
# 1 ------------------------------------------------------------------------
run 1-update 400 SUNEER_UPDATE_SELFTEST="$W/out/1" SUNEER_UPDATE_SELFTEST_ACTION=update SUNEER_UPDATE_AUTOYES=1
grep -h "UPDATE-SELFTEST\|UPDATE-INSTALLED\|Restore point\|\[Restore point\]" "$W/out/1-update.log" | sed 's/^/   /'
echo "   installed now: $(dpkg-query -W -f='${Version}' scribus)"
echo "   cache: $(ls "$CACHE" 2>/dev/null | tr '\n' ' ')"
echo "   backups: $(ls "$W/home/.config/Scribus/backup" 2>/dev/null | tr '\n' ' ')"
# 2 ------------------------------------------------------------------------
run 2-office 120 SUNEER_OFFICEDEF_SELFTEST="$W/out/2-office.txt"
sed 's/^/   /' "$W/out/2-office.txt"
grep -c "before-update marker" "$DESIGN" > "$W/out/2-marker.txt"; sed 's/^/   marker lines in the design conf now: /' "$W/out/2-marker.txt"
# 3 ------------------------------------------------------------------------
run 3-goback 400 SUNEER_UPDATE_SELFTEST="$W/out/3" SUNEER_UPDATE_SELFTEST_ACTION=goback SUNEER_ROLLBACK_SELFTEST="$VN"
grep -h "ROLLBACK\|\[Rollback\]" "$W/out/3-goback.log" | sed 's/^/   /'
# the waiter restores the settings and starts Scribus again: give it time, then stop that instance
for i in $(seq 1 60); do grep -q "^done" "$W"/home/.config/Scribus/backup/*_"$VN"/restore.log 2>/dev/null && break; sleep 1; done
sleep 8
# the restarted instance: the waiter exec()s into it, so it is found by its HOME, never by name (the user's own
# Scribus runs on :0, and a pattern on the command line also matches the shell that started this)
RESTARTED=""
for p in $(ps -eo pid=,comm= | awk '$2 ~ /^scribus/ {print $1}'); do
	tr '\0' '\n' < "/proc/$p/environ" 2>/dev/null | grep -qx "HOME=$W/home" && RESTARTED=$p
done
echo "   restarted scribus pid: ${RESTARTED:-none}"
echo "${RESTARTED:-none}" > "$W/out/3-restarted.txt"
if [ -n "$RESTARTED" ]; then
	tr '\0' '\n' < "/proc/$RESTARTED/environ" | grep -c "^SUNEER_UPDATE\|^SUNEER_ROLLBACK" | sed 's/^/   test variables in the restarted process: /'
	kill "$RESTARTED"; sleep 3; kill -9 "$RESTARTED" 2>/dev/null
fi
cat "$W"/home/.config/Scribus/backup/*_"$VN"/restore.log 2>/dev/null | sed 's/^/   /'
echo "   installed now: $(dpkg-query -W -f='${Version}' scribus)"
grep -c "before-update marker" "$DESIGN" | sed 's/^/   marker lines in the design conf after going back: /'
cat "$PREFS/update-rollback.json" 2>/dev/null | tr -d '\n ' | sed 's/^/   marker: /'; echo
# 4 ------------------------------------------------------------------------
run 4-stays 120 SUNEER_UPDATE_SELFTEST="$W/out/4" SUNEER_UPDATE_SELFTEST_ACTION=later
cat "$W/out/4-manual.txt" | sed 's/^/   /'
echo "   installed now: $(dpkg-query -W -f='${Version}' scribus)"
# checks -------------------------------------------------------------------
python3 - "$W" "$VN" "$VN1" "$CACHE" <<'PY'
import sys, os, glob, json, subprocess
W, VN, VN1, CACHE = sys.argv[1:5]; fails = 0
def check(w, ok):
    global fails; print(("PASS " if ok else "FAIL ") + w); fails += 0 if ok else 1
def rd(p): return open(p, encoding="utf-8", errors="replace").read() if os.path.isfile(p) else ""
design = W + "/home/.config/Scribus/SuneerDesignStyle.conf"
installed = subprocess.run(["dpkg-query", "-W", "-f=${Version}", "scribus"], capture_output=True, text=True).stdout
log1 = rd(W + "/out/1-update.log")
check("1 update: N+1 offered and installed", "UPDATE-INSTALLED " + VN1 in log1)
check("1 update: settings restore point of N saved", "Restore point: settings of " + VN in log1 and glob.glob(W + "/home/.config/Scribus/backup/*_" + VN + "/restore-point.json"))
bk = glob.glob(W + "/home/.config/Scribus/backup/*_" + VN)
check("1 update: the backup holds the edited design conf", bool(bk) and "before-update marker" in rd(bk[0] + "/Scribus/SuneerDesignStyle.conf"))
check("1 update: N fetched from the server and cached, N+1 cached", os.path.isfile(CACHE + "/scribus_%s_amd64.deb" % VN) and os.path.isfile(CACHE + "/scribus_%s_amd64.deb" % VN1) and "not fetched" not in log1)
o2 = rd(W + "/out/2-office.txt")
check("2 N+1 start: office settings applied without a question (marker gone, .bak kept)", "replaced -> office version " + VN1 in o2 and rd(W + "/out/2-marker.txt").strip() == "0" and glob.glob(design + ".bak-*"))
log3 = rd(W + "/out/3-goback.log")
check("3 go back: N installed from the cache, restart started", "ROLLBACK: installed " + VN in log3 and "restart yes" in log3)
rl = glob.glob(W + "/home/.config/Scribus/backup/*_" + VN + "/restore.log")
check("3 go back: settings copied back after exit", bool(rl) and "restored " + design in rd(rl[0]) and "done" in rd(rl[0]))
check("3 go back: the old settings are back (marker present)", "before-update marker" in rd(design))
check("3 go back: Scribus closed by itself (no timeout) and restarted", "== 3-goback exit 124" not in rd(W + "/run.log") and rd(W + "/out/3-restarted.txt").strip() != "none")
m = json.load(open(W + "/home/.config/scribus/update-rollback.json")) if os.path.isfile(W + "/home/.config/scribus/update-rollback.json") else {}
check("3 go back: rollback marker from N+1 to N", m.get("from") == VN1 and m.get("to") == VN)
check("3 go back: a restore point of N+1 was saved too", bool(glob.glob(W + "/home/.config/Scribus/backup/*_" + VN1 + "/restore-point.json")))
m4 = rd(W + "/out/4-manual.txt")
check("4 stays: N running, N+1 offered with the 'went back' note, not installed", ("installed " + VN) in m4 and ("latest " + VN1) in m4 and "You went back from " + VN1 in m4 and installed == VN)
print("ALL PASS" if fails == 0 else "FAILED: %d" % fails); sys.exit(1 if fails else 0)
PY
