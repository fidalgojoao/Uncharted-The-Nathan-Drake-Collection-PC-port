import struct,re
d=open("{{PROJECT}}/outputs/builds/lan-lobby-v7.29/boot-linked.bin",'rb').read()
for a in (0xc86090,0xc86080,0x3c8dd0,0x4b3610):
  p=struct.pack('<Q',a);L=[m.start() for m in re.finditer(re.escape(p),d)]
  print(hex(a),[hex(x) for x in L[:6]])
