from common import *
T=tree('dc1/ps4')
for name in ('ss-crane-region','ss-heli-roles'):
  s=sid(name);hits=[]
  for rel,(o,f) in T.items():
    if f.endswith('.bin') and 'render' not in rel:
      c=DC(f)
      if s in c.bysid:hits.append((rel,o,[nm(x[0]) for x in c.syms if x[1]==0x45c51f9f][:6]))
  print(name,hex(s),hits)
  ps=[]
  for n,(p,i,src) in ps3dc().items():
    if 'render' in n:continue
    try:d=p.read(i)
    except:continue
    if d[:4]==b'DC00' and struct.pack('>I',s) in d[:40000]:ps.append((n,src))
  print('  ps3',ps[:5])
t4=open(eff('pak23_ps4.txt')[1],encoding='latin-1').read()
for lv in ('facility-multi-pak','coop-syria-pak'):
  m=re.search(r'(?ms)^level-name '+lv+r'\n(.*?)(?=^level-name )',t4);print(lv,[l for l in m.group(1).split('\n') if l.startswith('symbol')][:40])
# who uses them in map paks
import pickle
sys.path.insert(0,AUD+'../recovered/coop-regions')
import detect
for pk,w in (('facility-multi-pak-ingame12.pak',b'ss-crane-region'),('coop-syria-pak-ingame12.pak',b'ss-heli-roles')):
  f,o=detect.eff(pk);d=open(f,'rb').read();print(pk,o,'str',d.count(w),'sid',d.count(struct.pack('<I',sid(w.decode()))))
for rel,(o,f) in T.items():
  if f.endswith('.bin') and 'render' not in rel:
    d=open(f,'rb').read()
    for w in (b'ss-crane-region',b'ss-heli-roles'):
      if struct.pack('<I',sid(w.decode())) in d and 'ss-' in rel:print('  ref',w,rel.split('/')[-1],o)
