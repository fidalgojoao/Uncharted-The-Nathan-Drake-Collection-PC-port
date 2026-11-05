from common import *
import bisect
os.chdir(AUD)
import importlib.util,io,contextlib
sp=importlib.util.spec_from_file_location('dcdis',P+'/scratch/live/coop-tmp/dcdis.py');dd=importlib.util.module_from_spec(sp);sp.loader.exec_module(dd)
dd.D.update(dic());dd.nm=lambda v:dic().get(v,hex(v))
F=[('ss-coop-adv-fort.bin',0x56958),('ss-multiplayer/ss-coop-arena-npcs.bin',0x2ee0),('ss-multiplayer/ss-facility-crane.bin',0xd0),('ss-multiplayer/ss-grave-events.bin',0x46c8),('ss-multiplayer/ss-ldn-streets-events.bin',0x5b8),('ss-multiplayer/ss-net-breakable-car-fort.bin',0x610),('ss-multiplayer/ss-oasis-events.bin',0x170),('ss-multiplayer/ss-old-quater-events.bin',0xf10),('ss-multiplayer/ss-sweeping-building.bin',0x140)]
for rel,k in F:
  o,f=eff('dc1/ps4/'+rel);c=DC(f)
  optsid=c.u32(k);ty=c.u32(k+4);p=c.ptr(k+0x10);st=c.u32(p)
  owners=[s for s,t,a in c.syms if t==0x45c51f9f and a<k]
  own=max([(a,s) for s,t,a in c.syms if t==0x45c51f9f and a<=k])
  # try: symbols whose table ptr reaches: nearest forward state-script option ptr
  cand=[(a,s) for s,t,a in c.syms if t==0x45c51f9f]
  print('##',rel,o,'entry',hex(k),'option',nm(optsid),nm(ty),'ptr->',hex(p),hex(st),nm(st),'next-script-after',[nm(s) for a,s in sorted(cand) if a>k][:1],'prev',nm(own[1]))
  # find who uses the option: search lambda dumps
  for s,t,a in c.syms:
    if t!=0x45c51f9f:continue
    b=io.StringIO()
    with contextlib.redirect_stdout(b):dd.walk(c,a)
    t2=b.getvalue();on=nm(optsid)
    ls=[l.strip() for l in t2.split('\n') if on in l]
    if ls:print('   in',nm(s),len(ls),[re.sub(r'^\d+: ','',x)[:80] for x in ls[:6]])
