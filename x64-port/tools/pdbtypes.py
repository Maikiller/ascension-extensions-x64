#!/usr/bin/env python3
"""Indexa out/types.txt (llvm-pdbutil dump --types do x64) -> out/types.json:
  classes: nome -> {membro: tipo}   (tipo = 'C:<classe>' | 'P' ponteiro | 'A:<elem>:<bytes64>' | 'B:<prim>')"""
import re, json, os
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'out')
recs = {}; cur = None
rxh = re.compile(r'^\s+0x([0-9A-F]+) \| (LF_\w+) \[size = \d+\](?: `(.*)`)?')
for line in open(os.path.join(OUT, 'types.txt'), errors='ignore'):
    m = rxh.match(line)
    if m:
        cur = {'k': m.group(2), 'name': m.group(3), 'lines': []}; recs[int(m.group(1), 16)] = cur; continue
    if cur is not None: cur['lines'].append(line.strip())
PRIM = {0x10: 1, 0x20: 1, 0x68: 1, 0x69: 1, 0x11: 2, 0x21: 2, 0x12: 4, 0x22: 4, 0x74: 4, 0x75: 4, 0x13: 8, 0x23: 8, 0x76: 8, 0x77: 8, 0x40: 4, 0x41: 8, 0x30: 1, 0x70: 1, 0x71: 2, 0x7A: 2, 0x7B: 4}
def tdesc(ti, depth=0):
    if ti < 0x1000:
        if (ti >> 8) & 0x7: return 'P'          # ponteiro para primitivo
        return 'B:%d' % PRIM.get(ti & 0xFF, 4)
    r = recs.get(ti)
    if not r or depth > 8: return '?'
    k = r['k']; L = ' '.join(r['lines'])
    if k in ('LF_CLASS', 'LF_STRUCTURE', 'LF_UNION', 'LF_INTERFACE'): return 'C:' + (r['name'] or '?')
    if k == 'LF_POINTER':
        return 'P'
    if k == 'LF_MODIFIER':
        m = re.search(r'referent = 0x([0-9A-F]+)', L); return tdesc(int(m.group(1), 16), depth + 1) if m else '?'
    if k == 'LF_ENUM': return 'B:4'
    if k == 'LF_ARRAY':
        m = re.search(r'element type: 0x([0-9A-F]+)', L); s = re.search(r'size: (\d+)', L)
        return 'A:%s:%s' % (tdesc(int(m.group(1), 16), depth + 1) if m else '?', s.group(1) if s else '?')
    if k == 'LF_BITFIELD':
        m = re.search(r'type = 0x([0-9A-F]+)', L); return tdesc(int(m.group(1), 16), depth + 1) if m else 'B:4'
    return '?'
classes = {}
for ti, r in recs.items():
    if r['k'] not in ('LF_CLASS', 'LF_STRUCTURE', 'LF_UNION'): continue
    L = ' '.join(r['lines'])
    if 'forward ref' in L: continue
    m = re.search(r'field list: 0x([0-9A-F]+)', L)
    if not m: continue
    fl = recs.get(int(m.group(1), 16))
    if not fl: continue
    mem = {}
    for l in fl['lines']:
        mm = re.match(r'- LF_MEMBER \[name = `([^`]+)`, Type = 0x([0-9A-F]+)', l)
        if mm: mem[mm.group(1)] = tdesc(int(mm.group(2), 16))
    classes.setdefault(r['name'], mem)
json.dump(classes, open(os.path.join(OUT, 'types.json'), 'w'))
print('classes com membros:', len(classes))
