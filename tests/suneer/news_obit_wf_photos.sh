#!/bin/bash
# "Process obituary photos" for pages opened from the workflow (suneer_news_obit.cpp processObitPhotos /
# finishObitWfBatch / maybeAutoProcessObitPhotos): the mock workflow (fixtures/news_page_lock_mock.py,
# KNR Page 3 = fixtures/make_obit_wf_zip.py) and SUNEER_OBITWF_SELFTEST as user A, twice.
# Phase 1: open -> asked -> processed in the background; undo / redo; skips; Cancel; selected; lock; menus.
# Phase 2: obituary.json with models that do not exist -> every photo keeps its original, with the reason.
# usage: tests/suneer/news_obit_wf_photos.sh <work dir> [scribus]
set -u
W=${1:?work dir}; SCRIBUS=${2:-/usr/local/bin/scribus}
HERE="$(cd "$(dirname "$0")" && pwd)"
FACES=/home/s1/tmp-obit/placed_face1_photos/original   # real headshots of earlier obituary tests (stay on this laptop)
mkdir -p "$W/out" "$W/home/.config/Faircode" "$W/run" "$W/fix" "$W/pages"; W="$(cd "$W" && pwd)"; chmod 700 "$W/run"
rm -rf "$W/pages/KNR" "$W/out"/* "$W/home/.cache" "$W/home/scribus-crashlogs" "$W/home/.config/scribus"
PORT=18745; DISP=:93; DATE=2026-11-02
printf '[General]\napiHost=http://127.0.0.1:%s\nloginEmail=a@test.local\npagesFolder=%s\n' $PORT "$W/pages" > "$W/home/.config/Faircode/ScribusNews.conf"
convert -size 400x500 gradient:'#7a9cc0'-'#e8d8b0' "$W/fix/noface.jpg"
convert -size 400x300 plasma:fractal -blur 0x2 "$W/fix/news.jpg" 2>/dev/null || convert -size 400x300 gradient:red-blue "$W/fix/news.jpg"
python3 "$HERE/fixtures/make_obit_wf_zip.py" "$HERE/fixtures/news_open_page.sla" "$FACES/CNTRL_john_118_0.jpg" "$FACES/CNTRL_john_137_0.jpg" \
	"$FACES/CNTRL_john_148_0.jpg" "$W/fix/noface.jpg" "$W/fix/news.jpg" "$W/fix/obit.zip" $DATE || exit 1
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
run_phase() {
	local ph=$1
	rm -f "$W/.doc.sla.lock"
	HOME="$W/home" XDG_CONFIG_HOME="$W/home/.config" XDG_DATA_HOME="$W/home/.local/share" XDG_CACHE_HOME="$W/home/.cache" \
	XDG_RUNTIME_DIR="$W/run" DISPLAY=$DISP QT_IM_MODULE= U2NET_HOME=/home/s1/.u2net \
	SUNEER_OBITWF_SELFTEST="$W/out/p$ph" SUNEER_OBITWF_PHASE=$ph SUNEER_WFSYNC_DATE=$DATE \
		timeout 600 dbus-run-session --config-file="$W/bare-session.conf" -- "$SCRIBUS" "$W/doc.sla" > "$W/out/scribus-p$ph.log" 2>&1
	echo "phase $ph: scribus exit $?" >> "$W/out/exit.txt"
	cat "$W/out/p$ph-report.txt" 2>/dev/null || echo "FAIL no phase $ph report"
}
run_phase 1
find "$W/pages" -type f | sed "s|$W/pages/||" | sort > "$W/out/files-after-p1.txt"
# phase 2: models that do not exist; no earlier results to reuse
rm -rf "$W/pages/KNR/$DATE/Page_3_photos"
python3 - "$W/home/.config/scribus/layout-models/obituary.json" <<'PY'
import json, sys, os
p = sys.argv[1]
o = json.load(open(p)) if os.path.isfile(p) else {}
part = o.setdefault("obituary", {}) if "format" in o else o     # a Layout Model file keeps the obit settings under "obituary"
part.setdefault("photo", {})
part["photo"]["model"] = "no-such-model"
part["photo"]["bgModels"] = ["no-such-model-2"]
os.makedirs(os.path.dirname(p), exist_ok=True)
json.dump(o, open(p, "w"), indent=1)
PY
run_phase 2
python3 - "$W" <<'PY'
import sys, os, glob
W = sys.argv[1]; fails = 0
def check(what, ok):
    global fails
    print(("PASS " if ok else "FAIL ") + what); fails += 0 if ok else 1
for ph in (1, 2):
    rep = open("%s/out/p%d-report.txt" % (W, ph)).read() if os.path.isfile("%s/out/p%d-report.txt" % (W, ph)) else ""
    check("phase %d self-test: ALL PASS" % ph, rep.rstrip().endswith("ALL PASS"))
ex = open(W + "/out/exit.txt").read()
check("both phases exited normally (%s)" % ex.strip().replace("\n", "; "), ex.count("exit 0") == 2)
files = open(W + "/out/files-after-p1.txt").read().split()
orig = [f for f in files if "/Page_3_photos/original/" in f]
edit = [f for f in files if "/Page_3_photos/edited/obit/" in f and f.endswith(".png")]
print("  phase 1 files: %d originals, %d processed" % (len(orig), len(edit)))
check("phase 1: originals in Page_3_photos/original (%d), results in edited/obit (%d)" % (len(orig), len(edit)), len(orig) >= 4 and len(edit) >= 4)
secrets = ("AT-A", "AT-B", "RT-AT", "S3cret", "Bearer")
logs = glob.glob(W + "/home/scribus-crashlogs/*.log") + glob.glob(W + "/home/.cache/scribus/news/*.log")
log = "".join(open(l, encoding="utf-8", errors="replace").read() for l in logs)
check("news-sync.log: the batches logged", "obit photos: Obituary photos: 4 processed" in log)
check("no token / password in the logs", not any(s in log for s in secrets))
for f in ("p1-processed.png",):
    check("screenshot %s" % f, os.path.isfile(W + "/out/" + f))
print("ALL PASS" if fails == 0 else "FAILED: %d" % fails)
sys.exit(1 if fails else 0)
PY
