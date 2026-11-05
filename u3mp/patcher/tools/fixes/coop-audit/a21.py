"""npc-* strings used by each map's paks (battle managers etc) vs spawners available in the map's pak set, PS3 and PS4"""
from common import *
import pickle
sys.path.insert(0,AUD+'../recovered/coop-regions')
import detect
I=detect.ps3idx()
N=pickle.load(open(AUD+'pak-names.pkl','rb'));M=pickle.load(open(AUD+'map-paks.pkl','rb'))
rx=re.compile(rb'npc-[a-z0-9-]+')
c4={};c3={}
def s4(pk):
  if pk not in c4:
    f=detect.eff(pk)[0];c4[pk]=set(rx.findall(open(f,'rb').read())) if f else set()
  return c4[pk]
def s3(pk):
  if pk not in c3:
    c3[pk]=set(rx.findall(I[pk][0].read(I[pk][1]))) if pk in I else set()
  return c3[pk]
out=open(AUD+'map-npc.txt','w')
for k,(lv,S) in M.items():
  names=sorted({z.strip('[]') for z in S if not z.startswith('net-levels') and '.' not in z})
  u3=set();u4=set();h3=set();h4=set()
  for z in names:
    pk=z+'-ingame12.pak'
    u3|=s3(pk);u4|=s4(pk)
    if pk in N:h3|={x.encode() for x in N[pk]['ps3s']};h4|={x.encode() for x in N[pk]['ps4s']}
  m3=u3-h3;m4=u4-h4
  out.write('%s paks=%d npcrefs3=%d npcrefs4=%d have3=%d have4=%d miss3=%d miss4=%d\n   only-ps4-missing: %s\n   not-even-referenced-by-ps4: %s\n'%(k,len(names),len(u3),len(u4),len(h3),len(h4),len(m3),len(m4),sorted(x.decode() for x in m4-m3),sorted(x.decode() for x in u3-u4)[:12]))
  print(k,names if 'SHADE' in k and k.startswith(('NET_GAME_FORT','NET_GAME_YEMEN')) else '', 'miss3',len(m3),'miss4',len(m4),'new',len(m4-m3),'lostrefs',len(u3-u4))
