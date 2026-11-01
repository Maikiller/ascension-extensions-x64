#!/usr/bin/env python3
"""Envolve cada funcao __declspec(naked) em #if !ASC_X64 ... #else stub AscX64::Unported #endif."""
import re, sys
for p in sys.argv[1:]:
    s = open(p).read(); out = []; i = 0; n = 0
    rx = re.compile(r'^([ \t]*)((?:static\s+)?(?:void|int|uint32_t|bool)\s+)?__declspec\(naked\)\s*((?:static\s+)?(?:void|int|uint32_t|bool)\s+)?(?:__cdecl\s+|__fastcall\s+|__stdcall\s+|__thiscall\s+)?(\w+)\s*\(([^)]*)\)\s*\n?\s*\{', re.M)
    while True:
        m = rx.search(s, i)
        if not m: out.append(s[i:]); break
        if '#if !ASC_X64' in s[max(0, m.start() - 40):m.start()]: out.append(s[i:m.end()]); i = m.end(); continue
        k = m.end() - 1; depth = 0
        while True:
            if s[k] == '{': depth += 1
            elif s[k] == '}':
                depth -= 1
                if depth == 0: break
            k += 1
        ind = m.group(1); ret = (m.group(2) or m.group(3) or 'void ').strip(); name = m.group(4); args = m.group(5)
        stat = 'static ' if 'static' in (m.group(2) or '') + (m.group(3) or '') else ''
        retv = '' if ret.endswith('void') else ' return {};'
        stub = '%s%s%s %s(%s) { AscX64::Unported("%s");%s }' % (ind, stat, ret.replace('static', '').strip(), name, args, name, retv)
        out.append(s[i:m.start()]); out.append('#if !ASC_X64\n' + s[m.start():k + 1] + '\n#else\n' + stub + '\n#endif')
        i = k + 1; n += 1
    s = ''.join(out)
    if n and '#include <Ascension/AscX64.hpp>' not in s:
        s = re.sub(r'(#include[^\n]*\n)', r'\1#include <Ascension/AscX64.hpp>\n', s, count=1)
    open(p, 'w').write(s); print(p, n)
