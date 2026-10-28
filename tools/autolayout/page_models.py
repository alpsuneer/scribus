#!/usr/bin/env python3
"""Page Models: the layout of whole old pages (.sla, 1.4 / 1.5 / 1.7) as reusable
models - every story as a slot on the grid, obituary modules, ad areas, fixed
items (masthead, folio), rules and boxes. Never the text or the pictures.

  page_models.py [--list] <file.sla | folder> ...

--list      only PAGE lines (file, page count, page numbers) - for the page picker
otherwise   one JSON object per page on stdout, one per line:
              {"file": ..., "page": i (0-based), "pages": n, "model": {...}}
            progress on stderr: "PROGRESS done total file"
A page that cannot be read gives {"file":..., "page": i, "error": "..."}.
Sizes are mm, page relative."""
import sys, os, json, glob, math, traceback
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from slaparse import Page, classify, PT, HEADLINE_RE
import analyze as AN

OBIT_PAGE_MIN = 6          # at least this many obituary modules: an obituary page
PHOTO_MIN_MM2 = 100.0
AD_MIN_MM2 = 400.0        # smaller things on the Advt layer are logos, rules, folio bits

def r1(v):
    return round(float(v), 1)

def rect(it):
    return dict(x=r1(it['x']), y=r1(it['y']), w=r1(it['w']), h=r1(it['h']))

def is_ad(it):
    name = (it.get('name') or '').lower()
    layer = (it.get('layername') or '').lower()
    return 'advt' in layer or layer in ('ad', 'ads', 'advert', 'advertisement') or \
        name.startswith('advt') or name.startswith('ad_') or 'advert' in name

def is_rule(it):
    t = it['type']
    if t in (5, 7):            # Line, PolyLine
        return True
    if t == 6 and (it['w'] < 1.5 or it['h'] < 1.5):   # a thin polygon used as a rule
        return True
    if t == 4 and (it['w'] < 1.5 or it['h'] < 1.5) and not it.get('paras'):
        return True
    return False

def headline_size(mod):
    return float((mod.get('headline') or {}).get('size') or 0)

def importance(mod):
    """Lead first: headline size, width in columns, area, then the higher one."""
    return (headline_size(mod) * 3.0 + mod['cols'] * 12.0 + (mod['w'] * mod['h']) / 400.0
            + (12.0 if mod['photos'] else 0.0) - mod['y'] / 40.0)

def photo_pos(ph):
    v = ph.get('pos_v', 'top')
    h = ph.get('pos_h', 'right')
    if v == 'above-headline':
        return 'topAbove'
    if v == 'between-head-and-body':
        return 'top'
    if v == 'bottom':
        return 'bottom'
    if v == 'middle':
        return 'middle'
    return 'left' if h == 'left' else ('right' if h == 'right' else 'top')

def build_model(path, page_index):
    pg = Page(path, page_index)
    a = AN.analyze(path, page_index, page=pg)
    g = a['grid']
    onpage = [it for it in pg.items if (it['page'] == pg.index or it['group']) and -5 < it['x'] < pg.w + 5 and -5 < it['y'] < pg.h + 5]
    top_level = [it for it in onpage if not it['group']]
    # frames taken by the stories / obits / captions (ids are not kept by analyze: match by rectangle)
    def covered(it, boxes, frac=0.6):
        area = max(0.01, it['w'] * it['h'])
        return any(AN.overlap(it, b) > frac * area for b in boxes)
    story_boxes = [dict(x=m['x'], y=m['y'], w=m['w'], h=m['h']) for m in a['modules']]
    obit_boxes = [dict(x=o['x'], y=o['y'], w=o['w'], h=o['h']) for o in a['obits']]
    mtop, mbot = pg.margins['t'], pg.h - pg.margins['b']

    slots = []
    for m in sorted(a['modules'], key=importance, reverse=True):
        head = m['headline']
        photos = []
        for ph in m['photos']:
            photos.append(dict(x=r1(ph['x']), y=r1(ph['y']), w=r1(ph['w']), h=r1(ph['h']), cols=int(ph['cols']),
                               pos=photo_pos(ph), captionH=r1(sum(c['h'] for c in ph.get('caption', []))),
                               caption=bool(ph.get('caption'))))
        slots.append(dict(kind='story', x=r1(m['x']), y=r1(m['y']), w=r1(m['w']), h=r1(m['h']),
                          col0=int(m['col0']), cols=int(m['cols']), bodyCols=int(m.get('frame_cols') or m['cols']),
                          headline=dict(style=head.get('style') or '', size=round(float(head.get('size') or 0), 1),
                                        lines=int(head.get('lines') or 1), chars=int(head.get('chars') or 0)),
                          kicker=bool(m.get('kicker')), byline=bool(m.get('byline')), dateline=bool(m.get('dateline')),
                          bodyChars=int(m.get('body_chars') or 0), split=bool(m.get('split')), photos=photos))
    # slots never overlap: the less important slot is cut back on the side where the cut is smaller;
    # a slot cut below 20 mm, or a photo left outside its slot, goes
    def cut_overlaps(slots):
        for i in range(len(slots)):
            for j in range(i + 1, len(slots)):
                a_, b_ = slots[i], slots[j]
                if a_.get('drop') or b_.get('drop'):
                    continue
                ox = min(a_['x'] + a_['w'], b_['x'] + b_['w']) - max(a_['x'], b_['x'])
                oy = min(a_['y'] + a_['h'], b_['y'] + b_['h']) - max(a_['y'], b_['y'])
                if ox <= 0.5 or oy <= 0.5:
                    continue
                if oy <= ox:                     # cut vertically: b above or below a
                    if b_['y'] >= a_['y']:
                        new_top = a_['y'] + a_['h'] + 1.0
                        b_['h'] = round(b_['y'] + b_['h'] - new_top, 1); b_['y'] = round(new_top, 1)
                    else:
                        b_['h'] = round(a_['y'] - 1.0 - b_['y'], 1)
                else:                            # cut horizontally
                    if b_['x'] >= a_['x']:
                        new_left = a_['x'] + a_['w'] + 1.0
                        b_['w'] = round(b_['x'] + b_['w'] - new_left, 1); b_['x'] = round(new_left, 1)
                        b_['cols'] = max(1, AN.cols_for_width(b_['w'], g)); b_['col0'] = AN.col_index(b_['x'], g)
                    else:
                        b_['w'] = round(a_['x'] - 1.0 - b_['x'], 1); b_['cols'] = max(1, AN.cols_for_width(b_['w'], g))
                if b_['h'] < 20.0 or b_['w'] < 20.0:
                    b_['drop'] = True
        out = []
        for s in slots:
            if s.get('drop'):
                continue
            s['photos'] = [ph for ph in s['photos'] if ph['x'] >= s['x'] - 1 and ph['y'] >= s['y'] - 1
                           and ph['x'] + ph['w'] <= s['x'] + s['w'] + 1 and ph['y'] + ph['h'] <= s['y'] + s['h'] + 1]
            out.append(s)
        return out
    slots = cut_overlaps(slots)
    for i, s in enumerate(slots):
        s['n'] = i + 1

    obits = []
    for o in a['obits']:
        obits.append(dict(x=o['x'], y=o['y'], w=o['w'], h=o['h'], photo=o.get('photo') is not None,
                          textH=o.get('text_h'), by=o.get('by')))

    ads, fixed, rules, boxes, images = [], [], [], [], []
    used = story_boxes + obit_boxes
    for it in top_level:
        if it['w'] * it['h'] < 4.0 and not is_rule(it):
            continue
        # the top / bottom margin bands hold the masthead and the folio - whatever layer they are on
        role = None
        if it['y'] + it['h'] <= mtop + 2.0:
            role = 'masthead'
        elif it['y'] >= mbot - 2.0:
            role = 'folio'
        if role:
            fixed.append(dict(rect(it), role=role, name=it['name'], type=it['type']))
            continue
        if is_rule(it):
            rules.append(dict(rect(it), orient='h' if it['w'] >= it['h'] else 'v'))
            continue
        if covered(it, used):
            continue                                  # part of a story / obit module (text, photo, caption)
        # an ad: on the Advt layer (or named so) and no part of a story - whole pages were made on that layer
        if is_ad(it) and it['w'] * it['h'] >= AD_MIN_MM2:
            ads.append(dict(rect(it), name=it['name']))
            continue
        if it['type'] == 4 and it.get('paras') and all(classify(p['style'], p) == 'caption' for p in it['paras'] if p['chars'] > 0):
            if covered(it, used, 0.2):
                continue
        if it['locked']:
            fixed.append(dict(rect(it), role='locked', name=it['name'], type=it['type']))
            continue
        if it['type'] == 2:
            if it['w'] * it['h'] >= PHOTO_MIN_MM2:
                images.append(dict(rect(it), cols=AN.cols_for_width(it['w'], g)))
            continue
        if it['type'] == 12 and it['w'] * it['h'] < 25.0:
            continue
        chars = sum(p['chars'] for p in it.get('paras', [])) if it['type'] == 4 else 0
        styles = sorted({p['style'] for p in it.get('paras', []) if p['style']})[:4] if it['type'] == 4 else []
        boxes.append(dict(rect(it), chars=chars, styles=styles, type=it['type'], name=it['name']))

    styles = sorted({s['headline']['style'] for s in slots if s['headline']['style'] and not s['headline']['style'].startswith('(')})
    ad_area = sum(x['w'] * x['h'] for x in ads)
    obit_grid = None
    if len(obits) >= OBIT_PAGE_MIN:
        ws = sorted(o['w'] for o in obits)
        med_w = ws[len(ws) // 2]
        xs = sorted({round(o['x'], 0) for o in obits})
        gaps = [b - a_ - med_w for a_, b in zip(xs, xs[1:]) if 0 < b - a_ - med_w < 12]
        gap = round(sorted(gaps)[len(gaps) // 2], 1) if gaps else 3.0
        usable = pg.w - pg.margins['l'] - pg.margins['r']
        ncols = max(1, round((usable + gap) / (med_w + gap)))
        obit_grid = dict(cols=ncols, colW=round(med_w, 1), gap=gap, top=r1(min(o['y'] for o in obits)))
    model = dict(format='faircode-page-model', version=1, name='', group='',
                 source=dict(file=os.path.basename(path), path=os.path.abspath(path), page=page_index, pages=pg.npages,
                             pageNum=pg.page_num, version=pg.version, master=pg.master),
                 page=dict(w=round(pg.w, 2), h=round(pg.h, 2), margins={k: round(v, 2) for k, v in pg.margins.items()}),
                 grid=dict(cols=g['ncols'], colW=round(g['colw'], 2), gutter=g['gutter'], type='obituary' if obit_grid else 'normal'),
                 obitGrid=obit_grid, styles=styles, slots=slots, obits=obits, ads=ads, fixed=fixed, rules=rules, boxes=boxes, images=images,
                 stats=dict(stories=len(slots), photos=sum(len(s['photos']) for s in slots) + len(images), obits=len(obits),
                            ads=len(ads), adArea=round(ad_area, 0), boxes=len(boxes)))
    return model

def expand(args):
    out = []
    for a in args:
        if os.path.isdir(a):
            out += sorted(glob.glob(os.path.join(a, '*.sla'))) + sorted(glob.glob(os.path.join(a, '*.sla.gz')))
        elif a.lower().endswith('.sla') or a.lower().endswith('.gz'):
            out.append(a)
    return out

def main():
    args = sys.argv[1:]
    only_list = '--list' in args
    files = expand([a for a in args if not a.startswith('--')])
    if only_list:
        for f in files:
            try:
                n = AN.pages_in(f)
                print(json.dumps(dict(file=f, pages=n), ensure_ascii=False)); sys.stdout.flush()
            except Exception as e:
                print(json.dumps(dict(file=f, error=str(e)), ensure_ascii=False)); sys.stdout.flush()
        return
    # pages: "<file>#<page>" picks one page of a file
    todo = []
    for a in args:
        if a.startswith('--'):
            continue
        if '#' in a and not os.path.exists(a):
            f, p = a.rsplit('#', 1)
            todo.append((f, int(p)))
    for f in files:
        try:
            n = AN.pages_in(f)
        except Exception as e:
            todo.append((f, -1))
            continue
        for i in range(n):
            todo.append((f, i))
    total = len(todo)
    for k, (f, i) in enumerate(todo):
        print(f"PROGRESS {k} {total} {os.path.basename(f)}", file=sys.stderr); sys.stderr.flush()
        try:
            if i < 0:
                raise ValueError('cannot read the file')
            m = build_model(f, i)
            print(json.dumps(dict(file=f, page=i, pages=m['source']['pages'], model=m), ensure_ascii=False))
        except Exception as e:
            traceback.print_exc(file=sys.stderr)
            print(json.dumps(dict(file=f, page=i, error=str(e)), ensure_ascii=False))
        sys.stdout.flush()
    print(f"PROGRESS {total} {total} done", file=sys.stderr)

if __name__ == '__main__':
    main()
