#!/bin/bash
# "Process obituary photos" on a REAL page downloaded from the workflow (a copy; the original is never
# touched): SUNEER_OBITWF_SELFTEST phase 3 in fixture mode (no server: the page lock check allows, the
# story list is built from the page's own text frames - news.serverId, first paragraph = headline,
# the rest = body - so the obituary / news classification sees the real text).
# usage: tests/suneer/obit_wf_realfile.sh <page.sla> <work dir> [scribus]
set -u
SLA=${1:?page.sla}; W=${2:?work dir}; SCRIBUS=${3:-/usr/local/bin/scribus}
mkdir -p "$W/out" "$W/home" "$W/run" "$W/page"; W="$(cd "$W" && pwd)"; chmod 700 "$W/run"
rm -rf "$W/out"/* "$W/page"/* "$W/home/.cache" "$W/home/scribus-crashlogs" "$W/home/.config"
base=$(basename "$SLA" .sla)
cp "$SLA" "$W/page/$base.sla"
python3 - "$W/page/$base.sla" "$W/fixture.json" <<'PY'
import sys, re, json, html
s = open(sys.argv[1], encoding="utf-8").read()
stories = {}
for m in re.finditer(r'<PageObject\b([^>]*ItemType="4"[^>]*)>(.*?)</PageObject>', s, re.S):
    body = m.group(2)
    sid = re.findall(r'Name="news.serverId"[^>]*Value="([^"]*)"', body)
    role = re.findall(r'Name="news.role"[^>]*Value="([^"]*)"', body)
    pid = re.findall(r'Name="news.pageId"[^>]*Value="([^"]*)"', body)
    if not sid or (role and role[0] != "text"):
        continue
    st = re.search(r'<StoryText>(.*?)</StoryText>', body, re.S)
    if not st:
        continue
    paras, cur = [], ""
    # the workflow writes <ITEXT CH=...>, Scribus 1.7 saves <Content Chars=...>
    for t in re.finditer(r'<(?:ITEXT\b[^>]*CH|Content\b[^>]*Chars)="([^"]*)"|<para\b|<breakline\b|<trail\b', st.group(1)):
        if t.group(1) is not None:
            cur += html.unescape(t.group(1))
        else:
            paras.append(cur); cur = ""
    if cur:
        paras.append(cur)
    paras = [p.replace("­", "") for p in paras if p.strip()]
    if not paras or sid[0] in stories:
        continue
    stories[sid[0]] = {"id": sid[0], "refId": sid[0][-8:], "title": paras[0], "content": "".join("<p>%s</p>" % p for p in paras[1:]),
                       "newsMedia": [], "page": {"id": pid[0] if pid else "", "name": "page", "dtpStatus": "USED"}}
json.dump({"data": list(stories.values())}, open(sys.argv[2], "w"), ensure_ascii=False)
print("fixture: %d stories from the page's text frames" % len(stories))
PY
DISP=:93
Xvfb $DISP -screen 0 1600x1000x24 -nolisten tcp >/dev/null 2>&1 & XVFB=$!
trap 'kill $XVFB 2>/dev/null' EXIT
sleep 1
HOME="$W/home" XDG_CONFIG_HOME="$W/home/.config" XDG_CACHE_HOME="$W/home/.cache" XDG_RUNTIME_DIR="$W/run" DISPLAY=$DISP QT_IM_MODULE= \
U2NET_HOME=/home/s1/.u2net SCRIBUS_NEWS_FIXTURE="$W/fixture.json" SUNEER_OBITWF_SELFTEST="$W/out/r" SUNEER_OBITWF_PHASE=3 \
	timeout 900 "$SCRIBUS" "$W/page/$base.sla" > "$W/out/scribus.log" 2>&1
echo "scribus exit $?" | tee "$W/out/exit.txt"
cat "$W/out/r-report.txt" 2>/dev/null || echo "FAIL no report"
grep -h "obit photos:" "$W/home/scribus-crashlogs/news-sync.log" > "$W/out/obit-photos-log.txt" 2>/dev/null
echo "log lines: $(wc -l < "$W/out/obit-photos-log.txt")  ($W/out/obit-photos-log.txt)"
