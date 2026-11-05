from common import *
a=DC(AUD+'p3/p18/net.bin');o=a.bysid[0x3b9a067d][1];n=a.u32(o);arr=a.ptr(o+4);ES=0xe4
for i in range(n):
  e=arr+ES*i
  if a.u32(e)==0x8a746a66:
    print(i,hex(e),[('%x'%k,hex(a.ptr(e+k))) for k in range(0,ES,4) if e+k in a.ptrs])
    for k in range(0,ES,4):
      print(hex(k),hex(a.u32(e+k)),'P' if e+k in a.ptrs else '',nm(a.u32(e+k)))
    pp=[k for k in range(0,ES,4) if e+k in a.ptrs]
    for k in pp:
      t=a.ptr(e+k);print('->',hex(k),[hex(a.u32(t+q))+('P' if t+q in a.ptrs else '') for q in range(0,0x20,4)])
