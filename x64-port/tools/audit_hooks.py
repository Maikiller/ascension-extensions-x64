import re,os,sys,json
_T=os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0,_T); import checksig as C
P=C.P; W=os.environ.get('AUDIT_W', os.path.join(os.path.dirname(_T),'ext'))
files=[os.path.join(dp,f) for r in (W+'/src',W+'/WotLKExtensions/src') for dp,_,fs in os.walk(r) for f in fs if f.endswith(('.cpp','.hpp','.h'))]
# definicoes de funcao do DLL (ramo x64 visivel)
rx_def=re.compile(r'^\s*(?:static\s+|inline\s+)*([\w:<>\*&\s]+?)\s+(__cdecl|__stdcall|__fastcall|__thiscall)?\s*(\w+)\s*\(([^;{}]*?)\)\s*(?://[^\n]*)?\s*\{', re.M)
defs={}
for f in files:
    if C.excluded(f): continue
    txt=open(f,errors='ignore').read(); skip=C.x86_only_lines(txt)
    for m in rx_def.finditer(txt):
        ln=txt.count('\n',0,m.start())+1
        if ln in skip: continue
        ret=m.group(1).strip().split()[-1] if m.group(1).strip() else ''
        defs.setdefault(m.group(3),[]).append((f,ln,m.group(1).strip(),m.group(2) or '__cdecl',m.group(4)))
rx_hook=re.compile(r'(Detour|ReplaceFunction|Hook)(?:<[^<>()]*>)?\(\s*(0x[0-9A-Fa-f]{6,8})\s*,[^;]*?&\s*([\w:]+)')   # inclui Hook<T>(0x...)
out=[]
import json as _j
_tags={x['addr']:x for x in _j.load(open(os.path.join(os.path.dirname(_T),'tags.json'))) if x.get('kind')=='OFFSET'}
for f in files:
    if C.excluded(f): continue
    txt=open(f,errors='ignore').read(); skip=C.x86_only_lines(txt)
    for n,l in enumerate(txt.split('\n'),1):
        if n in skip: continue
        for m in rx_hook.finditer(l.split('//')[0]):
            kind,addr,name=m.group(1),int(m.group(2),16),m.group(3).split('::')[-1]
            pr=P.get('%X'%addr)
            rel=os.path.relpath(f,W)
            if not pr: out.append(('SEMPROTO',rel,n,addr,name,'')); continue
            ds=defs.get(name)
            if not ds: out.append(('SEMDEF',rel,n,addr,name,pr['name'])); continue
            df,dl,dret,conv,args=ds[0]
            d=C.dll_cls(conv,args); r=C.proto_cls(pr)
            if 'v' in r: r=r[:r.index('v')]; d=d[:len(r)]
            probs=[]
            if len(d)!=len(r): probs.append('n args dll %d x64 %d'%(len(d),len(r)))
            else:
                for i,(x,y) in enumerate(zip(d,r)):
                    if x==y: continue
                    if x in 'qp' and y=='i': probs.append('arg%d: dll 64b, x64 int (32 de cima = lixo)'%i)
                    elif x=='i' and y in 'pq': probs.append('arg%d: dll int, x64 %s (TRUNCA)'%(i,'ptr' if y=='p' else '64b'))
                    elif {x,y}=={'p','q'}: pass
                    else: probs.append('arg%d: dll %s x64 %s'%(i,x,y))
            pret=(pr.get('ret') or '').replace('const ','').strip()
            dr=C.cls(re.sub(r'\b(static|inline)\b','',dret)) or 'v'
            prc=None if pret in ('','?') else ('v' if pret=='void' else C.cls(pret))
            if pret in ('','?') and dr in ('i',):
                tg=_tags.get(addr); dec=(tg or {}).get('decl','')
                mm=re.match(r'\s*(?:static\s+|virtual\s+|inline\s+)*(.+?)\s*[\w:~]+\s*\(',dec)
                rt=mm.group(1) if mm else ''
                if '*' in rt or re.search(r'\b(WOWGUID|uint64_t|int64_t|size_t|uintptr_t|HANDLE|HOBJECT)\b',rt): probs.append('ret(?): dll int, fonte %r (TRUNCA)'%rt)
            if prc and prc!='v' and dr=='i' and prc in 'pq': probs.append('ret: dll int, x64 %s (TRUNCA)'%pret)
            if prc=='f' and dr!='f' or dr=='f' and prc not in (None,'f','v'): probs.append('ret float: dll %s x64 %s'%(dret,pret))
            if probs: out.append(('PROB',rel,n,addr,name,pr['name']+' | '+'; '.join(probs)+'\n      dll: %s %s(%s)\n      x64: %s(%s)'%(dret,conv,args.strip()[:120],pret,', '.join(pr['args']))))
            else: out.append(('OK',rel,n,addr,name,''))
from collections import Counter

print(Counter(o[0] for o in out))
for o in out:
    if o[0]!='OK': print('%s %s:%d 0x%X %s %s'%o)
