#!/usr/bin/env python3
"""Para cada DIVERGE do checksig em que o DLL passa 32 bits (i) onde o x64 quer ponteiro/64 bits (p/q) e o numero de
argumentos bate, troca o tipo do parametro por uintptr_t/intptr_t (identico no x86) na declaracao (cast inline ou
typedef) e na funcao de hook ligada ao mesmo endereco (Detour/ReplaceFunction/Hook<...>(0xADDR, ..., &Fn))."""
import re, os, sys
W = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(W, 'tools'))
import checksig as C
INT = r'(?:const\s+)?(?:u?int32_t|unsigned(?:\s+int)?|int|DWORD|uint8_t|uint16_t|int8_t|int16_t)'
def widen_list(params, mask):
    """params: texto entre parenteses; mask: indices (na visao x64, sem ASC_EDX_P) a alargar."""
    parts = []; d = 0; cur = ''
    for ch in params:
        if ch in '(<[': d += 1
        elif ch in ')>]': d -= 1
        if ch == ',' and d == 0: parts.append(cur); cur = ''
        else: cur += ch
    parts.append(cur)
    # ASC_EDX_P fica grudado no 1o parametro ("void* self ASC_EDX_P(void* edx)") -- nao conta como posicao
    idx = 0; out = []
    for p in parts:
        if idx in mask:
            m = re.match(r'(\s*)(' + INT + r')(\b.*)$', p, re.S)
            if m and '*' not in m.group(3).split('ASC_EDX_P')[0] and '&' not in m.group(3):
                t = 'intptr_t' if re.search(r'\b(int32_t|int|int8_t|int16_t)\b', m.group(2)) and not re.search(r'u|unsigned|DWORD', m.group(2)) else 'uintptr_t'
                p = m.group(1) + t + m.group(3)
        out.append(p); idx += 1
    return ','.join(out)
def main():
    rows = open(os.path.join(W, 'out', 'checksig.txt')).read().split('DIVERGE ')[1:]
    edits = {}
    for r in rows:
        L = r.split('\n'); head = L[0].split()
        loc, addr = head[0], head[1]; name = head[2] if len(head) > 2 else ''
        d = re.search(r'dll : \S+ (\w*)\(', L[1]).group(1); x = re.search(r'x64 : .*? (\w*)\(', L[2]).group(1)
        if len(d) != len(x): continue
        diffs = [(a, b) for a, b in zip(d, x) if a != b]
        if not diffs or any(not (a == 'i' and b in 'pq') for a, b in diffs): continue
        mask = {i for i, (a, b) in enumerate(zip(d, x)) if a == 'i' and b in 'pq'}
        edits.setdefault(loc.split(':')[0], []).append((int(loc.split(':')[1]), addr, name, mask))
    n = 0
    for rel, lst in edits.items():
        path = os.path.join(W, 'ext', rel); s = open(path).read(); lines = s.split('\n')
        for ln, addr, name, mask in lst:
            fast = False
            if name:   # typedef
                rx = re.compile(r'(typedef\s+[^;()]+?\(\s*(__\w+)?\s*\*\s*' + re.escape(name) + r'\s*\)\s*\()([^;]*?)(\)\s*;)')
                m = rx.search(s)
                if m:
                    s = s[:m.start()] + m.group(1) + widen_list(m.group(3), mask) + m.group(4) + s[m.end():]; n += 1
            else:      # cast inline na linha
                lines = s.split('\n'); l = lines[ln - 1]
                m = re.search(r'(reinterpret_cast<\s*[^<>()]+?\(\s*(?:__\w+)?\s*\*\s*\)\s*\()([^()]*(?:\([^()]*\)[^()]*)*)(\)\s*>\s*\(\s*ASC_PTR\(\s*' + addr + r')', l, re.I)
                if m:
                    lines[ln - 1] = l[:m.start()] + m.group(1) + widen_list(m.group(2), mask) + m.group(3) + l[m.end():]
                    s = '\n'.join(lines); n += 1
            # funcao de hook ligada ao endereco
            for hm in re.finditer(r'\(\s*' + addr + r'\s*,[^;]*?&\s*(\w+)\s*\)', s, re.I):
                fn = hm.group(1)
                fm = re.search(r'(\b[\w:*&<> ]+?\s(?:__\w+\s+)?' + re.escape(fn) + r'\s*\()([^)]*(?:\([^()]*\)[^)]*)*)(\)\s*(?://[^\n]*)?\s*\{)', s)
                if fm:
                    s = s[:fm.start()] + fm.group(1) + widen_list(fm.group(2), mask) + fm.group(3) + s[fm.end():]
        open(path, 'w').write(s)
    print('alargados', n)
main()
