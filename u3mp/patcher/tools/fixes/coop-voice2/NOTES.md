# coop-voice2: co-op adventure character lines (x028)

PS3 addresses are 1.19 (Oct 2013 EBOOT, scratch/ps3-119/EBOOT.elf, sha256 aac4faa6..., identical to the old Downloads ELF;
scratch/coop-voice2/ppc.py reads it). "disc 01.10" = the D: GOTY disc EBOOT (0b961c31), cross-checked only.

## Cause (proven by run baseline-1793586362153677500, v7.36, Syria opening)
* The scripted hero lines reach the player's vox controller and SayRequest (PS4 0x4af5f0 = PS3 1.19 0x1b5540 area):
  controller vtable 0x16a06e8, voice 0x3b6fa41c (drake), vox state byte 1, MP byte 1 (st=1000100). Voice is not the problem.
* Every scripted line exits with reject code 8 "no vox entry": 0cd17383 syr-coop-drk-breaths-alright, d716663c
  syr-coop-drk-find-out-where, 07cb660d syr-coop-drk-around-tower, 48c1e3aa syr-coop-drk-heh. One entry, a1bce128, was
  accepted (code 0) in the same level: it lives in vox.bin. The four rejected ones live only in vox-mp.bin (retail and overlay).
* So in a co-op adventure level the vox-mp module's symbols are not resolvable while vox.bin's are. The lookup 0x4a7350
  (PS3 1.19 0x1b1cd8 sets the same three module ids at init) asks the DC symbol table 0x11f31b0 for (sid, module) with
  module = vox (0x4eb9055f), then vox-igc (0xfc53fc6b, flag 0x1b5a360), then vox-mp (0x71993c4b, flag 0x1b5a361); the flags
  are set unconditionally in the vox init 0x4a7c20 (called from GameInit 0x569ac0), so only the module's presence can fail.
* Data: every say / wait-say / say-pre-roll line of every co-op adventure script (apply_coop_vox.py, coop_lines.pkl) is a
  vox-mp.bin entry: airport 104, dig 129 (1 also in vox-igc), fort 51 (overlay script), ldn 61 (3 in vox.bin, 4 also in
  vox-igc), mon 72, syria 50; shambhala / fort-b / mon-2 have none. Their xvag files exist (overlay speech/english or
  english_ps4.psarc). So the whole co-op dialog of every map hangs on vox-mp resolving, which it does not.
* Not proven: why vox-mp does not resolve in co-op (never requested by the co-op level, or loaded under another id). Nothing
  names vox-mp except modules.bin itself (no module lists it, no pak manifest, no code outside the vox lookups), on PS3 1.19
  and PS4 alike, so the request path could not be found statically. The v7.37 readout (below) tells which of the two it is.

## Fix: scratch/coop-voice2/apply_coop_vox.py (dry run by default, --apply, --restore; backup work/backup-coop-voice2/)
Copies the 464 vox entries the co-op scripts request from vox-mp.bin into the overlay vox.bin (entry, block and line
records byte for byte, only the three pointer slots rewritten, type word kept, symbol table re-sorted, reloc bitmap rebuilt,
every old symbol and pointer verified) and sets the vox entry's size word in the overlay modules.bin (54629 -> 145767; the
field tracks each module's .bin size and the overlay copy was regenerated for its bigger files before). vox.bin is the module
the lookup searches first and the one proven to resolve in co-op, so the lines resolve whatever keeps vox-mp out; versus is
unaffected (the vox copy is found first and is identical). Covers every co-op adventure game in net.bin (netgame table
0x3b9a067d, type 11): 129 dig/Borneo (drake, sullivan, chloe), 130 London (drake, cutter, sullivan), 131 Monastery (sullivan,
elena, chloe), 132 Syria (drake, sullivan, elena), 133 Airport (drake, sullivan, cutter), 226 Fort (lazarevic, eddie,
flynn). Per map, after the fix, requested line sids resolving in vox.bin: airport 104/104 (drk 31, sul 16, ctr 12, laz 23,
edy 13, fln 5), dig 129/129 (drk 44, sul 32, chl 27, drz 12), fort 51/51 (laz 14, edy 19, fln 12, pirates 5), ldn 61/61
(drk 30, sul 10, ctr 15, chl 3), mon 72/72 (drk 25, sul 14, eln 15, chl 11, ctr 1, pirates 5), syria 50/50 (drk 27, eln 12,
sul 11): each slot's hero has lines in its own voice (prefixes drk/sul/chl/ctr/eln match the hero rows above; the Fort
villains laz/edy/fln). Cutscene (IGC) lines of these scripts are the same entries (the 5 dual vox-mp/vox-igc ones included).

## Diagnostic (kept, sharpened): edits_v737_voxdiag2.py for build_next.py 7.36 7.37
Kind 131 now also asks 0x11f31b0 where the requested sid resolves: ec[246] vox, ec[247] vox-mp, ec[248] vox-igc, ec[249]
any module, ec[250] global symbol count; counters moved to eb[247..249] (ec[230]/ec[254] were shared with older writers);
histogram ec[238..245]. Read with read_voxdiag.py (--v736 for the old layout). Verified to apply, compile and link on a
scratch copy of v7.36. With the data fix installed the expected reading is code 0 (accepted) and vox=1 for the co-op lines.

## What the user should hear
The scripted hero banter and cutscene lines of the co-op adventures, e.g. Syria's syr-coop-drk-* / eln / sul lines from the
opening on, Airport's drake/sully/cutter lines plus Lazarevic/Eddy/Flynn taunts, Borneo's drake/sully/chloe and Draza,
London's drake/cutter/sully, Monastery's drake/sully/elena/chloe, Fort's villains. Verify: install the fix (--apply), play
Syria's opening; SayRequest must exit with code 0 for 0cd17383 / d716663c / 07cb660d / 48c1e3aa.

## Earlier findings (still valid)
* x025's ec[238+i*12] readout never read the player (np+0x10 is not the Player handle; vtable 0x16a10e0, pointer changes
  every tick); it was removed in v7.36.
* The mesh-based voice pick (h002, vt+0x40 0x4af2e0) works in co-op: picks of chloe / drake at every spawn, so Player init's
  voice-0 path (0x691f0a; PS3 1.19 0x2b0f70, table rows with no list) is not the cause.
* SayRequest's reject 7 (voice 0) exists identically on PS3 1.19 (0x1b55d8..0x1b55e4 -> 0x1b6e90); SetVoxCharacter 0x4aec80
  = 1.19 0x1b1a6c (slot 0x1c, stores +0xa0 at 0x1b1aa4).
