import sys,re,struct,json
sys.path.insert(0,r"{{SCRATCH}}/gh2/u3mp/paktools")
from dc import DC,sid
D={int(k,16):v for k,v in json.load(open("coop-vo/dict.json")).items()}
R="{{PS4}}/u3data/build/main/dc1/ps4/vox.bin"
O="{{PROJECT}}/work/overlay-r3/u3data/build/main/dc1/ps4/vox.bin"
r=open(R,'rb').read();o=open(O,'rb').read()
print('diff',[(hex(i),hex(r[i]),hex(o[i])) for i in range(len(r)) if r[i]!=o[i]][:30])
for lab,f in(('ps4',O),('ps3','coop-vo/p3_vox.bin')):
  c=DC(f);print(lab,len(c.syms),[ (D.get(s,hex(s)),hex(t),hex(off)) for s,t,off in c.syms][:6])
  e=c.bysid.get(0x9441df14)
  print('table',e)
  if not e:continue
  t,off=e;n=c.u32(off);p=c.ptr(off+c.ps)
  print('n',n,hex(p),hex(c.u32(off-4)))
  sz=(0x28 if c.ps==8 else 0x20)
  rows=[]
  for i in range(n):
    q=p+i*sz
    w=[c.u32(q+k) for k in range(0,sz,4)]
    ss=[(D.get(x) or (hex(x) if x else '0')) for x in w]
    rows.append(ss)
  json.dump(rows,open('coop-vo/vox_%s.json'%lab,'w'))
  for s in rows:
    if any(re.search(r'coop|drake|sullivan|chloe|elena|cutter|hero',str(x)) for x in s):print(s)
