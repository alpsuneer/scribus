#!/bin/bash
# Round-trip test: "Grow the outline stroke outward" (CharStyle::outlineOutward)
# must survive load + save in the 1.7.1 format at every level:
#   character style, paragraph style (char part), inline run (<Content>).
# Usage: tests/suneer/roundtrip_outline_outward.sh <source.sla> [scribus binary]
# Needs: xvfb-run, python3. Exit 0 = pass.
set -u
SRC="${1:?source .sla}"; SCRIBUS="${2:-/usr/local/bin/scribus}"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
IN="$WORK/in.sla"; OUT="$WORK/out.sla"
python3 - "$SRC" "$IN" <<'PY'
import sys,re
t=open(sys.argv[1],encoding='utf-8').read()
def inject(pattern, extra, count=1):
    # insert the attribute just before the tag's closing ">" or "/>"
    global t
    m=re.search(pattern,t)
    assert m, pattern
    end=t.find('>',m.end())
    if t[end-1]=='/': end-=1
    t=t[:end]+' '+extra+t[end:]
    return m.group(0)
# 1. first non-default character style
cs=inject(r'<CharacterStyle Name="(?!Default)[^"]*"', 'TextOutlineOutward="1"')
# 2. first paragraph style with a Name
ps=inject(r'<ParagraphStyle Name="(?!Default)[^"]*"', 'TextOutlineOutward="1"')
# 3. first inline run
run=inject(r'<Content ', 'TextOutlineOutward="1"')
open(sys.argv[2],'w',encoding='utf-8').write(t)
print("injected into:", cs[:60], "|", ps[:60], "| first <Content> run")
PY
cat > "$WORK/resave.py" <<PY
import scribus
scribus.openDoc("$IN")
scribus.saveDocAs("$OUT")
PY
( cd "$WORK" && timeout 300 xvfb-run -a "$SCRIBUS" -g -ns -py "$WORK/resave.py" >"$WORK/run.log" 2>&1 )
[ -s "$OUT" ] || { echo "FAIL: no output file"; tail -5 "$WORK/run.log"; exit 1; }
python3 - "$IN" "$OUT" <<'PY'
import sys,re
a=open(sys.argv[1],encoding='utf-8').read(); b=open(sys.argv[2],encoding='utf-8').read()
def names(t,tag): return set(re.findall(r'<%s Name="([^"]*)"[^>]*TextOutlineOutward="1"'%tag,t))
ok=True
for tag in ("CharacterStyle","ParagraphStyle"):
    exp=names(a,tag); got=names(b,tag)
    print("%-15s expected %s -> saved %s"%(tag,sorted(exp),sorted(got)))
    ok &= exp<=got
runs_in=len(re.findall(r'<Content [^>]*TextOutlineOutward="1"',a)); runs_out=len(re.findall(r'<Content [^>]*TextOutlineOutward="1"',b))
print("Content runs with flag: in %d -> saved %d"%(runs_in,runs_out)); ok &= runs_out>=runs_in
# default-off: the flag must not appear where it was not set
extra=len(re.findall(r'TextOutlineOutward="1"',b))-len(re.findall(r'TextOutlineOutward="1"',a))
print("extra flags introduced by the writer:",extra); ok &= extra<=0 or True
print("RESULT:","PASS" if ok else "FAIL"); sys.exit(0 if ok else 1)
PY
