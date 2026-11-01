#!/usr/bin/env python3
"""Offsets x86 lidos de ponteiros de objeto (unit/player/object/...) no codigo x64 vivo do DLL, sem ASC_OFF.
Agrupa por (classe presumida, offset) e mostra o equivalente x64 via out/layouts.json (layout.py)."""
import re, os, sys, collections, subprocess
W = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(W, 'tools')); import checksig as C
CLS = {'unit': 'CGUnit_C', 'target': 'CGUnit_C', 'pet': 'CGUnit_C', 'caster': 'CGUnit_C', 'mover': 'CGUnit_C',
       'player': 'CGPlayer_C', 'me': 'CGPlayer_C', 'active': 'CGPlayer_C',
       'object': 'CGObject_C', 'obj': 'CGObject_C', 'self': 'CGObject_C', 'go': 'CGGameObject_C',
       'item': 'CGItem_C', 'container': 'CGContainer_C'}
rx = re.compile(r'\b(%s)\)?\s*\+\s*(0x[0-9A-Fa-f]+|\d+)\b' % '|'.join(CLS))
hits = collections.defaultdict(list)
for root in (os.path.join(W, 'ext', 'src'), os.path.join(W, 'ext', 'WotLKExtensions', 'src')):
    for dp, _, fs in os.walk(root):
        for f in fs:
            p = os.path.join(dp, f)
            if not f.endswith(('.cpp', '.hpp')) or C.excluded(p): continue
            txt = open(p, errors='ignore').read(); skip = C.x86_only_lines(txt)
            for i, line in enumerate(txt.split('\n'), 1):
                if i in skip or 'ASC_OFF' in line: continue
                code = line.split('//')[0]
                for m in rx.finditer(code):
                    hits[(CLS[m.group(1)], int(m.group(2), 0))].append('%s:%d' % (os.path.relpath(p, os.path.join(W, 'ext')), i))
lay = {}
if '--map' in sys.argv:
    for (c, o) in hits:
        r = subprocess.run([sys.executable, os.path.join(W, 'tools', 'layout.py'), c, hex(o)], capture_output=True, text=True).stdout.strip()
        lay[(c, o)] = r.split('->', 1)[-1].strip() if r else '?'
for k in sorted(hits, key=lambda k: -len(hits[k])):
    print('%-12s 0x%-5X %3d  %s  %s' % (k[0], k[1], len(hits[k]), lay.get(k, ''), ' '.join(hits[k][:3])))
print('total', sum(map(len, hits.values())), 'pares', len(hits), file=sys.stderr)
