# G18. Friends: list build / refresh, presence data, quick actions

Behaviour description for a reimplementation (reimpl2 pipeline, describer's document). It says what the functions
do, not how the old ones were written. Offsets are hexadecimal when prefixed with 0x. "Image offset X" means the
address `base + X`, where `base` is the game image base every function receives. All multi-byte values are
little-endian; "u8 / u16 / u32 / u64 / s16 / s32 / s64" are unsigned / signed integers of that width, "pointer" is
8 bytes. 32-bit additions and subtractions of clock values wrap (two's complement).

## 1. Purpose and callers

The PS4 game still has the menu pages that show the friend list and the "players met" list, but it lost the module
that fills them. The helper owns that module. This group is its core:

- **the friend list**: set-up at net start, the one-time fetch of all friends with their presence, adding or
  flagging a friend, re-reading the presence of flagged friends and sorting, tear-down at net shutdown;
- **the own presence data**: a 0x60-byte record (what the player is doing) that is rebuilt every 1500 clock ticks
  and published when it changed, with the end time of the running match;
- **the quick-action menus** that open when a friend row or a players-met row is clicked, the "wait" popup, and
  three small queries (is this friend in my party session, find a friend by id, compare two texts ignoring case).

Callers (all stay as they are):

| Function (new name) | Called by | When |
|---|---|---|
| `u3x_strcasecmp` | `u3x_friend_in_game` (C++, other group); C wrapper `fr_strcasecmp` (no caller left) | presence title test |
| `u3x_friend_in_session` | C wrapper `fr_in_session`, which is used by the C export `u3h_fr_in_session` (called by `u3x_friend_state_icon`, C++) and by the friend click of this group | row icon, click |
| `u3x_friend_find` | C wrapper `fr_find`, used by the C export `u3h_fr_find` (called by `u3x_history_thread`, `u3x_players_met_add`, `u3x_friend_header`, C++) | players met, friend header |
| `u3x_friend_add` | `u3x_friend_list_thread` (this group), `u3x_friend_events` (C++, other group, with a null presence); C wrapper `fr_add` (no caller left) | list fetch, friend events |
| `u3x_friend_refresh_one` | `u3x_friend_refresh`; C wrapper `fr_refresh_one` (no caller left) | refresh |
| `u3x_friend_refresh` | C wrapper `fr_refresh`, used by the C export `u3h_fr_refresh` (called by `u3x_friend_list`, C++, list operation 9) | the friend list page asks for a refresh |
| `u3x_friend_list_thread` | C wrapper `fr_list_thread` (called by the C per-frame `fr_tick` while the busy marker is set); `u3x_friend_shutdown`; `u3x_match_players` and `u3x_friend_events` (C++, other group, while the busy marker is set) | first tick after init or after a rebuild request |
| `u3x_friend_init` | C wrapper `fr_init`, called by the C export `u3h_netinit_services` (first of its three services), itself called by the C++ net init | net start |
| `u3x_presence_clear` | `u3x_friend_shutdown`; C wrapper `fr_presence_clear` (no caller left) | net shutdown |
| `u3x_friend_shutdown` | C wrapper `fr_shutdown`, called by the C export `u3h_net_shutdown_parts` (third of its four parts), itself called by the C++ net shutdown | net shutdown |
| `u3x_presence_tick` | C wrapper `fr_presence_tick`, called by `fr_tick` once per helper tick, only while friends are on and the list is set up | every frame |
| `u3x_match_time_left` | `u3x_match_end_time` only (C wrapper `fr_time_left`, called only by `u3h_match_end_time`) | presence data |
| `u3x_match_end_time` | `u3x_presence_tick` (directly, or through the C export `u3h_match_end_time`) | presence data, in a match |
| `u3x_friend_click` | C wrapper `fr_click`, called by the C dispatcher `fr_kind` (helper stub kind 68, sub id 1) with the list request the game's click handler received | a friend row is clicked |
| `u3x_met_click` | C wrapper `fr_pm_click`, called by `fr_kind` (sub id 2) | a players-met row is clicked |
| `u3x_wait_popup` | C wrapper `fr_wait_popup`, used by the C export `u3h_wait_popup` (called by `u3x_send_join`, C++, with text 0xe4e03363 and its page callback) | "Joining party..." popup |

Threading: every function of the group runs on the thread that runs the helper's hooks (the per-frame helper tick
and the menu click thunks). The group starts no thread, takes no lock and uses no atomic operation. The "busy
marker" of the friend object (3.3, +0xc8) is what is left of the PS3 list thread: it is set by the init (and by a
rebuild request of another group) and cleared when `u3x_friend_list_thread` has run; that function runs to
completion inside the call. No function of the group is re-entered.

## 2. Interface

No function takes or returns a float by value. All are `extern "C"`.

### 2.1 Functions to deliver

Existing C++ names (name and signature stay; the C wrappers already pass their arguments unchanged):

| Function | Signature | Result |
|---|---|---|
| compare ignoring case | `int u3x_strcasecmp(const char *a, const char *b)` | -1, 0 or 1 |
| add / flag a friend (stray, see section 7) | `void u3x_friend_add(uintptr_t base, const uint8_t *id, const uint8_t *presence)` | - |
| re-read one friend's presence (stray) | `void u3x_friend_refresh_one(uintptr_t base, uint8_t *e)` | - |
| refresh and sort | `void u3x_friend_refresh(uintptr_t base)` | - |
| the list fetch | `void u3x_friend_list_thread(uintptr_t base)` | - |
| set-up | `void u3x_friend_init(uintptr_t base)` | - |
| publish the title-only presence (stray) | `void u3x_presence_clear(uintptr_t base, void *s)` | - |
| tear-down | `void u3x_friend_shutdown(uintptr_t base, void *s)` | - |
| presence data | `void u3x_presence_tick(uintptr_t base, void *s)` | - |

New names for the seven functions whose body is still in C. Each C function becomes one statement that forwards:

| Old C function | New function | Signature | The C wrapper passes |
|---|---|---|---|
| `fr_in_session(base, e)` | `u3x_friend_in_session` | `int u3x_friend_in_session(uintptr_t base, const uint8_t *e)` | `base, e`; returns the result |
| `fr_find(base, npid)` | `u3x_friend_find` | `uint8_t *u3x_friend_find(uintptr_t base, const void *id)` | `base, npid`; returns the result |
| `fr_click(base, ev)` | `u3x_friend_click` | `void u3x_friend_click(uintptr_t base, const int32_t *ev)` | `base, ev` |
| `fr_pm_click(base, ev)` | `u3x_met_click` | `void u3x_met_click(uintptr_t base, const int32_t *ev, uint8_t *target)` | `base, ev, nf_pmtarget` (the C part's static 0x24-byte buffer "players-met id for Add Friend"; it has no export, so the wrapper hands it over) |
| `fr_wait_popup(base, text_id, cb)` | `u3x_wait_popup` | `void u3x_wait_popup(uintptr_t base, uint32_t text_id, uintptr_t callback)` | `base, text_id, cb` |
| `fr_time_left(base, s)` | `u3x_match_time_left` | `int32_t u3x_match_time_left(uintptr_t base, void *s)` | `base, s`; returns the result |
| `u3h_match_end_time(base, s, out)` | `u3x_match_end_time` | `int u3x_match_end_time(uintptr_t base, void *s, int64_t *out)` | `base, s, out`; returns the result |

Parameters: `base` game image base; `s` the helper's state object (opaque here: only handed on); `e` a friend entry
(3.4); `id` a player id (NUL-terminated text in a 0x24-byte field); `presence` a 0x190-byte presence details block
(3.5) or null; `ev` a list request, an array of s32 words of which only word 1 (byte offset 4, the row index) is
read; `target` a 0x24-byte buffer; `text_id` a text id; `callback` the address of a page event handler; `out`
where the end time (s64, seconds) is stored.

A file-private helper is free (old name `title_details`, 4.9): "fill a presence details block with the title
text". The old file-private pieces `friends_off()` (the switch test, 3.1) and the table `presence_stats` (replaced
by the rule in 4.10) belong to this group too.

### 2.2 Functions of other groups, of the C part and of the shared tools that the new code calls

Not to be described or reimplemented here. `u3h_` are exports of the C part.

| Name | Signature | What it does |
|---|---|---|
| `u3h_ext` | `uint8_t *u3h_ext(void)` | the helper's own state block (3.1); in CONVENTIONS.h |
| `u3h_fr_ent` | `uint8_t *u3h_fr_ent(uintptr_t base, uint32_t i)` | address of friend entry i: (pointer at friend object +8) + i x 0x240; no checks |
| `u3h_fr_strcmp` | `int u3h_fr_strcmp(uintptr_t base, const void *a, const void *b)` | calls the game's text compare at image offset 0x139c710 with (a, b) and returns its result: 0 when the two NUL-terminated texts are equal. Every id comparison of this group goes through it, with the argument order given in section 4 |
| `u3h_fr_sort` | `void u3h_fr_sort(uint8_t *entries, int64_t n)` | sorts n friend entries in place (in this game first, then online, then offline; by name inside a group); does nothing for n < 2 |
| `u3h_friend_storage` | `uint8_t *u3h_friend_storage(int which)` | the C part's static arrays: 0 = friend entries (100 x 0x240 bytes), 1 = players met (50 x 0x28), 2 = new players (0x10 x 0x24), 3 and 4 = two detail blocks (0x1b8 bytes each) |
| `u3h_friend_count` | `int32_t u3h_friend_count(uintptr_t base)` | number of friends on the account service; negative = not signed in |
| `u3h_friend_get` | `int32_t u3h_friend_get(uintptr_t base, uint32_t i, uint8_t *id, uint8_t *presence)` | friend i: fills `id` (0x24) and `presence` (0x190); 0, or negative on failure |
| `u3h_presence` | `int32_t u3h_presence(uintptr_t base, const uint8_t *id, uint8_t *presence)` | the presence details (0x190) of the friend `id`; 0, or negative on failure |
| `u3h_presence_set` | `int32_t u3h_presence_set(uintptr_t base, const uint8_t *details, uint32_t options)` | publishes the own presence details (0x190); negative = failed |
| `u3h_fr_key` | `uint32_t u3h_fr_key(uintptr_t base)` | the 32-bit network version word that starts every presence data record (it also keeps the game's copy of it current) |
| `u3h_fr_pdata` | `uint8_t *u3h_fr_pdata(uintptr_t base, uint8_t *e)` | the friend's 0x60-byte presence data (= e + 0x128) when the entry carries one of this game (size field 0x60 and an accepted version word), else null. Repeating the call changes nothing |
| `u3h_fr_qa_push` | `void u3h_fr_qa_push(uintptr_t base, void *q)` | shows a quick-action menu: removes the "View Profile" pairs (actions 0x3fe6f70e and 0xe1810202) from the list, adds the number removed to counter ea[77], and, when the list is not empty, calls the game function at image offset 0x6528d0 with (menu = address of image offset 0x17bd080, q, s32 1) |
| `u3x_quick_action_add` | `int u3x_quick_action_add(QuickActions *q, uint32_t text, uint32_t action)` | existing C++ function: appends the pair (text, action) and returns 1; returns 0 and does nothing when the list already holds 8 |
| `QuickActions` | `struct QuickActions { uint32_t argument, page; int32_t count; uint32_t pairs[16]; };` | existing type at file scope of the C++ part (the game's quick-action list: argument, page id, number of pairs, then (text id, action id) pairs) |
| `u3h_text` | `const char *u3h_text(uintptr_t base, void *s, uint32_t id)` | localized text of a text id; never null (empty text when there is none) |
| `u3x_strncpy_pad` | `void u3x_strncpy_pad(char *to, const char *from, uint32_t n)` | existing C++ function: copies the text up to its NUL but at most n bytes, and fills the rest of the n bytes with zeros (also reachable as `u3h_fr_strncpy`, same arguments) |
| `u3h_lg_profile` | `uint8_t *u3h_lg_profile(uintptr_t base, uint32_t user)` | the profile object of a local user, or null when the game has no profiles |
| `u3h_lg_rec` | `uint8_t *u3h_lg_rec(uintptr_t base, void *s, uint8_t *profile)` | the statistics record of a profile (group G22); never null for a non-null profile. It can change the profile's state word (+0x7ae8) from 3 to 2 |
| `u3x_sym_table` | `uint8_t *u3x_sym_table(void *s, uint32_t id)` | existing C++ function: the data of a script symbol, or null (unknown id, or the state has no lookup) |

Counters and switches are reached through `u3h_ext()` (3.1); CONVENTIONS.h has `ext_eb()` for the "eb" words but no
accessor for the "ea" words used here.

## 3. Game and helper facts

### 3.1 The helper's state block (address from `u3h_ext()`)

The "ea" words are u32 at `u3h_ext() + 0x3c50 + 4 x k` (ea[k]); the "eb" words are at `u3h_ext() + 0x4050 + 4 x k`.

| Word | Offset | Meaning |
|---|---|---|
| ea[1] | 0x3c54 | switches; bit 5 (value 0x20) set = friends are off |
| ea[64] | 0x3d50 | counter: list set-ups |
| ea[65] | 0x3d54 | counter: friends appended |
| ea[66] | 0x3d58 | counter: failed presence reads |
| ea[67] | 0x3d5c | counter: refreshes |
| ea[68] | 0x3d60 | counter: failed friend counts |
| ea[69] | 0x3d64 | counter: tear-downs |
| ea[70] | 0x3d68 | counter: presence publishes tried |
| ea[71] | 0x3d6c | counter: presence publishes failed |
| ea[77] | 0x3d84 | counter: View Profile entries removed (written by `u3h_fr_qa_push`, not by this group) |
| ea[78] | 0x3d88 | counter: friend row clicks accepted |
| ea[79] | 0x3d8c | counter: players-met row clicks accepted |
| eb[4..5] | 0x4060 | pointer (8 bytes): the "seconds now" function (C library `time`): one argument, a pointer that is passed as null; returns s64 seconds. May be null. |

Counters are plain u32 increments (wrap).

### 3.2 Game functions called

Called with the game's normal calling convention through `game_call`.

| Image offset | Arguments | Result | Meaning |
|---|---|---|---|
| 0x7a2b10 | (pointer: the party session, address of image offset 0x17dc298) | int | number of players in the party |
| 0x727290 | (pointer: the net game manager, address of image offset 0x17cb060) | int | a small number about the running match; only its low byte is used (presence data byte 6) |
| 0x651940 | (pointer: the menu, address of image offset 0x17bd080; u32 page id; int 0; int 0) | int, ignored | shows a menu page |

Reached only through other functions (the tester's recording will show them): the text compare 0x139c710 through
`u3h_fr_strcmp`; the quick-action menu 0x6528d0 through `u3h_fr_qa_push`; the "seconds now" function through the
pointer of 3.1.

### 3.3 The friend object (image offset 0x17caeb0, "NF" below)

| Offset | Type | Meaning |
|---|---|---|
| 0x00 | u32 | number of friends |
| 0x04 | u32 | capacity of the friend list |
| 0x08 | pointer | friend entries (0x240 bytes each, 3.4); null = not set up |
| 0x10 | u32 | number of players met |
| 0x14 | u32 | capacity of the players-met list |
| 0x18 | pointer | players-met entries (0x28 bytes each: id 0x24 bytes, then a u32) |
| 0x20 | u8 | "history changed" flag |
| 0x21 | 0x24 bytes | an id (only cleared by this group) |
| 0x48, 0x50 | pointer x 2 | two detail blocks (0x1b8 bytes each; their u32 at +0x1b0 is cleared at set-up) |
| 0x58, 0x5c | u32 x 2 | two words cleared at set-up |
| 0x60 | 0x60 bytes | the presence data last published (3.6); all zero = nothing published |
| 0xc0 | s32 | game clock value from which the presence data is built again |
| 0xc8 | u64 | busy marker of the list fetch: non-zero = the fetch still has to run, readers must not use the list |
| 0xe0 | u64 | busy marker of the history upload (other group) |
| 0xe8 | u32 | capacity of the new-players list |
| 0xec | u32 | number of new players |
| 0xf0 | pointer | new-players names (0x24 bytes each) |
| 0xf8 | u64 | cleared at set-up |

### 3.4 A friend entry (0x240 bytes)

| Offset | Type | Meaning |
|---|---|---|
| 0x00 | u8 | refresh flag: non-zero = the presence has to be read again |
| 0x01 | 0x24 bytes | the friend's id (NUL-terminated text) |
| 0x28 | 0x190 bytes | presence details (3.5) |
| 0x1b8 | u64 | cleared at set-up (an id of a service the helper does not support) |
| 0x1c0 | text | its first byte is cleared at set-up |

With 3.5: title text at entry +0x28, presence data at +0x128, data size at +0x1a8, state at +0x1b0.

### 3.5 Presence details (0x190 bytes)

| Offset | Type | Meaning |
|---|---|---|
| 0x000 | 0x80 bytes | title text, NUL-terminated |
| 0x080 | 0x80 bytes | status text |
| 0x100 | 0x80 bytes | data (this game: the 0x60-byte presence data of 3.6) |
| 0x180 | u64 | number of data bytes |
| 0x188 | s32 | state: 0 offline, 1 in this game, 2 online elsewhere |

### 3.6 The presence data (0x60 bytes)

| Offset | Type | Meaning |
|---|---|---|
| 0x00 | u32 | network version word (`u3h_fr_key`) |
| 0x04 | u8 | activity: 0 in the menus, 1 searching, 2 in a match, 3 in a match that has ended / is between rounds |
| 0x05 | u8 | party size |
| 0x06 | u8 | activity 2: the low byte of game function 0x727290; else 0 |
| 0x07 | u8 | party locked (non-zero) |
| 0x08 | u8 | the net menu mode (low byte of the word at NET +0x3e4) |
| 0x0c | s32 | activity 2: first score; else 0 |
| 0x10 | s32 | activity 2: second score; else 0 |
| 0x14 | s32 | activity 2 or 3: net game index; else -1 |
| 0x18 | s32 | activity 2 or 3: the rules byte (difficulty); else -1 |
| 0x20 | s64 | activity 2: the match end time in seconds (same scale as the "seconds now" function), 0 = none |
| 0x28 | u64 | party id |
| 0x30, 0x34 | u32 x 2 | two words of the own statistics record (rank, experience) |
| 0x38 | u32 x 8 | eight words of the own statistics record |
| 0x58 | u64 | 0 |

Every byte not listed (0x09..0x0b, 0x1c..0x1f) is 0.

### 3.7 Other game data

| Image offset | Type | Meaning |
|---|---|---|
| 0x17b4ba4 | s32 | the game clock (0x17b4a90 + 0x114), 300 ticks per second |
| 0x17b4ca4 | s32 | a second clock of the same object (0x17b4a90 + 0x214), same unit |
| 0x17bd080 | object | the menu: +0x1498 pointer to the wait popup's record, whose u32 at +8 is the popup's text id; +0x2160 s32 number of page event handlers; +0x2168 the handlers, 0x18 bytes each: u32 page id at +0, pointer callback at +8, u32 at +0x10, u32 at +0x14; the table has room for 0x37 handlers |
| 0x17c3570 | u8 | "party locked" setting |
| 0x17c3871 | 0x24 bytes | the selected player's id |
| 0x17c3898 | u64 | the selected player's id on a service the helper does not support (always written as 0) |
| 0x17d5c58 | object | the net state ("NET"): +8 u8 non-zero = LAN / not online (image 0x17d5c60); +0xc s32 the net clock, 300 ticks per second (image 0x17d5c64); +0x40 s32 non-zero = a search / matchmaking runs (image 0x17d5c98); +0x3e4 the net menu mode, of which the low byte is used (image 0x17d603c); +0x408 s32 (image 0x17d6060) |
| 0x17dc298 | object | the party session ("SES"): +0x18 u64 party id (0 = none); +0x204 u32, bit 0 = the session is locked; members: 10 records of 0x120 bytes from +0x680 (image 0x17dc918): the member's id text at +0, u8 "in use" at +0x25 |
| 0x17cb060 | object | the net game manager ("NGM"): +0x34d u8 and +0x34e u8 two phase flags; +0x661c s16 net game index; +0x6808 s32 non-zero = a match exists; +0x6a24 u8 non-zero = the match is over / between rounds; +0x6a3c 32-bit float, the time limit in seconds; +0x6a48 u8 the rules byte; +0x6a80 s32 and +0x6a84 s32 two end times in net clock ticks |
| 0x17e338e | s16 | first score (0x17e338c + 2) |
| 0x17e34be | s16 | second score (0x17e338c + 0x130 + 2) |

The net game table: script symbol 0x3b9a067d (through `u3x_sym_table`): s32 number of entries at +0, pointer to the
entries at +8; an entry is 0xd0 bytes; its s32 at +0xc is the game type.

The own profile: `u3h_lg_profile(base, 0)`; its u32 at +0x7ae8 is the state (3 = loaded). The statistics record
(`u3h_lg_rec`): u32 words at +0x4fc, +0x504 and +0x5a4 .. +0x5c0.

Ids. Text ids: 0xfd2de693 the game's title (presence title). Popup page: 0x471007a1. Quick-action pages:
0xecc3219d (friend actions), 0x99ab57c2 (the default page). Quick-action pairs (text id, action id):

| Entry | Text id | Action id |
|---|---|---|
| Join Party | 0xb1600ce3 | 0x0ace8463 |
| Invite to Party | 0x4a555973 | 0x45a9c65b |
| View Profile (friend) | 0x4040b706 | 0x3fe6f70e |
| View Profile (player met) | 0x4040b706 | 0xe1810202 |
| Statistics | 0x2962edc6 | 0x089143ea |
| Report Player | 0xc5790997 | 0x0ecbd5bd |
| Add Friend | 0x910fb4f9 | 0x20d51153 |

Float constants: 0x3b5a740e = 7158279 x 2^-31 = 0.0033333334140479565 (one tick in seconds, "K"); 0x43960000 =
300.0.

## 4. Behaviour

"Friends are off" = bit 5 of ea[1] is set. "Busy" = the u64 at NF +0xc8 is non-zero. "Entries" = the pointer at
NF +8; "count" = the u32 at NF +0; "capacity" = the u32 at NF +4.

### 4.1 `u3x_strcasecmp(a, b)`

Compares two NUL-terminated texts byte by byte, each byte taken as unsigned and the ASCII letters 'A'..'Z' counted
as 'a'..'z' (no other byte is changed). At the first position where the two bytes differ the result is -1 when
a's byte is the smaller one, else 1. When both texts end at the same position without a difference the result is
0. A text that is a prefix of the other is the smaller one. No reads past the first difference or the common NUL.

### 4.2 `u3x_friend_in_session(base, e)`

Result 1 when one of the 10 member records of the party session (3.7) is in use and its id text equals the
friend's id (e + 1); else 0. Members are examined in index order; a record whose "in use" byte is 0 is skipped
without a comparison; each comparison is `u3h_fr_strcmp(base, member record, e + 1)` (in this argument order), equal
= result 0; the search stops at the first match. No other effect.

### 4.3 `u3x_friend_find(base, id)`

Null while busy (no comparison is made). Otherwise the address of the first friend entry, in index order among the
first "count" entries, whose id equals `id`: comparisons `u3h_fr_strcmp(base, entry + 1, id)` (this order), stopping
at the first match; null when there is none (also when count is 0; the entries pointer is then not used).

### 4.4 `u3x_friend_add(base, id, presence)` (stray)

- If a listed friend (index order, the first "count" entries) has the same id (`u3h_fr_strcmp(base, id, entry + 1)`
  = 0; this order; stop at the first match): its refresh flag becomes 1; nothing else.
- Otherwise, if count >= capacity (unsigned): nothing.
- Otherwise the list grows by one: count becomes count + 1; in the new last entry the 0x24 id bytes are copied from
  `id` to entry +1; when `presence` is not null its 0x190 bytes are copied to entry +0x28 and the refresh flag
  becomes 0; when `presence` is null the 0x190 detail bytes are left as they are and the refresh flag becomes 1;
  ea[65] is incremented. No other byte of the entry is written.

### 4.5 `u3x_friend_refresh_one(base, e)` (stray)

The 0x190 detail bytes at e +0x28 are zeroed; then `u3h_presence(base, e + 1, e + 0x28)`; a negative result
increments ea[66] (the details then stay zero, or whatever the call left); the refresh flag (e[0]) becomes 0.

### 4.6 `u3x_friend_refresh(base)`

Nothing while busy. Otherwise, with n = count read once at entry: every entry 0 .. n-1 (index order) whose refresh
flag is non-zero is re-read as in 4.5; then `u3h_fr_sort(entries, n)` is called (always, also when nothing was
flagged or n is 0); then ea[67] is incremented.

### 4.7 `u3x_friend_list_thread(base)`

- When count is 0: c = `u3h_friend_count(base)`; a negative c increments ea[68] and counts as 0. The number of
  friends fetched is the smaller of c and capacity. For i = 0 .. that number - 1, in order: an id buffer (0x24
  bytes) and a presence buffer (0x190 bytes), both zeroed before each call, are filled by
  `u3h_friend_get(base, i, id buffer, presence buffer)` (its result is ignored) and handed to the add of 4.4 (with
  the presence buffer, never null). Consequences of 4.4 that must hold: two fetched friends with the same id give
  one entry whose refresh flag is 1; a failed fetch gives an entry with an empty id (once).
- When count is not 0: no fetch.
- In both cases the busy marker (u64 at NF +0xc8) is 0 at the end.

### 4.8 `u3x_friend_init(base)`

Nothing when friends are off. Otherwise, with E, M, N, B0, B1 = `u3h_friend_storage(0)` .. `(4)`, the state after
the call is:

- NF: count 0, capacity 100, entries = E; players met: number 0, capacity 50, pointer (+0x18) = M; new players:
  capacity (+0xe8) 0x10, number (+0xec) 0, pointer (+0xf0) = N; +0xe0 u64 0; +0x48 = B0; +0x50 = B1; +0x58 and
  +0x5c u32 0; the 0x60 bytes at +0x60 zero; +0x20 u8 1; +0xc0 u32 0; +0xc8 u64 1 (the fetch runs at the next
  tick); +0xf8 u64 0.
- In each of the 100 entries of E: u64 at +0x1b8 = 0 and u8 at +0x1c0 = 0. In each of the 50 entries of M: u32 at
  +0x24 = 0. u32 at B0 +0x1b0 = 0 and at B1 +0x1b0 = 0.
- ea[64] incremented.

Nothing else is written (the other bytes of the entries, NF +0x21 .. +0x44 and NF +0xd0 keep their values). No game
call.

### 4.9 Title details, `u3x_presence_clear(base, s)` (stray)

"Title details": a 0x190-byte block that is all zero except for the title text: t = `u3h_text(base, s, 0xfd2de693)`;
the bytes of t up to its NUL, at most 0x7f of them, are at offset 0; byte 0x7f is 0.

`u3x_presence_clear`: the 0x60 bytes at NF +0x60 become zero; then the title details are published with
`u3h_presence_set(base, details, 3)`; the result is ignored. (The old code publishes only when the text pointer is
not null; `u3h_text` never returns null. If it did: nothing is published, the 0x60 bytes are still cleared.)

### 4.10 `u3x_presence_tick(base, s)`

1. Nothing when the LAN byte (NET +8) is non-zero.
2. now = the game clock (image 0x17b4ba4). Nothing when now < the s32 at NF +0xc0 (signed). Otherwise NF +0xc0 =
   now + 1500, whatever happens next.
3. A new presence data record D (0x60 bytes, layout 3.6) is built:
   - +0x00 = `u3h_fr_key(base)`.
   - +0x07 = 1 when bit 0 of the u32 at SES +0x204 is set, else the byte at image 0x17c3570 as it is.
   - +0x28 = the u64 at SES +0x18.
   - +0x05 = the low byte of game function 0x7a2b10 (SES).
   - +0x08 = the byte at NET +0x3e4.
   - Statistics: p = `u3h_lg_profile(base, 0)`. When p is null, +0x30 .. +0x57 are zero. Otherwise r =
     `u3h_lg_rec(base, s, p)` is asked first, and only after that call the profile's state word (p +0x7ae8) is
     read: when it is 3, +0x30 = the u32 at r +0x4fc and +0x34 = the u32 at r +0x504, else both 0. The eight words
     at +0x38 are filled whenever p is not null, loaded or not: word k (k = 0 .. 5) = the u32 at r +0x5a4 + 4k,
     word 6 = the u32 at r +0x5c0, word 7 = the u32 at r +0x5bc.
   - Activity, by the match state:
     - s32 at NGM +0x6808 is 0 (no match): +0x04 = 1 when the s32 at NET +0x40 is non-zero, else 0; +0x06 = 0;
       +0x0c = 0; +0x10 = 0; +0x14 = -1; +0x18 = -1; +0x20 = 0.
     - a match exists and the byte at NGM +0x6a24 is 0: +0x04 = 2; +0x06 = the low byte of game function 0x727290
       (NGM); +0x10 = the s16 at image 0x17e34be, sign-extended; +0x0c = the s16 at image 0x17e338e, sign-extended;
       +0x14 = the s16 at NGM +0x661c, sign-extended; +0x18 = the byte at NGM +0x6a48, zero-extended; +0x20 = the
       end time of 4.12 when that function returns non-zero, else 0.
     - a match exists and that byte is non-zero: +0x04 = 3; +0x14 and +0x18 as in the previous case; +0x06, +0x0c,
       +0x10, +0x20 = 0.
   - everything else zero.
   Order of the calls in this step: `u3h_fr_key`, game 0x7a2b10, `u3h_lg_profile`, `u3h_lg_rec` (when p is not
   null), then in the "in a match" case game 0x727290 and after it the end time of 4.12.
4. When D equals the 0x60 bytes at NF +0x60: nothing more (no publish, no counter).
5. Otherwise D is copied to NF +0x60; a details block is built: the title details of 4.9 (this is where
   `u3h_text` is called), D at +0x100, the u64 0x60 at +0x180 (state and status stay zero); ea[70] is incremented;
   then `u3h_presence_set(base, details, 3)`. When its result is negative: the 0x60 bytes at NF +0x60 are zeroed,
   NF +0xc0 = (the game clock read again at this moment) + 3000, and ea[71] is incremented.

### 4.11 `u3x_match_time_left(base, s)`: time left in the match, in ticks

1. i = the s16 at NGM +0x661c; T = `u3x_sym_table(s, 0x3b9a067d)` (the lookup is made in every case). Result 0 when
   T is null, when i < 0, when i >= the s32 count of T, or when T's entries pointer is null.
2. type = the s32 at +0xc of entry i. Result 0 when type is 4, 9, 11, 13, 16 or 18 (game types without a time
   limit; the old condition lists 4, 8, 9, 11, 13, 16, 17, 18 and then takes 8 and 17 out again).
3. Otherwise, with C = the net clock (s32 at image 0x17d5c64):
   - A. the byte at NGM +0x34e is non-zero: d = (s32 at NGM +0x6a80) - C, a wrapping 32-bit difference taken as
     signed; result = TRUNC32(d x K x 300.0).
   - B. else the byte at NGM +0x34d is non-zero: the same with the s32 at NGM +0x6a84.
   - C. else: result = TRUNC32(f x 300.0), f = the 32-bit float at NGM +0x6a3c.

Float maths and rounding points (x87; checked in the shipped v7.212 helper): d is converted to floating point
exactly (it is NOT rounded to a 24-bit float: d = 16777217 stays 16777217); the product with K (pattern 0x3b5a740e)
is NOT stored as a 32-bit float; it is multiplied by 300.0 (pattern 0x43960000) and that product goes straight to
the integer conversion. Order: (d x K) x 300. No value is rounded to 32 bits anywhere in this function; in case C
the only operation is f x 300. TRUNC32 = conversion to s32 with rounding toward zero whatever the current rounding
mode is; a value that does not fit in s32 after truncation, an infinity or a NaN gives the pattern 0x80000000.

In exact arithmetic (which is what the x87 delivers at its 64-bit precision, all products here having at most 62
significant bits): cases A and B give trunc(d x 536870925 / 2^29) = trunc(d + 13 d / 2^29). So the result is d
itself for |d| <= 41297762; d = 41297763 gives 41297764; d = 2147483596 gives 2147483647 and d = -2147483596 gives
-2147483647; from d = 2147483597 upward and from d = -2147483597 downward the result is 0x80000000. Case C: the product is exact (24 x 7 significant bits); f = 7158278.0 gives 2147483400, f =
7158279.0 gives 0x80000000. (When the x87 unit runs at 53-bit precision the products of A and B are rounded for
|d| > 16777215; this cannot change the result for |d| <= 41297762. The reference for larger values is the operation
sequence above under the control word in force.)

No game call, no memory written.

### 4.12 `u3x_match_end_time(base, s, out)`: the match end time for the presence data

1. tl = the time left of 4.11. When tl is 0: result 0, `*out` untouched, nothing else is read or called.
2. Otherwise x = (the net clock, s32 at image 0x17d5c64) + tl (wrapping). Then now = the "seconds now" function
   (3.1) called with a null argument, or 0 when its pointer is null. Then e = (the s32 at image 0x17b4ca4, read
   after that call) - x, a wrapping 32-bit difference taken as signed. sec = TRUNC64(e x K). `*out` = now - sec
   (s64). Result 1.

Rounding points: e is converted exactly (not rounded to 24 bits: e = 16777499 gives 55924, not 55925), multiplied
once by K (0x3b5a740e), and the product goes straight to a 64-bit integer conversion with rounding toward zero;
nothing is stored as a 32-bit float. In exact arithmetic sec = trunc(e x 7158279 / 2^31), which is e / 300
truncated toward zero for every |e| <= 41297762 (299 -> 0, 300 -> 1, -299 -> 0, -300 -> -1). The value always fits.
(At 53-bit x87 precision the product is still exact for |e| <= 1258291169; beyond that the reference is the
operation sequence under the control word in force.)

### 4.13 `u3x_friend_click(base, ev)`: a friend row clicked

1. The u64 at image 0x17c3898 becomes 0, in every case.
2. row = ev word 1. Nothing more when busy, when row < 0, when row >= count (unsigned), or when the entries pointer
   is null.
3. The 0x24 id bytes of entry `row` (entry +1) are copied to image 0x17c3871; ea[78] is incremented.
4. A quick-action list is built: argument 0, page 0xecc3219d, and these pairs in this order (ids in 3.7):
   - Only when the friend is not in the party session (4.2 gives 0), the s32 at NET +0x40 is 0 and the s32 at
     NET +0x408 is 0 (tested in this order, each only when the previous held):
     - **Join Party**, when all of these hold, tested in this order, each only when the previous held: the friend
       has presence data D of this game (`u3h_fr_pdata` not null); the entry's state (s32 at +0x1b0) is 1; D byte 7
       is 0 (party not locked); the own party has fewer than 2 players (game function 0x7a2b10 (SES), signed
       result < 2) or D byte 4 is not 2 (D byte 4 is looked at only when the party has 2 or more); D byte 8 is
       neither 1 nor 4. Game function 0x7a2b10 is therefore called exactly once when the first three conditions
       hold, and not at all otherwise.
     - **Invite to Party**, always.
   - **View Profile (friend)**, then **Statistics**.
   - **Report Player**, when the LAN byte (NET +8) is 0.
5. The list is shown with `u3h_fr_qa_push(base, list)`. The View Profile pair must be in the list handed over:
   that function removes it and counts it (ea[77]).

`u3h_fr_pdata` may be asked once or several times (the old code asks up to five times); the number of these calls
is not part of the behaviour. Pair slots beyond the count are not read by anybody.

### 4.14 `u3x_met_click(base, ev, target)`: a players-met row clicked

1. row = ev word 1. Nothing when busy, when row < 0, when row >= the number of players met (u32 at NF +0x10,
   unsigned), or when the players-met pointer (NF +0x18) is null. (Image 0x17c3898 is not written by this
   function.)
2. The 0x24 id bytes of players-met entry `row` (pointer + row x 0x28) are copied to `target` and to image
   0x17c3871; ea[79] is incremented.
3. A quick-action list: argument 0, page 0x99ab57c2, pairs in this order: Add Friend, View Profile (player met),
   Statistics, and Report Player when the LAN byte (image 0x17d5c60) is 0. Shown with `u3h_fr_qa_push(base, list)`
   (the View Profile pair is in the list handed over).

### 4.15 `u3x_wait_popup(base, text_id, callback)`

1. The popup's text: the u32 at +8 of the record the menu's pointer +0x1498 points to becomes `text_id` (the
   pointer is not tested).
2. The popup page's event handler: n = the s32 handler count (menu +0x2160). Among handlers 0 .. n-1 the LAST one
   whose page id is 0x471007a1 is reused. When there is none: if n > 0x36 the function ends here (the text is
   already changed, no page is shown); otherwise handler n is taken and the count becomes n + 1.
3. The handler chosen gets: page id (u32 at +0) 0x471007a1, callback (8 bytes at +8) = `callback`, u32 at +0x10 =
   0, u32 at +0x14 = 0. Its bytes +4 .. +7 are not written.
4. Game function 0x651940 (menu, 0x471007a1, 0, 0); result ignored.

### 4.16 `u3x_friend_shutdown(base, s)`

Nothing when friends are off or when the entries pointer is null. Otherwise, in this order:

1. while busy: the fetch of 4.7 runs first (to completion);
2. the title-only presence is published (4.9);
3. NF is cleared: +8 u64, +4 u32, +0 u32, +0x18 u64, +0x14 u32, +0x10 u32, +0x48 u64, +0x50 u64, the 0x24 bytes at
   +0x21, +0x5c u32: all 0;
4. ea[69] is incremented.

Not written: NF +0x20, +0x58, +0xc0, +0xe0 .. +0xf8, and the storage arrays themselves.

## 5. Test scenarios

Fake image with NF, NET, SES, NGM, the menu, the clocks; recording stubs for the game functions 0x7a2b10,
0x727290, 0x651940, 0x139c710, 0x6528d0 and for the "seconds now" pointer; the account-service exports
(`u3h_friend_count`, `u3h_friend_get`, `u3h_presence`, `u3h_presence_set`) scripted.

1. `u3x_strcasecmp`: ("abc", "ABC") 0; ("abc", "abd") -1; ("b", "A") 1; ("", "") 0; ("a", "") 1; ("", "a") -1;
   ("ab", "abc") -1; bytes 0x80 and above compare as unsigned ("\xe9" against "z" gives 1); "[" (0x5b) against
   "a": -1, and "@" (0x40) against "`" (0x60): -1 (only A..Z are folded).
2. In session: no member in use (no comparison at all); the member in slot 3 equal, slot 1 in use and different
   (two comparisons, result 1); an equal id in a slot whose "in use" byte is 0 (result 0).
3. Find: busy (null, no comparison); count 0; id in entry 0; id in the last entry; id twice (the first is
   returned); no match.
4. Add: known id (flag 1, nothing else, with and without presence); new id with presence (copied, flag 0, count
   and ea[65] up); new id with null presence (old detail bytes kept, flag 1); list full (nothing); capacity 0.
5. Refresh one: `u3h_presence` succeeds (details from it, flag 0); fails (details zero, ea[66] up, flag 0).
6. Refresh: busy (nothing, no counter); n = 0 (sort called with 0, ea[67] up); three entries with flags 0, 1, 1
   (two presence reads, in index order, then the sort with n = 3).
7. List fetch: count already 3 (only the busy marker cleared); count 0 and the service reports -1 (ea[68], count
   stays 0, marker cleared); reports 5 with capacity 100 (5 gets, 5 entries); reports 120 with capacity 100 (100
   gets); reports 3 where the get of friend 1 fails and leaves its buffers untouched (entry with an empty id); two friends with the same id (one entry, flag 1).
8. Init: friends off (nothing); on: every field of 4.8 from a memory filled with 0xff, and the untouched bytes
   still 0xff.
9. Presence clear: text "Title" (details: "Title", zeros; options 3); a text of 0x90 characters (0x7f characters
   and a NUL at 0x7f); NF +0x60 zeroed.
10. Shutdown: off; not set up; set up and not busy (publish, fields cleared, ea[69]); set up and busy with count 0
    (the fetch runs first, then the rest).
11. Presence tick: LAN byte set (nothing); clock below NF +0xc0 (nothing, +0xc0 unchanged); no match, no search
    (activity 0, -1 / -1); search (activity 1); in a match with end time (all match fields, +0x20); in a match
    where the end time function returns 0 (+0x20 zero); match over (activity 3; byte 6, scores and +0x20 zero);
    profile null (0x30 .. 0x57 zero); profile state 3 before and after the record call (rank, experience, eight
    words in the order of 4.10); a record stub that changes the state from 3 to 2 (rank and experience 0, the
    eight words still filled); session locked bit set (byte 7 = 1) and clear with the setting byte 0, 1 and 5
    (byte 7 = 0, 1, 5); D equal to the published one (no publish, no ea[70]); D different and the publish
    succeeds (NF +0x60 = D, details checked byte by byte, size 0x60, options 3, ea[70]); the publish fails
    (NF +0x60 zero, +0xc0 = clock + 3000 with the clock changed by the stub, ea[70] and ea[71]); negative scores
    and a negative net game index (sign extension); the order of the calls.
12. Time left: table null; index -1; index = count; entries pointer null; each type 0 .. 20 (0 for 4, 9, 11, 13,
    16, 18 only); case A with d = 0, 1, -1, 9000, 16777217, 41297762, 41297763 (-> 41297764), -41297763
    (-> -41297764), 2147483596 (-> 2147483647), 2147483597 (-> 0x80000000), -2147483596 (-> -2147483647), -2147483648 (-> 0x80000000), and a
    wrapping difference (end 0x80000000, clock 1 -> d = 2147483647); case B the same values with flag A clear; both
    flags set (A wins); case C with f = 600.0 (180000), 0.5 (150), pattern 0x3b5a740e (1), pattern 0x3b5a740d (0),
    -1.0 (-300), 7158278.0 (2147483400), 7158279.0, +infinity, a NaN (all three 0x80000000), 0.0 and -0.0 (0).
13. End time: time left 0 (result 0, `*out` untouched, the "seconds now" function not called); e = -6000 (now +
    20), 299, 300, 301, -299, -300, -301, 16777499 (55924), 41297762, a wrapping e; the "seconds now" pointer null
    (now = 0); a time left of 0x80000000 (not 0: the computation goes on); the second clock changed by the
    "seconds now" stub (the changed value is the one used).
14. Friend click: busy; row -1; row = count; entries null (in all four image 0x17c3898 is zeroed and nothing
    else); friend in the session (View Profile, Statistics, Report handed over); not in the session with NET +0x40
    non-zero, and with NET +0x408 non-zero (same); free to invite, no presence data (Invite first, 0x7a2b10 not
    called); presence data with state 2; with byte 7 set (0x7a2b10 not called in both); party of 1 and D byte 4 =
    2 (Join); party of 2 and D byte 4 = 2 (no Join); party of 2 and D byte 4 = 3 (Join); D byte 8 = 1 and = 4 (no
    Join; 0x7a2b10 called once); LAN byte set (no Report); the full list Join, Invite, View Profile, Statistics,
    Report with page 0xecc3219d and argument 0; the id copy and ea[78].
15. Met click: busy; row -1; row = number; pointer null (nothing at all, image 0x17c3898 untouched); a valid row
    with the LAN byte 0 and 1 (four or three pairs handed over, page 0x99ab57c2, both id copies, ea[79]).
16. Wait popup: n = 0 (handler 0 written, count 1, page shown); an existing handler for the page among others (it
    is rewritten, count unchanged); two existing handlers for the page (the later one is rewritten, the earlier
    one untouched); n = 0x36 without a handler (handler 0x36 written, count 0x37); n = 0x37 without a handler
    (text changed, count unchanged, no handler written, the page function NOT called); n = 0x37 with a handler
    for the page (rewritten, page shown); bytes +4 .. +7 of the handler untouched.

## 6. Oddities of the old code that are behaviour to keep

1. Friend click: the old code has exits for "the list is full" after Join and after Invite and a test "fewer than
   8 pairs" before View Profile / Statistics. With at most five pairs they can never trigger; the list is as in
   4.13.
2. Friend click zeroes image 0x17c3898 before every check; the players-met click never writes it.
3. View Profile is put into both lists although `u3h_fr_qa_push` always takes it out again; only the counter ea[77]
   shows it.
4. Time left: the type list names 8 and 17 and then excludes them. A time left of exactly 0 ticks (the match ends
   now, or a time limit below 1/300 s) is reported as "no end time" by 4.12.
5. Time left, entries pointer: the old source tests the computed entry address for null; the shipped build tests
   the entries pointer (4.11 follows the shipped build; the two differ only for a null pointer with a non-zero
   index, which the source would dereference).
6. End time: it subtracts a net-clock value from the second clock of the game object (two different clocks) and
   uses the constant K, slightly above 1/300.
7. Wait popup: the text is changed before the room check, so a full handler table changes the text without
   showing the page; the last matching handler wins, not the first; a negative count is not guarded (handler
   index n is written as for any n <= 0x36).
8. Add with a null presence leaves the previous detail bytes of the slot in place until the refresh; the count is
   raised before the entry is filled (not visible: single thread).
9. List fetch: the result of `u3h_friend_get` is ignored, so a failed fetch appends one friend with an empty id;
   the fetch runs only while the list is empty, a second run just clears the busy marker.
10. Refresh sorts even when nothing was flagged.
11. Presence tick: the next-build time is advanced before anything is built; the "loaded" test reads the profile
    state after the record getter, which can change it; the eight statistics words are sent even when the profile
    is not loaded; byte 7 takes the setting byte unchanged (any non-zero value); the retry time after a failed
    publish uses a fresh clock read.
12. Presence clear tests the text pointer for null although `u3h_text` never returns null, and ignores the result
    of the publish.
13. Shutdown starts a pending list fetch (a network read) just before it throws the list away; it does not reset
    NF +0xc0, +0x20, +0x58 or the new-players fields.
14. Init does not clear the entries themselves (only two fields of each), nor NF +0x21 .. +0x44.
15. The C wrappers `fr_strcasecmp`, `fr_add`, `fr_refresh_one`, `fr_presence_clear` have no caller left in v7.212.

## 7. Strays, and what stays as it is

Strays added to this group (in no work group of the inventory, called by this group's functions, old port moved as
written): `u3x_friend_add` (4.4), `u3x_friend_refresh_one` (4.5), `u3x_presence_clear` and the file-private
`title_details` (4.9), the file-private offset table `presence_stats` (replaced by the rule in 4.10) and
`friends_off` (3.1).

Stays as it is:

- The C wrappers and the `u3h_` exports named in section 1 (other groups call them); `fr_tick` and `fr_kind` (the
  C dispatchers); the C part's static storage (`u3h_friend_storage`, `nf_pmtarget`), which lives in the C part's
  own data section; `u3h_fr_key` (its value is shared with another thread through the C part); `u3h_fr_pdata`;
  `u3h_fr_qa_push` (the View Profile removal and its counter are the helper's own decision, not part of this
  group); `u3h_fr_sort` / `u3x_friend_sort`; `u3x_quick_action_add`, `QuickActions`, `u3x_strncpy_pad`,
  `u3x_sym_table`, `u3x_friend_in_game`.
- Nothing of this group has to stay in C: the two float functions use only an integer-to-x87 load, one or two
  multiplications by a constant and a truncating integer store, all of which the C++ part can express (constants
  through `x87_const`, no float passed between functions). The comment at `fr_presence_tick` ("the match end time
  stays in C: x87 float") becomes obsolete.

## 8. Open questions

1. The meaning of game function 0x727290's result (presence byte 6) is not named in the old source; the spec gives
   the call and the store only, which is all an implementation needs.
2. x87 precision: the exact-arithmetic formulas of 4.11 / 4.12 assume the 64-bit precision setting. At a 53-bit
   setting results can differ only for tick differences beyond 41297762 (about 38 hours). The tester should run old
   and new under the same control word.
3. The number of `u3h_fr_pdata` / `u3h_fr_ent` calls is declared not observable (both are accessors of the C part).
   If the tester records them, 4.13 needs the old count (up to five `u3h_fr_pdata` calls).

## 9. Clarifications

Facts confirmed by the tester's old-against-new test (`reimpl2\G18\test_g18.cpp`, 2026-11-14). Nothing in sections 1
to 7 was found wrong; these are additions and the answers to the open questions of section 8.

1. **The two time functions, reference arithmetic (4.11, 4.12; open question 2).** The shipped v7.212 helper and the
   old code as built for the test execute the same x87 operations: the clock difference is loaded as an integer,
   multiplied by the 32-bit constant K, in 4.11 multiplied by the 32-bit constant 300.0, and stored as an integer with
   the rounding field of the control word set to "toward zero" for the store only; the precision field is the one in
   force. Nothing is stored as a float in between. With the control word 0x37f (64-bit precision: the x87 reset value, which
   the PC runtime also gives its fibers) the results are the exact-arithmetic values of 4.11 and 4.12 for EVERY input:
   all 2^32 clock differences of case A, of case B and of 4.12, and all 2^32 float patterns of case C. Exact integer
   arithmetic (trunc(d x 536870925 / 2^29) with 0x80000000 outside the s32 range; the float x 300 computed exactly;
   trunc(e x 7158279 / 2^31)) is therefore a complete replacement at that setting.
2. **The same at 53-bit precision (control word 0x27f, the default of a Windows thread).** Case C and 4.12 give the
   exact-arithmetic values for every input (no difference at all). Cases A and B differ from the exact value for
   exactly 356 clock differences d: 178 positive values and their negatives, the smallest being |d| = 206488812 (more
   than 7.9 days of time left; the sentence in 4.11 that names 41297762 is correct but far too cautious). The
   difference is always one tick: for 175 of the 178 the x87 result is one further from zero, for three (|d| =
   1321528399, 1651910499, 1858399311) one nearer to zero. The test program prints the full list (`test_g18
   list53`). Which control word is in force when the helper's hooks run on the PC was not determined here; it cannot
   matter for a match with less than 7.9 days left.
3. **Entries pointer of the net game table (6.5).** The old code built for the test behaves as the shipped build: a
   null entries pointer gives 0 for every index.
4. **Calls of the accessors (open question 3).** The test does not record `u3h_ext`, `u3h_fr_ent` and `u3h_fr_pdata`.
   The old friend click asks `u3h_fr_pdata` up to five times, one call is enough; the entry address may be computed
   from the entries pointer (3.3, 3.4) instead of asking `u3h_fr_ent`. The old title code copies the text with the
   padding copy `u3x_strncpy_pad` (0x80 bytes, then byte 0x7f cleared); copying at most 0x7f bytes into the zeroed
   block gives the same block, and that call is not part of the behaviour either.
5. **Presence tick without a title text (4.10 step 5).** If `u3h_text` returned null (it never does), the tick would
   still publish: the details then have an empty title, the data and its size. Only `u3x_presence_clear` skips the
   publish for a null text (4.9).
6. **Wait popup, handler count -1 (6.7).** Handler index -1 lies 0x18 bytes before the table: its page id goes to
   menu +0x2150, its callback to menu +0x2158, and its two zero words are the count itself (menu +0x2160) and the word
   after it (+0x2164). The count is first raised to 0 and then zeroed again by the handler write: it ends as 0 in
   either order. Counts below -1 write a handler further in front of the table and leave the count at n + 1.
7. **List fetch on a list that was never set up or was torn down** (count 0, capacity 0, entries pointer null):
   `u3h_friend_count` is still asked (and a negative answer counted in ea[68]), no friend is fetched, the busy marker
   is cleared. An add in that state does nothing and makes no comparison.
8. **After set-up the first presence tick publishes at once** (the next-build time is 0 and the published data is all
   zero, while a new record always holds the non-zero version word), when the LAN byte is 0 and the game clock is not
   negative.
