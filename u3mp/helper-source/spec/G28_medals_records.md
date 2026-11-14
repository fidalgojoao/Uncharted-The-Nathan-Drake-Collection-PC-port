# G28. Medals list, Profile and Records rows, player card tabs, stat bars, viewed-profile object, LAN profile delete

Behaviour description for a reimplementation (reimpl2 pipeline, describer's document). It says what the functions
do, not how the old ones were written. All offsets are hexadecimal when prefixed with 0x. "Image offset X" means the
address `base + X`, where `base` is the game image base. All multi-byte values are little-endian; "u32 / s32 / u16 /
u8 / u64 / s64" are unsigned / signed integers of that width, "pointer" is 8 bytes. "Word n" of a request means the
32-bit word at byte offset 4·n. A "float pattern" is the 32-bit pattern of a single-precision float, carried in a
u32. Names and conventions follow `G22_rank_xp.md`, `G23_boosters.md`, `G24_kickbacks.md` and `G27_treasures.md`
("menu user", "native k", ea[n] / eb[n], "text answer", image answers, `u3x_sym_table`). Read G23 sections 3.1 to
3.5 first: this document refers to them and does not repeat them.

Only one function of this group has float arithmetic (the stat bar, 4.12). Two float patterns are stored as plain
32-bit words elsewhere (4.4, 4.5).

## 1. Purpose and callers

Seven small features of the multiplayer menus that PS3 1.19 has and the PS4 game (built from 1.10) lost or never
had. They are delivered in three independent parts.

**Part A. The medals list** (PAGE_MULTIPLAYER_MEDALS, page id 0xc98ee52a). The PS4 game kept the page, but its list
source (source number 0xd3) is an empty function. The helper's replacement: which medals the current tab shows
(`u3x_medal_build`, `u3x_w04_tab`, `u3x_medal_record`), and every request of the list (`u3x_medals_source`: the row
texts name / description / "earned N" / points, the medal picture, selection, count, refresh).

- C `medals_source` (a one-line wrapper today) is called by the helper's event entry `u3_profile_event` for every
  list request, in its chain of list-source hooks (after the emblem source, before the Time Limit source); a result
  of 0 passes the request on to the next hook.
- `u3x_w04_tab`, `u3x_medal_build`, `u3x_medal_record`, `u3x_medal_list` are called only from inside part A. They
  have C one-line wrappers (`w04_tab`, `medal_build`, `medal_record`, `medal_list`) without a C caller.

**Part B. Profile and Records rows, player card tabs.**

- *The "new" marker of the Profile row* (`u3x_new_marker`): called by C `profile_new_marker` (stays in C: the
  helper's switch bit 0 of ea[0], its counters ea[4] to ea[7], the menu user's "new item" flags), which the lobby
  menu source (G31, through the C export `u3h_profile_new_marker`) calls for the picture of its Profile row.
- *Two more rows of the Network Profile list* (`u3x_retag_net_profile`, `u3x_net_profile_source`): PS3 1.19 has 12
  rows there, the PS4 game 10. Rows 10 ("Hat Stats") and 11 ("DLC"), each an On / Off toggle stored in the
  player's profile, are answered by the helper; everything else goes to the game's own source.
- *The Records rows in LAN play* (`u3x_n32_call`): the game shows three Records rows while its LAN byte is set and
  five online. The helper shows six (the two online-only rows, the three LAN rows, and a "Message of the Day" row).
  `u3x_n32_call` is the row translation; it is called by the Records list source `u3x_records_source` (G22 4.8,
  which also owns the Legacy row's picture) and by nothing else. `u3x_n32_records` (existing, stays) says whether
  this mode is on.
- *The player card's tab bar and body* (`u3x_n32_retag`, `u3x_n32_source`): the script data has only an "online"
  tab entry for the player card page (page id 0x83f24f04), so in LAN play the card had no tabs. The two lists are
  handed to the helper, which runs the game's own sources with the LAN byte cleared for the duration of the call.
- Callers: C `retag_net_profile` and C `n32_retag` (one-line wrappers) are called once for every page of the menu
  file while the helper prepares the menu (event kind 8), one after the other. C `netprofile_source` and C
  `n32_source` (one-line wrappers) are called by `u3_profile_event` in its chain of list-source hooks.

**Part C. Stat bars, the viewed-profile object, deleting a LAN profile.**

- *Stat bars* (`u3x_i02_bar`, new; C `i02_bar` today): the weapon pages draw bars (item type 0x2a: a base value
  and, when weapon mods change it, a second value). The PS4 game's drawing of that item type is wrong (a black
  fill, and the packed pair taken as one width). The builder replaces the start of that case in the game's item
  draw (image 0x645c96) by a stub that calls the helper (stub kind 222) and then jumps to the end of the item draw
  (image 0x645eea); the helper draws the whole bar as PS3 1.19 does. C `i_kind` (stays) calls it for kind 222 with
  the stub's register array (never null there).
- *The viewed-profile object* (`u3x_view_stats`, new; C `i05_stats` today; and `u3x_stats_defaults`, new; C
  `i05_defaults` today): the player card (page 0x83f24f04, "Statistics" of a party member) reads a whole profile
  object through a pointer that nothing in the PS4 game ever sets, so opening the card crashed. The helper owns
  such an object, installs it, shows the page and fills the object with the menu user's own record or with the
  record defaults. Called through the C export `u3h_view_stats` by the menu action handler `u3x_menu_action` (G32)
  for action 0x089143ea. `i05_defaults` is also called by C `j01_tick` (another group; stays in C).
- *Delete Profile* (`u3x_lan_delete_profile`, new; C `lan_delete_profile` today): the confirmation dialog and the
  removal of a LAN profile from the helper's own profile store, with its per-profile files. Called through the C
  export `u3h_lan_delete_profile` by `u3x_menu_action` for the actions 0x6d6ad418 (ask) and 0xda18d7d3 (yes).

Everything runs on the game thread that updates and draws the menus. There is no locking and no timing rule.

## 2. Interface

No function takes or returns a float. All are `extern "C"`. "existing" = exported today under this name, name and
signature stay. "new" = replaces a C body; the C function of the old name stays as a one-line wrapper.

### Part A: medals (about 160 old code lines)

| Function | Signature | Status | Called by |
|---|---|---|---|
| list source | `int u3x_medals_source(uintptr_t base, int32_t *event, void *s)` | existing (stray, see 7) | C `medals_source` |
| rebuild the list | `void u3x_medal_build(uintptr_t base, void *s)` | existing | `u3x_medals_source`; C `medal_build` |
| tab of the medals page | `uint32_t u3x_w04_tab(uintptr_t base, void *s)` | existing | part A; C `w04_tab` |
| a record of the medal table | `uint8_t *u3x_medal_record(void *s, uint32_t i)` | existing (stray) | part A; C `medal_record` |

`u3x_medal_list` (`void *u3x_medal_list(void *s)`, existing, stays as it is): returns `s` + 0x2800, the list state
of 3.2, and nothing else.

### Part B: Profile and Records rows, card tabs (about 130 old code lines)

| Function | Signature | Status | Called by |
|---|---|---|---|
| "new" marker answer | `int u3x_new_marker(int32_t *event, uint32_t flags)` | existing | C `profile_new_marker` |
| hand over the Profile list | `void u3x_retag_net_profile(uint8_t *page)` | existing | C `retag_net_profile` |
| Profile list source | `int u3x_net_profile_source(uintptr_t base, int32_t *ev, void *s)` | existing | C `netprofile_source` |
| Records rows in LAN play | `int u3x_n32_call(uintptr_t base, int32_t *ev, void *s, void (*native)(int32_t *))` | existing | `u3x_records_source` (G22), C `n32_call` |
| hand over the card lists | `void u3x_n32_retag(uint8_t *page)` | existing | C `n32_retag` |
| card tab bar and body | `int u3x_n32_source(uintptr_t base, int32_t *ev)` | existing | C `n32_source` (drops its state argument) |

`u3x_n32_mode` and `u3x_n32_records` (`int u3x_n32_mode(uintptr_t base)`, `int u3x_n32_records(uintptr_t base)`,
existing, stay as they are; one line each, see 4.8).

### Part C: stat bars, viewed profile, profile delete (about 65 old code lines)

| Function | Signature | Status | The C wrapper passes |
|---|---|---|---|
| stat bar | `int u3x_i02_bar(uintptr_t base, const uint64_t *r, uint32_t *counters)` | new (C `i02_bar`) | base, the stub's register array, the address of the C counter array `i02_d` (8 u32) |
| record defaults of the viewed profile | `void u3x_stats_defaults(uintptr_t base, void *s, uint8_t *object)` | new (C `i05_defaults`, a stray, see 7) | base, state, the object. The C function has no `base` parameter today: it gets one (both its callers have it) |
| open the player card | `void u3x_view_stats(uintptr_t base, void *s, uint8_t *object, uint32_t *counters)` | new (C `i05_stats`) | base, state, the address of the C part's own object `i05_vp` (0x7b00 bytes), the address of the C counter array `i05_d` (4 u32) |
| delete a LAN profile | `void u3x_lan_delete_profile(uintptr_t base, void *s, uint32_t action)` | new (C `lan_delete_profile`) | its three arguments unchanged |

### Parameters and results

- `base`: game image base. `s`: the helper's state object (opaque; fields in 3.2).
- `event`, `ev`: one data-source request (3.3).
- `i`: index of a record in the medal table. `flags`: the menu user's "new item" flags (a u32, 0 = nothing new).
- `page`: one page entry of the menu file (3.4), never null.
- `native`: the game's own Records source (image 0x76e460), a function of one argument (the request).
- `r`: the registers the stub of kind 222 saved, six u64 words (3.10).
- `counters` of `u3x_i02_bar`: d[0] bars drawn, d[1] base fills, d[2] second segments, d[3] the last packed value,
  d[4] the last element colour, d[5] bars drawn on page 0x35957e36, d[6] bars drawn on page 0x6d627b29, d[7] the
  last active page. Plain 32-bit increments and stores.
- `object`: the helper's own profile object, 0x7b00 bytes, zero at start (it lives in the C part because another
  group's C code compares pointers with it).
- `counters` of `u3x_view_stats`: v[0] pointer installed, v[1] own record shown, v[2] defaults shown, v[3] the
  page was shown with another object.
- `action`: 0x6d6ad418 or 0xda18d7d3.

Results: the three list sources return 1 when the request belongs to their list (tag match), else 0.
`u3x_new_marker` 1 when it answered, else 0. `u3x_n32_call` 1 when it handled the request, 0 when the caller must
pass it to the game's source. `u3x_i02_bar` always 1. `u3x_w04_tab` a tab's text id or 0. `u3x_medal_record` a
pointer or null.

### Functions the new code calls (declare them; do not reimplement them)

| Name | Signature | What it does | Part |
|---|---|---|---|
| `u3h_ext` | `uint8_t *u3h_ext(void)` | the helper's own state block (3.1). In CONVENTIONS.h, with `ext_ea()` / `ext_eb()` | all |
| `u3h_gate_closed` | `int u3h_gate_closed(uintptr_t base, int32_t *ev)` | 0 while multiplayer is active; otherwise stores the game's empty string as the request's text answer, counts, and returns 1 (G23). (The old medals source reached the same C function through a second export, `u3h_native_gate_closed`.) | A, B |
| `u3h_text` | `const char *u3h_text(uintptr_t base, void *s, uint32_t id)` | localized text of a text id; never null (G23) | A, B |
| `u3h_fmt` | `const char *u3h_fmt(void *s, const char *format, int64_t a0, const char *s0, const char *s1, const char *s2)` | the small formatter into a buffer of the C part (G24): "%d" becomes `a0` in plain decimal. Returns the buffer; valid until the next `u3h_fmt` call | A |
| `u3h_fmt_int` | `char *u3h_fmt_int(char *o, char *end, int64_t v, int32_t commas)` | writes `v` in decimal from `o` on, never at or beyond `end`: a leading "-" for a negative value, and with `commas` non-zero a comma between groups of three digits. Returns the position after the last character written; writes no NUL | A |
| `u3h_current_tab` | `uint32_t u3h_current_tab(uintptr_t base, void *s)` | text id of the tab shown on the active menu page; 0 when there is none (G26) | A |
| `u3x_sym_table` | `uint8_t *u3x_sym_table(void *s, uint32_t id)` | the data of a script symbol: one call of the state's lookup function (pointer at `s` + 656) with `id`; null when that pointer, the returned handle or the pointer the handle holds is null (G27). "Lookup of X" always means one such call, no caching | A, C |
| `u3h_player_profile` | `uint8_t *u3h_player_profile(uintptr_t base)` | pure read: the profile object of the menu user (G23 3.5), or null when the profile array pointer is null or the menu user is 4 or more | B, C |
| `u3h_show_dialog` | `void u3h_show_dialog(uintptr_t base, void *s, uint32_t body, uint32_t title, uint32_t action, int32_t buttons, uint32_t extra, uint32_t close)` | fills the game's message dialog record with these six words and opens the dialog page; does nothing when the dialog record or the show-page function is missing (G26) | B, C |
| `u3h_onb_mode` | `int u3h_onb_mode(void)` | pure read: the play mode word of the C part; 2 = offline mode | (inside `u3x_n32_mode`) |
| `u3h_active_page` | `uint32_t u3h_active_page(uintptr_t base)` | pure read: id of the menu page on top of the game's page stack; 0 when there is none (G26) | C |
| `u3h_show` | `int u3h_show(uintptr_t base, void *s, uint32_t page, int silent)` | shows a menu page by id through the state's show-page function (which it does not test for null). The page's activation runs inside this call. The result is not used here | C |
| `u3h_lg_rec` | `uint8_t *u3h_lg_rec(uintptr_t base, void *s, uint8_t *p)` | the multiplayer record of a profile object, `p` + 0x248, after the lazy default of G22 4.1. `p` must not be null | C |
| `u3h_i05_state` | `void *u3h_i05_state(void)` | address of the "whose card" mark in the C part (3.7) | C |
| `u3x_same_player` | `int u3x_same_player(const uint8_t *selected, const uint8_t *id)` | existing C++ function (stays), pure: 1 when `selected` is not empty and both texts are equal up to their NUL, at most 32 bytes compared; else 0 | C |
| `u3x_remove_name` | `void u3x_remove_name(uint8_t *names, uint32_t count, uint32_t index, uint32_t bytes)` | existing C++ function (stays): in an array of `count` names of `bytes` bytes each, the names after `index` move down one place and the last one is cleared | C |
| `u3_mp_path` | `unsigned u3_mp_path(char *out, uint32_t slot, int coop, int tmp)` | existing C function with external linkage, pure: writes the NUL-terminated path of the multiplayer file of profile slot `slot` (one decimal digit, `slot` modulo 10) into `out` (give it 64 bytes); `coop` non-zero = the co-op file; `tmp` 0 here | C |
| `u3h_profiles_save` | `int u3h_profiles_save(void *s, const void *store)` | **C export to add** (one line that calls the C function `save`): writes a LAN profile store (3.13, 0x210 bytes) to the helper's store file; 1 = written, 0 = failed | C |
| `u3h_ol_profile_deleted` | `void u3h_ol_profile_deleted(void *s, uint32_t index, uint32_t count)` | **C export to add** (one line that calls the C function `ol_profile_deleted`): the same slot shift for the per-profile file of the community server code (not part of this group) | C |
| `u3h_j01_filled` | `void u3h_j01_filled(int own)` | **C export to add** (one line that calls the C function `j01_filled`): tells the card keeper of another group what the viewed object now holds (non-zero = the player's own record) | C |

`u3h_ext`, `u3h_player_profile`, `u3h_active_page`, `u3h_i05_state`, `u3h_onb_mode`, `u3x_same_player` and
`u3_mp_path` have no side effect: how often and in which order they are called is not part of the behaviour. Every
other call of this table, every native and every game call is made exactly as often and in the order section 4
gives.

## 3. Game and helper facts

### 3.1 The helper's state block (address from `u3h_ext()`)

"ea" starts at offset 0x3c50 (`ext_ea()`), "eb" at 0x4050 (`ext_eb()`), "ec" at 0x4450 (no accessor in
CONVENTIONS.h: `u3h_ext()` + 0x4450 + 4·n); u32 words.

| Word | Meaning |
|---|---|
| ea[0] bit 1 (value 2) | set = Delete Profile is off |
| ea[0] bit 6 (value 0x40) | set = the two extra Profile rows are off (the list is not handed over) |
| ea[8] | counter: delete confirmations shown |
| ea[9] | counter: profiles deleted |
| ea[10] | the list row of the last deleted profile (its index + 1) |
| ea[24] | counter: Profile lists handed over |
| ea[25] | counter: requests for row 10 or 11 |
| ea[26] | counter: toggles of row 10 or 11 |
| ea[122] | switches: bit 0 = keep the three LAN Records rows; bit 1 = leave the player card alone; bit 2 = both only in offline mode |
| ea[125] | low 16 bits: Records count answers; from bit 16: clicks on Records rows 0 and 1 |
| ea[127] | low 24 bits: card list calls run with the LAN byte cleared; from bit 24: card lists handed over |
| eb[1] bit 19 (value 0x80000) | set = the viewed-profile object is off (the page is shown and nothing else) |
| eb[3] bit 27 (value 0x08000000) | set = old medals behaviour: the tab of the active page, no medal replacement |
| ec[250] | counter: "earned" texts |
| ec[251] | counter: points texts |
| ec[252] | counter: value queries of the medals list |
| ec[253] | the last value id asked of the medals list |

Counters are plain 32-bit increments (wrapping) unless section 4 says otherwise.

### 3.2 The state object `s`

| Offset | Type | Meaning |
|---|---|---|
| 32 | pointer to function | platform "open": (const char *path, int flags, int mode) → s64 file handle, negative = failed |
| 56 | pointer to function | platform "close": (int handle) → s64 |
| 64 | pointer to function | platform "rename": (const char *from, const char *to) → s64, 0 = done |
| 0x50 | u32 | the helper's error word; a failed store write puts 4 here |
| 0x78 | 0x210 bytes | the LAN profile store (3.13) |
| 648 | pointer to function | show-page function (used through `u3h_show` / `u3h_show_dialog`) |
| 656 | pointer to function | script symbol lookup (used through `u3x_sym_table`) |
| 768 + 8·k | pointer to function | "native k" (G23 3.2). Any of them may be null |
| 0x2800 | 0x210 bytes | the medal list state, owned by part A |
| 0x2b00 | at least 231 bytes | a text buffer shared with the treasures list (G27 3.2); the points line is composed here |

Natives used here (the game function each normally holds is given for orientation only; always call through the
pointer in the state object):

| k | Arguments | Result | Game function |
|---|---|---|---|
| 13 | (u32 symbol id) | pointer to the symbol's value, or null | 0x11f2e30 |
| 19 | (image 0x17bb878, pointer to a medal record, u32 user) | u32: how often the user earned the medal | 0x5ada60 |
| 20 | (image 0x1b31128, pointer to a NUL-terminated texture name) | a texture object or null; a miss queues the load (G22 3.2) | 0x1189430 |

The medal list state (offsets from `s` + 0x2800):

| Offset | Type | Meaning |
|---|---|---|
| 0 | 256 × u16 | the rows: for each row, the index of its medal in the medal table |
| 0x200 | u16 | number of rows |
| 0x202 | u16 | not used (never written) |
| 0x204 | u32 | the selected row |
| 0x208 | u32 | the tab the rows were built for (a tab's text id, or 0) |
| 0x20c | u32 | 1 once the rows have been built |

The layout is kept (the old-versus-new test compares the state object). The state starts as zeros.

### 3.3 Data-source requests

As G23 3.3. The fields this group uses:

| Field | Meaning |
|---|---|
| word 0 | operation |
| word 1 | row index (in, operations 2, 4, 10); the answer of operations 5, 6 and 0x15 (out) |
| words 2, 4 .. 12 | image answer; pointer at byte 0x10 (words 4 and 5) = the texture of a texture answer |
| word 0xe | the answer of a value query (operation 11) |
| word 0x13 | the id of the value asked (operation 11) |
| word 0x14 | the image slot asked (operation 4) |
| word 0x15 | the text slot asked (operation 2) |
| word 0x17 | the row of operations 0x15 and 0x16 |
| pointer at byte 0x40 | the text answer |
| pointer at byte 0x98 | the source descriptor; its u32 at +0x1c is the source tag |

Operations: 2 row text; 4 row picture; 5 selected row; 6 last row index (number of rows − 1); 9 refresh; 10 select
a row; 11 value query; 0x15 "which text id does row (word 0x17) have" (answer in word 1); 0x16 click on row (word
0x17).

Source tags: 0x4c414ed3 the medals list; 0x4c414ec6 the Network Profile list; 0x4c414ec7 the Records list (G22);
0x4e4e32c3 the player card's tab bar; 0x4e4e32de the player card's body.

"Text answer = T" means: the pointer at byte 0x40 of the request becomes T.

### 3.4 Menu pages and their items (the hand-over)

A page entry of the menu file: u32 at +8 = page id; pointer at +0x48 = the page's item list (may be null). An item
list: u32 at +0 = number of items; pointer at +8 = the items (may be null). An item is 0x68 bytes:

| Offset | Type | Meaning |
|---|---|---|
| 8 | u32 | item id |
| 0x30 | u32 | item kind; 21 = a list |
| 0x34 | u32 | the number of the list's data source |
| 0x38 | u32 | a parameter of the source |
| 0x40 | u32 | the source tag; 0 = none |

Source number 0xcf is the one the game routes to the helper. "Handing a list over" means: its source number
becomes 0xcf and its tag a value by which the helper's sources recognise it (3.3). A page with more than 64 items
is never touched.

Source numbers of the game: 0xc6 the Network Profile list (function 0x76d2d0), 0xc3 a tab bar (function
0x76bbf0), 0xde the player card body (function 0x76fa00).

### 3.5 Script tables (read at run time; nothing is copied or kept)

**The medal table**, symbol 0x980d6543, reached through native 13 (not through `u3x_sym_table`): u32 number of
records at +0; pointer to the records at +8. "Lookup of the medal table" = one call of native 13 with 0x980d6543;
a null native 13 counts as a null table (no call). A medal record is 0x48 bytes; its position in the table is the
medal's index:

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | text id of the medal's name (it identifies the medal) |
| 4 | u32 | text id of the description |
| 0x10 | u32 | class bits: which tabs show the medal |
| 0x18 | pointer | the name of the medal's picture, a NUL-terminated texture path (may be null) |
| 0x20 | s32 | points value |

**The tab table**, symbol 0xf1514340 (as G27 3.4): u32 number of rows at +0; pointer to the rows at +8. A row is
0x20 bytes: +0 u32 page id, +4 u32 LAN value (0 or 1), +0x10 pointer to the tabs, +0x18 s32 index of the selected
tab. A tab is 0xc bytes: +0 u32 text id of the tab.

**Symbol 0xc27df04c** (as G22 4.1): a value copied into a fresh record; its data starts with that u32.

### 3.6 The Records rows (game data)

Image 0x13a6620: the game's online Records rows, 5 rows of 4 u32: +0 text id of the row's title, +4 page id (0 =
none), +8 action id (0 = none), +0xc "new" mask. Image 0x13a65f0: the three LAN rows, same layout (the game's own
source reads them; this group does not). Both are read by the game and by G22; this group reads rows 0 and 1 of
the online table at run time.

### 3.7 Profile objects, the viewed profile, the "whose card" mark

Profile objects and "menu user": G23 3.5. A profile object is 0x7b00 bytes. Fields used here:

| Offset | Type | Meaning |
|---|---|---|
| 0x248 | 0x2828 bytes | the multiplayer record (G22 3.5); u32 at 0x248 = format version, 0x56 |
| 0x74c | u32 | record + 0x504 (G22 3.5) |
| 0xac8 | u32 | the "DLC" setting of row 11; 0 = on |
| 0x215c | u32 | the "Hat Stats" setting of row 10; 0 = on |
| 0x7ac0 | text | the player's id (name), NUL-terminated |
| 0x7ae8 | u32 | load state; 3 = loaded |

The pointer at image 0x17c3568 is "the viewed profile": the profile object the player card shows. The PS4 game
only reads it. When the card page is activated the game clears some state fields of that object (among them the
load state) and copies the selected player's id into its +0x7ac0; this happens inside the show call.

The "whose card" mark (`u3h_i05_state()`), 0x25 bytes of the C part: u8 at +0 = who the party list click meant (1
= the player's own row, 2 = another member, 0 = no click); 0x24 bytes from +1 = the selected player id as it was at
that click. It is written by the party list (another group).

### 3.8 Game data

| Image offset | Type | Meaning |
|---|---|---|
| 0x17d5c60 | u8 | LAN byte: non-zero = LAN / not online |
| 0x17d5c61 | u8 | multiplayer-active byte |
| 0x17bd080 | object | the menu (first argument of the two menu functions of 3.9) |
| 0x17c3568 | pointer | the viewed profile (3.7) |
| 0x17c3871 | 0x24 bytes | the id (name) of the player selected in a player list, NUL-terminated |
| 0x17d6010 | text | this machine's own player id (name) |
| 0x17bb878 | object | medal statistics (first argument of native 19) |
| 0x1b31128 | object | texture dictionary (first argument of native 20) |
| 0x1b6a400 | u32 | the selected row of the LAN profile list: 0 = none; k = the profile with index k − 1 |
| 0x13a6620 | 5 × 16 bytes | the online Records rows (3.6) |
| 0x662812 | 8 × 12 bytes | the record default words, inside the game's code (3.12) |

### 3.9 Game functions called directly

| Image offset | Call | Meaning |
|---|---|---|
| 0x76d2d0 | (s32 *request) | the game's own source of the Network Profile list (10 rows) |
| 0x76bbf0 | (s32 *request) | the game's tab bar source |
| 0x76fa00 | (s32 *request) | the game's player card body source |
| 0x64ce90 | (menu, u32 action id, s32 0) | sends a menu button action |
| 0x651940 | (menu, u32 page id, s32 0, s32 0) | shows a menu page |
| 0x5b9580 | (float out[4], u32 colour id), plus one float in xmm0 | writes the named UI colour as four floats (red, green, blue, alpha) to `out`; the alpha is the colour's own alpha multiplied by the float in xmm0 |
| 0x647470 | (a0, a1, const float rect[4], const float colour[4]) | draws a filled rectangle: rect = x, y, width, height; colour = red, green, blue, alpha. The game copies both during the call. `colour` must be 16-byte aligned (the game reads it with an aligned vector load); give `rect` the same alignment |

No result of these calls is used.

### 3.10 The stat bar site (stub kind 222)

The stub sits in the game's item draw (function 0x641560; arguments: laid-out item, a second pointer, a draw
context, a position pair, a colour, a flag, and one float in xmm0) at the start of its case for item type 0x2a. It
saves six registers and passes them as `r`:

| Word | Register | Meaning |
|---|---|---|
| r[0] | rbp | the item draw's frame pointer "F" |
| r[1] | r10 | the laid-out item "I" |
| r[2] | rbx | red of the element colour in bits 16 .. 23 |
| r[3] | r12 | blue in bits 0 .. 7 |
| r[4] | r13 | alpha in bits 8 .. 15 |
| r[5] | r14 | green in bits 8 .. 15 |

(The element colour is the item's own colour modulated by the colour argument of the item draw.)

Fields: four floats at F − 0x1eb0 = the item's rectangle (x, y, width, height); pointer at F − 0x2060 = "a0" and
pointer at F − 0x2070 = "a1", the two leading arguments of the rectangle draw; u32 at I + 0x10 = the packed value:
low 16 bits the base value, upper 16 bits the value with mods (0 = none).

This stub does not save or change any float or vector register, and nothing between the item draw's entry and the
helper writes xmm0 (checked in the PS4 image: no instruction on the path from 0x641560 to 0x645c96 writes it, and
the C part executes no vector instruction). So at entry of `u3x_i02_bar` xmm0 still holds the float argument of
the item draw. The game's item draw has two direct callers (image 0x63d8ae and 0x63d949, both in the function at
image 0x63d6f0): the first passes 0.0, the second 0.85 (pattern 0x3f59999a) when the mean of the red, green and
blue of the colour it draws with is above 0.25, else 0.0. (The helper's own calls of the item draw, G22 and G23,
never carry an item of type 0x2a.)

### 3.11 Ids, texts, constants

Page ids: 0xc98ee52a the medals page; 0xe0c0028b the Network Profile page; 0x83f24f04 the player card;
0x35957e36 and 0x6d627b29 two weapon pages (only counted, 4.12).

Medals (part A):

- Tabs of the medals page, by the tab's text id, and the class bit a medal must have to be shown: 0x091026d1 bit 1
  (value 2); 0xfb0dd522 bit 2 (value 4); 0xa13b68fb bit 3 (value 8); 0xadbd5585 bit 4 (value 0x10). Every other
  tab id (also 0) shows nothing.
- The replaced medal: 0xa8e90238 = the old record ("O"), 0x9befafde = its PS3 1.19 replacement ("N"), both by the
  record's name id.
- Text slots (word 0x15): 0 name; 0x80d4cc7b description; 0xadacdbf3 "earned N"; 0xb684d68b points.
- Image slot (word 0x14): 0xf5a294f3 the medal's picture.
- Value id (word 0x13): 0x67a48eb9, answered with 4.
- Text ids: 0xe9a0ddd7 the "earned" template (it holds a "%d"); 0xb4b0d5c2 the points label.
- A text of the helper's own (read-only helper data): the 14 characters " |@Cff7cce96|$" (a space, a colour
  escape, a dollar sign).
- Float patterns stored in image answers: 0x3f800000 (1.0), 0x40100000 (2.25).

Profile and Records rows (part B):

- Network Profile list: item id 0xc073ebb7. Row 10 title 0x2e153e84 ("Hat Stats"), row 11 title 0x9811415d
  ("DLC"). Setting texts: 0xd1831446 ("On"), 0x32066ec9 ("Off").
- Records: the Message of the Day row has title 0xa6694384; its dialog is `u3h_show_dialog(base, s, 0x60edaa4f,
  0xc6467f2d, 0, 0, 0, 0xe1e040aa)`.
- Player card: tab bar = a list with source number 0xc3 and source parameter 0x83f24f04; body = a list with source
  number 0xde.
- "New" marker: item type 0x2f.

Part C:

- Stat bar: track colour id 0xb9f73e49. Float constants: 1/255 = 0x3b808081 (0.003921569); 2.0 = 0x40000000; 4.0 =
  0x40800000; 0.01 = 0x3c23d70a; 1.0 = 0x3f800000; 0.8 = 0x3f4ccccd; 0.4 = 0x3ecccccd; 0.0.
- Delete Profile: the confirmation is `u3h_show_dialog(base, s, 0x1e01f666, 0x0260a263, 0xda18d7d3, 0x075bc82d,
  0xf99fbf09, 0)` (question, "Yes", the yes action, "No", the no action).
- File open flags 0x601 (write only, create, truncate), mode 0x180 (octal 600).

### 3.12 Tables of game values: where the running game holds them

**Record defaults (8 words).** The old code reached a literal copy of the eight default words of a fresh
multiplayer record (inside the old function `u3x_record_defaults`). The new code must not contain them: it reads
them from the running game exactly as G22 3.11 (a) says: eight consecutive 12-byte entries from image offset
0x662812, entry k at 0x662812 + 12·k: 4 instruction bytes, then an s32 = the destination offset **from the profile
object**, then a u32 = the value. (Unaligned 32-bit reads. Checked for this group against the PS4 image: the eight
entries are the old code's eight words at the old code's offsets.) Used by 4.13 only.

**The Records rows**: game data at image 0x13a6620, read at run time (3.6).

**Medal records, tabs**: script data read at run time (3.5).

There is no other literal table of game values in this group. The remaining literals are single ids (3.11). Two
notes on them:

- The four medal tab ids with their class bits are also in the PS4 image, as immediates of the game's own
  "show this medal" function (image 0x759800): the tab ids as unaligned u32 at image 0x759876, 0x759888, 0x75989a
  and 0x7598a7, in the order of 3.11. A cross-check, not needed by the new code.
- The title id of row 10 (0x2e153e84), the Message of the Day ids (0xa6694384, 0x60edaa4f), the delete question
  (0x1e01f666), the yes / no actions of the delete (0xda18d7d3, 0xf99fbf09) and the two medal ids are PS3 1.19
  values that occur nowhere in the PS4 image; no rule that generates them is known.

### 3.13 The helper's own files (not game formats)

**The LAN profile store** (state object + 0x78, also the content of the helper's store file), 0x210 bytes:

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | magic |
| 4 | u32 | version |
| 8 | u32 | number of profiles, 0 .. 8 |
| 0xc | u32 | the selected profile |
| 0x10 | 8 × 64 bytes | the profile names, NUL-terminated |

**Per-profile files.** The profile with index k (from 0) is "slot" k + 1. Each slot has a multiplayer file and a
co-op multiplayer file, whose paths `u3_mp_path(out, slot, 0, 0)` and `u3_mp_path(out, slot, 1, 0)` give. An empty
(zero-length) file counts as "no file". There is no "remove file" function available: emptying stands in for it.

"Empty the file of slot n" below means: build its path, call the platform open (3.2) with (path, 0x601, 0x180),
and when the result is 0 or more call the platform close with that result as a 32-bit handle.

## 4. Behaviour

Shared wording: "the list" in part A = the medal list state of 3.2 at `s` + 0x2800 (rows, count, selected, tab,
built). "Decimal": plain digits.

**Part A: medals**

### 4.1 `u3x_medal_record(s, i)`

One lookup of the medal table (3.5), made in every case in which native 13 exists. Null when the table is null or
`i` is not below the table's record count (u32). Otherwise the address of record `i`: records pointer + 0x48·`i`
(the records pointer is not tested). No memory is written.

### 4.2 `u3x_w04_tab(base, s)`

- Bit 27 of eb[3] set: the result is `u3h_current_tab(base, s)`; nothing else.
- Otherwise: one lookup of the tab table (`u3x_sym_table(s, 0xf1514340)`). Null table: 0. The rows are examined
  in index order, at most the first 1024 of them, and not at all when the rows pointer is null. The first row
  whose page id is 0xc98ee52a and whose LAN value (u32) equals the LAN byte (image 0x17d5c60, as a number 0 .. 255)
  decides: when its tabs pointer is not null and its selected index (s32) is 0 or more, the result is the u32 at
  tabs pointer + 0xc·index (the selected tab's text id; the index is not compared with a count); otherwise 0.
  Later rows are not examined. No such row: 0.

No memory is written.

### 4.3 `u3x_medal_build(base, s)`

Steps, in this order:

1. tab = `u3x_w04_tab(base, s)`; mask = the class bit of that tab (3.11), 0 for a tab that has none.
2. The list's tab = tab; built = 1; count = 0. (The selected row and the row words are not touched here.)
3. One lookup of the medal table. Null table, or mask 0: done (the selected row stays what it was).
4. n = the table's record count (u32).
5. The positions of the replaced medal: both "none" when bit 27 of eb[3] is set (then no record is looked at).
   Otherwise every index 0 .. n − 1 is asked once, in order, with `u3x_medal_record(s, index)` (one table lookup
   each; a null record is skipped): P_O = the first index whose name id is O (0xa8e90238), P_N = the first index
   whose name id is N (0x9befafde). When either was not found, or P_N is below P_O, both are "none".
6. The rows: for index = 0, 1, .. n − 1, as long as the count is below 256:
   - index = P_N: nothing (no lookup); the replacement is shown where the old medal is.
   - otherwise shown = P_N when index = P_O, else index. R = `u3x_medal_record(s, shown)` (one table lookup). When
     R is not null and (R's class bits AND mask) = mask: the row word at position count becomes shown (cut to 16
     bits) and the count goes up by 1.
   Row words beyond the new count keep their old values.
7. When the selected row (u32) is not below the count: selected = 0.

### 4.4 `u3x_medals_source(base, event, s)`

Steps, in this order:

1. Source descriptor (pointer at byte 0x98) null, or its tag not 0x4c414ed3: return 0, nothing touched.
2. `u3h_gate_closed(base, event)` non-zero: return 1.
3. When built is 0: `u3x_medal_build(base, s)` (and `u3x_w04_tab` is not called by this step itself). Otherwise
   t = `u3x_w04_tab(base, s)`, and when t differs from the list's tab: `u3x_medal_build(base, s)`. This is done
   for every operation.
4. row = word 1 as u32. "The row's record" = null when row is not below the count, else
   `u3x_medal_record(s, the row word of row)` (one table lookup each time it is asked for).
5. By operation (operations not listed: nothing). The result is 1 in every case.

| Operation | Effect |
|---|---|
| 2 | the row's record; when it is not null: the row text, see below |
| 10 | selected = row (any value, stored as it is); then exactly as operation 2 |
| 4 | when row is below the count and word 0x14 is 0xf5a294f3: the row's record; when it is not null: the row picture, see below. Otherwise nothing |
| 5 | word 1 = the selected row |
| 6 | word 1 = count − 1 (−1 for an empty list) |
| 9 | `u3x_medal_build(base, s)` (a second build when step 3 has just built) |
| 11 | ec[252] += 1; ec[253] = word 0x13; when that is 0x67a48eb9: word 0xe = 4 |

**Row text** of record R, by text slot (word 0x15):

| Text slot | Effect |
|---|---|
| 0 | text answer = `u3h_text(base, s, R's name id)` |
| 0x80d4cc7b | text answer = `u3h_text(base, s, R's description id)` |
| 0xadacdbf3 | ec[250] += 1. times = 0 when native 19 is null; otherwise native 19(image 0x17bb878, R, menu user), a u32 taken as an unsigned number. Then T = `u3h_text(base, s, 0xe9a0ddd7)`, then text answer = `u3h_fmt(s, T, times, null, null, null)`. (The menu user is not limited to 0 .. 3 here.) |
| 0xb684d68b | ec[251] += 1. The points line, see below; text answer = `s` + 0x2b00 |
| other | nothing |

**The points line.** First the number: R's points value (s32 at +0x20) as text with thousands separators,
`u3h_fmt_int(buffer, buffer + 23, value, 1)` into a local buffer of 24 characters, NUL-terminated at the returned
position. Then L = `u3h_text(base, s, 0xb4b0d5c2)`. The buffer `s` + 0x2b00 receives L, then the helper's own
text " |@Cff7cce96|$", then the number, cut after 120 characters in all, and a NUL (so at most 121 bytes are
written).

**Row picture** of record R:

1. The picture's name = the pointer at R + 0x18. It is usable when it is not null, its first 48 characters hold a
   NUL, every character before that NUL is printable ASCII (0x20 .. 0x7e), at least one of them is "/", and it is
   not empty. (So: 1 to 47 printable characters with a slash.) The pointer itself is not checked for validity.
2. texture = native 20(image 0x1b31128, name) when native 20 is not null and the name is usable; otherwise null
   (no call).
3. The answer: words 4 to 11 = 0, then words 9 and 11 = −1. With a texture: word 2 = 2, the pointer at byte 0x10 =
   the texture, word 12 = 0x3f800000. Without one: word 2 = 0x10 (the game's placeholder picture), word 12 =
   0x40100000. Word 3 is left as it is.

**Part B: Profile and Records rows, card tabs**

### 4.5 `u3x_new_marker(event, flags)`

`flags` = 0: return 0, nothing touched. Otherwise the request becomes the game's "new" marker answer: word 2 =
0x2f; words 4, 5, 6, 7, 8 and 10 = 0; words 9 and 11 = −1; word 12 = 0x3f800000 (1.0); word 3 is left as it is.
Return 1.

### 4.6 `u3x_retag_net_profile(page)`

Nothing when bit 6 of ea[0] is set, when the page id is not 0xe0c0028b, when the page has no item list, when the
item list has no items pointer, or when it has more than 64 items. Otherwise every item, in order, whose item id is
0xc073ebb7, whose kind is 21, whose source number is 0xc6 and whose tag is 0 is handed over (3.4) with the tag
0x4c414ec6, and ea[24] += 1 for each.

### 4.7 `u3x_net_profile_source(base, ev, s)`

1. Source descriptor null, or its tag not 0x4c414ec6: return 0, nothing touched.
2. `u3h_gate_closed(base, ev)` non-zero: return 1.
3. op = word 0. The row asked: word 0x17 for operations 0x15 and 0x16; word 1 for operations 2 and 4; none for
   every other operation.
4. **Row 10 or 11** (return 1 in every case): P = the menu user's profile object (`u3h_player_profile(base)`); the
   row's setting = the u32 at P + 0x215c (row 10) or P + 0xac8 (row 11); without P there is no setting. ea[25] +=
   1. Then:
   - operation 2, text slot (word 0x15) 0: text answer = `u3h_text(base, s, the row's title id)` (3.11);
   - operation 2, text slot 1, with a setting: text answer = `u3h_text(base, s, 0xd1831446)` when the setting is
     0, else `u3h_text(base, s, 0x32066ec9)`;
   - operation 0x15: word 1 = the row's title id;
   - operation 0x16, with a setting: the setting becomes 1 when it was 0, and 0 otherwise; ea[26] += 1;
   - anything else (operation 4, other text slots, no setting): nothing more.
5. **Every other request**: game function 0x76d2d0(`ev`). Afterwards, when op (as read in step 3) is 6 and word 1
   now holds 9: word 1 = 11 (twelve rows). Return 1.

### 4.8 `u3x_n32_mode` and `u3x_n32_records` (existing, stay)

For orientation and for test stand-ins. `u3x_n32_mode(base)`: non-zero when the LAN byte (image 0x17d5c60) and the
multiplayer-active byte (image 0x17d5c61) are both non-zero and it is not the case that bit 2 of ea[122] is set
while `u3h_onb_mode()` is not 2. `u3x_n32_records(base)`: non-zero when bit 0 of ea[122] is clear and
`u3x_n32_mode(base)` is non-zero. Both are pure reads.

### 4.9 `u3x_n32_call(base, ev, s, native)`

The caller (G22 4.8) calls it only while `u3x_n32_records` holds. The list then has six rows: 0 and 1 = rows 0 and
1 of the game's online table (3.6); 2 and 3 = the game's LAN rows 0 and 1; 4 = Message of the Day; 5 = the game's
LAN row 2 (Legacy). `native` answers with the LAN rows, because the LAN byte is set.

- **Operation 6.** `native(ev)`; then, when word 1 is 0 or more (signed), word 1 += 3. The low 16 bits of ea[125]
  go up by 1 without carry into the upper 16 bits. Return 1.
- **Operations 2 and 4** take the row from word 1, **operations 0x15 and 0x16** from word 0x17. **Every other
  operation**: return 0, nothing touched.
- Row 5, 2 or 3: the row word (word 1 or word 0x17, the one the row came from) becomes 2 for row 5, 0 for row 2, 1
  for row 3; `native(ev)`; then the row word gets its original value back. Return 1.
- Row 4 (return 1): operation 2 with text slot (word 0x15) 0: text answer = `u3h_text(base, s, 0xa6694384)`.
  Operation 0x15: word 1 = 0xa6694384. Operation 0x16: the dialog of 3.11. Anything else: nothing.
- A negative row, or a row of 6 or more: return 1, nothing.
- Row 0 or 1 (return 1), with R = the row's entry in the online table at image 0x13a6620 (3.6): operation 2 with
  text slot 0: text answer = `u3h_text(base, s, R's title id)`. Operation 0x15: word 1 = R's title id. Operation
  0x16: when R's action id is not 0, game function 0x64ce90(image 0x17bd080, the action id, 0); then, when R's
  page id is not 0, game function 0x651940(image 0x17bd080, the page id, 0, 0); then ea[125] += 0x10000. Operation
  4 and other text slots: nothing.

### 4.10 `u3x_n32_retag(page)`

Nothing when bit 1 of ea[122] is set, when the page id is not 0x83f24f04, when the page has no item list, when the
item list has no items pointer, or when it has more than 64 items. Otherwise every item, in order, whose kind is 21
and whose tag is 0 is looked at: with source number 0xc3 and source parameter 0x83f24f04 it is handed over (3.4)
with the tag 0x4e4e32c3; with source number 0xde (any parameter) with the tag 0x4e4e32de; every other item stays.
ea[127] += 0x1000000 for each item handed over.

### 4.11 `u3x_n32_source(base, ev)`

1. Source descriptor null, or its tag neither 0x4e4e32c3 nor 0x4e4e32de: return 0, nothing touched.
2. `u3h_gate_closed(base, ev)` non-zero: return 1.
3. K = the LAN byte (image 0x17d5c60) as it is now.
4. When bit 1 of ea[122] is clear and `u3x_n32_mode(base)` is non-zero: the LAN byte = 0, and the low 24 bits of
   ea[127] go up by 1 without carry into the upper 8 bits. (With bit 1 set `u3x_n32_mode` is not asked.)
5. Game function 0x76bbf0(`ev`) for the tag 0x4e4e32c3, 0x76fa00(`ev`) for the tag 0x4e4e32de.
6. The LAN byte = K (written back in every case). Return 1.

**Part C: stat bars, viewed profile, profile delete**

### 4.12 `u3x_i02_bar(base, r, counters)`

F = r[0] and I = r[1] as pointers. Either null: return 1, nothing else. Otherwise, with the names of 3.10 and
d[n] = `counters[n]`:

1. **Element colour.** R = bits 16 .. 23 of r[2]; B = bits 0 .. 7 of r[3]; A = bits 8 .. 15 of r[4]; G = bits 8 ..
   15 of r[5] (four numbers 0 .. 255). The colour C = (R/255, G/255, B/255, A/255), each computed as the number
   converted to float and multiplied by 0x3b808081 (a multiplication, not a division), rounded to single precision.
2. **Track colour.** A buffer T of four floats, all 0.0 before the call, then game function 0x5b9580(T,
   0xb9f73e49). **The float register is not set for this call.** The old code computed A/255 for it, but the helper
   is built without vector instructions and its compiler passed that float on the stack, where the game function
   does not look. The function therefore took its alpha factor from xmm0 as the game left it (3.10). To do the
   same, the new code makes this a plain call with two integer arguments and executes no instruction that writes a
   vector register (no `f_*` helper, no `GameCall` block) before it; the colour arithmetic of step 1 is integer
   work plus `f_*` operations that can all be done after this call. See 6 item 14 and 7 item 1.
3. **Track.** Game function 0x647470(a0, a1, Q, T) with Q = a copy of the four rectangle floats at F − 0x1eb0. (a0
   = pointer at F − 0x2060, a1 = pointer at F − 0x2070. The frame is only read, never written. The game calls of
   this function do not change the rectangle in the frame, so it does not matter when it is read.)
4. **Inner rectangle.** x′ = x + 2.0, y′ = y + 2.0, w′ = width − 4.0, h′ = height − 4.0, each one single-precision
   operation.
5. v = the u32 at I + 0x10; lo = its low 16 bits; hi = its upper 16 bits. d[0] += 1; d[3] = v; d[4] = A·2^24 +
   R·2^16 + G·2^8 + B. page = `u3h_active_page(base)`; d[7] = page; when page is 0x35957e36: d[5] += 1; when it is
   0x6d627b29: d[6] += 1. (All of this also when lo is 0.)
6. lo = 0: return 1.
7. **Base fill.** k = w′ · 0.01. bw = (lo as float) · k; when bw is below 1.0, bw = 1.0 (a NaN stays). Game
   function 0x647470(a0, a1, (x′, y′, bw, h′), C). d[1] += 1.
8. hi = 0: return 1.
9. **Second segment.** seg = ((hi − lo) as a signed number, converted to float) · k. Only when seg is above 0.0
   (false for a NaN): game function 0x647470(a0, a1, (x′ + bw, y′, seg, h′), (C.red · 0.8, C.green · 0.8, C.blue ·
   0.4, C.alpha)), then d[2] += 1.
10. Return 1.

Float rule (PIPELINE.md): every operation above is one single-precision operation on 32-bit patterns with the
`f_*` helpers, in the grouping written: `f_mul(f_from_int(n), 0x3b808081)` for a colour part; `f_add` / `f_sub` for
the inner rectangle; k = `f_mul(w′, 0x3c23d70a)`; bw = `f_mul(f_from_int(lo), k)`; the test `f_less(bw, 1.0)`; seg =
`f_mul(f_from_int(hi − lo), k)`; the test `f_less(0.0, seg)`; x′ + bw = `f_add`; the three colour products =
`f_mul`. Where the old build kept extended precision: only in the base width, which it computed as lo · (w′ · 0.01)
with the inner product unrounded and one rounding at the end (the second segment used the rounded k). All other
old results were rounded per operation, as here. The base width can therefore differ from the shipped helper in
its last bit: the accepted deviation of the float rule.

The rectangle and colour buffers passed to 0x647470 are four floats each, 16-byte aligned (3.9).

### 4.13 `u3x_stats_defaults(base, s, object)`

Makes the multiplayer record inside `object` a fresh one: the same writes as the record reset of G22 4.1, without
its load-state write and without its conditions.

- The 0x2824 bytes from `object` + 0x24c are cleared.
- `u3x_sym_table(s, 0xc27df04c)` is called exactly once; when it returns data, the u32 at `object` + 0x74c becomes
  the first u32 of that data (otherwise it stays 0 from the clear).
- The u32 at `object` + 0x248 = 0x56.
- The eight default words of 3.12 are stored: for each of the eight entries, the u32 at `object` + (the entry's
  offset) = the entry's value.

The final memory is what counts; the clear comes before the other stores. Nothing else of the object is written
(not the load state at +0x7ae8).

### 4.14 `u3x_view_stats(base, s, object, counters)`

V = the pointer at image 0x17c3568 (the viewed profile, 3.7); v[n] = `counters[n]`; M = the mark of 3.7.

1. Bit 19 of eb[1] set: `u3h_show(base, s, 0x83f24f04, 0)` and nothing else.
2. When V is null: V = `object` (stored at image 0x17c3568); v[0] += 1. A non-null V is left as it is.
3. who = the u8 at M + 0; then that byte = 0. When the 0x24 bytes at image 0x17c3871 are not all equal to the 0x24
   bytes at M + 1 (the selection changed since the click): who = 0.
4. `u3h_show(base, s, 0x83f24f04, 0)`. (The page's activation runs inside and resets fields of the viewed object.)
5. V is read again. When it is not `object`: v[3] += 1 and nothing else happens (no fill, no `u3h_j01_filled`).
6. P = `u3h_player_profile(base)`; sel = image 0x17c3871. "Own" holds when who is 1; or when who is 0 and either
   `u3x_same_player(sel, image 0x17d6010)` is non-zero, or P is not null and `u3x_same_player(sel, P + 0x7ac0)` is
   non-zero. (Every other value of who, 2 in practice: not own.)
7. Own and P not null: the 0x2828 bytes from `u3h_lg_rec(base, s, P)` are copied to `object` + 0x248; v[1] += 1.
   Otherwise: `u3x_stats_defaults(base, s, object)`; v[2] += 1.
8. The u32 at `object` + 0x7ae8 = 3 (loaded).
9. `u3h_j01_filled(1)` when the own record was copied (own and P not null), else `u3h_j01_filled(0)`.

### 4.15 `u3x_lan_delete_profile(base, s, action)`

G = the u32 at image 0x1b6a400 (3.8); "store" = the LAN profile store at `s` + 0x78 (3.13).

1. Bit 1 of ea[0] set: nothing at all.
2. `action` = 0x6d6ad418 (ask): when G is not 0, the confirmation dialog of 3.11 and ea[8] += 1. Nothing else in
   either case (G is not changed).
3. Every other `action` (the caller passes only 0xda18d7d3, "yes"): G = 0: nothing. Otherwise index = G − 1.
4. When index is below the store's profile count:
   1. A working copy of the whole store (0x210 bytes) is made. In the copy: `u3x_remove_name(the copy's names,
      the count, index, 64)`; then the count goes down by 1 (call it N); the selected profile = 0.
   2. `u3h_profiles_save(s, the copy)`. Result 0: the error word (u32 at `s` + 0x50) = 4 and the function ends
      here: the store in the state object is unchanged, G keeps its value, no file is touched.
   3. The store in the state object = the copy.
   4. For the multiplayer files first, then for the co-op files (3.13): for slot = index + 1, index + 2, .. N in
      this order: empty the file of slot (3.13), then the platform rename (3.2) with (the path of slot + 1, the
      path of slot); results are not looked at. After that loop: empty the file of slot N + 1.
   5. `u3h_ol_profile_deleted(s, index, N)`.
   6. ea[9] += 1; ea[10] = G's value (index + 1).
5. G = 0 (also when index was not below the count).

## 5. Test scenarios

Parts A and B and most of part C can be tested with a fake image (the fields of 3.8, the Records rows, the profile
array and menu user fields of G23 3.5, the 96 bytes of 3.12), a state object with recording stand-ins for its
function pointers (lookup, natives 13, 19, 20, platform open / close / rename), the two script tables in test
memory, and recording stubs for the functions of the "calls" table of section 2 and for the game functions of 3.9.
Compare memory (request, state object, image fields, counters, the helper's state block), results, and the order
and arguments of all calls.

**Part A**

A useful medal table: twelve records with class bits 0x02, 0x06, 0x1e, 0x17, 0x1f, 0, 0x10, ..; one with name id O
at index 3 and one with name id N at index 10; a second O and a second N further on; pictures "net-medals/a", a
null pointer, a name without a slash, a name of 47 and of 48 characters, a name with a byte 0x1f and one with 0x7f.

1. `u3x_medal_record`: native 13 null (null, no call); table null; i = 0, count − 1, count, 0xffffffff.
2. `u3x_w04_tab`: bit 27 set (the result of the `u3h_current_tab` stand-in, no lookup); table missing; rows pointer
   null; no row of the page; a row of the page with the other LAN value only; both LAN values (LAN byte 0 and 1,
   and LAN byte 2 with a LAN value 2); selected index 0, 2, −1; tabs pointer null; two matching rows of which the
   first has a null tabs pointer (0: the second is not used); a matching row at index 1023 and at index 1024 (not
   reached).
3. `u3x_medal_build`: each of the four tabs; an unknown tab and tab 0 (tab stored, built 1, count 0, selected
   kept, one table lookup); table missing with a known tab (the same); native 13 null. O before N (N shown at O's
   place, N's own place skipped, both in a tab that only N's class bits allow and in one only O's allow); N before
   O (no replacement); O without N; N without O; a second O and N (the first of each count); bit 27 set (no
   replacement, no position lookups). The number and order of native 13 calls. A table of 300 records of one class
   (256 rows, then stop); indices above 255 stored whole. Selected row below, equal to and above the new count; row
   words beyond the count unchanged.
4. `u3x_medals_source`: null descriptor; wrong tag; gate closed. Built 0 (one build, the tab asked once); built 1
   with the same tab (no build) and with another tab (build). Operation 2 with each text slot, an unknown slot,
   row = count and row = 0xffffffff (nothing); "earned" with native 19 null (0) and with the results 0, 7 and
   0xffffffff (4294967295), menu user 0, 1 and 7; the points line with the values 0, 999, 1000, 1234567, −5 and
   −1234, and with a label long enough to cut the line at 120 characters. Operation 10 with a valid row and with
   row 300 (selected stored, no text). Operation 4: wrong slot; row beyond the list; each picture name of the data
   set with native 20 returning a texture, returning null, and native 20 null; word 3 untouched. Operations 5, 6
   (empty and filled list), 9 (also right after an unbuilt state: two builds), 11 (the value id 0x67a48eb9 and
   another), an unlisted operation.

**Part B**

5. `u3x_new_marker`: flags 0 (untouched), 1 and 0x80000000; a request pre-filled with a pattern (words 3 and 13
   on unchanged).
6. `u3x_retag_net_profile`: switch bit set; another page id; item list null; items pointer null; 65 items; items
   that fail exactly one of the four tests each; two matching items (both handed over, ea[24] + 2).
7. `u3x_net_profile_source`: wrong tag; gate closed. Rows 10 and 11 with operations 2 (slots 0, 1, 2), 4, 0x15,
   0x16, each with a profile (setting 0, 1 and 5) and without one (profile array null; menu user 4). Row 9 and row
   12 (game source). Operation 6 with the game source answering 9 (becomes 11), 8 and 10 (unchanged). Operation 5
   (game source; word 1 not interpreted as a row). Operations 0x15 / 0x16 with word 0x17 = 3 while word 1 = 10
   (game source).
8. `u3x_n32_call`: operation 6 with the stand-in for `native` answering 2, 0 and −1; ea[125] low half at 0xffff
   (wraps to 0, upper half unchanged). Operations 5, 9, 10, 11 (0, untouched, no call). Rows −1, 0 .. 5, 6 and 100
   for each of the operations 2, 4, 0x15, 0x16: the row word seen by `native` and its value afterwards; text slots
   0 and 1; the Message of the Day dialog arguments; online rows with action and page both set, only one set, none
   set (the order action, then page; ea[125] + 0x10000 in all four cases).
9. `u3x_n32_retag`: switch bit set; another page; 65 items; a tab bar item with the right and with another source
   parameter; a body item; items of kind 20 or with a tag already set; ea[127].
10. `u3x_n32_source`: both tags, a third tag, null descriptor; gate closed; mode on (the game function sees LAN
    byte 0; afterwards the old value, also a value like 3); mode off; bit 1 of ea[122] set; the game function
    changing the LAN byte during the call (it is written back); ea[127] low part at 0xffffff (wraps, upper byte
    unchanged).

**Part C**

11. `u3x_i02_bar`: F null; I null. Value 0 (track only; counters 0, 3, 4, 7); lo only (track and base fill); lo
    and hi with hi above lo (three draws), equal to lo and below lo (two draws); hi set with lo 0 (track only).
    Widths that make lo · k fall below 1.0 (the fill is 1.0 wide); width 4.0 (k = 0), a width below 4.0 (k
    negative: fill 1.0, a segment only when hi is below lo), a NaN width. Colours 0, 255 and mixed values in every
    channel; upper bits of r[2] .. r[5] set (ignored). The two counted pages and another page. Random values and
    rectangles for the float comparison (bit for bit, except the base width, which may differ by one unit in the
    last place from an x87 build of the old code). The rectangle in the frame is unchanged afterwards. The stub for
    0x5b9580 writes four known floats; the track draw must receive them. Note for the tester: old code compiled
    for the host with vector instructions passes A/255 in xmm0 to that stub; the shipped helper does not (4.12
    step 2). What reaches the game in xmm0 can only be checked by a host stub that reads the register, or in the
    live game.
12. `u3x_stats_defaults`: an object filled with a pattern: the cleared range, the version, the eight words at the
    offsets the fake image gives (use offsets and values of the test's own to prove they are read from the image),
    +0x74c with the symbol present and missing, the load state and everything outside 0x248 .. 0x2a6f untouched;
    exactly one lookup.
13. `u3x_view_stats`: switch bit set (one show, nothing else). Viewed pointer null (installed, v[0]) and already
    equal to the object. Viewed pointer set to another object before the call, and the show stand-in replacing it
    during the call (v[3], nothing filled, no `u3h_j01_filled`, the mark's first byte cleared). who = 1 with the
    same selection (own record copied, v[1], `u3h_j01_filled(1)`); who = 1 with a changed selection (treated as 0);
    who = 2 (defaults); who = 0 with the selection equal to the machine's own id, equal to the profile's id, equal
    to neither, and empty; who = 1 without a profile object (defaults, `u3h_j01_filled(0)`). The load state 3 in
    every filled case, also when the show stand-in cleared it. The 0x2828 bytes copied from what the `u3h_lg_rec`
    stand-in returns.
14. `u3x_lan_delete_profile`: switch bit set. Ask with G = 0 (nothing) and G = 2 (dialog, ea[8], G kept). Yes with
    G = 0; with G beyond the count (only G cleared); deleting the first, a middle and the last of 3 profiles and
    the only profile of 1, and the first of 8: the store (names moved, last cleared, count, selected 0), the exact
    sequence of open / close / rename calls with their paths for both file kinds, the `u3h_ol_profile_deleted`
    arguments, ea[9], ea[10], G = 0. Save failing (error word 4, store and G unchanged, no file call). An open that
    fails (no close for it, the rename still made). An unknown action value (treated as yes).

Needs the live game: the look of everything (rows, pictures, the tab bar of the card, the bars); that the card
opens without a crash and shows the right record; that the track of a stat bar looks as in the shipped helper
(4.12 step 2).

## 6. Things that look like bugs or dead code (behaviour to keep)

1. `u3x_medal_build` marks the list as built and stores the tab also when there is no medal table or the tab is
   unknown; in that case it does not reset the selected row. Row words beyond the count keep stale values.
2. The medal table is looked up again for every record (once per record for the positions of O and N, once per
   record for the rows).
3. The replacement of O by N is made only when N comes after O; later duplicates of either are ordinary records
   (a second N would be listed).
4. Rows hold 16-bit indices and the list stops at 256 rows without a sign.
5. `u3x_w04_tab` examines at most 1024 rows and stops at the first row of the page with the right LAN value, also
   when that row has no usable tab. The selected index is not compared with a count.
6. `u3x_medals_source` rebuilds on a tab change for every operation; operation 9 builds again right after such a
   rebuild; operation 10 stores any row as selected and then answers a text like operation 2; operation 4 tests
   the row twice. Operations 5 and 6 answer from the list as it is.
7. "Earned": the menu user is passed to native 19 without a limit; a missing native gives "0". The text is in the
   C part's format buffer, the points line in the buffer shared with the treasures list: each is valid only until
   the next text of the same kind.
8. The row picture leaves word 3 as it came, and the placeholder answer carries 2.25 in word 12, the texture
   answer 1.0. A picture name of 48 or more characters, or without a slash, gives the placeholder.
9. Value query 0x67a48eb9 is answered with 4 whatever the list holds; every value query is counted.
10. `u3x_new_marker` leaves word 3 as it came.
11. `u3x_net_profile_source`: a count answer of 9 becomes 11, so the list has twelve rows and the "DLC" row 11 is
    shown and can be toggled. (A comment in the old C part says the row was dropped on the owner's wish and the
    list has eleven rows; the code does not do that. See 7 item 6.) Requests for rows 10 and 11 are counted for
    every operation, also those that answer nothing. A toggle changes the profile word only; nothing is saved
    here. Without a profile object the rows still show their titles.
12. `u3x_n32_call`: the count answer gets + 3 only when the game's own answer is 0 or more (an empty game list
    stays empty although rows 0, 1 and 4 would exist). Rows outside 0 .. 5 are "handled" with nothing. Rows 0, 1
    and 4 never get a picture (no "new" marker). A click on row 0 or 1 is counted also when the row has neither
    action nor page.
13. `u3x_n32_source` writes the LAN byte back in every case, also when it did not clear it; while the game source
    runs, the byte is 0 for everything else that reads it. Handing over (4.6, 4.10) skips pages with more than 64
    items silently.
14. `u3x_i02_bar`: the alpha computed for the track colour never reaches the game (4.12 step 2): the track's
    alpha is the named colour's alpha times the item draw's own float argument (0.0 or 0.85, 3.10), not times the
    element alpha. So the track is invisible in the item draw's first pass and for dark elements.
15. `u3x_i02_bar`: a base value of 0 draws no fill and no second segment even when the upper half is set; a base
    fill is at least 1.0 wide, also for a zero or negative inner width; the second segment is drawn only when it
    is wider than 0. The two page counters and the "last" words are diagnostics that are always written.
16. `u3x_view_stats`: the selection is compared over 0x24 bytes for the click mark but over at most 32 bytes (up
    to the NUL) for the name tests. A who value other than 0 and 1 means "not own" without any name test. When
    the game already had another viewed object, the page is shown and nothing is filled. The mark's first byte is
    cleared on every call (not with the switch bit set). The load state is set to 3 also for the defaults.
17. `u3x_lan_delete_profile`: the question is shown for any non-zero G, also one beyond the profile count; "yes"
    with such a G only clears G. A failed store write leaves G set (so "yes" could be answered again) and reports
    only through the error word. Files are emptied, not removed; rename results are ignored; the slot files are
    moved after the store was written, so a failure between the two leaves names and files shifted against each
    other. Every action value other than the ask action deletes.

## 7. Open questions and notes

1. **Stat bar track alpha (decision for the coordinator).** The description keeps the shipped behaviour (6 item
   14). What the old source meant is: alpha factor = A/255 in xmm0 (a `GameCall` block with x[0] = that pattern
   and al = 1, through `u3x_k26_call`), which is also what PS3 1.19 and the PS4 game's own code for this case do.
   If that is preferred it is a deliberate deviation from v7.212 to record in STATUS.md; step 2 of 4.12 then
   changes and its ordering rule goes away. The facts of 3.10 (xmm0 untouched from the item draw's entry to the
   helper; the two caller values) were read from the PS4 image, not measured in the running game.
2. **C exports to add (integrator):** `u3h_profiles_save`, `u3h_ol_profile_deleted`, `u3h_j01_filled` (section 2).
   `u3_mp_path` is called directly (it has external linkage today). C `i05_defaults` gets a `base` parameter; its
   remaining C caller `j01_tick` passes its own `base`.
3. **Strays covered here.** (a) The medals list source: `u3x_medals_source` is a one-line forward to a
   file-private function with four file-private helpers (row text, points line, picture name test, row picture),
   plus the tab-to-class-bit choice, the table lookup and `u3x_medal_record`. The inventory lists only the three
   build / tab functions of this section; read against the last C version (v7.209) the source is the same code
   split into pieces, in no work group. It is described in 4.1 and 4.4 and delivered with part A. (b) C
   `i05_defaults` (in no group): 4.13.
4. `u3x_record_defaults` (the literal copy of the eight words; STRAYS.md, found by G22) is no longer used by this
   group. It still has other callers in the C part (the fresh online record); it can only be removed when those
   read the image too.
5. The medal tab ids are given as single ids (3.11), as G27 gives its tab ids; they could also be read from the
   image (3.12). The ids listed in 3.12 as "nowhere in the PS4 image" stay literals.
6. Row 11 ("DLC"): the old code shows twelve rows; the old comment (a008) says eleven by the owner's decision. The
   description follows the code. If eleven is what is wanted, 4.7 step 5 would answer 10 instead of 11: a decision
   for the owner, not for the implementer.
7. What the value query 0x67a48eb9 of the medals list means (answer 4) is not known; in the boosters list the same
   id asks a level number (G23). The behaviour does not depend on it.
8. `u3h_show` does not test the show-page function for null; with a null one `u3x_view_stats` would crash as the
   old code does. Not a case the game produces.

## 8. Clarifications (tester, 2026-11-14)

Facts confirmed by the old-versus-new test (`reimpl2\G28\test_g28.cpp`, v7.212 against the three part files).

1. Sections 4.1 to 4.15 are confirmed as written: in every scenario of section 5 and in the random cases the
   results, all memory and the calls with their arguments and order are the same on both sides. No fact of the
   description had to be corrected.
2. Order of reads relative to calls in v7.212. None of these can be observed in the game, because the function
   that is called does not write the value in question; they are listed so that the description is complete.
   - 4.3: the row count lives only in the list state. The "below 256" test and the position of the next row read
     it from the list state again after every table lookup, and step 7 compares the selected row with the count
     the list state holds at that moment. (Code that keeps a count of its own gives the same result as long as the
     table lookup, native 13, does not write the list state.)
   - 4.4 step 3: when the list is built, the list's stored tab is read before `u3x_w04_tab` is called; the
     comparison uses that earlier value.
   - 4.9, operation 0x16 on row 0 or 1: the action id is read before the action call (0x64ce90), the page id
     from the image table after that call has returned.
   - 4.12: a0 and a1 are read from the frame before the colour call (0x5b9580). The rectangle is read from the
     frame twice: after the colour call (for the track) and again after the track draw (for the inner rectangle).
3. 4.12 step 2 (open question 1): built without vector instructions (`-mno-sse`), neither the old function nor a
   function written as step 2 says executes a vector instruction before the colour call: a value put into xmm0
   before the stat bar function is entered arrives unchanged at 0x5b9580 on both sides. Built with vector
   instructions (host flags) both sides overwrite xmm0 before that call; such a build is not the helper's.
4. 4.12 float values. Against the old code built for single-precision arithmetic every rectangle and colour value
   is bit-identical, also for NaN, infinite and denormal rectangles. Against the old code built with `-mno-sse`
   (x87) on the host: every value identical except the base fill width, which differs by one unit in the last
   place in about 18% of the fills and never by more, and, following from it, the x of the second segment (x′ +
   bw; up to a few units in the last place of the sum where x′ and bw nearly cancel). No decision (fill at least
   1.0 wide, segment drawn or not) differed. One more x87 effect: a signalling NaN in the frame's rectangle
   reaches the track draw with its quiet bit set in the x87 build of the old code (the copy goes through the x87
   unit) and unchanged otherwise; the game does not produce such a rectangle.
5. 3.12 / 4.13: with eight entries of the test's own (other offsets, other values) at image 0x662812 the result
   holds exactly those words; with the eight words of v7.212 there, old and new objects are identical. Offsets
   that are not multiples of 4 work (unaligned stores).
6. 4.15: v7.212 builds the file paths in 50-byte buffers; a larger buffer changes nothing.
7. Section 7 holds decisions, not facts a test can settle; items 1 to 8 stay as written. The implementers left no
   open question in the part files.
