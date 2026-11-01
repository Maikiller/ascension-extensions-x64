#!/usr/bin/env python3
"""Bateria de testes do CoA x64: puxa as rodadas do PC, simboliza, agrupa por assinatura e separa por sessao.

  python3 tools/bateria.py coleta     # scp das rodadas novas de <jogo>\\Bateria\\ -> out/bateria/runs/
  python3 tools/bateria.py triagem    # out/bateria/RELATORIO.md + out/bateria/para-<sessao>.md
  python3 tools/bateria.py tudo

Fontes por rodada (tools/bateria.ps1 junta no PC):
  Crashes/bateria_*.log  (AscBateria: INICIO / EXC / SAIDA / TRAVADO / VIVO, ja com modulo+rva)
  **/*.dmp               (Crash.dmp do cliente, procdump -e, WER, Saida_/Travado_ do AscBateria)
  Logs/Extensions.log, Logs/Fatal.txt, Logs/LUA.txt, nota.txt, run.json
Simbolos: o exe pelo procs64.json do build com o mesmo timestamp (out/ ou out/cliente-*/); a DLL pelo pdb
com o mesmo timestamp (out/ ou baixado do PC), via llvm-symbolizer. Cache em out/bateria/sym/.
Dono: o 1o quadro dentro do Extensions64.dll com arquivo-fonte -> tools/bateria_donos.txt; so exe -> cliente.
"""
import collections, glob, json, os, re, struct, subprocess, sys

W = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(W, 'out', 'bateria')
RUNS = os.path.join(OUT, 'runs')
SYM = os.path.join(OUT, 'sym')
# Maquina de teste: PC_HOST, PC_PORT, PC_DIR e SSH_KEY vem do ambiente ou de x64-port/ci/local.env (fora do git).
def _local_env():
    e = {}
    p = os.path.join(os.path.dirname(os.path.dirname(os.path.realpath(__file__))), 'ci', 'local.env')
    if os.path.exists(p):
        for l in open(p):
            l = l.strip()
            if l and not l.startswith('#') and '=' in l:
                k, v = l.split('=', 1); e[k.strip()] = v.strip().strip("'\"")
    e.update({k: v for k, v in os.environ.items() if k in ('PC_HOST', 'PC_PORT', 'PC_DIR', 'SSH_KEY')})
    return e
_E = _local_env()
PC = ['-P', _E.get('PC_PORT', '22'), '-i', os.path.expanduser(_E.get('SSH_KEY', '~/.ssh/id_ed25519')), '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=15']
PC_HOST = _E.get('PC_HOST', '')
PC_JOGO = _E.get('PC_DIR', '')
EXE, DLL = 'WowClient64.exe', 'Extensions64.dll'

# ---------------------------------------------------------------- coleta

def ssh(cmd, timeout=60):
    # ssh usa -p (o -P de PC e do scp; no ssh -P e 'tag' e caia na porta 22 em silencio -> listagem vazia)
    return subprocess.run(['ssh'] + ['-p' if a == '-P' else a for a in PC] + [PC_HOST, cmd], capture_output=True, text=True, timeout=timeout).stdout

def scp(remote, local, timeout=1800):
    return subprocess.run(['scp', '-r', '-q'] + PC + [f'{PC_HOST}:{remote}', local], timeout=timeout).returncode == 0

def coleta():
    os.makedirs(RUNS, exist_ok=True)
    lst = ssh(f'cmd /c dir /b /ad "{PC_JOGO.replace("/", chr(92))}\\Bateria"').split()
    novas = [d for d in lst if re.fullmatch(r'\d{8}-\d{6}', d) and not os.path.exists(os.path.join(RUNS, d, 'run.json'))]
    for d in novas:
        print('coleta', d)
        scp(f'{PC_JOGO}/Bateria/{d}', RUNS)
    soltas = coleta_soltas()
    print(f'{len(novas)} rodada(s) nova(s), {soltas} fora do script; total {len(os.listdir(RUNS))}')

def coleta_soltas():
    """Rodadas que nao ficaram em Bateria\\ (jogo aberto sem o bateria.ps1, ou o .ps1 nao juntou): cada
    Crashes\\bateria_<pid>_<AAAAMMDD-HHMMSS>.log que nao esta em nenhuma rodada vira runs/<AAAAMMDD-HHMMSS>/ com
    o log, os dumps dele (Crashes\\*_<pid>_*.dmp) e os Errors\\<data hora> Crash.txt/.dmp do intervalo dele (ate o
    proximo INICIO). Logs\\ nao: e sobrescrito pela sessao seguinte. run.json marca fora_do_script."""
    win = PC_JOGO.replace('/', chr(92))
    crashes = ssh(f'cmd /c dir /b "{win}\\Crashes"').split()
    errors = [l.strip() for l in ssh(f'cmd /c dir /b "{win}\\Errors"').splitlines() if l.strip()]
    ja = {os.path.basename(p) for p in glob.glob(os.path.join(RUNS, '**', 'bateria_*.log'), recursive=True)}
    logs = []
    for f in crashes:
        m = re.fullmatch(r'bateria_(\d+)_(\d{8})-(\d{6})\.log', f)
        if m: logs.append((m.group(2) + m.group(3), m.group(1), f))
    logs.sort()
    def err_stamp(n):
        m = re.match(r'(\d{4})-(\d{2})-(\d{2}) (\d{2})\.(\d{2})\.(\d{2}) ', n)
        return ''.join(m.groups()) if m else None
    n = 0
    for i, (st, pid, f) in enumerate(logs):
        if f in ja:
            continue
        fim = logs[i + 1][0] if i + 1 < len(logs) else '99999999999999'
        rid = f'{st[:8]}-{st[8:]}'
        d = os.path.join(RUNS, rid)
        if os.path.exists(os.path.join(d, 'run.json')) and glob.glob(os.path.join(d, '**', 'bateria_*.log'), recursive=True):
            continue   # ja existe rodada com esse id (veio de Bateria\ com outro log)
        print('coleta (fora do script)', rid)
        os.makedirs(os.path.join(d, 'Crashes'), exist_ok=True)
        os.makedirs(os.path.join(d, 'Errors'), exist_ok=True)
        scp(f'{PC_JOGO}/Crashes/{f}', os.path.join(d, 'Crashes'))
        for c in crashes:
            if c.lower().endswith('.dmp') and f'_{pid}_' in c:
                scp(f'{PC_JOGO}/Crashes/{c}', os.path.join(d, 'Crashes'))
        for e in errors:
            s = err_stamp(e)
            if s and st <= s < fim and re.search(r' Crash\.(txt|dmp)$', e):
                scp(f'{PC_JOGO}/Errors/{e}', os.path.join(d, 'Errors'))
        with open(os.path.join(d, 'run.json'), 'w') as j:
            json.dump({'segundos': '?', 'fora_do_script': True}, j)
        n += 1
    return n

# ---------------------------------------------------------------- simbolos

def pe_ts(path):
    with open(path, 'rb') as f:
        d = f.read(4096)
    e = struct.unpack_from('<I', d, 0x3C)[0]
    return struct.unpack_from('<I', d, e + 8)[0]

class ExeSym:
    def __init__(self, d):
        p = sorted(json.load(open(os.path.join(d, 'procs64.json'))))
        self.starts = [x[0] for x in p]; self.p = p
        self.x86 = {}
        a = os.path.join(d, 'WowClient64.addr')
        if os.path.exists(a):
            b = open(a, 'rb').read(); n = struct.unpack_from('<I', b, 12)[0]
            for i in range(n):
                x, r = struct.unpack_from('<II', b, 16 + 8 * i)
                self.x86.setdefault(r, x)
        self.img = open(os.path.join(d, EXE), 'rb').read() if os.path.exists(os.path.join(d, EXE)) else None
    def name(self, rva):
        import bisect
        i = bisect.bisect_right(self.starts, rva) - 1
        if i < 0: return '?'
        s, size, n = self.p[i]
        if size and rva >= s + size + 0x40: return f'?+0x{rva:X}'
        x = self.x86.get(s)
        return f'{n}+0x{rva - s:X}' + (f' [x86 0x{x:X}]' if x else '')

def link_or_copy(src, d):
    if subprocess.run(['cp', '-l', src, d], capture_output=True).returncode:
        subprocess.run(['cp', src, d])

def find_exe_dir(ts):
    c = os.path.join(SYM, f'exe-{ts:08X}')
    if os.path.exists(os.path.join(c, 'procs64.json')): return c
    for d in [os.path.join(W, 'out')] + sorted(glob.glob(os.path.join(W, 'out', 'cliente-*'))):
        e = os.path.join(d, EXE)
        if os.path.exists(e) and os.path.exists(os.path.join(d, 'procs64.json')) and pe_ts(e) == ts:
            os.makedirs(c, exist_ok=True)
            for f in (EXE, 'procs64.json', 'WowClient64.addr'):
                if os.path.exists(os.path.join(d, f)) and not os.path.exists(os.path.join(c, f)):
                    link_or_copy(os.path.join(d, f), c)
            return c
    return None

def find_dll_dir(ts):
    c = os.path.join(SYM, f'dll-{ts:08X}')
    if os.path.exists(os.path.join(c, DLL)): return c
    os.makedirs(c, exist_ok=True)
    o = os.path.join(W, 'out', DLL)
    if os.path.exists(o) and pe_ts(o) == ts:
        for f in (DLL, 'Extensions64.pdb'):
            if os.path.exists(os.path.join(W, 'out', f)): link_or_copy(os.path.join(W, 'out', f), c)
        return c
    # builds arquivados pela c1: out/Extensions64-<ext>.dll + .pdb
    for dll in sorted(glob.glob(os.path.join(W, 'out', 'Extensions64-*.dll'))):
        pdb = dll[:-4] + '.pdb'
        if os.path.exists(pdb) and pe_ts(dll) == ts:
            subprocess.run(['cp', dll, os.path.join(c, DLL)]); subprocess.run(['cp', pdb, os.path.join(c, 'Extensions64.pdb')])
            return c
    # ainda no PC?
    if os.path.exists(os.path.join(c, 'NAO_ACHADO')): return None
    tmp = os.path.join(c, 'pc'); os.makedirs(tmp, exist_ok=True)
    if scp(f'{PC_JOGO}/{DLL}', tmp) and pe_ts(os.path.join(tmp, DLL)) == ts:
        scp(f'{PC_JOGO}/Extensions64.pdb', tmp)
        for f in os.listdir(tmp): os.replace(os.path.join(tmp, f), os.path.join(c, f))
        return c
    open(os.path.join(c, 'NAO_ACHADO'), 'w').write('DLL deste timestamp nao esta em out/ nem no PC: copie Extensions64.dll+pdb para ca\n')
    return None

class DllSym:
    def __init__(self, d):
        self.d = d; self.cache = {}
    def resolve(self, rvas):
        rvas = [r for r in set(rvas) if r not in self.cache]
        if not rvas or not self.d: return
        inp = '\n'.join(hex(0x180000000 + r) for r in rvas) + '\n'
        out = subprocess.run(['llvm-symbolizer', '--obj=' + os.path.join(self.d, DLL), '--no-inlines'], input=inp,
                             capture_output=True, text=True, timeout=600).stdout.strip().split('\n\n')
        for r, blk in zip(rvas, out):
            ln = blk.strip().split('\n')
            fn = ln[0] if ln else '?'
            src = ln[1] if len(ln) > 1 else ''
            m = re.search(r'([A-Za-z0-9_]+)\.(?:cpp|hpp|inc|h):(\d+)', src)
            self.cache[r] = (fn.replace("`anonymous namespace'::", ''), (m.group(1), int(m.group(2))) if m else None)
    def get(self, rva):
        return self.cache.get(rva, ('?', None))

# ---------------------------------------------------------------- minidump (leitor minimo)

class Dump:
    def __init__(self, path):
        self.b = b = open(path, 'rb').read()
        sig, ver, n, rva = struct.unpack_from('<4sIII', b, 0)
        self.streams = {}
        for i in range(n):
            t, size, r = struct.unpack_from('<III', b, rva + 12 * i)
            self.streams.setdefault(t, (r, size))
        self.mods = []
        if 4 in self.streams:
            r, _ = self.streams[4]; c = struct.unpack_from('<I', b, r)[0]
            for i in range(c):
                o = r + 4 + 108 * i
                base, size, _, ts, nr = struct.unpack_from('<QIIII', b, o)
                ln = struct.unpack_from('<I', b, nr)[0]
                name = b[nr + 4:nr + 4 + ln].decode('utf-16le', 'replace').split('\\')[-1]
                self.mods.append((base, size, ts, name))
        self.mem = []   # (start, size, fileoff)
        if 9 in self.streams:
            r, _ = self.streams[9]; c, off = struct.unpack_from('<QQ', b, r)
            for i in range(c):
                s, sz = struct.unpack_from('<QQ', b, r + 16 + 16 * i); self.mem.append((s, sz, off)); off += sz
        if 5 in self.streams:
            r, _ = self.streams[5]; c = struct.unpack_from('<I', b, r)[0]
            for i in range(c):
                s, sz, fo = struct.unpack_from('<QII', b, r + 4 + 16 * i); self.mem.append((s, sz, fo))
        self.threads = {}
        if 3 in self.streams:
            r, _ = self.streams[3]; c = struct.unpack_from('<I', b, r)[0]
            for i in range(c):
                tid, _, _, _, teb, ss, sz, srva, csz, crva = struct.unpack_from('<IIIIQQIIII', b, r + 4 + 48 * i)
                self.threads[tid] = (ss, sz, srva, crva)
        self.exc = None
        if 6 in self.streams:
            r, _ = self.streams[6]
            tid, = struct.unpack_from('<I', b, r)
            code, flags, _, addr, np = struct.unpack_from('<IIQQI', b, r + 8)
            info = struct.unpack_from('<15Q', b, r + 8 + 32)
            csz, crva = struct.unpack_from('<II', b, r + 8 + 152)
            self.exc = dict(tid=tid, code=code, addr=addr, info=info[:np], ctx=crva)
    def read(self, a, n):
        for s, sz, fo in self.mem:
            if s <= a < s + sz:
                k = min(n, s + sz - a); return self.b[fo + a - s: fo + a - s + k]
        return b''
    def ctx(self, crva):
        return struct.unpack_from('<Q', self.b, crva + 0x98)[0], struct.unpack_from('<Q', self.b, crva + 0xF8)[0]   # rsp, rip
    def mod_of(self, a):
        for base, size, ts, name in self.mods:
            if base <= a < base + size: return base, ts, name
        return None

class Images:
    """bytes por rva dos modulos locais (para filtrar a varredura da pilha por 'call' antes do retorno)."""
    def __init__(self): self.c = {}
    def get(self, path):
        if path not in self.c:
            try:
                import pefile
                pe = pefile.PE(path, fast_load=True)
                self.c[path] = pe
            except Exception:
                self.c[path] = None
        return self.c[path]
    def after_call(self, path, rva):
        pe = self.get(path)
        if pe is None: return True
        try: d = pe.get_data(rva - 7, 7)
        except Exception: return False
        if len(d) < 7: return False
        if d[2] == 0xE8: return True                                        # call rel32
        if d[5] == 0xFF and (d[6] & 0x38) == 0x10: return True              # call r/m (2)
        if d[4] == 0xFF and (d[5] & 0x38) == 0x10: return True              # (3)
        if d[1] == 0xFF and (d[2] & 0x38) == 0x10: return True              # call [rip+d32] (6)
        if d[0] in (0x41, 0x48, 0x49) and d[1] == 0xFF and (d[2] & 0x38) == 0x10: return True
        if d[3] == 0xFF and (d[4] & 0x38) == 0x10: return True              # (4) [reg+d8]
        if d[0] == 0xFF and (d[1] & 0x38) == 0x10: return True              # (7) [reg+d32]
        return False

IMG = Images()

class Unwinder:
    """desenrolar x64 pelo .pdata/UNWIND_INFO do arquivo local do modulo (exe/DLL do mesmo build)."""
    def __init__(self, path):
        import pefile, bisect
        self.pe = pe = pefile.PE(path, fast_load=True)
        d = pe.OPTIONAL_HEADER.DATA_DIRECTORY[3]
        raw = pe.get_data(d.VirtualAddress, d.Size)
        self.rf = sorted(struct.unpack_from('<III', raw, i) for i in range(0, len(raw) - 11, 12))
        self.begins = [x[0] for x in self.rf]
        self.bisect = bisect
    def lookup(self, rva):
        i = self.bisect.bisect_right(self.begins, rva) - 1
        if i >= 0 and self.rf[i][0] <= rva < self.rf[i][1]: return self.rf[i]
        return None
    def step(self, rva, regs, read):
        """regs: dict 0..15 (rax..r15, 4=rsp) -> nova rip ou None. Modifica regs."""
        f = self.lookup(rva)
        if not f:
            regs['rip'] = read(regs[4]); regs[4] += 8; return regs['rip']
        off = rva - f[0]; first = True
        while True:
            ui = f[2] & ~1 if False else f[2]
            hdr = self.pe.get_data(ui, 4)
            flags, prolog, count, fr = hdr[0] >> 3, hdr[1], hdr[2], hdr[3]
            codes = self.pe.get_data(ui + 4, count * 2 + 12)
            i = 0
            while i < count:
                co, op = codes[2 * i], codes[2 * i + 1]
                kind, info = op & 15, op >> 4
                n = {0: 1, 1: 2 if info == 0 else 3, 2: 1, 3: 1, 4: 2, 5: 3, 8: 2, 9: 3, 10: 1}.get(kind, 1)
                apply = (not first) or co <= off
                if apply:
                    if kind == 0: regs[info] = read(regs[4]); regs[4] += 8
                    elif kind == 1:
                        regs[4] += struct.unpack_from('<H', codes, 2 * i + 2)[0] * 8 if info == 0 else struct.unpack_from('<I', codes, 2 * i + 2)[0]
                    elif kind == 2: regs[4] += info * 8 + 8
                    elif kind == 3: regs[4] = regs[fr & 15] - (fr >> 4) * 16
                    elif kind == 4: regs[info] = read(regs[4] + struct.unpack_from('<H', codes, 2 * i + 2)[0] * 8)
                    elif kind == 5: regs[info] = read(regs[4] + struct.unpack_from('<I', codes, 2 * i + 2)[0])
                    elif kind == 10:
                        b = regs[4] + (8 if info else 0)
                        regs['rip'] = read(b); regs[4] = read(b + 24); return regs['rip']
                i += n
            if flags & 4:
                k = count + (count & 1)
                f = struct.unpack_from('<III', codes, 2 * k) if len(codes) >= 2 * k + 12 else None
                if f is None: break
                first = False
                continue
            break
        regs['rip'] = read(regs[4]); regs[4] += 8
        return regs['rip']

UNW = {}
def unwinder(path):
    if path not in UNW:
        try: UNW[path] = Unwinder(path)
        except Exception: UNW[path] = None
    return UNW[path]


# ---------------------------------------------------------------- analise

# EXC na thread principal que param o processo (o vigia acusaria TRAVADO enquanto o procdump grava)
FATAIS_TRAVA = (0xC0000005, 0xC0000374, 0xC0000409, 0xC00000FD, 0xC000001D, 0xC0000096)
CODES = {0xC0000005: 'ACCESS_VIOLATION', 0xC0000409: 'FASTFAIL//GS', 0xC0000374: 'HEAP_CORRUPTION',
         0xC00000FD: 'STACK_OVERFLOW', 0xC000001D: 'ILLEGAL_INSTRUCTION', 0xC0000094: 'INT_DIV0',
         0xC0000420: 'ASSERT', 0xC0000417: 'CRT_INVALID_PARAM', 0x80000003: 'BREAKPOINT', 0xE06D7363: 'C++_EXCEPTION'}
FASTFAIL = {2: 'STACK_COOKIE(/GS)', 3: 'CORRUPT_LIST_ENTRY', 4: 'STACK_COOKIE_CHECK', 5: 'INVALID_ARG', 7: 'FATAL_APP_EXIT',
            10: 'INVALID_SET_OF_CONTEXT', 13: 'GUARD_ICALL_CHECK_FAILURE', 14: 'INVALID_BUFFER_ACCESS',
            23: 'GS_COOKIE_INIT', 38: 'HEAP_METADATA_CORRUPTION'}

def load_owners():
    m = {}
    p = os.path.join(W, 'tools', 'bateria_donos.txt')
    for ln in open(p, encoding='utf-8'):
        ln = ln.split('#')[0].strip()
        if ':' in ln:
            mods, who = ln.split(':', 1)
            for x in mods.split(): m[x.strip()] = who.strip()
    return m

OWN = None
def owner(src):
    global OWN
    if OWN is None: OWN = load_owners()
    if not src: return OWN.get('*cliente', 'operario699-c1')
    stem = src[0]
    for k in (stem, stem.replace('.generated', '')):
        if k in OWN: return OWN[k]
    return OWN.get('*', 'operario-mestre')

class Run:
    def __init__(self, d):
        self.d = d; self.id = os.path.basename(d)
        self.info = json.load(open(os.path.join(d, 'run.json'), encoding='utf-8-sig')) if os.path.exists(os.path.join(d, 'run.json')) else {}
        self.nota = open(os.path.join(d, 'nota.txt'), encoding='utf-8-sig', errors='replace').read().strip() if os.path.exists(os.path.join(d, 'nota.txt')) else ''
        self.events = []      # dict(kind, code, frames=[(mod,rva)], text, file)
        self.bat = sorted(glob.glob(os.path.join(d, '**', 'bateria_*.log'), recursive=True))
        self.exe_ts = self.dll_ts = None
        self.vivo = None; self.saida = None; self.excs = []; self.solucos = []
        for p in self.bat: self.parse_bat(p)
        self.dumps = sorted(glob.glob(os.path.join(d, '**', '*.dmp'), recursive=True))
        self.extlog = self.first('Logs/Extensions.log')
    def first(self, rel):
        g = glob.glob(os.path.join(self.d, rel)) + glob.glob(os.path.join(self.d, '**', os.path.basename(rel)), recursive=True)
        return g[0] if g else None
    def parse_bat(self, p):
        cur = None
        if not hasattr(self, 'pos_crash'): self.pos_crash = []
        for ln in open(p, encoding='utf-8', errors='replace'):
            ln = ln.rstrip()
            m = re.search(r'\] (INICIO|EXC|SAIDA|TRAVADO|RECUPEROU|VIVO|SOLUCO)\b(.*)', ln)
            if m:
                k, rest = m.groups()
                if k == 'INICIO':
                    t = re.findall(r'ts ([0-9A-F]{8})', rest)
                    if len(t) == 2: self.exe_ts, self.dll_ts = int(t[0], 16), int(t[1], 16)
                    cur = None; continue
                if k == 'VIVO': self.vivo = rest.strip(); cur = None; continue
                if k == 'SOLUCO':
                    ms = re.match(r'\s*(\d+) ms', rest)
                    if not ms: cur = None; continue
                    cur = dict(kind='SOLUCO', ms=int(ms.group(1)), frames=[], file=p, text=('SOLUCO' + rest).strip())
                    self.solucos.append(cur); continue
                if k == 'RECUPEROU': cur = None; continue
                cur = dict(kind=k, text=(k + rest).strip(), frames=[], file=p)
                hms = re.match(r'(\d\d):(\d\d):(\d\d)(?:\.(\d+))?', ln)
                cur['t'] = (int(hms.group(1)) * 3600 + int(hms.group(2)) * 60 + int(hms.group(3))
                            + float('0.' + (hms.group(4) or '0'))) if hms else None
                if k == 'TRAVADO' and cur['t'] is not None:
                    # TRAVADO logo depois de EXC fatal na thread principal = processo parado pelo procdump -ma / crash
                    # handler (D15 01/11: AV 01:45:43.8, dump de 4,6 GB em 26,7 s, TRAVADO 01:46:10.6). Nao e defeito.
                    sec = re.match(r':\s*(\d+) s', rest)
                    ini = cur['t'] - (int(sec.group(1)) if sec else 0) - 5
                    if any(e.get('t') is not None and '[principal]' in e['text'] and e.get('code') in FATAIS_TRAVA
                           and ini <= e['t'] <= cur['t'] for e in self.excs):
                        cur = dict(cur, kind='TRAVADO_POS_CRASH'); self.pos_crash.append(cur); continue
                if k == 'EXC':
                    c = re.search(r'EXC ([0-9A-F]{8})', ln); cur['code'] = int(c.group(1), 16) if c else 0
                    self.excs.append(cur)
                elif k == 'SAIDA':
                    c = re.search(r'codigo (\d+)', rest); cur['code'] = int(c.group(1)) if c else None
                    self.saida = cur
                else:
                    cur['code'] = None; self.events.append(cur)
                continue
            f = re.search(r'  (?:exc|saida|travado|soluco) (?:#\d+|rip) (\S+)\+0x([0-9A-F]+)', ln)
            if f and cur is not None:
                cur['frames'].append((f.group(1), int(f.group(2), 16)))

    def local(self, name):
        if name.lower() == EXE.lower() and self.exe_ts:
            d = find_exe_dir(self.exe_ts); return d and os.path.join(d, EXE)
        if name.lower() == DLL.lower() and self.dll_ts:
            d = find_dll_dir(self.dll_ts); return d and os.path.exists(os.path.join(d, DLL)) and os.path.join(d, DLL)
        return None

    def analyse_dump(self, p):
        try: dm = Dump(p)
        except Exception as e: return dict(kind='DUMP', text=f'dump ilegivel: {e}', frames=[], file=p, code=None)
        for base, size, ts, name in dm.mods:
            if name.lower() == EXE.lower() and not self.exe_ts: self.exe_ts = ts
            if name.lower() == DLL.lower() and not self.dll_ts: self.dll_ts = ts
        ev = dict(kind='DUMP', file=p, frames=[], code=None, text='')
        if dm.exc:
            code = dm.exc['code']; ev['code'] = code
            nm = CODES.get(code, '')
            if code == 0xC0000409 and dm.exc['info']:
                nm += ' ' + FASTFAIL.get(dm.exc['info'][0], f'codigo {dm.exc["info"][0]}')
            ev['text'] = f'{code:08X} {nm}'.strip()
            if code == 0xC0000005 and len(dm.exc['info']) >= 2:
                ev['text'] += f' ({["leitura","escrita"][dm.exc["info"][0] & 1] if dm.exc["info"][0] < 2 else "execucao"} de 0x{dm.exc["info"][1]:X})'
            rsp, rip = dm.ctx(dm.exc['ctx'])
            tid = dm.exc['tid']
        else:
            ev['text'] = 'dump sem excecao (Saida/Travado/manual)'
            return ev
        frames = self.walk(dm, dm.exc['ctx'])
        ev['scan'] = True
        ev['frames'] = frames
        return ev

    def walk(self, dm, crva, maxv=30):
        """pilha de um contexto do dump: desenrola pelo .pdata enquanto exe/DLL, depois varre."""
        rsp, rip = dm.ctx(crva)
        frames = []
        m = dm.mod_of(rip)
        frames.append((m[2], rip - m[0]) if m else ('?', rip))
        # 1) desenrolar de verdade enquanto o quadro estiver no exe/DLL (temos o .pdata local)
        regs = {i: struct.unpack_from('<Q', dm.b, crva + 0x78 + 8 * i)[0] for i in range(16)}
        def rd(a):
            b8 = dm.read(a, 8)
            if len(b8) < 8: raise ValueError
            return struct.unpack('<Q', b8)[0]
        cur = rip
        try:
            for _ in range(40):
                mm = dm.mod_of(cur)
                if not mm or mm[2].lower() not in (EXE.lower(), DLL.lower()): break
                local = self.local(mm[2])
                u = local and unwinder(local)
                if not u: break
                cur = u.step(cur - mm[0], regs, rd)
                if not cur: break
                m2 = dm.mod_of(cur)
                frames.append((m2[2], cur - m2[0]) if m2 else ('?', cur))
        except ValueError:
            pass
        # 2) daqui em diante (modulo do sistema, sem .pdata ou pilha sobrescrita) por varredura
        if frames and dm.mod_of(cur) is None:
            frames.pop()   # o 'retorno' lixo que parou o desenrolar
        frames.append(('--varredura--', 0))
        rsp = regs[4]
        st = dm.read(rsp, 0x6000)
        for i in range(0, len(st) - 7, 8):
            v = struct.unpack_from('<Q', st, i)[0]
            mm = dm.mod_of(v)
            if not mm or mm[2].lower() not in (EXE.lower(), DLL.lower()): continue
            rva = v - mm[0]
            local = self.local(mm[2])
            if local and not IMG.after_call(local, rva): continue
            frames.append((mm[2], rva))
            if len(frames) >= maxv: break
        if frames[-1][0] == '--varredura--': frames.pop()
        return frames

    def hang_dump(self, p):
        """Travado_*.dmp (procdump -ma disparado pelo vigia): pilha da thread principal + resumo das outras."""
        try: dm = Dump(p)
        except Exception as e: return None
        main = None
        m = re.search(r'TRAVADO[^\n]*\(t(\d+)\)', open(self.bat[0], errors='replace').read()) if self.bat else None
        if m: main = int(m.group(1))
        out = []
        for tid, (ss, sz, srva, crva) in sorted(dm.threads.items(), key=lambda kv: kv[0] != main):
            try: fr = self.walk(dm, crva, 25)
            except Exception: continue
            out.append((tid, fr))
        return dict(kind='TRAVADO', file=p, code=None, frames=out[0][1] if out else [],
                    text=f'dump do travamento (procdump -ma), thread principal t{main}', threads=out)

    def classify(self):
        # cada dump analisado vira <dump>.ev.json; o 'enxuga' pode apagar o .dmp depois e a triagem segue igual
        caches = sorted(glob.glob(os.path.join(self.d, '**', '*.dmp.ev.json'), recursive=True))
        feitos = set()
        for c in caches:
            try: j = json.load(open(c))
            except Exception: continue
            feitos.add(c[:-len('.ev.json')])
            if j.get('exe_ts') and not self.exe_ts: self.exe_ts = j['exe_ts']
            if j.get('dll_ts') and not self.dll_ts: self.dll_ts = j['dll_ts']
            if j.get('ev'): self.events.append(j['ev'])
        for p in self.dumps:
            if p in feitos or os.path.basename(p).startswith('Saida_'): continue
            ev = self.hang_dump(p) if os.path.basename(p).startswith('Travado_') else self.analyse_dump(p)
            if ev: self.events.append(ev)
            if ev is None or 'ilegivel' not in ev.get('text', ''):
                try: json.dump(dict(ev=ev, exe_ts=self.exe_ts, dll_ts=self.dll_ts), open(p + '.ev.json', 'w'))
                except Exception: pass
        if self.saida and self.saida.get('code'):
            self.events.append(dict(self.saida, kind='SAIDA_ERRO'))
        if self.saida is None and not any(e['kind'] == 'DUMP' and e.get('code') for e in self.events):
            # morreu sem SAIDA e sem dump: a ultima EXC fatal (heap / fastfail) e o defeito
            fatais = [e for e in self.excs if e.get('code') in (0xC0000374, 0xC0000409, 0xC00000FD)]
            if not fatais and self.excs and glob.glob(os.path.join(self.d, '**', '* Crash.txt'), recursive=True):
                fatais = self.excs[-1:]   # o cliente escreveu o Crash.txt sem .dmp (ex. AV 20:49): a ultima EXC e o crash
            if fatais:
                f = fatais[-1]
                quadros = [(m, r) for m, r in f['frames'] if m.lower() in (EXE.lower(), DLL.lower())]
                self.events.append(dict(f, kind='DUMP', code=f['code'], frames=quadros,
                                        text=f"{f['code']:08X} {CODES.get(f['code'], '')} (so EXC, sem dump)"))
        crash = [e for e in self.events if e['kind'] == 'DUMP' and e.get('code')]
        if crash: self.estado = 'CRASH'
        elif any(e['kind'] == 'TRAVADO' for e in self.events): self.estado = 'TRAVOU'
        elif self.saida is None: self.estado = 'SUMIU (sem SAIDA e sem dump)'
        elif self.saida.get('code'): self.estado = f'SAIDA {self.saida["code"]}'
        else: self.estado = 'OK'

def resolve(run, frames):
    """[(mod,rva)] -> [(texto, src|None)]"""
    exe = ExeSym(find_exe_dir(run.exe_ts)) if run.exe_ts and find_exe_dir(run.exe_ts) else None
    dd = find_dll_dir(run.dll_ts) if run.dll_ts else None
    ds = DLLSYMS.setdefault(dd, DllSym(dd))
    ds.resolve([r for m, r in frames if m.lower() == DLL.lower()])
    out = []
    for m, r in frames:
        if m.lower() == EXE.lower():
            out.append((f'exe {exe.name(r) if exe else hex(r)}', None))
        elif m.lower() == DLL.lower():
            fn, src = ds.get(r)
            out.append((f'dll {fn}' + (f'  ({src[0]}.cpp:{src[1]})' if src else f'  (rva 0x{r:X})'), src))
        elif m == '--varredura--':
            out.append(('-- a partir daqui: varredura da pilha (pode ter quadros velhos) --', None))
        else:
            out.append((f'{m}+0x{r:X}', None))
    return out

DLLSYMS = {}
CRT = re.compile(r'__report_gsfailure|__security_check_cookie|_invoke_watson|__fastfail|RtlFailFast|\babort\b|_CxxThrowException|SErrDisplay|RaiseException')

def tail(p, n=25):
    if not p or not os.path.exists(p): return []
    return open(p, encoding='utf-8', errors='replace').read().splitlines()[-n:]

def triagem():
    runs = [Run(d) for d in sorted(glob.glob(os.path.join(RUNS, '*'))) if os.path.isdir(d)]
    buckets = collections.OrderedDict()
    excb = collections.OrderedDict()
    for r in runs:
        r.classify()
        for e in r.events:
            res = resolve(r, e['frames'])
            res = [(t, s) for t, s in res if not (s and s[0] == 'AscBateria')] or res   # o detour de saida do proprio AscBateria nao e culpado
            e['res'] = res
            useful = [(t, s) for t, s in res if not CRT.search(t) and not t.startswith('--') and not re.match(r'(KERNEL|ntdll|ucrt)', t, re.I)] \
                or res
            # culpado = 1o quadro util; se ele e do exe, a DLL so leva a culpa quando aparece logo abaixo
            # (ate 2 quadros): mais longe e so um detour de passagem (ex. AscRuntime::Detour495810 no OnPaint)
            first_dll = next((i for i, (t, s) in enumerate(useful) if s), None)
            if useful and useful[0][1]:
                src, top = useful[0][1], useful[0][0]
            elif first_dll is not None and first_dll <= 2:
                src, top = useful[first_dll][1], useful[first_dll][0]
            else:
                src, top = None, (useful[0][0] if useful else '?')
            e['owner'] = owner(src)
            what = e.get('text', '').split(' (')[0] if e['kind'] == 'DUMP' else ''
            sig = f"{e['kind']} {what} @ " + re.sub(r'\+0x[0-9A-F]+', '', top)
            buckets.setdefault(sig, []).append((r, e))
        for e in r.excs:
            res = resolve(r, e['frames'][:12])
            src = next((s for t, s in res if s), None)
            top = res[0][0] if res else '?'
            sig = f"{e['code']:08X} @ " + re.sub(r'\+0x[0-9A-F]+', '', top)
            excb.setdefault(sig, []).append((r, e, res, owner(src) if src else owner(None)))

    L = ['# Bateria x64 — relatório', '',
         f'{len(runs)} rodada(s). Gerado por tools/bateria.py.', '', '## Rodadas', '',
         '| rodada | duração | estado | eventos | memória (último VIVO) | nota |', '|---|---|---|---|---|---|']
    for r in runs:
        L.append(f"| {r.id} | {r.info.get('segundos','?')} s | {r.estado} | {len(r.events)} | {(re.search(r'privada \d+ MB', r.vivo or '') or [''])[0]} | {r.nota.replace('|','/')} |")
    per = collections.defaultdict(list)
    L += ['', '## Defeitos (agrupados por assinatura)', '']
    for i, (sig, items) in enumerate(buckets.items(), 1):
        r, e = items[-1]
        blk = [f'### D{i}: {sig}', '',
               f'- ocorrências: {len(items)} em {len({x[0].id for x in items})} rodada(s): ' + ', '.join(sorted({x[0].id for x in items})),
               f'- dono: **{e["owner"]}**', f'- detalhe: {e.get("text","")}',
               f'- arquivo: `{os.path.relpath(e["file"], OUT)}`' if e.get('file') else '',
               f'- nota: {r.nota}' if r.nota else '',
               '', 'Pilha:', '```']
        blk += [f'  {t}' for t, s in e['res'][:20]] or ['  (sem quadros)']
        if e.get('threads'):
            blk += ['```', '', 'Outras threads (topo útil de cada uma; quem a principal espera costuma estar aqui):', '```']
            for tid, fr in e['threads'][1:]:
                res = [t for t, sr in resolve(r, fr[:12]) if not t.startswith('--')]
                util = [t for t in res if t.startswith(('exe', 'dll'))][:4]
                blk.append(f'  t{tid}: {res[0] if res else "?"}  <-  ' + '  <-  '.join(util))
        blk += ['```', '', 'Fim do Extensions.log:', '```'] + tail(r.extlog, 20) + ['```', '']
        blk = [x for x in blk if x != '']
        blk.append('')
        L += blk; per[e['owner']] += blk
    L += ['## Exceções de 1ª chance (EXC; tratadas ou não)', '',
          'Muitas são do próprio cliente (__try). Interessam as de dentro da DLL ou repetidas logo antes de um crash.', '']
    for sig, items in excb.items():
        r, e, res, own = items[-1]
        blk = [f'- `{sig}` ×{len(items)} ({own}): ' + ' ← '.join(t for t, s in res[:6])]
        L += blk
        if any(s for t, s in res): per[own] += ['### EXC de 1ª chance', ''] + blk + ['']
    # microtravadas: agrupadas pelo 1o quadro util (rip se for exe/DLL, senao o 1o da varredura)
    sol = collections.OrderedDict()
    for r in runs:
        for e in r.solucos:
            res = resolve(r, e['frames'][:9])
            res = [(t, sr) for t, sr in res if not re.match(r'(KERNEL|ntdll|ucrt|win32u|user32|d3d|nv|amd)', t, re.I)] or res
            if not res: continue
            key = re.sub(r'\+0x[0-9A-F]+', '', res[0][0])
            src = next((sr for t, sr in res[:3] if sr), None)
            g = sol.setdefault(key, dict(n=0, ms=0, maior=0, runs=set(), ex=res, own=owner(src)))
            g['n'] += 1; g['ms'] += e['ms']; g['maior'] = max(g['maior'], e['ms']); g['runs'].add(r.id)
    if sol:
        L += ['', '## Microtravadas (SOLUCO >= 200 ms), por onde a thread principal estava', '',
              '| total ms | vezes | maior | onde | dono |', '|---|---|---|---|---|']
        for key, g in sorted(sol.items(), key=lambda kv: -kv[1]['ms'])[:40]:
            L.append(f"| {g['ms']} | {g['n']} | {g['maior']} | {key} | {g['own']} |")
            if g['ms'] >= 1000:
                per[g['own']] += [f"### Microtravada: {key} — {g['n']}× total {g['ms']} ms (maior {g['maior']} ms)", '```'] + \
                                 [f'  {t}' for t, sr in g['ex'][:8]] + ['```', '']
    os.makedirs(OUT, exist_ok=True)
    open(os.path.join(OUT, 'RELATORIO.md'), 'w').write('\n'.join(L) + '\n')
    for f in glob.glob(os.path.join(OUT, 'para-*.md')): os.remove(f)
    for who, blk in per.items():
        open(os.path.join(OUT, f'para-{who}.md'), 'w').write(f'# Bateria x64 — itens para {who}\n\nRelatório completo: out/bateria/RELATORIO.md\n\n' + '\n'.join(blk) + '\n')
    print(f'{len(runs)} rodadas, {len(buckets)} defeito(s), {len(excb)} EXC distintas')
    for r in runs: print(f'  {r.id}: {r.estado}')
    for who in per: print(f'  para-{who}.md')

if __name__ == '__main__':
    os.makedirs(SYM, exist_ok=True)
    a = sys.argv[1] if len(sys.argv) > 1 else 'tudo'
    if a in ('coleta', 'tudo'): coleta()
    if a in ('triagem', 'tudo', 'enxuga'): triagem()
    if a == 'enxuga':
        # apaga os .dmp ja analisados (tem .ev.json), menos os da rodada mais nova
        runs = sorted(d for d in glob.glob(os.path.join(RUNS, '*')) if os.path.isdir(d))
        livre = 0
        for d in runs[:-1]:
            for p in glob.glob(os.path.join(d, '**', '*.dmp'), recursive=True):
                if os.path.exists(p + '.ev.json'):
                    livre += os.path.getsize(p); os.remove(p)
        print(f'enxuga: {livre / 2**30:.1f} GB de dumps analisados apagados (rodada mais nova mantida)')
