"""for chosen dc files: walk every PS3 version separately (bin, goty-*, p13, p18, DATA2 extras) and count real mismatches, plus retail PS4"""
from common import *
import pwalk
SRC=[('p18',U+'dlc-patch18.psarc'),('p13',U+'dlc-patch13.psarc'),('gotyp',G+'goty-patch.psarc'),('gotyd',G+'goty-dlc.psarc'),('bin',G+'build/main/bin.psarc')]
AR={k:Psarc(f) for k,f in SRC}
def real(ms):
    n=0
    for m in ms:
        if m[4] in('value','short') and isinstance(m[5],int) and isinstance(m[6],int):
            a,b=m[5],m[6]
            if a<0x10000 and b<0x10000:continue
            if (b>>16)==(a&0xffff) or (a>>16)==(b&0xffff):continue
            A=a.to_bytes(4,'big')
            if all(c==0 or 32<=c<127 for c in A) and 0 in A:continue
        n+=1
    return n
tmp=AUD+'_b.bin'
for rel in sys.argv[1:]:
    print('==',rel)
    e=eff('dc1/ps4/'+rel)
    tgts=[(e[0],e[1])]
    if e[0]!='retail' and os.path.exists(RET+'dc1/ps4/'+rel):tgts.append(('retail',RET+'dc1/ps4/'+rel))
    for k,p in AR.items():
        for i,n in enumerate(p.names):
            if n.endswith('dc1/'+rel):
                open(tmp,'wb').write(p.read(i+1))
                for tn,tf in tgts:
                    w,o3,o4,ty=pwalk.compare(tmp,tf)
                    print('  ps3 %-6s len %7d vs %-10s real-mism=%d ps3only=%d ps4only=%d'%(k,os.path.getsize(tmp),tn,real(w.mm),len(o3),len(o4)))
