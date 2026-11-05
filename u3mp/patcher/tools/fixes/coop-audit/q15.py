"""PS3-independent scan: every option entry in every effective dc1 script whose int32/float/boolean default holds a known
string id or whose target is also a pointer target of something else (the signature of the lost slot). Also lists option entries
with no PS3 counterpart (q14b 'unmatched').  writes options-scan.txt"""
from common import *
import collections
TY={0xc7cb275c:'int32',0x8b4e76ff:'boolean',0xf182ec3:'float',0xc4ab6121:'symbol',0xb3952e7:'string'}
T=tree('dc1/ps4');out=open(AUD+'options-scan.txt','w');n=0;tot=0
D=dic()
for rel,(o,f) in sorted(T.items()):
    if not f.endswith('.bin') or not re.search(r'/ss-|/ss/',rel):continue
    try:c=DC(f)
    except Exception:continue
    d=c.d;k=0
    tg=collections.Counter(c.ptr(q) for q in c.ptrs)
    while k+0x18<=len(d):
        s=c.u32(k);ty=c.u32(k+4)
        if ty in TY and TY[ty] in('int32','float','boolean') and k+0x10 in c.ptrs and s:
            t=c.ptr(k+0x10)
            if t+4<=len(d):
                tot+=1
                v=c.u32(t);shared=tg[t]>1 or t in {a for _,_,a in c.syms}
                susp=(TY[ty]=='int32' and v in D and v>0x10000) or (TY[ty]=='boolean' and v not in(0,1,0xffffffff,0xff000000,255)) or (TY[ty]=='float' and v in D) or (shared and t!=0xc)
                if susp:
                    n+=1;out.write('%s %s @%x opt=%s %s target@%x value=%08x(%s) shared=%s\n'%(o,rel,k,nm(s),TY[ty],t,v,D.get(v,''),shared))
        k+=4
print('options',tot,'suspicious',n)
