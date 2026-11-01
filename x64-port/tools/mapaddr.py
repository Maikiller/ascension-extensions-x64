#!/usr/bin/env python3
"""Gera a tabela endereco original (12340) -> RVA no WowClient64.exe.
Uso: mapaddr.py tags.json WowClient64.pdb WowClient64.exe saida.bin [relatorio.json]
- OFFSET: a marca em file:L aponta para a 1a funcao (bloco de linhas do PDB) que comeca em L+1..L+12 no mesmo arquivo.
- STATIC: nome da variavel declarada abaixo da marca, casado com S_GDATA32/S_LDATA32 do mesmo modulo (ou unico global).
Formato do .bin: 'WADR' u32 versao=1, u32 timestamp do PE, u32 n, depois n x (u32 addr32, u32 rva) ordenado."""
import sys, re, json, struct, subprocess, collections, bisect, os
import pefile
tags = json.load(open(sys.argv[1])); pdb = sys.argv[2]; exe = sys.argv[3]; outp = sys.argv[4]
rep = sys.argv[5] if len(sys.argv) > 5 else None
pe = pefile.PE(exe, fast_load=True)
secva = {i + 1: s.VirtualAddress for i, s in enumerate(pe.sections)}

def norm(p):
    # caminho do fonte no PDB: <unidade>:/build/<pasta do fonte>/... -> relativo a essa pasta
    p = p.replace('\\', '/'); m = re.search(r'/build/[^/]+/', p, re.I)
    return p[m.end():] if m else None

# --- blocos de linhas: (arquivo) -> lista de (primeira linha, rva)
blocks = collections.defaultdict(set)
cur = None
rxb = re.compile(r'^\s+([0-9A-F]{4}):([0-9A-F]{8})-([0-9A-F]{8}), line/addr entries')
rxl = re.compile(r'^\s+(\d+)\s+([0-9A-F]{8})')
pend = None
for line in subprocess.run(['llvm-pdbutil', 'dump', '-l', pdb], capture_output=True, text=True).stdout.split('\n'):
    if line and not line.startswith(' ') and '(SHA-256' in line or (line.startswith(('A:', 'B:', 'C:', 'D:', 'E:', 'H:')) and '\\' in line):
        cur = norm(line.split(' (')[0].strip()); pend = None; continue
    m = rxb.match(line)
    if m: pend = (int(m.group(1), 16), int(m.group(2), 16)); continue
    if pend and cur:
        m = rxl.match(line)
        if m:
            blocks[cur].add((int(m.group(1)), secva[pend[0]] + pend[1])); pend = None
idx = {f: sorted(v) for f, v in blocks.items()}

# --- dados
data = collections.defaultdict(list)  # nome -> [(modulo stem, rva)]
mod = None
rxm = re.compile(r'^\s*Mod \d+ \| `([^`]*)`')
rxd = re.compile(r'S_([GL])DATA32 \[size = \d+\] `([^`]+)`|S_([GL])PROC32 \[size = \d+\] `([^`]+)`')
procs = collections.defaultdict(set)   # nome do procedimento -> rvas (para tools/extra-addr.txt)
rxa = re.compile(r'addr = ([0-9A-F]{4}):(\d+)')
lastname = None
for line in subprocess.run(['llvm-pdbutil', 'dump', '--symbols', '--globals', pdb], capture_output=True, text=True).stdout.split('\n'):
    m = rxm.match(line)
    if m: mod = os.path.splitext(os.path.basename(m.group(1).replace('\\', '/')))[0].lower(); continue
    m = rxd.search(line)
    if m:
        if m.group(2): lastname = m.group(2); kind = m.group(1); isproc = False
        else: lastname = m.group(4); kind = m.group(3); isproc = True
        continue
    if lastname:
        m = rxa.search(line)
        if m and int(m.group(1), 16) in secva:
            rva = secva[int(m.group(1), 16)] + int(m.group(2))
            if isproc: procs[lastname].add(rva)
            else: data[lastname].append((mod, rva, kind))
        lastname = None

res = {}; why = collections.Counter(); miss = []
for t in tags:
    a = t['addr']
    if t['kind'] == 'OFFSET':
        lst = idx.get(t['file'])
        if not lst: why['sem_codigo_no_arquivo'] += 1; miss.append(t); continue
        i = bisect.bisect_right(lst, (t['line'], 1 << 40))
        cand = [x for x in lst[i:i + 6] if x[0] <= t['line'] + 12]
        if not cand: why['inline_ou_removida'] += 1; miss.append(t); continue
        first = cand[0][0]; same = {x[1] for x in cand if x[0] == first}
        if len(same) > 1: why['ambigua_template'] += 1; miss.append(t); continue
        res.setdefault(a, same.pop()); why['ok_codigo'] += 1
    else:
        m = re.search(r'\(\s*\*\s*([A-Za-z_][A-Za-z0-9_:]*)\s*\)', t['decl'])   # ponteiro de funcao: void (*nome)(...)
        if not m:
            m = re.search(r'([A-Za-z_][A-Za-z0-9_:]*)\s*(\[[^\]]*\])*\s*(=|;|\(|\{)', t['decl'])
        name = m.group(1) if m else None
        cands = data.get(name, []) if name else []
        stem = os.path.splitext(os.path.basename(t['file']))[0].lower()
        c2 = [r for mm, r, _ in cands if mm == stem]
        if not c2 and len({r for _, r, _ in cands}) == 1: c2 = [cands[0][1]]
        if not c2 and not re.match(r'\s*static\b', t['decl']):   # nao-static: o simbolo global (S_GDATA32) e o dele
            g = {r for _, r, k in cands if k == 'G'}
            if len(g) == 1: c2 = [g.pop()]
        if not c2: why['dado_nao_achado'] += 1; miss.append(t); continue
        res.setdefault(a, c2[0]); why['ok_dado'] += 1
# Enderecos do 12340 sem marca OFFSET no fonte (instancias de template, funcoes de biblioteca): tools/extra-addr.txt,
# linhas '0xADDR Nome::Exato' (nome do simbolo no PDB x64; '+0xN' no fim soma um deslocamento).
extra = os.environ.get('EXTRA_ADDR') or os.path.join(os.path.dirname(os.path.abspath(__file__)), 'extra-addr.txt')
_vft = None
def vftables():
    global _vft
    if _vft is None:
        _vft = {}; nm = None
        rxp = re.compile(r'S_PUB32 \[size = \d+\] `(\?\?_7[^`]+)`')
        for l in subprocess.run(['llvm-pdbutil', 'dump', '--publics', pdb], capture_output=True, text=True).stdout.split('\n'):
            m = rxp.search(l)
            if m: nm = m.group(1); continue
            if nm:
                m = rxa.search(l)
                if m and int(m.group(1), 16) in secva: _vft[nm] = secva[int(m.group(1), 16)] + int(m.group(2))
                nm = None
    return _vft
_img = None
def find_string(s):
    global _img
    if _img is None: _img = pe.get_memory_mapped_image()
    out = []; i = 0; needle = b'\0' + s + b'\0'
    while True:
        i = _img.find(needle, i)
        if i < 0: return out
        out.append(i + 1); i += 1
if os.path.exists(extra):
    for line in open(extra):
        line = line.split('#')[0].strip()
        if not line: continue
        a, name = line.split(None, 1); add = 0
        # 'vft:Classe +0xN' = vftable do PDB (??_7Classe@@6B@) + deslocamento; 'str:"texto"' = string unica do exe
        # (terminada em NUL). Os dois sobrevivem a rebuild do cliente, ao contrario do '@rva'.
        mm = re.match(r'vft:\s*(\S+)\s*\+\s*(0x[0-9A-Fa-f]+)$', name)
        if mm:
            r = vftables().get('??_7%s@@6B@' % mm.group(1))
            if r is not None: res[int(a, 16)] = r + int(mm.group(2), 16); why['ok_extra_vft'] += 1
            else: why['extra_falhou'] += 1; print('extra-addr: %s vftable %s nao achada' % (a, mm.group(1)))
            continue
        mm = re.match(r'str:\s*"(.*)"$', name)
        if mm:
            r = find_string(mm.group(1).encode('latin-1'))
            if len(r) == 1: res[int(a, 16)] = r[0]; why['ok_extra_str'] += 1
            else: why['extra_falhou'] += 1; print('extra-addr: %s string %r -> %d ocorrencias' % (a, mm.group(1), len(r)))
            continue
        mm = re.match(r'@\s*(0x[0-9A-Fa-f]+)$', name)   # RVA x64 direto (conferido no disasm)
        if mm: res[int(a, 16)] = int(mm.group(1), 16); why['ok_extra_rva'] += 1; continue
        mm = re.match(r'=\s*(0x[0-9A-Fa-f]+)\s*\+\s*(0x[0-9A-Fa-f]+)$', name)   # campo de dado: base 12340 + offset x64
        if mm:
            b = int(mm.group(1), 16)
            if b in res: res[int(a, 16)] = res[b] + int(mm.group(2), 16); why['ok_extra_campo'] += 1
            else: why['extra_falhou'] += 1; print('extra-addr: base %s nao mapeada' % mm.group(1))
            continue
        mm = re.match(r'(.*?)\s*\+\s*(0x[0-9A-Fa-f]+)$', name)
        if mm: name, add = mm.group(1), int(mm.group(2), 16)
        nth = None
        mm = re.match(r'(.*?)\s*\[(\d+)\]$', name)   # 'Nome [n]': n-esima funcao com esse nome, por RVA (sobrecargas)
        if mm: name, nth = mm.group(1), int(mm.group(2))
        r = procs.get(name)
        if r and nth is not None and nth < len(r): r = {sorted(r)[nth]}
        if r and len(r) == 1: res[int(a, 16)] = next(iter(r)) + add; why['ok_extra'] += 1
        else: why['extra_falhou'] += 1; print('extra-addr: %s %s -> %s' % (a, name, sorted(r) if r else 'nao achado'))
ts = pe.FILE_HEADER.TimeDateStamp
with open(outp, 'wb') as f:
    f.write(b'WADR' + struct.pack('<III', 1, ts, len(res)))
    for a in sorted(res): f.write(struct.pack('<II', a, res[a]))
print(dict(why), 'total na tabela', len(res))
if rep: json.dump({'missing': miss}, open(rep, 'w'))
