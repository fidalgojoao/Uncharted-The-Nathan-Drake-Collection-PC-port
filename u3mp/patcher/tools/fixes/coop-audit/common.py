import sys,os,glob,struct,pickle,re
sys.path.insert(0,'{{PAKTOOLS}}')
from dc import DC,sid
from psarc_lib import Psarc
P='{{PROJECT}}'
RET="{{PS4}}/u3data/build/main/"
OV=['overlay-r3','overlay-r2','overlay-port','overlay-mp']
G="{{PS3_DISC}}/"
AUD=P+'/scratch/coop-audit/'
def eff(rel):
    """effective file: overlay precedence r3>r2>port>mp>retail. returns (origin,path) or None"""
    for o in OV:
        f=P+'/work/'+o+'/u3data/build/main/'+rel
        if os.path.exists(f):return o,f
    f=RET+rel
    if os.path.exists(f):return 'retail',f
    return None
def tree(sub):
    """rel->(origin,path) for every file below main/sub, effective"""
    out={}
    for o,b in [(o,P+'/work/'+o+'/u3data/build/main/') for o in OV]+[('retail',RET)]:
        for f in glob.glob(glob.escape(b+sub)+'/**/*',recursive=True):
            if os.path.isfile(f):
                r=f.replace(chr(92),'/')[len(b):]
                out.setdefault(r,(o,f.replace(chr(92),'/')))
    return out
_D=None
def dic():
    global _D
    if _D is None:
        _D=dict(pickle.load(open(AUD+'dict.pkl','rb'))) if os.path.exists(AUD+'dict.pkl') else dict(pickle.load(open(P+'/scratch/live/coop-tmp/dict.pkl','rb')))
    return _D
def nm(v):return dic().get(v,hex(v))
U="{{PS3_UPDATE}}/"
_I=None
def ps3dc():
    """dc1/<rel> -> (psarc,index,source) with PS3 1.19 precedence patch18>patch13>goty-patch>goty-dlc>bin.psarc"""
    global _I
    if _I is not None:return _I
    I={}
    for pf in(U+'dlc-patch18.psarc',U+'dlc-patch13.psarc',G+'goty-patch.psarc',G+'goty-dlc.psarc',G+'build/main/bin.psarc'):
        if not os.path.exists(pf):print('missing',pf);continue
        p=Psarc(pf)
        for i,n in enumerate(p.names):
            j=n.find('dc1/')
            if j>=0 and n.endswith('.bin'):I.setdefault(n[j:],(p,i+1,os.path.basename(pf)))
    _I=I;return I
