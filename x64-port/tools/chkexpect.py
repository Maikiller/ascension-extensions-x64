"""Confere bytes de WriteCodeX64/ToggleX64/applyX64/kPatchesX64 do ext contra out/WowClient64.exe (.addr)."""
import re,glob,struct,pefile,os,sys
W=os.environ.get('COA_WORK') or os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.realpath(__file__))))); E=os.environ.get('EXT',W+'/ext')
B=os.environ.get('BASE',W+'/out'); d=open(B+'/WowClient64.addr','rb').read(); n=struct.unpack('<I',d[12:16])[0]
A={}
for i in range(n):
    a,r=struct.unpack('<II',d[16+8*i:24+8*i]); A[a]=r
pe=pefile.PE(B+'/WowClient64.exe',fast_load=True)
def bytes_at(a,delta,k):
    r=A.get(a); 
    if r is None: return None
    return pe.get_data(r+delta,k)
def parse_arr(s):
    return bytes(v for v in (int(x,0) for x in re.findall(r'0x[0-9A-Fa-f]+|\b\d+\b',s)) if v<256)
bad=0; tot=0
for f in sorted(glob.glob(E+'/src/**/*.cpp',recursive=True)+glob.glob(E+'/WotLKExtensions/**/*.cpp',recursive=True)):
    t=open(f,errors='ignore').read()
    defs=[]
    for m in re.finditer(r'(?:static\s+)?const\s+uint8_t\s+([^;]*?);',t,re.S):
        for mm in re.finditer(r'(\w+)\s*\[[^\]]*\]\s*=\s*\{([^}]*)\}',m.group(1)): defs.append((m.start(),mm.group(1),parse_arr(mm.group(2))))
    def arr_at(name,pos):
        c=[b for p_,n_,b in defs if n_==name and p_<pos]
        return c[-1] if c else None
    # kPatchesX64 rows
    for m in re.finditer(r'\{\s*(0x[0-9A-Fa-f]+)\s*,\s*(-?\w+)\s*,\s*(\d+)\s*,\s*\{([^}]*)\}\s*,\s*\{([^}]*)\}\s*\}',t):
        a=int(m.group(1),16); dl=int(m.group(2),0); k=int(m.group(3)); new=parse_arr(m.group(4)); exp=parse_arr(m.group(5))
        tot+=1; got=bytes_at(a,dl,k); ln=t.count('\n',0,m.start())+1
        if got is None: print('SEMEND %s:%d 0x%X'%(os.path.relpath(f,E),ln,a)); bad+=1
        elif got!=exp[:k] and got!=new[:k]: print('DIF %s:%d 0x%X%+d exe=%s exp=%s'%(os.path.relpath(f,E),ln,a,dl,got.hex(),exp[:k].hex())); bad+=1
    for m in re.finditer(r'(WriteCodeX64|ToggleX64|applyX64)\(\s*(0x[0-9A-Fa-f]+)\s*,([^;]*)\);',t):
        fn=m.group(1); a=int(m.group(2),16); args=[x.strip() for x in m.group(3).split(',')]
        ln=t.count('\n',0,m.start())+1; names=[re.sub(r'.*?(\w+)\s*$',r'\1',x) for x in args]
        cand=[arr_at(x,m.start()) for x in re.findall(r'\b(k\w+)\b',m.group(3)) if arr_at(x,m.start()) is not None]
        dl=0
        if fn=='WriteCodeX64' and len(args)>=4:
            last=args[-1]
            if re.fullmatch(r'-?(0x[0-9A-Fa-f]+|\d+)',last) and len(args)==5: dl=int(last,0)
        if not cand: continue
        k=min(len(c) for c in cand)
        mk=re.search(r',\s*(\d+|sizeof\(\w+\))\s*,',m.group(3))
        tot+=1; got=bytes_at(a,dl,k)
        if got is None: print('SEMEND %s:%d 0x%X'%(os.path.relpath(f,E),ln,a)); bad+=1; continue
        if not any(got==c[:k] for c in cand):
            print('DIF %s:%d %s 0x%X%+d exe=%s cand=%s'%(os.path.relpath(f,E),ln,fn,a,dl,got.hex(),[c[:k].hex() for c in cand])); bad+=1
print('total',tot,'problemas',bad)
