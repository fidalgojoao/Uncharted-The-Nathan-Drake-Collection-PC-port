from common import *
T=tree('dc1/ps4');T.update(tree('pak23/ps4'))
want=[b'bm-shade-survival-1',b'bm-survival-1',b'bm-shade-marked-2-1',b'npc-shade-mark-ak-1',b'npc-shade-armor-1',b'npc-kickback-mark-grappler-1']
wsid=[(w,struct.pack('<I',sid(w.decode()))) for w in want]
H={w:[] for w in want}
for rel,(o,f) in T.items():
  if not f.endswith(('.bin','.pak')) or 'render-settings' in rel:continue
  if rel.endswith('.pak') and not rel.endswith('-ingame12.pak'):continue
  d=open(f,'rb').read()
  for w,s in wsid:
    a=d.count(w);b=d.count(s)
    if a or b:H[w].append((os.path.basename(rel),o,a,b))
for w,l in H.items():print(w.decode(),len(l),l[:14])
