#!/usr/bin/env python3
# For general Scribus (>=1.3.2) copyright and licensing information please
# refer to the COPYING file provided with the program.
#
# Imposes two single-page PDFs (left/right) onto one CTP plate-sized PDF
# page, with optional plate marks (registration marks, CMYK colour bar,
# slug line, per-page auto/trim marks). Invoked by ScImpositionEngine
# (scimpositionengine.cpp) via QProcess -- see that file for the C++ side
# of this contract. Requires pypdf and reportlab.
#
# Coordinates: command-line arguments use the same top-left-origin,
# millimetre convention as ImpositionSettings/ScImpositionEngine::PlacedPage
# (marginTopMm measures down from the plate's top edge). PDF space itself is
# bottom-left-origin, points -- every mm value is converted and flipped
# internally; nothing outside this script needs to know that.

import argparse
import sys

from pypdf import PdfReader, PdfWriter, Transformation
from reportlab.pdfgen import canvas
from reportlab.lib.units import mm


def mm2pt(v):
    return v * 72.0 / 25.4


class Patch:
    __slots__ = ("cmyk", "label")

    def __init__(self, cmyk, label):
        self.cmyk = cmyk
        self.label = label


COLOUR_BAR_PATCHES = [
    Patch((1, 0, 0, 0), "C100"), Patch((0, 1, 0, 0), "M100"), Patch((0, 0, 1, 0), "Y100"), Patch((0, 0, 0, 1), "K100"),
    Patch((0.5, 0, 0, 0), "C50"), Patch((0, 0.5, 0, 0), "M50"), Patch((0, 0, 0.5, 0), "Y50"), Patch((0, 0, 0, 0.5), "K50"),
    Patch((1, 1, 1, 1), "Reg"),
]
REGISTRATION_CMYK = (1, 1, 1, 1)


def draw_registration_mark(c, cx_mm, cy_mm):
    """Circle + crosshair, 6mm diameter, matching
    ScImpositionEngine's addRegistrationMark()."""
    diameter_mm = 6.0
    radius_mm = diameter_mm / 2.0
    arm_mm = 4.0
    c.setStrokeColorCMYK(*REGISTRATION_CMYK)
    c.setLineWidth(0.5)
    c.circle(cx_mm * mm, cy_mm * mm, radius_mm * mm, stroke=1, fill=0)
    c.setLineWidth(0.25)
    c.line((cx_mm - arm_mm) * mm, cy_mm * mm, (cx_mm + arm_mm) * mm, cy_mm * mm)
    c.line(cx_mm * mm, (cy_mm - arm_mm) * mm, cx_mm * mm, (cy_mm + arm_mm) * mm)


def draw_regmarks(c, plate_w_mm, plate_h_mm):
    """4 corners + center top/bottom, 10mm inset. The position set is
    symmetric under a top/bottom flip, so no top/bottom-origin conversion
    is needed here -- unlike the colour bar and slug line below."""
    inset = 10.0
    positions = [
        (inset, inset), (plate_w_mm - inset, inset),
        (inset, plate_h_mm - inset), (plate_w_mm - inset, plate_h_mm - inset),
        (plate_w_mm / 2.0, inset), (plate_w_mm / 2.0, plate_h_mm - inset),
    ]
    for cx, cy in positions:
        draw_registration_mark(c, cx, cy)


def draw_colour_bar(c, plate_h_mm):
    """9 patches, 6x6mm, left-aligned from x=20mm, each labelled below in
    6pt -- matches ScImpositionEngine's addColorBar(). Patches sit with
    their bottom edge 5mm above the plate's bottom edge (bottom-origin,
    so no flip needed -- this is naturally bottom-anchored already)."""
    patch_mm = 6.0
    start_x_mm = 20.0
    bottom_clearance_mm = 5.0
    label_h_mm = 4.0

    patch_bottom_y_mm = bottom_clearance_mm
    label_bottom_y_mm = bottom_clearance_mm - label_h_mm

    c.setFont("Helvetica", 6)
    for i, patch in enumerate(COLOUR_BAR_PATCHES):
        x_mm = start_x_mm + i * patch_mm
        c.setFillColorCMYK(*patch.cmyk)
        c.setStrokeColorCMYK(0, 0, 0, 1)
        c.setLineWidth(0.5)
        c.rect(x_mm * mm, patch_bottom_y_mm * mm, patch_mm * mm, patch_mm * mm, stroke=1, fill=1)
        c.setFillColorCMYK(0, 0, 0, 1)
        c.drawCentredString((x_mm + patch_mm / 2.0) * mm, (label_bottom_y_mm + label_h_mm / 2.0 - 1.0) * mm, patch.label)


def draw_slug_line(c, plate_w_mm, plate_h_mm, pub_code, edition_code, left_page_num, right_page_num):
    """'{PUB} | {DATE} | Page {L}-{R} | Edition {ED}' in the top margin,
    Helvetica 8pt -- matches ScImpositionEngine's addSlugLine()."""
    import datetime
    text = "%s | %s | Page %d-%d | Edition %s" % (
        pub_code or "PUB",
        datetime.date.today().strftime("%d-%m-%Y"),
        left_page_num, right_page_num,
        edition_code or "ED",
    )
    x_mm = 10.0
    top_margin_mm = 3.0
    box_h_mm = 6.0
    y_baseline_mm = plate_h_mm - top_margin_mm - box_h_mm / 2.0 - 1.5  # roughly vertically centered in the 6mm band
    c.setFillColorCMYK(0, 0, 0, 1)
    c.setFont("Helvetica", 8)
    c.drawString(x_mm * mm, y_baseline_mm * mm, text)


def draw_auto_marks_for_page(c, page_x_mm, page_y_up_mm, page_w_mm, page_h_mm):
    """0.25pt lines, 5mm long, 3mm offset from each page edge, at that
    page's 4 corners -- matches ScImpositionEngine's addCropMarksForPage().
    page_y_up_mm is the page's bottom edge in bottom-origin mm (already
    converted by the caller), so this function works entirely in
    bottom-origin coordinates."""
    offset_mm = 3.0
    length_mm = 5.0
    c.setStrokeColorCMYK(*REGISTRATION_CMYK)
    c.setLineWidth(0.25)

    left = page_x_mm
    right = page_x_mm + page_w_mm
    bottom = page_y_up_mm
    top = page_y_up_mm + page_h_mm

    def pair(corner_x, corner_y, dir_x, dir_y):
        hx1 = corner_x + (-(offset_mm + length_mm) if dir_x < 0 else offset_mm)
        hx2 = corner_x + (-offset_mm if dir_x < 0 else (offset_mm + length_mm))
        hy = corner_y + dir_y * offset_mm
        c.line(hx1 * mm, hy * mm, hx2 * mm, hy * mm)

        vy1 = corner_y + (-(offset_mm + length_mm) if dir_y < 0 else offset_mm)
        vy2 = corner_y + (-offset_mm if dir_y < 0 else (offset_mm + length_mm))
        vx = corner_x + dir_x * offset_mm
        c.line(vx * mm, vy1 * mm, vx * mm, vy2 * mm)

    pair(left, top, -1, 1)
    pair(right, top, 1, 1)
    pair(left, bottom, -1, -1)
    pair(right, bottom, 1, -1)


def build_marks_overlay(path, args, left_w_mm, left_h_mm, right_w_mm, right_h_mm):
    plate_w_pt = mm2pt(args.plate_w)
    plate_h_pt = mm2pt(args.plate_h)
    c = canvas.Canvas(path, pagesize=(plate_w_pt, plate_h_pt))

    # Bottom-origin Y for each page, derived the same way main() derives it
    # for pypdf placement -- see the comment there for why this must be
    # top-margin-driven (matching ScImpositionEngine::computeLeftPlacement/
    # computeRightPlacement), not an independently-specified bottom margin.
    left_y_up = args.plate_h - args.top_margin - left_h_mm
    right_y_up = args.plate_h - args.top_margin - right_h_mm
    right_x = args.left_margin + left_w_mm + args.gutter

    if args.auto_marks:
        draw_auto_marks_for_page(c, args.left_margin, left_y_up, left_w_mm, left_h_mm)
        draw_auto_marks_for_page(c, right_x, right_y_up, right_w_mm, right_h_mm)

    if args.regmarks:
        draw_regmarks(c, args.plate_w, args.plate_h)

    if args.colour_bar:
        draw_colour_bar(c, args.plate_h)

    if args.furnitures:
        draw_slug_line(c, args.plate_w, args.plate_h, args.pub, args.edition, args.left_page_num, args.right_page_num)

    c.save()


def main():
    p = argparse.ArgumentParser(description="Impose two PDFs onto one CTP plate.")
    p.add_argument("--left", required=True, help="Left source PDF path")
    p.add_argument("--right", required=True, help="Right source PDF path")
    p.add_argument("--output", required=True, help="Output imposed PDF path")
    p.add_argument("--plate-w", type=float, required=True, help="Plate width, mm")
    p.add_argument("--plate-h", type=float, required=True, help="Plate height, mm")
    p.add_argument("--left-margin", type=float, required=True, help="mm from plate's left edge")
    p.add_argument("--top-margin", type=float, required=True, help="mm from plate's top edge -- drives page Y placement")
    p.add_argument("--gutter", type=float, required=True, help="mm between the two pages")
    p.add_argument("--print-area", type=int, default=1, help="1=place page content, 0=marks-only plate")
    p.add_argument("--regmarks", type=int, default=1)
    p.add_argument("--auto-marks", type=int, default=0)
    p.add_argument("--furnitures", type=int, default=1)
    p.add_argument("--colour-bar", type=int, default=1)
    p.add_argument("--pub", default="")
    p.add_argument("--edition", default="")
    p.add_argument("--left-page-num", type=int, default=1)
    p.add_argument("--right-page-num", type=int, default=1)
    args = p.parse_args()

    try:
        left_reader = PdfReader(args.left)
        right_reader = PdfReader(args.right)
        left_page = left_reader.pages[0]
        right_page = right_reader.pages[0]
    except Exception as exc:
        print("Could not read source PDF: %s" % exc, file=sys.stderr)
        return 1

    left_w_mm = float(left_page.mediabox.width) * 25.4 / 72.0
    left_h_mm = float(left_page.mediabox.height) * 25.4 / 72.0
    right_w_mm = float(right_page.mediabox.width) * 25.4 / 72.0
    right_h_mm = float(right_page.mediabox.height) * 25.4 / 72.0

    plate_w_pt = mm2pt(args.plate_w)
    plate_h_pt = mm2pt(args.plate_h)

    writer = PdfWriter()
    plate = writer.add_blank_page(width=plate_w_pt, height=plate_h_pt)

    if args.print_area:
        # Y placement is top-margin-driven, matching
        # ScImpositionEngine::computeLeftPlacement/computeRightPlacement
        # (both pages hang from the SAME top margin, independent of their
        # own heights) -- converted here to PDF's bottom-origin space.
        left_x_mm = args.left_margin
        left_y_mm = args.plate_h - args.top_margin - left_h_mm
        right_x_mm = args.left_margin + left_w_mm + args.gutter
        right_y_mm = args.plate_h - args.top_margin - right_h_mm

        plate.merge_transformed_page(left_page, Transformation().translate(mm2pt(left_x_mm), mm2pt(left_y_mm)))
        plate.merge_transformed_page(right_page, Transformation().translate(mm2pt(right_x_mm), mm2pt(right_y_mm)))

    if args.regmarks or args.colour_bar or args.furnitures or args.auto_marks:
        marks_path = args.output + ".marks.pdf"
        build_marks_overlay(marks_path, args, left_w_mm, left_h_mm, right_w_mm, right_h_mm)
        marks_reader = PdfReader(marks_path)
        plate.merge_page(marks_reader.pages[0])
        import os
        os.remove(marks_path)

    try:
        with open(args.output, "wb") as f:
            writer.write(f)
    except Exception as exc:
        print("Could not write output PDF: %s" % exc, file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
