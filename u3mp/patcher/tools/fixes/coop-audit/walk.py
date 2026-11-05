"""Static progression walk per coop map, using the dcdis.py pseudo-dump of the effective modules.
For each stage script: states, go() targets, boolean waits with their setters, referenced entity names, how the stage ends.
Writes walk-<kind>.txt + walk-summary.txt.  Waits are matched syntactically (see report for limits)."""
from common import *
import pickle,io,contextlib,collections,importlib.util
os.chdir(AUD)  # dcdis reads dict.pkl from cwd
sp=importlib.util.spec_from_file_location('dcdis',P+'/scratch/live/coop-tmp/dcdis.py');dd=importlib.util.module_from_spec(sp);sp.loader.exec_module(dd)
dd.D.update(dic());dd.nm=lambda v:dic().get(v,hex(v))
CL=pickle.load(open(AUD+'map-closure.pkl','rb'))
T=tree('dc1/ps4');SYM={}
for rel,(o,f) in T.items():
    if not f.endswith('.bin') or 'render-settings' in rel or '/vox' in rel:continue
    if not re.search(r'dc1/ps4/(ss-|ss/)',rel+'/') and 'ss-' not in rel:continue
    try:c=DC(f)
    except Exception:continue
    for s,t,a in c.syms:
        if t==0x45c51f9f:SYM.setdefault(s,(rel,c,a))
print(len(SYM))
TXT={}
def dump(s):
    if s in TXT:return TXT[s]
    if s not in SYM:return None
    rel,c,a=SYM[s];b=io.StringIO()
    try:
        with contextlib.redirect_stdout(b):dd.walk(c,a)
    except Exception as ex:b.write('!!dump-error %r\n'%ex)
    TXT[s]=b.getvalue();return TXT[s]
SETRX=re.compile(r'(?:net-)?set-boolean\(([^,()]+)')
GETRX=re.compile(r'get-boolean\(([^,()]+), ?([^,()]+)\)')
GORX=re.compile(r'\b(?:net-)?go\(([^,()]+)\)')
STRX=re.compile(r'^ state (\S+) blocks',re.M)
EVRX=re.compile(r'send-event\(([^,()]+), ([^,()]+)')
NAMERX=re.compile(r'\b((?:reg|rgn|bm|obj|online-spawner|player-spawner|npc|group|spawner|ss)-[a-z0-9-]+)\b')
def parse(t):
    st=STRX.findall(t)
    gos=set(GORX.findall(t));gets=set(GETRX.findall(t));sets=set(SETRX.findall(t));nam=set(NAMERX.findall(t))
    ons=set(re.findall(r'on type=\d+ (\S+) tracks',t))
    return st,gos,gets,sets,nam,ons
out=open(AUD+'walk-summary.txt','w')
GLOBSET=collections.defaultdict(set)
for s in list(SYM):
    t=dump(s)
    if t:
        for q in parse(t)[3]:GLOBSET[q].add(nm(s))
b=DC(eff('dc1/ps4/net.bin')[1]);o=b.bysid[0x3b9a067d][1];n=b.u32(o);x=b.ptr(o+8)
for i in range(n):
    e=x+0xd0*i
    if b.u32(e+8)!=0x6f96651f:continue
    k=nm(b.u32(e));C=CL[k]
    allsets=set();allons=set();tb={}
    for s in C['ds']:
        t=dump(s)
        if t is None:continue
        tb[s]=parse(t);allsets|=tb[s][3];allons|=tb[s][5]
    # tags on regions/spawners can supply booleans (not read here)
    names4={z for z in C['r4']|C['s4'] if z};names3={z for z in C['r3']|C['s3'] if z}
    f=open(AUD+'walk/%s.txt'%k.replace('/','_') ,'w') if os.path.isdir(AUD+'walk') else None
    if f is None:
        os.makedirs(AUD+'walk',exist_ok=True);f=open(AUD+'walk/%s.txt'%k,'w')
    prob=collections.defaultdict(list)
    for j,sv in enumerate(C['stg']):
        if not sv:continue
        st,gos,gets,sets,nam,ons=(tb.get(sv) or ([],set(),set(),set(),set(),set()))
        f.write('stage %d %s: %s\n'%(j,nm(sv),'(no script)' if sv not in tb else ''))
        if sv not in tb:continue
        states=set(st)
        badgo=sorted(g for g in gos if g not in states and not g.startswith('0x') and g not in('self',))
        f.write('   states: %s\n'%' '.join(st))
        if badgo:f.write('   go() target not a state: %s\n'%badgo);prob['badgo'].append((nm(sv),badgo))
        for key,tgt in sorted(gets):
            ok=key in allsets;gl=sorted(GLOBSET.get(key,()))[:3]
            f.write('   wait get-boolean %s on %s setter:%s\n'%(key,tgt,'closure' if ok else ('other-script '+str(gl) if gl else 'NONE-ANYWHERE')))
            if not ok and not gl:prob['boolean-unset'].append((nm(sv),key,tgt))
            if not ok and gl:prob['boolean-outside-closure'].append((nm(sv),key,tgt,gl[:2]))
        for z in sorted(nam):
            if z.startswith('ss-'):continue
            where='ok' if z in names4 else ('LOST' if z in names3 else 'not-region/spawner')
            f.write('   uses %s: %s\n'%(z,where))
            if where=='LOST':prob['lost'].append((nm(sv),z))
            if where=='not-region/spawner':prob['nonreg'].append(z)
    out.write('### %s: stages %d, scripts in closure %d, dumped %d\n'%(k,len(C['stg']),len(C['ds']),len(tb)))
    for kk,v in prob.items():
        if kk=='nonreg':
            out.write('   other-entity names (objects/not in region+spawner tables) %d: %s\n'%(len(set(v)),sorted(set(v))[:30]))
        else:
            out.write('   %s %d: %s\n'%(kk,len(v),v[:14]))
print('done')
