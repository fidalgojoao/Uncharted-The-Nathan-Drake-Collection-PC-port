from common import *
a=DC(AUD+'p3/p18/net.bin');b=DC(eff('dc1/ps4/net.bin')[1])
oa=a.bysid[0x3b9a067d][1];na=a.u32(oa);xa=a.ptr(oa+4);EA=0xe4
ob=b.bysid[0x3b9a067d][1];nb=b.u32(ob);xb=b.ptr(ob+8);EB=0xd0
Ea={a.u32(xa+EA*i):xa+EA*i for i in range(na)};Eb={b.u32(xb+EB*i):xb+EB*i for i in range(nb)}
k=0x8a746a66
sa=a.ptr(a.ptr(Ea[k]+0x48)+8);sb=b.ptr(b.ptr(Eb[k]+0x48)+0x10)
for st in (1,2):
  A=sa+0x70*st;B=sb+0x78*st
  print('PS3');print([('%x'%q,hex(a.u32(A+q))+('P' if A+q in a.ptrs else '')) for q in range(0,0x70,4)])
  print('PS4');print([('%x'%q,hex(b.u32(B+q))+('P' if B+q in b.ptrs else '')) for q in range(0,0x78,4)])
# also mismatch type summary of first entry head of ptr at L+0
La=a.ptr(Ea[k]+0x48);Lb=b.ptr(Eb[k]+0x48);print(hex(a.u32(La)),hex(a.u32(La+4)),hex(b.ptr(Lb)),[hex(b.u32(Lb+q)) for q in range(0,0x48,4)])
