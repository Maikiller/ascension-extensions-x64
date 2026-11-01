#!/usr/bin/env python3
"""Lista as marcas // OFFSET: e // STATIC: do fonte do cliente -> JSON [{kind,addr,file,line,decl}].
file = caminho relativo ao repo com '/', line = linha da marca (1-based)."""
import os, re, sys, json
root = sys.argv[1]
rx = re.compile(r'//\s*(OFFSET|STATIC):\s*0x([0-9A-Fa-f]+)')
out = []
for dp, _, fs in os.walk(root):
    if '/ThirdParty' in dp or '/build' in dp or '/.git' in dp: continue
    for f in fs:
        if not f.endswith(('.cpp', '.h', '.hpp', '.c', '.inl')): continue
        p = os.path.join(dp, f); rel = os.path.relpath(p, root).replace('\\', '/')
        lines = open(p, errors='ignore').read().split('\n')
        for i, l in enumerate(lines):
            m = rx.search(l)
            if not m: continue
            decl = ''
            for j in range(i + 1, min(i + 8, len(lines))):
                s = lines[j].strip()
                if s and not s.startswith('//'): decl = s; break
            out.append(dict(kind=m.group(1), addr=int(m.group(2), 16), file=rel, line=i + 1, decl=decl))
json.dump(out, sys.stdout)
