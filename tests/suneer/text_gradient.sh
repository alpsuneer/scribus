#!/bin/bash
# Gradient text fill (CharStyle FillGradient*, suneertextgradient.cpp) end to end:
#   1. builds an A3 page with four 80 pt Malayalam headlines (fixtures/text_gradient_make.py),
#   2. SUNEER_TEXTGRAD_SELFTEST applies Red headline per line / per frame, Grey radial and a
#      one-word range through itemSelection_ApplyCharStyle, checks undo / redo, saves grad.sla,
#   3. reopens grad.sla headless and exports PDF + PostScript (print to file),
#   4. checks: Malayalam in the SLA, text still text (pdffonts, pdftotext same as solid),
#      CMYK shadings, pattern fills, gradient visible in the PDF and PS renders.
# usage: tests/suneer/text_gradient.sh <work dir> [scribus]   (HOME / PREFS for an isolated profile)
set -u
W=${1:?work dir}; SCRIBUS=${2:-/usr/local/bin/scribus}; HERE="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$W/out"; PR=(); [ -n "${PREFS:-}" ] && PR=(-pr "$PREFS")
sed "s#/home/s1/tmp-textgrad#$W#g" "$HERE/fixtures/text_gradient_make.py" > "$W/make.py"
timeout 300 xvfb-run -a "$SCRIBUS" -g -ns "${PR[@]}" -py "$W/make.py" > "$W/make.log" 2>&1
[ -s "$W/base.sla" ] || { echo "FAIL: no base.sla"; exit 1; }
rm -f "$W/.base.sla.lock"
SUNEER_TEXTGRAD_SELFTEST="$W/out/t" SUNEER_TEXTGRAD_SAVEAS="$W/grad.sla" QT_IM_MODULE= timeout 300 \
  xvfb-run -a -s '-screen 0 1600x1000x24' "$SCRIBUS" "${PR[@]}" "$W/base.sla" > "$W/out/t.log" 2>&1
cat "$W/out/t-report.txt"
for n in base grad; do
cat > "$W/export-$n.py" <<PY
import scribus
scribus.openDoc("$W/$n.sla")
pdf = scribus.PDFfile(); pdf.file = "$W/out/$n.pdf"; pdf.outdst = 1; pdf.save()
p = scribus.Printer(); p.printer = "File"; p.file = "$W/out/$n.ps"; p.printNow()
PY
rm -f "$W/.$n.sla.lock"
timeout 300 xvfb-run -a "$SCRIBUS" -g -ns "${PR[@]}" -py "$W/export-$n.py" > "$W/out/export-$n.log" 2>&1
done
pdftoppm -r 150 -png -x 0 -y 0 -W 700 -H 260 "$W/out/grad.pdf" "$W/out/pdfcrop" 2>/dev/null
gs -q -dSAFER -dBATCH -dNOPAUSE -sDEVICE=png16m -r150 -sOutputFile="$W/out/grad-ps.png" "$W/out/grad.ps" 2>/dev/null
pdftoppm -r 60 -png "$W/out/grad.pdf" "$W/out/grad-pdf" 2>/dev/null
python3 - "$W" <<'PY'
import sys,re,zlib,subprocess
W=sys.argv[1]; fails=0
def check(what,ok):
    global fails
    print(("PASS " if ok else "FAIL ")+what); fails+=0 if ok else 1
sla=open(W+'/grad.sla',encoding='utf-8').read()
check('Malayalam in grad.sla (%d codepoints)'%sum(0xD00<=ord(c)<=0xD7F for c in sla), sum(0xD00<=ord(c)<=0xD7F for c in sla)>50)
check('gradient attributes saved (%d runs)'%sla.count('TextFillGradient="Red headline"'), sla.count('TextFillGradient="Red headline"')>=3)
check('per frame + radial saved', 'TextFillGradientMapping="1"' in sla and 'TextFillGradientType="1"' in sla)
check('preset gradients saved', '<Gradient Name="Red headline"' in sla and '<Gradient Name="Grey headline"' in sla)
d=open(W+'/out/grad.pdf','rb').read(); txt=b''
for s in re.findall(rb'stream\r?\n(.*?)\r?\nendstream',d,re.S):
    try: txt+=zlib.decompress(s)
    except Exception: pass
allb=d+txt
fonts=subprocess.run(['pdffonts',W+'/out/grad.pdf'],capture_output=True,text=True).stdout
check('pdffonts: embedded font', 'yes' in fonts.splitlines()[-1] if len(fonts.splitlines())>2 else False)
t1=subprocess.run(['pdftotext',W+'/out/grad.pdf','-'],capture_output=True).stdout
t0=subprocess.run(['pdftotext',W+'/out/base.pdf','-'],capture_output=True).stdout
check('pdftotext same as the solid version (%d bytes)'%len(t1), t1==t0 and len(t1)>20)
check('glyphs drawn as text (Tj) with pattern fills (%d)'%len(re.findall(rb'/Pattern\d+ scn',txt)), b' Tj' in txt and len(re.findall(rb'/Pattern\d+ scn',txt))>=5)
check('shadings are DeviceCMYK, no RGB', allb.count(b'/ColorSpace /DeviceCMYK')>=5 and b'/ColorSpace /DeviceRGB' not in allb)
check('dark red stop 0/100/100/45', b'/C0 [0.00000 1.00000 1.00000 0.45098]' in allb)
from PIL import Image
def ramp(png):
    im=Image.open(png).convert('RGB'); w,h=im.size; band=[]
    for y in range(h):
        ink=[im.getpixel((x,y)) for x in range(0,w,2)]
        ink=[p for p in ink if p[1]<80 and p[2]<80 and p[0]>60]
        if len(ink)>=8: band.append(sum(p[0] for p in ink)/len(ink))
    return (band[0],band[-1]) if len(band)>10 else (0,0)
a,b=ramp(W+'/out/pdfcrop-1.png'); check('PDF: first line dark at the top (R %.0f) -> bright at the bottom (R %.0f)'%(a,b), b-a>50)
ps=open(W+'/out/grad.ps','rb').read()
check('PS: gradient patterns defined (%d) and used'%ps.count(b'makepattern def'), ps.count(b'makepattern def')>=5 and b'setpattern' in ps)
im=Image.open(W+'/out/grad-ps.png').convert('RGB'); w,h=im.size
crop=im.crop((0,0,w,h//4)); crop.save(W+'/out/pscrop.png')
a,b=ramp(W+'/out/pscrop.png'); check('PS render: dark top (R %.0f) -> bright bottom (R %.0f)'%(a,b), b-a>50)
print('ALL PASS' if fails==0 else 'FAILED: %d'%fails); sys.exit(1 if fails else 0)
PY
