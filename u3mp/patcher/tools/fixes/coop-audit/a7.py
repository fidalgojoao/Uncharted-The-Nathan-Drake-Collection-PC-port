from common import *
print(sorted(set(n for n in tree('dc1/ps4') if n.endswith('.bin') and 'net' in n))[:10])
# compare net.bin stage tables PS3 p18 vs effective
a=DC(AUD+'p3/p18/net.bin');b=DC(eff('dc1/ps4/net.bin')[1])
print(a.ps,b.ps,0x3b9a067d in a.bysid, a.u32(a.bysid[0x3b9a067d][1]),b.u32(b.bysid[0x3b9a067d][1]))
import pwalk
w=pwalk.W(a,b)
s=0x3b9a067d;w.walk(s,a.bysid[s][1],b.bysid[s][1])
print('words',w.nw,'ptrs',w.np,'mism',len(w.mm))
import collections
for m in w.mm[:60]:print(pwalk.fmt(m)[:170])
print(len(w.mm))
