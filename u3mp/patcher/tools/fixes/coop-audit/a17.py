from common import *
T=tree('dc1/ps4')
for rel,(o,f) in T.items():
  if not f.endswith('.bin') or 'render' in rel:continue
  c=DC(f)
  for s in (0x823ef744,0x505c8ba9):
    if s in c.bysid:print(rel,o,nm(s),nm(c.bysid[s][0]),hex(c.bysid[s][1]))
c=DC(eff('dc1/ps4/ss-multiplayer/ss-survival.bin')[1])
def dumpgrp(c,o,depth=0,pref=''):
  n=c.u32(o);p=c.ptr(o+8) if o+8 in c.ptrs else None
  print(pref,'n',n,hex(p) if p else None)
for s,ty,o in c.syms:
  print(nm(s),nm(ty),hex(o),c.u32(o),[hex(c.u32(o+q)) for q in range(0,16,4)])
