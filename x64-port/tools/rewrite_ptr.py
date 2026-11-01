#!/usr/bin/env python3
"""Troca enderecos literais do exe 12340 em contexto de cast por ASC_PTR(0x...):
   reinterpret_cast<T>(0xADDR)  ->  reinterpret_cast<T>(ASC_PTR(0xADDR))
   (T*)0xADDR / (T)(0xADDR)     ->  (T*)ASC_PTR(0xADDR)
So codigo (nao comentarios), so enderecos em [0x401000, 0xE00000). Inclui AscX64.hpp quando muda o arquivo."""
import re, sys, os
LO, HI = 0x401000, 0xE00000
rx_rc = re.compile(r'(reinterpret_cast<[^;]*?>\s*\(\s*)(0x[0-9A-Fa-f]{5,8})(\s*\))')
rx_c = re.compile(r'(\(\s*(?:const\s+)?[\w:]+(?:\s*<[^;()]*?>)?\s*(?:const\s*)?\*+\s*(?:const\s*)?\)\s*)\(?\s*(0x[0-9A-Fa-f]{5,8})\s*\)?')
def inrange(h): v = int(h, 16); return LO <= v < HI
def fix_code(code):
    n = 0
    def r1(m):
        nonlocal n
        if not inrange(m.group(2)): return m.group(0)
        n += 1; return m.group(1) + 'ASC_PTR(' + m.group(2) + ')' + m.group(3)
    def r2(m):
        nonlocal n
        if not inrange(m.group(2)): return m.group(0)
        n += 1; return m.group(1) + 'ASC_PTR(' + m.group(2) + ')'
    code = rx_rc.sub(r1, code); code = rx_c.sub(r2, code)
    return code, n
tot = 0
for root in sys.argv[1:]:
    files = [root] if os.path.isfile(root) else [os.path.join(dp, f) for dp, _, fs in os.walk(root) for f in fs if f.endswith(('.cpp', '.hpp', '.h', '.inl'))]
    for f in files:
        if f.endswith(('AscX64.cpp', 'AscX64.hpp')): continue
        lines = open(f, errors='ignore').read().split('\n'); changed = 0; incomment = False
        for i, line in enumerate(lines):
            if line.lstrip().startswith('#'): continue
            if '/*' in line and '*/' not in line: incomment = True
            if incomment:
                if '*/' in line: incomment = False
                continue
            idx = line.find('//'); code, cm = (line, '') if idx < 0 else (line[:idx], line[idx:])
            new, n = fix_code(code)
            if n: lines[i] = new + cm; changed += n
        if changed:
            s = '\n'.join(lines)
            if 'AscX64.hpp' not in s:
                s = re.sub(r'(#include[^\n]*\n)', r'\1#include <Ascension/AscX64.hpp>\n', s, count=1)
            open(f, 'w').write(s); tot += changed; print('%5d %s' % (changed, f))
print('total', tot)
