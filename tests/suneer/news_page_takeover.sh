#!/bin/bash
# News Browser "Take over page" (suneer_news_panel.cpp onTakeOverPage / pageLockDetails /
# sendLockHeartbeat): the mock workflow (fixtures/news_page_lock_mock.py) and SUNEER_TAKEOVER_SELFTEST
# as user A (Locked Pages right); user B through the API. B's Page 2 -> Continue anyway -> sync skipped
# -> Details -> Take over -> sync; no right -> no button; own account elsewhere -> nothing asked;
# with the (spec) heartbeat switched on in the mock: A's heartbeat, B's stale lock offered, fresh not.
# usage: tests/suneer/news_page_takeover.sh <work dir> [scribus]
set -u
W=${1:?work dir}; SCRIBUS=${2:-/usr/local/bin/scribus}
HERE="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$W/out" "$W/home/.config/Faircode" "$W/run" "$W/fix" "$W/pages"; W="$(cd "$W" && pwd)"; chmod 700 "$W/run"
rm -rf "$W/pages/KNR" "$W/out"/* "$W/home/.cache" "$W/home/scribus-crashlogs"
PORT=18744; DISP=:93; DATE=2026-11-02
printf '[General]\napiHost=http://127.0.0.1:%s\nloginEmail=a@test.local\npagesFolder=%s\n' $PORT "$W/pages" > "$W/home/.config/Faircode/ScribusNews.conf"
python3 "$HERE/fixtures/make_sync_sla.py" "$HERE/fixtures/news_open_page.sla" "$W/fix/sync.sla" || exit 1
cp "$HERE/fixtures/news_open_page.sla" "$W/doc.sla"; rm -f "$W/.doc.sla.lock"
WFOPEN_FIXTURES="$W/fix" python3 "$HERE/fixtures/news_page_lock_mock.py" $PORT 2>"$W/mock.log" & MOCK=$!
Xvfb $DISP -screen 0 1600x1000x24 -nolisten tcp >/dev/null 2>&1 & XVFB=$!
trap 'kill $MOCK $XVFB 2>/dev/null' EXIT
sleep 1.5
cat > "$W/bare-session.conf" <<'CONF'
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig><type>session</type><listen>unix:tmpdir=/tmp</listen><auth>EXTERNAL</auth>
<policy context="default"><allow send_destination="*" eavesdrop="true"/><allow eavesdrop="true"/><allow own="*"/></policy></busconfig>
CONF
HOME="$W/home" XDG_CONFIG_HOME="$W/home/.config" XDG_DATA_HOME="$W/home/.local/share" XDG_CACHE_HOME="$W/home/.cache" \
XDG_RUNTIME_DIR="$W/run" DISPLAY=$DISP QT_IM_MODULE= SUNEER_TAKEOVER_SELFTEST="$W/out/t" SUNEER_WFSYNC_DATE=$DATE \
	timeout 300 dbus-run-session --config-file="$W/bare-session.conf" -- "$SCRIBUS" "$W/doc.sla" > "$W/out/scribus.log" 2>&1
echo "scribus exit $?" > "$W/out/exit.txt"
curl -s -m 5 "http://127.0.0.1:$PORT/api/v1/mock/state" > "$W/out/state-after-exit.json"
cat "$W/out/t-report.txt" 2>/dev/null || echo "FAIL no self-test report"
python3 - "$W" <<'PY'
import sys, json, os, glob
W = sys.argv[1]; fails = 0
def check(what, ok):
    global fails
    print(("PASS " if ok else "FAIL ") + what); fails += 0 if ok else 1
rep = open(W + "/out/t-report.txt").read() if os.path.isfile(W + "/out/t-report.txt") else ""
check("self-test report: ALL PASS", rep.rstrip().endswith("ALL PASS"))
check("Scribus exited normally (%s)" % open(W + "/out/exit.txt").read().strip(), "exit 0" in open(W + "/out/exit.txt").read())
st = json.load(open(W + "/out/state-after-exit.json"))["data"]
held = {s["pageId"]: s["lockedById"] for s in st if s["lockedById"]}
print("  locks after exit: %s" % held)
check("after exit: A's taken-over Page 6 released; B keeps 4 and 7; A's own earlier Page 5 left (it was A's before)",
      "p-knr-6" not in held and held.get("p-knr-4") == "u-B" and held.get("p-knr-7") == "u-B" and held.get("p-knr-5") == "u-A")
secrets = ("AT-A", "AT-B", "RT-AT", "S3cret", "Bearer")
logs = glob.glob(W + "/home/scribus-crashlogs/*.log") + glob.glob(W + "/home/.cache/scribus/news/*.log")
log = "".join(open(l, encoding="utf-8", errors="replace").read() for l in logs)
check("news-sync.log: take over logged", "take over of Page 2 2026-11-02 from otheruser -> HTTP 200, yours now" in log)
sync = open(W + "/home/scribus-crashlogs/news-sync.log", encoding="utf-8").read() if os.path.isfile(W + "/home/scribus-crashlogs/news-sync.log") else ""
check("heartbeat: 404 noted once (news-sync.log), then not sent again until the mock learned it", sync.count("has no lock heartbeat route") == 1)
check("no token / password in the logs", not any(s in log for s in secrets))
for f in ("t-window-locked.png", "t-details.png", "t-window-stale.png"):
    check("screenshot %s" % f, os.path.isfile(W + "/out/" + f))
print("ALL PASS" if fails == 0 else "FAILED: %d" % fails)
sys.exit(1 if fails else 0)
PY
