"""Decode the x028 co-op voice diagnostic out of a run's peek.jsonl.
usage: python -I read_voxdiag.py [run dir] [--v736]   (default: newest work/runtime-tests/baseline-*, v7.37 layout)
v7.36 layout (kinds 131..133): ec[230] calls, ec[231] controller vtable - base, ec[232] +0xfc voice, ec[233] +0x104 | vox state
byte << 8 | MP byte << 24, ec[234] line sid, ec[235] flags, ec[236] last exit code, ec[237] result, ec[238..253] histogram of
codes 0..15, ec[254] say events, ec[255] voice-0 arrivals.  ec[230] and ec[254] are shared with older writers (polluted).
v7.37 layout (edits_v737_voxdiag2.py): eb[247] calls, eb[248] say events, eb[249] voice-0 arrivals, histogram ec[238..245]
(codes 0..6, 7 = 7 and above), ec[246..249] the line sid found by the game's lookup in vox / vox-mp / vox-igc / any module,
ec[250] symbols in the global table."""
import json,struct,glob,os,sys
P="{{PROJECT}}/"
CODES={0:'accepted',1:'queued',5:'vox state 0 (+5)',6:'state+5 / +0x104 set',7:'VOICE 0',8:'no vox entry for sid',
       9:'0x4afb52 filter',0xa:'0x4af9d0 owner check',0xc:'0x4af8d5 entry filter',0xd:'0x4b158d',0xe:'0x4b192e',0xf:'0x4b159b/0x4b1849',
       0x14:'owner dead, kind != 13',0x15:'0x4afa2b',0x17:'0x4b01ac',0x1f:'0x4b1645'}
args=[a for a in sys.argv[1:] if not a.startswith('--')];old='--v736' in sys.argv
d=args[0] if args else sorted(glob.glob(P+'work/runtime-tests/baseline-*'),key=os.path.getmtime)[-1]
print('run',os.path.basename(d),'layout','v7.36' if old else 'v7.37')
last=None;rows=[]
for l in open(d+'/peek.jsonl'):
    p=json.loads(l)
    if not p.get('ec'):continue
    e=struct.unpack('<256I',bytes.fromhex(p['ec']));b=struct.unpack('<256I',bytes.fromhex(p['eb'])) if p.get('eb') else (0,)*256
    k=e[230:256]+b[247:250]
    if k!=last:rows.append((p['seconds'],e,b));last=k
if not rows:raise SystemExit('no ec in peek.jsonl')
for t,e,b in rows:
    if old:calls,ev,v0=e[230],e[254],e[255];hist={c:e[238+c] for c in range(16) if e[238+c]};lk=''
    else:
        calls,ev,v0=b[247],b[248],b[249];hist={c:e[238+c] for c in range(8) if e[238+c]}
        lk=' | found: vox=%d vox-mp=%d vox-igc=%d any=%d symbols=%d'%(e[246],e[247],e[248],e[249],e[250])
    print('%7.1fs calls=%d events=%d voice0=%d | ctl vt=%x voice=%08x st=%06x line=%08x flags=%08x | last code=%d(%s) res=%d | hist=%s%s'%(
        t,calls,ev,v0,e[231],e[232],e[233],e[234],e[235],e[236],CODES.get(e[236],'?'),e[237],
        {('%d:%s'%(c,CODES.get(c,'7+' if (c==7 and not old) else '15+' if c==15 else '?'))):n for c,n in hist.items()},lk))
e=rows[-1][1]
print('\nverdict:')
if old:
    if e[238+8]:print('  reject 8 (no vox entry) seen %d times: the line sid does not resolve in the modules the vox lookup 0x4a7350 searches (vox, vox-igc, vox-mp). Fix: scratch/coop-voice2/apply_coop_vox.py.'%e[238+8])
    if e[238]+e[239]:print('  %d accepted/queued.'%(e[238]+e[239]))
else:
    print('  last request: found in vox=%d vox-mp=%d vox-igc=%d any=%d (0 everywhere = the entry is not loaded at all; any=1 but vox-mp=0 = loaded but not under the vox-mp module id); last code %d (%s).'%(e[246],e[247],e[248],e[249],e[236],CODES.get(e[236],'?')))
    if e[238+7]:print('  bucket 7+ = %d (7 voice 0, 8 no entry, ...; see the last code).'%e[238+7])
    if e[238]+e[239]:print('  %d accepted/queued: the request side is fine.'%(e[238]+e[239]))
