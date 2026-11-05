import struct,json,bisect
J=json.load(open('p119.json'));ent=J['ent'];VA,of,SZ=J['seg'][0]
d=open("{{PROJECT}}/scratch/ps3-119/EBOOT.elf",'rb').read()
W=struct.unpack('>%dI'%(SZ//4),d[of:of+SZ-SZ%4])
c=[0x3be620,0xf1b20,0xe85a0,0x65608,0x368e48,0x3d18c8,0x71ce8,0x16dc68,0x6dc46c,0x2f8194,0x1687bc]
for f in c:
  i=bisect.bisect_right(ent,f);e=ent[i]
  ws=W[(f-VA)//4:(e-VA)//4]
  n13=sum(1 for w in ws if w>>26==11 and w&0xffff==13)
  n20=sum(1 for w in ws if w>>26==14 and (w>>16)&31==0 and w&0xffff==20)
  print(hex(f),hex(e-f),'cmpwi13',n13,'li20',n20)
# all functions: with cmpwi 13, li 20 and size>0x2000, and contains 0xa5a63e9b
import re
for k,f in enumerate(ent[:-1]):
  e=ent[k+1];sz=e-f
  if 0x1800<sz<0x6000:
    ws=W[(f-VA)//4:(e-VA)//4]
    a=any(w>>26==24 and w&0xffff==0x3e9b for w in ws)
    if a and any(w>>26==14 and (w>>16)&31==0 and w&0xffff==20 for w in ws):
      print('cand',hex(f),hex(sz))
