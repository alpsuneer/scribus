#!/usr/bin/env python3
"""Copy the release laptop's "office" PDF export presets into the package.

  tools/update-office-pdf-presets.py [--profile ~/.config/scribus]
                                     [--dest resources/pdf-presets] [--summary FILE]

Reads <profile>/pdf-presets/*.json (written by File > Export > Save as PDF) and
copies every preset whose "Office preset" box is ticked into --dest, which the
package installs read-only under share/scribus/pdf-presets/.

  - Passwords are blanked in the copy: this folder is committed to a public
    repository and installed on every office PC.
  - A preset that is no longer marked is removed from --dest.
  - If the laptop's Default preset is one of the office presets it becomes the
    office Default (office-default.txt); otherwise no office Default is shipped.

Prints what changed; with --summary also writes a one-line summary plus detail
for the release commit message. Exit status 0 even when nothing is marked.
"""
import json, os, sys, urllib.parse

FORMAT = "scribus-pdf-preset"

def file_name_for(name):
    # must match PdfPresets::fileNameFor()
    return urllib.parse.quote(name, safe=" ()+,-=@[]{}") + ".json"

def main():
    args = sys.argv[1:]
    profile = os.path.expanduser("~/.config/scribus")
    dest = "resources/pdf-presets"
    summary = None
    while args:
        a = args.pop(0)
        if a == "--profile": profile = args.pop(0)
        elif a == "--dest": dest = args.pop(0)
        elif a == "--summary": summary = args.pop(0)
        else: sys.exit(__doc__)
    src = os.path.join(profile, "pdf-presets")
    os.makedirs(dest, exist_ok=True)

    office = {}
    if os.path.isdir(src):
        for f in sorted(os.listdir(src)):
            if not f.endswith(".json"):
                continue
            try:
                j = json.load(open(os.path.join(src, f), encoding="utf-8"))
            except (OSError, ValueError):
                print("skipped unreadable %s" % f)
                continue
            if j.get("format") != FORMAT or not j.get("name") or not j.get("office"):
                continue
            had_password = bool(j.get("passOwner") or j.get("passUser"))
            j["passOwner"] = ""
            j["passUser"] = ""
            office[j["name"]] = (j, had_password)

    default = ""
    default_file = os.path.join(src, "default.txt")
    if os.path.exists(default_file):
        default = open(default_file, encoding="utf-8").read().strip()

    lines = []
    wanted = set()
    for name, (j, had_password) in sorted(office.items()):
        target = os.path.join(dest, file_name_for(name))
        wanted.add(os.path.basename(target))
        text = json.dumps(j, indent=4, ensure_ascii=False, sort_keys=True) + "\n"
        old = open(target, encoding="utf-8").read() if os.path.exists(target) else None
        if old != text:
            open(target, "w", encoding="utf-8").write(text)
            lines.append(("updated" if old is not None else "added") + ": " + name + (" (passwords left out)" if had_password else ""))
        elif had_password:
            print("unchanged: %s (passwords left out)" % name)
    for f in sorted(os.listdir(dest)):
        if f.endswith(".json") and f not in wanted:
            os.remove(os.path.join(dest, f))
            lines.append("removed: " + f[:-5])

    default_target = os.path.join(dest, "office-default.txt")
    old_default = open(default_target, encoding="utf-8").read().strip() if os.path.exists(default_target) else ""
    new_default = default if default in office else ""
    if new_default != old_default:
        if new_default:
            open(default_target, "w", encoding="utf-8").write(new_default + "\n")
            lines.append("office Default: " + new_default)
        else:
            os.remove(default_target)
            lines.append("office Default: none")
    if default and default not in office:
        print('note: the laptop Default "%s" is not marked as an office preset, so no office Default is shipped' % default)

    head = "%d office PDF preset(s)%s" % (len(office), (', Default "%s"' % new_default) if new_default else ", no office Default")
    print(head)
    for l in lines:
        print("  " + l)
    if not lines:
        print("  unchanged")
    if summary:
        open(summary, "w", encoding="utf-8").write(head + "\n" + "\n".join(lines) + ("\n" if lines else ""))

if __name__ == "__main__":
    main()
