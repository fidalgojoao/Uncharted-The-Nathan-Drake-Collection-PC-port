"""Data-side coverage: for every say/wait-say/say-pre-roll line in the co-op adventure scripts, is the vox entry in the PS4 tables
(retail + overlay) and is its xvag present (overlay loose speech/english or english_ps4.psarc)?"""
import sys,os,struct,pickle,collections,re
sys.path.insert(0,'{{PAKTOOLS}}')
sys.path.insert(0,'{{TRANSFER_TOOLS}}')
from dc import DC
P="{{PROJECT}}/"
R="{{PS4}}/u3data/build/main/"
O=P+"work/overlay-r3/u3data/build/main/"
D=dict(pickle.load(open(P+'scratch/live/coop-tmp/dict.pkl','rb')));nm=lambda v:D.get(v,'%08x'%v)
def voxsyms(f):
    c=DC(f);o={};S=sorted(c.syms,key=lambda x:x[2])
    for k,(s,t,off) in enumerate(S):
        if t!=0xe3825e8d:continue
        e=S[k+1][2] if k+1<len(S) else off+0x200
        names=[c.cstr(c.ptr(p)) for p in range(off,min(e,off+0x400),8) if p in c.ptrs and re.fullmatch(r'[a-z0-9-]{4,}',c.cstr(c.ptr(p)) or '')]
        o[s]=(os.path.basename(f),names)
    return o
V={}
for base in (R,O):
    for rel in ('dc1/ps4/vox.bin','dc1/ps4/vox-mp.bin','dc1/ps4/vox-igc.bin'):
        if os.path.exists(base+rel):V.update(voxsyms(base+rel))
    for sub in ('dc1/ps4/vox','dc1/ps4/vox-mp','dc1/ps4/vox-mp/characters','dc1/ps4/vox/characters'):
        if os.path.isdir(base+sub):
            for x in os.listdir(base+sub):
                if x.endswith('.bin'):V.update(voxsyms(base+sub+'/'+x))
print('vox entries',len(V))
loose=set(x[:-5] for x in os.listdir(O+'sound1/streams/speech/english')) if os.path.isdir(O+'sound1/streams/speech/english') else set()
arc=set()
try:
    from psarc_lib import Psarc
    ps=Psarc(R+'sound1/streams/speech/english_ps4.psarc')
    for n in (ps.names if hasattr(ps,'names') else [e.name for e in ps.entries]):arc.add(os.path.basename(n).replace('.xvag',''))
except Exception as e:print('psarc:',e)
print('loose',len(loose),'archive',len(arc))
SAY={0x235cfa87:'say',0x95f6b8dd:'wait-say',0x159dcdd2:'say-pre-roll'}
for n in sorted(os.listdir(R+'dc1/ps4')):
    if not (n.startswith('ss-coop-adv') and n.endswith('.bin')):continue
    f=O+'dc1/ps4/'+n if os.path.exists(O+'dc1/ps4/'+n) else R+'dc1/ps4/'+n
    d=open(f,'rb').read();lines={};
    for i in range(0,len(d)-24,4):
        w=struct.unpack_from('<I',d,i)[0]
        if w in SAY:
            a=struct.unpack_from('<I',d,i+8)[0];v=struct.unpack_from('<I',d,i+16)[0];lines.setdefault(v,set()).add(nm(a))
    miss=[];nofile=[];ok=0
    for v,sp in lines.items():
        if v not in V:miss.append((nm(v),sorted(sp)));continue
        src,names=V[v]
        bad=[x for x in names if x not in loose and x not in arc]
        if names and bad:nofile.append((names[0],src))
        else:ok+=1
    print('%-28s %s lines=%3d ok=%3d vox-missing=%d file-missing=%d'%(n,'ov' if f.startswith(O) else 'rt',len(lines),ok,len(miss),len(nofile)),miss[:4],nofile[:3])
