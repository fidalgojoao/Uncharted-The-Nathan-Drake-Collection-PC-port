import pickle,sys,bisect,re
P="{{PROJECT}}/"
A,T=pickle.load(open(P+'scratch/live/instance-mat/_dis.pkl','rb'))
def rng(a,b):
    return [(v,T[v]) for v in A[bisect.bisect_left(A,a):bisect.bisect_left(A,b)]]
def fn(a):
    i=bisect.bisect_left(A,a)
    while i>0 and not T[A[i]].startswith('push rbp'): i-=1
    return A[i]
def callers(t):
    return [(v,fn(v)) for v in A if T[v] in('call 0x%x'%t,'jmp 0x%x'%t)]
if __name__=='__main__':
    a=sys.argv[1:]
    if a[0]=='c':
        for t in a[1:]:print(t,[hex(v)+'<'+hex(f) for v,f in callers(int(t,16))])
    elif a[0]=='g':
        p=re.compile(a[1]);lo=int(a[2],16);hi=int(a[3],16)
        for v,t in rng(lo,hi):
            if p.search(t):print('%x: %s <%x'%(v,t,fn(v)))
    else:
        for s in a:
            x,y=[int(v,16) for v in s.split('-')]
            print('==',s)
            for v,t in rng(x,y):print('%x: %s'%(v,t[:120]))
