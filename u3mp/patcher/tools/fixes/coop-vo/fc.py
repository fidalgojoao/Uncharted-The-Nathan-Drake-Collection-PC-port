import struct,json,bisect,sys,collections
J=json.load(open('p119.json'));ent=J['ent'];VA,of,SZ=J['seg'][0]
d=open("{{PROJECT}}/scratch/ps3-119/EBOOT.elf",'rb').read()
W=struct.unpack('>%dI'%(SZ//4),d[of:of+SZ-SZ%4])
def find(t):
  h=t>>16;l=t&0xffff;a2=(h+1)&0xffff if l&0x8000 else h;out=[]
  for i,w in enumerate(W):
    op=w>>26;imm=w&0xffff
    if (op==15 and imm in(h,a2)) or (op in(25,27) and imm==h):
      for k in range(0,10):
        x=W[i+k]
        if x&0xffff==l and x>>26 in(24,14,26):
          a=VA+i*4;out.append((a,ent[bisect.bisect_right(ent,a)-1]));break
  return out
sets={}
for t in map(lambda s:int(s,16),sys.argv[1:]):
  r=find(t);sets[t]=set(f for _,f in r);print(hex(t),[hex(a) for a,_ in r][:12],[hex(f) for _,f in r][:12])
print('common',[hex(x) for x in set.intersection(*sets.values())])
