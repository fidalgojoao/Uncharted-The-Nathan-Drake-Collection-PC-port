from common import *
o,f=eff('dc1/ps4/net.bin');c=DC(f)
a0=c.bysid[0x3b9a067d][1];n=c.u32(a0);a=c.ptr(a0+8);ES=0xd0
for i in range(n):
  e=a+ES*i
  if c.u32(e+8)!=0x6f96651f:continue
  if e+0x48 not in c.ptrs:print(i,'noL');continue
  L=c.ptr(e+0x48);cnt=c.u32(L+8);arr=c.ptr(L+0x10) if L+0x10 in c.ptrs else None
  print(i,nm(c.u32(e)),nm(c.u32(e+4)),hex(c.u32(e+0x2c)),nm(c.u32(e+0x64)),'n',cnt,'first',nm(c.u32(c.ptr(L)+0x2c)) if L in c.ptrs else '')
  if i in(112,):pass
  if arr:
    for k in range(min(30,cnt)):
      s=arr+0x78*k
      print('    ',k,nm(c.u32(s)),'list',[nm(c.u32(c.ptr(s+0x10)+4*j)) for j in range(c.u32(s+0xc))] if s+0x10 in c.ptrs else '',[nm(c.u32(s+q)) for q in (0x18,0x1c,0x20,0x24,0x28,0x2c,0x30,0x34)])
