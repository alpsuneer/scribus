# -*- coding: utf-8 -*-
# Authors Faircode Frames wave 2 batch A: TitleRule, SideRule, PullQuote,
# NumberBox, ShadowBox. Same pipeline as author_batch2.py.
# Run: xvfb-run -a scribus -g -ns -py author_wave2a.py
import scribus
import glob, os, shutil, traceback

OUT = '/tmp/claude-0/-home-s1-suneer-scribus-1-7-3/5bb52615-10b9-47c2-8103-895dfd70b647/scratchpad/sce-w2a'
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
    scribus.createCharStyle('FC Malayalam Quote', ml, 13.0)
    scribus.createCharStyle('FC Malayalam Title', mlb, 13.0, fillcolor='FC-White')
    scribus.createCharStyle('FC Malayalam TitleDark', mlb, 13.0)
    scribus.createParagraphStyle('FC Body ML', alignment=scribus.ALIGN_LEFT,
                                 charstyle='FC Malayalam Body')
    scribus.createParagraphStyle('FC Quote ML', alignment=scribus.ALIGN_CENTERED,
                                 charstyle='FC Malayalam Quote')
    scribus.createParagraphStyle('FC Title ML', alignment=scribus.ALIGN_LEFT,
                                 charstyle='FC Malayalam Title')
    scribus.createParagraphStyle('FC TitleDark ML', alignment=scribus.ALIGN_LEFT,
                                 charstyle='FC Malayalam TitleDark')

    BODY = u'ഇവിടെ ടൈപ്പ് ചെയ്യുക'
    QUOTE = u'ഉദ്ധരണി ഇവിടെ ചേർക്കുക'
    TITLE = u'തലക്കെട്ട്'

    # ---------- 9. TitleRule: title + thick accent underline
    x, y = 20.0, 20.0
    tt = scribus.createText(x, y, 80, 10)
    scribus.setText(TITLE, tt)
    scribus.setStyle('FC TitleDark ML', tt)
    scribus.setTextVerticalAlignment(scribus.ALIGNV_BOTTOM, tt)
    rule = scribus.createLine(x, y+11, x+80, y+11)
    scribus.setLineColor('FC-Red', rule)
    scribus.setLineWidth(4.25, rule)
    g = scribus.groupObjects([tt, rule])
    copy_out(g, OUT + '/TitleRule.sce')

    # ---------- 10. SideRule: thick left accent bar + body
    x, y = 120.0, 20.0
    bar = scribus.createRect(x, y, 3, 40)
    scribus.setFillColor('FC-Red', bar)
    scribus.setLineColor('None', bar)
    body = scribus.createText(x+5, y, 75, 40)
    scribus.setText(BODY, body)
    scribus.setStyle('FC Body ML', body)
    scribus.setTextDistances(2, 2, 2, 2, body)
    g = scribus.groupObjects([bar, body])
    copy_out(g, OUT + '/SideRule.sce')

    # ---------- 11. PullQuote: rules above and below, centered quote
    x, y = 20.0, 60.0
    top = scribus.createLine(x, y, x+80, y)
    bot = scribus.createLine(x, y+35, x+80, y+35)
    for l in (top, bot):
        scribus.setLineColor('FC-Black', l)
        scribus.setLineWidth(2.85, l)
    t = scribus.createText(x+2, y+3, 76, 29)
    scribus.setText(QUOTE, t)
    scribus.setStyle('FC Quote ML', t)
    scribus.setTextVerticalAlignment(scribus.ALIGNV_CENTERED, t)
    g = scribus.groupObjects([top, bot, t])
    copy_out(g, OUT + '/PullQuote.sce')

    # ---------- 12. NumberBox: accent number badge + body (listicles)
    x, y = 120.0, 80.0
    badge = scribus.createText(x, y, 14, 14)
    scribus.setFillColor('FC-Red', badge)
    scribus.setText(u'1', badge)
    scribus.setFont(latinb, badge)
    scribus.setFontSize(20, badge)
    scribus.setTextColor('FC-White', badge)
    scribus.setTextAlignment(scribus.ALIGN_CENTERED, badge)
    scribus.setTextVerticalAlignment(scribus.ALIGNV_CENTERED, badge)
    body = scribus.createText(x, y+16, 80, 28)
    scribus.setText(BODY, body)
    scribus.setStyle('FC Body ML', body)
    scribus.setTextDistances(2, 2, 2, 2, body)
    g = scribus.groupObjects([badge, body])
    copy_out(g, OUT + '/NumberBox.sce')

    # ---------- 13. ShadowBox: offset solid shadow behind white text box
    x, y = 20.0, 110.0
    shadow = scribus.createRect(x+3, y+3, 80, 40)
    scribus.setFillColor('FC-Black', shadow)
    scribus.setLineColor('None', shadow)
    boxf = scribus.createText(x, y, 80, 40)
    scribus.setFillColor('FC-White', boxf)
    scribus.setLineColor('FC-Black', boxf)
    scribus.setLineWidth(0.5, boxf)
    scribus.setText(BODY, boxf)
    scribus.setStyle('FC Body ML', boxf)
    scribus.setTextDistances(3, 3, 3, 3, boxf)
    g = scribus.groupObjects([shadow, boxf])
    copy_out(g, OUT + '/ShadowBox.sce')

    scribus.saveDocAs(OUT + '/faircode-frames-wave2a.sla')
    img = scribus.ImageExport()
    img.type = 'PNG'
    img.dpi = 150
    img.saveAs(OUT + '/wave2a-page.png')
    log('DONE')

try:
    main()
except Exception:
    os.makedirs(OUT, exist_ok=True)
    with open(LOG, 'a') as f:
        f.write('FAILED\n' + traceback.format_exc())
