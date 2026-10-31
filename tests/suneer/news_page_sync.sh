#!/bin/bash
# News Browser page sync (suneer_news_panel.cpp reconcilePage / autoMarkStories / noteDocClosing):
# the mock workflow (fixtures/news_page_lock_mock.py) and SUNEER_WFSYNC_SELFTEST as user A, twice.
# Phase 1: Open page from workflow (KNR Page 2, frames written like the workflow's export: no ids),
# link by layout rows and by headline, a used story not on the page -> Mark unused, a page user B
# holds -> nothing sent, place 3 stories, delete 1 while the server refuses marks, Save, quit.
# Phase 2: the server answers again; Scribus opened with the saved file -> sync -> the waiting mark
# goes out -> the list shows 2 used, 1 unused of the 3 placed; another PC marks n-3 used again -> Keep.
# Phase 3: restart with the News Browser on Page 2 already: still asked; Keep remembered (no dialog);
# Settings > Forget Keep -> the next Check asks again (answered Keep again).
# usage: tests/suneer/news_page_sync.sh <work dir> [scribus]
set -u
W=${1:?work dir}; SCRIBUS=${2:-/usr/local/bin/scribus}
HERE="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$W/out" "$W/home/.config/Faircode" "$W/run" "$W/fix" "$W/pages"; W="$(cd "$W" && pwd)"; chmod 700 "$W/run"
rm -rf "$W/pages/KNR" "$W/out"/* "$W/home/.cache" "$W/home/scribus-crashlogs"
PORT=18743; DISP=:93; DATE=2026-11-02
printf '[General]\napiHost=http://127.0.0.1:%s\nloginEmail=a@test.local\npagesFolder=%s\n' $PORT "$W/pages" > "$W/home/.config/Faircode/ScribusNews.conf"
# sync.sla: the open-page fixture + frames as the workflow's export writes them (slaXml.ts
# buildTextPageObject / buildImagePageObject: legacy attribute names, random ItemID, no name,
# soft hyphens in the headline). Page at 100,20: row n-1 (30,40 200x300) -> frame 130,60 with a
# welded photo under it; row n-2 (250,40 200x150) -> 350,60; n-4 has no row (moved) -> 130,500.
python3 - "$HERE/fixtures/news_open_page.sla" "$W/fix/sync.sla" <<'PY'
import sys
src, dst = sys.argv[1], sys.argv[2]
sla = open(src, encoding="utf-8").read()
SH = "­"
def text(iid, x, y, w, h, title, body, weld=None):
    rect = "M0 0 L%.4f 0 L%.4f %.4f L0 %.4f L0 0 Z" % (w, w, h, h)
    welds = '<WeldEntry Target="%d" WX="%.4f" WY="%.4f"/>' % (weld, w / 2, h) if weld else ""
    story = ('<StoryText><DefaultStyle Language="ml"/><ITEXT CH="%s"/><para ALIGN="0"/><ITEXT CH="%s"/><trail/></StoryText>' % (title, body))
    return ('<PageObject XPOS="%.4f" YPOS="%.4f" OwnPage="0" ItemID="%d" PTYPE="4" WIDTH="%.4f" HEIGHT="%.4f" FRTYPE="0" CLIPEDIT="0" '
            'PWIDTH="0.2000" PLINEART="1" PCOLOR="None" PCOLOR2="None" SHADE="100" SHADE2="100" FILLTR="1" %sTEXTFLOWMODE="0" '
            'LOCALSCX="1" LOCALSCY="1" LOCALX="0" LOCALY="0" LOCALROT="0" PICART="1" SCALETYPE="1" RATIO="1" COLUMNS="1" COLGAP="0.0000" '
            'AUTOTEXT="0" EXTRA="0.0000" TEXTRA="0.0000" BEXTRA="0.0000" REXTRA="0.0000" VAlign="0" VerticalAlignment="0" FLOP="1" PLTSHOW="0" '
            'BASEOF="0" textPathType="0" textPathFlipped="0" path="%s" copath="%s" gXpos="%.4f" gYpos="%.4f" gWidth="0" gHeight="0" '
            'LAYER="0" NEXTITEM="-1" BACKITEM="-1">%s%s</PageObject>'
            % (x, y, iid, w, h, ('IsWeldItem="1" WeldSource="%d" ' % iid) if weld else "", rect, rect, x, y, story, welds))
def image(iid, x, y, w, h, weld):
    rect = "M0 0 L%.4f 0 L%.4f %.4f L0 %.4f L0 0 Z" % (w, w, h, h)
    return ('<PageObject XPOS="%.4f" YPOS="%.4f" OwnPage="0" ItemID="%d" PTYPE="2" WIDTH="%.4f" HEIGHT="%.4f" FRTYPE="0" '
            'IsWeldItem="1" WeldSource="%d" CLIPEDIT="0" PWIDTH="0.2" PLINEART="1" LOCALSCX="1.000000" LOCALSCY="1.000000" LOCALX="0.0000" '
            'LOCALY="0.0000" LOCALROT="0" PICART="1" SCALETYPE="1" RATIO="1" COLUMNS="1" COLGAP="0" AUTOTEXT="0" EXTRA="0.0000" TEXTRA="0.0000" '
            'BEXTRA="0.0000" REXTRA="0.0000" VAlign="0" FLOP="0" PLTSHOW="0" BASEOF="0" textPathType="0" textPathFlipped="0" TEXTFLOWMODE="0" '
            'PFILE="" path="%s" copath="%s" gXpos="%.4f" gYpos="%.4f" gWidth="0" gHeight="0" LAYER="0" NEXTITEM="-1" BACKITEM="-1">'
            '<WeldEntry Target="%d" WX="%.4f" WY="0.0000"/></PageObject>' % (x, y, iid, w, h, iid, rect, rect, x, y, weld, w / 2))
body = "ജില്ലയിൽ ഇന്നലെ നടന്ന പരിപാടിയിൽ നിരവധി പേർ പങ്കെടുത്തു."
objs = [
    text(1600000001, 130, 60, 200, 150, "തിരു" + SH + "വ" + SH + "ന" + SH + "ന്ത" + SH + "പു" + SH + "ര" + SH + "ത്ത് കനത്ത മഴ", body, weld=1600000002),
    image(1600000002, 130, 215, 200, 120, 1600000001),
    text(1600000003, 350, 60, 200, 150, "കണ്ണൂ" + SH + "രിൽ പുതിയ പാലം തുറന്നു", body),
    text(1600000004, 130, 500, 200, 120, "കോഴി" + SH + "ക്കോട് പുസ്‌" + SH + "ത" + SH + "കോ" + SH + "ത്സ" + SH + "വം തുടങ്ങി", body),
]
assert sla.count("</Document>") == 1
sla = sla.replace("</Document>", "".join(objs) + "</Document>")
open(dst, "w", encoding="utf-8").write(sla)
PY
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
run_phase() {   # phase, args...
	local ph=$1; shift
	HOME="$W/home" XDG_CONFIG_HOME="$W/home/.config" XDG_DATA_HOME="$W/home/.local/share" XDG_CACHE_HOME="$W/home/.cache" \
	XDG_RUNTIME_DIR="$W/run" DISPLAY=$DISP QT_IM_MODULE= SUNEER_WFSYNC_SELFTEST="$W/out/p$ph" SUNEER_WFSYNC_PHASE=$ph SUNEER_WFSYNC_DATE=$DATE \
		timeout 300 dbus-run-session --config-file="$W/bare-session.conf" -- "$SCRIBUS" "$@" > "$W/out/scribus-p$ph.log" 2>&1
	echo "phase $ph: scribus exit $?" >> "$W/out/exit.txt"
	curl -s -m 5 "http://127.0.0.1:$PORT/api/v1/mock/news" > "$W/out/news-after-p$ph.json"
	cp "$W/home/.cache/scribus/news/pending-marks.json" "$W/out/pending-after-p$ph.json" 2>/dev/null
	cat "$W/out/p$ph-report.txt" 2>/dev/null || echo "FAIL no phase $ph report"
}
# phase 1 starts with a document (no startup New Document dialog, which a scripted quit cannot close)
cp "$HERE/fixtures/news_open_page.sla" "$W/doc.sla"; rm -f "$W/.doc.sla.lock"
run_phase 1 "$W/doc.sla"
curl -s -m 5 -X POST -H 'Authorization: Bearer AT-A' -H 'Content-Type: application/json' -d '{"on": false}' \
	"http://127.0.0.1:$PORT/api/v1/mock/fail-patch" > /dev/null
run_phase 2 "$W/pages/KNR/$DATE/Page_2.sla"
run_phase 3 "$W/pages/KNR/$DATE/Page_2.sla"
python3 - "$W" <<'PY'
import sys, json, os, glob
W = sys.argv[1]; fails = 0
def check(what, ok):
    global fails
    print(("PASS " if ok else "FAIL ") + what); fails += 0 if ok else 1
for ph in (1, 2, 3):
    rep = open("%s/out/p%d-report.txt" % (W, ph)).read() if os.path.isfile("%s/out/p%d-report.txt" % (W, ph)) else ""
    check("phase %d self-test: ALL PASS" % ph, rep.rstrip().endswith("ALL PASS"))
n1 = json.load(open(W + "/out/news-after-p1.json"))["data"]["news"]
pend1 = open(W + "/out/pending-after-p1.json").read() if os.path.isfile(W + "/out/pending-after-p1.json") else ""
check("after phase 1 (server refusing): n-6 still USED on the server (%s)" % n1["n-6"], n1["n-6"]["p-knr-2"] == "USED")
check("after phase 1: pending-marks.json keeps n-6 UNUSED for p-knr-2", '"n-6"' in pend1 and "UNUSED" in pend1 and "p-knr-2" in pend1)
d2 = json.load(open(W + "/out/news-after-p3.json"))["data"]
n2 = {k: v for k, v in d2["news"].items()}
want = {"n-1": "USED", "n-2": "USED", "n-3": "USED", "n-4": "USED", "n-5": "USED", "n-6": "UNUSED", "n-7": "USED"}
got = {k: n2[k]["p-knr-2"] for k in want}
check("server after phase 3, Page 2 (n-3 USED again by another PC, kept): %s" % got, got == want)
check("n-8 on Page 1 (held by user B) untouched: %s" % n2["n-8"], n2["n-8"] == {"p-knr-1": "USED"})
bad = [p for p in d2["patches"] if p["dtpStatus"] != "UNUSED" and p.get("fromStatus") is not None]
check("no USED/BALANCED PATCH carried fromStatus (the workflow refuses it): %d" % len(bad), not bad)
check("every PATCH by user A (%d)" % len(d2["patches"]), all(p["user"] == "u-A" for p in d2["patches"]))
pend2 = json.load(open(W + "/out/pending-after-p3.json")) if os.path.isfile(W + "/out/pending-after-p3.json") else []
check("after phase 3: pending-marks.json empty (%s)" % pend2, pend2 == [])
conf = open(W + "/home/.config/Faircode/ScribusNews.conf", encoding="utf-8").read()
check("Keep stored in ScribusNews.conf (sync/kept has p-knr-2|n-3)", "p-knr-2|n-3" in conf)
secrets = ("AT-A", "AT-B", "RT-AT", "S3cret", "Bearer")
logs = glob.glob(W + "/home/scribus-crashlogs/*.log") + glob.glob(W + "/home/.cache/scribus/news/*.log") + glob.glob(W + "/home/.cache/scribus/news/*.json")
check("logs written: %s" % [os.path.relpath(l, W) for l in logs], any(l.endswith("news-sync.log") for l in logs))
leak = [l for l in logs if any(s in open(l, encoding="utf-8", errors="replace").read() for s in secrets)]
check("no token / password in the logs or the queue file %s" % leak, not leak)
sla = open(W + "/pages/KNR/2026-11-02/Page_2.sla", encoding="utf-8").read()
check("saved Page_2.sla: no token, story ids kept (n-1, n-4, n-5, n-7 linked; n-6 gone)",
      not any(s in sla for s in secrets) and all('"%s"' % n in sla for n in ("n-1", "n-2", "n-4", "n-5", "n-7")) and '"n-6"' not in sla)
check("Malayalam in the saved page (U+0D24)", "ത" in sla)
for f in ("p2-panel.png", "p2-window.png", "p3-settings.png"):
    check("screenshot %s" % f, os.path.isfile(W + "/out/" + f))
print("ALL PASS" if fails == 0 else "FAILED: %d" % fails)
sys.exit(1 if fails else 0)
PY
