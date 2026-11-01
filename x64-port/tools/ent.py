#!/usr/bin/env python3
"""Funcoes 12340 sem OFFSET -> funcao x64: acha os chamadores x86 (call rel32), alinha a sequencia de calls do
chamador (ancoras = alvos presentes no .addr) com a do x64 e vota no alvo x64.
Uso: ent.py 0xADDR [...]   (WOW32=caminho do wow.exe 12340; cache dos procs x64 em out/procs64.json)"""
import json,struct,bisect,collections,sys
import pefile,capstone
import os, subprocess, re
W=os.path.dirname(os.path.dirname(os.path.abspath(__file__))); S=os.path.join(W,'out')
WOW32=os.environ.get('WOW32','/projects/tmp-dbc/wow.exe')
if not os.path.exists(S+'/procs64.json'):
    rx=re.compile(r'S_[GL]PROC32 \[size = \d+\] `(.*)`'); ra=re.compile(r'addr = 0001:(\d+), code size = (\d+)'); pr=[]; nm=None
    for l in subprocess.run(['llvm-pdbutil','dump','-symbols',S+'/WowClient64.pdb'],capture_output=True,text=True).stdout.split('\n'):
        m=rx.search(l)
        if m: nm=m.group(1); continue
        if nm:
            m=ra.search(l)
            if m: pr.append((int(m.group(1))+0x1000,int(m.group(2)),nm))
            nm=None
    json.dump(sorted(pr),open(S+'/procs64.json','w'))
tags=sorted([t for t in json.load(open(W+'/tags.json')) if t['kind']=='OFFSET'],key=lambda t:t['addr']); starts=[t['addr'] for t in tags]
d=open(W+'/out/WowClient64.addr','rb').read(); n=struct.unpack('<I',d[12:16])[0]
a2r=dict(struct.unpack('<II',d[16+8*i:24+8*i]) for i in range(n))
procs=json.load(open(S+'/procs64.json')); pst=[p[0] for p in procs]
byrva={p[0]:p for p in procs}
def proc_of(r):
    i=bisect.bisect_right(pst,r)-1
    if i>=0 and procs[i][0]<=r<procs[i][0]+procs[i][1]: return procs[i]
pe32=pefile.PE(WOW32); img32=pe32.get_memory_mapped_image()
pe64=pefile.PE(W+'/out/WowClient64.exe'); img64=pe64.get_memory_mapped_image()
c32=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32); c64=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
text=img32[0x1000:0x1000+0x5dd3b3]
callers=collections.defaultdict(list); i=text.find(b'\xe8')
while i>=0:
    if i+5<=len(text):
        t=0x401000+i+5+struct.unpack('<i',text[i+1:i+5])[0]
        if 0x401000<=t<0x9df000: callers[t].append(0x401000+i)
    i=text.find(b'\xe8',i+1)
def tgt(op):
    try: return int(op,16)
    except: return None
def calls32(f,end):
    out=[]
    for x in c32.disasm(img32[f-0x400000:end-0x400000],f):
        if x.mnemonic=='call': out.append((x.address,tgt(x.op_str)))
    return out
def calls64(p):
    out=[]
    for x in c64.disasm(img64[p[0]:p[0]+p[1]],0x140000000+p[0]):
        if x.mnemonic in('call','jmp'):
            t=tgt(x.op_str)
            if t and (x.mnemonic=='call' or not (0x140000000+p[0]<=t<0x140000000+p[0]+p[1])): out.append((x.address,t-0x140000000))
    return out
def fn32(a):
    k=bisect.bisect_right(starts,a)-1; return tags[k], (starts[k+1] if k+1<len(starts) else a+1)
def align(c32s,c64s):
    # anchors: x86 calls whose target maps; match in order to x64 calls with same rva
    pairs=[];j0=0
    for idx,(ad,t) in enumerate(c32s):
        r=a2r.get(t)
        if r is None: continue
        for j in range(j0,len(c64s)):
            if c64s[j][1]==r: pairs.append((idx,j)); j0=j+1; break
    return pairs
def guess(X):
    votes=collections.Counter(); ev=[]
    for c in callers.get(X,[]):
        t,end=fn32(c)
        r=a2r.get(t['addr']); p=proc_of(r) if r is not None else None
        if not p: continue
        a=calls32(t['addr'],end); b=calls64(p)
        idx=[k for k,(ad,_) in enumerate(a) if ad==c]
        if not idx: continue
        k=idx[0]; pairs=align(a,b)
        lo=max([pj for pi,pj in pairs if pi<k],default=-1); hi=min([pj for pi,pj in pairs if pi>k],default=len(b))
        cand=[b[j][1] for j in range(lo+1,hi)]
        # numero de calls desconhecidos entre as ancoras do lado x86
        pl=max([pi for pi,pj in pairs if pi<k],default=-1); ph=min([pi for pi,pj in pairs if pi>k],default=len(a))
        pos=k-pl-1; nx=ph-pl-1
        if len(cand)==nx and cand: votes[cand[pos]]+=2; ev.append((hex(c),t['decl'][:50],'exato'))
        else:
            for x in set(cand): votes[x]+=1.0/len(set(cand))
            ev.append((hex(c),t['decl'][:50],'%d cand'%len(set(cand))))
    return votes,ev
if __name__=='__main__':
    for a in sys.argv[1:]:
        X=int(a,16); v,ev=guess(X)
        print('== %s callers=%d'%(a,len(callers.get(X,[]))))
        for r,s in v.most_common(4):
            p=byrva.get(r); print('   %.1f 0x%X %s'%(s,0x140000000+r,p[2] if p else '(nao e inicio de proc)'))
        for e in ev[:6]: print('     ',e)
