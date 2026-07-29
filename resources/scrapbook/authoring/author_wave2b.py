# -*- coding: utf-8 -*-
# Authors Faircode Frames wave 2 batch B: Ribbon, Badge, DoubleBox,
# DashedBox, Highlight. Completes the 18-template starter library.
# Run: xvfb-run -a scribus -g -ns -py author_wave2b.py
import scribus
import glob, os, shutil, traceback

OUT = '/tmp/claude-0/-home-s1-suneer-scribus-1-7-3/5bb52615-10b9-47c2-8103-895dfd70b647/scratchpad/sce-w2b'
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

def copy_out(names, target):
    before = tmp_sce_files()
    scribus.copyObjects(names)
    new = tmp_sce_files() - before
    if len(new) != 1:
        raise RuntimeError('expected 1 new sce, got %r' % new)
    shutil.copy(new.pop(), target)
    log('harvested -> ' + target)

def main():
    os.makedirs(OUT, exist_ok=True)
    scribus.newDocument(scribus.PAPER_A4, (10,10,10,10), scribus.PORTRAIT, 1,
                        scribus.UNIT_MILLIMETERS, scribus.PAGE_1, 0, 1)

    fonts = scribus.getFontNames()
    ml = pick_font(fonts, 'Noto Sans Malayalam Regular', ['Rachana Regular'])
    mlb = pick_font(fonts, 'Noto Sans Malayalam Bold', [ml] if ml else [])
    latinb = pick_font(fonts, 'DejaVu Sans Bold', ['Liberation Sans Bold'])
    log('fonts: %r %r %r' % (ml, mlb, latinb))
    if not ml or not latinb:
        raise RuntimeError('missing fonts')

    scribus.defineColorRGB('FC-Black', 0, 0, 0)
    scribus.defineColorRGB('FC-White', 255, 255, 255)
    scribus.defineColorRGB('FC-Red', 218, 37, 29)
    scribus.defineColorRGB('FC-Cyan', 0, 174, 239)

    scribus.createCharStyle('FC Malayalam Body', ml, 11.0)
    scribus.createCharStyle('FC Malayalam Title', mlb, 13.0, fillcolor='FC-White')
    scribus.createCharStyle('FC Malayalam TitleDark', mlb, 13.0)
    scribus.createParagraphStyle('FC Body ML', alignment=scribus.ALIGN_LEFT,
                                 charstyle='FC Malayalam Body')
    scribus.createParagraphStyle('FC Title ML', alignment=scribus.ALIGN_LEFT,
                                 charstyle='FC Malayalam Title')
    scribus.createParagraphStyle('FC TitleDark ML', alignment=scribus.ALIGN_LEFT,
                                 charstyle='FC Malayalam TitleDark')

    BODY = u'ഇവിടെ ടൈപ്പ് ചെയ്യുക'
    TITLE = u'തലക്കെട്ട്'

    # ---------- 14. Ribbon: banner with notched (chevron) ends + title
    x, y = 20.0, 20.0
    band = scribus.createPolygon([x, y, x+80, y, x+74, y+6, x+80, y+12,
                                  x, y+12, x+6, y+6])
    scribus.setFillColor('FC-Red', band)
    scribus.setLineColor('None', band)
    tt = scribus.createText(x+10, y+1.5, 60, 9)
    scribus.setText(TITLE, tt)
    scribus.setStyle('FC Title ML', tt)
    scribus.setTextAlignment(scribus.ALIGN_CENTERED, tt)
    scribus.setTextVerticalAlignment(scribus.ALIGNV_CENTERED, tt)
    g = scribus.groupObjects([band, tt])
    copy_out(g, OUT + '/Ribbon.sce')

    # ---------- 15. Badge: accent circle with short label + body beside
    x, y = 120.0, 20.0
    circ = scribus.createEllipse(x, y, 16, 16)
    scribus.setFillColor('FC-Cyan', circ)
    scribus.setLineColor('None', circ)
    lbl = scribus.createText(x+0.5, y+4, 15, 8)
    scribus.setText(u'പുതിയ', lbl)
    scribus.setFont(mlb, lbl)
    scribus.setFontSize(8, lbl)
    scribus.setTextColor('FC-White', lbl)
    scribus.setTextAlignment(scribus.ALIGN_CENTERED, lbl)
    body = scribus.createText(x+18, y, 62, 18)
    scribus.setText(BODY, body)
    scribus.setStyle('FC Body ML', body)
    scribus.setTextDistances(2, 2, 2, 2, body)
    g = scribus.groupObjects([circ, lbl, body])
    copy_out(g, OUT + '/Badge.sce')

    # ---------- 16. DoubleBox: thin outer + thick inner border
    x, y = 20.0, 60.0
    outer = scribus.createRect(x, y, 80, 40)
    scribus.setFillColor('None', outer)
    scribus.setLineColor('FC-Black', outer)
    scribus.setLineWidth(0.5, outer)
    inner = scribus.createText(x+2, y+2, 76, 36)
    scribus.setLineColor('FC-Black', inner)
    scribus.setLineWidth(1.6, inner)
    scribus.setText(BODY, inner)
    scribus.setStyle('FC Body ML', inner)
    scribus.setTextDistances(3, 3, 3, 3, inner)
    g = scribus.groupObjects([outer, inner])
    copy_out(g, OUT + '/DoubleBox.sce')

    # ---------- 17. DashedBox: dashed border, rounded corners (single frame)
    x, y = 20.0, 110.0
    df = scribus.createText(x, y, 80, 40)
    scribus.setLineColor('FC-Black', df)
    scribus.setLineWidth(1.0, df)
    scribus.setLineStyle(scribus.LINE_DASH, df)
    scribus.setCornerRadius(11, df)
    scribus.setText(BODY, df)
    scribus.setStyle('FC Body ML', df)
    scribus.setTextDistances(3, 3, 3, 3, df)
    copy_out(df, OUT + '/DashedBox.sce')

    # ---------- 18. Highlight: single-line section header on tinted bar
    x, y = 120.0, 60.0
    hl = scribus.createText(x, y, 80, 9)
    scribus.setFillColor('FC-Cyan', hl)
    scribus.setFillShade(25, hl)
    scribus.setText(TITLE, hl)
    scribus.setStyle('FC TitleDark ML', hl)
    scribus.setTextDistances(2, 2, 1, 1, hl)
    scribus.setTextVerticalAlignment(scribus.ALIGNV_CENTERED, hl)
    copy_out(hl, OUT + '/Highlight.sce')

    scribus.saveDocAs(OUT + '/faircode-frames-wave2b.sla')
    img = scribus.ImageExport()
    img.type = 'PNG'
    img.dpi = 150
    img.saveAs(OUT + '/wave2b-page.png')
    log('DONE')

try:
    main()
except Exception:
    os.makedirs(OUT, exist_ok=True)
    with open(LOG, 'a') as f:
        f.write('FAILED\n' + traceback.format_exc())
