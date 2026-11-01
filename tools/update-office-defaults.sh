#!/bin/bash
# update-office-defaults.sh - the laptop's current settings become the office defaults the .deb ships
# (resources/office-defaults/, installed to <share>/office-defaults/; SuneerOfficeDefaults syncs them into
# every profile at start - see scribus/suneerofficedefaults.h).
#
#   tools/update-office-defaults.sh [--version V] [--summary FILE] [--check-only] [--profile DIR]
#
# Files: SuneerDesignStyle.conf and ParagraphStyleShortcuts.conf from <profile>/Scribus/ (QSettings
# "Scribus"), suneer-text-gradients.json from <profile>/scribus/ when present. Icon paths in the design
# conf are reduced to the file name (the package's design-icons folder). office-defaults.json records
# the version, the sha256 of each file and of every earlier shipped copy ("previous"), so a profile
# whose file equals an older shipped version is updated without a question.
# Prints the diff against the committed copies; tools/release.sh runs this before every build.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/resources/office-defaults"
PROFILE="${SCRIBUS_PROFILE_ROOT:-/home/s1/.config}"
VERSION=""; SUMMARY=""; CHECK=0
while [ $# -gt 0 ]; do
	case "$1" in
		--version) VERSION="$2"; shift ;;
		--summary) SUMMARY="$2"; shift ;;
		--profile) PROFILE="$2"; shift ;;
		--check-only) CHECK=1 ;;
		*) echo "unknown option $1" >&2; exit 2 ;;
	esac
	shift
done
mkdir -p "$OUT"
python3 - "$OUT" "$PROFILE" "$VERSION" "$CHECK" "$SUMMARY" <<'PY'
import sys, os, re, json, hashlib, datetime, shutil
out, profile, version, check, summary = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4] == "1", sys.argv[5]
src = {
    "SuneerDesignStyle.conf": os.path.join(profile, "Scribus", "SuneerDesignStyle.conf"),
    "ParagraphStyleShortcuts.conf": os.path.join(profile, "Scribus", "ParagraphStyleShortcuts.conf"),
    "suneer-text-gradients.json": os.path.join(profile, "scribus", "suneer-text-gradients.json"),
}
manifest_path = os.path.join(out, "office-defaults.json")
old = json.load(open(manifest_path, encoding="utf-8")) if os.path.isfile(manifest_path) else {"files": {}}
files = {}
lines = []
for name, path in src.items():
    if not os.path.isfile(path):
        if name in old.get("files", {}) and os.path.isfile(os.path.join(out, name)):
            files[name] = old["files"][name]          # keep what is shipped already
            lines.append("%s: not on this laptop, shipped copy kept" % name)
        else:
            lines.append("%s: not on this laptop, not shipped" % name)
        continue
    data = open(path, "rb").read()
    if name == "SuneerDesignStyle.conf":
        text = data.decode("utf-8")
        # icons by file name only: the package's design-icons folder serves every PC
        text = re.sub(r"(?m)^(\d+\\iconPath=).*[/\\]([^/\\]+)$", r"\1\2", text)
        data = text.encode("utf-8")
    sha = hashlib.sha256(data).hexdigest()
    prev = list(old.get("files", {}).get(name, {}).get("previous", []))
    old_sha = old.get("files", {}).get(name, {}).get("sha256", "")
    if old_sha and old_sha != sha and old_sha not in prev:
        prev.append(old_sha)
    files[name] = {"sha256": sha, "previous": prev, "size": len(data)}
    dest = os.path.join(out, name)
    cur = open(dest, "rb").read() if os.path.isfile(dest) else None
    if cur == data:
        lines.append("%s: unchanged" % name)
    else:
        lines.append("%s: %s (%d bytes)" % (name, "new" if cur is None else "changed", len(data)))
        if not check:
            open(dest, "wb").write(data)
manifest = {"_comment": "Office default settings shipped with Scribus; written by tools/update-office-defaults.sh - do not edit by hand.",
            "version": version or old.get("version", ""), "date": datetime.date.today().isoformat(), "files": files}
if not check:
    json.dump(manifest, open(manifest_path, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
    open(manifest_path, "a").write("\n")
changed = [l for l in lines if "unchanged" not in l and "kept" not in l and "not shipped" not in l]
head = "office defaults: " + (", ".join(l.split(":")[0] for l in changed) + " updated" if changed else "unchanged")
print(head)
for l in lines:
    print("   " + l)
if summary:
    open(summary, "w", encoding="utf-8").write(head + "\n" + "\n".join(lines) + "\n")
PY
cd "$ROOT"
if ! git -c core.fileMode=false diff --quiet -- resources/office-defaults || [ -n "$(git status --porcelain --untracked-files=all -- resources/office-defaults)" ]; then
	echo "   diff against the committed office defaults:"
	git -c core.fileMode=false --no-pager diff --stat -- resources/office-defaults | sed 's/^/   /' || true
	git -c core.fileMode=false --no-pager diff -- resources/office-defaults | grep -E '^[-+][^-+]' | grep -v 'sha256\|"date"\|"size"' | head -60 | sed 's/^/   /' || true
	git status --porcelain --untracked-files=all -- resources/office-defaults | grep '^??' | sed 's/^/   new: /' || true
fi
