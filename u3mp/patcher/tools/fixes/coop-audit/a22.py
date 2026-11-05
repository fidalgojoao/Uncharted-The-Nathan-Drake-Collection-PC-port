"""all-string diff PS3 vs effective for every -ingame pak on a coop map; shade-npc pak identity"""
from common import *
import pickle,hashlib
sys.path.insert(0,AUD+'../recovered/coop-regions')
import detect
I=detect.ps3idx();M=pickle.load(open(AUD+'map-paks.pkl','rb'))
names=set()
for k,(lv,S) in M.items():names|={z.strip('[]') for z in S if not z.startswith('net-levels') and '.' not in z}
rx=re.compile(rb'[a-zA-Z0-9_\-*!?<>=+:/.]{4,}')
out=open(AUD+'pak-strings.txt','w');n=0
for z in sorted(names):
  pk=z+'-ingame12.pak'
  if pk not in I:out.write('NOPS3 %s effective=%s\n'%(pk,detect.eff(pk)[1]));continue
  f,o=detect.eff(pk)
  a=set(rx.findall(I[pk][0].read(I[pk][1])))
  if not f:out.write('MISSING %s\n'%pk);continue
  b=set(rx.findall(open(f,'rb').read()))
  # drop paths etc.
  def clean(s):return {x for x in s if b'/' not in x and b'.' not in x and not x.startswith((b'g_',b'ND_',b'u3final'))}
  d3=sorted(clean(a-b));d4=sorted(clean(b-a))
  if d3 or d4:
    n+=1
    out.write('%s [%s] %s ps3-only %d: %s\n   ps4-only %d: %s\n'%(pk,o,I[pk][2],len(d3),[x.decode() for x in d3[:40]],len(d4),[x.decode() for x in d4[:25]]))
print(len(names),n)
for z in ('fort','gompa','shambhala','village','warzone','yemen','air','desert','facility','highrise','museum','ruins','oasis','streets','ldn-streets','syria','cha','san'):
  pk=z+'-multi-shade-npc-ingame12.pak'
  f,o=detect.eff(pk);d=open(f,'rb').read();p=I[pk][0].read(I[pk][1])
  r=z+'-multi-shade-npc.pak'
  print(z,o,len(d),hashlib.md5(d).hexdigest()[:8],'ps3',len(p),hashlib.md5(p).hexdigest()[:8],'name-count',d.count(z.encode()),p.count(z.encode()),'nonIngame',detect.eff(r)[1], 'pak23', ('level-name '+z+'-multi-shade-npc') in open(eff('pak23_ps4.txt')[1],encoding='latin-1').read())
