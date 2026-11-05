import json,struct,glob,os
P="{{PROJECT}}/"
for d in sorted(glob.glob(P+'work/runtime-tests/baseline-*')):
    print('==',os.path.basename(d))
    last=None
    for l in open(d+'/peek.jsonl'):
        p=json.loads(l)
        if not p.get('ec'):continue
        b=bytes.fromhex(p['ec']);e=struct.unpack('<%dI'%(len(b)//4),b)
        rows=[(e[238+i*12],e[239+i*12],e[240+i*12],e[241+i*12]) for i in range(2)]
        al=[(e[216+i*6],e[217+i*6]) for i in range(2)]
        key=([r[:3] for r in rows],al)
        if key!=last:
            print('%7.1fs'%p['seconds'],' | '.join('p%d voice=%08x row=%02x el=%02x d00=%02x np770=%02x vt=%x n=%d'%(i,v,r&0xff,(r>>8)&0xff,(r>>16)&0xff,r>>24,vt,n) for i,(v,r,vt,n) in enumerate(rows)),'alive',al)
            last=key
