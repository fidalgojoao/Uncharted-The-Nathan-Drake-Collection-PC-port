"""extract PS3 dc versions to p3/<ver>/<basename>"""
from common import *
SRC=[('p18',U+'dlc-patch18.psarc'),('p13',U+'dlc-patch13.psarc'),('gotyp',G+'goty-patch.psarc'),('gotyd',G+'goty-dlc.psarc'),('bin',G+'build/main/bin.psarc')]
pat=re.compile(sys.argv[1])
for k,f in SRC:
    p=Psarc(f);os.makedirs(AUD+'p3/'+k,exist_ok=True)
    for i,n in enumerate(p.names):
        j=n.find('dc1/')
        if j>=0 and n.endswith('.bin') and pat.search(n[j:]):
            open(AUD+'p3/'+k+'/'+os.path.basename(n),'wb').write(p.read(i+1))
