# G23. Boosters list, booster icons and level disc, pick sounds, weapon stats bars

Behaviour description for a reimplementation (reimpl2 pipeline, describer's document). It says what the functions
do, not how the old ones were written. All offsets are hexadecimal when prefixed with 0x. "Image offset X" means the
address `base + X`, where `base` is the game image base every function receives. All multi-byte values are
little-endian; "u32 / s32 / u16 / u64 / u8" are unsigned / signed integers of that width, "pointer" is 8 bytes.
"Word n" of a request means the 32-bit word at byte offset 4·n. Names and conventions follow `G22_rank_xp.md`
("menu user", "native k", ea[n] / eb[n], image answers).

This group has no float arithmetic. Two float patterns are stored as plain 32-bit words (4.1, operation 12).

## 1. Purpose and callers

The PS4 game kept the menu pages of the multiplayer boosters but its list source for them is an empty function
(source number 0x43), and its weapon list source (0xa2) has no answer for the weapon statistics bars. PS3 1.19 has
both. This group is the helper's replacement:

- **The boosters list** (`u3x_boosters_source`): every request of the list on PAGE_BOOSTERS and of the page's own
  source: title, row texts (name, level and tier, cost or unlock text, cash, the three tier lines), selection,
  refresh, value queries, cell size, and the click that equips a booster and saves the profile.
- **The row icons** (`u3x_booster_icons`, `u3x_k46_op4`): the booster's icon, the "equipped" check, the "new"
  marker or the lock-status icon of a row, and a level mark put into the icon answer.
- **The level disc** (`u3x_k46_draw`, `u3x_booster_level_icon`): PS3 draws a booster icon on a coloured disc that
  shows the booster's level (0, 1 or 2). The PS4 item draw has no such item type, so the helper draws the disc
  itself, right before the game draws the icon, for every icon item that carries the level mark.
- **Pick sounds** (`u3x_k49_booster`, `u3x_k49_kickback`): the menu sound of a click on a booster row or a kickback
  row ("picked" or "refused").
- **Weapon stats bars** (`u3x_comp_weapon_source`): the competitive weapon list answers which weapon a row is and
  whether it is locked; everything else of that list goes to the game's own source.

Callers (all stay as they are):

- C `boosters_source` (a one-line wrapper today) and C `comp_weapon_source` are called by the helper's event entry
  `u3_profile_event` for every list request, one after the other in its chain of list-source hooks; a result of 0
  passes the request on to the next hook.
- C `k46_draw` is called by the stub dispatcher (`cinema_kind`) for stub kind 168. The builder replaces the first six
  bytes of the game's item draw (image 0x641560) by a jump to a register-saving stub; the helper is called at the
  entry of every UI item draw, and afterwards the original draw runs, whatever the helper returned. (The dispatcher
  first calls a read-only logging function of another group, `l03_item`.)
- C `k46_op4`, `k49_booster` and `booster_icons` are reached through the C exports `u3h_k46_op4`, `u3h_k49_booster`
  and `u3h_booster_icons`, whose only caller is `u3x_boosters_source`; `booster_icons` is also called by `k46_op4`.
- C `k49_kickback` is reached through the C export `u3h_k49_kickback`, whose only caller is the kickbacks list
  source `u3x_kickback_source` (G24) for its click operation.
- `u3x_booster_list`, `u3x_booster_def`, `u3x_booster_unlocked` (C++ today): by this group, by three C one-line
  wrappers (`booster_list`, `booster_def`, `booster_unlocked`), and `u3x_booster_unlocked` also by the kickbacks
  list (G24).
- `u3x_unlock_status` (C++ today): by this group through the C wrapper `unlock_status`, and through the C export
  `u3h_unlock_status` by the co-op weapon mods list (G25).
- `u3x_booster_level_icon` (C++ today): only by `k46_draw`.

Everything runs on the game thread that updates and draws the menus. There is no locking and no timing rule.

## 2. Interface

No function takes or returns a float. All are `extern "C"`. "existing" = exported today under this name, keeps name
and signature. "new" = replaces a C body; the C function of the old name stays as a one-line wrapper.

The group is delivered in two independent parts.

### Part A: the list source and the list accessors

| Function | Signature | Status | Called by |
|---|---|---|---|
| list source | `int u3x_boosters_source(uintptr_t base, int32_t *ev, void *s)` | existing | C `boosters_source` |
| current list | `uint8_t *u3x_booster_list(uintptr_t base, void *s)` | existing (stray, see report) | part A; C `booster_list` |
| definition of a row | `uint8_t *u3x_booster_def(uint8_t *list, int32_t index)` | existing (stray) | parts A and B; C `booster_def` |
| unlocked test | `int u3x_booster_unlocked(void *s, uint32_t user, uint32_t id)` | existing (stray) | parts A and B; C `booster_unlocked`; G24 |

### Part B: icons, level disc, sounds, stats bars

| Function | Signature | Status | Called by | C wrapper passes |
|---|---|---|---|---|
| row icons | `void u3x_booster_icons(uintptr_t base, void *s, int32_t *ev, uint8_t *list, uint32_t slot, uint32_t user, int32_t loadout, uint8_t *profile)` | new (C `booster_icons`) | C `booster_icons`; `u3x_k46_op4` | its eight arguments unchanged |
| row icons with level mark | `int u3x_k46_op4(uintptr_t base, void *s, int32_t *ev, uint8_t *list, uint32_t slot, uint32_t user, int32_t loadout, uint8_t *profile, uint32_t *counters)` | new (C `k46_op4`) | C `k46_op4` | its eight arguments, then the address of the C counter array `k46_d` |
| level disc draw | `int u3x_k46_draw(uintptr_t base, const uint64_t *r, uint32_t *counters)` | new (C `k46_draw`) | C `k46_draw` | base, the stub's register array, the address of `k46_d` |
| level disc icon | `uint32_t u3x_booster_level_icon(uint32_t level)` | existing; its literal id list is replaced by the rule of 3.9 | `u3x_k46_draw` | - |
| booster pick sound | `void u3x_k49_booster(uintptr_t base, void *s, uint8_t *list, uint32_t slot, uint32_t user, int32_t row)` | new (C `k49_booster`) | C `k49_booster` | base, state, list, slot, user, row (its `loadout` and `profile` arguments are not used) |
| kickback pick sound | `void u3x_k49_kickback(uintptr_t base, void *s, uint32_t user, int coop, int32_t row)` | new (C `k49_kickback`) | C `k49_kickback` | its five arguments unchanged |
| weapon stats source | `int u3x_comp_weapon_source(uintptr_t base, int32_t *ev, void *s)` | new (C `comp_weapon_source`) | C `comp_weapon_source` | its three arguments (event second, state third) |
| unlock status | `int u3x_unlock_status(uintptr_t base, void *s, uint32_t user, uint32_t id, uint32_t mod)` | existing (stray) | part B; C `unlock_status` | - |

Parameters:

- `base`: game image base. `s`: the helper's state object (opaque; fields in 3.2).
- `ev`: one data-source request (3.3).
- `list`: a booster list (3.4), as `u3x_booster_list` returned it. `slot`: its slot number, 0 to 3.
- `user`: local user index, already limited to 0 .. 3 by the caller. `loadout`: the s32 at image 0x17d9988.
- `profile`: the menu user's profile object, or null.
- `counters`: four u32 diagnostic counters, called k[0] .. k[3] below (plain 32-bit increments): k[0] discs drawn,
  k[1] icon answers marked, k[2] disc icon missing from the UI table, k[3] disc texture missing.
- `r`: fifteen u64 words, the registers at the entry of the game's item draw: r[7] = r9, r[8] = r8, r[9] = rdi,
  r[10] = rsi, r[11] = rdx, r[12] = rcx (the other words are not used here).
- `row`: the clicked row. `coop`: non-zero for the co-op kickback list.
- `id`, `mod`: an item id and a mod bit (0 for items without mods).

Results: `u3x_boosters_source` and `u3x_comp_weapon_source` return 1 when the request belongs to their list (tag
match), else 0. `u3x_k46_op4` returns 0 when the booster icons are switched off, else 1. `u3x_k46_draw` always
returns 0. `u3x_booster_unlocked` 1 or 0. `u3x_unlock_status` a status 0 .. 255 (0 = owned, 1 = can be bought,
other values = locked in different ways; 3 when it cannot ask).

Functions of the C part, of other groups and existing exported C++ functions that the new code calls (declare them;
do not reimplement them):

| Name | Signature | What it does |
|---|---|---|
| `u3h_ext` | `uint8_t *u3h_ext(void)` | the helper's own state block (3.1). In CONVENTIONS.h, with `ext_ea()` / `ext_eb()` |
| `u3h_gate_closed` | `int u3h_gate_closed(uintptr_t base, int32_t *ev)` | 0 while multiplayer is active (u8 at image 0x17d5c61 non-zero); otherwise stores the game's empty string (image 0x13ac250) as the request's text answer, counts, and returns 1 |
| `u3h_sym_ready` | `int u3h_sym_ready(uintptr_t base, void *s, uint32_t id)` | non-zero when the script module that holds symbol `id` is loaded (the state's "find module" native is called with (`id`, 1)); 0 without that native |
| `u3h_j05_inc` | `void u3h_j05_inc(int i)` | adds 1 to diagnostic counter `i` AND 3 of the C part |
| `u3h_text` | `const char *u3h_text(uintptr_t base, void *s, uint32_t id)` | localized text of a text id through the game's text lookup; never null (empty text when there is none) |
| `u3h_publish` | `int u3h_publish(uintptr_t base, void *s, uint8_t *profile)` | saves the multiplayer record of a profile object (the helper's profile publish); the result is not used here; the argument may be null |
| `u3h_k46_op4` | `int u3h_k46_op4(uintptr_t base, void *s, int32_t *ev, uint8_t *list, uint32_t slot, uint32_t user, int32_t loadout, uint8_t *profile)` | C export that ends in `u3x_k46_op4` (part B) with the counter array added |
| `u3h_k49_booster` | `void u3h_k49_booster(uintptr_t base, void *s, uint8_t *list, uint32_t slot, uint32_t user, int32_t loadout, uint8_t *profile, int32_t row)` | C export that ends in `u3x_k49_booster` (part B) |
| `u3h_booster_icons` | `void u3h_booster_icons(uintptr_t base, void *s, int32_t *ev, uint8_t *list, uint32_t slot, uint32_t user, int32_t loadout, uint8_t *profile)` | C export that ends in `u3x_booster_icons` (part B) |
| `u3h_icon_index` | `int32_t u3h_icon_index(uintptr_t base, uint32_t id)` | index of UI icon `id` in the game's UI texture table (3.7), or −1 (also when the table is not there) |
| `u3h_set_icon` | `void u3h_set_icon(uintptr_t base, int32_t *ev, uint32_t id)` | when `u3h_icon_index(base, id)` is 0 or more: makes the request a plain icon answer, exactly `u3x_icon_answer(ev, 3, 0, id, 1, that index)`; otherwise leaves the request untouched |
| `u3x_icon_answer` | `void u3x_icon_answer(int32_t *ev, int32_t kind, int32_t a, int32_t b, int32_t c, int32_t index)` | image answer: word 2 = `kind`, words 4, 5, 10 = 0, words 6, 7, 8 = `a`, `b`, `c`, word 9 = `index`, word 11 = −1, word 12 = 0x3f800000 (1.0); word 3 is left as it is (G37) |
| `u3x_unlock_icon` | `uint32_t u3x_unlock_icon(int status)` | the UI icon id that stands for unlock status 1, 2 or 3; 0 for every other status |
| `u3x_click_sound` | `int u3x_click_sound(int unlock_status)` | the menu sound of a click: 0 ("picked") for status 0 or 1, 8 ("refused") for every other status |
| `u3x_booster_already` | `int u3x_booster_already(uint32_t slot_word, uint32_t player_word, uint32_t booster)` | 1 when `booster` equals `slot_word`, or, when `slot_word` is 0, equals `player_word`; else 0 |
| `u3x_booster_level_mark` | `uint32_t u3x_booster_level_mark(int level)` | the level mark of 3.3: 0x2e4c0000 OR the low 8 bits of `level` (a negative level counts as 0) |
| `u3x_comp_weapon_answer` | `int u3x_comp_weapon_answer(uint32_t query, uint32_t weapon, int locked, int32_t *answer)` | query 0xa1443de7: `*answer` = 1 when `locked` is non-zero, else 0, result 1; query 0x1720a1ed: `*answer` = `weapon`, result 1; any other query: result 0, `*answer` untouched |
| `u3x_sym_table` | `uint8_t *u3x_sym_table(void *s, uint32_t id)` | data of a script symbol through the state's lookup function; null when the function pointer, the handle or the pointer the handle holds is null |
| `u3x_kickback_nth` | `uint8_t *u3x_kickback_nth(void *s, int32_t n, int coop)` | the `n`-th (from 0) kickback definition of the co-op list (`coop` non-zero) or the competitive list; null when there is none; the kickback's id is its first u32 (G24) |
| `u3h_k49_snd` | `void u3h_k49_snd(uintptr_t base, int n)` | **C export to add** (one line that calls the C function `k49_snd`): plays menu sound `n` through the game's menu sound function (image 0x651fa0, one int argument) and counts it, unless the list sounds are switched off (bit 14 of eb[1]) |

`u3h_ext` is a plain address accessor: how often it is called is not part of the behaviour.

## 3. Game and helper facts

### 3.1 The helper's state block (address from `u3h_ext()`)

The counter array "ea" starts at offset 0x3c50 (`ext_ea()`), the switch array "eb" at 0x4050 (`ext_eb()`); u32
words.

| Word | Meaning |
|---|---|
| ea[0] bit 3 (value 8) | set = the page-level sources are off (the page branch of 4.1 is never taken) |
| ea[0] bit 5 (value 0x20) | set = booster icons off |
| ea[15] | counter: requests answered by the page branch |
| ea[20] | counter: booster icon requests of a row |
| ea[21] | counter: "equipped" checks |
| ea[22] | counter: "new" markers |
| ea[23] | counter: lock-status icons |
| ea[114] | counter: weapon stats answers |
| eb[1] bit 12 (value 0x1000) | set = no level mark and no level disc |
| eb[1] bit 22 (value 0x400000) | set = the page title is not answered before the table test (4.1 step 4) |

Counters are plain 32-bit increments (wrapping).

### 3.2 The state object `s`

| Offset | Type | Meaning |
|---|---|---|
| 656 | pointer to function | script symbol lookup (used through `u3x_sym_table`) |
| 760 | u64 | the image base; the two list sources of this group store `base` here on the requests named in section 4, and `u3x_booster_unlocked` reads it from here |
| 768 + 8·k | pointer to function | "native k": a game function the helper found at start. Any of them may be null |
| 0x2c80 | 251 characters | a text buffer shared with other lists; the text answers composed here stay valid until the next request |
| 0x3000 | 4 × 0x210 bytes | the four booster lists, one per slot (3.4) |
| 0x3840 + 4·u | s32 | the selected row of local user u (u = 0 .. 3) |
| 0x3850 | u32 | "built" bits: bit n set = the list of slot n has been filled |

Natives used here (the game function each one normally holds is given for orientation only; always call through the
pointer in the state object and treat a null pointer as said in section 4):

| k | Arguments | Result | Game function |
|---|---|---|---|
| 23 | (image 0x17c3b40, u32 user, definition pointer) | s32 level of the booster for the user: 0, 1, 2 ..., −1 for none | 0x6882b0 |
| 24 | (image 0x17f3068, s32 id, s32 mod) | pointer: the unlock entry of an item (may be null; it is passed on as it is) | 0x7bb470 |
| 25 | (image 0x17f3068, u32 user, entry pointer) | low 8 bits non-zero = unlocked | 0x7bba70 |
| 26 | (u32 id, u32 0) | pointer to a text: what unlocks the item / its price | 0x751b30 |
| 27 | (image 0x17cb060) | low 8 bits non-zero = cash features are on | 0x7270f0 |
| 28 | (profile pointer, s32 item, u32 value, s32 loadout) | none: stores a loadout item | 0x780220 |
| 29 | (list pointer, u32 flag) | none: fills a booster list (3.4) for the slot in its first byte; flag 1 = co-op loadout | 0x7596a0 |
| 31 | (image 0x17da200 + 0x1018·user, u32 id) | pointer, non-null = a mission tier is open for the item | 0x775d30 |
| 32 | (image 0x17f3068, u32 user, u32 id, u32 0) | none: marks the item as seen (no longer "new") | 0x7bda50 |
| 34 | (profile pointer, s32 item, s32 loadout) | u32: a loadout item | 0x7800d0 |
| 35 | (image 0x17f3068, u32 user, entry pointer) | s32 unlock status; only the low 8 bits count | 0x7bca00 |
| 36 | (image 0x17f3068, u32 user, u32 id, u32 0) | low 8 bits non-zero = the item shows the "new" marker | 0x7bd6f0 |

Loadout item numbers: booster slot n (n = 0 .. 2) is loadout item n + 4.

### 3.3 Data-source requests

A request is an array of 32-bit words.

| Field | Meaning |
|---|---|
| word 0 | operation |
| word 1 | row index (in); the answer of operations 5, 6 and 0x11 (out) |
| word 2, 4 .. 12 | image answer (`u3x_icon_answer`); word 2 is the item type, 3 = plain UI icon, 0x2f = the "new" marker |
| word 4 | in a plain icon answer: free; the helper puts the **level mark** here: 0x2e4c0000 OR level (0 .. 255) |
| word 7, 8, 9 | in a plain icon answer: the icon id, 1 (the UI icon class), the icon's index in the UI texture table |
| word 0xe | the answer of a value query (operation 11, and 19) |
| word 0x13 | the id of the value asked (operation 11) |
| word 0x14 | the image slot asked (operation 4): 0xf5a294f3 = the booster's icon, 0 = the status picture |
| word 0x15 | the text slot asked (operation 2) |
| word 0x17 | the clicked row (operation 0x16) |
| word 0x1f, 0x20 | cell size (operation 12), two float patterns |
| word 0x24 | 0x89e96364 when the request comes from the page itself (PAGE_BOOSTERS) and not from its list |
| pointer at byte 0x40 | the text answer |
| pointer at byte 0x98 | the source descriptor; its u32 at +0x1c is the source tag |

Source tags: 0x4c414e43 = the boosters list (and the page source of PAGE_BOOSTERS), 0x4c414ea2 = the competitive
weapon list.

The game turns an image answer into an item layout by copying the answer from word 2 on, so in the layout the item
draw receives: u32 at +0 = item type, u32 at +8 = the level mark, u32 at +0x14 = icon id, u32 at +0x18 = icon
class, u32 at +0x1c = UI index, pointer at +0x48 = the icon's texture record (filled by the game's layout step).

### 3.4 Booster lists and definitions

A booster list is 0x210 bytes: u8 at +0 = the slot number it is for; pointers to booster definitions from +8 (8
bytes each); s32 at +0x208 = the number of definitions. The game fills it (native 29).

A booster definition (script data, table symbol 0x916e03b0):

| Offset | Type | Meaning |
|---|---|---|
| 4 | u32 | booster id |
| 8 | u32 | text id of the name |
| 0xc | s32 | the slot the booster belongs to (0 .. 2 loadout slots, 3 = the paid slot) |
| 0x10 | s32 | number of tiers (levels) |
| 0x18 | pointer | the tiers, 0x10 bytes each; u32 at +0 of a tier = text id of its description |
| 0x20 | s32 | cost |
| 0x28 | u32 | UI icon id |

### 3.5 Profile objects

The pointer at image 0x17dd610 is an array of profile objects of 0x7b00 bytes, one per local user. "Profile object
of user u" = that pointer + 0x7b00·u, or null when the pointer is null. The u32 at +0x74c of a profile object is the
player's cash.

"Menu user" = 1 when the u8 at image 0x17bde75 is non-zero, else the u32 at image 0x17bf714.

### 3.6 Game data

| Image offset | Type | Meaning |
|---|---|---|
| 0x17d9988 | s32 | the loadout being edited; 4 = the co-op loadout |
| 0x17d9990 | u32 | the booster slot being edited (0 .. 3) |
| 0x17d9994 | u8 | non-zero: the weapon list shows the second weapon table (symbol 0xe77d8fba), else the first (0xa94f33c0) |
| 0x17c3b40 + 4·u | u32 | the paid-slot booster of user u (u = 0 or 1). (The same address is the first argument of native 23.) |
| 0x17c3b48 + 4·n | u32 | override word of booster slot n (n = 0 .. 3): non-zero = the booster shown as equipped in that slot |
| 0x1b6a630 + row | u8 | weapon list: the table index of visible row `row` |
| 0x1b6a6b0 | u32 | weapon list: number of visible rows |
| 0x17f3068 | object | unlock manager (argument of natives 24, 25, 32, 35, 36) |
| 0x17cb060 | object | net game manager (argument of native 27) |
| 0x17da200 + 0x1018·u | object | missions of user u (argument of native 31) |

A weapon table (data of symbol 0xa94f33c0 or 0xe77d8fba): pointer at +8 = entries of 0x28 bytes; u32 at +0 of an
entry = weapon id.

### 3.7 Game functions called directly

| Image offset | Call | Meaning |
|---|---|---|
| 0x766870 | (s32 *request) | the game's own source of the competitive weapon list |
| 0x11b89b0 | (u64 key, s32 index) → pointer or null | the texture record of a UI icon: key = the icon id in the low 32 bits and the icon class (1) in the high 32 bits; index = the icon's index in the UI texture table. It uses no float register |
| 0x641560 | (layout, a2, context, position pair, u32 colour, u32 flag), plus one float in xmm0 | the game's item draw (the function the stub of kind 168 sits on) |

The UI texture table is reached only through `u3h_icon_index` and 0x11b89b0.

### 3.8 Ids and texts

Text ids (looked up with `u3h_text`): 0x2239ab55 the page's button prompt; 0x77789975, 0x73b984c2, 0xf917b294 the
list titles of slot 0, 1 and 3; 0xadfc7da6 the word "Level"; 0x6ba044a1 the cost label; 0xba6d58f7 the cash label.

Text slots (word 0x15): 0 name, 3 level and tier, 4 cost, 8 cash, 0x80d4cc7b / 0xadacdbf3 / 0xb684d68b the lines of
tier 1 / 2 / 3.

Value ids (word 0x13): boosters list 0x67a48eb9 (level number), 0x18567c73 (mission tier open); weapon list
0xa1443de7 (locked), 0x1720a1ed (weapon id).

Other ids: action 0x92f7fefa (the page's button action); UI icons 0x08208d0a (the "equipped" check) and 0xd8b04236
(the icon shown for a booster whose own icon is not in the UI texture table).

Texts of the helper's own (they must live in read-only helper data): an empty text; a single space " "; a line
break (one character, 0x0a); ": " (colon, space); the colour escapes "|@Cffff0000|" (cost above the cash),
"|@Cff7cce96|" (cost within the cash) and "|@Cff00ff00|" (cash).

### 3.9 The level disc icons: a rule, not a table

The disc of booster level 0, 1 or 2 is a UI icon of the multiplayer art (three pictures). Their icon ids are
PS3 1.19 values: the PS4 image holds none of them (no occurrence of the three 32-bit patterns anywhere in the
image file), so they cannot be read from the running game. They are generated by a rule:

- The disc icon of level n (n = 0, 1, 2) is the string id of the texture name "general/net/boosters-" followed by
  the one decimal digit n followed by "/background". Every other level has no disc (id 0).
- The string id of a name is the 32-bit CRC of its characters (without the terminating NUL) with the POSIX `cksum`
  polynomial 0x04C11DB7, processed most significant bit first (not reflected), start value 0, and **no** final
  inversion and no appended length (the "CRC-32/CKSUM" algorithm of the public CRC catalogue, without its output
  inversion). Check values: the nine characters "123456789" give 0x89a1897f, and the name
  "general/net/menu-icons/icon-check" gives 0x08208d0a (the check icon of 3.8).

The CRC needs no 256-entry table (the names are about 33 characters; a bit-by-bit computation is enough). Whether
the three ids are computed on every call or kept after the first is not observable.

There is no other literal table of game values in this group. The booster definitions, the booster lists, the
weapon tables and the weapon row map are game data read at run time (3.4, 3.6); the three lock-status icon ids stay
inside the existing `u3x_unlock_icon` (not part of this group).

## 4. Behaviour

### 4.0 The three list accessors and the unlock status

**`u3x_booster_list`.** slot = u32 at image 0x17d9990; a value above 3 counts as 0. The result is the list of that
slot inside the state object (s + 0x3000 + 0x210·slot); before returning, its first byte is set to the slot number
(always, also when the list is not filled yet).

**`u3x_booster_def`.** Null when `index` is negative or not below the list's count (s32 at list + 0x208, signed
comparison); otherwise the pointer stored at list + 8 + 8·`index` (which may itself be null: callers treat null as "no such
row").

**`u3x_booster_unlocked`.** 0 when native 24 or native 25 is null (no call). Otherwise, with B = the image base
stored in the state object (offset 760): entry = native 24(B + 0x17f3068, `id`, 0); the result is 1 when the low 8
bits of native 25(B + 0x17f3068, `user`, entry) are non-zero, else 0. The entry is not tested for null.

**`u3x_unlock_status`.** 3 when native 24 or native 35 is null (no call). Otherwise entry = native 24(base +
0x17f3068, `id`, `mod`); the result is the low 8 bits (0 .. 255) of native 35(base + 0x17f3068, `user`, entry).

### 4.1 `u3x_boosters_source`

"Text answer = T" means: the pointer at byte 0x40 of the request becomes T. "Definition of row i" =
`u3x_booster_def(list, i)`. "Level of def" = native 23(base + 0x17c3b40, user, def). "Unlocked(id)" =
`u3x_booster_unlocked(s, user, id)`. "Fill the list" = native 29(list, 1 when the loadout is 4, else 0), then the
built bit of the slot is set.

Steps, in this order:

1. Source descriptor (pointer at byte 0x98) null, or its tag not 0x4c414e43: return 0, nothing touched.
2. `u3h_gate_closed(base, ev)` non-zero: return 1.
3. The image base is stored in the state object (offset 760).
4. Early page title: when the operation is 1, the request is from the page (word 0x24 = 0x89e96364), bit 3 of ea[0]
   is clear and bit 22 of eb[1] is clear: text answer = the helper's empty text, `u3h_j05_inc(0)`, return 1.
5. `u3h_sym_ready(base, s, 0x916e03b0)` is 0 (the booster table is not loaded): for operation 6, word 1 = −1; for
   operation 1, `u3h_j05_inc(1)` (the text answer is not written); return 1 in every case.
6. Set-up: list = `u3x_booster_list(base, s)`; slot = its first byte; user = the menu user AND 3; loadout = s32 at
   image 0x17d9988; profile = the profile object of the menu user (null when the profile array pointer is null or the
   menu user is 4 or more); cash = u32 at profile + 0x74c, or 0 without profile. When the built bit of the slot is clear and
   native 29 is not null: fill the list. (With a null native 29 the bit stays clear and the list is used as it is.)
7. **Page branch**: when the request is from the page and bit 3 of ea[0] is clear: ea[15] += 1, then by operation,
   then return 1:

   | Operation | Effect |
   |---|---|
   | 6 | word 1 = −1. Then def = definition of the user's selected row; none: done. Then own = level of def (−1 when native 23 is null). Only when own is below (tier count − 1), both compared as unsigned 32-bit values: when unlocked(id of def), word 1 = 0. (So: the page's button is offered while a further tier exists for an unlocked booster.) |
   | 2 | only when word 0x15 is 0: text answer = `u3h_text(base, s, 0x2239ab55)` |
   | 11 | word 0xe = 8 |
   | 19 | word 0xe = 0x92f7fefa |
   | 1 | text answer = the helper's empty text (reached only with bit 22 of eb[1] set) |
   | other | nothing |

8. Operation 4: `u3h_k46_op4(base, s, ev, list, slot, user, loadout, profile)`; a non-zero result: return 1.
9. Operation 0x16: `u3h_k49_booster(base, s, list, slot, user, loadout, profile, word 0x17)` (the sound), then go on.
10. By operation, then return 1 (also for operations not listed):

| Operation | Effect |
|---|---|
| 1 | slot 0, 1, 3: text answer = `u3h_text` of the slot's title id (3.8). Slot 2: text answer = the helper's empty text |
| 2 | row text, see below |
| 4 | (only reached when step 8 returned 0) when bit 5 of ea[0] is clear: `u3h_booster_icons(base, s, ev, list, slot, user, loadout, profile)` |
| 5 | word 1 = the user's selected row |
| 6 | word 1 = the list's count − 1 |
| 9 and 0x17 | when native 29 is not null: fill the list (again). Then def = definition of the user's selected row; when there is one and native 32 is not null: native 32(base + 0x17f3068, user, id of def, 0) |
| 10 | when row (word 1) has a definition: the user's selected row = word 1 |
| 11 | def = definition of row (word 1); none: nothing. Value 0x67a48eb9: when unlocked(id of def) (evaluated first) and native 23 is not null: word 0xe = level of def + 1. Value 0x18567c73: when native 31 is not null: word 0xe = 1 when native 31(base + 0x17da200 + 0x1018·user, id of def) is non-null, else 0. Other values: nothing |
| 12 | when bit 5 of ea[0] is clear: word 0x1f = word 0x20 = 0x43000000 (128.0) |
| 0x11 | word 1 = 8 |
| 0x16 | equip, see below |

**Operation 2 (row text).** def = definition of row (word 1); none: nothing at all. Otherwise, always: own = level of def
(−1 when native 23 is null; the native is called once per request, before any text lookup), and the first byte of
the text buffer (s + 0x2c80) is set to 0. Then by text slot (word 0x15), with T(k) = `u3h_text` of the text id of
tier k (u32 at tiers + 0x10·k):

| Text slot | Text answer |
|---|---|
| 0 | `u3h_text(base, s, name id of def)` |
| 3 | nothing when own is negative, own is not below the tier count, or the tiers pointer is null. Else the buffer: "Level" text (0xadfc7da6), a space, own + 1 in decimal, a line break, T(own) |
| 4 | not unlocked(id of def): the text native 26(id, 0) returns, or the helper's empty text when native 26 is null. Unlocked: when the cost is below 1, or native 27 is null, or the low 8 bits of native 27(base + 0x17cb060) are 0: the helper's empty text. Else the buffer: cost label (0x6ba044a1), then "\|@Cffff0000\|" when cash is below the cost (unsigned comparison) or "\|@Cff7cce96\|" otherwise, then the cost in decimal with thousands separators |
| 8 | the buffer: cash label (0xba6d58f7), "\|@Cff00ff00\|", the cash (unsigned) in decimal with thousands separators |
| 0x80d4cc7b, 0xadacdbf3, 0xb684d68b | n = 0, 1, 2. Nothing when the tiers pointer is null or n is not below the tier count. When n = 0 and the booster has exactly one tier: text answer = T(0) directly (no buffer). Else the buffer: "Level" text, a space, n + 1 in decimal, ": ", T(n) |
| other | nothing |

Evaluation order inside a row: the unlocked test (natives 24, 25) comes before native 26 or 27; texts are looked up
in the order they appear in the composition (the "Level" text before the tier text; the label before the number).

Composition rule for the buffer: the pieces are written one after the other; the result is cut after 250
characters; a NUL follows. "Decimal" is plain digits. "With thousands separators" puts a comma between groups of
three digits counted from the right ("1,234,567"; no comma for values below 1000). The numbers of this group are
never negative.

**Operation 0x16 (equip).** def = definition of row (word 0x17); none: nothing. Not unlocked(id of def): nothing.
Otherwise, with home = the slot field of def (s32 at def + 0xc):

1. home within 0 .. 3: the override word of slot home (image 0x17c3b48 + 4·home) = 0.
2. home within 0 .. 2 and profile not null: when native 28 is not null, native 28(profile, home + 4, id of def, loadout).
   home = 3: the paid-slot word of user (user AND 1) (image 0x17c3b40 + 4·(user AND 1)) = id of def.
   (home within 0 .. 2 without profile: nothing more. Other home values: nothing.)
3. In every case (also for a home slot outside 0 .. 3): `u3h_publish(base, s, the profile object of user)` (3.5; the
   argument is null when the profile array pointer is null).

### 4.2 `u3x_booster_icons`: the picture of a row

def = `u3x_booster_def(list, word 1)`; none: nothing.

**Image slot 0xf5a294f3 (the booster's icon).** The level is asked: native 23(base + 0x17c3b40, `user`, def), unless native 23 is
null (the value is not used further here, but the call is made). ea[20] += 1. Then: when the booster's
icon id (u32 at def + 0x28) is in the UI texture table (`u3h_icon_index` ≥ 0), `u3h_set_icon(base, ev, that id)`;
otherwise `u3h_set_icon(base, ev, 0xd8b04236)` (which answers nothing when that icon is missing too). Done.

**Image slot 0 (the status picture).** In this order, the first that applies ends the request:

1. Equipped: worn = the override word of the slot (image 0x17c3b48 + 4·(`slot` AND 3)). When it is 0: for `slot`
   0 .. 2, worn = native 34(`profile`, `slot` + 4, `loadout`) when both `profile` and native 34 are non-null, else 0; for
   `slot` 3, worn = the paid-slot word of user (`user` AND 1). When worn is non-zero and equals the id of def:
   `u3h_set_icon(base, ev, 0x08208d0a)`, ea[21] += 1 (counted even when the icon is missing from the table), done.
2. New: when native 36 is not null and the low 8 bits of native 36(base + 0x17f3068, `user`, id of def, 0) are
   non-zero: `u3x_icon_answer(ev, 0x2f, 0, 0, 0, −1)`, ea[22] += 1, done.
3. Lock status: when native 35 or native 24 is null: nothing. Otherwise entry = native 24(base + 0x17f3068, id of
   def, 0), status = native 35(base + 0x17f3068, `user`, entry) (the full s32 result), icon =
   `u3x_unlock_icon(status)`; when icon is non-zero: `u3h_set_icon(base, ev, icon)` and ea[23] += 1 (counted even when
   the icon is missing from the table).

**Any other image slot:** nothing.

### 4.3 `u3x_k46_op4`: row icons with the level mark

1. Bit 5 of ea[0] set: return 0, nothing else.
2. `u3x_booster_icons(base, s, ev, list, slot, user, loadout, profile)` (4.2).
3. Return 1 without a mark when: bit 12 of eb[1] is set; or word 0x14 is not 0xf5a294f3; or word 2 is not 3 after
   step 2 (no plain icon was answered); or `u3x_booster_def(list, word 1)` is null.
4. Level for the mark: 0 when the answered icon (word 7) is 0xd8b04236; otherwise native 23(base + 0x17c3b40,
   `user`, def) once more (0 when the native is null), a negative level counting as 0.
5. Word 4 = `u3x_booster_level_mark(level)`; k[1] += 1; return 1.

(For a booster icon request native 23 is therefore called twice: once in 4.2 and once here, unless the fallback
icon was answered.)

### 4.4 `u3x_booster_level_icon`

`level` 0, 1 or 2: the string id of that level's disc texture name (3.9). Any other value: 0.

### 4.5 `u3x_k46_draw`: the level disc

layout = the pointer r[9]. Return 0 with nothing done when: bit 12 of eb[1] is set; or layout is not a plausible
object pointer (`ptr_ok` of CONVENTIONS.h); or the item type (u32 at layout + 0) is not 3; or the upper 16 bits of the
u32 at layout + 8 are not 0x2e4c (no level mark).

Otherwise, with level = the low 8 bits of the u32 at layout + 8:

1. icon = `u3x_booster_level_icon(level)`; 0 (level above 2): return 0.
2. index = `u3h_icon_index(base, icon)`; negative: k[2] += 1, return 0.
3. texture = game function 0x11b89b0(icon in the low 32 bits with 1 in the high 32 bits, index); null: k[3] += 1,
   return 0.
4. A local copy of the first 0x70 bytes of layout is made (on the stack: the function is entered again from inside
   step 5), in which: u32 at +8 = 0 (no mark), u32 at +0x14 = icon, u32 at +0x18 = 1, u32 at +0x1c = index,
   pointer at +0x48 = texture. Everything else as copied. layout itself is never modified.
5. Game function 0x641560 is called with (the copy, r[10], r[11], r[12], r[8], r[7]): the same second argument,
   context, position pair, colour and flag the game passed for the icon. Its float argument must be the one the
   game passed too: at entry of the helper xmm0 still holds it (the stub saves and restores the float state around
   the helper, it does not change it), neither `u3h_icon_index` nor 0x11b89b0 uses a float register, so the
   function only has to make sure that it executes no instruction that writes a vector register before this call
   (no `f_*` helper, no `GameCall` block: a plain call with six integer arguments).
6. k[0] += 1; return 0.

The nested draw enters the stub again with the copy; the copy has no mark, so that inner call of this function
returns at once. The game then draws the icon itself on top of the disc.

### 4.6 `u3x_k49_booster` and `u3x_k49_kickback`: the pick sounds

"Sound(n)" = `u3h_k49_snd(base, n)`.

**`u3x_k49_booster`.** def = `u3x_booster_def(list, row)`; none: nothing. With id = the booster id of def:

- `slot` = 3 (the paid slot): nothing when the booster is already there, that is
  `u3x_booster_already(override word of slot 3 (image 0x17c3b54), paid-slot word of user (user AND 1), id)` is
  non-zero; nothing when `u3x_booster_unlocked(s, user, id)` is 0 (evaluated only when the first test was 0);
  otherwise sound(0).
- any other `slot`: sound(`u3x_click_sound(u3x_unlock_status(base, s, user, id, 0))`).

**`u3x_k49_kickback`.** `row` 0 or negative (the "None" row): sound(0). Otherwise entry =
`u3x_kickback_nth(s, row − 1, coop)`; none: nothing; else
sound(`u3x_click_sound(u3x_unlock_status(base, s, user, first u32 of entry, 0))`).

### 4.7 `u3x_comp_weapon_source`: weapon stats bars

1. Source descriptor null, or its tag not 0x4c414ea2: return 0, nothing touched.
2. `u3h_gate_closed(base, ev)` non-zero: return 1.
3. Operation other than 11: game function 0x766870(ev); return 1.
4. Operation 11 (the game's source is not called): the image base is stored in the state object (offset 760).
   Return 1 with no answer when: row (word 1, unsigned) is not below the u32 at image 0x1b6a6b0; or
   `u3x_sym_table(s, 0xe77d8fba when the u8 at image 0x17d9994 is non-zero, else 0xa94f33c0)` is null; or the
   table's entry pointer (at +8) is null.
5. weapon = the first u32 of the entry number (u8 at image 0x1b6a630 + row) of that table (entries of 0x28 bytes;
   the entry number is not compared with any count). user = the menu user AND 3.
6. Value 0xa1443de7 (word 0x13): word 0xe = 1 when `u3x_booster_unlocked(s, user, weapon)` is 0, else 0;
   ea[114] += 1. Value 0x1720a1ed: word 0xe = weapon; ea[114] += 1 (the unlocked test is not made). Any other value:
   nothing. (This is `u3x_comp_weapon_answer` with the locked flag computed only for the first value.) Return 1.

## 5. Test scenarios

Everything except the two points at the end can be tested with a fake image (the fields of 3.5 and 3.6), a state
object whose natives are recording stand-ins, and recording stubs for the `u3h_` / `u3x_` functions of the second
table of section 2 and for the three game functions of 3.7. Compare memory (request, state object, image fields,
ea / eb, the counters), results, and the order and arguments of all calls.

List source:
1. Null descriptor; wrong tag (0, nothing touched). Gate closed (1, nothing else).
2. Early page title: operation 1 from the page with both switch bits clear (empty text, counter 0); with bit 22 of
   eb[1] set and the table not ready (counter 1, no text); with bit 22 set and the table ready (page branch, empty
   text, ea[15]); with bit 3 of ea[0] set (list title of the slot instead).
3. Table not ready: operations 6 (word 1 = −1), 1, and another one.
4. Lazy fill: built bit clear / set, native 29 null; loadout 4 and not 4 (flag 1 / 0); slot value 5 at 0x17d9990
   (list 0, first byte 0).
5. Page branch operation 6: no selected definition; native 23 null; level = tiers − 2, tiers − 1, −1; tier count 0;
   unlocked and not. Operations 2 (word 0x15 = 0 and 1), 11, 19, 1, and an unlisted one.
6. Operation 4: `u3h_k46_op4` returning 1 (nothing more) and 0 with bit 5 of ea[0] clear (then `u3h_booster_icons`)
   and set.
7. Operation 1 for slots 0, 1, 2, 3.
8. Operation 2: no definition (buffer untouched); each text slot; slot 3 with level −1, level = tier count, null tiers;
   slot 4 locked with and without native 26, unlocked with cost 0, native 27 null, native 27 answering 0, cash
   below / equal / above the cost, cost 999, 1000 and 1234567; slot 8 with cash 0 and 4294967295 and without a
   profile; tier slots with one tier (direct text), two tiers, n beyond the count; a tier text long enough to cut
   the composition at 250 characters. Check the first buffer byte is cleared for every slot once the definition
   exists, and the order level native → unlocked natives → texts.
9. Operations 5, 6, 10 (valid and invalid row), 0x11, 12 (bit 5 clear / set), an unlisted operation (1, nothing).
10. Operations 9 and 0x17: native 29 null / present, selected definition present / absent, native 32 null.
11. Operation 11: both values and another; locked; native 23 null; native 31 null, returning null and non-null.
12. Operation 0x16: the sound call first with word 0x17; no definition; locked; home = 0, 2 (with and without profile, with
    and without native 28), 3 (user 0, 1, 2, 3: the word of user AND 1), 4 and −1 (publish only); profile array
    null (publish with null).

Accessors:
13. `u3x_booster_def`: i = −1, count − 1, count; a null stored pointer. `u3x_booster_unlocked`: either native null;
    native 25 answering 0, 1 and 0x100 (0x100 counts as not unlocked); base taken from offset 760.
    `u3x_unlock_status`: either native null (3); status 0, 1, 2, 0x103 (3); mod passed on.

Icons:
14. `u3x_booster_icons`: no definition; other image slot. Icon slot: icon in the table / not in the table with the
    fallback present / both missing; native 23 null. Status slot: override word set and equal / different; override
    0 with slot 0 .. 2 (native 34 answering the id, profile null, native null) and slot 3 (user 0 and 1); worn 0 while the
    id is 0; new marker (native 36 null, 0, 1); status 0, 1, 2, 3, 4 with natives 24 / 35 null; check icon missing
    from the table (counter still moves).
15. `u3x_k46_op4`: bit 5 of ea[0] set (0, nothing); bit 12 of eb[1] set; status slot; nothing answered (word 2 not
    3); normal icon with level −1, 0, 2, 300 (mark low byte 0x2c); fallback icon (mark level 0, native 23 called
    once only); k[1].
16. `u3x_booster_level_icon`: levels 0, 1, 2 give three different non-zero ids that are found by the CRC rule of
    3.9 (checked against the old function), 3 and 0xffffffff give 0; the two check values of 3.9.

Disc:
17. `u3x_k46_draw`: switch set; layout null / odd / type 2 / no mark / mark with level 3 and 0xff (nothing); icon not
    in the table (k[2]); texture null (k[3]); normal: the copy's fields, layout unchanged, the six arguments of the
    nested call, k[0], result 0; a nested call that calls the function again with the copy (returns at once).

Sounds and stats bars:
18. `u3x_k49_booster`: no definition; slot 3 already equipped through the override word and through the user
    word, locked, unlocked; slot 0 with status 0, 1, 2, 3. `u3x_k49_kickback`: row −1, 0; row 1 without and with an
    entry, status 0 and 2; coop 0 and 1 passed on.
19. `u3x_comp_weapon_source`: wrong tag; gate closed; operation 4 (game source called, nothing else); operation 11
    with the row at the count, the symbol missing, the entry pointer null, both tables (u8 at 0x17d9994 0 / 1), both
    values (locked and unlocked) and another value; ea[114]; the base stored at offset 760 only for operation 11.

Needs the live game: that the disc is really drawn under the icon with the icon's size and colour (the float
argument in xmm0 reaching the nested draw untouched cannot be seen by a call-recording stub, only by a host stub
that reads the register, or on screen), and that the sounds play.

## 6. Things that look like bugs or dead code (behaviour to keep)

1. `u3x_boosters_source` operation 4: with the real `u3h_k46_op4` the call of `u3h_booster_icons` in step 10 can
   never happen (that function returns 0 only when bit 5 of ea[0] is set, and then step 10 does nothing). It is
   kept as described because the two are separate functions.
2. Operation 2 clears the first byte of the shared text buffer and asks the level (native 23) for every text slot,
   also for those that use neither.
3. The page branch's operation 1 is reachable only with bit 22 of eb[1] set; without it step 4 answers first.
4. With a null native 29 the list is used unfilled (count as left by earlier use, initially 0).
5. Equip publishes the profile even when the booster's slot field is outside 0 .. 3 and nothing was stored, and
   with a null profile pointer. For slots 0 .. 2 without a profile object nothing is stored but the override word is
   still cleared.
6. Equip and the status picture use the paid-slot word of user AND 1, although the user index is limited to 0 .. 3.
7. `u3x_booster_icons` asks the level (native 23) and never uses it; `u3x_k46_op4` asks again. The counters ea[21]
   and ea[23] also count answers that were not written because the icon is missing from the UI texture table.
8. `u3x_booster_icons` compares the unlock status as the full s32 the native returns, `u3x_unlock_status` (used
   for the sounds) only its low 8 bits.
9. `u3x_k46_op4` decides "a plain icon was answered" from word 2 of the request alone; a request that already
   came with 3 in word 2 and got no answer would be marked. (The game clears requests before sending them.)
10. `u3x_k46_draw` copies only the first 0x70 bytes of the layout; anything the game's draw read beyond would come
    from the helper's stack. The game's plain icon draw does not read there.
11. `u3x_comp_weapon_source` does not compare the entry number from the row map with the table's count, and stores
    the base in the state object only for operation 11.
12. `u3x_booster_unlocked` takes the image base from the state object instead of a parameter; it relies on a list
    source having stored it (both sources of this group do before they use it; the sound functions run after
    `u3x_boosters_source` stored it).
13. In the old C part the helper function `icon46` carries a level argument it ignores (the level reaches the item
    only through the mark of 4.3): nothing to deliver.

## 7. Open questions

1. The rule of 3.9 was found by computing the CRC of the art names the old comments give ("boosters-0 / 1 / 2
   background") and comparing with the three ids of the old code: all three match, and so do two more known pairs
   (the check icon, the fallback icon as "general/net/boosters-0/default"). The game has its own "string id of a
   name" function (a function pointer at image 0x1702720, wrapped by the existing `u3x_sid`); it was not checked
   whether it returns the same values for these names in the running game, so the description asks for the CRC to
   be computed by the helper itself.
2. `u3h_k49_snd` does not exist yet: the integrator adds this one-line C export (the C function `k49_snd` and its
   counters stay in C; the co-op mod sound of another group uses them too).
3. `u3x_unlock_status` is also the status function of the co-op weapon mods list (G25). It is described here
   because no group lists it; if G25's description covers it too, one of the two has to drop it.
4. What the float argument of the item draw (xmm0 of 0x641560) means is not known; it is passed through.

## 8. Clarifications (tester, 2026-11-14)

Facts confirmed by the differential test (`reimpl2\G23\test_g23.cpp`, old v7.212 against the new parts A and B). They
settle the implementers' questions. Nothing in sections 1 to 6 was found wrong; these items make precise what the
text leaves open.

1. **Comparisons with the tier count.** Only the page's operation 6 (4.1 step 7) compares unsigned: level and
   (tier count − 1) as unsigned 32-bit values, so a tier count of 0 lets every level from 0 on through and a level
   of −1 never passes. Everywhere in operation 2 the comparisons are signed 32-bit: text slot 3 answers nothing
   when the level is negative or not below the tier count, the tier slots answer nothing when n is not below the
   tier count; a negative tier count therefore gives no text at all. "Exactly one tier" is tier count = 1.
2. **Cost text, order of the tests.** For an unlocked booster the cost is tested first, then the pointer of
   native 27, and only then is native 27 called: with a cost below 1 native 27 is not called. The cost is a signed
   value for the "below 1" test and is compared with the cash as unsigned.
3. **Results of the natives.** Natives 25, 27 and 36: only the low 8 bits count (0x100 is "no"). Native 23 and
   native 35: a signed 32-bit value (the upper half of the register is ignored). Native 34: an unsigned 32-bit
   value. Native 31: a pointer, tested for null with all 64 bits (0x1_0000_0000 is non-null). The entry native 24
   returns is passed on with all 64 bits, also when it is null. Native 26's result becomes the text answer as it
   is, also when it is null.
4. **`u3h_gate_closed`, `u3h_sym_ready`, `u3h_k46_op4`:** every non-zero result counts as "yes".
5. **Menu user of 4 or more (4.1).** user = menu user AND 3 is used for the selected row, the natives, the
   paid-slot word and the profile object that is published; the profile used for the cash, for native 28 and
   passed to the three exports is null. So an equip then clears the override word, does not call native 28 for
   home slots 0 .. 2, stores the paid-slot word of user AND 1 for home slot 3, and publishes the profile object of
   user (menu user AND 3), which is not null when the profile array exists. The cash counts as 0.
6. **Operation 4 of the list (4.1 steps 8 and 10).** `u3h_k46_op4` is called first, whatever bit 5 of ea[0] holds.
   Only after it returned 0 is bit 5 tested, and `u3h_booster_icons` called when it is clear.
7. **Operation 0x16 (4.1 step 9).** The sound export is called before anything of the equip (before the unlocked
   test and its natives), with the profile of the menu user (null for a menu user of 4 or more) and word 0x17.
8. **Operation 11 of the list, value 0x67a48eb9.** The unlocked test (natives 24, 25) is always made first; a
   null native 23 is noticed only afterwards. A level of −1 gives the answer 0.
9. **Operations 9 and 0x17.** The fill (native 29, then the built bit of the slot) comes before the selected row
   is looked up, so the row is looked up in the list as the game just filled it.
10. **The text buffer while a text is composed.** Observable are: its first byte, cleared after the level native
    and before any text lookup, for every text slot of a row that has a definition; and its final content with
    the NUL after at most 250 characters. What the rest of the buffer holds between two text lookups of one
    request is not observable (the old code wrote its NUL once, at the end; the lookups do not read the buffer).
11. **`u3x_booster_icons`, slot argument above 3 (4.2).** The override word of (slot AND 3) is read; when it is 0
    nothing counts as worn (neither native 34 nor a paid-slot word is used).
12. **`u3x_k46_op4` (4.3).** The level of the mark is the result of the second call of native 23 (the one of step
    4), not of the call made in 4.2. A request that arrives with 3 in word 2 and gets no answer (the booster's
    icon and the fallback icon both missing from the UI texture table) is marked, with word 7 as it arrived
    deciding whether native 23 is asked (section 6 item 9).
13. **`u3x_k46_draw` (4.5).** The level is the low 8 bits of the mark; bits 8 to 15 of the mark are ignored. The
    texture pointer is tested for null with all 64 bits. Whether the copy is made before or after the two lookups
    is not observable.
14. **`u3x_k49_booster` (4.6).** Every `slot` other than 3 (also 4 and above) takes the unlock-status branch.
15. **`u3x_comp_weapon_source` (4.7).** `u3x_comp_weapon_answer` is called for every value id once the row, the
    table and its entries are there, also for an unknown id (then with locked = 0, result 0, nothing written and
    ea[114] not counted). The unlocked test is made only for value 0xa1443de7, before that call.
16. **3.9: the rule gives the old ids.** The string ids of "general/net/boosters-0/background",
    "general/net/boosters-1/background" and "general/net/boosters-2/background" computed with the rule of 3.9 are
    exactly the three icon ids the old code held for levels 0, 1 and 2; both check values of 3.9 hold.
17. **`u3h_set_icon` in the old row-icon code.** The old code answered the booster's icon (4.2, image slot
    0xf5a294f3) with its own lines: the index of the icon looked up again, then `u3x_icon_answer(ev, 3, 0, id, 1,
    index)` when it is 0 or more. That is `u3h_set_icon` as section 2 defines it; how many times the (read-only)
    index lookup runs is not part of the behaviour.
