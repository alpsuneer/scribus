#!/usr/bin/env python3
"""Two saved obituary pages (1.7 SLA) with the same stories: per workflow story id, the module's
bounding box (text + photo + caption) in mm on the page, its role tags, and the difference A -> B.
Also the two self-test reports' Gallery lines. Exit 1 when a story differs by more than 0.05 mm,
a role differs, or the fit lines differ.
usage: compare_obit_pages.py A.sla B.sla [A-report.txt B-report.txt]"""
import sys, re
import xml.etree.ElementTree as ET
MM = 72 / 25.4
TOL = 0.05


def read(path):
    root = ET.parse(path).getroot()
    doc = root.find('Document') if root.tag != 'Document' else root
    pg = doc.find('Page').attrib
    px, py = float(pg['PageXPosition']), float(pg['PageYPosition'])
    out = {}
    for it in doc.findall('PageObject'):
        a = it.attrib
        at = {ia.get('Name'): ia.get('Value') for ia in it.findall('PageItemAttributes/ItemAttribute')}
        sid = at.get('news.serverId')
        if not sid or at.get('news.parked') == '1':
            continue
        x, y, w, h = (float(a['XPosition']) - px) / MM, (float(a['YPosition']) - py) / MM, float(a['Width']) / MM, float(a['Height']) / MM
        m = out.setdefault(sid, {'story': at.get('news.story', ''), 'l': 1e9, 't': 1e9, 'r': -1e9, 'b': -1e9, 'roles': set(), 'model': set(), 'cols': set(), 'seq': set(), 'n': 0, 'text': None})
        m['l'] = min(m['l'], x); m['t'] = min(m['t'], y); m['r'] = max(m['r'], x + w); m['b'] = max(m['b'], y + h)
        m['roles'].add(at.get('news.role', '?')); m['model'].add(at.get('news.model', '-')); m['cols'].add(at.get('news.cols', '-')); m['seq'].add(at.get('news.seq', '-'))
        m['n'] += 1
        if at.get('news.role') == 'text':
            m['text'] = (x, y, w, h, a.get('Columns'))
    return out


def gallery_lines(path):
    try:
        lines = open(path, encoding='utf-8').read().splitlines()
    except OSError:
        return []
    return [l.strip() for l in lines if 'obituary card' in l or 'gallery:' in l or 'gallery checkboxes' in l or 'modules:' in l or 'obituary page' in l]


A = read(sys.argv[1]); B = read(sys.argv[2])
fails = 0
print('%-22s %-5s %-5s %8s %8s %8s %8s   %s' % ('story', 'A', 'B', 'dx', 'dy', 'dw', 'dh', 'role A / role B'))
worst = 0.0
for sid in sorted(set(A) | set(B), key=lambda s: (A.get(s) or B.get(s))['story']):
    a, b = A.get(sid), B.get(sid)
    name = (a or b)['story'][:20]
    if not a or not b:
        print('%-22s %-5s %-5s   only in %s' % (name, 'yes' if a else '-', 'yes' if b else '-', 'A' if a else 'B')); fails += 1
        continue
    dx, dy = b['l'] - a['l'], b['t'] - a['t']
    dw, dh = (b['r'] - b['l']) - (a['r'] - a['l']), (b['b'] - b['t']) - (a['b'] - a['t'])
    ra = '%s/%s/c%s' % (','.join(sorted(a['model'])), ','.join(sorted(a['roles'])), ','.join(sorted(a['cols'])))
    rb = '%s/%s/c%s' % (','.join(sorted(b['model'])), ','.join(sorted(b['roles'])), ','.join(sorted(b['cols'])))
    flag = ''
    if max(abs(dx), abs(dy), abs(dw), abs(dh)) > TOL:
        flag = '  <-- differs'; fails += 1
    if ra != rb:
        flag += '  <-- role differs'; fails += 1
    worst = max(worst, abs(dx), abs(dy), abs(dw), abs(dh))
    print('%-22s %-5d %-5d %8.2f %8.2f %8.2f %8.2f   %s / %s%s' % (name, a['n'], b['n'], dx, dy, dw, dh, ra, rb, flag))
print('stories: A %d, B %d; worst difference %.2f mm' % (len(A), len(B), worst))
if len(sys.argv) > 4:
    ga, gb = gallery_lines(sys.argv[3]), gallery_lines(sys.argv[4])
    print('-- A gallery:'); [print('   ' + l) for l in ga]
    print('-- B gallery:'); [print('   ' + l) for l in gb]
    ca = [l for l in ga if 'obituary card' in l]; cb = [l for l in gb if 'obituary card' in l]
    norm = lambda l: re.sub(r'^(PASS|FAIL)\s+', '', l[0]) if l else ''
    if norm(ca) != norm(cb):
        print('FAIL the Obituary card lines differ'); fails += 1
    else:
        print('PASS the Obituary card lines are the same')
print('ALL PASS' if fails == 0 else 'FAILED: %d' % fails)
sys.exit(1 if fails else 0)
