import sys,re,struct,json,os,collections
sys.path.insert(0,"gh2/u3mp/paktools")
from dc import DC,sid
D={int(k,16):v for k,v in json.load(open("coop-vo/dict.json")).items()}
R="{{PS4}}/u3data/build/main/dc1/ps4/"
O="{{PROJECT}}/work/overlay-r3/u3data/build/main/dc1/ps4/"
L=json.load(open('coop-vo/lists.json'))
b=lambda K:{os.path.basename(n).replace('.xvag','') for n in L[K]}
p4,ov,mp,pa=b('ps4'),b('ov'),b('mp'),b('dlc-patch13.psarc')
def syms(f):
  c=DC(f);o={}
  S=sorted(c.syms,key=lambda x:x[2])
  for k,(s,t,off) in enumerate(S):
    if t!=0xe3825e8d:continue
    e=S[k+1][2] if k+1<len(S) else off+0x200
    o[s]=[c.cstr(c.ptr(p)) for p in range(off,e,8) if p in c.ptrs and re.fullmatch(r'[a-z0-9-]{4,}',c.cstr(c.ptr(p)) or '')]
  return o
V={}
for f in ('vox-mp.bin','vox-igc.bin'):V.update(syms(R+f))
vr=os.path.join(R,'vox-mp/characters')
for x in os.listdir(vr):
  if x.endswith('.bin'):V.update(syms(os.path.join(vr,x)))
if os.path.exists(O+'vox.bin'):V.update(syms(O+'vox.bin'))
print('vox syms',len(V))
SAY={sid('say'),sid('wait-say'),sid('say-pre-roll')}
out={}
names=[n for n in os.listdir(R) if n.startswith('ss-coop-') and n.endswith('.bin')]+['ss-multiplayer/'+n for n in os.listdir(R+'ss-multiplayer') if n.endswith('.bin') and 'igc' in n]
for n in sorted(names):
  f=O+n if os.path.exists(O+n) else R+n
  d=open(f,'rb').read()
  tot=miss=0;nofile=0;sp=collections.Counter();miss_l=[];nf=[]
  for i in range(0,len(d)-24,4):
    if struct.unpack_from('<I',d,i)[0] in SAY:
      a,v=struct.unpack_from('<II',d,i+8) if False else (struct.unpack_from('<I',d,i+8)[0],struct.unpack_from('<I',d,i+16)[0])
      if D.get(a,'').startswith('coop-') or a==sid('self') or D.get(a):
        tot+=1;sp[D.get(a,hex(a))]+=1
        if v not in V:miss+=1;miss_l.append(hex(v))
        else:
          for ln in V[v]:
            if ln not in p4 and ln not in ov:nofile+=1;nf.append(ln)
  print(n,'ov' if f.startswith(O) else 'rt','say',tot,'vox-missing',miss,'file-missing',nofile,dict(sp.most_common(6)),miss_l[:3],nf[:3])
