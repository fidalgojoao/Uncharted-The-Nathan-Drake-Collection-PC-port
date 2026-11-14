# G27. Treasures lists (treasure sets, the treasures of a set, the "found in" line)

Behaviour description for a reimplementation (reimpl2 pipeline, describer's document). It says what the functions
do, not how the old ones were written. All offsets are hexadecimal when prefixed with 0x. "Image offset X" means the
address `base + X`, where `base` is the game image base. All multi-byte values are little-endian; "u32 / s32 / u16 /
u8" are unsigned / signed integers of that width, "pointer" is 8 bytes. "Word n" of a request means the 32-bit word
at byte offset 4·n. Names and conventions follow `G23_boosters.md`, `G24_kickbacks.md` and `G22_rank_xp.md` ("menu
user", ea[n], "text answer", the string id rule, `u3x_sym_table`). Read G23 sections 3.1 to 3.5 and 3.9 first: this
document refers to them and does not repeat them.

This group has no float arithmetic and stores no float pattern.

## 1. Purpose and callers

The PS4 game kept the two menu pages of the multiplayer treasures, PAGE_NET_TREASURE_SETS (page id 0xda577fac: the
treasure sets of a tab, with a preview of the highlighted set's treasures) and the treasure list page (page id
0xecfa738a: the treasures of the chosen set). Its list source for the sets (source number 0xd4) is an empty
function; its list source for the treasures of the second page (source number 0xd5, image 0x759ba0) is there but
has to be told which treasures to show. PS3 1.19 has both. This group is the helper's replacement:

- **The sets list** (`u3x_treasures_source`): every request of the list on the sets page: the set rows (name,
  "owned / total", the "found in" line), the preview rows of the highlighted set's treasures (name, description,
  the "owned" check), the tab picture, selection, refresh, the number of preview cells, and the click that opens
  the treasure list page for a set. It also answers the title of the treasure list page (the chosen set's name).
- **The treasures of the chosen set** (`u3x_treasure_items_source`): title and refresh are answered by the helper
  (the refresh hands the game the treasures of the chosen set); every other request goes to the game's own source.
- **The list state and its accessors** (`u3x_treasure_build_sets`, `u3x_treasure_build_items`,
  `u3x_treasure_set_entry`, `u3x_treasure_tab`, the set name lookup): which sets belong to the current tab, which
  treasures belong to a set, out of the game's script tables.
- **The "found in" line** (`u3x_x66_found`, `u3x_x66_subst`): where the first treasure of the highlighted set is
  found (game type, map, difficulty), composed from a game text with placeholders.

Callers (all stay as they are):

- C `treasures_source` and C `treasure_items_source` (one-line wrappers today) are called by the helper's event
  entry `u3_profile_event` for every list request, one after the other in its chain of list-source hooks (after
  the Advanced Settings source, before the spoiler list); a result of 0 passes the request on to the next hook.
- All other functions of this group are called only from inside the group. They are exported (`extern "C"`) and
  keep name and signature, but have no C declaration and no caller elsewhere.
- `u3x_treasure_first_row` (existing, stays, see 2) is called through the C export `u3h_treasure_first_row` by the
  menu action handler (`u3x_menu_action`, action 0xe9bce205 "Treasures") right before it shows the sets page.

Everything runs on the game thread that updates and draws the menus. There is no locking and no timing rule.

## 2. Interface

No function takes or returns a float. The group is small enough for one part (217 old code lines).

Functions to deliver. All but the last are `extern "C"` and "existing": exported today under this name, name and
signature stay. Nothing is still in C: the two list sources already have their C one-line wrappers (they pass
base, request and state object unchanged), the other functions have no C side at all.

| Function | Signature | Called by |
|---|---|---|
| sets list source | `int u3x_treasures_source(uintptr_t base, int32_t *ev, void *s)` | C `treasures_source` |
| treasures list source | `int u3x_treasure_items_source(uintptr_t base, int32_t *ev, void *s)` | C `treasure_items_source` |
| set rows of a group | `void u3x_treasure_build_sets(void *s, int32_t group)` | `u3x_treasures_source` |
| treasure rows of a set | `void u3x_treasure_build_items(void *s, uint32_t set)` | both list sources |
| set entry of a row | `uint8_t *u3x_treasure_set_entry(void *s, int32_t row)` | `u3x_treasures_source` |
| current tab of the sets page | `uint8_t *u3x_treasure_tab(uintptr_t base, void *s)` | `u3x_treasures_source` |
| placeholder substitution | `void u3x_x66_subst(char *o, char *end, const char *t, const char *a, const char *b, const char *c)` | `u3x_x66_found` |
| "found in" line | `void u3x_x66_found(uintptr_t base, int32_t *ev, void *s)` | `u3x_treasures_source` |
| name of a set (file-private, not exported; any name) | (base, state object, u32 set id) → text pointer or null | both list sources |

Parameters:

- `base`: game image base. `s`: the helper's state object (opaque; fields in 3.2).
- `ev`: one data-source request (3.3).
- `group`: a set group number (3.4). `set`: a set id. `row`: a row of the sets list.
- `o`, `end`: start of an output buffer and the address where its text must end at the latest (the buffer has at
  least `end − o + 1` bytes). `t`: a NUL-terminated template. `a`, `b`, `c`: three NUL-terminated texts (never
  null).

Results: the two list sources return 1 when the request belongs to their list (tag match), else 0.
`u3x_treasure_set_entry` returns a pointer to a set entry (3.4) or null. `u3x_treasure_tab` returns a pointer to a
tab (3.4) or null.

Functions of the C part, of other groups and existing exported C++ functions that the new code calls (declare them;
do not reimplement them):

| Name | Signature | What it does |
|---|---|---|
| `u3h_ext` | `uint8_t *u3h_ext(void)` | the helper's own state block (3.1). In CONVENTIONS.h, with `ext_ea()` |
| `u3h_gate_closed` | `int u3h_gate_closed(uintptr_t base, int32_t *ev)` | 0 while multiplayer is active; otherwise stores the game's empty string as the request's text answer, counts, and returns 1 (G23) |
| `u3h_text` | `const char *u3h_text(uintptr_t base, void *s, uint32_t id)` | localized text of a text id; never null (G23) |
| `u3h_icon_index` | `int32_t u3h_icon_index(uintptr_t base, uint32_t id)` | index of UI icon `id` in the game's UI texture table, or −1 (G23) |
| `u3h_set_icon` | `void u3h_set_icon(uintptr_t base, int32_t *ev, uint32_t id)` | makes the request a plain icon answer for `id` when the icon is in the UI texture table; otherwise leaves the request untouched (G23) |
| `u3h_show` | `int u3h_show(uintptr_t base, void *s, uint32_t page, int silent)` | shows a menu page by id through the state object's show-page function; the result is not used here |
| `u3h_player_profile` | `uint8_t *u3h_player_profile(uintptr_t base)` | existing C export, a pure read: the profile object of the menu user (G23 3.5; G24 3.5), or null when the profile array pointer is null or the menu user is 4 or more |
| `u3h_crc_sid` | `uint32_t u3h_crc_sid(const char *name)` | existing C export, pure: the string id of a name by the rule of G23 3.9 |
| `u3x_sym_table` | `uint8_t *u3x_sym_table(void *s, uint32_t id)` | existing C++ function (stays): the data of a script symbol. It calls the state's lookup function (the pointer at `s` + 656) with `id` once; null when that pointer, the returned handle or the pointer the handle holds is null. "Lookup of X" below always means one such call (no caching) |
| `u3x_treasure_entry` | `uint8_t *u3x_treasure_entry(void *s, uint32_t index)` | existing C++ function of the inventory's trivial list (stays; only this group calls it): one lookup of the treasure table (3.4), made in every case; null when the table is null or `index` is not below the table's count (u32); otherwise the address of entry `index` (entries pointer + 0x68·`index`; the entries pointer is not tested) |

`u3x_treasure_first_row` (`void u3x_treasure_first_row(void *s)`, existing, stays): sets the selected set row of the
list state (3.2) to 0 and does nothing else. It is named here only because it writes the state this group owns.

`u3h_ext`, `u3h_player_profile` and `u3h_crc_sid` have no side effect: how often they are called, and whether the
string id of 3.6 is computed on every use or kept after the first, is not part of the behaviour. Every lookup
(`u3x_sym_table`, also the one inside `u3x_treasure_entry`), every `u3h_text`, `u3h_icon_index`, `u3h_set_icon`,
`u3h_show` call and the game call are made exactly as often and in the order section 4 gives.

## 3. Game and helper facts

### 3.1 The helper's state block (address from `u3h_ext()`)

| Word | Meaning |
|---|---|
| ea[0] bit 3 (value 8) | set = the page-level sources are off (G23 3.1): the page title branch of 4.7 step 7 is not taken |

This group has no counters.

### 3.2 The state object `s`

| Offset | Type | Meaning |
|---|---|---|
| 648 | pointer to function | the show-page function (used through `u3h_show`); the click of 4.7 only tests it for null |
| 656 | pointer to function | script symbol lookup (used through `u3x_sym_table`) |
| 760 | u64 | the image base; `u3x_treasures_source` stores `base` here (4.7 step 3) |
| 0x2b00 | at least 231 bytes | a text buffer shared with another list (the medals); the "found in" line is composed here |
| 0x2c80 | 251 characters | the text buffer shared with other lists (G23 3.2); the "owned / total" text is composed here |
| 0x3a00 | 0x310 bytes | the treasure list state, owned by this group |

Text answers composed in a buffer stay valid until the next request that uses the same buffer.

The treasure list state (offsets from `s` + 0x3a00):

| Offset | Type | Meaning |
|---|---|---|
| 0 | 256 × u8 | the set rows: for each row of the sets list, the index of its set in the set table |
| 0x100 | u32 | number of set rows |
| 0x104 | 256 × u16 | the treasure rows: for each row, the index of the treasure in the treasure table |
| 0x304 | u32 | number of treasure rows |
| 0x308 | s32 | the selected (highlighted) set row |
| 0x30c | u32 | the chosen set id (the set whose treasures the treasure list page shows) |

The layout is kept (the old-versus-new test compares the state object). The state starts as zeros.

### 3.3 Data-source requests

As G23 3.3. The fields these lists use:

| Field | Meaning |
|---|---|
| word 0 | operation |
| word 1 | row index (in); the answer of operations 5 and 6 (out) |
| word 2, 4 .. 12 | image answer (written only through `u3h_set_icon`) |
| word 0xe | the answer of a value query (operation 11) |
| word 0xf | the answer of operation 0x14 |
| word 0x13 | the id of the value asked (operation 11) |
| word 0x14 | the image slot asked (operation 4) |
| word 0x15 | the text slot asked (operation 2) |
| word 0x16 | the sub-question of operation 0x14 |
| word 0x17 | the clicked row (operation 0x16) |
| word 0x24 | the page marker: 1 when the request comes from the page source of the treasure list page (the page itself asks, not one of its lists) |
| pointer at byte 0x40 | the text answer |
| pointer at byte 0x98 | the source descriptor; its u32 at +0x1c is the source tag |

Source tags: 0x4c414ed4 = the sets list (and the page source of the treasure list page); 0x4c414ed5 = the list of
the chosen set's treasures.

"Text answer = T" below means: the pointer at byte 0x40 of the request becomes T.

### 3.4 Script tables (read at run time through `u3x_sym_table`; nothing is copied or kept)

**The set table**, symbol 0xf1f296cb: u32 number of entries at +0; pointer to the entries at +8 (may be null). A
set entry is 0xc bytes:

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | set id |
| 4 | u32 | text id of the set's name |
| 8 | s32 | group number: which tab of the sets page the set belongs to (1, 2 or 3) |

**The treasure table**, symbol 0xb114fe00: u32 number of entries at +0; pointer to the entries at +8 (may be null).
A treasure entry is 0x68 bytes; the position of an entry in the table is the treasure's index:

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | text id of the treasure's name |
| 0x18 | u32 | text id of the description |
| 0x2c | u32 | id of the set the treasure belongs to |
| 0x34 | u32 | map id: the map on which it is found (a value that is no map id = no particular map) |
| 0x38 | s32 | game type number in which it is found |
| 0x3c | s32 | difficulty number on which it is found (1 .. 4 have a text, 3.6) |

**The tab table**, symbol 0xf1514340 (as G19 3.5): u32 number of rows at +0; pointer to the rows at +8. A row is
0x20 bytes: +0 u32 page id, +4 u32 LAN value (0 or 1), +0x10 pointer to the tabs, +0x18 s32 index of the selected
tab. A tab is 0xc bytes: +0 u32 text id of the tab (it identifies the tab), +4 u32 UI icon id of the tab.

**The map table**, symbol 0x0dec97a4: u32 number of entries at +0, pointer to the entries at +8; an entry is 0x98
bytes: +0 u32 map id, +4 u32 text id of the map's name.

**The rule sets**, symbol 0xf985c7b6: s32 number of entries at +0, pointer to the entries at +8; an entry is 0x58
bytes: +0 u32 text id of the game type's name. Entry number = game type number.

### 3.5 Game data and the game's own treasure list

| Image offset | Type | Meaning |
|---|---|---|
| 0x17d5c60 | u8 | LAN byte: non-zero = LAN / not online (selects the row of the tab table) |
| 0x1b6aa38 | pointer | the game's treasure list: the treasure table it shows rows of |
| 0x1b6aa40 | 256 × u16 | the game's treasure list: the treasure index of each row |
| 0x1b6ac40 | u32 | the game's treasure list: number of rows |
| 0x759ba0 | function (s32 *request) | the game's own source of the treasures list (source number 0xd5). It answers rows, pictures, selection and counts from the three fields above; no result is used |

A profile object (G23 3.5) holds one byte per treasure from offset 0xacc: the byte at 0xacc + treasure index is
non-zero when the player owns the treasure. Indices from 0x400 on have no byte. **"Owned(i)"** below means: the
menu user's profile object is not null, i is below 0x400 and the byte at profile + 0xacc + i is non-zero.

The menu user's profile object is `u3h_player_profile(base)`.

### 3.6 Ids, texts and the icon

Page ids: 0xda577fac the sets page (the row of the tab table to look for); 0xecfa738a the treasure list page (the
page the click opens).

Tabs of the sets page, by the tab's text id: 0x49d4b22d shows the sets of group 2; 0x5baab3cd those of group 3;
every other tab those of group 1.

Text slots (word 0x15) of the sets list: 0 the set's name; 1 "owned / total"; 4 the "found in" line; 0x0843d4ad
the name of a preview treasure; 0x0a56c0ab its description.

Image slots (word 0x14): 0xf5a294f3 the tab's picture; 0x21981e34 the "owned" check of a preview treasure.

Value id (word 0x13): 0xbcf5ea24 the number of preview cells.

Text ids (all looked up with `u3h_text`):

| Text id | Meaning |
|---|---|
| 0xdcdd1012 | the "found in" template with three placeholders: `[TEXT]` game type, `[TEXT2]` map, `[TEXT3]` difficulty |
| 0x2b3ffa5b | the "found in" template with one placeholder `[TEXT]` |
| 0x35561665 | the complete "found in" line of game type 21 (no placeholder) |
| 0xd5fc28f0 | the name put in for game type 20 |
| 0x85a5bbbe | the name put in for every other game type without a map |
| 0x14956c33 | the difficulty text for difficulty 1 and 2 |
| 0x87d40e88 | the difficulty text for difficulty 3 and 4 |

Game type numbers with a meaning here: 17 and 18 (the game type's own name is used), 20, 21.

The "owned" check is the UI icon "general/net/menu-icons/icon-check", the same icon as the "equipped" check of G23
and G24. Its id is not held as a number: the new code holds the name (read-only helper data) and takes its string
id (`u3h_crc_sid`, G23 3.9); the result must be 0x08208d0a (G23's check value). Cross-check, not needed by the new
code: the game's own source 0x759ba0 compares against the same id (an immediate of the instruction at image
0x75a092).

Texts of the helper's own (they must live in read-only helper data): an empty text; a single slash "/"; the five
characters "[TEXT" (the start of a placeholder); the icon name above.

There is no literal table of game values in this group. Sets, treasures, tabs, maps and rule sets are game data
read at run time (3.4); the remaining literals are single ids (3.6).

## 4. Behaviour

Shared wording:

- "Decimal": plain digits of an unsigned number, no separators.
- "The helper's empty text": a NUL-only text in read-only helper data.
- "Show icon X": first `u3h_icon_index(base, X)`; only when its result is 0 or more, `u3h_set_icon(base, ev, X)`.
- "List state": the fields of 3.2 at `s` + 0x3a00.

### 4.1 Name of a set (file-private)

One lookup of the set table. Null table: null, nothing else. Otherwise the entries are walked in table order, from
the first to the count-th (not at all when the entries pointer is null); for every entry whose id equals the set
id asked, `u3h_text(base, s, the entry's name id)` is called. The result is the text of the last such entry, or
null when no entry has the id. (Set ids are unique in the game's data: one call.)

### 4.2 `u3x_treasure_build_sets` and `u3x_treasure_build_items`

**`u3x_treasure_build_sets(s, group)`.** One lookup of the set table; then the number of set rows becomes 0 (also
when the table is null, and then nothing else happens). The entries are walked in table order (not at all when the
entries pointer is null); every entry whose group number (s32) equals `group` gets the next set row: the row's
byte becomes the entry's index, cut to its low 8 bits. The walk stops as soon as 256 rows are filled. Rows beyond
the new count keep their old bytes.

**`u3x_treasure_build_items(s, set)`.** One lookup of the treasure table; then the number of treasure rows becomes
0 (also when the table is null, and then nothing else happens). The entries are walked in table order (not at all
when the entries pointer is null); every entry whose set id (u32 at +0x2c) equals `set` gets the next treasure
row: the row's u16 becomes the entry's index, cut to its low 16 bits. The walk stops as soon as 256 rows are
filled. Rows beyond the new count keep their old values.

Neither function touches the selected row or the chosen set id.

### 4.3 `u3x_treasure_set_entry(s, row)`

One lookup of the set table, made in every case. Null when the table is null, `row` is negative, `row` is not
below the number of set rows, or the row's byte (the set index) is not below the table's count (u32). Otherwise
the address of that set entry: entries pointer + 0xc · index. The entries pointer is not tested. No memory is
written.

### 4.4 `u3x_treasure_tab(base, s)`

One lookup of the tab table. Null table: null. The rows are examined in index order (not at all when the rows
pointer is null). The first row whose page id is 0xda577fac and whose LAN value (u32) equals the LAN byte (image
0x17d5c60, taken as a number 0 .. 255) decides: when its tabs pointer is not null and its selected index (s32) is 0
or more, the result is the address of the selected tab (tabs pointer + 0xc · index; the index is not compared with
a count); otherwise null. Later rows are not examined. No such row: null. No memory is written.

### 4.5 `u3x_x66_subst(o, end, t, a, b, c)`

Copies the template `t` to the output, replacing placeholders. The output position starts at `o`; the reading
position at the start of `t`. Repeated while the character at the reading position is not NUL and the output
position is below `end`:

- When the five characters from the reading position on are "[TEXT" (a NUL among the five means: not a
  placeholder; the old code reads the bytes behind such a NUL without using them, the new code need not), the
  sixth character decides:
  - `]`: the piece is `a`; the reading position moves on by 6.
  - `2`: the piece is `b`; the reading position moves on by 7.
  - anything else (also `3`, also NUL): the piece is `c`; the reading position moves on by 7.
  The piece's characters up to its NUL are written to the output, stopping when the output position reaches `end`.
  The piece is not searched for placeholders. In the 7-character case the seventh character is skipped without
  being looked at (for "[TEXT2]" and "[TEXT3]" it is the closing bracket).
- Otherwise the one character is copied and both positions move on by 1.

At the end a NUL is written at the output position (which can be `end` itself). When `o` is not below `end` at
entry, only that NUL is written (at `o`).

Moving on by 6 or 7 does not look at the characters skipped: a template that ends inside a placeholder ("[TEXT"
followed by fewer than two more characters, the first of them not `]`) makes the reading position pass the
template's NUL, and the bytes behind it are then read as template. This is the old behaviour (section 6, point 5);
the game's two templates do not end this way.

### 4.6 `u3x_x66_found(base, ev, s)`: the "found in" line

It describes the first treasure row of the list state. Steps, in this order:

1. No treasure rows (count 0): nothing at all (no lookup).
2. T = `u3x_treasure_entry(s, the first treasure row's index)`; null: nothing more. From T: map id (u32 at +0x34),
   game type g (s32 at +0x38), difficulty d (s32 at +0x3c).
3. One lookup of the map table, then one lookup of the rule sets (both always, in this order).
4. The map: null when the map table is null or its entries pointer is null; otherwise the first entry, in table
   order among the table's count (u32), whose map id equals T's; null when there is none.
5. The rule set: null when the rule sets are null, g is negative, or g is not below their count (signed
   comparison); otherwise entries pointer + 0x58 · g (the entries pointer is not tested).
6. By case:
   - **A map was found.** In this order: the game type's name = `u3h_text(base, s, the rule set's name id)`, or
     the helper's empty text without a rule set (no call), is copied into a local buffer, cut after 63
     characters; then the map's name = `u3h_text(base, s, the map entry's name id)` is copied into a second local
     buffer, cut after 63 characters; then the template = `u3h_text(base, s, 0xdcdd1012)`; then the difficulty
     text = `u3h_text(base, s, 0x14956c33)` when d is 1 or 2, `u3h_text(base, s, 0x87d40e88)` when d is 3 or 4,
     and the helper's empty text for every other d (no call). Then the substitution of 4.5 with output `s` +
     0x2b00, end `s` + 0x2b00 + 230, that template, and the pieces: the first local buffer, the second local
     buffer, the difficulty text. Text answer = `s` + 0x2b00.
   - **No map, g is 21.** Text answer = `u3h_text(base, s, 0x35561665)`. The buffer is not touched.
   - **No map, any other g.** The name id is 0xd5fc28f0 when g is 20; the rule set's name id when g is 17 or 18
     and there is a rule set; 0x85a5bbbe otherwise. In this order: template = `u3h_text(base, s, 0x2b3ffa5b)`;
     name = `u3h_text(base, s, the name id)`. Then the substitution of 4.5 with output `s` + 0x2b00, end `s` +
     0x2b00 + 230, that template, and the pieces: the name (used as returned, not copied first), the helper's
     empty text, the helper's empty text. Text answer = `s` + 0x2b00.

The composed line is therefore at most 230 characters plus the NUL.

### 4.7 `u3x_treasures_source`: the sets list

"Set entry of row r" = `u3x_treasure_set_entry(s, r)` (one lookup each time). "Preview row of r": with N = the
number of treasure rows, r − (4 − N) when N is below 4, else r (signed; the preview has four cells and fewer than
four treasures sit at its end).

Steps, in this order:

1. Source descriptor (pointer at byte 0x98) null, or its tag not 0x4c414ed4: return 0, nothing touched.
2. `u3h_gate_closed(base, ev)` non-zero: return 1.
3. The image base is stored in the state object (offset 760).
4. One lookup of the set table; when it is not null, one lookup of the treasure table. Either null: for operation
   6, word 1 = −1; return 1 in every case, nothing else.
5. The menu user's profile object is taken (for "owned"); r = word 1.
6. (From here on the result is always 1.)
7. **Page title.** When word 0x24 is 1 and bit 3 of ea[0] is clear: for operation 1, the name of the chosen set
   (4.1, with the chosen set id of the list state) is looked up and, when it is not null, becomes the text answer.
   Return 1 for every operation (nothing else is answered for a page request).
8. By operation (operations not listed: nothing):

| Operation | Effect |
|---|---|
| 2 | row text, see below |
| 4 | row picture, see below |
| 5 | word 1 = the selected set row |
| 6 | word 1 = the number of set rows − 1 (−1 for an empty list) |
| 9 | refresh, see below |
| 10 | the selected set row = r (stored as it is, not compared with anything) |
| 11 | when word 0x13 is 0xbcf5ea24: word 0xe = 4 when the number of treasure rows is 1, 2 or 3, else that number (0, or 4 and more). Other values: nothing |
| 0x14 | when word 0x16 is 1: word 0xf = −1. Otherwise nothing |
| 0x16 | the click, see below |

**Operation 2 (row text)**, by text slot (word 0x15):

| Text slot | Effect |
|---|---|
| 0 | set entry of row r; when there is one: text answer = `u3h_text(base, s, its name id)` |
| 1 | set entry of row r; none: nothing. Otherwise one more lookup of the treasure table (its result is used without a null test: step 4 has just found it), and over all its entries in table order (none when the entries pointer is null): total = the number of entries whose set id equals the set entry's id; own = the number of those with owned(index). The text buffer `s` + 0x2c80 receives own in decimal, "/", total in decimal, and a NUL (never more than 60 characters). Text answer = that buffer |
| 4 | `u3x_x66_found(base, ev, s)` (4.6) |
| 0x0843d4ad and 0x0a56c0ab | p = the preview row of r. p negative: text answer = the helper's empty text. p not below the number of treasure rows: nothing. Otherwise T = `u3x_treasure_entry(s, index of treasure row p)`; when T is not null: text answer = `u3h_text(base, s, id)` with id = T's name id for slot 0x0843d4ad and T's description id for slot 0x0a56c0ab |
| other | nothing |

**Operation 4 (row picture)**, by image slot (word 0x14):

- 0xf5a294f3: tab = `u3x_treasure_tab(base, s)`; when there is one: show icon (the tab's icon id, u32 at +4).
- 0x21981e34: p = the preview row of r. When p is 0 or more, p is below the number of treasure rows and
  owned(index of treasure row p): show icon (the "owned" check of 3.6). Otherwise nothing (and then
  `u3h_icon_index` is not called).
- other: nothing.

**Operation 9 (refresh).** tab = `u3x_treasure_tab(base, s)`; none: nothing (the list state stays as it is).
Otherwise `u3x_treasure_build_sets(s, group of the tab)` with the group of 3.6 by the tab's text id (u32 at +0);
then the set entry of the selected set row: when there is one, `u3x_treasure_build_items(s, its id)`; otherwise the
number of treasure rows becomes 0. (So the preview always shows the highlighted set of the current tab.)

**Operation 0x16 (click).** Set entry of row (word 0x17); none: nothing. Otherwise the chosen set id = the entry's
id; `u3x_treasure_build_items(s, that id)`; then, when the show-page function of the state object (pointer at
`s` + 648) is not null: `u3h_show(base, s, 0xecfa738a, 0)`.

### 4.8 `u3x_treasure_items_source`: the treasures of the chosen set

Steps, in this order:

1. Source descriptor null, or its tag not 0x4c414ed5: return 0, nothing touched.
2. `u3h_gate_closed(base, ev)` non-zero: return 1.
3. One lookup of the treasure table (for every operation); P = its result. (The image base is not stored in the
   state object by this function.)
4. Operation 1 (title): text answer = the helper's empty text. Then, only when word 0x24 is 1: one lookup of the
   set table; when it is not null: the name of the chosen set (4.1: a second lookup of the set table), and when
   that name is not null it becomes the text answer. Return 1. (Bit 3 of ea[0] is not looked at here.)
5. Operation 9 (refresh): nothing when P is null. Otherwise `u3x_treasure_build_items(s, the chosen set id)`; then
   the game's treasure list (3.5) is filled, in this order: the pointer at image 0x1b6aa38 = P; the u16 words from
   image 0x1b6aa40 on = the treasure rows of the list state, one per row, in row order (words beyond the count
   are left as they are); the u32 at image 0x1b6ac40 = the number of treasure rows. Return 1.
6. Every other operation: game function 0x759ba0(`ev`) (also when P is null); return 1.

## 5. Test scenarios

Everything can be tested with a fake image (the fields of 3.5, the profile array and menu user fields of G23 3.5),
the five tables in test memory behind a recording stand-in for the state's lookup function, a state object, and
recording stubs for `u3h_gate_closed`, `u3h_text`, `u3h_icon_index`, `u3h_set_icon`, `u3h_show`, `u3h_ext` and for
game function 0x759ba0 (`u3h_crc_sid`, `u3h_player_profile`, `u3x_sym_table` and `u3x_treasure_entry` real, or
stand-ins that do what section 2 says). Compare memory (request, state object with both text buffers and the list
state, image fields), results, and the order and arguments of all calls and lookups.

A useful data set: six sets with groups 1, 2, 1, 3, 1, 2, two of them with the same id; ten treasures spread over
them (one set with 0, one with 2, one with 5 treasures); a tab table with a row of another page, a row of the sets
page for LAN value 0 and one for LAN value 1.

Accessors:
1. Name of a set: table missing; id not in the table (null, no text call); id once; id twice (two text calls, the
   second text returned); entries pointer null.
2. `u3x_treasure_build_sets`: table missing (count becomes 0); groups 1, 2, 3 and a group no set has; entries
   pointer null; a table of 300 entries of one group (256 rows, then stop); a table whose matching entries lie at
   indices above 255 (the stored bytes are the low 8 bits); old row bytes beyond the count unchanged.
3. `u3x_treasure_build_items`: table missing; a set with 0, 2 and 5 treasures; 300 treasures of one set (256
   rows); entries pointer null.
4. `u3x_treasure_set_entry`: table missing; row −1, 0, count − 1, count (the lookup is made each time); a row byte
   that is not below the table's count (null).
5. `u3x_treasure_tab`: table missing; rows pointer null; no row of the page; row of the page with the other LAN
   value only; both LAN values; selected index 0, 2 and −1; tabs pointer null; two matching rows of which the
   first has a null tabs pointer (null: the second is not used).

Substitution:
6. `u3x_x66_subst`: no placeholder; each of "[TEXT]", "[TEXT2]", "[TEXT3]" alone, at the start, in the middle and
   at the end; all three in one template; the same placeholder twice; "[TEXT9]" and "[TEXTx]" (piece `c`, seven
   characters consumed); "[TEXT2x" followed by more text (the `x` is swallowed); "[TEX]" and "[text]" (copied as
   they are); a piece that itself contains "[TEXT]" (not replaced again); empty pieces; an empty template; output
   cut inside plain text, exactly at `end`, and inside a piece; `o` equal to `end`. Templates that end inside a
   placeholder are not compared (4.5, last paragraph) unless the test controls the bytes behind the template.

"Found in" line:
7. No treasure rows (nothing, no lookup); first treasure index beyond the table (nothing after the one lookup).
8. Map found: with a rule set and without (g −1, g = count, rule sets missing); difficulty 0, 1, 2, 3, 4, 5; a
   game type name and a map name longer than 63 characters (cut); a template long enough to cut the line at 230
   characters; the order rule name → map name → template → difficulty text.
9. No map (map table missing, entries pointer null, id not in the table): g = 21 (direct text, buffer untouched);
   g = 20; g = 17 and 18 with and without a rule set; g = 5; the order template → name.

Sets list:
10. Null descriptor; wrong tag (0, nothing touched). Gate closed (1, nothing else, base not stored).
11. Tables: set table missing (one lookup only), treasure table missing: operation 6 (word 1 = −1), operations 1,
    2 and 9 (nothing).
12. Page title: word 0x24 = 1, bit 3 of ea[0] clear: operation 1 with a known chosen set (its name), with an
    unknown one (text answer untouched); operations 2, 6 and 9 (nothing at all); bit 3 set: the request is handled
    as a list request (operation 1: nothing; operation 6: the count); word 0x24 = 2: list request.
13. Operation 2 slot 0: a valid row, row −1, a row beyond the list. Slot 1: no profile object (own 0); some owned;
    a treasure at index 0x400 and above whose would-be byte is non-zero (not owned); a set without treasures
    ("0/0"); two-digit numbers; no set entry (nothing, buffer untouched). Slot 4 reaches 4.6. Slots 0x0843d4ad /
    0x0a56c0ab with 0, 1, 3, 4 and 6 treasure rows: every request row 0 .. 5 (empty text, a treasure's text, or
    nothing); a treasure row whose index is beyond the table (nothing). An unknown slot.
14. Operation 4: tab picture with a tab whose icon is in the UI table / is missing / no tab; the check for an
    owned treasure, a not owned one, a shifted row below 0, a row beyond the list (no `u3h_icon_index` call in the
    last three), the check icon missing from the UI table (index call only); another slot.
15. Operations 5, 6 (0 and several set rows), 10 (row 3, row −7), 11 (0, 1, 3, 4, 9 treasure rows; another value
    id), 0x14 (word 0x16 = 1 and 0), an unlisted operation.
16. Operation 9: no tab (state untouched); tab ids 0x49d4b22d, 0x5baab3cd and another (groups 2, 3, 1); the
    selected row valid (its treasures) and beyond the new list (treasure rows 0); the order of the four lookups.
17. Operation 0x16: no set entry; a set (chosen id stored, treasure rows rebuilt, `u3h_show` with 0xecfa738a and
    0); show-page pointer null (stored and rebuilt, no `u3h_show`).

Treasures list:
18. Wrong tag; gate closed. Operation 1 with word 0x24 = 0 (empty text only), = 1 with a known set (name), an
    unknown set (empty text), the set table missing (empty text, one set-table lookup); bit 3 of ea[0] set (no
    difference).
19. Operation 9: treasure table missing (nothing written); a set with 0 and with 5 treasures: the three image
    fields, words beyond the count untouched, the list state rebuilt, two treasure-table lookups.
20. Operations 2, 4, 5, 6, 10, 0x16: the game function called once with the request, nothing else written; the
    same with the treasure table missing.

Needs the live game: only the look (that the rows, the check and the tab picture appear in their cells, that the
treasure list page opens with the chosen set and its title).

## 6. Things that look like bugs or dead code (behaviour to keep)

1. The set name lookup does not stop at the first set with the id: the last one wins and the text lookup is made
   for each.
2. A set row holds the set's index in 8 bits and a treasure row in 16 bits: indices above 255 (sets) are cut.
   Both lists stop at 256 rows without a sign.
3. `u3x_treasure_set_entry`, `u3x_treasure_entry` and the rule set of 4.6 do not test the entries pointer; the
   "owned / total" count uses the treasure table of its own lookup without a null test.
4. The row builders clear the count after their lookup even when the table is missing; rows beyond the count keep
   stale values.
5. `u3x_x66_subst`: a sixth character other than `]` and `2` selects the third piece; the seventh character is
   swallowed whatever it is; a template ending inside a placeholder is read beyond its NUL; the final NUL is
   written even when the output start is not below the end.
6. The "found in" line always describes the first treasure of the set, whichever preview row asks, and is
   answered for every request row. For game type 21 the buffer is not used; a set without treasures leaves the
   text answer untouched.
7. In the "found in" line the game type's name is put in only for a map, or without a map for game types 17 and
   18; without a rule set the first placeholder becomes empty (map case).
8. `u3x_treasure_tab` stops at the first row of the page with the right LAN value, also when that row has no
   usable tab.
9. A page request (word 0x24 = 1) gets nothing but the title: operation 6 is not answered. With the two tables
   missing only operation 6 is answered.
10. Operations 5, 6, 10 and 11 answer from the list state as the last refresh (operation 9) or click left it;
    operation 10 stores any row; a refresh keeps the selected row even when the new tab has fewer sets (the
    preview is then empty until another row is selected).
11. The click (operation 0x16) stores the chosen set and rebuilds the treasure rows also when the page cannot be
    shown, and does not change the selected row.
12. The icon presence is asked twice for every icon shown (`u3h_icon_index`, then inside `u3h_set_icon`).
13. `u3x_treasure_items_source` does not store the image base, looks up the treasure table for every operation
    (and the set table twice for a page title), answers the empty text for operation 1 before it knows whether
    there is a title, does not look at bit 3 of ea[0], and passes all other operations to the game even when the
    treasure table is missing.
14. The treasure rows of the list state serve two purposes: the preview of the highlighted set (refresh of the
    sets list) and the treasures of the chosen set (click, refresh of the treasures list). After the treasures
    list was open the preview state holds the chosen set's treasures until the sets list is refreshed.
15. Preview: fewer than four treasures are shifted to the last cells and the value query then answers 4; with no
    treasure it answers 0 and the name and description slots answer the empty text for rows 0 .. 3.

## 7. Open questions and notes

1. What operation 0x14 with sub-question 1 asks, and what its answer −1 in word 0xf means, is not known; the
   behaviour does not depend on it.
2. The wording of the seven texts of 3.6 and the names of game types 17, 18, 20 and 21 and of set groups 1, 2, 3
   were not looked up (the texts are the game's; the helper only passes ids). The behaviour does not depend on
   them.
3. `u3x_x66_subst` with a template that ends inside a placeholder reads memory behind the template (6, point 5).
   The description keeps it as it is. If the integrator prefers a safe rule (stop at the template's NUL), the
   result is the same for the game's two templates; it would be a documented deviation for artificial inputs only.
4. The "owned" check moves from a literal id to the icon's name (3.6), as G24 did for its icons. As in G23 (open
   question 1) the helper's own CRC (`u3h_crc_sid`) is asked for, not the game's string id function.
5. `u3x_treasure_first_row` and `u3x_sym_table` are in no list of the inventory (not even the trivial one); both
   are one-line functions that sit between this group's functions in the old file and stay. `u3x_treasure_entry`
   and the file-private "owned" test are in the inventory's trivial list; "owned" is described in 3.5 and is part
   of the new code, `u3x_treasure_entry` is called as it is.
6. PS3 also shows a reward line per set; the helper never had it (it is not part of the old code and not part of
   this description).

## 8. Clarifications (tester, 2026-11-14)

Facts confirmed by the differential test (`reimpl2\G27\test_g27.cpp`, old v7.212 against the new `part.cpp`, built
at -O1 and -O2). They settle the implementer's questions. Nothing in sections 1 to 6 was found wrong; these items
make precise what the text leaves open.

1. **Index arithmetic.** Positions of the form "pointer + entry size × index" are formed in pointer width (64 bits):
   the tab of 4.4 (tested up to index 0x7fffffff), the rule set of 4.6 (tested up to game type 0x7ffffffe), the
   walks of the treasure table (4.2, the "owned / total" count) and of the map table (4.6): in tables of more
   than 4 GB the entries behind the 4 GB mark are reached. Exception in v7.212: the two walks of the set table
   (the name of a set, 4.1, and `u3x_treasure_build_sets`, 4.2) formed the byte position of a field inside the
   table as a 32-bit number (0xc × index + field offset, modulo 2^32). For a set table of more than 0x15555555
   entries (4 GB) v7.212 therefore read the start of the table again instead of the entries behind 4 GB (from
   index 0x15555555 on for the group number and the name id, from index 0x15555556 on for the set id). The new
   code walks the set table in pointer width too and reads the real entries. The game's set table has a few dozen
   entries and a set row holds the index in a byte, so this cannot be seen in the game: a deviation for artificial
   tables only, kept as the new code has it unless the coordinator decides otherwise.
2. **Preview row.** "r − (4 − N)" is 32-bit arithmetic that wraps: the difference is taken modulo 2^32 and then
   read as a signed number. r = −2^31 with N = 0 gives 2^31 − 4: not negative and not below the count, so nothing
   is answered (not the empty text); the same holds for the check of operation 4. The shipped v7.212 code does
   this, and so does the new code (rows at both ends of the 32-bit range were compared with N = 0 .. 9).
3. **`u3h_player_profile`.** v7.212 takes the profile object once per request of the sets list, right after step
   4 found both tables and before the page test of step 7: also for a page request and for operations that do
   not use it, and never when the request ends at step 1, 2 or 4. `u3x_treasure_items_source` and the other
   functions never take it. The new code calls it at the same places (equal in every compared case). As section
   2 says, this is not part of the behaviour.
4. **`u3x_x66_subst`, NUL and the end of the template.**
   - A NUL among the five characters never makes a placeholder, whatever stands behind the NUL (also the rest of
     a placeholder): the characters before the NUL are copied and the template ends at the NUL.
   - Reads: v7.212 looks at the fifth of the five characters first, so every step that copies a single character
     reads the byte four positions ahead. A template whose last character is copied singly (it does not end with
     a complete placeholder) therefore has up to three bytes behind its NUL read; they are not used. The new code
     compares from the first character and stops at the first difference: it reads no byte behind the NUL of
     such a template. The new code never reads a byte v7.212 does not read.
   - A template that ends inside a placeholder: "[TEXT" directly before the NUL makes both codes skip the NUL and
     the byte behind it and read on from the second byte behind the NUL; "[TEXT" and one more character (not
     `]`) before the NUL makes both skip the NUL and read on from the first byte behind it. Old and new read the
     same bytes and give the same output.
   - The output area must not overlap the template or the pieces (no caller does that). For overlapping areas
     v7.212 and the new code can give different results (v7.212 looks at the sixth character a second time
     after the piece was written); this is outside the described behaviour.
5. **The check icon.** "general/net/menu-icons/icon-check" gives 0x08208d0a by the rule of G23 3.9; in every case
   the id that reached `u3h_icon_index` and `u3h_set_icon` was the one v7.212 passed.
6. **Not comparable.** A list state with more than 256 set or treasure rows (the builders cannot produce it), and
   an entries pointer that is null while the table's count is not zero and rows from another table are still in
   the list state (6, point 3: the pointer is not tested, v7.212 then reads memory near address 0).
