# G26. Missions list (co-op missions), taking a mission, time-left texts

Behaviour description for a reimplementation (reimpl2 pipeline, describer's document). It says what the functions
do, not how the old ones were written. All offsets are hexadecimal when prefixed with 0x. "Image offset X" means the
address `base + X`, where `base` is the game image base. All multi-byte values are little-endian; "u32 / s32 / u8 /
s64" are unsigned / signed integers of that width, "pointer" is 8 bytes. "Word n" of a request means the 32-bit word
at byte offset 4·n. Names and conventions follow `G23_boosters.md` and `G24_kickbacks.md` ("native k", eb[n],
"text answer", plain icon answers, the string id rule). Read G23 sections 3.2, 3.3 and 3.9 first: this document
refers to them for what the lists share and does not repeat them.

This group has no float arithmetic and stores no float pattern.

## 1. Purpose and callers

The PS4 game kept the menu page of the co-op missions (PAGE_MISSIONS, page id 0xdfdb36fa) and its dialogs, but its
list source for the page is an empty function (source number 0xd9) and the handler of the "take this mission"
button is gone. PS3 1.19 has both. This group is the helper's replacement:

- **The missions list** (`u3x_missions_source`): every request of the list: which of the three mission sets the
  page's tab shows, the row texts (name, required level, cost, reward, duration, goal, goal count, time left), the
  row's status icon, selection, row count, value queries, and the click that opens the "cancel" or the "take"
  dialog.
- **Taking a mission** (`u3x_take_mission`): makes a row of a set the player's active mission.
- **The mission set of a number** (`u3x_mission_set`), out of the game's mission table.
- **Time left** (`u3x_mission_seconds_left`) of the active mission's 24 hours, and its text
  (`u3x_mission_time_text`: "h hours m minutes" with singular and plural texts).
- **The number fix** (`u3x_x79_num`): the PS4 texts for "minutes" and "hours" lost the number placeholder the PS3
  texts have; a text without any digit gets the number put in front.
- **Four small accessors** (`u3x_mission_obj`, `u3x_mission_profile`, `u3x_mission_finished`,
  `u3x_player_level`). They are in the inventory's list of trivial functions and in no work group; they sit
  between this group's functions and are used by them, so they are covered here (4.1).

Callers (all stay as they are):

- C `missions_source` (a one-line wrapper) is called by the helper's event entry `u3_profile_event` for every list
  request, in its chain of list-source hooks, right after the co-op weapon mods list; a result of 0 passes the
  request on to the next hook.
- C `mission_take` (hand-written glue, stays in C) is reached through the C export `u3h_mission_take`, which the
  menu action dispatcher (`u3x_menu_action`, other group) calls for action id 0xea1f40c5, the action of the "take"
  dialog's button (4.6, operation 0x16). It does, in this order: owner = `u3x_mission_obj(base)`, profile =
  `u3x_mission_profile(base)`; nothing more without a profile; set = `u3x_mission_set(base, s, the s32 at owner +
  0x100c)`; nothing more without a set; then `u3x_take_mission(owner, profile, set, the u32 at owner + 0x1010,
  native 39 of the state object (may be null), a C function that forwards to u3x_mission_finished, the clock
  function of 3.1 (may be null))`; when that returns 1 it saves the profile (the helper's profile publish).
- C `mission_seconds_left` and C `mission_profile` (one-line wrappers) are called by the stub dispatcher
  (`cinema_kind`) for stub kind 172: the builder replaces six bytes at image 0x774b82, where the game's "mission
  progress" message passes 0 as the seconds of its time text, by a value stub; the dispatcher answers the stub's
  r8 value with `u3x_mission_seconds_left(u3x_mission_profile(base))`, zero-extended to 64 bits.
- `u3x_mission_set`: by this group and C `mission_set` (used by `mission_take`).
- `u3x_mission_obj`, `u3x_mission_profile`, `u3x_mission_finished`: by this group and the C one-line wrappers
  `mission_obj`, `mission_profile`, `mission_done`.
- `u3x_x79_num`, `u3x_mission_time_text`, `u3x_player_level`: only by this group (they have C declarations but no
  C caller).

Everything runs on the game thread that updates and draws the menus and the HUD messages. There is no locking.
The only timing rule is the 24-hour window of 4.3.

## 2. Interface

No function takes or returns a float. All are `extern "C"` and "existing": exported today under this name, name and
signature stay; no C function of this group is left, so no new wrapper is needed. The group is small enough for
one part.

| Function | Signature | Called by |
|---|---|---|
| list source | `int u3x_missions_source(uintptr_t base, int32_t *ev, void *s)` | C `missions_source` |
| take a mission | `int u3x_take_mission(uint8_t *owner, uint8_t *profile, const uint8_t *set, uint32_t row, uint8_t (*can)(uint8_t *owner, uint8_t *mission, int flag), int (*done)(uint8_t *profile, uint32_t id), int64_t (*clock)(int64_t *out))` | C `mission_take` |
| mission set | `uint8_t *u3x_mission_set(uintptr_t base, void *s, int32_t set)` | `u3x_missions_source`; C `mission_set` |
| seconds left | `int32_t u3x_mission_seconds_left(const uint8_t *pr)` | `u3x_missions_source`; C `mission_seconds_left` |
| number fix | `void u3x_x79_num(char *o, int32_t v)` | `u3x_missions_source`, `u3x_mission_time_text` |
| time text | `void u3x_mission_time_text(uintptr_t base, void *s, int32_t t, char *o)` | `u3x_missions_source` |
| mission object | `uint8_t *u3x_mission_obj(uintptr_t base)` | `u3x_missions_source`; C `mission_obj` |
| mission profile | `uint8_t *u3x_mission_profile(uintptr_t base)` | `u3x_missions_source`; C `mission_profile` |
| completed test | `int u3x_mission_finished(const uint8_t *pr, uint32_t id)` | `u3x_missions_source`; C `mission_done` |
| player level | `int32_t u3x_player_level(uintptr_t base, void *s, uint8_t *pr)` | `u3x_missions_source` |

Parameters:

- `base`: game image base. `s`: the helper's state object (opaque; fields in 3.2).
- `ev`: one data-source request (3.3).
- `owner`: a mission object (3.5); it is only handed on to `can`.
- `profile`, `pr`: a profile object (3.5), or null.
- `set`: a mission set (3.4), or null; as a number (`u3x_mission_set`): the index of a set in the mission table.
- `row`: index of a mission in the set, counted from 0.
- `can`: the game's "this mission can be taken" test (in practice native 39): arguments (owner, mission entry, 0);
  only the low 8 bits of the result count, non-zero = allowed.
- `done`: "the profile has completed mission `id`": non-zero = completed (in practice `u3x_mission_finished`).
- `clock`: the clock function of 3.1 or null.
- `id`: a mission id. `t`: a time in seconds. `v`: the number to put in front.
- `o`: a NUL-terminated text in a buffer the caller owns: at least 64 bytes for `u3x_mission_time_text`, at least
  53 bytes for `u3x_x79_num`.

Results: `u3x_missions_source` returns 1 when the request belongs to its list (tag match), else 0.
`u3x_take_mission` returns 1 when the mission was taken, else 0. `u3x_mission_set` returns a pointer to a set or
null. `u3x_mission_seconds_left` returns 0 .. 86400. `u3x_mission_finished` returns 1 or 0. `u3x_player_level`
returns a level value (4.1).

Functions of the C part and of other groups that the new code calls (declare them; do not reimplement them):

| Name | Signature | What it does |
|---|---|---|
| `u3h_ext` | `uint8_t *u3h_ext(void)` | the helper's own state block (3.1). In CONVENTIONS.h, with `ext_eb()` |
| `u3h_gate_closed` | `int u3h_gate_closed(uintptr_t base, int32_t *ev)` | 0 while multiplayer is active; otherwise stores the game's empty string as the request's text answer, counts, and returns 1 (G23) |
| `u3h_sym_ready` | `int u3h_sym_ready(uintptr_t base, void *s, uint32_t id)` | non-zero when the script module that holds symbol `id` is loaded; 0 without the "find module" native (G23) |
| `u3h_active_page` | `uint32_t u3h_active_page(uintptr_t base)` | id of the menu page on top of the game's page stack; 0 when there is none |
| `u3h_current_tab` | `uint32_t u3h_current_tab(uintptr_t base, void *s)` | id of the tab that is currently shown on the active menu page (from the game's tab table, for the current play kind LAN / online); 0 when there is no active page, no tab table, or the page has no tabs |
| `u3h_text` | `const char *u3h_text(uintptr_t base, void *s, uint32_t id)` | localized text of a text id; never null (G23) |
| `u3h_text_num` | `void u3h_text_num(uintptr_t base, void *s, char *buf, uint32_t size, uint32_t id, int32_t value)` | sets `buf[0]` to 0; then, when the state's native 38 is not null, has the game write the localized text `id` with the number `value` filled in into `buf` (at most `size` bytes, NUL-terminated) (G24) |
| `u3h_icon_index` | `int32_t u3h_icon_index(uintptr_t base, uint32_t id)` | index of UI icon `id` in the game's UI texture table, or −1 (G23) |
| `u3h_set_icon` | `void u3h_set_icon(uintptr_t base, int32_t *ev, uint32_t id)` | makes the request a plain icon answer for `id` when the icon is in the UI texture table; otherwise leaves the request untouched (G23) |
| `u3h_show_dialog` | `void u3h_show_dialog(uintptr_t base, void *s, uint32_t body, uint32_t title, uint32_t action, int32_t buttons, uint32_t extra, uint32_t close)` | fills the game's message dialog record with these text and action ids and opens the dialog page; `close` 0 = the game's default close label |
| `u3h_crc_sid` | `uint32_t u3h_crc_sid(const char *name)` | existing C export: the string id of a name by the rule of G23 3.9. Pure: the result depends on the characters only |

`u3h_ext` and `u3h_crc_sid` have no side effect: how often they are called, and whether a string id is computed on
every request or kept after the first, is not part of the behaviour.

## 3. Game and helper facts

### 3.1 The helper's state block (address from `u3h_ext()`)

The switch array "eb" starts at offset 0x4050 (`ext_eb()`); u32 words.

| Words | Meaning |
|---|---|
| eb[4] and eb[5] together (8 bytes at block offset 0x4060) | pointer to the **clock function**, or null. The builder stores it at start: the platform's C library `time` function. Signature `int64_t clock(int64_t *out)`; called with a null argument it returns the current calendar time in whole seconds (seconds since 1970-01-01 00:00 UTC). It may be called from any thread |

This group reads no other word of the block and changes none.

### 3.2 The state object `s`

| Offset | Type | Meaning |
|---|---|---|
| 760 | u64 | the image base; `u3x_missions_source` stores `base` here (4.6 step 3) |
| 768 + 8·k | pointer to function | "native k": a game function the helper found at start. Any of them may be null |
| 0x2c80 | 251 characters | the text buffer shared with other lists (G23 3.2); the text answers composed here stay valid until the next request |
| 0x3904 | s32 | the **shown set**: the number of the mission set the list shows (0, 1 or 2; 0 at start). Only this group uses it |

Natives used here (the game function each one normally holds is given for orientation only; always call through the
pointer in the state object and treat a null pointer as said in section 4):

| k | Arguments | Result | Game function |
|---|---|---|---|
| 13 | (u32 symbol id) | pointer to the data of a script symbol. The game function stops the game on a symbol that is not loaded, so it is only called behind `u3h_sym_ready` | 0x11f2e30 |
| 21 | (profile pointer) | u32: the **rank word** of the profile (G22 3.6: bits 0..15 the level counted from 0, bits 16..29 the legacy level, bit 30 and 31 two badge marks). In the patched game this call is answered by G22's kind 64, which counts every call in a diagnostic word: the number of calls is observable | 0x781ea0 |
| 38 | (image 0x17026b8, char *buffer, u32 size, u32 text id, s64 first number, s64 second number) | none: writes the localized text `text id` with its number placeholders filled in into the buffer (at most `size` bytes, NUL-terminated) | 0xf32e00 |
| 39 | (mission object, mission entry, s32 0) | low 8 bits non-zero = the mission can be taken by the object's user | 0x771cf0 |

### 3.3 Data-source requests

As G23 3.3. The fields this list uses:

| Field | Meaning |
|---|---|
| word 0 | operation |
| word 1 | row index (in, s32); the answer of operations 5 and 6 (out) |
| word 2, 4 .. 12 | image answer (a plain icon answer through `u3h_set_icon`) |
| word 0xe | the answer of a value query (operation 11) |
| word 0x13 | the id of the value asked (operation 11) |
| word 0x14 | the image slot asked (operation 4); this list only answers slot 0 |
| word 0x15 | the text slot asked (operation 2) |
| pointer at byte 0x40 | the text answer |
| pointer at byte 0x98 | the source descriptor; its u32 at +0x1c is the source tag |

Source tag of this list: 0x4c414ed9.

### 3.4 The mission table

The data of script symbol 0x4360c988, reached through native 13 (which returns the data pointer itself):

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | number of mission sets |
| 8 | pointer | the sets, 0x20 bytes each |

A mission set:

| Offset | Type | Meaning |
|---|---|---|
| 8 | u32 | number of missions |
| 0x18 | pointer | the missions, 0x4c bytes each (may be null) |

A mission entry:

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | mission id (below 0x400 for a mission that can be completed) |
| 4 | u32 | text id of the name |
| 8 | u32 | text id of the goal text (a text with one number placeholder) |
| 0x10 | u32 | goal kind; 0x5c494554 = the goal count is shown through a text (3.8) |
| 0x14 | s32 | goal count |
| 0x18 | s32 | duration number (shown in the duration column with the "minute / minutes" texts of 3.8) |
| 0x20 | s32 | required level, counted from 1 |
| 0x24 | s32 | reward (cash) |
| 0x44 | u32 | cost (cash) |

The table is game data, read at run time on every request; nothing of it is kept or copied.

### 3.5 Mission user, mission object, profile object

The missions code does **not** use the "menu user" of G23 3.5. Its user, the **mission user**, is the u32 at image
0x17c23a0 AND 3 (0 .. 3).

- **Mission object** of the mission user: the address image 0x17da200 + 0x1018 · mission user (game data, always
  there). Fields used: s32 at +0x100c = the set the page shows (the helper writes it, `mission_take` reads it);
  s32 at +0x1010 = the selected row.
- **Profile object** of the mission user: the pointer at image 0x17dd610 (an array of profile objects of 0x7b00
  bytes, G23 3.5) + 0x7b00 · mission user; null when that pointer is null.

Profile object fields used:

| Offset | Type | Meaning |
|---|---|---|
| 0x74c | u32 | the player's cash |
| 0xecc .. 0xf4b | 0x80 bytes | completed missions: one bit per mission id 0 .. 0x3ff; mission `id` is bit (`id` AND 7) of the byte at 0xecc + (`id` divided by 8) |
| 0xf4c | s32 | progress of the active mission (towards its goal count) |
| 0x1864 | u32 | id of the active mission; 0 = none |
| 0x1868 | u32 | start time of the active mission: the low 32 bits of the clock (3.1) when it was taken; 0 = never started |

### 3.6 Game data

| Image offset | Type | Meaning |
|---|---|---|
| 0x17c23a0 | u32 | the user the missions page acts for (3.5) |
| 0x17da200 | 4 × 0x1018 bytes | the mission objects |
| 0x17dd610 | pointer | profile object array |
| 0x17d7abc | s32 | debug level override; non-zero = level + 1 (G22 3.8) |
| 0x17d5c60 | u8 | LAN byte (non-zero in LAN mode) |
| 0x17026b8 | object | text database (first argument of native 38) |

No game function is called at a fixed image offset; everything goes through the natives and the `u3h_` exports.

### 3.7 Icons: names, not numbers

The three UI icon ids this list needs are string ids (G23 3.9, `u3h_crc_sid`) of texture names of the game's
multiplayer menu art. The new code holds the names (read-only helper data) and computes the ids; it holds no icon
id as a number.

| Icon | Texture name | Used for |
|---|---|---|
| "completed" check | "general/net/menu-icons/icon-check" | a mission the player has completed (the same icon as the "equipped" check of G23 / G24) |
| "active" star | "general/net/message-star/default" | the player's active mission |
| "locked" | "general/net/menu-icons/icon-locked" | a mission above the player's level (the same id as `u3x_unlock_icon(3)` of G24) |

Checked for this document: the string id rule applied to the three names gives exactly the three numbers of the
old code (the check is 0x08208d0a, as G23 3.9 states; the three ids are different and non-zero; "123456789" gives
0x89a1897f).

### 3.8 Ids and texts

Page id: 0xdfdb36fa (PAGE_MISSIONS). Tab ids of that page: 0x102fc82e = mission set 1, 0x35fa8d17 = mission set 2,
every other tab (and "no tab", 0) = mission set 0.

Text slots (word 0x15): 0 name; 0xbee56a68 required level; 0xed9203d6 cost; 0xa6ed5ba5 reward; 0x8dc65186
duration; 0x7046d624 goal; 0x4bdd7c07 goal count; 0xdda1fdb4 time left.

Value ids (word 0x13): 0xe0bf7062 "is the active mission"; 0x4bdd7c07 goal count; 0x63f35e57 progress; 0x464129ab
"is completed".

Text ids with number placeholders (all through native 38, directly or through `u3h_text_num`):

| Text id | Text | Numbers |
|---|---|---|
| 0x9d59ec8a | one minute (singular) | minutes |
| 0x38e8ba93 | minutes (plural) | minutes |
| 0x669b8ea8 | one hour (singular) | hours |
| 0x5879aefb | hours (plural) | hours |
| 0x7ce24100 | one hour, one minute | hours, minutes |
| 0x422d0b5d | one hour, several minutes | hours, minutes |
| 0xe8f6e6ab | several hours, one minute | hours, minutes |
| 0x6093ed1f | several hours, several minutes | hours, minutes |
| 0x4afc6214 | goal count as a text (for goal kind 0x5c494554) | the goal count |

In the PS4 text files the one-number texts carry no number placeholder any more (that is what `u3x_x79_num`
repairs); the repair is applied to the four one-number texts only (4.5, 4.6).

Where the PS4 image holds these ids (read from the image file for this document; a cross-check, the new code keeps
the ids as ids like every other text id of these lists): the four one-number ids are the 32-bit immediates of
four consecutive 10-byte store instructions starting at image 0x5690ab (immediates at 0x5690b1 hour, 0x5690bb
hours, 0x5690c5 minute, 0x5690cf minutes), which fill a static array of u32 at image 0x1d40c48 in that order; the
minute pair is also at image 0x753f37 and 0x753f3c, where the game picks singular or plural by "number is 1", the
same rule as here. The four two-number ids and 0x4afc6214 occur nowhere in the PS4 image: they are PS3 1.19 ids
whose texts are in the game's text files.

Dialogs (arguments of `u3h_show_dialog` after `base` and `s`: body text, title text, button action, button
layout, extra word, close label):

| Dialog | Arguments |
|---|---|
| cancel the active mission | 0x84538677, 0x0260a263, 0xe6452a77, 0x075bc82d, 0x1bc6f0e5, 0 |
| take a mission (none active) | 0x6748cb03, 0x0260a263, 0xea1f40c5, 0x075bc82d, 0x1dd02cc2, 0 |
| take a mission (another one is active) | 0x2aac16e8, 0x0260a263, 0xea1f40c5, 0x075bc82d, 0x1dd02cc2, 0 |

Button action 0xea1f40c5 comes back to the helper (`mission_take`, section 1). The helper has no handler for
0xe6452a77 (cancel): the PS4 game's own menu button handler still tests that id (a compare at image 0x64f9fb);
the cancelling is the game's business.

Texts of the helper's own (they must live in read-only helper data): the colour escapes "|@Cff7cce96|" (requirement
met, also used for the reward) and "|@Cfffe7c66|" (requirement not met); the dollar sign "$"; a single space " ".

### 3.9 Constants; no table of game values

There is no literal table of game values in this group. The mission table is game data read at run time (3.4); the
icon ids are generated from names (3.7). Two single constants belong to the helper's rules and stay as numbers:
86400, the length of a mission's window in seconds (24 hours), and 74, the level value LAN play counts as at least
(the top level, rank 75, counted from 0).

## 4. Behaviour

Shared wording:

- "Text answer = T": the pointer at byte 0x40 of the request becomes T.
- "Show icon X": first `u3h_icon_index(base, X)`; only when its result is 0 or more, `u3h_set_icon(base, ev, X)`.
- "Decimal": plain digits, with a leading "-" for a negative number. "With thousands separators": a comma between
  groups of three digits counted from the right ("1,234,567"; no comma below 1000; a negative number is "-1,234").
- "Native k is called" always means through the pointer in the state object, read at the time of the call.

### 4.1 The four small accessors

**`u3x_mission_obj`.** The mission object of the mission user (3.5). No other effect; never null.

**`u3x_mission_profile`.** The profile object of the mission user (3.5), or null when the profile array pointer is
null.

**`u3x_mission_finished`.** 1 when `pr` is not null, `id` is below 0x400 and the completed bit of `id` (3.5) is
set; else 0 (also for every `id` of 0x400 and above: no memory is read for those).

**`u3x_player_level`.** When the debug level override (s32 at image 0x17d7abc) is non-zero: the override minus 1,
and no call. Otherwise: 0 when native 21 is null or `pr` is null (no call); else the result of one call of native
21(`pr`), the whole 32-bit rank word taken as s32.

### 4.2 `u3x_mission_set`

1. `u3h_sym_ready(base, s, 0x4360c988)` is 0: null, nothing else.
2. table = null when native 13 is null, else native 13(0x4360c988). (The call is made before `set` is looked at,
   also for a negative `set`.)
3. Null when the table is null, `set` is negative, or `set` is not below the table's number of sets (unsigned).
4. Otherwise the address of set number `set`: the table's sets pointer + 0x20 · `set`. The sets pointer is not
   tested for null.

### 4.3 `u3x_mission_seconds_left`

start = the u32 at `pr` + 0x1868, or 0 when `pr` is null. When start is 0 or the clock function pointer (3.1) is
null: 0, and the clock is not called. Otherwise the clock is called once with a null argument; with now = its s64
result and start zero-extended to 64 bits:

left = 86400 − (now − start), in 64-bit signed arithmetic.

The result is 0 when left is negative, 86400 when left is above 86400 (a clock that reads earlier than the start),
else left.

### 4.4 `u3x_x79_num`

When the text at `o` contains at least one of the characters "0" .. "9" anywhere: nothing is changed.

Otherwise the text is replaced, in place, by: `v` in decimal, one space, then the first 40 characters of the old
text (an old text longer than 40 characters is cut there), then a NUL. An empty old text gives the number and the
space. (At most 11 + 1 + 40 + 1 = 53 bytes are written.)

### 4.5 `u3x_mission_time_text`

1. The first byte of `o` is set to 0.
2. When native 38 is null: done (the text stays empty).
3. hours = `t` divided by 3600 and minutes = (`t` divided by 60) modulo 60, both as C integer division: the
   quotient is cut towards zero and the remainder has the sign of `t` (for a negative `t` both can be negative).
4. The text id and its two numbers:

   | Case | Text id | First number | Second number |
   |---|---|---|---|
   | hours = 0, minutes = 1 | 0x9d59ec8a | minutes | 0 |
   | hours = 0, minutes not 1 | 0x38e8ba93 | minutes | 0 |
   | hours = 1, minutes = 0 | 0x669b8ea8 | hours | 0 |
   | hours not 0 and not 1, minutes = 0 | 0x5879aefb | hours | 0 |
   | hours = 1, minutes = 1 | 0x7ce24100 | hours | minutes |
   | hours = 1, minutes not 0 and not 1 | 0x422d0b5d | hours | minutes |
   | hours not 0 and not 1, minutes = 1 | 0xe8f6e6ab | hours | minutes |
   | hours not 0 and not 1, minutes not 0 and not 1 | 0x6093ed1f | hours | minutes |

5. Native 38(base + 0x17026b8, `o`, 0x40, text id, first number, second number); the numbers are passed as s64
   (sign-extended).
6. When hours is 0 or minutes is 0 (the four one-number texts): `u3x_x79_num(o, first number)`.

### 4.6 `u3x_missions_source`

Steps, in this order:

1. Source descriptor (pointer at byte 0x98) null, or its tag not 0x4c414ed9: return 0, nothing touched.
2. `u3h_gate_closed(base, ev)` non-zero: return 1.
3. The image base is stored in the state object (offset 760).
4. object = `u3x_mission_obj(base)`, profile = `u3x_mission_profile(base)` (both of the mission user; plain reads).
5. The tab picks the set. Only when `u3h_active_page(base)` is 0xdfdb36fa: tab = `u3h_current_tab(base, s)`;
   wanted = 1 for tab 0x102fc82e, 2 for tab 0x35fa8d17, 0 for every other value. When wanted differs from the
   shown set (s + 0x3904): the selected row (s32 at object + 0x1010) becomes 0 and the shown set becomes wanted.
   On any other page `u3h_current_tab` is not called and the shown set stays what it was.
6. The s32 at object + 0x100c = the shown set (on every request that gets here).
7. set = `u3x_mission_set(base, s, shown set)`. Null: for operation 6, word 1 = −1; return 1 in every case,
   nothing else (no level, no buffer byte).
8. Set-up, for every operation:
   - count = the set's number of missions; missions = the set's missions pointer; row = word 1 (s32).
   - "The row's mission" = the entry number row (missions + 0x4c · row) when missions is not null and 0 ≤ row <
     count; otherwise there is none.
   - active = the u32 at profile + 0x1864, or 0 without a profile.
   - level, in this order:
     a. level = `u3x_player_level(base, s, profile)` (one native 21 call when the override is 0, native 21 is not
        null and there is a profile; none otherwise).
     b. When native 21 is not null and there is a profile: native 21(profile) is called and its result taken as
        s32; when it is greater than level (signed), native 21(profile) is called **once more** and that last
        result, as s32, becomes level. (So the override can only raise the level. With the override at 0 this
        makes two calls, three when the second answer is greater than the first.)
     c. When the LAN byte (u8 at image 0x17d5c60) is non-zero and level is below 74: level = 74.
   - cash = the u32 at profile + 0x74c, or 0 without a profile.
   - The first byte of the text buffer (s + 0x2c80) is set to 0.
9. By operation, then return 1 (also for operations not listed):

| Operation | Effect |
|---|---|
| 2 | row text, see below |
| 4 | row icon, see below |
| 5 | word 1 = the selected row (s32 at object + 0x1010, as it is after step 5) |
| 6 | word 1 = count − 1 (−1 for an empty set) |
| 10 | the selected row = row (stored as it is, not compared with anything) |
| 11 | value query, see below |
| 0x16 | click, see below |

"Level too low for a mission" below means: level + 1 is less than the mission's required level (s32 at +0x20),
signed 32-bit comparison.

**Operation 2 (row text).** Nothing without the row's mission. Otherwise by text slot (word 0x15):

| Text slot | Text answer |
|---|---|
| 0 | `u3h_text(base, s, name id of the mission)` |
| 0xbee56a68 | the buffer: "\|@Cfffe7c66\|" when the level is too low for the mission, else "\|@Cff7cce96\|"; then the required level in decimal |
| 0xed9203d6 | the buffer: "\|@Cfffe7c66\|" when cash is below the mission's cost (unsigned comparison), else "\|@Cff7cce96\|"; then "$"; then the cost (unsigned) in decimal with thousands separators |
| 0xa6ed5ba5 | the buffer: "\|@Cff7cce96\|", "$", the reward (signed) in decimal with thousands separators |
| 0x8dc65186 | n = the mission's duration number (s32 at +0x18). `u3h_text_num(base, s, buffer, 0x40, 0x9d59ec8a when n is 1 else 0x38e8ba93, n)`, then `u3x_x79_num(buffer, n)`; text answer = the buffer |
| 0x7046d624 | `u3h_text_num(base, s, buffer, 0x100, goal text id of the mission, goal count)`; text answer = the buffer |
| 0x4bdd7c07 | goal kind 0x5c494554: `u3h_text_num(base, s, buffer, 0x80, 0x4afc6214, goal count)`. Any other kind: the buffer = the goal count in decimal. Text answer = the buffer |
| 0xdda1fdb4 | only when the mission's id equals active: `u3x_mission_time_text(base, s, u3x_mission_seconds_left(profile), buffer)`; text answer = the buffer. Otherwise nothing |
| other | nothing |

"The buffer" is the text buffer of the state object; composed texts end with a NUL (they are far shorter than the
buffer's 250 characters). The seconds are computed before the time text is written.

**Operation 4 (row icon).** Nothing without the row's mission or when the image slot (word 0x14) is not 0.
Otherwise the first that applies:

1. `u3x_mission_finished(profile, id of the mission)` non-zero: show icon "completed" check.
2. The mission's id equals active: show icon "active" star.
3. The level is too low for the mission: show icon "locked".
4. Otherwise nothing.

**Operation 11 (value query).** Nothing without the row's mission. Otherwise by value id (word 0x13); other ids:
nothing:

| Value id | Word 0xe |
|---|---|
| 0xe0bf7062 | 1 when the mission's id equals active, else 0 |
| 0x4bdd7c07 | the mission's goal count |
| 0x63f35e57 | the profile's progress (s32 at profile + 0xf4c) when the mission's id equals active and active is not 0; else 0 |
| 0x464129ab | `u3x_mission_finished(profile, id of the mission)` |

**Operation 0x16 (click).** It works on the **selected** row (the s32 at object + 0x1010 taken as u32), not on
word 1 and not on a clicked-row word. Nothing when missions is null or the selected row is not below count
(unsigned, so a negative selection is none). Otherwise, with the selected mission:

- Its id equals active: the "cancel the active mission" dialog (3.8). Done.
- Otherwise: nothing when native 39 is null, or when the low 8 bits of native 39(object, selected mission, 0) are
  0. Else the "take a mission" dialog: the "another one is active" variant when active is not 0, else the "none
  active" variant.

Nothing is stored by the click itself; the dialog's button does the rest (section 1).

### 4.7 `u3x_take_mission`

1. 0 when `profile`, `set` or `can` is null.
2. 0 when the set's missions pointer is null or `row` is not below the set's number of missions (unsigned).
3. mission = entry number `row`; id = its mission id.
4. 0 when the low 8 bits of `can(owner, mission, 0)` are 0; `done` is not called then.
5. 0 when `done(profile, id)` is non-zero. (`done` is not tested for null.)
6. The mission is taken, in this order: the u32 at profile + 0x1864 = id; the u32 at profile + 0xf4c = 0; then,
   when `clock` is not null, `clock(null)` is called once and the u32 at profile + 0x1868 = the low 32 bits of
   its result (without a clock the start time keeps its old value).
7. Return 1.

The cost is not taken from the cash, the level is not tested here (that is `can`'s business), and the profile is
not saved here (the caller does it).

## 5. Test scenarios

Everything can be tested with a fake image (the fields of 3.6 and four mission objects), a mission table in test
memory behind a stand-in for native 13, a profile array in test memory, a state object whose natives are recording
stand-ins, a fake helper state block behind `u3h_ext` whose eb[4..5] points to a recording clock stand-in, and
recording stubs for the `u3h_` functions of section 2 (`u3h_crc_sid` real, or checked separately). Compare memory
(request, state object, mission object, profile, image fields), results, and the order and arguments of all calls.
The native 38 stand-in should write a text that depends on its arguments (and one variant without digits), the
native 21 stand-in should be able to answer a different value on each call.

A useful table: three sets with 3, 0 and 2 missions, one set with a null missions pointer; ids 0, 5, 0x3ff, 0x400;
required levels 1, 10, 75; goal kinds 0x5c494554 and another.

Accessors:
1. `u3x_mission_obj` / `u3x_mission_profile`: user word 0, 2, 3 and 7 (counts as 3); profile array pointer null.
2. `u3x_mission_finished`: null profile; ids 0, 7, 8, 0x3ff with the bit set and clear; ids 0x400 and 0xffffffff
   (0, no read past the bit field).
3. `u3x_player_level`: override 1 (0, no call), 76 (75), −5 (−6); override 0 with native 21 null, with a null
   profile, with answers 0, 74, 0x1004a and 0x80000005 (a negative level).

Mission set:
4. Symbol not ready (no native 13 call); native 13 null; table null; set −1 (native 13 still called once), 0, the
   last, one beyond; a table with a null sets pointer and set 0 (null result) and set 1 (0x20).

Seconds left:
5. Null profile; start 0 (clock not called); clock pointer null; clock equal to start (86400), start + 1 (86399),
   start + 86399 (1), start + 86400 (0), start + 100000 (0), start − 10 (86400); start 0xfffffff0 with a clock of
   0x100000010 (86400 − 32); exactly one clock call with a null argument.

Number fix:
6. A text with a digit at the start, in the middle, at the end, inside a colour escape (unchanged); a text without
   digits and v = 0, 1, 45, −3, 2147483647, −2147483648; an empty text ("5 "); a text of exactly 40, 41 and 60
   characters (cut after 40).

Time text:
7. Native 38 null (empty text, first byte cleared). t = 0, 59, 60, 61, 120, 3599 (minutes only: singular for 60
   .. 119), 3600 (one hour), 7200 (hours), 3660, 3720, 7260, 7320 (the four two-number texts), 86400, 86399;
   t = −60 and −3600 (negative numbers); the six arguments of native 38 each time; the number fix applied for the
   one-number texts only (with a native 38 stand-in that writes no digit, and one that does).

List source:
8. Null descriptor; wrong tag (0, nothing touched). Gate closed (1, base not stored).
9. Tab handling: active page not the missions page (no `u3h_current_tab` call, shown set unchanged, object +
   0x100c still written); the missions page with tab 0x102fc82e, 0x35fa8d17, 0 and another id; a tab change
   (selection reset to 0) and the same tab again (selection kept).
10. Set not available: operation 6 (word 1 = −1), operations 2 and 5 (nothing; buffer byte untouched, no native 21
    call).
11. Level: every combination of 4.6 step 8 (override 0 / below the rank / above the rank; native 21 null; no
    profile; a native 21 stand-in that answers 3, 5, 9 on successive calls (level 9 after three calls); LAN byte
    set with a level below, at and above 74); the number of native 21 calls in each case; the calls also happen
    for operations 5, 6 and 10.
12. Operation 2, each text slot: with the row's mission, with row −1, row = count, a null missions pointer
    (nothing). Level slot: level + 1 below, equal to and above the requirement. Cost slot: cash below, equal,
    above; cost 0, 999, 1000, 4294967295; no profile. Reward: 0, 1500, −1500, 2147483647. Duration: n = 1 (the
    singular id), 0, 2, −1; the size 0x40; the number fix after it. Goal: the size 0x100 and the two arguments.
    Goal count: both kinds (size 0x80 and text 0x4afc6214, or plain decimal, also negative). Time left: the
    active mission's row; another row (text answer untouched); id 0 with no active mission. An unknown slot.
13. Operation 4: image slot not 0; completed (check, also when the mission is the active one and too high);
    active; level too low; none of them (request untouched); an icon missing from the UI table (request
    untouched, `u3h_set_icon` not called); the three ids equal the string ids of the three names.
14. Operations 5, 6 (count 3, count 0), 10 (row 2, row −7, row 100).
15. Operation 11: each value id with the active row, another row, id 0 with no active mission (0xe0bf7062 gives
    1, 0x63f35e57 gives 0), no profile; an unknown id (word 0xe untouched); no mission for the row.
16. Operation 0x16: selection beyond the set, selection −1, null missions pointer (nothing); the active mission
    (cancel dialog, native 39 not called); another mission with native 39 null, answering 0, 1 and 0x100 (0x100 =
    refused); with and without an active mission (the two take dialogs); the eight arguments of each dialog call;
    word 1 different from the selection (the selection wins).
17. An unlisted operation (1, 3, 9, 12): return 1, buffer's first byte cleared, level computed, nothing else.

Take:
18. Null profile, null set, null `can` (0, no call); null missions pointer; row = count; `can` answering 0 and
    0x100 (0, `done` not called); `done` answering 1 (0, nothing stored); success with a clock (three fields, in
    the order of 4.7 step 6: a clock stand-in that reads the profile sees the new id and progress 0 and the old
    start time) and without a clock (start time kept); a clock answer above 32 bits (low 32 bits stored).

Needs the live game: only the look (texts and colours in the right columns, the icons in the cells), that the tab
switch shows the other set, the dialogs and what the cancel button then does, and the "mission progress" message
of stub kind 172.

## 6. Things that look like bugs or dead code (behaviour to keep)

1. The level of the list is the whole rank word of native 21 taken as a signed number, not its low 16 bits: a
   player with a legacy level has a level of 0x10000 and more (nothing is "locked"), and a rank word with bit 31
   set gives a negative level (every mission with a requirement shows as locked, unless LAN play lifts the level
   to 74).
2. Native 21 is asked two or three times per request (4.6 step 8), and for every operation, also for those that
   do not use the level; the third call's answer is used, not the second's.
3. The debug level override can only raise the level of this list: the real rank wins when it is higher.
4. In LAN play the level counts as at least 74, whatever the profile says.
5. The click (operation 0x16) uses the selected row, never the row of the request. Operation 10 stores any row,
   also a negative one or one beyond the set; all readers cope (no mission).
6. The first byte of the shared text buffer is cleared on every request that gets past the set test, also for
   operations that compose nothing.
7. The goal text passes a size of 0x100 for the shared buffer, which other lists treat as 251 characters.
8. "The mission's id equals active" is also true for a mission with id 0 while no mission is active (or there is
   no profile): such a row gets the star, the time-left text and the "is active" value 1. Only the progress value
   also tests that active is not 0.
9. A completed mission that is active shows the check, not the star.
10. `u3x_x79_num` keeps only the first 40 characters of the text it repairs, and any digit anywhere in the text
    (also one inside a colour escape) counts as "the number is there".
11. With native 38 null the duration column shows the bare number and a space ("5 "), while the time-left text
    stays empty.
12. The duration column uses the minute texts (the same ids the time text uses for minutes) for the mission's
    duration number.
13. `u3x_mission_set` asks native 13 before it looks at the set number and does not test the sets pointer.
14. `u3x_take_mission` does not test `done` for null, does not take the cost from the cash and stores only the
    low 32 bits of the clock; `u3x_mission_seconds_left` reads the start back as an unsigned 32-bit value.
15. A clock that reads earlier than the start gives the full 86400 seconds.
16. When the missions list is asked while another page is on top, the set of the last tab seen is used.
17. Operation 6 answers −1 for an empty set; operation 4 answers only image slot 0; operation 11 writes word 0xe
    only for its four ids.
18. `u3x_x79_num`, `u3x_mission_time_text` and `u3x_player_level` have C declarations but no C caller.

## 7. Open questions and notes for other groups

1. The meaning of the mission entry's number at +0x18 is not settled: the old sources call it "hours" in one
   place and "games" in another, and the list shows it with the minute texts. The behaviour does not depend on
   the meaning.
2. 74 (3.9) is written as a number. It is presumably "number of levels − 1" of the XP table (G22 3.9: the s32 at
   +0x68 of the data of symbol 0xc85e199d), but the old code does not read it from there, and a table with
   another count would change the result; the description keeps the number. The owner may decide to read it.
3. The eight time-text ids and the text 0x4afc6214 are ids of game texts (the texts themselves are in the game's
   text files and are fetched at run time), chosen by the rule of 4.5; they are kept as ids, like the text ids of
   G23 and G24. Four of them could be read from the running game instead (the static array at image 0x1d40c48,
   3.8), but it was not checked when the game fills that array, and the other five ids are in no PS4 image
   location; reading four of nine would not make the code simpler. The owner may decide otherwise.
4. The cancel action 0xe6452a77 is handled by the game (3.8). What exactly the game's handler resets was not
   looked at; this group only opens the dialog.
5. As G24 (open question 2) asks, the three icons are generated from names here. The integrator may let this
   list call `u3x_unlock_icon(3)` (`uint32_t u3x_unlock_icon(int status)`, G24 4.1) for the "locked" icon instead
   of holding the name a second time; the id is the same.
6. `u3x_mission_obj`, `u3x_mission_profile`, `u3x_mission_finished` and `u3x_player_level` are from the
   inventory's trivial list. They are described here (4.1) so that the whole missions block is written from this
   document; nothing else in the helper's C++ part calls them.

## 8. Clarifications (tester, 2026-11-14)

Facts confirmed by the differential test (`reimpl2\G26\test_g26.cpp`, old v7.212 against the new `part.cpp`) and,
where a rule of the C++ language leaves the old source open, by the code of the shipped v7.212 helper. They settle
the implementer's questions. Nothing in sections 1 to 6 was found wrong; these items make precise what the text
leaves open.

1. **"Level + 1" at the top of the range.** Level + 1 is computed in 32 bits and wraps: with a level of
   0x7fffffff it is −2147483648, so every requirement except −2147483648 itself counts as "too low" (level slot
   red, lock icon). The same holds for "override minus 1" (4.1): an override of 0x80000000 gives the level
   0x7fffffff.
2. **Rows and counts of 0x80000000 and more.** The row of the request (word 1) is a signed 32-bit number: a
   negative row has no mission, whatever the count. A row of 0 or more is compared with the count as unsigned 32-bit
   numbers, so in a set with a count of 0x80000000 or more the row 0x7fffffff has a mission. Operation 6 answers
   count − 1 computed in 32 bits (count 0 gives −1, count 0x80000000 gives 0x7fffffff). The selected row of the
   click and the `row` of `u3x_take_mission` are unsigned 32-bit numbers, compared with the count unsigned and
   zero-extended for the address of the entry (a selection of 0x80000005 in a set with a count of 0xffffffff is
   entry number 0x80000005).
3. **Results of the natives and stand-ins.** Native 39 and `can`: only the low 8 bits count; 0x100 and
   0xffffffffffffff00 refuse, 0x80, 0xff and 0x101 allow. `done`: all 32 bits count (0x100 and 0x80000000 are
   "completed"); the upper half of the register is ignored. Native 21: the low 32 bits, taken as s32; the upper
   half of the register is ignored. Native 13: a pointer, tested for null with all 64 bits. `u3h_gate_closed` and
   `u3h_sym_ready`: every non-zero 32-bit result counts as "yes". The clock: all 64 bits enter the subtraction of
   4.3; `u3x_take_mission` stores its low 32 bits.
4. **Native 21 in the list (4.6 step 8).** The pointer is tested for null by the level accessor (step a) and
   once more, together with the profile, before the second call (step b). There is no further test before the
   third call. The old code reads the pointer once at step b and makes the second and the third call through that
   value; a reimplementation that reads the pointer again for the third call behaves the same, because nothing
   that native 21 calls writes the state object (a difference shows only with a stand-in that replaces or clears
   native 21 inside its own second call).
5. **`u3x_take_mission`, order of reading.** The mission id is read before `can` is called. When `can` or `done`
   changes the entry, `done` still gets, and the profile still stores, the id as it was before the `can` call.
   Likewise in the click (operation 0x16): the active id and the selected entry's id are read before native 39 is
   called, and the dialog variant follows the active id as it was then.
6. **`u3x_mission_seconds_left`, the arithmetic.** left = 86400 − (now − start) is computed modulo 2^64 and then
   taken as a signed 64-bit number (no overflow case behaves differently). Start 0: result 0 and no clock call,
   also when there is no profile. A clock that reads earlier than the start, or −1 (a failed clock), gives 86400;
   exactly 86400 seconds after the start gives 0, one second less gives 1. Start 0xfffffff0 with a clock of
   0x100000010 gives 86400 − 32 (the start is zero-extended, the clock is not cut). The clock is called exactly
   once, with a null argument.
7. **Order of reading relative to the calls in the list.** Observable with the real callees is only what sections
   4.6 and 4.7 say. For the record, the old code reads: the row (word 1), the set's count and missions pointer and
   the active id after the set lookup and before the native 21 calls; the cash and the LAN byte after them; the
   selected row and the fields of a mission entry when they are used. The operation (word 0) is read by the old
   code after the set lookup (for the "no set" answer) and again after the native 21 calls; reading it earlier is
   not observable, because `u3h_sym_ready`, native 13 and native 21 do not write the request.
8. **The text buffer.** No call is made while a level, cost, reward or plain goal count text is composed.
   `u3h_text_num` and native 38 (through `u3x_mission_time_text`) receive the buffer with its first byte already
   0. The operation is compared with all 32 bits (0x10002 is an unlisted operation, not operation 2).
9. **The three icons.** The string id rule gives 0x08208d0a, 0xe2315f64 and 0x7bebc95f for the three names of
   3.7, in this order; these are the numbers that reach `u3h_icon_index` and `u3h_set_icon`. An icon with index 0
   is shown; a negative index of any value is "not there".
10. **`u3x_x79_num`.** The digit test looks at the whole text, also beyond its 40th character (a text of 60
    characters with a digit at the end stays unchanged). "/" and ":" (the characters next to the digits) are not
    digits. Bytes of 0x80 and above are not digits.
