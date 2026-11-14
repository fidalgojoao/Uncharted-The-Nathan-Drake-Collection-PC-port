# G38. Custom-game settings rows, map preview / pause / last-game info, time-limit row, EULA pager

Behaviour description for a reimplementation (reimpl2 pipeline, describer's document). It says what the functions
do, not how the old ones were written. All offsets are hexadecimal when prefixed with 0x. "Image offset X" means the
address `base + X`, where `base` is the game image base. All multi-byte values are little-endian; "u32 / s32 / u16 /
s16 / u8" are unsigned / signed integers of that width, "pointer" is 8 bytes. "Word n" of a request means the 32-bit
word at byte offset 4·n. Names and conventions follow `G23_boosters.md` (requests, "native k", text answer, image
answer), `G19_friends_rows.md` (net game table, rule sets, map table) and `G18_friends_list.md` / `G30_post_match.md`
("NGM" = the net game manager at image 0x17cb060).

Float: the Advanced Settings rows hold two values as 32-bit float patterns and convert them with the single
precision helpers of CONVENTIONS.h (`f_from_int`, `f_to_int`, `f_add`, `f_mul`); the old code did the same, so
there is no x87 code and no rounding question in this group (4.8 gives the operation order). One more float pattern
is stored as a plain word (4.3).

## 1. Purpose and callers

The PS4 game kept the menu pages of the multiplayer lobby but lost, or changed, several of the list sources PS3
1.19 has. This group is the helper's replacement for seven of them:

- **The custom game's map card** (source tag 0x4c414e31, on PAGE_LOBBY and the custom-game load page): preview
  picture of the chosen map, "map name / subtitle", game type name, co-op difficulty.
- **The pause header** (0x4c414ec2, PAGE_PAUSE_MULTIPLAYER): the same card for the map being played.
- **The "last game" title** (0x4c414e6e, the last-game results page): "map - game type - third part".
- **The Advanced Settings** (0x4c414f38 = the button on PAGE_LOBBY; 0x4c414fe7 .. 0x4c414fef without 0x4c414feb =
  the rows of the second custom-rules page): respawn time, health, weapon mode and five on / off rules of a custom
  game, their defaults, and the helper's block that holds them.
- **The Time Limit row** of Custom Game > Settings (0x4c414e2a): the game's own source runs, then the number of
  minutes is put in front of a label that has none; and, at menu load, the Friendly Fire row of that page gets the
  row style PS3 uses and the Time Limit row is pointed at this source.
- **The EULA pager** (0x4c414850): seven local pictures "eula/p1" .. "eula/p7", page by page, and the "EULA
  accepted" mark in the profile.
- One small shared answer: the test "is this game type a co-op type",
  without a table of game values in the helper (3.9).

Callers (all stay as they are):

- C `eu_source`, `pause_info_source`, `last_game_source`, `k_time_source`, `custom_load_source` and `adv_source`
  are called by the helper's event entry `u3_profile_event` for every list request, one after the other in its
  chain of list-source hooks, in this order (other groups' hooks in between); a result of 0 passes the request on
  to the next hook, a result of 1 ends the chain.
- C `k04_retag` is called by C `prepare_lobby_menu` once for every page record of the menu file when the menu data
  is prepared.
- C `custom_map` is called by `custom_load_source` and, through the C export `u3h_custom_map`, by
  `u3x_adv_source`.
- `u3x_adv_defaults` is called by `u3x_adv_source`, by the C wrapper `adv_defaults` (no caller left in C) and by
  `u3x_adv_tick` (existing one-line C++ function, stays as it is: every frame it first calls `u3h_cs_tick(base,
  s)`; then, unless the Advanced Settings are switched off (3.1), when MODE (3.5) is 2 and `u3x_sym_table(s,
  0x3b9a067d)` is not null, it calls `u3x_adv_defaults(base, s, the game type stored in the rules block)`).
- `u3x_adv_subst` is called only by `u3x_adv_source` (it has a C declaration but no C caller).

Everything runs on the game thread that updates the menus. No locking, no timing rule.

## 2. Interface

No function takes or returns a float. All are `extern "C"`. "existing" = exported today under this name, keeps
name and signature. "new" = replaces a C body; the C function of the old name stays as a one-line wrapper.

The group is delivered in two independent parts.

### Part A: map cards, last-game title, the co-op type test

| Function | Signature | Status | Called by | C wrapper passes |
|---|---|---|---|---|
| chosen map of the custom game | `uint8_t *u3x_custom_map(uintptr_t base, void *s, int32_t *gametype)` | new (C `custom_map`) | C `custom_map` (and through it `u3h_custom_map`); part A | its three arguments |
| map card source | `int u3x_custom_load_source(uintptr_t base, int32_t *ev, void *s)` | new (C `custom_load_source`) | C `custom_load_source` | base, request, state |
| pause header source | `int u3x_pause_info_source(uintptr_t base, int32_t *ev, void *s)` | new (C `pause_info_source`) | C `pause_info_source` | base, request, state |
| last-game title source | `int u3x_last_game_source(uintptr_t base, int32_t *ev, void *s)` | new (C `last_game_source`) | C `last_game_source` | base, request, state |
| co-op type test | `int u3x_type_is_coop(uintptr_t base, int32_t type)` | new, shared | parts A and B | - |

### Part B: Advanced Settings, Time Limit row, EULA pager

| Function | Signature | Status | Called by | C wrapper passes |
|---|---|---|---|---|
| defaults of the custom rules | `void u3x_adv_defaults(uintptr_t base, void *s, int32_t gt)` | existing | `u3x_adv_source`, `u3x_adv_tick`, C `adv_defaults` | - |
| text with a number put in | `void u3x_adv_subst(char *out, char *end, const char *text, int64_t value)` | existing | `u3x_adv_source` | - |
| Advanced Settings source | `int u3x_adv_source(uintptr_t base, int32_t *ev, void *s)` | existing | C `adv_source` | - |
| settings page preparation | `void u3x_k04_retag(uint8_t *page, uint32_t *counters)` | new (C `k04_retag`) | C `k04_retag` | the page record, the address of the C counter array `k04_d` |
| Time Limit source | `int u3x_k_time_source(uintptr_t base, int32_t *ev, void *s, uint32_t *counters)` | new (C `k_time_source`) | C `k_time_source` | base, request, state, the address of `k04_d` |
| EULA pager source | `int u3x_eu_source(uintptr_t base, int32_t *ev, void *s)` | new (C `eu_source`) | C `eu_source` | base, request, state |

Parameters: `base` = game image base; `s` = the helper's state object (3.2); `ev` = one data-source request (3.3);
`gametype` = where the game type number of the chosen game is stored (may be null); `gt` = a game type number;
`page` = one page record of the menu file (3.7); `counters` = the six u32 diagnostic counters of the settings page,
called k[0] .. k[5] (plain 32-bit increments; this group changes k[3], k[4], k[5] only; the other three belong to C
code of another group, which is why the array stays in C).

Results: every source returns 1 when the request carries its tag (it is then answered or deliberately left alone),
else 0. `u3x_custom_map` returns a map entry (3.6) or null.
`u3x_type_is_coop` returns 1 or 0.

Storage the new code owns (zero at start, private, nobody else reads it): in part B the EULA state = the current
page (s32) and the texture last answered (pointer), and a text buffer of 48 bytes for the Time Limit text. (They
replace the C variables `eu_cur`, `eu_tex` and `k04_buf`, which the integrator removes; the C variable `eu_row`
stays, other C code uses it.)

Functions of the C part, of other groups and existing exported C++ functions that the new code calls (declare them;
do not reimplement them):

| Name | Signature | What it does | Part |
|---|---|---|---|
| `u3h_ext` | `uint8_t *u3h_ext(void)` | the helper's own state block (3.1). In CONVENTIONS.h | A, B |
| `u3h_gate_closed` | `int u3h_gate_closed(uintptr_t base, int32_t *ev)` | 0 while multiplayer is active (u8 at image 0x17d5c61 non-zero); otherwise stores the game's empty string (image 0x13ac250) as the request's text answer, counts, and returns 1 | A, B |
| `u3h_text` | `const char *u3h_text(uintptr_t base, void *s, uint32_t id)` | localized text of a text id through the game's text lookup; never null (empty text when there is none) | A, B |
| `u3h_nmh` | `uint8_t *u3h_nmh(uintptr_t base)` | address of the custom-game menu table (3.6): the helper's own copy while its game-type list is on, else image 0x17d7b30 | A |
| `u3h_netgame` | `uint8_t *u3h_netgame(uintptr_t base, void *s)` | the net game record (3.4) at index NGM + 0x661c, or null (one symbol lookup) | A |
| `u3h_netgame_type` | `int32_t u3h_netgame_type(uintptr_t base, void *s)` | the game type of that record; 0 when there is none | A |
| `u3h_net_coop_mode` | `int u3h_net_coop_mode(uintptr_t base, void *s)` | non-zero while multiplayer is active and the current net game is of a co-op game type | A |
| `u3h_custom_map` | `uint8_t *u3h_custom_map(uintptr_t base, void *s, int32_t *gametype)` | C export that ends in `u3x_custom_map` (part A) | B |
| `u3x_map_entry_cur` | `uint8_t *u3x_map_entry_cur(uintptr_t base, void *s)` | G41: the map table entry (3.4) of the current net game's map, or null | A |
| `u3x_sym_table` | `uint8_t *u3x_sym_table(void *s, uint32_t id)` | data of a script symbol through the state's lookup function (offset 656); null when the function pointer, the handle or the pointer the handle holds is null | A, B |
| `u3x_image_answer` | `void u3x_image_answer(int32_t *ev, void *texture)` | makes the request an image answer: words 3 to 12 cleared, words 9 and 11 = −1, word 12 = 0x3f800000 (1.0); with a texture word 2 = 2 and the pointer stored at byte 0x10, without one word 2 = 0x10 (the "loading" picture) | A, B |
| `u3x_map_name_id` | `int u3x_map_name_id(const int32_t *maps, uint32_t map, uint32_t *text_id)` | search of a map table (3.4) given by its data pointer: 0 when `maps` or its entries pointer is null or no entry among the first min(count, 128) has map id `map`; otherwise `*text_id` = that entry's name text id and result 1 (first match in index order) | A |
| `u3x_difficulty_texts` | `const uint32_t *u3x_difficulty_texts(uintptr_t base)` | G37 (3.10 there; note the `base` parameter, new with G37): the address of five u32, the text ids of the names of co-op difficulty 0 .. 4, read from the running game. A pure accessor: how often it is called is not part of the behaviour | A |
| `u3x_last_game_third` | `int u3x_last_game_third(int32_t game_type, uint32_t subtitle, uint32_t difficulty, const uint32_t *difficulty_texts, uint32_t *text_id)` | the third part of the last-game title. Game type 4 or 11: result 2 when `difficulty` is 5 or more, else `*text_id` = `difficulty_texts[difficulty]` and result 1. Any other game type: result 0 when `subtitle` is 0, 0x22a276ec or 0x6f96651f, else `*text_id` = `subtitle` and result 1 | A |
| `u3x_join_dash` | `void u3x_join_dash(char *out, char *end, const char *a, const char *b, const char *c)` | writes the non-null ones of `a`, `b`, `c` in this order, with the three characters space, minus, space between two written parts (an empty part counts as written), never writing at or past `end`, then a NUL (at `end` at the latest) | A |
| `u3x_two_lines` | `void u3x_two_lines(char *out, char *end, const char *first, const char *second)` | writes `first`, one line feed character (0x0a), `second`, never writing text at or past `end`, then a NUL | A |
| `u3x_time_limit_text` | `void u3x_time_limit_text(char *out, char *end, const uint8_t *rule, int32_t count, const char *label)` | writes the decimal number V = (s32 at `rule` + 0x18) × `count` + (s32 at `rule` + 0x10), computed in 64 bits; then, when `label` is not empty, one space and `label`; never writing text at or past `end`; then a NUL | B |
| `u3x_has_digit` | `int u3x_has_digit(const char *text)` | 1 when the text holds one of the characters '0' .. '9', else 0 | B |
| `u3x_retag_widget` | `uint32_t u3x_retag_widget(uint8_t *page, uint32_t widget, uint32_t source, uint32_t tag, uint32_t max_items)` | on a page (3.7) with an item list of at most `max_items` items: every item with widget id `widget`, source kind 21, source number `source` and source parameter 0 gets source number 0xcf and source parameter `tag`; returns how many items it changed (0 without a list, without items or with more than `max_items`) | B |
| `u3x_pager_answer` | `int u3x_pager_answer(int32_t *ev, int32_t *current, int32_t pages)` | a picture pager's answers by operation (word 0): 5: word 1 = `*current`; 6: word 1 = `pages` − 1; 7: word 1 = 0; 10: `*current` = word 1 limited to 0 .. `pages` − 1; 23: `*current` = 0 when word 14 is non-zero; returns 1 for these five operations, 0 (nothing done) for any other | B |
| `u3x_pager_step` | `void u3x_pager_step(int32_t *current, int32_t pages, int step)` | `step` 3: one page back unless on page 0; `step` 4: one page on unless on the last page; other values: nothing | B |
| `u3x_party_guest` | `int u3x_party_guest(const uint8_t *party)` | 1 when the party object holds a party (u64 at +0x18 non-zero) that another member leads (u32 at +0x12e4 differs from u32 at +0x12e8), else 0 | B |

`u3h_ext` is a plain address accessor: how often it is called is not part of the behaviour.

## 3. Game and helper facts

### 3.1 The helper's state block (address from `u3h_ext()`)

| Offset | Type | Meaning |
|---|---|---|
| 0x2130 + 4·n | u32 | diagnostic words "sw[n]" (n = 0 .. 31); this group uses n = 0 .. 7 and 24 .. 31 (table below). Words 8 .. 23 belong to other groups |
| 0x31f0 | 32 bytes | the custom rules block (3.8) |
| 0x3a70 | u32 | function switches; bit 0 set = the Advanced Settings are switched off |
| 0x4050 + 4·n | u32 | switch words eb[n] (`ext_eb()`); eb[2] bit 9 (value 0x200) set = the settings page fixes are switched off |

sw words (plain 32-bit increments unless a value is stored):

| n | Meaning |
|---|---|
| 0 | pause header requests that passed the gate |
| 1 | pause header: map picture answers |
| 2 | pause header: map name texts |
| 3 | pause header: game type texts |
| 4 | pause header: difficulty texts |
| 5 | pause header: requests without a current map |
| 6 | last-game titles answered |
| 7 | last-game requests without a net game |
| 24 .. 27 | map card: the last four text slots asked that the card does not know (a ring, see 4.3) |
| 28 | map card: difficulty texts |
| 29 | map card: the difficulty value last used |
| 30 | map card: the game type last tested for co-op |
| 31 | map card: number of unknown text slots asked (also the ring position) |

### 3.2 The state object `s`

| Offset | Type | Meaning |
|---|---|---|
| 656 | pointer to function | script symbol lookup (used through `u3x_sym_table`) |
| 768 + 8·k | pointer to function | "native k": a game function the helper found at start. Any of them may be null |
| 0x2c80 | 251 characters | a text buffer shared with other lists; a text answer composed here stays valid until the next request |

Natives used here (always call through the pointer in the state object; the game function each one normally holds
is given for orientation only):

| k | Arguments | Result | Game function |
|---|---|---|---|
| 1 | (pointer to the custom-game menu table) | none: fills the table | 0x7503c0 |
| 13 | (u32 symbol id) | pointer to the symbol's data, or null | 0x11f2e30 |
| 20 | (address of image 0x1b31128, pointer to a NUL-terminated texture name) | a texture object or null; a miss queues the load, so a later call can succeed | 0x1189430 |
| 44 | (pointer to the custom-game menu table) | s32: the index of the map chosen in that table | 0x7508f0 |

### 3.3 Data-source requests

As G23 3.3. The fields used here:

| Field | Meaning |
|---|---|
| word 0 | operation |
| word 1 | in: the row / value index of operations 2 and 10; out: the answer of operations 5, 6, 7 and 8. For operation 2 of a card source it is the text slot |
| words 2 .. 12 | image answer (`u3x_image_answer`); word 12 is the draw scale, a float pattern |
| word 0xe | the answer of a value query (operation 11); the "reset" flag of operation 23 |
| word 0x13 | the id of the value asked (operation 11) |
| word 0x14 | the picture slot asked (operation 4) |
| word 0x15 | the text index asked (operation 2); only index 0 is answered here |
| pointer at byte 0x40 | the text answer |
| pointer at byte 0x98 | the source descriptor; its u32 at +8 is the text id of the row's own label, its u32 at +0x1c the source tag |

"Text answer = T" means: the pointer at byte 0x40 of the request becomes T. "Empty text" is a NUL-terminated text
of length 0 that the new code owns (which one is not part of the behaviour).

Operations of a value row (a row with a label and a value that steps left / right), as the Advanced Settings and
the EULA pager answer them: 1 = the label; 8 = can the row be used (word 1 = 1 yes, 0 no); 5 = the current index;
6 = the highest index; 7 = the lowest index; 10 = set the index (word 1 in); 2 = the text of index word 1; 4 = a
picture; 9 = the row is polled for input; 23 = the page is (re)opened.

Slot and query ids of the two cards: picture slot / text slot 0x16094dd2 = the map; text slot 0xa42c91c5 = the
game type; text slots 0xaceaf52d and 0xacebf52d = the co-op difficulty (both are accepted, see section 6);
value queries 0x17809498, 0xbedebbb3, 0x7754e7af, 0x7bc431fb are answered 0.

### 3.4 Script tables (read at run time; nothing is copied or kept)

As G19 3.4 / G27 3.4, with the fields this group adds:

- **Net game table**, symbol 0x3b9a067d: +0 number of records (s32; tested as u32 where section 4 says so), +8
  pointer to the records; a record is 0xd0 bytes: +4 u32 map id, +8 u32 text id of its subtitle (0 = none), +0xc
  s32 game type, +0x20 u32 = the respawn time of that game in seconds as a float pattern.
- **Rule sets**, symbol 0xf985c7b6: +0 number of entries, +8 pointer to the entries; an entry is 0x58 bytes and
  entry number = game type number: +0 u32 text id of the game type's name, +0x10 s32 time limit of setting 0 (in
  minutes), +0x18 s32 time limit step per setting (0 = the game type has no time limit).
- **Map table**, symbol 0x0dec97a4: +0 s32 number of entries, +8 pointer to the entries; an entry ("map entry") is
  0x98 bytes: +0 u32 map id, +4 u32 text id of the map's name, +8 u32 text id of its subtitle (0 = none), +0x10
  pointer to the NUL-terminated name of its preview texture (may be null).
- **Weapon modes**, symbol 0x2982116f: +0 u32 number of modes, +8 pointer to the modes; a mode is two u32: the mode
  value, then the text id of its name.

### 3.5 Game data

| Image offset | Type | Meaning |
|---|---|---|
| 0x17d5c61 | u8 | non-zero = multiplayer is active (read by `u3h_gate_closed`) |
| 0x17d603c | u32 | "MODE": the lobby's mode; 2 = a custom game is being set up |
| 0x17d167c | s16 | "SEL": the index of the selected net game (= NGM + 0x661c, the same word `u3h_netgame` reads) |
| 0x17d1aa8 | u8 | the co-op difficulty, 0 .. 4 (= NGM + 0x6a48; this group reads one byte) |
| 0x17dc298 | object | the party object (argument of `u3x_party_guest`) |
| 0x17dd610 | pointer | the profile objects (G23 3.5); the u32 at +0x778 of the first one is the EULA version the player accepted |
| 0x16e3188 | pointer | the screen menu object of screen 0 (may be null); its pointer at +0x898 is a pending dialog object (may be null); the EULA page step waits while that object's u32 at +0x10 is non-zero |
| 0x1b31128 | object | texture dictionary (first argument of native 20) |
| 0x721100 | u32 | the game's co-op game type mask (3.9 b); read only |

### 3.6 The custom-game menu table ("NetMenuHost")

At the address `u3h_nmh(base)` returns: u8 at +0 = number of games (0 = not filled yet); from +8 the games, 0x308
bytes each: s32 at +0 = game type number; from +8 of a game its maps, one every 0x18 bytes, each starting with a
pointer to a map entry (3.4).

### 3.7 Menu page records and items

A page record: u32 at +8 = page id; pointer at +0x48 = the item list (may be null): u32 at +0 = number of items,
pointer at +8 = the items (may be null). An item is 0x68 bytes: u32 at +8 = widget id; u32 at +0x10 = row style;
u32 at +0x28 = text id of the label; u32 at +0x30 = source kind; u32 at +0x34 = source number; u32 at +0x40 =
source parameter (the tag a source sees in its descriptor).

Page 0x86799eee = Custom Game > Settings. Label 0x9b7aa1a9 = its Friendly Fire row. Row style 13 is drawn by PS4
as another control than PS3 draws; style 6 is the plain value row.

### 3.8 The custom rules block (state block + 0x31f0, 32 bytes)

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | SEL + 1 at the time the defaults were last applied (0 = never) |
| 4 | u32 | respawn time in seconds, float pattern |
| 8 | u32 | health multiplier, float pattern (1.0 = 100 %) |
| 0xc | u32 | weapon mode value |
| 0x10 | u8 | flags 1: bit 1 = buddy spawn, bit 0 = sprint |
| 0x11 | u8 | flags 2: bit 7 = kickbacks, bit 6 = grenade throw-back, bit 5 = boosters |
| 0x14 | u32 | the game type the defaults were applied for |
| 0x18 | u32 | number of weapon modes last seen |
| 0x1c | u32 | counters: low 16 bits = requests answered, high 16 bits = values set |

Other groups read and write this block (the LAN party sends it to the members and a member stores what the host
sent; the match start applies it): the layout must stay.

### 3.9 Tables of game values: where the running game holds them

Two sets of game values were written out in the old code. The new code must not contain them.

**(a) The five difficulty names (text ids).** Covered by G37 (3.10 there): `u3x_difficulty_texts(base)` reads
them from the running game image. This group only calls it, with its `base`; it holds no list.

**(b) The co-op game types.** Checked on the bytes of the PS4 image the builder patches (the builder changes no
byte of game function 0x7210a0). The game tests "type T is a co-op type" inline in about sixty places, always the
same way: D = T − 4 as u32; co-op when D ≤ 14 and bit D of a 32-bit mask is set. It holds the mask as the
immediate of each such test; the copy the new code reads is the unaligned u32 at image offset 0x721100 (in game
function 0x7210a0). The set of types is the one G30 3.11 names.

Not tables: the single ids of 3.3 and section 4 (tags, slots, text ids), the game types named in 4.8 for hidden
rows, and the number of EULA pictures (seven local files the port ships).

### 3.10 Game functions called directly

| Image offset | Call | Meaning |
|---|---|---|
| 0x753e40 | (s32 *request) | the game's own Time Limit source. Called without a code check. For operation 2, text index 0, in custom-game mode with a time limit it composes the label in a game buffer and makes it the text answer; in the PS4 texts the label carries no number |
| 0x6a8010 | () → pointer or null | the EULA entry that applies to this player; its u32 at +0xc is the current EULA version. Called without a code check |
| 0x658050 | (pointer menu, int, int) → int | the menu's button input of this frame, called with (menu, 1, 0); 3 = "previous", 4 = "next" here. Called only when the first 12 bytes of its code are 53 48 8D 0D 38 CA 15 01 8B 99 7C 08 (as G25 3.10) |

## 4. Behaviour, part A

### 4.1 `u3x_type_is_coop` and "the difficulty text"

`u3x_type_is_coop(base, type)`: D = `type` − 4 as u32. Result 1 when D ≤ 14 and bit D of the u32 at image 0x721100
is set, else 0. No other effect.

"The difficulty text of d" (used in 4.3, 4.4): the empty text when d is 5 or more; otherwise
`u3h_text(base, s, u3x_difficulty_texts(base)[d])`: exactly one `u3h_text` call.

### 4.2 `u3x_custom_map`

The map entry of the map chosen for the custom game, and the game type of the chosen game. In this order:

1. Null when MODE is not 2.
2. Null when `u3x_sym_table(s, 0x3b9a067d)` is null. (The menu table must never be refreshed before the net game
   table is loaded.)
3. N = `u3h_nmh(base)`; n = the u8 at N + 0. When n is 0: native 1, when it is not null, is called with (N), and n
   is read again; null when n is still 0.
4. T = `u3x_sym_table(s, 0x3b9a067d)` again (a second lookup). The game number g starts as 0. When T is not null,
   SEL ≥ 0 and SEL (as u32) is below the u32 at T + 0: type = the u32 at (pointer at T + 8) + SEL · 0xd0 + 0xc, and
   g becomes the lowest i in 0 .. n − 1 whose game (N + 8 + i · 0x308) has that u32 at +0; it stays 0 when no game
   has it. (The records pointer is not tested.)
5. m = 0 when native 44 is null, else native 44(N) as s32; a negative m becomes 0. (No upper limit.)
6. G = N + 8 + g · 0x308. When `gametype` is not null: `*gametype` = the s32 at G + 0.
7. Result = the pointer at G + 8 + m · 0x18 (it may be null; `*gametype` is then already written).

In the cases 1 to 3 `*gametype` is not written.

### 4.3 `u3x_custom_load_source` (tag 0x4c414e31)

1. Result 0 when the descriptor pointer is null or its tag is not 0x4c414e31. Nothing else happens.
2. `u3h_gate_closed(base, ev)` non-zero: result 1.
3. MODE not 2: text answer = empty text, word 1 = 0, result 1 (for every operation).
4. gt starts as 0; M = `u3x_custom_map(base, s, &gt)`. M null: result 1, nothing written.
5. By operation (word 0); every case ends with result 1, as does any other operation:
   - **4**, only for picture slot 0x16094dd2: name = the pointer at M + 0x10; texture = native 20(image 0x1b31128,
     name) when native 20 and the name are both not null, else null; `u3x_image_answer(ev, texture)`; without a
     texture word 12 then becomes 0x40100000 (2.25: the "loading" picture is drawn larger on this card).
   - **2**, only for text index 0 (word 0x15), by text slot (word 1):
     - 0x16094dd2: when the u32 at M + 8 is 0: text answer = `u3h_text` of the u32 at M + 4. Otherwise the buffer
       at `s` + 0x2c80 gets: `u3h_text` of the u32 at M + 4, one backslash character (0x5c, the menu text's line
       break), `u3h_text` of the u32 at M + 8, then a NUL; at most 250 characters are written in all (what does
       not fit is cut); text answer = the buffer. The name is looked up before the subtitle.
     - 0xa42c91c5: R = `u3x_sym_table(s, 0xf985c7b6)`; when R is not null, gt ≥ 0 and gt (as u32) is below the
       u32 at R + 0: text answer = `u3h_text` of the u32 at (pointer at R + 8) + gt · 0x58. Otherwise nothing.
     - 0xacebf52d or 0xaceaf52d: sw[30] = gt. Nothing more when `u3x_type_is_coop(base, gt)` is 0 or gt is 0x12.
       Otherwise d = the u8 at image 0x17d1aa8; sw[29] = d; text answer = the difficulty text of d (4.1);
       sw[28] + 1.
     - any other slot: sw[24 + (sw[31] AND 3)] = the slot, then sw[31] + 1.
   - **11**: when word 0x13 is one of 0x17809498, 0xbedebbb3, 0x7754e7af, 0x7bc431fb: word 0xe = 0.

### 4.4 `u3x_pause_info_source` (tag 0x4c414ec2)

1. Result 0 when the descriptor pointer is null or its tag is not 0x4c414ec2.
2. `u3h_gate_closed(base, ev)` non-zero: result 1.
3. sw[0] + 1.
4. gt = `u3h_netgame_type(base, s)`; then M = `u3x_map_entry_cur(base, s)`. M null: sw[5] + 1, result 1.
5. By operation; result 1 in every case:
   - **4**, only for picture slot 0x16094dd2: texture as in 4.3 (name at M + 0x10, native 20);
     `u3x_image_answer(ev, texture)`; sw[1] + 1. (Word 12 keeps the 1.0 of the image answer here.)
   - **11**: when word 0x13 is 0x17809498: word 0xe = 0.
   - **2**, only for text index 0, by text slot (word 1):
     - 0xacebf52d or 0xaceaf52d: nothing when `u3h_net_coop_mode(base, s)` is 0 or gt is 0x12. Otherwise text
       answer = the difficulty text (4.1) of the u8 at image 0x17d1aa8; sw[4] + 1.
     - 0x16094dd2: when the u32 at M + 8 is 0: text answer = `u3h_text` of the u32 at M + 4. Otherwise
       `u3x_two_lines(buffer, buffer + 250, u3h_text of the u32 at M + 4, u3h_text of the u32 at M + 8)` with the
       buffer at `s` + 0x2c80, and text answer = the buffer. In both cases sw[2] + 1. (Here the two lines are
       separated by a line feed, on the map card by a backslash: both as they are today.)
     - 0xa42c91c5: R = `u3x_sym_table(s, 0xf985c7b6)`; when R is not null, gt ≥ 0 and gt (as u32) is below the
       u32 at R + 0: text answer = `u3h_text` of the u32 at (pointer at R + 8) + gt · 0x58, and sw[3] + 1.

### 4.5 `u3x_last_game_source` (tag 0x4c414e6e)

1. Result 0 when the descriptor pointer is null or its tag is not 0x4c414e6e.
2. `u3h_gate_closed(base, ev)` non-zero: result 1.
3. Any operation other than 1: result 1, nothing done.
4. G = `u3h_netgame(base, s)`. Null: sw[7] + 1, result 1.
5. From G: map id (u32 at +4), subtitle id (u32 at +8), game type gt (s32 at +0xc). Three parts, looked up in this
   order:
   - **Map**: table = null when native 13 is null, else native 13(0x0dec97a4). When `u3x_map_name_id(table, map
     id, &id)` is 1 the part is `u3h_text` of that id; otherwise there is no map part.
   - **Game type**: R = `u3x_sym_table(s, 0xf985c7b6)`; when R is not null, gt ≥ 0 and gt (as u32) is below the
     u32 at R + 0: `u3h_text` of the u32 at (pointer at R + 8) + gt · 0x58. Otherwise the part is the empty text
     (it is still a part).
   - **Third**: k = `u3x_last_game_third(gt, subtitle id, the u8 at image 0x17d1aa8, u3x_difficulty_texts(base),
     &id)`. k = 0: no third part. k = 1: `u3h_text` of that id. k = 2: the empty text (still a part).
6. `u3x_join_dash(buffer, buffer + 0x7f, map part or null, game type part, third part or null)` with the buffer at
   `s` + 0x2c80; text answer = the buffer; sw[6] + 1; result 1.

## 5. Behaviour, part B

### 5.1 `u3x_adv_defaults`

Nothing at all when the u32 at block + 0 (3.8) equals SEL + 1 (SEL sign-extended, the sum taken as u32): the
defaults were already applied for this selection. Otherwise:

1. T = `u3x_sym_table(s, 0x3b9a067d)`. Respawn pattern = 0x41200000 (10.0); when T is not null, SEL ≥ 0 and SEL
   (as u32) is below the u32 at T + 0: the u32 at (pointer at T + 8) + SEL · 0xd0 + 0x20 instead (copied as a
   pattern, not converted).
2. Written: respawn (+4) = that pattern; health (+8) = 0x3f800000 (1.0); weapon mode (+0xc) = 0; flags 1 (+0x10)
   = 3; flags 2 (+0x11) = 0xe0; game type (+0x14) = `gt`; then +0 = SEL + 1. The bytes at +0x12, +0x13 and the
   words at +0x18 and +0x1c are not touched.

### 5.2 `u3x_adv_subst`

Copies `text` to `out`, replacing every occurrence of the three characters `[A]` by the decimal form of `value`
(a minus sign first when negative; no separators, no padding). Nothing is written at or past `end`: copying stops
when the text ends or the write position reaches `end`, and a number is cut where it reaches `end`. A NUL is then
written at the write position (which can be `end` itself: the buffer must hold one byte more).

### 5.3 `u3x_adv_source` (tags 0x4c414f38 and 0x4c414fe7 .. 0x4c414fef without 0x4c414feb)

"Row number" r = the low 8 bits of the tag: 0x38 the lobby's Advanced Settings button, 0xe7 respawn time, 0xe8
buddy spawn, 0xe9 health, 0xea sprint, 0xec weapon mode, 0xed kickbacks, 0xee grenade throw-back, 0xef boosters.
"Count a set" = add 0x10000 to the counters word (block + 0x1c). B = the buffer at `s` + 0x2c80.

1. Result 0 when the descriptor pointer is null, when the upper 24 bits of its tag are not 0x4c414f, or when r is
   none of the nine row numbers. Result 0 also when the Advanced Settings are switched off (3.1). Nothing else
   happens in these cases.
2. The low 16 bits of the counters word go up by 1 (wrapping inside the 16 bits; the high 16 bits stay).
3. **A party guest gets no button**: when `u3x_party_guest(image 0x17dc298)` is non-zero and r is 0x38: operation
   1: text answer = empty text; operation 8: word 1 = 0; result 1 (other operations: nothing).
4. custom = MODE is 2. gt = −1. When custom: `u3h_custom_map(base, s, &gt)` (its result is not used), then
   `u3x_adv_defaults(base, s, gt)`.
5. label = `u3h_text(base, s, the u32 at descriptor + 8)` (looked up on every request that comes this far).
6. **The button** (r = 0x38). Not custom: text answer = empty text and word 1 = 0, for every operation. Custom:
   operation 1: text answer = empty text when `u3x_type_is_coop(base, gt)` is 1, else the label; operation 8:
   word 1 = 1. Result 1.
7. Respawn (0xe7) and health (0xe9) outside custom mode: text answer = empty text and word 1 = 0, for every
   operation; result 1. (The other rows go on with gt = −1.)
8. Operation 1 (all remaining rows): text answer = empty text when the row is hidden for the game type, else the
   label. Hidden: respawn for gt 10; buddy spawn for gt 10 and 14; kickbacks and boosters for gt 19. Result 1.
9. Operation 8: word 1 = 1. Result 1.
10. **On / off rows** (0xe8 = flags 1 bit 1, 0xea = flags 1 bit 0, 0xed = flags 2 bit 7, 0xee = flags 2 bit 6,
    0xef = flags 2 bit 5), result 1 after each:
    - 5: word 1 = the bit (0 or 1). 6: word 1 = 1.
    - 10: the bit is set when word 1 is exactly 1, cleared for every other value; count a set.
    - 2 with text index 0: text answer = `u3h_text(base, s, 0xd1831446)` ("on") when word 1 is exactly 1, else
      `u3h_text(base, s, 0x32066ec9)` ("off").
    - other operations: nothing.
11. **Respawn time** (0xe7): index i stands for 2·i + 3 seconds; 29 values. Result 1 after each:
    - 5: word 1 = (`f_to_int`(respawn pattern) − 3) / 2, an integer division that truncates towards zero.
    - 6: word 1 = 28.
    - 10: respawn pattern = `f_from_int`(word 1 · 2 + 3); count a set. (Word 1 is not range-checked.)
    - 2 with text index 0: `u3x_adv_subst(B, B + 120, u3h_text(base, s, 0x61275961), word 1 · 2 + 3)`; text
      answer = B.
12. **Health** (0xe9): index i stands for 20·i + 20 percent; 20 values. Result 1 after each:
    - 5: word 1 = (`f_to_int`(`f_mul`(health pattern, 0x42c80000 = 100.0)) − 10) / 20, truncating towards zero.
    - 6: word 1 = 19.
    - 10: health pattern = `f_mul`(`f_add`(`f_from_int`(word 1 · 20 + 20), 0x3e99999a = 0.3), 0x3c23d70a = 0.01):
      the percent value, plus 0.3, times 0.01, each step rounded to single precision; count a set.
    - 2 with text index 0: B = the decimal form of word 1 · 20 + 20 (minus sign when negative; at most 120
      characters, which a 32-bit number never reaches), NUL-terminated; text answer = B.
13. **Weapon mode** (0xec): W = `u3x_sym_table(s, 0x2982116f)`. W null: result 1. n = the u32 at W + 0; the u32
    at block + 0x18 becomes n; E = the pointer at W + 8; E null: result 1. Then, result 1 after each:
    - 5: word 1 = the index of the last mode whose value equals the block's weapon mode; 0 when none does.
    - 6: word 1 = n − 1.
    - 10: when word 1 (as u32) is below n: weapon mode = the value of mode word 1; count a set.
    - 2 with text index 0: when word 1 (as u32) is below n: text answer = `u3h_text` of the name id of mode
      word 1.

All integer expressions on word 1 (· 2 + 3, · 20 + 20, − 3, − 10) are 32-bit two's-complement and wrap; `f_to_int`
gives 0x80000000 for a NaN or out-of-range pattern, and the subtraction then wraps as well. With the values the
menu sends (indices inside the ranges of operation 6) nothing wraps.

### 5.4 `u3x_k04_retag`

Nothing when eb[2] bit 9 is set or the page id (u32 at `page` + 8) is not 0x86799eee. Nothing when the item list
pointer is null, the items pointer is null or the number of items is above 32. Otherwise:

1. Every item whose row style is 13 and whose label is 0x9b7aa1a9 gets row style 6; k[3] + 1 for each.
2. k[4] += `u3x_retag_widget(page, 0, 0x2a, 0x4c414e2a, 32)` (the Time Limit row: widget id 0, source number 0x2a).

### 5.5 `u3x_k_time_source` (tag 0x4c414e2a)

1. Result 0 when the descriptor pointer is null or its tag is not 0x4c414e2a.
2. `u3h_gate_closed(base, ev)` non-zero: result 1.
3. Game function 0x753e40(ev) is called (for every operation). Everything below ends with result 1.
4. Nothing more unless the operation is 2 and the text index (word 0x15) is 0.
5. L = the text answer the game left. Nothing more when L is null or `u3x_has_digit(L)` is 1.
6. T = `u3x_sym_table(s, 0x3b9a067d)`, then R = `u3x_sym_table(s, 0xf985c7b6)` (both lookups are made). Nothing
   more when either is null.
7. Nothing more when SEL < 0 or SEL ≥ the s32 at T + 0. type = the s32 at (pointer at T + 8) + SEL · 0xd0 + 0xc.
   Nothing more when type < 0 or type ≥ the s32 at R + 0 (signed comparisons here).
8. `u3x_time_limit_text(buffer, buffer + 47, (pointer at R + 8) + type · 0x58, word 1, L)` with the 48-byte
   buffer of this part; text answer = the buffer; k[5] + 1.

### 5.6 `u3x_eu_source` (tag 0x4c414850)

State: the current page P (0 .. 6, starts at 0) and the texture last answered X (starts null).

1. Result 0 when the descriptor pointer is null or its tag is not 0x4c414850.
2. Result 1 with nothing done when `base` is 0.
3. By operation; result 1 in every case, also for operations not named:
   - **4**: the name is the seven characters `eula/p` followed by the digit character '1' + P ("eula/p1" for page
     0), NUL-terminated, in a local buffer. X = native 20(image 0x1b31128, name), or null when native 20 is null.
     `u3x_image_answer(ev, X)`.
   - **5, 6, 7, 10, 23**: `u3x_pager_answer(ev, &P, 7)`.
   - **9**, in this order:
     1. m = the pointer at image 0x16e3188; profile = the pointer at image 0x17dd610; entry = game function
        0x6a8010() (always called).
     2. When profile and entry are both not null and the u32 at profile + 0x778 differs from the u32 at entry +
        0xc: profile + 0x778 = that u32. (The game lets the player leave the page only with the current version
        accepted.)
     3. The page step is made only when all of these hold, tested in this order: m is not null; X is not null;
        the pending dialog (pointer at m + 0x898) is null or its u32 at +0x10 is 0; the code check of 0x658050
        (3.10) passes. Then r = game function 0x658050(m, 1, 0) and `u3x_pager_step(&P, 7, r)`.

## 6. Test scenarios

Everything in this group can be tested old against new on a fake image with call-recording stubs: a state object
whose lookup (offset 656) and natives 1, 13, 20 and 44 are recording stand-ins; recording stubs for the `u3h_` /
`u3x_` functions of the second table of section 2 (or the real ones where they are pure); recording stand-ins for
the game functions 0x753e40 (which must also be able to leave a text answer), 0x6a8010 and 0x658050; the bytes of
3.9 (b) and the 12 code bytes of 3.10 placed at their image offsets, and a stand-in for `u3x_difficulty_texts`. Compare results, the request, the state block
(sw words, rules block), k[0..5], the profile word, the text the answer points to, and the order and arguments of
the calls. Nothing needs the live game except a look at the pages themselves.

Part A:

1. `u3x_type_is_coop`: every type from −8 to 40, and 0x10004, 0x80000004; with the image's mask and with a mask
   that has other bits (the function must follow the image).
2. The difficulty text: difficulty 0 .. 4 (the id asked of `u3h_text` is the one `u3x_difficulty_texts(base)` gives), 5, 6, 255 (no `u3h_text` call), on each of the two cards.
3. `u3x_custom_map`: MODE 0 / 1 / 2 / 3; net game table missing; table count 0 with native 1 null, native 1
   leaving it 0, native 1 filling it; SEL −1, 0, last, = count; a type that matches game 0, a later game, two
   games (first wins), none (game 0); native 44 null, −1, 0, 3; `gametype` null; a null map pointer in the slot
   (`*gametype` still written); the number of symbol lookups (two when it gets past step 2).
4. Map card: wrong tag, null descriptor; gate closed; MODE not 2 with operations 1, 2, 4, 11; no map; operation 4
   with the map slot (texture found, not found: word 12 = 0x40100000, native 20 null, name null) and another slot;
   operation 2 with text index 1; map slot with and without subtitle, with texts longer than 250 characters in
   all; game type slot with gt −1, in range, = count, rule sets missing; both difficulty slots with a co-op type,
   a non co-op type, type 0x12, difficulty 0, 4, 5, 255; five unknown slots in a row (ring and counter);
   operation 11 with each of the four ids and another id; operations 5 and 9.
5. Pause header: the same list (no MODE test); no current map (sw[5]); co-op mode off / on with gt 0x12; the two
   lines' separator; the counters sw[0] .. sw[4].
6. Last-game title: operations 0, 1, 2; no net game; native 13 null; map table null; map not in the table; map
   found; rule sets missing, gt out of range (empty middle part); gt 4 and 11 with difficulty 0, 4, 5; other gt
   with subtitle 0, each of the two "none" ids, a real id; texts that together exceed 127 characters.

Part B:

7. `u3x_adv_defaults`: block + 0 already SEL + 1 (no lookup, nothing written); SEL −1 (block + 0 becomes 0), 0,
   last, = count; table missing (10.0); a table value copied bit for bit (also a NaN pattern); the untouched
   bytes.
8. `u3x_adv_subst`: no tag, one, two, tag at the start and the end, "[A" and "[B]" left alone; value 0, 7, −7,
   2^31, −2^63 + 1; a buffer that ends inside the text, inside the number, exactly at the end.
9. `u3x_adv_source`: every tag from 0x4c414f00 to 0x4c414fff and tags with other upper bits; switched off; the
   counter's low half wrapping from 0xffff; guest with the button (operations 1, 8, 5) and with a row; MODE not 2
   for every row and operations 1, 2, 5, 8, 10; the button for every gt −1 .. 20; hidden labels (gt 10, 14, 19)
   per row; on / off rows: operations 5, 6, 10 with word 1 = 0, 1, 2, operation 2 with text index 0 and 1; respawn
   and health: operation 5 for every value operation 10 can store (round trip of all 29 and all 20 indices) and
   for the default 10.0 (index 3), for patterns 0.0, 2.0, 2.99, −1.0, NaN, 1e20; operation 10 with −1 and
   0x7fffffff; the texts; weapon mode: table missing, entries null (count still stored), n = 0 (operation 6 gives
   −1), the mode found once, twice (last wins), not found; operation 10 and 2 with word 1 = n − 1, n, −1.
10. `u3x_k04_retag`: switch on; another page id; list null; items null; 32 and 33 items; a style-13 row with the
    label, with another label; a style-6 row with the label; the Time Limit item with widget id 0 / 5, source
    kind 21 / 20, source number 0x2a / 0x2b, parameter 0 / non-zero.
11. `u3x_k_time_source`: wrong tag; gate closed (game function not called); operation 1 (called, nothing more);
    operation 2 with text index 1; text answer null; label with a digit; label without (also the empty label: no
    space is added); either table missing (both lookups made when the first is there); SEL −1, = count; type −1,
    = count; step × index + base with negative values; a label longer than the buffer.
12. `u3x_eu_source`: wrong tag; `base` 0; operation 4 on pages 0 and 6 (name text), native 20 null, texture found
    / not found; operations 5, 6, 7; operation 10 with −1, 3, 7; operation 23 with word 0xe 0 and 1; operation 9:
    profile null, entry null, version equal / different; m null; no texture answered yet; pending dialog null, up,
    not up; code check failing (0x658050 not called); results 3 and 4 at the first, a middle and the last page,
    another result; sequences (next, next, picture, back) across calls.

## 7. Notes: what looks odd, and what must stay as it is

Behaviour to keep (the new code must do all of it):

1. Both 0xacebf52d and 0xaceaf52d are accepted as the difficulty slot (the first came from a misread id; the menu
   data asks the second).
2. `u3x_custom_map` looks the net game table up twice, does not test the records pointer, does not limit the map
   index from above, and writes `*gametype` even when the map pointer it returns is null.
3. The map card answers "empty text, word 1 = 0" for every operation outside custom-game mode, also for
   operations where word 1 is an input. The same holds for the button and the respawn / health rows of 5.3.
4. The map card separates name and subtitle with a backslash, the pause header with a line feed. The "loading"
   scale 2.25 is set only on the map card.
5. The pause header asks the game type before it knows whether there is a map, and uses game type 0 when there is
   no net game.
6. Last-game title: the game type part is always there, so a missing map name gives a text that starts with the
   game type, and a missing game type with a third part gives " - third"; an out-of-range difficulty in the two
   co-op types gives a trailing " - ". Only types 4 and 11 show a difficulty here (not the full co-op set).
7. `u3x_adv_source`: the label is looked up before it is known to be needed; the result of `u3h_custom_map` is
   ignored (only the game type it stores is used, and it stays −1 when that function stops early); outside
   custom-game mode the on / off and weapon rows keep working on the block. Weapon mode: the count is stored
   before the entries pointer is tested; of several modes with the same value the last one is shown.
8. Respawn index = (seconds − 3) / 2 truncated: the default of 10.0 seconds (no table) shows as index 3 = 9
   seconds, and a value below 3 gives index 0 or −1. Health is stored as (percent + 0.3) × 0.01 so that the way
   back, ×100 truncated, lands on the same step.
9. `u3x_adv_subst` writes its NUL at `end` when the text fills the buffer.
10. `u3x_k04_retag` does nothing at all on a page with more than 32 items (not even the Friendly Fire restyle).
11. `u3x_k_time_source` calls the game's source without a code check and before looking at the operation; the
    number is only added when the label has no digit, so texts that already carry one are left alone.
12. `u3x_eu_source`: `base` 0 is answered with 1 (it does not happen in play); the accepted-version word is
    written on every operation 9, whatever page is open; the pager does not move until a picture has been
    answered once (X is never reset to null by a page change, only by a failed lookup); the result of 0x6a8010 is
    used without a pointer check beyond null.

What must stay as it is:

- The names and signatures of section 2's existing functions, the C wrappers and `u3h_custom_map`: their callers
  stay. `u3x_adv_tick` stays (one line).
- The layout of the rules block (3.8) and of the sw words: other groups and the launcher's diagnostics read them.
- The C array `k04_d` and the C variable `eu_row`: C code of other groups uses them.
- The 12-byte code check of 0x658050: the helper's guard against a different game build (as G25).
- The first-batch functions of section 2's second table (`u3x_image_answer`, `u3x_map_name_id`, `u3x_join_dash`,
  `u3x_two_lines`, `u3x_time_limit_text`, `u3x_has_digit`, `u3x_retag_widget`, `u3x_pager_answer`,
  `u3x_pager_step`, `u3x_party_guest`): called as they are.

- `u3x_difficulty_texts` (G37 delivers it, with the new `base` parameter) and `u3x_last_game_third` (it keeps
  receiving the pointer): called as they are. The three C callers of `u3x_difficulty_texts` become one-line
  wrappers of this group's new functions, so the C declaration of it can go (integrator).

To remove (integrator): the C variables `eu_cur`, `eu_tex`, `k04_buf`.

## 8. Open questions

1. **Difficulty names.** Settled by the coordinator: G37's `u3x_difficulty_texts(base)`. (For the record: the
   game's own function 0x741600(difficulty) returns the same five texts, but not in every case the way the old
   code did: with a settings byte of the game set, u8 at +0x4c7c of the object at image 0x1b24b80, it takes
   another lookup path with a "missing text" fallback, and it can return null.)
2. **Co-op mask.** G30 and G34 treat the co-op game types as a rule (G30 3.11) or ask `u3x_pm_coop`. Here the
   test is made on a game type that is not always the current net game's (4.2 step 4 falls back to game 0), so
   `u3x_pm_coop` would not give the same answer in every case; 3.9 (b) reads the game's own mask instead.
   `u3x_type_is_coop` could serve the other groups too (coordinator's decision). Not checked in the running game:
   that image 0x721100 holds the mask there (it does in the image file; the builder does not patch that function).
3. **Order of two lookups.** In the pause header's two-line text the old code looks up name and subtitle as two
   arguments of one call, so their order was the compiler's choice; this description asks for name first. If the
   tester sees the opposite order in the old build, the order of these two read-only lookups is to be accepted
   either way.
4. **Wrapping arithmetic** (5.3, last paragraph): the old code leaves signed overflow to the compiler; the
   description states what the shipped build does. Unreachable with the menu's own values.
5. Old comments call image 0x17d167c "the custom game's selection" and NGM + 0x661c "the selected net game" as if
   they were two words; they are the same address. Nothing in this group depends on the difference.
6. The EULA version word (profile + 0x778) and the entry of 0x6a8010 were not looked at beyond what the old code
   uses; which script table the entry comes from is the game's business.

## 9. Clarifications (tester, 2026-11-14)

Facts found by the old-against-new test (`reimpl2\G38\test_g38.cpp`) and by reading the shipped v7.212 build.

1. **5.3, last paragraph, and open question 4 (wrapping arithmetic): wrong for the two texts.** In the shipped
   build only operations 5 and 10 of the respawn and health rows use 32-bit wrapping arithmetic (word 1 · 2 + 3 and
   word 1 · 20 + 20 before `f_from_int`; the − 3 and − 10 after `f_to_int`). The number of the two **texts**
   (operation 2) is computed in 64 bits: word 1 is sign-extended to 64 bits first, then · 2 + 3 (respawn, the value
   given to `u3x_adv_subst`) or · 20 + 20 (health, the number written into B). It does not wrap. Examples the text
   must show: respawn, word 1 = 0x40000000: 2147483651; word 1 = 0x7fffffff: 4294967297; word 1 = 0x80000000:
   -4294967293. Health, word 1 = 107374182: 2147483660; word 1 = 0x7fffffff: 42949672960; word 1 = 0x80000000:
   -42949672940. For every word 1 whose number fits 32 bits (respawn: −1073741825 .. 1073741822; health:
   −107374183 .. 107374181) both ways give the same text; the menu only sends 0 .. 28 and 0 .. 19.
2. **Open question 3 (pause header, order of the two lookups): name first.** The shipped build looks up the map
   name (id at M + 4), then the subtitle (id at M + 8), then calls `u3x_two_lines`. The description's order is the
   old order; nothing has to be accepted "either way".
3. **Open question 2 (co-op mask).** The PS4 image file holds the bytes b1 72 00 00 at image offset 0x721100 (the
   immediate of a "load mask, test bit D" pair that follows a "D above 14" test), that is the mask 0x72b1, the
   value the old code had as a literal. With that mask `u3x_type_is_coop` answers as the old code did for every
   game type. That the running game holds the same bytes there is still unchecked (live game).
4. **`u3x_adv_source` reads the operation once.** The old code reads word 0 one time, before the
   `u3x_party_guest` call, and uses that value for the whole request. (Only visible if one of the functions called
   in between changed the request's word 0; none does.)
5. **Subtitle id of the map card and of the pause header.** The old code reads the u32 at M + 8 once for the
   "is there a subtitle" test and a second time, after the name lookup, as the id of the subtitle lookup. (Only
   visible if the name lookup changed the map entry; it does not.)
6. **Map card, name and subtitle (4.3).** The old code copies the name into the buffer before it looks the
   subtitle up, so the name text only has to stay valid until the next lookup. The pause header and the last-game
   title keep two or three looked-up texts until they are joined, so a text of `u3h_text` has to stay valid across
   later lookups there anyway.
7. **`u3x_has_digit` (5.5).** The old Time Limit source scanned the label itself; the call of `u3x_has_digit` is
   not part of the old call sequence (a pure function, free).
8. **5.3 step 3.** The old guest test compared the whole tag with 0x4c414f38; after step 1 that is the same as
   r = 0x38.
9. **5.1.** The default respawn pattern 0x41200000 is what `f_from_int(10)` gives (the old code computed it).
10. **Respawn text (5.3 step 11, operation 2).** The old code reads word 1 after the lookup of the text
    0x61275961, not before it. (Only visible if that lookup changed the request's word 1; it does not.)
