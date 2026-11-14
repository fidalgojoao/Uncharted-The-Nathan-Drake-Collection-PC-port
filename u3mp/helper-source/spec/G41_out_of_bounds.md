# G41. Out-of-bounds grid (kill volume) and the death-animation guard

Behaviour description for a reimplementation (reimpl2 pipeline, describer's document). It says what the functions
do, not how the old ones were written. All offsets are hexadecimal when prefixed with 0x. "Image offset X" means the
address `base + X`, where `base` is the game image base. All multi-byte values are little-endian; "u32 / s32 / u16 /
u8" are unsigned / signed integers of that width, "pointer" is 8 bytes. Names follow `G12_packets_monitor.md` (net
game manager, net player records, net player id), `G35_custom_game_start.md` (map list, kill-volume level, the oob
words), `G38_custom_settings.md` (net game record, map entry) and `G11_game_sockets_time.md` (net ticks: one tick
is 1/300 s).

Float arithmetic: all of it is single precision, one rounding per operation, on 32-bit patterns (the `f_*` tools of
CONVENTIONS.h). The old code of this group already computed every float step in single precision (none of it ran
on the x87 unit), so the new results are expected to be bit-identical to the shipped helper, with no deviation.
In every formula below the left operand is the first argument of the `f_*` tool and the right one the second
(a − b = `f_sub(a, b)`, a · b = `f_mul(a, b)`, a + b = `f_add(a, b)`, a / b = `f_div(a, b)`). The order matters
only when both operands are NaN (the result then carries the first one's payload); the test compares such results
bit for bit, so keep it.

## 1. Purpose and callers

Two unrelated pieces.

**Parts A and B. The out-of-bounds kill.** A PS3 multiplayer map has a "kill-volume level": a level that holds one
item, a voxel grid whose non-zero cells are outside the playable area. A player who stays in such cells gets a
5-second timer and is then killed. The PS4 game still loads that item and keeps its address (image 0x1b3ae78), but
it has no reader for it. This group is the reader: it tests the eight grid cells around a point 1 m above the
player and starts or clears the kill timer in the player's net record. The kill itself (when the timer runs out)
belongs to G40 (`u3x_respawn_tick`).

- Part A: the grid reader. Pure functions on a grid in memory: pointer test, cell numbers of a coordinate, value of
  one cell, count of out-of-bounds corners, first out-of-bounds cell, centre-free "cell point".
- Part B: the per-frame check `u3x_vis_oob_tick`, the current map's entry `u3x_map_entry_cur`, the test point of a
  player object, the "object is in state X" test.

**Part C. The death-animation guard.** The game's death event constructor (image 0x6ba9d0) looks the death
animation up in the dying character's eight animation tables and stops the game with a trap when it is not there.
`u3x_death_anim_check` makes the same lookup first, read-only, records what it saw in the helper's diagnostics, and
tells the stub to skip the constructor when the animation is missing.

Callers (all stay as they are):

- `u3x_vis_oob_tick`: C one-line wrapper `vis_oob_tick`, exported to the C++ part as
  `u3h_vis_oob_tick(base, s, p, pl, now, test)`. Its only caller is G40's `u3x_respawn_tick`, once per frame and
  per local net player that is alive in a running match, on the game's main thread, only with a non-null player
  object, and only when the "timed test kill" switch of G40 is off. `test` is non-zero when G40's test switch is
  on and the player's current life is older than 20 s. G40 ignores the result.
- `u3x_map_entry_cur`: C one-line wrapper `map_entry_cur`, called by the C function `oasis_tick` (every frame; it
  reads the entry's map id) and by the pause-menu info source of G38 (G38 4, its step "M = `u3x_map_entry_cur`";
  null there means "no map"), and by `u3x_vis_oob_tick`.
- `u3x_vis_cell`, `u3x_vis_cheat_corners`, `u3x_vis_find_kill`, `u3x_vis_cell_point`, `u3x_vis_ok`,
  `u3x_vis_axis`, `u3x_vis_test_point`, `u3x_obj_state_is`: called only inside this group (the C part declares
  some of them, but does not call them). They keep their exported names because the old-versus-new test calls
  them one by one.
- `u3x_death_anim_check`: C one-line wrapper `death_anim_check`, called by the stub dispatcher for stub kind 56
  when `base` is not 0. Kind 56 is a value stub at the entry of game function 0x6ba9d0; it runs on whatever game
  thread builds the death event (the main thread). The dispatcher returns this function's result to the stub:
  0 = the game's constructor runs, 1 = the constructor is skipped (the stub returns to the constructor's caller
  with all registers as they were on entry).

## 2. Interface

All functions are `extern "C"`, in `.text.ext` (`U3X_FN`). No float is passed by value: floats travel as 32-bit
patterns.

### 2.1 Part A (grid reader)

| Name | Signature | Result |
|---|---|---|
| `u3x_vis_ok` | `int u3x_vis_ok(const void *p)` | 1 when `p` passes the grid pointer test (3.2), else 0 |
| `u3x_vis_axis` | `void u3x_vis_axis(uint32_t p, uint32_t sc, uint32_t o, int64_t out[2])` | the lower and upper cell number of coordinate `p` (float pattern) for scale `sc` and origin `o` (float patterns): `out[0]`, `out[1]` (4.2) |
| `u3x_vis_cell` | `int u3x_vis_cell(const uint8_t *g, const int64_t c[3])` | the state of cell `c` of grid `g`: 2 = outside the grid (or the grid cannot be read), 1 = inside and playable (or its row cannot be read), 0 = out of bounds (4.3) |
| `u3x_vis_cheat_corners` | `int u3x_vis_cheat_corners(uintptr_t base, const uint32_t pos[3])` | how many of the eight cells around `pos` (x, y, z float patterns) are out of bounds, 0 .. 8; 0 without a usable grid (4.4) |
| `u3x_vis_find_kill` | `int u3x_vis_find_kill(const uint8_t *g, int32_t c[3])` | 1 and the cell numbers of the grid's first out-of-bounds run in `c`, or 0 with `c` untouched (4.5) |
| `u3x_vis_cell_point` | `uint32_t u3x_vis_cell_point(int32_t c, uint32_t sc, uint32_t o)` | the coordinate (float pattern) of cell number `c` (4.6) |

`g` is never null and is readable when these are called from this group (`u3x_vis_cheat_corners` and
`u3x_vis_oob_tick` test it first); `u3x_vis_cell` and `u3x_vis_find_kill` do not test `g` themselves.

### 2.2 Part B (per-frame check)

| Name | Signature | Result |
|---|---|---|
| `u3x_vis_test_point` | `void u3x_vis_test_point(uint8_t *o, uint32_t out[3])` | the world position of object `o` plus 1 m up, as three float patterns x, y, z (4.7) |
| `u3x_obj_state_is` | `int u3x_obj_state_is(uint8_t *o, uint32_t h)` | 1 when the current state id of object `o` is `h`, else 0 (4.8) |
| `u3x_map_entry_cur` | `uint8_t *u3x_map_entry_cur(uintptr_t base, void *s)` | the map entry (3.4) of the current net game's map, or null (4.9) |
| `u3x_vis_oob_tick` | `int u3x_vis_oob_tick(uintptr_t base, void *s, uint8_t *p, uint8_t *pl, int32_t now, int test)` | `p`: a net player record; `pl`: its player object (not null); `now`: the game clock in net ticks; `test`: non-zero = test mode. Result 1 when the current map has a kill-volume level, else 0 (4.10) |

### 2.3 Part C

| Name | Signature | Result |
|---|---|---|
| `u3x_death_anim_check` | `int u3x_death_anim_check(uintptr_t base, void *s, const uint64_t *blk)` | `blk`: the value stub's register block (3.7), never null. 1 = skip the game's constructor, 0 = let it run (4.11) |

### 2.4 Functions of other groups and of the C part that this group calls

| Name | Signature | What it does | Used by |
|---|---|---|---|
| `u3h_ext` | `uint8_t *u3h_ext(void)` | the helper's extension state (CONVENTIONS.h) | B, C |
| `u3h_netgame` | `uint8_t *u3h_netgame(uintptr_t base, void *s)` | the current net game record (G38 3.4; 0xd0 bytes, u32 map id at +4), or null | B |
| `u3x_sorted_index` | `int32_t u3x_sorted_index(const uint8_t *entries, uint32_t count, uint32_t id)` | existing function, stays: the index of the entry whose first u32 is `id` in a table of `count` 16-byte entries sorted ascending by that word, or −1 (also −1 for a null table or count 0) | C |
| `u3x_net_player_id` | `uint32_t u3x_net_player_id(const uint8_t *np)` | existing function, stays: a net player's id (G12 3.1) from its record | C |

Native 13 (the function pointer at `s` + 872) has the form `void *(*)(uint32_t symbol_id)`.

The C wrappers are already one-liners and stay: `map_entry_cur(base, s)` passes (base, s); `vis_oob_tick(base, s,
p, pl, now, test)` passes all six; `death_anim_check(base, s, blk)` passes all three.

## 3. Game facts

### 3.1 Game data

| Image offset | Type | Meaning |
|---|---|---|
| 0x1b3ae78 | pointer | the kill-volume grid item of the loaded kill-volume level (the game's own level login stores it, its logout clears it); null or stale when no such level is loaded |
| 0x17cb060 + 0x390 + i·0x9c0, i = 0 .. 9 | record | the ten net player records (G12 3.1) |
| 0x6ba9d0 | function | the death event constructor: second argument (esi) = the death animation's symbol id, third (edx) = the dying player's net player id. Not called by this group |
| 0x99cce0 | code address | the return address of the co-op damage path's call of that constructor |

### 3.2 The grid pointer test

A pointer passes when its value is at least 0x10000, below 2^47, and even (bit 0 clear). (This is weaker than
CONVENTIONS.h's `ptr_ok`, which wants a multiple of 8: slices and rows of the grid are only 2-aligned.)

### 3.3 The grid item

The layout is the one our own level converter writes. Nothing is copied from it.

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | type: 1 = no origin, 2 = with origin. Other values: not a grid for 4.4 / 4.10 |
| 4 + 8·i, i = 0 .. 2 | s32 | lowest cell number of world axis i (x, y, z) |
| 8 + 8·i | s32 | one past the highest cell number of world axis i |
| 0x1c, 0x1d, 0x1e | u8 | a0, a1, a2: which world axis (0 .. 2) is the column axis, the row axis and the slice axis |
| 0x20 + 4·i | f32 | scale of world axis i: cells per metre |
| 0x30 + 4·i | f32 | origin of world axis i; present only for type 2. For type 1 the origin is 0.0 (pattern 0) |
| H | pointer | the row store: u32 at +0 = number of rows, from +8 one pointer per row |
| H + 8 | u32 | number of slices |
| H + 0x10 + 8·k | pointer | slice k |

H is 0x40 when the type word is exactly 2 and 0x30 for every other value.

A **slice** is: u16 at +0 = number of entries n, then n u16 entries from +2. An entry's top 2 bits are a repeat
count rep (0 .. 3), its low 14 bits a row number. Entries are consecutive: the first covers row positions
0 .. rep, the next the following rep + 1 positions, and so on.

A **row** is run-length coded along the column axis: the byte at +0 is the value of the first run; the bytes from
+1 are the run lengths, one byte each. After a run whose length byte is not 255 the value becomes its bitwise
complement (as a byte: 0 becomes 0xff, 0xff becomes 0; any other start value stays non-zero for ever); after a
length byte of 255 the value stays (a longer run continues). A cell whose value is not 0 is out of bounds.

Let D = (one past the highest) − (lowest) for the column axis a0. A row is decoded only while the column position
reached so far is below D and at most 2·D + 2 length bytes have been used.

### 3.4 Map list and map entry

As G35 3.6 / G38 3.4: the table of symbol 0x0dec97a4, obtained from native 13 (the pointer to function at
`s` + 872, called with the u32 symbol id, result = pointer to the table or null): s32 count at +0, pointer to the
entries at +8; an entry is 0x98 bytes: u32 map id at +0, u32 at +0x40 = the id of the map's kill-volume level
(0 = none, or cleared by G35's start preparation).

### 3.5 Objects

A game object (the player object `pl`, a character):

| Offset | Type | Meaning |
|---|---|---|
| 0x28 | u32 | recorded only (diagnostics of 4.11) |
| 0x30 | u32 | type id: 0xa5a63e9b = Player, 0xf2c86c22 = NetCharacter |
| 0x7c | u32 | the object's handle id |
| 0x80 | pointer | state machine; its pointer at +0 leads to a record whose pointer at +0x10 is the current state; the state's u32 at +0x18 is its id |
| 0xd0 | 3 f32 | position P of the object's parent frame (x, y, z) |
| 0xe0 | 4 f32 | rotation of the parent frame as a quaternion (x, y, z, w) |
| 0xf0 | 3 f32 | position V of the object inside that frame |
| 0x468 | 8 pointers | NetCharacter only: its eight animation tables, in the object itself |
| 0x1c20 | pointer | Player only: pointer to its array of eight animation table pointers (may be null) |

The 16-byte groups at 0xd0, 0xe0, 0xf0 are not guaranteed to be 16-aligned.

State ids that suspend the out-of-bounds timer: 0xb713a9fd, 0x50b12ba3, 0xc388fdab.

An **animation table** T (pointer, may be null): pointer at T + 0x30 (may be null) → its pointer at +0x10 (may be
null) → list L: u32 at +0 = number of entries, pointer at +8 = the entries, 16 bytes each, sorted ascending by the
u32 symbol id at +0.

A **handle** is a pair (holder pointer, u32 id). It resolves to an object when the holder pointer is not null, the
id is not 0, the pointer stored at the holder is not null, and that object's u32 at +0x7c equals the id; the
object is the pointer stored at the holder. Otherwise it resolves to nothing. Only null tests are made, no pointer
range tests.

Net player record `np` (G12 3.1), fields used here: four u32 at +0 .. +0xc (recorded only), handle of its
character = (pointer at +0x10, u32 at +0x18), s32 at +0x848 = the out-of-bounds kill time in net ticks (0 or
negative = no timer).

Damage record (pointer saved at ExtState + 0x3c20, see 3.6): u32 at +0x1c = damage type; handle of the attacker =
(pointer at +8, u32 at +0x10); its first 0x40 bytes and the 0x30 bytes from +0x40 are recorded as u32 words.

### 3.6 The state object `s` and ExtState

`s` + 872: native 13 (3.4). It is called without a null test.
`s` + 0x20c0: u32 switch word; bit 0x200 = "death guard off" (the constructor always runs).

ExtState (address from `u3h_ext()`), all u32 unless said:

| Offset | Words | Meaning |
|---|---|---|
| 0x2070 | oob[0..15] | [0] checks made, [1] low 32 bits of the grid pointer last looked at, [2] last corner count, [3] timers started, [4] checks refused (no map entry or no kill-volume level), [5..7] last test point x, y, z (float patterns), [8] checks ended by a character state, [10] kill-volume level id of the current map at the last check, [13..15] the test cell x, y, z (s32; 0x80000000 in [13] = the grid has none). [9], [11], [12] belong to G40 / G35 / another group and are not touched here |
| 0x20b0 | oob2[0..1] | [0] low 32 bits of the grid the test cell was found in, [1] test-mode checks |
| 0x3b20 | dth[0..63] | death guard diagnostics, see 4.11 |
| 0x3c20 | u64 | the damage record pointer the builder's stub saved at the co-op damage path's call of the constructor (r13 there); 0 until that path runs. Read only |

### 3.7 The stub's register block

`blk` is the value stub's array of 64-bit words: the nine saved registers in the order r11, r10, r9, r8, rdi, rsi,
rdx, rcx, rax (as G11 2.1 / G35), then the saved rbp at index 9 and, at index 10, the return address of the
interrupted game function (the address its caller continues at). Used here: the low 32 bits of `blk[5]` (the
animation's symbol id), the low 32 bits of `blk[6]` (the net player id), `blk[10]`.

### 3.8 Constants

| Value | Meaning |
|---|---|
| 0x3f800000 | 1.0: the height added to the test point, in metres (`F_ONE`) |
| 1500 | the kill delay: 5 s in net ticks |
| 128 | most map entries searched |
| 4096 | most slices searched by 4.5 |

## 4. Behaviour

### 4.1 `u3x_vis_ok(p)`

The test of 3.2. Reads nothing.

### 4.2 `u3x_vis_axis(p, sc, o, out)`

With p, s, o the floats of the three patterns:

1. a = (p − o) · s: the subtraction first, rounded, then the product, rounded.
2. lo = floor(a), hi = ceil(a): exact, toward −∞ / +∞ to an integral float; a NaN stays a NaN, an infinity stays;
   no exception is raised or needed. (The old code used the processor's single-precision round instruction in its
   "down" and "up" modes; any way that gives the same patterns will do.)
3. For r = lo and then r = hi: q = ((r / s) + o − o) · s, four operations in exactly that order (divide, add the
   origin, subtract the origin, multiply), each rounded to single precision. These steps must not be simplified
   away: for large values they change the result.
4. `out[0]` = q of lo, `out[1]` = q of hi, each converted to a signed 64-bit integer by truncation toward zero; a
   NaN or a value outside the 64-bit range gives −2^63 (0x8000000000000000). Note that CONVENTIONS.h's `f_to_int`
   is a 32-bit conversion and is not the same thing.

### 4.3 `u3x_vis_cell(g, c)`

`c[i]` is the cell number on world axis i. The answers, in this order of precedence:

1. For each world axis i: when `c[i]` is below the lowest cell number of that axis or not below its "one past the
   highest": 2. Otherwise the cell's index on that axis is `c[i]` − lowest, and the axis size is (one past the
   highest) − lowest (64-bit arithmetic on the sign-extended s32 fields).
2. When any of a0, a1, a2 is above 2: 2.
3. When the row store pointer fails 3.2, or the cell's index on the slice axis a2 is not below the slice count
   (unsigned comparison): 2.
4. When the pointer of that slice fails 3.2: 2.
5. The slice entry that covers the cell's index on the row axis a1 is the first entry, in order, whose last
   covered row position (3.3) is not below that index. No such entry among the slice's n entries: 1.
6. When that entry's row number is not below the row store's number of rows: 1. When that row's pointer fails
   3.2: 1.
7. The row is decoded (3.3) with D = the size of the column axis a0. The run that contains the cell's index on
   the column axis decides: value 0 → 1, any other value → 0. A run of length 0 contains nothing and still
   complements the value. When decoding stops (3.3's limits) before a run contains the index: 1.

The row store's row count and the slice's entry count are read only after step 4 passed. No byte is
written. No pointer other than the three named is tested.

### 4.4 `u3x_vis_cheat_corners(base, pos)`

1. G = the pointer at image 0x1b3ae78. oob[1] = the low 32 bits of G, written in every case.
2. G fails 3.2: result 0. The grid's type is neither 1 nor 2: result 0.
3. For each world axis i: (lo_i, hi_i) from 4.2 with the coordinate `pos[i]`, the scale of axis i and the origin
   of axis i (type 2) or pattern 0 (type 1).
4. Result = the number of the eight cells (one of lo_0 / hi_0, one of lo_1 / hi_1, one of lo_2 / hi_2) for which
   4.3 answers 0. When lo and hi of an axis are equal the same cell is counted once per combination (so a point
   exactly on cell numbers can give 8 from a single cell).

### 4.5 `u3x_vis_find_kill(g, c)`

Finds the first non-zero run of the grid in this scan order: slices in ascending number; inside a slice its
entries in order; inside an entry's row the runs in order.

- Result 0 (and `c` untouched) when: any axis size (one past the highest − lowest, here computed in 32 bits) is 0
  or negative; any of a0, a1, a2 is above 2; the row store pointer fails 3.2; or the scan finds nothing.
- At most the first min(slice count, 4096) slices are scanned. A slice whose pointer fails 3.2 is skipped. An
  entry whose row number is not below the row count, or whose row pointer fails 3.2, is skipped but still
  advances the row position by rep + 1.
- A row is decoded as in 3.3 (D = size of the column axis; the limit 2·D + 2 is 32-bit here, see open question
  2). The first run whose value is not 0 and whose length
  byte is not 0 ends the search (a length byte of 255 counts as a run like any other).
- The answer: index on the column axis a0 = the column position where that run starts; index on the row axis a1 =
  the entry's first row position + rep / 2 (integer division: 0, 0, 1, 1 for rep 0 .. 3); index on the slice axis
  a2 = the slice number. `c[i]` = index on world axis i + lowest cell number of axis i (32-bit arithmetic), and the
  result is 1.
- In real grids a0, a1, a2 are a permutation of 0, 1, 2. When they are not, the three indexes are assigned in the
  order a0, a1, a2 (a later one replaces an earlier one on the same world axis) and the index of a world axis that
  none of them names is unspecified (the old code used an uninitialised value): the test does not cover it.

### 4.6 `u3x_vis_cell_point(c, sc, o)`

The pattern of (float(c) / s) + o: `c` converted to single precision (rounded to nearest), divided by the scale,
rounded, the origin added, rounded. For a cell number n this is the point whose lower and upper cell number
(4.2) are both n.

### 4.7 `u3x_vis_test_point(o, out)`

With P, Q = (x, y, z, w), V of 3.5 read from `o` (no pointer test), and for each component in cyclic order
(i, j, k) = (x, y, z), (y, z, x), (z, x, y):

1. t_i = d + d with d = (Q_j · V_k) − (Q_k · V_j): two products, each rounded, their difference rounded, then
   doubled.
2. u_i = (Q_j · t_k) − (Q_k · t_j): two products, each rounded, their difference rounded.
3. r_i = P_i + ((V_i + (Q_w · t_i)) + u_i): the product rounded, added to V_i, rounded, u_i added, rounded, P_i
   added, rounded.

`out[0]` = r_x, `out[2]` = r_z, `out[1]` = r_y + 1.0 (rounded). No fused multiply-add anywhere: every product is
rounded before it is used.

### 4.8 `u3x_obj_state_is(o, h)`

Follows 3.5's chain: the pointer at `o` + 0x80, the pointer at +0 of that, the pointer at +0x10 of that. Each of
the three pointers must pass 3.2 before it is followed; a failure gives 0. Otherwise 1 when the u32 at +0x18 of
the last one equals `h`, else 0. `o` itself is not tested.

### 4.9 `u3x_map_entry_cur(base, s)`

1. N = `u3h_netgame(base, s)`. Null: result null (native 13 is not called).
2. Native 13 is called with 0x0dec97a4 (no null test of the function pointer). Null answer: result null.
3. The entries pointer of the table is read. With map = the u32 at N + 4: the result is the first entry, among
   the first min(count, 128) entries (count signed: 0 or negative = none), whose u32 at +0 equals map. A null
   entries pointer, or no such entry: null.

Exactly one call of `u3h_netgame` and at most one call of native 13, in that order.

### 4.10 `u3x_vis_oob_tick(base, s, p, pl, now, test)`

The timer is the s32 at `p` + 0x848.

1. M = `u3x_map_entry_cur(base, s)`. oob[10] = the u32 at M + 0x40, or 0 when M is null. When M is null or that
   word is 0: oob[4] + 1, result 0; nothing else happens (the timer keeps its value).
2. oob[0] + 1. The test point = `u3x_vis_test_point(pl)`.
3. Only when `test` is non-zero and the pointer G at image 0x1b3ae78 passes 3.2 and the grid's type is 1 or 2:
   - oob2[1] + 1.
   - When oob2[0] differs from the low 32 bits of G: oob2[0] = those bits and oob[13], oob[14], oob[15] = 0.
   - When oob[13], oob[14] and oob[15] are all 0: `u3x_vis_find_kill(G, …)`; found → oob[13..15] = the three cell
     numbers; not found → oob[13] = 0x80000000 (oob[14], oob[15] stay 0).
   - When oob[13] is not 0x80000000: each coordinate i of the test point is replaced by
     `u3x_vis_cell_point(oob[13 + i] as s32, scale of axis i, origin of axis i for type 2 or pattern 0)`.
4. oob[5], oob[6], oob[7] = the test point's x, y, z patterns. n = `u3x_vis_cheat_corners(base, test point)`
   (which writes oob[1]); oob[2] = n.
5. When the player object's state (4.8) is one of the three ids of 3.5: oob[8] + 1, the timer = 0, result 1.
6. Otherwise: n = 8 and the timer is 0 or negative → the timer = `now` + 1500 (32-bit, wrapping) and oob[3] + 1;
   n = 8 and the timer is positive → unchanged; n not 8 → the timer = 0. Result 1.

Steps 1 to 4 write their counters before step 5 / 6 touch the timer; nothing reads them in between, so only the
final memory matters.

### 4.11 `u3x_death_anim_check(base, s, blk)`

anim = low 32 bits of `blk[5]`; player = low 32 bits of `blk[6]`; caller = `blk[10]` − `base` (64 bits).

1. Always: dth[0] + 1; dth[4] = the low 32 bits of caller; dth[5] = player; dth[7] = anim.
2. When caller is exactly 0x99cce0 (compared as 64-bit): dth[12] + 1, and with R = the pointer at ExtState +
   0x3c20:
   - R null: dth[9] = 0xffffffff; nothing else of this step is written.
   - R not null: dth[9] = the u32 at R + 0x1c; dth[16 + i] = the u32 at R + 4·i for i = 0 .. 15; dth[52 + i] = the
     u32 at R + 0x40 + 4·i for i = 0 .. 11; then, with A = the attacker handle of R resolved (3.5): dth[10] = the
     u32 at A + 0x30, dth[11] = the u32 at A + 0x28, dth[32 + i] = the u32 at A + 0x20 + 4·i for i = 0 .. 7; all
     ten words 0 when the handle resolves to nothing.
3. player is 0: result 0.
4. The ten net player records are searched in index order for the first whose net player id (G12 3.1) equals
   player. None: result 0. For that record: dth[48 .. 51] = its four u32 at +0 .. +0xc, and C = its character
   handle resolved. Later records are not looked at. C nothing: result 0.
5. dth[3] = the type id of C. NetCharacter: the table array is the eight pointers inside C at +0x468. Player: the
   table array is where the pointer at C + 0x1c20 points; null → result 0. Any other type: result 0.
6. The eight tables i = 0 .. 7 in order, with a mask "present" that starts at 0:
   - no list L (3.5: T, T + 0x30 or its +0x10 null): dth[40 + i] = 0; next table.
   - otherwise dth[40 + i] = L's entry count; bit i of present is set; k = `u3x_sorted_index(L's entries pointer,
     that count, anim)`. k ≥ 0: dth[8] = present (bits of the tables seen so far, this one included), dth[13] + 1,
     dth[14] = i, dth[15] = k, result 0. Later tables are not looked at and their dth[40 + i] words keep their old
     values.
7. Not found in any table: dth[8] = present; dth[1] + 1; dth[2] = anim. When the switch word of `s` has bit 0x200:
   result 0. Otherwise dth[6] + 1 and result 1.

The function reads game memory only and writes only dth words. `s` is read only in step 7. Words dth[9 .. 11],
[16 .. 39], [52 .. 63] keep their last values on calls from other callers.

## 5. Split for implementers

- **Part A** (4.1 to 4.6): `u3x_vis_ok`, `u3x_vis_axis`, `u3x_vis_cell`, `u3x_vis_cheat_corners`,
  `u3x_vis_find_kill`, `u3x_vis_cell_point`. Needs `u3h_ext` (oob[1]) and image 0x1b3ae78 only.
- **Part B** (4.7 to 4.10): `u3x_vis_test_point`, `u3x_obj_state_is`, `u3x_map_entry_cur`, `u3x_vis_oob_tick`.
  Calls part A's `u3x_vis_ok`, `u3x_vis_cheat_corners`, `u3x_vis_find_kill`, `u3x_vis_cell_point` by the
  signatures of 2.1.
- **Part C** (4.11): `u3x_death_anim_check`. Independent of A and B.

## 6. Test scenarios

Everything here can be tested without the live game: the functions read memory and call only `u3h_ext`,
`u3h_netgame`, native 13 (a function pointer in a fake `s`), `u3x_sorted_index` and `u3x_net_player_id`. A fake
image (a buffer used as `base`) holds the grid pointer and the net player records; recording stand-ins replace
`u3h_netgame` and native 13. Float results are compared bit for bit. `u3x_vis_axis` and `u3x_vis_test_point` are
pure and take the random cases directly.

Only the live game can show: that the grid of a real kill-volume level is read as meant (walk out of bounds on a
map that has one: the 5 s timer starts, oob[2] = 8, and it clears on walking back), that the timer is suspended in
the three character states, and that the kind 56 stub skips the constructor without a crash when an animation is
missing (dth[6] counts). These are checks of the game facts, not of the new code against the old.

Part A:
1. `u3x_vis_ok`: 0, 0xffff, 0x10000, an odd value, 2 mod 8, 2^47 − 2, 2^47.
2. `u3x_vis_axis`: integral and fractional coordinates, negative ones, origin 0 and non-zero, scales 1, 0.5, 4 and
   awkward ones (1/3); large coordinates where adding and subtracting the origin changes the value; scale 0,
   infinities and NaN (−2^63 results); thousands of random patterns.
3. `u3x_vis_cell`: types 1 and 2 (the header moves); each axis below the lowest, at the lowest, at highest − 1, at
   one past the highest; an axis byte of 3; a bad row store pointer; a slice index equal to the slice count; a bad
   slice pointer; a slice with no entries; entries with rep 0 .. 3 and a row index on the first, a middle and the
   last covered position and beyond the last entry; a row number equal to the row count; a bad (odd) row pointer;
   rows starting with 0, 0xff, 1; runs of 0, 1, 254, 255 (the value kept after 255); a column in the first, a
   later and the last run; a row whose runs end before the column (limit of 2·D + 2 length bytes and position D);
   all six axis permutations.
4. `u3x_vis_cheat_corners`: a null, an odd and a good grid pointer (oob[1] written each time); types 0, 1, 2, 3; a
   point with all eight cells out of bounds, with some, with none; a point on exact cell numbers; a point outside
   the grid.
5. `u3x_vis_find_kill`: an axis of size 0 and of negative size; a bad axis byte; a bad row store; an empty grid
   (`c` untouched, result 0); the first hit in slice 0 and in a later slice, after skipped slices (bad pointer),
   skipped entries (row number too high, bad row pointer); a row starting non-zero with a first length of 0 (not a
   hit) followed by a hit; rep 0 .. 3 (the row index offset 0, 0, 1, 1); more than 4096 slices with the only hit
   beyond the limit; all six permutations.
6. `u3x_vis_cell_point`: random cell numbers, scales and origins; then `u3x_vis_axis` of the result gives the cell
   number twice for well-behaved scales.

Part B:
7. `u3x_vis_test_point`: identity rotation (result P + V, y + 1), 90° rotations about each axis, random
   unit and non-unit quaternions, unaligned object addresses, NaN components.
8. `u3x_obj_state_is`: each of the three links failing 3.2 (null, odd, too small), a matching and a non-matching
   id.
9. `u3x_map_entry_cur`: no net game (native 13 not called); table null; entries null; count 0, negative, 1, 128,
   129 with the match at index 0, 127 and 128 (not found); two entries with the same map id (the first wins).
10. `u3x_vis_oob_tick`: no map entry and level id 0 (oob[4], oob[10], result 0, timer kept); normal mode with n = 8
    and timer 0, negative, positive; n below 8 with a running timer; each of the three states (timer cleared,
    oob[8]) and another state; test mode with no grid, a grid of a wrong type, a first call (cell found, point
    replaced, oob2 words), a second call on the same grid (no new search), a changed grid pointer (search again),
    a grid without an out-of-bounds cell (0x80000000, point not replaced), a found cell of (0, 0, 0) (searched
    again on every call); `now` near the 32-bit limit.

Part C:
11. `u3x_death_anim_check`: caller other than 0x99cce0 (step 2 skipped, old words kept); caller 0x99cce0 with a
    null damage pointer, with a damage record whose attacker handle resolves, and with each way it does not
    (holder null, id 0, object null, id mismatch); `blk[10]` − `base` equal to 0x99cce0 only in its low 32 bits
    (not treated as that caller); player 0; no record with that id; two records with the id (the first wins); a
    character handle that does not resolve; a NetCharacter, a Player with and without a table array, another
    type; tables missing at each level (T, +0x30, +0x10); the animation in table 0, in table 7, in two tables
    (the first wins), in none with the switch bit clear (result 1, dth[6]) and set (result 0); a list with count
    0; high bits set in `blk[5]` and `blk[6]` (ignored).

## 7. Behaviour to keep that looks like a bug or dead code

1. 4.5: with axis bytes that are not a permutation, one cell number is unspecified (uninitialised in the old
   code). Real grids never have this.
2. 4.10 step 3: a test cell whose three numbers are all 0 looks like "not searched yet" and is searched again on
   every test-mode call (same answer each time); a found cell whose x number is −2^31 would be taken for "none".
3. 4.10: the result (1 / 0) is no longer used by any caller (G40 ignores it). Keep it.
4. 4.9: native 13 is called without a null test, unlike G35's start preparation.
5. 4.3 / 4.5: the two functions treat the same row differently at its end (4.3 answers "playable" when the runs
   end early; 4.5 just goes on to the next entry) and 4.5 does not check that its row position lies inside the
   row axis. Keep both as described.
6. 4.3 step 7 and 3.3: the value flips by bitwise complement, so a row whose first byte is neither 0 nor 0xff is
   out of bounds along its whole length.
7. 4.11 step 2: when the saved damage pointer is from an earlier death (the stub stores it only on the co-op
   path's call), it is still read whenever the caller is 0x99cce0; the read is a plain dereference with a null
   test only.
8. 4.11 step 6: on a hit the counts dth[40 + i] of the tables after the hit are stale values of earlier calls.
9. 4.2: the divide / add / subtract / multiply round trip after floor and ceil looks redundant; it is the
   original game's order and must stay.

## 8. Tables

The group has no literal table of game values (section C of the inventory lists none for it). What it holds are
single ids, given in section 3: the map list's symbol id, two object type ids, three state ids, one code address.
The grid itself is data of the loaded kill-volume level, read at run time through image 0x1b3ae78; the map list
and the animation tables are read from the running game as well.

## 9. What stays as it is

- The C one-line wrappers `map_entry_cur`, `vis_oob_tick`, `death_anim_check`, the export `u3h_vis_oob_tick`, and
  the stub dispatcher's line for kind 56: they are the callers.
- The builder's two stubs (not helper code): the value stub at the entry of game function 0x6ba9d0 (kind 56) and
  the two-instruction stub at the co-op damage path's call (image 0x99ccdb), which stores r13 at ExtState + 0x3c20
  and r14 at ExtState + 0x3c28 and then goes on to 0x6ba9d0. The word at + 0x3c28 is written by that stub and read
  by nobody.
- The ExtState layout (oob, oob2, dth, the saved pointer): the diagnostics reader of the launcher and the other
  groups (G35 oob[11], G40 oob[9], oob[12]) use the same words.
- The grid layout of 3.3: it is what our level converter writes into the kill-volume level; reader and converter
  must agree, so neither changes alone.
- `u3x_sorted_index` and `u3x_net_player_id`: existing shared functions, called as they are. The old code of 4.11
  used a file-private copy of the net player id rule and a file-private handle test; both are also used by other
  parts of the old file and are not this group's to remove. The new code calls `u3x_net_player_id` and makes the
  handle test of 3.5 itself.
- All eleven exported names and signatures of 2.1 to 2.3 (the C part declares them, the old-versus-new test calls
  them one by one).

## 10. Open questions

1. The names of the three character states that suspend the timer (3.5) are not known; only their ids are. Nothing
   depends on the names.
2. 4.5 with axis bytes that are not a permutation leaves one cell number unspecified, and for an axis size of
   2^30 or more the old 32-bit limit "2·D + 2" overflows. Neither occurs in a grid our converter writes (sizes are
   a few hundred cells). Proposed: the new code is free there, and the tester keeps both out of the random cases.
3. 4.10 step 3: should the "not searched yet" mark stay "all three numbers are 0" (oddity 7.2)? It is a test-mode
   path only; kept as it is unless the owner wants it changed.
4. The damage record's fields beyond the damage type and the attacker handle (3.5) are recorded without being
   understood; the description only fixes which words are copied.

## Clarifications (tester, 2026-11-14)

Facts found or confirmed by the old-against-new test (`reimpl2\G41\test_g41.cpp`: grids whose every part lies
between unreadable pages and is read-only, recording stand-ins, stand-ins that change one field during a call) and
by reading the instructions of the shipped helper (v7.212 `profile-helper.bin`). "Not observable" means: no result,
no memory written and no call changes.

1. **3.3 / 4.3 / 4.5, the row decode limit.** It is a total of 2·D + 2 length bytes (the row's bytes 1 to 2·D + 2),
   not one more, in both functions. A row whose first value is 0 followed by 2·D + 1 zero-length runs and then a
   length byte of 200: 4.3 answers 0 for every column and 4.5 finds column position 0. A row whose first value is
   0xff followed by 2·D + 2 zero-length runs and then a byte of 200: 4.3 answers 1 and 4.5 finds nothing (that byte
   is never read).
2. **4.5, the size test (the text "computed in 32 bits" is not exact).** The test of the shipped helper is the
   signed 32-bit comparison of the two fields: result 0 when "one past the highest" is not above "lowest". It is
   not the sign of the wrapped 32-bit difference. The two differ only when the true difference is outside the
   32-bit range (never in a grid our converter writes): with lowest 5 and one past the highest −2^31 the result is
   0; with lowest −2^31 and one past the highest 5 the test passes, and because only the column axis' size is used
   afterwards (as its wrapped 32-bit value, here negative: no row is decoded, result 0) a row or slice axis with
   such fields does not stop the search (the answer cell number on that axis is index + lowest in wrapping 32-bit
   arithmetic).
3. **4.5, when the row count is read.** Once, after the row store pointer passed the pointer test and before the
   first slice is looked at (the shipped helper does not read it when the slice count is 0). Not observable with a
   readable row store.
4. **4.3, when the row count is read.** The shipped helper reads it after the slice's entry count, when that count
   is not 0, and before it looks for the covering entry; reading it only once an entry covers the row position is
   not observable with a readable row store.
5. **4.10 step 3, the grid pointer.** The old code reads image 0x1b3ae78 in this step only when `test` is
   non-zero; 4.4 reads it in every case right after. Reading it always is not observable.
6. **4.10 step 5.** The state test is up to three evaluations of 4.8, in the order 0xb713a9fd, 0x50b12ba3,
   0xc388fdab, stopping at the first that answers 1; each follows the three pointers again. Nothing is called in
   between, so one evaluation of the chain compared with three ids would not be observable either.
7. **4.11 step 2, attacker handle resolving to nothing.** Exactly ten words are set to 0: dth[10], dth[11] and
   dth[32 .. 39]. dth[9], dth[16 .. 31] and dth[52 .. 63] are still copied from the damage record in that case.
8. **4.9 read order.** The map id (net game + 4), the native 13 pointer (`s` + 872), the table's count and its
   entries pointer are all read after the `u3h_netgame` call; the map id, the count and the entries pointer after
   the native 13 call (a value changed by either call is the one used).
9. **Float.** In the shipped helper 4.2 is: subtract, multiply, round toward −∞ and toward +∞ (precision exception
   suppressed), then twice divide, add, subtract, multiply and a truncating conversion to a 64-bit integer, all
   scalar single precision; 4.7 is packed single precision in the operand order section 4.7 gives (the unused
   fourth lane computes values that are thrown away); 4.6 is convert, divide, add. No x87 instruction is in any
   function of this group.
10. **Open question 2, what the old code does (host build of the old source).** Axis bytes that are not a
    permutation: the cell number of the world axis that no byte names is index (uninitialised stack value) + lowest,
    different from run to run; the named axes are as section 4.5 says (a later byte replaces an earlier one). A
    column size of 2^30 and more: the host builds of the old code still decode the row and find the same cell as
    for a small size.
