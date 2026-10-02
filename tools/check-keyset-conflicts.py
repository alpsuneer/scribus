#!/usr/bin/env python3
"""Check a Scribus shortcut-set file for keys that would not work.

  tools/check-keyset-conflicts.py KEYSET.xml [--profile ~/.config]

Reports, and exits 1 on any of them:
  - a key used by two or more actions inside the set (Qt fires neither);
  - a key the set gives to an action that one of our custom shortcuts also
    uses: Column Style configs, Design Styles, Next Style Chains (read from the
    QSettings files under --profile/Scribus/, the same stores the shortcut
    registry reads at run time).
Keys are compared with modifier order normalised (Meta+Ctrl+1 == Ctrl+Meta+1).
"""
import os, re, sys, urllib.parse
from xml.etree import ElementTree as ET

MODS = ("Meta", "Ctrl", "Alt", "Shift")

# Actions that exist in the compiled key table but are attached to no menu or
# toolbar in this build, so their key can never fire and cannot make another
# action ambiguous. Upstream keeps the New-from-Template plugin action out of
# every menu (actionmanager.cpp, "NewFromDocumentTemplate" commented out) and
# gives both it and fileNewFromTemplate Ctrl+Alt+N.
INERT_ACTIONS = {"NewFromDocumentTemplate"}

def norm(k):
    k = k.strip()
    if not k:
        return ""
    parts = k.split("+")
    if k.endswith("+"):
        parts = parts[:-1] + ["+"]
    key = parts[-1]
    mods = {{"Control": "Ctrl", "Super": "Meta"}.get(m, m) for m in parts[:-1]}
    return "+".join([m for m in MODS if m in mods] + [key.upper() if len(key) == 1 else key])

def main():
    args = sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    path = args[0]
    profile = os.path.expanduser("~/.config")
    if "--profile" in args:
        profile = os.path.expanduser(args[args.index("--profile") + 1])
    root = ET.parse(path).getroot()
    if root.tag != "shortcutset":
        sys.exit("%s: not a <shortcutset> file" % path)
    by_key = {}
    for f in root.findall("function"):
        nk = norm(f.get("shortcut", ""))
        if nk and f.get("name") not in INERT_ACTIONS:
            by_key.setdefault(nk, []).append(f.get("name"))
    problems = []
    for nk, names in sorted(by_key.items()):
        if len(names) > 1:
            problems.append("%-18s used by %d actions in the set: %s" % (nk, len(names), ", ".join(names)))

    custom = {}  # nk -> label
    def conf_array(file, label):
        p = os.path.join(profile, "Scribus", file)
        if not os.path.exists(p):
            return
        names = {}
        for line in open(p, encoding="utf-8"):
            m = re.match(r'(\d+)\\(tooltip|name)=(.*)', line.strip())
            if m:
                names[m.group(1)] = m.group(3)
        for line in open(p, encoding="utf-8"):
            m = re.match(r'(\d+)\\shortcut=(.+)', line.strip())
            if m and norm(m.group(2)):
                custom.setdefault(norm(m.group(2)), []).append(label % names.get(m.group(1), "Config " + m.group(1)))
    conf_array("SuneerColumnConfig.conf", "Column Style config %s")
    conf_array("SuneerDesignStyle.conf", "Design Style %s")
    p = os.path.join(profile, "Scribus", "ParagraphStyleShortcuts.conf")
    if os.path.exists(p):
        for line in open(p, encoding="utf-8"):
            m = re.match(r'([^=\[]+)=(.+)', line.strip())
            if m and norm(m.group(2)):
                custom.setdefault(norm(m.group(2)), []).append("Next Style Chain of '%s'" % urllib.parse.unquote(m.group(1)))
    for nk, labels in sorted(custom.items()):
        if nk in by_key:
            problems.append("%-18s set gives it to %s, but it is also %s" % (nk, ", ".join(by_key[nk]), "; ".join(labels)))

    name = root.get("name", "?")
    keyed = sum(1 for f in root.findall("function") if f.get("shortcut"))
    print("%s: set \"%s\", %d actions, %d with a key, %d custom shortcut keys checked" % (os.path.basename(path), name, len(root.findall("function")), keyed, len(custom)))
    if problems:
        print("CONFLICTS (%d):" % len(problems))
        for pr in problems:
            print("   " + pr)
        sys.exit(1)
    print("no conflicts")

if __name__ == "__main__":
    main()
