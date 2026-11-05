from common import *
b=DC(eff('dc1/ps4/net.bin')[1]);a=DC(AUD+'p3/p18/net.bin')
r=DC(RET+'dc1/ps4/net.bin')
print('retail sym',0x3b9a067d in r.bysid,len(r.d))
if 0x3b9a067d in r.bysid:
  o=r.bysid[0x3b9a067d][1];n=r.u32(o);x=r.ptr(o+8);print('retail entries',n)
  import collections
  c=collections.Counter()
  for i in range(n):
    e=x+0xd0*i
    if e+0x48 in r.ptrs:
      L=r.ptr(e+0x48);cnt=r.u32(L+8);s=r.ptr(L+0x10) if L+0x10 in r.ptrs else 0
      if s:
        for k in range(cnt):
          S=s+0x78*k;c[(r.u32(S+0xc))]+=1
          if i<140 and k<3 and nm(r.u32(e+4))=='dig-adv':print(nm(r.u32(S)),r.u32(S+8),r.u32(S+0xc))
  print('retail stage +c histogram',c)
for nmz in ('cha-multi-shade-pak','warzone-multi-car-coop-pak','warzone-multi-car-pak','grave-multi-comp-pak','san-multi-hunter-pak','ldn-streets-multi-shade-npcs','fort-coop-seawall','yemen-multi-hunter-pak','world-coop-adv-dig'):
  s=sid(nmz);pa=struct.pack('<I',s);pb=struct.pack('>I',s)
  print(nmz,hex(s),'r3 net sid hits',b.d.count(pa),'str',b.d.count(nmz.encode()),'ps3 net',a.d.count(pb),a.d.count(nmz.encode()))
  for t,(o,f) in tree('dc1/ps4').items():
    if f.endswith('.bin') and 'render-settings' not in t:
      d=open(f,'rb').read()
      if nmz.encode() in d or pa in d:print('    in',t,o,d.count(nmz.encode()))
  txt=open(eff('pak23_ps4.txt')[1],encoding='latin-1').read()
  print('    pak23 level-name',('level-name '+nmz+'\n') in txt,'package ref',txt.count('package '+nmz+'-ingame'))
