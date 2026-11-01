#!/usr/bin/env python3
"""Layouts de classe do fonte do cliente (MSVC /d1reportAllClassLayout) em Win32 x x64.
  layout.py build            -> out/layouts.json (a partir de out/probe-win32.log e out/probe-x64.log)
  layout.py CLASSE 0xOFF ... -> campo que cobre o offset de 32 bits e o offset x64 equivalente
  layout.py find 0xOFF [tamanho32]  -> classes que tem um campo comecando exatamente nesse offset (ajuda a achar o tipo)"""
import re, sys, json, os, bisect
W = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(W, 'out')

def parse(path):
    cls = {}; cur = None; stack = []
    txt = open(path, encoding='utf-8', errors='ignore').read().split('\n')
    for line in txt:
        m = re.match(r'^\s*class (.+?)\tsize\((\d+)\):', line)
        if m:
            cur = {'size': int(m.group(2)), 'fields': []}; cls[m.group(1).strip()] = cur; stack = []; continue
        if cur is None: continue
        if '$vftable@' in line or '$vbtable@' in line: cur = None; continue
        m = re.match(r'^\s*(\d+)\t((?:\| ?)*)(.*)$', line)
        if not m: 
            if line.strip() == '' : cur = None
            continue
        off = int(m.group(1)); lvl = m.group(2).count('|') - 1; rest = m.group(3).strip()
        stack = stack[:lvl]
        b = re.match(r'\+--- \(base class (.+)\)', rest)
        if b: stack.append('<' + b.group(1).strip() + '>'); continue
        if rest.startswith('+---') or rest.startswith('<alignment'): continue
        name = rest
        cur['fields'].append([off, '.'.join(stack + [name])])
    return cls

def N(n): return re.sub(r'\b(class|struct|union|enum) ', '', n).replace(' ', '')

def build():
    a = parse(os.path.join(OUT, 'probe-win32.log')); b = parse(os.path.join(OUT, 'probe-x64.log'))
    a = {N(k): v for k, v in a.items()}; b = {N(k): v for k, v in b.items()}
    json.dump({'win32': a, 'x64': b}, open(os.path.join(OUT, 'layouts.json'), 'w'))
    print('classes win32', len(a), 'x64', len(b))

def load():
    return json.load(open(os.path.join(OUT, 'layouts.json')))

def query(L, c, off):
    a = L['win32'].get(c); b = L['x64'].get(c)
    if not a: return '%s: classe desconhecida' % c
    fa = [f for f in a['fields'] if f[1] not in ('{vfptr}',)] + [f for f in a['fields'] if f[1] == '{vfptr}']
    fa = sorted(a['fields'])
    cand = [f for f in fa if f[0] <= off]
    if not cand: return '%s+0x%X: antes do 1o campo' % (c, off)
    f = max(cand, key=lambda x: x[0]); delta = off - f[0]
    nb = [g for g in b['fields'] if g[1] == f[1]] if b else []
    x = ('x64 +0x%X' % (nb[0][0] + delta)) if nb else 'x64 ?'
    nxt = [g[0] for g in fa if g[0] > f[0]]
    span = (min(nxt) if nxt else a['size']) - f[0]
    warn = '' if delta == 0 else '  (dentro do campo +%d de %d bytes: conferir tipo)' % (delta, span)
    return '%s+0x%X -> %s @ win32 +0x%X; %s  [size win32 %d / x64 %d]%s' % (c, off, f[1], f[0], x, a['size'], b['size'] if b else 0, warn)

_T = None
def types():
    global _T
    if _T is None:
        t = json.load(open(os.path.join(OUT, 'types.json')))
        fix = lambda d: re.sub(r'^(A:)?C:([^:]*)', lambda m: (m.group(1) or '') + 'C:' + N(m.group(2)), d)
        _T = {N(k): {mk: fix(mv) for mk, mv in v.items()} for k, v in t.items()}
    return _T

def esize(L, d):
    if d == 'P': return 4, 8
    if d.startswith('B:'): n = int(d[2:]); return n, n
    if d.startswith('C:'):
        c = N(d[2:]); a = L['win32'].get(c); b = L['x64'].get(c)
        if a and b: return a['size'], b['size']
    return None, None

def trans(L, c, off, depth=0):
    """(caminho, offset x64) para o offset de 32 bits `off` dentro da classe `c`; levanta ValueError se nao der."""
    c = N(c); a = L['win32'].get(c); b = L['x64'].get(c)
    if not a or not b: raise ValueError('classe sem layout: ' + c)
    if off >= a['size']: raise ValueError('%s+0x%X alem do tamanho %d' % (c, off, a['size']))
    fa = sorted(f for f in a['fields'])
    f = max((x for x in fa if x[0] <= off), key=lambda x: x[0]); delta = off - f[0]
    nb = [g for g in b['fields'] if g[1] == f[1]]
    if not nb: raise ValueError('campo %s sem par no x64' % f[1])
    base64 = nb[0][0]; path = f[1]
    if delta == 0: return path, base64
    segs = path.split('.'); owner = c
    for sg in segs[:-1]:
        if sg.startswith('<'): owner = N(sg[1:-1])
    mem = segs[-1].split(' ')[-1]
    d = types().get(owner, {}).get(mem)
    if d is None: raise ValueError('tipo de %s::%s desconhecido (delta %d)' % (owner, mem, delta))
    if d.startswith('C:'):
        p2, o2 = trans(L, d[2:], delta, depth + 1); return path + '.' + p2, base64 + o2
    if d.startswith('A:'):
        e = d[2:d.rfind(':')]; s32, s64 = esize(L, e)
        if not s32: raise ValueError('array de %s sem tamanho' % e)
        i, inner = divmod(delta, s32)
        if inner and e.startswith('C:'):
            p2, o2 = trans(L, e[2:], inner, depth + 1); return '%s[%d].%s' % (path, i, p2), base64 + i * s64 + o2
        if inner: raise ValueError('meio de elemento %s' % e)
        return '%s[%d]' % (path, i), base64 + i * s64
    if d.startswith('B:') and int(d[2:]) > delta:
        return '%s(+%d)' % (path, delta), base64 + delta
    raise ValueError('%s: delta %d em tipo %s' % (path, delta, d))

if __name__ == '__main__':
    if sys.argv[1] == 'build': build(); sys.exit()
    L = load()
    if sys.argv[1] == 'find':
        off = int(sys.argv[2], 16)
        for c, v in L['win32'].items():
            for f in v['fields']:
                if f[0] == off and not f[1].startswith('<'): print('%-40s %s' % (c, f[1]))
        sys.exit()
    for o in sys.argv[2:]:
        try:
            p_, x = trans(L, sys.argv[1], int(o, 16)); print('%s+%s -> %s ; x64 +0x%X' % (sys.argv[1], o, p_, x))
        except ValueError as e: print('%s+%s -> ERRO %s' % (sys.argv[1], o, e))
