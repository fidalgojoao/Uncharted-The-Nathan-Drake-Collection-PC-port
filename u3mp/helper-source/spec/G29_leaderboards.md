# G29. Leaderboards: manager set-up, page and friends queries, score submits, the widget hook

Behaviour description for a reimplementation (reimpl2 pipeline, describer's document). It says what the functions
do, not how the old ones were written. Conventions as in `G18_friends_list.md` and `G22_rank_xp.md`: offsets are
hexadecimal when prefixed with 0x; "image offset X" is the address `base + X`, where `base` is the game image base
every function receives; all multi-byte values are little-endian; u8 / u16 / u32 / u64 / s32 / s64 are integers of
that width, "pointer" is 8 bytes; 32-bit additions, subtractions and multiplications wrap. A "text" is a
NUL-terminated byte string; two texts are "equal" when they have the same bytes up to and including the NUL (no
length limit is applied by this group). There is no float code in this group.

## 1. Purpose and callers

The PS4 game kept the leaderboard page: the list widget, the manager object the widget reads its rows from, and
the callbacks that cycle through the four boards and switch between "everyone" and "friends". It lost the code
that fills the manager: the set-up at net start, the query the widget asks for, the score submit after a match and
all of the exchange with the leaderboard service. The helper owns that code. This group is the manager's side of
it:

- **set-up** of the manager object at net start;
- **the query**: the widget asks for a page of a board; the query runs as a small state machine, one step per
  helper tick, that asks the service for rows and polls the answer (the page that holds the player's own row, a
  given page, or the friends' rows sorted and cut into pages);
- **score submits**: up to five submits can be on their way at once; each tick frees the finished ones;
- **the widget hook**: the builder replaces the last instructions of the game's widget update by a code thunk
  (helper stub kind 66); the helper does what those instructions did and starts the query the PS4 game no longer
  starts;
- two small pieces in the C part that only the query uses (part C, strays): the ids of the friend list for the
  friends board, and the local answer when no leaderboard service is there.

The group does not talk to the service itself. It starts requests and polls their tickets through four function
slots of the C part's service table (3.5); what is behind the slots (nothing, or a backend) is not this group's
business and not described here.

Callers (all stay as they are; every C wrapper already forwards its arguments unchanged):

| Function | Called by | When |
|---|---|---|
| `u3x_lb_off` | `u3x_lb_init`, `u3x_lb_tick`, `u3x_lb_widget_tail`; `u3x_pm_lb_submits` (C++, group G30); C wrapper `lb_off` (no caller left) | the "leaderboards off" switch |
| `u3x_lb_week_board` | `u3x_lb_query`, `u3x_lb_submit` | board number of a query / submit |
| `u3x_lb_init` | C wrapper `lb_init119`, called by the C export `u3h_netinit_services` (second of its three services: friends, leaderboards, invites), itself called by the C++ net init | net start |
| `u3x_lb_query` | `u3x_lb_widget_tail` only | the widget wants a page |
| `u3x_lb_submit` | `u3x_pm_lb_submits` (C++, group G30: two or three calls per local user after a co-op or ranked match, only while the switch is not "off"); C wrapper `lb_submit` (no caller left) | after a match |
| `u3x_lb_net_error`, `u3x_lb_me` | `u3x_lb_tick` only | - |
| `u3x_lb_tick` | C wrapper `lb_tick119`, called by the helper's per-frame tick (`u3_profile_event`, hook kind 16, non-zero `base`), after the online tick and before the friends tick | every frame |
| `u3x_lb_widget_tail` | `u3x_lb_kind` only | - |
| `u3x_lb_kind` | C wrapper `lb_kind`, called by `u3_profile_event` for stub kinds 66 and 67, only with a non-zero `base` | 66: end of the leaderboard widget's update (3.6); 67: the game's post-match handler was entered |
| `u3x_lb_friend_ids` (part C, new name) | C `fr_lb_names`, used by the C export `u3h_fr_lb_names` (called by `u3x_lb_tick`) | friends board query |
| `u3x_lb_local` (part C, new name) | C `m02_lb_local`, used by the C export `u3h_m02_lb_local` (called by `u3x_lb_tick`) | start of every query |

Threading: every function runs on the thread that runs the helper's hooks (the per-frame tick and the widget
update thunk). The group starts no thread, takes no lock and uses no atomic operation; no function is re-entered.
The "busy" word of the manager (3.2, +0x58) is what is left of the PS3 worker thread: it is set when a query is
accepted and cleared when the state machine has finished, and the game's widget update does nothing while it is set.

## 2. Interface

All functions are `extern "C"`. No function takes or returns a float.

### 2.1 Functions to deliver

**Parts A and B, the manager** (existing C++ names; names and signatures stay, the C wrappers stay as they are):

| Function | Signature | Result |
|---|---|---|
| the switch (stray) | `int u3x_lb_off(void)` | 1 = leaderboards are off, 0 = on |
| board of the week (stray) | `uint32_t u3x_lb_week_board(void *s, uint32_t board)` | the board number to use |
| set-up | `void u3x_lb_init(uintptr_t base, void *s)` | - |
| start a query | `void u3x_lb_query(uintptr_t base, void *s, uint32_t board, int32_t page)` | - |
| submit a score | `int u3x_lb_submit(uintptr_t base, void *s, uint32_t board, int64_t score, const uint8_t *blob)` | 0 = a submit slot was taken, -1 = dropped (all five in use) |
| raise the net error (stray) | `void u3x_lb_net_error(uintptr_t base)` | - |
| "is this my row" (stray) | `int u3x_lb_me(uintptr_t base, const char *name)` | 1 / 0 |
| per-tick work | `void u3x_lb_tick(uintptr_t base, void *s)` | - |
| end of the widget update | `void u3x_lb_widget_tail(uintptr_t base, void *s, uint8_t *w, int32_t idx)` | - |
| stub kinds 66 / 67 | `int u3x_lb_kind(uintptr_t base, void *s, uint64_t *ev, int kind)` | 1 = kind handled, 0 = not one of the two |

The group is small enough for one implementer. If it is split, the natural cut is **A** = everything except
`u3x_lb_tick`, `u3x_lb_net_error`, `u3x_lb_me`, and **B** = those three; the two halves share only the private
variables of 3.9 (the work state and the five submit slots), which then need one shared declaration.

**Part C, two functions whose body is still in C** (strays, see section 7; independent of parts A and B). Each C
function becomes one statement that forwards:

| Old C function | New function | Signature | The C wrapper passes |
|---|---|---|---|
| `fr_lb_names(base, out, max)` | `u3x_lb_friend_ids` | `uint32_t u3x_lb_friend_ids(uintptr_t base, char *out, uint32_t max)` | `base, out, max`; returns the result |
| `m02_lb_local(base, s, m, board, req)` | `u3x_lb_local` | `int u3x_lb_local(uintptr_t base, void *s, uint8_t *m, uint32_t board, int32_t req, int backend, uint32_t *diag)` | `base, s, m, board, req`, then `backend` = 1 when the "rows of these players" slot of the service table (3.5, +0x20) is not the C part's offline stand-in, else 0 (only the C part can tell: the stand-in is a private C function), then `diag` = the C part's array of eight u32 diagnostic words `m02_d`; returns the result |

Parameters: `base` game image base; `s` the helper's state object (opaque: handed on to `u3x_sym_table`,
`u3h_m02_lb_local`, `u3h_pm119`, `u3h_lg_rec`; unused by `u3x_lb_init`); `board` a board number (3.7); `page` a
page number, or -1 for "the page that holds my own row"; `score` the score to submit; `blob` 0x40 bytes that travel
with the score (3.3; never null); `name` a text; `w` the leaderboard widget object (3.6); `idx` a board index;
`ev` the register block of a code thunk (3.6); `kind` the stub kind; `out` room for `max` ids of 0x24 bytes; `m`
the manager object (3.2); `req` the requested page.

File-private pieces of the old code that are free to shape in the new one: the work state and the submit slots
(3.9), the "equal texts" test, the accessors for the manager and for the service table.

### 2.2 Functions of other groups, of the C part and of the shared tools that the new code calls

Not to be described or reimplemented here. `u3h_` are exports of the C part.

| Name | Signature | What it does |
|---|---|---|
| `u3h_ext` | `uint8_t *u3h_ext(void)` | the helper's state block; CONVENTIONS.h has `ext_ea()` and `ext_eb()` for the words of 3.1 |
| `u3h_ona` | `void *u3h_ona(void)` | address of the C part's service table (3.5); never null, the same address on every call; the first call fills every slot with an offline stand-in |
| `u3h_lb_buf` | `uint8_t *u3h_lb_buf(int which)` | the C part's five static leaderboard buffers (3.4): 0 rows, 1 blobs, 2 ids, 3 temporary rows, 4 temporary blobs (any other value also gives 4) |
| `u3h_m02_lb_local` | `int u3h_m02_lb_local(uintptr_t base, void *s, uint8_t *m, uint32_t board, int32_t req)` | forwards to the C `m02_lb_local`, i.e. to part C's `u3x_lb_local` (4.12): 1 = the query was answered locally and is complete, 0 = not answered, nothing changed |
| `u3h_fr_lb_names` | `uint32_t u3h_fr_lb_names(uintptr_t base, char *out, uint32_t max)` | forwards to the C `fr_lb_names`, i.e. to part C's `u3x_lb_friend_ids` (4.11): writes the ids of the friend list (0x24 bytes each, at most `max`) to `out` and returns their number |
| `u3h_pm119` | `void u3h_pm119(uintptr_t base, void *s, uint8_t *param)` | forwards to `u3x_pm119` (group G30): the helper's replacement of the game's post-match handler; `param` is that handler's first argument |
| `u3x_sym_table` | `uint8_t *u3x_sym_table(void *s, uint32_t id)` | existing C++ function: the data of a script symbol, or null. Each call makes one call of the state's lookup function (the pointer at `s` + 656) when that pointer is not null; null when the function pointer, the returned handle or the pointer the handle holds is null |
| `u3h_fr_ent` (part C) | `uint8_t *u3h_fr_ent(uintptr_t base, uint32_t i)` | address of friend entry i: (pointer at the friend object +8) + 0x240 x i; no checks (G18) |
| `u3h_lg_profile` (part C) | `uint8_t *u3h_lg_profile(uintptr_t base, uint32_t user)` | the profile object of a local user, or null when the game has no profiles (G22) |
| `u3h_lg_rec` (part C) | `uint8_t *u3h_lg_rec(uintptr_t base, void *s, uint8_t *profile)` | the multiplayer record of a profile (G22); never null for a non-null profile. The first call can reset a just-loaded record to its defaults; it does not accept a null profile |

`u3h_ext`, `u3h_ona`, `u3h_lb_buf`, `u3h_fr_ent` and `u3h_lg_profile` are plain address accessors: how often and in
which order they are called is not part of the behaviour. Every other call of this table, every call through a slot
of the service table, every game call and every `u3x_sym_table` lookup is made exactly as often and in the order
section 4 says.

## 3. Game and helper facts

### 3.1 The helper's state block

"ea" words: u32 at `u3h_ext() + 0x3c50 + 4 x k` (`ext_ea()[k]`); "eb" words: u32 at `u3h_ext() + 0x4050 + 4 x k`
(`ext_eb()[k]`). Counters are plain u32 increments (wrap).

| Word | Offset | Meaning |
|---|---|---|
| ea[1] | 0x3c54 | switches; bit 4 (value 0x10) set = leaderboards are off |
| ea[48] | 0x3d10 | counter: set-ups |
| ea[49] | 0x3d14 | counter: queries accepted |
| ea[50] | 0x3d18 | counter: rows put into the rows buffer by the query (sum) |
| ea[51] | 0x3d1c | counter: submits that took a slot |
| ea[52] | 0x3d20 | counter: submits dropped |
| ea[53] | 0x3d24 | counter: net errors raised |
| ea[54] | 0x3d28 | counter: queries finished |
| ea[55] | 0x3d2c | counter: widget hook calls |
| ea[56] | 0x3d30 | counter: board numbers left without the week shift because the week symbol is missing |
| eb[2] | 0x4058 | switches; bit 12 (value 0x1000) set = no local answer (part C) |

ea[200] .. ea[211] (0x3f70 .. 0x3f9f) hold, in the v7.212 build, twelve words the builder writes: for each of the
four boards its number, its column set id and its title id. The old code reads the number and the column set from
there. The new code does not read these words (3.7).

### 3.2 The leaderboard manager (image offset 0x17d6230, "M" below)

An object of the game. The game's own code (the widget, its draw and the board callbacks) reads and writes the
fields marked "game"; this group must leave every field exactly as section 4 says.

| Offset | Type | Meaning |
|---|---|---|
| 0x00 | 0x24 bytes | the player's own id (text, zero padded) |
| 0x24 | s32 | friends board: number of rows the last friends query delivered; -1 = that query failed |
| 0x28 | pointer | rows shown by the widget: up to 11 rows of 0x80 bytes (3.3) (game reads) |
| 0x30 | pointer | their blobs: up to 11 x 0x40 bytes (game reads) |
| 0x38 | pointer | ids buffer |
| 0x40 | pointer | temporary rows |
| 0x48 | pointer | temporary blobs |
| 0x50 | u32 | a flag; only cleared at set-up |
| 0x58 | u64 | busy: non-zero while a query runs (game reads: the widget update returns at once while it is set) |
| 0x60 | u32 | board number of the running / last query (after the week shift) |
| 0x64 | s32 | requested page of the running / last query; -1 = "my page" |
| 0x68 | u64 | only cleared at set-up |
| 0x70 | u32 | number of rows in the rows buffer, 0 .. 11 (game reads; more than 10 means "there can be a next page") |
| 0x74 | s32 | the page the rows buffer shows (game reads; -1 after set-up) |
| 0x78 | u8 | "reset the widget" flag (set to 1 at set-up; the game's widget update clears it) |
| 0x7c | s32 | board index 0 .. 3 selected on the page (game writes) |
| 0x80 | u8 | non-zero = the friends board is selected (game writes) |
| 0x81 | u8 | "refresh" flag (game writes and clears; cleared at set-up) |
| 0x88 | 0x160 bytes | cleared at set-up (PS3 kept its submit slots here; the helper keeps them as private variables, 3.9) |

### 3.3 Rows, blobs and ids

A **row** (0x80 bytes, as the service delivers it and as the widget shows it):

| Offset | Type | Meaning |
|---|---|---|
| 0x00 | 0x5c bytes | the player's id (text) |
| 0x5c | u32 | position: from the service the position on the board counted from 0; in the rows buffer the position shown, counted from 1 |
| 0x60 | 0x10 bytes | not used by this group (moves with the row) |
| 0x70 | s64 | score |
| 0x78 | 8 bytes | not used by this group (moves with the row) |

A **blob** is 0x40 bytes that belong to a row (the values of the extra columns). Row k of a buffer and blob k of
the matching blob buffer always describe the same player. The blob a submit carries, and the blob of the local
answer of part C:

| Offset | Type | Meaning |
|---|---|---|
| 0x00 | u32 | the rank value of the profile (result of game function 0x781ea0) |
| 0x04 | s32 | sum of the record's s32 words at +0x1f30 and +0x1f28 |
| 0x08 | u32 | the record's word at +0x1c58 |
| 0x0c | u32 | the record's word at +0x1f24 |
| 0x10 | u32 | the record's word at +0x1eac |
| 0x14 .. 0x3f | | zero |

(Parts A and B never look into a blob: they copy and move blobs whole. Only part C builds one. Group G30 builds the
same blob for its submits.)

An **id** is a text in a field of 0x24 bytes; a list of n ids is n such fields one after the other.

### 3.4 The buffers of the C part (`u3h_lb_buf(which)`)

| which | Size | Use |
|---|---|---|
| 0 | 11 x 0x80 | rows the widget shows (M +0x28 points here after set-up) |
| 1 | 11 x 0x40 | their blobs (M +0x30) |
| 2 | 102 x 0x24 | ids sent with a "rows of these players" request (M +0x38) |
| 3 | 102 x 0x80 | temporary rows: answer of a "rows of these players" request (M +0x40) |
| 4 | 102 x 0x40 | temporary blobs (M +0x48) |

`u3x_lb_tick` takes the five addresses from `u3h_lb_buf`, never from the pointer fields of M (it also works when
the set-up has not run).

### 3.5 The service table (`u3h_ona()`)

A structure of the C part with function pointers. The slots this group uses (never null; called as ordinary C
functions):

| Offset | Signature | Meaning |
|---|---|---|
| 0x20 | `int32_t (*)(uintptr_t base, uint32_t board, const char *ids, uint32_t n, uint8_t *rows, uint8_t *blobs, uint32_t max)` | "rows of these players": starts a request for the rows of the `n` players of the id list on `board`. Result: a ticket (0 or more), or negative = not started. The service stores at most `max` rows and blobs into the two buffers given here; they are complete when the poll reports the answer |
| 0x28 | `int32_t (*)(uintptr_t base, uint32_t board, uint32_t first, uint32_t last, uint8_t *rows, uint8_t *blobs, uint32_t max)` | "rows of a range": the same for the board positions `first` .. `last` (counted from 0, both included) |
| 0x30 | `int32_t (*)(uintptr_t base, uint32_t board, const char *id, int64_t score, const uint8_t *blob64)` | "submit": starts the submit of `score` with its 0x40-byte blob for the player `id`; ticket or negative |
| 0x38 | `int32_t (*)(uintptr_t base, int32_t ticket)` | "poll": state of a ticket: -2 = still running; any other negative value = failed; otherwise done: the number of rows delivered (0 for a submit). A ticket is finished by the first answer other than -2 and must not be polled again |
| 0x48 | `uint32_t (*)(uintptr_t base, char *out, uint32_t max)` | "more friend ids": writes up to `max` ids (0x24 bytes each) of friends known to the service side to `out` and returns their number |

The other slots (+8, +0x10, +0x18, +0x40 and those after +0x48) are not used by this group. With the offline
stand-ins the three request slots return -1, the poll returns -1 and "more friend ids" returns 0.

### 3.6 The widget object, and the two code thunks

The leaderboard list widget `w` (an object of the game):

| Offset | Type | Meaning |
|---|---|---|
| 0x10 | u32 | column set id of the board shown |
| 0x1c | s32 | the page the widget asks for; -1 = "the page with my own row" (the game writes it before the hook runs) |
| 0x20 | s32 | board index the widget shows |
| 0x24 | u8 | non-zero = the widget shows the friends board (the game writes it before the hook runs) |

Stub kind 66. The game's widget update (image offset 0x63a310) compares the widget with the manager (board index,
friends flag, page step, refresh flag) and, when something has to be loaded, ends with two stores: the board index
into `w` +0x20 and the board's column set id (3.7) into `w` +0x10. PS3 started the query right there. The builder
replaces these last instructions (image offset 0x63a426) by a thunk that calls the helper; the function returns to
the widget's caller afterwards. The thunk hands over a block of four u64 words `ev`: word 0 = the widget object
(the update's first argument), word 1 = not meaningful, word 2 = the manager's board index (s32 at M +0x7c)
sign-extended, word 3 = 0 (it becomes the function's return value, which nobody uses; this group does not write
it). The thunk is reached only when the manager is not busy.

Stub kind 67. The builder replaces the entry of the game's post-match handler (image offset 0x678b00) by the same
kind of thunk; `ev` word 0 = the handler's first argument. The replacement itself is group G30's `u3x_pm119`.

Both thunks exist only in a build whose leaderboards are on (the builder sets ea[1] bit 4 exactly when it does not
plant them).

### 3.7 The four boards: where the game holds their values

The PS4 game holds the board table at **image offset 0x13a5f30**: 4 entries of 12 bytes, entry i (i = board index
0 .. 3) = { u32 the PS4 game's own board number (0x12c + i), u32 column set id, u32 title text id }. The replaced
instructions of the widget update read the column set id of entry i from there (u32 at image offset
0x13a5f30 + 12 x i + 4); the game's title getter and its board cycle still read the table themselves.

- **Column set id of board index i**: the u32 at image offset 0x13a5f30 + 12 x i + 4, read at run time. (The
  builder's words ea[201 + 3 x i] hold the same four values; checked against the game image.)
- **Board number of board index i**: 0x514 + i. These are the PS3 1.19 numbers, which the helper uses everywhere
  (the same four numbers appear in group G30's submits and in part C); the PS4 game does not hold them: its own
  table has 0x12c + i, exactly 1000 less. Index 0 = competitive kills, 1 = money, 2 = competitive wins, 3 = co-op.
- The title ids (ea[202 + 3 x i]) are read by nobody in the helper.

No table of these values is written into the new code.

**The week shift.** Script symbol 0x8466d21a (`u3x_sym_table(s, 0x8466d21a)`) is the week number: an s32 at offset
0 of its data. The PS4 game data does not have this symbol yet, so today the lookup answers null.

### 3.8 Other game data and game functions

| Image offset | Type | Meaning |
|---|---|---|
| 0x17d6010 | text, field of 0x24 bytes | the player's own network name (set by the net init just before the set-up of this group) |
| 0x17d5c80 | u8 | net error byte (the net state object 0x17d5c58 + 0x28): 1 = "cannot connect" |
| 0x17caeb0 (part C) | object | the friend object "NF" of G18 3.3: +0 u32 number of friends, +8 pointer to the friend entries (0x240 bytes each, id text at entry +1), +0xc8 u64 busy marker of the list fetch |

Game function (part C only), called with the game's normal calling convention through `game_call`:

| Image offset | Arguments | Result | Meaning |
|---|---|---|---|
| 0x781ea0 | (pointer: profile object) | u32 | the rank value of a profile (G22; in the patched game this enters the helper again through stub kind 64) |

The multiplayer record (part C; `u3h_lg_rec`, G22 3.5), s32 words: +0x1c54 the score of board 0x514, +0x1f38 the
score of board 0x515, +0x1f34 the score of board 0x516, +0x1f2c the score of board 0x517; +0x1f30, +0x1f28,
+0x1c58, +0x1f24, +0x1eac the blob values of 3.3.

### 3.9 File-private variables of the new code (all zero at start)

- **The work state** of the query: a step number (0 idle, 1 start, 2 waiting for my own position, 3 waiting for
  the page's rows, 4 waiting for the friends' rows), the ticket of the running request (s32) and the page being
  loaded (s32). "W.step", "W.ticket", "W.page" below.
- **Five submit slots**, each: an "in use" flag, the ticket (s32) and a copy of the 0x40-byte blob. The copy must
  live in the slot (static storage): its address is what the submit request gets.

(The old code also stores, without ever reading them, the number of ids of the last friends request and each
slot's board and score. They are not behaviour.)

## 4. Behaviour

Short names used below: **finish** = W.step becomes 0, the u64 at M +0x58 becomes 0, ea[54] += 1.
**Net error** = `u3x_lb_net_error(base)`. ROWS, BLOBS, IDS, TROWS, TBLOBS = the buffers 0 .. 4 of 3.4. "Slot
+0x20 / +0x28 / +0x30 / +0x38 / +0x48" = a call through that function pointer of the service table.

### 4.1 `u3x_lb_off()`

1 when bit 4 (0x10) of ea[1] is set, else 0.

### 4.2 `u3x_lb_week_board(s, board)`

When `board` is 0x513 or less (unsigned): the result is `board`; nothing else happens. Otherwise one lookup
`u3x_sym_table(s, 0x8466d21a)`: with data, the result is `board` + 4 x (the s32 at data +0), in 32-bit wrapping
arithmetic; without data (null), ea[56] += 1 and the result is `board`.

### 4.3 `u3x_lb_init(base, s)`

Nothing when leaderboards are off (4.1). Otherwise the manager and the private variables get these values (no
call other than the accessors is made, so the order of the stores is free):

- M +0x00 .. +0x23: all zero, then the own network name copied in: the bytes of the text at image offset 0x17d6010
  up to its NUL, but at most 16 bytes (the rest of the field stays zero);
- M +0x24 (u32) = 0;
- M +0x28, +0x30, +0x38, +0x40, +0x48 = the addresses of the buffers 0, 1, 2, 3, 4;
- M +0x50 (u32) = 0; M +0x58 (u64) = 0; M +0x60 (u32) = 0; M +0x64 (s32) = 0; M +0x68 (u64) = 0; M +0x70 (u32) = 0;
- M +0x74 (s32) = -1; M +0x78 (u8) = 1; M +0x7c (u32) = 0; M +0x80 (u8) = 0; M +0x81 (u8) = 0;
- M +0x88 .. +0x1e7 (0x160 bytes) = 0;
- every submit slot becomes free (its "in use" flag 0; a ticket still running is forgotten); W.step = 0;
- ea[48] += 1.

Bytes of M not listed (+0x54 .. +0x57, +0x79 .. +0x7b, +0x82 .. +0x87) are not written. `s` is not used.

### 4.4 `u3x_lb_query(base, s, board, page)`

When the u64 at M +0x58 is not 0: nothing at all (no lookup, no counter). Otherwise, in this order: B =
`u3x_lb_week_board(s, board)`; M +0x64 = `page`; M +0x60 = B; the u64 at M +0x58 = 1; W.step = 1; ea[49] += 1. The
switch of 4.1 is not tested here (the caller does). No request is started here: the next tick does that.

### 4.5 `u3x_lb_submit(base, s, board, score, blob)`

1. B = `u3x_lb_week_board(s, board)` (always, also when step 3 applies).
2. Take the free slot with the lowest number. Its blob copy = the 0x40 bytes at `blob`. Then one call of slot
   +0x30 with (`base`, B, the address of M (the own id text), `score`, the address of the slot's blob copy); its
   result is the slot's ticket, and the slot is in use exactly when the result is 0 or more. ea[51] += 1. Result 0
   (also when the service refused the submit: the slot then stays free).
3. No free slot: ea[52] += 1, result -1, no request.

The switch of 4.1 is not tested here (the caller does).

### 4.6 `u3x_lb_net_error(base)` and `u3x_lb_me(base, name)`

`u3x_lb_net_error`: the u8 at image offset 0x17d5c80 = 1; ea[53] += 1.

`u3x_lb_me`: 1 when the text `name` equals the own id text at M +0, else 0.

### 4.7 `u3x_lb_tick(base, s)`

Nothing when leaderboards are off (4.1): not even the submit slots are looked at. Otherwise:

**1. Submits.** For each submit slot in use, in slot order: one poll (slot +0x38 with `base` and the slot's
ticket); any answer other than -2 frees the slot.

**2.** When W.step is 0 the tick ends here. Otherwise let board = the u32 at M +0x60 and R = the s32 at M +0x64,
and continue with the step W.step names. A step that says "wait" ends the tick and changes nothing else.

**Step 1, start.** One call `u3h_m02_lb_local(base, s, M, board, R)`. A non-zero answer: finish (the local answer
has filled the manager). Zero: M +0x24 (u32) = 0, then

- *everyone's board* (the u8 at M +0x80 is 0): M +0x70 = 0.
  - R = -1: the 0x24 bytes of the own id (M +0) are copied to id 0 of IDS; W.ticket = slot +0x20 with (`base`,
    board, IDS, 1, TROWS, TBLOBS, 102). Ticket 0 or more: W.step = 2, wait. Negative: W.page = 0, net error, then
    the range request below in this same tick.
  - any other R: W.page = R, then the range request below in this same tick.
- *friends board* (the u8 at M +0x80 is not 0): the id list is built in IDS:
  1. n = `u3h_fr_lb_names(base, IDS, 101)`: the friend list's ids are ids 0 .. n-1.
  2. c = slot +0x48 with (`base`, a private buffer for 100 ids, 100); a c above 100 counts as 100. The ids of that
     buffer are gone through in order while n is 100 or less: an id that equals (as texts) one of the n ids
     already in IDS is skipped; any other is copied (its whole 0x24 bytes) to id n of IDS, and n grows by 1. Once
     n has passed 100 the remaining ids are ignored.
  3. The own id (0x24 bytes at M +0) is copied to id n of IDS, without a test for a duplicate. The list now has
     n + 1 ids (102 at most).
  4. W.ticket = slot +0x20 with (`base`, board, IDS, n + 1, TROWS, TBLOBS, 102). Ticket 0 or more: W.step = 4,
     wait. Negative: M +0x24 (s32) = -1, net error, finish. (M +0x70 and M +0x74 keep their values.)

**Step 2, my own position.** r = poll of W.ticket. -2: wait. Other negative: W.page = 0 and net error. 0: W.page =
0. Positive: W.page = (the position word, u32 at +0x5c, of row 0 of TROWS) / 11, an unsigned 32-bit division with
the quotient truncated. In all three cases the range request follows in this same tick.

**The range request.** M +0x74 = W.page; M +0x70 = 0; with F = W.page x 11 (as u32, wrapping): W.ticket = slot
+0x28 with (`base`, board, F, F + 10, ROWS, BLOBS, 11). Ticket 0 or more: W.step = 3, wait. Negative: finish (no
net error).

**Step 3, the page's rows.** r = poll of W.ticket. -2: wait. When r is positive: with k = r but at most 11, the
position word of row i of ROWS (i = 0 .. k-1) becomes W.page x 11 + i + 1 (as u32), M +0x70 = k, ea[50] += k. When
r is 0 or negative nothing is written (no net error either; M +0x70 stays 0). Then finish.

**Step 4, the friends' rows.** r = poll of W.ticket. -2: wait. Other negative: M +0x24 (s32) = -1, net error,
finish. Otherwise M +0x24 = r, and

- r = 0: M +0x70 = 0; finish. (M +0x74 keeps its value.)
- r positive (r rows in TROWS with their blobs in TBLOBS; r is not limited by this group, the service was told
  102):
  1. The position word of each of the r rows grows by 1.
  2. The r rows are put into ascending order of their position word (unsigned); rows with equal positions keep
     the order in which they were delivered (a stable sort). Each row's blob moves with its row, and whole rows
     (all 0x80 bytes) move. The sorted rows and blobs stay in TROWS and TBLOBS.
  3. "My index" = the index, in the sorted order, of the last row whose id text (row +0) equals the own id
     (`u3x_lb_me`); 0 when there is none.
  4. The page P: when R is -1, P = my index / 11 (integer division). Otherwise let L = the s32 at M +0x74, plus 1
     when the u32 at M +0x70 is above 10 (both as they are now, left by the query before this one); P = R when R
     is L or less (signed), else L.
  5. M +0x74 = P. The sorted rows whose index i is not below P x 11 (compared as u32, the product wrapping) are
     copied in order, with their blobs, to rows 0, 1, ... of ROWS and BLOBS, at most 11 of them; M +0x70 = the
     number copied (0 when there is none).
  6. ea[50] += that number. Finish.

**Any other value of W.step** (cannot occur): the tick ends, nothing changes.

Timing that follows from this: a query takes at least two ticks (start, answer). "My page" on everyone's board
takes three (my position; then, in the tick that brings its answer, the range request; then its answer). The
widget sees nothing of a running query (busy); when the busy word clears, its next update takes the page from
M +0x74.

### 4.8 `u3x_lb_widget_tail(base, s, w, idx)`

1. The s32 at `w` +0x20 = `idx`, as given.
2. i = `idx` when it is 0 .. 3, else 0.
3. When leaderboards are not off (4.1): `u3x_lb_query(base, s, 0x514 + i, the s32 at w +0x1c)`.
4. The u32 at `w` +0x10 = the column set id of board index i (3.7).
5. ea[55] += 1.

### 4.9 `u3x_lb_kind(base, s, ev, kind)`

- `kind` 66: `u3x_lb_widget_tail(base, s, w, idx)` with `w` = `ev` word 0 as a pointer and `idx` = the low 32
  bits of `ev` word 2 as s32; result 1.
- `kind` 67: `u3h_pm119(base, s, ev word 0 as a pointer)`; result 1. The switch of 4.1 is not tested.
- anything else: result 0, nothing happens.

`ev` is only read.

### 4.10 Part C: common

Both functions are called by `u3x_lb_tick` through the C exports of 2.2, so parts A / B and part C can be written
and tested apart.

### 4.11 `u3x_lb_friend_ids(base, out, max)` (stray, C `fr_lb_names`)

With NF = image offset 0x17caeb0: when the busy marker (u64 at NF +0xc8) is not 0 or the entries pointer (NF +8)
is null, the result is 0 and nothing is written. Otherwise n = the smaller of `max` and the number of friends (u32
at NF +0); for i = 0 .. n-1 the 0x24 bytes at friend entry i +1 (`u3h_fr_ent(base, i)` + 1) are copied to `out` +
0x24 x i; the result is n.

### 4.12 `u3x_lb_local(base, s, m, board, req, backend, diag)` (stray, C `m02_lb_local`)

The answer to a query when no leaderboard service is there: one row, the player's own. Result 0, with nothing
written and no call made beyond the profile accessor, when any of these holds, tested in this order: bit 12
(0x1000) of eb[2] is set; `backend` is not 0; p = `u3h_lg_profile(base, 0)` is null. Otherwise, in this order:

1. r = `u3h_lg_rec(base, s, p)`.
2. Row 0 of ROWS (`u3h_lb_buf(0)`): all 0x80 bytes zero, then the id = the 0x24 bytes at `m` +0, the position
   word = 1, the score = the record's s32 word of the board sign-extended to 64 bits: +0x1c54 for `board` 0x514,
   +0x1f38 for 0x515, +0x1f34 for 0x516, +0x1f2c for 0x517; 0 for any other `board` (the value read from the
   record of step 1).
3. Blob 0 of BLOBS (`u3h_lb_buf(1)`): all 0x40 bytes zero; then word +0 = the result of game function 0x781ea0
   with (p); then a second call r = `u3h_lg_rec(base, s, p)`, and from that record the words +4, +8, +0xc, +0x10
   of 3.3.
4. P = `req` when it is positive, else 0. The s32 at `m` +0x74 = P; the u32 at `m` +0x70 = 1 when P is 0, else 0
   (pages after the first are empty).
5. When the u8 at `m` +0x80 is not 0 (friends board): the s32 at `m` +0x24 = 1.
6. `diag[2]` += 1; `diag[3]` = `board`. Result 1.

## 5. Test scenarios

Everything can be tested without the game: a fake image (the manager at image offset 0x17d6230, the own name at
0x17d6010, the net error byte, the board table at 0x13a5f30, for part C the friend object and a profile with its
record), a fake state block for the ea / eb words, a fake state object whose lookup function (pointer at +656)
records its calls, a fake service table whose five slots record their arguments and answer from a script, and
recording stand-ins for `u3h_lb_buf` (five real buffers), `u3h_m02_lb_local`, `u3h_fr_lb_names`, `u3h_pm119`
(parts A / B) and `u3h_lg_profile`, `u3h_lg_rec`, game function 0x781ea0 (part C). Compare the manager, the five
buffers, the widget object, the ea words, results, and the order and arguments of the recorded calls. For the old
code the fake state block's ea[200..211] must hold the same board numbers and column sets as 3.7 gives (0x514 + i;
the image table's column sets); the title words are free. Only the look of the page needs the live game.

1. **Off** (ea[1] bit 4 set): `u3x_lb_init` and `u3x_lb_tick` change nothing and call nothing (a slot taken
   before stays in use); `u3x_lb_widget_tail` writes `w` +0x20 and +0x10 and counts ea[55] but starts no query;
   `u3x_lb_kind` 67 still calls `u3h_pm119`.
2. **Set-up**: a manager filled with 0xff bytes, own name of 0, 5, 16 and 20 characters (20: only 16 copied); all
   fields of 4.3, the untouched bytes still 0xff; slots free afterwards (five more submits are accepted); ea[48].
3. **Week board**: boards 0, 0x513 (no lookup), 0x514 with week data 0, 3 and -1, and without the symbol (ea[56]);
   a state object without lookup function.
4. **Query**: accepted (fields, W.step through the next tick's behaviour, ea[49]); a second query while busy does
   nothing and makes no lookup.
5. **Submit**: five submits take slots 0 .. 4 (arguments of slot +0x30: the manager address, the score, a copy of
   the blob), the sixth is dropped (ea[52], result -1, but the week lookup is still made); a refused submit
   (negative ticket) returns 0, counts ea[51] and leaves the slot free; ticks poll each used slot once per tick
   and free it on 0, 5 and -1 but not on -2.
6. **Idle tick**: W.step 0: only the slot polls.
7. **Local answer**: `u3h_m02_lb_local` answers 1: finish in the same tick, no service call, M +0x24 not written.
8. **My page, everyone's board** (R = -1): request with one id (the own id) and max 102; then poll -2 for some
   ticks; answer 1 with position 0, 10, 11, 25 (pages 0, 0, 1, 2); answer 0 (page 0); answer -1 (page 0, net
   error byte, ea[53]); in each case the range request in the same tick with first = page x 11, last = first + 10,
   max 11; then the rows: answers -2, 3, 11, 12 and 50 (cut to 11), 0, -1; position words page x 11 + i + 1;
   M +0x70, M +0x74, ea[50], ea[54], busy cleared.
9. **My page refused**: slot +0x20 answers -1: net error, range request for page 0 in the same tick.
10. **Given page** (R = 0, 4): range request at once; a refused range request: finish without net error, M +0x70
    = 0, M +0x74 = R. R = -2: first = 0xffffffea, last = 0xfffffff4.
11. **Friends board, id list**: friend list of 0, 3, 100 and 101 ids; "more friend ids" answering 0, some ids of
    which one is a duplicate of a friend and one a duplicate of an earlier "more" id, 100 and 150 (counts as 100);
    the list stops growing after 101 ids; the own id is appended even when it is already in the list; request with
    n + 1 ids, max 102.
12. **Friends board, request refused**: M +0x24 = -1, net error, finish; M +0x70 and M +0x74 unchanged.
13. **Friends board, answers**: -2; -1 (M +0x24 = -1, net error); 0 (M +0x24 = 0, M +0x70 = 0, page kept); rows
    in mixed order with equal positions (stable order, blobs follow, positions + 1, the 0x10 and 8 unused bytes of
    each row move with it); own id present once, twice (the last counts) and not at all; R = -1 with my index 0,
    10, 11, 30; R given with the old page / old count making L below, equal to and above R (old count 10 and 11);
    25 rows with P = 0, 1, 2 (11, 11 and 3 rows copied) and P = 3 (none); a negative P.
14. **Widget hook**: `idx` -1, 0, 3, 4 (raw value at `w` +0x20, entry 0 for the out-of-range ones); requested page
    taken from `w` +0x1c; busy manager (hook still writes the widget and counts, query ignored); `u3x_lb_kind`
    with kinds 66, 67, 65 and 68; `ev` word 2 with garbage in its high half.
15. **Part C, friend ids**: busy marker set; entries pointer null; 0 friends; more friends than `max`; `max` 0.
16. **Part C, local answer**: switch bit set; `backend` 1; no profile; each of the four boards and an unknown
    board (score 0); a negative score (sign extension); `req` -1, 0, 1, 5; friends flag set and clear; the two
    record calls around the game call; `diag[2]`, `diag[3]`.

## 6. Oddities of the old code that are behaviour to keep

1. `u3x_lb_submit` answers 0 and counts ea[51] also when the service refused the submit; the slot is then free
   again. It makes the week lookup even when no slot is free.
2. A tick with leaderboards off does not poll or free submit slots.
3. The set-up frees the slots and stops a running query without telling the service: such tickets are never
   polled.
4. The own id is copied at set-up only up to 16 characters, although the field has 0x24 bytes. "My index" and the
   submit use that copy.
5. Step 3 raises no net error for a failed answer, and a refused range request raises none either; only the "my
   position" request / answer and the friends request / answer do. A failed "my position" still loads page 0.
6. "My index" is the last matching row, and 0 when the player has no row (the first page is then shown).
7. The page limit L of the friends board uses the page and the row count of the query before; with R above L the
   page shown is L even when the new answer would have more rows.
8. The friends' rows are not limited to 102 by this group; the page's rows are limited to 11.
9. The ids of "more friend ids" are tested for duplicates, the own id is not: a player who is in the list already
   is sent twice.
10. The friends board does not clear M +0x70 when its query starts or fails; everyone's board clears it at the
    start.
11. `u3x_lb_widget_tail` stores an out-of-range board index as it is and uses board 0 for the query and the
    column set (the game's own instructions would have read past the table).
12. `u3x_lb_kind` 67 ignores the switch (in a build with leaderboards off the thunk does not exist).
13. Part C, local answer: the board it gets is the one after the week shift (M +0x60). With a week other than 0
    none of the four numbers matches and the score shown is 0. It writes the row and the blob also for a page
    after the first (the count is then 0).
14. The values stored but never read (3.9).

## 7. Strays, and what stays as it is

Strays covered here (none of them is in a work group of the inventory, or only in its "trivial" list):

- C++: `u3x_lb_off`, `u3x_lb_week_board`, `u3x_lb_net_error`, `u3x_lb_me`, and the file-private pieces behind the
  group (the work state, the submit slots, the row layout, the "equal texts" test, the manager and service table
  accessors).
- C: `fr_lb_names` (new entry `u3x_lb_friend_ids`) and `m02_lb_local` (new entry `u3x_lb_local`), hand-written C
  that only `u3x_lb_tick` uses. If the final sweep assigns them elsewhere, part C is dropped from this group
  without any change to parts A / B.

Stays as it is:

- the C wrappers `lb_off`, `lb_init119`, `lb_submit`, `lb_tick119`, `lb_kind` and the C exports of 2.2;
- the five buffers of the C part (3.4) and `u3h_lb_buf`;
- the service table and everything behind its slots (the offline stand-ins and the backends): C part, not this
  group;
- the two code thunks of the builder (3.6) and the switch bit;
- the game's own widget code, board callbacks, title getter and board table.

For the integrator: the new code no longer reads ea[200] .. ea[211]; the builder's twelve literal words can go
once this group is in (3.7).

## 8. Open questions

1. **Source of the board values** (3.7). The old code takes board number and column set from the builder's words
   ea[200..211]; this description takes the column set from the game's own table (as the replaced instructions
   did) and the board number from the rule 0x514 + index. In the shipped helper both give the same values in every
   state that can occur. They differ only where the builder did not write the words (a build with leaderboards
   off, in which the thunk does not exist): there the old code would store column set 0. To be confirmed by the
   owner; if the words are to stay the source, 4.8 steps 3 and 4 read ea[200 + 3 x i] and ea[201 + 3 x i] instead.
2. **The week symbol** 0x8466d21a is not in the PS4 game data: every query and submit counts ea[56] and uses week
   0. When the symbol is added, the local answer of part C stops matching the boards (section 6, item 13).
3. **The blob** (3.3) is built twice in the helper: by part C here and by `u3x_pm_lb_submits` (group G30) for
   the submits. G30's description should refer to 3.3; one shared function could build it.

## 9. Clarifications (tester, 2026-11-14)

Facts confirmed by the old-against-new test (`reimpl2\G29`). None of them changes sections 1 to 7; the test found
no difference between the old and the new code other than the planned one of open question 1.

1. **Open question 1, tested.** With ea[200 + 3 x i] = 0x514 + i and ea[201 + 3 x i] = the column set of the
   image table, the old and the new widget hook are equal in every case. They differ only when the words do not
   hold those values: the old hook then starts the query with the word's board number and stores the word's column
   set; the new one always uses 0x514 + i and the image table. With the words not written (all zero, a build with
   leaderboards off) the old hook stores column set 0, the new one the table's value. The title words
   ea[202 + 3 x i] are read by nobody.
2. **Tickets.** Every negative answer of a request slot is a refusal, -2 included; ticket 0 is a valid ticket and
   keeps the slot / the query waiting.
3. **Step 2.** A positive answer takes the position word of row 0 of TROWS whatever the number of rows answered.
   The division is unsigned: position 0xffffffff gives page 390451572, and the range request then gets first =
   0xfffffffc, last = 6 (the product and the sum wrap).
4. **Step 3.** Any answer above 11, up to 0x7fffffff, counts as 11; the answer 0x80000000 is negative (nothing
   written).
5. **Step 4, page limit.** L is the s32 at M +0x74 plus 0 or 1; R is compared with it as s32. A negative page
   (M +0x74 = -1 after set-up with fewer than 11 rows before, or a negative R) shows no rows: M +0x74 = that page,
   M +0x70 = 0. (With M +0x74 = 0x7fffffff and a count above 10 the old addition overflows; that case cannot occur
   and was not compared.)
6. **Step 4, more rows than asked for.** An answer above 102 is taken as it is: positions + 1, sort and "my
   index" run over all r rows, i.e. over memory behind the 102 rows of TROWS / TBLOBS. Old and new do the same
   (with a page that cannot be accessed behind the buffers both stop at the position word of row 102). The service
   must keep to the 102 it was told.
7. **Friends id list, a friend list function that answers more than 101.** The number is used as it is: the ids of
   "more friend ids" are then all ignored (n is above 100) and the own id is written to id n, behind the 102 ids of
   IDS, with n + 1 ids requested. Old and new do the same. `u3h_fr_lb_names` must keep to the maximum it is given.
8. **"More friend ids" that answers a larger number than it wrote.** The ids it did not write are read from the
   private buffer as it is. In the old code that buffer is on the stack (its content is not defined); in the new
   code it is a static buffer (zero at first, afterwards what earlier answers left). This is outside the slot's
   contract (3.5: the answer is the number written) and is not behaviour.
9. **Unknown step numbers** (5 and above): the tick still polls the submit slots and then changes nothing.
10. **`u3x_lb_tick` and the manager's pointer fields.** Confirmed: the tick works with null pointers at M +0x28 ..
    +0x48 (buffers from `u3h_lb_buf`).
11. **Week shift.** 4 x week wraps in 32 bits (week 0x40000001 gives + 4, week 0x80000000 gives + 0); a negative
    week moves the board down. Boards 0 .. 0x513 make no lookup. A state object without lookup function, a null
    handle and a handle that holds null all count as "symbol missing" (ea[56]).
12. **Part C, local answer.** Between the two record calls the game function may change the record: the score of
    the row is the value read after the first call, the four blob words +4 .. +0x10 are read after the second.
    When the second call answers another record address, the blob words come from that one.
