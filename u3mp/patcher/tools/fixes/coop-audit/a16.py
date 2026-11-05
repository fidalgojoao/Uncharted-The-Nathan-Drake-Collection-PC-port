from common import *
from pakcmp import load,detect
I=detect.ps3idx()
import hashlib
ref=None
for n in ('fort','gompa','shambhala','village','warzone','yemen','syria','air','cha','desert','facility','san'):
  nmz=n+'-multi-shade-npc-ingame12.pak'
  f,o=detect.eff(nmz)
  a=load(I[nmz][0].read(I[nmz][1]));b=load(f)
  sa={k:v[0] for k,v in a[4].items()};sb={k:v[0] for k,v in b[4].items()}
  print(n,o,'size',os.path.getsize(f),'ps3',len(sa),'ps4',len(sb),'extra',sorted(set(sb)-set(sa))[:4],'mtime',__import__('time').strftime('%m-%d %H:%M',__import__('time').localtime(os.path.getmtime(f))))
  rp=os.path.join(RET,'pak23/ps4',nmz);print('   retail',os.path.exists(rp),os.path.getsize(rp) if os.path.exists(rp) else 0)
  if ref is None:ref=(n,sa)
  # same templates across maps?
  same=sum(1 for k in sa if k in ref[1] and sa[k]['tv']==ref[1][k]['tv'])
  print('   same tag sets vs',ref[0],same,'/',len(sa))
sc=set()
for rel,(o,f) in tree('dc1/ps4').items():
  if f.endswith('.bin') and 'render' not in rel:
    d=open(f,'rb').read()
    for w in set(re.findall(rb'npc-(?:shade|kickback)[a-z0-9-]*',d)):sc.add((rel,w.decode()))
print(sorted(sc)[:30])
for w in ('npc-shade-mark-ak-1','npc-shade-ak-1','npc-kickback-grappler-1','npc-shade-armor-1'):
  s=struct.pack('<I',sid(w));hits=[]
  for rel,(o,f) in tree('dc1/ps4').items():
    if f.endswith('.bin') and 'render' not in rel:
      d=open(f,'rb').read()
      if s in d:hits.append((rel.split('/')[-1],o))
  print(w,hex(sid(w)),hits)
