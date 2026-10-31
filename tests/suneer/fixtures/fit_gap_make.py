# Fixture for tests/suneer/fit_gap.sh: two A4 pages per fit path (menu, toolbar, placement, caption, box) -
# Malayalam on the first, English on the second - each with 10 frames: DBI Text 8, 10, 12, 18 pt and
# DZ DB Display 18 pt x line spacing auto / fixed (120 %); 90 mm apart, so a grown frame meets no other. Text ends in two empty paragraphs, as text pasted from elsewhere.
# Every frame starts too tall, text distance 1 mm on all sides, first line offset = font ascent (default).
import scribus, sys
OUT = sys.argv[1]
TRAIL = "" if len(sys.argv) > 2 and sys.argv[2] == "clean" else "\r\r"   # clean: no empty paragraphs at the end
ML = "സംസ്ഥാനത്ത് ഇന്ന് ശക്തമായ മഴയ്ക്ക് സാധ്യതയുണ്ടെന്ന് കാലാവസ്ഥാ കേന്ദ്രം അറിയിച്ചു."
EN = "Heavy rain is likely in the state today, the weather centre said on Sunday morning."
FACES = [("DBI Text Regular", 8), ("DBI Text Regular", 10), ("DBI Text Regular", 12), ("DBI Text Regular", 18), ("DZ DB Display 3 Medium", 18)]
scribus.newDocument((210, 297), (10, 10, 10, 10), scribus.PORTRAIT, 1, scribus.UNIT_MILLIMETERS, scribus.PAGE_1, 0, 10)
MM = 72.0 / 25.4
for pg in range(1, 11):
    scribus.gotoPage(pg)
    lang, text = (("ml", ML), ("en", EN))[(pg - 1) % 2]
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
