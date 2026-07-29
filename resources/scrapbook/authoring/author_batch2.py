# -*- coding: utf-8 -*-
# Authors Faircode Frames batch 2 (.sce via copy-to-scrapbook) inside Scribus scripter.
# Run: xvfb-run -a scribus -g -ns -py author_batch2.py
import scribus
import glob, os, shutil, traceback

OUT = '/tmp/claude-0/-home-s1-suneer-scribus-1-7-3/5bb52615-10b9-47c2-8103-895dfd70b647/scratchpad/sce-out2'
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

def copy_out(names, target):
    before = tmp_sce_files()
    scribus.copyObjects(names)
    harvest(before, target)

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

    # Faircode palette (same names as batch 1; FC-Pink is new)
    scribus.defineColorRGB('FC-Black', 0, 0, 0)
    scribus.defineColorRGB('FC-White', 255, 255, 255)
    scribus.defineColorRGB('FC-Red', 218, 37, 29)
    scribus.defineColorRGB('FC-Pink', 236, 0, 140)
    scribus.defineColorRGB('FC-Cyan', 0, 174, 239)

    scribus.createCharStyle('FC Malayalam Body', ml, 11.0)
    scribus.createCharStyle('FC Malayalam Quote', ml, 13.0)
    scribus.createCharStyle('FC Malayalam Title', mlb, 13.0, fillcolor='FC-White')
    scribus.createParagraphStyle('FC Body ML', alignment=scribus.ALIGN_LEFT,
                                 charstyle='FC Malayalam Body')
    scribus.createParagraphStyle('FC Quote ML', alignment=scribus.ALIGN_CENTERED,
                                 charstyle='FC Malayalam Quote')
    scribus.createParagraphStyle('FC Title ML', alignment=scribus.ALIGN_LEFT,
                                 charstyle='FC Malayalam Title')

    BODY = u'ഇവിടെ ടൈപ്പ് ചെയ്യുക'
    QUOTE = u'ഉദ്ധരണി ഇവിടെ ചേർക്കുക'
    TITLE = u'തലക്കെട്ട്'

    # ---------- Template 2: quote box B (pink/cyan, L-lines on TR/BL diagonal)
    x, y = 20.0, 20.0
    ltr = scribus.createPolyLine([x+66, y, x+80, y, x+80, y+14])
    scribus.setLineColor('FC-Pink', ltr)
    scribus.setLineWidth(3.4, ltr)
    lbl = scribus.createPolyLine([x+14, y+48, x, y+48, x, y+34])
    scribus.setLineColor('FC-Cyan', lbl)
    scribus.setLineWidth(3.4, lbl)

    q1 = scribus.createText(x+3, y+1.5, 16, 16)
    scribus.setText(u'“', q1)
    scribus.setFont(latinb, q1)
    scribus.setFontSize(30, q1)
    scribus.setTextColor('FC-Pink', q1)

    q2 = scribus.createText(x+61, y+30.5, 16, 16)
    scribus.setText(u'”', q2)
    scribus.setFont(latinb, q2)
    scribus.setFontSize(30, q2)
    scribus.setTextColor('FC-Cyan', q2)
    scribus.setTextAlignment(scribus.ALIGN_RIGHT, q2)
    scribus.setTextVerticalAlignment(scribus.ALIGNV_BOTTOM, q2)

    t2 = scribus.createText(x+10, y+10, 60, 28)
    scribus.setText(QUOTE, t2)
    scribus.setStyle('FC Quote ML', t2)
    scribus.setTextVerticalAlignment(scribus.ALIGNV_CENTERED, t2)

    g = scribus.groupObjects([ltr, lbl, q1, q2, t2])
    copy_out(g, OUT + '/quote-box-B.sce')

    # ---------- Template 4: callout, leader line + dot exiting LEFT
    x, y = 120.0, 20.0
    dot = scribus.createEllipse(x, y+12.5, 5, 5)
    scribus.setFillColor('FC-Red', dot)
    scribus.setLineColor('None', dot)
    lead = scribus.createLine(x+2.5, y+15, x+25, y+15)
    scribus.setLineColor('FC-Black', lead)
    scribus.setLineWidth(0.7, lead)
    box = scribus.createText(x+25, y, 60, 30)
    scribus.setLineColor('FC-Black', box)
    scribus.setLineWidth(0.5, box)
    scribus.setText(BODY, box)
    scribus.setStyle('FC Body ML', box)
    scribus.setTextDistances(3, 3, 3, 3, box)

    g = scribus.groupObjects([dot, lead, box])
    copy_out(g, OUT + '/callout-left.sce')

    # ---------- Template 5: callout, leader line + dot exiting RIGHT
    x, y = 120.0, 70.0
    box = scribus.createText(x, y, 60, 30)
    scribus.setLineColor('FC-Black', box)
    scribus.setLineWidth(0.5, box)
    scribus.setText(BODY, box)
    scribus.setStyle('FC Body ML', box)
    scribus.setTextDistances(3, 3, 3, 3, box)
    lead = scribus.createLine(x+60, y+15, x+82.5, y+15)
    scribus.setLineColor('FC-Black', lead)
    scribus.setLineWidth(0.7, lead)
    dot = scribus.createEllipse(x+80, y+12.5, 5, 5)
    scribus.setFillColor('FC-Red', dot)
    scribus.setLineColor('None', dot)

    g = scribus.groupObjects([box, lead, dot])
    copy_out(g, OUT + '/callout-right.sce')

    # ---------- Template 6: parallelogram/angled title strip + body with left rule
    x, y = 20.0, 90.0
    para = scribus.createPolygon([x+6, y, x+80, y, x+74, y+10, x, y+10])
    scribus.setFillColor('FC-Red', para)
    scribus.setLineColor('None', para)
    tt = scribus.createText(x+9, y+1, 60, 8)
    scribus.setText(TITLE, tt)
    scribus.setStyle('FC Title ML', tt)
    scribus.setTextVerticalAlignment(scribus.ALIGNV_CENTERED, tt)
    rule = scribus.createLine(x, y+13, x, y+47)
    scribus.setLineColor('FC-Black', rule)
    scribus.setLineWidth(0.5, rule)
    body = scribus.createText(x+3, y+13, 77, 34)
    scribus.setText(BODY, body)
    scribus.setStyle('FC Body ML', body)
    scribus.setTextDistances(1, 1, 1, 1, body)

    g = scribus.groupObjects([para, tt, rule, body])
    copy_out(g, OUT + '/angled-title-strip.sce')

    # ---------- Template 7: bracket-corner box (corners only)
    x, y = 120.0, 120.0
    c1 = scribus.createPolyLine([x, y+8, x, y, x+8, y])
    c2 = scribus.createPolyLine([x+72, y, x+80, y, x+80, y+8])
    c3 = scribus.createPolyLine([x+80, y+32, x+80, y+40, x+72, y+40])
    c4 = scribus.createPolyLine([x+8, y+40, x, y+40, x, y+32])
    for c in (c1, c2, c3, c4):
        scribus.setLineColor('FC-Black', c)
        scribus.setLineWidth(0.9, c)
    inner = scribus.createText(x+4, y+4, 72, 32)
    scribus.setText(BODY, inner)
    scribus.setStyle('FC Body ML', inner)
    scribus.setTextDistances(2, 2, 2, 2, inner)

    g = scribus.groupObjects([c1, c2, c3, c4, inner])
    copy_out(g, OUT + '/bracket-corner-box.sce')

    scribus.saveDocAs(OUT + '/faircode-frames-batch2.sla')
    img = scribus.ImageExport()
    img.type = 'PNG'
    img.dpi = 150
    img.saveAs(OUT + '/batch2-page.png')
    log('DONE')

try:
    main()
except Exception:
    os.makedirs(OUT, exist_ok=True)
    with open(LOG, 'a') as f:
        f.write('FAILED\n' + traceback.format_exc())
