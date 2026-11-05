# x028b (v7.37, scratch/coop-voice2): the co-op voice diagnostic learns where a requested line resolves. Builds on v7.36
# (kinds 131..133, slots 0x13780/0x14080/0x14100 from scratch/tools/edits_v736.py); the stubs and the kind dispatch stay.
# Result of v7.36 (run baseline-1793586362153677500, Syria): every scripted hero line (syr-coop-drk-*, all in vox-mp.bin)
# exits SayRequest with code 8 "no vox entry" while a vox.bin entry (a1bce128) is accepted in the same level. The data fix is
# scratch/coop-voice2/apply_coop_vox.py (the co-op lines duplicated into the overlay vox.bin); this build only sharpens the readout.
# Kind 131 (SayRequest entry) now also asks the game's own symbol lookup 0x11f31b0(sid, module, 0) where the line sid lives:
#   ec[246] found in module vox (0x4eb9055f)   ec[247] in vox-mp (0x71993c4b)   ec[248] in vox-igc (0xfc53fc6b)
#   ec[249] in any loaded module (0xaaefa679)  ec[250] number of symbols in the global table ([[0x1b3aea0+0x140]+8])
# The counters leave the polluted slots: eb[247] SayRequest calls (was ec[230]), eb[248] say events at 0x4b3610 (was ec[254],
# n09_d's), eb[249] calls that arrived with voice 0 (was ec[255]). ec[231..236] as before; the exit-code histogram shrinks to
# ec[238..245] (codes 0..6, bucket 7 = 7 and above) so ec[246..250] are free for the lookups. ec[237] last result (r12b).
C='u3_lan_profiles.c'
OLD_IN='''EXT static void vox_req_in119(uintptr_t base,uint64_t *r){
 uint8_t *c=(uint8_t*)r[9];X.ec[230]++;X.ec[234]=(uint32_t)r[10];X.ec[235]=(uint32_t)r[12];
 if(!stage_ptr_ok(c)||((uintptr_t)c&7))return;
 X.ec[231]=(uint32_t)(*(uint64_t*)c-(uint64_t)base);X.ec[232]=*(uint32_t*)(c+0xfc);
 X.ec[233]=c[0x104]|(uint32_t)*(uint8_t*)(base+0x1b5a830u)<<8|(uint32_t)*(uint8_t*)(base+0x17d5c61u)<<24;
 if(!*(uint32_t*)(c+0xfc)){X.ec[255]++;
  if(X.ec[1]&0x40000000u){uint32_t v=X.ec[201]?X.ec[201]:0x3b6fa41cu;*(uint32_t*)(c+0xfc)=v;X.ec[232]=v;}}}   /* U3MP_VOX_VOICEFIX=1 */
EXT static void vox_req_out119(uint64_t *r){
 uint32_t code=(uint32_t)r[1];X.ec[236]=code;X.ec[237]=(uint32_t)r[3]&0xff;X.ec[238+(code<15u?code:15u)]++;}
'''
NEW_IN='''EXT static void vox_req_in119(uintptr_t base,uint64_t *r){
 uint8_t *c=(uint8_t*)r[9];uint32_t sid=(uint32_t)r[10];X.eb[247]++;X.ec[234]=sid;X.ec[235]=(uint32_t)r[12];
 {void *(*lk)(uint32_t,uint32_t,uint64_t)=(void *(*)(uint32_t,uint32_t,uint64_t))(base+0x11f31b0u);   /* DC symbol lookup (sid, module, 0), as 0x4a7350 calls it */
  X.ec[246]=lk(sid,0x4eb9055fu,0)!=0;X.ec[247]=lk(sid,0x71993c4bu,0)!=0;X.ec[248]=lk(sid,0xfc53fc6bu,0)!=0;X.ec[249]=lk(sid,0xaaefa679u,0)!=0;
  uint8_t *reg=*(uint8_t**)(base+0x1b3aea0u+0x140);X.ec[250]=stage_ptr_ok(reg)?*(uint32_t*)(reg+8):0xffffffffu;}
 if(!stage_ptr_ok(c)||((uintptr_t)c&7))return;
 X.ec[231]=(uint32_t)(*(uint64_t*)c-(uint64_t)base);X.ec[232]=*(uint32_t*)(c+0xfc);
 X.ec[233]=c[0x104]|(uint32_t)*(uint8_t*)(base+0x1b5a830u)<<8|(uint32_t)*(uint8_t*)(base+0x17d5c61u)<<24;
 if(!*(uint32_t*)(c+0xfc)){X.eb[249]++;
  if(X.ec[1]&0x40000000u){uint32_t v=X.ec[201]?X.ec[201]:0x3b6fa41cu;*(uint32_t*)(c+0xfc)=v;X.ec[232]=v;}}}   /* U3MP_VOX_VOICEFIX=1 */
EXT static void vox_req_out119(uint64_t *r){
 uint32_t code=(uint32_t)r[1];X.ec[236]=code;X.ec[237]=(uint32_t)r[3]&0xff;X.ec[238+(code<7u?code:7u)]++;}
'''
rep(C,OLD_IN,NEW_IN)
rep(C,' if(kind==133){X.ec[254]++;return 1;}   /* x028: say event handler 0x4b3610 */',
      ' if(kind==133){X.eb[248]++;return 1;}   /* x028: say event handler 0x4b3610 */')
