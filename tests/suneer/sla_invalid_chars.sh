#!/bin/bash
# SLA files with characters XML 1.0 forbids (suneerxmlclean.cpp, the 1.5 / 1.7 / 1.7.1 loaders and writers).
#   sudo -u s1 tests/suneer/sla_invalid_chars.sh <page.sla> <work dir> [scribus]
# The page is copied, never changed. Cases, each a headless Scribus with a throw-away profile:
#   real      the page as it is: opens, one report line, Malayalam kept, saved file valid and reopens without a report
#   gz        the same page as .sla.gz
#   ref       the character as a numeric reference (&#25;) and a raw U+FFFE as well: 2 more removed
#   stray     a story given a stray U+0019 and a frame name with U+0007: the saved file holds neither
# Needs the plugins of the same build: run it with the installed package.
set -u
SLA=${1:?page.sla}; W=${2:?work dir}; SCRIBUS=${3:-/usr/local/bin/scribus}
mkdir -p "$W"; W=$(cd "$W" && pwd); rm -rf "$W/run"; mkdir -p "$W/run/home/.config" "$W/run/prefs" "$W/run/page"
dir=$(dirname "$SLA"); base=$(basename "$SLA" .sla)
cp "$SLA" "$W/run/page/p.sla"; [ -d "$dir/images" ] && cp -r "$dir/images" "$W/run/page/"
gzip -c "$SLA" > "$W/run/page/g.sla.gz"
python3 - "$SLA" "$W/run/page/r.sla" <<'PY'
import sys
b = open(sys.argv[1], "rb").read()
i = b.index(b' CH="', 200000) + 5
b = b[:i] + b"&#25;\xef\xbf\xbe" + b[i:]
open(sys.argv[2], "wb").write(b)
PY
cat > "$W/run/t.py" <<PY
import scribus, sys, json
name, path, saveas, stray = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4] == "1"
out = {"name": name}
try:
    scribus.openDoc(path)
    objs = [o[0] for o in scribus.getPageItems()]
    out["items"] = len(objs)
    texts = []
    for o, t, _ in scribus.getPageItems():
        if t == 4:
            texts.append(scribus.getAllText(o))
    joined = "\n".join(texts)
    out["ml"] = sum(0xD00 <= ord(c) <= 0xD7F for c in joined)
    # Scribus' own markers (object U+0019, breaks, page number) are text too; anything else below 32 is a control code
    out["ctrl"] = sum(ord(c) < 32 and c not in "\t\n\r" and not 23 <= ord(c) <= 30 for c in joined)
    out["objects"] = joined.count("\x19")
    i = joined.find("തിരു")
    if stray:
        f = [o for o, t, _ in scribus.getPageItems() if t == 4][0]
        scribus.insertText("\x19", 0, f)
        scribus.setItemName("Bad\x07Name", f)
    scribus.saveDocAs(saveas)
    out["saved"] = True
except Exception as e:
    out["error"] = str(e)
open(path + ".json", "w", encoding="utf-8").write(json.dumps(out, ensure_ascii=False))
PY
run() {   # run <name> <file> <save as> <stray> ; report in <name>.report
	rm -f "$W/run/$1.report"
	env HOME="$W/run/home" XDG_CONFIG_HOME="$W/run/home/.config" QT_IM_MODULE= SUNEER_XMLCLEAN_REPORT="$W/run/$1.report" \
		timeout 120 xvfb-run -a "$SCRIBUS" -g -ns -pr "$W/run/prefs" -py "$W/run/t.py" "$1" "$2" "$3" "$4" > "$W/run/$1.log" 2>&1
	echo "== $1 exit $?: $(cat "$2.json" 2>/dev/null)"; sed 's/^/   /' "$W/run/$1.report" 2>/dev/null
}
P=$W/run/page
run real "$P/p.sla" "$P/p-saved.sla" 0
run reopen "$P/p-saved.sla" "$P/p-saved2.sla" 0
run gz "$P/g.sla.gz" "$P/g-saved.sla" 0
run ref "$P/r.sla" "$P/r-saved.sla" 0
run stray "$P/p-saved.sla" "$P/s-saved.sla" 1
python3 - "$W/run" "$SLA" <<'PY'
import sys, re, json, os, hashlib
R, orig = sys.argv[1], sys.argv[2]; P = R + "/page"; fails = 0
def check(w, ok):
    global fails; print(("PASS " if ok else "FAIL ") + w); fails += 0 if ok else 1
def j(n):
    try: return json.load(open(P + "/" + n + ".json", encoding="utf-8"))
    except Exception: return {}
def rep(n): return open(R + "/" + n + ".report", encoding="utf-8").read() if os.path.isfile(R + "/" + n + ".report") else ""
bad = re.compile(rb"[\x00-\x08\x0b\x0c\x0e-\x1f]|\xef\xbf[\xbe\xbf]|&#(?:x0*(?:[0-8bcef]|1[0-9a-f])|0*(?:[0-8]|1[124-9]|2[0-9]|3[01]));", re.I)
def clean(n):
    p = P + "/" + n
    return os.path.isfile(p) and not bad.search(open(p, "rb").read())
r = j("p.sla"); 
check("real page opens (%s items, %s Malayalam codepoints, %s control chars, %s inline objects in text)" % (r.get("items"), r.get("ml"), r.get("ctrl"), r.get("objects")), r.get("items", 0) > 10 and r.get("ml", 0) > 500 and r.get("ctrl") == 0 and "error" not in r)
check("one message, names the story and the line: " + rep("real").strip(), rep("real").count("\n") == 1 and "Removed 1 invalid character" in rep("real") and "line 488" in rep("real") and "ലേബർ" in rep("real") and "please save the page" in rep("real"))
check("saved copy has no invalid character", clean("p-saved.sla"))
check("the saved copy reopens without a message, same items and text", rep("reopen") == "" and j("p-saved.sla").get("items") == r.get("items") and j("p-saved.sla").get("ml") == r.get("ml"))
check("the given file is unchanged by the test", b"\x19" in open(orig, "rb").read())
g = j("g.sla.gz")
check("gz page opens with the same message", g.get("items") == r.get("items") and "line 488" in rep("gz"))
x = j("r.sla")
check("numeric reference + U+FFFE removed too: " + rep("ref").strip(), x.get("items") == r.get("items") and "Removed 3 invalid characters" in rep("ref"))
s = open(P + "/s-saved.sla", "rb").read() if os.path.isfile(P + "/s-saved.sla") else b""
saved = open(P + "/p-saved.sla", "rb").read()
check("stray U+0019 in a story: not written (Unicode=\"25\" elements: %d before, %d after; the page's own inline objects stay)" % (saved.count(b'Unicode="25"'), s.count(b'Unicode="25"')),
      clean("s-saved.sla") and s.count(b'Unicode="25"') == saved.count(b'Unicode="25"') and (b"COBJ" in saved or b'Unicode="25" Object=' in saved))
check("control character in a frame name: not written", b"BadName" in s and clean("s-saved.sla"))
print("ALL PASS" if fails == 0 else "FAILED: %d" % fails); sys.exit(1 if fails else 0)
PY
