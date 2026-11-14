# G06. Cinema film rows and film HUD tabs

Behaviour description for a reimplementation (reimpl2 pipeline, describer's document). It says what the functions
do, not how the old ones were written. All offsets are hexadecimal when prefixed with 0x. "Image offset X" means the
address `base + X`, where `base` is the game image base every function receives. All multi-byte values are
little-endian; "u32 / s32 / u16 / s16 / s64 / u8" are unsigned / signed integers of that width, "pointer" is 8 bytes.

One function of the group, the C function `cin_fixed`, is not described here: it "formats a float as fixed-point
text" and is covered by `film_cameras.md`. Section 4.5 explains why the megabytes text below is specified on its own
and must not be routed through a formatter that takes the megabyte value as a 32-bit float.

## 1. Purpose and callers

The group has two unrelated halves.

**A. The Cinema film list (menu list `LIST_CINEMA`).** The menu asks a list data source for its title, the text of
each column of each row, the images of each row, the selected row, the last row index, and tells it when the player
picks a row. The helper answers these requests for the list of recorded films (the local films or the online files).

- `u3x_cinema_rows` is called by the C function `cinema_rows`, itself called only by `cinema_source` (the helper's
  list-source hook) for every request to the list whose source tag is 0x4c414e39, after the hook's own gate. When
  `u3x_cinema_rows` returns 0 (Cinema switched off) the C caller answers an empty list itself; when it returns 1 the
  request is answered.
- `u3x_cin_netgame` (net game record by index) is used by `u3x_cinema_rows` and through the C wrapper `cin_netgame`.
- `u3x_cin_icon_index` (index of an image in a UI image table) and `u3x_cin_image` (fill the image answer of a
  request) are used by `u3x_cinema_rows` and are reachable from C through the wrappers `cin_icon_index` / `cin_img`
  (no other user in v7.212).
- The megabytes text (old C export `u3h_cin_megabytes`) is used only by `u3x_cinema_rows`, for the size column.

**B. The film playback HUD's tab bar.**

- `u3x_k23_rep` (old internal name `rw::button_fires`): "does this pad button fire this frame" with auto-repeat. Called
  twice per frame by the tab bar update (`rw::update_bar`, another group: once for up, once for down) and reachable
  from C through the wrapper `k23_rep`.
- `u3x_k23_hud` (old internal name `rw::bar_hook`): the hook that the helper's stub dispatcher runs (stub kind 160,
  at the place where the game's HUD draw would update and draw the tab bar during film playback) once per HUD draw.
  Called through the C wrapper `k23_hud`.

## 2. Interface

No function takes or returns a float by value. The names below are the existing exported names; they and their
signatures stay. The old internal functions `ui_icon_index`, `image_answer` (anonymous namespace), `rw::button_fires`
and `rw::bar_hook` were only the bodies behind these exports; a new part can deliver the exported functions directly.
The one remaining internal user is `rw::update_bar` (other group), which calls `button_fires(base, pad, mask)` and
uses the result as a truth value: it has to reach the new `u3x_k23_rep` (same arguments, non-zero = fires).

| Function | C++ signature (`extern "C"`) | Called by | C wrapper passes |
|---|---|---|---|
| icon index | `int32_t u3x_cin_icon_index(uintptr_t base, uint32_t id, int which)` | `u3x_cinema_rows`; C `cin_icon_index` | its three arguments unchanged |
| image answer | `void u3x_cin_image(int32_t *ev, int32_t type, void *ptr, int32_t a4, int32_t a5, int32_t a6, int32_t a7)` | `u3x_cinema_rows`; C `cin_img` | its seven arguments unchanged |
| net game record | `uint8_t *u3x_cin_netgame(void *s, int idx)` | `u3x_cinema_rows`; C `cin_netgame` | state pointer, index |
| list source | `int u3x_cinema_rows(uintptr_t base, int32_t *ev, void *s)` | C `cinema_rows` | base, the request, the state |
| megabytes text | `void u3x_cin_megabytes(uintptr_t base, void *s, char *b, const uint8_t *e)` (new name; replaces the C body of `u3h_cin_megabytes`) | `u3x_cinema_rows` | if the C export `u3h_cin_megabytes` is kept, it passes its four arguments unchanged |
| button fires | `int u3x_k23_rep(uintptr_t base, uint8_t *pad, uint32_t m)` | `rw::update_bar`; C `k23_rep` | its three arguments unchanged |
| HUD hook | `int u3x_k23_hud(uintptr_t base, uint64_t *r)` | C `k23_hud` (stub kind 160) | base, the stub's saved-register array |

Parameters:

- `base`: game image base.
- `s`: the helper's state object (opaque; the fields used are in 3.2).
- `ev`: one list request, an array of 32-bit words (fields in 3.3). Words are written "word n" = byte offset 4·n.
- `id`: a 32-bit name id. `which`: 0 = first UI image table, anything else = second.
- `type`, `ptr`, `a4` .. `a7`: values stored into the image answer (4.2).
- `idx`: net game index.
- `b`: output buffer of 0x40 bytes. `e`: one film list entry (3.5).
- `pad`: a game pad record (3.8). `m`: button mask.
- `r`: array of 64-bit saved registers; only elements 3 and 14 are used.

Results: `u3x_cin_icon_index` the index or −1; `u3x_cin_netgame` a record pointer or null; `u3x_cinema_rows` 0 or 1;
`u3x_k23_rep` 1 (fires) or 0; `u3x_k23_hud` always 0.

Helpers of the C part and of other groups that the new code calls (not to be described or reimplemented here):

| Name | Signature | What it does |
|---|---|---|
| `u3h_ext` | `uint8_t *u3h_ext(void)` | address of the helper's own state block ("ExtState", 3.1) |
| `u3h_cin_state` | `uint8_t *u3h_cin_state(uintptr_t base)` | the game's film state, = image offset 0x17c3ba8 |
| `u3h_cin_buffer` | `char *u3h_cin_buffer(int which)` | two static text buffers of the C part: 0 = row text, 0x200 bytes; non-zero = scratch, 0x100 bytes |
| `u3h_text` | `const char *u3h_text(uintptr_t base, void *s, uint32_t id)` | localized text of a text id; never null (empty string when there is none) |
| `u3h_cin_cat` | `char *u3h_cin_cat(char *o, char *end, const char *t)` | copies the characters of `t` to `o` while there are characters and the write position is below `end`, writes a NUL at the final position, returns that position |
| `u3h_cin_num` | `char *u3h_cin_num(char *o, char *end, int64_t v, int width)` | decimal text of `v`: a '-' first when `v` < 0, then the digits of the magnitude padded with leading zeros to at least `width` digits (never more than 22 digits); each character only while the write position is below `end`; NUL at the final position, which is returned |
| `u3h_cin_localtime` | `const int32_t *u3h_cin_localtime(const int64_t *t)` | calendar fields of a time in seconds since 1970 (the port has no time zone: UTC), as s32 words in a static record: word 1 minute, word 2 hour (0..23), word 3 day of month (1..31), word 4 month (0..11), word 5 year minus 1900 |
| `u3x_film_select` | `int u3x_film_select(uintptr_t base, void *s, int idx)` | selects local film `idx` (group G05) |
| `u3x_k23_callees` | `int u3x_k23_callees(uintptr_t base)` | non-zero when the game functions the tab bar calls are present (other group) |
| `u3x_k23_update` | `void u3x_k23_update(uintptr_t base, uint8_t *el)` | one update of the tab bar element (other group) |
| `u3h_k26_draw` | `int u3h_k26_draw(uintptr_t b, void *el, void *rbp)` | draws the tab bar element (other group) |

## 3. Game and helper facts

### 3.1 The helper's state block (ExtState, address from `u3h_ext()`)

| Offset | Type | Meaning |
|---|---|---|
| 0x3210, 0x3218, 0x3220, 0x3228 | 4 pointers | the four kernel imports the Cinema needs |
| 0x3250 | u32 | counter: calls of the film list source (Cinema counter 8) |
| 0x3254 | u32 | last local film picked (Cinema counter 9) |
| 0x3a70 | u32 | function switches; bit 1 (value 2) = Cinema port on |
| 0x4054 | u32 | switch word "eb[1]": bit 0 = skip the tab bar entirely, bit 1 = update but do not draw |
| 0x40b0 | s32 | the selected row of the online file list ("online selection") |
| 0x4370 | u32 | counter: HUD hook entries |
| 0x4374 | u32 | counter: tab bar updates run |
| 0x437c | u32 | counter: draws skipped |

"Cinema is on" = bit 1 of the word at 0x3a70 is set and all four pointers at 0x3210..0x3228 are non-null.

### 3.2 The state object `s`

| Offset | Type | Meaning |
|---|---|---|
| 656 | pointer to function | script symbol lookup: called with one u32 id; returns null or a pointer to a pointer to the symbol's data. May itself be null. |
| 800 | pointer to function | "native 4", the game's text lookup (used only by `u3h_text`; its presence matters in 4.5) |
| 872 | pointer to function | "native 13", the game's symbol value lookup: one u32 id argument, returns a data pointer or null |
| 928 | pointer to function | "native 20", the game's texture-by-name lookup: arguments (address of image offset 0x1b31128, pointer to a NUL-terminated name), returns a texture pointer or null. May be null. |

(Natives are an array of pointers at offset 768; native k is at 768 + 8·k.)

### 3.3 The list request `ev`

| Byte offset | Word | Type | Meaning |
|---|---|---|---|
| 0x00 | 0 | s32 | operation: 1 title, 2 row text, 4 row image, 5 get selection, 6 get last index, 10 row picked |
| 0x04 | 1 | s32 | row index (in), or the answer of operations 5 and 6 (out) |
| 0x08 | 2 | s32 | image answer: kind |
| 0x10 | 4..5 | pointer | image answer: pointer |
| 0x18..0x24 | 6..9 | s32 ×4 | image answer: four values |
| 0x28 | 10 | s32 | image answer: set to 0 |
| 0x2c | 11 | s32 | image answer: set to −1 |
| 0x30 | 12 | u32 | image answer: scale, float |
| 0x40 | 16..17 | pointer | text answer (pointer to a NUL-terminated string) |
| 0x50 | 0x14 | u32 | image slot id (operation 4) |
| 0x54 | 0x15 | u32 | column id (operation 2) |

### 3.4 Game data

| Image offset | Type | Meaning |
|---|---|---|
| 0x16e7168 | u8 | list flag: non-zero = the local films are shown, 0 = the online files |
| 0x17d603c | u32 | net menu mode; the Cinema is mode 4 |
| 0x17c3ba8 | structure | film state: +0x1e48 s32 selected local film; +0x1e4c s32 local film count; +0x1e50 pointer to the local film entries; +0x1e58 s32 online file count; +0x1e60 pointer to the online file entries |
| 0x17cb060 | object | the net game manager (passed as first argument to its functions); its custom-rules block is at image offset 0x17cb060 + 0x6a38 = 0x17d1a98 |
| 0x1acd198 | structure | language table: +0x604 u32 index of the current language; the language id of entry i is the u32 at +8 + 0x38·i. Language id 0 = English. |
| 0x17b9cfc | u32 | region setting (= 0x17b6368 + 0x3994); 1 = the region with month-first dates and the 12-hour clock |
| 0x1b24bc8 | pointer | UI object, may be null: +0x20 pointer to the first image table, +0x28 pointer to the second. An image table: +2 u16 entry count, +8 pointer to the entries; an entry is 40 bytes with its u32 id at +8. |
| 0x1b31128 | object | texture manager (first argument of native 20) |
| 0x1b24b98 | pointer | array of clocks, 64 bytes each: +0x54 s32 current time in ticks, +0x70 s32 the time at the previous frame, in ticks. A tick is 1/300 s. |
| 0x17b4a68 | u64 | non-zero while a cut-scene runs (= 0x17b4a48 + 0x20) |

Script symbols (through the lookup at `s` + 656, or native 13):

- 0x3b9a067d, the net game table: +0 s32 record count, +8 pointer to the records; a record is 0xd0 bytes:
  +4 u32 map id (also the value shown as the net game image), +0xc s32 game type index.
- 0x0dec97a4 (through native 13), the map table: +0 u32 entry count, +8 pointer to the entries; an entry is 0x98
  bytes: +0 u32 map id, +4 u32 text id of the map's name.

### 3.5 A film list entry (0x1e90 bytes; at most 20 per list)

| Offset | Type | Meaning |
|---|---|---|
| 0x210 | u32 | file size in bytes |
| 0x214 | u32 | file mode; a film is unprotected when (mode AND 0x180) = 0x180 |
| 0x220 | u32 | net game index: only the low 16 bits count, read as s16 |
| 0x230 | s32 | duration in seconds |
| 0x238 | s64 | recording time, seconds since 1970 |
| 0x258 | text | user name, NUL-terminated |
| 0x378 | text | film title, NUL-terminated |
| 0x3f8 | s32 | the number shown in column 0x7cc44c67 |

(The film header starts at entry + 0x218; the last six rows are header + 8, + 0x18, + 0x20, + 0x40, + 0x160, + 0x1e0.)

### 3.6 Game functions

All use the System V x86-64 convention (integer and pointer arguments only).

| Image offset | Arguments | Result | Meaning |
|---|---|---|---|
| 0x741260 | (net game manager = address of image offset 0x17cb060, output buffer, s32 buffer size, net game record, address of image offset 0x17d1a98, s32 1, s32 1, s32 1): eight arguments, the last two on the stack | ignored | writes the display name of a net game into the buffer |
| 0x741070 | (net game manager, s32 game type index) | pointer to a game type record or null | game type record: +0 u32 text id of its name, +0x40 pointer to the name of its image (may be null) |

### 3.7 Ids and texts

- Title text ids: 0xe626e948 (local films), 0xdc27f8a0 (online files).
- Decimal separator text id: 0xaaa1ef9b.
- Column ids: 0 (net game and title), 0x8adf5c31 (user), 0x7cc44c67 (number), 0x9586358c (duration), 0x0ed4812e
  (size), 0x8419ca2a (date), 0x16094dd2 (map), 0xa42c91c5 (game type).
- Image slot ids: 0x2138a9b2 (net game image), 0xa42c91c5 (game type image), 0 (lock).
- Lock icon id: 0xea36b17f.
- Fixed texts: `" - "`, `":"`, `"/"`, `" "`, `"AM"`, `"PM"`, `" MB"`, and the empty string.

### 3.8 The pad record

| Offset | Type | Meaning |
|---|---|---|
| 0x94 | u32 | buttons that went down this frame (bit mask) |
| 0x9c | u32 | buttons held (bit mask) |
| 0xa8 | s32 | index of the pad's clock in the clock array |
| 0xbc + 4·n, n = 0..21 | float (32 bits) | the time, in seconds of the pad's clock, at which button bit n went down |

### 3.9 Float constants (32-bit patterns)

| Pattern | Value | Meaning |
|---|---|---|
| 0x3f800000 | 1.0 | image scale |
| 0x3b5a740e | 1/300 (0.0033333334) | seconds per tick |
| 0x3e4ccccd | 0.2 | delay before the auto-repeat starts |
| 0x3dcccccd | 0.1 | first repeat step |
| 0x3f000000 | 0.5 | step halving |
| 0x40000000 | 2.0 | hold time after which the step is halved |
| 0x40800000 | 4.0 | hold time after which the step is halved again |
| 0xbf800000 | −1.0 | press time used when the mask names no button with a press time |
| 0x35800000 | 2^-20 (9.5367431640625e-07) | bytes to megabytes (4.5) |

### 3.10 The HUD

The tab bar element is at HUD + 0x2408. `r[3]` of the hook's register array is the HUD pointer, `r[14]` the frame
pointer of the interrupted HUD draw (handed to the draw function unchanged).

## 4. Behaviour

### 4.1 `u3x_cin_icon_index(base, id, which)`

Result: the zero-based position of the first entry whose id equals `id` in the UI image table selected by `which`
(0: the first table, otherwise the second), or −1. It is −1 when the UI object pointer is null, when the table pointer
is null, when the table's entry pointer is null, when the count is 0 or when no entry matches. At most `count`
(u16) entries are examined, in ascending order. Nothing is written, no game function is called.

### 4.2 `u3x_cin_image(ev, type, ptr, a4, a5, a6, a7)`

Writes the image answer of the request, nothing else: word 2 = `type`; the pointer at byte 0x10 = `ptr`; words 6, 7,
8, 9 = `a4`, `a5`, `a6`, `a7`; word 10 = 0; word 11 = −1; word 12 = 0x3f800000. Word 3 is not written. No checks.

### 4.3 `u3x_cin_netgame(s, idx)`

Result: the address of record `idx` of the net game table (records pointer + `idx` · 0xd0), or null. Steps that are
observable: if the lookup function pointer at `s` + 656 is non-null it is called exactly once with 0x3b9a067d; a null
function pointer, a null result, or a null pointer behind the result gives null (the lookup is called before `idx` is
looked at). Null also when `idx` < 0, when `idx` ≥ the table's record count (s32), or when the records pointer is
null. Nothing is written.

### 4.4 `u3x_cinema_rows(base, ev, s)`

In this order:

1. If Cinema is not on (3.1): return 0. Nothing else happens.
2. Add 1 to the counter at ExtState + 0x3250.
3. If the net menu mode is not 4: the text answer becomes the empty string, word 1 becomes 0, return 1.
4. Clamp the online selection: let C = the online file count. If C ≠ 0 and the online selection is greater than C − 1,
   the online selection becomes C − 1. (This happens for every operation, and only this upper clamp.)
5. Determine the active list: local when the list flag is non-zero, else online. The active count is the local film
   count or the online file count, the active entries pointer the corresponding one. The requested entry is entry
   number (word 1 taken as u32) of the active list (entry size 0x1e90), when the entries pointer is non-null. The
   entry address may be formed for any index but is only used after the guards of step 6.
6. Act on the operation (word 0), then return 1. Any operation not listed does nothing (the request is left as it
   is) and still returns 1.

**Operation 1 (title).** Text answer = `u3h_text` of 0xe626e948 when local, of 0xdc27f8a0 when online.

**Row guard** (operations 2 and 4). Nothing is done (no text, no image, no call) when the active count is 0, when
the active entries pointer is null, or when word 1 taken as u32 is 20 or more.

Let G = the entry's net game index (s16 of the low 16 bits of the u32 at entry + 0x220), and "the net game record" =
`u3x_cin_netgame(s, G)`, looked up only in the branches that say so (one lookup call per branch).

**Operation 2 (row text)**, by column id (word 0x15). "Row buffer" is the 0x200-byte buffer `u3h_cin_buffer(0)`;
every text built in it is cut after 0x1ff characters and NUL-terminated, and the text answer then points to the row
buffer (which the next request overwrites).

- Column 0, "net game - title". Look up the net game record. If there is none: text answer = the empty string, done.
  Otherwise fill the 0x100-byte scratch buffer `u3h_cin_buffer(1)` with zeros, then call game function 0x741260
  with (net game manager, scratch buffer, 0x100, the record, address of image offset 0x17d1a98, 1, 1, 1). The row
  text is: the scratch buffer's string, then `" - "`, then the film title (entry + 0x378).
- Column 0x8adf5c31, user. Text answer = the address entry + 0x258 itself (no copy).
- Column 0x7cc44c67, number. Row text = the s32 at entry + 0x3f8 in decimal (no padding; '-' for negative).
- Column 0x9586358c, duration. With D = the s32 at entry + 0x230: row text = (D / 60) in decimal, `":"`, then
  (D mod 60) in decimal padded to two digits. Division and remainder truncate toward zero (the remainder has the
  sign of D); a negative part is written as '-' followed by its magnitude (the remainder still padded to two digits),
  so D = −5 gives "0:-05".
- Column 0x0ed4812e, size. Row text = the megabytes text of the entry (4.5, produced into a separate 0x40-byte
  buffer) followed by `" MB"`.
- Column 0x8419ca2a, date and time. Take the calendar fields of the s64 at entry + 0x238 from `u3h_cin_localtime`
  (if it returned null nothing would be done; it never does). Read the current language id and the region (3.4).
  Build four short texts, each cut after 15 characters:
  - time: when the region is 1, the 12-hour form: H12 as two digits, `":"`, the minute as two digits, then `"AM"`
    when the hour is below 12 and `"PM"` otherwise, where H12 = hour mod 12, and 12 when that is 0. In any other
    region the 24-hour form: hour as two digits, `":"`, minute as two digits.
  - day: day of month as two digits; month: month field + 1 as two digits; year: year field + 1900 padded to four
    digits.
  - From the time, the day and the month text exactly one leading character is dropped when it is '0' (the year
    is never shortened).
  The row text is: first, `"/"`, second, `"/"`, year, `" "`, time, where (first, second) = (day, month), except
  (month, day) when the language id is 0 and the region is 1. Examples: region 1, English, 7 March 2026 09:05 gives
  "3/7/2026 9:05AM"; another region: "7/3/2026 9:05"; midnight in region 1 gives "12:00AM", in another region
  "0:00" (only one zero dropped).
- Column 0x16094dd2, map name. Look up the net game record; if there is none, nothing is done (text answer
  untouched). Otherwise call native 13 with 0x0dec97a4 (the function pointer is not tested: it is always present).
  The text id is 0, unless the result is non-null, its entries pointer is non-null, and an entry (searched in
  ascending order over the table's u32 count) has a map id equal to the record's map id (record + 4): then it is
  that first entry's name text id. Text answer = `u3h_text` of that id (also when the id is 0).
- Column 0xa42c91c5, game type name. Look up the net game record; none: nothing is done. Otherwise call game
  function 0x741070 with (net game manager, the s32 at record + 0xc). Text answer = `u3h_text` of the u32 at the
  start of the returned record, or of 0 when the result is null.
- Any other column: nothing is done.

**Operation 4 (row image)**, by slot id (word 0x14); "answer (k, p, v1, v2, v3, v4)" is 4.2 with those values:

- Slot 0x2138a9b2. Look up the net game record. When there is one: answer (0x24, null, the s32 at record + 4, 0, 0,
  −1). When there is none: nothing.
- Slot 0xa42c91c5. Look up the net game record; none: nothing. Otherwise call game function 0x741070 with (net
  game manager, the s32 at record + 0xc). Let N = the pointer at result + 0x40, or null when the result is null. When
  N is non-null and the native-20 pointer is non-null, call native 20 with (address of image offset 0x1b31128, N).
  If that gave a non-null texture: answer (2, the texture, 0, 0, 0, −1). In every other case: answer (0x10, null,
  0, 0, 0, −1).
- Slot 0, and the entry is protected, i.e. (the u32 at entry + 0x214 AND 0x180) ≠ 0x180: answer (3, null, 0,
  0xea36b17f, 0, the icon index of 0xea36b17f in the first UI image table (4.1), which may be −1). An unprotected
  entry: nothing.
- Any other slot: nothing.

**Operation 5.** Word 1 = the selected local film (film state + 0x1e48) when local, else the online selection.

**Operation 6.** Word 1 = the active count − 1 (so −1 for an empty list). No row guard.

**Operation 10.** When local: the counter at ExtState + 0x3254 = word 1, then `u3x_film_select(base, s, word 1)`
(its result is ignored). When online: the online selection = word 1 (not clamped here). No row guard.

Order of the observable calls inside one request: the net game lookup first, then the game function / native, then
`u3h_text`. No call is made that the branch does not name.

### 4.5 `u3x_cin_megabytes(base, s, b, e)`: the film size as megabytes with two decimals

Let S = the u32 at `e` + 0x210, W = S divided by 2^20 rounded down (0..4095), K = S mod 2^20, and
H = (K · 100) divided by 2^20, rounded down (0..99), with one exception: when the text-lookup native of the state
(the pointer at `s` + 800) is non-null and K = 828375, H is 79 (the plain rule gives 78).

The text written to `b` is: W in decimal without padding; then the decimal separator, i.e. the string
`u3h_text(base, s, 0xaaa1ef9b)` (called exactly once per call); then H as exactly two digits (leading zero when
H < 10). At most 0x3f characters are written in all: each character of the separator and of the two digits is
written only while fewer than 0x3f characters have been written; a NUL follows the last character written. Examples
with separator ".": S = 0 gives "0.00"; S = 1572864 gives "1.50"; S = 4294967295 gives "4095.99"; S = 33554431 gives
"31.99".

Where the rule comes from (so that an equivalent float formulation can be checked against it): the old code works on
the x87 unit. It converts S to floating point exactly and multiplies by 2^-20 (pattern 0x35800000): the megabyte
value V is exact and is **not** rounded to 32 bits (so 33554431 bytes are 31.99, not 32.00). W is V truncated; the
fraction V − W is exact; it is multiplied by 10.0 and again by 10.0 with no rounding in between (the product has at
most 27 significant bits and is exact). **Rounding point:** this product is stored as a 32-bit float (round to
nearest, ties to even) on the path on which the separator text is fetched through the text native, and is not stored
(stays exact) when the state has no text native; it is then truncated toward zero to give H. Rounding to 24 bits can
only change the truncated value when the exact product is 79 − 2^-18, which is K = 828375; that is the exception
above. The sign test of the formatter can never be true here (V ≥ 0, never NaN): no '-' is ever written.

A formatter that receives V as a 32-bit float (as the shared "formats a float as fixed-point text" function of
`film_cameras.md` would across a call) rounds V first and gives different digits for sizes of 16 MiB and more whose
byte count needs more than 24 bits; it therefore cannot be used for this text unless it is given W and the fraction
with the rounding points above.

### 4.6 `u3x_k23_rep(base, pad, m)`: a button fires (press or auto-repeat)

All float operations here are single-precision operations: **every** operation's result is rounded to a 32-bit
float (round to nearest, ties to even) before it is used again; nothing is kept in higher precision between two
operations. Comparisons are ordered: a comparison with a NaN operand is false.

1. If any button of `m` went down this frame (pad + 0x94 AND `m` ≠ 0): result 1.
2. Otherwise, if no button of `m` is held (pad + 0x9c AND `m` = 0): result 0.
3. Press time P: the 32-bit float at pad + 0xbc + 4·n, where n is the lowest bit set in `m` among bits 0..21; when
   `m` has none of these bits, P = −1.0 (0xbf800000).
4. Read the clock array pointer (image offset 0x1b24b98). If it is not a plausible pointer (below 0x10000, at or
   above 2^47, or not a multiple of 8): result 0. The pad's clock is at that pointer + 64 · (the s32 at pad + 0xa8,
   sign-extended).
5. Held time now: T1 = (the clock's current ticks, s32 at +0x54, converted to float) × (1/300 as 0x3b5a740e), then
   T1 − P. Held time at the previous frame: T0, the same with the s32 at +0x70. Order: convert (rounded), multiply
   (rounded), subtract (rounded).
6. If T1 < 0.2 (0x3e4ccccd): result 0.
7. If T0 < 0.2: result 1 (the repeat delay was crossed this frame).
8. Step: 0.1 (0x3dcccccd); multiplied by 0.5 if 2.0 < T1; multiplied by 0.5 again if 4.0 < T1 (the two tests are
   independent; this gives 0.1, 0.05 or 0.025).
9. Result 1 when the two repeat counts differ, else 0, where the count of a held time T is ((T − 0.2) / step)
   converted to s32 by truncation toward zero: subtract (rounded), divide (rounded), truncate. A value that is NaN or
   outside the s32 range converts to 0x80000000 (−2147483648).

Nothing is written; no game function is called.

### 4.7 `u3x_k23_hud(base, r)`: the film HUD hook

In this order; the result is always 0:

1. Add 1 to the counter at ExtState + 0x4370.
2. If bit 0 of the switch word (ExtState + 0x4054) is set: stop.
3. If the u64 at image offset 0x17b4a68 is non-zero (cut-scene): stop.
4. Take the HUD pointer from `r[3]`. If it is not a plausible pointer (below 0x10000, at or above 2^47, or not a
   multiple of 8): stop, without calling `u3x_k23_callees`.
5. If `u3x_k23_callees(base)` returns 0: stop.
6. Add 1 to the counter at ExtState + 0x4374.
7. Call `u3x_k23_update(base, HUD + 0x2408)`.
8. Read the switch word again (after the update): if bit 1 is set, add 1 to the counter at ExtState + 0x437c;
   otherwise call `u3h_k26_draw(base, HUD + 0x2408, r[14] as a pointer)` and ignore its result.

## 5. Test scenarios

Icon index: UI pointer null; first table null with `which` 0; second table null with `which` 1; entries pointer null;
count 0; id at position 0; id at the last position; id present twice (lowest index wins); id only beyond `count`
(−1); the same id in the other table only (−1).

Image answer: arbitrary values; check words 2, 4..12 and that words 0, 1, 3, 13.. are untouched.

Net game record: lookup pointer null (no call); lookup returns null; returns a pointer to null; `idx` −1; `idx` =
count; `idx` = count − 1; records pointer null; `idx` 0. In all cases with a non-null lookup pointer exactly one
lookup call with 0x3b9a067d.

Film rows:

- Cinema off in each of the five ways (switch bit clear, each kernel pointer null): result 0, counter unchanged.
- Mode ≠ 4: empty text, word 1 = 0, counter + 1, online selection not clamped.
- Clamp: online count 0 (no clamp), selection above count − 1 (clamped), equal (unchanged), negative count.
- Operation 1 local and online.
- Row guard for operations 2 and 4: active count 0; entries pointer null; index 20; index 0xffffffff; index 19
  (allowed); local and online lists with different contents so that the active one is recognisable.
- Each column: column 0 with and without a net game record (check the scratch buffer is zeroed before the game
  call, the eight arguments, the text, the cut at 0x1ff characters with long names); user (pointer identity);
  number (0, negative, large); duration 0, 59, 60, 3599, 3600, −5; size (see below); date with region 1 / language
  0, region 1 / other language, region 0 / language 0, hours 0, 9, 11, 12, 13, 23, single- and two-digit days and
  months, a time before 1970; map with no record, native result null, entries null, no matching entry (text id 0),
  matching entry, two matching entries; game type with no record, null rule, a rule; an unknown column.
- Each image slot: net game image with and without record; game type image with no record, null rule, null name,
  native 20 null, native 20 returning null, returning a texture; lock with mode bits 0x180, 0x100, 0x080, 0 and
  with the icon absent (−1) and present; an unknown slot; slot 0xa42c91c5 must not be confused with the column of
  the same id.
- Operations 5, 6, 10, local and online; operation 6 with count 0; operation 10 local with an index the film select
  refuses; other operation numbers (0, 3, 7, 8, 9, 11): only the counter and the clamp.
- Net game index: low 16 bits 0xffff (negative: no record), high bits set with a valid low half.

Megabytes: S = 0, 1, 10485 (0.00), 10486 (0.01), 1048575, 1048576, 1572864, 33554431, 33554432, 4294967295; K =
828375 with and without the text native (79 / 78), K = 828374 and 828376; separators "", ".", ",", a separator of 100
characters (cut at 0x3f characters, no digits); exhaustive comparison over all 2^20 values of K is cheap.

Button fires: pressed this frame; not held; held with mask bits only above bit 21 (P = −1.0); a mask of several
buttons (lowest bit's press time); clock pointer null, odd, below 0x10000; negative clock index; T1 just below and
at 0.2; T0 below 0.2 with T1 above; both above with equal and with different repeat counts at steps 0.1, 0.05 (T1
just above 2.0) and 0.025 (T1 just above 4.0); T1 exactly 2.0 and 4.0 (no halving); P = NaN, P = ±infinity, huge
negative P (count out of range: 0x80000000 on both sides, result 0); random ticks and press times compared bit for
bit.

HUD hook: switch bit 0 set; cut-scene word non-zero; HUD pointer null / odd / too high; callees check failing;
normal run (order: callees check, update, draw; draw arguments); switch bit 1 set (update, no draw, counter); the
update setting bit 1 of the switch word during the call (draw skipped); result 0 everywhere; counters.

## 6. Oddities of the old code, to be kept

1. Megabytes, K = 828375: the hundredths are 79 with the text native present and 78 without (4.5). This comes from
   where the shipped v7.212 helper happens to store the product as a 32-bit float (around the text call); it was
   read from the built helper, not from the source text. A reference built from the old source with another compiler
   or other options may place this rounding differently: the shipped helper is the reference.
2. Megabytes: the megabyte value itself is never rounded to 24 bits (sizes of 16 MiB and more keep all their bits).
3. Map name column: native 13 is called without testing its pointer (the other natives and the lookup are tested).
4. Column 0 answers the empty string when the film's net game is unknown; the map and game type columns and the
   net game image leave the request untouched in the same situation.
5. Column 0: if the game filled all 0x100 bytes of the scratch buffer without a NUL, the copy would run on into the
   memory behind it (up to the 0x1ff limit). Not guarded.
6. The user column answers a pointer into the film entry; all built texts share one static buffer.
7. The online selection is only clamped from above, on every request in mode 4 (also for the local list), and with
   a negative online count it becomes more negative; operation 10 stores an online pick unchecked.
8. Operation 6 answers −1 for an empty list; operations 5, 6 and 10 ignore the row guard; operation 10 (local)
   records the pick in the counter even when the film select refuses it.
9. Dates: only one leading zero is dropped per part; no space before AM / PM; the null test of the calendar result
   can never be true; the calendar is UTC.
10. Duration: a negative duration gives a minus sign on each part.
11. Button fires: a mask with several buttons uses the press time of its lowest button only, while the pressed /
    held tests accept any of its buttons; a mask without bits 0..21 uses −1.0 as press time; NaN times fall through
    the comparisons as described (every comparison false), and the clock pointer is only tested after the pressed /
    held tests.
12. HUD hook: the entry counter counts skipped calls too; the result is always 0; the plausibility test of the HUD
    pointer requires 8-byte alignment (the tab entries elsewhere only 4).

## 7. Clarifications (tester, 2026-11-14)

Facts settled by the differential test of the old code (v7.212) against the new part. Nothing here changes a
statement above; each item makes one exact or answers a question the implementer raised.

1. **Clamp of the online selection (4.4 step 4).** The test is "the online selection is greater than or equal to C"
   on signed 32-bit values, and the value stored is C − 1 in 32-bit arithmetic. For every C except −2^31 this is the
   same as "greater than C − 1". For C = −2^31 (0x80000000) the selection is replaced by 0x7fffffff whatever it was
   (C − 1 wraps; the shipped helper does this). Operation 6 answers 0x7fffffff for a count of −2^31 in the same way.
2. **Number width (4.4, number and duration columns).** `u3h_cin_num` always writes at least one digit: the value 0
   gives "0". A width of 0 and a width of 1 give the same text for every value; "no padding" may be asked either way
   (the old code asks for width 0 for the film number and for the minutes).
3. **Date column: the calendar call.** `u3h_cin_localtime` receives the address of the entry's time field itself
   (entry + 0x238), not a copy, and is called before the language and the region are read.
4. **Date column: fields outside their usual range.** The fields are used as they come, with no range check. The
   12-hour value is the truncating remainder of hour / 12 (it has the sign of the hour), replaced by 12 when it is 0
   (hour −12 gives "12"); "AM" is written for every hour below 12, negative ones included, "PM" for all others (hour
   25 gives "01" and "PM"). A negative value is written as '-' followed by its magnitude padded to the width ("-04",
   year −100 gives "-0100"); a value with more digits than the width is written in full (year 10000 gives "10000").
   Month + 1 and year + 1900 are formed without 32-bit wrap-around (month field 0x7fffffff gives "2147483648", year
   field 0x7fffffff gives "2147485547"). Each of the four parts is cut after 15 characters first; then the one
   leading '0' is dropped (day 0 gives "00", shown as "0"). With the real `u3h_cin_localtime` only the year can leave
   its usual range (recording times far from today).
5. **Order inside one request (4.4).** The answer of the request is stored last: no word of the request is written
   before the last helper or game call of the branch has returned (this includes every `u3h_cin_cat` and
   `u3h_cin_num` call). In column 0 nothing but the zero fill of the scratch buffer is written to the two text
   buffers before the game call 0x741260. In operation 10 (local) the counter at ExtState + 0x3254 is written before
   `u3x_film_select` is called. `u3h_ext`, `u3h_cin_state` and `u3h_cin_buffer` only return addresses: when and how
   often they are called is not observable (the old code calls them in another pattern than the steps of 4.4 suggest,
   for instance `u3h_cin_state` also when the mode is not 4 and both buffers on every request in mode 4).
6. **Size column buffer.** The 0x40-byte buffer of the megabytes text is a local buffer of the list function; nothing
   static holds it. The megabytes function writes the whole megabytes (digits and NUL) into the buffer before it asks
   for the separator text.
7. **Lock slot.** The icon index is looked up only when the lock answer is given (operation 4, row guard passed, slot
   0, entry protected). In every other situation the UI object pointer is not followed.
8. **Row guard.** When the guard fails, and for every operation other than 2 and 4, no byte of a film entry is read;
   the entries pointer of the list that is not shown is never followed.
9. **Net game index.** The index of a film is a signed 16-bit value whatever the size of the net game table: the
   values 0x8000..0xffff never name a record, also when the table has more than 32767 records.
10. **Megabytes (4.5).** "Text native present" means only that the pointer at `s` + 800 is non-null: the 79 for
    K = 828375 also holds when the native answers null (the separator is then the text table's default string, or
    empty). The rule of 4.5 equals the code of the shipped v7.212 helper for every K (0 .. 2^20 − 1) on both paths,
    with megabyte values 0, 16, 4095 and mixed ones, at 64-bit and at 53-bit x87 precision: there is no second
    exception. A build of the old C source with x87 code (clang 22.1.7, -O2 -mno-sse) gives the same texts as the
    shipped helper, so here the rounding point of oddity 6.1 does not depend on that build.
11. **Button fires (4.6).** The previous-frame ticks (clock + 0x70) are read only after the test "T1 < 0.2" has
    failed; reading them earlier is not observable in the game (both fields lie in the same clock data). The tests
    against 2.0 and 4.0 are strict as stated. With a clock that moves forward the strictness cannot be seen at the
    exact boundary: for T1 = 2.0 (4.0) the repeat count of T1 is exactly 18 or 36 (76 or 152) and every earlier T0
    has a smaller count with either step, so the result is 1 both ways; it shows only when the previous-frame time
    is later than the current one.
