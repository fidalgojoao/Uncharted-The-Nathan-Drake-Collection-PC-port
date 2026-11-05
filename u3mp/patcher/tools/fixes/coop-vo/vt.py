import struct,re,sys,collections
d=open("{{PROJECT}}/outputs/builds/lan-lobby-v7.29/boot-linked.bin",'rb').read()
ds=0x257958
# scan 8-aligned relative to VA across file
lo,hi=0x4a0000,0x4c0000
start=ds%8
cand=collections.defaultdict(list)
for o in range(0x1000000,len(d)-8,8):
  pass
# pointer alignment: try VA=o-ds aligned 8
o=(0x1200000+ds)&~7
o+= (ds%8)
tabs={}
run=[]
while o<len(d)-8:
  v=struct.unpack_from('<Q',d,o)[0]
  if 0x100000<v<0x1400000:
    run.append((o-ds,v))
  else:
    if len(run)>=25:
      n=[x for x in run if lo<=x[1]<hi]
      if len(n)>=3:
        tabs[run[0][0]]=run
    run=[]
  o+=8
print(len(tabs))
for a,r in tabs.items():
  print(hex(a),len(r),'[+0x98]=',hex(r[19][1]) if len(r)>19 else '', 'first',[hex(x[1]) for x in r[:6]])
