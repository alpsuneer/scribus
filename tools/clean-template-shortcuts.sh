#!/bin/bash
# Remove, from the newspaper templates, every style shortcut that collides with
# a key Scribus itself uses. Scribus actions win; the style keeps its name and
# every other attribute, byte for byte.
#
#   tools/clean-template-shortcuts.sh [--dry-run | --apply] [options]
#
#   --dry-run          (default) scan and report only; nothing is written
#   --apply            back up the template folder, then remove the colliding keys
#   --templates DIR    template folder (default: Preferences > Paths > Templates
#                      from ~/.config/scribus/scribus172.rc, else /home/s1/Desktop/F/template)
#   --keys-json FILE   keys dumped from a running Scribus (the registry's view:
#                      every menu action and custom QShortcut with its key).
#                      Without it, the keys come from the files listed below.
#   --also-drop 'STYLE=KEY'  remove KEY from STYLE even though Scribus does not use
#                      it (a duplicate inside the template where another style keeps
#                      the key). Repeatable. Defaults: 'churukkam=Meta+1', '14 M (2)=Meta+1'
#                      ("14 M" keeps Meta+1). --no-default-drops clears the defaults.
#   --all-meta         treat EVERY Meta+<key> as reserved (default: only the
#                      combinations the desktop / fcitx5 really grab, read from gsettings
#                      and ~/.config/fcitx5/config)
#   --backup DIR       (apply) backup folder, default <templates>-backup-<YYYYMMDD>
#   --report FILE      also write the report there
#
# Keys Scribus uses are collected from:
#   ~/.config/scribus/scribus172.rc            <Shortcut Action= KeySequence=>  (the live set)
#   ~/.config/scribus/shortcut-sets/*.xml      the user's saved sets (the Default set)
#   resources/keysets/malayalam-dtp.xml        the Newspaper Default keyset (repo and installed copy)
#   ~/.config/Scribus/SuneerColumnConfig.conf  Column Style config keys
#   ~/.config/Scribus/SuneerDesignStyle.conf   Design Style keys
#   ~/.config/Scribus/ParagraphStyleShortcuts.conf  Next Style Chain keys
#   --keys-json                                 anything a running Scribus has (Fit Caption Frame etc.)
#   gsettings org.cinnamon.desktop.keybindings.* / org.gnome.*   keys the desktop grabs
#   ~/.config/fcitx5/config [Hotkey]           keys fcitx5 grabs
#   Policy: Scribus actions win; the headline/advert/obit Meta keys stay unless the
#   desktop really grabs that exact combination.
#
# A template with a lock file (.<name>.lock) from another PC, or from a live
# process on this PC, is skipped and reported. Keys inside one template that two
# of its own styles share are reported separately and never removed.
set -euo pipefail
MODE=dry; TEMPLATES=""; KEYS_JSON=""; META=0; BACKUP=""; REPORT=""; DROPS=$'churukkam=Meta+1\n14 M (2)=Meta+1'; 
while [ $# -gt 0 ]; do
	case "$1" in
		--dry-run) MODE=dry ;;
		--apply) MODE=apply ;;
		--templates) shift; TEMPLATES="${1:?}" ;;
		--keys-json) shift; KEYS_JSON="${1:?}" ;;
		--all-meta) META=1 ;;
		--also-drop) shift; DROPS="${DROPS}"$'\n'"${1:?}" ;;
		--no-default-drops) DROPS="" ;;
		--backup) shift; BACKUP="${1:?}" ;;
		--report) shift; REPORT="${1:?}" ;;
		-h|--help) sed -n '2,32p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
	shift
done
ROOT=$(cd "$(dirname "$0")/.." && pwd)
export MODE TEMPLATES KEYS_JSON META BACKUP REPORT ROOT DROPS
exec python3 - <<'PY'
import os, re, sys, glob, json, shutil, datetime, socket, getpass, urllib.parse
from xml.etree import ElementTree as ET

import subprocess
mode=os.environ["MODE"]; meta=os.environ["META"]=="1"; root=os.environ["ROOT"]
drops=set()
for line in os.environ.get("DROPS","").split("\n"):
    if "=" in line:
        st,k=line.rsplit("=",1); drops.add((st.strip(), k.strip()))
home=os.path.expanduser("~")
rc=os.path.join(home,".config/scribus/scribus172.rc")

# ---- template folder -----------------------------------------------------------
tpl=os.environ["TEMPLATES"]
if not tpl and os.path.exists(rc):
    m=re.search(r'<Paths [^>]*Templates="([^"]*)"', open(rc,encoding="utf-8").read())
    if m and m.group(1): tpl=m.group(1)
if not tpl: tpl="/home/s1/Desktop/F/template"
tpl=os.path.abspath(tpl)
if not os.path.isdir(tpl): sys.exit("template folder not found: "+tpl)

# ---- key normalisation: order-independent modifiers -----------------------------
MODS=("Meta","Ctrl","Alt","Shift")
def norm(k):
    k=k.strip()
    if not k: return ""
    parts=k.split("+")
    if k.endswith("+") : parts=parts[:-1]+["+"]      # "Ctrl++" is Ctrl and plus
    key=parts[-1]; mods=set(p for p in parts[:-1])
    mods={ {"Control":"Ctrl","Super":"Meta","Win":"Meta"}.get(m,m) for m in mods }
    return "+".join([m for m in MODS if m in mods]+[key.upper() if len(key)==1 else key])

# ---- keys Scribus uses ---------------------------------------------------------
uses={}   # norm key -> set(labels)
def add(k,label):
    n=norm(k)
    if n: uses.setdefault(n,set()).add(label)
if os.path.exists(rc):
    for m in re.finditer(r'<Shortcut Action="([^"]*)" KeySequence="([^"]+)"', open(rc,encoding="utf-8").read()):
        add(m.group(2), "Menu action "+m.group(1)+" (scribus172.rc, live set)")
for f in glob.glob(os.path.join(home,".config/scribus/shortcut-sets/*.xml")):
    for m in re.finditer(r'<function name="([^"]*)" shortcut="([^"]+)"', open(f,encoding="utf-8").read()):
        add(m.group(2), "Menu action "+m.group(1)+" (shortcut set "+os.path.basename(f)+")")
for f in [x for x in (os.path.join(root,"resources/keysets/malayalam-dtp.xml"), "/usr/local/share/scribus/keysets/malayalam-dtp.xml") if os.path.exists(x)][:1]:
    if True:
        for m in re.finditer(r'<function name="([^"]*)" shortcut="([^"]+)"', open(f,encoding="utf-8").read()):
            add(m.group(2), "Menu action "+m.group(1)+" (Newspaper Default keyset)")
def conf_array(path, label):
    if not os.path.exists(path): return
    names={}
    for line in open(path,encoding="utf-8"):
        m=re.match(r'(\d+)\\(tooltip|name)=(.*)',line.strip())
        if m: names[m.group(1)]=m.group(3)
    for line in open(path,encoding="utf-8"):
        m=re.match(r'(\d+)\\shortcut=(.+)',line.strip())
        if m: add(m.group(2), label % (names.get(m.group(1), "Config "+m.group(1))))
conf_array(os.path.join(home,".config/Scribus/SuneerColumnConfig.conf"), "Column Style config %s")
conf_array(os.path.join(home,".config/Scribus/SuneerDesignStyle.conf"), "Design Style %s")
p=os.path.join(home,".config/Scribus/ParagraphStyleShortcuts.conf")
if os.path.exists(p):
    for line in open(p,encoding="utf-8"):
        m=re.match(r'([^=\[]+)=(.+)',line.strip())
        if m: add(m.group(2), "Next Style Chain of '%s'" % urllib.parse.unquote(m.group(1)))
kj=os.environ["KEYS_JSON"]
if kj:
    for k,labels in json.load(open(kj,encoding="utf-8")).items():
        for l in labels: add(k, l+" (running Scribus)")

# ---- keys the desktop / fcitx5 really grab -------------------------------------------
GTKMOD={"control":"Ctrl","primary":"Ctrl","ctrl":"Ctrl","shift":"Shift","alt":"Alt","mod1":"Alt","super":"Meta","mod4":"Meta","meta":"Meta"}
GTKKEY={"space":"Space","equal":"=","minus":"-","plus":"+","comma":",","period":".","slash":"/","backslash":"\\","grave":"`",
        "Return":"Return","Escape":"Esc","BackSpace":"Backspace","Tab":"Tab","Delete":"Del","Print":"Print","Up":"Up","Down":"Down",
        "Left":"Left","Right":"Right","Home":"Home","End":"End","Page_Up":"PgUp","Page_Down":"PgDown","Insert":"Ins","Pause":"Pause","Menu":"Menu"}
def gtk_to_norm(accel):
    mods=[]; key=accel
    for m in re.findall(r'<([A-Za-z0-9_]+)>', accel):
        if m.lower() not in GTKMOD: return ""
        mods.append(GTKMOD[m.lower()])
    key=re.sub(r'<[^>]+>','',accel).strip()
    if not key or key.startswith("XF86") or key.endswith("_L") or key.endswith("_R"): return ""
    key=GTKKEY.get(key, key.upper() if len(key)==1 else key)
    return norm("+".join(mods+[key]))
desktop=os.environ.get("XDG_CURRENT_DESKTOP","").lower()
schemas=["org.cinnamon.desktop.keybindings.wm","org.cinnamon.desktop.keybindings.media-keys","org.cinnamon.desktop.keybindings"]
if "gnome" in desktop: schemas=["org.gnome.desktop.wm.keybindings","org.gnome.settings-daemon.plugins.media-keys","org.gnome.shell.keybindings"]
if shutil.which("gsettings") and os.environ.get("DISPLAY"):
    for sch in schemas:
        try: out=subprocess.run(["gsettings","list-recursively",sch],capture_output=True,text=True,timeout=3).stdout
        except Exception: out=""
        for line in out.splitlines():
            m=re.match(r'(\S+)\s+(\S+)\s+(.*)$',line)
            if not m: continue
            for acc in re.findall(r"'([^']+)'", m.group(3)):
                if acc and acc!="disabled" and gtk_to_norm(acc): add(gtk_to_norm(acc), "Desktop shortcut %s (%s)" % (m.group(2), sch.split(".")[-1]))
fx=os.path.join(home,".config/fcitx5/config")
if os.path.exists(fx):
    sec=""
    for line in open(fx,encoding="utf-8",errors="replace"):
        line=line.strip()
        if line.startswith("["): sec=line; continue
        if not (sec=="[Hotkey]" or sec.startswith("[Hotkey/")) or "=" not in line or line.startswith("#"): continue
        k,v=line.split("=",1); v=v.strip()
        if not v or k=="ModifierOnlyKeyTimeout": continue
        parts=v.split("+")
        if parts[-1].endswith("_L") or parts[-1].endswith("_R"): continue
        n=gtk_to_norm("".join("<%s>"%p for p in parts[:-1])+parts[-1])
        if n: add(n, "fcitx5 hotkey %s" % (sec[8:-1] if sec.startswith("[Hotkey/") else k))

def collision(nk, style=None):
    """labels of what Scribus / the desktop uses nk for, plus the agreed per-style drops."""
    out=sorted(uses.get(nk,()))
    if meta and nk.startswith("Meta+"): out.append("reserved for the desktop (Meta/Super key, --all-meta)")
    if style is not None and any(st==style and norm(k)==nk for st,k in drops): out.append("duplicate inside the template; another style keeps this key (agreed drop)")
    return out

# ---- locks ---------------------------------------------------------------------
me_user=getpass.getuser(); me_host=socket.gethostname()
def live_lock(path):
    lock=os.path.join(os.path.dirname(path), "."+os.path.basename(path).lower()+".lock")
    if not os.path.exists(lock): return None
    info=dict(l.split("=",1) for l in open(lock,encoding="utf-8",errors="replace").read().split("\n") if "=" in l)
    if info.get("user")==me_user and info.get("host")==me_host:
        try:
            os.kill(int(info.get("pid","0")),0); return "live process %s on this PC" % info.get("pid")
        except ProcessLookupError: return None       # our own dead lock: harmless for reading/writing
        except Exception: return "pid unknown"
    return "%s on %s since %s" % (info.get("user","?"), info.get("host","?"), info.get("opened","?"))

# ---- scan ----------------------------------------------------------------------
STYLE_TAGS=("ParagraphStyle","CharacterStyle","CharStyle","STYLE","CHARSTYLE")
ATTR_RE=re.compile(r'\s([A-Za-z]*Shortcut)="([^"]+)"')
files=sorted(p for p in glob.glob(tpl+"/**/*.sla", recursive=True)+glob.glob(tpl+"/**/*.sla.gz", recursive=True))
by_key={}      # nk -> {file: [(tag,name,rawkey)]}
dups={}        # file -> {nk: [names]}
plan={}        # file -> list of (tag,name,attr,rawkey)
skipped={}
for f in files:
    rel=os.path.relpath(f,tpl)
    data=open(f,"rb").read()
    text=data.decode("utf-8")
    seen={}
    for m in re.finditer(r'<(\w+)\s[^>]*>', text):
        tag=m.group(1)
        if tag not in STYLE_TAGS: continue
        el=m.group(0)
        nm=re.search(r'\sName="([^"]*)"', el) or re.search(r'\sNAME="([^"]*)"', el)
        name=nm.group(1) if nm else "?"
        for a in ATTR_RE.finditer(el):
            attr,raw=a.group(1),a.group(2); nk=norm(raw)
            if not nk: continue
            seen.setdefault(nk,[]).append((tag,name))
            if collision(nk, name):
                by_key.setdefault(nk,{}).setdefault(rel,[]).append((tag,name,raw))
                plan.setdefault(f,[]).append((tag,name,attr,raw))
    for nk,owners in seen.items():
        if len(owners)>1: dups.setdefault(rel,{})[nk]=[n for _,n in owners]

# ---- report --------------------------------------------------------------------
lines=[]
P=lines.append
P("Template folder: %s   (%d .sla files)" % (tpl, len(files)))
P("Scribus/desktop keys known: %d%s%s" % (len(uses), "  (+ every Meta+* reserved)" if meta else "", "  [keys-json: %s]" % kj if kj else ""))
P("Agreed per-style drops: %s" % (", ".join("%s=%s"%d for d in sorted(drops)) or "none"))
P("")
P("A. Style shortcuts that collide with a Scribus key  ->  will be REMOVED on --apply")
P("   key -> what Scribus uses it for -> style(s) @ template(s)")
total=0
for nk in sorted(by_key, key=lambda k:(k.startswith("Meta+"), k)):
    P("")
    P("  %s" % nk)
    styles_here={name for rel,items in by_key[nk].items() for _,name,_ in items}
    labels=set()
    for st in styles_here: labels.update(collision(nk, st))
    for l in sorted(labels): P("      Scribus: %s" % l)
    files_here=by_key[nk]
    names={}
    for rel,items in files_here.items():
        for tag,name,raw in items: names.setdefault((tag,name,raw),[]).append(rel); total+=1
    for (tag,name,raw),rels in sorted(names.items()):
        P("      %s '%s' (written %s) in %d template(s): %s" % (tag,name,raw,len(rels), ", ".join(rels) if len(rels)<=4 else ", ".join(rels[:4])+", ..."))
P("")
P("   total: %d style keys in %d files" % (total, len(plan)))
P("")
P("B. Keys used by two or more styles inside the SAME template  ->  NOT removed, decide")
if not dups: P("   none")
for rel in sorted(dups):
    for nk,names in sorted(dups[rel].items()):
        P("   %s: %s -> %s" % (rel, nk, " | ".join(names)))
P("")
P("C. Templates skipped (lock from another PC or a live process)")
for f in files:
    why=live_lock(f)
    if why: skipped[f]=why; P("   %s: %s" % (os.path.relpath(f,tpl), why))
if not skipped: P("   none")
report="\n".join(lines)
print(report)
rp=os.environ["REPORT"]
if rp: open(rp,"w",encoding="utf-8").write(report+"\n")

if mode!="apply":
    print("\n(dry run: nothing written; use --apply)"); sys.exit(0)

# ---- apply ----------------------------------------------------------------------
backup=os.environ["BACKUP"] or tpl.rstrip("/")+"-backup-"+datetime.date.today().strftime("%Y%m%d")
if os.path.exists(backup): sys.exit("backup folder exists, refusing: "+backup)
shutil.copytree(tpl, backup, symlinks=True)
print("\nbacked up to", backup)
changed=[]
for f,items in sorted(plan.items()):
    if f in skipped: print("SKIP (lock)", os.path.relpath(f,tpl)); continue
    data=open(f,"rb").read(); text=data.decode("utf-8"); n=0
    def fix(m):
        global n
        el=m.group(0)
        if m.group(1) not in STYLE_TAGS: return el
        nm=re.search(r'\sName="([^"]*)"', el) or re.search(r'\sNAME="([^"]*)"', el)
        sname=nm.group(1) if nm else "?"
        def sub(a):
            global n
            if collision(norm(a.group(2)), sname): n+=1; return ""
            return a.group(0)
        return ATTR_RE.sub(sub, el)
    new=re.sub(r'<(\w+)\s[^>]*>', fix, text)
    if n:
        out=new.encode("utf-8")
        assert len(data)-len(out)==sum(len((' %s="%s"'%(a,r)).encode("utf-8")) for _,_,a,r in items), "unexpected byte difference in "+f
        st=os.stat(f); open(f,"wb").write(out); os.chmod(f, st.st_mode & 0o7777)
        changed.append((os.path.relpath(f,tpl), n))
print("changed %d files:" % len(changed))
for rel,n in changed: print("   %s  (%d key(s) removed)" % (rel,n))
PY
