# G37. Find Game record table (game type, map, net game records) and the matchmaking vote list

Behaviour description for a reimplementation (reimpl2 pipeline, describer's document). It says what the functions
do, not how the old ones were written. All offsets are hexadecimal when prefixed with 0x. "Image offset X" means the
address `base + X`, where `base` is the game image base. All multi-byte values are little-endian; "u32 / s32 / s16 /
u8" are unsigned / signed integers of that width, "pointer" is 8 bytes. "Word n" of a request means the 32-bit word
at byte offset 4·n. Names and conventions follow `G22_rank_xp.md` and `G23_boosters.md` (requests, image answers,
ea[n] / eb[n], `u3x_sym_table`), and `G19_friends_rows.md` (net game table, map table, rule sets).

This group has no float arithmetic. One float pattern (0x3f800000 = 1.0) is stored as a plain 32-bit word.

## 1. Purpose and callers

Two menu features of PS3 1.19 that the PS4 game (built from 1.10) lacks or has cut down, and two small pieces
they share with other lists.

**Part A. The record table of the Game Type / Map / Variation menus.** The game keeps a table "game type → its
maps → the net game records of that type and map", which its Custom Game rows read (and which the Find Game flow
reads to know the selected map). The PS4 game's own table (image 0x17d7b30) has room for 10 game types and one
byte per record number; 1.19 has 11 game types and more than 255 net game records, lists the types in another
order and by another LAN rule. The helper therefore keeps its own, larger table in its state block and the builder
points the game's readers at it. Part A builds that table and answers the lookups on it:

- `u3x_m04_refresh` builds the table (it replaces the game's own rebuild function 0x7503c0).
- `u3x_m04_info` fills one game type's part of it; `u3x_m04_map` finds a map entry by id.
- `u3x_m04_rec` reads one record number; `u3x_m04_cur_type`, `u3x_m04_cur_map` find the slot of a net game.
- `u3x_m04_select_var` selects another variation of the selected map (it replaces game function 0x750db0);
  `u3x_m04_var_value` tells which variation row is the selected one.

Callers of part A (all stay as they are): eight C one-line wrappers of the same names without `u3x_`
(`m04_map`, `m04_info`, `m04_refresh`, `m04_rec`, `m04_cur_type`, `m04_cur_map`, `m04_select_var`,
`m04_var_value`). They are used by the C function `m04_kind`, the answer of the builder's code stubs: stub kind 243
(the entry of 0x7503c0) calls the refresh; kind 256 (entry of 0x7508f0, "selected map slot") calls
`m04_cur_map(T, m04_cur_type(T, net game table, selected), selected)`; kind 257 (entry of 0x750db0) calls
`m04_select_var`; kind 258 (inside the variation list source) calls `m04_rec`; kinds 259 / 260 call
`m04_var_value` with `solo` 0 / 1. `m04_map` is also used by the C function `h09_layout` (map card). The stubs run on
the game thread that updates the menus.

**Part B. The matchmaking vote list.** On the matchmaking page the players vote between two candidate games. The
PS4 game kept the page and the list widget, but the widget's data source (source number 0x50) is an empty function.
Part B is the replacement:

- `u3x_retag_mm_vote` hands the widget to the helper when a menu page is loaded.
- `u3x_mmv_source` answers the widget's requests: header, the candidates' texts, the map picture, selection, row
  count and the value queries (votes and state).
- `u3x_mmv_coop`, `u3x_mmv_netgame`, `u3x_mmv_playlist`, `u3x_mmv_rule`, `u3x_mmv_diff`: the lookups the source
  needs (is the current playlist a co-op one; a net game record; a map entry; a rule set; a difficulty's name).
- `u3x_vote_query`: the two value queries the PS4 list control asks under its own ids.
- `u3x_difficulty_texts`: the text ids of the five co-op difficulty names.

Callers of part B (all stay): C one-line wrappers `retag_mm_vote` (called by the menu-load function
`prepare_lobby_menu` for every loaded page), `mmv_source` (called by `u3_profile_event` in its chain of
list-source hooks, and by the C source `m05_source`, which handles the same source tag first, after its own gate
test), `mmv_netgame` (also used by a C function of the difficulty vote), `mmv_playlist`, `mmv_rule`, `mmv_coop`,
`mmv_diff`. `u3x_vote_query` is called by the C function `n08_vq` (reached from `u3x_mmv_source` through
`u3h_n08_vq`). `u3x_difficulty_texts` is called by three C list sources of another group (`custom_load_source`,
`last_game_source`, `pause_info_source`) and by `u3x_mmv_diff`.

**Shared pieces.** `u3x_icon_answer`, the image answer most list sources give (called from many groups: boosters,
kickbacks, lobby rows, the players list), and the tinted variant the spoofed players list of G36 uses (a
file-private function `icon` today).

Everything runs on the game thread that updates and draws the menus. No locking, no timing rule.

**Not in this group** although the inventory lists it here: the file-private `rank_icon` of the party list. It is
covered by `G34_party_members.md` (4.8).

## 2. Interface

No function takes or returns a float. All are `extern "C"`. "existing" = exported today under this name; it keeps
name and signature (one exception, marked). The group is delivered in two independent parts; the only link is that
`u3x_mmv_playlist` and `u3x_m04_map` have the same behaviour (5.2), each part writes its own.

### Part A: the record table

| Function | Signature | Status | Called by |
|---|---|---|---|
| map entry by id | `uint8_t *u3x_m04_map(void *s, uint32_t id)` | existing | `u3x_m04_info`; C `m04_map` |
| one game type's maps | `void u3x_m04_info(void *s, uint8_t *info, uint32_t type, const uint8_t *rec, int32_t n, uint8_t *hi)` | existing | `u3x_m04_refresh`; C `m04_info` (no other user) |
| build the table | `void u3x_m04_refresh(uintptr_t base, void *s)` | existing | C `m04_refresh`; `u3x_m04_select_var` |
| record number | `int32_t u3x_m04_rec(const uint8_t *t, uint32_t k, uint32_t j, uint32_t i)` | existing | part A; C `m04_rec` |
| type slot of a net game | `uint32_t u3x_m04_cur_type(const uint8_t *t, const uint8_t *ng, int32_t cur)` | existing | part A; C `m04_cur_type` |
| map slot of a net game | `uint32_t u3x_m04_cur_map(const uint8_t *t, uint32_t k, int32_t cur)` | existing | part A; C `m04_cur_map` |
| select a variation | `void u3x_m04_select_var(uintptr_t base, void *s, int32_t v)` | existing | C `m04_select_var` |
| selected variation row | `uint32_t u3x_m04_var_value(uintptr_t base, int solo)` | existing | C `m04_var_value` |

Parameters: `base` image base; `s` the helper's state object (3.2); `id` a map id; `info` the address of one game
type slot of a table (3.3); `type` a game type number; `rec` the first net game record and `n` their number (3.5);
`hi` the address of that game type's 0x100 high bytes (3.3); `t` the address of a table (the helper's or any other
of the same layout); `k`, `j`, `i` game type slot, map slot, record slot; `ng` the net game table (3.5) or null;
`cur` a net game index; `v` a variation row; `solo` non-zero when the variation list has one extra row in front.

### Part B: the vote list and the shared pieces

| Function | Signature | Status | Called by |
|---|---|---|---|
| hand the widget over | `void u3x_retag_mm_vote(uint8_t *page)` | existing | C `retag_mm_vote` |
| net game record | `uint8_t *u3x_mmv_netgame(void *s, int16_t i)` | existing (stray) | part B; C `mmv_netgame` |
| map entry by id | `uint8_t *u3x_mmv_playlist(void *s, uint32_t id)` | existing (stray) | part B; C `mmv_playlist` |
| rule set | `const uint8_t *u3x_mmv_rule(void *s, int32_t i)` | existing (stray) | part B; C `mmv_rule` |
| co-op playlist test | `int u3x_mmv_coop(uintptr_t base, void *s)` | existing | part B; C `mmv_coop` |
| difficulty name | `const char *u3x_mmv_diff(uintptr_t base, void *s, uint32_t n)` | existing (stray) | part B; C `mmv_diff` |
| the list source | `int u3x_mmv_source(uintptr_t base, int32_t *ev, void *s)` | existing | C `mmv_source` |
| PS4 control's queries | `int u3x_vote_query(uint32_t query, int closed, int32_t my_vote, int32_t selected, int32_t winner, int32_t *answer)` | existing (stray) | C `n08_vq` |
| difficulty text ids | `const uint32_t *u3x_difficulty_texts(uintptr_t base)` | existing, **signature changed**: it gains the `base` parameter (it had none) | `u3x_mmv_diff`; three C callers, which must pass their `base` |
| image answer | `void u3x_icon_answer(int32_t *ev, int32_t kind, int32_t a, int32_t b, int32_t c, int32_t index)` | existing | many groups and C functions |
| tinted image answer | `void u3x_icon_tinted(int32_t *ev, int32_t kind, int32_t value, int32_t tint)` | new export (the file-private `icon` of the spoofed players list) | `u3x_m03_players` (G36) |

File-private pieces of part B, free in shape: the candidate's texts (5.7), the candidate's picture (5.8), the
value queries (5.9), the vote list's selected row (one s32 of the helper's own zero-initialised memory), an
all-zero block of 0x58 bytes in read-only memory (the "empty rule set"), and the texts of 3.9.

Results: `u3x_mmv_source` returns 1 when the request belongs to the vote list (tag match), else 0.
`u3x_mmv_coop` 1 or 0. `u3x_vote_query` 1 when it answered, else 0.

### Functions of the C part, of other groups and existing functions that the new code calls

Declare them; do not reimplement them.

| Name | Signature | What it does | Part |
|---|---|---|---|
| `u3h_ext` | `uint8_t *u3h_ext(void)` | the helper's state block (3.1). In CONVENTIONS.h, with `ext_eb()`. A plain address accessor: how often it is called is not part of the behaviour | A, B |
| `u3x_sym_table` | `uint8_t *u3x_sym_table(void *s, uint32_t id)` | data of a script symbol through the state's lookup function (the pointer at `s` + 656, called with the id): null when the function pointer, the handle it returns or the pointer the handle holds is null. Each call is one lookup, visible in a recording | A, B |
| `u3h_m04_lan` | `int u3h_m04_lan(uintptr_t base)` | non-zero when the game type lists follow the LAN rule: the LAN byte (image 0x17d5c60) is set and the helper's online lobby is not up | A |
| `u3h_m04_diag` | `volatile uint32_t *u3h_m04_diag(void)` | address of eight u32 diagnostic words of the C part, called m[0] .. m[7] below. A plain address accessor | A |
| `u3h_text` | `const char *u3h_text(uintptr_t base, void *s, uint32_t id)` | localized text of a text id through the game's text lookup; never null (empty text when there is none). Each call is one game text lookup | B |
| `u3h_native_gate_closed` | `int u3h_native_gate_closed(uintptr_t base, int32_t *ev)` | 0 while multiplayer is active (u8 at image 0x17d5c61 non-zero); otherwise stores the game's empty string (image 0x13ac250) as the request's text answer, counts, and returns 1 (the same C function as `u3h_gate_closed` of other specs) | B |
| `u3h_fr_tab` | `uint32_t u3h_fr_tab(uintptr_t base, void *s)` | the text id of the selected tab of the top menu page, 0 when unknown (G19 `u3x_page_tab_text`); it reads game data, so each call shows in a recording | B |
| `u3h_n06_on` | `int u3h_n06_on(void)` | non-zero while the helper's 1.19 matchmaking card is switched on (a switch word of the C part; no side effect) | B |
| `u3h_n06_mapimg` | `void u3h_n06_mapimg(uintptr_t base, void *s, int32_t *ev, uint8_t *map)` | makes the request the picture answer of a map entry (texture looked up by the picture name the entry points to at +0x10; a "no picture" answer when `map` is null or the texture is not loaded); counts | B |
| `u3h_h11_tag` | `void u3h_h11_tag(int32_t *ev)` | marks a picture answer as the matchmaking map card (it changes words 6 and 7 of a request whose word 2 is 2, unless switched off) | B |
| `u3h_n08_vq` | `int u3h_n08_vq(uintptr_t base, int32_t *ev)` | answers the two value queries of 5.10 for the request (through `u3x_vote_query`) when its switch is on, and counts; the result is not used here | B |

Game functions called directly: 3.7.

## 3. Game and helper facts

### 3.1 The helper's state block (address E from `u3h_ext()`)

| Where | Meaning |
|---|---|
| eb[2] bit 15 (value 0x8000) | set = the helper's record table is switched off: the game uses its own table; `u3x_m04_rec` then never adds a high byte |
| eb[170] | counter: vote list widgets handed over |
| eb[171] | counter: vote list requests that passed the gate |
| eb[172] | counter: vote list requests answered |
| eb[173] | the operation of the last vote list request answered |
| E + 0xed000 | the helper's record table "T" (3.3), 0x2200 bytes reserved |
| E + 0xef200 | the high bytes "H" of the record numbers: 11 × 32 × 8 bytes; the byte of game type slot k, map slot j, record slot i is at H + 0x100·k + 8·j + i |

eb[] is the u32 array at E + 0x4050 (`ext_eb()`). Counters are plain 32-bit increments (wrapping).

The diagnostic words from `u3h_m04_diag()` used here: m[0] table builds, m[1] game types of the last build, m[2]
maps listed (running total), m[5] record numbers of 256 or more listed by the last build, m[6] variations selected.
They are read from outside the game thread for diagnosis, so every change is a real read, add and store of the
word (the pointer is to volatile words). m[3], m[4] and m[7] belong to the C part.

### 3.2 The state object `s`

Only the script symbol lookup at offset 656 is used, through `u3x_sym_table`.

### 3.3 The record table

One layout for the helper's table T and the game's own (image 0x17d7b30, 10 game types, not touched by this
group).

| Offset | Type | Meaning |
|---|---|---|
| 0 | u8 | number of game type slots in use (0 = the table is empty / being built) |
| 8 + 0x308·k | 0x308 bytes | game type slot k (k = 0 .. 10 in the helper's table) |

A game type slot (address G):

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | the game type number |
| 4 | u8 | number of map slots in use (at most 32) |
| 8 + 0x18·j | 0x18 bytes | map slot j (j = 0 .. 31) |

A map slot (address M):

| Offset | Type | Meaning |
|---|---|---|
| 0 | pointer | the map's entry in the map table (3.5) |
| 8 | u8 | number of record slots in use (at most 8) |
| 9 + i | u8 | record slot i (i = 0 .. 7): the low 8 bits of a net game index |

Seen from the table address t: the map count of type slot k is the u8 at t + 0xc + 0x308·k; the record count of
map slot j is the u8 at t + 0x18 + 0x308·k + 0x18·j; record slot i is the u8 at t + 0x19 + 0x308·k + 0x18·j + i.

A net game index of the helper's table has 16 bits: the low 8 in the record slot, the high 8 in H (3.1). The
records of one map slot are the "variations" of that map for that game type (same type, same map, different
name).

### 3.4 Data-source requests and menu pages

A request is an array of 32-bit words (G23 3.3). Fields used here:

| Field | Meaning |
|---|---|
| word 0 | operation |
| word 1 | row index (in); the answer of operations 5, 6 and 8 (out); the row to select (operation 10, in) |
| words 2, 4 .. 12 | image answer (5.11) |
| word 0xe | the answer of a value query (operation 11) |
| word 0x13 | the id of the value asked (operation 11) |
| word 0x15 | the text slot ("column") asked (operation 2) |
| pointer at byte 0x40 | the text answer |
| pointer at byte 0x98 | the source descriptor; its u32 at +0x1c is the source tag |

"The text answer becomes X" means the pointer at byte 0x40 is set to X. The vote list's source tag is 0x4c414e50.

A loaded menu page (the argument of `u3x_retag_mm_vote`): u32 at +8 = the page id; pointer at +0x48 = its widget
list (may be null): u32 at +0 = number of widgets, pointer at +8 = the widgets, 0x68 bytes each. A widget: u32 at
+8 = widget id, u32 at +0x30 = widget type (21 = a list fed by a data source), u32 at +0x34 = data source number,
u32 at +0x40 = source tag (0 = none).

### 3.5 Script tables (through `u3x_sym_table`)

| Symbol | Layout |
|---|---|
| 0x3b9a067d, the net game table | +0 s32 number of records, +8 pointer to the records; a record is 0xd0 bytes: +4 u32 map id, +8 u32 text id of the record's own name (its variation / rules name), +0xc u32 game type |
| 0x0dec97a4, the map table | +0 s32 number of entries, +8 pointer to the entries; an entry is 0x98 bytes: +0 u32 map id, +4 u32 text id of the map's name, +8 u32 text id of its subtitle (0 = none), +0x10 pointer to its picture name (used by `u3h_n06_mapimg`), +0x94 u32 non-zero = the map is not listed |
| 0xf985c7b6, the rule sets | +0 s32 number of entries, +8 pointer to the entries; an entry is 0x58 bytes, indexed by game type: +0 u32 text id of the game type's name |
| 0xc1b0be0d, the playlists | +0 s32 number of entries, +8 pointer to the entries; an entry is 0x68 bytes: +0 u32 the playlist's key, +0x28 u32 non-zero = a co-op playlist |

The index of a record in the net game table is "the net game index".

### 3.6 Game data

| Image offset | Type | Meaning |
|---|---|---|
| 0x17d167c | s16 | the selected net game index (net game manager, image 0x17cb060, +0x661c) |
| 0x17d1aa8 | u8 | the co-op difficulty of the current game, 0 .. 4 (net game manager +0x6a48) |
| 0x17d5c60 | u8 | LAN byte: non-zero = LAN / not online |
| 0x17d6065 | u8 | the co-op byte of the net block: non-zero = the co-op side is chosen |
| 0x17d7760 | block "V" | the map vote: +0 u8 non-zero = a vote runs; +1 u8 non-zero = voting is closed; +4 the local player's vote (0 or 1 = the row voted for, 2 = none yet), read as u32 or s32 as said below; +8 and +0xc s32 the net game indexes of candidate 0 and 1; +0x10 and +0x14 s32 their vote counts |
| 0x17d7784 | block "W" | the co-op difficulty vote: +0 u8 non-zero = open; +1 u8 non-zero = closed (the difficulty is decided) |
| 0x1b676f0 | text buffer, 0x100 bytes | the static buffer the game's formatter writes (3.7) |

This group writes none of these.

### 3.7 Game functions called

Called with the game's normal calling convention through `game_call`.

| Image offset | Arguments | Result | Meaning | Part |
|---|---|---|---|---|
| 0x7511f0 | (pointer to three u32: a game type, a map id, the text id of a record's name) | none used | the game's "select net game by type, map and name": makes the matching record the selected net game | A |
| 0x73f630 | none | u32 | the key of the playlist the player has chosen (0 = none). It reads the menu user's profile and may reset a profile record in passing: call it exactly where section 5 says | B |
| 0x62aa60 | (const char *format, ...) variadic | pointer: the address of the static buffer (image 0x1b676f0) | printf-style formatting into the static buffer, at most 0xff characters. It is variadic: call it through a variadic function type so that no vector-register count is announced (al = 0), as G22 3.6 says | B |

### 3.8 Ids and numbers

| Value | Meaning |
|---|---|
| 0xe35f0f1a | page id of the matchmaking page |
| 0xdc6e89d8 | widget id of the vote list |
| 0x50 | the game's (empty) data source number of the vote list |
| 0xcf | the data source number of lists the helper answers |
| 0x4c414e50 | source tag of the vote list |
| 0xbd1d2c53 | text id of the tab on which the vote list is answered |
| 0x81cc5e93 | text id of the list's header while voting is open |
| 0x3cf3527a | text slot of the candidate's name with subtitle (the map card) |
| 0x22a276ec, 0x6f96651f | two record-name text ids that mean "no own name" |
| 0xbedebbb3, 0x88abe7af, 0xe87f9498, 0x7bc431fb | value queries of the list (5.9) |
| 0x7754e7af, 0x17809498 | the PS4 list control's two queries (5.10) |
| game types 8, 11, 17 | the game types of the co-op side on LAN |
| game type 16 | never listed in the record table |
| game types 10, 19 | Elimination and Classic: 10 is listed directly after 19 (4.3) |
| image answer kind 36 (0x24) | a map icon by map id |

### 3.9 Texts of the helper

Read-only NUL-terminated texts the new code holds itself (no game data): the formats `%s %s`, `%s - %s` and
`%s` + line feed (0x0a) + `%s`; a text of one space; an empty text.

### 3.10 Table of game values: the five difficulty names

The old code held the text ids of the five co-op difficulty names (difficulty 0 to 4, easiest first) as a literal
list. The new code must not contain them. The running PS4 game holds the same five ids, in the same order, inside
its own "name of a difficulty" function at image offset 0x741600 (the function G19 calls for the friends header):

- That function selects by the difficulty number through a jump table at image offset 0x7418c8: six s32 entries
  (difficulty 0 .. 5), each the distance from the table's own address to the code of that difficulty.
- For difficulty n = 0 .. 4 the code of the difficulty starts at C(n) = 0x7418c8 + entry n. At C(n) + 0x17 is the
  one-byte instruction code 0xBF, and the u32 at C(n) + 0x18 (unaligned) is the text id of difficulty n.
- For orientation and as a check: in the supported image the five u32 are at image offsets 0x741645, 0x741694,
  0x7416e3, 0x741732 and 0x74177e, each preceded by the byte 0xBF. (Entry 5 of the jump table is a sixth name the
  old list did not have; it is not used.)

The builder does not change these bytes (checked: no builder site lies in 0x741600 .. 0x7418e0, and the bytes are
the same in an unpatched and a patched image).

The game function itself is **not** called here: it looks the text up by another path when a debug setting is on,
and it knows a sixth difficulty, so calling it would not be the same behaviour as 5.4.

`u3x_difficulty_texts(base)` returns the address of five u32 in the helper's own writable memory, holding these
five ids in difficulty order, read from the image as said. Whether they are read on every call or only on the
first is not observable (the game code does not change); the returned address must stay valid and its content
unchanged between calls (the C callers and `u3x_last_game_third` of another group index it with 0 .. 4).

There is no other literal table of game values in this group. The record table of part A is not a table of the
source: it is built at run time from the game's script data (3.5), by the rules of 4.2 and 4.3.

## 4. Behaviour, part A

### 4.1 `u3x_m04_map(s, id)`

One lookup `u3x_sym_table(s, 0x0dec97a4)`. Null when the table is null or its entry pointer is null. Otherwise the
address of the first entry, in index order among the s32 count at +0, whose u32 at +0 equals `id`; null when there
is none (also when the count is 0 or negative).

### 4.2 `u3x_m04_info(s, info, type, rec, n, hi)`: the maps of one game type

Writes the game type slot at `info` and the high bytes at `hi` from the `n` net game records at `rec`.

1. The slot's type (u32 at +0) = `type`; its map count (u8 at +4) = 0.
2. The records are visited in index order, i = 0 .. n − 1 (nothing when n is 0 or less). A record whose game type
   differs from `type` is passed over. For a record of this type, with map id D:
   a. When a map slot already in use (index below the map count) points to a map entry whose map id is D, the
      record is passed over (each map is listed once). No lookup is made for it.
   b. Otherwise the map entry is looked up: `u3x_m04_map(s, D)` (one lookup per such record).
   c. When the entry exists and its "not listed" word (+0x94) is non-zero, the record is passed over.
   d. When the map count is already 32, the visit ends here (no further record is looked at).
   e. The map slot at index "map count" gets: pointer = the entry (also when it is null), record count = 0.
   f. When the entry is null, the record is passed over: the map count is **not** raised, so the slot just written
      stays outside the count (and is written again by the next map).
   g. Otherwise the slot's records are collected: all records are scanned in index order, r = 0 .. n − 1 but never
      past index 0xffff; every record with game type `type` and map id D, as long as the slot holds fewer than 8,
      is added: record slot c (c = the slot's record count) = the low 8 bits of r; the high byte `hi`[8 · map
      count + c] = bits 8 .. 15 of r; the record count goes up by one; and when r is 256 or more, m[5] goes up by
      one. (The ninth and later matching records are ignored.)
   h. Then the map count goes up by one and m[2] goes up by one.

Nothing else is written: record slots and high bytes beyond the counts keep their old content.

### 4.3 `u3x_m04_refresh(base, s)`: build the table

T = E + 0xed000, H = E + 0xef200. The helper's table is built whatever the switch eb[2] bit 15 says.

1. T's type count (u8 at +0) = 0; m[0] goes up by one.
2. N = `u3x_sym_table(s, 0x3b9a067d)`. Return when N is null, when its record pointer is null or when its record
   count n is 0 or less (the table then stays empty; m[1] and m[5] are not touched).
3. lan = `u3h_m04_lan(base)` is non-zero; coop = the co-op byte (image 0x17d6065) is non-zero.
4. The list of game types, at most 11: the records are visited in index order; for the game type g of a record:
   - when lan: g is passed over unless ("g is 8, 11 or 17") equals coop (the co-op side lists exactly these
     three, the competitive side every other type);
   - g = 16 is passed over;
   - a g already in the list is passed over;
   - when the list already holds 11 types the visit ends; otherwise g is appended.
5. Order: when the list holds both 10 and 19 and 10 stands more than one place after 19, 10 is taken out and put
   directly after 19 (the types between them move one place back; everything else keeps its order). In every
   other case the list stays as it is.
6. m[5] = 0.
7. For k = 0 .. (number of types − 1), in order: `u3x_m04_info(s, T + 8 + 0x308·k, type k, the records, n,
   H + 0x100·k)`.
8. T's type count = the number of types; m[1] = the number of types.

The type count is 0 while the slots are being written and gets its value last.

### 4.4 `u3x_m04_rec(t, k, j, i)`: a record number

The value is the u8 at t + 0x19 + 0x308·k + 0x18·j + i (the sum of the three products is formed in 32-bit
unsigned arithmetic; callers pass small numbers). When all of these hold: eb[2] bit 15 is clear, `t` is the
helper's table address (E + 0xed000), k < 11, j < 32 and i < 8, the byte at H + 0x100·k + 8·j + i is added as
bits 8 .. 15. The result is that number (0 .. 0xffff) as s32. No test of counts: a slot beyond the counts returns
whatever is stored there.

### 4.5 `u3x_m04_cur_type(t, ng, cur)`: the type slot of a net game

0 when `ng` is null, when the table's type count (u8 at t + 0) is 0, when ng's record pointer is null, or when
`cur` is negative or not below ng's record count. Otherwise g = the game type of record `cur`; the result is the
lowest slot index k below the type count whose type (u32 at t + 8 + 0x308·k) is g, and 0 when there is none. (0
therefore means "slot 0" as well as "not found". The type count is used as stored, not limited to 11.)

### 4.6 `u3x_m04_cur_map(t, k, cur)`: the map slot that lists a net game

The map slots j of type slot k are visited in order, j below the slot's map count and below 32; in each, the
record slots i in order, i below the map slot's record count and below 8. The result is the first j for which
`u3x_m04_rec(t, k, j, i)` equals `cur`; 0 when there is none (0 again means both).

### 4.7 `u3x_m04_select_var(base, s, v)`: select variation `v` of the selected map

Always works on the helper's table T.

1. N = `u3x_sym_table(s, 0x3b9a067d)`; return when null.
2. cur = the selected net game index (s16 at image 0x17d167c, sign-extended). Return when N's record pointer is
   null, cur is negative, or cur is not below N's record count n.
3. k = `u3x_m04_cur_type(T, N, cur)`; j = `u3x_m04_cur_map(T, k, cur)`.
4. Return when j is not below the map count of type slot k, when v is negative, when v is not below the record
   count of map slot (k, j), or when v is 8 or more.
5. x = `u3x_m04_rec(T, k, j, v)`; return when x is not below n.
6. Game function 0x7511f0 with the address of three consecutive u32: the game type of record cur, the map id of
   record cur, the name text id (+8) of record x.
7. m[6] goes up by one; then `u3x_m04_refresh(base, s)`.

### 4.8 `u3x_m04_var_value(base, solo)`: the selected variation's row

b = 1 when `solo` is non-zero, else 0. cur = the selected net game index (s16 at image 0x17d167c, sign-extended).
The helper's table T is searched completely: type slots k in order (below T's type count and below 11), map slots
j in order (below the type's map count and below 32), record slots i in order (below the map slot's record count
and below 8). The result is i + b for the first slot whose `u3x_m04_rec(T, k, j, i)` equals cur; b when there is
none. (It does not start from the selected game type: the first match in table order counts.)

## 5. Behaviour, part B

### 5.1 `u3x_retag_mm_vote(page)`

Nothing unless the page id (u32 at page + 8) is 0xe35f0f1a. Nothing when the widget list pointer (page + 0x48) is
null, when its widget pointer is null, or when its widget count is above 64. Otherwise every widget, in order,
whose id is 0xdc6e89d8, whose type is 21, whose data source number is 0x50 and whose source tag is 0 gets: data
source number = 0xcf, source tag = 0x4c414e50; and eb[170] goes up by one per such widget. `page` is never null.

### 5.2 `u3x_mmv_netgame`, `u3x_mmv_playlist`, `u3x_mmv_rule`

- `u3x_mmv_netgame(s, i)`: N = `u3x_sym_table(s, 0x3b9a067d)`. Null when N is null, when i is negative or when i
  is not below N's record count. Otherwise N's record pointer + 0xd0·i (the record pointer itself is not tested).
- `u3x_mmv_playlist(s, id)`: exactly the behaviour of `u3x_m04_map(s, id)` (4.1): one lookup of 0x0dec97a4, the
  first entry with that map id, or null.
- `u3x_mmv_rule(s, i)`: R = `u3x_sym_table(s, 0xf985c7b6)`. When R is null, i is negative, i is not below R's
  entry count, or R's entry pointer is null: the address of the helper's own all-zero block of 0x58 bytes (the
  "empty rule set"; never null). Otherwise the entry pointer + 0x58·i.

### 5.3 `u3x_mmv_coop(base, s)`: is the chosen playlist a co-op one

1. key = game function 0x73f630() (always called, and first).
2. P = `u3x_sym_table(s, 0xc1b0be0d)`; 0 when P is null or its entry pointer is null.
3. The entries are visited in index order (count = the s32 at P + 0). When the LAN byte (image 0x17d5c60) is
   non-zero, an entry whose co-op word (u32 at +0x28) differs from the value of the co-op byte (image 0x17d6065,
   taken as a number 0 .. 255) is passed over. The first remaining entry for which key is 0 or the entry's key
   equals key decides: the result is 1 when its co-op word is non-zero, else 0.
4. 0 when no entry decides.

### 5.4 `u3x_mmv_diff(base, s, n)` and `u3x_difficulty_texts(base)`

`u3x_difficulty_texts(base)`: see 3.10.

`u3x_mmv_diff(base, s, n)`: for n below 5, `u3h_text(base, s, the text id of difficulty n)` (3.10); for every other
n the helper's empty text, without any call.

### 5.5 `u3x_mmv_source(base, ev, s)`: the list source

1. Result 0, nothing else, when the request's source descriptor pointer is null or its tag is not 0x4c414e50.
2. `u3h_native_gate_closed(base, ev)` non-zero: result 1, nothing else.
3. eb[171] goes up by one.
4. Result 1, nothing more, when no vote runs (V's u8 at +0 is 0; `u3h_fr_tab` is then not asked) or when
   `u3h_fr_tab(base, s)` is not 0xbd1d2c53.
5. Result 1, nothing more, by the difficulty vote: when `u3h_n06_on()` is non-zero, if W is open and not closed
   (u8 +0 non-zero and u8 +1 zero); when it is zero, if W is neither open nor closed (both bytes zero).
6. eb[172] goes up by one; eb[173] = word 0 of the request.
7. The candidate of the request: candidate 1 when word 1 (the row) is non-zero, else candidate 0. "Its record" =
   `u3x_mmv_netgame(s, its net game index taken as s16)` (the low 16 bits of the s32 at V + 8 or V + 0xc,
   sign-extended).
8. By the operation (word 0):

| Operation | Effect |
|---|---|
| 1 (header) | the text answer becomes the helper's one-space text when voting is closed (V's u8 at +1 non-zero), else `u3h_text(base, s, 0x81cc5e93)` |
| 2 (row text) | G = the candidate's record; when G is not null: M = `u3x_mmv_playlist(s, G's map id)`; when M is not null: the candidate's texts (5.7). Otherwise nothing |
| 4 (row picture) | G = the candidate's record; when not null: the candidate's picture (5.8) |
| 5 (selected row?) | word 1 = the list's selected row (the helper's own s32, 0 at start) |
| 6, 8 | word 1 = 1 |
| 10 (select row) | the list's selected row = word 1 |
| 11 (value query) | 5.9 |
| any other | nothing |

9. Result 1.

For operations 5, 6, 8, 10, 11 and the others no record is looked up. For operation 11 the candidate (step 7) is
still taken from word 1.

### 5.6 Order of calls

Where several lookups and text calls happen in one request, their order is as written in 5.7 and 5.8 and is
observable (each `u3x_sym_table` lookup, each `u3h_text` call, 0x73f630 and 0x62aa60 are game calls).

### 5.7 The candidate's texts (operation 2; G = record, M = map entry, both non-null)

By the text slot (word 0x15):

- **0** (the map's name): the text answer becomes `u3h_text(base, s, M's name id)`.
- **3** (the rules line): first `u3x_mmv_coop(base, s)`.
  - Not co-op: R = `u3x_mmv_rule(s, G's game type taken as s32)`; the text answer becomes `u3h_text(base, s, the
    u32 at R + 0)`. Then, when G's own name id (+8) is neither 0x22a276ec nor 0x6f96651f, the text answer becomes
    `u3h_text(base, s, that name id)` instead. (The rules text is looked up in both cases.)
  - Co-op: R and the rules text as above (lookup, then `u3h_text`). When the difficulty vote is not closed (W's
    u8 at +1 is 0): the text answer becomes the result of game function 0x62aa60 with the format `%s %s`, the
    rules text and the helper's empty text (so the rules name followed by one space). Otherwise: D =
    `u3x_mmv_diff(base, s, the difficulty byte at image 0x17d1aa8)`, then the text answer becomes the result of
    0x62aa60 with the format `%s - %s`, the rules text and D.
- **0x3cf3527a** (name with subtitle): when M's subtitle id (+8) is 0, the text answer becomes
  `u3h_text(base, s, M's name id)`. Otherwise first the name `u3h_text(base, s, M's name id)`, then the subtitle
  `u3h_text(base, s, M's subtitle id)`, then the text answer becomes the result of 0x62aa60 with the format
  `%s` + line feed + `%s`, the name and the subtitle.
- **every other slot**: nothing (the text answer keeps what the request came with).

A text answer that is the static buffer is valid until the formatter is used again.

### 5.8 The candidate's picture (operation 4; G = record, non-null)

- `u3h_n06_on()` non-zero: M = `u3x_mmv_playlist(s, G's map id)` (may be null); then `u3h_n06_mapimg(base, s, ev,
  M)`; then `u3h_h11_tag(ev)`.
- Otherwise the request becomes the map icon answer: exactly the effect of `u3x_icon_answer(ev, 36, G's map id,
  0, 0, −1)` (5.11). No lookup.

(`u3h_n06_on` is thus asked once in step 5 of 5.5 and once more here.)

### 5.9 The value queries (operation 11)

By the query id (word 0x13); c = the candidate of 5.5 step 7; "closed" = V's u8 at +1 is non-zero:

| Query | Word 0xe becomes |
|---|---|
| 0xbedebbb3 | the vote count of candidate c (s32 at V + 0x10 or V + 0x14) when it is not 0; when it is 0: −1 if closed, else 0 |
| 0x88abe7af | −1 when not closed; when closed: 1 if the selected net game index (s16 at image 0x17d167c, sign-extended) differs from candidate 0's net game index (s32 at V + 8), else 0 |
| 0xe87f9498 | 0 when closed; else 1 if V's word at +4, taken as u32, is above 1, else 0 |
| 0x7bc431fb | V's word at +4 (s32) |
| any other | word 0xe is not written here; `u3h_n08_vq(base, ev)` is called (result unused) |

### 5.10 `u3x_vote_query(query, closed, my_vote, selected, winner, answer)`

Pure. The two queries the PS4 list control asks under its own ids:

- query 0x7754e7af ("which row won"): `*answer` = −1 when `closed` is 0; otherwise 0 when `selected` equals
  `winner`, else 1. Result 1.
- query 0x17809498 ("may the list take the focus"): `*answer` = 1 when `closed` is 0 and `my_vote` is below 0 or
  above 1; else 0. Result 1.
- any other query: result 0, `*answer` untouched.

### 5.11 `u3x_icon_answer` and `u3x_icon_tinted`

`u3x_icon_answer(ev, kind, a, b, c, index)`: word 2 = `kind`; words 4 and 5 = 0; word 6 = `a`; word 7 = `b`; word
8 = `c`; word 9 = `index`; word 10 = 0; word 11 = −1; word 12 = 0x3f800000. Word 3 and every other word stay as
they are.

`u3x_icon_tinted(ev, kind, value, tint)`: word 2 = `kind`; words 4 and 5 = 0; word 6 = `value`; words 7 and 8 = 0;
word 9 = −1; word 10 = 0; word 11 = `tint`; word 12 = 0x3f800000. Word 3 and every other word stay as they are.
(With `tint` = −1 this is `u3x_icon_answer(ev, kind, value, 0, 0, −1)`.) The order of the stores is not
observable.

## 6. Test scenarios

Everything in this group can be tested old against new without the live game, on a fake image (a memory block
addressed as `base`) with a fake state object whose lookup function (s + 656) returns handles to test tables, a
fake state block behind `u3h_ext`, an array behind `u3h_m04_diag`, recording stubs for the three game functions
(0x7511f0 records the three key words; 0x73f630 returns a set key; 0x62aa60 formats into the fake static buffer
and returns it) and recording stand-ins for the `u3h_` functions (`u3h_text` returns a recognisable text per id).
Compare memory (table T, H, the request, eb[], m[]), results, and the order and arguments of all calls. The live
game is needed only to see the menus (Custom Game: Game Type / Map / Variation rows; the vote list on the
matchmaking page).

Part A:

1. `u3x_m04_map`: table null; entry pointer null; count 0 and negative; id in the first, a middle, the last
   entry; id twice (first wins); id absent.
2. `u3x_m04_info`: n = 0 and negative; no record of the type; one map with 1, 8 and 9 records (the ninth
   ignored); the same map in records that are not adjacent (listed once, records collected over the whole table);
   a hidden map (skipped, lookup made for each of its records); a map id missing from the map table (slot written
   with a null pointer and count 0, map count not raised, the next map overwrites it; the last slot left behind
   when it is the last record); 32 maps and a 33rd (visit ends: no lookup after it); records with index 255, 256,
   257 and above (high bytes, m[5]); stale bytes beyond the counts stay; m[2] per map.
3. `u3x_m04_refresh`: net game table null / record pointer null / count 0 (T count 0, m[0] up, m[1] and m[5]
   unchanged, `u3h_m04_lan` not called); online (lan 0): every type but 16, in first-appearance order; lan with
   coop 0 and 1 (types 8, 11, 17 on one side, the rest on the other; type 10 is listed on the competitive side);
   12 distinct types (11 kept, visit ends); the order rule: 19 then others then 10 (10 moved behind 19), 10
   directly behind 19, 10 before 19, 10 without 19, 19 without 10 (all unchanged); m[5] reset and counted; the
   switch eb[2] bit 15 set (the table is built all the same).
4. `u3x_m04_rec`: the helper's table with the switch clear (16 bits) and set (8 bits); another table address (8
   bits); k = 11, j = 32, i = 8 on the helper's table (8 bits, read at the computed offset).
5. `u3x_m04_cur_type` / `u3x_m04_cur_map`: every early 0; found in slot 0, in a later slot; not found; a record
   number above 255 found through the high byte; counts above the limits (map count 40: only 32 looked at; record
   count 9: only 8).
6. `u3x_m04_select_var`: each return of steps 1, 2, 4 and 5 (no game call, no refresh, m[6] unchanged); a valid
   selection (the three key words; m[6]; the refresh after the game call); v = 0 and v = count − 1; the switch
   set (record numbers taken as 8 bits).
7. `u3x_m04_var_value`: selected game in the first type, in a later type, listed under two types (first in table
   order wins), absent; solo 0 and 1; empty table.

Part B:

8. `u3x_retag_mm_vote`: another page id; widget list null; widgets null; 65 widgets; widgets that differ in one
   of the four tests each (id, type, source number, tag already set); two matching widgets (eb[170] + 2).
9. `u3x_mmv_netgame`: table null; i = −1, 0, count − 1, count; record pointer null with i = 0 and i = 2.
   `u3x_mmv_playlist`: as scenario 1. `u3x_mmv_rule`: every "empty rule set" case (its u32 at +0 reads 0), a
   valid index.
10. `u3x_mmv_coop`: table null (0x73f630 still called first); key 0 online (first entry decides); key 0 on LAN
    with co-op byte 0 and 1 (first entry of that side); a key found on a co-op and on a competitive playlist; a
    key whose entry is passed over by the LAN rule; key absent; entry pointer null.
11. `u3x_mmv_diff`: n = 0 .. 4 (the five ids read from a fake image that holds the jump table and the five code
    places of 3.10), n = 5 and 0xffffffff (empty text, no call). `u3x_difficulty_texts`: the five words; the same
    address and content on a second call.
12. `u3x_mmv_source` gates: descriptor null; other tag; gate closed; no vote running (no `u3h_fr_tab` call, eb[171]
    counted); another tab; the four W states with `u3h_n06_on` 0 and non-zero.
13. Operation 1: voting open and closed. Operations 5, 6, 8, 10 (selection kept between requests), an unknown
    operation (only the counters).
14. Operation 2, rows 0 and 1: candidate index out of range (nothing); map id unknown (nothing); slot 0; slot 3
    not co-op with an own name, with each of the two "no own name" ids; slot 3 co-op with W closed 0 (trailing
    space) and non-zero (difficulty 0, 4 and 5, the last giving an empty second part); slot 0x3cf3527a with
    subtitle 0 and non-zero; another slot. Check the call order of 5.7 in each.
15. Operation 4: `u3h_n06_on` non-zero with a known and an unknown map id (the stand-ins receive the entry or
    null); zero (the nine words; word 3 untouched).
16. Operation 11: each query with voting open and closed; counts 0 and non-zero for both rows; own vote 0, 1, 2;
    selected index equal to and different from candidate 0; an unknown query (`u3h_n08_vq` called, word 0xe
    untouched by this function).
17. `u3x_vote_query`: both queries over closed 0 / 1, my_vote −1, 0, 1, 2, selected equal / different; another
    query. `u3x_icon_answer` / `u3x_icon_tinted`: a request filled with a pattern, every word compared.

Mutation ideas: the limits 8, 11, 32 and 0xffff; "more than one place after"; the three LAN co-op types; the test
of the map count before or after the hidden test; signed against unsigned reading of V + 4.

## 7. Behaviour to keep although it looks odd

1. 4.2 f: a map id that is not in the map table writes a map slot (null pointer, count 0) without counting it,
   and looks the id up again for every further record of that map.
2. 4.2 c before d: a hidden map is skipped even when the 32 slots are full, so the visit goes on past hidden maps
   and ends only at the first 33rd map that is not hidden.
3. 4.3: the helper's table is built even when the switch says the game uses its own table; 4.7 and 4.8 also
   always read the helper's table.
4. 4.3: the old notes say Elimination (10) is never listed on LAN; the code lists it on the competitive side
   like any other type. Only type 16 is never listed.
5. 4.5 / 4.6: "not found" and "slot 0" give the same result; 4.7 then tests the row against slot 0.
6. 4.8 searches the whole table, not only the selected game type.
7. 5.2: `u3x_mmv_netgame` does not test the record pointer.
8. 5.3: 0x73f630 is called before the table test; with key 0 the first entry (of the LAN side) decides.
9. 5.5: eb[171] is counted before the vote and tab tests; the gate function is asked here although the C caller
   `m05_source` has asked it for the same request just before (two gate calls per request on that path).
10. 5.7: the co-op rules line without a decided difficulty ends in a space; the not-co-op line looks the rules
    text up even when the record's own name replaces it.
11. 5.9: query 0xe87f9498 reads V + 4 unsigned, 0x7bc431fb signed.
12. The direct call of `mmv_source` in the hook chain of `u3_profile_event` comes after `m05_source`, which
    already answers every request with this tag; it stays.

## 8. Notes for the integrator, strays, open questions

Strays (functions of the old C++ file that are in no work group and are covered here): `u3x_mmv_netgame`,
`u3x_mmv_playlist`, `u3x_mmv_rule`, `u3x_mmv_diff`, `u3x_difficulty_texts` with its literal list
`difficulty_names`, `u3x_vote_query`, and the file-private `rules_text`, `candidate_picture`, `vote_query`,
`game_printf` (vote list) and `nmh`, `nmh_high`, `m04_on` (record table accessors).

Integrator:

1. `u3x_difficulty_texts` gains the parameter `base`: its C declaration and its three C callers (the lobby card
   source, the last-game line and the pause header; each has `base` at hand) change by one token. The describer
   of the group that owns those three C functions (G38) must use the new signature.
2. `u3x_icon_tinted` is a new export for G36's `u3x_m03_players`; if G36's implementation writes the nine words
   itself, the export is simply unused.
3. The private `rank_icon` of the party list is G34's (4.8), not delivered here.

Open questions:

1. 3.10 relies on game code bytes (the jump table at 0x7418c8 and five immediates in 0x741600). They are as
   stated in the supported image; the builder does not verify them. A builder check of the byte 0xBF before each
   of the five ids (image 0x741644, 0x741693, 0x7416e2, 0x741731, 0x74177d) would make a wrong image fail at
   build time instead of showing wrong difficulty names.
2. The meaning of V + 4 ("the local player's vote, 2 = none") is taken from the notes of the C part; only the
   tests of 5.9 matter for this group.

## 9. Clarifications (tester, 2026-11-14)

Facts confirmed by the differential test (`reimpl2\G37\test_g37.cpp`, old v7.212 against the new parts A and B).
Nothing in sections 1 to 7 was found wrong for any input a caller can pass; these items make precise what the text
leaves open and settle the open questions.

1. **3.10, the five difficulty text ids (open question 1).** With the real bytes 0x741600 .. 0x7418e0 of the
   supported PS4 image the rule gives exactly the five ids the old code held, in the same order, and the byte
   before each id is 0xBF. `u3x_mmv_diff` therefore passes the same id to `u3h_text` as before for n = 0 .. 4. On
   made-up code bytes the new function follows the rule as written (entry n of the jump table, + 0x18, an
   unaligned u32). A builder check of the five 0xBF bytes stays a matter for the integrator.
2. **Open question 2 (V + 4).** Only the tests of 5.9 exist: query 0xe87f9498 compares the word unsigned with 1
   (0xffffffff and 0x80000000 count as "above 1"), query 0x7bc431fb returns it unchanged as s32.
3. **4.4, the offset arithmetic.** In the old code the constant 0x19 is inside the 32-bit unsigned sum too (the
   whole offset 0x19 + 0x308·k + 0x18·j + i wraps at 2^32); the text has it outside. The two differ only when
   the three products add up to 0xffffffe7 or more. Likewise the old code addresses the counts of 4.5 to 4.8
   (0xc + 0x308·k, 0x18 + 0x308·k + 0x18·j, 8 + 0x308·k) with 32-bit unsigned wrap. No caller passes such
   numbers (k comes from a u8 count, j is below 32, i below 8), and either form reads far outside the table
   there, so this was not run.
4. **4.2 a.** The "already listed" test looks only at map slots below the map count; those never hold a null
   pointer (a slot with a null pointer is never counted). A slot's records are numbered in the order of the net
   game table; the scan of 4.2 g has no effect after the eighth record.
5. **4.2 g, the limit 0xffff.** A record with index 0x10000 or more is still visited by the outer visit of 4.2
   (its map gets a slot); it is only never stored as a record number. A map whose records all have an index of
   0x10000 or more is listed with a record count of 0.
6. **4.3.** On LAN a non-zero co-op byte of any value means the co-op side. The order rule moves type 10 also
   when it is the eleventh and last type of the list.
7. **4.7.** When the selected net game is not in the table at all (or the table's type count is 0), type slot 0
   and map slot 0 are taken and the game function is called with the game type and map of the selected record
   and the name of record "slot (0, 0, v)" (7.5). The record number x is 0 .. 0xffff, so only "x below n" is
   tested. The game function may change the selected net game; nothing reads it afterwards.
8. **5.3.** The co-op word (u32) is compared with the co-op byte as numbers: word 2 with byte 2 is the same
   side, word 1 with byte 2 is not. The result is "co-op word non-zero" of the deciding entry (word 2 gives 1).
9. **5.5 step 7.** Only the low 16 bits of a candidate's net game index pick the record (0x30002 is record 2;
   0x8000 is negative, no record). Query 0x88abe7af (5.9) however compares the selected index, sign-extended
   from 16 bits, with the full 32-bit word at V + 8: with 0x10002 there and record 2 selected the answer is 1.
10. **5.5, operation 10 / 5.** Operation 10 stores word 1 as it is (any value, also negative ones); operation 5
    returns the stored value. The row is kept between requests and is 0 before the first operation 10.
11. **5.8.** `u3h_n06_on` is asked a second time for a picture; that second answer alone chooses between the map
    card and the map icon.
12. **Read order, not observable.** The old code asks `u3h_n06_on` (5.5 step 5) before it reads the two bytes of
    W; reading them first gives the same result because `u3h_n06_on` has no side effect. In 5.7 (name with
    subtitle) the old code reads the subtitle id once for the test and once more after the name lookup; reading
    it once is the same because a text lookup does not change the map table. In 5.8 the old code reads the
    record's map id after the second `u3h_n06_on`; reading it before is the same for the same reason as the
    first. No other read of an input lies on
    the other side of a call (checked with one changed field per call: selected net game, LAN and co-op bytes,
    V, W, the difficulty byte, the request words, the script tables, eb[2], T's counts).
13. **Writes relative to calls.** Confirmed as written: T's type count is 0 during every lookup of a build and
    set last; m[0] is raised before the net game table is looked up; `u3h_m04_lan` is asked only after the table
    tests; m[6] is raised before the rebuild's first lookup; eb[171] is raised before `u3h_fr_tab`; eb[172] and
    eb[173] after the second gate (`u3h_n06_on`); the rules text is looked up after the playlist test of 5.3 and
    before the record's own name.
