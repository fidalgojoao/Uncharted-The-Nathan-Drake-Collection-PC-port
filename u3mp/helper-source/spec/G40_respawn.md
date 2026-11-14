# G40. Respawn: helpers and the per-frame state machine

Behaviour description for a reimplementation (reimpl2 pipeline, describer's document). It says what the functions
do, not how the old ones were written. All offsets are hexadecimal when prefixed with 0x. "Image offset X" means the
address `base + X`, where `base` is the game image base. All multi-byte values are little-endian; "u8 / u16 / u32 /
s16 / s32 / u64" are integers of that width, "pointer" is 8 bytes. Reads may be unaligned (a pointer at an odd
multiple of 4 occurs). Names follow `G25_coop_lists.md` (net player `p`, state object `s`, natives),
`G11_game_sockets_time.md` (net clock) and `G35_custom_game_start.md` (late join, ec[n]).

**Float.** This group has no float arithmetic. Two things are float PATTERNS handled with integer operations only:
the respawn delay (a 32-bit float pattern converted to ticks by the integer rule of 4.1) and the fade level
(0x3f800000 = 1.0 or 0x00000000 = 0.0, stored as a plain word). Nothing here uses the x87 unit or the `f_*` tools.

## 1. Purpose and callers

The PS4 game (built from PS3 1.10) lost the piece that respawns a LOCAL net player: on PS3 a per-frame function
walks each local player through "no character yet: fade, ask for the levels around the spawn point, wait, spawn"
and "character dead: remove the body after the delay". The helper restores it (rules of PS3 1.19), with the
screen fade, the out-of-bounds kill and the late-join rule of Elimination.

Two parts, independent of each other (part B reaches part A only through the C part's `u3h_` exports, which
forward to part A):

| Part | Content | Exported names |
|---|---|---|
| **A** | the helpers: delay to ticks, type object lookup, default spawner test, spawn locator, fade event, level request, levels-ready test, current net game | `u3x_f_ticks`, `u3x_dc_type_object`, `u3x_default_spawner_exists`, `u3x_spawn_locator`, `u3x_spawn_fade`, `u3x_spawn_region_request`, `u3x_spawn_levels_ready`, `u3x_netgame_cur` |
| **B** | the late-join fade rule, the per-frame state machine, and two small tests it needs | `u3x_late_join_fade`, `u3x_respawn_tick`, `u3x_net_late_joining`, `u3x_net_coop_mode` |

Callers (all stay as they are):

- `u3x_respawn_tick`: the C one-line wrapper `respawn_tick`, called by the C function `net_local_players_tick`
  for each of the two local net players (pointers at manager + 0x370 and + 0x378; null ones skipped; index 0 / 1
  is the `idx` argument). That function runs from two code stubs of the builder inside the game's own two
  per-frame functions for the local net players (image 0x722e00 and the match's net task 0x7230f0), on the game
  thread; from the first one, players whose u8 at +0x5e4 (leaving) is set are skipped by the caller. It calls
  nothing when native 53 or native 62 of the state object is null: part B may rely on both being set. The other
  natives of 3.2 are resolved together with them and are called without a test, as before. Everything of this
  group runs on that one thread; nothing is locked.
- `u3x_late_join_fade`: `u3x_respawn_tick`, and the C wrapper `late_join_fade119` (which has no caller left).
- Part A functions: each has a C one-line wrapper of the old C name (`f_ticks`, `dc_type_object`,
  `default_spawner_exists`, `spawn_locator`, `spawn_fade`, `spawn_region_request`, `spawn_levels_ready`,
  `netgame_cur`) and, for the ones the C++ part calls back, a `u3h_` export forwarding to that wrapper
  (`u3h_f_ticks`, `u3h_spawn_locator`, `u3h_spawn_fade`, `u3h_spawn_region_request`, `u3h_spawn_levels_ready`,
  `u3h_netgame`). Users of those exports: `u3x_respawn_tick` (all five spawn ones and `u3h_f_ticks`);
  `u3x_spoiler_can_pick` of G25 (`u3h_f_ticks`); the camera function `u3x_camera_tick` of G42
  (`u3h_spawn_fade`); many list sources (`netgame_cur`). Inside part A, `u3x_spawn_locator` calls
  `u3x_default_spawner_exists`, and `u3x_spawn_region_request` calls `u3x_netgame_cur`, `u3x_spawn_locator` and
  `u3x_dc_type_object` directly.
- `u3x_net_late_joining`, `u3x_net_coop_mode`: C wrappers `net_late_joining`, `net_coop_mode` and the exports
  `u3h_net_late_joining`, `u3h_net_coop_mode` (used by this group, G25, G35, G38, G42 and others).

No wrapper changes: every function keeps its name and signature; no function of this group is still in C.

## 2. Interface

### 2.1 Functions to deliver

| Function | Signature | Result |
|---|---|---|
| delay to ticks | `int64_t u3x_f_ticks(uint32_t f, int64_t half)` | see 4.1 |
| type object | `void *u3x_dc_type_object(uintptr_t base, uint32_t key)` | object pointer or null |
| default spawner | `int u3x_default_spawner_exists(uintptr_t base, void *s)` | 1 or 0 |
| spawn locator | `int u3x_spawn_locator(uintptr_t base, void *s, uint8_t *p, void *loc, uint32_t spawner, uint32_t index)` | 1 or 0; `loc` = 64 bytes, 16-aligned, filled by the game |
| fade event | `void u3x_spawn_fade(uintptr_t base, void *s, uint8_t *p, int on)` | none (`p` is only read) |
| level request | `void u3x_spawn_region_request(uintptr_t base, void *s, uint8_t *p, uint32_t spawner)` | none |
| levels ready | `int u3x_spawn_levels_ready(uintptr_t base, void *s, uint32_t spawner)` | 1 or 0 |
| current net game | `uint8_t *u3x_netgame_cur(uintptr_t base, void *s)` | record pointer or null |
| late-join fade | `int u3x_late_join_fade(uintptr_t base, void *s, const uint8_t *p)` | the fade to show: 1 (black) or 0 |
| respawn tick | `void u3x_respawn_tick(uintptr_t base, void *s, uint8_t *p, int idx)` | none; `idx` is 0 or 1 |
| late joining | `int u3x_net_late_joining(uintptr_t base)` | 1 or 0 |
| co-op mode | `int u3x_net_coop_mode(uintptr_t base, void *s)` | 1 or 0 |

All are `extern "C"`. One private piece is part of part A: the tag search of 4.3.

### 2.2 Functions of other groups, of the C part and shared ones (declare, do not write)

| Name | Signature | What it does | Part |
|---|---|---|---|
| `u3h_ext` | `uint8_t *u3h_ext(void)` | the helper's extension state (CONVENTIONS.h) | B |
| `u3h_mp_code_is` | `int u3h_mp_code_is(uintptr_t base, uint32_t off, const char *hex12)` | 1 when the 12 bytes at image offset `off` equal the 24 lower-case hex digits of `hex12`, else 0; reads only | A |
| `u3h_crc_sid` | `uint32_t u3h_crc_sid(const char *p)` | the game's 32-bit name checksum of a zero-terminated text (pure) | A |
| `u3x_sym_table` | `uint8_t *u3x_sym_table(void *s, uint32_t id)` | data of a script symbol: one call of the state's lookup function (pointer at `s` + 656) when that pointer is not null; null when the pointer, the returned handle or the pointer the handle holds is null | A |
| `u3x_type_is_coop` | `int u3x_type_is_coop(uintptr_t base, int32_t type)` | G38: 1 when `type` is a co-op game type (it reads the game's own mask from the image), else 0 | B |
| `u3x_netgame_type` | `int32_t u3x_netgame_type(uintptr_t base, void *s)` | existing one-liner: the s32 at +0xc of `u3x_netgame_cur(base, s)`, 0 when that is null | B |
| `u3h_netgame_type` | `int32_t u3h_netgame_type(uintptr_t base, void *s)` | C export forwarding to `u3x_netgame_type` | B |
| `u3h_np_has_effect` | `int u3h_np_has_effect(void *s, uint8_t *p, uint32_t h)` | non-zero when net player `p` carries the effect with id `h` (one call of native 56 with (`p`, `h`, null); its low result byte) | B |
| `u3h_np_alive` | `int u3h_np_alive(void *s, uint8_t *p)` | non-zero when `p` is alive: it has effect 0xc054e78b; or its character handle (`p` + 0x10, 3.4) is valid and s32 `p` + 0x39c ≤ s32 `p` + 0x3a0; or its u8 at +0x770 is non-zero (tests in this order; the first makes one native 56 call) | B |
| `u3h_respawn_delay` | `uint32_t u3h_respawn_delay(void *s, uint8_t *p)` | the respawn delay of `p` in seconds as a 32-bit float pattern (one call of native 55) | B |
| `u3h_f_ticks` | `int64_t u3h_f_ticks(uint32_t f, int64_t half)` | forwards to `u3x_f_ticks` | B |
| `u3h_screen_player` | `uint8_t *u3h_screen_player(uintptr_t base, uint8_t *p)` | the character object of `p`'s screen: the object of the handle (3.4) at image 0x1bb9500 + 0x10 · (s32 at `p` + 0x1e8), or null; reads only | B |
| `u3h_net_late_joining` | `int u3h_net_late_joining(uintptr_t base)` | forwards to `u3x_net_late_joining` | B |
| `u3h_net_coop_mode` | `int u3h_net_coop_mode(uintptr_t base, void *s)` | forwards to `u3x_net_coop_mode` | B |
| `u3h_spawn_fade` | `void u3h_spawn_fade(uintptr_t base, void *s, uint8_t *p, int on)` | forwards to `u3x_spawn_fade` | B |
| `u3h_spawn_locator` | `int u3h_spawn_locator(uintptr_t base, void *s, uint8_t *p, void *loc, uint32_t spawner, uint32_t index)` | forwards to `u3x_spawn_locator` | B |
| `u3h_spawn_region_request` | `void u3h_spawn_region_request(uintptr_t base, void *s, uint8_t *p, uint32_t spawner)` | forwards to `u3x_spawn_region_request` | B |
| `u3h_spawn_levels_ready` | `int u3h_spawn_levels_ready(uintptr_t base, void *s, uint32_t spawner)` | forwards to `u3x_spawn_levels_ready` | B |
| `u3h_vis_oob_tick` | `int u3h_vis_oob_tick(uintptr_t base, void *s, uint8_t *p, uint8_t *pl, int32_t now, int test)` | G41: the out-of-bounds grid test of character `pl` of net player `p` at net time `now`; it maintains the kill time at `p` + 0x848 | B |
| `u3h_camera_tick` | `void u3h_camera_tick(uintptr_t base, void *s, uint8_t *p, int idx)` | G42: the camera half of the per-frame function (death / spectate cameras) | B |
| `u3x_replication` | `void u3x_replication(uintptr_t base, void *s, uint8_t *np, uint8_t *player)` | the player state sends of net player `np` with character `player` (another group) | B |

Part B calls the `u3h_` forms named in section 4 (that is what the old code does, and what the tester's stand-ins
record); it never calls a part A function directly.

## 3. Game facts

### 3.1 Image data

| Image offset | Type | Meaning |
|---|---|---|
| 0x17cb060 | object | the net game manager ("manager") |
| manager + 0x320 (0x17cb380) | s32 | a countdown of the match's net task; while it is above 0 the fade stays black |
| manager + 0x661c | s16 | number of the current net game record |
| manager + 0x66e8 (0x17d1748) | s32 | net time at which the current round started |
| 0x17b4ca4 | s32 | the net clock, ticks of 1/300 s ("now") |
| 0x17c3580 | pointer | the net clock object in use (G11) or null; s32 at +0xc = its time |
| 0x17d5c61 | u8 | non-zero while multiplayer is active |
| 0x17d6420 | object | the match session: u64 at +0x18 non-zero = it has a room; u32 at +0x12e4 and +0x12e8 equal = this machine leads it |
| 0x1b69530 | s32 | first word of the game's late-join status (G35); above 3 = this machine is past the first late-join steps |
| 0x1afcf10 | object | the level manager ("streamer"); u8 at +0x8a10 (0x1b05920) is copied into level requests |
| 0x1b3ee38 | handle (3.4) | the net game process (receiver of the fade event) |
| 0x1b3ee70 | hash table (3.5) | the game's type objects by id |
| 0x1bb9500 + 0x10·n | handle (3.4) | the character of screen n; its id word is at 0x1bb9508 + 0x10·n |
| 0x1805fa0 + 0x270·n | object | the HUD object of screen n (n = 0, 1) |
| 0x1702720 | pointer to function | the game's string id function: called as (s32 0, text, s32 1), returns the u32 id of the text |
| 0x17006d0, 0x1700670 | objects | the event argument type markers "float" and "integer" (only their addresses are used) |

### 3.2 Game functions

Called directly (System V AMD64 convention, integer arguments only):

| Image offset | Arguments | Result | Meaning | Check before the call |
|---|---|---|---|---|
| 0x11b7d60 | (HUD object of the screen, u32 0x100000) | none | "this player is alive" request to the HUD | its first 12 bytes are 09 77 08 8B 47 38 85 C0 74 4E 48 8D |
| 0x72fb00 | (manager) | pointer | the level object holding the match's spawner list, or null | `u3h_mp_code_is(base, 0x72fb00, "554889e553504889fbbf7d06")` |
| 0x1197380 | (result block, null, u32 event id, process, arg1, arg2, arg3, arg4) | none | send an event with up to four arguments to a process | `u3h_mp_code_is(base, 0x1197380, "554889e54157415641554154")` |

The two texts must be kept in U3X_RO arrays. The 12-byte comparison of 0x11b7d60 was made by a file-private byte
compare in the old code; the new code may make it itself or use `u3h_mp_code_is` with the text
`0977088b473885c0744e488d` (both only read).

Called through the state object's natives (pointer to function at `s` + 768 + 8·k; orientation only: always call
through the native):

| k | Arguments | Result | Meaning |
|---|---|---|---|
| 13 | (u32 symbol id) | pointer | value of a script symbol: the data pointer itself (no handle in between) |
| 42 | (streamer, u32 name checksum) | pointer | find a level; null when unknown. u32 at level + 0x30 = its state: 8 = loaded, 0xc = failed / missing |
| 53 | (`p`, u32 spawner id, s32 force) | u8 | the game's "spawn this net player at the spawner" |
| 54 | (`p`, locator out, u32 spawner id, u32 index) | u8 | fills the 64-byte, 16-aligned locator of spawn point `index` of the spawner; non-zero = found. Its first 16 bytes are the position |
| 57 | (object) | none | kill a process (here: the dead character) |
| 58 | (streamer, u32 spawner id) | pointer | find a spawner object, or null |
| 59 | (streamer, list block) | none | fills the list of levels that are wanted but not loaded yet: u32 count at +0, 32 text pointers from +8 (block of 264 bytes) |
| 60 | (streamer, request block) | none | want-load the levels around a position (request layout in 4.7) |
| 61 | (streamer, level name text) | none | want-load a level by name |
| 62 | (type object or null, u32 0x9f2aeafb, u32 0xd3b86d60) | u64 | an id taken from a type object, used in the level request |

Virtual calls of 4.11: "slot X of object O" = the function whose address is the pointer at (pointer at O + 0) + X
bytes, called with O as its only argument.

### 3.3 The spawner filter stub

At address `s` − 0xfce0 the builder places its guarded entry to the game's spawner filter (game function
0x72fbc0): called as (manager, spawner object) it returns a u8, non-zero when the spawner is usable in the
current game. It is recognised by its first four bytes 48 8B 46 68.

### 3.4 Handles

A handle is 16 bytes: pointer H at +0 (to a slot holding the object pointer), u32 id at +8. It is valid when H is
not null, the id is not 0, the pointer O at H is not null and the u32 at O + 0x7c equals the id; its object is
then O. (The shared one-liners `u3x_handle_obj_valid` / `u3x_handle_obj` of the old file do this; they stay, but
are not in CONVENTIONS.h: the test is written out where needed.)

### 3.5 The type object table (image 0x1b3ee70)

Pointer at +0 = bucket array (8-byte entries), s32 at +0x3c = number of buckets N. Bucket of a u32 key = FNV-1a
(32-bit, public: offset basis 0x811c9dc5, prime 0x01000193) over the key's four bytes, low byte first, modulo N
(unsigned). Each bucket entry is a pointer to a chain node or null; node: pointer to the next node at +0, u32 key
at +0x10, object pointer at +0x18. The array entry with index N (one past the last bucket) holds the address of
the table's end node, which is not an object.

### 3.6 Tag tables and the spawner list

A spawner (and any level entity) has at +0x68 a pointer to its tag table, or null: pointer D at +0 to the entries,
u32 count at +8. Entry = 16 bytes: u32 tag id at +0, pointer to the value at +8. The entries are sorted by tag id,
ascending, unsigned; a tag id may occur several times in a row. A value starts with a 16-bit type and has an
8-byte pointer at +4. Types used: 1 (the pointer at +4 leads to a u32), 5 (the pointer at +4 is a text), 0xd (the
pointer at +4 leads to a u32 id). Tags used: 0x5bfae217 (marks a default spawner), 0x38a0841f (a level the
spawner needs).

The level object returned by 0x72fb00 has at +0x190 a pointer to the spawner list, or null: pointer at list + 8 to
an array of spawner pointers, s32 at list + 0x10 = their number.

### 3.7 Script symbols

- 0x3b9a067d, the net game table (through `u3x_sym_table`): s32 number of records at +0, pointer at +8 to the
  records of 0xd0 bytes. Record: u32 id at +0, u32 map id at +4, s32 game type at +0xc.
- 0x0dec97a4, the map table (through native 13): s32 number of entries at +0, pointer at +8 to entries of 0x98
  bytes. Entry: u32 map id at +0, pointer at +0x70 to the name text of the map's own level (may be null or empty).

### 3.8 Net player fields (`p`)

| Offset | Type | Meaning |
|---|---|---|
| 0x10 | handle (3.4: pointer at 0x10, id at 0x18) | the player's character |
| 0x60 | u8 | non-zero: the respawn handling is off for this player |
| 0x61 | u8 | non-zero: no spawn, screen shown |
| 0x1e8 | s32 | screen number |
| 0x21c | s32 | team; 1 = the hunters' side in co-op |
| 0x39c | s32 | net time of the last death (the time the respawn delay counts from) |
| 0x3a0 | s32 | net time of the last spawn |
| 0x3a8 | s32 | net time of the last spawn request answered by the host; 0 = never |
| 0x3b4 | u32 | id of the spawner to spawn at |
| 0x3b8 | u8 | non-zero: the spawner need not exist (the locator is asked without the test of 4.5) |
| 0x420 | u8 | non-zero: a respawn request is pending |
| 0x424 | u32 | spawn point index within the spawner |
| 0x5f9 | u8 | joined late (G35) |
| 0x5fa | u8 | has spawned in this round (G35) |
| 0x7c0 | s32 | step of the spawn sequence: 0, 1, 2 |
| 0x848 | s32 | net time at which the out-of-bounds kill is due; ≤ 0 = none |

### 3.9 State object (`s`) and extension state

| Address | Type | Meaning |
|---|---|---|
| `s` + 656 | pointer to function | script symbol lookup (used through `u3x_sym_table`) |
| `s` + 768 + 8·k | pointer to function | native k |
| `s` + 0x20c0 | u32 | switch word: bit 0 (1) = no HUD alive request; bit 5 (0x20) = failed levels still count as pending |
| `s` + 0x2098 | u32 | counter of HUD alive requests |
| `s` + 0x3d40 | u32 rs[0..] | rs[0]: its LOW BYTE is the "spawn now" flag the builder's stub in the spawn function reads (only that byte is written); rs[2 + idx] trace word; rs[4] spawns made; rs[5] bodies removed; rs[6] last spawn result; rs[7] level requests; rs[8] pending levels (raw); rs[9] locator refusals seen by the tick; rs[10] last delay pattern; rs[11] last now |
| `s` + 0x3d78 | u32 rs2[0..] | rs2[0] switches: bit 1 (2) = no fades, bit 2 (4) = debug: force the out-of-bounds kill, bit 5 (0x20) = ask G41 for its test mode; rs2[3] fades sent; rs2[4] last fade; rs2[5] pending levels (raw); rs2[6] pending levels ignored; rs2[7] locator refusals; rs2[8] last spawn result; rs2[9] spawns made; rs2[10] out-of-bounds kills |
| `s` + 0x3dc0 | u32 life[0..3] | life[idx] = net time at which the current life of local player idx was first seen; life[2 + idx] = the character handle id of that life |
| `u3h_ext()` + 0x4450 | u32 ec[0..] | ec[0] bit 1 (2) = the Elimination late-join rule is off; ec[17] = rule evaluations |
| `u3h_ext()` + 0x2094 | u32 | counter of real out-of-bounds kills |

Times: 300 ticks = 1 s. 1500 = 5 s, 4500 = 15 s, 6000 = 20 s, 150 = 0.5 s.

## 4. Behaviour

### 4.1 `u3x_f_ticks(f, half)` (part A, pure)

`f` is a float pattern (seconds): S = bit 31, E = bits 23..30, M = bits 0..22. T = `half` · 150. All arithmetic in
signed 64 bits.

1. E = 0 and M = 0 (either zero): result −T.
2. The magnitude Q, in units of 1/65536 tick. With A = (M + 2^23) · 300 and k = E − 134:
   - k > 30: Q = 2^46 (a cap; infinities and NaN land here);
   - 0 ≤ k ≤ 30: Q = A · 2^k;
   - k < 0: Q = A divided by 2^(−k), rounded down (0 once −k is 33 or more: do not shift by 64 or more).
   The hidden bit is added also when E = 0 (denormals); their Q is 0 anyway.
3. V = (−Q when S is set, else Q) − T · 65536.
4. Result = V / 65536 with the fraction cut off TOWARD ZERO (for negative V: minus the rounded-down quotient of
   −V).

Callers pass `half` = 0, 1 or 2. Examples: (0x40a00000 = 5.0, 0) → 1500; (5.0, 1) → 1350; (0x3f000000 = 0.5, 2) →
−150; (0xbf800000 = −1.0, 0) → −300; (0x3c23d70a ≈ 0.01, 0) → 2; (0x3c23d70a, 1) → −147 (not −148);
(0x7f800000, 0) → 2^30; (0x52000000 = 2^37, 0) → 300 · 2^37; (0x52800000 = 2^38, 0) → 2^30.

### 4.2 `u3x_dc_type_object(base, key)` (part A, reads only)

With the table of 3.5: null when the bucket array pointer is null or N ≤ 0. Otherwise walk the chain of the key's
bucket from its first node; at the FIRST node whose key equals `key`: null when that node's address equals the
array entry with index N, else the node's object pointer. Null when the chain ends without such a node.

### 4.3 The tag search (private, part A)

Given entries D, a count and a tag id: the lowest index i such that the tag id of entry i is not below the wanted
one (unsigned), or the count when there is none. It is the standard lower-bound halving search (as C++
`std::lower_bound`: with n entries left from index L, probe index L + n/2; when its id is below the wanted one
continue with the n − n/2 − 1 entries after it, else with the n/2 entries before it), so that the same entries are
read as before even on a table that is not sorted. A count of 0 reads nothing and gives 0.

### 4.4 `u3x_default_spawner_exists(base, s)` (part A)

Tells whether the match's level has a usable spawner marked as default. In this order:

1. `u3h_mp_code_is(base, 0x72fb00, ...)` is 0: result 1 ("cannot tell: assume yes"), nothing else.
2. Call 0x72fb00(manager). Null, or a null spawner list pointer (3.6): result 0.
3. Read the list's count and array pointer. The first four bytes at `s` − 0xfce0 are not 48 8B 46 68: result 1.
4. For index i = 0, 1, ... while the array pointer is not null, i is below the count (s32) and below 4096: take
   spawner pointer i. It is skipped when null, or when the filter stub (3.3), called with (manager, spawner),
   returns a zero byte, or when its tag table pointer or the table's entry pointer D is null. Otherwise search
   tag 0x5bfae217 (4.3, with the table's count); when the index found is below the count and that entry's tag id
   is 0x5bfae217, look at its value pointer: result 1 when the value pointer is not null, the value's type (u16)
   is 1, the pointer at value + 4 is not null and the u32 it leads to is not 0. Only the first entry of that tag
   is looked at.
5. Result 0.

The filter is called once per non-null spawner until the result is known; nothing is written.

### 4.5 `u3x_spawn_locator(base, s, p, loc, spawner, index)` (part A)

1. When the u8 at `p` + 0x3b8 is 0: call native 58 (streamer, `spawner`). When it returns null: call
   `u3x_default_spawner_exists(base, s)`; when that is 0: rs2[7] += 1 and the result is 0 (native 54 is not
   called). (Native 58 is not called when the byte is set; the default test is not made when the spawner exists.)
2. Otherwise call native 54 (`p`, `loc`, `spawner`, `index`); result 1 when its result byte is non-zero, else 0.

### 4.6 `u3x_spawn_fade(base, s, p, on)` (part A)

Sends the fade level of `p`'s screen to the net game process, which draws the black fade.

1. Nothing when rs2[0] has bit 1. Nothing when `u3h_mp_code_is(base, 0x1197380, ...)` is 0 (asked only when the
   bit is clear).
2. Level = 0x3f800000 when `on` is non-zero or the s32 at manager + 0x320 is above 0, else 0.
3. Process = the object of the handle at image 0x1b3ee38 (3.4), or null (the event is sent all the same).
4. Screen = s32 at `p` + 0x1e8.
5. Four argument records of three u64 each (24 bytes; keep them consecutive in one block): record 1 = {level
   pattern (upper half 0), 0, address of image 0x17006d0}; record 2 = {screen sign-extended to 64 bits, 0, address
   of image 0x1700670}; records 3 and 4 all zero. A result block of 64 zero bytes, 16-aligned.
6. Call 0x1197380(result block, null, u32 0xa1a13050, process, record 1, record 2, record 3, record 4): the
   records are passed by address (the last two addresses go on the stack).
7. rs2[3] += 1; rs2[4] = (screen as u32, shifted left 31) OR (level shifted right 1).

### 4.7 `u3x_spawn_region_request(base, s, p, spawner)` (part A)

Asks the streamer for the levels around the spawn point and for the map's own level.

1. G = `u3x_netgame_cur(base, s)` (may be null; asked first, in every case).
2. A 64-byte, 16-aligned locator, all zero; `u3x_spawn_locator(base, s, p, locator, spawner, 0)` (index 0, not
   the player's index). Result 0: return.
3. Copy the locator's first 16 bytes to a 16-aligned position block.
4. O = `u3x_dc_type_object(base, 0x6e51b48f)`; I = native 62 (O, 0x9f2aeafb, 0xd3b86d60), called also when O is
   null.
5. Request block, 0x20 bytes, 16-aligned, zero except: pointer to the position block at +0; u64 I at +8; u32
   0x2743c4b0 at +0x10; u32 at +0x14 = the u32 at G + 0 (0 when G is null); u8 at +0x19 = the u8 at streamer +
   0x8a10. Call native 60 (streamer, request block). rs[7] += 1.
6. Return when G is null. M = native 13 (0x0dec97a4); return when null. Go through the map table's entries (3.7)
   in order (none when the entry pointer is null or the count is ≤ 0) to the FIRST one whose map id equals the
   u32 at G + 4; none: return. For that entry: return when its level name pointer is null or the name is empty;
   L = native 42 (streamer, `u3h_crc_sid(name)`); return when L is not null and the u32 at L + 0x30 is 8;
   otherwise call native 61 (streamer, name).

### 4.8 `u3x_spawn_levels_ready(base, s, spawner)` (part A)

Tells whether the spawn may go ahead: no level is pending, or a pending one is a level the spawner names.

1. A list block (3.2, native 59) of 264 zero bytes; call native 59 (streamer, block). C = its count. rs[8] = C;
   rs2[5] = C.
2. Drop failed levels. For j = 0 .. min(C, 32) − 1 in order: name j; when it is not null and not empty, L =
   native 42 (streamer, `u3h_crc_sid(name)`), else no call and L = null. The name is DROPPED when L is not null,
   the u32 at L + 0x30 is 0xc and the switch word (`s` + 0x20c0) has bit 5 clear; otherwise it is kept. Kept names
   are moved to the front of the same array in their order (slots behind them keep what they held). C' = number
   kept, plus C − 32 when C is above 32. rs2[6] = number dropped.
3. P = native 58 (streamer, `spawner`). When P is null or C' is 0: result = 1 when C' is 0, else 0.
4. Result 0 when P's tag table pointer is null, or the table's entry pointer is null, or its count is 0.
5. F = the string id function (pointer read from image 0x1702720 at this point). "Id of a text" = 0 for a null or
   empty text without a call, else F(0, text, 1). First = the tag search (4.3) for 0x38a0841f.
6. For k = 0 .. min(C', 32) − 1 in order: W = id of the text in slot k of the array (one call per k when the
   text is not empty, made before the entries are looked at; with C above 32 the slots past the kept names still
   hold the earlier pointers and are used as they are). Then for each entry from index First on, while the index
   is below the count and the entry's tag id is 0x38a0841f: V = its value pointer; N = 0; when V is not null and
   its type (s16) is 5: N = id of the text the pointer at V + 4 is; when V is not null and its type is 0xd: N =
   the u32 the pointer at V + 4 leads to (not tested for null). Result 1 at the first entry with N = W (also
   when both are 0).
7. Result 0.

### 4.9 `u3x_netgame_cur(base, s)` (part A)

The record of the net game the match uses: T = `u3x_sym_table(s, 0x3b9a067d)` (the lookup is made first, in every
case); i = the s16 at manager + 0x661c. Null when T is null, i < 0, i ≥ the s32 at T + 0, or the record pointer
at T + 8 is null; else that pointer + 0xd0 · i.

### 4.10 `u3x_late_join_fade(base, s, p)` (part B)

The fade for a player who joined a running match and has not been given a spawn: black, except in Elimination
(game type 10), where a player who already spawned in this round sees the game again 5 s after the round's start.

1. ec[0] has bit 1: result 1 (nothing else).
2. `u3h_netgame_type(base, s)` is not 10: result 1. The u8 at `p` + 0x5fa is 0: result 1.
3. The pointer at image 0x17c3580 is null: result 1.
4. D = (s32 at that object + 0xc) − (s32 at manager + 0x66e8), as a 32-bit difference. ec[17] += 1. Result 0 when
   D ≥ 1500, else 1.

### 4.11 `u3x_respawn_tick(base, s, p, idx)` (part B)

Runs once per frame for local net player `p` (local index `idx`). Steps in this order; "fade(v)" =
`u3h_spawn_fade(base, s, p, v)`; "camera" = `u3h_camera_tick(base, s, p, idx)`.

**1. HUD alive request.** When the switch word (`s` + 0x20c0) has bit 0 clear, and then `u3h_np_alive(s, p)` is
non-zero, and then the 12 bytes at image 0x11b7d60 are the expected ones (3.2): with n = s32 at `p` + 0x1e8, when
0 ≤ n < 2: call 0x11b7d60(image 0x1805fa0 + 0x270 · n, 0x100000) and add 1 to the u32 at `s` + 0x2098. (Each
test is made only when the one before it held.)

**2. Early ends.** Return (no camera) when `u3h_np_has_effect(s, p, 0xc054e78b)` is non-zero. Then return (no
camera) when the u8 at `p` + 0x60 is non-zero.

**3. Values for this frame.** now = s32 at image 0x17b4ca4; delay = `u3h_respawn_delay(s, p)`; rs[10] = delay;
rs[11] = now. Read once and keep for the whole call: death = s32 at `p` + 0x39c, request = s32 at `p` + 0x3a8,
team = s32 at `p` + 0x21c.

**4. No character** (`u3h_screen_player(base, p)` is null). rs[2 + idx] = 0x100 OR the u32 at `p` + 0x7c0. Then
exactly one of (a), (b), (c), then camera, then return:

(a) Late join. When `u3h_net_late_joining(base)` is non-zero, and request is 0, and (asked only then)
`u3h_net_coop_mode(base, s)` is 0 or team is not 1: fade(`u3x_late_join_fade(base, s, p)`).

(b) Not to spawn yet. Otherwise, when request < death; or the u8 at `p` + 0x61 is non-zero; or request < 1 and
(asked only then, after the two tests before it failed) `u3h_net_coop_mode(base, s)` is non-zero and team is 1:
fade(0).

(c) The spawn sequence. Otherwise: spawner = u32 at `p` + 0x3b4; a 64-byte, 16-aligned locator, all zero;
`u3h_spawn_locator(base, s, p, locator, spawner, u32 at p + 0x424)`. Result 0: rs[9] += 1 and nothing more.
Otherwise by the step (s32 at `p` + 0x7c0):
- step 0: `u3h_spawn_region_request(base, s, p, spawner)`; fade(1); step = 1.
- step 1: fade(1); then the step becomes 2 when (`u3h_netgame_type(base, s)` is 0xb and the u8 at `p` + 0x5f9 is
  0) or else, asked only when that does not hold, `u3h_spawn_levels_ready(base, s, spawner)` is non-zero.
- step 2: fade(0); u8 at `p` + 0x420 = 0; the low byte of rs[0] = 1; R = native 53 (`p`, spawner, 1), its result
  byte; the low byte of rs[0] = 0; then write back s32 `p` + 0x39c = death and s32 `p` + 0x3a8 = request (the
  values of step 3: the spawn function overwrites both); u8 at `p` + 0x420 = 0; step = 0; s32 at `p` + 0x3a0 =
  the net clock read again (image 0x17b4ca4); rs[4] += 1; rs[6] = R; rs2[8] = R; rs2[9] += 1.
- any other step value: nothing.

**5. Character present.** rs[2 + idx] = 0x200 OR the u32 at `p` + 0x7c0.

**6. Out-of-bounds kill.** C = `u3h_screen_player(base, p)` (asked again). I = the u32 at image 0x1bb9508 +
0x10 · (s32 at `p` + 0x1e8). K = the s32 at `p` + 0x848 (read and written in place below).
- New life: when C is not null and life[2 + idx] is not I: life[2 + idx] = I; life[idx] = now.
- Age = now − life[idx] as s32.
- When rs2[0] has bit 2 (debug): when C is not null and Age > 4500: K = now + 1500 if K ≤ 0 (else unchanged);
  otherwise K = 0.
- Else, when C is not null: `u3h_vis_oob_tick(base, s, p, C, now, t)` with t = 1 when rs2[0] has bit 5 and Age
  > 6000, else 0 (result unused; G41 maintains K).
- The kill: when C is not null and K > 0 and K ≤ now and `u3h_np_alive(s, p)` is non-zero: H = the pointer at
  C + 0x598. When H is not null: call slot 0x78 of H; D = the pointer returned by slot 0x118 of H; when D is not
  null: store the u64 value 0x11a89844 at D + 0x14 (eight bytes: the damage type id and four zero bytes), call
  slot 0x530 of C, rs2[10] += 1, and, when rs2[0] has bit 2 clear, add 1 to the u32 at `u3h_ext()` + 0x2094.
  Then K = 0 (whether or not H or D was null).

**7. State sends.** `u3x_replication(base, s, p, u3h_screen_player(base, p))` (the character asked a third time).

**8. Dead with a character.** When `u3h_np_alive(s, p)` is 0: Since = now − death (32-bit difference). The body
is removed when all hold: delay is a positive finite pattern (bit 31 clear, the low 31 bits not all 0, bits
23..30 not all 1); and (death < request, or `u3h_f_ticks(delay, 2)` < Since); and `u3h_f_ticks(delay, 1)` <
Since (64-bit comparisons). To remove it: O = the object of the handle at `p` + 0x10 (3.4); when O is not null:
native 57 (O) and rs[5] += 1. Then camera, return. (How many times the pure `u3h_f_ticks` is asked is free.)

**9. Alive.** fade(0); camera.

### 4.12 `u3x_net_late_joining(base)` (part B, reads only)

0 when the match session (image 0x17d6420) has no room (u64 at +0x18 is 0) or this machine leads it (u32 at
+0x12e4 equals u32 at +0x12e8). Otherwise 1 when the s32 at image 0x1b69530 is above 3, else 0.

### 4.13 `u3x_net_coop_mode(base, s)` (part B)

0 when the u8 at image 0x17d5c61 is 0 (nothing else is asked). Otherwise
`u3x_type_is_coop(base, u3x_netgame_type(base, s))`.

## 5. Tables

No table of game values belongs to this group (none of INVENTORY section C). Two near-tables, both resolved:

- The co-op game type set (the old `u3x_net_coop_mode` held it as a literal 15-bit mask): not copied; G38's
  `u3x_type_is_coop` reads the game's own mask from the image (G38 3.9 b).
- FNV-1a's two constants (3.5): a public algorithm.

The single ids of section 3 and 4 (tags, event id, effect id, damage type id, symbol ids, request words) are
interoperability constants, not tables.

## 6. Test scenarios

Testable off-line with a fake image, a fake state object whose natives are call-recording stand-ins, and
recording stand-ins for the functions of 2.2 (compare memory, results, order and arguments of every call):

**Part A**
1. `u3x_f_ticks` (pure): the examples of 4.1; every E from 0 to 255 with M = 0, 1, 0x7fffff, both signs, `half`
   0, 1, 2; random patterns.
2. `u3x_dc_type_object`: null array; N = 0 and N < 0; key in the first / a later node; key absent; the matching
   node being the end node (array entry N); two nodes with the same key (first wins).
3. Tag search: counts 0, 1, 2, 7, 8; wanted id below all, above all, present once, present three times in a row,
   absent between two; an unsorted table (same entries read).
4. `u3x_default_spawner_exists`: code check fails (1, no game call); null level; null list; filter bytes wrong
   (1); null array with a positive count (0); count above 4096 (only 4096 looked at); spawners null / refused by
   the filter / without tag table / with null entry pointer; tag absent; tag present with value null, type 2,
   type 1 with null pointer, type 1 leading to 0, type 1 leading to non-zero (1).
5. `u3x_spawn_locator`: byte at +0x3b8 set (only native 54); spawner found; not found and default exists; not
   found and none (rs2[7], result 0, no native 54); native 54 returning 0 / 1 / 0x100 (low byte 0 → 0).
6. `u3x_spawn_fade`: rs2[0] bit 1; code check fails; `on` 0 with countdown 0 / 5 / −1; `on` 1; process handle
   valid / null slot / id 0 / id mismatch; screen 0 and 1 (rs2[4] bit 31); the eight arguments and the contents of
   the four records and of the result block at the call.
7. `u3x_spawn_region_request`: locator refused (only the net game lookup and the locator calls); null net game
   (request word 0, no map step); null type object; map table null / empty / without the map / name null / name
   empty / level loaded (state 8) / level unknown / level in another state (native 61); request block bytes.
8. `u3x_spawn_levels_ready`: count 0 (1, with and without spawner); pending levels and no spawner (0); levels in
   state 0xc with the switch bit clear (dropped; all dropped → 1) and set (kept); null and empty names; count 33
   and 40 with drops (stale slots, C'); spawner without tags / with count 0; tag values of type 5 (match, no
   match, empty text), type 0xd, another type, null value with an empty pending name (1); several entries of the
   tag; the calls of the string id function in order.
9. `u3x_netgame_cur`: null table; index −1, 0, count − 1, count; null record pointer.

**Part B**
10. `u3x_late_join_fade`: ec[0] bit 1; type 9 / 10 / 11; byte +0x5fa 0 / 1; null clock; D = 1499, 1500, negative,
    wrapped; ec[17].
11. `u3x_respawn_tick`, step 1: switch bit 0 set (no alive call); not alive; wrong code bytes; screen −1, 0, 1, 2.
12. Step 2: effect present; byte +0x60 set (neither reaches the camera).
13. No character: (a) with late joining and request 0, co-op off / co-op on with team 0 / team 1 (falls to (b):
    request < 1, co-op, team 1 → fade 0); (b) request < death; byte +0x61; (c) locator refused (rs[9]); step 0;
    step 1 with type 0xb and late flag 0 (no levels-ready call), type 0xb with late flag 1, other type, ready 0 /
    1; step 2 (order of the writes around native 53, a stand-in that changes +0x39c, +0x3a8 and +0x420 and reads
    the low byte of rs[0]; upper bytes of rs[0] unchanged; +0x3a0 from a clock changed by the stand-in); step 3
    (nothing). Camera once in every case.
14. Character present: new life / same life; debug bit with Age 4500 / 4501 and K 0, positive, negative; grid
    path with the test flag at Age 6000 / 6001 with and without bit 5; kill due with K = now, K = now + 1, K = 0;
    not alive at the kill; H null; D null; counters with and without the debug bit; the eight bytes at D + 0x14.
15. Dead with a character: delay patterns 0, negative, infinity, NaN, 5.0; death < request with Since on both
    sides of delay − 0.5 s; death ≥ request with Since on both sides of delay − 1 s; handle valid / stale; rs[5].
16. Alive: fade(0), camera.
17. `u3x_net_late_joining`: the four combinations and status 3 / 4. `u3x_net_coop_mode`: byte 0 (no lookup);
    byte 1 with a stand-in for `u3x_type_is_coop`.

Needs the live game: that a local player really spawns (and not twice) at match start, after a death, after a
late join and as a co-op hunter; the fade going black and clearing; levels around a far spawn point loading
before the spawn; the body disappearing after the delay; the out-of-bounds kill; Elimination late join after 5 s.

## 7. Must stay as it is

- The builder's stubs: the "spawn now" byte (low byte of rs[0]) is read by the stub inside the game's spawn
  function, and the filter stub at `s` − 0xfce0; their addresses and the 1-then-0 writing around native 53 are
  fixed.
- The C wrappers and `u3h_` exports of section 1, and `net_local_players_tick` (the helper's own glue).
- The existing one-liners this group uses or is used by (`u3x_netgame_type`, `u3x_np_has_effect`, `u3x_np_alive`,
  `u3x_screen_player`, `u3x_respawn_delay_bits`, `u3x_handle_obj_valid`, `u3x_handle_obj`, `u3x_f_positive`,
  `u3x_net_now`, `u3x_loc_now`): single reads / one game call each; not delivered again.
- The counters and trace words of 3.9 (the test rigs and the boot log read them).

## 8. Oddities to keep (the new code must do the same)

1. `u3x_f_ticks`: the cap of 2^46 for k > 30 is BELOW the uncapped values of large delays with k ≤ 30 (every
   delay above 2^30 ticks, about 3.58 million seconds, and below 2^38 s gives more than the cap's 2^30 ticks), so
   the result is not monotonic: delays of 2^38 s and more, infinity and NaN give 2^30 (with the sign applied).
   −0.0 gives −T.
2. `u3x_spawn_levels_ready`: with more than 32 pending levels the slots behind the kept names are compared again
   (stale pointers), and the levels past the 32nd count as pending without being looked at; an empty pending name
   "matches" a tag entry whose value is null or of another type (both ids 0); a value of type 0xd with a null
   pointer at +4 is read without a test.
3. `u3x_default_spawner_exists`: the answer is 1 when the game function or the filter stub is not recognised, but
   0 when the level or list is missing; the filter-stub bytes are tested only after the level call.
4. `u3x_spawn_region_request` asks the locator of spawn point 0, not of the player's spawn index, and calls
   native 62 with a null type object.
5. `u3x_spawn_fade` sends the event with a null process when the handle is stale, and still counts it.
6. `u3x_respawn_tick`: the character of the screen is asked three times; the "positive delay" test is written
   out instead of asking `u3x_f_positive`; the kill writes eight bytes at D + 0x14; K is cleared when the kill
   was due even if nothing could be killed; the screen number is range-checked only for the HUD request, not for
   the handle id read of step 6; in step 4 (a) / (b) the co-op test can run twice in one call; the two early
   ends skip the camera half while every other path runs it.
7. `u3x_late_join_fade` counts ec[17] only when it reaches the time comparison.

## 9. Open questions

1. `u3x_net_coop_mode` through `u3x_type_is_coop` (G38) depends on the coordinator keeping that shared function;
   G38 notes that the mask's place in the image was checked in the image file, not in the running game. If the
   coordinator prefers, the type set of G30 3.11 as a rule gives the same answers.
2. The meaning of the net player bytes +0x60, +0x61 and +0x3b8 is taken from the code's use only (PS3 1.19 never
   sets the first one); nothing depends on the names.
3. Whether the tester records `u3h_f_ticks` / `u3h_np_alive` calls: the number of `u3h_f_ticks` calls in 4.11
   step 8 is declared free (pure function); all other call counts are fixed as written.
4. Declared free (not observable in any reachable case): the number of `u3h_ext()` calls (a plain accessor; the
   old `u3x_late_join_fade` asks it once at its start, the kill of 4.11 step 6 once when it counts); and the
   behaviour of the tag tables of 3.6 with 2^28 or more entries (the old code formed entry offsets in 32 bits).
5. The co-op mask: the old `u3x_net_coop_mode` accepted game types 4 to 18 by a literal 15-bit set; the tester
   must confirm that the u32 the G38 function reads from the image (G38 4.1) gives the same answer for every type.

## Clarifications

Facts confirmed by the tester's differential test against v7.212 (2026-11-14). Where a point corrects the text
above, this section holds.

1. **Tag values of type 5 (3.6, 4.8 step 6) - correction.** The 8-byte pointer at value + 4 does not point at the
   text: it leads to an 8-byte TEXT POINTER (the stored value, as it leads to the stored u32 for types 1 and 0xd),
   and the level name is the text that second pointer points at. N of such an entry = the id of that text: 0
   without a call when the text pointer is null or the text is empty, else one call of the string id function
   with that text. The pointer at value + 4 itself is not tested for null (as for type 0xd).
2. **Pending level count (4.8).** It is unsigned in every step. With a count of 2^31 or more the first 32 names
   are looked at, C' = kept + C - 32 (32-bit unsigned), rs[8] and rs2[5] hold the raw count, and min(C', 32) slots
   are compared.
3. **Switch word bit 5 in 4.8 step 2.** v7.212 reads the switch word for each pending level it finds in state
   0xc, after that level's native 42 call. Reading it once after native 59 gives the same result in every case in
   which no native 42 / checksum call writes the helper's switch word (the game's functions do not).
4. **4.4, null array pointer against the filter-stub bytes.** The order is: code check; level call; null level or
   null list gives 0; then the four filter-stub bytes (wrong: 1); only then the array pointer (null: no spawner is
   looked at, 0). A null array pointer together with wrong stub bytes gives 1.
5. **4.7 step 6, map table.** The search ends at the first entry whose map id matches, whatever that entry leads
   to (null or empty name: nothing; level loaded: nothing; else native 61). Later entries with the same map id are
   never looked at.
6. **4.11 step 6, read points.** rs2[0] is read at each use: for the debug bit and bit 5 before the grid test (no
   call in between), and again, after the three function table calls of the kill, for the real-kill counter (a
   debug bit that changed during those calls decides). K is read from the net player after the grid test call: a
   kill time G41 sets in this frame is used in this frame. Age uses life[idx] as it is after the new-life step.
7. **4.11 step 4 (c), read points.** The spawner id and the spawn point index are read before the locator call
   (the id read there is the one passed to natives and to the part A exports in every step); the step is read
   after the locator call. death, request and team are the values of step 3 everywhere.
8. **4.11 step 8, `u3h_f_ticks` calls.** None when the delay is not a positive finite pattern. With death <
   request: one call (half 1). Otherwise half 2 first, and half 1 only when the half-2 comparison held. The new
   code makes the same calls in the same order.
9. **4.11 step 4, co-op test and team.** In (a) and in (b) `u3h_net_coop_mode` is asked before the team is
   compared (it is asked also when the team is not 1); in (b) only after request < death and the +0x61 byte both
   failed and request < 1 held.
10. **4.11 step 1, guard bytes.** v7.212 does not call `u3h_mp_code_is` for 0x11b7d60: it compares the 12 bytes
    itself, after `u3h_np_alive` answered non-zero. Each of the 12 bytes counts.
11. **4.13, co-op mask.** The u32 at image 0x721100 of the PS4 image file (file offset 0x725100) is 0x000072b1,
    the old literal set; G38's rule with it gives the old answer for all 2^32 game type values.
12. **4.1.** Old and new `u3x_f_ticks` are equal for all 2^32 patterns with half 0, 1 and 2. A zero exponent with
    a non-zero mantissa (denormal) gives -T like zero does (Q is 0).
13. **Results.** `u3x_default_spawner_exists`, `u3x_spawn_locator`, `u3x_spawn_levels_ready`,
    `u3x_late_join_fade`, `u3x_net_late_joining` and `u3x_net_coop_mode` return exactly 1 or 0; the natives' and
    the filter stub's u8 results are used by their low byte only (garbage above it is ignored).
