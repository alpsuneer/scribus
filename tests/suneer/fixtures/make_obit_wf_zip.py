#!/usr/bin/env python3
"""obit.zip for tests/suneer/news_obit_wf_photos.sh: an obituary page as the workflow's .sla download
ships it (slaTemplates.service.ts: a stored zip with <page>_<date>.sla + images/<n>_obit_<file>,
PFILE="images/...", frames written by buildImagePageObject / buildTextPageObject: legacy attribute
names, random ItemID, a caption named caption_<imageItemId> welded to its photo).

Page at 100,20 (the open-page fixture). Photos 120x140 pt:
  0_obit_face1.jpg (40,50)  1_obit_face2.jpg (200,50)  2_obit_face3.jpg (360,50)   real headshots
  3_obit_noface.jpg (40,300) a picture without a face   4_obit_broken.jpg (200,300) not a JPEG
  5_news.jpg (360,300)       a news photo (not an obituary photo)
  caption_1700000001 (40,200 120x20) welded to face1; a Malayalam name under each obituary photo.
usage: make_obit_wf_zip.py <news_open_page.sla> <face1> <face2> <face3> <noface> <news> <out.zip> <date>"""
import sys
import zipfile

src, f1, f2, f3, nf, nw, out, date = sys.argv[1:9]
sla = open(src, encoding="utf-8").read()
X0, Y0 = 100.0, 20.0


def rect(w, h):
    return "M0 0 L%.4f 0 L%.4f %.4f L0 %.4f L0 0 Z" % (w, w, h, h)


def image(iid, x, y, w, h, pfile, weld=None):
    welds = '<WeldEntry Target="%d" WX="%.4f" WY="%.4f"/>' % (weld, w / 2, h) if weld else ""
    return ('<PageObject XPOS="%.4f" YPOS="%.4f" OwnPage="0" ItemID="%d" PTYPE="2" WIDTH="%.4f" HEIGHT="%.4f" FRTYPE="0" '
            '%sCLIPEDIT="0" PWIDTH="0.2" PLINEART="1" LOCALSCX="0.300000" LOCALSCY="0.300000" LOCALX="0.0000" LOCALY="0.0000" '
            'LOCALROT="0" PICART="1" SCALETYPE="1" RATIO="1" COLUMNS="1" COLGAP="0" AUTOTEXT="0" EXTRA="0.0000" TEXTRA="0.0000" '
            'BEXTRA="0.0000" REXTRA="0.0000" VAlign="0" FLOP="0" PLTSHOW="0" BASEOF="0" textPathType="0" textPathFlipped="0" '
            'TEXTFLOWMODE="0" PFILE="%s" path="%s" copath="%s" gXpos="%.4f" gYpos="%.4f" gWidth="0" gHeight="0" LAYER="0" '
            'NEXTITEM="-1" BACKITEM="-1"%s'
            % (X0 + x, Y0 + y, iid, w, h, ('IsWeldItem="1" WeldSource="%d" ' % iid) if weld else "", pfile, rect(w, h), rect(w, h),
               X0 + x, Y0 + y, (">" + welds + "</PageObject>") if welds else "/>"))


def text(iid, x, y, w, h, chars, auto=None, weld=None):
    welds = '<WeldEntry Target="%d" WX="%.4f" WY="0.0000"/>' % (weld, w / 2) if weld else ""
    return ('<PageObject XPOS="%.4f" YPOS="%.4f" OwnPage="0" ItemID="%d" PTYPE="4" WIDTH="%.4f" HEIGHT="%.4f" FRTYPE="0" CLIPEDIT="0" '
            'PWIDTH="0.2000" PLINEART="1" PCOLOR="None" PCOLOR2="None" SHADE="100" SHADE2="100" FILLTR="1" %s%sTEXTFLOWMODE="0" '
            'LOCALSCX="1" LOCALSCY="1" LOCALX="0" LOCALY="0" LOCALROT="0" PICART="1" SCALETYPE="1" RATIO="1" COLUMNS="1" COLGAP="0.0000" '
            'AUTOTEXT="0" EXTRA="0.0000" TEXTRA="0.0000" BEXTRA="0.0000" REXTRA="0.0000" VAlign="0" FLOP="1" PLTSHOW="0" BASEOF="0" '
            'textPathType="0" textPathFlipped="0" path="%s" copath="%s" gXpos="%.4f" gYpos="%.4f" gWidth="0" gHeight="0" LAYER="0" '
            'NEXTITEM="-1" BACKITEM="-1"><StoryText><DefaultStyle Language="ml"/><ITEXT CH="%s"/><trail/></StoryText>%s</PageObject>'
            % (X0 + x, Y0 + y, iid, w, h, ('IsWeldItem="1" WeldSource="%d" ' % iid) if weld else "",
               ('AutoName="%s" ' % auto) if auto else "", rect(w, h), rect(w, h), X0 + x, Y0 + y, chars, welds))


objs = [
    image(1700000001, 40, 50, 120, 140, "images/0_obit_face1.jpg", weld=1700000011),
    text(1700000011, 40, 200, 120, 20, "ഫോട്ടോ", auto="caption_1700000001", weld=1700000001),
    image(1700000002, 200, 50, 120, 140, "images/1_obit_face2.jpg"),
    image(1700000003, 360, 50, 120, 140, "images/2_obit_face3.jpg"),
    image(1700000004, 40, 300, 120, 140, "images/3_obit_noface.jpg"),
    image(1700000005, 200, 300, 120, 140, "images/4_obit_broken.jpg"),
    image(1700000006, 360, 300, 120, 140, "images/5_news.jpg"),
    text(1700000021, 200, 200, 120, 20, "ജാനകി"),
    text(1700000022, 360, 200, 120, 20, "ദിവാകരൻ നായർ"),
]
assert sla.count("</Document>") == 1
sla = sla.replace("</Document>", "".join(objs) + "</Document>")
with zipfile.ZipFile(out, "w", zipfile.ZIP_STORED) as z:
    z.writestr("page_3_%s.sla" % date, sla)
    for name, path in (("0_obit_face1.jpg", f1), ("1_obit_face2.jpg", f2), ("2_obit_face3.jpg", f3), ("3_obit_noface.jpg", nf), ("5_news.jpg", nw)):
        z.write(path, "images/" + name)
    z.writestr("images/4_obit_broken.jpg", b"this is not a JPEG " * 100)
