#!/usr/bin/env python3
"""Learn Layout Models ("archive variants") from our own SLA pages.

    learn_models.py --folder DIR --page-type TYPE --out-dir MODELS_DIR
                    [--name-filter TEXT] [--variants N] [--max-pages N]

Reads every *.sla in DIR (name filter optional), measures the modules with
analyze.py (no network, no AI), infers the grid from the frame edges, groups
the pages by structure (lead position, brief rails, decks) and writes one
Faircode Layout Model per group (largest groups first) to MODELS_DIR as
archive-<type>-<letter>.json.  Progress goes to stderr ("PROGRESS i n file"),
the result summary (JSON) to stdout.
"""
import argparse, glob, json, os, re, statistics as st, sys, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze  # noqa: E402

HEAD_RE = re.compile(r'^\d+(\.\d+)? ?(M|B)$')


def log(*a):
    print(*a, file=sys.stderr, flush=True)


def median(v, d):
    v = [x for x in v if x is not None]
    return st.median(v) if v else d


def infer_grid(pages):
    """Columns / gutter that explain most module edges (all pages share the grid)."""
    boxes = []
    for p in pages:
        for m in p['modules']:
            boxes.append((p, m))
    best = None
    for n in range(5, 13):
        for g10 in range(20, 105, 5):
            g = g10 / 10.0
            score = 0
            for p, m in boxes:
                mg = p['margins']; avail = p['page_w'] - mg['l'] - mg['r']
                colw = (avail - g * (n - 1)) / n
                if colw < 15:
                    break
                pitch = colw + g
                # closeness, not a yes/no window: a near miss of a wrong gutter scores less than an exact hit
                c = (m['x'] - mg['l']) / pitch
                score += max(0.0, 1.0 - abs(c - round(c)) * pitch / 1.5)
                k = (m['w'] + g) / pitch
                if round(k) >= 1:
                    score += max(0.0, 1.0 - abs(k - round(k)) * pitch / 2.0)
            if best is None or score > best[0] + 1e-6:
                best = (score, n, g)
    return dict(columns=best[1], gutter=best[2], score=round(best[0], 1), of=2 * len(boxes))


def reanalyze(path, n, g):
    def grid_of(page):
        m = page.margins; avail = page.w - m['l'] - m['r']
        colw = (avail - g * (n - 1)) / n
        return dict(ncols=n, gutter=g, colw=colw, pitch=colw + g, x0=m['l'], y0=m['t'], y1=page.h - m['b'], avail=avail)
    analyze.grid_of = grid_of
    return analyze.analyze(path)


def structure(p, n):
    mods = p['modules']
    top = p['margins']['t']; bottom = p['page_h'] - p['margins']['b']
    height = max(1.0, bottom - top)
    left = [m for m in mods if m['col0'] == 0]
    right = [m for m in mods if m['col0'] + m['cols'] >= n]
    left_rail = sum(1 for m in left if m['cols'] == 1) >= 2 and sum(m['h'] for m in left if m['cols'] == 1) > 0.35 * height
    right_rail = sum(1 for m in right if m['cols'] == 1) >= 2 and sum(m['h'] for m in right if m['cols'] == 1) > 0.35 * height
    main_first = 1 if left_rail else 0
    main_last = n - 2 if right_rail else n - 1
    big = sorted([m for m in mods if m['cols'] >= 3 and m['y'] < top + height / 2], key=lambda m: -m['w'] * m['h'])
    lead = None
    if big and (len(big) == 1 or big[0]['w'] * big[0]['h'] > 1.3 * big[1]['w'] * big[1]['h']):
        lead = big[0]
    lead_pos = 'none'
    if lead:
        if lead['col0'] <= main_first:
            lead_pos = 'top-left'
        elif lead['col0'] + lead['cols'] - 1 >= main_last:
            lead_pos = 'top-right'
        else:
            lead_pos = 'top-centre'
    rest = [m for m in mods if m is not lead and m['cols'] >= 2]
    tops = []
    for m in sorted(rest, key=lambda m: m['y']):
        if tops and abs(m['y'] - tops[-1][0]) < 4:
            tops[-1][1] += 1
        else:
            tops.append([m['y'], 1])
    decks = sum(1 for t in tops if t[1] >= 2)
    return dict(lead=lead, lead_pos=lead_pos, lead_cols=lead['cols'] if lead else 0,
                left_rail=left_rail, right_rail=right_rail, decks=decks if decks >= 2 else 0)


def photo_key(ph, mod_cols):
    v, h = ph['pos_v'], ph['pos_h']
    if v == 'above-headline':
        return 'topAbove'
    if v == 'between-head-and-body' and ph['cols'] >= mod_cols:
        return 'top'
    if v == 'bottom':
        return 'bottom'
    return {'right': 'right', 'left': 'left'}.get(h, 'middle')


def mode(v, d=None):
    v = [x for x in v if x is not None]
    return collections.Counter(v).most_common(1)[0][0] if v else d


def learn(pages, group, n, g, page_type, letter):
    mods = [m for p in group for m in p['modules']]
    leads = [p['struct']['lead'] for p in group if p['struct']['lead']]
    lead_ids = {id(m) for m in leads}
    normal = [m for m in mods if id(m) not in lead_ids]
    # width by body length: median length per width, thresholds half way
    by_cols = collections.defaultdict(list)
    for m in normal:
        if m['body_chars'] > 0:
            by_cols[min(m['cols'], n)].append(m['body_chars'])
    ks = sorted(k for k in by_cols if len(by_cols[k]) >= 1)
    meds = [(k, st.median(by_cols[k])) for k in ks]
    width_rules = []
    for i, (k, md) in enumerate(meds):
        if i + 1 < len(meds):
            nxt = meds[i + 1][1]
            thr = int((md + nxt) / 2) if nxt > md else int(md * 1.2)
        else:
            thr = 999999
        if width_rules and thr <= width_rules[-1]['maxChars']:
            continue
        width_rules.append(dict(maxChars=thr, columns=k))
    if not width_rules:
        width_rules = [dict(maxChars=450, columns=1), dict(maxChars=900, columns=2), dict(maxChars=1300, columns=3), dict(maxChars=999999, columns=4)]
    width_rules[-1]['maxChars'] = 999999
    heads = collections.defaultdict(list)
    for m in normal:
        s = m['headline']['style']
        if HEAD_RE.match(s):
            heads[m['cols']].append(s)
    head_rules = [dict(columns=k, style=mode(v)) for k, v in sorted(heads.items())]
    lead_cols_mode = mode([m['cols'] for m in leads], 0)
    # the lead headline of leads as wide as the learned lead (an 80 B on a 5-col lead is not a 3-col style)
    lead_style = mode([m['headline']['style'] for m in leads if HEAD_RE.match(m['headline']['style']) and m['cols'] == lead_cols_mode], '')
    pcols = collections.defaultdict(list)
    keys = []
    heights = []
    for m in mods:
        for ph in m['photos']:
            pcols[m['cols']].append(ph['cols'])
            keys.append(photo_key(ph, m['cols']))
            heights.append(ph['h'])
    photo_rules = [dict(storyColumns=k, photoColumns=mode(v)) for k, v in sorted(pcols.items())]
    heights.sort()
    max_h = heights[int(0.9 * (len(heights) - 1))] if heights else 100.0
    vg = [x for p in group for x in p['vgaps'] if x > 0.5]
    cg = [m['colgap'] for m in mods if m['frame_cols'] > 1 and m['colgap'] > 0]
    capgap = [ph_c for m in mods for ph in m['photos'] for ph_c in []]
    s0 = group[0]['struct']
    lead_cols = mode([p['struct']['lead_cols'] for p in group if p['struct']['lead_cols']], 5)
    mg = {k: median([p['margins'][k] for p in group], None) for k in ('t', 'b', 'l', 'r')}
    files = [p['file'] for p in group]
    desc = 'Learned from %d archive page(s): %s. %s lead%s%s%s.' % (
        len(group), ', '.join(files[:6]) + (' ...' if len(files) > 6 else ''),
        {'none': 'no', 'top-left': 'top-left', 'top-right': 'top-right', 'top-centre': 'top-centre'}[s0['lead_pos']],
        ' %d col' % lead_cols if s0['lead_pos'] != 'none' else '',
        ', left rail' if s0['left_rail'] else '', ', right rail' if s0['right_rail'] else '')
    slug = re.sub(r'[^a-z0-9]+', '-', page_type.lower()).strip('-') or 'page'
    model = dict(
        id='archive-%s-%s' % (slug, letter.lower()), name='%s archive %s' % (page_type, letter), engine='news', origin='archive',
        description=desc, pageTypes=[page_type],
        grid=dict(columns=n, fromGuides=False, columnWidthMm=0, gutterMm=g, useDocumentMargins=True,
                  margins=dict(top=round(mg['t'], 1), bottom=round(mg['b'], 1), left=round(mg['l'], 1), right=round(mg['r'], 1))),
        structure=dict(decks=s0['decks'], leftRail=s0['left_rail'], rightRail=s0['right_rail'], leadPosition=s0['lead_pos'], leadColumns=lead_cols),
        stories=dict(widthByLength=width_rules, fixedColumns=0, headlineByWidth=head_rules, leadHeadlineStyle=lead_style),
        photos=dict(widthByStory=photo_rules, fixedColumns=0, position=mode(keys, 'right'), maxHeightMm=round(max(30.0, max_h), 1)),
        spacing=dict(gapVerticalMm=round(median(vg, 4.8), 1), gapHorizontalMm=g, headlineBodyMm=0, photoTextMm=0, wrapPaddingMm=2.0,
                     captionGapMm=0, captionTextMm=2.0, textInsetMm=0, columnGapMm=round(median(cg, 2.5), 2), ruleWidthPt=0, ruleColor='Black'))
    stats = dict(pages=len(group), modules=len(mods), files=files, signature=[s0['lead_pos'], s0['left_rail'], s0['right_rail'], s0['decks']])
    return model, stats


OBIT_PAGE_MIN = 6   # obit modules on a page that make it an obituary page


def pct(v, q, d):
    v = sorted(x for x in v if x is not None)
    return v[int(q * (len(v) - 1))] if v else d


def learn_obituary(group, page_type, letter, base):
    """An Obituary-engine model from obit pages: obit grid, photo size / shape, gaps, styles, classification."""
    obits = [o for p in group for o in p['obits']]
    mg = {k: median([p['margins'][k] for p in group], None) for k in ('t', 'b', 'l', 'r')}
    content_w = median([p['page_w'] - p['margins']['l'] - p['margins']['r'] for p in group], 330.0)
    col_w = median([o['w'] for o in obits if o['photo']], 30.3)
    # column starts (clustered) -> the gap between obit columns
    starts = []
    for x in sorted(o['x'] for o in obits):
        if not starts or x - starts[-1][-1] > 2.0:
            starts.append([x])
        else:
            starts[-1].append(x)
    xs = [st.median(c) for c in starts if len(c) >= 2] or [st.median(c) for c in starts]
    steps = [b - a for a, b in zip(xs, xs[1:]) if 0.5 * col_w < b - a < 1.6 * col_w]
    gap = max(0.0, median([d - col_w for d in steps], 3.0))
    n = max(1, int(round((content_w + gap) / (col_w + gap))))
    left = median([p['margins']['l'] for p in group], 10.0)
    col_of = lambda x: int(round((x - left) / (col_w + gap)))
    # the obit columns: those holding a real share of the modules (a stray photo + short text in a news column is no obit column)
    per_col = collections.Counter(min(n - 1, max(0, col_of(o['x']))) for o in obits)
    top_count = max(per_col.values()) if per_col else 0
    used = sorted(c for c, k in per_col.items() if k >= 0.35 * top_count)
    obit_first, obit_last = (used[0] + 1, used[-1] + 1) if used else (1, max(1, n - 2))
    if obit_last >= n and n >= 3:
        news_first, news_last = n - 1, n   # every column has obits: short news share the last two
    else:
        news_first, news_last = min(n, obit_last + 1), n
    # vertical gap between modules of one column
    vg = []
    for p in group:
        cols = collections.defaultdict(list)
        for o in p['obits']:
            cols[col_of(o['x'])].append(o)
        for c in cols.values():
            c.sort(key=lambda o: o['y'])
            for a, b in zip(c, c[1:]):
                d = b['y'] - (a['y'] + a['h'])
                if 0 <= d < 15:
                    vg.append(d)
    photos = [o['photo'] for o in obits if o['photo']]
    ratio = median([ph['h'] / ph['w'] for ph in photos if ph['w'] > 0], 1.0)
    photo_w = median([ph['w'] for ph in photos], 30.3)
    photo_h = median([ph['h'] for ph in photos], photo_w)
    text_gap = max(0.0, median([o['text_gap'] for o in obits if o['text_gap'] is not None], 0.0))
    is_obit_style = lambda s: bool(s) and bool(re.search(r'obit', s, re.I))
    name_style = mode([o['name_style'] for o in obits if is_obit_style(o['name_style'])], None)
    place_style = mode([o['place_style'] for o in obits if is_obit_style(o['place_style'])], None)
    body_style = mode([o['body_style'] for o in obits if is_obit_style(o['body_style'])], None)
    align = mode([o['align'] for o in obits if o['align'] is not None], None)
    top = median([min(o['y'] for o in p['obits']) for p in group if p['obits']], mg['t'])
    ob = json.loads(json.dumps(base)) if base else {}
    ob.setdefault('grid', {}); ob.setdefault('module', {}); ob.setdefault('styles', {}); ob.setdefault('classify', {})
    ob.setdefault('photo', {}); ob.setdefault('text', {}); ob.setdefault('news', {})
    ob['grid'].update(columns=n, gapMm=round(gap, 1), contentTopMm=round(top, 1), obitColumns=[obit_first, obit_last], newsColumns=[news_first, news_last])
    ob['module'].update(photoMm=round(photo_w, 1), gapMm=round(median(vg, 2.3), 1))
    if name_style: ob['styles']['name'] = name_style
    if place_style: ob['styles']['place'] = place_style
    if body_style: ob['styles']['body'] = body_style
    ob['classify'].update(titleMaxChars=int(max(18, pct([o['name_chars'] for o in obits], 0.95, 24))),
                          titleMaxWords=int(max(2, pct([o['name_words'] for o in obits], 0.95, 3))),
                          bodyMaxChars=int(max(250, pct([o['place_chars'] + o['body_chars'] for o in obits], 0.9, 450))))
    if ratio > 1.08:
        ob['photo'].update(frameShape='portrait', portraitHeightMm=round(photo_h, 1))
    else:
        ob['photo'].update(frameShape='square')
    if align is not None:
        ob['text']['align'] = 'centre' if str(align) == '1' else 'left'
    files = [p['file'] for p in group]
    slug = re.sub(r'[^a-z0-9]+', '-', page_type.lower()).strip('-') or 'page'
    model = dict(
        id='archive-%s-%s' % (slug, letter.lower()), name='%s archive %s' % (page_type, letter), engine='obituary', origin='archive',
        description='Learned from %d obituary page(s): %s. %d columns, obits in %d-%d, %s photos %.1f mm, %d obit modules.' % (
            len(group), ', '.join(files[:6]) + (' ...' if len(files) > 6 else ''), n, obit_first, obit_last,
            'portrait' if ratio > 1.08 else 'square', photo_w, len(obits)),
        pageTypes=[page_type],
        grid=dict(columns=n, fromGuides=False, columnWidthMm=0, gutterMm=round(gap, 1), useDocumentMargins=True,
                  margins=dict(top=round(top, 1), bottom=round(mg['b'], 1), left=round(mg['l'], 1), right=round(mg['r'], 1))),
        structure=dict(decks=0, leftRail=False, rightRail=False, leadPosition='none', leadColumns=5),
        stories=dict(widthByLength=[dict(maxChars=999999, columns=max(1, news_last - news_first + 1))], fixedColumns=0, headlineByWidth=[], leadHeadlineStyle=''),
        photos=dict(widthByStory=[], fixedColumns=0, position='top', maxHeightMm=100.0),
        spacing=dict(gapVerticalMm=round(median(vg, 2.3), 1), gapHorizontalMm=round(gap, 1), headlineBodyMm=0, photoTextMm=round(text_gap, 1),
                     wrapPaddingMm=2.0, captionGapMm=0, captionTextMm=2.0, textInsetMm=0, columnGapMm=2.5, ruleWidthPt=0, ruleColor='Black'))
    stats = dict(pages=len(group), modules=len(obits), files=files, signature=['obituary', n, obit_first, obit_last])
    return model, stats, ob


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--folder', required=True)
    ap.add_argument('--page-type', default='General')
    ap.add_argument('--out-dir', required=True)
    ap.add_argument('--name-filter', default='')
    ap.add_argument('--variants', type=int, default=3)
    ap.add_argument('--max-pages', type=int, default=120)
    a = ap.parse_args()
    files = sorted(glob.glob(os.path.join(a.folder, '*.sla')) + glob.glob(os.path.join(a.folder, '*', '*.sla')))
    files = [f for f in files if not os.path.basename(f).startswith('.')]
    if a.name_filter:
        files = [f for f in files if a.name_filter.lower() in os.path.basename(f).lower()]
    files = files[:a.max_pages]
    if not files:
        print(json.dumps(dict(error='no .sla file in %s%s' % (a.folder, (' matching "%s"' % a.name_filter) if a.name_filter else ''))))
        return 2
    # pass 1: the modules (grid independent boxes)
    first = []
    for i, f in enumerate(files):
        log('PROGRESS %d %d %s' % (i, 2 * len(files), os.path.basename(f)))
        try:
            r = analyze.analyze(f)
            if len(r['modules']) >= 2 or len(r.get('obits', [])) >= OBIT_PAGE_MIN:
                r['path'] = f
                first.append(r)
            else:
                log('SKIP %s: %d story modules' % (os.path.basename(f), len(r['modules'])))
        except Exception as e:  # a page we cannot read is skipped, never fatal
            log('SKIP %s: %s' % (os.path.basename(f), e))
    if not first:
        print(json.dumps(dict(error='none of the %d page(s) has story or obituary modules analyze.py recognises' % len(files))))
        return 3
    news_first = [r for r in first if len(r.get('obits', [])) < OBIT_PAGE_MIN] or first
    grid = infer_grid(news_first)
    n, g = grid['columns'], grid['gutter']
    log('GRID %d columns, gutter %.1f mm (edge fit %.1f of %d)' % (n, g, grid['score'], grid['of']))
    pages = []
    for i, r in enumerate(first):
        log('PROGRESS %d %d %s' % (len(files) + i, 2 * len(files), r['file']))
        try:
            p = reanalyze(r['path'], n, g)
        except Exception as e:
            log('SKIP %s: %s' % (r['file'], e))
            continue
        p['struct'] = structure(p, n)
        pages.append(p)
    groups = collections.defaultdict(list)
    for p in pages:
        if len(p.get('obits', [])) >= OBIT_PAGE_MIN:
            groups[('obituary',)].append(p)     # an obituary page: the Obituary engine, its own grid
            continue
        s = p['struct']
        groups[(s['lead_pos'], s['left_rail'], s['right_rail'], s['decks'] > 0)].append(p)
    ordered = sorted(groups.values(), key=lambda gr: -len(gr))[:max(1, a.variants)]
    os.makedirs(a.out_dir, exist_ok=True)
    out = []
    # the obit engine's own settings (photo processing, background, face fill) come from the Obituary model in use
    base = {}
    try:
        with open(os.path.join(a.out_dir, 'obituary.json'), encoding='utf-8') as fh:
            base = json.load(fh).get('obituary', {})
    except Exception:
        base = {}
    for i, gr in enumerate(ordered):
        letter = chr(ord('A') + i)
        root = {'format': 'faircode-layout-model', 'version': 1,
                '_comment': 'Learned by tools/autolayout/learn_models.py from archive pages (Layout Gallery > Generate). Sizes in mm.'}
        if len(gr[0].get('obits', [])) >= OBIT_PAGE_MIN:
            model, stats, ob = learn_obituary(gr, a.page_type, letter, base)
            root['obituary'] = ob
            obit_gap = ob['grid']['gapMm']
        else:
            model, stats = learn(pages, gr, n, g, a.page_type, letter)
        root['model'] = model
        path = os.path.join(a.out_dir, model['id'] + '.json')
        with open(path, 'w', encoding='utf-8') as fh:
            json.dump(root, fh, ensure_ascii=False, indent=2)
        out.append(dict(id=model['id'], name=model['name'], file=path, **stats))
        log('MODEL %s: %d pages, %d modules' % (model['id'], stats['pages'], stats['modules']))
    # only obituary pages: the grid worth reporting is the obit grid, not the one of their few news modules
    if out and all(o['signature'][0] == 'obituary' for o in out):
        grid = dict(grid, columns=out[0]['signature'][1], gutter=obit_gap)
    print(json.dumps(dict(grid=grid, pages=len(pages), read=len(files), models=out), ensure_ascii=False))
    return 0


if __name__ == '__main__':
    sys.exit(main())
