# G19. Friends: list rows, players met, friend header

Behaviour description for a reimplementation (reimpl2 pipeline, describer's document). It says what the functions
do, not how the old ones were written. Conventions as in `G18_friends_list.md` (read it first: this document uses
its names and layouts and does not repeat them): offsets are hexadecimal when prefixed with 0x; "image offset X" is
the address `base + X`; all multi-byte values are little-endian; "u8 / u16 / u32 / u64 / s16 / s32 / s64" are
integers of that width, "pointer" is 8 bytes. "NF", "NET", "SES", "NGM", "friend entry", "presence data", "busy",
"entries", "count" and "friends are off" mean what G18 sections 3 and 4 define.

The group is written as **two independent parts**. Neither part calls a function of the other.

- **Part 1, what the pages show** (sections 4.1 to 4.9): the Friends list source, the friend header source and the
  small helpers both use (party suffix, joined text, activity text, icon answer, state icon, current tab).
- **Part 2, list upkeep** (sections 5.1 to 5.6): players met, the match's new players and their history, the
  players-met fetch, friend events.

## 1. Purpose and callers

The PS4 game kept the Friends page and the Friend Actions page but their data sources are empty functions, and it
lost the code that records the players of a match. Part 1 answers the list requests of the two sources (row texts,
row icons, selection, the header lines of the friend that was picked). Part 2 keeps the players-met list and the
friend list current: players of a match go to the account service's history, the players-met list is read back
from it, and friend events (a friend went offline, a friend's presence changed, the friend list changed on the
service) flag or add friends.

Callers (all stay as they are; every C wrapper already forwards its arguments unchanged):

| Function | Part | Called by | When |
|---|---|---|---|
| `u3x_friend_party_suffix` | 1 | `u3x_friend_list`, `u3x_friend_header`; C wrapper `fr_party_suffix` (no caller left) | a status line with a party size |
| `u3x_friend_join` (stray) | 1 | `u3x_friend_list`, `u3x_friend_header`; C wrapper `fr_sprintf2` (no caller left) | a status line made of two texts |
| `u3x_friend_activity` (stray) | 1 | `u3x_friend_list`; C wrapper `fr_activity_text` (no caller left) | the list's status column |
| `u3x_friend_icon` | 1 | `u3x_friend_list`, `u3x_friend_header`; C wrapper `fr_icon22`, used by the C export `u3h_fr_icon22` (called by `u3x_invite_list`, C++, other group) | a state icon answer |
| `u3x_friend_state_icon` (stray) | 1 | `u3x_friend_list`, `u3x_friend_header`; C wrapper `fr_state_icon`, used by the C export `u3h_fr_state_icon` (called by `u3x_invite_list`) | a state icon answer |
| `u3x_page_tab_text` | 1 | `u3x_friend_list`; C wrapper `fr_tab`, used by the C source `fr_pm_source` and by the C export `u3h_fr_tab` (called by `u3x_invite_list` and by the vote list source `u3x_mmv_source`, C++, other groups) | "which tab of the top page is selected" |
| `u3x_friend_list` | 1 | C wrapper `fr_list_source`, first of the chain `fr_sources`, which `u3_profile_event` and `n03_source` call for every list request | every list request |
| `u3x_friend_header` | 1 | C wrapper `fr_header_source`, fourth of the chain `fr_sources` | every list request the earlier sources did not take |
| `u3x_history_thread` | 2 | `u3x_players_met_add`; C wrapper `fr_pm_hist_thread` (no caller left) | new players of a match |
| `u3x_players_met_add` | 2 | `u3x_players_met_fetch`, `u3x_match_players`; C wrapper `fr_pm_add` (no caller left) | fetch, match start |
| `u3x_players_met_fetch` | 2 | C wrapper `fr_pm_fetch`, called by the C source `fr_pm_source` for operation 9 while the selected tab's text is 0xacb77201 (Players Met) | the Players Met tab asks for a refresh |
| `u3x_match_players` | 2 | C wrapper `fr_match_players`, called by `u3_profile_event` for hook kind 10 (match set-up) | a match starts |
| `u3x_friend_events` | 2 | C wrapper `fr_events`, called by the C per-frame `fr_tick` (after the pending list fetch, only while friends are on and the list is set up) | every frame |

Threading: as in G18. Every function runs on the thread that runs the helper's hooks; the group starts no thread,
takes no lock and uses no atomic operation. The "busy" markers at NF +0xc8, +0xd0 and +0xe0 are what is left of
the PS3 worker threads: the work they guard runs to completion inside the call. No function is re-entered.

No function takes or returns a float. The only float value of the group is the bit pattern 0x3f800000 (1.0) stored
as a u32 into an image answer.

## 2. Interface

All functions are `extern "C"`; names and signatures stay.

### 2.1 Part 1: functions to deliver

| Function | Signature | Result |
|---|---|---|
| party suffix | `void u3x_friend_party_suffix(uintptr_t base, void *s, char *buf, uint8_t size)` | - |
| join two texts (stray) | `const char *u3x_friend_join(const char *a, const char *b)` | the buffer that holds the joined text |
| activity text (stray) | `uint32_t u3x_friend_activity(uint8_t a)` | a text id, or 0 |
| state icon answer | `void u3x_friend_icon(int32_t *ev, int32_t v)` | - |
| state icon number (stray) | `int32_t u3x_friend_state_icon(uintptr_t base, uint8_t *e)` | 0, 1 or 2 |
| selected tab | `uint32_t u3x_page_tab_text(uintptr_t base, void *s)` | a text id, or 0 |
| Friends list source | `int u3x_friend_list(uintptr_t base, int32_t *ev, void *s)` | 1 = the request was this source's, 0 = not |
| friend header source | `int u3x_friend_header(uintptr_t base, int32_t *ev, void *s)` | 1 / 0 as above |

File-private pieces of part 1 (free to shape): "the net game record of an index" (4.8), "the rule set of a net
game" (4.8), a bounded text append, the remembered selection and the noted-operations mask (3.2).

### 2.2 Part 2: functions to deliver

| Function | Signature | Result |
|---|---|---|
| history of new players | `void u3x_history_thread(uintptr_t base, void *s)` | - |
| add players | `void u3x_players_met_add(uintptr_t base, void *s, const uint8_t *names, uint32_t n, int flag)` | - |
| players-met fetch | `void u3x_players_met_fetch(uintptr_t base, void *s)` | - |
| players of a match | `void u3x_match_players(uintptr_t base, void *s)` | - |
| friend events | `void u3x_friend_events(uintptr_t base)` | - |

File-private piece of part 2 (old name `listed`): "is this id in the players-met list" (5.1).

Parameters: `base` game image base; `s` the helper's state object (opaque: handed on to `u3h_text`,
`u3h_text_num`, `u3x_sym_table`); `ev` one list request (3.3); `e` a friend entry (G18 3.4); `buf` a buffer of
0x80 bytes; `size` a party size; `a`, `b` NUL-terminated texts; `v` an icon number; `names` `n` ids of 0x24 bytes
each, one after the other; `flag` 0 = into the players-met list, anything else = new players of a match.

### 2.3 Functions of other groups, of the C part and of the shared tools that the new code calls

Not to be described or reimplemented here. `u3h_` are exports of the C part.

| Name | Signature | What it does | Part |
|---|---|---|---|
| `u3h_ext` | `uint8_t *u3h_ext(void)` | the helper's own state block; `ext_ea()` / `ext_eb()` of CONVENTIONS.h give its "ea" and "eb" words | 1, 2 |
| `u3h_gate_closed` | `int u3h_gate_closed(uintptr_t base, int32_t *ev)` | 0 while multiplayer is active (u8 at image 0x17d5c61 non-zero); otherwise stores the game's empty text as the request's text answer, counts, and returns 1 | 1 |
| `u3h_so_note` | `void u3h_so_note(const char *a, const char *name, int32_t v, const char *b)` | one line for the online log: `a`, then `name` (when not null), then `v` in decimal (when v >= 0), then `b` (when not null); nothing while the online part is not loaded | 1 |
| `u3h_text` | `const char *u3h_text(uintptr_t base, void *s, uint32_t id)` | localized text of a text id; never null | 1, 2 |
| `u3h_text_num` | `void u3h_text_num(uintptr_t base, void *s, char *buf, uint32_t size, uint32_t id, int32_t v)` | writes the text `id` with the number `v` filled in into `buf` (at most `size` bytes with the NUL); it first makes `buf` an empty text and leaves it empty when the game's function is missing | 1 |
| `u3h_fr_buffer` | `char *u3h_fr_buffer(void)` | the next of four rotating 0x100-byte text buffers of the C part (a returned buffer is reused at the fifth request after it) | 1 |
| `u3h_fr_ent` | `uint8_t *u3h_fr_ent(uintptr_t base, uint32_t i)` | address of friend entry i (no checks) | 1 |
| `u3h_fr_find` | `uint8_t *u3h_fr_find(uintptr_t base, const uint8_t *name)` | the friend entry with this id, or null; null while busy (G18 4.3) | 1, 2 |
| `u3h_fr_in_session` | `int u3h_fr_in_session(uintptr_t base, uint8_t *e)` | 1 when the friend is a member of the own party session (G18 4.2), else 0 | 1 |
| `u3h_fr_in_title` | `int u3h_fr_in_title(uint8_t *e)` | 1 when the entry's state is 1, or when it is 2 and the presence title text equals this game's title ignoring ASCII case; else 0 | 1 |
| `u3h_fr_pdata` | `uint8_t *u3h_fr_pdata(uintptr_t base, uint8_t *e)` | the friend's 0x60-byte presence data (e + 0x128) when the entry carries one of this game, else null; repeating the call changes nothing | 1 |
| `u3h_fr_refresh` | `void u3h_fr_refresh(uintptr_t base)` | re-reads the flagged friends and sorts the list (G18 4.6) | 1 |
| `u3h_set_icon_plain` | `void u3h_set_icon_plain(uintptr_t base, int32_t *ev, uint32_t id)` | fills the request's image answer with the icon `id` (kind 3, the icon's index in the game's icon table, scale 1.0) | 1 |
| `u3x_menu_page` | `uint32_t u3x_menu_page(const uint8_t *m)` | existing C++ function (group G33): the id of the page on top of a menu's page stack, or 0 (null menu, stack depth outside 0..159, or no page object) | 1 |
| `u3x_sym_table` | `uint8_t *u3x_sym_table(void *s, uint32_t id)` | existing C++ function: the data of a script symbol, or null. Each call makes one call of the state's lookup function (the pointer at `s` + 656) when that pointer is not null | 1 |
| `u3h_fr_strcmp` | `int u3h_fr_strcmp(uintptr_t base, const void *a, const void *b)` | the game's text compare (image offset 0x139c710) with (a, b); 0 = equal | 2 |
| `u3h_pm_add` | `int32_t u3h_pm_add(uintptr_t base, const uint8_t *name, const char *title)` | adds the player `name` to the account service's players history with the description `title`; negative = failed | 2 |
| `u3h_pm_count` | `int32_t u3h_pm_count(uintptr_t base)` | number of entries of the players history; negative = not available | 2 |
| `u3h_pm_get` | `int32_t u3h_pm_get(uintptr_t base, uint32_t i, uint8_t *id)` | history entry i: fills `id` (0x24 bytes); 0, or negative on failure | 2 |
| `u3h_fr_event` | `int32_t u3h_fr_event(uintptr_t base, uint8_t *id)` | takes the next friend event from the account service's queue: 0 = the friend `id` went offline, 1 = the friend's presence changed, negative = no event waits. It fills `id` (0x24 bytes) | 2 |
| `u3h_take_rebuild` | `int u3h_take_rebuild(void)` | 1 once after the account service asked for the friend list to be built again (the request is consumed by the call), else 0 | 2 |
| `u3x_friend_list_thread` | `void u3x_friend_list_thread(uintptr_t base)` | G18 4.7: the pending list fetch; clears the busy marker | 2 |
| `u3x_friend_add` | `void u3x_friend_add(uintptr_t base, const uint8_t *id, const uint8_t *presence)` | G18 4.4: flags a listed friend for a refresh, or appends a new one | 2 |

`u3h_fr_pdata`, `u3h_fr_ent` and `u3h_ext` are plain accessors of the C part: how often they are called is not
part of the behaviour. Every other call of this table, every game call and every `u3x_sym_table` lookup is made
exactly as often and in the order section 4 / 5 says.

## 3. Game and helper facts

### 3.1 The helper's state block

"ea" words as in G18 3.1 (u32 at `u3h_ext() + 0x3c50 + 4 x k`; plain increments that wrap):

| Word | Meaning | Part |
|---|---|---|
| ea[1] | switches; bit 5 (0x20) set = friends are off | 2 |
| ea[72] | counter: history adds that worked | 2 |
| ea[73] | counter: history adds that failed | 2 |
| ea[74] | counter: players-met fetches | 2 |
| ea[75] | counter: match player lists handed over | 2 |
| ea[76] | counter: friend events taken | 2 |
| ea[85] | counter: Friends list requests answered | 1 |
| ea[86] | counter: friend header requests answered | 1 |

The "seconds now" function: the 8-byte pointer at `u3h_ext() + 0x4060` (eb[4..5]), as in G18 3.1: one argument (a
pointer, passed as null), returns s64 seconds; the pointer may be null (then "now" is 0). Part 1 only.

### 3.2 The group's own data (part 1)

- the remembered selection of the Friends list: one s32, 0 at start;
- the noted-operations mask: one u32, 0 at start (bit k set = the log line of operation k was written);
- a zero-filled record of 0x58 bytes, never written ("the empty rule set", 4.8);
- constant texts: the empty text; `Friends list: op ` and ` asked (first time)` (log line); `- `, ` / ` and the
  three characters line feed (0x0a), `-`, space (written "\n- " below); and the 31 characters
  `|@C0xa0a0a0||FB||@R0xffffffff| ` (ending with one space; colour markup around "FB").

### 3.3 The list request `ev`

An array of 32-bit words; "word n" is at byte offset 4 x n.

| Byte offset | Word | Type | Meaning |
|---|---|---|---|
| 0x00 | 0 | s32 | operation: 1 title, 2 row text, 4 row image, 5 get selection, 6 get last row index, 8 "is the list shown", 9 refresh, 10 row selected; others exist |
| 0x04 | 1 | s32 | row index (in), or the answer of operations 5, 6, 8 (out) |
| 0x08 | 2 | s32 | image answer: kind (0 none, 3 icon, 0x22 state icon, 0x24 net game picture) |
| 0x10, 0x14 | 4, 5 | s32 x 2 | image answer: set to 0 |
| 0x18 | 6 | s32 | image answer: the value (icon number, map id) |
| 0x1c, 0x20 | 7, 8 | s32 x 2 | image answer: set to 0 |
| 0x24 | 9 | s32 | image answer: set to -1 |
| 0x28 | 10 | s32 | image answer: set to 0 |
| 0x2c | 11 | s32 | image answer: set to -1 |
| 0x30 | 12 | u32 | image answer: scale as a float pattern, set to 0x3f800000 (1.0) |
| 0x40 | 16..17 | pointer | text answer (pointer to a NUL-terminated text) |
| 0x50 | 0x14 | u32 | image slot id (operation 4) |
| 0x54 | 0x15 | u32 | column number (operation 2 of the Friends list) |
| 0x98 | - | pointer | the source descriptor; its u32 at +0x1c is the source tag |

"The image answer of kind K with value V" below means exactly: word 2 = K, words 4, 5 = 0, word 6 = V, words 7,
8 = 0, word 9 = -1, word 10 = 0, word 11 = -1, word 12 = 0x3f800000. Word 3 and every other word stay as they are.
"The text answer becomes T" means the pointer at byte 0x40 is set to T.

Source tags: 0x4c414eae the Friends list; 0x4c414e13 the friend header.

### 3.4 Game functions called (part 1)

Called with the game's normal calling convention through `game_call`.

| Image offset | Arguments | Result | Meaning |
|---|---|---|---|
| 0xf33010 | (pointer: the text database, address of image offset 0x17026b8; pointer: output buffer; u32 buffer size; u32 text id; s64 value; s64 0) | none | writes the text with its time placeholder replaced by the value (seconds) into the buffer |
| 0x741600 | (s32 difficulty number) | pointer to a NUL-terminated text | the name of a difficulty |

Reached only through other functions: the state's symbol lookup (through `u3x_sym_table`), the game's text
functions (through `u3h_text`, `u3h_text_num`), the text compare 0x139c710 (through `u3h_fr_strcmp` and inside
`u3h_fr_find`, `u3h_fr_in_session`), the "seconds now" function through its pointer (3.1). Part 2 makes no direct
game call.

### 3.5 Game data

Additions to G18 3.3 (NF, image offset 0x17caeb0):

| Offset | Type | Meaning |
|---|---|---|
| 0xd0 | u64 | busy marker of the players-met fetch (not touched by G18's set-up) |

and the fields G18 lists: +0x10 / +0x14 / +0x18 players met (number, capacity, entries of 0x28 bytes: id 0x24
bytes, then a u32), +0x20 "history changed" flag, +0xc8 busy marker of the list fetch, +0xe0 busy marker of the
history upload, +0xe8 / +0xec / +0xf0 new players (capacity, number, ids of 0x24 bytes each).

Friend entry (G18 3.4) fields used here: +1 the id text; +0x1b0 s32 state (0 offline, 1 in this game, 2 online
elsewhere); +0x1c0 a NUL-terminated text (a second name; empty in practice, G18's set-up clears its first byte).
Presence data (G18 3.6) fields used here: +4 u8 activity, +5 u8 party size, +0x14 s32 net game index, +0x18 s32
rules byte (difficulty), +0x20 s64 match end time in seconds (0 = none).

| Image offset | Type | Meaning | Part |
|---|---|---|---|
| 0x17bd080 | object | the menu (argument of `u3x_menu_page`) | 1 |
| 0x17c3570 | u8 | "party locked" setting | 1 |
| 0x17c3871 | 0x24 bytes | the selected player's id (written by the row clicks of G18) | 1 |
| 0x17d5c60 | u8 | LAN byte (NET +8): non-zero = LAN / not online | 1, 2 |
| 0x17d5c61 | u8 | NET +9: non-zero = multiplayer is active | 2 |
| 0x17d6010 | text | the own player name (NET +0x3b8), NUL-terminated | 1 |
| 0x17dd584 | u8 | SES +0x12ec: non-zero = the own party is locked | 1 |
| 0x17cb060 | object | NGM: +0x370 and +0x378 two pointers, the player records of the local players; player records: 10 records of 0x9c0 bytes from +0x390 (image 0x17cb3f0), a record's id text (0x24 bytes) at +0x5b8 | 2 |

Script symbols (data through `u3x_sym_table`; all part 1). None of these tables is copied: they are read at run
time.

| Symbol | Layout |
|---|---|
| 0xf1514340, the tab table | +0 u32 number of rows, +8 pointer to the rows; a row is 0x20 bytes: +0 u32 page id, +4 u32 LAN value (0 or 1), +8 u32 number of tabs, +0x10 pointer to the tabs, +0x18 u32 index of the selected tab; a tab is 0xc bytes with its u32 text id at +0 |
| 0x3b9a067d, the net game table | +0 s32 number of records, +8 pointer to the records; a record is 0xd0 bytes: +4 u32 map id, +8 u32 text id of the rules' name, +0xc s32 game type |
| 0xf985c7b6, the rule sets | +0 s32 number of entries, +8 pointer to the entries; an entry is 0x58 bytes: +0 u32 text id of the game type's name, +8 u32 icon id of the game type |
| 0x0dec97a4, the map table | +0 s32 number of entries, +8 pointer to the entries; an entry is 0x98 bytes: +0 u32 map id, +4 u32 text id of the map's name |

Ids. The Friends tab's text: 0x6e83df53. Image slot of the Friends list's row icon: 0xf5a294f3. Image slots of the
header: 0xb81e2263 avatar, 0x0a071025 net game picture, 0x57135906 game type icon, 0x8e39ed40 state icon.

| Text id | Text (meaning) |
|---|---|
| 0x51663192 | Online |
| 0x6ea711d6 | Offline |
| 0xf461ef26 | Campaign |
| 0xfd2de693 | the game's title |
| 0x953825d0 | Playing another game |
| 0xba8db526 | "[n] players in party" (takes a number) |
| 0x93ac6446 | "Match ends in [TIME]" (takes a time) |
| 0x40d3b7a2 | own party is locked |
| 0x052ac89a | own party is open |
| 0x84041d79 | activity 0 (in the lobby) |
| 0xbd1d2c53 | activity 1 (matchmaking) |
| 0xcf292b57 | activity 2 (in a match) |
| 0xea3bb70e | activity 3 (the match has ended / between rounds) |

## 4. Behaviour, part 1

"Bounded join of texts t1, t2, ... into a buffer": the characters of t1 up to its NUL, then those of t2, and so on,
stopping as soon as 0xff characters are in the buffer; then a NUL. The buffer (0x100 bytes) therefore always ends
with a NUL at index 0xff or before. Once the limit is reached nothing more is written (the first byte of a later
text may still be read, so every text handed in must be a valid pointer).

### 4.1 `u3x_friend_party_suffix(base, s, buf, size)`

All 0x80 bytes of `buf` become zero. When `size` is 2 or more: bytes 0, 1, 2 become space, `-`, space, and
`u3h_text_num(base, s, buf + 3, 0x7d, 0xba8db526, size)` writes the "players in party" text behind them. When
`size` is 0 or 1 nothing else happens (no call).

### 4.2 `u3x_friend_join(a, b)` (stray)

Takes the next rotating buffer (`u3h_fr_buffer()`, exactly one call), writes the bounded join of `a` and `b` into
it and returns it.

### 4.3 `u3x_friend_activity(a)` (stray)

The text id of an activity value: 0 gives 0x84041d79, 1 gives 0xbd1d2c53, 2 gives 0xcf292b57, 3 gives 0xea3bb70e;
every other value gives 0.

### 4.4 `u3x_friend_icon(ev, v)`

The image answer of kind 0x22 with value `v` (3.3). Nothing else.

### 4.5 `u3x_friend_state_icon(base, e)` (stray)

2 when `u3h_fr_in_session(base, e)` is non-zero (called exactly once, in every case); otherwise 1 when the entry's
state (s32 at +0x1b0) is not 0, else 0.

### 4.6 `u3x_page_tab_text(base, s)`

1. T = `u3x_sym_table(s, 0xf1514340)`. Null: result 0, nothing else is read.
2. page = `u3x_menu_page(address of image offset 0x17bd080)`; lan = the LAN byte (image 0x17d5c60), taken as a u32.
3. The rows of T are examined in index order (row count = the u32 at T +0). The first row whose page id equals
   `page` and whose LAN value (u32) equals `lan` decides: the result is the text id (u32 at +0) of tab number
   "selected" (the row's u32 at +0x18) of the row's tabs (pointer at +0x10, 0xc bytes per tab). Neither the tabs
   pointer nor the index is checked (the number of tabs at +8 is not used).
4. No such row (also with 0 rows; the rows pointer is then not used): result 0.

No memory is written.

### 4.7 `u3x_friend_list(base, ev, s)`: the Friends list source

1. Source descriptor (pointer at byte 0x98) null, or its tag not 0x4c414eae: result 0, nothing else.
2. `u3h_gate_closed(base, ev)` non-zero: result 1, nothing else.
3. ea[85] is incremented. op = word 0. From here on the result is always 1.
4. The log line. Only when op is in 0 .. 31 and bit `op` of the noted-operations mask is clear: the selected tab is
   asked (4.6); when it is the Friends tab (0x6e83df53) the bit is set and
   `u3h_so_note("Friends list: op ", null, op, " asked (first time)")` is called. (So each operation number is
   noted once, the first time it arrives while the Friends tab is selected.)
5. The selected tab is asked (4.6; again, whatever happened in step 4: one `u3x_sym_table` lookup per ask). When
   it is not the Friends tab: when op is 6, word 1 becomes -1; nothing else.
6. On the Friends tab, by operation:
   - 1: the text answer becomes the helper's empty text.
   - 5: word 1 becomes the remembered selection.
   - 6: word 1 becomes the last row index: 0 while busy; otherwise count - 1, but 0 when count is 0.
   - 8: word 1 becomes 1.
   - 9: `u3h_fr_refresh(base)`.
   - 10: the remembered selection becomes word 1 (no range check).
   - 2 and 4: step 7. Operations 3 and 7 pass the checks of step 7 and then do nothing.
   - every other operation: nothing.
7. row = word 1. Nothing more while busy, when row < 0, when row >= count (unsigned), or when the entries pointer
   (NF +8) is null. Otherwise e = friend entry `row`.
   - Operation 4: only when word 0x14 is 0xf5a294f3: the state icon answer, `u3x_friend_icon(ev,
     u3x_friend_state_icon(base, e))` (4.5, 4.4). Any other slot: nothing.
   - Operation 2, by column (word 0x15):
     - column 0: the text answer becomes the address of the id text inside the entry (e + 1).
     - column 2: nothing when the byte at e +0x1c0 is 0. Otherwise one rotating buffer is taken (`u3h_fr_buffer`),
       the bounded join of the 31-character markup text of 3.2 and the text at e +0x1c0 is written into it, and
       the text answer becomes that buffer.
     - column 1, the status. In this order:
       1. st = the entry's state. The text answer becomes `u3h_text(base, s, 0x51663192)` (Online) when st is not
          0, else `u3h_text(base, s, 0x6ea711d6)` (Offline).
       2. `u3h_fr_in_title(e)` is asked (always); non-zero: the text answer becomes `u3h_text(base, s,
          0xf461ef26)` (Campaign).
       3. Nothing more when the entry has no presence data of this game (`u3h_fr_pdata` null) or st is not 1.
       4. With D = that presence data: the party suffix of 4.1 is built in a local 0x80-byte buffer for the party
          size D +5 (this is where `u3h_text_num` is called, when the size is 2 or more), whatever the activity
          is.
       5. id = the activity text of 4.3 for D +4. 0: nothing more (the answer of steps 1 / 2 stays).
       6. Otherwise the text answer becomes the joined text of 4.2: `u3h_text(base, s, id)` followed by the
          suffix (the text is fetched first, then the rotating buffer is taken).
     - any other column: nothing.

### 4.8 Net game record and rule set (file-private)

**The net game record of an index** (the index is the s32 at presence data +0x14): i = the low 16 bits of the
index taken as s16. T = `u3x_sym_table(s, 0x3b9a067d)` (one lookup per use). No record (null) when T is null, when
i < 0, or when i >= the s32 record count at T +0. Otherwise the record is at (pointer at T +8) + i x 0xd0; the
pointer itself is not tested.

**The rule set of a net game record**: R = `u3x_sym_table(s, 0xf985c7b6)` (one lookup per use); g = the s32 game
type at record +0xc. When R is not null and g < the s32 count at R +0 (signed compare; there is no lower bound):
the entry at (pointer at R +8) + g x 0x58, g taken as a signed 64-bit factor. Otherwise the empty rule set of 3.2
(all fields 0).

### 4.9 `u3x_friend_header(base, ev, s)`: the friend header source

1. Source descriptor null, or its tag not 0x4c414e13: result 0, nothing else.
2. `u3h_gate_closed(base, ev)` non-zero: result 1, nothing else.
3. e = `u3h_fr_find(base, address of image offset 0x17c3871)` (the selected player among the friends; null while
   busy). Then ea[86] is incremented. op = word 0. From here on the result is always 1.

**A. No such friend (e null): the own player.**

- Operation 1: the text answer becomes the address of the own name (image 0x17d6010).
- Operation 2, row (word 1) 0: the text answer becomes `u3h_text(base, s, 0xfd2de693)` (the title).
- Operation 2, row 1: the text answer becomes `u3h_text(base, s, id)` with id = 0x40d3b7a2 when the byte at image
  0x17dd584 is non-zero or the byte at image 0x17c3570 is non-zero, else 0x052ac89a.
- Anything else: nothing.

**B. The friend e.**

- Operation 1: the text answer becomes the address of the id text in the entry (e + 1).
- Operation 4 (images). First D = `u3h_fr_pdata(base, e)` and, only when D is not null, G = the net game record
  of D's index (4.8: one lookup of 0x3b9a067d, made for every operation 4 request with presence data, whatever
  the slot); G = none when D is null. Then by the slot (word 0x14):
  - 0xb81e2263: the image answer of kind 0 with value 0 (no avatar).
  - 0x0a071025: nothing when G is none; else the image answer of kind 0x24 with the value = the s32 at G +4 (the
    map id).
  - 0x57135906: nothing when G is none; else `u3h_set_icon_plain(base, ev, icon)` with icon = the u32 at +8 of
    G's rule set (4.8: one lookup of 0xf985c7b6; 0 from the empty rule set).
  - 0x8e39ed40: the state icon answer, `u3x_friend_icon(ev, u3x_friend_state_icon(base, e))`.
  - any other slot: nothing.
- Operation 2, row 0 (what the friend plays):
  - when the entry has presence data of this game and its state is 1, or else when `u3h_fr_in_title(e)` is
    non-zero (asked only when the first condition failed): the text answer becomes `u3h_text(base, s,
    0xfd2de693)`;
  - otherwise, state 0: nothing (no text for an offline friend);
  - otherwise: the text answer becomes `u3h_text(base, s, 0x953825d0)`.
- Operation 2, row 1 (what the friend does). Nothing when the entry has no presence data of this game or its state
  is not 1. Otherwise, with D = the presence data, in this order:
  1. The party suffix of 4.1 is built in a local 0x80-byte buffer for the party size D +5.
  2. Activity (D +4) 0 or 1: the text answer becomes the joined text (4.2) of `u3h_text(base, s, 0x84041d79)` and
     the suffix. Done.
  3. Activity 3: the same with text 0xea3bb70e. Done.
  4. Activity other than 2: nothing more.
  5. Activity 2 (in a match): G = the net game record of D's index (4.8); none: nothing more.
  6. The map: M = `u3x_sym_table(s, 0x0dec97a4)`; the first entry, in index order among the s32 count at M +0,
     whose map id (u32 at +0) equals the u32 at G +4. M null or no such entry: nothing more.
  7. The time text, a local 0x80-byte buffer that starts as an empty text. end = the s64 at D +0x20. Only when
     end is not 0: now = the "seconds now" function called with null (0 when its pointer is null); left = end -
     now (s64); v = 0 when left < 1, 216000 when left > 215999, else left; then game function 0xf33010 (address
     of image offset 0x17026b8, the buffer, 0x80, 0x93ac6446, v, 0).
  8. map name = `u3h_text(base, s, the u32 at map entry +4)`; then type name = `u3h_text(base, s, the u32 at +0
     of G's rule set)` (4.8: the lookup of 0xf985c7b6 comes between the two text calls).
  9. One rotating buffer is taken (`u3h_fr_buffer`, after step 8). It receives the bounded join of: `- `, the map
     name, ` / `, the type name, and then, by t = the game type of G (the u32 at G +0xc):
     - t is 4, 11 or 16 (co-op types): ` / ` and the text game function 0x741600 returns for the s32 at D +0x18
       (the difficulty; the result is used as it is, not tested for null);
     - t is 8, 9, 13, 17 or 18: nothing more;
     - every other t (also t above 18): ` / `, `u3h_text(base, s, the u32 at G +8)` (the rules' name), "\n- " and
       the time text of step 7.
  10. The text answer becomes that buffer.
  Calls of steps 8 and 9 happen in the order written: map name, rule set lookup, type name, buffer, then the
  difficulty call or the rules' name.
- Operation 2 with another row, and every other operation: nothing.

## 5. Behaviour, part 2

"Is a friend" for an id: the list is not busy (u64 at NF +0xc8 is 0) and `u3h_fr_find(base, id)` is not null.
While busy `u3h_fr_find` is not called and the id counts as "not a friend".

### 5.1 "Is in the players-met list" (file-private)

For an id: among the first "number" players-met entries (entries pointer NF +0x18, 0x28 bytes each), in index
order, `u3h_fr_strcmp(base, entry, id)` (this argument order) is asked until one returns 0. True at the first 0,
false when none does (no call when the number is 0).

### 5.2 `u3x_history_thread(base, s)`

1. title = `u3h_text(base, s, 0xfd2de693)`, fetched once, in every case (also with no new player).
2. For each of the new players (number = u32 at NF +0xec; ids of 0x24 bytes from the pointer at NF +0xf0), in
   index order: skipped when the id's first byte is 0; skipped when the id is a friend; otherwise
   `u3h_pm_add(base, id, title)`: a negative result increments ea[73], any other ea[72].
3. Then the u64 at NF +0xe0 becomes 0 and the u32 at NF +0xec becomes 0.

### 5.3 `u3x_players_met_add(base, s, names, n, flag)`

Nothing at all when n is 0.

**flag = 0, into the players-met list.** For each of the n ids, in order, with these tests in this order (the
first that applies skips the id):

1. the id's first byte is 0;
2. the id is a friend;
3. the list is full: number (u32 at NF +0x10) >= capacity (u32 at NF +0x14), unsigned;
4. the id is in the players-met list (5.1).

An id that passes is appended: its 0x24 bytes are copied to entry "number", that entry's u32 at +0x24 becomes 0,
and the number grows by one. (So an id that occurs twice in `names` is added once.) Nothing else is written; `s`
is not used.

**flag not 0, the new players of a match.**

1. Nothing when the history upload is busy (u64 at NF +0xe0 non-zero).
2. The number of new players (u32 at NF +0xec) becomes 0.
3. For each of the n ids, in order, with these tests in this order: first byte 0; the id is a friend; the
   new-players list is full (number at NF +0xec >= capacity at NF +0xe8, unsigned); the id is in the players-met
   list (5.1). An id that passes is appended to the new players: its 0x24 bytes are copied to slot "number"
   (pointer at NF +0xf0, 0x24 bytes per slot), the number grows by one and the byte at NF +0x20 becomes 1. The
   new-players list itself is not searched: an id that occurs twice in `names` is appended twice.
4. When the number of new players is now not 0: the u64 at NF +0xe0 becomes 1 and the history upload of 5.2 runs
   (it ends with both fields 0 again). When it is 0 nothing more happens (NF +0xe0 and +0x20 stay as they were).

### 5.4 `u3x_players_met_fetch(base, s)`

Nothing when the fetch is busy (u64 at NF +0xd0 non-zero) or the "history changed" byte (NF +0x20) is 0.
Otherwise, in this order:

1. the number of players met (NF +0x10) becomes 0; the u64 at NF +0xd0 becomes 1;
2. c = `u3h_pm_count(base)`; negative counts as 0; the number fetched is the smaller of c and the capacity (u32
   at NF +0x14);
3. for i = 0 .. that number - 1: an id buffer of 0x24 bytes, zeroed before each call, is filled by
   `u3h_pm_get(base, i, buffer)` (result ignored) and handed to 5.3 as one id with flag 0. Consequences of 5.3
   that must hold: friends, ids already fetched and an empty id (a failed read) are not listed;
4. the u64 at NF +0xd0 becomes 0, the byte at NF +0x20 becomes 0, ea[74] is incremented.

### 5.5 `u3x_match_players(base, s)`

1. Nothing when friends are off (bit 5 of ea[1]) or the entries pointer (NF +8) is null.
2. The ids of the remote players are collected, in the order of the player records 0 .. 9 of NGM (3.5): a record
   is left out when its address equals the pointer at NGM +0x370 or the pointer at NGM +0x378 (the local
   players); from every other record the 0x24 bytes at +0x5b8 are taken, used or not (an unused record gives an
   empty id, which 5.3 skips). When the byte at image 0x17d5c61 is 0 no record is taken at all (the list is
   empty). The list holds at most 10 ids.
3. Nothing more when the LAN byte (image 0x17d5c60) is non-zero: no call, no counter.
4. While busy (u64 at NF +0xc8 non-zero): `u3x_friend_list_thread(base)` first.
5. The list goes to 5.3 with flag 1 (`s` is handed on; an empty list does nothing there).
6. ea[75] is incremented (also for an empty list).

### 5.6 `u3x_friend_events(base)`

1. `u3h_take_rebuild()` is called first, in every case. When it returns non-zero and the list is not busy: count
   (u32 at NF +0) becomes 0 and the busy marker (u64 at NF +0xc8) becomes 1, so the list is fetched again (by
   step 2 of this call when an event waits, else by the caller's next tick). When it returns non-zero while busy
   the request is dropped.
2. Up to 64 times: an id buffer of 0x24 bytes is zeroed and `u3h_fr_event(base, buffer)` is asked.
   - negative: the function ends.
   - 0 or 1: while busy, `u3x_friend_list_thread(base)` runs first; then `u3x_friend_add(base, buffer, null)`;
     then ea[76] is incremented.
   - any other value: ignored (it still uses one of the 64 turns).

## 6. Test scenarios

Everything in this group can be tested old against new on a fake image with call-recording stubs; nothing needs
the live game, a socket or a file (the account service is behind `u3h_pm_*`, `u3h_fr_event`, `u3h_take_rebuild`).
Needed: a fake image with NF, NET, SES, NGM, the menu, the selected id; a state object whose lookup (pointer at
`s` + 656) is scripted with the four symbol tables and records its calls; recording stubs for game functions
0xf33010 (which writes a known text) and 0x741600, for the "seconds now" pointer and for every `u3h_` export of
2.3 (`u3h_fr_buffer` with four real buffers). The remembered selection and the noted-operations mask start at 0
for old and new alike; the order of the scenarios therefore matters for the log line. Only the wording of the
texts on the real pages is a live check.

Part 1:

1. Party suffix: size 0 and 1 (0x80 zero bytes from a 0xff-filled buffer, no call); size 2 and 255 (" - " and the
   stub's text behind it, arguments buf + 3, 0x7d, 0xba8db526, the size).
2. Join: two short texts; `a` of 0x120 characters (0xff characters, NUL at 0xff, `b` not copied); `a` of 0xfe
   characters and `b` of 5 (one character of `b`); both empty; five calls in a row (the fifth returns the first
   buffer).
3. Activity text: 0, 1, 2, 3, 4, 255.
4. Icon answer: every word of 3.3 from a 0xa5-filled request, word 3 and words 13 and up untouched; v negative.
5. State icon: in the session with state 0 (2); not in the session with state 0, 1, 2, -1 (0, 1, 1, 1).
6. Tab text: table missing (0, the menu page not asked); no row for the page; a row for the page with the other
   LAN value before the right one; two matching rows (the first wins); selected tab 0 and 2; 0 rows.
7. Friends list: descriptor null; another tag (both 0, nothing written, no counter); gate closed (1, no counter);
   another tab selected with op 6 (word 1 = -1) and with op 2 (nothing), no log line; first op 5 on the Friends
   tab (log line with 5, bit set, two lookups), second op 5 (no line, one lookup); op 40 (no line, nothing); op 1;
   op 5 after op 10 with word 1 = 7 and = -3; op 6 busy, count 0, count 1, count 9; op 8; op 9 (refresh called);
   ops 3 and 7 (nothing); op 2 / 4 while busy, row -1, row = count, entries null (nothing); op 4 with the slot and
   with another slot; column 0; column 2 with an empty and with a non-empty second name (also one of 0x100
   characters: cut at 0xff); column 3; column 1: offline, online elsewhere (Online), online elsewhere with the
   title match (Campaign), state 1 without presence data (Campaign), state 1 with data and activity 0 .. 3 with
   party sizes 1 and 4, activity 7 with party size 4 (Campaign stays, `u3h_text_num` still called), presence data
   with state 2 (no activity text).
8. Friend header: descriptor null / other tag / gate closed; no friend: op 1, op 2 rows 0, 1 (both lock bytes 0;
   only the first; only the second), row 2, op 4 (nothing); busy list (treated as no friend). Friend: op 1; op 4
   without presence data for each of the four slots (avatar cleared, the two net game slots untouched, state
   icon; no symbol lookup); with data: index -1, index = count, index 0x10002 (record 2), table missing; the map
   id picture; the game type icon with a valid type, with type = count (icon 0) and with the rule table missing
   (icon 0); an unknown slot (lookup of 0x3b9a067d still made). Op 2 row 0: data and state 1; no data and state 1
   (in-title stub says 1); state 2 with and without the title match; state 0. Row 1: no data; state 2; activity
   0, 1, 3 with party 1 and 3; activity 5 (nothing, suffix built); activity 2: no record; map table missing; map
   id not in the table; end 0 (no clock call, no 0xf33010 call, empty time text); end = now - 5 (v 0), now + 1,
   now + 215999, now + 216000 and now + 400000 (v 216000); clock pointer null; types 4, 11, 16 (difficulty text;
   argument = the s32 at D +0x18), 8, 9, 13, 17, 18 (short form), 0, 3, 12, 19 and 100 with a rule table of 20
   entries (rules' name and time line; for 100 the type name comes from text id 0); the rule table missing; a
   result longer than 0xff characters (cut); the order of all calls. Row 2 and op 9: nothing. (A negative game
   type is not a scenario: it addresses memory before the rule table.)

Part 2:

9. In the players-met list: number 0 (no compare); match in entry 0; in the last entry; none.
10. History: no new players (title fetched, both fields cleared, no add); three new players of which one is empty
    and one a friend (one add); the same while busy (the friend is added too, `u3h_fr_find` not called); an add
    that fails (ea[73]) and one that works (ea[72]).
11. Add, flag 0: n = 0 (nothing); a new id (copied, +0x24 cleared, number up); an empty id; a friend; a friend
    while busy (added); a full list (the friend test is still made before the capacity test); an id already
    listed; the same id twice in one call (added once); capacity 0.
12. Add, flag 1 (also flag 5 and -1): n = 0; history busy (nothing, NF +0xec not cleared); two new ids (both in
    the new list, NF +0x20 = 1, then the history runs and clears +0xe0 / +0xec; two adds); all ids skipped (+0xec
    = 0, +0xe0 and +0x20 unchanged, no history, no title fetch); an id already in the players-met list (skipped);
    the same id twice (two history adds); 17 ids with capacity 16 (16 taken); a friend.
13. Fetch: fetch busy; flag byte 0 (both: nothing); count -1 (list emptied, markers and flag cleared, ea[74]);
    count 3 with capacity 50 (three reads, three entries); count 60 with capacity 50 (50 reads); a read that
    leaves its buffer untouched (no entry); the same id twice and a friend's id (listed once / not at all).
14. Match players: friends off; list not set up (both nothing); the multiplayer byte 0 (online: empty list, ea[75]
    still incremented, NF untouched when not busy); LAN byte set (nothing, no counter, the list fetch not started); online with
    local pointers at records 0 and 3 (eight ids in record order, flag 1); busy (the list fetch runs first);
    both local pointers null (ten ids handed over; with a new-players capacity of 6 in the fake image six are
    taken).
15. Friend events: no rebuild, no event; rebuild while idle and no event (count 0, busy 1, list fetch not run);
    rebuild while busy (nothing changed); rebuild and one event (the list fetch runs before the add); events 0, 1
    (add with a null presence, ea[76]), 2 and 7 (ignored), then -1; 70 events waiting (64 taken); the id buffer
    handed to the add is the one the event filled.

## 7. Oddities of the old code that are behaviour to keep

1. Friends list: the selected tab is looked up a second time for the log line of a not yet noted operation; the
   log line is the helper's own diagnostic.
2. Friends list, status column: the Online / Offline text is fetched even when "Campaign" or an activity text
   replaces it; the party suffix is built before the activity is checked, so `u3h_text_num` runs for an unknown
   activity too. `u3h_fr_in_title` is true for every friend in state 1, so a friend in this game without usable
   presence data, or with an unknown activity, reads "Campaign".
3. Friends list: operation 10 stores any row number; operation 5 returns it unchecked (also when the list got
   shorter). Operation 6 answers 0 both for one friend and for none.
4. Tab text: the selected-tab index is not checked against the number of tabs, nor the tabs pointer for null.
5. Net game record: the index is cut to 16 bits; the records pointer is not tested. Rule set: no lower bound on
   the game type (a negative type addresses memory before the table); an unknown type silently gives text id 0
   and icon id 0.
6. Header, activity line: activities 0 and 1 both show the lobby text 0x84041d79 (the list shows 0xbd1d2c53 for
   1); the list shows text 0xcf292b57 for activity 2 where the header shows map and game type.
7. Header, images: the net game table is looked up before the slot is known. The map table's entries pointer is
   not tested.
8. Header, match line: for the co-op types the time text is built (clock and 0xf33010 called) and then not shown;
   for types 8, 9, 13, 17, 18 likewise. The time is clamped to 216000 seconds (60 hours).
9. Header without a friend also answers while the friend list is busy (`u3h_fr_find` returns null then), so the
   own name and lock state show for a moment after a rebuild.
10. History: the title is fetched even when there is nothing to add. While the list is busy friends are not
    recognized, so they can enter the history and the players-met list.
11. Add with flag 1 clears the new-players number before it knows whether anybody is new, and does not search the
    new-players list for duplicates; with flag 0 the friend test (a call into the C part) comes before the cheap
    capacity test.
12. Fetch: the result of `u3h_pm_get` is ignored (a failed read gives an empty id, which is skipped). The busy
    marker NF +0xd0 is set and cleared inside the same call.
13. Match players: the "in use" state of a player record is not looked at; empty ids are dropped later by the
    add. The multiplayer byte only empties the list, the counter still counts. The player list is built before
    the LAN test.
14. Friend events: a rebuild request that arrives while busy is consumed and dropped; an event of an unknown kind
    uses up one of the 64 turns.
15. The C wrappers `fr_party_suffix`, `fr_sprintf2`, `fr_activity_text`, `fr_pm_hist_thread`, `fr_pm_add` have no
    caller left in v7.212.

## 8. Strays, tables, and what stays as it is

Strays added to this group (in no work group of the inventory, or only in its "trivial" list; called by this
group's functions; old port):

- `u3x_friend_join` (4.2), `u3x_friend_activity` (4.3), `u3x_friend_state_icon` (4.5): exported, part 1;
- the file-private `game_rule` (the rule set, 4.8) and `net_game` of u3x_fixes.cpp line 7611 (the net game record,
  4.8). The inventory lists that `net_game` under G30 (it resembles G30's `pm_ngentry`), but it is file-private
  and its only callers are in `u3x_friend_header`: it is described here; G30 has its own `u3x_pm_ngentry`;
- the file-private `listed` (5.1), part 2;
- the group's statics: the remembered selection, the noted-operations mask, the empty rule set, the text
  constants (3.2).

Tables: the group holds no literal table of game values. The four script tables (tabs, net games, rule sets,
maps) are read from the running game through `u3x_sym_table` (3.5). Small literal id sets remain and are given as
rules, not as tables: the activity texts (four text ids, 4.3; a PS3-only mapping, the PS4 game has no table of
it) and the two game-type sets of the match line (4.9 step 9: {4, 11, 16} and {8, 9, 13, 17, 18}; branch
conditions of the PS3 function, no table in either game).

Stays as it is:

- every C wrapper and `u3h_` export named in section 1 and 2.3; the chain `fr_sources`, `fr_pm_source`,
  `fr_tick`, the hook kind 10 call; the C part's rotating buffers (`u3h_fr_buffer`) and friend storage;
- `u3x_sym_table`, `u3x_menu_page` (G33), `u3x_friend_list_thread`, `u3x_friend_add` (G18), and the shared
  file-private tools of the old file that other groups also use (the symbol lookup behind `u3x_sym_table`, the
  bounded append, the one-field "set the text answer" write): the new part writes its own small versions;
- nothing of this group has to stay in C or in assembly: there is no float arithmetic and no call that passes a
  float register.

## 9. Open questions

1. The exact wording of the four activity texts is not in the old source; the meanings in 3.5 follow G18's
   presence data (0 menus, 1 searching, 2 in a match, 3 match ended). An old comment at the mapping names them
   differently ("3 in a match, 2 in a party"). The ids are what matters.
2. Operation 8 of a list request is described as "is the list shown" from the neighbouring hidden source, which
   answers 0 to it; the old source does not name it. The answer (1) is all an implementation needs.
3. If the tester does not record `u3x_sym_table` lookups, the lookup counts of 4.7 steps 4 / 5 and 4.9 are not
   observable and could be relaxed; they are specified because the lookup is a game function.
4. The inventory assigns the file-private `net_game` (line 7611) to G30. It is covered here (4.8) because only
   the friend header calls it; G30's describer should leave it out.
5. The byte at image 0x17dd584 (SES +0x12ec) is called "the own party is locked" after the old comment; G18 reads
   the lock from bit 0 of SES +0x204. Both are kept as the old code has them.

## Clarifications

Facts confirmed by the tester's differential test (old v7.212 against the reimplementation), 2026-11-14. Items 1
to 3 are the only places where the text above and the old code differ; none of them can be reached with the
values the game holds (friend list capacity 100, players met 50, new players 16, a handful of tabs).

1. 4.7 step 6, operation 6: the count is taken as a signed 32-bit number. The answer is count - 1 computed as s32,
   and an answer below 0 becomes 0. So a count (u32) of 0x80000001 or more answers 0, not count - 1. Counts of
   1 .. 0x7fffffff answer count - 1 as written.
2. 4.6 step 3: the byte offset of the selected tab is "selected" x 0xc computed in 32 bits (it wraps at 2^32) and
   only then added to the tabs pointer; a row's offset is its index x 0x20, also in 32 bits. A selected index of
   0x15555556 therefore reads the tab at byte offset 8. The other index products of the old code are 32-bit as
   well (players-met entry x 0x28, new-player slot x 0x24, map entry x 0x98); they could only show with a list of
   more than 2^32 bytes.
3. 5.2 step 2: the number of new players (NF +0xec) is read again before every turn of the loop, and the ids
   pointer (NF +0xf0) is read once, before the title is fetched. This shows only if `u3h_text`, `u3h_fr_find` or
   `u3h_pm_add` changed those fields during the call; the C part's functions do not.
4. 4.9 step 7: when game function 0xf33010 is entered only byte 0 of the time buffer is defined (0); the function
   gets the buffer as an output. Zeroing the whole buffer first is equally right.
5. Bounded join (4): once the limit is reached by an earlier text, reading or not reading the first byte of a
   later text are both right (the old code reads it); nothing else of a later text is touched. All calls that
   fetch later texts are still made (4.9 steps 8 and 9 run in full when the map name alone fills the buffer).
6. 4.7 step 6, operations 3 and 7: nothing observable happens (the old code only calls the accessor `u3h_fr_ent`
   for a valid row).
7. Widths and signs, each confirmed by a scenario: the busy markers at NF +0xc8, +0xd0 and +0xe0 are tested as
   whole u64 values (0x100000000 counts as busy); the row's LAN value is compared as 32 bits (a row with LAN
   value 0x101 does not match the LAN byte 1); the lock bytes at image 0x17dd584 and 0x17c3570 count for any
   non-zero value; the game type of 4.9 step 9 is compared as a whole u32 (0x10004 is not a co-op type; with the
   rule set of 4.8 it is "not below the count" and gets the empty rule set); `u3h_fr_in_session` and
   `u3h_take_rebuild` count for any non-zero answer; `u3h_pm_add` fails only for a
   negative answer (0 and positive count as added).
8. 5.4 step 3: a read that reports a failure but did fill its buffer is listed like any other (the result is not
   looked at); only a buffer left empty is skipped.
9. 5.5: with multiplayer active and every remote id empty, the list of 8 to 10 empty ids still goes to 5.3 with
   flag 1, so the number of new players (NF +0xec) is reset to 0 (unless the history upload is busy) and ea[75]
   is incremented. With the multiplayer byte 0 the list has no id and NF +0xec stays as it was.
10. 5.3, flag 0: the u32 behind a new entry's id (+0x24) is cleared; the bytes of the id behind its NUL are copied
    as they are (all 0x24 bytes).
11. Open question 3 (section 9): the test records every call of the state's lookup function, so the lookup counts
    and their order in 4.6 to 4.9 are compared and hold as written: two lookups of 0xf1514340 for a Friends list
    request whose operation is in 0 .. 31 and not yet noted, one otherwise; one lookup of 0x3b9a067d for every
    header operation 4 with presence data, whatever the slot; 0xf985c7b6 between the map name and the type name.
    Not compared, as 2.3 says: the number of calls of `u3h_fr_pdata`, `u3h_fr_ent` and `u3h_ext`.
