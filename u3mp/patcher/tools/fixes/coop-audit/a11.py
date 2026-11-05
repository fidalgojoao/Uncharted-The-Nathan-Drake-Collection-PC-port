"""explicit stage-field comparison for all coop entries, PS3 1.19 vs effective net.bin"""
from common import *
import collections
a=DC(AUD+'p3/p18/net.bin');b=DC(eff('dc1/ps4/net.bin')[1])
oa=a.bysid[0x3b9a067d][1];na=a.u32(oa);xa=a.ptr(oa+4);EA=0xe4
ob=b.bysid[0x3b9a067d][1];nb=b.u32(ob);xb=b.ptr(ob+8);EB=0xd0
Ea={a.u32(xa+EA*i):xa+EA*i for i in range(na)};Eb={b.u32(xb+EB*i):xb+EB*i for i in range(nb)}
F=[('sid',0,0),('lvl',4,4),('s8',8,8),('18',0x14,0x18),('1c',0x18,0x1c),('20',0x1c,0x20),('24',0x20,0x24),('kind',0x24,0x28),('script',0x28,0x2c),('2c',0x2c,0x30),('t',0x30,0x34),('ff',0x34,0x38),('38',0x38,0x3c),('58',0x58,0x60),('5c',0x5c,0x64),('60',0x60,0x68),('64',0x64,0x6c),('68',0x68,0x70)]
C=collections.Counter();ex={}
def names(c,s,cnt_off,ptr_off):
  n=c.u32(s+cnt_off);
  if s+ptr_off not in c.ptrs:return None
  p=c.ptr(s+ptr_off);return [c.u32(p+4*i) for i in range(max(n,0))]
out=open(AUD+'net-stages.txt','w')
for k,e in Eb.items():
  if b.u32(e+8)!=0x6f96651f or k not in Ea:continue
  p=Ea[k];La=a.ptr(p+0x48);Lb=b.ptr(e+0x48);ca=a.u32(La+4);cb=b.u32(Lb+8);sa=a.ptr(La+8);sb=b.ptr(Lb+0x10)
  out.write('%s %s n=%d\n'%(nm(k),nm(a.u32(p+4)),ca))
  for i in range(ca):
    A=sa+0x70*i;B=sb+0x78*i
    for f,x,y in F:
      if a.u32(A+x)!=b.u32(B+y):C[(f,a.u32(A+x),b.u32(B+y))]+=1;ex[f]=(nm(k),i)
    pa=a.ptr(A+0x10);c3=a.u32(A+0xc);pb=b.ptr(B+0x10);c4=b.u32(B+0xc)
    # count real entries in ps3 list up to a cut
    la=[a.u32(pa+4*j) for j in range(max(c3,8))];lb=[b.u32(pb+4*j) for j in range(c4)]
    if c3:
      if la[:c3]!=lb:C[('list',c3,c4)]+=1;ex['list']=(nm(k),i,[nm(x) for x in la[:c3]],[nm(x) for x in lb])
    else:
      C[('ps3cnt0-ps4',c4,la[0]==lb[0] if lb else None)]+=1
    out.write('  %2d %-46s c3=%d c4=%d list=%s kind=%s lb=%s\n'%(i,nm(a.u32(A+0x28)),c3,c4,[nm(x) for x in lb],nm(a.u32(A+0x24)),nm(a.u32(A))))
for k,v in C.most_common(40):print(k,v)
print(ex)
