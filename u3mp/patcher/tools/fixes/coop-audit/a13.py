from common import *
b=DC(eff('dc1/ps4/net.bin')[1])
print(b.bysid.get(0xe4e6147))
s=struct.pack('<I',0xe4e6147);k=-1;hits=[]
while True:
  k=b.d.find(s,k+1)
  if k<0:break
  hits.append(k)
print([hex(h) for h in hits][:6],len(hits))
# looks like table of levels: dump around first non-entry
o=b.bysid[0x3b9a067d][1];x=b.ptr(o+8)
for h in hits:
  if x<=h<x+247*0xd0:continue
  print(hex(h),[hex(b.u32(h+q))+('P' if h+q in b.ptrs else '') for q in range(-0x10,0x80,4)]);
  for q in range(-0x10,0x90,4):
    if h+q in b.ptrs:
      t=b.ptr(h+q);z=b.cstr(t);print('  ',hex(q),z[:40] if z and z.isprintable() and len(z)>2 else hex(t),[hex(b.u32(t+w)) for w in range(0,16,4)])
  break
z=b.bysid.get(0x3b9a067d);
txt=open(eff('dc1/ps4/net.bin')[1],'rb').read()
for w in (b'yemen-multi-hunter-pak',b'fort-multi-shade-npc',b'world-coop-adv-dig'):
  print(w,[hex(m.start()) for m in re.finditer(w,txt)])
  i=txt.find(w);pv=struct.pack('<Q',i)
  j=txt.find(pv);print('  ref',hex(j),[hex(b.u32(j+q))+('P' if j+q in b.ptrs else '') for q in range(-0x28,0x38,4)])
