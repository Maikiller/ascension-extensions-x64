#!/usr/bin/env python3
"""Confere as linhas "0xADDR Funcao +0xOFF" e "0xADDR @0xRVA" do tools/extra-addr.txt contra o exe x64 atual.

Uso: python3 tools/chkretaddr.py [out/WowClient64.exe] [ext]
  - linha usada como RETORNO de call (o 12340 aparece junto de _ReturnAddress no ext, ou o comentario diz
    "retorno"): o endereco x64 tem de vir logo depois de um call (E8 rel32, FF /2 em qualquer forma).
    Mostra o alvo do call quando e E8 (nome pelo procs64.json).
  - linha com bytes esperados no comentario (", 83 FB 0A"): os bytes do exe tem de bater.
  - "+0xOFF" que passa do tamanho da funcao (procs64.json) = erro.
Rodar depois de cada rebuild do cliente (RVAs mudam)."""
import json, os, re, struct, sys
sys.path.insert(0, os.path.dirname(os.path.realpath(__file__)))
from relfields import rebase

T = os.path.dirname(os.path.abspath(__file__))
R = os.path.dirname(T)
exe = sys.argv[1] if len(sys.argv) > 1 else os.path.join(R, 'out', 'WowClient64.exe')
ext = sys.argv[2] if len(sys.argv) > 2 else os.environ.get('AUDIT_W', os.path.join(R, 'ext'))

data = open(exe, 'rb').read()
pe = struct.unpack_from('<I', data, 0x3C)[0]
nsec = struct.unpack_from('<H', data, pe + 6)[0]
optsz = struct.unpack_from('<H', data, pe + 20)[0]
secs = []
for i in range(nsec):
    o = pe + 24 + optsz + i * 40
    vsz, va, rsz, raw = struct.unpack_from('<IIII', data, o + 8)
    secs.append((va, max(vsz, rsz), raw))


def rd(rva, n):
    for va, sz, raw in secs:
        if va <= rva < va + sz:
            return data[raw + rva - va: raw + rva - va + n]
    return b''


procs = json.load(open(os.path.join(R, 'out', 'procs64.json')))
byname = {}
for rva, size, name in sorted(procs):
    byname.setdefault(name, []).append((rva, size))
starts = sorted((r[0], r[2]) for r in procs)


def name_at(rva):
    import bisect
    i = bisect.bisect_right(starts, (rva, '￿')) - 1
    return starts[i][1] if i >= 0 and starts[i][0] == rva else hex(rva)


def after_call(rva):
    """(True, alvo) se rva vem logo apos um call."""
    b = rd(rva - 7, 7)
    if len(b) < 7:
        return False, None
    if b[2] == 0xE8:
        tgt = rva + struct.unpack_from('<i', b, 3)[0]
        return True, name_at(tgt)
    # FF /2: FF D0-D7 (reg), FF 1x/5x xx/9x xxxxxxxx (mem), FF 15 rel32 (rip), com ou sem REX
    for back, ok in ((2, lambda m: 0xD0 <= m <= 0xD7 or (m & 0xF8) == 0x10 and m & 7 not in (4, 5)),
                     (3, lambda m: (m & 0xF8) == 0x50 or (m & 0xC7) == 0x14),
                     (4, lambda m: (m & 0xC7) == 0x54),
                     (6, lambda m: (m & 0xF8) == 0x90 or m == 0x15),
                     (7, lambda m: (m & 0xC7) == 0x94)):
        i = 7 - back
        if b[i] == 0xFF and ok(b[i + 1]):
            return True, 'indireto'
        if i >= 1 and b[i - 1] & 0xF0 == 0x40 and b[i] == 0xFF and ok(b[i + 1]):
            return True, 'indireto'
    return False, None


# enderecos 12340 usados como retorno no ext
used_ret = set()
for root, _, fs in os.walk(os.path.join(ext, 'src')):
    for f in fs:
        if not f.endswith(('.cpp', '.hpp', '.h')):
            continue
        for l in open(os.path.join(root, f), errors='ignore'):
            if '_ReturnAddress' in l or 'RETADDR' in l:
                for a in re.findall(r'0x([0-9A-Fa-f]{6})\b', l):
                    used_ret.add(int(a, 16))

rx = re.compile(r'^\s*(?:#\s*\(.*?\)\s*)?0x([0-9A-Fa-f]+)\s+(?:@0x([0-9A-Fa-f]+)|(.+?)\s+\+0x([0-9A-Fa-f]+))\s*(?:#\s*(.*))?$')
bad = 0
n = 0
for ln, line in enumerate(open(os.environ.get('EXTRA_ADDR') or os.path.join(T, 'extra-addr.txt')), 1):
    if line.lstrip().startswith('#'):
        continue
    if re.match(r'^\s*0x[0-9A-Fa-f]+\s+(vft|str):', line):
        continue   # resolvidas pelo nome no mapaddr (vftable do PDB / string unica); falha la, nao aqui
    m = rx.match(line.rstrip('\n'))
    if not m:
        continue
    n += 1
    a12, rvadirect, fn, off, comment = m.groups()
    a12 = int(a12, 16)
    comment = comment or ''
    if rvadirect:
        rva = int(rvadirect, 16)
        where = '@0x%X' % rva
    else:
        nth = re.match(r'(.*?)\s*\[(\d+)\]$', fn)
        key, k = (nth.group(1), int(nth.group(2))) if nth else (fn, None)
        cands = byname.get(key, [])
        if k is not None:
            cands = cands[k:k + 1]
        if len(cands) != 1:
            print('ERRO  %4d 0x%06X %s: funcao %s no exe' % (ln, a12, fn, 'ambigua (use "Nome [n]")' if cands else 'nao existe')); bad += 1; continue
        base, size = cands[0]
        o = int(off, 16)
        if o >= size:
            print('ERRO  %4d 0x%06X %s +0x%X: passa do fim (tamanho 0x%X)' % (ln, a12, fn, o, size)); bad += 1; continue
        rva = base + o
        where = '%s +0x%X' % (fn, o)
    probs = []
    is_ret = a12 in used_ret or re.search(r'retorno', comment, re.I)
    if is_ret:
        ok, tgt = after_call(rva)
        if not ok:
            probs.append('nao vem depois de call')
        else:
            comment = (comment + '  [call %s]' % tgt).strip()
    hx = re.search(r',\s*((?:[0-9A-Fa-f]{2}\s+)*[0-9A-Fa-f]{2})\s*$', comment.split('[call')[0].strip())
    if hx:
        want = bytes.fromhex(hx.group(1))
        have = rd(rva, len(want))
        if have != want and rebase(want, have)[0] != have:   # rel32/disp32 o DLL rebaseia sozinho
            probs.append('bytes %s != esperado %s' % (have.hex(' ').upper(), hx.group(1).upper()))
    if probs:
        bad += 1
        print('ERRO  %4d 0x%06X %s: %s' % (ln, a12, where, '; '.join(probs)))
    elif is_ret:
        print('ok    %4d 0x%06X %s  %s' % (ln, a12, where, comment[:90]))
print('linhas %d, problemas %d' % (n, bad))
