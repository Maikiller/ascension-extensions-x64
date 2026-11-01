#!/usr/bin/env python3
"""Literais de endereco do 12340 usados SEM ASC_PTR no codigo x64 vivo do DLL (cast direto, aritmetica, comparacao com
_ReturnAddress, guardados em struct). Ignora: chamadas com o endereco como argumento de helpers que ja resolvem
(ASC_PTR/Detour/Hook/Replace/CLIENT_*/WriteCode/Patch*/Row/Copy/ClientDbcRow/Unported) e comentarios."""
import re, os, sys
W = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(W, 'tools')); import checksig as C
lit = re.compile(r'0x(?:[4-9A-Da-d][0-9A-Fa-f]{5})\b')
ok_ctx = re.compile(r'(Patch\w*<[^>]*>|WriteCode\w*|ASC_PTR|Detour|ReplaceFunction|Hook<[^>]*>|Hook|CLIENT_FUNCTION|CLIENT_ADDRESS|WriteCode|Patch\w*|Fn<[^>]*>|Util::\w+|Row|Copy|Native|Field|ClientDbcRow|MinId|MaxId|DbcMinId|DbcMaxId|Insert|Unported|Printf|CacheRecord|SetSun|WriteProtected|Guid|Var\w*|Register\w*|AddPacketHandler|On\w+)\s*\(\s*(?:[\w:]+\s*,\s*)?$')
rows = []
for root in (os.path.join(W, 'ext', 'src'), os.path.join(W, 'ext', 'WotLKExtensions', 'src')):
    for dp, _, fs in os.walk(root):
        for f in fs:
            p = os.path.join(dp, f)
            if not f.endswith(('.cpp', '.hpp')) or C.excluded(p): continue
            txt = open(p, errors='ignore').read(); skip = C.x86_only_lines(txt)
            for i, line in enumerate(txt.split('\n'), 1):
                if i in skip: continue
                code = re.sub(r'"(?:\\.|[^"\\])*"', '""', line.split('//')[0])
                if re.match(r'\s*(case\b|mov|push|call|jmp|cmp|lea|add|sub|and|or|test)\b', code): continue
                for m in lit.finditer(code):
                    pre = code[:m.start()]
                    if ok_ctx.search(pre): continue
                    if re.search(r'&\s*$|\|\s*$|\^\s*$', pre) or re.search(r'^\s*[&|]', code[m.end():]): continue   # mascaras de bit
                    if re.search(r'\b(Patch\w*|WriteCode\w*)\s*(<[^>]*>)?\s*\(', code): kind = 'PATCH'
                    elif '_ReturnAddress' in code: kind = 'RETADDR'
                    else: kind = 'RAW'
                    rows.append(kind + ' ' + '%s:%d  %s' % (os.path.relpath(p, os.path.join(W, 'ext')), i, code.strip()[:140]))
print('\n'.join(rows)); print('total', len(rows), file=sys.stderr)
