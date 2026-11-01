#!/usr/bin/env python3
"""Confere as assinaturas que o DLL usa para chamar/enganchar funcoes do exe contra o prototipo x64 real
(out/protos.json). Classes de argumento: f=float/double (XMM no x64), q=64 bits inteiro, p=ponteiro, i=<=32 bits, v=variadico.
Uso: checksig.py [arquivos...]  (padrao: ext/src + ext/WotLKExtensions)"""
import re, os, sys, json
W = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
P = json.load(open(os.path.join(W, 'out', 'protos.json')))

def cls(t):
    t = re.sub(r'/\*.*?\*/', '', t).strip()   # comentario no meio do parametro (ex. "uint32_t /*a6*/")
    if re.match(r'^(const\s+)?FT_(Face|Library|GlyphSlot|Size|Memory)\b(?!\w)', t): return 'p'   # typedefs ponteiro do FreeType
    if not t: return None
    if t == '...': return 'v'
    if t == 'ASC_GUID2_T' or t.startswith('ASC_GUID2_P('): return 'q'   # x64: um uint64_t
    t2 = re.sub(r'\b(const|volatile|struct|class|enum|unsigned|signed)\b', ' ', t).strip()
    if '*' not in t and '&' not in t and re.search(r'^(__int64|u?int64_t|long long|WOWGUID|WoWGUID|size_t|u?intptr_t|ptrdiff_t|ULONGLONG|Hos)$', t2.split()[-1] if t2.split() else ''): return 'q'
    if re.search(r'^(Hos|HANDLE|HMODULE|HWND|LPVOID|PVOID|LPCSTR|LPSTR)$', t2.split()[-1] if t2.split() else ''): return 'p'
    if '*' not in t and '&' not in t and re.search(r'^u?int(8|16|32)_t$', t2.split()[-1] if t2.split() else ''): return 'i'
    if '*' in t or '&' in t or 'lua_CFunction' in t or t2.endswith('_t*') or re.search(r'Fn\b|_t$|Func|Callback', t2) and '(' not in t2 and not re.search(r'u?int\d+_t', t2):
        if not re.search(r'^(u?int\d+_t|size_t|DWORD|BOOL|bool|char|short|int|long)$', t2.split()[-1] if t2.split() else ''):
            return 'p'
    if re.search(r'\b(float|double)\b', t2): return 'f'
    if re.search(r'__int64|int64_t|uint64_t|long long|WOWGUID|WoWGUID|CGGUID|ULONGLONG|size_t|uintptr_t|intptr_t|ptrdiff_t', t2): return 'q'
    if t2 in ('void',): return None
    return 'i'

SCALAR = re.compile(r'^(void|bool|char|short|int|long|float|double|unsigned.*|signed.*|u?int\d+_t|__int64|size_t|WOWGUID|.*\*|.*&|[A-Z_]+|HRESULT|BOOL|DWORD)$')
def proto_cls(pr):
    a = [cls(x) for x in pr['args']]
    ret = (pr.get('ret') or '').replace('const ', '').strip()
    hidden = ['p'] if ret not in ('', '?') and not SCALAR.match(ret) else []   # struct por valor: ponteiro oculto
    return (['p'] if pr.get('this') else []) + hidden + [x for x in a if x]

def split_args(s):
    out, d, cur = [], 0, ''
    for ch in s:
        if ch in '(<[': d += 1
        elif ch in ')>]': d -= 1
        if ch == ',' and d == 0: out.append(cur); cur = ''
        else: cur += ch
    if cur.strip(): out.append(cur)
    return [re.sub(r'\s+\w+\s*$', '', x).strip() if re.search(r'[\w\*&>]\s+\w+\s*$', x.strip()) and not re.search(r'^(unsigned|signed|const)?\s*\w+$', x.strip()) else x.strip() for x in out]

def dll_cls(conv, args):
    if 'ASC_EDX_P' in args:   # visao x64: a macro some e nao ha edx a descartar
        args = re.sub(r'\s*ASC_EDX_P\([^()]*\)', '', args); conv = '__thiscall'
    a = [cls(x) for x in split_args(args)]
    a = [x for x in a if x]
    if conv == '__fastcall' and len(a) >= 2:   # emulacao de __thiscall: (this, edx, ...)
        a = [a[0]] + a[2:]
    return a

rx_cf = re.compile(r'CLIENT_FUNCTION\(\s*(\w+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*(\w+)\s*,\s*([^,]+?)\s*,\s*\((.*?)\)\s*\)')
rx_rc = re.compile(r'reinterpret_cast<\s*([^<>()]+?)\s*\(\s*(__cdecl|__stdcall|__fastcall|__thiscall)?\s*\*\s*\)\s*\(([^()]*(?:\([^()]*\)[^()]*)*)\)\s*>\s*\(\s*(?:ASC_PTR\(\s*)?(0x[0-9A-Fa-f]{6,8})\s*\)')
rx_td = re.compile(r'typedef\s+([^;()]+?)\s*\(\s*(__cdecl|__stdcall|__fastcall|__thiscall)?\s*\*\s*(\w+)\s*\)\s*\(([^;]*?)\)\s*;')
rx_us = re.compile(r'using\s+(\w+)\s*=\s*([^;()]+?)\s*\(\s*(__cdecl|__stdcall|__fastcall|__thiscall)?\s*\*\s*\)\s*\(([^;]*?)\)\s*;')
rx_tdcall = re.compile(r'(?:reinterpret_cast<\s*(\w+)\s*>|\(\s*(\w+)\s*\))\s*\(\s*ASC_PTR\(\s*(0x[0-9A-Fa-f]{6,8})\s*\)\s*\)')
rx_tdhook = re.compile(r'reinterpret_cast<\s*(\w+)\s*>\s*\(\s*AscRuntime::(?:Detour|ReplaceFunction)\(\s*(0x[0-9A-Fa-f]{6,8})')
def x86_only_lines(txt):
    """Linhas que so compilam no x86: ramo #else de '#if ASC_X64' e corpo de '#if !ASC_X64'."""
    out, stack = set(), []   # stack: (tipo, em_x86_only)
    for n, line in enumerate(txt.split('\n'), 1):
        t = line.strip()
        if re.match(r'#\s*if', t):
            if re.match(r'#\s*if\s+ASC_X64\b', t): stack.append(('x64', False))
            elif re.match(r'#\s*if\s+!\s*ASC_X64\b', t): stack.append(('nx64', True))
            else: stack.append(('o', False))
            continue
        if re.match(r'#\s*else', t) and stack:
            k, v = stack[-1]
            if k == 'x64': stack[-1] = (k, True)
            elif k == 'nx64': stack[-1] = (k, False)
            continue
        if re.match(r'#\s*endif', t) and stack: stack.pop(); continue
        if any(v for _, v in stack): out.add(n)
    return out

CM = open(os.path.join(W, 'ext', 'CMakeLists.txt')).read()
EXCL = [re.compile(m.replace('\\\\', '\\')) for m in re.findall(r'list\(FILTER UP_SOURCES EXCLUDE REGEX "([^"]+)"\)', CM)]
# compilados mas so chamados por arquivos excluidos (codigo morto)
DEAD = ('WorldData/CWorld.cpp', 'Client/CFormula.cpp', 'Client/CGChat.cpp', 'Client/ClientServices.cpp')
def excluded(f):
    g = f.replace(os.sep, '/')
    return any(r.search(g) for r in EXCL) or g.endswith(DEAD)

def scan(paths):
    res = []
    for root in paths:
        files = [root] if os.path.isfile(root) else [os.path.join(dp, f) for dp, _, fs in os.walk(root) for f in fs if f.endswith(('.cpp', '.hpp', '.h', '.inl'))]
        for f in files:
            if excluded(f): continue
            txt = open(f, errors='ignore').read()
            skip = x86_only_lines(txt)
            ln = lambda m: txt.count('\n', 0, m.start()) + 1
            tds = {m.group(3): (m.group(2) or '__cdecl', m.group(4), m.group(1)) for m in rx_td.finditer(txt) if ln(m) not in skip}
            tds.update({m.group(1): (m.group(3) or '__cdecl', m.group(4), m.group(2)) for m in rx_us.finditer(txt) if ln(m) not in skip})
            for n, line in enumerate(txt.split('\n'), 1):
                code = line.split('//')[0]
                for m in list(rx_tdcall.finditer(code)) + list(rx_tdhook.finditer(code)):
                    g = m.groups(); name = g[0] or (g[1] if len(g) > 2 else None); addr = g[-1]
                    if name in tds and n not in skip:
                        res.append((f, n, name, int(addr, 16), tds[name][0], tds[name][1], tds[name][2], 'hook' if m.re is rx_tdhook else 'call'))
            for n, line in enumerate(open(f, errors='ignore'), 1):
                if n in skip: continue
                code = line.split('//')[0]
                for m in rx_cf.finditer(code):
                    res.append((f, n, m.group(1), int(m.group(2), 16), m.group(3), m.group(5), m.group(4), 'call'))
                for m in rx_rc.finditer(code):
                    res.append((f, n, '', int(m.group(4), 16), m.group(2) or '__cdecl', m.group(3), m.group(1), 'call'))
    return res

if __name__ == '__main__':
    paths = sys.argv[1:] or [os.path.join(W, 'ext', 'src'), os.path.join(W, 'ext', 'WotLKExtensions', 'src')]
    ok = bad = miss = rb = 0; rows = []
    for f, n, name, a, conv, args, dret, kind in scan(paths):
        pr = P.get('%X' % a)
        rel = os.path.relpath(f, os.path.join(W, 'ext'))
        if not pr: miss += 1; rows.append('SEM_PROTO %s:%d 0x%X %s' % (rel, n, a, name)); continue
        # retorno: o exe devolve bool/char (so AL vale) e o DLL le int de 32/64 bits -> lixo no EAX alto no x64
        # (01/11: HasAuraBySpellId 0x7282A0 lido como int acendia todos os SpellActivationOverlays)
        xr = re.sub(r'\b(const|unsigned|signed)\b', ' ', pr.get('ret') or '').split()
        dr = re.sub(r'\b(const|unsigned|signed)\b', ' ', dret or '').split()
        if xr and xr[-1] in ('bool', 'char', 'uint8_t', 'int8_t', 'BYTE') and dr and dr[-1] in (
                'int', 'long', 'int32_t', 'uint32_t', 'BOOL', 'DWORD', 'int64_t', 'uint64_t', 'uintptr_t', 'intptr_t', 'size_t', '__int64'):
            linha = open(f, errors='ignore').read().split('\n')[n - 1]
            if kind == 'hook':   # trampolim: so e bug se o DLL TESTAR o retorno; repassar ao exe e ok (revisado 01/11)
                rows.append('aviso RETORNO_BOOL_HOOK %s:%d 0x%X %s (%s -> %s %s)' % (rel, n, a, name, (dret or '').strip(), pr.get('ret'), pr['name']))
            elif not re.search(r'&\s*0x[fF][fF]\b', linha):
                rb += 1; rows.append('RETORNO_BOOL %s:%d 0x%X %s\n    dll : %s ...\n    x64 : %s %s' % (
                    rel, n, a, name, (dret or '').strip(), pr.get('ret'), pr['name']))
        d = dll_cls(conv, args); r = proto_cls(pr)
        if 'v' in d and 'v' not in r:   # PDB nao marca '...' no arglist
            d = d[:d.index('v')]; r = r[:len(d)]
        if 'v' in r: r = r[:r.index('v')]; d = d[:len(r)] if len(d) >= len(r) else d
        # benigno: o DLL passa 64 bits onde o callee le 32 (so estreita)
        if len(d) == len(r) and all(x == y or (x == 'q' and y == 'i') or {x, y} == {'p', 'q'} or (x == 'p' and y == 'i') for x, y in zip(d, r)): ok += 1
        else:
            bad += 1; rows.append('DIVERGE %s:%d 0x%X %s\n    dll : %s %s(%s)\n    x64 : %s %s(%s)' % (rel, n, a, name, conv, ''.join(d), args.strip(), pr['name'], ''.join(r), ', '.join(pr['args'])))
    print('\n'.join(rows)); print('ok %d  diverge %d  sem_proto %d  retorno_bool %d' % (ok, bad, miss, rb), file=sys.stderr)
