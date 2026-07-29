# -*- coding: utf-8 -*-
# Authors Faircode Frames batch 1 (.sce via copy-to-scrapbook) inside Scribus scripter.
import scribus
import glob, os, shutil, sys, traceback

OUT = '/tmp/claude-0/-home-s1-suneer-scribus-1-7-3/5bb52615-10b9-47c2-8103-895dfd70b647/scratchpad/sce-out'
LOG = OUT + '/author.log'

def log(msg):
    with open(LOG, 'a') as f:
        f.write(msg + '\n')

def pick_font(fonts, want, fallbacks):
    for w in [want] + fallbacks:
        if w in fonts:
            return w
    for f in fonts:
        if f.startswith(want.split(' Regular')[0]):
            return f
    return None

def tmp_sce_files():
    out = []
    for base in glob.glob(os.path.expanduser('~/.local/share/scribus*')):
        out += glob.glob(base + '/scrapbook/tmp/*.sce')
    return set(out)

def harvest(before, target):
    new = tmp_sce_files() - before
    if len(new) != 1:
        raise RuntimeError('expected 1 new sce, got %d: %r' % (len(new), new))
    src = new.pop()
    shutil.copy(src, target)
    log('harvested %s -> %s (%d bytes)' % (src, target, os.path.getsize(target)))

def main():
    os.makedirs(OUT, exist_ok=True)
    scribus.newDocument(scribus.PAPER_A4, (10,10,10,10), scribus.PORTRAIT, 1,
                        scribus.UNIT_MILLIMETERS, scribus.PAGE_1, 0, 1)

    fonts = scribus.getFontNames()
    ml = pick_font(fonts, 'Noto Sans Malayalam Regular', ['Rachana Regular', 'AnjaliOldLipi Regular'])
    mlb = pick_font(fonts, 'Noto Sans Malayalam Bold', [ml] if ml else [])
    latinb = pick_font(fonts, 'DejaVu Sans Bold', ['Liberation Sans Bold'])
    log('fonts: ml=%r mlb=%r latinb=%r' % (ml, mlb, latinb))
    if not ml or not latinb:
        raise RuntimeError('missing fonts')

    # Faircode palette
    scribus.defineColorRGB('FC-Black', 0, 0, 0)
    scribus.defineColorRGB('FC-White', 255, 255, 255)
    scribus.defineColorRGB('FC-Red', 218, 37, 29)
    scribus.defineColorRGB('FC-Orange', 247, 148, 29)
    scribus.defineColorRGB('FC-Green', 58, 170, 53)
    scribus.defineColorRGB('FC-Cyan', 0, 174, 239)

    # Styles (embedded in .sce; keep typing Malayalam-ready after delete-all)
    scribus.createCharStyle('FC Malayalam Body', ml, 11.0)
    scribus.createCharStyle('FC Malayalam Quote', ml, 13.0)
    scribus.createCharStyle('FC Malayalam Title', mlb, 13.0, fillcolor='FC-White')
    scribus.createParagraphStyle('FC Body ML', alignment=scribus.ALIGN_LEFT,
                                 charstyle='FC Malayalam Body')
    scribus.createParagraphStyle('FC Quote ML', alignment=scribus.ALIGN_CENTERED,
                                 charstyle='FC Malayalam Quote')
    scribus.createParagraphStyle('FC Title ML', alignment=scribus.ALIGN_LEFT,
                                 charstyle='FC Malayalam Title')

    # ---------- Template 1: quote box A (orange/green corner Ls + quote glyphs)
    x, y = 20.0, 20.0
    lt = scribus.createPolyLine([x, y+14, x, y, x+14, y])
    scribus.setLineColor('FC-Orange', lt)
    scribus.setLineWidth(3.4, lt)
    lb = scribus.createPolyLine([x+80, y+34, x+80, y+48, x+66, y+48])
    scribus.setLineColor('FC-Green', lb)
    scribus.setLineWidth(3.4, lb)

    q1 = scribus.createText(x+3, y+1.5, 16, 16)
    scribus.setText(u'“', q1)
    scribus.setFont(latinb, q1)
    scribus.setFontSize(30, q1)
    scribus.setTextColor('FC-Orange', q1)

    q2 = scribus.createText(x+61, y+30.5, 16, 16)
    scribus.setText(u'”', q2)
    scribus.setFont(latinb, q2)
    scribus.setFontSize(30, q2)
    scribus.setTextColor('FC-Green', q2)
    scribus.setTextAlignment(scribus.ALIGN_RIGHT, q2)
    scribus.setTextVerticalAlignment(scribus.ALIGNV_BOTTOM, q2)

    t1 = scribus.createText(x+10, y+10, 60, 28)
    scribus.setText(u'ഉദ്ധരണി ഇവിടെ ചേർക്കുക', t1)  # "ഉദ്ധരണി ഇവിടെ ചേർക്കുക"
    scribus.setStyle('FC Quote ML', t1)
    scribus.setTextVerticalAlignment(scribus.ALIGNV_CENTERED, t1)

    g1 = scribus.groupObjects([lt, lb, q1, q2, t1])
    before = tmp_sce_files()
    scribus.copyObjects(g1)
    harvest(before, OUT + '/quote-box-A.sce')

    # ---------- Template 3: title-bar box
    x, y = 120.0, 20.0
    tt = scribus.createText(x, y, 80, 10)
    scribus.setFillColor('FC-Red', tt)
    scribus.setText(u'തലക്കെട്ട്', tt)  # "തലക്കെട്ട്"
    scribus.setStyle('FC Title ML', tt)
    scribus.setTextDistances(2, 2, 1, 1, tt)
    scribus.setTextVerticalAlignment(scribus.ALIGNV_CENTERED, tt)

    tb = scribus.createText(x, y+10, 80, 40)
    scribus.setLineColor('FC-Black', tb)
    scribus.setLineWidth(0.5, tb)
    scribus.setText(u'ഇവിടെ ടൈപ്പ് ചെയ്യുക', tb)  # "ഇവിടെ ടൈപ്പ് ചെയ്യുക"
    scribus.setStyle('FC Body ML', tb)
    scribus.setTextDistances(3, 3, 3, 3, tb)

    g3 = scribus.groupObjects([tt, tb])
    before = tmp_sce_files()
    scribus.copyObjects(g3)
    harvest(before, OUT + '/title-bar-box.sce')

    # ---------- Template 8: rounded tinted box (single frame, radius stays editable)
    x, y = 20.0, 90.0
    rt = scribus.createText(x, y, 80, 40)
    scribus.setFillColor('FC-Cyan', rt)
    scribus.setFillShade(12, rt)
    scribus.setCornerRadius(11, rt)  # points, ~4mm
    scribus.setText(u'ഇവിടെ ടൈപ്പ് ചെയ്യുക', rt)
    scribus.setStyle('FC Body ML', rt)
    scribus.setTextDistances(4, 4, 4, 4, rt)

    before = tmp_sce_files()
    scribus.copyObjects(rt)
    harvest(before, OUT + '/rounded-tint-box.sce')

    # keep the authoring doc + a page render for visual review
    scribus.saveDocAs(OUT + '/faircode-frames-batch1.sla')
    img = scribus.ImageExport()
    img.type = 'PNG'
    img.dpi = 150
    img.saveAs(OUT + '/batch1-page.png')
    log('DONE')

try:
    main()
except Exception:
    os.makedirs(OUT, exist_ok=True)
    with open(LOG, 'a') as f:
        f.write('FAILED\n' + traceback.format_exc())
