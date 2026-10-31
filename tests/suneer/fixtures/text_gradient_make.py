import scribus, sys
scribus.newDocument(scribus.PAPER_A3, (15, 15, 15, 15), scribus.PORTRAIT, 1, scribus.UNIT_MILLIMETERS, scribus.NOFACINGPAGES, scribus.FIRSTPAGERIGHT, 1)
fonts = scribus.getFontNames()
font = next((f for f in ["DZ DB Display 5 Bold", "DZ DB Display 4 Medium Condensed", "DZ DB Display 1 Roman"] if f in fonts), None)
print("FONT", font, file=sys.stderr)
head = "മിസ്റ്റർ ട്രംപ്\rഇന്ത്യയിലേക്ക്"
specs = [("GradLine", 15, 20), ("GradFrame", 15, 115), ("GradRadial", 15, 210), ("GradRange", 15, 305)]
for name, x, y in specs:
    f = scribus.createText(x, y, 267, 85, name)
    scribus.setText(head, f)
    if font:
        scribus.setFont(font, f)
    scribus.setFontSize(80, f)
    scribus.setLineSpacing(84, f)
    scribus.setTextColor("Black", f)
# a solid reference headline: same text, black
f = scribus.createText(15, 395, 267, 20, "Solid")
scribus.setText("കറുത്ത തലക്കെട്ട് (solid)", f)
if font:
    scribus.setFont(font, f)
scribus.setFontSize(36, f)
scribus.saveDocAs("/home/s1/tmp-textgrad/base.sla")
print("SAVED", file=sys.stderr)
