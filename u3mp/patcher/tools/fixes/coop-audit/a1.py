from common import *
pf=glob.glob(glob.escape(G)+'**/bin.psarc',recursive=True);print(pf)
ps=Psarc(pf[0]);print(len(ps.names));print([n for n in ps.names if 'net.bin' in n or 'modules' in n or 'pak23' in n][:10])
for n in ps.names[:5]:print(n)
print(sum(1 for n in ps.names if n.startswith('dc1/ss')),sum(1 for n in ps.names if 'ss-multiplayer' in n))
print([n for n in ps.names if re.search('coop|horde|hunter',n)])
for f in glob.glob('{{PS3_UPDATE}}/*.psarc')+glob.glob('{{PS3_DATA2}}/build/main/*.psarc'):
  try:
    p=Psarc(f);print(f.rsplit('/',1)[-1],len(p.names),[n for n in p.names if n.endswith(('.bin','pak23.txt')) and ('dc1' in n or 'pak23' in n)][:6])
  except Exception as e:print('E',f,e)
o,f=eff('dc1/ps4/net.bin');c=DC(f);print(o,len(c.d),len(c.syms))
print(hex(c.syms[0][0]))
for s,t,a in c.syms:
  print(hex(s),nm(s),nm(t),hex(a)) if not nm(s).startswith('ss') else 0
