#!/usr/bin/env python3
"""Protótipo x64 de cada endereço 12340 mapeado -> out/protos.json {addr_hex: {name, ret, args:[...], this:bool}}"""
import re, json, struct, subprocess, os
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'out')
import pefile
pe = pefile.PE(os.path.join(OUT, 'WowClient64.exe'), fast_load=True)
sv = {i + 1: s.VirtualAddress for i, s in enumerate(pe.sections)}
# tipos
recs = {}; cur = None
rxh = re.compile(r'^\s+0x([0-9A-F]+) \| (LF_\w+) \[size = \d+\](?: `(.*)`)?')
for line in open(os.path.join(OUT, 'types.txt'), errors='ignore'):
    m = rxh.match(line)
    if m: cur = {'k': m.group(2), 'name': m.group(3), 'lines': []}; recs[int(m.group(1), 16)] = cur; continue
    if cur is not None: cur['lines'].append(line.strip())
def arglist(ti):
    r = recs.get(ti)
    if not r: return []
    out = []
    for l in r['lines']:
        m = re.match(r'0x[0-9A-F]+(?: \([^)]*\))?: `(.*)`$', l)
        if m: out.append(m.group(1))
    return out
def proto(ti):
    r = recs.get(ti)
    if not r: return None
    L = ' '.join(r['lines'])
    m = re.search(r'return type = 0x[0-9A-F]+ \(([^)]*)\)|return type = 0x([0-9A-F]+)', L)
    ret = (m.group(1) or recs.get(int(m.group(2), 16), {}).get('name') or '?') if m else '?'
    p = re.search(r'param list = 0x([0-9A-F]+)', L)
    args = arglist(int(p.group(1), 16)) if p else []
    return {'ret': ret, 'args': args, 'this': r['k'] == 'LF_MFUNCTION' and 'this type = <no type>' not in L and 'this type = 0x' in L}
procs = {}; last = None
for l in subprocess.run(['llvm-pdbutil', 'dump', '--symbols', os.path.join(OUT, 'WowClient64.pdb')], capture_output=True, text=True).stdout.split('\n'):
    m = re.search(r'S_[GL]PROC32 \[size = \d+\] `([^`]+)`', l)
    if m: last = [m.group(1), None]; continue
    if last and last[1] is None:
        m = re.search(r'addr = ([0-9A-F]{4}):(\d+)', l)
        if m: last[1] = sv[int(m.group(1), 16)] + int(m.group(2)); continue
    if last and last[1] is not None:
        m = re.search(r'type = `0x([0-9A-F]+)', l)
        if m: procs[last[1]] = (last[0], int(m.group(1), 16)); last = None
d = open(os.path.join(OUT, 'WowClient64.addr'), 'rb').read(); n = struct.unpack('<I', d[12:16])[0]
res = {}
for i in range(n):
    a, rva = struct.unpack('<II', d[16 + 8 * i:24 + 8 * i])
    if rva in procs:
        name, ti = procs[rva]; p = proto(ti) or {}
        res['%X' % a] = dict(name=name, **p)
json.dump(res, open(os.path.join(OUT, 'protos.json'), 'w'), indent=0)
print('protótipos:', len(res))
