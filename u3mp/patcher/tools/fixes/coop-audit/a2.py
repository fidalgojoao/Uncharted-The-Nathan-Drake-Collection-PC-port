"""build bigger sid dictionary: all effective ps4 dc1, ps3 bin.psarc dc1, pak23 text, ingame paks of coop"""
from common import *
S=set()
rx=re.compile(rb'[\x21-\x7e]{3,80}')
T=tree('dc1/ps4')
for r,(o,f) in T.items():
    if f.endswith('.bin'):S.update(rx.findall(open(f,'rb').read()))
ps=Psarc(glob.glob(glob.escape(G)+'**/bin.psarc',recursive=True)[0])
for i,n in enumerate(ps.names):
    if n.endswith('.bin') and n.startswith('dc1/') and 'render-settings' not in n:S.update(rx.findall(ps.read(i+1)))
    S.add(os.path.basename(n).encode().rsplit(b'.',1)[0])
S.update(rx.findall(open(eff('pak23_ps4.txt')[1],'rb').read()))
S.update(rx.findall(open("D:/1. PC Games/Uncharted-windows/_claude_transfer/u3_ps4.elf",'rb').read()))
for r,(o,f) in tree('pak23/ps4').items():
    if re.search(r'-ingame12\.pak$',f) and re.search(r'coop|hunter|arena|world|stage|multi',f):
        S.update(rx.findall(open(f,'rb').read()))
D=dict(pickle.load(open(P+'/scratch/live/coop-tmp/dict.pkl','rb')))
extra=[]
for s in S:
    k=s.decode('latin1')
    for v in (k,k.strip('*"\'(),')):
        h=sid(v)
        if h and h not in D:D[h]=v
print(len(S),len(D))
pickle.dump(D,open(AUD+'dict.pkl','wb'))
