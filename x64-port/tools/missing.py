#!/usr/bin/env python3
"""Enderecos 12340 usados pelo codigo x64 do DLL (ASC_PTR/Detour/Hook/Replace/CLIENT_*) que nao estao no WowClient64.addr."""
import re, os, sys, struct, collections
W = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(W, 'tools')); import checksig as C
d = open(os.path.join(W, 'out', 'WowClient64.addr'), 'rb').read(); n = struct.unpack('<I', d[12:16])[0]
have = {struct.unpack('<II', d[16 + 8 * i:24 + 8 * i])[0] for i in range(n)}
rx = re.compile(r'(?:ASC_PTR\(\s*|Detour\(\s*|ReplaceFunction\(\s*|Hook<[^>]*>\(\s*|CLIENT_FUNCTION\(\s*\w+\s*,\s*|CLIENT_ADDRESS\(\s*\w+\s*,\s*|Fn<[^>]*>\(\s*|Patch\w*\(\s*|WriteCode\(\s*|Util::\w+\(\s*)(0x[0-9A-Fa-f]{6,8})\b')
miss = collections.defaultdict(set)
for root in (os.path.join(W, 'ext', 'src'), os.path.join(W, 'ext', 'WotLKExtensions', 'src')):
    for dp, _, fs in os.walk(root):
        for f in fs:
            p = os.path.join(dp, f)
            if not f.endswith(('.cpp', '.hpp', '.h', '.inc')) or C.excluded(p): continue
            txt = open(p, errors='ignore').read(); skip = C.x86_only_lines(txt)
            for i, line in enumerate(txt.split('\n'), 1):
                if i in skip: continue
                for m in rx.finditer(line.split('//')[0]):
                    a = int(m.group(1), 16)
                    if 0x400000 <= a < 0x1000000 and a not in have: miss[a].add('%s:%d' % (f, i))
for a in sorted(miss): print('0x%06X %s' % (a, ' '.join(sorted(miss[a])[:4])))
print('faltando', len(miss), file=sys.stderr)
