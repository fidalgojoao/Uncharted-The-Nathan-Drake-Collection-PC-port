from common import *
import collections
r=DC(RET+'dc1/ps4/net.bin');b=DC(eff('dc1/ps4/net.bin')[1])
for nmz,c in(('retail',r),('eff',b)):
  o=c.bysid[0x3b9a067d][1];n=c.u32(o);x=c.ptr(o+8);H=collections.Counter()
  for i in range(n):
    e=x+0xd0*i
    if e+0x48 not in c.ptrs:continue
    L=c.ptr(e+0x48);cnt=c.u32(L+8)
    if L+0x10 not in c.ptrs:continue
    s=c.ptr(L+0x10)
    for k in range(cnt):
      S=s+0x78*k;H[(hex(c.u32(e+8))[:6],c.u32(S+0xc),S+0x10 in c.ptrs)]+=1
  print(nmz,n,H)
print([nm(r.u32(r.ptr(r.bysid[0x3b9a067d][1]+8)+0xd0*i)) for i in range(r.u32(r.bysid[0x3b9a067d][1]))][-30:])
a=DC(AUD+'p3/p18/net.bin');o=a.bysid[0x3b9a067d][1];x=a.ptr(o+4);H=collections.Counter()
for i in range(a.u32(o)):
  e=x+0xe4*i
  if e+0x48 in a.ptrs:
    L=a.ptr(e+0x48)
    if L+8 in a.ptrs:
      s=a.ptr(L+8)
      for k in range(a.u32(L+4)):
        S=s+0x70*k;H[(hex(a.u32(e+8))[:6],a.u32(S+0xc),S+0x10 in a.ptrs,a.u32(S+0x14))]+=1
print('ps3',H)
