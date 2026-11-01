#!/bin/bash
# Office default settings sync (scribus/suneerofficedefaults.cpp): two shipped versions (v1 = 9 design styles,
# no 02 BodyText shortcut; v2 = the repo's resources/office-defaults with v1 listed as "previous") against
#   A  a clean profile            -> v2 copied in, receipt written, dbi made the Default shortcut set
#   B  a profile on v1 untouched  -> v2 replaces it (old file kept as .bak); a second start does nothing
#   C  a profile on v1 with edits -> asked once: merge (new style / shortcut added, edits kept), keep, office
# usage: tests/suneer/office_defaults.sh <work dir> [scribus]
set -u
W=${1:?work dir}; SCRIBUS=${2:-/usr/local/bin/scribus}; HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(cd "$HERE/../.." && pwd)"
rm -rf "${W:?}/cases" "${W:?}/ship"; mkdir -p "$W/cases" "$W/ship/v1" "$W/ship/v2" "$W/out"
python3 - "$ROOT/resources/office-defaults" "$W/ship" <<'PY'
import sys, os, re, json, hashlib, shutil
src, ship = sys.argv[1], sys.argv[2]
def sha(b): return hashlib.sha256(b).hexdigest()
design = open(os.path.join(src, "SuneerDesignStyle.conf"), encoding="utf-8").read()
short = open(os.path.join(src, "ParagraphStyleShortcuts.conf"), encoding="utf-8").read()
# v1: style 10 missing, size 9; no 02 BodyText shortcut
d1 = "\n".join(l for l in design.splitlines() if not l.startswith("10\\")).replace("size=10", "size=9") + "\n"
s1 = "\n".join(l for l in short.splitlines() if not l.startswith("02%20BodyText")) + "\n"
open(os.path.join(ship, "v1", "SuneerDesignStyle.conf"), "w", encoding="utf-8").write(d1)
open(os.path.join(ship, "v1", "ParagraphStyleShortcuts.conf"), "w", encoding="utf-8").write(s1)
json.dump({"version": "test-v1", "files": {"SuneerDesignStyle.conf": {"sha256": sha(d1.encode()), "previous": []},
                                            "ParagraphStyleShortcuts.conf": {"sha256": sha(s1.encode()), "previous": []}}},
          open(os.path.join(ship, "v1", "office-defaults.json"), "w"), indent=1)
shutil.copy(os.path.join(src, "SuneerDesignStyle.conf"), os.path.join(ship, "v2", "SuneerDesignStyle.conf"))
shutil.copy(os.path.join(src, "ParagraphStyleShortcuts.conf"), os.path.join(ship, "v2", "ParagraphStyleShortcuts.conf"))
json.dump({"version": "test-v2", "files": {"SuneerDesignStyle.conf": {"sha256": sha(design.encode()), "previous": [sha(d1.encode())]},
                                            "ParagraphStyleShortcuts.conf": {"sha256": sha(short.encode()), "previous": [sha(s1.encode())]}}},
          open(os.path.join(ship, "v2", "office-defaults.json"), "w"), indent=1)
print("shipped v1 (9 styles) and v2 (%d styles)" % design.count("\\tooltip="))
PY
run() {   # run <case> <ship version> <answer> <report name>
	local c=$1 v=$2 ans=$3 rep=$4; mkdir -p "$W/cases/$c/home/.config" "$W/cases/$c/prefs"
	HOME="$W/cases/$c/home" XDG_CONFIG_HOME="$W/cases/$c/home/.config" QT_IM_MODULE= SUNEER_OFFICE_DEFAULTS_DIR="$W/ship/$v" \
	SUNEER_OFFICEDEF_SELFTEST="$W/out/$c-$rep.txt" SUNEER_OFFICEDEF_ANSWER="$ans" \
		timeout 120 xvfb-run -a -s '-screen 0 1280x900x24' "$SCRIBUS" -ns -pr "$W/cases/$c/prefs" > "$W/out/$c-$rep.log" 2>&1
	echo "== $c ($v, answer '$ans') exit $?"; sed 's/^/   /' "$W/out/$c-$rep.txt" 2>/dev/null || echo "   FAIL no report"
}
edit() {   # the user's edits: tooltip of style 1, an own shortcut
	local c=$1; sed -i 's/^1\\tooltip=.*/1\\tooltip=my own style/' "$W/cases/$c/home/.config/Scribus/SuneerDesignStyle.conf"
	printf '12%%20Kicker=Ctrl+Shift+9\n' >> "$W/cases/$c/home/.config/Scribus/ParagraphStyleShortcuts.conf"
}
run A v2 "" first
run B v1 "" first; run B v2 "" second; run B v2 "" third
run Cm v1 "" first; edit Cm; run Cm v2 merge second
run Ck v1 "" first; edit Ck; run Ck v2 keep second; run Ck v2 keep third
run Co v1 "" first; edit Co; run Co v2 office second
python3 - "$W" <<'PY'
import sys, os, json, hashlib, glob, re
W = sys.argv[1]; fails = 0
def check(w, ok):
    global fails; print(("PASS " if ok else "FAIL ") + w); fails += 0 if ok else 1
def sha(p): return hashlib.sha256(open(p, "rb").read()).hexdigest()
def rep(c, n): return open("%s/out/%s-%s.txt" % (W, c, n), encoding="utf-8").read() if os.path.isfile("%s/out/%s-%s.txt" % (W, c, n)) else ""
def design(c): return "%s/cases/%s/home/.config/Scribus/SuneerDesignStyle.conf" % (W, c)
def short(c): return "%s/cases/%s/home/.config/Scribus/ParagraphStyleShortcuts.conf" % (W, c)
def receipt(c):
    p = "%s/cases/%s/prefs/office-defaults-received.json" % (W, c)
    return json.load(open(p)) if os.path.isfile(p) else {}
def styles(p): return open(p, encoding="utf-8").read().count("\\tooltip=") if os.path.isfile(p) else -1
v2d = sha("%s/ship/v2/SuneerDesignStyle.conf" % W); v2s = sha("%s/ship/v2/ParagraphStyleShortcuts.conf" % W)
# A
check("A clean: design conf copied (10 styles) and equals v2", os.path.isfile(design("A")) and sha(design("A")) == v2d and styles(design("A")) == 10)
check("A clean: shortcuts copied", os.path.isfile(short("A")) and sha(short("A")) == v2s)
check("A clean: receipt records v2", receipt("A").get("SuneerDesignStyle.conf") == v2d and receipt("A").get("version") == "test-v2")
check("A clean: dbi made the Default shortcut set", "made the Default" in rep("A", "first"))
# B
check("B: v1 first (9 styles)", "missing -> office version test-v1" in rep("B", "first"))
check("B: v2 replaced the untouched v1 file (10 styles, .bak kept)", "unchanged since the last office version -> office version test-v2" in rep("B", "second") and styles(design("B")) == 10 and glob.glob(design("B") + ".bak-*"))
check("B: third start does nothing", rep("B", "third").count("received already") == 2 and "left as chosen" in rep("B", "third"))
# C merge
d = open(design("Cm"), encoding="utf-8").read() if os.path.isfile(design("Cm")) else ""
check("C merge: asked and merged (new entries added)", "merged, 1 new entry added" in rep("Cm", "second"))
check("C merge: user's tooltip kept, Style 10 added, size 10", "my own style" in d and "double.png" in d and "size=10" in d)
s = open(short("Cm"), encoding="utf-8").read()
check("C merge: user's shortcut kept and 02 BodyText added", "12%20Kicker=Ctrl+Shift+9" in s and "02%20BodyText=Ctrl+Shift+4" in s and "merged, 1 new entry added" in rep("Cm", "second").split("ParagraphStyleShortcuts")[1])
# C keep
check("C keep: files untouched, receipt updated, not asked again", "kept (not asked again" in rep("Ck", "second") and styles(design("Ck")) == 9 and rep("Ck", "third").count("received already") == 2)
# C office
check("C office: replaced with v2, .bak kept", "local changes -> office version test-v2" in rep("Co", "second") and sha(design("Co")) == v2d and glob.glob(design("Co") + ".bak-*"))
print("ALL PASS" if fails == 0 else "FAILED: %d" % fails); sys.exit(1 if fails else 0)
PY
