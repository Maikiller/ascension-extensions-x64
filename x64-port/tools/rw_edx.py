#!/usr/bin/env python3
"""Emulacao de __thiscall via __fastcall(this, edx, ...): no x64 nao ha edx falso (rcx=this, rdx=1o arg).
Reescreve o 2o parametro dos __fastcall para ASC_EDX_P(...) e os usos do nome dele como argumento para
ASC_EDX_A(nome). x86 fica byte-identico (macros expandem para ', x')."""
import re, os, sys
W = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SECOND = r'(?:void\s*\*\s*(?:/\*\s*edx\s*\*/)?\s*\w*|u?int32_t\s+unused)'
# __fastcall seguido de lista de parametros: typedef R(__fastcall* T)(A, B, ...), cast (__fastcall*)(A, B...), def R __fastcall F(A, B, ...)
rx = re.compile(r'(__fastcall\s*(?:\*\s*\w*\s*\)|\*\s*\)|[\w:]+)\s*\(\s*[^,()]+?)\s*,\s*(' + SECOND + r')\s*(?=[,)])')
def body_end(s, i):
    j = s.find('{', i); k = s.find(';', i)
    if j < 0 or (0 <= k < j): return None
    d = 0
    for p in range(j, len(s)):
        if s[p] == '{': d += 1
        elif s[p] == '}':
            d -= 1
            if d == 0: return (j, p)
    return None
def run(path):
    s = open(path, errors='ignore').read(); orig = s; out = []; pos = 0; n = 0
    for m in rx.finditer(s):
        out.append(s[pos:m.start()]); sec = m.group(2).strip()
        out.append(m.group(1) + ' ASC_EDX_P(' + sec + ')'); pos = m.end(); n += 1
    out.append(s[pos:]); s = ''.join(out)
    # corpos: nome do 2o parametro usado como argumento
    for m in re.finditer(r'ASC_EDX_P\((?:void\s*\*\s*(?:/\*\s*edx\s*\*/)?\s*|u?int32_t\s+)(\w+)\)', s):
        name = m.group(1)
        if name in ('unused',) and False: pass
        be = body_end(s, m.end())
        if not be: continue
        a, b = be; body = s[a:b]
        nb = re.sub(r'\s*,\s*' + re.escape(name) + r'\b(?=\s*[,)])', ' ASC_EDX_A(' + name + ')', body)
        if nb != body: s = s[:a] + nb + s[b:]
    if s != orig:
        if 'ASC_EDX_P' in s and 'AscX64.hpp' not in s and 'AscX64.hpp' not in path:
            s = re.sub(r'(#include[^\n]*\n)', r'#include <Ascension/AscX64.hpp>\n\1', s, count=1)
        open(path, 'w').write(s)
    return n
tot = 0
for root in sys.argv[1:]:
    for dp, _, fs in os.walk(root):
        for f in fs:
            if f.endswith(('.cpp', '.hpp', '.h')):
                k = run(os.path.join(dp, f))
                if k: tot += k; print(k, os.path.join(dp, f))
print('total', tot)
