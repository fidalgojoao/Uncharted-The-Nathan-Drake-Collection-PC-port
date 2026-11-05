from common import *
import collections
o,f=eff('dc1/ps4/net.bin');c=DC(f)
a0=c.bysid[0x3b9a067d][1];n=c.u32(a0);a=c.ptr(a0+8);ES=0xd0
M=collections.defaultdict(list)
for i in range(n):
  e=a+ES*i
  M[c.u32(e+8)].append(i)
print(n)
for m,l in M.items():print(nm(m),hex(m),len(l),[nm(c.u32(a+ES*l[0])),nm(c.u32(a+ES*l[0]+4))])
for i in range(n):
  e=a+ES*i
  nm0=nm(c.u32(e));
  if re.search('COOP|HUNT|HORDE|ARENA',nm0,re.I) or re.search('coop|hunt|horde|arena',nm(c.u32(e+8))):
    L=c.ptr(e+0x48) if e+0x48 in c.ptrs else None
    sc=[(hex(k),nm(c.u32(e+k))) for k in (0x2c,0x3c,0x40,0x50,0x54,0x58,0x5c,0x60,0x64) if c.u32(e+k)]
    print(i,nm0,nm(c.u32(e+4)),nm(c.u32(e+8)),sc,hex(L) if L else '')
