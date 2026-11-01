#!/bin/bash
# News Browser "Open page from workflow" (suneer_news_panel.cpp): the mock workflow
# (fixtures/news_page_lock_mock.py, page-context + sla-templates download) and
# SUNEER_WFOPEN_SELFTEST driving the panel as user A: a plain .sla, an obituary page as a zip
# with its picture, no template, the page's last-used template, HTTP 500 / 400 / 401, a server
# that never answers, the existing-file question, the page lock, the ids in the document.
# usage: tests/suneer/news_open_page.sh <work dir> [scribus]   (PREFS for an isolated profile)
set -u
W=${1:?work dir}; SCRIBUS=${2:-/usr/local/bin/scribus}
HERE="$(cd "$(dirname "$0")" && pwd)"
PR=(); [ -n "${PREFS:-}" ] && PR=(-pr "$PREFS")
mkdir -p "$W/out" "$W/home/.config/Faircode" "$W/run" "$W/fix" "$W/pages"; W="$(cd "$W" && pwd)"; chmod 700 "$W/run"
rm -rf "$W/pages/KNR" "$W/out"/*
PORT=18742; DISP=:93; DATE=2026-11-02
printf '[General]\napiHost=http://127.0.0.1:%s\nloginEmail=a@test.local\npagesFolder=%s\n' $PORT "$W/pages" > "$W/home/.config/Faircode/ScribusNews.conf"
# fixtures: page.sla (Malayalam text frames, an image frame) and obit.zip (.sla + images/, stored, like fflate level 0)
cp "$HERE/fixtures/news_open_page.sla" "$W/fix/page.sla"
# NEWS_OPEN_BADCHAR=1: the workflow sends a raw U+0019 in the first text run: cleaned, the file as sent kept as .original
[ -n "${NEWS_OPEN_BADCHAR:-}" ] && python3 - "$W/fix/page.sla" <<'BAD'
import sys, re
b = open(sys.argv[1], "rb").read()
m = re.search(rb' (?:CH|Chars)="', b)
if not m: sys.exit("no text run in the fixture")
open(sys.argv[1], "wb").write(b[:m.end()] + b"\x19" + b[m.end():])
BAD
convert -size 300x400 gradient:'#d0a080'-'#503020' -fill white -draw "circle 150,150 150,90" "$W/fix/obit_photo.jpg"
python3 - "$W/fix" "$DATE" <<'PY'
import sys, zipfile, re
d, date = sys.argv[1], sys.argv[2]
sla = open(d + "/page.sla", encoding="utf-8").read()
sla = re.sub(r'(<PageObject [^>]*AutoName="IMG1")', r'\1 PFILE="images/0_obit_photo.jpg"', sla, count=1)
assert 'PFILE="images/0_obit_photo.jpg"' in sla
with zipfile.ZipFile(d + "/obit.zip", "w", zipfile.ZIP_STORED) as z:
    z.writestr("page_3_%s.sla" % date, sla)
    z.write(d + "/obit_photo.jpg", "images/0_obit_photo.jpg")
PY
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
XDG_RUNTIME_DIR="$W/run" DISPLAY=$DISP QT_IM_MODULE= SUNEER_WFOPEN_SELFTEST="$W/out/t" SUNEER_WFOPEN_DATE=$DATE SUNEER_WFOPEN_TIMEOUT_MS=3000 \
	timeout 300 dbus-run-session --config-file="$W/bare-session.conf" -- "$SCRIBUS" "${PR[@]}" "$W/doc.sla" > "$W/out/scribus.log" 2>&1
echo "scribus exit $?" > "$W/out/exit.txt"
curl -s -m 5 "http://127.0.0.1:$PORT/api/v1/mock/state" > "$W/out/state-after-exit.json"
cat "$W/out/t-report.txt" 2>/dev/null || echo "FAIL no self-test report"
python3 - "$W" "$DATE" <<'PY'
import sys, json, os
W, DATE = sys.argv[1], sys.argv[2]; fails = 0
def check(what, ok):
    global fails
    print(("PASS " if ok else "FAIL ") + what); fails += 0 if ok else 1
rep = open(W + "/out/t-report.txt").read() if os.path.isfile(W + "/out/t-report.txt") else ""
check("self-test report: ALL PASS", rep.rstrip().endswith("ALL PASS"))
files = sorted(os.path.relpath(os.path.join(r, f), W + "/pages") for r, _, fs in os.walk(W + "/pages") for f in fs)
originals = [f for f in files if f.endswith(".original")]
files = [f for f in files if not f.endswith(".original")]
if os.environ.get("NEWS_OPEN_BADCHAR"):
    check("the file as sent kept: %s" % originals, len(originals) >= 1 and all(b"\x19" in open(W + "/pages/" + f, "rb").read() for f in originals))
    check("the opened pages hold no U+0019", not [f for f in files if f.endswith(".sla") and b"\x19" in open(W + "/pages/" + f, "rb").read()])
print("  files: " + ", ".join(files))
check("pages folder: KNR/%s/ Page_1, Page_1_2, Page_3 (+images), Page_5; no Page_4/6/7/8" % DATE,
      files == ["KNR/%s/%s" % (DATE, f) for f in ["Page_1.sla", "Page_1_2.sla", "Page_3.sla", "Page_3_images/0_obit_photo.jpg", "Page_5.sla"]])
secrets = ("AT-A", "AT-B", "RT-AT", "S3cret", "Bearer")
leak = [f for f in files if f.endswith(".sla") and any(s.encode() in open(W + "/pages/" + f, "rb").read() for s in secrets)]
check("no token / password in the saved .sla files %s" % leak, not leak)
log = open(W + "/home/.cache/scribus/news/api.log", encoding="utf-8").read() if os.path.isfile(W + "/home/.cache/scribus/news/api.log") else ""
check("api.log: downloads logged", "open from workflow: GET /api/v1/sla-templates/tpl-obit/download -> HTTP 200" in log)
check("api.log: no token or password", not any(s in log for s in secrets[:4]))
st = json.load(open(W + "/out/state-after-exit.json"))["data"]
held = [s["pageId"] for s in st if s["lockedById"] == "u-A"]
check("Scribus closed: no page left locked by A (%s)" % held, not held)
for f in ("t-obit-opened.png", "t-panel.png"):
    check("screenshot %s" % f, os.path.isfile(W + "/out/" + f))
print("ALL PASS" if fails == 0 else "FAILED: %d" % fails)
sys.exit(1 if fails else 0)
PY
