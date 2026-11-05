from common import *
from pakcmp import load,detect
I=detect.ps3idx()
c=DC(eff('dc1/ps4/ss-multiplayer/ss-survival.bin')[1])
for s,ty,o in c.syms:
  if ty==0x372cf7e:
    n=c.u32(o);p=c.ptr(o+8);print(nm(s),[nm(c.u32(p+4*i)) for i in range(n)])
a=load(I['fort-multi-shade-npc-ingame12.pak'][0].read(I['fort-multi-shade-npc-ingame12.pak'][1]))
f,o=detect.eff('fort-multi-shade-npc-ingame12.pak');b=load(f)
names=sorted(a[4]);ex=sorted(set(b[4])-set(a[4]))
print('ps3',names);print('ps4',sorted(b[4]))
print('tags sample',{hex(k):v for k,v in a[4][names[3]][0]['tv'].items()})
print({nm(k):v for k,v in a[4][names[3]][0]['tv'].items()})
sids={sid(n):n for n in names+ex}
pos={}
for rel,(o,f) in tree('dc1/ps4').items():
  if f.endswith('.bin') and 'render' not in rel:
    d=open(f,'rb').read()
    for k,v in sids.items():
      if struct.pack('<I',k) in d:pos.setdefault(v,[]).append(rel.split('/')[-1])
print('dc refs by spawner',{k:v[:4] for k,v in pos.items()})
for pk in ('world-multiplayer-ingame12.pak','coop-shade-all-ingame12.pak','yemen-multi-hunter-pak-ingame12.pak','fort-multi-hunter-pak-ingame12.pak','fort-multi-stage-ingame12.pak'):
  r=detect.eff(pk);print(pk,r[1])
  if r[0]:
    d=open(r[0],'rb').read();print('   npc-shade strings',sorted(set(re.findall(rb'npc-shade[a-z0-9-]*',d)))[:6],len(set(re.findall(rb'npc-[a-z0-9-]*',d))),sorted(set(re.findall(rb'npc-[a-z0-9-]*',d)))[:6])
