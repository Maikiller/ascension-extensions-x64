#!/usr/bin/env python3
"""Membros (offset x64, nome, tipo) de uma classe/struct direto do PDB do WowClient64 (out/types.txt,
llvm-pdbutil dump --types). Inclui bases (LF_BCLASS). Uso: pdbfields.py 'NomeExato<...>'"""
import re, sys, os
W = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
T = open(os.path.join(W, 'out', 'types.txt'), errors='ignore').read().split('\n')
idx = {}
for i, l in enumerate(T):
    m = re.match(r'\s*(0x[0-9A-F]+) \| (LF_\w+)', l)
    if m: idx[m.group(1)] = i
def block(tid):
    i = idx[tid]; out = [T[i]]; j = i + 1
    while j < len(T) and not re.match(r'\s*0x[0-9A-F]+ \|', T[j]): out.append(T[j]); j += 1
    return out
def find_class(name):
    best = None
    for i, l in enumerate(T):
        m = re.match(r'\s*(0x[0-9A-F]+) \| LF_(CLASS|STRUCTURE|UNION) \[size = \d+\] `(.+)`$', l)
        if m and m.group(3) == name:
            b = block(m.group(1)); txt = '\n'.join(b)
            if 'forward ref' in txt: continue
            fl = re.search(r'field list: (0x[0-9A-F]+)', txt); sz = re.search(r'sizeof (\d+)', txt)
            if fl: best = (fl.group(1), int(sz.group(1)) if sz else -1)
    return best
def fields(name, base=0, depth=0):
    c = find_class(name)
    if not c: print('  ' * depth + '?? %s' % name); return
    fl, sz = c
    print('  ' * depth + '%s  sizeof %d' % (name, sz))
    for l in block(fl):
        m = re.search(r'LF_BCLASS.*type = (0x[0-9A-F]+) \(([^)]*)\).*offset = (\d+)', l)
        if m: fields(m.group(2).replace('class ', '').replace('struct ', ''), base + int(m.group(3)), depth + 1); continue
        m = re.search(r'LF_MEMBER \[name = `(\w+)`, Type = (0x[0-9A-F]+)(?: \(([^)]*)\))?, offset = (\d+)', l)
        if m: print('  ' * depth + '  +0x%X %s : %s' % (base + int(m.group(4)), m.group(1), m.group(3) or m.group(2)))
        if 'LF_VFUNCTAB' in l: print('  ' * depth + '  +0x%X {vfptr}' % base)
for n in sys.argv[1:]: fields(n)
