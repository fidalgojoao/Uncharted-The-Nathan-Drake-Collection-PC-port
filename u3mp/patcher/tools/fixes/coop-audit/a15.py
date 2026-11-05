from common import *
b=DC(eff('dc1/ps4/net.bin')[1]);a=DC(AUD+'p3/p18/net.bin')
# who references fort-multi-shade-npc in ps4 net
t=open(eff('dc1/ps4/net.bin')[1],'rb').read()
for w in (b'fort-multi-shade-npc',b'gompa-multi-shade-npc',b'yemen-multi-shade-npc',b'village-multi-shade-npc',b'warzone-multi-shade-npc',b'shambhala-multi-shade-npc',b'syria-multi-shade-npc',b'cha-multi-shade-npc',b'cha-multi-shade-pak',b'ldn-streets-multi-shade-npc',b'streets-multi-shade-npc',b'sham-multi-shade-npc'):
  i=t.find(w)
  refs=[]
  if i>=0:
    pv=struct.pack('<Q',i);j=-1
    while True:
      j=t.find(pv,j+1)
      if j<0:break
      refs.append(hex(j))
  print(w.decode(),'net4' ,hex(i),refs,'ps3net',a.d.count(w))
j=0x6a218
print([ (hex(q),nm(b.u32(q)), b.cstr(b.ptr(q)) if q in b.ptrs and b.cstr(b.ptr(q)) and len(b.cstr(b.ptr(q)))>3 else '') for q in range(j-0x60,j+0x30,8)])
# which symbols contain it
syms=sorted((o,s,ty) for s,ty,o in b.syms)
import bisect
k=bisect.bisect([x[0] for x in syms],j)-1;print('sym',nm(syms[k][1]),nm(syms[k][2]),hex(syms[k][0]))
