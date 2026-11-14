# G24. Kickbacks list (medal kickbacks), kickback row icons, lock-status icons

Behaviour description for a reimplementation (reimpl2 pipeline, describer's document). It says what the functions
do, not how the old ones were written. All offsets are hexadecimal when prefixed with 0x. "Image offset X" means the
address `base + X`, where `base` is the game image base. All multi-byte values are little-endian; "u32 / s32 / u8"
are unsigned / signed integers of that width, "pointer" is 8 bytes. "Word n" of a request means the 32-bit word at
byte offset 4·n. Names and conventions follow `G23_boosters.md` and `G22_rank_xp.md` ("menu user", "native k",
ea[n], image answers, "text answer", the string id rule). Read G23 first: this document refers to its sections for
everything the two lists share and does not repeat them.

This group has one float step: the duration text of 4.4 (one addition and one conversion to an integer). Two float
patterns are stored as plain 32-bit words (4.4, operation 12).

## 1. Purpose and callers

The PS4 game kept the menu page of the medal kickbacks (PAGE_MEDAL_STREAKS) but its list source for it is an empty
function. PS3 1.19 has the list. This group is the helper's replacement:

- **The kickbacks list** (`u3x_kickback_source`): every request of the list: title, row texts (name, level with
  tier description and medal cost, owned / unlock text, cash, first-tier description, duration, cost), selection,
  "seen" marking, the locked flag, cell size, and the click that equips a kickback.
- **The row icons** (`u3x_kickback_icons`): the kickback's picture (that of the tier the player has reached), and
  the status picture: the "equipped" check, the "new" marker or the lock-status icon.
- **The list accessors** (`u3x_kickback_nth`, `u3x_kickback_count`, `u3x_kickback_by_id`): the kickbacks of the
  competitive or the co-op loadout, out of the game's kickback table.
- **The lock-status icon** (`u3x_unlock_icon`): which UI icon stands for unlock status 1, 2 and 3. It is in no work
  group; it is covered here (section 3.7 and 4.1). Its three ids are no longer literal numbers: they are computed
  from texture names.

Row 0 of the list is always "None"; row n (n ≥ 1) is the n-th kickback of the loadout's kind.

Callers (all stay as they are):

- C `kickback_source` (a one-line wrapper today) is called by the helper's event entry `u3_profile_event` for every
  list request, in its chain of list-source hooks, right after the boosters list; a result of 0 passes the request
  on to the next hook.
- `u3x_kickback_nth`: by this group, by the C one-line wrapper `kickback_nth` and by the kickback pick sound of
  G23 (`u3x_k49_kickback`).
- `u3x_kickback_icons`, `u3x_kickback_count`, `u3x_kickback_by_id`: only by this group (they have C declarations
  but no C caller).
- `u3x_unlock_icon`: by the boosters' row icons (G23 `u3x_booster_icons`) and by this group. The co-op weapon mods
  list (G25) and the missions list carry literal copies of its values today; their describers are told to call it.

Everything runs on the game thread that updates and draws the menus. There is no locking and no timing rule.

## 2. Interface

No function takes or returns a float. All are `extern "C"` and "existing": exported today under this name, name and
signature stay. The group is small enough for one part.

| Function | Signature | Called by |
|---|---|---|
| list source | `int u3x_kickback_source(uintptr_t base, int32_t *ev, void *s)` | C `kickback_source` |
| row icons | `void u3x_kickback_icons(uintptr_t base, void *s, int32_t *ev, int32_t row, int coop, uint32_t user, uint8_t *profile, int32_t loadout)` | `u3x_kickback_source` |
| n-th kickback | `uint8_t *u3x_kickback_nth(void *s, int32_t n, int coop)` | this group; C `kickback_nth`; G23 |
| number of kickbacks | `int32_t u3x_kickback_count(void *s, int coop)` | `u3x_kickback_source` |
| kickback of an id | `uint8_t *u3x_kickback_by_id(void *s, uint32_t id)` | `u3x_kickback_icons` |
| lock-status icon | `uint32_t u3x_unlock_icon(int status)` | this group; G23; (G25 and the missions list, see 7) |

Parameters:

- `base`: game image base. `s`: the helper's state object (opaque; fields in 3.2).
- `ev`: one data-source request (3.3).
- `row`: the row asked (word 1 of the request, as the list source read it).
- `coop`: non-zero for the co-op kickback list, 0 for the competitive one.
- `user`: local user index, already limited to 0 .. 3 by the caller.
- `profile`: the menu user's profile object, or null. `loadout`: the s32 at image 0x17d9988.
- `n`: position in the list, counted from 0 (row − 1). `id`: a kickback id.
- `status`: an unlock status as native 35 returns it (the full s32).

Results: `u3x_kickback_source` returns 1 when the request belongs to its list (tag match), else 0. The accessors
return a pointer to a kickback entry (3.4) or null. `u3x_kickback_count` returns the number of kickbacks of the
kind. `u3x_unlock_icon` returns a UI icon id, or 0.

Functions of the C part, of other groups and existing exported C++ functions that the new code calls (declare them;
do not reimplement them):

| Name | Signature | What it does |
|---|---|---|
| `u3h_ext` | `uint8_t *u3h_ext(void)` | the helper's own state block (3.1). In CONVENTIONS.h, with `ext_ea()` |
| `u3h_gate_closed` | `int u3h_gate_closed(uintptr_t base, int32_t *ev)` | 0 while multiplayer is active; otherwise stores the game's empty string as the request's text answer, counts, and returns 1 (G23) |
| `u3h_sym_ready` | `int u3h_sym_ready(uintptr_t base, void *s, uint32_t id)` | non-zero when the script module that holds symbol `id` is loaded; 0 without the "find module" native (G23) |
| `u3h_text` | `const char *u3h_text(uintptr_t base, void *s, uint32_t id)` | localized text of a text id; never null (G23) |
| `u3h_text_num` | `void u3h_text_num(uintptr_t base, void *s, char *buf, uint32_t size, uint32_t id, int32_t value)` | sets `buf[0]` to 0; then, when the state's native 38 is not null, has the game write the localized text `id` with the number `value` filled in into `buf` (at most `size` bytes, NUL-terminated) |
| `u3h_fmt` | `const char *u3h_fmt(void *s, const char *format, int64_t a0, const char *s0, const char *s1, const char *s2)` | a small formatter into a buffer of the C part (at most 255 characters): "%d", "%i", "%u" (digits between "%" and the letter are skipped) become `a0` in plain decimal, "%s" the next of the three strings (empty for a null one), "%%" a percent sign. Returns the buffer; it stays valid until the next `u3h_fmt` call |
| `u3h_icon_index` | `int32_t u3h_icon_index(uintptr_t base, uint32_t id)` | index of UI icon `id` in the game's UI texture table, or −1 (G23) |
| `u3h_set_icon` | `void u3h_set_icon(uintptr_t base, int32_t *ev, uint32_t id)` | makes the request a plain icon answer for `id` when the icon is in the UI texture table; otherwise leaves the request untouched (G23) |
| `u3x_icon_answer` | `void u3x_icon_answer(int32_t *ev, int32_t kind, int32_t a, int32_t b, int32_t c, int32_t index)` | image answer: word 2 = `kind`, words 4, 5, 10 = 0, words 6, 7, 8 = `a`, `b`, `c`, word 9 = `index`, word 11 = −1, word 12 = 0x3f800000 (1.0); word 3 is left as it is |
| `u3h_k49_kickback` | `void u3h_k49_kickback(uintptr_t base, void *s, uint32_t user, int coop, int32_t row)` | C export that ends in the kickback pick sound of G23 (`u3x_k49_kickback`) |
| `u3x_booster_unlocked` | `int u3x_booster_unlocked(void *s, uint32_t user, uint32_t id)` | 1 when the item `id` is unlocked for the user, else 0 (also 0 when native 24 or 25 is null). It takes the image base from the state object, offset 760 (G23 4.0). It works for any item id, not only boosters |
| `u3x_unlock_entry` | `void *u3x_unlock_entry(uintptr_t base, void *s, uint32_t id)` | existing one-line function (it stays): null when native 24 is null, else native 24(base + 0x17f3068, `id`, 0): the unlock entry of an item, which may be null |
| `u3h_crc_sid` | `uint32_t u3h_crc_sid(const char *name)` | existing C export: the string id of a name by the rule of G23 3.9 (the 32-bit CRC with polynomial 0x04C11DB7, most significant bit first, start value 0, no final inversion, over the characters before the NUL). Pure: the result depends on the characters only |

`u3h_ext` and `u3h_crc_sid` have no side effect: how often they are called, and whether a string id is computed on
every call or kept after the first, is not part of the behaviour. (The new code may also compute the CRC itself,
bit by bit, as G23 3.9 describes; the result is the same.)

## 3. Game and helper facts

### 3.1 The helper's state block (address from `u3h_ext()`)

The counter array "ea" starts at offset 0x3c50 (`ext_ea()`); u32 words.

| Word | Meaning |
|---|---|
| ea[0] bit 4 (value 0x10) | set = kickback row icons off (operation 4 answers nothing) |
| ea[0] bit 5 (value 0x20) | set = booster icons off; this list uses it for its cell size (operation 12) |
| ea[16] | counter: kickback picture requests of a row |
| ea[17] | counter: "equipped" checks |
| ea[18] | counter: "new" markers |
| ea[19] | counter: lock-status icons |

Counters are plain 32-bit increments (wrapping).

### 3.2 The state object `s`

| Offset | Type | Meaning |
|---|---|---|
| 760 | u64 | the image base; `u3x_kickback_source` stores `base` here (4.4 step 3) |
| 768 + 8·k | pointer to function | "native k": a game function the helper found at start. Any of them may be null |
| 0x2c80 | 251 characters | the text buffer shared with other lists (G23 3.2); the text answers composed here stay valid until the next request |

Natives used here (the game function each one normally holds is given for orientation only; always call through the
pointer in the state object and treat a null pointer as said in section 4):

| k | Arguments | Result | Game function |
|---|---|---|---|
| 13 | (u32 symbol id) | pointer to the data of a script symbol. The game function stops the game on a symbol that is not loaded, so it is only called behind `u3h_sym_ready` (4.4 step 4) | 0x11f2e30 |
| 24 | (image 0x17f3068, s32 id, s32 0) | pointer: the unlock entry of an item (may be null) | 0x7bb470 |
| 26 | (u32 id, u32 0) | pointer to a text: what unlocks the item / its price | 0x751b30 |
| 28 | (profile pointer, s32 item, u32 value, s32 loadout) | none: stores a loadout item | 0x780220 |
| 32 | (image 0x17f3068, u32 user, u32 id, u32 0) | none: marks the item as seen (no longer "new") | 0x7bda50 |
| 33 | (pointer to a state block, u32 kickback id) | s32: the level the user has reached for the kickback (0 = first tier) | 0x74eba0 |
| 34 | (profile pointer, s32 item, s32 loadout) | u32: a loadout item | 0x7800d0 |
| 35 | (image 0x17f3068, u32 user, entry pointer) | s32 unlock status: 0 = owned, 1 = can be bought, 2, 3 = locked in different ways | 0x7bca00 |
| 36 | (image 0x17f3068, u32 user, u32 id, u32 0) | low 8 bits non-zero = the item shows the "new" marker | 0x7bd6f0 |

The state block of native 33: the game function reads only the user index, a u32 at offset 0xc of the block. The
helper passes a block of its own: 32 bytes, all zero, except the u32 at +0xc = the user. The block lives on the
stack for the one call.

Loadout item number 7 is the equipped kickback (value: the kickback id, 0 = none).

### 3.3 Data-source requests

As G23 3.3. The fields this list uses:

| Field | Meaning |
|---|---|
| word 0 | operation |
| word 1 | row index (in); the answer of operations 5, 6 and 0x11 (out) |
| word 2, 4 .. 12 | image answer (`u3x_icon_answer`; a plain icon answer through `u3h_set_icon`) |
| word 0xe | the answer of operation 3 (2 = locked) and of a value query (operation 11) |
| word 0x13 | the id of the value asked (operation 11) |
| word 0x14 | the image slot asked (operation 4): 0xf5a294f3 = the kickback's picture, 0 = the status picture |
| word 0x15 | the text slot asked (operation 2) |
| word 0x17 | the clicked row (operation 0x16) |
| word 0x1f, 0x20 | cell size (operation 12), two float patterns |
| pointer at byte 0x40 | the text answer |
| pointer at byte 0x98 | the source descriptor; its u32 at +0x1c is the source tag |

Source tag of this list: 0x4c414e44.

### 3.4 The kickback table

The data of script symbol 0xd9f6ada2, reached through native 13 (not through the lookup of offset 656 that G23's
`u3x_sym_table` uses: native 13 returns the data pointer itself):

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | number of entries |
| 8 | pointer | the entries, 0x28 bytes each (may be null) |

A kickback entry:

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | kickback id |
| 4 | u32 | text id of the name |
| 8 | s32 | medal cost |
| 0xc | s32 | number of tiers (levels) |
| 0x18 | pointer | the tiers, 0x18 bytes each (may be null) |
| 0x20 | u32 | flags: bit 0 (value 1) = in the competitive list, bit 1 (value 2) = in the co-op list |

A tier:

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | UI icon id of the tier's picture |
| 4 | u32 | text id of the tier's description |
| 8 | float (32 bits) | duration in seconds; zero or below = lasts until death |

The table is game data, read at run time on every call; nothing of it is kept or copied.

"The table" below = null when native 13 is null, else native 13(0xd9f6ada2). The flag of a list kind = 2 for the
co-op list, 1 for the competitive list.

### 3.5 Profile objects, menu user

As G23 3.5: "menu user" = 1 when the u8 at image 0x17bde75 is non-zero, else the u32 at image 0x17bf714. "Profile
object of the menu user" = the pointer at image 0x17dd610 + 0x7b00 · menu user; null when that pointer is null or
the menu user is 4 or more. The u32 at +0x74c of a profile object is the player's cash.

### 3.6 Game data

| Image offset | Type | Meaning |
|---|---|---|
| 0x17d9988 | s32 | the loadout being edited; 4 = the co-op loadout |
| 0x17d998c | s32 | the selected row of the kickbacks list (0 = "None"); the game's loadout screen sets it too |
| 0x17f3068 | object | unlock manager (argument of natives 24, 32, 35, 36) |

No game function is called at a fixed image offset; everything goes through the natives and the `u3h_` exports.

### 3.7 Icons: names, not numbers

Every UI icon id this group needs is the string id (G23 3.9, `u3h_crc_sid`) of a texture name of the game's
multiplayer menu art. The new code holds the names (read-only helper data) and computes the ids; it holds no icon
id as a number.

| Icon | Texture name | Used for |
|---|---|---|
| "can be bought" | "general/net/menu-icons/money" | unlock status 1 |
| "cannot be bought" | "general/net/menu-icons/money-slashed" | unlock status 2 |
| "locked" | "general/net/menu-icons/icon-locked" | unlock status 3 |
| "equipped" check | "general/net/menu-icons/icon-check" | the equipped row (the same icon as in G23) |
| empty kickback | "general/net/medal-streak/empty" | the picture of the "None" row |
| kickback fallback | "general/net/medal-streak/medal-streak" | the picture of a kickback whose tier icon is missing |

How the names were found and checked: the material names of the game's multiplayer art (pak "net": folders
"general/net/menu-icons" with nine materials and "general/net/medal-streak") were run through the string id rule
and compared with the six numbers of the old code; all six match. Two check values for a test: "123456789" gives
0x89a1897f (G23), and the three status icons must give three different non-zero ids.

Cross-check against the running game (not needed by the new code): the PS4 image holds the three status ids as
immediates of its own list sources (eight or nine places each), which make the same choice by status after the
same two calls (natives 24 and 35); one such place is inside the game's list source at image 0x7699e0: u32 at
image 0x76a478 (status 1), 0x76a3e2 (status 2), 0x76a430 (status 3). The image holds none of the names as text.

### 3.8 Ids and texts

Text ids (looked up with `u3h_text`, except the one marked): 0x87b43182 the list title; 0x9d3c6e80 the name of the
"None" row; 0x034c8e1e the description of the "None" row; 0xadfc7da6 the word "Level"; 0x69443c24 the medal cost
line, a text with a number (through `u3h_text_num`); 0xecf338f6 "owned"; 0xba6d58f7 the cash label; 0x3badfdd5 the
duration label; 0xa80e2d11 "until death"; 0xe7624faa "seconds"; 0x819103cc the cost text, a format with one number
(through `u3h_fmt`).

Text slots (word 0x15): 0 name; 3 level, tier description and cost; 4 owned or unlock text; 8 cash; 0x80d4cc7b the
description; 0xadacdbf3 the duration; 0xb684d68b the cost.

Value id (word 0x13): 0x67a48eb9.

Texts of the helper's own (they must live in read-only helper data): an empty text; a single space " "; a single
backslash (one character, 0x5c: the menu text's line separator in this list); the colour escape "|@Cff00ff00|"
(cash).

There is no literal table of game values in this group. The kickback table is game data read at run time (3.4);
the icon ids are generated from names (3.7).

## 4. Behaviour

Shared wording:

- "Text answer = T": the pointer at byte 0x40 of the request becomes T.
- "Level of id": 0 when native 33 is null (no call). Otherwise native 33(the state block of 3.2 for `user`, id);
  a negative result counts as 0.
- "Show icon X": first `u3h_icon_index(base, X)`; only when its result is 0 or more, `u3h_set_icon(base, ev, X)`.
  (The request stays untouched for an icon that is not in the UI texture table.)
- "Status of id": entry = `u3x_unlock_entry(base, s, id)` (null when native 24 is null), then native 35(base +
  0x17f3068, `user`, entry); the full s32 result. Only used where native 35 was found non-null first. The entry is
  not tested for null.

### 4.1 `u3x_unlock_icon`

`status` 1: the string id of "general/net/menu-icons/money". 2: of "general/net/menu-icons/money-slashed". 3: of
"general/net/menu-icons/icon-locked". Every other value (0, negative, 4 and above): 0. No other effect.

### 4.2 The three list accessors

All three fetch the table first (one native 13 call, when native 13 is not null), also when the answer is already
clear from the arguments. A null table, a null entries pointer or a count of 0 gives "no entry". Entries are
walked in table order, from the first to the count-th.

**`u3x_kickback_nth`.** Null when the table is null or `n` is negative. Otherwise the entry number `n` (from 0)
among the entries whose flags have the flag of the list kind (`coop`) set; null when there are fewer.

**`u3x_kickback_count`.** 0 when the table is null. Otherwise the number of entries whose flags have the flag of
the list kind set.

**`u3x_kickback_by_id`.** Null when the table is null. Otherwise the first entry whose id (u32 at +0) equals `id`,
whatever its flags; null when there is none.

### 4.3 `u3x_kickback_icons`: the picture of a row

First, always: entry = `u3x_kickback_nth(s, row − 1, coop)` (so row 0 and negative rows have no entry, and the
table is fetched all the same). Then by image slot (word 0x14):

**Image slot 0xf5a294f3 (the kickback's picture).** ea[16] += 1. Then:

- `row` is 0, or there is no entry: show icon "empty kickback". Done.
- Otherwise: level = level of the entry's id (the native is called before the tiers are looked at); icon = 0 when
  the entry's tiers pointer is null, else the icon id of tier number level (u32 at tiers + 0x18·level; the level
  is not compared with the tier count). When icon is non-zero and `u3h_icon_index(base, icon)` is 0 or more:
  `u3h_set_icon(base, ev, icon)`. Otherwise: show icon "kickback fallback". Done.

**Image slot 0 (the status picture).** In this order, the first that applies ends the request:

1. Equipped. worn = null when `profile` is null or native 34 is null (no call); otherwise
   `u3x_kickback_by_id(s, native 34(profile, 7, loadout))`. When the row's entry and worn are the same (the same
   entry, or both null): show icon "equipped" check, ea[17] += 1 (counted even when the icon is missing from the
   table), done. (So the check also marks the "None" row while nothing is equipped.)
2. No entry: nothing, done.
3. New: when native 36 is not null and the low 8 bits of native 36(base + 0x17f3068, `user`, id of the entry, 0)
   are non-zero: the request becomes the "new" marker, exactly the effect of `u3x_icon_answer(ev, 0x2f, 0, 0, 0,
   −1)`; ea[18] += 1; done.
4. Lock status: when native 35 is null: nothing. Otherwise icon = `u3x_unlock_icon(status of the entry's id)`; when
   icon is non-zero: show icon, ea[19] += 1 (counted even when the icon is missing from the table).

**Any other image slot:** nothing more (the table fetch of the first step has happened).

### 4.4 `u3x_kickback_source`

Steps, in this order:

1. Source descriptor (pointer at byte 0x98) null, or its tag not 0x4c414e44: return 0, nothing touched.
2. `u3h_gate_closed(base, ev)` non-zero: return 1.
3. The image base is stored in the state object (offset 760).
4. `u3h_sym_ready(base, s, 0xd9f6ada2)` is 0 (the kickback table is not loaded): for operation 6, word 1 = −1;
   return 1 in every case, nothing else.
5. Set-up: loadout = s32 at image 0x17d9988; coop = 1 when the loadout is 4, else 0; user = the menu user AND 3;
   profile = the profile object of the menu user (3.5); cash = u32 at profile + 0x74c, or 0 without profile;
   row = word 1. The first byte of the text buffer (s + 0x2c80) is set to 0, for every operation.
6. Operation 0x16: `u3h_k49_kickback(base, s, user, coop, word 0x17)` (the sound), then go on.
7. By operation, then return 1 (also for operations not listed):

| Operation | Effect |
|---|---|
| 1 | text answer = `u3h_text(base, s, 0x87b43182)`; then everything operation 9 does |
| 9 | entry = `u3x_kickback_nth(s, (s32 at image 0x17d998c) − 1, coop)`; when there is one and native 32 is not null: native 32(base + 0x17f3068, user, id of the entry, 0) |
| 2 | row text, see below |
| 3 | nothing when row is 0 (no table fetch). Otherwise entry = `u3x_kickback_nth(s, row − 1, coop)`; none: nothing. When `u3x_booster_unlocked(s, user, id of the entry)` is 0: word 0xe = 2 |
| 4 | when bit 4 of ea[0] is clear: `u3x_kickback_icons(base, s, ev, row, coop, user, profile, loadout)` |
| 5 | word 1 = the s32 at image 0x17d998c |
| 6 | word 1 = `u3x_kickback_count(s, coop)` (the index of the last row, because of the "None" row) |
| 10 | the s32 at image 0x17d998c = row (stored as it is, not compared with anything) |
| 11 | when word 0x13 is 0x67a48eb9: word 0xe = 4. Other values: nothing |
| 12 | when bit 5 of ea[0] is clear: word 0x1f = word 0x20 = 0x43000000 (128.0) |
| 0x11 | word 1 = 8 |
| 0x16 | equip, see below |

**Operation 2 (row text).** First, for every text slot: entry = `u3x_kickback_nth(s, row − 1, coop)`. Then by text
slot (word 0x15):

| Text slot | Text answer |
|---|---|
| 0 | row 0: `u3h_text(base, s, 0x9d3c6e80)`. Otherwise, with an entry: `u3h_text(base, s, name id of the entry)`. No entry: nothing |
| 3 | row 0: `u3h_text(base, s, 0x034c8e1e)`. Otherwise, without an entry: nothing. With an entry: the level line, see below |
| 4 | nothing for row 0 or without an entry. `u3x_booster_unlocked(s, user, id of the entry)` non-zero: `u3h_text(base, s, 0xecf338f6)`. Else: the text native 26(id of the entry, 0) returns, or the helper's empty text when native 26 is null |
| 8 | (whatever the row) the buffer: `u3h_text(base, s, 0xba6d58f7)`, "\|@Cff00ff00\|", the cash (unsigned) in decimal with thousands separators |
| 0x80d4cc7b | with an entry whose tiers pointer is not null: `u3h_text(base, s, description id of the first tier)` (u32 at tiers + 4). Else nothing |
| 0xadacdbf3 | with an entry whose tiers pointer is not null: the duration line, see below. Else nothing |
| 0xb684d68b | with an entry: `u3h_fmt(s, u3h_text(base, s, 0x819103cc), medal cost of the entry, null, null, null)`. Else nothing |
| other | nothing |

*The level line (text slot 3).* In this order: level = level of the entry's id (native 33); a local buffer of 128
bytes, its first byte 0, is filled by `u3h_text_num(base, s, that buffer, 128, 0x69443c24, medal cost of the
entry)`. Then the text buffer is composed: `u3h_text(base, s, 0xadfc7da6)` ("Level"), a space, level + 1 in
decimal, a backslash; then, only when the tiers pointer is not null and level is below the tier count (signed
comparison): `u3h_text(base, s, description id of tier number level)` (u32 at tiers + 0x18·level + 4); then a
backslash; then the local buffer's text. Text answer = the text buffer. (Both backslashes are always there.)

*The duration line (text slot 0xadacdbf3).* d = the duration of the first tier (the 32-bit float at tiers + 8).
The text buffer is composed: `u3h_text(base, s, 0x3badfdd5)`, a space, then

- when d is less than or equal to zero as floats (true for +0.0 and −0.0, false for a NaN):
  `u3h_text(base, s, 0xa80e2d11)`;
- otherwise (d greater than zero, or a NaN): the number of seconds N in decimal, a space,
  `u3h_text(base, s, 0xe7624faa)`.

N: the sum d + 0.5 (0.5 = pattern 0x3f000000), one single-precision addition (`f_add`: one rounding to a 32-bit
float), then converted to a signed 64-bit integer by cutting off the fraction (towards zero). A sum that is a
NaN, infinite, or 2^63 or more gives the smallest 64-bit integer, which prints as "-9223372036854775808". (`f_to_int`
of CONVENTIONS.h converts to 32 bits only: it gives the right N for sums below 2^31; above that the conversion must
be a 64-bit one, or the value be taken from the float's bits.) Rounding point: the old build added on the x87 unit
and did not round the sum to a 32-bit float before the conversion; under the pipeline's float rule the new code
rounds it. The two differ only where the exact sum lies exactly half-way between two floats and the rounding
crosses a whole number: the largest float below 0.5 (old 0, new 1) and the odd whole numbers from 2^23 up to 2^24
(old d, new d + 1). Real kickback durations are small whole numbers of seconds; there the results are equal.

Composition rule for the text buffer (as G23 4.1): the pieces are written one after the other; the result is cut
after 250 characters; a NUL follows. "Decimal" is plain digits, with a leading "-" for a negative number. "With
thousands separators" puts a comma between groups of three digits counted from the right ("1,234,567"; no comma
below 1000). Texts are looked up in the order they appear in the composition.

**Operation 0x16 (equip).** Nothing when native 28 is null or there is no profile object. Otherwise, with clicked =
word 0x17 (signed):

- clicked is 0 or negative (the "None" row): native 28(profile, 7, 0, loadout): no kickback.
- otherwise: entry = `u3x_kickback_nth(s, clicked − 1, coop)`; none: nothing. When native 35 is not null and the
  status of the entry's id is exactly 0 (owned; the full s32 is compared): native 28(profile, 7, id of the entry,
  loadout). Any other status: nothing (buying is not part of this list).

The profile is not saved by this operation.

## 5. Test scenarios

Everything can be tested with a fake image (the fields of 3.5 and 3.6), a kickback table in test memory behind a
stand-in for native 13, a state object whose natives are recording stand-ins, and recording stubs for the `u3h_` /
`u3x_` functions of the second table of section 2 (`u3h_crc_sid` real, or checked separately). Compare memory
(request, state object, image fields, ea), results, and the order and arguments of all calls.

A useful table: five entries with flags 1, 2, 3, 1, 0; tier counts 1 to 3; one entry with a null tiers pointer.

Icons by name:
1. `u3x_unlock_icon`: status 1, 2, 3 give three different non-zero ids equal to the string ids of the three names
   of 3.7 (and equal to what the old function returns); 0, 4, −1 and 0x101 give 0. The string id of "123456789"
   is 0x89a1897f.

Accessors:
2. `u3x_kickback_nth`: native 13 null; n = −1 (null, native 13 still called once); competitive and co-op list:
   n = 0, 1, the last, one beyond; an entry with flags 3 appears in both lists, one with flags 0 in neither;
   count 0; entries pointer null.
3. `u3x_kickback_count`: native 13 null (0); both kinds; entries pointer null (0).
4. `u3x_kickback_by_id`: an id of a competitive entry, of a co-op entry, of the flags-0 entry (found), an id not in
   the table, id 0; native 13 null.

Row icons:
5. Picture slot: row 0 and a row beyond the list (the empty icon, in the table and missing from it); a normal row
   with level 0, level 2, level −1 from the native (tier 0), native 33 null (tier 0); the state block passed to
   native 33 (32 bytes, user at +0xc, zero elsewhere); tier icon 0, tier icon missing from the UI table (fallback
   shown), fallback missing too (nothing written); tiers pointer null (fallback); ea[16] in every case.
6. Status slot: profile null; native 34 null; nothing equipped and row 0 (check, ea[17]); nothing equipped and a
   normal row; the equipped id on its own row (check) and on another row; an equipped id that is not in the table
   (no check on normal rows, check on row 0); the check icon missing from the table (ea[17] still moves); a row
   beyond the list while something is equipped (nothing).
7. Status slot, not equipped: native 36 null, answering 0, 1 and 0x100 (0x100 = not new); the eleven words of the
   "new" answer, word 3 untouched; native 35 null (nothing); status 0, 1, 2, 3, 4 and 0x101 (no icon for 0, 4,
   0x101); native 24 null (native 35 called with a null entry); icon missing from the table (ea[19] still moves).
8. Another image slot (only the table fetch).

List source:
9. Null descriptor; wrong tag (0, nothing touched). Gate closed (1, nothing else, base not stored).
10. Table not ready: operation 6 (word 1 = −1), operations 1 and 2 (nothing, buffer byte untouched).
11. Set-up: buffer's first byte cleared for an unlisted operation; loadout 4 (co-op list) and 0; menu user from
    both sources; menu user 5 (user 1, no profile).
12. Operation 1: the title, then the "seen" call for the selected row; selection 0 (no call), selection beyond the
    list, native 32 null. Operation 9: the same without the title.
13. Operation 2, slot 0: row 0, a normal row, a row beyond the list, row −1.
14. Slot 3: row 0; no entry; level 0 of 2 tiers; level equal to the tier count (no tier text, both backslashes);
    tiers pointer null; native 33 null; the order native 33 → `u3h_text_num` → texts; a `u3h_text_num` stand-in
    that writes nothing (empty cost line); long texts that cut the result at 250 characters.
15. Slot 4: row 0; locked with and without native 26; unlocked. Slot 8: cash 0, 999, 1000, 4294967295, no
    profile; row 0 and a row beyond the list (still answered).
16. Slot 0x80d4cc7b: entry with tiers, with a null tiers pointer, no entry. Slot 0xb684d68b: with and without an
    entry; the arguments of `u3h_fmt`. An unknown slot.
17. Slot 0xadacdbf3, durations: 0.0, −0.0, −1.0 ("until death"); 0.25 (0), 0.5 (1), 1.0 (1), 1.4 (1), 1.5 (2),
    20.0 (20), 4194303.5 (4194304), 3.0e9 (3000000000), a NaN and +infinity ("-9223372036854775808"); the
    largest float below 0.5 and 8388609.0 are the documented deviations from the shipped helper (1 and 8388610
    in the new code and in the old code built for single precision). Null tiers pointer; no entry.
18. Operation 3: row 0 (no native 13 call), no entry, locked (word 0xe = 2), unlocked (word 0xe untouched).
19. Operation 4: bit 4 of ea[0] clear (the eight arguments of `u3x_kickback_icons`) and set.
20. Operations 5, 6 (competitive and co-op), 10 (row 3, row −7), 0x11; 11 with the value id and another;
    12 with bit 5 of ea[0] clear and set.
21. Operation 0x16: the sound call first, with word 0x17; native 28 null; no profile; clicked 0 and −1 (item 7 =
    0); clicked beyond the list; native 35 null; status 0 (stored with the entry's id and the loadout), 1, 3 and
    0x100 (not stored).

Needs the live game: only the look (that the pictures and the check appear in the cells, the texts in the right
panel fields) and the sound.

## 6. Things that look like bugs or dead code (behaviour to keep)

1. The cell size (operation 12) is switched by the boosters' icon switch (bit 5 of ea[0]), the row icons
   (operation 4) by the kickbacks' own (bit 4).
2. The picture slot takes the tier of the level without comparing the level with the tier count; the level line
   (text slot 3) does compare. A level at or beyond the count reads a tier past the entry's tiers.
3. The description and duration slots always use the first tier, not the tier of the player's level.
4. The first byte of the shared text buffer is cleared on every request that gets past the table test, also for
   operations that compose nothing.
5. Operation 1 marks the selected kickback as seen, like operation 9.
6. Operation 10 stores any row, also one beyond the list or a negative one; all readers cope (no entry).
7. The "equipped" test compares entries: a row without an entry (row 0, or a row beyond the list) gets the check
   whenever no equipped entry is found (nothing equipped, no profile, native 34 null, or an equipped id that is
   not in the table).
8. `u3x_kickback_by_id` looks through all entries whatever their flags; `u3x_kickback_nth` only through those of
   the list kind.
9. The accessors fetch the table before they look at their arguments (`u3x_kickback_nth` with a negative `n`).
10. The status used here calls native 35 with a null entry when native 24 is null, and compares the full s32;
    G23's `u3x_unlock_status` answers 3 without a call in that case and keeps only the low 8 bits. The pick sound
    (G23) uses the latter, so a status such as 0x100 sounds "picked" but is not equipped here.
11. Equip stores only an owned kickback (status 0); status 1 ("can be bought") plays the "picked" sound and
    stores nothing. Equip does not save the profile (the boosters list does).
12. ea[17] and ea[19] also count answers that were not written because the icon is missing from the UI texture
    table.
13. Operation 3 writes word 0xe only for a locked row; operation 11 answers 4 for the value id whatever the row.
14. `u3x_kickback_count`, `u3x_kickback_by_id` and `u3x_kickback_icons` have C declarations but no C caller.

## 7. Open questions and notes for other groups

1. The six names of 3.7 give exactly the six old ids, so the rule is settled. As in G23 (open question 1) it was
   not checked whether the game's own "string id of a name" function returns the same values in the running game;
   the description therefore asks for the helper's own CRC (`u3h_crc_sid`, or the bit-by-bit computation of
   G23 3.9). G23, G24 and G25 all need this CRC: the integrator may keep one shared function.
2. `u3x_unlock_icon` moves from literal ids to names. G23's 3.9 says the three ids "stay inside the existing
   `u3x_unlock_icon`": with this document they are generated there. G25 (the status switch of the co-op weapon
   mods list: statuses 1, 2, 3 and the check) and the missions list (its "locked" icon = `u3x_unlock_icon(3)`,
   its check = "general/net/menu-icons/icon-check") should call `u3x_unlock_icon` or use the names instead of
   repeating numbers. For the missions describer: the third icon of that list, 0xe2315f64, is the string id of
   "general/net/message-star/default" (found the same way).
3. What unlock status 2 means exactly is not known here; its icon is the crossed-out money sign, so "cannot be
   bought" is used as its name. The behaviour does not depend on the meaning.
4. `u3x_unlock_entry` is a one-line function of the inventory's trivial list that sits between this group's
   functions; it is used only by this group and declared in C without a C caller. It is kept and called; if the
   integrator prefers, the same two lines can live inside the new code instead.
5. The float deviation of the duration line (4.4) is the one the pipeline's float rule accepts; it cannot occur
   with the game's kickback data as long as durations are whole seconds below 2^23.

## 8. Clarifications (tester, 2026-11-14)

Facts confirmed by the differential test (`reimpl2\G24\test_g24.cpp`, old v7.212 against the new `part.cpp`; the old
list source was also built without SSE, as the helper is built). One item (1) makes precise what the text leaves
open; nothing else in sections 1 to 6 was found wrong. The implementer left no open question for this group.

1. **Level line, the number after "Level" (4.4, text slot 3).** It is level + 1 as a whole number, not wrapped to 32
   bits. After the "negative counts as 0" rule the level is 0 .. 2147483647, so the number is 1 .. 2147483648: for
   level 2147483647 the text shows "2147483648", not "-2147483648". (The old build passes the 32-bit sum on as an
   unsigned value to the 64-bit number writer; the old code built for the host at both optimisation levels and the
   old code built as the helper builds it show the same.) Native 33 answers small tier numbers in the game, so this
   only matters for exactness.
2. **Icons by name (3.7, open question 1).** With the string id rule computed by the test itself, the six texture
   names give exactly the six ids the old code held as numbers (status 1, 2, 3, the check, the empty kickback, the
   fallback), and "123456789" gives 0x89a1897f. The three status ids are different and non-zero.
3. **Duration line (4.4), all 2^32 float patterns.** The rule of 4.4 (one single-precision addition, then cutting
   off the fraction to a 64-bit integer) gives, for every one of the 2^32 patterns, the same text as the old code
   built for the host. Against the old code built as the helper builds it (x87) the text differs for exactly
   4 194 305 patterns: 0x3effffff (the largest float below 0.5: shipped 0, rule 1) and the 4 194 304 patterns
   0x4b000001, 0x4b000003, .. 0x4b7fffff (the odd whole numbers from 2^23 + 1 to 2^24 − 1: shipped d, rule d + 1).
   The difference is always 1 second; no other pattern differs, and the "until death" decision is the same for
   every pattern. The precision setting of the x87 control word (extended or double) changes none of these results.
4. **Row 0 and "no entry".** Row 0 never has an entry (position −1 of the list), so in the picture slot (4.3) and in
   text slot 4 (4.4) the test "row is 0" can never decide on its own; only in text slots 0 and 3 and in operation 3
   does row 0 behave differently from another row without entry (the "None" texts; no table fetch in operation 3).
