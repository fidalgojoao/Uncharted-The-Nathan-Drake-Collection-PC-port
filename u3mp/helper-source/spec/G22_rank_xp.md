# G22. Legacy rank and XP: levels, thresholds, profile box, challenge counters, rank badges

Behaviour description for a reimplementation (reimpl2 pipeline, describer's document). It says what the functions
do, not how the old ones were written. All offsets are hexadecimal when prefixed with 0x. "Image offset X" means the
address `base + X`, where `base` is the game image base every function receives. All multi-byte values are
little-endian; "u32 / s32 / u16 / s16 / u64 / u8" are unsigned / signed integers of that width, "pointer" is 8 bytes.
"Word n" of a request means the 32-bit word at byte offset 4·n. A "float pattern" is the 32-bit pattern of a
single-precision float, carried in a u32.

## 1. Purpose and callers

PS3 1.19 raised the Legacy (prestige) cap from 3 to 5 and made the XP curve depend on the profile's legacy level:
every XP threshold is multiplied by 2.5 at Legacy 4 and by 5.0 at Legacy 5. The PS4 game (built from 1.10) has
neither. This group is the helper's replacement for the PS4 game's level / threshold functions and for the places
that show them, plus two neighbours that work on the same profile record: the per-game-type challenge counters of
the post-match handler and the layered rank badge of the menus.

Four parts:

**A. Record, level and threshold (7 functions).** The profile record getter with its lazy default
(`u3x_lg_rec`), the demo / beta title test (`u3x_lg_beta`), the threshold of a level (`u3x_lg_thresh`), the level of
an XP amount (`u3x_lg_from_xp`), the current level of a user (`u3x_lg_cur_level`), and the answers of six code
thunks the builder plants in the game (`u3x_legacy_kind`, kinds 60 to 65).

- The C function `lg_rec` is called by `mp_quit119`, `i05_stats`, `j01_tick`, `m02_lb_local`, `lg_xp4fc`, and,
  through the C export `u3h_lg_rec`, by many C++ functions of other groups (post-match handler, missions, friends,
  leaderboards). It is the only way the helper reaches a user's multiplayer record.
- `lg_beta`, `lg_from_xp` and `lg_cur_level` have no caller outside this group in v7.212.
- `lg_thresh` (a C one-liner today) is also called by `l01_rank` (other group, stays in C).
- `legacy_kind` is called by the helper's event entry `u3_profile_event` for stub kinds 60 to 65, only with a
  non-zero `base`. The thunks replace: 60 the game's "current level" function (image 0x662760), 61 four call sites
  of its "level from XP" function (0x6625a0: two in the XP award, two in the post-match XP bar), 62 its "tier"
  function (0x662950), 63 its "XP gate" function (0x662ba0), 64 its "rank value of a profile" function (0x781ea0),
  65 two inlined threshold reads in the post-match draw (0x617ff7).

**B. The profile box (`u3x_lg_profile_box`).** Called by `profile_box_source` (the helper's hook on the profile box
data source, native enum 0x89) for every request that is not operation 21 or 1, before the game's own source runs.
It answers the six requests that depend on XP thresholds (bar percent, two flags, three texts) with legacy-scaled
values; a result of 0 lets the caller run the game's source.

**C. The Records list (`u3x_records_source`).** Called by `u3_profile_event` for every list request, in the chain
of list-source hooks. It handles the list whose source tag is 0x4c414ec7 (the Records list of the net profile
page): everything goes to the game's own source, except the icon of the Legacy row, which becomes the 1.19
"prestige back" picture of the user's legacy level.

**D. Challenge counters (`u3x_pm_challenge`, `u3x_pm_365c5c`, `u3x_pm_365da8`).** Called by the post-match handler's
per-user part (`pm_user`, C++, other group) after a match: `u3x_pm_365c5c` on a win, `u3x_pm_365da8` when a co-op
game was completed, `u3x_pm_challenge` directly for two more counters. `u3x_pm_challenge` has a C declaration but no
C caller.

**E. Rank badge (`u3x_rank_bits`, `u3x_rank_tag`, `u3x_rank_back`, `u3x_texture_sharp`, `u3x_rank_draw`).**
- `u3x_rank_bits`: by `u3x_legacy_kind` kind 64 and the C wrapper `l01_rankbits` (no other user).
- `u3x_rank_tag`: through the C wrapper `l01_tag`, by `rank_icon_as_texture` (the profile box's rank picture and
  every other rank icon answer) and `n28_rank_item` (HUD rank-up popup item).
- `u3x_rank_back`: by `u3x_rank_draw` and, through the C wrapper `l01_back_get`, by `n28_rank_item`.
- `u3x_texture_sharp`: through the C wrapper `l01_sharp`, by `l01_sweep` (a per-frame walk over the rank textures).
- `u3x_rank_draw`: through the C wrapper `l01_draw`, by the stub dispatcher for stub kind 230: the builder
  replaces the one call of the game's textured UI quad (image 0x63dc40) inside the item draw (call site 0x64228e)
  by a stub; the helper must issue that quad call itself, always.

## 2. Interface

No function takes or returns a float by value. Names marked "existing" are exported today and keep name and
signature. Names marked "new" replace a C body; the C function of the old name stays as a one-line wrapper (or, where
it has no outside caller, may be dropped). All are `extern "C"`.

| Function | Signature | Status | Called by | C wrapper passes |
|---|---|---|---|---|
| record getter | `uint8_t *u3x_lg_rec(uintptr_t base, void *s, uint8_t *p)` | new (C `lg_rec`) | this group; C `lg_rec` and through it `u3h_lg_rec` | its three arguments unchanged |
| beta title | `int u3x_lg_beta(uintptr_t base)` | new (C `lg_beta`) | this group | `base` (no outside caller) |
| threshold | `int32_t u3x_lg_thresh(void *s, int32_t level, int32_t legacy)` | new (C `lg_thresh`, a forwarding one-liner with a counter) | this group; C `lg_thresh` (for `l01_rank`) | its three arguments unchanged |
| level from XP | `int32_t u3x_lg_from_xp(uintptr_t base, void *s, int32_t xp, int32_t legacy)` | new (C `lg_from_xp`) | this group | its four arguments (no outside caller) |
| current level | `int32_t u3x_lg_cur_level(uintptr_t base, void *s, uint32_t user)` | new (C `lg_cur_level`) | this group | its three arguments (no outside caller) |
| thunk answers | `int u3x_legacy_kind(uintptr_t base, void *s, uint64_t *ev, int kind)` | new (C `legacy_kind`) | C `legacy_kind` | its four arguments unchanged |
| profile box | `int u3x_lg_profile_box(uintptr_t base, void *s, int32_t *event)` | new (C `lg_profile_box`) | C `lg_profile_box` | its three arguments unchanged |
| Records list | `int u3x_records_source(uintptr_t base, int32_t *event, void *s)` | new (C `records_source`) | C `records_source` | its three arguments unchanged |
| challenge counter | `void u3x_pm_challenge(uintptr_t base, void *s, uint32_t u, uint32_t id, int32_t v, int bit)` | existing | `pm_user`, this group | - |
| win challenge | `void u3x_pm_365c5c(uintptr_t base, void *s, uint32_t u, int32_t mine, int32_t theirs)` | existing | `pm_user` | - |
| co-op challenges | `void u3x_pm_365da8(uintptr_t base, void *s, uint32_t u, uint32_t map, int32_t a3)` | existing | `pm_user` | - |
| badge bits | `uint32_t u3x_rank_bits(const uint8_t *rec, uint32_t star_old)` | existing | this group; C `l01_rankbits` | both arguments |
| badge tag | `void u3x_rank_tag(uintptr_t base, void *s, int32_t *ev, uint32_t v)` | existing | C `l01_tag` | its four arguments |
| prestige back | `void *u3x_rank_back(uintptr_t base, void *s, uint32_t level)` | existing (stray, see report) | this group; C `l01_back_get` | its three arguments |
| sharp sampler | `void u3x_texture_sharp(void *t)` | existing | C `l01_sharp` | the pointer |
| badge draw | `int u3x_rank_draw(uintptr_t base, void *s, const uint64_t *r)` | existing | C `l01_draw` | base, state, the stub's register array |

The old file-private function `quad_rect` (four corner positions of a rectangle, 4.14) has one user,
`u3x_rank_draw`; it is not exported and need not exist as a function of its own.

Parameters:

- `base`: game image base. `s`: the helper's state object (opaque; fields in 3.2).
- `p`: a profile object (3.4), never null. `user`, `u`: local user index (0 or 1).
- `level`, `xp`, `legacy`: s32 values; `legacy` is the profile's legacy level (0 to 5 in practice, any value allowed).
- `ev` of `u3x_legacy_kind`: four u64 words; words 0 to 2 are the first three integer arguments (rdi, rsi, rdx) of
  the replaced game function, word 3 becomes its 64-bit result. Kind 65 uses the words differently (4.6).
- `event`: one data-source request (3.3).
- `id`, `v`, `bit`: challenge stat number, amount, and mode (0 = add, non-zero = set a map bit).
- `mine`, `theirs`: the two teams' scores. `map`: co-op map index. `a3`: a u16 match value passed by the caller.
- `rec`: a multiplayer record (3.5). `star_old`: fallback for the star bit.
- `ev` of `u3x_rank_tag`: an image answer (request or item, at least 8 words). `v`: a rank word (3.6).
- `t`: a texture object or anything else. `r`: seven u64 words (4.14).

Results: `u3x_lg_rec` returns `p + 0x248`. `u3x_lg_beta` 1 or 0. `u3x_legacy_kind` 1 when the kind is 60 to 65, else
0. `u3x_lg_profile_box` 1 when it answered, else 0. `u3x_records_source` 1 when the request belongs to this list,
else 0. `u3x_rank_back` a texture pointer or null. `u3x_rank_draw` always 1.

Functions of the C part, of other groups and existing exported C++ functions that the new code calls (declare them;
do not reimplement them):

| Name | Signature | What it does |
|---|---|---|
| `u3h_ext` | `uint8_t *u3h_ext(void)` | address of the helper's own state block (3.1). In CONVENTIONS.h |
| `u3h_lg_profile` | `uint8_t *u3h_lg_profile(uintptr_t base, uint32_t user)` | profile object of a user: the pointer at image 0x17dd610 plus `user` · 0x7b00, or null when that pointer is null |
| `u3h_l01_state` | `uint8_t *u3h_l01_state(int which)` | fixed addresses of the badge state in the C part (3.7) |
| `u3h_icon_index` | `int32_t u3h_icon_index(uintptr_t base, uint32_t id)` | index of image `id` in the game's UI image table, or −1 |
| `u3h_text` | `const char *u3h_text(uintptr_t base, void *s, uint32_t id)` | localized text of a text id through the game's text lookup; never null (empty string when there is none) |
| `u3h_set_text` | `void u3h_set_text(int32_t *ev, const char *t)` | stores `t` as the request's text answer (pointer at byte 0x40) |
| `u3h_gate_closed` | `int u3h_gate_closed(uintptr_t base, int32_t *ev)` | 0 while multiplayer is active (u8 at image 0x17d5c61 non-zero); otherwise stores the game's empty string (image 0x13ac250) as the request's text answer, counts, and returns 1 |
| `u3x_sym_table` | `uint8_t *u3x_sym_table(void *s, uint32_t id)` | data of a script symbol: calls the lookup function of the state (3.2) with `id`; null when the function pointer, the returned handle or the pointer the handle holds is null |
| `u3x_xp_scale` | `int32_t u3x_xp_scale(int32_t xp, int32_t legacy)` | the legacy scaling of one XP value (3.9) |
| `u3x_level_threshold` | `int32_t u3x_level_threshold(const int32_t *table, int32_t count, int32_t level, int32_t legacy)` | `u3x_xp_scale(table[i], legacy)` with i = `level` limited to 0 .. `count` − 1 (below 0 gives 0, above gives `count` − 1) |
| `u3x_level_for_xp` | `int32_t u3x_level_for_xp(const int32_t *table, int32_t count, int32_t xp, int32_t legacy)` | the lowest L in 0 .. `count` − 2 with scaled table[L] ≤ `xp` < scaled table[L+1] (signed); `count` − 1 when there is none |
| `u3x_xp_percent` | `int32_t u3x_xp_percent(int32_t low, int32_t high, int32_t xp)` | 0 when `xp` < `low` (signed) or `high` = `low`; else (`xp` − `low`) · 100 / (`high` − `low`), all in 32-bit signed arithmetic, quotient truncated toward zero |
| `u3x_rank_word` | `uint32_t u3x_rank_word(int32_t level, uint32_t legacy, uint32_t badge_bits)` | `badge_bits` OR (`level` AND 0xffff) OR ((`legacy` AND 0x3fff) shifted left 16) |
| `u3x_legacy_back_name` | `void u3x_legacy_back_name(char *out, char *end, int32_t legacy)` | writes the NUL-terminated texture name "ranks/prestige-back-" followed by `legacy` in decimal into `out`, never writing past `end` |
| `u3x_image_answer` | `void u3x_image_answer(int32_t *ev, void *texture)` | makes the request an image answer: words 3 to 12 cleared, words 9 and 11 = −1, word 12 = 1.0 (0x3f800000); with a texture word 2 = 2 and the pointer stored at byte 0x10, without one word 2 = 0x10 |
| `u3x_challenge_entry` | `const uint8_t *u3x_challenge_entry(const uint8_t *table, int32_t number)` | the entry of challenge `number` in the challenge definitions (3.10), or null (also for a null table) |
| `u3x_n32_records` | `int u3x_n32_records(uintptr_t base)` | non-zero while the Records list shows the online rows although the LAN byte is set (other group) |
| `u3x_n32_call` | `int u3x_n32_call(uintptr_t base, int32_t *ev, void *s, void (*native)(int32_t *))` | answers a Records request in that online-rows mode (calling `native` itself where needed); 1 = answered, 0 = not its request (other group) |
| `u3x_pm_ngentry` | `uint8_t *u3x_pm_ngentry(uintptr_t base, void *s)` | the definition record of the current net game, or null (other group; it performs one `u3x_sym_table`-style lookup of symbol 0x3b9a067d) |
| `u3x_pm_type` | `int32_t u3x_pm_type(uintptr_t base, void *s)` | the game type of the current net game (s32 at record + 0xc), 0 when there is no record (other group; one call of `u3x_pm_ngentry`) |
| `u3x_pm_coop` | `int u3x_pm_coop(uintptr_t base, void *s)` | non-zero when the current net game is a co-op type (other group; one call of `u3x_pm_ngentry`) |
| `u3x_k26_call` | `uint64_t u3x_k26_call(void *game_call_block)` | a game call that passes float registers. In CONVENTIONS.h |

`u3h_ext`, `u3h_l01_state` and `u3h_lg_profile` are plain address accessors: how often and in which order they are
called is not part of the behaviour.

## 3. Game and helper facts

### 3.1 The helper's state block (address from `u3h_ext()`)

The counter array "ea" starts at offset 0x3c50 (u32 words; ea[n] is at 0x3c50 + 4·n); the switch array "eb" at
0x4050 (`ext_eb()` of CONVENTIONS.h).

| Word | Offset | Meaning |
|---|---|---|
| ea[1] | 0x3c54 | switches; bit 0 set = the Legacy 4/5 feature is off (only `u3x_lg_profile_box` tests it) |
| ea[28] | 0x3cc0 | counter: current-level evaluations |
| ea[29] | 0x3cc4 | counter: level-from-XP evaluations |
| ea[30] | 0x3cc8 | counter: threshold evaluations made with legacy 4 or 5 |
| ea[31] | 0x3ccc | the last legacy value a level-from-XP evaluation saw |
| ea[32] | 0x3cd0 | counter: profile box requests answered here |
| ea[33] | 0x3cd4 | counter: Records legacy pictures answered |
| ea[34] | 0x3cd8 | counter: kind 65 answers |
| ea[35] | 0x3cdc | counter: kind 64 answers |
| ea[57] | 0x3d34 | challenge counter calls (+1 per call) and updates (+0x10000 per update) |
| ea[212] | 0x3fa0 | counter: prestige-back texture requests |
| ea[213] | 0x3fa4 | counter: prestige-back textures found loaded |
| ea[214] | 0x3fa8 | counter: badge draws without a loaded back |
| eb[2] | 0x4058 | switches; bit 20 set = the layered rank badge is off |

Counters are plain 32-bit increments (wrapping).

### 3.2 The state object `s`

| Offset | Type | Meaning |
|---|---|---|
| 656 | pointer to function | script symbol lookup (used through `u3x_sym_table`) |
| 928 | pointer to function | "native 20", the game's texture-by-name lookup: arguments (address of image offset 0x1b31128, pointer to a NUL-terminated name); returns a texture object or null. A miss queues the load, so a later call can succeed. May itself be null. |

### 3.3 Data-source requests

Common: word 0 (s32) is the operation; the pointer at byte 0x98 is the source descriptor, whose u32 at +0x1c is
the source tag; the pointer at byte 0x40 is the text answer.

Profile box:

| Operation | In | Out |
|---|---|---|
| 0xb (value) | word 0x13: value id | word 0xe: s32 value |
| 2 (text) | word 1: text id; word 0x15: sub index | text answer (pointer at byte 0x40) |

Records list, operation 4 (row image): word 1 = row index, word 0x14 = image slot; the answer is an image answer
(`u3x_image_answer`).

### 3.4 Profile objects

The pointer at image 0x17dd610 is an array of profile objects of 0x7b00 bytes, one per local user.

| Offset | Type | Meaning |
|---|---|---|
| 0x248 | 0x2828 bytes | the multiplayer record (3.5). Its first word is the format version |
| 0x7ae8 | u32 | load state; 3 = loaded |
| 0x7afc | u8 | non-zero: no challenge counting for this profile |

### 3.5 The multiplayer record (offsets from the record = profile + 0x248)

| Offset | Type | Meaning |
|---|---|---|
| 0x000 | u32 | format version; the current one is 0x56 |
| 0x004 .. 0x2827 | | the rest (0x2824 bytes) |
| 0x4fc | s32 | XP shown in the profile box and tested by the XP gate ("box XP") |
| 0x500 | s32 | XP the level is computed from ("level XP") |
| 0x504 | u32 | a value copied from script symbol 0xc27df04c when the record is reset |
| 0x1618 | s32 | legacy level |
| 0x1624 | u32 | co-op adventure bits; all of the low five set = the co-op star |
| 0x1627 | u8 | the top byte of that word (see 4.6 kind 64 and section 6) |
| 0x1630 + 4·m | s32 | games completed on co-op map m, m = 0 .. 0x1f |
| 0x1cdc + 4·(m − 0x20) | s32 | the same for m = 0x20 .. 0x3f |
| 4·n + 8 | u32 | challenge counter of stat number n (n = 0x755 .. 0x795: offsets 0x1d5c .. 0x1e5c) |
| 0x1d78 | s32 | a win counter for game type 0x13 (it is stat number 0x75c's slot) |
| 0x1f1c | u32 | non-zero = the profile shows the ND paw |

### 3.6 The rank word

u32: bits 0..15 level (index, 0 = rank 1), bits 16..29 legacy level, bit 30 paw, bit 31 co-op star. A tagged badge
answer carries the marker 0x4c303142 in word 6 and the rank word in word 7 of the image answer; in the item layout
the draw receives, these are the u32 at +0x10 and +0x14, and the u32 at +0 is the item type (2 = texture icon).

### 3.7 Badge state in the C part (`u3h_l01_state(which)`)

| which | Type | Meaning |
|---|---|---|
| 0 | 16 × u32 "d" | diagnostics: d[5] badges tagged, d[6] tagged draws, d[7] backs drawn, d[8] stars drawn, d[9] paws drawn, d[10] star index + 1, d[11] textures not found, d[12] last rank word tagged, d[13] stars without a layout. (Other elements belong to other code.) |
| 1 | 4 × u32 "q" | last badge rectangle: x0, y0, x1, y1 (float patterns) |
| 2 | 16 × u32 "t" | copy of the first 0x40 bytes of the last badge texture object |
| 3 | 6 × pointer | prestige-back texture per legacy level (index 1 .. 5 used) |
| 4 | pointer | the ND paw texture |
| any other value (5 is used) | s32 | UI image index of the co-op star plus 1; 0 = not found yet |

### 3.8 Game data

| Image offset | Type | Meaning |
|---|---|---|
| 0x17dd610 | pointer | profile object array (3.4) |
| 0x17bde75 | u8 | non-zero: the menu acts for user 1 |
| 0x17bf714 | u32 | the menu's user index otherwise. "Menu user" below = 1 when the u8 at 0x17bde75 is non-zero, else this word |
| 0x17d5c60 | u8 | LAN byte (non-zero in LAN mode) |
| 0x17d5c61 | u8 | multiplayer-active byte |
| 0x17d5c80 | u8 | net error byte |
| 0x17d5c98 | s32 | a net flag word (0x17d5c58 + 0x40); see 4.10 |
| 0x17d1a84 | u8 | a net game manager flag (0x17cb060 + 0x6a24); non-zero = no challenge counting |
| 0x17d1aa8 | u32 | a value of the current game's rules (0x17cb060 + 0x6a48), "X" in 4.12: the co-op difficulty step |
| 0x17d7abc | s32 | debug level override (debug options 0x17d7a50 + 0x6c); non-zero = level + 1 |
| 0x17c3a45 | u8 | a flag the tier answer tests (4.6 kind 62) |
| 0x17b9ca0 | text | the running title's id, NUL-terminated |
| 0x17b9d10 | u32 | a content id word of the running title |
| 0x17f3180 + 4·user | u32 | "new item" flags of a user (unlock manager 0x17f3068 + 0x118) |
| 0x13a65f0 | 3 rows × 4 u32 | Records rows in LAN mode: row + 8 = the row's action id, row + 12 = its "new" mask |
| 0x13a6620 | 5 rows × 4 u32 | Records rows online, same layout |
| 0x1b31128 | object | texture dictionary (first argument of native 20) |
| 0x1b676f0 | text buffer | the static buffer the game's formatter 0x62aa60 writes |
| 0x13ac250 | text | the game's empty string |

Ids: Legacy row action 0xa0bacf62. Co-op star image id 0x0242a063. Badge marker 0x4c303142. Texture names (the
helper's own art names): "ranks/prestige-back-" + one digit, "ranks/ndpaw-icon". Script symbols: 0xc85e199d XP
table (3.9), 0xc27df04c (record reset, 3.11), 0xb630d01c challenge definitions (3.10), 0x6426ff70 (tier value,
looked up with the game function 0x11f2e30).

Game functions:

| Image offset | Call | Meaning |
|---|---|---|
| 0x11f2e30 | (u32 id) → pointer or null | script symbol value lookup: pointer to the symbol's value |
| 0xf32080 | (char *buffer, s32 size, s32 value) | writes `value` as plain decimal text |
| 0xf31e60 | (char *buffer, s32 size, s32 value) | writes `value` with thousands separators |
| 0x62aa60 | (const char *format, ...) variadic | printf-style formatting into the static buffer at 0x1b676f0. It is a variadic function: call it through a variadic function type so that no vector-register count is announced (al = 0) |
| 0x76e460 | (s32 *request) | the game's own Records list source |
| 0x63dc40 | (ctx, a2, texture, vertices) | textured UI quad: four pointers / words exactly as the item draw passed them |
| 0x640ac0 | (out layout, item, text, size pair, 4 zero words, u32 0xffffffff), xmm0 = a float | lays an item out: fills `layout` (0x80 bytes) from an item description of 12 u32 |
| 0x641560 | (layout, a2, ctx, position pair, u32 colour, u32 flag), xmm0 = a float | draws a laid-out item |

The last two take one float in xmm0: use the `GameCall` block and `u3x_k26_call` of CONVENTIONS.h (x[0] = the
pattern, al = 1, nothing on the stack). The other float registers and al are not significant to these callees.

### 3.9 The XP table and the legacy scaling

The level thresholds are not in the game image and not in the helper: they are script data (net.bin), found at run
time as `u3x_sym_table(s, 0xc85e199d)`: s32 at data + 0x68 = number of levels, pointer at data + 0x70 = that many
s32 thresholds (threshold of level index 0 first, ascending). The PS4 game reads the same record the same way (for
example at image 0x6625cb / 0x6625d4).

"Table lookup" below always means: call `u3x_sym_table(s, 0xc85e199d)` again (no caching); a null result is the
"no table" case of the function.

Legacy scaling (PS3 1.19 only; the PS4 game has no scaling, so these two factors exist nowhere in the PS4 image):
scaled(v, legacy) = v for every legacy except 4 and 5; for legacy 4 the value is converted to single precision,
multiplied by 2.5 (0x40200000) in single precision and truncated toward zero to s32; for legacy 5 the same with 5.0
(0x40a00000). This is the existing `u3x_xp_scale`; this group never does the arithmetic itself, it goes through
`u3x_xp_scale`, `u3x_level_threshold` and `u3x_level_for_xp`.

### 3.10 Challenge definitions

`u3x_sym_table(s, 0xb630d01c)` (script data of PS3 1.19; absent data = no counting): s32 count, pointer at +8 to
entries of 0x28 bytes; entry + 8 = challenge number (s32, = stat number − 0x755), entry + 0x20 = a u32 mask that
must be non-zero for the challenge to count. Searched with `u3x_challenge_entry`.

### 3.11 Tables of game values: where the running game holds them

Two small sets of game values were written out in the old code. The new code must not contain them; it reads them
from the running game image. All facts below were checked against the PS4 image the builder patches (the builder
changes none of these bytes: at 0x662760 it replaces only the first 6 bytes of the function, 0x6625a0 is untouched).

**(a) Record defaults (8 words).** When the game resets a multiplayer record it stores eight u32 values into the
profile object. The game holds them as eight consecutive 12-byte store instructions starting at image offset
0x662812 (inside the game's current-level function 0x662760): entry k (k = 0 .. 7) is at 0x662812 + 12·k and
consists of 4 instruction bytes, then an s32 = the destination offset **from the profile object** (not from the
record), then a u32 = the value to store. (Both are unaligned 32-bit reads.) For orientation only, the destination
offsets are 0x260, 0x264, 0x26c, 0x320, 0x324, 0x32c, 0x784 and 0x1870: seven default item / appearance ids and one
float setting. The new code takes both offset and value of each entry from the image.

**(b) Demo / beta titles (3 texts, 2 words).** The game holds the three title ids as NUL-terminated texts of 9
characters at image offsets 0x145b9a4, 0x145b9ae and 0x145b9b8 (three consecutive 10-byte entries), and the two
content id words as 32-bit immediates of two compare instructions in its level-from-XP function: u32 at image
offset 0x66265a and u32 at image offset 0x662661 (unaligned).

Not tables of this group (all read at run time already, see above): the XP thresholds (3.9), the challenge
definitions (3.10), the Records rows (3.8).

## 4. Behaviour

### 4.1 `u3x_lg_rec`: the record getter with its lazy default

Returns `p + 0x248` in every case. Before that, when all three hold:

- the profile's load state (u32 at p + 0x7ae8) is 3,
- the multiplayer-active byte (image 0x17d5c61) is 0,
- the net error byte (image 0x17d5c80) is non-zero,

the record is reset: version word (p + 0x248) = 0x56; the 0x2824 bytes from p + 0x24c are cleared;
`u3x_sym_table(s, 0xc27df04c)` is called once and, when it returns data, the u32 at p + 0x74c (record + 0x504)
becomes the first u32 of that data; the eight default words of 3.11 (a) are stored at their offsets from `p`; the
load state becomes 2. The final memory is what counts; among the writes only this is fixed: the clear happens
before the other stores, and the lookup is called exactly once per reset.

Because the reset leaves the load state at 2, a second call does nothing. `p` is not checked for null.

### 4.2 `u3x_lg_beta`

1 when the title id text at image 0x17b9ca0 is exactly equal (same characters, same length) to one of the three
texts of 3.11 (b), or when the u32 at image 0x17b9d10 equals one of the two words of 3.11 (b); else 0. No game call.

### 4.3 `u3x_lg_thresh`

Table lookup; no table: 0. Otherwise, when `legacy` is 4 or 5, ea[30] += 1; the result is
`u3x_level_threshold(thresholds, count, level, legacy)`.

### 4.4 `u3x_lg_from_xp`

ea[29] += 1 and ea[31] = `legacy` (as u32), always. Table lookup; no table: 0. Otherwise the level is
`u3x_level_for_xp(thresholds, count, xp, legacy)`; when the title is a demo / beta title (4.2) and the level is
above 49, the result is 50; else the level.

### 4.5 `u3x_lg_cur_level`

ea[28] += 1. When the debug override (s32 at image 0x17d7abc) is non-zero the result is the override minus 1 and
nothing else happens. Otherwise the profile object of `user` is taken (`u3h_lg_profile`); null: 0. Otherwise:

- level XP = 0; when the load state is 3, the record getter (4.1) is applied and the level XP is the s32 at
  record + 0x500 read after it (so after a reset it is 0);
- legacy = s32 at record + 0x1618, read through the record getter, whatever the load state;
- result = `u3x_lg_from_xp(base, s, level XP, legacy)`.

"Box XP of a profile" in the next sections means: 0 when the load state is not 3; otherwise the record getter is
applied and the s32 at record + 0x4fc is read after it. "Legacy of a profile" means the s32 at record + 0x1618
read through the record getter. Every such read applies the getter again.

### 4.6 `u3x_legacy_kind`

Result words are stored as 64-bit values, zero-extended from the 32-bit answer unless said otherwise. Kinds other
than 60 to 65: return 0, nothing touched. The feature switch ea[1] is not tested here.

- **60 (current level).** ev[3] = `u3x_lg_cur_level(base, s, low 32 bits of ev[1])`. Return 1.
- **61 (level from XP).** legacy = low 32 bits of ev[2] as s32. When it is −1: the menu user's profile is taken;
  legacy = its legacy, or 0 when there is no profile. ev[3] = `u3x_lg_from_xp(base, s, low 32 bits of ev[1] as s32,
  legacy)`. Return 1.
- **62 (tier).** Answer 1, unless the multiplayer-active byte is non-zero; then the game function 0x11f2e30 is
  called with 0x6426ff70: a non-null result gives the u32 it points to as the answer; a null result gives 2 when
  `u3x_lg_cur_level(base, s, 0)` is below 10 and the u8 at image 0x17c3a45 is non-zero, else 1. The current level is
  evaluated only in that null case (it counts in ea[28]). ev[3] = answer. Return 1.
- **63 (XP gate).** user = low 32 bits of ev[1], amount = low 32 bits of ev[2] (unsigned). amount = 0: answer 1.
  Otherwise have = box XP of the user's profile as u32 (0 without a profile). amount > have (unsigned): answer 0.
  Otherwise level = `u3x_lg_cur_level(base, s, user)`, then a table lookup; no table: answer 1; with a table the
  **unscaled** threshold of the level (index limited to 0 .. count − 1 as in `u3x_level_threshold`, but the raw
  table value, no legacy scaling) is compared as u32: have below it gives 0, else 1. ev[3] = answer. Return 1.
- **64 (rank value of a profile).** ev[0] is a profile object pointer. Read through the record getter, in this
  order: level XP (record + 0x500, **without** a load-state test), legacy. level = `u3x_lg_from_xp(base, s, level
  XP, legacy)`. old star = 1 when the low five bits of the u8 at record + 0x1627 are all set, else 0. ev[3] =
  `u3x_rank_word(level, legacy as u32, u3x_rank_bits(record, old star))`. ea[35] += 1. Return 1.
- **65 (two thresholds).** legacy = the menu user's profile's legacy, 0 without a profile. level = low 32 bits of
  ev[0] as s32. ev[1] = `u3x_lg_thresh(s, level, legacy)`, then ev[2] = `u3x_lg_thresh(s, level + 1, legacy)` (each
  zero-extended from 32 bits; ev[3] is not written). ea[34] += 1. Return 1.

### 4.7 `u3x_lg_profile_box`

Returns 0 without any effect when: bit 0 of ea[1] is set; or the operation is neither 0xb nor 2; or, for operation
0xb, the value id (word 0x13) is not one of 0x4c4594db, 0xdaf38e1e, 0x771d7cb7; or, for operation 2, the sub index
(word 0x15) is not 0 or the text id (word 1) is not one of 0x38ece8ef, 0x05134917, 0xe607d83d; or the menu user has
no profile object.

Otherwise ea[32] += 1, level = `u3x_lg_cur_level(base, s, menu user)`, and with L = the profile's legacy (read
again for every threshold), T(n) = `u3x_lg_thresh(s, n, L)` and XP = the profile's box XP (read after the
thresholds of the same answer):

Operation 0xb, answer in word 0xe, return 1:

| Value id | Evaluated in this order | Answer |
|---|---|---|
| 0x4c4594db | T(level), T(level + 1), XP | `u3x_xp_percent(T(level), T(level + 1), XP)` |
| 0xdaf38e1e | T(level), XP | 1 when XP is below T(level) compared as **unsigned** 32-bit values, else 0 |
| 0x771d7cb7 | T(level + 1), XP | 1 when T(level + 1) − XP (32-bit wrapping difference, taken as signed) is below 1, else 0 |

Operation 2, return 1. First the label text is fetched: `u3h_text(base, s, 0x9f5e40e4)` (the "XP" label). A local
buffer of 0x20 bytes receives the number. Then:

| Text id | Steps | Text answer |
|---|---|---|
| 0x38ece8ef | T(level); 0xf32080(buffer, 0x20, T(level)); 0x62aa60("\|@Cffcece7c\|%s", buffer, label) | the static buffer at image 0x1b676f0 |
| 0x05134917 | T(level + 1); 0xf31e60(buffer, 0x20, T(level + 1)); 0x62aa60("\|@Cffcece7c\|%s %s", buffer, label) | the static buffer |
| 0xe607d83d | T(level + 1); XP; D = T(level + 1) − XP (32-bit wrapping, signed). D below 1: `u3h_text(base, s, 0x235a194e)` is the answer and nothing is formatted. Else 0xf31e60(buffer, 0x20, D); 0x62aa60("\|@Cffcece7c\|%s %s", buffer, label) | that text, or the static buffer |

The two format texts are "|@Cffcece7c|%s" and "|@Cffcece7c|%s %s" (a colour escape, then the number, then for the
second a space and the label). In the first row the label is still passed as a third argument although the format
does not use it. The text answer is stored with `u3h_set_text`.

### 4.8 `u3x_records_source`

1. Source descriptor (pointer at byte 0x98) null, or its tag (u32 at +0x1c) not 0x4c414ec7: return 0.
2. `u3h_gate_closed(base, event)` non-zero: return 1.
3. full = `u3x_n32_records(base)` (always evaluated here).
4. Only for operation 4 with image slot (word 0x14) 0, the Legacy row test:
   - lan = LAN byte non-zero and full is 0. Row table = image 0x13a65f0 with 3 rows when lan, else 0x13a6620 with
     5 rows. row = word 1 as u32; when full is non-zero, row 4 counts as no row and row 5 means table row 4.
   - When the row is inside the table and its action id is 0xa0bacf62: the menu user's profile object is taken and
     fresh = that user's "new item" flags AND the row's mask. With a profile and fresh = 0, the request ends here
     with **return 1**: legacy = the profile's legacy; a name buffer of 40 characters gets
     `u3x_legacy_back_name(buffer, buffer + 39, legacy)`; when native 20 exists it is called with (image
     0x1b31128, the name), whatever the legacy value is; when it returned a texture and legacy is 1 .. 5,
     `u3x_image_answer(event, texture)` and ea[33] += 1; otherwise the request is left exactly as it came.
   - In every other case (no such row, no profile, fresh non-zero) go on with step 5.
5. When full is non-zero and `u3x_n32_call(base, event, s, the game source 0x76e460)` returns non-zero: return 1.
6. Call the game source 0x76e460 with the request. Return 1.

### 4.9 `u3x_rank_bits`

Result bit 31 = 1 when the low five bits of the u32 at rec + 0x1624 are all set, else the low bit of `star_old`.
Result bit 30 = 1 when the u32 at rec + 0x1f1c is non-zero. All other bits 0.

### 4.10 `u3x_pm_challenge`

ea[57] += 1 first, always. Then, stopping at the first test that fails (in this order):

1. the profile object of `u` exists and its u8 at +0x7afc is 0;
2. `id` is within 0x755 .. 0x795;
3. the s32 at image 0x17d5c98 is non-zero, or (only evaluated when it is 0) `u3x_pm_coop(base, s)` is non-zero;
4. the u8 at image 0x17d1a84 is 0;
5. `u3x_sym_table(s, 0xb630d01c)` returns data;
6. `u3x_challenge_entry(data, id − 0x755)` returns an entry and its u32 at +0x20 is non-zero.

Then the counter is the u32 at record + 4·`id` + 8, the record taken with the record getter (4.1):

- `bit` = 0: counter += `v` (32-bit wrapping).
- `bit` ≠ 0: `u3x_pm_ngentry(base, s)` is called; null: stop here (no update counted). Otherwise n = the low six
  bits of the u32 at that record + 0xb8 (the map index); bit n of the counter is set when n is below 32, nothing is
  set for n = 32 .. 63.

After either update ea[57] += 0x10000.

### 4.11 `u3x_pm_365c5c`: the win challenge of the game type

T = `u3x_pm_type(base, s)` (called once, first). "Count n" = `u3x_pm_challenge(base, s, u, n, 1, 0)`.

| T | Effect |
|---|---|
| 2 (Plunder) | when `mine` = 5 and `theirs` = 0: count 0x776; then, always: count 0x768 |
| 0 | count 0x76f |
| 1 | count 0x77c |
| 0xf | count 0x780 |
| 10 | count 0x777 |
| 0x13 | the s32 at record + 0x1d78 of user `u` += 1, directly (record through `u3h_lg_profile` and the record getter; none of the tests of 4.10, the profile is not checked for null) |
| any other | nothing |

### 4.12 `u3x_pm_365da8`: co-op map counters and difficulty challenges

In this order:

1. `map` (unsigned) below 0x20: the s32 at record + 0x1630 + 4·`map` of user `u` += 1. `map` 0x20 .. 0x3f: the s32
   at record + 0x1cdc + 4·(`map` − 0x20) += 1. Larger: nothing. (Record as in the 0x13 row of 4.11.)
2. X = u32 at image 0x17d1aa8; T = `u3x_pm_type(base, s)` (called once, after step 1).
3. Only when `a3` is 0, with "count n" as in 4.11 and unsigned comparisons, in the listed order:
   - T = 0xb: X ≥ 2: count 0x75d; X ≥ 3: count 0x769; X ≥ 4: count 0x773.
   - T = 0x11: X ≥ 4: count 0x77b; X ≥ 3: count 0x76a.
   - T = 8: X ≥ 2: count 0x75e.
4. Only when X = 4 (whatever `a3` is): T = 0xb: `u3x_pm_challenge(base, s, u, 0x77f, 1, 1)`; T = 0x11:
   `u3x_pm_challenge(base, s, u, 0x771, 1, 1)` (bit mode: the map's bit).

### 4.13 `u3x_rank_tag`, `u3x_rank_back`, `u3x_texture_sharp`

"Find texture(name)" = native 20 called with (image 0x1b31128, name). The back name of a level is
"ranks/prestige-back-" followed by the one digit of the level (1 .. 5).

**`u3x_rank_tag`.** Nothing happens when bit 20 of eb[2] is set. From `v`: level = bits 16..29, star = bit 31,
paw = bit 30. Nothing happens when level is not 1 .. 5 and neither star nor paw is set, or when native 20 is null.
Otherwise, in this order:

1. level 1 .. 5: back[level] (3.7) = find texture(back name); when null, d[11] += 1.
2. paw: the paw texture pointer = find texture("ranks/ndpaw-icon"); when null, d[11] += 1.
3. star: the star value (3.7) = `u3h_icon_index(base, 0x0242a063)` + 1; d[10] = that value.
4. ev[6] = 0x4c303142, ev[7] = `v`; d[5] += 1; d[12] = `v`.

The answer is tagged even when a texture was not found.

**`u3x_rank_back`.** Null when `level` is not 1 .. 5 or native 20 is null (nothing counted). Otherwise find
texture(back name); ea[212] += 1; when there is no texture or its u8 at +0x10 is 0 (not loaded yet): null.
Otherwise ea[213] += 1, back[level] = the texture, and the texture is the result. The badge switch is not tested.

**`u3x_texture_sharp`.** Does nothing unless `t` is a plausible object pointer (`ptr_ok` of CONVENTIONS.h), the
u32 at t + 0 has bit 1 set, and the pointer m at t + 8 is plausible too. Then, with w = u32 at m + 0xc: when the
4-bit field in bits 16..19 of w is above 1, that field becomes 1 (other bits unchanged).

### 4.14 `u3x_rank_draw`

r[0] = draw context, r[1] = second argument of the quad, r[2] = texture, r[3] = vertices (four vertices of 0x40
bytes, position x at +0 and y at +4 of each, as float patterns; the block the game passes is 0x110 bytes),
r[4] = item layout, r[5] = colour (low 32 bits), r[6] = frame pointer of the game's item draw (may be 0).
"Quad(texture, vertices)" = game function 0x63dc40 called with (r[0], r[1], texture, vertices).

**Plain case.** When bit 20 of eb[2] is set, or the layout is not a plausible pointer, or its type (u32 at +0) is
not 2, or its u32 at +0x10 is not the marker 0x4c303142, or the vertices are not a plausible pointer: quad(r[2],
r[3]) once, return 1. Nothing else.

**Tagged badge.** d[6] += 1. v = u32 at layout + 0x14, level = bits 16..29 of v. Rectangle: x0, y0 = position of
vertex 0, x1, y1 = position of vertex 2 (bytes +0x80, +0x84). Centre: xm = (x0 + x1) · 0.5 and ym = (y0 + y1) · 0.5,
computed in single precision with two rounding points each: the sum is rounded to a 32-bit float, then the product
with 0.5 (0x3f000000) is rounded to a 32-bit float (`f_add`, then `f_mul`). q[0..3] = x0, y0, x1, y1. When r[2] is a
plausible pointer, its first 16 u32 are copied to t[0..15]. Then the layers, in this order:

1. **Back.** Only for level 1 .. 5: `u3x_rank_back(base, s, level)`; a texture: quad(that texture, r[3]) and
   d[7] += 1; null: ea[214] += 1.
2. **Rank art.** quad(r[2], r[3]), always.
3. **Co-op star.** Only when bit 31 of v is set and the star value (3.7) is above 0. An item description of 12
   u32, all 0 except: [0] = 3 (image item), [5] = 0x0242a063, [6] = 1, [7] = star value − 1, [9] = 0xffffffff,
   [10] = 1.0 (0x3f800000). A layout buffer of 0x80 bytes, zeroed. A size pair {x1 − xm, y1 − ym} (each one
   single-precision subtraction, `f_sub`), four zero u32, a position pair {xm, ym}. Game function 0x640ac0 is
   called with (layout buffer, item description, image 0x13ac250, size pair, the four zero words, 0xffffffff) and
   xmm0 = 64.0 (0x42800000). When afterwards the u64 at buffer + 0x48 and the u32 at buffer + 0x3c are both
   non-zero (as bit patterns): flag = the u32 at r[6] − 0x2080 when r[6] is non-zero, else 0; game function
   0x641560 is called with (layout buffer, r[1], r[0], position pair, low 32 bits of r[5], flag) and xmm0 = 0.0
   (pattern 0); d[8] += 1. Otherwise d[13] += 1. (Buffers may be 16-byte aligned; the game does not need more.)
4. **ND paw.** Only when bit 30 of v is set and the paw texture pointer (3.7) is non-null and its u8 at +0x10 is
   non-zero: the 0x110 bytes of the vertex block are copied to a local block, in which the four positions become
   the lower-left quarter: vertex 0 = (x0, ym), vertex 1 = (xm, ym), vertex 2 = (xm, y1), vertex 3 = (x0, y1)
   (x at +0, y at +4 of each 0x40-byte vertex; everything else as copied); quad(paw texture, the local block);
   d[9] += 1.

Return 1. The game's own vertex block is never modified.

## 5. Test scenarios

Fake image with the fields of 3.8, a state object with recording stand-ins for the lookup (offset 656) and native
20 (offset 928), recording stubs for the eight game functions, and the bytes of 3.11 placed at their image offsets.

Record and levels:
1. `u3x_lg_rec`: load state 3, active byte 0, error byte 1: version, clear, symbol copy (with and without symbol
   data), the eight words at the offsets the image entries give, state 2; second call changes nothing. Each of the
   three conditions failing alone: nothing written. Always returns p + 0x248.
2. `u3x_lg_beta`: title id equal to each of the three image texts; a title that is a prefix of one, or one of them
   with an extra character (0); each content word (1); none (0).
3. `u3x_lg_thresh`: no table (0, no ea[30] change); legacy 0, 3 (no count), 4 and 5 (ea[30] counts); level below 0
   and above the table.
4. `u3x_lg_from_xp`: no table (0, but ea[29] and ea[31] change); XP below the first threshold, inside a band, on a
   boundary, above the last; legacy 4 / 5 bands; beta title with levels 49, 50, 51 and a table of more than 51
   levels.
5. `u3x_lg_cur_level`: override 1 and 7; no profile array; state 3 (XP used) and state 2 (XP 0, legacy still
   read); state 3 with the reset condition true (level of XP 0, legacy 0 after the reset).
6. Kinds 60 to 65 and one other kind: 61 with legacy 3, 5 and −1 (with and without profile); 62 with the active
   byte 0, with the symbol present, with it absent and level 9 / 10 and the flag byte 0 / 1; 63 with amount 0,
   amount above / equal / below the box XP, XP below and above the unscaled threshold at legacy 5 (the scaled
   level is used for the index, the unscaled value for the comparison), no table; 64 with the top byte's low five
   bits set while the word's are not (star through the old bit) and the reverse, paw field 0 / non-zero; 65 with
   and without profile, checking ev[3] untouched.

Profile box:
7. Switch off; other operations; wrong ids; sub index 1; no profile: 0 and no effect.
8. Each of the three values: XP below T(level), between, equal to T(level + 1), top level (both thresholds equal:
   percent 0); XP "negative" for the unsigned comparison of 0xdaf38e1e.
9. Each of the three texts, including D ≤ 0 (the max text, no formatter call) and D = 1; check the order text
   lookup → thresholds → number function → formatter → text answer and the formatter's three arguments.

Records:
10. Wrong tag (0); gate closed (1, native not called); operation 2 (native); operation 4 slot 1 (native).
11. Operation 4 slot 0 in LAN mode row 2, online row 4, full mode row 5 (Legacy) and row 4 (not Legacy): with
    fresh bit set (native), no profile (native), legacy 0 / 3 / 6 with the texture found and not found, native 20
    null. Check the name passed and that the request is unchanged when no picture is answered.
12. full mode with `u3x_n32_call` answering 1 (native not called here) and 0.

Challenges:
13. `u3x_pm_challenge`: each of the six tests failing alone (only the low part of ea[57] moves); add mode with
    positive and negative `v`; bit mode with map index 0, 31, 32, 63 and without a net game record.
14. `u3x_pm_365c5c`: every row of the table; Plunder with 5:0, 5:1, 4:0.
15. `u3x_pm_365da8`: map 0, 0x1f, 0x20, 0x3f, 0x40; each type with X = 1 .. 5 and `a3` 0 and non-zero; order of the
    calls for type 0x11 with X = 4.

Badge:
16. `u3x_rank_bits`: the four combinations, `star_old` 0, 1 and 2.
17. `u3x_rank_tag`: switch off; level 0 without star and paw; native 20 null; level 3 found / not found; paw; star
    with icon index −1 and 7; level 6 with star only.
18. `u3x_rank_back`: level 0 and 6; native null; texture null; loaded byte 0 and 1.
19. `u3x_texture_sharp`: null, odd pointer, flag bit clear, inner pointer bad, field 0, 1, 2 and 15.
20. `u3x_rank_draw`: each condition of the plain case; tagged with level 0 and 5 (back loaded / not), star with star
    value 0 and above 0 and with the layout result empty / filled, r[6] zero / non-zero, paw with texture null,
    not loaded, loaded; rectangles whose sum is not exactly representable (rounding of xm, ym); the sequence and
    arguments of all game calls; q and t.

## 6. Things that look like bugs or dead code (behaviour to keep)

1. Kind 64 takes the old star bit from the byte at record + 0x1627 (the top byte of the little-endian word at
   +0x1624), which is the wrong end of the word; `u3x_rank_bits` then ORs the right test over it. Keep both.
2. Kind 63 compares against the unscaled threshold although the level comes from the scaled curve (as 1.19 does).
   With a table whose count is 0 or less it reads in front of the table (the index limit gives count − 1): never
   happens with real data, no test needed.
3. `u3x_lg_profile_box` passes the label as an unused third argument to the formatter for text 0x38ece8ef.
4. `u3x_lg_cur_level` reads the legacy (and applies the record getter) even when the profile is not loaded.
5. Kind 64 reads the level XP without the load-state test.
6. `u3x_records_source` looks the back texture up for any legacy value (also 0 or above 5; a miss queues a load)
   and, when no picture is answered, returns 1 with the request untouched (no icon answer is written).
7. `u3x_lg_profile_box` is the only function that tests the feature switch; kinds 60 to 65 and the Records list
   do not.
8. `u3x_pm_365c5c` type 0x13 and the map counters of `u3x_pm_365da8` write the record without any of the tests of
   `u3x_pm_challenge` and without a null test of the profile.
9. `u3x_pm_challenge` counts an update (ea[57] high part) in bit mode even when the map index is 32 .. 63 and no
   bit was set; in add mode `v` is used, in bit mode it is ignored.
10. `u3x_rank_tag` tags the answer even when the back or paw texture was not found, and looks the textures up on
    every answer. `u3x_rank_back` ignores the badge switch.
11. `u3x_rank_draw` stores the rectangle and a copy of the texture object's first 0x40 bytes on every tagged draw
    (diagnostics only).
12. The old C part has an unused function `lg_scale` (legacy scaling with the ea[30] count): dead, nothing to
    deliver.

## 7. Open questions

1. 3.11 relies on game code bytes (eight store instructions at 0x662812, two immediates at 0x66265a / 0x662661)
   and three texts at 0x145b9a4. They are as stated in the supported image; the builder does not verify these
   particular bytes today. Whether a builder-side check should be added is the maintainers' decision; the old
   helper had no run-time fallback and the new code needs none.
2. The stat numbers of 4.11 and 4.12 (which game type counts which challenge) are PS3 1.19 logic; the PS4 image
   has no counterpart, and the old code gives no rule that would generate them. They are given here as record
   field facts. Whether the challenge definitions (3.10) carry a field from which they could be derived is not
   known: only the entry fields at +8 and +0x20 are used anywhere.
3. The meaning of the s32 at image 0x17d5c98, the u8 at 0x17d1a84 and the u8 at 0x17c3a45 is not known beyond how
   they are tested here.

## 8. Clarifications (tester, 2026-11-14)

Facts confirmed by the differential test (`reimpl2\G22\test_g22.cpp`, old v7.212 against new) and, where the old C
source leaves the result open, by the machine code of the shipped v7.212 helper. They settle the implementer's
questions; item 1 corrects 4.7.

1. **4.7, value 0x771d7cb7 and text 0xe607d83d: the test is a comparison, not a wrapped difference.** The answer of
   value 0x771d7cb7 is 1 exactly when T(level + 1) ≤ XP, both taken as signed 32-bit values, else 0. For text
   0xe607d83d the "maximum" text (0x235a194e) is answered exactly when T(level + 1) ≤ XP (signed); otherwise the
   number given to 0xf31e60 is T(level + 1) − XP as a 32-bit wrapping difference. This is the same as "D below 1"
   whenever the exact difference fits in 32 signed bits. It differs when it does not: with T(level + 1) = 12000 and
   XP = −2147483643 the flag is 0 and the text shows the number −2147471653; with T(level + 1) = −2147483548 and
   XP = 2147483642 the flag is 1 and the maximum text is answered. (The old C statement subtracts two signed values,
   which is undefined in C when it overflows; the shipped helper compares the two values for the flag, and for the
   text branches on the signed "less or equal" result of the subtraction. Real profiles and tables never hold values
   that far apart.) Value 0xdaf38e1e is as 4.7 says (unsigned comparison XP below T(level)).
2. **4.6 kind 64: the record getter is applied five times.** For the level XP and for the legacy before the level
   evaluation, and three more times after it: for the legacy that goes into the rank word (read again), for the u8
   at record + 0x1627, and for the record given to `u3x_rank_bits`. The three later applications can only be told
   apart from reusing the first record when the reset condition of 4.1 becomes true during the level evaluation
   (a profile's load state changing to 3 while the script symbol lookup runs): then the reset (with its lookup of
   0xc27df04c) happens between the level evaluation and `u3x_rank_word`, and the rank word's legacy, the old star
   bit and the badge bits come from the reset record. No function called here changes a load state, so the game's
   own call sequence cannot show the difference.
3. **4.6 kind 62: the level is evaluated before the flag byte is tested.** With multiplayer active and the symbol
   0x6426ff70 absent, `u3x_lg_cur_level(base, s, 0)` is always called (ea[28] counts, its lookups and a possible
   record reset happen), also when the u8 at image 0x17c3a45 is 0.
4. **4.4: the title test has no observable moment.** The old code evaluated the demo / beta test after the level
   computation whenever a table exists (for every level), never without a table. The test only reads the image
   (title id at 0x17b9ca0, word at 0x17b9d10, the data of 3.11 b) and calls nothing, so evaluating it only for a level
   above 49 gives the same results, memory and calls.
5. **4.10 bit mode: the record getter comes before the net-game lookup.** The counter's record is taken through
   the getter (with a possible reset, including its lookup of 0xc27df04c) before `u3x_pm_ngentry` is called, also
   when that returns null; in that case the reset stays and no update is counted.
6. **4.13: `u3x_rank_tag` and `u3x_rank_back` do not call `u3x_legacy_back_name`.** They build "ranks/prestige-back-"
   plus the one digit themselves; `u3x_legacy_back_name` is called only by `u3x_records_source` (4.8), once per
   answered Legacy row, with the buffer end 39 characters after its start.
7. **4.7: the number buffer is not initialised.** The old code hands the 0x20-byte buffer to 0xf32080 / 0xf31e60 as
   it is on the stack; the number function is its only writer, and in the "maximum" case the buffer is not used at
   all. Writing a first NUL into it before the call changes nothing the game can see.
8. **3.8 / 4.14: the two calls with a float register.** The old code set only xmm0 (64.0 for 0x640ac0, 0.0 for
   0x641560), passed the colour as the low 32 bits of r[5] zero-extended and the flag zero-extended, and put nothing
   on the stack; al was not set on purpose (it held the low byte of the callee's address). A call block with x[0] = the pattern, x[1..4] = 0, no stack
   words and al = 1 is the same call for these two callees.
