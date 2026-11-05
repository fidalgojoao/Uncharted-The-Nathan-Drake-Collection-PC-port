"""map -> package lists from net.bin level records"""
from common import *
b=DC(eff('dc1/ps4/net.bin')[1]);a=DC(AUD+'p3/p18/net.bin')
o=b.bysid[0x3b9a067d][1];n=b.u32(o);x=b.ptr(o+8);ES=0xd0
def isstr(t):
  z=b.cstr(t);return z if z and len(z)>2 and all(32<=ord(c)<127 for c in z) else None
# find level records: any 8-aligned+0 sid followed by 'net-levels' string pointer at +0x10
recs={}
for q in sorted(b.ptrs):
  t=b.ptr(q)
  z=isstr(t)
  if z and z.startswith('net-levels') :
    recs[b.u32(q-0x10)]=q-0x10
print(len(recs))
# record length: gap
hs=sorted(recs.values());print(hs[:8],[hs[i+1]-hs[i] for i in range(7)])
def collect(h,ln=0x98):
  S=[]
  for q in range(h,h+ln,8):
    if q in b.ptrs:
      t=b.ptr(q);z=isstr(t)
      if z:S.append(z)
      else:
        c=b.u32(t);
        if 0<c<64 and t+8 in b.ptrs:
          a2=b.ptr(t+8)
          for j in range(c):
            if a2+16*j in b.ptrs and isstr(b.ptr(a2+16*j)):S.append("["+isstr(b.ptr(a2+16*j))+"]")
  return S
f=open('map-paks.txt','w')
import pickle
M={}
for i in range(n):
  e=x+ES*i
  if b.u32(e+8)!=0x6f96651f:continue
  lv=b.u32(e+4);r=recs.get(lv)
  S=collect(r) if r else None
  M[nm(b.u32(e))]=(lv,S)
  f.write('%s lvl=%s kind=%s\n   %s\n'%(nm(b.u32(e)),nm(lv),hex(b.u32(e+0x2c)),' '.join(S) if S else 'NO-RECORD'))
pickle.dump(M,open('map-paks.pkl','wb'))
