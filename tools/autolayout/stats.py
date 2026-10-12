import json, statistics as st, collections, sys, os
sys.path.insert(0,'tools')
A=json.load(open('analysis.json'))
G=["21page3N.sla","21page4.sla","21page7.sla","21page9.sla","21page10.sla","21page11.sla","21page14.sla"]
mods=[dict(m,page=p) for p in G for m in A[p]['modules']]
print("modules:",len(mods))
def q(v): 
    v=sorted(v); n=len(v)
    return dict(n=n, min=round(v[0],2), p25=round(v[n//4],2), median=round(st.median(v),2), p75=round(v[(3*n)//4],2), max=round(v[-1],2), mean=round(st.mean(v),2)) if v else None
# 1. grid fit: frame widths & x positions
ws=collections.Counter(round(m['w']) for m in mods); print("widths:",sorted(ws.items()))
xs=collections.Counter(round(m['x']) for m in mods); print("x positions:",sorted(xs.items()))
print("module cols:",sorted(collections.Counter(m['cols'] for m in mods).items()))
print("frame cols == grid cols:",sum(m['frame_cols']==m['cols'] for m in mods),"/",len(mods), " colgap:",collections.Counter(m['colgap'] for m in mods).most_common(3))
print("col0 by cols:",{c:sorted(collections.Counter(m['col0'] for m in mods if m['cols']==c).items()) for c in range(1,6)})
# 2. heights by cols
for c in range(1,6):
    print(f"height cols={c}:", q([m['h'] for m in mods if m['cols']==c]))
# 3. headline by cols
print("\nHEADLINE size by cols")
for c in range(1,6):
    sel=[m for m in mods if m['cols']==c]
    print(c, "sizes", sorted(collections.Counter(m['headline']['size'] for m in sel).items()), "lines", sorted(collections.Counter(m['headline']['lines'] for m in sel).items()), "fonts", collections.Counter((m['headline']['font'] or '?')[:22] for m in sel).most_common(3))
print("headline styles:", collections.Counter(m['headline']['style'] for m in mods).most_common(30))
print("kicker:", collections.Counter((m['cols'],m['kicker']) for m in mods).most_common(20))
print("byline by cols:", {c:(sum(m['byline'] for m in mods if m['cols']==c), sum(1 for m in mods if m['cols']==c)) for c in range(1,6)})
print("dateline by cols:", {c:(sum(m['dateline'] for m in mods if m['cols']==c), sum(1 for m in mods if m['cols']==c)) for c in range(1,6)})
print("fullspan:", collections.Counter(m.get('fullspan') for m in mods))
# headline chars per line vs width: calibrate char width factor k = width_pt / (chars_per_line * size)
ks=[]
for m in mods:
    h=m['headline']
    if h['manual_breaks']>=1 and h['chars']>0:
        cpl=h['chars']/h['lines']; k=(m['head_w']*72/25.4)/(cpl*h['size']); ks.append((round(k,2),m['cols'],h['size'],h['lines'],h['chars']))
print("headline width factor k=width_pt/(chars_per_line*size):", q([k[0] for k in ks]))
print("  (k is an upper bound: lines are broken early; use ~p25 as 'fits' estimate)")
# 4. capacity
print("\nCAPACITY chars per column-mm")
for c in range(1,6):
    sel=[m['chars_per_colmm'] for m in mods if m['cols']==c and m['chars_per_colmm']]
    selnp=[m['chars_per_colmm'] for m in mods if m['cols']==c and m['chars_per_colmm'] and not m['photos']]
    print(c,"all",q(sel),"\n   no-photo",q(selnp))
print("all no-photo:",q([m['chars_per_colmm'] for m in mods if m['chars_per_colmm'] and not m['photos']]))
# per-column body chars vs body colmm linear fit (no photo)
xs_=[m['body_colmm'] for m in mods if m['chars_per_colmm'] and not m['photos']]; ys_=[m['body_chars'] for m in mods if m['chars_per_colmm'] and not m['photos']]
n=len(xs_); sx=sum(xs_); sy=sum(ys_); sxx=sum(x*x for x in xs_); sxy=sum(x*y for x,y in zip(xs_,ys_))
b=(n*sxy-sx*sy)/(n*sxx-sx*sx); a=(sy-b*sx)/n
print(f"linear fit body_chars = {a:.1f} + {b:.2f} * body_colmm  (n={n})")
b0=sxy/sxx; print(f"through origin: {b0:.2f} chars per column-mm")
# 5. photos
print("\nPHOTOS")
ph=[(m['cols'],p) for m in mods for p in m['photos']]
print("modules with photo:",sum(1 for m in mods if m['photos']),"/",len(mods), " by cols:",{c:(sum(1 for m in mods if m['cols']==c and m['photos']),sum(1 for m in mods if m['cols']==c)) for c in range(1,6)})
print("photo cols vs module cols:",sorted(collections.Counter((c,p['cols']) for c,p in ph).items()))
print("photo pos_h:",collections.Counter(p['pos_h'] for c,p in ph), " pos_v:",collections.Counter(p['pos_v'] for c,p in ph))
print("photo pos by module cols:",{c:collections.Counter((p['pos_h'],p['pos_v']) for cc,p in ph if cc==c).most_common(4) for c in range(1,6)})
print("photo width mm:",sorted(collections.Counter(round(p['w']) for c,p in ph).items()))
print("photo height mm by photo cols:",{c:q([p['h'] for cc,p in ph if p['cols']==c]) for c in range(1,4)})
print("photo aspect w/h:",q([p['w']/p['h'] for c,p in ph]))
caps=[cp for c,p in ph for cp in p['caption']]
print("captions:",len(caps),"/",len(ph),"styles",collections.Counter(cp['style'] for cp in caps), "height",q([cp['h'] for cp in caps]), "chars",q([cp['chars'] for cp in caps]))
print("caption lines est (h/3.0mm):",collections.Counter(round(cp['h']/ (8.5/72*25.4)) for cp in caps))
# 6. gaps
vg=[g for p in G for g in A[p]['vgaps']]; hg=[g for p in G for g in A[p]['hgaps']]
print("\nGAPS vertical:",q(vg),sorted(collections.Counter(round(g) for g in vg).items()))
print("GAPS horizontal:",q(hg),sorted(collections.Counter(round(g) for g in hg).items()))
# 7. page usage: top-most module y, bottom-most
for p in G:
    ms=A[p]['modules']; print(p,"modules y range",round(min(m['y'] for m in ms)),"-",round(max(m['y']+m['h'] for m in ms)), "margins",A[p]['margins'], "other_text:",len(A[p]['other_text']),"loose img:",len(A[p]['loose_images']))
print("\nOTHER TEXT (not in modules) per page:")
for p in G:
    for o in A[p]['other_text']: print("  ",p,o)
print("\nLOOSE IMAGES per page:")
for p in G:
    for o in A[p]['loose_images']: print("  ",p,o)
