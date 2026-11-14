# G30. Post-match handler: match counters, page routing, submits; and the search re-arm

Behaviour description for a reimplementation (reimpl2 pipeline, describer's document). It says what the functions
do, not how the old ones were written. All offsets are hexadecimal when prefixed with 0x. "Image offset X" means the
address `base + X`, where `base` is the game image base every function receives. All multi-byte values are
little-endian; "u8 / u16 / s16 / u32 / s32 / u64" are unsigned / signed integers of that width, "pointer" is 8
bytes. A "float pattern" is the 32-bit pattern of a single-precision float, carried in a u32. Additions to counters
and record fields are plain 32-bit additions (wrapping).

## 1. Purpose and callers

When a multiplayer match ends, the game calls its post-match handler once. The PS4 game carries the handler of
PS3 version 1.10 (image 0x678b00). The builder replaces it by a stub of kind 67, so that the helper runs the 1.19
behaviour instead: clear the "in a match" mark of the profile and save it, reset the menus, ask for the menu level,
route the page stack (leaving, back to the Custom Game lobby, or back to matchmaking), tell the session backend and
the party, and, for each local user, add the match to the counters of the multiplayer record (co-op, unranked or
competitive: games, wins / ties / losses with 1.19's weekly fields, booster and kickback use, challenges), save
the record and send the leaderboard scores.

Call chain today: `u3_profile_event` (C, stub kind 67, only with a non-zero `base`) -> C `lb_kind` ->
`u3x_lb_kind` (G29) -> C export `u3h_pm119` -> C `pm119` -> `u3x_pm119`. One call per match end, on the game's
main thread. Nothing in this group runs on another thread and nothing takes a lock.

The group also holds the small queries about the current net game that the handler and other groups use, and the
"search re-arm": after a matchmade match that the helper's offline matchmaking stand-in started, the page stack
goes back to the matchmaking page and a new search of the same playlist is armed.

Three parts, to be written independently:

- **Part A, queries and tools** (12 small functions): the current net game's record, type, co-op test, the
  "results count" test, session lead, a backend call, the page id stack, the menu reset, the playlist key, the
  re-arm switch.
- **Part B, one user's match** (3 functions): the counters of one local user, the weapon statistics block, the
  leaderboard blob and submits.
- **Part C, the handler and the re-arm** (2 functions).

`net_game` (old file, near line 7611; the inventory lists it here) is NOT part of this group: G19 covers it.

## 2. Interface

No function takes or returns a float by value. All are `extern "C"`. "existing" = exported today, keeps name and
signature. "new" = a new exported name.

### 2.1 Part A

| Function | Signature | Status | Called by |
|---|---|---|---|
| net game record | `uint8_t *u3x_pm_ngentry(uintptr_t base, void *s)` | existing | this group; G22 (`u3x_pm_challenge`); C `pm_ngentry` (and through it the C export `u3h_netgame_entry`) |
| game type | `int32_t u3x_pm_type(uintptr_t base, void *s)` | existing | this group; G22; C `pm_type` (export `u3h_pm_type`) |
| co-op test | `int u3x_pm_coop(uintptr_t base, void *s)` | existing | this group; G22; C `pm_coop` (exports `u3h_pm_coop`, `u3h_coop`) |
| results count | `int u3x_pm_ranked(uintptr_t base, void *s)` | existing | this group; C `pm_ranked` (the co-op score code) |
| session lead | `int u3x_pm_leader_of(uintptr_t base, uintptr_t obj)` | existing | this group only (C declares it, no C caller) |
| backend call | `void u3x_pm_party_vcall(uintptr_t base, uint32_t off, int arg, int three)` | existing | this group only |
| stack clear | `void u3x_pm_pg_clear(uintptr_t base)` | existing | this group; C `pm_pg_clear` |
| stack push | `void u3x_pm_pg_push(uintptr_t base, uint32_t id)` | existing | this group; C `pm_pg_push` |
| menu reset | `void u3x_pm_menu_reset(uintptr_t base, uintptr_t menu)` | existing | this group only |
| playlist key | `uint32_t u3x_pm_map(uintptr_t base, void *s)` | existing | this group only |
| playlist test | `int u3x_pm_map_is(uintptr_t base, void *s, uint32_t id)` | existing | this group only |
| re-arm switch | `int u3x_n13_on(void)` | existing | this group only |

Parameters: `s` = the helper's state object (opaque; only handed on). `obj` = the image offset of a session (3.4).
`off` = byte offset of an entry in the backend's function table (a multiple of 8); `arg` = the third argument;
`three` = non-zero: the call has three arguments, zero: two. `menu` = the image offset of a menu object (3.5).
`id` = a page id, or a playlist key for `u3x_pm_map_is`.

The C wrappers `pm_ngentry`, `pm_type`, `pm_coop`, `pm_ranked`, `pm_pg_clear`, `pm_pg_push` stay as they are (one
line each, same arguments).

### 2.2 Part B

| Function | Signature | Status | Called by |
|---|---|---|---|
| one user's match | `void u3x_pm_user(uintptr_t base, void *s, uint32_t u)` | new (the old file-private `pm_user`) | part C |
| weapon statistics | `void u3x_pm_weapon_stats(uintptr_t base)` | existing | this part |
| leaderboard submits | `void u3x_pm_lb_submits(uintptr_t base, void *s, uint32_t u)` | existing | this part |

`u` = local user index, 0 or 1.

### 2.3 Part C

| Function | Signature | Status | Called by | C wrapper passes |
|---|---|---|---|---|
| the handler | `void u3x_pm119(uintptr_t base, void *s, uint8_t *param)` | existing | C `pm119` | its three arguments |
| search re-arm | `void u3x_n13_rearm(uintptr_t base, void *s)` | new (C `n13_rearm`) | this part; C `n13_rearm` | `base`, `s` |

`param` = the first argument the game gave its own handler (word 0 of the stub's event): an object described in
3.9. `s` of `u3x_n13_rearm` is not used.

The C function `n13_rearm` becomes a one-line wrapper. Its only C user is the export `u3h_n13_rearm`, whose only
user was the old handler; the new handler calls `u3x_n13_rearm` directly, so wrapper and export are left without a
caller (the integrator may drop them).

### 2.4 Functions of other groups, of the C part and of the shared tools that this group calls

Declare them; do not reimplement them.

| Name | Signature | What it does | Part |
|---|---|---|---|
| `u3h_ext` | `uint8_t *u3h_ext(void)` | the helper's own state block (3.1). In CONVENTIONS.h | A, B, C |
| `u3h_lg_profile` | `uint8_t *u3h_lg_profile(uintptr_t base, uint32_t user)` | profile object of a local user: the pointer at image 0x17dd610 plus `user` · 0x7b00, or null when that pointer is null. A plain address accessor | A, B, C |
| `u3h_lg_rec` | `uint8_t *u3h_lg_rec(uintptr_t base, void *s, uint8_t *profile)` | the multiplayer record of a profile object (G22 4.1): returns `profile` + 0x248; on its first call for a profile in a certain state it resets the record first. The profile is not tested for null | A, B, C |
| `u3h_publish` | `int u3h_publish(uintptr_t base, void *s, uint8_t *profile)` | saves the multiplayer record of a profile object (file and, when signed in, the upload to the online service); result not used | B, C |
| `u3h_party_send` | `int u3h_party_send(uintptr_t base, uint32_t msg, const void *data, uint32_t len)` | hands a party message to the online interface (its sender entry; offline it does nothing); result not used | C |
| `u3h_m03` | `uint32_t *u3h_m03(void)` | the helper's matchmaking stand-in state (3.10) | C |
| `u3h_n09_reset` | `void u3h_n09_reset(uintptr_t base)` | NEW one-line C export, to be added by the integrator: calls the C function `n09_reset(base)` (the helper's own reset of the difficulty vote) | C |
| `u3h_n02_arm` | `void u3h_n02_arm(uint32_t now)` | NEW one-line C export, to be added by the integrator: sets the C part's two search-popup words: "popup gone" count = 0 and "armed at" = `now` (C statics `n02_gone`, `n02_tarm`) | C |
| `u3x_sym_table` | `uint8_t *u3x_sym_table(void *s, uint32_t id)` | data of a script symbol through the state's lookup function, or null (G22 2) | A |
| `u3x_lg_beta` | `int u3x_lg_beta(uintptr_t base)` | G22 4.2: 1 when the running title is one of the demo / beta titles, else 0. Pure read | A |
| `u3x_session_host` | `int u3x_session_host(const uint8_t *session)` | G12: non-zero when the session has no room id (u64 at +0x18 is 0) or its own member id (+0x12e4) equals its host member id (+0x12e8) | A |
| `u3x_net_player_id` | `uint32_t u3x_net_player_id(const uint8_t *np)` | a net player's id (G12 3.1) | B, C |
| `u3x_menu_set_state` | `void u3x_menu_set_state(uintptr_t base, uint8_t *menu, uint32_t state)` | G33: announces a menu state change to the menu's listeners (game calls 0xf23970 and 0x6534e0, see 4.9) and writes the state | A |
| `u3x_shows_match_stats` | `int u3x_shows_match_stats(uint32_t mode, int private_party)` | 1 when `mode` is not 0, 3 or 8 and `private_party` is 0 | C |
| `u3x_lb_off` | `int u3x_lb_off(void)` | G29: non-zero when the leaderboard feature is switched off (ea[1] bit 4) | B |
| `u3x_lb_submit` | `int u3x_lb_submit(uintptr_t base, void *s, uint32_t board, int64_t score, const uint8_t *blob)` | G29: queues one score with its 0x40-byte blob for a leaderboard and sends it to the online service (the function that sends; this group never talks to a server itself); result not used | B |
| `u3x_pm_challenge` | `void u3x_pm_challenge(uintptr_t base, void *s, uint32_t u, uint32_t id, int32_t v, int bit)` | G22 4.10: one challenge counter | B |
| `u3x_pm_365c5c` | `void u3x_pm_365c5c(uintptr_t base, void *s, uint32_t u, int32_t mine, int32_t theirs)` | G22 4.11: the win challenge of the game type | B |
| `u3x_pm_365da8` | `void u3x_pm_365da8(uintptr_t base, void *s, uint32_t u, uint32_t map, int32_t a3)` | G22 4.12: co-op map counters and difficulty challenges | B |

`u3h_ext`, `u3h_lg_profile` and `u3h_m03` are address accessors: how often they are called is not part of the
behaviour. For `u3h_lg_rec` see 3.3.

### 2.5 Strays (old functions in no work group that this document covers)

All are described here and delivered by the part named:

- `u3x_pm_type` (in none of the inventory's lists), `u3x_pm_coop`, `u3x_pm_leader_of`, `u3x_pm_pg_clear`,
  `u3x_pm_map`, `u3x_pm_map_is`, `u3x_n13_on` (inventory: "trivial" list): part A.
- File-private pieces of the old file that disappear into the new code: the record fetch and "add to a record
  field" helpers (3.3), the accessor of the re-arm mark (ea[94]), the title compare (replaced by `u3x_lg_beta`,
  4.4), the 64-bit integer to float conversion (6.1 step 14).
- The second literal copy of the demo / beta title ids (STRAYS.md, noted by the G22 describer) is gone: 4.4.

## 3. Game and helper facts

### 3.1 The helper's state block (address from `u3h_ext()`)

Counter array "ea": u32 words from offset 0x3c50 (`ext_ea()` of CONVENTIONS.h). Switch array "ec": u32 words from
offset 0x4450 (ec[n] at 0x4450 + 4·n; CONVENTIONS.h has no accessor for it).

| Word | Meaning |
|---|---|
| ea[59] | handler runs |
| ea[60] | leaderboard submit rounds (one per user handled) |
| ea[61] | handler runs that reached the end of the return routing |
| ea[91] | leaving runs with the re-arm mark set |
| ea[92] | re-arms |
| ea[93] | the routing word of the last run (6.1 step 2) |
| ea[94] | the re-arm mark: non-zero = the running match was started by the matchmaking stand-in (set by the C part) |
| ec[3] | switches; bit 8 (0x100) set = the re-arm is off |

ea[57] belongs to G22's challenge counter.

### 3.2 Short names for game objects

| Name | Image offset | What |
|---|---|---|
| NET | 0x17d5c58 | the net state |
| NGM | 0x17cb060 | the net game manager |
| MS | 0x17d6420 | the match session (G12 3.2) |
| PS | 0x17dc298 | the party session (same layout, G21 3) |
| MENU | 0x17bd080 | the menu of screen 0 |
| MENU2 | 0x17bfa10 | the menu of screen 1 |
| SM | 0x17dd778 | the match score object |
| CLOCK | 0x17b4ba4 | s32, the game clock, 300 ticks per second |
| ABORT | 0x16e7170 | u8, the "leaving the match" mark |

### 3.3 Profiles and the multiplayer record

"REC(u)" = `u3h_lg_rec(base, s, u3h_lg_profile(base, u))`: the multiplayer record of local user u (G22 3.5). The
old code fetched it anew for every field it touched. The new code may fetch it less often, with one rule: the
first fetch for a user is made before the first read or write of that user's record (the first fetch can reset the
record). The number of fetches is not part of the behaviour. Null profiles are not tested for anywhere.

"Add v to field F of REC(u)" = the s32 at REC(u) + F grows by v (v is first widened to 32 bits as said for each
value).

Record fields this group uses (offsets from the record):

| Offset | Type | Use here |
|---|---|---|
| 0x52c | u32 | the key of the playlist the profile picked (read from user 0 only) |
| 0x558 | u32 | "in a match" mark; the handler clears it for user 0 |
| 0x5a4, 0x5a8 | s32 | competitive totals fed by stat words W8 and WC (3.7) |
| 0x5ac | s32 | wins |
| 0x5b0 | s32 | losses (also counted for an aborted match) |
| 0x5b4 | s32 | competitive total fed by stat word W48 |
| 0x5b8 | s32 | competitive total fed by the member value V14 |
| 0x5bc, 0x5c0 | s32 | co-op totals fed by TOTAL and V14 |
| 0x60c | s32 | ties |
| 0x614, 0x618 | s32 | wins, losses of two-team games while NGM byte 0x34d is set (and 0x34e is not) |
| 0x61c, 0x620 | s32 | wins, losses of two-team games while NGM byte 0x34e is set |
| 0x654, 0x658 | s32 | totals of POINTS for game type 2, for game type 0xf |
| 0x65c | s32 | total fed by stat word WA |
| 0x65c + 4·X, X = 1..4 | s32 | co-op games at difficulty step X (0x660, 0x664, 0x668, 0x66c) |
| 0x13fc + 4·r | s32 | uses of booster r (written as offset 4·(r + 0x4fd) + 8) |
| 0x14fc + 4·r | s32 | uses of kickback r (written as offset 4·(r + 0x53d) + 8) |
| 0x15fc, 0x1600 | s32 | co-op games, co-op games ended with mode 4 |
| 0x1604 | s32 | competitive games |
| 0x1c54, 0x1c58 | s32 | leaderboard totals fed by W8 and WC |
| 0x1eac | u32 | copied into the leaderboard blob |
| 0x1f24 | s32 | weekly losses |
| 0x1f28, 0x1f2c | s32 | weekly co-op totals (V14, TOTAL) |
| 0x1f30 | s32 | weekly competitive total (V14) |
| 0x1f34 | s32 | weekly wins |
| 0x1f38 | s32 | read only: the score of board 0x515 |

For the two "uses" rows the offset is computed in 32-bit unsigned arithmetic, ((u32)(r + 0x4fd)) · 4 + 8 (or with
0x53d), and added as an unsigned 32-bit number to the record address: r = −1 gives 0x13f8 (0x14f8).

### 3.4 Sessions (MS and PS)

| Offset | Type | Meaning |
|---|---|---|
| 0x08 | pointer | backend object, null = none. Its first 8 bytes point to its function table (8-byte entries) |
| 0x18 | u64 | room id, 0 = none |
| 0xc8 | u8 | non-zero while the session is in a room |
| 0x12e4, 0x12e8 | u32 | own member id, host member id |
| 0x1334 | u8 | (PS) "leave asked": 1 makes the game's party machine leave the party room |
| 0x1339 | u8 | (MS) non-zero = the match was aborted for the whole session |

Backend function table entries used (names as in G14): entry 7 at byte 0x38 "Leave" (backend, session); entry 8
at 0x40 "SetAttr" (backend, session, int); entry 11 at 0x58 "RoomFlags" (backend, session, int); entry 13 at 0x68
"SetAttr4" (backend, session, int). No result is used.

### 3.5 Menus (MENU, MENU2)

| Offset | Type | Meaning |
|---|---|---|
| 0x008 | u32 | UI state |
| 0xdb4 | u32 | transition timer |
| 0xdb8 | float pattern | transition speed |
| 0xef8 | s32 | depth of the page id stack |
| 0xefc + 4·k | u32 | page id number k of the stack (the top one is k = depth) |

Page ids: 0x37656388 (the root page of the stack), 0x53cbb5f2 (multiplayer page), 0xb99a3235 (the lobby),
0xe35f0f1a (matchmaking page), 0x0c3a3716 (match statistics page), 0xb808d03c (the page shown when leaving).
0x5ef96af1 is the second argument of every "show page" call here.

### 3.6 NET and NGM fields

| Field | Type | Meaning |
|---|---|---|
| NET + 0x08 (image 0x17d5c60) | u8 | LAN byte |
| NET + 0x09 (image 0x17d5c61) | u8 | multiplayer-active byte |
| NET + 0x40 (image 0x17d5c98) | u32 | non-zero = the match came from matchmaking (a search runs / goes on) |
| NET + 0x78 (image 0x17d5cd0) | u64 | the match room id |
| NET + 0x3e4 (image 0x17d603c) | s32 | the net menu mode (G18 3.7; 2 = Custom Game). Only the value 1 matters here: results of a match played in mode 1 count |
| NET + 0x40d (image 0x17d6065) | u8 | the co-op byte of LAN mode |
| NGM + 0x34d, + 0x34e | u8 | two marks that choose which win / loss pair of 3.3 counts |
| NGM + 0x370 + 8·u | pointer | the net player of local user u (u = 0, 1), null = none |
| NGM + 0x380 | pointer | the net player message 0x105 goes to (the old notes call it the host's; G14 3.1 lists the same field as "the local net player") |
| NGM + 0x390 + 0x9c0·i, i = 0..9 | record | net player slot i: u8 at +0xe1 non-zero = joined; u32 at +0x21c = team |
| NGM + 0x661c | s16 | index of the current net game |
| NGM + 0x6808 | u32 | match state (0 = no match) |
| NGM + 0x680c | u32 | the "mode" word of the ended match (the argument of `u3x_shows_match_stats`) |
| NGM + 0x6a24 (image 0x17d1a84) | u8 | non-zero = film playback |
| NGM + 0x6a48 (image 0x17d1aa8) | u32 | co-op difficulty step "X" |

A net player (pointer of NGM + 0x370 / 0x380): id by `u3x_net_player_id`; u32 at +0x5b4 = its connection id.

### 3.7 The match score object SM

| Offset | Type | Meaning |
|---|---|---|
| 0x08 | u32 | number of member records |
| 0xe8 | u32 | number of weapon entries |
| 0xec + 0x28·i | record | weapon entry i: u16 at +2, u16 at +4 (both must be non-zero for the entry to count), u8 at +0xc = weapon stat index |
| 0x50ec + 0xa4·i | record | member record i: u32 at +0 = the net player id, u8 at +7 = team, s32 at +0x14 = "V14" |
| 0x5c14 + 0x130·t, t = 0..2 (image 0x17e338c + 0x130·t) | record | team block t: s16 at +0 = place (1 = first), s16 at +2 = score |
| 0x60d4 + 0x130·i | record | stat block of member i |

Stat block fields: s16 at +2 "POINTS"; u16 at +8 "W8"; u16 at +0xa "WA"; u16 at +0xc "WC"; s32 at +0x24 "TOTAL";
u16 at +0x48 "W48". The game keeps an all-default member record at image 0x1b7f070 and an all-default stat block at
image 0x1b7f114; they stand in for a user who has no entry.

### 3.8 Other game data

| Image offset | Type | Meaning |
|---|---|---|
| 0x13a6170 | 19 × u32 | the game's weapon ids of stat indexes 0 .. 0x12 (read at run time) |
| 0x1443b34, 0x1443b38, 0x1443b3c | float patterns | three constants of the game (read at run time; 6.1 steps 8 and 14) |
| 0x145e7f2 | text | the game's own NUL-terminated name of the menu level ("multi-menu"; read at run time, not copied) |
| 0x16e7171, 0x16e7172 | u8 | two marks set to 1 when the stack goes back to the lobby |
| 0x16e7918 | u64 | time-stamp-counter ticks per second |
| 0x1702720 | pointer to function | (int 0, const char *name, int flag) -> u32: the id of a name |
| 0x17b9d18 | float pattern | a setting (0x17b6368 + 0x39b0) |
| 0x17bb670 | u8 | non-zero = the playlist key is fixed (4.10) |
| 0x17bb860 | block | u8 state at +0, float patterns at +4 and +8 (6.1 step 8) |
| 0x17c3b40 | object | the booster manager: its u32 at +0, +4 and +0x14 are cleared by the handler |
| 0x17c3ba8 | object | the film stream |
| 0x17d3630 | object | the weapon statistics object "WS" (5.2) |
| 0x17d7760, 0x17d7761 | u8 | map vote: open, closed |
| 0x17d79e0 | float pattern | a matchmaking time in seconds (6.1 step 14) |
| 0x17d79e4 | u8 | the "leave Find Game" mark |
| 0x17d79f0 | block | argument of game function 0x74e6d0 (5.1 step 4) |
| 0x17d7b27 | u8 | a debug option |
| 0x17fa983 | u8 | a mark of the player custom manager (0x17f9938 + 0x104b), cleared by the handler |
| 0x1afcf10 | object | the level manager |
| 0x1b68810, 0x1b68814 | u32, u32 | the game's net state word and the clock value of its last change |
| 0x1b68a18 + 4·u | u32 | "match recorded" mark of user u |
| 0x1b68a20 | u8 | "left the match" mark |

Script symbols: 0x3b9a067d = the net game definitions: s32 count at +0, pointer at +8 to records of 0xd0 bytes;
record + 0xc = game type (s32), record + 0xb8 = map index (u32). 0x9e354807 = the extra weapon ids (5.2).

Game functions (System V calling convention, integer arguments only):

| Image offset | Arguments | Result | Meaning |
|---|---|---|---|
| 0x10e95b0 | (const char *name) | u64 | 64-bit hash of a level name |
| 0x10eb990 | (level manager, pointer to a request) | none | asks for a level |
| 0x11f2e30 | (u32 id) | pointer or null | value of a script symbol |
| 0x6534e0, 0xf23970 | | | only inside `u3x_menu_set_state` |
| 0x688160 | (address of image 0x17c3b40, u64 user, int slot, int −1) | s32 | the booster of a user's slot |
| 0x6a3130 | (address of image 0x17c3ba8) | none | ends the film stream of the match |
| 0x6b85a0 | (u32 connection id, int message, pointer data, int length) | none | sends a game message |
| 0x7210a0 | (NGM) | s32 | number of teams of the game type: 2 or 3 matter here |
| 0x729b10 | (NGM) | int | a test of the net game (non-zero = the leaderboard totals count) |
| 0x73f7d0 | (NGM, u32 key) | pointer or null | the playlist entry of a key; its first u32 is the entry's key |
| 0x748ef0 | (WS, u32 weapon id) | none | adds a weapon to the weapon statistics object |
| 0x74b290 | () | none | resets the pending player-state sends |
| 0x74d1f0 | (u64 room id) | none | tells the matchmaking code which room the match was in |
| 0x74e6d0 | (pointer, int −1, int co-op) | s32 | the kickback of a user |
| 0x751aa0 | (u32 page, u32 0x5ef96af1) | none | shows a page |
| 0x781ea0 | (profile object) | u32 | the rank word of a profile (G22 3.6; answered by G22's kind 64 in the patched game) |

### 3.9 The handler's parameter

`param`: pointer at +8 to an object "Q" (not tested for null); u32 at +0x18; u32 at +0x1c. Q: u8 at +0x198; float
patterns at +0x19c and +0x1a0.

### 3.10 The matchmaking stand-in state (`u3h_m03()`)

u32 words: word 0 = "armed" (a search will start when the matchmaking page is up), word 1 = "running". The other
words are not touched here.

### 3.11 Tables of game values

This group holds no table of game values. Three sets of values were literal in the old code:

- The demo / beta title ids and content words (old `u3x_pm_ranked`): no longer present here; `u3x_lg_beta` (G22)
  reads them from the running game (G22 3.11 b).
- The co-op game types: a rule, not data of the image: a type is a co-op type when it is 4, 8, 9, 11, 13, 16, 17
  or 18 (PS3 1.19's set; the same rule is used by other groups).
- The weapon ids of stat indexes 0 .. 0x12: the running game holds them at image 0x13a6170 (19 × u32); the rest
  come from script symbol 0x9e354807. Both are read at run time (the old code did so already).

The text "_weapon-stats" (5.2) is a name the game's statistics reader expects, not a table.

## 4. Behaviour, part A

### 4.1 `u3x_pm_ngentry`

i = the s16 at NGM + 0x661c. T = `u3x_sym_table(s, 0x3b9a067d)`. Result null when T is null, i < 0, i >= the s32
at T + 0, or the pointer at T + 8 is null; otherwise that pointer + i · 0xd0. One lookup per call, no other
effect.

### 4.2 `u3x_pm_type`

The s32 at `u3x_pm_ngentry(base, s)` + 0xc; 0 when there is no record.

### 4.3 `u3x_pm_coop`

0 when there is no record. Otherwise 1 when the u32 at record + 0xc is one of 4, 8, 9, 11, 13, 16, 17, 18 (compared
as a whole u32), else 0.

`u3x_pm_ngentry`, `u3x_pm_type` and `u3x_pm_coop` are pure queries (one script lookup each). Where sections 5 and
6 say "type" or "co-op", the value is the one these functions give at that moment; nothing the handler calls
changes it, so how often they are asked inside one function is not part of the behaviour (the tester's lookup stub
must not count).

### 4.4 `u3x_pm_ranked`: do the results of this match count

Result 1 or 0. Evaluated in this order, each step only when the earlier ones did not decide:

1. "Counted kind": co-op (4.3) with a type other than 0x12; or the net menu mode (image 0x17d603c) is 1; or the
   debug byte (image 0x17d7b27) is non-zero; or `u3x_lg_beta(base)` is non-zero. None of the four: result 0.
2. Film playback (u8 at NGM + 0x6a24 non-zero): result 0.
3. LAN byte (image 0x17d5c60) non-zero: result 1 when the co-op byte of LAN mode (image 0x17d6065) is non-zero,
   else 0.
4. Result 1.

No game call. (The old function compared the title texts itself with up to three calls of the game's string
compare, image 0x139c710, a plain `strcmp`; the new one asks `u3x_lg_beta`, which gives the same answer from the
same bytes. The tester must not expect calls of 0x139c710.)

### 4.5 `u3x_pm_leader_of`

`u3x_session_host(address of image offset obj)`.

### 4.6 `u3x_pm_party_vcall`: a call into the match session's backend

B = the pointer at MS + 8. B null: nothing. F = the pointer at (function table of B) + `off`; the table pointer
itself is not tested. F null: nothing. Otherwise F is called with (B, address of MS, `arg`) when `three` is
non-zero, with (B, address of MS) when it is zero. It always works on MS, whatever session the caller thinks of.

### 4.7 `u3x_pm_pg_clear`

The depth of MENU's page id stack (s32 at MENU + 0xef8) becomes 0.

### 4.8 `u3x_pm_pg_push`

d = the depth (s32). Nothing happens when d > 0x9f (signed), or when the top id, the u32 at MENU + 0xefc + 4·d
(d taken as a signed number), equals `id`. Otherwise the depth becomes d + 1 and the u32 at MENU + 0xefc + 4·(d + 1)
becomes `id`.

### 4.9 `u3x_pm_menu_reset`

M = address of image offset `menu`. When the u32 at M + 8 is 0, 5 or 6: nothing. Otherwise
`u3x_menu_set_state(base, M, 5)`. (Its effects, for the test: game call 0xf23970(event buffer, 0, 0); game call
0x6534e0(M, event buffer, 3); u32 at M + 8 = 5, u32 at M + 0xdb4 = 0, float pattern at M + 0xdb8 = 0x3f800000
(1.0); game call 0x6534e0(M, event buffer, 0).)

### 4.10 `u3x_pm_map`: the playlist key

0x0b70ed28 when the u8 at image 0x17bb670 is non-zero; otherwise the u32 at REC(0) + 0x52c (always user 0).

### 4.11 `u3x_pm_map_is`

E = game call 0x73f7d0(address of NGM, `u3x_pm_map(base, s)`). Result 1 when E is not null and the u32 at E equals
`id`, else 0. Exactly one game call per call.

### 4.12 `u3x_n13_on`

1 when bit 8 of ec[3] is clear, else 0.

## 5. Behaviour, part B

### 5.1 `u3x_pm_user`: one local user's match

P = the pointer at NGM + 0x370 + 8·u. P null: return; nothing at all happens for this user.

Values taken first, before any game call of this function:

- A = the u8 at ABORT.
- S (stat block) and M (member record): the defaults at image 0x1b7f114 and 0x1b7f070. When the u32 at SM + 8 (n)
  is not 0: k = `u3x_net_player_id(P)`; the first i in 0 .. n − 1 whose member record has id k gives M = member
  record i and S = stat block i (3.7); none: the defaults stay.
- team = the u8 at M + 7. Scores sc0, sc1, sc2 = the s16 at +2 of team blocks 0, 1, 2.
- Players per team c0, c1: only when the multiplayer-active byte (image 0x17d5c61) is non-zero, count the net
  player slots i = 0 .. 9 that are joined (u8 at +0xe1 non-zero) by their team word (u32 at +0x21c); c0 = team 0,
  c1 = team 1. With the byte 0 both are 0.

Then, in this order:

1. Boosters: for slot 0, then slot 1: r = game call 0x688160(address of image 0x17c3b40, u, slot, −1); add 1 to
   the "uses of booster r" field of REC(u) (3.3). (Call, add, call, add.)
2. Kickback: r = game call 0x74e6d0(address of image 0x17d79f0 + 0xc·u, −1, co-op (4.3)); add 1 to the "uses of
   kickback r" field.
3. TOTAL is read from S now. The kind of match:
   - co-op (4.3) and type not 0x12: step 4;
   - otherwise `u3x_pm_ranked(base, s)` is asked once: 0: step 5; non-zero: step 6.
4. **Co-op.**
   - Add TOTAL to 0x5bc and to 0x1f2c; add V14 to 0x5c0 and to 0x1f28.
   - u32 at image 0x1b68a18 + 4·u = 1.
   - Only when the u8 at MS + 0x1339 is 0: add 1 to 0x15fc; add WA (unsigned 16 bits) to 0x65c; and, only when
     the u32 at NGM + 0x680c is 4:
     - add 1 to 0x1600;
     - E = `u3x_pm_ngentry(base, s)`; when not null: `u3x_pm_365da8(base, s, u, the u32 at E + 0xb8, WC)` (WC as
       an unsigned 16-bit value);
     - then X = the u32 at NGM + 0x6a48: for X = 1, 2, 3 or 4 add 1 to 0x65c + 4·X.
   - Continue at step 7 with "save" = yes.
5. **Not counted.** Continue at step 7 with "save" = yes.
6. **Competitive.** "save" = no. In this order:
   1. When `u3x_session_host(MS)` is non-zero and u = 0: `u3x_pm_weapon_stats(base)`.
   2. u32 at image 0x1b68a18 + 4·u = 1.
   3. When (the u8 at MS + 0x1339 is non-zero or A is non-zero) and not co-op: add 1 to 0x5b0 and to 0x1f24.
   4. Add 1 to 0x1604.
   5. When A is 0: `u3x_pm_map_is(base, s, 0x5242014b)` is asked (only then); non-zero:
      `u3x_pm_challenge(base, s, u, 0x78b, 1, 0)`.
   6. When the type is none of 0x12, 0xf, 2: add W8 to 0x5a4 and WC to 0x5a8 (unsigned 16-bit values).
   7. Leaderboard totals. When `u3x_pm_map(base, s)` is 0x5242014b: nothing, and no game call. Otherwise game
      call 0x729b10(address of NGM) is made (exactly once); when its result is non-zero or the type is 10 or 0xe,
      and the type is neither 0xf nor 2: add W8 to 0x1c54 and WC to 0x1c58.
   8. Add W48 (unsigned 16 bits) to 0x5b4; add V14 to 0x5b8 and to 0x1f30; add WA to 0x65c.
   9. Type 0xf: add POINTS (signed 16 bits) to 0x658, then `u3x_pm_challenge(base, s, u, 0x765, POINTS, 0)`.
      Type 2: add POINTS to 0x654.
   10. Result of the match. Only when `u3x_pm_map(base, s)` is not 0x5242014b and the u32 at NGM + 0x680c is 2, 6
       or 9: N = game call 0x7210a0(address of NGM) (exactly once). "Mine" = team block number `team` (address
       image 0x17e338c + 0x130·team, also for a team above 2: no range test in the two-team case).
       - N = 2: when c0 and c1 are both non-zero: a tie when sc0 = sc1; otherwise a loss when the place of Mine
         (s16 at +0) is not 1; otherwise a win. When c0 or c1 is 0: a win. Then, before the common effects:
         on a win add 1 to 0x61c when the u8 at NGM + 0x34e is non-zero, else to 0x614 when the u8 at NGM + 0x34d
         is non-zero; on a loss the same with 0x620 and 0x618; on a tie nothing.
       - N = 3 and team <= 2: a loss when the place of Mine is not 1; otherwise a tie when another team's score
         (of the three, sc0 .. sc2) equals the score of team `team`; otherwise a win.
       - anything else: no result, nothing more in this step.
       - Common effects. Win: add 1 to 0x5ac and to 0x1f34, then `u3x_pm_365c5c(base, s, u, the s16 at Mine + 2,
         the s16 at Other + 2)`, Other = team block 1 when `team` is 0, else team block 0. Tie: add 1 to 0x60c.
         Loss: add 1 to 0x5b0 and to 0x1f24.
7. When "save": `u3h_publish(base, s, u3h_lg_profile(base, u))`.
8. `u3x_pm_lb_submits(base, s, u)` (every kind of match).

### 5.2 `u3x_pm_weapon_stats`: the weapon statistics block of the match

WS = address of image 0x17d3630. Fields of WS used: u32 at +0x24a0 = number of weapons in it; u8 at +0x24a4 + 4·i =
a per-weapon number; u64 at +0x2528 + 8·i = a per-weapon value; and one output block per weapon of 0xa0 bytes from
+0x1118: a name text at +0x1118 + 0xa0·i, a u32 at +0x112c + 0xa0·i, a u64 at +0x1130 + 0xa0·i.

1. u32 at WS + 0x24a0 = 0.
2. For each weapon entry i = 0 .. (u32 at SM + 0xe8) − 1, in order (3.7): skip it when its u16 at +2 or its u16
   at +4 is 0. Otherwise b = its u8 at +0xc, and X = game call 0x11f2e30(0x9e354807) (made for every entry not
   skipped so far, also when b < 0x13). X, when not null: s32 count at +0, pointer at +8 to u32 ids.
   - b < 0x13: id = the u32 at image 0x13a6170 + 4·b.
   - b >= 0x13: skip the entry when X is null or b − 0x13 >= the count (unsigned compare); else id = ids[b − 0x13].
   - game call 0x748ef0(WS, id).
3. Then for i = 0 .. (u32 at WS + 0x24a0, as the calls of step 2 left it) − 1: the 14 bytes at
   WS + 0x1118 + 0xa0·i become the text "_weapon-stats" with its NUL (bytes after them are not written); the u32
   at WS + 0x112c + 0xa0·i = (u8 at WS + 0x24a4 + 4·i) + 100; the u64 at WS + 0x1130 + 0xa0·i = the u64 at
   WS + 0x2528 + 8·i.

### 5.3 `u3x_pm_lb_submits`: the leaderboard blob and the scores

p = `u3h_lg_profile(base, u)`, R = REC(u). A blob of 0x40 bytes, all 0, then:

| Blob offset | Type | Value |
|---|---|---|
| 0x00 | u32 | game call 0x781ea0(p) (exactly one call, always) |
| 0x04 | s32 | field 0x1f30 + field 0x1f28 of R |
| 0x08 | u32 | field 0x1c58 |
| 0x0c | u32 | field 0x1f24 |
| 0x10 | u32 | field 0x1eac |

Then, only when `u3x_lb_off()` is 0:

- co-op (4.3; here type 0x12 counts as co-op too): `u3x_lb_submit(base, s, 0x517, field 0x1f2c, blob)`, then
  `u3x_lb_submit(base, s, 0x515, field 0x1f38, blob)`;
- otherwise, when `u3x_pm_ranked(base, s)` (asked once, only here) is non-zero: boards 0x514 with field 0x1c54,
  0x515 with field 0x1f38, 0x516 with field 0x1f34, in this order;
- otherwise nothing.

Every score is the s32 field sign-extended to 64 bits. The board numbers and scores are all this group knows of
the submit; where it goes is `u3x_lb_submit`'s business (the configured value of the online service).

Last, in every case: ea[60] += 1.

## 6. Behaviour, part C

### 6.1 `u3x_pm119`: the handler

"Lead(X)" = `u3x_session_host(address of X)`. "Push id" = `u3x_pm_pg_push(base, id)`; "clear" =
`u3x_pm_pg_clear(base)`. "Backend(off, three)" = `u3x_pm_party_vcall(base, off, 0, three)`. "Show page P" = game
call 0x751aa0(P, 0x5ef96af1). ABORT is a byte of the game: wherever a step says "ABORT now", it is read from
memory at that moment (game calls and step 9 can change it).

1. ea[59] += 1.
2. n13 = `u3x_n13_on()` non-zero and ea[94] non-zero. Then ea[94] = 0 (always).
   ea[93] = (u32 at NET + 0x40) · 0x100 OR (n13 ? 0x10 : 0) OR (ABORT now, as a number).
3. u32 at REC(0) + 0x558 = 0; `u3h_publish(base, s, u3h_lg_profile(base, 0))`.
4. u32 at `param` + 0x1c = 0.
5. Game call 0x74b290(). Game call 0x6a3130(address of image 0x17c3ba8).
6. Q = the pointer at `param` + 8: float patterns at Q + 0x1a0 and Q + 0x19c = 0x3f800000 (1.0); u32 at
   `param` + 0x18 = CLOCK.
7. `u3x_pm_menu_reset(base, 0x17bd080)`, then `u3x_pm_menu_reset(base, 0x17bfa10)`. u8 at image 0x17fa983 = 0.
8. The block at image 0x17bb860: when its u8 at +0 is not 1: it becomes 1, and with c = (CLOCK converted to
   float) / (float at image 0x1443b34): float at +4 = c + (float at image 0x17b9d18); float at +8 = c + (float
   at image 0x1443b38). Single precision, one rounding per operation (`f_from_int`, `f_div`, `f_add`).
9. When the u8 at MS + 0x1339 is non-zero: when the u64 at PS + 0x18 is not 0, u8 at PS + 0x1334 = 1; and
   ABORT = 1.
10. u32 at NGM + 0x6808 = 0; u32 at image 0x17c3b40, 0x17c3b44 and 0x17c3b54 = 0; u8 at image 0x1b68a20 = 0.
11. The menu level. N = address of image 0x145e7f2 (the game's text). A request of 0x18 bytes, all 0, then:
    u64 at +0 = game call 0x10e95b0(N); u32 at +8 = the function whose address is stored at image 0x1702720,
    called with (0, N, 0); u8 at +0xe = 0x0a. (The two calls in this order.) Then game call
    0x10eb990(address of image 0x1afcf10, the request).
12. Leaving or staying, by ABORT now:
    - **Non-zero (leaving).** Clear; push 0x37656388; push 0x53cbb5f2; then: when the u32 at NET + 0x40 is 0 and
      not n13: push 0xb99a3235; when n13 (whatever NET + 0x40 is): ea[91] += 1 and no push. Push 0xb808d03c.
      Then the party: when Lead(PS): with the u32 at NET + 0x40 = 0: `u3h_party_send(base, 0x117, address of
      NET + 0x78, 8)` (the 8 bytes of the match room id); else u8 at PS + 0x1334 = 1. When not Lead(PS): u8 at
      PS + 0x1334 = 1 only when the u8 at Q + 0x198 is 0.
      Then game call 0x74d1f0(the u64 at MS + 0x18 when the u8 at MS + 0xc8 is non-zero, else 0). Then
      Backend(0x38, 0) (Leave). Then u8 at image 0x1b68a20 = 1; u32 at NET + 0x40 = 0; u32 at image 0x1b68810 =
      0x26; u32 at image 0x1b68814 = CLOCK.
    - **Zero (staying).** Backend(0x58, 1) (RoomFlags with 0). Then, when the pointers at NGM + 0x370 (L) and at
      NGM + 0x380 (H) are both not null: a message of 0x10 bytes: u32 at +0 = `u3x_net_player_id(L)`, u32 at
      +4 = 0, u64 at +8 = the u64 at NET + 0x78; game call 0x6b85a0(the u32 at H + 0x5b4, 0x105, the message,
      0x10).
13. `u3x_pm_user(base, s, 0)`, then `u3x_pm_user(base, s, 1)`.
14. The return routing:
    - ABORT now non-zero: return (ea[61] is not counted).
    - n13: `u3x_n13_rearm(base, s)`; ea[92] += 1; ea[61] += 1; return.
    - The u32 at NET + 0x40 (read now) is 0, **back to the lobby**: u8 at image 0x16e7171 = 1 and at 0x16e7172 =
      1; Backend(0x38, 0) (Leave); clear; push 0x37656388, 0x53cbb5f2, 0xb99a3235; show page 0xb99a3235; then
      `u3x_pm_ranked(base, s)` is asked (once, always); when it is non-zero and
      `u3x_shows_match_stats(the u32 at NGM + 0x680c, the u8 at MS + 0x1339)` is non-zero: push 0x0c3a3716.
      u32 at image 0x1b68810 = 3 when Lead(MS), else 0x12; u32 at image 0x1b68814 = CLOCK.
    - Otherwise, **back to matchmaking**: the float at image 0x17d79e0 = (q + r / f) + (float at image
      0x1443b3c), where t = the processor's time-stamp counter (the `rdtsc` instruction, 64 bits), F = the u64
      at image 0x16e7918, D = t / F as an unsigned 64-bit division (0 when F is 0), E = t − D·F; q, r and f are D,
      E and F converted from signed 64-bit integers to single precision (the `cvtsi2ss` instruction with a
      64-bit source: CONVENTIONS.h has only the 32-bit `f_from_int`); the division, then the inner addition,
      then the outer addition are `f_div`, `f_add`, `f_add`. With F = 0 this is t / 0, plus infinity.
      Then clear; push 0x37656388, 0x53cbb5f2, 0xe35f0f1a; show page 0xe35f0f1a; when
      `u3x_shows_match_stats(the u32 at NGM + 0x680c, the u8 at MS + 0x1339)`: push 0x0c3a3716. When Lead(MS):
      Backend(0x68, 1) (SetAttr4 with 0), then Backend(0x40, 1) (SetAttr with 0).
    - ea[61] += 1.

### 6.2 `u3x_n13_rearm`: back to the matchmaking page with a new search armed

In this order (only memory and one game call):

1. Clear; push 0x37656388, 0x53cbb5f2, 0xe35f0f1a; show page 0xe35f0f1a.
2. When `u3x_shows_match_stats(the u32 at NGM + 0x680c, the u8 at MS + 0x1339)`: push 0x0c3a3716.
3. u8 at image 0x17d7760 = 0 and at 0x17d7761 = 0; `u3h_n09_reset(base)`.
4. u8 at image 0x17d79e4 = 0.
5. now = CLOCK (as u32). Word 0 of `u3h_m03()` = 1, word 1 = 0; `u3h_n02_arm(now)`.
6. u32 at image 0x1b68810 = 20; u32 at image 0x1b68814 = now.

The C part's search tick then sees "armed" with net state 20 and a fresh clock (what the "Starting in" popup's ok
leaves behind) and starts the search once the matchmaking page is up.

## 7. Test scenarios

All three parts run against a fake image (zero-filled memory of at least 0x1b80000 bytes as `base`) with
call-recording stubs. Nothing needs the live game for the old-versus-new comparison; a live check (one Custom Game,
one Find Game match of the stand-in, one aborted match) is only the final confirmation.

Stubs: the state's lookup function (through `u3x_sym_table`: not counted, see 4.3); recording stubs at the game
function offsets of 3.8 with scripted results; a function pointer stored at image 0x1702720; a fake backend object
with a function table for MS; stand-ins for `u3h_lg_profile` / `u3h_lg_rec` (a profile array in the fake image;
not counted), `u3h_publish`, `u3h_party_send`, `u3h_m03`, `u3h_n09_reset`, `u3h_n02_arm`, `u3x_lb_submit`,
`u3x_lb_off`, `u3x_lg_beta`, `u3x_menu_set_state`, and for part B's test of G22's three challenge functions
(recording). Compare memory, results, and order and arguments of the recorded calls.

Part A:

1. `u3x_pm_ngentry`: no table; index −1, 0, count − 1, count; null record pointer.
2. `u3x_pm_type` / `u3x_pm_coop`: no record; every type 0 .. 20; type 0x10004.
3. `u3x_pm_ranked`: each of the four "counted kind" reasons alone and none; co-op with type 0x12 (not a reason);
   film byte; LAN byte with the co-op byte 0 and non-zero.
4. `u3x_pm_party_vcall`: no backend; null entry; two and three arguments; several offsets.
5. Page stack: push after clear onto a stack whose word at +0xefc equals / differs from the id; pushing the top id
   again; depth 0x9f and 0xa0; negative depth.
6. `u3x_pm_menu_reset`: states 0 .. 8.
7. `u3x_pm_map` / `u3x_pm_map_is`: fixed-key byte set; lookup null; lookup with equal and different key.
8. `u3x_n13_on`: bit 8 of ec[3] set and clear.

Part B:

9. No net player for the user: nothing happens.
10. Lookup of S and M: member count 0; id found at index 0 and at a later index; not found; a net player with
    byte +0x1ec set and low id part 0 / not 0.
11. Boosters and kickback: results −1, 0, 5; user 0 and 1 (the kickback argument address for user 1).
12. Co-op: aborted byte set / clear; mode 4 and not; X = 0 .. 5; no net game record.
13. Not counted: only the save and the submits.
14. Competitive: session lead and user 0 / 1 (weapon statistics once); aborted byte or A with co-op 0;
    A = 0 with the playlist test true / false; every type among 0, 2, 10, 0xe, 0xf, 0x12 and one other; the
    playlist key 0x5242014b (no 0x729b10 call, no result step); 0x729b10 answering 0 and 1; mode 0 .. 10.
15. Result, two teams: both teams populated with equal scores, with place 1 and not 1; one team empty; the marks
    at NGM + 0x34d / 0x34e in all four combinations; team 0 and 1 (the "other" score).
16. Result, three teams: place not 1; place 1 with no equal score, with team 0 / 1 / 2 equal to another; team 3
    (no result); N = 1 or 4 (no result).
17. Weapon statistics: entries with a zero u16; index below 0x13; index above with and without the script data,
    at and beyond its count; the stub of 0x748ef0 raising the count so that step 3 writes blocks.
18. Submits: feature off; co-op (also type 0x12); counted and not counted; negative scores.

Part C:

19. Re-arm mark set / clear with the switch on / off; ea[93] for several values.
20. Menu states that reset and that do not; block at 0x17bb860 already 1 and not (float results bit for bit).
21. Aborted byte of MS with and without a party room.
22. Leaving: NET + 0x40 zero / non-zero, with and without n13; party lead or not, Q + 0x198 zero / non-zero; MS in
    a room or not; no backend.
23. Staying: no backend; local or other net player missing; the message bytes.
24. Return routing: ABORT set by the per-user stubs; n13; lobby with "counted" yes / no, modes 0, 3, 8, other,
    aborted byte, lead or not; matchmaking with stats page or not, lead or not.
25. The time of step 14 comes from `rdtsc`: with F = 0 the result is exactly plus infinity (bit for bit, for both
    old and new); with a real F compare old and new called back to back with a tolerance of the elapsed time.
26. `u3x_n13_rearm`: stats page rule; the six memory effects; the two export calls with their arguments.

## 8. Oddities of the old code, to be kept

1. The first push after a clear compares the new id with the word at MENU + 0xefc, which no push here ever writes:
   a stale equal value there drops that push.
2. `u3x_pm_party_vcall` always uses MS and does not test the backend's function table pointer.
3. The kickback call takes image 0x17d79f0 + 0xc·u, although other code steps through that block by 0x30 per user;
   for user 0 it is the same address.
4. The playlist key is always read from user 0's record, also for user 1.
5. A user without a net player gets nothing, not even the submits (normally user 1).
6. A competitive match can count two losses: one for the abort (5.1 step 6.3) and one for the result (step 6.10).
7. Field 0x65c takes WA in co-op (not aborted) and in competitive matches.
8. Three teams: the "other" score handed to `u3x_pm_365c5c` is still team 1's for team 0 and team 0's otherwise.
9. Two teams: the own team block is addressed without a range test of the team number.
10. `u3x_pm_lb_submits` treats type 0x12 as co-op, while the match kind of 5.1 does not.
11. The blob takes field 0x1eac (a tournament field of 1.19) although the tournament parts are not ported.
12. Weapon statistics: the script symbol is looked up again for every counted entry; the per-weapon number is read
    as one byte at a 4-byte step.
13. Leaving with the re-arm mark set pushes no lobby page, and ea[91] counts it whatever NET + 0x40 holds.
14. Neither Q (the pointer at `param` + 8) nor any profile pointer is tested for null.
15. The handler writes the booster manager's three words to 0 before the per-user booster calls read from it.
16. Competitive results are not saved by this handler ("save" = no); the co-op and not-counted cases are.

## 9. What stays as it is

- The C wrappers and exports (`pm_ngentry`, `pm_type`, `pm_coop`, `pm_ranked`, `pm_pg_clear`, `pm_pg_push`,
  `pm119`, `u3h_pm119`, `u3h_pm_type`, `u3h_pm_coop`, `u3h_coop`, `u3h_netgame_entry`): one-line forwards.
- `n09_reset`, the search-popup words and the matchmaking stand-in (`m03`, its tick, the place that sets ea[94]):
  the helper's own logic in the C part; this group only reaches them through `u3h_m03`, `u3h_n09_reset` and
  `u3h_n02_arm`.
- `u3x_shows_match_stats`, `u3x_session_host`, `u3x_net_player_id`, `u3x_sym_table`: existing small functions
  (one-line rules, or written new earlier); called, not delivered again.
- `u3x_lb_submit`, `u3x_lb_off` (G29), `u3x_menu_set_state` (G33), the three challenge functions and
  `u3x_lg_beta` (G22): their own groups.

## 10. Open questions

1. `u3x_pm_ranked` now uses `u3x_lg_beta` instead of three calls of the game's `strcmp` (4.4). Same answer, no
   game call. If the coordinator wants the calls kept, the rule is: title text at image 0x17b9ca0 compared with
   the three texts of G22 3.11 (b) in their order, each compare only when the earlier ones differed, made only
   when the first three reasons of step 1 failed.
2. Two new one-line C exports are needed (`u3h_n09_reset`, `u3h_n02_arm`); names are proposals.
3. The meaning of several fields is only known by use (stat words W8 / WA / WC / W48, V14, NGM + 0x34d / 0x34e,
   the block at 0x17bb860, the page 0xb808d03c). The offsets and effects are certain; the names are labels.
4. The text at image 0x145e7f2 and the three float constants at 0x1443b34 .. 0x1443b3c are read from the image as
   the old code read them; their values were not looked at.
5. Party message 0x117: the old handler called the sender entry of the online interface itself (the function
   pointer at byte 8 of the block `u3h_ona()` returns, never null). `u3h_party_send` makes exactly that call; a
   tester that stands in for the interface block instead of the export sees the same call (base, 0x117, address
   of NET + 0x78, 8).
6. `u3x_pm_menu_reset` goes through `u3x_menu_set_state`. The old function made the same four steps itself with
   an event buffer of 0x80 bytes that it did not clear first (`u3x_menu_set_state` clears its buffer of 0xa0
   bytes before the game fills it). The game calls, their order and arguments and the menu fields are the same.

## 11. Clarifications (tester, 2026-11-14)

Facts confirmed by the differential test (`reimpl2\G30\test_g30.cpp`, old v7.212 against the new parts A, B, C; all
plain cases equal at -O1 and -O2). Items 1 to 3 are read-order facts of the old code that the sections above leave
open; they can only show when a function called in between changes the value named, which nothing the game does
there is known to do.

1. **5.2 step 2: the number of weapon entries is taken once.** The u32 at SM + 0xe8 is read before the first entry
   and that value bounds the whole loop. A change of the word by game call 0x11f2e30 or 0x748ef0 during the loop
   has no effect on how many entries are looked at. (Step 3 is the other way round in the old code, the u32 at
   WS + 0x24a0 is read again before every block, but nothing is called inside step 3 and no block write reaches
   that word for fewer than 32 weapons, so there the moment of the read cannot be observed.)
2. **6.1 step 12, leaving, not Lead(PS): Q is fetched again.** The byte tested is at +0x198 of the pointer that
   `param` + 8 holds at that moment (after the calls of steps 7 and 11), not of the pointer taken in step 6. The
   two are the same object unless one of those calls stores another pointer at `param` + 8.
3. **6.1 step 6: order of the three stores.** Q + 0x1a0, then `param` + 0x18 (CLOCK), then Q + 0x19c. The order
   only matters if Q + 0x19c or Q + 0x1a0 were the same four bytes as `param` + 0x18 (Q overlapping the parameter),
   which the game's objects do not do: then the word ends as 1.0 where the old order wrote Q + 0x19c last.
4. **4.6, `off` not a multiple of 8.** The old code used entry number `off` / 8 (rounded down) of the function
   table; reading the pointer at byte `off` is the same for every multiple of 8. All callers pass 0x38, 0x40, 0x58
   or 0x68.
5. **4.3 / 5.1: when type and co-op are asked.** The old `pm_user` asked the co-op test and the type again at every
   use (after the booster calls, after 0x729b10, ...). As 4.3 says this is free; the test changes the net game
   record during a run only to list where the two sides ask, and counts nothing there.
6. **Null pointers.** With a null pointer at `param` + 8 the handler faults in step 6 (before `param` + 0x18 is
   written); with a null profile pointer (image 0x17dd610) `u3x_pm_map`, `u3x_pm_lb_submits`, `u3x_pm_user` and
   the handler fault in the record getter; with a backend whose function table pointer is null
   `u3x_pm_party_vcall` faults; with a null id pointer in the data of symbol 0x9e354807 and an index in range
   `u3x_pm_weapon_stats` faults. Old and new fault at the same point with the same memory written before it.
7. **Open question 1 (title test).** Settled: `u3x_lg_beta` gives the old answer for every title text tried (each
   of the three titles, one character short, one more, one changed, empty, random) and both content words, and
   the new code asks it exactly in the cases where the old code made its first title compare (only when the first
   three reasons of 4.4 step 1 fail). No call of 0x139c710 is made by the new code.
8. **Open question 5 (party message 0x117).** Settled: one call (base, 0x117, address of NET + 0x78, 8), at the same
   place in the call sequence, only when Lead(PS) and the u32 at NET + 0x40 is 0.
9. **Open question 6 (`u3x_pm_menu_reset`).** Settled: with `u3x_menu_set_state` standing in as G33 describes it,
   the calls 0xf23970(buffer, 0, 0), 0x6534e0(menu, same buffer, 3), 0x6534e0(menu, same buffer, 0), their order
   and the three menu fields are the same as the old function's, for states 0 to 8 and any other value (only 0, 5
   and 6 do nothing).
10. **6.1 step 14, lobby.** `u3x_shows_match_stats` is a pure rule; the old code applied it only after
    `u3x_pm_ranked` gave non-zero. Asking it or not when the match does not count cannot be observed.
11. **5.1, user index.** `u` = 2 or 3 (never passed by the handler) behaves like 0 / 1 with the addresses that the
    formulas of 5.1 give; old and new agree there too.
12. **6.1 step 14, the time.** With the time-stamp counter value t given to both sides, the float stored at image
    0x17d79e0 is bit for bit the same for every t and F tried (F = 0, 1, 2, 3, 1000, real rates, 2^63 − 1, 2^63,
    2^64 − 1; t = 0, 1, 2^63 − 1, 2^63, 2^64 − 1 and random): D, E and F are converted as SIGNED 64-bit values
    (values from 2^63 up become negative floats), as 6.1 says.
