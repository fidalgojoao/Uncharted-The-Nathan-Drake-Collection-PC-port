"""q14 redone: option defaults on every EFFECTIVE dc1 script (overlay>retail) against the PS3 1.19 file (patch18>13>goty>bin).
q14 used bin.psarc only, skipped patch-only files and retail files.  writes defaults-119.txt"""
from common import *
TY={0xc7cb275c:'int32',0x8b4e76ff:'boolean',0xf182ec3:'float',0xc4ab6121:'symbol',0xb3952e7:'string'}
def entries(c):
  d=c.d;e=c.e;out=[];k=0;stride=0x18 if c.ps==8 else 0x10
  while k+stride<=len(d):
    sid_=struct.unpack_from(e+'I',d,k)[0];ty=struct.unpack_from(e+'I',d,k+4)[0]
    po=k+0x10 if c.ps==8 else k+0xc
    if ty in TY and po in c.ptrs and sid_:
      t=c.ptr(po)
      if t and t+4<=len(d):out.append((k,sid_,TY[ty],t,c.u32(t)))
    k+=4
  return out
I=ps3dc();T=tree('dc1/ps4');out=open(AUD+'defaults-119.txt','w');tmp=AUD+'_q.bin'
tot=bad=files=0;un=0
for n in sorted(I):
  if not re.match(r'dc1/(ss/.*|ss-.*)\.bin$',n):continue
  rel='dc1/ps4/'+n[4:]
  if rel not in T:continue
  o,f=T[rel];open(tmp,'wb').write(I[n][0].read(I[n][1]));a=DC(f);b=DC(tmp);files+=1
  ea=entries(a);eb=entries(b);j=0
  for ka,sa,ta,pa,va in ea:
    m=None
    for q in range(j,len(eb)):
      if eb[q][1]==sa and eb[q][2]==ta:m=q;break
    if m is None:un+=1;continue
    j=m+1;tot+=1;vb=eb[m][4]
    if ta=='string':continue
    if va!=vb:
      bad+=1;out.write('%s %s [%s] @%x %s %s ps4=%08x(%s) ps3=%08x(%s)\n'%(o,n,I[n][2],ka,nm(sa),ta,va,dic().get(va,''),vb,dic().get(vb,'')))
s='files %d compared %d unmatched %d bad %d'%(files,tot,un,bad);print(s);out.write(s+'\n')
