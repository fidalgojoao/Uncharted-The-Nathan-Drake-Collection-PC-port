"""Per-map static audit.  For every coop-mode entry in net.bin:
 paks: the level record's package names -> -ingame12 union of regions/spawners PS3 1.19 vs effective PS4
 scripts: closure from stage scripts over symbol references in all effective dc1 modules
 checks: names used by the closure that exist in PS3's map union but not in PS4's;
         symbols defined in PS3 dc but missing from effective PS4 dc; pak23 'symbol' lines PS3 vs PS4 per pack
writes map-audit.txt, map-closure.pkl"""
from common import *
import pickle,collections
N=pickle.load(open(AUD+'pak-names.pkl','rb'))
M=pickle.load(open(AUD+'map-paks.pkl','rb'))
sys.setrecursionlimit(100000)
# effective symbols
T=tree('dc1/ps4')
SY={};FILES={}
for rel,(o,f) in T.items():
    if not f.endswith('.bin') or 'render-settings' in rel or '/vox' in rel:continue
    try:c=DC(f)
    except Exception:continue
    FILES[rel]=c
    for s,t,a in c.syms:SY.setdefault(s,(rel,t,a))
P3={}
for n,(p,i,src) in ps3dc().items():
    if 'render-settings' in n or '/vox' in n:continue
    try:
        d=p.read(i)
        if d[:4]!=b'DC00':continue
        open(AUD+'_m.bin','wb').write(d);c=DC(AUD+'_m.bin')
    except Exception:continue
    for s,t,a in c.syms:P3.setdefault(s,(n,t))
print('ps4 syms',len(SY),'ps3 syms',len(P3))
SCRIPTY={0x45c51f9f,0x9ed499e1,0x372cf7e}
def refs(c,a0):
    """all u32 values reachable (pointer graph) from symbol, limited to this symbol's objects"""
    seen=set();st=[a0];vals=set();syms=sorted(o for _,_,o in c.syms)+[len(c.d)]
    import bisect
    cuts=sorted(set([c.ptr(q) for q in c.ptrs]+syms))
    # memo
    return cuts
CUT={}
def walk(rel,a0):
    c=FILES[rel]
    import bisect
    if rel not in CUT:
        CUT[rel]=sorted(set([c.ptr(q) for q in c.ptrs]+[o for _,_,o in c.syms]+[c.reloc,len(c.d)]))
    cuts=CUT[rel];symoff={o for _,_,o in c.syms}
    seen=set();st=[a0];vals=set()
    while st:
        a=st.pop()
        if a in seen or a>=len(c.d) or a<0:continue
        seen.add(a)
        if a!=a0 and a in symoff:continue   # do not cross into another symbol
        i=bisect.bisect_right(cuts,a);e=cuts[i] if i<len(cuts) else a
        q=a
        if q%4:continue
        while q+4<=e:
            if q in c.ptrs:st.append(c.ptr(q));q+=8;continue
            vals.add(c.u32(q));q+=4
    return vals
MEMO={}
def closure(start):
    out=set();st=list(start);used=set()
    while st:
        s=st.pop()
        if s in out:continue
        if s not in SY:continue
        out.add(s)
        if s not in MEMO:
            rel,t,a=SY[s]
            if t not in SCRIPTY:MEMO[s]=set()
            else:MEMO[s]=walk(rel,a)
        used|=MEMO[s]
        for v in MEMO[s]:
            if v in SY and SY[v][1] in SCRIPTY and v not in out:st.append(v)
    return out,used
# stage scripts from net.bin
b=DC(eff('dc1/ps4/net.bin')[1]);o=b.bysid[0x3b9a067d][1];n=b.u32(o);x=b.ptr(o+8)
# pak23 symbols
ps3t=None
for pf in(U+'dlc-patch18.psarc',):
    p=Psarc(pf);ps3t=p.read(p.names.index('build/main/pak23.txt')+1).decode('latin-1')
ps4t=open(eff('pak23_ps4.txt')[1],encoding='latin-1').read()
def blocks(t):
    B={}
    for blk in re.split(r'(?m)^(?=level-name )',t):
        if blk.startswith('level-name '):
            ls=blk.split('\n');nmz=ls[0][11:].strip()
            B[nmz]=dict(syms={l[7:].strip() for l in ls if l.startswith('symbol ')},pk=[l[8:].strip() for l in ls if l.startswith('package ')],cache=[l[6:].strip() for l in ls if l.startswith('cache ')])
    return B
B3=blocks(ps3t);B4=blocks(ps4t)
out=open(AUD+'map-audit.txt','w');CL={}
AGG=collections.defaultdict(list)
for i in range(n):
    e=x+0xd0*i
    if b.u32(e+8)!=0x6f96651f:continue
    k=nm(b.u32(e));L=b.ptr(e+0x48);cn=b.u32(L+8);sa=b.ptr(L+0x10);lvl,S=M[k]
    names=[z.strip('[]') for z in (S or []) if not z.startswith('net-levels') and '.' not in z]
    # expand 'level-name' -> caches are not loaded together; only listed names
    r3=set();s3=set();r4=set();s4=set();miss=[]
    for z in names:
        pk=z+'-ingame12.pak';I=N.get(pk)
        if not I:
            # PS3 did not have ingame pak of this name
            if eff('pak23/ps4/'+pk) is None and z in B3:miss.append(z+'(no ps4+no ps3 ingame)')
            continue
        r3|=I['ps3r'];s3|=I['ps3s'];r4|=I['ps4r'];s4|=I['ps4s']
        if not I['ps4r'] and not I['ps4s'] and (I['ps3r'] or I['ps3s']):miss.append(z)
    stg=[b.u32(sa+0x78*j+0x2c) for j in range(cn)]
    if L in b.ptrs:
        dflt=b.u32(b.ptr(L)+0x2c)
        if dflt:stg.insert(0,dflt)
    undefined=[nm(v) for v in stg if v and v not in SY]
    ds,used=closure([v for v in stg if v])
    allnames3={sid(z):z for z in r3|s3 if z};allnames4={sid(z) for z in r4|s4 if z}
    lostr=[allnames3[v] for v in used if v in allnames3 and v not in allnames4]
    # stage spawner lists
    lostsp=[]
    for j in range(cn):
        q=sa+0x78*j
        for h in range(b.u32(q+0xc)):
            v=b.u32(b.ptr(q+0x10)+4*h)
            if v in allnames3 and v not in allnames4:lostsp.append(nm(v))
            elif v not in allnames4 and v not in allnames3:lostsp.append('?'+nm(v))
    undefs=sorted(nm(v) for v in used if v in P3 and v not in SY and P3[v][1] in SCRIPTY)
    # pak23 symbol diffs for listed packs
    lostsym=[]
    for z in names:
        if z in B3 and z in B4:
            d=B3[z]['syms']-B4[z]['syms']
            if d:lostsym.append((z,sorted(d)))
        elif z in B3 and z not in B4:lostsym.append((z,'BLOCK-MISSING'))
    CL[k]=dict(stg=stg,ds=ds,used=used,names=names,r4=r4,s4=s4,r3=r3,s3=s3)
    out.write('### %s level=%s kind=%s stages=%d paks=%s\n'%(k,nm(lvl),hex(b.u32(e+0x2c)),cn,names))
    out.write('  regions %d/%d spawners %d/%d closure scripts %d\n'%(len(r3),len(r4),len(s3),len(s4),len(ds)))
    if miss:out.write('  PAK-MISSING %s\n'%miss)
    if undefined:out.write('  STAGE-SCRIPT-UNDEFINED %s\n'%undefined)
    if lostr:out.write('  NAME-LOST(ps3 map name used by script, absent in ps4 map): %s\n'%sorted(lostr))
    if lostsp:out.write('  STAGE-SPAWNER-LOST %s\n'%sorted(set(lostsp)))
    if undefs:out.write('  SYMBOL-MISSING(ps3 defined, ps4 undefined): %s\n'%undefs)
    if lostsym:out.write('  PAK23-SYMBOLS-LOST %s\n'%str(lostsym)[:400])
    miss_any=sorted(s3-s4)[:60]
    if miss_any:out.write('  spawners only in ps3 map (%d): %s\n'%(len(s3-s4),miss_any))
    miss_r=sorted(r3-r4)[:30]
    if miss_r:out.write('  regions only in ps3 map (%d): %s\n'%(len(r3-r4),miss_r))
pickle.dump(CL,open(AUD+'map-closure.pkl','wb'))
print('ok')
