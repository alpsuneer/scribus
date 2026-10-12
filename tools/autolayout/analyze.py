import sys, json, glob, os, statistics as stt
sys.path.insert(0, os.path.dirname(__file__))
from slaparse import Page, classify, PT, HEADLINE_RE

def overlap(a, b):
    ox = max(0, min(a['x']+a['w'], b['x']+b['w']) - max(a['x'], b['x']))
    oy = max(0, min(a['y']+a['h'], b['y']+b['h']) - max(a['y'], b['y']))
    return ox*oy

def grid_of(page):
    m = page.margins; avail = page.w - m['l'] - m['r']
    gutter = 8.0; ncols = 8
    colw = (avail - gutter*(ncols-1))/ncols
    return dict(ncols=ncols, gutter=gutter, colw=colw, pitch=colw+gutter, x0=m['l'], y0=m['t'], y1=page.h-m['b'], avail=avail)

def cols_for_width(w, g):  return max(1, round((w + g['gutter'])/g['pitch']))
def col_index(x, g):       return round((x - g['x0'])/g['pitch'])

def analyze(path):
    pg = Page(path); g = grid_of(pg)
    onpage = [it for it in pg.items if it['page'] == 0 and it['type'] in (2,4) and it['x'] < pg.w and it['y'] < pg.h]
    texts = [it for it in onpage if it['type']==4]
    images = [it for it in onpage if it['type']==2]
    # chains: story = head frame + followers
    heads = [t for t in texts if t.get('back') in (None,'-1')]
    def chain(t):
        out=[t]; nxt=t.get('next')
        while nxt and nxt!='-1' and nxt in pg.by_id and pg.by_id[nxt] not in out:
            out.append(pg.by_id[nxt]); nxt = pg.by_id[nxt].get('next')
        return out
    stories=[]; captions=[]; others=[]
    for t in heads:
        frames = chain(t)
        paras = [p for f in frames for p in f['paras']]
        kinds = [classify(p['style'], p) for p in paras]
        if not paras: others.append(t); continue
        if all(k=='caption' for k in kinds): captions.append(t); continue
        body_chars = sum(p['chars'] for p,k in zip(paras,kinds) if k=='body')
        if 'headline' in kinds[:3] and body_chars > 60: stories.append((frames, paras, kinds)); continue
        others.append(t)
    # split modules: head-only frames (kicker / headline / deck) stacked above a frame
    # that starts with byline/dateline/body; several body frames side by side are one story
    def is_headish(t):
        return bool(t['paras']) and sum(p['chars'] for p in t['paras'])>3 and all(classify(p['style'],p) in ('headline','kicker','other') for p in t['paras']) and \
               any(classify(p['style'],p)=='headline' or (p.get('run_size') or 0)>=13 for p in t['paras']) and sum(p['chars'] for p in t['paras'])<200
    headonly = [t for t in others if is_headish(t)]
    bodyonly = [t for t in others if t['paras'] and classify(t['paras'][0]['style'],t['paras'][0]) in ('byline','dateline','body') and sum(p['chars'] for p in t['paras'] if classify(p['style'],p)=='body')>60]
    def hover(a,b): return min(a['x']+a['w'],b['x']+b['w'])-max(a['x'],b['x']) > 0.5*min(a['w'],b['w'])
    used=set()
    for b in sorted(bodyonly, key=lambda t:(t['y'],t['x'])):
        if b['id'] in used: continue
        heads=[h for h in headonly if h['id'] not in used and -4 <= b['y']-(h['y']+h['h']) < 26 and hover(h,b)]
        if not heads: continue
        # more head frames stacked above the found ones (kicker above headline)
        changed=True
        while changed:
            changed=False
            for h in headonly:
                if h['id'] in used or h in heads: continue
                if any(-2 <= hh['y']-(h['y']+h['h']) < 10 and hover(h,hh) for hh in heads):
                    heads.append(h); changed=True
        top=min(h['y'] for h in heads)
        bodies=[bb for bb in bodyonly if bb['id'] not in used and abs(bb['y']-b['y'])<6 and any(hover(h,bb) for h in heads)]
        frames=sorted(heads,key=lambda t:t['y'])+sorted(bodies,key=lambda t:t['x'])
        for f in frames:
            used.add(f['id'])
            if f in others: others.remove(f)
        paras=[p for f in frames for p in f['paras']]; kinds=[classify(p['style'],p) for p in paras]
        if 'headline' not in kinds:
            # the largest run in a head frame is the headline
            big=max((p for f in heads for p in f['paras'] if p.get('run_size')), key=lambda p:p['run_size'], default=None)
            if big is None: continue
            kinds=[('headline' if p is big else k) for p,k in zip(paras,kinds)]
        stories.append((frames,paras,kinds))
    modules=[]
    used_imgs=set(); used_caps=set()
    for frames, paras, kinds in stories:
        f0 = frames[0]
        box = dict(x=min(f['x'] for f in frames), y=min(f['y'] for f in frames))
        box['w'] = max(f['x']+f['w'] for f in frames)-box['x']; box['h'] = max(f['y']+f['h'] for f in frames)-box['y']
        # photos: image frames overlapping the story frame area by > 50% of the image
        photos=[]
        for im in images:
            if im['id'] in used_imgs: continue
            ov = sum(overlap(im,f) for f in frames)
            if ov > 0.5*im['w']*im['h'] and im['w']*im['h'] > 100:
                photos.append(im); used_imgs.add(im['id'])
        caps=[]
        for im in photos:
            for c in captions:
                if c['id'] in used_caps: continue
                if abs(c['y'] - (im['y']+im['h'])) < 3 and min(c['x']+c['w'], im['x']+im['w']) - max(c['x'], im['x']) > 0.5*min(c['w'], im['w']):
                    caps.append(dict(c, photo=im['id'])); used_caps.add(c['id'])
        # photos stacked directly above the module (photo + caption, then the frame)
        for im in images:
            if im['id'] in used_imgs or im['w']*im['h'] <= 100: continue
            gap = box['y'] - (im['y']+im['h'])
            hx = min(im['x']+im['w'], box['x']+box['w']) - max(im['x'], box['x'])
            if 0 <= gap < 14 and hx > 0.6*im['w']:
                photos.append(im); used_imgs.add(im['id'])
                for c in captions:
                    if c['id'] in used_caps: continue
                    if abs(c['y'] - (im['y']+im['h'])) < 3 and min(c['x']+c['w'], im['x']+im['w']) - max(c['x'], im['x']) > 0.5*min(c['w'], im['w']):
                        caps.append(dict(c, photo=im['id'])); used_caps.add(c['id'])
                box['h'] += box['y']-im['y']; box['y'] = im['y']
        def hsize(i):
            p_=paras[i]; st_=pg.style_info(p_['style'])
            return p_['run_size'] if (p_.get('run_size') and not HEADLINE_RE.match(p_['style'])) else st_['size']
        hi = max((i for i,k in enumerate(kinds) if k=='headline'), key=hsize)
        head = paras[hi]; hs = pg.style_info(head['style'])
        if head['run_size'] and (not HEADLINE_RE.match(head['style'])):
            hs = dict(hs, size=head['run_size'], linesp=head.get('para_linesp') or round(head['run_size']*0.88,1), font=head['run_font'] or hs['font'],
                      before=head.get('para_before',0), after=head.get('para_after',0))
        hf_ = next((f for f in frames if head in f['paras']), f0)
        head_w = hf_['w']
        # header block height (mm): kicker + headline + byline + dateline lines
        header_pt = 0.0
        for p,k in zip(paras,kinds):
            if k=='body': break
            si = pg.style_info(p['style'])
            if k=='headline' and p is head: si = hs
            lines = p['breaks']+1 if k=='headline' else 1
            if k=='headline' and p is head:
                import math
                est = math.ceil(p['chars']*0.40*hs['size']/(head_w*PT)) if head_w>0 else 1
                lines = max(lines, est)
            header_pt += si['before'] + lines*si['linesp'] + si['after']
        header_mm = header_pt/PT
        body_chars = sum(p['chars'] for p,k in zip(paras,kinds) if k=='body')
        ncols = cols_for_width(box['w'], g)
        split = len(frames)>1 and not any(f.get('back') not in (None,'-1') for f in frames)
        body_frames = [f for f in frames if any(classify(p['style'],p)=='body' for p in f['paras'])]
        colmm_total = sum(f['cols']*f['h'] for f in body_frames)
        # photo area removed from text (in column-mm): photo columns x (photo+caption height + 2 mm wrap)
        photo_colmm = 0.0
        for im in photos:
            if not any(overlap(im,f) > 0.3*im['w']*im['h'] for f in body_frames): continue
            cap_h = sum(c['h'] for c in caps if c['photo']==im['id'])
            photo_colmm += cols_for_width(im['w'], g) * (im['h'] + cap_h + 2.0)
        # header inside the body frame only counts when the headline shares that frame
        in_frame_header = header_mm if not split else sum((pg.style_info(p['style'])['linesp']+pg.style_info(p['style'])['before']+pg.style_info(p['style'])['after'])/PT for p,k in zip(paras,kinds) if k in ('byline','dateline'))
        body_colmm = colmm_total - body_frames[0]['cols']*in_frame_header - photo_colmm
        mod = dict(name=f0['name'], split=split, fullspan=head.get('fullspan') if head.get('fullspan') is not None else pg.style_attr(head['style'],'FullSpan'), head_w=round(head_w,1), x=round(box['x'],1), y=round(box['y'],1), w=round(box['w'],1), h=round(box['h'],1),
                   col0=col_index(f0['x'],g), cols=ncols, frame_cols=f0['cols'], frames=len(frames), colgap=round(f0['colgap'],2),
                   headline=dict(style=head['style'] if HEADLINE_RE.match(head['style']) else f"(local {hs['size']:g})", size=hs['size'], linesp=hs['linesp'], font=hs['font'], chars=head['chars'], lines=max(head['breaks']+1, __import__('math').ceil(head['chars']*0.40*hs['size']/(head_w*PT))), manual_breaks=head['breaks'], text=head['text']),
                   kicker=next((p['style'] for p,k in zip(paras,kinds) if k=='kicker'), None),
                   byline='byline' in kinds, dateline='dateline' in kinds,
                   header_mm=round(header_mm,1), body_chars=body_chars, body_paras=sum(1 for k in kinds if k=='body'),
                   blurb_chars=sum(p['chars'] for p,k in zip(paras,kinds) if k=='blurb'),
                   colmm_total=round(colmm_total,1), body_colmm=round(body_colmm,1),
                   chars_per_colmm=round(body_chars/body_colmm,2) if body_colmm>5 else None,
                   photos=[], captions=[], group=f0['group'])
        for im in photos:
            rel_x = im['x']-box['x']; pos_h = 'left' if rel_x < 3 else ('right' if (box['x']+box['w'])-(im['x']+im['w']) < 3 else 'centre')
            bf = body_frames[0]
            if im['y']+im['h'] <= bf['y']+1 and im['y'] < box['y']+1: pos_v = 'above-headline'
            elif im['y']+im['h'] <= bf['y']+1: pos_v = 'between-head-and-body'
            elif im['y'] - bf['y'] < in_frame_header + 4: pos_v = 'top'
            elif (bf['y']+bf['h'])-(im['y']+im['h']) < 3: pos_v = 'bottom'
            else: pos_v = 'middle'
            mod['photos'].append(dict(x=round(im['x'],1), y=round(im['y'],1), w=round(im['w'],1), h=round(im['h'],1),
                                      cols=cols_for_width(im['w'],g), pos_h=pos_h, pos_v=pos_v, y_in_module=round(im['y']-f0['y'],1),
                                      caption=[dict(h=round(c['h'],1), style=c['paras'][0]['style'], chars=sum(p['chars'] for p in c['paras'])) for c in caps if c['photo']==im['id']]))
        modules.append(mod)
    # gaps between modules
    vgaps=[]; hgaps=[]
    for a in modules:
        for b in modules:
            if a is b: continue
            hx = min(a['x']+a['w'], b['x']+b['w']) - max(a['x'], b['x'])
            if hx > 10:
                d = b['y'] - (a['y']+a['h'])
                if 0 <= d < 20: vgaps.append(round(d,1))
            hy = min(a['y']+a['h'], b['y']+b['h']) - max(a['y'], b['y'])
            if hy > 10:
                d = b['x'] - (a['x']+a['w'])
                if 0 <= d < 20: hgaps.append(round(d,1))
    other_items = [dict(name=it['name'], type=it['type'], x=round(it['x'],1), y=round(it['y'],1), w=round(it['w'],1), h=round(it['h'],1),
                        styles=sorted({p['style'] for p in it.get('paras',[])})[:4], chars=sum(p['chars'] for p in it.get('paras',[])))
                   for it in others if it['w']*it['h']>200]
    loose_imgs = [dict(x=round(i['x'],1), y=round(i['y'],1), w=round(i['w'],1), h=round(i['h'],1), file=os.path.basename(i['file'])) for i in images if i['id'] not in used_imgs and i['w']*i['h']>200]
    return dict(file=os.path.basename(path), page_w=round(pg.w,2), page_h=round(pg.h,2), margins={k:round(v,2) for k,v in pg.margins.items()},
                grid={k:round(v,3) if isinstance(v,float) else v for k,v in g.items()},
                modules=modules, vgaps=vgaps, hgaps=hgaps, other_text=other_items, loose_images=loose_imgs,
                n_text=len(texts), n_images=len(images))

if __name__ == '__main__':
    out = {}
    for p in sys.argv[1:]:
        try: out[os.path.basename(p)] = analyze(p)
        except Exception as e:
            import traceback; traceback.print_exc(); out[os.path.basename(p)] = dict(error=str(e))
    json.dump(out, open(os.path.join(os.path.dirname(__file__), '..', 'analysis.json'), 'w'), ensure_ascii=False, indent=1)
    for f, a in out.items():
        if 'error' in a: print(f, 'ERROR', a['error']); continue
        print(f"== {f}: {len(a['modules'])} modules, {a['n_text']} text, {a['n_images']} images; margins {a['margins']}, colw {a['grid']['colw']:.2f}")
        for m in a['modules']:
            ph = ','.join(f"{p['cols']}c{p['pos_h'][0]}{p['pos_v'][0]}" for p in m['photos'])
            print(f"   {m['cols']}col@{m['col0']} {m['w']:.0f}x{m['h']:.0f} y={m['y']:.0f} {'SPLIT' if m['split'] else 'one  '} head={m['headline']['style']}({m['headline']['lines']}L,{m['headline']['chars']}ch,w{m['head_w']:.0f}) k={m['kicker'] or '-'} b={int(m['byline'])} d={int(m['dateline'])} body={m['body_chars']} cpcm={m['chars_per_colmm']} photos=[{ph}] fc={m['frame_cols']} fr={m['frames']}")
