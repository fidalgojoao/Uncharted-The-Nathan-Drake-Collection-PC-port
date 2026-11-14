# G25. Co-op weapon list, co-op weapon mods list, hunter class list

Behaviour description for a reimplementation (reimpl2 pipeline, describer's document). It says what the functions
do, not how the old ones were written. All offsets are hexadecimal when prefixed with 0x. "Image offset X" means the
address `base + X`, where `base` is the game image base. All multi-byte values are little-endian; "u32 / s32 / u16 /
u8" are unsigned / signed integers of that width, "pointer" is 8 bytes. "Word n" of a request means the 32-bit word
at byte offset 4·n. Names and conventions follow `G23_boosters.md` and `G24_kickbacks.md` ("menu user", "native k",
ea[n] / eb[n], image answers, "text answer", "show icon", the string id rule). Read both first: this document refers
to their sections for everything the lists share and does not repeat them. In particular `u3x_unlock_status`
(G23 4.0) and `u3x_unlock_icon` (G24 4.1) are described there and are only called here.

This group has no float arithmetic. Two float patterns are stored as plain 32-bit words (4.5, operation 12), and
one float pattern is tested bit-wise and handed on (4.11).

## 1. Purpose and callers

The PS4 game kept the menu pages of the co-op weapons, of their mods and of the hunter class choice, but its list
sources for them are empty or cut down. PS3 1.19 has all three lists. This group is the helper's replacement:

- **The co-op weapon list** (`u3x_coop_weapon_source`, source number 0xa6): the weapon names, the gun picture of a
  row, the "new" marker, the icons of the first two fitted mods, selection, cell size, and the click that chooses
  the weapon, refreshes the 3D preview and opens the mods page.
- **The co-op weapon mods list** (`u3x_coop_mods`, source number 0xa7), for the weapon chosen in the first list:
  mod name, description, unlock text, cash, the weapon's name, the mod's icon and status picture, selection, the
  "seen" marking, and the click that fits or removes a mod and refreshes the preview.
- **The accessors** both lists use: the co-op weapon table (`u3x_coop_weapon`, `u3x_coop_weapon_count`), the weapon
  definitions (`u3x_weapon_def`), the mod icons (`u3x_mod_icon`), the cash text (`u3x_money_text`) and the pick
  sound of a mod row (`u3x_k49_coopmod`).
- **The hunter class list** (`u3x_spoiler_list`, source number 0xbf, on the page a co-op hunter gets while dead):
  the classes the current round allows, their names, level icons, the player's kickback points, selection, the
  "can pick" answer and the pick itself, which stores the class in the player's record and closes the menu.
  `u3x_spoiler_entry`, `u3x_spoiler_rows` and `u3x_spoiler_can_pick` are its building blocks.

Callers (all stay as they are):

- C `coop_weapon_source`, `coop_mods_source` and `spoiler_source` (one-line wrappers today) are called by the
  helper's event entry `u3_profile_event` for every list request, in its chain of list-source hooks (the first two
  right after the kickbacks list, the third near the end of the chain); a result of 0 passes the request on to the
  next hook.
- `u3x_weapon_def`, `u3x_mod_icon`, `u3x_money_text`, `u3x_coop_weapon`: by this group, and by the C one-line
  wrappers `weapon_def`, `mod_icon`, `money_text`, `coop_weapon`, which are reached through the C exports
  `u3h_weapon_def`, `u3h_mod_icon`, `u3h_money_text`, `u3h_coop_weapon`; the only user of those exports is
  `u3x_coop_mods`.
- `u3x_coop_weapon_count`, `u3x_spoiler_entry`, `u3x_spoiler_rows`, `u3x_spoiler_can_pick`: only by this group
  (their C wrappers `coop_weapon_count`, `spoiler_entry`, `spoiler_rows`, `spoiler_can_pick` have no caller).
- C `k49_coopmod` (the mod pick sound) is reached through the C export `u3h_coop_mod_sound`, whose only caller is
  `u3x_coop_mods`.

Everything runs on the game thread that updates and draws the menus. There is no locking and no timing rule
(the one time comparison is in 4.11).

## 2. Interface

No function takes or returns a float. All are `extern "C"`. "existing" = exported today under this name, keeps name
and signature. "new" = replaces a C body; the C function of the old name stays as a one-line wrapper.

The group is delivered in two independent parts. Part A is the co-op weapons (two lists and their accessors),
part B the hunter class list. Neither part calls a function of the other; they share only the functions of the
last table of this section.

### Part A: co-op weapon list, mods list, accessors

| Function | Signature | Status | Called by |
|---|---|---|---|
| weapon list source | `int u3x_coop_weapon_source(uintptr_t base, int32_t *ev, void *s)` | existing | C `coop_weapon_source` |
| mods list source | `int u3x_coop_mods(uintptr_t base, int32_t *ev, void *s)` | existing | C `coop_mods_source` |
| co-op weapon of an index | `uint8_t *u3x_coop_weapon(void *s, uint32_t i)` | existing (stray, see 7) | part A; C `coop_weapon` |
| number of co-op weapons | `int32_t u3x_coop_weapon_count(void *s)` | existing (stray) | part A; C `coop_weapon_count` |
| weapon definition of an id | `uint8_t *u3x_weapon_def(void *s, uint32_t id)` | existing | part A; C `weapon_def` |
| icon of a mod bit | `uint32_t u3x_mod_icon(void *s, uint32_t bit)` | existing | part A; C `mod_icon` |
| cash text | `void u3x_money_text(uintptr_t base, void *s, int32_t *ev)` | existing | part A; C `money_text` |
| mod pick sound | `void u3x_k49_coopmod(uintptr_t base, void *s, uint32_t user, uint32_t weapon, uint8_t *mods, uint32_t n, int32_t row)` | new (C `k49_coopmod`, stray) | C `k49_coopmod`, which passes its seven arguments unchanged |

Two existing functions of this area are **not** delivered again: `u3x_mod_name(uint32_t bit)` and
`u3x_mod_desc(uint32_t bit)` with their two literal lists of thirteen text ids. The running game has the same two
mappings as functions of its own (3.7); the new `u3x_coop_mods` calls those. The integrator removes
`u3x_mod_name`, `u3x_mod_desc`, their lists, the C wrappers `mod_name` / `mod_desc` and the C exports
`u3h_mod_name` / `u3h_mod_desc`, which have no other user.

### Part B: hunter class list

| Function | Signature | Status | Called by |
|---|---|---|---|
| class list source | `int u3x_spoiler_list(uintptr_t base, int32_t *ev, void *s)` | existing | C `spoiler_source` |
| class of a row | `uint8_t *u3x_spoiler_entry(uintptr_t base, void *s, int32_t row, int32_t *index)` | existing | part B; C `spoiler_entry` |
| number of rows | `int32_t u3x_spoiler_rows(uintptr_t base)` | existing (stray) | part B; C `spoiler_rows` |
| may the hunter pick | `int u3x_spoiler_can_pick(uintptr_t base, void *s, uint8_t *p)` | existing (stray) | part B; C `spoiler_can_pick` |

### Parameters and results

- `base`: game image base. `s`: the helper's state object (opaque; fields in 3.2). `ev`: one data-source request
  (3.3).
- `i`: index into the co-op weapon table. `id`: a weapon id. `bit`: a mod bit (a weapon mod is identified by one
  bit, 1 << k with k = 0 .. 12).
- `user`: local user index, already limited to 0 .. 3. `weapon`: the id of the chosen co-op weapon. `mods`, `n`:
  the weapon's mod rows and their number (3.4). `row`: a list row.
- `index`: null, or where the position of the class in the class table is stored when the class is found (left
  untouched otherwise). `p`: a net player record (3.9), never null.

Results: the three list sources return 1 when the request belongs to their list (tag match), else 0. The
accessors return a pointer into game data, or null. `u3x_coop_weapon_count` and `u3x_spoiler_rows` return a count.
`u3x_mod_icon` returns a UI icon id, or 0. `u3x_spoiler_can_pick` returns 1 or 0.

### Functions the new code calls (declare them; do not reimplement them)

| Name | Signature | What it does | Part |
|---|---|---|---|
| `u3h_ext` | `uint8_t *u3h_ext(void)` | the helper's own state block (3.1). In CONVENTIONS.h, with `ext_ea()` | A, B |
| `u3h_gate_closed` | `int u3h_gate_closed(uintptr_t base, int32_t *ev)` | 0 while multiplayer is active; otherwise stores the game's empty string as the request's text answer, counts, and returns 1 (G23) | A, B |
| `u3h_sym_ready` | `int u3h_sym_ready(uintptr_t base, void *s, uint32_t id)` | non-zero when the script module that holds symbol `id` is loaded; 0 without the "find module" native (G23) | A, B |
| `u3h_text` | `const char *u3h_text(uintptr_t base, void *s, uint32_t id)` | localized text of a text id; never null (G23) | A, B |
| `u3h_icon_index` | `int32_t u3h_icon_index(uintptr_t base, uint32_t id)` | index of UI icon `id` in the game's UI texture table, or −1 (G23) | A |
| `u3h_set_icon` | `void u3h_set_icon(uintptr_t base, int32_t *ev, uint32_t id)` | makes the request a plain icon answer for `id` when the icon is in the UI texture table; otherwise leaves the request untouched (G23) | A, B |
| `u3h_crc_sid` | `uint32_t u3h_crc_sid(const char *name)` | the string id of a name by the rule of G23 3.9; pure (G24) | A, B |
| `u3x_sym_table` | `uint8_t *u3x_sym_table(void *s, uint32_t id)` | data of a script symbol through the state's lookup function (the pointer at `s` + 656); null when the function pointer, the handle or the pointer the handle holds is null. Each call makes one lookup call when the function pointer is not null | A |
| `u3x_unlock_status` | `int u3x_unlock_status(uintptr_t base, void *s, uint32_t user, uint32_t id, uint32_t mod)` | unlock status 0 .. 255 of an item or of a mod of it: 0 owned, 1 can be bought, 2 and 3 locked in different ways; 3 when native 24 or 35 is null (G23 4.0). The C export `u3h_unlock_status` (same arguments) forwards to it | A |
| `u3x_unlock_icon` | `uint32_t u3x_unlock_icon(int status)` | the UI icon id that stands for unlock status 1, 2 or 3; 0 for every other status (G24 4.1) | A |
| `u3x_mod_id` | `int u3x_mod_id(const uint8_t *mods, uint32_t count, int32_t row, uint32_t *id)` | existing: 0 (and `*id` untouched) when `mods` is null, `row` is negative or `row` is not below `count`; otherwise stores the mod bit of row `row` (the u32 at `mods` + 0x14·`row`) in `*id` and returns 1 | A |
| `u3x_click_sound` | `int u3x_click_sound(int unlock_status)` | the menu sound of a click: 0 ("picked") for status 0 or 1, 8 ("refused") for every other status (G23) | A |
| `u3h_k49_snd` | `void u3h_k49_snd(uintptr_t base, int n)` | plays menu sound `n` and counts it, unless the list sounds are switched off (the C export G23 asks the integrator to add) | A |
| `u3h_coop_mod_sound` | `void u3h_coop_mod_sound(uintptr_t base, void *s, uint32_t user, uint32_t weapon, uint8_t *mods, uint32_t n, int32_t row)` | C export that ends in `u3x_k49_coopmod` (part A) | A |
| `u3h_lock_overlay` | `void u3h_lock_overlay(int32_t *ev)` | makes the request the "new" marker item: word 2 = 0x2f, words 3, 4, 5, 6, 8, 10 = 0, words 7, 9, 11 = −1, word 12 = 0x3f800000 (1.0). (Not the same words as `u3x_icon_answer(ev, 0x2f, 0, 0, 0, −1)`: here word 3 is cleared and word 7 is −1.) | A |
| `u3h_j11_rowimg` | `int u3h_j11_rowimg(int32_t *ev, uint32_t weapon)` | returns 0 with nothing done when bit 2 of eb[2] is set; otherwise makes the request the game's weapon picture item for weapon id `weapon` (word 2 = 0x21, word 6 = `weapon`, words 4, 5, 7, 8, 10 = 0, words 9, 11 = −1, word 12 = 1.0), counts, and returns 1. The result is not used here | A |
| `u3h_preview_mods` | `void u3h_preview_mods(uintptr_t base, uint32_t mask)` | nothing when bit 2 of eb[2] is set; otherwise gives the weapon preview of the menu scene the mod mask `mask` and has the game rebuild the preview gun (when the preview object is alive), and counts | A |
| `u3h_show` | `int u3h_show(uintptr_t base, void *s, uint32_t page, int silent)` | opens a menu page through the state object's show-page function (the pointer at `s` + 648, which it does not test); result unused here | A, B |
| `u3x_menu_back` | `int u3x_menu_back(uintptr_t base, uint8_t *m, int silent)` | "back" on a screen menu (G33); nothing for a null `m`; result unused here | B |
| `u3x_menu_close` | `void u3x_menu_close(uintptr_t base, uint8_t *m)` | closes a screen menu (G33); nothing for a null `m` | B |
| `u3h_net_coop_mode` | `int u3h_net_coop_mode(uintptr_t base, void *s)` | non-zero while multiplayer is active and the current net game is of a co-op game type | B |
| `u3h_np_alive` | `int u3h_np_alive(void *s, uint8_t *p)` | non-zero when the net player `p` is alive | B |
| `u3h_respawn_delay` | `uint32_t u3h_respawn_delay(void *s, uint8_t *p)` | the respawn delay of net player `p` in seconds, as a 32-bit float pattern (native 55) | B |
| `u3h_f_ticks` | `int64_t u3h_f_ticks(uint32_t f, int64_t half)` | the float pattern `f` (seconds) as game ticks, 300 per second, the fraction cut off, less 150·`half` | B |

`u3h_ext`, `u3h_crc_sid` and `u3x_unlock_icon` have no side effect: how often they are called, and whether a string
id is computed on every call or kept after the first, is not part of the behaviour. Every other call of this table
is made exactly as section 4 says.

Inside part A the accessors of the part are called directly (`u3x_coop_weapon`, `u3x_weapon_def`, `u3x_mod_icon`,
`u3x_money_text`). The old mods list reached them through the one-line C exports `u3h_coop_weapon`,
`u3h_weapon_def`, `u3h_mod_icon`, `u3h_money_text`, which stay in the C part; both ways are the same call.

## 3. Game and helper facts

### 3.1 The helper's state block (address from `u3h_ext()`)

| Offset | Type | Meaning | Part |
|---|---|---|---|
| 0x100 + 4·7 | u32 | counter: requests of the hunter class list that got past the gate | B |
| 0x100 + 4·8 | u32 | counter: classes picked | B |
| 0x100 + 4·9 | u32 | the table position of the class picked last | B |
| 0x180 + 4·u | s32 | the selected row of the hunter class list of local user u (u = 0 .. 3) | B |
| 0x190 + u | u8 | a toggle of local user u (flipped by a button on the class page, 4.10 operation 9); non-zero = the list answers "cannot be picked" | B |
| 0x3c50 (ea[0]) bit 5 (value 0x20) | u32 | set = booster icons off; the co-op weapon list uses it for its cell size | A |

Counters are plain 32-bit increments (wrapping).

### 3.2 The state object `s`

| Offset | Type | Meaning |
|---|---|---|
| 648 | pointer to function | the show-page function; part A tests it for null before it opens the mods page (4.5) |
| 656 | pointer to function | script symbol lookup (used through `u3x_sym_table`) |
| 760 | u64 | the image base; the two list sources of part A store `base` here |
| 768 + 8·k | pointer to function | "native k": a game function the helper found at start |
| 0x2c80 | 251 characters | the text buffer shared with other lists (G23 3.2); `u3x_money_text` composes in it |
| 0x3900 | s32 | the selected row of the co-op weapon list (one value, not per user) |

Natives used here (the game function each one normally holds is given for orientation only; always call through the
pointer in the state object and treat a null pointer as said in section 4):

| k | Arguments | Result | Game function | Part |
|---|---|---|---|---|
| 13 | (u32 symbol id) | pointer to the data of a script symbol. The game function stops the game on a symbol that is not loaded, so the list sources only get here behind `u3h_sym_ready` | 0x11f2e30 | A, B |
| 26 | (u32 id, u32 mod) | pointer to a text: what unlocks the item's mod / its price | 0x751b30 | A |
| 32 | (image 0x17f3068, u32 user, u32 id, u32 mod) | none: marks the mod of the item as seen | 0x7bda50 | A |
| 36 | (image 0x17f3068, u32 user, u32 id, u32 mod) | low 8 bits non-zero = the mod of the item shows the "new" marker | 0x7bd6f0 | A |
| 37 | (image 0x17f3068, u32 user, u32 id) | low 8 bits non-zero = the item (a weapon) has something newly available: its row shows the "new" marker | 0x7bdc70 | A |

Natives 24 and 35 are used only inside `u3x_unlock_status`, native 55 only inside `u3h_respawn_delay`.

### 3.3 Data-source requests

As G23 3.3. The fields this group uses:

| Field | Meaning |
|---|---|
| word 0 | operation |
| word 1 | row index (in); the answer of operations 5, 6, 0x11, 0x15 and 0x18 (out) |
| word 2 .. 12 | image answer (through `u3h_set_icon`, `u3h_lock_overlay`, `u3h_j11_rowimg`) |
| word 0xd | a flag of operation 0x17 (hunter class list) |
| word 0x14 | the image slot asked (operation 4) |
| word 0x15 | the text slot asked (operation 2) |
| word 0x17 | the clicked row (operation 0x16), the row asked (operation 0x15) |
| word 0x18, 0x19 | row and column of a cell picture (operation 0xe) |
| word 0x1f, 0x20 | cell size (operation 12), two float patterns |
| pointer at byte 0x40 | the text answer |
| pointer at byte 0x98 | the source descriptor; its u32 at +0x1c is the source tag |

Source tags: 0x4c414ea6 = the co-op weapon list, 0x4c414ea7 = the co-op weapon mods list, 0x4c414ebf = the hunter
class list.

### 3.4 Part A: the co-op weapon table, the weapon definitions, the mod icons

**The co-op weapon table**: data of script symbol 0x556e3fb5, through `u3x_sym_table`.

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 (read as s32 by `u3x_coop_weapon_count`) | number of weapons |
| 8 | pointer | the weapons, 0x18 bytes each |

A weapon of that table:

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | weapon id |
| 8 | pointer | the weapon's mod rows, 0x14 bytes each; u32 at +0 of a row = the mod bit. May be null |
| 0x10 | u32 | number of mod rows |

The index of a weapon in this table is "the weapon index"; row r of the weapon list is weapon index r.

**The weapon definitions**: data of script symbol 0x61d589d9, through native 13 (not through `u3x_sym_table`:
native 13 returns the data pointer itself). It is an open-addressed hash table:

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | number of slots N |
| 8 | pointer | N keys, u32 each: a weapon id, or 0 for an empty slot |
| 0x10 | pointer | N pointers: the definition of the slot's key |

The home slot of an id is id modulo N (unsigned); a collision goes to the next slot, wrapping from N − 1 to 0. A
weapon definition: u32 at +4 = text id of the weapon's name.

**The mod icons**: data of script symbol 0x36a4c63b, through `u3x_sym_table`: u32 at +0 = number of icons; pointer
at +8 = the icons, u32 each; the icon at position k belongs to mod bit 1 << k.

None of these tables is copied; they are read at run time on every call.

### 3.5 Part A: profile object, game data

"Menu user" and "profile object of the menu user" as G23 3.5 / G24 3.5 (the pointer at image 0x17dd610 + 0x7b00 ·
menu user; null when that pointer is null or the menu user is 4 or more). Fields of a profile object:

| Offset | Type | Meaning |
|---|---|---|
| 0x74c | u32 | the player's cash |
| 0x1534 + weapon index | u8 | the mod mask of a co-op weapon: bit k set = the mod of mod row k is fitted |

| Image offset | Type | Meaning |
|---|---|---|
| 0x17d9998 | s32 / u32 | the weapon index chosen in the co-op weapon list (written signed by the click, read unsigned by the mods list) |
| 0x1b6ae68 | s32 | the selected row of the mods list |
| 0x17bba90 | object | the menu scene (argument of game function 0x5b3e40) |
| 0x17f3068 | object | unlock manager (argument of natives 32, 36, 37) |

### 3.6 Part A: game functions called directly

| Image offset | Call | Meaning |
|---|---|---|
| 0x5b3e40 | (pointer menu scene, int, int, int), no result | refreshes the 3D weapon preview of the menu scene. Called with (image 0x17bba90, 0, 0, 1): "show the chosen co-op weapon". Four integer arguments, no float argument |
| 0xd47450 | (s32 mod bit) → u32 | the text id of a mod's name (3.7) |
| 0xd47570 | (s32 mod bit) → u32 | the text id of a mod's description (3.7) |

### 3.7 Part A: the mod texts are asked from the game, not listed

The old code held two lists of thirteen text ids (the name and the description of the mod of each bit 1 << 0 ..
1 << 12) and one fallback id each. They are not copied here. The running PS4 game holds exactly these values as
two small functions:

- image 0xd47450: one s32 argument (the mod bit, first integer argument), result a u32 in the result register:
  the text id of the mod's name; the fallback id for every value that is not exactly one of the thirteen bits
  (0, several bits, a higher bit, negative values);
- image 0xd47570: the same for the description.

Both are leaf functions: they read no memory but their own code, write nothing, call nothing and use no float
register. They may be called at any time, any number of times.

How this was checked: the bytes of both functions were taken from the PS4 image and executed on a PC for every one
of the 2^32 argument values; the results equal the old helper lists and fallbacks for all of them. (The game's own
mods list at image 0x7680f0 carries the same ids once more as immediates; that is where the old lists came from.)

### 3.8 Part A: ids and texts

Text ids (looked up with `u3h_text`): 0xba6d58f7 the cash label (the same text as in G23 and G24).

Text slots (word 0x15): weapon list: 0 the weapon's name. Mods list: 0 the mod's name; 3 its description; 1 its
unlock text; 8 the cash; 0x5d822913 the weapon's name.

Image slots (word 0x14): weapon list: 0 the gun picture of the row; 0xbb36238e the "new" marker; 0x5095aa91 and
0x5454b726 the icon of the first and of the second fitted mod. Mods list: 0xf5a294f3 the mod's icon; 0 the status
picture.

Page id: 0xba7ae3b4 = the co-op weapon mods page.

The "equipped" check icon is the string id of the texture name "general/net/menu-icons/icon-check" (G23 3.9,
G24 3.7; through `u3h_crc_sid`). The three lock-status icons come from `u3x_unlock_icon`. The new code holds no
icon id as a number.

Texts of the helper's own (they must live in read-only helper data): the colour escape "|@Cff00ff00|" (cash) and
the texture name above.

### 3.9 Part B: game data

| Image offset | Type | Meaning |
|---|---|---|
| 0x17c23a0 | u32 | the local user the menu belongs to; this list uses its low 2 bits (not the "menu user" rule of part A) |
| 0x17d79f0 + 0x30·u | u32 | the kickback points (medals) of local user u |
| 0x16e3188 | pointer | the screen menu object of screen 0 (may be null) |
| 0x17cb060 | object | the net game manager; pointer at +0x370 + 8·u = the net player record of local user u (may be null) |
| 0x17b4ca4 | s32 | the net clock, in ticks of 1/300 s |

Net player record fields:

| Offset | Type | Meaning |
|---|---|---|
| 0x21c | s32 | team; 1 = the hunters |
| 0x39c | s32 | a time stamp on the net clock: the time the respawn delay counts from |
| 0x420 | u8 | non-zero: a respawn request is pending |
| 0x5e4 | u8 | non-zero: the player is leaving |
| 0x608 | s32 | the table position of the hunter class the player picked |
| 0x60c | u8 | 1 = a hunter class has been picked |

**The class table**: data of script symbol 0x2b6127a0, through native 13:

| Offset | Type | Meaning |
|---|---|---|
| 0 | s32 | number of classes |
| 8 | pointer | the classes, 0x28 bytes each (may be null) |

A class: u32 at +0 = class id; u32 at +4 = text id of its name; u32 at +0x1c = its kickback level, which is at the
same time its cost in kickback points.

**The classes of the current round**: game function 0x723060 returns the entry of the current round, or null. The
pointer at +0x40 of that entry leads to the round's class list: s32 at +0 = number of classes (= rows of the
list), pointer at +8 = their class ids, u32 each. Row r of the list is the class whose id is the r-th of these.

Both tables are game data, read at run time on every call.

### 3.10 Part B: game functions called directly, and their code checks

| Image offset | Call | Meaning |
|---|---|---|
| 0x723060 | (pointer net game manager = image 0x17cb060) → pointer or null | the entry of the current round |
| 0x76b0f0 | (s32 *request), no result | the game's own (cut-down) source of this list; it still answers operation 1, the title with the kickback points |
| 0x658050 | (pointer menu, int, int) → int | the menu's button input of this frame, called with (menu, 1, 0); the results 7 and 0xc matter here |

Each of the three is called only when the first 12 bytes of its code are the expected ones (a failed check simply
leaves the call out, as said in section 4). The expected bytes, to keep in read-only helper data:

- 0x723060: 55 48 89 E5 41 56 53 48 89 FB BF 7D
- 0x76b0f0: 55 48 89 E5 41 57 41 56 53 48 83 EC
- 0x658050: 53 48 8D 0D 38 CA 15 01 8B 99 7C 08

The check only reads the image; how often it is made is not part of the behaviour.

### 3.11 Part B: icons by name, ids and texts

Every UI icon id of the list is the string id (G23 3.9, `u3h_crc_sid`) of a texture name of the multiplayer menu
art (materials of the folder "general/net/hunter-medals" of the game's "net" art package):

| Icon | Texture name | Used for |
|---|---|---|
| points icon | "general/net/hunter-medals/hunter-medal-blank" | the picture beside the kickback points |
| class level icon of level n, n = 0 .. 10 | "general/net/hunter-medals/hunter-medal-" followed by n in decimal (one digit for 0 .. 9, "10" for ten) | the level picture of a class row |

The old code held the eleven level icon ids as a literal list (a PS3 1.19 table). The PS4 image holds none of those
eleven values, so they cannot be read from the running game; they are generated by the rule above. It was checked
by computing the string ids of the twelve names and comparing them with the old code's twelve numbers: all match.
(The points icon id also occurs once in the PS4 image, as an immediate of HUD code at image 0xc3ceaa; the new code
does not need it.) There is no class level icon above level 10.

Text slots (word 0x15): 0 the class name; 0xee5350e3 the kickback points. Image slot (word 0x14): 0xee5350e3 the
points icon. Page id: 0xf3026799 (shown on menu input result 7).

Texts of the helper's own: the single character "x", and the texture names above (read-only helper data); one
writable text buffer of 24 bytes of the helper (the points text, 4.10), which must outlive the request.

## 4. Behaviour

"Text answer = T": the pointer at byte 0x40 of the request becomes T. "Show icon X" (G24 4): first
`u3h_icon_index(base, X)`; only when its result is 0 or more, `u3h_set_icon(base, ev, X)`.

**Part A (4.1 to 4.7)**

### 4.1 `u3x_coop_weapon`, `u3x_coop_weapon_count`

**`u3x_coop_weapon`.** T = `u3x_sym_table(s, 0x556e3fb5)`. Null when T is null or `i` is not below the number of
weapons (u32 at T + 0, unsigned comparison). Otherwise the address (weapons pointer of T) + 0x18·`i`. The weapons
pointer is not tested for null.

**`u3x_coop_weapon_count`.** T as above; 0 when T is null, else the s32 at T + 0.

### 4.2 `u3x_weapon_def`

Null when native 13 is null (no call). Otherwise H = native 13(0x61d589d9); null when H is null or its slot count
N is 0. Otherwise the slots are probed from the home slot of `id` (3.4), one after the other with wrap-around:

- a slot whose key equals `id`: the result is that slot's definition pointer (as stored, also when it is null);
- otherwise a slot whose key is 0: null;
- when all N slots have been probed without either: null.

The key test comes before the empty test, so for `id` 0 the first empty slot from the home slot "matches" and its
definition pointer is returned. The key and definition pointers of H are not tested for null.

### 4.3 `u3x_mod_icon`

T = `u3x_sym_table(s, 0x36a4c63b)`; 0 when T is null. Otherwise: when `bit` is exactly 1 << k for a k that is
below the number of icons (u32 at T + 0) and below 32, the result is the icon at position k (u32 at (icons pointer
of T) + 4·k). In every other case (0, several bits, a bit at or beyond the count) 0. The icons pointer is not
tested for null.

### 4.4 `u3x_money_text`

The text buffer (s + 0x2c80) is composed: `u3h_text(base, s, 0xba6d58f7)`, then "|@Cff00ff00|", then the cash in
decimal with thousands separators, where cash = the u32 at +0x74c of the profile object of the menu user, or 0
without a profile object. Composition rule as G23 4.1 (pieces one after the other, cut after 250 characters, a NUL
follows; a comma between groups of three digits counted from the right, none below 1000). Then text answer = the
text buffer. Nothing else.

### 4.5 `u3x_coop_weapon_source`

"Weapon of row" = `u3x_coop_weapon(s, row taken as u32)` (so a negative row has no weapon).

Steps, in this order:

1. Source descriptor (pointer at byte 0x98) null, or its tag not 0x4c414ea6: return 0, nothing touched.
2. `u3h_gate_closed(base, ev)` non-zero: return 1.
3. The image base is stored in the state object (offset 760).
4. `u3h_sym_ready(base, s, 0x556e3fb5)` is 0, or (asked only when the first was non-zero)
   `u3h_sym_ready(base, s, 0x61d589d9)` is 0: for operation 6, word 1 = −1; return 1 in every case.
5. With row = word 1 and user = the menu user AND 3, by operation, then return 1 (also for operations not listed):

| Operation | Effect |
|---|---|
| 2 | only when word 0x15 is 0 (otherwise no table lookup at all): w = weapon of row; none: nothing. d = `u3x_weapon_def(s, id of w)`; when d is not null: text answer = `u3h_text(base, s, name id of d)` |
| 4 | by image slot, see below |
| 5 | word 1 = the selected row (s32 at s + 0x3900) |
| 6 | word 1 = `u3x_coop_weapon_count(s)` − 1 |
| 10 | the selected row (s + 0x3900) = row, stored as it is |
| 12 | when bit 5 of ea[0] is clear: word 0x1f = 0x43800000 (256.0), word 0x20 = 0x43000000 (128.0) |
| 0x11 | word 1 = 4 |
| 0x16 | the click, see below |

**Operation 4, image slot 0 (the gun picture).** w = weapon of row; when there is one:
`u3h_j11_rowimg(ev, id of w)`.

**Image slot 0xbb36238e (the "new" marker).** w = weapon of row (looked up in every case). When there is one,
native 37 is not null, and the low 8 bits of native 37(base + 0x17f3068, user, id of w) are non-zero:
`u3h_lock_overlay(ev)`.

**Image slots 0x5095aa91 (first fitted mod) and 0x5454b726 (second fitted mod).**

1. mask = the u8 at +0x1534 + row of the profile object of the menu user, or 0 without a profile object. The row
   is used as it came (signed, not compared with anything) and the byte is read before the row is looked up.
2. mask 0: nothing (no table lookup). Otherwise w = weapon of row; none: nothing.
3. Among the first 8 mod rows of w (fewer when w has fewer; none when its mod rows pointer is null), in row order,
   the rows whose bit in mask is set (bit k for mod row k) are the fitted mods. The slot's mod is the first of them
   for slot 0x5095aa91, the second for slot 0x5454b726; when there is no such mod: nothing.
4. icon = `u3x_mod_icon(s, the mod bit of that row)`; show icon (also asked for an icon of 0, which simply is not
   in the table).

**Any other image slot:** nothing.

**Operation 0x16 (the click).** In this order: the s32 at image 0x17d9998 = word 0x17 (stored as it is); game
function 0x5b3e40(base + 0x17bba90, 0, 0, 1); the s32 at image 0x1b6ae68 = 0 (the mods list starts at its first
row); when the state's show-page function (s + 648) is not null: `u3h_show(base, s, 0xba7ae3b4, 0)`.

### 4.6 `u3x_coop_mods`

Steps, in this order:

1. Source descriptor null, or its tag not 0x4c414ea7: return 0, nothing touched.
2. `u3h_gate_closed(base, ev)` non-zero: return 1.
3. The image base is stored in the state object (offset 760).
4. The two `u3h_sym_ready` tests exactly as 4.5 step 4 (for operation 6, word 1 = −1; return 1).
5. wi = the u32 at image 0x17d9998 (the weapon index); w = `u3x_coop_weapon(s, wi)`. None: for operation 6,
   word 1 = −1; return 1 in every case.
6. Set-up: weapon = id of w; n = its number of mod rows; mods = its mod rows pointer; profile = the profile object
   of the menu user; user = the menu user AND 3; row = word 1; **bit** = the mod bit of row `row` (u32 at mods +
   0x14·row) when mods is not null and row is within 0 .. n − 1, else 0. "The mask" = the u8 at profile + 0x1534 +
   wi. "Status of b" = `u3x_unlock_status(base, s, user, weapon, b)`.
7. Operation 0x16: `u3h_coop_mod_sound(base, s, user, weapon, mods, n, word 0x17)` (the sound), then go on.
8. By operation, then return 1 (also for operations not listed):

| Operation | Effect |
|---|---|
| 2 | row text, see below |
| 4 | row picture, see below |
| 5 | word 1 = the s32 at image 0x1b6ae68 |
| 6 | word 1 = n − 1 |
| 9 | when there is a profile object: `u3h_preview_mods(base, the mask)` (the saved mods again). Then, with sel = the s32 at image 0x1b6ae68: when native 32 is not null, mods is not null and sel is within 0 .. n − 1: native 32(base + 0x17f3068, user, weapon, mod bit of row sel) |
| 10 | the s32 at image 0x1b6ae68 = row, stored as it is |
| 0x16 | the toggle, see below |

**Operation 2 (row text)**, by text slot (word 0x15):

| Text slot | Text answer |
|---|---|
| 0 | only when bit is not 0: `u3h_text(base, s, N)` with N = game function 0xd47450(bit) |
| 3 | only when bit is not 0: `u3h_text(base, s, D)` with D = game function 0xd47570(bit) |
| 1 | only when bit is not 0 and status of bit is not 0 (the status is asked first): when native 26 is not null, the pointer native 26(weapon, bit) returns (stored as it is). Native 26 null: nothing |
| 8 | status = status of bit when bit is not 0, else 0 (no call). Only when status is 1 or 2: `u3x_money_text(base, s, ev)` |
| 0x5d822913 | d = `u3x_weapon_def(s, weapon)`; when d is not null: `u3h_text(base, s, name id of d)`. (Whatever the row.) |
| other | nothing |

**Operation 4 (row picture).** Nothing when bit is 0. Otherwise by image slot (word 0x14):

- 0xf5a294f3 (the mod's icon): `u3h_set_icon(base, ev, u3x_mod_icon(s, bit))` (called directly, also with an icon
  of 0; `u3h_set_icon` itself leaves the request alone for an icon that is not in the UI texture table).
- 0 (the status picture), the first that applies:
  1. New: native 36 is not null and the low 8 bits of native 36(base + 0x17f3068, user, weapon, bit) are non-zero:
     `u3h_lock_overlay(ev)`. Done.
  2. Otherwise by status of bit:
     - 0 (owned): when there is a profile object and bit number (row AND 31) of the mask is set (the mask has 8
       bits: bit numbers 8 .. 31 are never set): `u3h_set_icon(base, ev, the "equipped" check icon of 3.8)`.
     - 1, 2, 3: `u3h_set_icon(base, ev, u3x_unlock_icon(status))`.
     - any other status: nothing.
- any other slot: nothing.

**Operation 0x16 (the toggle).** With clicked = word 0x17 (signed): nothing when there is no profile object, mods
is null, or clicked is not within 0 .. n − 1. Otherwise: when the status of the mod bit of row clicked is 0 (owned),
bit number (clicked AND 7) of the mask is flipped (the byte at profile + 0x1534 + wi is rewritten). Then, whatever
the status was: `u3h_preview_mods(base, the mask as it is now)`.

The profile is not saved by this operation, and nothing is bought.

### 4.7 `u3x_k49_coopmod`: the pick sound of a mod row

`u3x_mod_id(mods, n, row, &bit)` is 0 (no such row): nothing. Otherwise
`u3h_k49_snd(base, u3x_click_sound(u3x_unlock_status(base, s, user, weapon, bit)))`.

(So for one click of an owned mod the unlock status is asked twice: here and in the toggle of 4.6.)

**Part B (4.8 to 4.11)**

### 4.8 `u3x_spoiler_entry`: the class of a row

In this order:

1. C = native 13(0x2b6127a0). Native 13 is called without a null test: the function is only used while the
   natives are set (see 6, item 12).
2. Null when C is null, or when the code check of 0x723060 fails (3.10).
3. R = game function 0x723060(base + 0x17cb060); L = the pointer at R + 0x40, or null when R is null. Null when L
   is null, `row` is negative, or `row` is not below the s32 at L + 0.
4. want = the `row`-th class id of L (u32 at (pointer at L + 8) + 4·`row`).
5. The classes of C (pointer at C + 8; a null pointer means no classes) are compared in table order, positions 0
   to (s32 at C + 0) − 1: the first whose id (u32 at +0) equals want is the result; when `index` is not null, its
   position is stored there first. No class with that id: null, `*index` untouched.

### 4.9 `u3x_spoiler_rows`, and the points of a user

**`u3x_spoiler_rows`.** 0 when the code check of 0x723060 fails (no call). Otherwise R = game function
0x723060(base + 0x17cb060); 0 when R is null or the pointer at R + 0x40 is null; else the s32 that pointer leads
to (the number of classes of the round).

"Points of the user" below = the u32 at image 0x17d79f0 + 0x30·user.

### 4.10 `u3x_spoiler_list`

Steps, in this order:

1. Source descriptor null, or its tag not 0x4c414ebf: return 0, nothing touched.
2. `u3h_gate_closed(base, ev)` non-zero: return 1.
3. The request counter (state block + 0x100 + 4·7) += 1. user = the u32 at image 0x17c23a0 AND 3.
4. Operation 1: when the code check of 0x76b0f0 passes: game function 0x76b0f0(ev). Return 1. (No table test
   before it.)
5. `u3h_sym_ready(base, s, 0x2b6127a0)` is 0: return 1, nothing else.
6. By operation, then return 1 (also for operations not listed):

| Operation | Effect |
|---|---|
| 2 | text slot 0: e = `u3x_spoiler_entry(base, s, word 1, null)`; when there is one: text answer = `u3h_text(base, s, name id of e)`. Text slot 0xee5350e3: the helper's points buffer (3.11) is filled with "x" followed by the points of the user in plain decimal and a NUL; text answer = that buffer. Other slots: nothing |
| 4 | when word 0x14 is 0xee5350e3: `u3h_set_icon(base, ev, the points icon of 3.11)` |
| 5 | word 1 = the selected row of the user (state block + 0x180 + 4·user) |
| 6 | word 1 = `u3x_spoiler_rows(base)` − 1 |
| 9 | m = the pointer at image 0x16e3188. When m is not null and the code check of 0x658050 passes: r = game function 0x658050(m, 1, 0). r = 7: `u3h_show(base, s, 0xf3026799, 0)`. r = 0xc: the toggle byte of the user (state block + 0x190 + user) is flipped between 0 and 1 (its lowest bit is inverted). Other results: nothing |
| 10 | the selected row of the user = word 1, stored as it is |
| 0xe | only when word 0x19 is 2 (the level column): e = `u3x_spoiler_entry(base, s, word 0x18, null)`; none: nothing. level = the u32 at e + 0x1c; when level is below 11: `u3h_set_icon(base, ev, the class level icon of that level)` (3.11). Level 11 and above: nothing |
| 0x11 | word 1 = 3 |
| 0x15 | ("can this row be picked") when the toggle byte of the user is non-zero: word 1 = 0, without looking at the row. Otherwise e = `u3x_spoiler_entry(base, s, word 0x17, null)`; word 1 = 1 when there is one and its cost (u32 at +0x1c) is not above the points of the user (unsigned), else 0 |
| 0x16 | the pick, see below |
| 0x17 | when word 0xd is not 0: the selected row of the user = 0 |
| 0x18 | word 1 = 1 |

**Operation 0x16 (the pick).** In this order; each "nothing more" ends the operation:

1. e = `u3x_spoiler_entry(base, s, word 0x17, &position)` with position preset to 0. No class: nothing more.
2. The points of the user are below the cost of e (unsigned): nothing more.
3. p = the net player record of the user (pointer at image 0x17cb060 + 0x370 + 8·user). Null: nothing more.
4. `u3x_spoiler_can_pick(base, s, p)` is 0: nothing more.
5. The u8 at p + 0x60c = 1; the s32 at p + 0x608 = position; the pick counter (state block + 0x100 + 4·8) += 1; the
   word at state block + 0x100 + 4·9 = position.
6. m = the pointer at image 0x16e3188; `u3x_menu_back(base, m, 0)`; then `u3x_menu_close(base, m)` (both are
   called also with a null m).

The points are not reduced here, and the toggle byte is not looked at.

### 4.11 `u3x_spoiler_can_pick`

A hunter may pick while dead, past the respawn delay, with no request pending. The tests are made in this order
and the first that fails gives 0 (the later ones are not made):

1. `u3h_net_coop_mode(base, s)` non-zero;
2. the team of `p` (s32 at +0x21c) is 1;
3. `u3h_np_alive(s, p)` is 0;
4. the u8 at `p` + 0x420 is 0;
5. the u8 at `p` + 0x5e4 is 0;
6. d = `u3h_respawn_delay(s, p)` is a positive, finite float pattern: sign bit clear, not all of the low 31 bits
   zero, exponent field (bits 23 .. 30) not 0xff. (Denormals count as positive; +0, −0, negative values, infinities
   and NaNs do not.) This is a test on the bits; no float instruction is needed.

Then the result is 1 when `u3h_f_ticks(d, 0)` is below (the s32 at image 0x17b4ca4 − the s32 at `p` + 0x39c), else
0. The difference is taken in 32 bits (wrapping), then compared as a signed 64-bit value with the 64-bit tick
count.

## 5. Test scenarios

Everything except the points at the end can be tested with a fake image (the fields of 3.5 and 3.9, the twelve code
bytes of 3.10 in place or not), script tables in test memory behind stand-ins for the state's lookup function and
native 13, a state object whose natives are recording stand-ins, and recording stubs for the functions of the last
table of section 2 (`u3h_crc_sid` real, or checked separately) and for the game functions of 3.6 and 3.10. Compare
memory (request, state object, image fields, profile, net player record, state block), results, and the order and
arguments of all calls.

One planned difference from the old code in the call record: the old mods list asked the helper's own
`u3h_mod_name` / `u3h_mod_desc` for the text id; the new one asks game functions 0xd47450 / 0xd47570 (3.7). The
test gives both the same answers and compares the id that reaches `u3h_text`. A second one: the old code passed the
three lock-status icon ids and the check icon id as numbers; the new code gets them from `u3x_unlock_icon` and
`u3h_crc_sid` (the ids that reach `u3h_set_icon` must be equal).

Part A, accessors:
1. `u3x_coop_weapon`: no lookup function; null table; i = 0, count − 1, count, 0xffffffff.
   `u3x_coop_weapon_count`: null table (0); a table with 3 weapons.
2. `u3x_weapon_def`: native 13 null (no call); null table; N = 0; an id in its home slot; an id one and two slots
   further (collisions), with wrap-around from the last slot to slot 0; an id that is not there with an empty slot
   on the way (null); a full table without the id (null after N probes); id 0 (the first empty slot's definition
   pointer); a matching slot whose definition pointer is null.
3. `u3x_mod_icon`: null table; bit 1, 2, 0x1000 with a count of 13; bit 1 << count (0); bit 0, bit 3, bit
   0x80000000 with a count of 13 (0); a count above 32 with bit 0x80000000 (the icon at position 31).
4. `u3x_money_text`: cash 0, 999, 1000, 1234567, 4294967295; no profile array (0); menu user 5 (0); a label long
   enough to cut the result at 250 characters; the text answer is the buffer.
5. `u3x_k49_coopmod`: mods null; row −1; row = n; a row with status 0, 1, 2, 3 (sound 0, 0, 8, 8); the mod bit
   passed to `u3x_unlock_status`.

Part A, weapon list:
6. Null descriptor; wrong tag (0, nothing touched). Gate closed (1, base not stored).
7. First symbol not ready (the second not asked); second not ready; operation 6 (word 1 = −1) and another.
8. Operation 2: word 0x15 = 1 (no lookup); row −1; row beyond the table; a weapon without a definition; a normal
   row.
9. Operation 4, slot 0: valid row (`u3h_j11_rowimg` with the weapon id), invalid row. Slot 0xbb36238e: native 37
   null (the row is still looked up), answering 0, 1 and 0x100 (0x100 = no marker), invalid row.
10. Slots 0x5095aa91 / 0x5454b726: no profile; mask 0 (no lookup); mask with one bit (first slot answered, second
    not); mask 0b0110 (first slot = mod row 1, second = mod row 2); a mask bit at or beyond the number of mod rows;
    a weapon with 10 mod rows and mask bit 7 (found) ; mods pointer null; an icon missing from the UI table (asked,
    not set); icon 0; row beyond the table with a non-zero byte at +0x1534 + row (nothing); row −1 (the byte at
    +0x1533 is read).
11. Operations 5, 6 (also with a null table: −1), 10 (row 2, row −3), 0x11, 12 with bit 5 of ea[0] clear and set,
    an unlisted operation.
12. Operation 0x16: the four effects in order; word 0x17 = −1 and 99 (stored all the same); show-page function
    null (no `u3h_show`).

Part A, mods list:
13. Null descriptor; wrong tag; gate closed; symbols not ready; weapon index beyond the table (operation 6: −1;
    another operation: nothing).
14. Bit: mods null; row −1; row = n; a normal row.
15. Operation 2: slot 0 and 3 with bit 0 (nothing) and with a bit (the id from the game function reaches
    `u3h_text`); slot 1 with status 0 (nothing), status 2 with native 26 present (its pointer, also a null one) and
    null; slot 8 with bit 0 (no status call), status 0, 1, 2, 3; slot 0x5d822913 with and without a definition, on
    a row without a bit; another slot.
16. Operation 4: bit 0 (nothing, whatever the slot); slot 0xf5a294f3 with an icon and with icon 0; slot 0 with
    native 36 null, answering 1 (overlay words, no status call), answering 0 and then status 0 with the mask bit
    set / clear / no profile, status 1, 2, 3 (the ids of `u3x_unlock_icon`), status 4 and 255 (nothing); row 9 of
    a weapon with 10 mods and status 0 (never a check); another slot.
17. Operations 5, 6 (n = 0 gives −1), 10.
18. Operation 9: with and without a profile; native 32 null; selection −1, n, a valid row (the four arguments);
    mods null.
19. Operation 0x16: the sound call first with word 0x17; no profile; mods null; clicked −1 and n (nothing, no
    preview); owned mod on row 0 and row 3 (bit flipped on, then off again on a second click); owned mod on row 9
    (bit 1 of the mask flipped); status 1 and 3 (mask unchanged, preview still called with it).

Part B:
20. `u3x_spoiler_entry`: null class table; code check of 0x723060 failing (native 13 called, game function not);
    round null; class list pointer null; row −1, count − 1, count; an id that is in the class table (with and
    without `index`), one that is not (`*index` untouched); class table with a null classes pointer; count 0.
21. `u3x_spoiler_rows`: check failing (0, no call); round null; list null; 4 classes.
22. `u3x_spoiler_can_pick`: each of the five conditions failing alone (and the later calls not made); delay
    patterns 0, 0x80000000, 0x3f800000, 0xbf800000, 0x00000001, 0x7f800000, 0x7fc00000; ticks below, equal to and
    above the time difference; a time difference that wraps in 32 bits.
23. List: null descriptor; wrong tag; gate closed (counter untouched); the counter on every other request.
24. Operation 1: check passing (game function called with the request) and failing; with the table not ready (still
    handled).
25. Table not ready: operations 2, 5, 6 (nothing at all).
26. Operation 2: slot 0 with and without a class; slot 0xee5350e3 with 0, 7, 1234567 and 4294967295 points
    ("x0", "x7", "x1234567", "x4294967295"), users 0 .. 3 (the points word of that user); another slot.
27. Operation 4: slot 0xee5350e3 (the points icon id = string id of its name) and another slot.
28. Operations 5, 10 (per user), 6, 0x11, 0x18; 0x17 with word 0xd = 0 and 1.
29. Operation 9: menu null; check failing; result 7 (page 0xf3026799), 0xc (toggle 0 → 1 → 0, only that user's
    byte), 0, 3.
30. Operation 0xe: word 0x19 = 1 (nothing); no class; levels 0, 5, 9, 10 (the string ids of the eleven names,
    equal to the old code's list); level 11 and 0xffffffff (nothing).
31. Operation 0x15: toggle set (0, no lookup); no class (0); cost below, equal to, above the points (1, 1, 0).
32. Operation 0x16: no class; points below the cost; player null; `u3x_spoiler_can_pick` 0; success: the two
    player fields, both counters, position of the class in the class table (not the row), the two menu calls in
    order, also with a null menu pointer; the toggle set (still picked).

Needs the live game: that the gun pictures, mod icons and markers appear in the cells; that the preview gun
changes with the mods; that the sounds play; that the hunter respawns with the picked class and the page closes;
and one run of the mods page to see the mod names and descriptions (the game functions of 3.7 in the running
game).

## 6. Things that look like bugs or dead code (behaviour to keep)

1. Mods list: the check mark tests bit (row AND 31) of an 8-bit mask, the toggle flips bit (row AND 7). For rows
   8 and above the toggle changes the bit of row − 8 (− 16, ...) and the row never shows a check.
2. The toggle does not save the profile and does not buy: for status 1 ("can be bought") the "picked" sound plays
   and nothing changes. The preview is refreshed after every valid click, also when the mod is locked.
3. One click asks the unlock status twice (sound, then toggle).
4. Mods list, text slot 1: the pointer native 26 returns is stored without a null test.
5. Mods list, operation 9 refreshes the preview with the saved mask although nothing changed.
6. The mods list reads the weapon index unsigned and the weapon list writes it signed and unchecked: a click on a
   row beyond the table stores it, and the mods list then finds no weapon (operation 6 answers −1).
7. Weapon list: the fitted-mod slots read the profile byte at +0x1534 + row before the row is known to be a
   weapon, with the signed row as it came. Only the first 8 mod rows are looked at.
8. Weapon list: `u3h_j11_rowimg` may answer nothing (its switch); the row then has no picture.
9. Weapon list: the cell size (operation 12) is switched by the boosters' icon switch (bit 5 of ea[0]).
10. `u3x_coop_weapon`, `u3x_weapon_def` and `u3x_mod_icon` do not test the inner pointers of their tables;
    `u3x_weapon_def` answers id 0 with the definition pointer of an empty slot.
11. The second `u3h_sym_ready` call is skipped when the first fails (both lists of part A).
12. `u3x_spoiler_entry` calls native 13 without testing it for null, and before the code check. With a null
    native 13 the old code would jump to address 0; that case cannot be compared. The new code may return null
    there.
13. Hunter list: operation 1 goes to the game's function before the table test, and the request counter also
    counts requests that are answered with nothing.
14. Hunter list: the toggle byte makes operation 0x15 answer 0, but operation 0x16 does not look at it.
15. Hunter list: the selected row is stored unchecked; operation 0x17 resets it only when word 0xd is set.
16. Hunter list: the class level and the class cost are the same field (+0x1c); a level above 10 has no icon.
17. Hunter list: the pick does not take the points away, and it calls both
    menu functions even when the menu pointer is null.
18. Hunter list: the user comes from image 0x17c23a0, not from the "menu user" rule the other lists use.
19. `u3x_spoiler_can_pick`: the time difference is a wrapping 32-bit subtraction.
20. `u3x_coop_weapon_count`, `u3x_spoiler_rows`, `u3x_spoiler_can_pick` and `u3x_spoiler_entry` have C wrappers
    without a caller.

## 7. Open questions and notes

1. **Strays added** (in no work group of the inventory, covered here): `u3x_coop_weapon`,
   `u3x_coop_weapon_count` (4.1); `u3x_spoiler_rows` with the private round lookup behind it (4.9);
   `u3x_spoiler_can_pick` (4.11); the C function `k49_coopmod` as `u3x_k49_coopmod` (4.7: the third of the pick
   sounds, whose two sisters are in G23); `u3x_mod_name`, `u3x_mod_desc` and their private lookup, which are
   dropped in favour of the game's functions (3.7). The private pieces of the old file that these use (the 12-byte
   code check, the number composition with separators, the per-user selection / toggle / points accessors) are
   described where they are used.
2. **Tables.** (a) The two lists of thirteen mod text ids and their fallbacks: not copied; the game's functions at
   image 0xd47450 and 0xd47570 return them (3.7; checked for all 2^32 arguments on the image's bytes, not yet in
   the running game). (b) The eleven class level icon ids: PS3-only values, not in the PS4 image; generated from
   texture names (3.11; all eleven and the points icon checked against the old numbers). (c) The three lock-status
   icon ids and the check icon id, which the old mods list repeated as numbers: from `u3x_unlock_icon` and from the
   name "general/net/menu-icons/icon-check". (d) The three 12-byte code checks of 3.10 are kept as they are: they
   are the helper's guard against a different game build, as in G12.
3. The game functions 0xd47450 / 0xd47570 are called without a code check (like 0x5b3e40, which the old code also
   calls unguarded). If the integrator wants a guard, the first 12 bytes of each contain that function's fallback
   text id; a guard on the following bytes would avoid holding it.
4. `u3h_k49_snd` does not exist yet (G23, open question 2): the integrator adds this one-line C export.
5. The C export `u3h_lock_overlay` (C `set_lock_overlay`) and the weapon picture behind `u3h_j11_rowimg` stay as
   they are; both are the helper's own few lines and in no group of the inventory.
6. What menu input results 7 and 0xc of game function 0x658050 are called in the game is not known (7 behaves as
   "back / cancel", 0xc as a second face button); nor which page 0xf3026799 is. The behaviour does not depend on
   the names.
7. Native 36 is called "can buy" in the C part's list of natives and "shows the new marker" in G23; here its
   non-zero answer puts the "new" marker on the mod row. Native 37 is the same question for a whole weapon.
8. As in G23 and G24 it was not checked whether the game's own "string id of a name" function returns the same
   values in the running game; the description asks for the helper's CRC (`u3h_crc_sid`).

## 8. Clarifications (tester, 2026-11-14)

Facts confirmed by the differential test (`reimpl2\G25\test_g25.cpp`, old v7.212 against the new parts A and B, at
-O1 and -O2). Item 1 corrects a fact of 4.11 and of section 6, item 19. The other items settle the implementers'
open points and make precise what the text leaves open; nothing else in sections 1 to 6 was found wrong.

1. **`u3x_spoiler_can_pick`, the time difference (correction).** The shipped helper does not take the difference of
   the net clock and the time stamp as a wrapping 32-bit value. It widens each of the two s32 values to a signed
   64-bit value first and subtracts in 64 bits, so the difference is exact (anything from −(2^32 − 1) to 2^32 − 1),
   and that exact value is compared, signed, with the 64-bit tick count. The two readings differ only when the exact
   difference does not fit in a signed 32-bit value. Examples (the result with ticks = 1500): clock 0, stamp
   −2147483648: exact difference +2147483648, result 1 (a wrapped difference would be −2147483648, result 0);
   clock −100, stamp 2147483647: exact difference −2147483747, result 0 (wrapped: +2147483549, result 1). Seen in
   the helper's compiled code as shipped in v7.212 and in the old source compiled for the test at both levels.
   Whether the game ever holds a stamp that far from the clock (a sentinel such as the lowest s32 value in a record
   that was never stamped, or a clock near the end of its range) is not known.
2. **Mod row tests are unsigned in the count.** "Row within 0 .. n − 1" (4.6: the bit of the row, the selected row
   of operation 9, the clicked row of operation 0x16; and `u3x_mod_id`) is: mods not null, the row not negative as
   a signed value, and the row below n with n taken unsigned. A count of 0x80000000 or more is a large count, not
   a negative one. Operation 6 answers n − 1 as a wrapping 32-bit value (n = 0 gives −1, n = 0x80000000 gives
   0x7fffffff). The weapon index tests (4.1) are unsigned on both sides, so with a weapon count above 0x7fffffff a
   negative row can count as a weapon index (the row taken as u32 is then below the count).
3. **`u3x_money_text` and the text buffer.** Nothing is written to the text buffer before the cash label has been
   looked up: at the `u3h_text` call the buffer still holds what it held on entry (also when the label text lies
   inside the buffer; the pieces are then copied character by character, front to back). The old code reads the
   cash word after the label lookup; as `u3h_text` does not change the profile, reading it before cannot be told
   apart, and the test does not count it.
4. **The mods list does not call `u3x_mod_id`.** The mod bit of the row asked, of the selected row (operation 9)
   and of the clicked row (operation 0x16) is read from the mod rows directly. `u3x_mod_id` is called only by the
   pick sound (4.7), once per call.
5. **When the mod mask byte of the profile is read.** Weapon list (fitted mod slots): once, before the row's weapon
   is looked up. Mods list: for the status picture after the unlock status has been asked (and only for status 0);
   in operation 9 when the preview is called, before the "seen" native; in operation 0x16 after the unlock status
   of the clicked row has been asked, and once more, from the profile, after the byte was rewritten, for the
   preview call. The sound call of step 7 comes before any of these.
6. **Result widths.** Natives 36 and 37: only the low 8 bits count (an answer of 0x100 or 0xffffff00, or any value
   whose low byte is 0, is "no marker"; the upper bits may hold anything). Native 26: the 64-bit pointer is stored
   as it is (null and non-text addresses too). Native 13 and the lookup function: 64-bit pointers. Game functions
   0xd47450 / 0xd47570: the low 32 bits of the result. Game function 0x658050: the low 32 bits, compared whole
   with 7 and 0xc (0x107 is "another result").
7. **`u3x_spoiler_entry`, order.** Native 13 is called first, also when the code check of 0x723060 fails. With a
   null class table nothing else happens (0x723060 is not called). Otherwise 0x723060 is called before the row is
   tested: a negative row, or one beyond the round's count, still makes the call. With a null native 13 the old
   code calls address 0 (the game stops); the new code returns null without a call (6, item 12; not counted).
8. **Operation 9 of the hunter list, the toggle byte.** Input result 0xc inverts the lowest bit of the byte
   whatever it holds (2 becomes 3, 0x80 becomes 0x81, 0xff becomes 0xfe). Operation 0x15 takes any non-zero byte
   as "set".
9. **No float instruction.** The respawn delay test of 4.11 is a test on the bits in the shipped helper too (no
   x87 and no SSE instruction in any function of this group, old or new).
10. **Tables, checked again.** The string ids of the eleven level texture names, of the blank medal name and of
    "general/net/menu-icons/icon-check" equal the old code's thirteen numbers. The bytes of the game functions at
    image 0xd47450 and 0xd47570, taken from the PS4 image file and run on the test machine, return the ids of the
    old lists and their fallbacks for all 2^32 argument values.
