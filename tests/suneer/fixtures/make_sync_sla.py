#!/usr/bin/env python3
"""sync.sla for tests/suneer/news_page_sync.sh and news_page_takeover.sh: the open-page fixture + frames
as the workflow's export writes them (slaXml.ts buildTextPageObject / buildImagePageObject: legacy
attribute names, random ItemID, no name, soft hyphens in the headline). Page at 100,20: row n-1
(30,40 200x300) -> frame 130,60 with a welded photo under it; row n-2 (250,40 200x150) -> 350,60;
n-4 has no row (moved) -> 130,500.
usage: make_sync_sla.py <news_open_page.sla> <out sync.sla>"""
import sys
src, dst = sys.argv[1], sys.argv[2]
sla = open(src, encoding="utf-8").read()
SH = "­"
def text(iid, x, y, w, h, title, body, weld=None):
    rect = "M0 0 L%.4f 0 L%.4f %.4f L0 %.4f L0 0 Z" % (w, w, h, h)
    welds = '<WeldEntry Target="%d" WX="%.4f" WY="%.4f"/>' % (weld, w / 2, h) if weld else ""
    story = ('<StoryText><DefaultStyle Language="ml"/><ITEXT CH="%s"/><para ALIGN="0"/><ITEXT CH="%s"/><trail/></StoryText>' % (title, body))
    return ('<PageObject XPOS="%.4f" YPOS="%.4f" OwnPage="0" ItemID="%d" PTYPE="4" WIDTH="%.4f" HEIGHT="%.4f" FRTYPE="0" CLIPEDIT="0" '
            'PWIDTH="0.2000" PLINEART="1" PCOLOR="None" PCOLOR2="None" SHADE="100" SHADE2="100" FILLTR="1" %sTEXTFLOWMODE="0" '
            'LOCALSCX="1" LOCALSCY="1" LOCALX="0" LOCALY="0" LOCALROT="0" PICART="1" SCALETYPE="1" RATIO="1" COLUMNS="1" COLGAP="0.0000" '
            'AUTOTEXT="0" EXTRA="0.0000" TEXTRA="0.0000" BEXTRA="0.0000" REXTRA="0.0000" VAlign="0" VerticalAlignment="0" FLOP="1" PLTSHOW="0" '
            'BASEOF="0" textPathType="0" textPathFlipped="0" path="%s" copath="%s" gXpos="%.4f" gYpos="%.4f" gWidth="0" gHeight="0" '
            'LAYER="0" NEXTITEM="-1" BACKITEM="-1">%s%s</PageObject>'
            % (x, y, iid, w, h, ('IsWeldItem="1" WeldSource="%d" ' % iid) if weld else "", rect, rect, x, y, story, welds))
def image(iid, x, y, w, h, weld):
    rect = "M0 0 L%.4f 0 L%.4f %.4f L0 %.4f L0 0 Z" % (w, w, h, h)
    return ('<PageObject XPOS="%.4f" YPOS="%.4f" OwnPage="0" ItemID="%d" PTYPE="2" WIDTH="%.4f" HEIGHT="%.4f" FRTYPE="0" '
            'IsWeldItem="1" WeldSource="%d" CLIPEDIT="0" PWIDTH="0.2" PLINEART="1" LOCALSCX="1.000000" LOCALSCY="1.000000" LOCALX="0.0000" '
            'LOCALY="0.0000" LOCALROT="0" PICART="1" SCALETYPE="1" RATIO="1" COLUMNS="1" COLGAP="0" AUTOTEXT="0" EXTRA="0.0000" TEXTRA="0.0000" '
            'BEXTRA="0.0000" REXTRA="0.0000" VAlign="0" FLOP="0" PLTSHOW="0" BASEOF="0" textPathType="0" textPathFlipped="0" TEXTFLOWMODE="0" '
            'PFILE="" path="%s" copath="%s" gXpos="%.4f" gYpos="%.4f" gWidth="0" gHeight="0" LAYER="0" NEXTITEM="-1" BACKITEM="-1">'
            '<WeldEntry Target="%d" WX="%.4f" WY="0.0000"/></PageObject>' % (x, y, iid, w, h, iid, rect, rect, x, y, weld, w / 2))
body = "ജില്ലയിൽ ഇന്നലെ നടന്ന പരിപാടിയിൽ നിരവധി പേർ പങ്കെടുത്തു."
objs = [
    text(1600000001, 130, 60, 200, 150, "തിരു" + SH + "വ" + SH + "ന" + SH + "ന്ത" + SH + "പു" + SH + "ര" + SH + "ത്ത് കനത്ത മഴ", body, weld=1600000002),
    image(1600000002, 130, 215, 200, 120, 1600000001),
    text(1600000003, 350, 60, 200, 150, "കണ്ണൂ" + SH + "രിൽ പുതിയ പാലം തുറന്നു", body),
    text(1600000004, 130, 500, 200, 120, "കോഴി" + SH + "ക്കോട് പുസ്‌" + SH + "ത" + SH + "കോ" + SH + "ത്സ" + SH + "വം തുടങ്ങി", body),
]
assert sla.count("</Document>") == 1
sla = sla.replace("</Document>", "".join(objs) + "</Document>")
open(dst, "w", encoding="utf-8").write(sla)
