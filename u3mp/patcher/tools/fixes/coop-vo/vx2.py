import sys,re,struct,json
sys.path.insert(0,r"{{SCRATCH}}/gh2/u3mp/paktools")
from dc import DC,sid
D={int(k,16):v for k,v in json.load(open("coop-vo/dict.json")).items()}
R="{{PS4}}/u3data/build/main/dc1/ps4/vox.bin"
O="{{PROJECT}}/work/overlay-r3/u3data/build/main/dc1/ps4/vox.bin"
def rows(f):
  c=DC(f);M={}
  for s,t,off in c.syms:
    if t==0x12243393:
      # name ptr at off, char sid
      nm=None;ch=None
      for k in range(0,0x28,c.ps):
        if off+k in c.ptrs:
          v=c.cstr(c.ptr(off+k))
          if v and nm is None:nm=v
      ws=[c.u32(off+k) for k in range(0,0x20,4)]
      ch=[D.get(w) for w in ws if D.get(w)]
      M[nm or hex(s)]=(hex(s),ch)
  return M
A,R4,O4=rows('coop-vo/p3_vox.bin'),rows(R),rows(O)
print(len(A),len(R4),len(O4));print('sample',list(A.items())[:3],list(O4.items())[:3])
print('ps3 only',{k:A[k] for k in A if k not in O4});print('ps4 only',{k:O4[k] for k in O4 if k not in A})
print('char diff',[(k,A[k][1],O4[k][1]) for k in A if k in O4 and A[k][1]!=O4[k][1]][:30])
print('overlay vs retail',[(k,R4.get(k),O4[k]) for k in O4 if R4.get(k)!=O4[k]])
print('hero',[(k,O4[k][1]) for k in sorted(O4) if re.search('mp-|hero|coop',k)])
