from common import *
o,f=eff('dc1/ps4/net.bin');c=DC(f)
print(nm(0x3b9a067d),c.bysid.get(0x3b9a067d))
ps=c.ps;a0=c.bysid[0x3b9a067d][1];n=c.u32(a0);a=c.ptr(a0+8);ES=0xd0
for i in range(n):
  e=a+ES*i
  w=[]
  for k in range(0,ES,4):
    if e+k in c.ptrs:
      t=c.ptr(e+k);s=c.cstr(t)
      w.append('%x->%s'%(k,repr(s[:30]) if s and len(s)>2 and s.isprintable() else hex(t)))
    else:
      v=c.u32(e+k)
      if v:w.append('%x=%s'%(k,nm(v) if v>0xffff else v))
  print(i,' '.join(w)[:260])
  if i>2 and False:break
