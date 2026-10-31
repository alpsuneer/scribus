#!/bin/bash
# News Browser page lock ("Page 2 is opened by ..."), suneer_news_panel.cpp "Page lock":
#   a mock workflow (fixtures/news_page_lock_mock.py, the workflow's own lock rules, two users)
#   and SUNEER_PAGELOCK_SELFTEST driving the panel as user A while user B acts through the API:
#   dropdown labels, the question (Choose another page / Continue anyway), taking and releasing
#   on page / date change, the whole-set PATCH keeping A's other-edition lock, keep-alive after an
#   admin unlock and after B takes the page, a page already A's in the browser, Fetch News checking
#   first, document close, a hanging server, and the release when Scribus is closed.
# usage: tests/suneer/news_page_lock.sh <work dir> <doc.sla> [scribus]   (PREFS for an isolated profile)
set -u
W=${1:?work dir}; DOC=${2:?a small .sla to have a document open}; SCRIBUS=${3:-/usr/local/bin/scribus}
HERE="$(cd "$(dirname "$0")" && pwd)"
PR=(); [ -n "${PREFS:-}" ] && PR=(-pr "$PREFS")
mkdir -p "$W/out" "$W/home/.config/Faircode" "$W/run"; W="$(cd "$W" && pwd)"; chmod 700 "$W/run"
PORT=18741; DISP=:93
printf '[General]\napiHost=http://127.0.0.1:%s\nloginEmail=a@test.local\n' $PORT > "$W/home/.config/Faircode/ScribusNews.conf"
cp "$DOC" "$W/doc.sla"; rm -f "$W/.doc.sla.lock"
python3 "$HERE/fixtures/news_page_lock_mock.py" $PORT 2>"$W/mock.log" & MOCK=$!
Xvfb $DISP -screen 0 1600x1000x24 -nolisten tcp >/dev/null 2>&1 & XVFB=$!
trap 'kill $MOCK $XVFB 2>/dev/null' EXIT
sleep 1.5
# A session bus that can start nothing: no keyring, the login stays in memory (no prompt, no files).
cat > "$W/bare-session.conf" <<'CONF'
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig><type>session</type><listen>unix:tmpdir=/tmp</listen><auth>EXTERNAL</auth>
<policy context="default"><allow send_destination="*" eavesdrop="true"/><allow eavesdrop="true"/><allow own="*"/></policy></busconfig>
CONF
t0=$(date +%s%N)
HOME="$W/home" XDG_CONFIG_HOME="$W/home/.config" XDG_DATA_HOME="$W/home/.local/share" XDG_CACHE_HOME="$W/home/.cache" \
XDG_RUNTIME_DIR="$W/run" DISPLAY=$DISP QT_IM_MODULE= SUNEER_PAGELOCK_SELFTEST="$W/out/t" SUNEER_PAGELOCK_DATE=2026-11-02 \
	timeout 300 dbus-run-session --config-file="$W/bare-session.conf" -- "$SCRIBUS" "${PR[@]}" "$W/doc.sla" > "$W/out/scribus.log" 2>&1
echo "scribus exit $? after $(( ($(date +%s%N) - t0) / 1000000 )) ms" > "$W/out/exit.txt"
curl -s -m 5 "http://127.0.0.1:$PORT/api/v1/mock/state" > "$W/out/state-after-exit.json"
cat "$W/out/t-report.txt" 2>/dev/null || echo "FAIL no self-test report"
python3 - "$W" <<'PY'
import sys, json, os
W = sys.argv[1]; fails = 0
def check(what, ok):
    global fails
    print(("PASS " if ok else "FAIL ") + what); fails += 0 if ok else 1
rep = open(W + "/out/t-report.txt").read() if os.path.isfile(W + "/out/t-report.txt") else ""
check("self-test report: ALL PASS", rep.rstrip().endswith("ALL PASS"))
st = json.load(open(W + "/out/state-after-exit.json"))["data"]
p5 = [s for s in st if s["pageId"] == "p-knr-5" and s["date"] == "2026-11-02"]
check("Scribus closed: KNR Page 5 released on the server (%s)" % p5, bool(p5) and p5[0]["lockedById"] is None)
keep = [s for s in st if s["lockedById"] == "u-A"]
check("A's own browser locks untouched by the exit: %s" % sorted(s["pageId"] for s in keep),
      sorted(s["pageId"] for s in keep) == ["p-knr-6", "p-tvm-2"])
log = open(W + "/home/.cache/scribus/news/api.log", encoding="utf-8").read() if os.path.isfile(W + "/home/.cache/scribus/news/api.log") else ""
check("api.log: release logged on exit", "page lock: Page 5 2026-11-02 released (Scribus closed)" in log)
check("api.log: no token or password", not any(s in log for s in ("AT-A", "AT-B", "RT-", "S3cret")))
for f in ("t-dropdown.png", "t-question.png", "t-continued.png"):
    check("screenshot %s" % f, os.path.isfile(W + "/out/" + f))
print("ALL PASS" if fails == 0 else "FAILED: %d" % fails)
sys.exit(1 if fails else 0)
PY
