#!/usr/bin/python3 -I
# The "What is new" text (Help > Check for Updates, latest.json) is English only, one short line per change.
#   tools/changelog-english.py [--max N] < text > text      lines with Malayalam are dropped, long lines cut
#   tools/changelog-english.py --check latest.json          exit 1 when the manifest's changelog has Malayalam
# Reports for the developer stay in Malayalam; this is only the text the PCs show.
import json, re, sys
ML = re.compile("[\u0d00-\u0d7f]")
args = sys.argv[1:]
if args[:1] == ["--check"]:
    text = json.load(open(args[1], encoding="utf-8")).get("changelog", "")
    sys.exit(1 if ML.search(text) else 0)
limit = int(args[1]) if args[:1] == ["--max"] else 120
sys.stdin.reconfigure(encoding="utf-8"); sys.stdout.reconfigure(encoding="utf-8"); sys.stderr.reconfigure(encoding="utf-8")
out = []
for line in sys.stdin.read().splitlines():
    line = line.rstrip()
    if not line.strip():
        continue
    if ML.search(line):
        print("changelog: Malayalam line dropped: " + line[:60], file=sys.stderr)
        continue
    if not line.startswith("- "):
        line = "- " + line
    if len(line) > limit:
        line = line[:limit].rsplit(" ", 1)[0].rstrip(" ,;:-") + "..."
    out.append(line)
print("\n".join(out))
