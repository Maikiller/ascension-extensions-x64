#!/usr/bin/env python3
"""Classifica os enderecos de codigo de out/missing.txt (< 0xA00000): funcao do fonte (tags.json OFFSET, bisect) + offset,
disasm x86 em volta e candidatos x64 (mesma mnemonica perto das ancoras = calls vizinhos cujo alvo esta no .addr).
Uso: an.py > relatorio.txt   (gera out/an.json; WOW32=caminho do wow.exe 12340)"""
import json,struct,bisect,re,sys,collections
import pefile,capstone
import os
W=os.path.dirname(os.path.dirname(os.path.abspath(__file__))); S=os.path.join(W,'out')
WOW32=os.environ.get('WOW32','/projects/tmp-dbc/wow.exe')
tags=[t for t in json.load(open(W+'/tags.json')) if t['kind']=='OFFSET']
tags.sort(key=lambda t:t['addr']); starts=[t['addr'] for t in tags]
d=open(W+'/out/WowClient64.addr','rb').read(); n=struct.unpack('<I',d[12:16])[0]
a2r={}
for i in range(n):
    a,r=struct.unpack('<II',d[16+8*i:24+8*i]); a2r[a]=r
r2a=collections.defaultdict(list)
for a,r in a2r.items(): r2a[r].append(a)
procs=json.load(open(S+'/procs64.json')); pst=[p[0] for p in procs]
def proc_of(r):
    i=bisect.bisect_right(pst,r)-1
    if i>=0 and procs[i][0]<=r<procs[i][0]+procs[i][1]: return procs[i]
pe32=pefile.PE(WOW32); img32=pe32.get_memory_mapped_image()
pe64=pefile.PE(W+'/out/WowClient64.exe'); img64=pe64.get_memory_mapped_image()
c32=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32); c64=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
def dis32(a,size): return list(c32.disasm(img32[a-0x400000:a-0x400000+size],a))
def dis64(r,size): return list(c64.disasm(img64[r:r+size],0x140000000+r))
def tgt(i):
    try: return int(i.op_str,16)
    except: return None
uses=collections.defaultdict(list)
srcs={}
for root in (W+'/ext/src', W+'/ext/WotLKExtensions/src'):
    for dp,_,fs in os.walk(root):
        for f in fs: srcs.setdefault(f, os.path.join(dp,f))
for l in open(S+'/missing.txt'):
    p=l.split()
    if not p or not p[0].startswith('0x') or int(p[0],16)>=0xA00000: continue
    for r in p[1:]:
        f,ln=r.rsplit(':',1); txt=open(srcs[f],errors='ignore').read().split('\n')[int(ln)-1].strip() if f in srcs else ''
        uses[int(p[0],16)].append('%s | %s'%(r,txt))
out=[]
for a in sorted(uses):
    e={'addr':a,'uses':uses[a]}
    k=bisect.bisect_right(starts,a)-1; t=tags[k] if k>=0 else None
    nxt=starts[k+1] if k+1<len(starts) else None
    e['func']=t['decl'] if t else None; e['file']='%s:%d'%(t['file'],t['line']) if t else None
    e['fstart']=t['addr'] if t else None; e['off']=a-t['addr'] if t else None; e['next']=nxt
    if not (0x401000<=a<0x9df000): e['sec']='rdata/data'; out.append(e); continue
    # boundary check via linear sweep from function start
    ins=dis32(t['addr'],a-t['addr']+64); ok=False; seq=[]
    for i in ins:
        seq.append(i)
        if i.address==a: ok=True
        if i.address>a+40: break
    e['aligned']=ok
    idx=[j for j,i in enumerate(seq) if i.address==a]
    if idx:
        j=idx[0]; e['x86']=['%s%X: %s %s %s'%('>' if s.address==a else ' ',s.address,s.bytes.hex(),s.mnemonic,s.op_str) for s in seq[max(0,j-4):j+4]]
        e['ins']=(seq[j].mnemonic,seq[j].op_str)
        # anchors: calls near
        anchors=[]
        for s in seq[max(0,j-12):j+12]:
            if s.mnemonic=='call':
                tg=tgt(s)
                if tg and tg in a2r: anchors.append((s.address-a,tg,a2r[tg]))
        e['anchors']=anchors
    else:
        e['x86']=['%X: %s %s'%(s.address,s.mnemonic,s.op_str) for s in seq if a-12<=s.address<=a+12]
    # x64 function
    r=a2r.get(t['addr']) if t else None
    if r is None: e['x64']='funcao sem mapeamento no .addr'; out.append(e); continue
    p=proc_of(r); e['x64func']=(hex(0x140000000+r),p[2] if p else '?',p[1] if p else 0)
    if not p: out.append(e); continue
    ins64=dis64(p[0],p[1]); cands=[]
    if 'ins' in e:
        mn,op=e['ins']
        imms=set(re.findall(r'0x[0-9a-f]+',op))
        for i in ins64:
            s=0
            if i.mnemonic==mn: s+=1
            if imms and any(x in i.op_str for x in imms if int(x,16)>8 and int(x,16)<0x400000): s+=2
            for (dd,tg,tr) in e.get('anchors',[]):
                if i.mnemonic=='call' and tgt(i)==0x140000000+tr and abs(dd)<60: s+=0  # recorded below
            if s>=1 and (mn==i.mnemonic or s>=2): cands.append((s,i))
        # anchor positions in x64
        apos=[]
        for (dd,tg,tr) in e.get('anchors',[]):
            for i in ins64:
                if i.mnemonic=='call' and tgt(i)==0x140000000+tr: apos.append((dd,i.address))
        e['anchor64']=[('%+d'%dd,hex(x)) for dd,x in apos]
        def near(i): return min([abs(i.address-(x-dd)) for dd,x in apos] or [9999])
        cands.sort(key=lambda c:(-c[0],near(c[1])))
        e['cands']=['%X(%+d anc=%d): %s %s %s'%(i.address,i.address-0x140000000-p[0],near(i),i.bytes.hex(),i.mnemonic,i.op_str) for s,i in cands[:5]]
        e['ncands']=len(cands)
    out.append(e)
json.dump(out,open(S+'/an.json','w'),indent=1,default=str)
for e in out:
    print('=== 0x%X'%e['addr'],e.get('func'),'off=+0x%X'%e['off'] if e['off'] is not None else '', 'next=%s'%(hex(e['next']) if e['next'] else ''), 'aligned=%s'%e.get('aligned'))
    for u in e['uses'][:2]: print('  use:',u[:150])
    for x in e.get('x86',[]): print('  ',x)
    print('  x64:',e.get('x64func') or e.get('x64'),'anchors',e.get('anchor64'),'ncands',e.get('ncands'))
    for c in e.get('cands',[]): print('    ',c)
