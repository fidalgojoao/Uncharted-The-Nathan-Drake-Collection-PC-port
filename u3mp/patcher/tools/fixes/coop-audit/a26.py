"""state-script tags on spawners/regions of every coop-map pak must resolve to an effective DC symbol; PS3 baseline for comparison"""
from common import *
import pickle
from pakcmp import load,detect
I=detect.ps3idx();M=pickle.load(open(AUD+'map-paks.pkl','rb'))
names=set()
for k,(lv,S) in M.items():names|={z.strip('[]') for z in S if not z.startswith('net-levels') and '.' not in z}
SY=set();T=tree('dc1/ps4')
for rel,(o,f) in T.items():
  if f.endswith('.bin') and 'render-settings' not in rel:
    try:SY|={s for s,_,_ in DC(f).syms}
    except:pass
P3=set()
for n,(p,i,src) in ps3dc().items():
  if 'render-settings' in n:continue
  d=p.read(i)
  if d[:4]==b'DC00':
    open(AUD+'_m.bin','wb').write(d);P3|={s for s,_,_ in DC(AUD+'_m.bin').syms}
print(len(SY),len(P3),'crane',0x113c6beb in SY,0x113c6beb in P3,'heli-roles',0x3f7c0145 in SY,0x3f7c0145 in P3)
KEY=0x45c51f9f;bad={}
tot=0
for z in sorted(names):
  pk=z+'-ingame12.pak';f,o=detect.eff(pk)
  if not f:continue
  try:b=load(f)
  except Exception as e:print('ERR',pk,e);continue
  for n,l in b[4].items():
    for sp in l:
      v=sp['tv'].get(KEY)
      if v and isinstance(v[2],tuple) and v[2][0]=='str':
        tot+=1;s=v[2][1].decode()
        if sid(s) not in SY:bad.setdefault(s,[]).append((z,'sp:'+str(n),sid(s) in P3))
  for n,l in b[1].items():
    for q in l:
      for k,t,v in q['tags']:
        if k==KEY and isinstance(v,str):
          tot+=1
          if sid(v) not in SY:bad.setdefault(v,[]).append((z,'rg:'+str(n),sid(v) in P3))
        elif k==KEY and isinstance(v,int):
          tot+=1
          if v not in SY:bad.setdefault(nm(v),[]).append((z,'rg:'+str(n),v in P3))
      s=q.get('script')
print('checked',tot)
for k,v in sorted(bad.items()):print(k,len(v),'ps3-defined' if v[0][2] else 'undefined-on-ps3-too',v[:3])
