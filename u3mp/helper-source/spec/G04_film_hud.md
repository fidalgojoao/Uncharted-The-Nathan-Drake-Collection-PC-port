# G04. Film HUD panel and film actions

Behaviour description for the clean-room reimplementation (pipeline `reimpl2`, role 1). Old code: helper v7.212.
The work group itself is 293 code lines (13 still in C: `k41_film`; 280 in the C++ part: `u3x_is_film_action` and
the film HUD drawing). Part C adds the about 62 lines of the same film bar port that are in no work group (the glow
under the cursor, the time text, the colour packing, the small selection and blend helpers, the text calls).
Everything below is WHAT the code does. All image offsets are offsets from the game image base (`base`).

Three parts for two implementers:

- **Part A, "film actions and drawing primitives"** (about 85 old lines): `u3x_is_film_action`, `u3x_film_action`
  (new, takes over the body of the C function `k41_film`), `u3x_k26_tp`, `u3x_k26_tex`, `u3x_k26_quad`,
  `u3x_k26_sprite`, `u3x_k26_callees`.
- **Part C, "small helpers of the film bar"** (about 62 old lines): `u3x_k26_f2d`, `u3x_k26_fsel`, `u3x_k26_rd`,
  `u3x_k26_c4`, `u3x_k26_col`, `u3x_k26_text`, `u3x_k26_str`, `u3x_k26_time`, `u3x_k26_cursor` (new name for the
  old internal `rw::cursor`).
- **Part B, "the film bar draw"** (about 208 old lines): `u3x_k26_draw` (old internal pieces: frame, panel, tab,
  timeline, texts).

**Part A and Part C go to one implementer, Part B to the other.** Part B uses Parts A and C only through the
exported names and the behaviour written there; Parts A and C do not use Part B. The section "Common" is for both.

After the three parts are in, nothing of the old film bar block is left except `u3x_k26_call` (the assembly that
performs a call block; a shared tool). In particular these old internal functions of namespace `rw` are gone and
are NOT delivered as functions: `text_params`, `texture`, `quad`, `sprite`, `callees`, `panel`, `tab`, `progress`,
`texts`, `draw` (replaced by Parts A and B), `cursor`, `time_text`, `packed_colour` (replaced by exported functions
of Part C), `select`, `at_least_0`, `at_most_1`, `at_least_1`, `mix`, `filled`, `call_ints`, `pointer` (see G.4).

---------------------------------------------------------------------------------------------------------------------

## Common

### G.1 Numbers

Every float in this group is handled as its 32-bit IEEE single pattern in a `uint32_t` ("float bits"). Nothing is
passed or returned as `float` or `double`. In the formulas:

- `a + b`, `a - b`, `a * b`, `a / b` on float bits: ONE IEEE single-precision operation, round to nearest even, and
  the result is a 32-bit float before it is used again. There is never an unrounded intermediate between two of
  these operations (CONVENTIONS.h: `f_add`, `f_sub`, `f_mul`, `f_div`). Division by zero does not fault (infinity
  or NaN).
- `PS(a, b, c)`, the "product-sum": `a * b + c` where the product is NOT rounded to single (the product of two
  singles is exact in a double), the sum is rounded once to a 64-bit double (53-bit significand) and that double is
  then rounded to single. This is not a true fused multiply-add and not "x87 extended, then single": the middle
  rounding to double must happen. CONVENTIONS.h: `f_madd(a, b, c)` does exactly this.
- `I2F(n)`: a signed 32-bit integer converted to single, round to nearest even (`f_from_int`).
- `F2I(x)`: a single converted to a signed 32-bit integer by truncation toward zero; NaN or out of range gives
  0x80000000 (`f_to_int`).
- `x < y` on float bits: ordered less-than, false when either is NaN (`f_less`).
- `SEL(a, b, c)`: `c` when `a` is NaN or `a < 0`; else `b` (so `a` = -0 gives `b`). This is `u3x_k26_fsel(a, b, c)`
  (Part C).
- `NONNEG(x) = SEL(x, x, +0)`: x when x is not negative (-0 stays -0), else +0 (pattern 0); NaN gives +0.
- `CAP1(x) = SEL(x - 1, 1, x)`: 1.0 when x >= 1 (also +infinity), else x; NaN stays NaN.
- `FLOOR1(x) = SEL(1 - x, 1, x)`: 1.0 when x <= 1 (also -infinity), else x; NaN stays NaN.
- `MIX(a, b, t) = PS(1 - t, a, t * b)` (`1 - t` and `t * b` are each rounded to single first).
- `|x|`: the pattern with bit 31 cleared.

Constants are written as value (pattern), for example 72.0 (0x42900000).

### G.2 Calling the game: the call block

Every game function of this group is called with the game's PS4 calling convention through the shared function
`u3x_k26_call` (CONVENTIONS.h; it stays as it is and returns the game function's 64-bit result). The block is
`struct GameCall` of CONVENTIONS.h, 0x78 bytes:

| Field | Offset | Size | Meaning |
|---|---|---|---|
| `fn` | 0x00 | 8 | address of the game function (`base` + image offset) |
| `i[0..5]` | 0x08 | 6 x 8 | integer arguments 1 to 6 (rdi, rsi, rdx, rcx, r8, r9) |
| `x[0..4]` | 0x38 | 5 x 8 | the low 8 bytes of xmm0 to xmm4: two packed singles (first in the low 4 bytes), or one double |
| `s[0..1]` | 0x60 | 2 x 8 | stack arguments 1 and 2 |
| `al` | 0x70 | 8 | the count of vector registers used, for variadic functions |

In this spec "call 0x... with (...)" always means: a block that is all zero except the function address and the
slots named. Unused slots are 0 (the old code always passes zeros there; a test compares them). A 32-bit argument is
stored zero-extended in its 8-byte slot.

### G.3 Things of other groups and of the C part that this group calls

The implementer declares them from these signatures (all `extern "C"`; the `u3h_` ones are exports of the C part
with hidden visibility):

- `uint64_t u3x_k26_call(void *game_call_block);` (CONVENTIONS.h) see G.2.
- `uint8_t *u3h_ext(void);` and `uint32_t *ext_eb()` (CONVENTIONS.h): the 32-bit word array **eb** of the helper's
  state (eb[n] at byte 0x4050 + 4n of the block `u3h_ext()` returns). eb[1] is a word of switches set at build
  time; the other eb words named below are diagnostics counters (plain 32-bit adds and stores, read by maintainers
  in the running game's memory; a test compares them).
- `bool ptr_ok(const void *p)` (CONVENTIONS.h): at least 0x10000, below 2^47, a multiple of 8.
- `int u3x_k29_eok(const void *p);` (film bar tabs, another group): 1 when p can be a game object: at least
  0x10000, below 2^47, a multiple of 4; else 0.
- `uint8_t *u3x_live_object(uint8_t *const *handle, uint32_t id);` (another group): the object a game handle
  points to while it still has that id, else null (a null `handle` or id 0 gives null; otherwise the object is the
  pointer stored in `*handle`, kept only when it is not null and the 32-bit word at object + 0x7c equals `id`).
- `volatile uint32_t *u3h_i03_d(void);` (C part): eight diagnostic words of the C part.
- `uint32_t u3h_sincos(uint32_t x, uint32_t cosine);` (C part): the sine (cosine = 0) or cosine (cosine = 1) of the
  angle x in radians, float bits in and out (the helper's single-precision sine and cosine routine).

### G.4 Old internal helpers that are not delivered

- `select`, `at_least_0`, `at_most_1`, `at_least_1`, `mix`: one-line forms of SEL, NONNEG, CAP1, FLOOR1 and MIX of
  G.1. Their only callers were the film bar functions. Each part computes these from the G.1 definitions
  (`u3x_k26_fsel`, `f_sub`, `f_mul`, `f_madd`); how (inline or as a file-local helper of the part) is free.
- `filled(base, context, texture, x0, y0, x1, y1, r, g, b, a)`: stored the four colour values in a block and called
  the quad draw. Its only caller was the timeline (Part B), which now calls `u3x_k26_quad` with a colour block.
- `call_ints(function, integers...)` and `pointer(p)`: built a call block with only integer arguments (function
  address, the integers in order in `i[0]`.., everything else 0), performed it through `u3x_k26_call` and returned
  the result; `pointer` turned an address into a 64-bit integer. Their only callers were film bar functions. The
  new code fills a `GameCall` as G.2 says.
- No function of another group calls any of these (checked in v7.212: the film bar update `rw::update_bar` and
  the HUD hook use none of them; the hook reaches the draw only through `u3h_k26_draw`). Nothing outside this spec
  has to be redirected.

### G.5 The bar layout

Part B computes four values per draw and hands them to `u3x_k26_cursor` (Part C). Both parts define the same plain
structure, 16 bytes, four float bits in this order (the integrator keeps one definition):

`struct FilmBarLayout { uint32_t width, left, baseline, top; };`

- `width`: the bar's width in pixels (visible tabs x 72 - 8).
- `left`: the x of the bar's left edge (the bar is centred on the display).
- `baseline`: the y of the bar's lower reference line (550 shown, 650 hidden); icons, glow and timeline are placed
  relative to it.
- `top`: the y of the timeline strip's upper edge (baseline - 51).

---------------------------------------------------------------------------------------------------------------------

## Part A. Film actions and drawing primitives

### A.1 Purpose and callers

**Film actions.** The Cinema's film player shows a bar of tabs (play, pause, speeds, camera modes and so on). A
confirmed tab sends a menu action id. On PS4 nothing handles these ids, so the helper's menu catch-all sends them to
the film player object as game events.

- `u3x_menu_action` (C++, another group) is the catch-all for menu actions the game routes to no handler. It asks
  its handlers in a fixed order (RPCN options, friends report, Start Match, then the film action, then Community)
  and stops at the first that answers "handled". The film handler is reached through the C export
  `u3h_film_action(base, event)`, which forwards to the C function `k41_film(base, event)`. It runs once for every
  such menu action.
- `u3x_is_film_action` is called only by that handler.

**Drawing primitives.** Small building blocks for 2D drawing with the game's own functions. In v7.212 they are
used by the film bar draw (Part B) and by the glow under the cursor (Part C); the C part keeps one-line wrappers
`k26_tp`, `k26_tex`, `k26_quad`, `k26_sprite`, `k26_callees` that forward to them unchanged (no other C caller
exists in v7.212).

Callers the inventory's list for this group does not name: in the old code the exported one-line functions
`u3x_k26_quad` and `u3x_k26_draw` and the internal `rw::filled` and `rw::cursor` also called the group's internal
functions. In the new code the exported functions carry the behaviour themselves, `rw::filled` is gone (G.4) and
`rw::cursor` is `u3x_k26_cursor` (Part C), so no such caller is left over.

### A.2 Interface

All are `extern "C" U3X_FN`. Names and signatures of the six existing ones must stay exactly (parameter names are
free).

1. `int u3x_is_film_action(uint32_t action);`
   1 when the action id is one of the 14 film ids (A.3), else 0. Pure. Called by `u3x_film_action`.

2. NEW: `int u3x_film_action(uintptr_t base, const int32_t *event, volatile uint32_t *counters, uint64_t *boxes);`
   Returns 1 when the action was a film action and was sent as an event, else 0. `event` points to the menu
   action record; only its first 32-bit word (the action id) is read. `counters` points to 4 words, `boxes` to 20
   quadwords (5 argument boxes of 4 quadwords = 32 bytes each, 16-byte aligned, static storage).
   The C function `k41_film(base, event)` keeps its name and becomes one statement: it passes `base`, `event`, its
   own static counter array `k41_d` (4 words) and the address of its own static box array `k41_box` (5 x 4
   quadwords). Both arrays stay in the C part (their place in the helper's data segment is read by maintainers).

3. `void u3x_k26_tp(uint32_t *params);`
   Fills a text parameter block (48 words) with the defaults. Called by Part B and the C wrapper `k26_tp`.

4. `uint64_t u3x_k26_tex(uintptr_t base, uint32_t id);`
   The game's texture handle for UI image `id`. Called by Part B and the C wrapper `k26_tex`.

5. `void u3x_k26_quad(uintptr_t base, uint64_t context, uint64_t texture, uint32_t x0, uint32_t y0, uint32_t x1,
   uint32_t y1, const uint32_t colour[4]);`
   Draws one axis-aligned rectangle, textured or plain. Coordinates and colour (r, g, b, a) are float bits.
   Called by Part B and the C wrapper `k26_quad`.

6. `void u3x_k26_sprite(uintptr_t base, uint64_t image, uint32_t index, const uint32_t colour[4], uint32_t x,
   uint32_t y, uint32_t w, uint32_t h, uint32_t centred, uint32_t hud_flag);`
   Draws one HUD sprite. x, y, w, h and the colour are float bits. Called by Part B, by `u3x_k26_cursor` (Part C)
   and by the C wrapper `k26_sprite`.

7. `int u3x_k26_callees(uintptr_t base);`
   1 when the game functions the bar draw uses look as expected, else 0; decided once. Called by Part B and the C
   wrapper `k26_callees`.

### A.3 Game facts

**Film action ids (14).** 0x38231b34, 0x3cf5deed, 0x43348860, 0x516bfbb8, 0x682c6868, 0x806c9824, 0x887c7d6e,
0x953b8f2a, 0x9eb9632d, 0xa65c61ba, 0xbd8d7974, 0xc1f65aeb, 0xe8184ce8, 0xff502160. These are the event ids the
film player's event handler accepts (PS4 image 0x667d50); the game holds them only as compare values inside its
code, there is no data table to read at run time. They are interface ids, used here as a membership test only.

**Film player handle.** Image 0x1b3ee38: an 8-byte pointer to a handle slot (may be null). Image 0x1b3ee40: the
32-bit id the object had when the handle was taken. `u3x_live_object(pointer at 0x1b3ee38, id at 0x1b3ee40)` gives
the film player object or null.

**Send event, image 0x1197380.** Arguments: 1 = address of a result box, 2 = 0, 3 = the event id (zero-extended 32
bits), 4 = the target object (may be null), 5 and 6 = addresses of event argument boxes 1 and 2, stack 1 and stack
2 = addresses of argument boxes 3 and 4. Each box is 32 bytes, all zero ("empty argument"). Result unused.

**Switch.** eb[1] bit 7 (0x80) set: film events are not sent (U3MP_FILM_EVENTS=0).

**Text parameter block** of the game's text draw 0xf1d9f0: 0xc0 bytes (48 words), 16-byte aligned. Fields used in
this group: +0x00 x, +0x04 y (float); +0x08..+0x1c clip rectangle (4 floats); +0x20, +0x24 scale x, y (float);
+0x28 colour (packed word as `u3x_k26_col` gives it); +0x2c a float that is 1.0; +0x30 font index (0xffffffff = the
language's font); +0x34 flags (int); +0x50.. a 4 x 4 float matrix in rows of 5 words (its diagonal is at +0x50,
+0x64, +0x78, +0x8c); +0x90 a float that is 1.0; +0x94 width (float; in: the available width, out after a measuring
call: the measured width).

**UI image table.** Image 0x1b24b80 + 0x48 holds an 8-byte pointer to the UI image directory. Directory + 0x28: an
8-byte pointer to the class-1 image table. Table + 2: entry count (16 bits, unsigned); table + 8: 8-byte pointer to
the entries; each entry is 0x28 bytes with its 32-bit image id at entry + 8.

**Texture of an image, image 0x11b89b0.** Arguments: 1 = the 64-bit value (1 << 32) + id (class 1 in the high half),
2 = the entry index as an unsigned 32-bit value (0xffffffff = not found). Result: the 64-bit texture handle.

**Quad draw, image 0xe42310.** Arguments: 1 = address of a context block of 6 quadwords, the first is the draw
context (0 = let the game queue the quad for its normal UI pass), the rest 0; 2 = address of a texture block of 4
quadwords, the first is the texture handle (0 = untextured), the rest 0; 3 = address of the vertex block. Vertex
block: 16-byte aligned, 0x110 bytes: four vertices of 0x40 bytes, then 16 zero bytes (an integer 0 follows the
vertices). A vertex, as 16 words: [0] x, [1] y, [2] 0, [3] u, [4] v, [5..7] 0, [8..11] colour r, g, b, a (float
bits), [12..15] four times 1.0 (0x3f800000).

**Sprite draw, image 0x11b8dd0.** Integer arguments: 1 = 0, 2 = image (64 bits), 3 = index (32 bits), 4 = address
of a 16-byte aligned block of the four colour float bits, 5 = the "centred" flag, 6 = a second flag (the old notes
call it "HUD alpha"; the tab icons pass 0, the glow of Part C passes 1). Float
registers (two singles each, low first): xmm0 = (x, y), xmm1 = (w, h), xmm2 = (0.0, 1.0), xmm3 = (0.0, 0.0),
xmm4 = (1.0, 1.0).

**Expected function starts** (first 4 bytes, little-endian word, of each function): 0xe5894855 at image
0x11b8dd0, 0x11b89b0, 0xe42310, 0xf1d9f0, 0xf32ae0, 0x640ac0, 0x63d6f0; 0x009cbf80 at image 0x6a2700; 0x6aca25ff at
image 0x139cab0.

### A.4 Behaviour

**A.4.1 `u3x_is_film_action(action)`.** 1 exactly for the 14 ids of A.3, 0 for every other value. No memory
access, no call.

**A.4.2 `u3x_film_action(base, event, counters, boxes)`**, in this order:

1. counters[0] is increased by 1 (every call).
2. If eb[1] has bit 0x80: return 0. Nothing else happens.
3. The id is the first word of `event`, as unsigned. If `u3x_is_film_action(id)` is 0: return 0.
4. The target is `u3x_live_object(the pointer stored at image 0x1b3ee38, the 32-bit word at image 0x1b3ee40)`.
5. All 20 quadwords of `boxes` are set to 0.
6. Call 0x1197380 with integer arguments 1 = address of box 4 (`boxes + 16`), 2 = 0, 3 = id, 4 = target,
   5 = address of box 0, 6 = address of box 1; stack 1 = address of box 2, stack 2 = address of box 3. Float
   slots and the vector count are 0.
7. counters[1] is increased by 1; counters[2] = id; if the target was null, counters[3] is increased by 1.
8. Return 1.

A null target does not stop the call: the event is sent with target 0 and the action still counts as handled.

**A.4.3 `u3x_k26_tp(params)`.** Writes all 48 words: 0 everywhere, except: -1.0 (0xbf800000) at +0x14 and +0x1c
(no clipping); 1.0 (0x3f800000) at +0x2c, +0x50, +0x64, +0x78, +0x8c and +0x90; 0xffffffff at +0x30. No call.

**A.4.4 `u3x_k26_tex(base, id)`.** Finds the index of the first class-1 image table entry whose id equals `id`
(entries 0 to count - 1 in order), then makes one call 0x11b89b0 with (1 << 32) + id and the index, and returns its
64-bit result. The index is "not found" (0xffffffff) when any of these holds: the directory pointer, the table
pointer or the entries pointer fails `ptr_ok`; the count is 0; no entry matches. Later pointers are not read once
an earlier one fails. The game call is made in every case.

**A.4.5 `u3x_k26_quad(base, context, texture, x0, y0, x1, y1, colour)`.** Builds the three blocks of A.3 on its own
stack and makes one call 0xe42310 with their addresses. Context block: {context, 0, 0, 0, 0, 0}. Texture block:
{texture, 0, 0, 0}. Vertex block: all 0x110 bytes zero, then per vertex position, texture coordinate, the colour
copied from `colour[0..3]`, and four 1.0. The four vertices in order:

| Vertex | x | y | u | v |
|---|---|---|---|---|
| 0 | x0 | y0 | 0.0 | 0.0 |
| 1 | x1 | y0 | 1.0 | 0.0 |
| 2 | x1 | y1 | 1.0 | 1.0 |
| 3 | x0 | y1 | 0.0 | 1.0 |

The coordinate and colour bits are copied as they are (no arithmetic). Nothing is returned; `colour` is only read.

**A.4.6 `u3x_k26_sprite(base, image, index, colour, x, y, w, h, centred, hud_flag)`.** Copies the four colour words
to a 16-byte aligned block of its own and makes one call 0x11b8dd0 with the arguments of A.3: integers (0, image,
index, address of the copy, centred, hud_flag); xmm0 slot = x in the low 4 bytes and y in the high 4 bytes; xmm1
slot = w low, h high; xmm2 slot = 0 low, 0x3f800000 high; xmm3 slot = 0; xmm4 slot = 0x3f800000 low and high. Stack
slots and vector count 0. Values are copied as they are.

**A.4.7 `u3x_k26_callees(base)`.** One byte of the C++ part's own zero-initialised data keeps the answer: not
decided, good, bad. On the first call the nine words of A.3 "Expected function starts" are compared; the answer is
"good" only when all nine match. Every later call returns the stored answer without reading the image again (the
answer never changes for the life of the process, also when it is "bad"). Returns 1 for good, 0 for bad. No game
call, no counter.

### A.5 Test scenarios

1. `u3x_is_film_action`: each of the 14 ids gives 1; 0, 0xffffffff, each id plus and minus 1, and a few thousand
   random values give 0.
2. Film action, switch bit 0x80 set, event id a film id: returns 0, counters[0] + 1, no call, boxes untouched.
3. Film action, id not a film id (for example 0x9db19163): returns 0, counters[0] + 1 only, boxes untouched, the
   handle is not read.
4. Film id, live player (handle pointer valid, object id at +0x7c equal to the word at 0x1b3ee40, not 0): one call
   0x1197380 with target = the object, the four box addresses and the result box as in A.4.2, boxes all zero at
   the time of the call (fill them with garbage first); counters[1] + 1, counters[2] = id, counters[3] unchanged;
   returns 1.
5. Film id, no player: handle pointer null; or id word 0; or the slot holds null; or the object's id differs. In
   each case the call is made with target 0, counters[3] + 1, returns 1.
6. `u3x_k26_tp` on a block filled with 0xa5: the 48 words as A.4.3; the bytes after the block untouched.
7. `u3x_k26_tex`: id found at entry 0, in the middle, at the last entry; the same id twice (the first index wins);
   not found; count 0; directory pointer null / odd / below 0x10000; table pointer bad; entries pointer bad. Each
   time one call with (1 << 32) + id and the index (0xffffffff when not found), and the stub's 64-bit result comes
   back unchanged (use a result with high bits set).
8. `u3x_k26_quad`: random bits for the coordinates and the colour, context 0 and not 0, texture 0 and not 0: the
   three blocks at the time of the call as in A.4.5, vertex block address a multiple of 16, the 16 bytes after the
   four vertices zero.
9. `u3x_k26_sprite`: random bits; the call block as in A.4.6; the colour pointer passed is a 16-byte aligned copy
   with the same four words (pass an unaligned source).
10. `u3x_k26_callees`: all nine words right gives 1, and still 1 after the image is changed afterwards; with a
    fresh state, each single wrong word in turn gives 0, and still 0 after the image is repaired.

---------------------------------------------------------------------------------------------------------------------

## Part C. Small helpers of the film bar

(Same implementer as Part A.)

### C.1 Purpose and callers

The film bar draw is built from a few small helpers: a float selection, a float-to-double conversion, a colour
packer, the game's text draw and text lookup, a time formatter, and the glow that pulses under the selected tab.
In v7.212 they are internal functions of the film bar block (`rw::cursor`, `rw::time_text`, `rw::packed_colour`)
or short exported functions (`u3x_k26_f2d`, `u3x_k26_fsel`, `u3x_k26_rd`, `u3x_k26_c4`, `u3x_k26_col`,
`u3x_k26_text`, `u3x_k26_str`, `u3x_k26_time`). No work group of the inventory lists them.

Callers: the film bar draw (Part B) and each other; the C part keeps one-line wrappers `k26_f2d`, `k26_fsel`,
`k26_rd`, `k26_c4`, `k26_col`, `k26_text`, `k26_str`, `k26_time` that forward unchanged (no other C caller exists in
v7.212). The glow has one caller: Part B, once per draw, between the panel and the tabs.

### C.2 Interface

All are `extern "C" U3X_FN`. Names and signatures of the eight existing ones must stay exactly (parameter names are
free); their C wrappers stay as they are.

1. `uint64_t u3x_k26_f2d(uint32_t x);`
   The 64-bit double pattern of the single x.
2. `uint32_t u3x_k26_fsel(uint32_t a, uint32_t b, uint32_t c);`
   SEL of G.1: b or c, chosen by a.
3. `uint32_t u3x_k26_rd(const void *p);`
   The 32 bits stored at p.
4. `void u3x_k26_c4(uint32_t *out, uint32_t r, uint32_t g, uint32_t b, uint32_t a);`
   Stores the four values in out[0..3].
5. `uint32_t u3x_k26_col(const uint32_t colour[4]);`
   Packs a colour of four float bits (r, g, b, a) into the game's 32-bit text colour.
6. `void u3x_k26_text(uintptr_t base, uint32_t *params, const char *string);`
   Draws or measures a text with the game's text draw; `params` is a text parameter block (A.3).
7. `const char *u3x_k26_str(uintptr_t base, uint32_t id);`
   The localized text of a text id; never null as long as the game's fallback text is set.
8. `void u3x_k26_time(uintptr_t base, char *buffer, int32_t seconds);`
   Writes a time as text into `buffer` (64 bytes) with the game's snprintf.
9. NEW: `void u3x_k26_cursor(uintptr_t base, const uint8_t *element, const FilmBarLayout *layout, uint32_t opacity);`
   Draws the glow under the selected tab. `element` is the film bar element (B.3), `layout` the bar layout (G.5;
   only `left` and `baseline` are used), `opacity` float bits. It takes the place of the old internal `rw::cursor`,
   which received the layout by reference and drew through the internal `rw::sprite`: the one caller (the bar draw,
   Part B) calls `u3x_k26_cursor`, and the glow draws through `u3x_k26_sprite` (A.2). No C wrapper is needed.

### C.3 Game facts

- **Text draw, image 0xf1d9f0.** Arguments: 1 = address of the text parameter block (A.3), 2 = address of the
  zero-terminated string. Result unused.
- **Text lookup, image 0xf32ae0.** Arguments: 1 = base + 0x17026b8 (the game's text table object), 2 = the text id
  (32 bits). Result: the address of the text, or null. The 8-byte pointer stored at image 0x17026b8 + 0x10 is the
  game's fallback text.
- **snprintf, image 0x139cab0.** Arguments: 1 = buffer, 2 = buffer size, 3 = format, 4.. = the values; with only
  integer values the vector count is 0.
- **Time formats of the game**: image 0x1496f61 takes three integers (hours, minutes, seconds), image 0x1496f6e two
  (minutes, seconds). Their addresses are passed; the strings are not copied.
- **Element fields used by the glow**: +0x004 the smoothed cursor position in tabs (float); +0x1dc the glow's
  image (64 bits; the address is not a multiple of 8, the read is unaligned); +0x1e4 the glow's index (32 bits).
- **Game clock**: image 0x17b4a90 + 0x114, signed 32 bits, in ticks of 1/300 s.
- **Constants**: 255.0 (0x437f0000); 72.0 (0x42900000); 3.0 (0x40400000); 4/300 (0x3c5a740e, 0.0133333...); 35.0
  (0x420c0000); 0.5 (0x3f000000); 0.4 (0x3ecccccd); 0.7 (0x3f333333); 0.8 (0x3f4ccccd); 70.0 (0x428c0000); 1.0
  (0x3f800000).

### C.4 Behaviour

Notation of G.1; game calls as G.2 (every slot not named is 0).

**C.4.1 `u3x_k26_f2d(x)`.** The IEEE double with the same value as the single x (the conversion is exact), as the
CPU's own single-to-double conversion gives it: zeros and infinities keep their sign; a NaN stays a NaN with its
sign and its payload moved to the top of the double's fraction, and comes out quiet. No memory access, no call.

**C.4.2 `u3x_k26_fsel(a, b, c)`.** Returns the pattern c when a is a NaN (its magnitude bits are above 0x7f800000)
or when a < 0 (ordered; -0 is not below 0); otherwise the pattern b. b and c are returned unchanged, whatever
they are. No memory access, no call.

**C.4.3 `u3x_k26_rd(p)`.** Returns the 32-bit word at address p. p is not checked.

**C.4.4 `u3x_k26_c4(out, r, g, b, a)`.** out[0] = r, out[1] = g, out[2] = b, out[3] = a. Nothing else.

**C.4.5 `u3x_k26_col(colour)`.** For each of the four values x: v = CAP1(NONNEG(x)); n = F2I(v * 255.0); the
channel's byte is the low 8 bits of n. Result = (byte of colour[3]) << 24 | (byte of colour[2]) << 16 | (byte of
colour[1]) << 8 | (byte of colour[0]). So values of 1 or more (and +infinity) give 255; negative values, -0 and
NaN give 0; in between, the product is rounded to single and then truncated. `colour` is only read. No call.

**C.4.6 `u3x_k26_text(base, params, string)`.** One call 0xf1d9f0 with integers 1 = `params`, 2 = `string`. What
the game writes into the block (for example the measured width) is left there.

**C.4.7 `u3x_k26_str(base, id)`.** One call 0xf32ae0 with integers 1 = base + 0x17026b8, 2 = id. A non-null result
is returned as it is. For a null result the pointer stored at image 0x17026b8 + 0x10 is returned (that pointer is
read only in this case).

**C.4.8 `u3x_k26_time(base, buffer, seconds)`.** With integer division that truncates toward zero and a remainder
that has the sign of the dividend: H = (seconds / 3600) mod 24; M = (seconds / 60) mod 60; S = seconds mod 60.

- H not 0: one call 0x139cab0 with integers 1 = `buffer`, 2 = 64, 3 = base + 0x1496f61, 4 = H, 5 = M, 6 = S.
- H equal 0: one call 0x139cab0 with integers 1 = `buffer`, 2 = 64, 3 = base + 0x1496f6e, 4 = M, 5 = S.

H, M and S go into their slots as 32-bit patterns, zero-extended (a negative number fills only the low 32 bits).
Vector count 0. The function itself writes nothing to `buffer`.

Examples: 0 gives the short form with (0, 0); 3599 the short form with (59, 59); 3600 the long form with (1, 0, 0);
86399 the long form with (23, 59, 59); 86400 the short form with (0, 0) (the hour wraps at 24); 90061 the long form
with (1, 1, 1); -1 the short form with (0, 0xffffffff); -3600 the long form with (0xffffffff, 0, 0).

**C.4.9 `u3x_k26_cursor(base, element, layout, opacity)`**, with C = the float bits at element + 4:

1. gx = PS(C, 72.0, layout->left) - 3.0.
2. angle = I2F(the game clock ticks) * (4/300).
3. wave = `u3h_sincos(angle, 1)` + 1.0 (the cosine, so 0 to 2).
4. gy = layout->baseline - 35.0.
5. pulse = CAP1(NONNEG(wave * 0.5)). (The old code applies this clamp twice; the second time changes nothing.)
6. Colour (1.0, 1.0, 0.8, opacity * MIX(0.4, 0.7, pulse)): the glow's opacity swings between 0.4 and 0.7 of the
   bar's with the clock.
7. `u3x_k26_sprite(base, the 8 bytes at element + 0x1dc, the 32 bits at element + 0x1e4, colour, gx, gy, 70.0,
   70.0, 0, 1)`.

Order of the calls: `u3h_sincos` once, then the sprite's game call (0x11b8dd0). `layout->width` and `layout->top`
are not used. Nothing is written except by the game.

### C.5 Test scenarios

1. `u3x_k26_f2d`: 0, -0, 1.0, 0.1 (0x3dcccccd gives 0x3fb99999a0000000), the smallest subnormal, the largest
   single, both infinities, a quiet and a signalling NaN with payload; thousands of random patterns against the
   old function.
2. `u3x_k26_fsel`: a = +0 and -0 (b), a positive, +infinity (b), a negative, -infinity (c), quiet and signalling
   NaN of both signs (c), the smallest negative subnormal (c); b and c random patterns, NaN included, returned
   bit for bit.
3. `u3x_k26_rd`, `u3x_k26_c4`: random values; for c4 the words before and after the four stay untouched.
4. `u3x_k26_col`: (0, 0, 0, 0) gives 0; (1, 1, 1, 1) gives 0xffffffff; (1, 0, 0, 0) gives 0x000000ff; (0, 0, 0, 1)
   gives 0xff000000; values 0.5, 0.8, 0.9, just below 1, 2.0, -0.5, -0, +infinity, -infinity, NaN in each
   channel; thousands of random patterns against the old function.
5. `u3x_k26_text`: one call with the two addresses, every other slot 0; the stub's changes to the block stay.
6. `u3x_k26_str`: the stub returns a pointer (returned unchanged, the fallback is not read); the stub returns null
   (the fallback pointer of the fake image is returned).
7. `u3x_k26_time`: the examples of C.4.8, plus 59, 60, 61, 7325, 0x7fffffff, 0x80000000 as a signed value, and
   random values: format address and the two or three numbers as 32-bit patterns in their slots, size 64, vector
   count 0.
8. `u3x_k26_cursor`: cursor 0, 2.0, 13.5, negative, NaN; clock ticks 0 (cosine 1, pulse 1, glow factor 0.7), random
   ticks, negative ticks, with a recording `u3h_sincos` stub that returns chosen values: -1.0 (pulse 0, factor
   0.4), 0, 1.0, 3.0 (clamped to pulse 1), -3.0 (wave negative, pulse 0), NaN (pulse 0). Opacity 0, 0.5, 1.0.
   Compare the `u3h_sincos` argument bits, then the sprite call block and the colour block bit for bit.

---------------------------------------------------------------------------------------------------------------------

## Part B. The film bar draw

### B.1 Purpose and callers

While a film plays in the Cinema, the HUD shows a bar: a dark panel, a glow under the selected tab, the tab icons
with the label of the tab near the cursor, a timeline (track, played part, play head, markers of the current clip),
the current and total time, the film name, and the play speed while the film does not run at normal speed. The bar
slides down and fades while a menu page is open.

`u3x_k26_draw` is called once per HUD draw, after the bar's update, by the HUD hook of the film bar
(`u3x_k23_hud`, another group) through the C export `u3h_k26_draw` and the one-line C wrapper `k26_draw`, both
forwarding the three arguments unchanged. The hook does not call it when eb[1] bit 1 (0x2) is set or when its own
checks fail. It runs inside the game's HUD draw function.

### B.2 Interface

`extern "C" U3X_FN int u3x_k26_draw(uintptr_t base, uint8_t *element, uint8_t *frame);` (name and signature stay)

- `element`: the film bar element of the HUD (the caller passes HUD + 0x2408); not checked here.
- `frame`: the frame pointer (rbp) of the game's HUD draw function at the hook; the HUD draw keeps its draw context
  in the quadword at frame - 0x11f8.
- Result: always 0.

How the function is cut into internal pieces is free. The C wrapper `k26_draw(b, el, rbp)` stays as it is.

Part B calls these functions and nothing else outside itself; it declares them from the signatures given there:
`u3x_k26_callees`, `u3x_k26_tp`, `u3x_k26_tex`, `u3x_k26_quad`, `u3x_k26_sprite` (A.2); `u3x_k26_fsel`,
`u3x_k26_f2d`, `u3x_k26_col`, `u3x_k26_text`, `u3x_k26_str`, `u3x_k26_time`, `u3x_k26_cursor` (C.2; `u3x_k26_rd`
and `u3x_k26_c4` may be used too); `u3x_k26_call`, `ext_eb`, `ptr_ok`, `u3x_k29_eok`, `u3h_i03_d` (G.3); the
structure `FilmBarLayout` (G.5).

### B.3 Game facts

**Element** (float = float bits): +0x004 the smoothed cursor position in tabs (float); +0x160 array of 8-byte
pointers to the visible tab entries; +0x1d0 number of visible tabs (signed 32 bits); +0x1d4 opacity of the bar
(float, 0 hidden to 1 shown). (The glow, `u3x_k26_cursor`, also reads +0x1dc and +0x1e4: C.3.)

**Tab entry** (the pointers at element + 0x160): +0x08 the icon's image (64 bits), +0x10 the icon's index (32 bits),
+0x14 the text id of the label (32 bits).

**Globals** (image offsets): 0x1acab60 + 0x13: byte, not 0 while a screenshot is taken. 0x18c6b34 + 8: display
width in pixels (signed 32 bits). 0x16e716c: the film's speed factor (float; the game resets it to 1.0).
0x17c3571: a byte one of the film player's actions sets to 1 (the game clears it when it resets the speed factor).
0x17b4a90 + 0x8c: the current time scale (float). 0x17bd090: the game's menu object (image 0x17bd080) + 0x10; its
address is the panel draw's first argument, nothing in it is read here.

**Film state**: the block at image 0x17c3ba8. +0x158 film length in seconds (signed 32 bits); +0x1dbc the tick at
which the film starts (signed 32 bits; a tick is 1/300 s); +0x1e6c the film's name, a zero-terminated string;
+0x1e8c index of the current clip (signed 32 bits, -1 = none); the clips: records of 0x2a8 bytes starting at
+0x3c8 (this code uses indices 0 to 9 only); in a clip record: +0 marker count (unsigned byte), +4 the markers, 8 bytes each: +0 kind (signed 16 bits:
-1 a cut, -2 a second special kind, anything else ordinary), +4 tick (signed 32 bits).

**Game functions** (all through the call block, G.2):

- 0x6a2700 (film state address): the film's current second; the low 32 bits of the result are a signed integer.
- 0x640ac0, "make a panel": integers 1 = address of a result block (16-byte aligned, 0x200 bytes, zero before the
  call), 2 = address of a panel description, 3 = 0, 4 = address of two floats (width, height), 5 = address of four
  zero words, 6 = 0xffffffff; xmm0 slot = 1.0 in the low 4 bytes (0x3f800000, high 4 bytes 0).
  Panel description, 12 words: [0] = 24 (the game's default panel type), [7] = 0xffffffff, [9] = 0xffffffff,
  [10] = 1.0 (0x3f800000), all others 0.
- 0x63d6f0, "draw a panel": integers 1 = base + 0x17bd090, 2 = the draw context, 3 = address of a style block,
  4 = address of the result block the make call filled, 5 = address of the rectangle (4 floats x, y, width,
  height), 6 = 1; stack 1 = 1, stack 2 = 0xffffffff.
  Style block, 10 words: [0] = 1.0, [4] = the panel's opacity `Ap`, [7] = 1.0, all others 0 (a black panel).
  `Ap` = I2F(128) * (1/255 as single, 0x3b808081), which is exactly the pattern 0x3f008081 (0.50196...).
- 0x139cab0: the game's snprintf(buffer, size, format, ...); a double argument goes in the xmm0 slot with vector
  count 1.
- Text draw, text lookup, texture, quad and sprite: through `u3x_k26_text`, `u3x_k26_str`, `u3x_k26_tex`,
  `u3x_k26_quad`, `u3x_k26_sprite` (Parts A and C). Text parameter block: A.3.
- Text flags (+0x34) used: 0 (default), 5 (measure only: the game writes the text's width to +0x94), 1, 2 (right
  aligned in the width), 9.

**Texts and ids.** Image id of the play head and the markers: 0xeae7862a. Speed text format, the helper's own
string: `x%2.1f` (6 characters and the terminating zero).

**Switches** in eb[1]: bit 18 (0x40000) set = draw with the HUD frame's own context (U3MP_CIN_DC0=0); clear
(the default) = "deferred mode": context 0 and a plain quad as the panel. Bit 5 (0x20) set = always the plain quad
as the panel.

**Counters**: eb[220] draws; eb[221] refused draws and bad tab entries; eb[222] last visible count; eb[223] last
current second; eb[224] last film length; eb[225] labels drawn; eb[226] markers drawn; eb[227] marker guard;
eb[228] speed texts; eb[229] screenshot skips; eb[243] panels made; eb[244..246] three words of the made panel;
eb[46] plain panels; eb[47], eb[63] the panel's corner x, y (float bits). Word 0 of `u3h_i03_d()`: frames drawn in
deferred mode.

**Numeric constants**: 72.0 (0x42900000) the step from tab to tab; -8.0 (0xc1000000); 550.0 (0x44098000) and
650.0 (0x44228000) the bar's baseline shown and hidden; 51.0 (0x424c0000); 0.5 (0x3f000000); 10.0 (0x41200000);
20.0 (0x41a00000); 21.0 (0x41a80000); 16.0 (0x41800000); 64.0 (0x42800000); 32.0 (0x42000000); 0.3 (0x3e99999a);
0.8 (0x3f4ccccd); 0.001 (0x3a83126f); 42.0 (0x42280000); 468.0 (0x43ea0000); 0.7 (0x3f333333); 1000.0
(0x447a0000); -0.5 (0xbf000000); 0.2 (0x3e4ccccd); 160.0 (0x43200000); 4.0 (0x40800000); 80.0 (0x42a00000); 12.0
(0x41400000); 6.0 (0x40c00000); 24.0 (0x41c00000); 1/300 (0x3b5a740e); 2.5 (0x40200000); 5.0 (0x40a00000); 0.9
(0x3f666666); 84.0 (0x42a80000); 400.0 (0x43c80000); 150.0 (0x43160000); 100.0 (0x42c80000); 38.0 (0x42180000);
1.0 (0x3f800000).

### B.4 Behaviour

Notation of G.1. "Colour (r, g, b, a)" is four float bits. The order of the numbered steps and of the game calls
inside them is observable and must be kept.

**B.4.1 Guards.**

1. eb[220] + 1.
2. Screenshot byte (image 0x1acab60 + 0x13) not 0: eb[229] + 1, return 0.
3. `u3x_k26_callees(base)` is asked first (always); if it says 0, or if `frame` fails `ptr_ok`: eb[221] + 1,
   return 0.
4. The draw context `ctx` is the quadword at frame - 0x11f8 (read in every case). If eb[1] bit 0x40000 is clear:
   deferred mode: `ctx` becomes 0 and word 0 of `u3h_i03_d()` + 1.
5. N = the visible count (element + 0x1d0); eb[222] = N. If N < 0 or N > 14: eb[221] + 1, return 0.

**B.4.2 Layout** (the four values of `FilmBarLayout`, G.5):

- S = I2F(display width).
- V = the opacity (element + 0x1d4). hide = CAP1(NONNEG(1 - V)).
- width = PS(I2F(N), 72.0, -8.0).
- baseline = PS(1 - hide, 550.0, hide * 650.0).
- left = (S - width) * 0.5.
- top = baseline - 51.0.

(Example, exact: display 1280, N = 8, V = 1.0 gives width 568, baseline 550, left 356, top 499. V = 0 gives
baseline 650, top 599.)

**B.4.3 Panel.**

1. Rectangle: rx = left - 10.0; ry = top - 10.0; rw = width + 20.0; rh = ((baseline + 21.0) - top) + 20.0.
2. Call 0x640ac0 "make a panel" as in B.3, with the description of B.3, the size (rw, rh), four zero words and a
   zeroed result block.
3. eb[243] + 1; eb[244] = the low 32 bits of the result block's first quadword; eb[245] = the word at result
   block + 0x3c; eb[246] = the word at result block + 0x40.
4. If eb[1] bit 0x20 is set or deferred mode is on: eb[46] + 1, then `u3x_k26_quad(base, ctx, 0, rx, ry, rx + rw,
   ry + rh, colour (0, 0, 0, Ap))`. Otherwise: call 0x63d6f0 "draw a panel" as in B.3 with `ctx`, the style block,
   the result block and the rectangle (rx, ry, rw, rh).
5. eb[47] = rx; eb[63] = ry.

The make call happens in both cases, also when only the plain quad is drawn.

**B.4.4 Cursor.** The opacity A = the float bits at element + 0x1d4, read again now (after the panel calls). Then
`u3x_k26_cursor(base, element, address of the layout, A)` (Part C: one `u3h_sincos` call, then one sprite).

**B.4.5 Tabs.** For i = 0, 1, ... while i is below the visible count (element + 0x1d0, read again before each tab)
and below 14: the entry is the pointer at element + 0x160 + 8 i. If `u3x_k29_eok(entry)` is 0: eb[221] + 1 and go
on with the next i. Otherwise:

1. Icon. tx = PS(I2F(i), 72.0, left). ty = baseline - 16.0. prox = 1 - |C - I2F(i)|, where C is the smoothed cursor
   (element + 4, read for each tab) and the difference is rounded before the sign is cleared. glow =
   CAP1(NONNEG(prox)). Colour (1.0, 1.0, 0.8, A * MIX(0.3, 0.8, glow)).
   `u3x_k26_sprite(base, entry image, entry index, colour, tx, ty, 64.0, 32.0, 0, 0)`.
2. If prox < 0.001: this tab is finished (no label). A NaN `prox` is not below 0.001, so the label is drawn.
3. Label. Packed colour K = `u3x_k26_col` of (1.0, 1.0, 0.8, A * prox). A text block gets the defaults
   (`u3x_k26_tp`). ly = ty + 42.0.
4. Measuring call: with x = tx - 468.0, y = ly, both scales 0.7, colour K, flags 5, width 1000.0, everything
   else default: text = `u3x_k26_str(base, entry text id)` (the lookup call comes after the block was prepared and
   before the text call), then `u3x_k26_text(base, block, text)`.
5. lw = (the width the game left at +0x94) + 1.0. mid = tx + 32.0. limit = (left + width) - lw.
   want = PS(lw, -0.5, mid). pos = SEL(want - left, want, left) (not left of the panel's left edge), then
   x = SEL(pos - limit, limit, pos) (the label's right end not past the bar's right end; this second test wins).
6. Drawing call: the same block as the game left it, with these fields set again: flags 1, font 0xffffffff, colour
   K, y = ly, both scales 0.7, +0x2c = 1.0, width = lw, x as computed. `u3x_k26_text(base, block, text)` with the
   same text pointer.
7. eb[225] + 1.

**B.4.6 Timeline.** F = the film state (base + 0x17c3ba8).

1. len = I2F(film length). Call 0x6a2700 with F; sec = the low 32 bits of the result as a signed integer.
   eb[223] = sec; eb[224] = the film length word. cur = I2F(sec).
2. part = 0 (pattern 0) when len is +0 or -0, else cur / len.
3. Track: `u3x_k26_quad(base, ctx, 0, left, top, left + width, top + 20.0, colour (1.0, 1.0, 0.8, 0.2))`.
4. bw = width - 160.0; by = top + 4.0; bx = left + 80.0. pw = FLOOR1(bw * part) (at least one pixel).
   Played part: `u3x_k26_quad(base, ctx, 0, bx, by, bx + pw, by + 12.0, colour (1.0, 1.0, 0.8, 0.5))`.
5. Play head: hx = (pw + bx) - 12.0; hy = by - 6.0. texture = `u3x_k26_tex(base, 0xeae7862a)`, then
   `u3x_k26_quad(base, ctx, texture, hx, hy, hx + 24.0, hy + 24.0, colour (1.0, 1.0, 0.8, 1.0))`.
6. Markers. ci = the current clip index. If ci <= -1: no markers. If ci >= 10: eb[227] + 1, no markers.
   Otherwise, for n = 0, 1, ... below the clip's marker count (the count byte is read again before each marker):
   if n has reached 64: eb[227] + 1 and stop. Else, for marker n:
   - where = 0 (pattern 0) when len is +0 or -0, else (1.0 / len) * (I2F(marker tick - film start tick) * (1/300)).
     The tick difference is a 32-bit integer subtraction (wrapping); the product in the inner brackets is rounded
     first; 1.0 / len is one division, rounded.
   - Colour: white (1, 1, 1, 1); kind -1: (1, 0, 0, 1); kind -2: (0.3, 0.3, 0.3, 1).
   - qx = (FLOOR1(bw * where) + bx) - 2.5; qy = by - 6.0.
   - texture = `u3x_k26_tex(base, 0xeae7862a)` (looked up again for every marker), then
     `u3x_k26_quad(base, ctx, texture, qx, qy, qx + 5.0, qy + 24.0, colour)`.
   - eb[226] + 1.

**B.4.7 Texts.** One text block with the defaults (`u3x_k26_tp`), kept and changed from text to text (the game may
change fields during a text call; only the fields named are set again), and one 64-byte character buffer.
Tk = `u3x_k26_col` of (1.0, 1.0, 0.8, 0.9).

1. Current time: both scales 0.5, colour Tk, width 80.0, x = left + 4.0, y = top (flags still the default 0).
   `u3x_k26_time(base, buffer, F2I(cur + 0.5))`, then `u3x_k26_text(base, block, buffer)`.
2. Total time: y = top, flags 2, width 80.0, x = (width + left) - 84.0.
   `u3x_k26_time(base, buffer, F2I(len + 0.5))`, then `u3x_k26_text(base, block, buffer)`.
3. Film name: width 400.0, both scales 1.0, y = 150.0, flags 9, x = (S - 400.0) * 0.5.
   `u3x_k26_text(base, block, F + 0x1e6c)`.
4. Speed. Nothing more happens when the 32 bits at image 0x16e716c are exactly 0x3f800000 (a comparison of the bit
   pattern, not of the value), or when the byte at image 0x17c3571 is 0. Otherwise: sp = the float bits at image
   0x17b4a90 + 0x8c; width 100.0, flags 1, both scales 0.5, x = hx - 38.0, y = hy - 20.0. The format `x%2.1f` is
   copied to a writable 8-byte buffer on the stack (the characters, then zeros). Call 0x139cab0 with integers
   1 = buffer, 2 = 64, 3 = the format copy; xmm0 slot = `u3x_k26_f2d(sp)`; vector count 1. Then
   `u3x_k26_text(base, block, buffer)`; eb[228] + 1.

Then return 0.

**B.4.8 Order of the calls of a full draw**: panel make (0x640ac0); plain quad or panel draw (0x63d6f0);
the glow (`u3h_sincos`, sprite); per tab: sprite, and for a label: text lookup, text (measure), text (draw); current second
(0x6a2700); track quad; played quad; texture, head quad; per marker: texture, quad; time format, text; time
format, text; name text; and for the speed: snprintf, text.

**Storage.** The panel result block (0x200 bytes), the text blocks and (inside the primitives) the vertex block
must be 16-byte aligned. Give every other block handed to the game (panel description, style, rectangle, size,
zero words, colours) 16-byte alignment too: the old build had them so.

### B.5 Test scenarios

Fake image with recording stubs; compare the call sequence with all call block slots, the blocks the pointers
lead to at the time of each call, the eb words, the i03 word and the result. The text draw stub should write a
width to +0x94 on a measuring call (flags 5) and may scribble on other fields of the block.

1. Screenshot byte set: eb[220] and eb[229] + 1, no call, callee check not consulted.
2. Callee check bad (one function start wrong): eb[221] + 1 on every call, no draw, also after the image is
   repaired.
3. `frame` null, below 0x10000, not a multiple of 8, 2^47 or above: eb[221] + 1, nothing read from it.
4. N = -1 and N = 15: eb[222] = N, eb[221] + 1, no draw; in deferred mode the i03 word is already counted.
5. Default switches (bit 18 clear): context 0 in every quad, panel as the plain black quad with opacity
   0x3f008081, eb[46] + 1, i03 word + 1; the make call is still made; eb[243..246], eb[47], eb[63] set.
6. Bit 18 set, bit 5 clear: panel through 0x63d6f0 with the frame's context, the two stack arguments 1 and
   0xffffffff; eb[46] unchanged; quads carry the frame's context.
7. Bit 18 set and bit 5 set: plain quad with the frame's context.
8. N = 0: width -8, no tab, the rest drawn.
9. N = 14 with all entries valid; one entry null, one odd (address not a multiple of 4), one a multiple of 4 but
   not of 8 (accepted): eb[221] counts the bad ones, the others are drawn.
10. Cursor on tab i exactly (prox = 1): icon opacity A * 0.8 path, label drawn. Cursor one tab away (prox = 0): no
    label. prox just above 0.001 (label) and just below (no label). Cursor NaN: label drawn. Cursor far away (prox
    negative, glow 0).
11. Label placement: a measured width that leaves the label centred; a wide label on tab 0 (held at `left`); a
    wide label on the last tab (held at `limit`); a label wider than the whole bar (the right limit wins and lies
    left of `left`).
12. Text lookup returning the fallback string.
13. Opacity V of 1, 0.5, 0, above 1 (hide 0), negative (hide 1), NaN (hide 0, baseline 550).
14. Film length 0 (part 0, played width 1.0, every marker at where 0); length negative; current second above the
    length; current second negative; a game result with high 32 bits set (only the low 32 count).
15. Clip index -1, -7, 10, 1000 (guard counted for the last two); clip 0 and 9 with 0 markers; markers of kind -1,
    -2, 0, 5; marker ticks before the film start; counts 64 (all drawn, no guard), 65 and 255 (64 drawn, guard
    counted once).
16. Speed text: the word at 0x16e716c equal to 0x3f800000 (none); the byte at 0x17c3571 equal to 0 (none); both
    conditions passed with time scales 0.5, 2.0, 0.25: one snprintf call with the double pattern and vector count
    1, one more text, eb[228] + 1.
17. Display widths 1280, 1920, 3840, 0 and random; thousands of random element and film states for the float
    results, compared bit for bit.

---------------------------------------------------------------------------------------------------------------------

## Things in the old code that look odd, to be kept as behaviour

1. Film action: the counter of calls rises before the switch test; an action with no live film player is still
   sent (target 0), counted as sent and reported as handled.
2. The five event boxes are static storage, cleared before every send, not stack blocks.
3. `u3x_k26_callees` never checks again; a "bad" answer turns the bar off for the rest of the run and every later
   frame adds 1 to eb[221].
4. eb[221] counts two different things: refused draws and bad tab entries.
5. The HUD frame's context is read even when deferred mode then replaces it by 0; eb[222] and the i03 word are
   written before the visible count is checked.
6. The "make a panel" call is made also when the plain quad is drawn; its result is then used only for the three
   diagnostic words.
7. The opacity and the visible count are read from the element more than once during one draw (B.4.4, B.4.5).
8. Tab label: between the measuring call and the drawing call, fields are set again that were already set (font,
   colour, y, scales, +0x2c). This restores them in case the game's measuring call changed them.
9. A tab whose distance value is NaN gets its label (the "too far" test is an ordered less-than).
10. The play head / marker texture is looked up again for the head and for every marker (one 0x11b89b0 call each).
11. 1 / length is computed also when the length is 0 (infinity, then unused); current / length may be computed
    and thrown away. Neither may fault.
12. The marker guard at 64 adds 1 to eb[227] once and ends the markers; a clip index of 10 or more also adds 1.
13. Total time text: y and width are set again to the values they already have.
14. The film name is drawn in every frame; on PS4 nothing writes the name field, so it is normally an empty text
    (the text call is still made).
15. The speed text's "normal speed" test compares bit patterns: only the pattern 0x3f800000 counts as 1.0.
16. The speed text is formatted from the time scale (0x17b4a90 + 0x8c), not from the speed factor that is tested.
17. `u3x_k26_tex` calls the game also when the image is not in the table (index 0xffffffff).
18. The unused call block slots are zero on every call (also xmm registers and stack arguments the game function
    does not take).
19. Part C, time text: the hour wraps at 24, so a time of exactly 24 hours (or 48, ...) is written in the short
    form as "0:00"; negative seconds give negative numbers (as 32-bit patterns) to the game's snprintf.
20. Part C, glow: the pulse is clamped to 0..1 twice in a row; the image is read from an address that is not a
    multiple of 8.
21. Part C, colour packing: -0 and NaN give 0; the product with 255 is rounded to single before it is truncated
    (0.9 gives 229).
22. Part C, text lookup: the fallback pointer is read only when the game returns null; a null fallback is returned
    as null.

## What stays as it is

- `u3x_k26_call` (the assembly that performs a call block) and the block layout (G.2): the game's calling
  convention; a shared tool of CONVENTIONS.h.
- The exported names and signatures of A.2, B.2 and C.2 (two are new: `u3x_film_action`, `u3x_k26_cursor`); the C
  wrappers `k26_call`, `k26_f2d`, `k26_fsel`, `k26_rd`, `k26_c4`, `k26_col`, `k26_tp`, `k26_text`, `k26_str`,
  `k26_sprite`, `k26_tex`, `k26_quad`, `k26_time`, `k26_callees`, `k26_draw`; the exports `u3h_k26_draw` and
  `u3h_film_action`; the callers `u3x_menu_action` and `u3x_k23_hud`. Only `k41_film` changes (A.2).
- The C statics `k41_d`, `k41_box`, `i03_d` and the eb array: they live in the C part (maintainers read them in the
  running game); the new code reaches them through the pointers and accessors named above.
- All image offsets, ids, the function-start words and the format text `x%2.1f`.

## Exported names per part

- Part A: `u3x_is_film_action`, `u3x_film_action` (new), `u3x_k26_tp`, `u3x_k26_tex`, `u3x_k26_quad`,
  `u3x_k26_sprite`, `u3x_k26_callees`.
- Part C (same implementer as A): `u3x_k26_f2d`, `u3x_k26_fsel`, `u3x_k26_rd`, `u3x_k26_c4`, `u3x_k26_col`,
  `u3x_k26_text`, `u3x_k26_str`, `u3x_k26_time`, `u3x_k26_cursor` (new).
- Part B: `u3x_k26_draw`.
- Not delivered, stays: `u3x_k26_call`.

## Decisions (answers of the pipeline owner, 2026-11-14)

1. CONVENTIONS.h has `f_from_int`, `f_to_int`, `f_less`, `f_div`, `f_madd`, `f_root`, `ext_eb()`, `struct
   GameCall` and the prototype of `u3x_k26_call`. Functions of other groups and of the C part are declared by the
   implementer from the signatures in G.3.
2. The counters and event boxes of the film action stay in the C part and are passed as pointers (A.2).
3. The 14 film action ids are interface ids and are used as compare values.
4. The pieces of the old film bar that were in no work group are Part C; the one-line helpers of G.4 are not
   delivered.

## Clarifications (tester, 2026-11-14)

Facts found or confirmed by the old-against-new test (`reimpl2\G04\test_g04.cpp`; recording stubs, stubs that change
the game's memory during a call, and a tracer of every memory word touched). "Not observable" means: no result, no
memory written and no call changes, whatever the game does.

1. **B.4.6 step 1, film length (a wrong reading is possible; the new Part B differed here).** The film length word
   (film state + 0x158) is read TWICE: once before the 0x6a2700 call, for `len` and everything computed from it
   (played part, markers, total time text), and once more after the call has returned, for eb[224]. So eb[224] is
   the length word as it is after the call; when the call changes the word, eb[224] holds the new value while the
   bar of this frame is still drawn with the old one. Order after the call: eb[223] = sec, read the length word
   again, eb[224] = that value.
2. **B.4.6 step 6, film start tick.** It is read for every marker (after the texture and quad calls of the marker
   before), like the marker's tick and kind; a value changed by one of those calls is used from the next marker on.
   When `len` is +0 or -0, the old code reads neither the marker's tick nor the start tick (not observable).
3. **B.4.7, the 64-byte character buffer.** It is not zeroed or filled before use. Only the game's snprintf writes to
   it; if that wrote nothing, the following text call would get whatever the stack held.
4. **B.3, "make a panel" call.** Confirmed: vector count (al) 0; xmm0 slot 0x000000003f800000 (1.0 in the low half,
   high half 0); xmm1 to xmm4 slots 0; both stack slots 0; integer 3 is 0; integer 6 is 0x00000000ffffffff. The
   "draw a panel" call: all five float slots 0, vector count 0, stack 1 = 1, stack 2 = 0x00000000ffffffff.
5. **NaN and operand order.** With the single operations of CONVENTIONS.h a lone NaN operand comes out quiet with its
   sign and payload, and of two NaN operands the first one wins. In this group two different NaNs can meet in one
   place only: the label colour's opacity `A * prox` (opacity NaN and cursor NaN), and that value only goes into
   `u3x_k26_col`, which gives 0 for every NaN. So no operand order is observable here; writing the operands in the
   order of the formulas of this spec is right. Everywhere else at most one operand can be NaN: the layout values
   and the timeline values are never NaN (they come from integers and from clamped values); the glow's and the tab
   icons' blend factor is never NaN (the pulse and the glow value are clamped first, NaN becomes 0); a NaN width
   left by the measuring call ends with the label at `left` and the width field holding that NaN plus 1.0 (quiet).
6. **When the eb[1] bits are read.** Bit 0x40000: once per draw, after the callee check and the frame check and
   before `u3h_i03_d()` is called; a change of the word during a later call does not change the mode of this draw.
   Bit 0x20: after the make-a-panel call has returned (a change made during that call counts); in deferred mode
   the bit does not matter (the plain quad is drawn anyway).
   Bit 0x80 (film action): after counters[0] was increased and before the event word is read.
7. **How often `u3h_ext` is called.** The old draw asks for the state block once at its start (also in a refused
   draw), once more for the panel, once for every label drawn, once for the timeline and once for a speed text:
   1 call in a refused draw, 3 + labels + speed texts in a drawn one. `u3h_ext` has no effect and always returns
   the same address, so asking once per draw and keeping the address is the same (not observable). The old film
   action (C) used the state directly; the new one asks once per call, after counters[0].
8. **C.4.9 step 5, pulse clamp.** Clamping once and clamping twice give the same pattern for all 2^32 inputs
   (tested exhaustively), NaN and -0 included (NaN gives +0, -0 stays -0).
9. **A.4.4, read order in `u3x_k26_tex`.** The old code reads: the directory pointer, the table pointer, the count,
   the entries pointer, then the entry ids from entry 0 up to the first match. No call lies between these reads, so
   the order of the count and the entries pointer is not observable; the "not found" conditions are as A.4.4 says.
10. **A.4.7, how many words `u3x_k26_callees` reads.** On the first call the old code compares all nine words without
    stopping at a mismatch; only the ninth (0x139cab0) is not read when the eighth (0x6a2700) is wrong. Stopping at
    the first mismatch gives the same answer (the nine words are game code, always readable): not observable.
11. **A.4.2, what `u3x_film_action` reads.** Switch bit 0x80 set: counters[0] and eb[1], nothing else (not the event).
    Not a film id: also the event's first word; the handle words are not read. Film id: also the 8 bytes at image
    0x1b3ee38 and the 4 bytes at image 0x1b3ee40, both in every case (the pointer is not dereferenced here: that is
    `u3x_live_object`'s job, also for a null pointer and an id of 0); the boxes are cleared after `u3x_live_object`
    has returned; counters[1], [2] and [3] are updated after the game call has returned.
12. **Text parameter blocks.** All 48 words of every block passed to 0xf1d9f0 were compared: nothing is set besides
    the defaults of A.4.3 and the fields B.4.5 and B.4.7 name; fields the game's text draw changed during a call and
    that are not named for the next text stay as the game left them (in the label block between measuring and
    drawing, and in the times block from text to text).
13. **B.4.1 step 4.** The old source reads the quadword at frame - 0x11f8 in every case; in the old code as compiled
    (host build, -O1 and -O2) the read only happens when bit 0x40000 is set. Not observable: the frame passed
    `ptr_ok` and is the HUD draw's own stack frame.
14. **B.4.5, visible count.** The old code reads it once more after the 14th tab (not observable).
15. **B.4.4 / C.4.9, glow.** The cursor position, `layout->left` and the game clock are read before the `u3h_sincos`
    call; the glow's image and index (element + 0x1dc, + 0x1e4) are read after it.
16. **Storage.** In the test every block the new code hands to the game is 16-byte aligned.
