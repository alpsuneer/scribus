"""Parse Scribus 1.5.x / 1.7.1 SLA pages into page-relative items (mm)."""
import xml.etree.ElementTree as ET, re
PT = 72/25.4
HEADLINE_RE = re.compile(r'^(\d+) (M|B)$')
KICKER = {'12 Kicker','11 Kicker Lead','kickerline'}
BYLINE = {'07 Byline'}
DATELINE = {'01 Dateline'}
BODY = {'02 BodyText','03 BodyNoInd','05 BodyBullet1','06 BodyBullet 2','Notification','Default Paragraph Style','','-'}
CAPTION = {'09 Caption','10 Cutline'}
BLURB = {'08 Blurb','08 BlurbBullet 1','08 BlurbBullet 2','08 BlurbBullet 3','08 BlurbBullet 4','13 Pull Quote'}

def A(el, *names, default=None):
    for n in names:
        v = el.get(n)
        if v is not None: return v
    return default

def clean_len(s):
    return len(s.replace('\xad','').replace('‌','').replace('‍',''))

class Page:
    def __init__(self, path):
        self.path = path
        root = ET.parse(path).getroot()
        self.doc = root[0]
        self.new = self.doc.tag == 'Document'
        self.styles = {}
        for s in self.doc.findall('ParagraphStyle' if self.new else 'STYLE'):
            self.styles[A(s,'NAME','Name')] = s.attrib
        self.cstyles = {}
        for s in self.doc.findall('CharacterStyle' if self.new else 'CHARSTYLE'):
            self.cstyles[A(s,'CNAME','Name')] = s.attrib
        pg = self.doc.find('Page' if self.new else 'PAGE')
        self.px = float(A(pg,'PAGEXPOS','PageXPosition'))/PT
        self.py = float(A(pg,'PAGEYPOS','PageYPosition'))/PT
        self.w = float(A(pg,'PAGEWIDTH','PageWidth'))/PT
        self.h = float(A(pg,'PAGEHEIGHT','PageHeight'))/PT
        self.margins = dict(l=float(A(pg,'BORDERLEFT','BorderLeft'))/PT, r=float(A(pg,'BORDERRIGHT','BorderRight'))/PT,
                            t=float(A(pg,'BORDERTOP','BorderTop'))/PT, b=float(A(pg,'BORDERBOTTOM','BorderBottom'))/PT)
        self.items = []
        tag = 'PageObject' if self.new else 'PAGEOBJECT'
        for o in self.doc.findall(tag):
            self._walk(o, tag, None)
        self.by_id = {it['id']: it for it in self.items}

    def style_attr(self, name, *keys):
        seen = set()
        while name and name not in seen and name in self.styles:
            seen.add(name)
            s = self.styles[name]
            for k in keys:
                if s.get(k) is not None: return s.get(k)
            # char-style parent for font
            cp = s.get('CPARENT') or s.get('CharStyleParent') or s.get('CharParent')
            if cp and cp in self.cstyles:
                for k in keys:
                    if self.cstyles[cp].get(k) is not None: return self.cstyles[cp].get(k)
            name = s.get('PARENT') or s.get('Parent')
        return None

    def style_info(self, name):
        fs = self.style_attr(name,'FONTSIZE','FontSize'); ls = self.style_attr(name,'LINESP','LineSpacing')
        font = self.style_attr(name,'FONT','Font')
        return dict(size=float(fs) if fs else 9.3, linesp=float(ls) if ls else 9.3, font=font,
                    before=float(self.style_attr(name,'VOR','SpaceAbove') or 0), after=float(self.style_attr(name,'NACH','SpaceBelow') or 0))

    def _walk(self, o, tag, group):
        a = o.attrib
        t = int(A(a,'PTYPE','ItemType'))
        it = dict(id=A(a,'ItemID','ItemID', default=None) or str(id(o)), type=t, name=A(a,'ANNAME','AnName') or '',
                  x=float(A(a,'XPOS','XPos','XPosition'))/PT - self.px, y=float(A(a,'YPOS','YPos','YPosition'))/PT - self.py,
                  w=float(A(a,'WIDTH','Width'))/PT, h=float(A(a,'HEIGHT','Height'))/PT,
                  page=int(A(a,'OwnPage', default=-1)), group=group, locked=A(a,'LOCK','Locked') == '1',
                  layer=A(a,'LAYER','Layer','LayerID'), rot=float(A(a,'ROT','Rotation', default=0)))
        if t == 4:
            it['cols'] = int(A(a,'COLUMNS','Columns', default=1)); it['colgap'] = float(A(a,'COLGAP','ColumnGap', default=0))/PT
            it['next'] = A(a,'NEXTITEM','NextItem'); it['back'] = A(a,'BACKITEM','BackItem')
            it['flow'] = A(a,'TEXTFLOWMODE','TextFlowMode')
            it['paras'] = self._paras(o)
        if t == 2:
            it['file'] = A(a,'PFILE','ImageFile') or ''
            it['flow'] = A(a,'TEXTFLOWMODE','TextFlowMode')
        if t == 12:
            it['children'] = []
        self.items.append(it)
        if t == 12:
            for c in o.findall(tag):
                self._walk(c, tag, it['id'])
                it['children'].append(self.items[-1]['id'])

    def _paras(self, o):
        st = o.find('StoryText')
        if st is None: return []
        out = []; buf = []; brk = 0; sizes = []; fonts = []
        def flush(ch):
            txt = ''.join(buf)
            if txt.strip() or (ch is not None and ch.tag == 'para'):
                st_ = A(ch,'PARENT','Parent') if ch is not None else ''
                psize = A(ch,'FONTSIZE','FontSize') if ch is not None else None
                plin = A(ch,'LINESP','LineSpacing') if ch is not None else None
                out.append(dict(style=st_ or '', chars=clean_len(txt), breaks=brk, text=txt.replace('\xad','')[:60],
                                run_size=max(sizes) if sizes else (float(psize) if psize else None), run_font=fonts[0] if fonts else None,
                                para_linesp=float(plin) if plin else None, fullspan=(A(ch,'FullSpan') if ch is not None else None),
                                para_before=float(A(ch,'VOR','SpaceAbove', default=0) or 0) if ch is not None else 0,
                                para_after=float(A(ch,'NACH','SpaceBelow', default=0) or 0) if ch is not None else 0))
        for ch in st:
            if ch.tag in ('ITEXT','Content'):
                t = ch.get('CH','') if ch.tag=='ITEXT' else (ch.text or '')
                buf.append(t)
                fs = A(ch,'FONTSIZE','FontSize'); fn = A(ch,'FONT','Font')
                if fs and t.strip(): sizes.append(float(fs))
                if fn and t.strip(): fonts.append(fn)
            elif ch.tag == 'breakline': brk += 1
            elif ch.tag in ('para','trail'):
                flush(ch); buf = []; brk = 0; sizes = []; fonts = []
        if buf and ''.join(buf).strip(): flush(None)
        return out

def classify(style, para=None):
    if HEADLINE_RE.match(style) or style.startswith('Headings:'): return 'headline'
    if para is not None and style in ('Default Paragraph Style','','-','[No paragraph style]') and para.get('run_size') and para['run_size'] >= 13 and para['chars'] < 120:
        return 'headline'
    if style in KICKER: return 'kicker'
    if style in BYLINE: return 'byline'
    if style in DATELINE: return 'dateline'
    if style in CAPTION: return 'caption'
    if style in BLURB: return 'blurb'
    if style in BODY: return 'body'
    return 'other'
