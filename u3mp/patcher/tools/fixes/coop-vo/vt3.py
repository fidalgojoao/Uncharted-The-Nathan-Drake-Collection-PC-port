import struct,re,json,collections
d=open("{{PROJECT}}/outputs/builds/lan-lobby-v7.29/boot-linked.bin",'rb').read()
o=0x1e6988-0x10
print(d[o:o+24].hex(),struct.unpack_from('<QQQ',d,o))
# walk both directions
s=o
while s>=24:
  a,i,v=struct.unpack_from('<QQQ',d,s-24)
  if i&0xffffffff!=8:break
  s-=24
e=o
while e+24<len(d):
  a,i,v=struct.unpack_from('<QQQ',d,e)
  if i&0xffffffff!=8:break
  e+=24
print(hex(s),hex(e),(e-s)//24)
M={}
for p in range(s,e,24):
  a,i,v=struct.unpack_from('<QQQ',d,p);M[a]=v
json.dump({hex(k):v for k,v in M.items()},open('rela.json','w'))
