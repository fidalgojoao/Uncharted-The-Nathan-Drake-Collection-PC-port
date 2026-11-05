from common import *
for f in glob.glob('{{PS3_UPDATE}}/*.psarc'):
  p=Psarc(f);print(f.rsplit(chr(92),1)[-1],len(p.names))
  print([n for n in p.names if re.search(r'dc1/(ss-|net\.bin|modules)',n)][:400])
  print(sorted({n.rsplit('/',1)[0] for n in p.names})[:40])
