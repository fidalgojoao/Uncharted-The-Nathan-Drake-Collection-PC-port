"""net.bin game tables PS3 1.19 (p18) vs effective: entries by name, top fields, stage arrays by parallel walk"""
from common import *
import pwalk,collections
a=DC(AUD+'p3/p18/net.bin');b=DC(eff('dc1/ps4/net.bin')[1])
oa=a.bysid[0x3b9a067d][1];na=a.u32(oa);xa=a.ptr(oa+4);EA=0xe4
ob=b.bysid[0x3b9a067d][1];nb=b.u32(ob);xb=b.ptr(ob+8);EB=0xd0
Ea={a.u32(xa+EA*i):xa+EA*i for i in range(na)};Eb={b.u32(xb+EB*i):xb+EB*i for i in range(nb)}
coop={k:v for k,v in Eb.items() if b.u32(v+8)==0x6f96651f}
print('ps3 entries',na,'ps4',nb,'ps3-mode-coop',sum(1 for v in Ea.values() if a.u32(v+8)==0x6f96651f),'ps4-coop',len(coop))
print('coop in PS3 not PS4:',[nm(k)+'/'+nm(a.u32(v+4)) for k,v in Ea.items() if a.u32(v+8)==0x6f96651f and k not in Eb])
print('coop in PS4 not PS3:',[nm(k) for k in coop if k not in Ea])
tot=collections.Counter()
for k,e in coop.items():
  if k not in Ea:continue
  p=Ea[k]
  for fa,fb,nmf in((4,4,'level'),(8,8,'mode'),(0x2c,0x2c,'kind'),(0x3c,0x3c,'c'),(0x40,0x40,'d'),(0x44,0x44,'e')):
    if a.u32(p+fa)!=b.u32(e+fb):print(nm(k),nmf,nm(a.u32(p+fa)),nm(b.u32(e+fb)))
  La=a.ptr(p+0x48);Lb=b.ptr(e+0x48)
  ca=a.u32(La+4);cb=b.u32(Lb+8)
  if ca!=cb:print(nm(k),'STAGE COUNT',ca,cb)
  w=pwalk.W(a,b);w.walk(k,a.ptr(La+8),b.ptr(Lb+0x10))
  tot['w']+=w.nw;tot['p']+=w.np
  if w.mm:
    print(nm(k),nm(a.u32(p+4)),'stages',ca,'mism',len(w.mm))
    for m in w.mm[:8]:print('   ',pwalk.fmt(m)[:150])
  # header default stage
  if La+0 in a.ptrs or Lb in b.ptrs:
    fa=a.ptr(La) if La in a.ptrs else None;fb=b.ptr(Lb) if Lb in b.ptrs else None
print(tot)
