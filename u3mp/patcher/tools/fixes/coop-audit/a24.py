from common import *
a=DC(AUD+'p3/p18/net.bin');b=DC(eff('dc1/ps4/net.bin')[1])
oa=a.bysid[0x3b9a067d][1];na=a.u32(oa);xa=a.ptr(oa+4);EA=0xe4
ob=b.bysid[0x3b9a067d][1];nb=b.u32(ob);xb=b.ptr(ob+8);EB=0xd0
Ea={a.u32(xa+EA*i):xa+EA*i for i in range(na)}
for i in range(nb):
  e=xb+EB*i
  if b.u32(e+8)!=0x6f96651f:continue
  k=b.u32(e);p=Ea[k];La=a.ptr(p+0x48);Lb=b.ptr(e+0x48)
  da=a.ptr(La) if La in a.ptrs else None;db=b.ptr(Lb) if Lb in b.ptrs else None
  sa=a.u32(da+0x28) if da else None;sb=b.u32(db+0x2c) if db else None
  extra=[hex(a.u32(La+q)) for q in (0xc,0x10,0x14,0x18,0x1c,0x20,0x24)]
  extb=[hex(b.u32(Lb+q)) for q in (0x18,0x20,0x24,0x28,0x30,0x38,0x40)]
  print(nm(k)[:26],nm(sa) if sa is not None else None,nm(sb) if sb is not None else None,'' if sa==sb else 'DIFF', 'sid',nm(a.u32(da)) if da else None, nm(b.u32(db)) if db else None)
