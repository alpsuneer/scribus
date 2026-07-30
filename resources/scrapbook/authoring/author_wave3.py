# -*- coding: utf-8 -*-
# Authors Faircode Frames wave 3: AttrQuote, SectionStrip, NewsBox,
# BannerHead, LedeBars — modeled on Hindi newspaper design references.
# Adds FC-Yellow and FC-Primary to the palette and the FC BodyBold ML style.
# Run: xvfb-run -a scribus -g -ns -py author_wave3.py
import scribus
import glob, os, shutil, traceback

OUT = '/tmp/fc-wave3'
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
    scribus.defineColorRGB('FC-Yellow', 255, 204, 0)
    scribus.defineColorRGB('FC-Primary', 0, 84, 166)

    scribus.createCharStyle('FC Malayalam Body', ml, 11.0)
    scribus.createCharStyle('FC Malayalam Quote', ml, 13.0)
    scribus.createCharStyle('FC Malayalam Title', mlb, 13.0, fillcolor='FC-White')
    scribus.createCharStyle('FC Malayalam TitleDark', mlb, 13.0)
    scribus.createCharStyle('FC Malayalam BodyBold', mlb, 10.0)
    scribus.createParagraphStyle('FC Body ML', alignment=scribus.ALIGN_LEFT,
                                 charstyle='FC Malayalam Body')
    scribus.createParagraphStyle('FC Quote ML', alignment=scribus.ALIGN_CENTERED,
                                 charstyle='FC Malayalam Quote')
    scribus.createParagraphStyle('FC Title ML', alignment=scribus.ALIGN_LEFT,
                                 charstyle='FC Malayalam Title')
    scribus.createParagraphStyle('FC TitleDark ML', alignment=scribus.ALIGN_LEFT,
                                 charstyle='FC Malayalam TitleDark')
    scribus.createParagraphStyle('FC BodyBold ML', alignment=scribus.ALIGN_LEFT,
                                 charstyle='FC Malayalam BodyBold')

    BODY = u'ഇവിടെ ടൈപ്പ് ചെയ്യുക'
    TITLE = u'തലക്കെട്ട്'
    QUOTE = u'ഉദ്ധരണി ഇവിടെ ചേർക്കുക'

    # ---------- 19. AttrQuote: attributed quote box on yellow tint
    x, y = 20.0, 20.0
    bg = scribus.createRect(x, y, 80, 55)
    scribus.setFillColor('FC-Yellow', bg)
    scribus.setFillShade(18, bg)
    scribus.setLineColor('None', bg)
    q1 = scribus.createText(x+3, y+1, 18, 16)
    scribus.setText(u'“', q1)
    scribus.setFont(latinb, q1)
    scribus.setFontSize(36, q1)
    scribus.setTextColor('FC-Red', q1)
    q2 = scribus.createText(x+59, y+23, 18, 16.5)
    scribus.setText(u'”', q2)
    scribus.setFont(latinb, q2)
    scribus.setFontSize(36, q2)
    scribus.setTextColor('FC-Red', q2)
    scribus.setTextAlignment(scribus.ALIGN_RIGHT, q2)
    scribus.setTextVerticalAlignment(scribus.ALIGNV_BOTTOM, q2)
    qt = scribus.createText(x+11, y+12, 58, 24)
    scribus.setText(QUOTE, qt)
    scribus.setStyle('FC Quote ML', qt)
    scribus.setTextVerticalAlignment(scribus.ALIGNV_CENTERED, qt)
    an = scribus.createText(x+10, y+39, 60, 7)
    scribus.setText(u'ലേഖകന്റെ പേര്', an)
    scribus.setStyle('FC BodyBold ML', an)
    scribus.setTextAlignment(scribus.ALIGN_CENTERED, an)
    ds = scribus.createText(x+10, y+46, 60, 6.5)
    scribus.setText(u'സ്ഥാനപ്പേര്', ds)
    scribus.setStyle('FC Body ML', ds)
    scribus.setFontSize(8.5, ds)
    scribus.setTextAlignment(scribus.ALIGN_CENTERED, ds)
    g = scribus.groupObjects([bg, q1, q2, qt, an, ds])
    copy_out(g, OUT + '/AttrQuote.sce')

    # ---------- 20. SectionStrip: yellow strip + twin accent bars, centered title
    x, y = 120.0, 20.0
    strip = scribus.createRect(x, y+1, 80, 9)
    scribus.setFillColor('FC-Yellow', strip)
    scribus.setFillShade(35, strip)
    scribus.setLineColor('None', strip)
    b1 = scribus.createRect(x+1.5, y, 1.8, 11)
    scribus.setFillColor('FC-Red', b1)
    scribus.setLineColor('None', b1)
    b2 = scribus.createRect(x+4.2, y, 1.8, 11)
    scribus.setFillColor('FC-Black', b2)
    scribus.setLineColor('None', b2)
    tt = scribus.createText(x+8, y+1.6, 68, 8)
    scribus.setText(TITLE, tt)
    scribus.setStyle('FC TitleDark ML', tt)
    scribus.setTextAlignment(scribus.ALIGN_CENTERED, tt)
    scribus.setTextVerticalAlignment(scribus.ALIGNV_CENTERED, tt)
    g = scribus.groupObjects([strip, b1, b2, tt])
    copy_out(g, OUT + '/SectionStrip.sce')

    # ---------- 21. NewsBox: bordered item box, corner square + edge bar accents
    x, y = 20.0, 90.0
    box = scribus.createRect(x, y, 80, 45)
    scribus.setFillColor('None', box)
    scribus.setLineColor('FC-Black', box)
    scribus.setLineWidth(0.6, box)
    sq = scribus.createRect(x-1, y-1, 4, 4)
    scribus.setFillColor('FC-Red', sq)
    scribus.setLineColor('None', sq)
    bar = scribus.createRect(x-0.7, y+5, 1.4, 12)
    scribus.setFillColor('FC-Red', bar)
    scribus.setLineColor('None', bar)
    kick = scribus.createText(x+3.5, y+3, 73, 5.5)
    scribus.setText(u'വിഭാഗം', kick)
    scribus.setStyle('FC BodyBold ML', kick)
    titl = scribus.createText(x+3.5, y+8.5, 73, 8)
    scribus.setText(TITLE, titl)
    scribus.setStyle('FC TitleDark ML', titl)
    body = scribus.createText(x+3.5, y+17, 73, 25)
    scribus.setText(BODY, body)
    scribus.setStyle('FC Body ML', body)
    g = scribus.groupObjects([box, sq, bar, kick, titl, body])
    copy_out(g, OUT + '/NewsBox.sce')

    # ---------- 22. BannerHead: rounded blue banner, red rules above/below
    x, y = 120.0, 90.0
    r1 = scribus.createLine(x, y, x+80, y)
    scribus.setLineColor('FC-Red', r1)
    scribus.setLineWidth(1.0, r1)
    bar = scribus.createRect(x+3, y+2.5, 74, 12)
    scribus.setFillColor('FC-Primary', bar)
    scribus.setLineColor('None', bar)
    scribus.setCornerRadius(12, bar)
    tt = scribus.createText(x+5, y+3.5, 70, 10)
    scribus.setText(TITLE, tt)
    scribus.setStyle('FC Title ML', tt)
    scribus.setTextAlignment(scribus.ALIGN_CENTERED, tt)
    scribus.setTextVerticalAlignment(scribus.ALIGNV_CENTERED, tt)
    r2 = scribus.createLine(x, y+17, x+80, y+17)
    scribus.setLineColor('FC-Red', r2)
    scribus.setLineWidth(1.0, r2)
    g = scribus.groupObjects([r1, bar, tt, r2])
    copy_out(g, OUT + '/BannerHead.sce')

    # ---------- 23. LedeBars: 4 fading blue bars + bold single-line lede
    x, y = 20.0, 150.0
    bars = []
    for i, s in enumerate([100, 75, 50, 25]):
        b = scribus.createRect(x + i*2.9, y, 1.8, 9)
        scribus.setFillColor('FC-Primary', b)
        scribus.setFillShade(s, b)
        scribus.setLineColor('None', b)
        bars.append(b)
    txt = scribus.createText(x+12.5, y, 67, 9)
    scribus.setText(BODY, txt)
    scribus.setStyle('FC BodyBold ML', txt)
    scribus.setTextVerticalAlignment(scribus.ALIGNV_CENTERED, txt)
    g = scribus.groupObjects(bars + [txt])
    copy_out(g, OUT + '/LedeBars.sce')

    scribus.saveDocAs(OUT + '/faircode-frames-wave3.sla')
    img = scribus.ImageExport()
    img.type = 'PNG'
    img.dpi = 150
    img.saveAs(OUT + '/wave3-page.png')
    log('DONE')

try:
    main()
except Exception:
    os.makedirs(OUT, exist_ok=True)
    with open(LOG, 'a') as f:
        f.write('FAILED\n' + traceback.format_exc())
