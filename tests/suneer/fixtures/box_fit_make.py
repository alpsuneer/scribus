# Fixture for tests/suneer/box_fit.sh: Malayalam frames 8-12 pt, auto / fixed line spacing, left / justified,
# each far too tall and ending in empty paragraphs; plus a welded, a linked and a grouped story.
import scribus, sys
OUT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/box_fit_base.sla"
ML = ("സംസ്ഥാനത്ത് ഇന്ന് ശക്തമായ മഴയ്ക്ക് സാധ്യതയുണ്ടെന്ന് കാലാവസ്ഥാ നിരീക്ഷണ കേന്ദ്രം അറിയിച്ചു. "
      "തീരപ്രദേശങ്ങളിൽ ജാഗ്രതാ നിർദേശം നൽകി; മത്സ്യത്തൊഴിലാളികൾ കടലിൽ പോകരുതെന്നും മുന്നറിയിപ്പുണ്ട്.")
FONT = "DZ DB Text Bold"
scribus.newDocument((210, 297), (10, 10, 10, 10), scribus.PORTRAIT, 1, scribus.UNIT_MILLIMETERS, scribus.PAGE_1, 0, 1)

def frame(name, x, y, w, h, text, size, ls=None, align=0):
    f = scribus.createText(x, y, w, h, name)
    scribus.setText(text, f)
    scribus.setFont(FONT, f)
    scribus.setFontSize(size, f)
    if ls:
        scribus.setLineSpacingMode(0, f)
        scribus.setLineSpacing(ls, f)
    else:
        scribus.setLineSpacingMode(1, f)
    scribus.setTextAlignment(align, f)
    return f

singles = [(8, None, 0, "auto_left"), (8, 1.2, 3, "fixed120_justify"), (10, None, 0, "auto_left"),
           (10, 1.2, 3, "fixed120_justify"), (10, 1.5, 0, "fixed150_left"), (12, None, 0, "auto_left"),
           (12, 1.2, 3, "fixed120_justify")]
x, y = 12, 12
for size, ls, align, tag in singles:
    frame("S_%d_%s" % (size, tag), x, y, 60, 70, ML + "\r\r", size, size * ls if ls else None, align)
    x += 64
    if x > 140:
        x, y = 12, y + 74
# welded story: headline + body (welded by the self-test)
frame("ST_head", 12, 215, 90, 30, "കനത്ത മഴ: ജാഗ്രത", 14)
frame("ST_body", 12, 229, 90, 40, ML + "\r", 10)
# linked story: two columns
frame("CH_1", 110, 160, 40, 30, ML + " " + ML, 9)
c2 = frame("CH_2", 154, 160, 40, 30, "", 9)
scribus.linkTextFrames("CH_1", "CH_2")
# grouped story
frame("GR_head", 110, 225, 85, 12, "മുന്നറിയിപ്പ്", 12)
frame("GR_body", 110, 238, 85, 40, ML, 9)
scribus.groupObjects(["GR_head", "GR_body"])
scribus.saveDocAs(OUT)
