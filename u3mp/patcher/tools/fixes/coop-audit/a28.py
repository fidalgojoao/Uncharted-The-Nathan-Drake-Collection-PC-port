from common import *
f=eff('dc1/ps4/ss-coop-adv-fort.bin')[1];c=DC(f)
Ss=sorted((a,s) for s,t,a in c.syms if t==0x45c51f9f)
for k in (0x3f1a8,0x40130,0x56958,0x58c88,0x5aeb0,0x5cf30,0x67658):
  o=[(a,s) for a,s in Ss if a<=k][-1];print(hex(k),nm(c.u32(k)),'owner',nm(o[1]),hex(o[0]))
