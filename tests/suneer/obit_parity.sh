#!/bin/bash
# Obituary auto-layout parity: the same stories (a real obituary page's list, fixture mode) placed with
# "Place selected" on a blank page (A) and opened from the workflow (B: the page the workflow exported,
# its frames linked to the stories as "Open page from workflow" leaves them) must give the Layout
# Gallery the same page: the obituary page checkbox, the "N obits + M news · fits" line, the Obituary
# card, and Apply with 0 mm difference per story. SUNEER_OBITPAR_SELFTEST drives both runs
# (SuneerNewsPanel::runObitParitySelfTest); compare_obit_pages.py measures the two results.
#
# usage: tests/suneer/obit_parity.sh <work dir> [scribus]
#   needs /home/s1/tmp-obit (prefs, home, obit_test.sla), the stories fixture and the workflow page
#   (OBITPAR_FIXTURE, OBITPAR_WFSLA below), faces in /home/s1/tmp-obit/placed_face1_photos/original
set -u
W=${1:?work dir}; SCRIBUS=${2:-/usr/local/bin/scribus}; HERE="$(cd "$(dirname "$0")" && pwd)"
FIXTURE=${OBITPAR_FIXTURE:-/home/s1/tmp-obitreal/fixture.json}
WFSLA=${OBITPAR_WFSLA:-/home/s1/WorkflowPages/KNR/2026-10-17/obituary.sla}
FACES=${OBITPAR_FACES:-/home/s1/tmp-obit/placed_face1_photos/original}
PORT=${OBITPAR_PORT:-18749}
mkdir -p "$W/A" "$W/B" "$W/photos"; W="$(cd "$W" && pwd)"; rm -f "$W"/photos/*
rm -rf "$W"/A/* "$W"/B/*
# profile: the obituary harness prefs + the user's current layout models (the one settings source)
rm -rf "$W/home" "$W/prefs"; cp -r /home/s1/tmp-obit/home "$W/home"; cp -r /home/s1/tmp-obit/prefs "$W/prefs"
mkdir -p "$W/prefs/layout-models"; cp /home/s1/.config/scribus/layout-models/*.json "$W/prefs/layout-models/" 2>/dev/null
rm -f "$W/home/.cache/scribus/news/api.log"
python3 - "$W/home/.config/Faircode/ScribusNews.conf" $PORT <<'PY'
import sys, re
p, port = sys.argv[1], sys.argv[2]
s = open(p, encoding='utf-8').read()
s = re.sub(r'(?m)^apiHost=.*$', 'apiHost=http://127.0.0.1:%s' % port, s)
s = re.sub(r'(?m)^filesBase=.*$', 'filesBase=http://127.0.0.1:%s' % port, s)
s = re.sub(r'(?m)^Fixture%20page=.*$', 'Fixture%20page=obituary', s)
open(p, 'w', encoding='utf-8').write(s)
PY
# the stories: the page's list made from the exported page itself (the same text, the same pictures), the
# ids of the stored fixture: title = the first paragraph, content = the rest; a 2-column frame = workflow
# count 2; the inline picture of each story's photo frame served as its media
python3 - "$FIXTURE" "$WFSLA" "$W/fixture.json" "$W/photos" <<'PY'
import sys, json, base64, zlib, os
import xml.etree.ElementTree as ET
src, wfsla, dst, photos = sys.argv[1:5]
j = json.load(open(src, encoding='utf-8'))
arr = j['data'] if isinstance(j, dict) else j
by = {n['id']: n for n in arr}
doc = ET.parse(wfsla).getroot().find('Document')
texts, pics = {}, {}
for it in doc.findall('PageObject'):
    at = {ia.get('Name'): ia.get('Value') for ia in it.findall('PageItemAttributes/ItemAttribute')}
    sid = at.get('news.serverId')
    if not sid:
        continue
    if at.get('news.role') == 'text':
        paras, cur = [], ''
        for c in it.find('StoryText'):
            if c.tag == 'Content':
                cur += (c.get('Chars') or '').replace('\u00ad', '')
            elif c.tag in ('para', 'trail'):
                paras.append(cur); cur = ''
        if cur:
            paras.append(cur)
        texts[sid] = (paras, it.get('Columns'))
    elif at.get('news.role') == 'photo':
        if it.get('IsInlineImage') == '1':
            pics[sid] = ('png', zlib.decompress(base64.b64decode(it.get('ImageData'))[4:]))
        elif it.get('ImageFileName'):
            f = os.path.join(os.path.dirname(wfsla), it.get('ImageFileName'))
            if os.path.isfile(f):
                pics[sid] = (os.path.splitext(f)[1].lstrip('.').lower() or 'jpg', open(f, 'rb').read())
# the loose photo of the 2-column news: the image frame inside its text frame
loose = [it for it in doc.findall('PageObject') if it.get('ItemType') == '2' and not any(ia.get('Name') == 'news.serverId' for ia in it.findall('PageItemAttributes/ItemAttribute'))]
for it in doc.findall('PageObject'):
    at = {ia.get('Name'): ia.get('Value') for ia in it.findall('PageItemAttributes/ItemAttribute')}
    sid = at.get('news.serverId')
    if sid and at.get('news.role') == 'text' and sid not in pics:
        x, y, w, h = (float(it.get(k)) for k in ('XPosition', 'YPosition', 'Width', 'Height'))
        for im in loose:
            ix, iy = float(im.get('XPosition')), float(im.get('YPosition'))
            if x - 3 <= ix <= x + w and y - 3 <= iy <= y + h:
                if im.get('IsInlineImage') == '1':
                    pics[sid] = ('png', zlib.decompress(base64.b64decode(im.get('ImageData'))[4:]))
                elif im.get('ImageFileName') and os.path.isfile(os.path.join(os.path.dirname(wfsla), im.get('ImageFileName'))):
                    f = os.path.join(os.path.dirname(wfsla), im.get('ImageFileName'))
                    pics[sid] = (os.path.splitext(f)[1].lstrip('.').lower().replace('jpeg', 'jpg') or 'jpg', open(f, 'rb').read())
out, withPic = [], 0
for n in arr:
    sid = n['id']
    if sid not in texts:
        continue
    paras, cols = texts[sid]
    n['title'] = paras[0] if paras else n.get('title', '')
    n['content'] = '\n'.join(p for p in paras[1:] if p.strip())
    if cols == '2':
        n['noOfColumns'] = 2
    if isinstance(n.get('page'), dict):
        n['page']['dtpStatus'] = 'UNUSED'
    n.pop('newsPages', None); n.pop('otherPages', None)
    n['newsMedia'] = []
    if sid in pics:
        ext, data = pics[sid]
        f = sid + '.' + ext
        open(os.path.join(photos, f), 'wb').write(data); withPic += 1
        n['newsMedia'] = [{"media": {"id": "m-" + sid, "caption": n['title'], "fileName": f, "originalName": f, "fileType": "image/" + ('jpeg' if ext == 'jpg' else ext), "fileUrl": "/" + f}}]
    out.append(n)
json.dump({'data': out}, open(dst, 'w', encoding='utf-8'), ensure_ascii=False)
print('fixture: %d stories from the page text, %d with its picture' % (len(out), withPic))
PY
fuser -k -n tcp $PORT >/dev/null 2>&1
(cd "$W/photos" && python3 -m http.server $PORT --bind 127.0.0.1 > "$W/photos.log" 2>&1) & SRV=$!
trap 'kill $SRV 2>/dev/null' EXIT
sleep 1
run() {   # run <A|B> <mode> <sla>
	local tag=$1 mode=$2 sla=$3
	cp "$sla" "$W/$tag/page.sla"; rm -f "$W/$tag/.page.sla.lock"
	[ -d "${sla%.sla}_photos" ] && { rm -rf "$W/$tag/page_photos"; cp -r "${sla%.sla}_photos" "$W/$tag/$(basename "${sla%.sla}")_photos"; }   # the linked pictures
	if [ "$mode" = workflow ]; then
		# a story on the exported page that is not in the list cannot be placed in A: dropped from the copy
		python3 - "$W/$tag/page.sla" "$W/fixture.json" <<'PY'
import sys, json
import xml.etree.ElementTree as ET
sla, fx = sys.argv[1], sys.argv[2]
ids = set(n['id'] for n in json.load(open(fx, encoding='utf-8'))['data'])
tree = ET.parse(sla); doc = tree.getroot().find('Document')
n0 = len(doc.findall('PageObject')); dropped = 0
for it in list(doc.findall('PageObject')):
    at = {ia.get('Name'): ia.get('Value') for ia in it.findall('PageItemAttributes/ItemAttribute')}
    if at.get('news.story') and at.get('news.serverId') not in ids:
        doc.remove(it); dropped += 1
tree.write(sla, encoding='UTF-8', xml_declaration=True)
print('workflow copy: %d of %d objects kept (%d of a story not in the list dropped)' % (n0 - dropped, n0, dropped))
PY
	fi
	HOME="$W/home" SCRIBUS_NEWS_FIXTURE="$W/fixture.json" QT_IM_MODULE= SUNEER_LAYOUT_DEBUG=1 \
	SUNEER_OBITPAR_SELFTEST="$W/$tag/t" SUNEER_OBITPAR_MODE=$mode SUNEER_OBITPAR_GALLERY_S=${OBITPAR_GALLERY_S:-25} SUNEER_OBITPAR_MODEL=${OBITPAR_MODEL:-obituary} \
		timeout 900 xvfb-run -a -s '-screen 0 1600x1000x24' "$SCRIBUS" -ns -pr "$W/prefs" "$W/$tag/page.sla" > "$W/$tag/scribus.log" 2>&1
	echo "$tag ($mode) scribus exit $?"
	cat "$W/$tag/t-report.txt" 2>/dev/null || echo "FAIL $tag: no report"
}
echo "== A: Place selected on the blank page"
run A place /home/s1/tmp-obit/obit_test.sla
echo "== B: the page opened from the workflow"
run B workflow "$WFSLA"
cp "$W/home/.cache/scribus/news/api.log" "$W/api.log" 2>/dev/null
[ -s "$W/A/t-gallery.png" ] && [ -s "$W/B/t-gallery.png" ] && convert "$W/A/t-gallery.png" "$W/B/t-gallery.png" +append "$W/gallery-side-by-side.png" 2>/dev/null
[ -s "$W/A/t-card.png" ] && [ -s "$W/B/t-card.png" ] && convert "$W/A/t-card.png" "$W/B/t-card.png" +append "$W/card-side-by-side.png" 2>/dev/null
echo "== compare"
python3 "$HERE/fixtures/compare_obit_pages.py" "$W/A/t-after.sla" "$W/B/t-after.sla" "$W/A/t-report.txt" "$W/B/t-report.txt"
