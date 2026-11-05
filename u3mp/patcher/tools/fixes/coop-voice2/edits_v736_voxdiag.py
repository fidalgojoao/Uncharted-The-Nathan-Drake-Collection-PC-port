# x028: co-op voice diagnostic (scratch/coop-voice2). Replaces the x025 per-player voice readout (it followed np+0x10, which is
# not the Player handle: the object it reached changed every tick and had vtable 0x16a10e0, so ec[238]/ec[250] were always -1).
# Three regs stubs (kinds 170..172, x-stub slots 0x13600/0x13680/0x13700) on the PS4 vox controller:
#   170  SayRequest entry 0x4af5f0 (vt+0x98; PS3 1.19 0x1b5540): rdi = vox controller, esi = line sid, ecx = flags
#   171  SayRequest common exit 0x4af6c6 (reached from every reject and from success): r14d = reject code, r12b = result
#   172  script say-event handler 0x4b3610 entry (Player event 0x9f893922 -> vox controller; it is what (say 'coop-N line) ends in)
# ec[230] SayRequest calls          ec[231] controller vtable - base (0x166e890 base class, 0x166bf90 derived/net)
# ec[232] controller +0xfc voice    ec[233] +0x104 | vox-state byte 0x1b5a830 << 8 | MP-active byte 0x17d5c61 << 24
# ec[234] last line sid             ec[235] last flags (ecx)
# ec[236] last reject code          ec[237] last result (r12b)
# ec[238..253] histogram of exit codes 0..14, bucket 15 = 15 and above (0x20000 too)
# ec[254] say events at 0x4b3610    ec[255] SayRequest calls that arrived with voice 0 (reject 7 on PS4 and PS3 alike)
# Exit codes (0x4af5f0): 0 accepted/played, 1 queued, 5/6 vox state byte != 1 (code = state+5), 7 voice 0, 8 no vox entry
# for the sid (0x4a7350 lookup / type != 0xe3825e8d), 9, 0xa, 0xc entry filter (0x4af8d5), 0xd, 0xe, 0xf, 0x14 owner dead and
# entry kind != 13 (0x4b07b9), 0x15, 0x17, 0x1f.
# U3MP_VOXDIAG=0 builds without the three stubs. U3MP_VOX_VOICEFIX=1 (default off) makes kind 170 write the last mesh-picked
# character (ec[201], else 'drake') straight into +0xfc when a request arrives with voice 0: a test lever for reject 7 only,
# not the real SetVoxCharacter (0x4aec80 also resets +0x38/+0x3c/+0xf8/+0x106 and binds the character's bank).
C='u3_lan_profiles.c';B='build_u3_profile_probe.py'

# 1. drop the x025 per-player block (ec[230..253] writers); the x027 respawn block that follows keeps using mg
t=read(C)
a=t.index('for(int i=0;i<2;i++){uint8_t *np=mg+0x390+(size_t)i*0x9c0;uint32_t *o=&X.ec[230+i*12];')
e=t.index('o[11]++;}',a)+len('o[11]++;}')
assert t.count('uint32_t *o=&X.ec[230+i*12];')==1 and e-a<1400,(e-a)
write(C,t[:a]+'/* x025 removed (x028, scratch/coop-voice2) */'+t[e:])
rep(C,'/* x025 diagnostic, ec[230..254]: per player of the first two:','/* x025 (removed, see x028 in scratch/coop-voice2): per player of the first two:')

# 2. kind handlers
DEFS='''/* x028 co-op voice diagnostic (scratch/coop-voice2): regs stubs on the vox controller, see the edits file for the ec map */
EXT static void vox_req_in119(uintptr_t base,uint64_t *r){
 uint8_t *c=(uint8_t*)r[9];X.ec[230]++;X.ec[234]=(uint32_t)r[10];X.ec[235]=(uint32_t)r[12];
 if(!stage_ptr_ok(c)||((uintptr_t)c&7))return;
 X.ec[231]=(uint32_t)(*(uint64_t*)c-(uint64_t)base);X.ec[232]=*(uint32_t*)(c+0xfc);
 X.ec[233]=c[0x104]|(uint32_t)*(uint8_t*)(base+0x1b5a830u)<<8|(uint32_t)*(uint8_t*)(base+0x17d5c61u)<<24;
 if(!*(uint32_t*)(c+0xfc)){X.ec[255]++;
  if(X.ec[1]&0x40000000u){uint32_t v=X.ec[201]?X.ec[201]:0x3b6fa41cu;*(uint32_t*)(c+0xfc)=v;X.ec[232]=v;}}}   /* U3MP_VOX_VOICEFIX=1 */
EXT static void vox_req_out119(uint64_t *r){
 uint32_t code=(uint32_t)r[1];X.ec[236]=code;X.ec[237]=(uint32_t)r[3]&0xff;X.ec[238+(code<15u?code:15u)]++;}
'''
rep(C,'int u3_profile_event(uintptr_t base,int32_t *event,int kind,ProfileState *s,uintptr_t a5){',
    DEFS+'int u3_profile_event(uintptr_t base,int32_t *event,int kind,ProfileState *s,uintptr_t a5){')
rep(C,' if(kind==122){',
    ' if(kind==170){if(event)vox_req_in119(base,(uint64_t*)event);return 1;}   /* x028: SayRequest 0x4af5f0 entry */\n'
    ' if(kind==171){if(event)vox_req_out119((uint64_t*)event);return 1;}   /* x028: SayRequest exit 0x4af6c6 */\n'
    ' if(kind==172){X.ec[254]++;return 1;}   /* x028: say event handler 0x4b3610 */\n'
    ' if(kind==122){')

# 3. the stubs, after the c010 kind-122 site
OLD=" at,w=regs_stub(0x13c00,122,lambda h:b'\\x8b\\x0d'+P('<i',EC105-(h+6))+b'\\x85\\xc9',0xc38999);agc_site(0xc38989,o,at,w,122)\n"
NEW=OLD+''' # x028 co-op voice diagnostic (scratch/coop-voice2/edits_v736_voxdiag.py): regs stubs on the vox controller's SayRequest
 #   0x4af5f0 (kind 170, entry: push rbp; mov rbp,rsp; push r15 replayed), its common exit 0x4af6c6 (kind 171: mov rax,[r13];
 #   cmp rax,[rbp-0x30] replayed, r14d = reject code, r12b = result) and the script say-event handler 0x4b3610 (kind 172).
 if os.environ.get('U3MP_VOXDIAG','1')!='0':
  for site,o_,slot,kind,back in ((0x4af5f0,'554889e54157',0x13600,170,0x4af5f6),(0x4af6c6,'498b4500483b45d0',0x13680,171,0x4af6ce),(0x4b3610,'554889e54157',0x13700,172,0x4b3616)):
   ob=bytes.fromhex(o_);agc_check(site,ob,'x028 vox diag %#x'%site)
   at,w=regs_stub(slot,kind,(lambda h,ob=ob:ob),back);agc_site(site,ob,at,w,kind)
'''
rep(B,OLD,NEW)
# ec[1] bit30: U3MP_VOX_VOICEFIX=1 test lever (kind 170 writes a voice into +0xfc when the request arrives with voice 0)
rep(B," if os.environ.get('U3MP_PARTMP119','1')=='0':image[rw2+0x4454]|=0x08\n",
      " if os.environ.get('U3MP_PARTMP119','1')=='0':image[rw2+0x4454]|=0x08\n"
      " # x028: U3MP_VOX_VOICEFIX=1 -> ec[1] bit30: a SayRequest that arrives with voice 0 gets ec[201] (last mesh pick) or drake\n"
      " if os.environ.get('U3MP_VOX_VOICEFIX','0')=='1':image[rw2+0x4454]|=0x40000000\n")
