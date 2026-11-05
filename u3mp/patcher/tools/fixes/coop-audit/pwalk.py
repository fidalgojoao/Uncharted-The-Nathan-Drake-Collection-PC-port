"""Parallel pointer-graph walk of a PS3 DC file (BE, 4-byte ptrs) and a PS4 DC file (LE, 8-byte ptrs).
Layout-agnostic: from each shared symbol, walks PS3 words in order against PS4 words, allowing PS4 alignment
padding; every relocated PS3 word must correspond to a relocated PS4 slot; every plain word must match
(word-swapped, halfword-swapped or byte-identical).  Reports where they stop corresponding.
usage: pwalk.py            (all effective dc1 ss files vs PS3) -> pwalk-all.txt
"""
from common import *
import bisect,collections
sys.setrecursionlimit(100000)
OPS=collections.Counter();OKOPS={(0x3f,0x44),(0x0d,0x4f)};OFF=collections.Counter();ORG=['']
class W:
    def __init__(s,a,b):
        s.a=a;s.b=b
        s.ca=sorted(set([a.ptr(q) for q in a.ptrs]+[o for _,_,o in a.syms]+[a.reloc,len(a.d)]))
        s.cb=sorted(set([b.ptr(q) for q in b.ptrs]+[o for _,_,o in b.syms]+[b.reloc,len(b.d)]))
        s.seen={};s.mm=[];s.nw=0;s.np=0
    def ext(s,cuts,o):
        i=bisect.bisect_right(cuts,o);return cuts[i] if i<len(cuts) else o
    def eq(s,A,B):
        a,b=s.a,s.b
        if B+4>len(b.d) or A+4>len(a.d):return False
        x=a.d[A:A+4];y=b.d[B:B+4]
        if x[0]==0x3b and y[0]==0x3b and x[1:3]==y[1:3]:
            OFF[(ORG[0],x[3],y[3])]+=1;return True
        if x==y or x==y[::-1]:return True
        if x[:2]==y[1::-1] and x[2:]==y[:1:-1]:return True  # two u16
        if x[0]==0 and x[1]!=0 and x[1]==y[0] and y[1]==0 and y[3]==0 and x[2]<0x40 and y[2]<0x40:return True  # {count:16,line:16} with another source line
        if x[1:]==y[1:] and x[0]<0x60 and y[0]<0x60:
            OPS[(x[0],y[0])]+=1
            if (x[0],y[0]) in OKOPS:return True
        return False
    def walk(s,sym,top,tb):
        stack=[(top,tb,'')]
        while stack:
            A0,B0,path=stack.pop()
            k=(A0,B0)
            if k in s.seen:continue
            s.seen[k]=1
            a,b=s.a,s.b
            if A0>=len(a.d) or B0>=len(b.d):continue
            ea=s.ext(s.ca,A0);eb=s.ext(s.cb,B0)
            # unaligned objects (strings) -> compare bytes
            A=A0;B=B0
            z=a.d.find(b'\0',A0)
            if z-A0>=4 and all(32<=c<127 for c in a.d[A0:z]) and not (A0&3==0 and B0&3==0 and s.eq(A0,B0)):
                za=a.d[A0:z];zb=b.d[B0:B0+len(za)+1]
                if za+b'\0'!=zb and b'big3/src' not in za:s.mm.append((sym,path,A0,B0,'bytes',za[:40],zb[:40]))
                continue
            if A0&3 or B0&3:
                z=a.d.find(b'\0',A0);za=a.d[A0:z];zb=b.d[B0:B0+len(za)+1]
                if za+b'\0'!=zb and not (A0&3==0 or B0&3==0):s.mm.append((sym,path,A0,B0,'bytes',za[:24],zb[:24]))
                continue
            kids=[];nbad=0
            while A<ea:
                aptr=A in a.ptrs
                # align B / skip pad
                if aptr:
                    if B%8:
                        if b.u32(B) in(0,0xaaaaaaaa) and B+4 not in b.ptrs:B+=4
                    if B not in b.ptrs:
                        # skip padding words
                        q=B
                        while q<eb and q not in b.ptrs and b.u32(q) in(0,0xaaaaaaaa):q+=4
                        if q in b.ptrs and q<eb:B=q
                        else:
                            s.mm.append((sym,path+'+%x'%(A-A0),A,B,'ptr-lost',a.ptr(A),b.u32(B) if B+4<=len(b.d) else None));break
                    kids.append((a.ptr(A),b.ptr(B),path+'+%x'%(A-A0)));A+=4;B+=8;s.np+=1
                    continue
                if B>=eb and A<ea:
                    # A has more words than the B object
                    rest=a.d[A:ea]
                    if rest.strip(b'\0\xaa')==b'':break
                    # B extent ended by a cut inside: allow continuing if B bytes still match
                    if not s.eq(A,B):
                        s.mm.append((sym,path+'+%x'%(A-A0),A,B,'short',a.u32(A),b.u32(B) if B+4<=len(b.d) else None));break
                if B in b.ptrs:
                    # B has a pointer where A has plain word
                    rest=a.d[A:ea]
                    if rest.strip(b'\0\xaa')==b'':break
                    if a.u32(A)==0:
                        A+=4;continue
                    s.mm.append((sym,path+'+%x'%(A-A0),A,B,'ptr-gained',a.u32(A),b.ptr(B)));break
                if s.eq(A,B):A+=4;B+=4;s.nw+=1;continue
                # skip B pad
                q=B;ok=False
                while q<eb and q not in b.ptrs and b.u32(q) in(0,0xaaaaaaaa):
                    q+=4
                    if q<eb and q not in b.ptrs and s.eq(A,q):ok=True;break
                if ok:B=q;continue
                # A pad / tail
                rest=a.d[A:ea]
                if rest.strip(b'\0\xaa')==b'':
                    if A==A0 and B==B0 and a.u32(A)==0 and b.u32(B) not in(0,0xaaaaaaaa) and ea-A0<=8:
                        s.mm.append((sym,path,A,B,'zero-lost',0,b.u32(B)))
                    break
                s.mm.append((sym,path+'+%x'%(A-A0),A,B,'value',a.u32(A),b.u32(B)))
                nbad+=1
                if nbad>40:break
                A+=4;B+=4
            stack.extend(kids[::-1])
def compare(fa,fb):
    a=DC(fa);b=DC(fb);w=W(a,b)
    sa={s:(t,o) for s,t,o in a.syms};sb={s:(t,o) for s,t,o in b.syms}
    only3=[s for s in sa if s not in sb];only4=[s for s in sb if s not in sa]
    ty=[s for s in sa if s in sb and sa[s][0]!=sb[s][0]]
    for s in sa:
        if s in sb and sa[s][0]==sb[s][0]:w.walk(s,sa[s][1],sb[s][1])
    return w,only3,only4,ty
def fmt(m):
    sym,path,A,B,kind,x,y=m
    def v(q):
        if isinstance(q,int):return '%08x(%s)'%(q,dic().get(q,''))
        return repr(q)
    return '%s path=%s ps3@%x ps4@%x %s ps3=%s ps4=%s'%(nm(sym),path,A,B,kind,v(x),v(y))
if __name__=='__main__':
    I=ps3dc()
    I=ps3dc();T=tree('dc1/ps4');out=open(AUD+'pwalk-all.txt','w')
    tmp=AUD+'_t.bin';tot=collections.Counter()
    pat=sys.argv[1] if len(sys.argv)>1 else r'dc1/(ss-.*|ss-multiplayer/.*)\.bin$'
    for n in sorted(I):
        if not re.match(pat,n):continue
        rel='dc1/ps4/'+n[4:]
        if rel not in T:
            out.write('NO-PS4 %s [%s]\n'%(n,I[n][2]));tot['noport']+=1;continue
        o,f=T[rel];open(tmp,'wb').write(I[n][0].read(I[n][1]))
        ORG[0]=o
        try:w,o3,o4,ty=compare(tmp,f)
        except Exception as ex:
            out.write('ERR %s %r\n'%(rel,ex));tot['err']+=1;continue
        out.write('## %s <%s> [%s] words=%d ptrs=%d mism=%d ps3only=%d ps4only=%d tydiff=%d\n'%(n,I[n][2],o,w.nw,w.np,len(w.mm),len(o3),len(o4),len(ty)))
        for s in o3:out.write('  PS3ONLY %s\n'%nm(s))
        for s in o4:out.write('  PS4ONLY %s\n'%nm(s))
        for s in ty:out.write('  TYPE %s\n'%nm(s))
        for m in w.mm:out.write('  '+fmt(m)+'\n')
        tot['files']+=1;tot['mm']+=len(w.mm);tot['w']+=w.nw;tot['p']+=w.np
    print(tot);print({'%02x>%02x'%k:v for k,v in OPS.items()});print(sorted(OFF.items()))
