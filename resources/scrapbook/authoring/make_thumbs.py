# -*- coding: utf-8 -*-
# Build square white-background preview PNGs for Faircode Frames .sce entries.
# The scrapbook palette prefers a sibling <base>.png over the embedded
# previewData, so these ship next to the .sce files.
#
# Crops from the authoring page render (ImageExport PNG). Those renders place
# items at authored-number * ~5.905 px regardless of doc units, so crop boxes
# are given in the same mm numbers used by the author_*.py scripts.
#
# Usage: python3 make_thumbs.py <render.png> <outdir> then per-template entries
# are defined in TEMPLATES below (render key, name, x, y, w, h, margin_mm).

import sys
from PIL import Image

PX_PER_UNIT = 5.905
THUMB = 240

# name -> (render_key, x, y, w, h) in authored units (mm numbers)
TEMPLATES = {
    'QuoteA':     ('batch1', 20, 20, 80, 48),
    'TitleBox':   ('batch1', 120, 20, 80, 50),
    'TintedBox':  ('batch1', 20, 90, 80, 40),
    'QuoteB':     ('batch2', 20, 20, 80, 48),
    'CalloutL':   ('batch2', 120, 20, 85, 30),
    'CalloutR':   ('batch2', 120, 70, 82.5, 30),
    'AngledBar':  ('batch2', 20, 90, 80, 47),
    'Brackets':   ('batch2', 120, 120, 80, 40),
    'TitleRule':  ('wave2a', 20, 20, 80, 12),
    'SideRule':   ('wave2a', 120, 20, 80, 40),
    'PullQuote':  ('wave2a', 20, 60, 80, 35),
    'NumberBox':  ('wave2a', 120, 80, 80, 44),
    'ShadowBox':  ('wave2a', 20, 110, 83, 43),
    'Ribbon':     ('wave2b', 20, 20, 80, 12),
    'Badge':      ('wave2b', 120, 20, 80, 20),
    'DoubleBox':  ('wave2b', 20, 60, 80, 40),
    'DashedBox':  ('wave2b', 20, 110, 80, 40),
    'Highlight':  ('wave2b', 120, 60, 80, 9),
    'AttrQuote':  ('wave3', 20, 20, 80, 55),
    'SectionStrip': ('wave3', 120, 20, 80, 11),
    'NewsBox':    ('wave3', 19, 89, 81, 46),
    'BannerHead': ('wave3', 120, 90, 80, 17),
    'LedeBars':   ('wave3', 20, 150, 80, 9),
}

def make_thumb(render, x, y, w, h, out, margin=4.0):
    x0 = int((x - margin) * PX_PER_UNIT)
    y0 = int((y - margin) * PX_PER_UNIT)
    x1 = int((x + w + margin) * PX_PER_UNIT)
    y1 = int((y + h + margin) * PX_PER_UNIT)
    crop = render.crop((max(x0, 0), max(y0, 0), x1, y1)).convert('RGB')
    side = max(crop.size)
    canvas = Image.new('RGB', (side, side), (255, 255, 255))
    canvas.paste(crop, ((side - crop.size[0]) // 2, (side - crop.size[1]) // 2))
    canvas = canvas.resize((THUMB, THUMB), Image.LANCZOS)
    canvas.save(out)
    print('wrote', out)

def main():
    # usage: make_thumbs.py <outdir> key=render.png [key=render.png ...]
    # only templates whose render key is supplied are generated
    outdir = sys.argv[1]
    renders = dict(a.split('=', 1) for a in sys.argv[2:])
    imgs = {k: Image.open(v) for k, v in renders.items()}
    for name, (key, x, y, w, h) in TEMPLATES.items():
        if key in imgs:
            make_thumb(imgs[key], x, y, w, h, '%s/%s.png' % (outdir, name))

if __name__ == '__main__':
    main()
