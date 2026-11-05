import struct,sys,re,os
E=os.environ.get('U3_PS3_ELF','{{PROJECT}}/scratch/ps3-119/EBOOT.elf')
d=open(E,'rb').read()
phoff=struct.unpack('>Q',d[0x20:0x28])[0];phes,phn=struct.unpack('>HH',d[0x36:0x3a])
S=[]
for i in range(phn):
    t,fl,off,va,pa,fs,ms,al=struct.unpack('>IIQQQQQQ',d[phoff+i*phes:phoff+i*phes+56])
    if t==1:S.append((va,off,fs,fl))
def o(v):
    for va,off,fs,fl in S:
        if va<=v<va+fs:return off+v-va
    return None
def u32(v):
    x=o(v);return struct.unpack('>I',d[x:x+4])[0] if x is not None else None
def cstr(v):
    x=o(v)
    if x is None:return None
    e=d.find(b'\0',x);return d[x:e].decode('latin1')
def s16(x):return x-0x10000 if x&0x8000 else x
D={32:'lwz',33:'lwzu',34:'lbz',35:'lbzu',36:'stw',37:'stwu',38:'stb',39:'stbu',40:'lhz',41:'lhzu',42:'lha',44:'sth',45:'sthu',48:'lfs',50:'lfd',52:'stfs',54:'stfd'}
X={40:'add',266:'add',8:'subfc',40+0:'subf',235:'mullw',28:'and',444:'or',316:'xor',124:'nor',60:'andc',412:'orc',24:'slw',536:'srw',792:'sraw',27:'sld',539:'srd',794:'srad',23:'lwzx',87:'lbzx',279:'lhzx',343:'lhax',151:'stwx',215:'stbx',407:'sthx',21:'ldx',149:'stdx',341:'lwax',491:'divw',459:'divwu',489:'divd',233:'mulld',104:'neg',10:'addc',138:'adde',136:'subfe',202:'addze'}
def dis(a):
    w=u32(a);op=w>>26;rt=(w>>21)&31;ra=(w>>16)&31;rb=(w>>11)&31;si=s16(w&0xffff);ui=w&0xffff
    if w is None:return '??'
    if op in D:return f'{D[op]} r{rt},{si:#x}(r{ra})'
    if op==14:return f'li r{rt},{si:#x}' if ra==0 else f'addi r{rt},r{ra},{si:#x}'
    if op==15:return f'lis r{rt},{ui:#x}' if ra==0 else f'addis r{rt},r{ra},{ui:#x}'
    if op==24:return 'nop' if w==0x60000000 else f'ori r{ra},r{rt},{ui:#x}'
    if op==25:return f'oris r{ra},r{rt},{ui:#x}'
    if op==26:return f'xori r{ra},r{rt},{ui:#x}'
    if op==27:return f'xoris r{ra},r{rt},{ui:#x}'
    if op==28:return f'andi. r{ra},r{rt},{ui:#x}'
    if op==29:return f'andis. r{ra},r{rt},{ui:#x}'
    if op==11:return f'cmp{"d" if rt&1 else "w"}i cr{rt>>2},r{ra},{si:#x}'
    if op==10:return f'cmpl{"d" if rt&1 else "w"}i cr{rt>>2},r{ra},{ui:#x}'
    if op==7:return f'mulli r{rt},r{ra},{si:#x}'
    if op==8:return f'subfic r{rt},r{ra},{si:#x}'
    if op==12:return f'addic r{rt},r{ra},{si:#x}'
    if op==13:return f'addic. r{rt},r{ra},{si:#x}'
    if op==58:
        ds=s16(w&0xfffc);return ['ld','ldu','lwa','?'][w&3]+f' r{rt},{ds:#x}(r{ra})'
    if op==62:
        ds=s16(w&0xfffc);return ['std','stdu','?','?'][w&3]+f' r{rt},{ds:#x}(r{ra})'
    if op==18:
        li=w&0x3fffffc
        if li&0x2000000:li-=0x4000000
        t=(a+li)&0xffffffff if not w&2 else li&0xffffffff
        return ('bl' if w&1 else 'b')+f' {t:#x}'
    if op==16:
        bd=s16(w&0xfffc);bo=rt;bi=ra
        cc=['lt','gt','eq','so'][bi&3];cr=bi>>2
        t=(a+bd)&0xffffffff
        if bo&0x14==0x14:n='b'
        elif bo&0x10:n='bd'+('z' if bo&2 else 'nz')
        else:n=('b' if bo&8 else 'bn')+cc
        return n+('l' if w&1 else '')+f' cr{cr},{t:#x}'
    if op==19:
        xo=(w>>1)&0x3ff
        if xo==16:return ('blr' if rt&0x14==0x14 else f'bclr {rt},{ra}')+('l' if w&1 else '')
        if xo==528:return ('bctr' if rt&0x14==0x14 else f'bcctr {rt},{ra}')+('l' if w&1 else '')
        return f'cr-op{xo} {rt},{ra},{rb}'
    if op==21:
        sh=rb;mb=(w>>6)&31;me=(w>>1)&31
        return f'rlwinm{"." if w&1 else ""} r{ra},r{rt},{sh},{mb},{me}'
    if op==23:
        mb=(w>>6)&31;me=(w>>1)&31;return f'rlwnm r{ra},r{rt},r{rb},{mb},{me}'
    if op==20:
        mb=(w>>6)&31;me=(w>>1)&31;return f'rlwimi r{ra},r{rt},{rb},{mb},{me}'
    if op==30:
        k=(w>>2)&7;sh=rb|((w>>1)&1)<<5;m=((w>>6)&31)|((w>>5)&1)<<5
        return ['rldicl','rldicr','rldic','rldimi'][k&3 if k<4 else 0]+('.' if w&1 else '')+f' r{ra},r{rt},{sh},{m}'
    if op==31:
        xo=(w>>1)&0x3ff
        if xo==0:return f'cmp{"d" if rt&1 else "w"} cr{rt>>2},r{ra},r{rb}'
        if xo==32:return f'cmpl{"d" if rt&1 else "w"} cr{rt>>2},r{ra},r{rb}'
        if xo==444 and rt==rb:return f'mr{"." if w&1 else ""} r{ra},r{rt}'
        if xo==339:
            spr=((w>>16)&31)|((w>>11)&31)<<5;return f'mf{ {8:"lr",9:"ctr",1:"xer"}.get(spr,spr)} r{rt}'
        if xo==467:
            spr=((w>>16)&31)|((w>>11)&31)<<5;return f'mt{ {8:"lr",9:"ctr",1:"xer"}.get(spr,spr)} r{rt}'
        if xo==986:return f'extsw r{ra},r{rt}'
        if xo==922:return f'extsh r{ra},r{rt}'
        if xo==954:return f'extsb r{ra},r{rt}'
        if xo==26:return f'cntlzw r{ra},r{rt}'
        if xo==824:return f'srawi r{ra},r{rt},{rb}'
        if xo==19:return f'mfcr r{rt}'
        if xo in(266,):return f'add r{rt},r{ra},r{rb}'
        if xo==40:return f'subf r{rt},r{ra},r{rb}'
        n=X.get(xo)
        if n:
            if xo in(28,444,316,124,60,412,24,536,792,27,539,794):return f'{n} r{ra},r{rt},r{rb}'
            return f'{n} r{rt},r{ra},r{rb}'
        return f'op31.{xo}'
    return f'.long {w:#x}'
def rng(a,b):
    for v in range(a,b,4):print(f'{v:x}: {dis(v)}')
def fnend(a,m=0x4000):
    v=a
    while v<a+m:
        if (u32(v)==0x4e800020) :
            # next
            nx=u32(v+4)
            if nx is not None and nx>>16==0xf821 or (nx==0x7c0802a6) or (nx>>16)&0xfc00==0xf800:return v+4
        v+=4
    return v
if __name__=='__main__':
    for sp in sys.argv[1:]:
        if '-' in sp:
            a,b=[int(x,16) for x in sp.split('-')]
        else:
            a=int(sp,16);b=fnend(a)
        print('==',hex(a));rng(a,b)
