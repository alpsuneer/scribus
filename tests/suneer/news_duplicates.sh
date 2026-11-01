#!/bin/bash
# Duplicate news / photos (ui/suneer_news_dup.cpp, suneerphotohash.cpp): SUNEER_DUP_SELFTEST in fixture
# mode on a REAL obituary page (a copy of <page.sla> with its _photos folder) and a list built here from
# its stories plus planted duplicates:
#   dup-1  the obituary of one page story again: new id, the same text with soft hyphens, ZWNJ, spaces
#          and punctuation changed; its photo resized 60 %, JPEG quality 50, cropped 4 %, renamed
#   dup-1b the same obituary a third time (no photo): the drag-and-drop warning
#   dup-2  a story of its own whose photo is another page photo resized 45 % and renamed
#   dup-3 / dup-4  a new pair: a page obituary with another name and age (must NOT match that obituary:
#          the ages differ), and the same with a few words changed (must match each other)
#   f-1 .. f-120  filler stories from the page's words (speed, false positives)
# usage: tests/suneer/news_duplicates.sh <page.sla> <work dir> [scribus]
# Modes (env), list = the page's own stories only, all USED, nothing planted; only the DUP count is checked:
#   NEWS_DUP_PLAIN=1             frames linked: no DUP
#   NEWS_DUP_PLAIN=1 NEWS_DUP_UNLINK=1   news.* removed from the frames (a page before the sync): no DUP
#   NEWS_DUP_PLAIN=1 NEWS_DUP_TWICE=1    one story's text frame copied, same id: DUP on that story only
set -u
SLA=${1:?page.sla}; W=${2:?work dir}; SCRIBUS=${3:-/usr/local/bin/scribus}
mkdir -p "$W/out" "$W/home" "$W/run" "$W/page" "$W/fix"; W="$(cd "$W" && pwd)"; chmod 700 "$W/run"
rm -rf "$W/out"/* "$W/page"/* "$W/fix"/* "$W/home/.cache" "$W/home/.config" "$W/home/scribus-crashlogs"
base=$(basename "$SLA" .sla); dir=$(dirname "$SLA")
cp "$SLA" "$W/page/$base.sla"
[ -d "$dir/${base}_photos" ] && cp -r "$dir/${base}_photos" "$W/page/"
OUT=$(python3 - "$W/page/$base.sla" "$W/page/${base}_photos" "$W/fix" <<'PY'
import sys, re, json, html, random, subprocess, os
sla, photos, fix = sys.argv[1:4]
s = open(sla, encoding="utf-8").read()
def attrs(body):
    return dict(re.findall(r'<ItemAttribute Name="([^"]*)"[^>]*Value="([^"]*)"', body))
stories, photo = {}, {}
for m in re.finditer(r'<PageObject\b([^>]*)>(.*?)</PageObject>', s, re.S):
    head, body = m.group(1), m.group(2)
    a = attrs(body)
    sid = a.get("news.serverId")
    if not sid:
        continue
    if 'ItemType="2"' in head:
        src = a.get("obit.src") or a.get("news.original") or ""
        if src:
            photo[sid] = os.path.join(photos, "original", os.path.basename(src))
        continue
    if a.get("news.role", "text") != "text":
        continue
    st = re.search(r'<StoryText>(.*?)</StoryText>', body, re.S)
    if not st:
        continue
    paras, cur = [], ""
    for t in re.finditer(r'<(?:ITEXT\b[^>]*CH|Content\b[^>]*Chars)="([^"]*)"|<para\b|<breakline\b|<trail\b', st.group(1)):
        if t.group(1) is not None:
            cur += html.unescape(t.group(1))
        else:
            paras.append(cur); cur = ""
    if cur: paras.append(cur)
    paras = [p.replace("­", "") for p in paras if p.strip()]
    if paras and sid not in stories:
        stories[sid] = (paras[0], paras[1:])
def news(sid, title, body, ph=None, used=True):
    n = {"id": sid, "refId": sid[-8:], "title": title, "content": "".join("<p>%s</p>" % p for p in body),
         "newsMedia": [], "page": {"id": "", "name": "page", "dtpStatus": "USED" if used else "UNUSED"}}
    if ph and os.path.isfile(ph):
        n["newsMedia"] = [{"media": {"fileUrl": ph}}]
    return n
data = [news(k, t, b, photo.get(k)) for k, (t, b) in stories.items()]
if os.environ.get("NEWS_DUP_PLAIN"):
    twice = ""
    if os.environ.get("NEWS_DUP_TWICE"):
        for m in re.finditer(r'<PageObject\b[^>]*>.*?</PageObject>', s, re.S):
            a = attrs(m.group(0))
            if a.get("news.role") == "text" and a.get("news.serverId"):
                twice = a["news.serverId"]
                s = s[:m.end()] + "\n" + m.group(0) + s[m.end():]
                break
    if os.environ.get("NEWS_DUP_UNLINK"):
        s = re.sub(r'<ItemAttribute Name="news\.[^>]*/>\s*', '', s)
    open(sla, "w", encoding="utf-8").write(s)
    json.dump({"data": data}, open(fix + "/fixture.json", "w"), ensure_ascii=False)
    print("ORIG=")
    print("EXPECT=%s" % twice)
    print("INFO=%d page stories, plain list %d" % (len(stories), len(data)))
    sys.exit(0)
withPhoto = [k for k in stories if k in photo and os.path.isfile(photo[k]) and len("".join(stories[k][1])) > 120]
X, Y = withPhoto[0], withPhoto[1]
aged = [k for k in stories if k not in (X, Y) and re.search(r"\((\d{1,3})\)", " ".join(stories[k][1])) and len("".join(stories[k][1])) > 150]
Z = aged[0]
# dup-1: X again, typed differently; its photo resized + recompressed + cropped + renamed
tX, bX = stories[X]
def retype(p):
    p = p.replace("‌", "").replace(" ", "  ").replace(".", " .")
    return "".join(c + ("­" if i % 7 == 3 else "") for i, c in enumerate(p))
subprocess.run(["convert", photo[X], "-resize", "60%", "-gravity", "center", "-crop", "92%x92%+0+0", "+repage", "-quality", "50", fix + "/dup_x_photo.jpg"], check=True)
data.append(news("dup-1", retype(tX), [retype(p) for p in bX], fix + "/dup_x_photo.jpg", used=False))
# dup-1b: the same obituary a third time (for the drag-and-drop warning), no photo
data.append(news("dup-1b", tX, [p.replace(" ", "  ") for p in bX], used=False))
# dup-2: a story of its own, the photo of Y resized and renamed
subprocess.run(["convert", photo[Y], "-resize", "45%", "-quality", "70", fix + "/dup_y_photo.jpg"], check=True)
data.append(news("dup-2", "പഞ്ചായത്ത് ഓഫീസിൽ പുതിയ കെട്ടിടം ഉദ്ഘാടനം ചെയ്തു", ["നാട്ടുകാരുടെ ഏറെക്കാലത്തെ ആവശ്യമായിരുന്ന പഞ്ചായത്ത് ഓഫീസ് കെട്ടിടം മന്ത്രി ഉദ്ഘാടനം ചെയ്തു. ചടങ്ങിൽ പ്രസിഡന്റ് അധ്യക്ഷനായി. നിരവധി പേർ പങ്കെടുത്തു."], fix + "/dup_y_photo.jpg", used=False))
# dup-3 / dup-4: Z with another name and age, and that again with a few words changed
tZ, bZ = stories[Z]
age = re.search(r"\((\d{1,3})\)", " ".join(bZ)).group(1)
newAge = str(int(age) + 9 if int(age) < 90 else int(age) - 9)
def rename(p):
    return p.replace(tZ.strip(), "രാജൻ").replace("(%s)" % age, "(%s)" % newAge)
b3 = [rename(p) for p in bZ]
data.append(news("dup-3", "രാജൻ", b3, used=False))
b4 = [p.replace("സംസ്‌കാരം", "സംസ്കാരം ചടങ്ങ്").replace(".", ". ") + " ഭാര്യ: ശാന്ത." for p in b3]
data.append(news("dup-4", "രാജൻ", b4, used=False))
# 120 fillers from the page's words
random.seed(7)
words = [w for t, b in stories.values() for p in b for w in p.split() if len(w) > 2]
for i in range(1, 121):
    random.shuffle(words)
    data.append(news("f-%d" % i, " ".join(words[:3]), [" ".join(random.sample(words, 45))], used=False))
json.dump({"data": data}, open(fix + "/fixture.json", "w"), ensure_ascii=False)
print("ORIG=%s" % tX.strip())
print("EXPECT=dup-1,dup-1b,dup-2,dup-3,dup-4,%s,%s" % (X, Y))   # the originals get DUP too ("Same as: <the copy>"), never hidden
print("INFO=%d page stories, X=%s Y=%s Z=%s (age %s->%s), list %d" % (len(stories), X[-6:], Y[-6:], Z[-6:], age, newAge, len(data)))
PY
) || exit 1
echo "$OUT" | grep INFO
ORIG=$(echo "$OUT" | sed -n 's/^ORIG=//p'); EXPECT=$(echo "$OUT" | sed -n 's/^EXPECT=//p')
Xvfb :93 -screen 0 1600x1000x24 -nolisten tcp >/dev/null 2>&1 & XVFB=$!
trap 'kill $XVFB 2>/dev/null' EXIT
sleep 1
rm -f "$W/page/.$base.sla.lock"
HOME="$W/home" XDG_CONFIG_HOME="$W/home/.config" XDG_CACHE_HOME="$W/home/.cache" XDG_RUNTIME_DIR="$W/run" DISPLAY=:93 QT_IM_MODULE= \
SCRIBUS_NEWS_FIXTURE="$W/fix/fixture.json" SUNEER_DUP_SELFTEST="$W/out/d" SUNEER_DUP_EXPECT="$EXPECT" SUNEER_DUP_ORIG="$ORIG" \
SUNEER_DUP_COUNT_ONLY="${NEWS_DUP_PLAIN:-}" timeout "${NEWS_DUP_TIMEOUT:-600}" "$SCRIBUS" "$W/page/$base.sla" > "$W/out/scribus.log" 2>&1
echo "scribus exit $?" | tee "$W/out/exit.txt"
cat "$W/out/d-report.txt" 2>/dev/null || echo "FAIL no report"
grep -ah "duplicates:" "$W"/home/.cache/scribus*/news/api.log 2>/dev/null | tail -2
