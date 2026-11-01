#!/usr/bin/env python3
"""Prontidao x64 por arquivo do ext: missing (end. sem traducao), patch/raw (rawaddr), objoffs, Unported, __asm."""
import os, re, sys, subprocess, collections
W = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(W, 'tools')); import checksig as C
cnt = collections.defaultdict(collections.Counter)
for l in open(os.path.join(W, 'out', 'missing.txt')):
    m = re.search(r'(\w+\.cpp):\d+', l)
    if m: cnt[m.group(1)]['miss'] += 1
for l in subprocess.run([sys.executable, os.path.join(W, 'tools', 'rawaddr.py')], capture_output=True, text=True).stdout.split('\n'):
    m = re.match(r'(\w+) \S*?(\w+\.cpp):', l)
    if m: cnt[m.group(2)][m.group(1).lower()] += 1
for l in subprocess.run([sys.executable, os.path.join(W, 'tools', 'objoffs.py')], capture_output=True, text=True).stdout.split('\n'):
    for f in re.findall(r'(\w+\.cpp):\d+', l): cnt[f]['obj'] += 1
for root in (os.path.join(W, 'ext', 'src'), os.path.join(W, 'ext', 'WotLKExtensions', 'src')):
    for dp, _, fs in os.walk(root):
        for f in fs:
            p = os.path.join(dp, f)
            if not f.endswith('.cpp') or C.excluded(p): continue
            t = open(p, errors='ignore').read(); skip = C.x86_only_lines(t)
            for i, line in enumerate(t.split('\n'), 1):
                if i in skip: continue
                if 'Unported(' in line and 'void Unported' not in line: cnt[f]['unp'] += 1
                if 'TODO x64' in line: cnt[f]['todo'] += 1
            cnt[f]['_'] += 0
cols = ['miss', 'patch', 'raw', 'retaddr', 'obj', 'unp', 'todo']
rows = sorted(cnt.items(), key=lambda kv: sum(kv[1][c] for c in cols))
print('%-34s ' % 'arquivo' + ' '.join('%6s' % c for c in cols))
for f, c in rows:
    print('%-34s ' % f + ' '.join('%6d' % c[k] for k in cols))
