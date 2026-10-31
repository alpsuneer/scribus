# Fixture for tests/suneer/fit_gap.sh: two A4 pages per fit path (menu, toolbar, placement, caption, box) -
# Malayalam on the first, English on the second - each with 10 frames: DBI Text 8, 10, 12, 18 pt and
# DZ DB Display 18 pt x line spacing auto / fixed (120 %); 90 mm apart, so a grown frame meets no other. Text ends in two empty paragraphs, as text pasted from elsewhere.
# Every frame starts too tall, text distance 1 mm on all sides, first line offset = font ascent (default).
import scribus, sys
OUT = sys.argv[1]
MODE = sys.argv[2] if len(sys.argv) > 2 else ""
TRAIL = "" if MODE == "clean" else "\r\r"   # clean: no empty paragraphs at the end
# columns: a long Malayalam story in 1 / 2 / 3 / 4 columns at 8 and 10 pt, plus a linked chain of two frames
ML = "സംസ്ഥാനത്ത് ഇന്ന് ശക്തമായ മഴയ്ക്ക് സാധ്യതയുണ്ടെന്ന് കാലാവസ്ഥാ കേന്ദ്രം അറിയിച്ചു."
EN = "Heavy rain is likely in the state today, the weather centre said on Sunday morning."
FACES = [("DBI Text Regular", 8), ("DBI Text Regular", 10), ("DBI Text Regular", 12), ("DBI Text Regular", 18), ("DZ DB Display 3 Medium", 18)]
scribus.newDocument((210, 297), (10, 10, 10, 10), scribus.PORTRAIT, 1, scribus.UNIT_MILLIMETERS, scribus.PAGE_1, 0, 1 if MODE == "app" else 10)
MM = 72.0 / 25.4
if MODE == "app":
    # the running-app check: one page, rows of 1-5 column frames 100 mm wide for the menu action, the toolbar
    # action and Styled Paste (targets empty; S_src holds the story), 9 pt, column gap 3 mm, distances 1 mm
    STORY = ML + " " + ML + " " + ML + " " + ML
    def setup(f, cols, with_text):
        if with_text:
            scribus.setText(STORY + TRAIL, f)
        scribus.setFont("DBI Text Regular", f); scribus.setFontSize(9, f); scribus.setLineSpacingMode(1, f)
        scribus.setColumns(cols, f); scribus.setColumnGap(3, f); scribus.setTextDistances(1, 1, 1, 1, f)
    # page 1: menu (left) and toolbar (right) rows, 90 mm wide; page 2: paste targets and S_src
    for path, x in (("menu", 12), ("toolbar", 108)):
        y = 14
        for cols in range(1, 6):
            f = scribus.createText(x, y, 90, 45, "A_%s_c%d" % (path, cols))
            setup(f, cols, True)
            y += 50
    scribus.newPage(-1)
    scribus.gotoPage(2)
    y = 14
    for cols in range(1, 6):
        f = scribus.createText(12, y, 90, 14, "A_paste_c%d" % cols)
        setup(f, cols, False)
        y += 34
    src = scribus.createText(108, 14, 90, 60, "S_src")
    setup(src, 1, True)
    scribus.saveDocAs(OUT)
    raise SystemExit
for pg in range(1, 11):
    scribus.gotoPage(pg)
    lang, text = (("ml", ML), ("en", EN))[(pg - 1) % 2]
    if MODE == "columns":
        if (pg - 1) % 2 == 1:
            continue
        i = 0
        for size in (8, 10):
            for cols in (1, 2, 3, 4):
                x, y = 12 + (i % 4) * 48, 12 + (i // 4) * 120
                name = "F_p%d_ml_text%d_c%d" % (pg, size, cols)
                f = scribus.createText(x, y, 44, 110, name)
                scribus.setText(ML + " " + ML + " " + ML + TRAIL, f)
                scribus.setFont("DBI Text Regular", f); scribus.setFontSize(size, f)
                scribus.setLineSpacingMode(1, f)
                scribus.setColumns(cols, f); scribus.setColumnGap(2, f)
                scribus.setTextDistances(1, 1, 1, 1, f)
                i += 1
        a = scribus.createText(108, 250, 44, 14, "L_p%d_1" % pg)
        b = scribus.createText(156, 250, 44, 25, "L_p%d_2" % pg)
        scribus.setText(ML + " " + ML + " " + ML + TRAIL, a)
        for f in (a, b):
            scribus.setFont("DBI Text Regular", f); scribus.setFontSize(9, f); scribus.setTextDistances(1, 1, 1, 1, f)
        scribus.linkTextFrames(a, b)
        continue
    i = 0
    for font, size in FACES:
        for sp in ("auto", "fixed"):
            col, row = i % 4, i // 4
            x, y = 12 + col * 48, 12 + row * 90
            name = "F_p%d_%s_%s%d_%s" % (pg, lang, "disp" if "Display" in font else "text", size, sp)
            f = scribus.createText(x, y, 44, 50, name)
            scribus.setText(text + TRAIL, f)
            scribus.setFont(font, f)
            scribus.setFontSize(size, f)
            if sp == "fixed":
                scribus.setLineSpacingMode(0, f)
                scribus.setLineSpacing(size * 1.2, f)
            else:
                scribus.setLineSpacingMode(1, f)
            scribus.setTextDistances(1, 1, 1, 1, f)
            i += 1
scribus.saveDocAs(OUT)
