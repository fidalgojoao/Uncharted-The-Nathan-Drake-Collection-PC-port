from common import *
import hashlib,pickle
sys.path.insert(0,AUD+'../recovered/coop-regions')
import detect
I=detect.ps3idx()
txt=open(eff('pak23_ps4.txt')[1],encoding='latin-1').read()
def blk(n):
  m=re.search(r'(?m)^level-name '+re.escape(n)+r'\n(.*?)(?=^level-name )',txt,re.S);return m.group(1) if m else None
for z in ('yemen','fort','gompa','shambhala','village','warzone','ruins'):
  n=z+'-multi-shade-npc'
  f,o=detect.eff(n+'.pak');d=open(f,'rb').read()
  b=[l for l in (blk(n) or '').split('\n') if l and not l.startswith(('level-id','package-ts'))]
  print(z,o,len(d),hashlib.md5(d).hexdigest()[:8],len(b),hashlib.md5('\n'.join(sorted(x for x in b if z not in x)).encode()).hexdigest()[:6],[x for x in b if not x.startswith(('package ','symbol'))][:6], [x for x in b if x.startswith('symbol')][:3])
  # stored ingame size vs dci/manifest references?
print(blk('fort-multi-shade-npc')[:600])
print('---',blk('yemen-multi-shade-npc')[:600])
rx=re.compile(rb'[a-z0-9*][a-z0-9*-]{4,}')
pk='coop-shade-all-ingame12.pak'
a=set(rx.findall(I[pk][0].read(I[pk][1])));b=set(rx.findall(open(detect.eff(pk)[0],'rb').read()))
print('coop-shade-all',detect.eff(pk)[1],len(a),len(b),'3-4',sorted(x for x in a-b if x[1:] not in b and x[2:] not in b)[:40],'4-3',sorted(x for x in b-a)[:20])
print('bm npc refs PS4',sorted(x.decode() for x in b if x.startswith(b'npc-')))
print('bm groups',sorted(x.decode() for x in b if x.startswith(b'bm-'))[:50],len([x for x in b if x.startswith(b'bm-')]))
