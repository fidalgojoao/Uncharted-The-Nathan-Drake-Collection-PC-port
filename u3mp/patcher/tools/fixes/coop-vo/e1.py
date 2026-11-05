import sys,struct,re
sys.path.insert(0,"gh2/u3mp/paktools")
from dc import DC,sid
R="{{PS4}}/u3data/build/main/dc1/ps4/vox-mp.bin"
a=DC('coop-vo/p3_vox-mp.bin');c=DC(R)
sa=sorted(a.syms,key=lambda x:x[2]);sc=sorted(c.syms,key=lambda x:x[2])
s=0xd729a788;ta,oa=a.bysid[s];tc,oc=c.bysid[s]
def dm(c,o,n):
  for k in range(0,n,c.ps):
    p=o+k
    if p in c.ptrs:
      v=c.ptr(p);st=c.cstr(v) or ''
      print('%05x P %05x %s'%(p,v,st[:40] if re.fullmatch(r'[ -~]+',st) else ''))
    else:
      print('%05x  '%p,' '.join('%08x'%c.u32(p+j) for j in range(0,c.ps,4)))
print('PS3 shared');dm(a,oa-4,0x60)
print('PS4 shared');dm(c,oc-8,0x70)
fr=[(x,t,o) for x,t,o in a.syms if x not in c.bysid]
print(len(fr))
x,t,o=fr[0];print('PS3 frt',hex(x),hex(t));dm(a,o-4,0x60)
import collections
# sizes
print(collections.Counter(sa[i+1][2]-sa[i][2] for i in range(len(sa)-1)).most_common(6))
print(collections.Counter(sc[i+1][2]-sc[i][2] for i in range(len(sc)-1)).most_common(6))
print(hex(sa[0][2]),hex(sc[0][2]),a.ptr(0xc)if 0 else '',struct.unpack_from('>IIIII',a.d,0),struct.unpack_from('<IIQII',c.d,0))
