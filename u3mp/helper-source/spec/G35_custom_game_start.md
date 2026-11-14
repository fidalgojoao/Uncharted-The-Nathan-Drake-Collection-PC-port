# G35. Custom game start: checks, countdown, late join entry and gate, Elimination wait

Behaviour description for the clean-room pipeline (reimpl2, describer's document). It says what the functions do,
not how the old ones were written. Source of the facts: helper v7.212, the C functions `latejoin_closed119`,
`elim_wait_set`, `elim_round_end_hold`, `elim_wait_text` (agc_port.inc) and `u3h_ng_limits` (u3_lan_profiles.c), the
C++ functions `u3x_jip_enter`, `timed_popup`, `start_checks`, `start_game`, `prepare_start` and the stray `start_now`
with their private pieces (about 230 code lines), and all their callers.

Conventions of this document (as in `G12_packets_monitor.md`): "image offset X" is the address `base + X` (`base` =
game image base). Offsets with 0x are hexadecimal, others decimal. All values are little endian. "u8 / u16 / u32 /
u64 / s16 / s32" are integers of that width; "pointer" is 8 bytes. Clock values are 32 bits; their sums and
differences wrap modulo 2^32. Game functions use the System V x86-64 convention (integer and pointer arguments in
order, result in rax). "Native k" is the function pointer the helper's state object holds at 768 + 8·k (G23 3.2,
G10 3.4); any of them may be null.

No function of this group takes, returns or computes a float.

The group is small enough for one implementer; it can also be done as two independent parts:

| Part | Content | Exported names |
|---|---|---|
| **A** | the lobby's Start button: player limits, start checks, countdown popup, preparation, immediate start | `u3x_ng_limits` (new), `u3x_popup_timed`, `u3x_start_checks`, `u3x_start_game`, `u3x_start_prepare`, `u3x_start_game_now` |
| **B** | joining a running match (entry step), the late-join gate for Elimination, the Elimination "waiting for new players" hold and text | `u3x_jip_enter`, `u3x_latejoin_closed` (new), `u3x_elim_wait_set` (new), `u3x_elim_round_end_hold` (new), `u3x_elim_wait_text` (new) |

The parts share nothing but the facts of section 3.

## 1. Purpose and callers

The PS4 game lost the code behind the lobby's Start button for custom games, the client's entry into a match that
is already running, and three rules that PS3 1.19 added for the game type Elimination (net game type 10). The helper
owns that code.

**Part A.**

- `u3x_start_game`: the player pressed Start in the lobby. Caller: the C wrapper `start_game`, from the C
  dispatcher `u3_profile_event`, stub kind 9 (the helper's stub on the game's empty handler of the Start button,
  image 0x66edf0); the dispatcher returns 1.
- `u3x_start_checks`: is the party allowed to start the selected net game. Callers: `u3x_start_game`; the C wrapper
  `start_checks` (no caller left).
- `u3x_ng_limits`: the most and the fewest players of a net game type. Caller: `u3x_start_checks` only.
- `u3x_popup_timed`: shows the menu's timed popup. Callers: `u3x_start_game` (5 s countdown); the C wrapper
  `popup_timed`, from the two matchmaking start functions `onb_startmatch` and `m03_startmatch` (group G36; 2 s
  countdown with their own action ids). Those two are themselves called from `u3x_start_game` (through
  `u3h_onb_startmatch` / `u3h_m03_startmatch`), so `u3x_popup_timed` can run nested inside `u3x_start_game`.
- `u3x_start_prepare`: prepares the game for a match start. Callers: `u3x_start_game`, `u3x_start_game_now`; the C
  wrapper `start_prepare`, from the two C functions that make a party member follow its host into the match room
  (`fj_dir_tick` and `lnb_follow_tick`).
- `u3x_start_game_now`: starts without checks and countdown. Callers: `u3x_start_game` (countdown switched off);
  the C wrapper `start_game_now`, from the matchmaking ticks `m05_tick` and `m03_tick` (G36) when a matchmade game
  starts.

**Part B.**

- `u3x_jip_enter`: a party member whose host is already playing has joined the host's match room; this is the
  step that puts the game into its late-join flow. Caller: the C wrapper `jip_enter`, from `lnb_follow_tick`, once,
  when the helper's "join in progress" flag is set, the game's net state is 0x12 and the match session has a room.
- `u3x_latejoin_closed`: the helper's value stub of kind 100 sits on the entry of the game's "is late join
  closed?" function (image 0x7462b0; argument: the net game manager). The C dispatcher calls the C function
  `latejoin_closed119(base, s, r)` only when `base` and the register block are non-null and returns its result: 1
  = the game function returns the value the helper stored in the block, 0 = the game's own body runs. PS3 1.19
  changed that function for Elimination only; the helper answers Elimination and leaves every other type to the
  game.
- `u3x_elim_round_end_hold`: value stub of kind 101 in place of the game's call of its "send round end" function
  (image 0x6bcfc0) at image 0x7259af, inside the Elimination round check of the net task. Dispatcher as for kind
  100 (C function `elim_round_end_hold(base, s, r)`): result 1 = the call is left out and its caller sees the
  stored value, 0 = the game's call is made.
- `u3x_elim_wait_text`: stub of kind 102 in the game's net task (image 0x72317a), once per run of the net task.
  The dispatcher calls the C function `elim_wait_text(base)` when `base` is non-null and returns 1.
- `u3x_elim_wait_set`: sets the "waiting for new players" state. Callers: `u3x_elim_round_end_hold` and
  `u3x_elim_wait_text` only.

Threading: everything runs on the game thread that runs the helper's stubs (menu handler, net task). No thread is
started, no lock is taken.

## 2. Interface

All functions are `extern "C"`.

### 2.1 Functions to deliver

| Function | C++ signature | Status | The C wrapper passes |
|---|---|---|---|
| player limits | `void u3x_ng_limits(uintptr_t base, int32_t type, int32_t *most, int32_t *fewest)` | new; replaces the C export `u3h_ng_limits(type, mx, mn)` | no wrapper: see 2.3 |
| timed popup | `int u3x_popup_timed(uintptr_t base, void *s, uint32_t text, int seconds, uint32_t ok, uint32_t cancel)` | existing name | unchanged (`popup_timed`) |
| start checks | `int u3x_start_checks(uintptr_t base, void *s)` | existing name | unchanged (`start_checks`) |
| Start pressed | `void u3x_start_game(uintptr_t base, void *s)` | existing name | unchanged (`start_game`) |
| preparation | `void u3x_start_prepare(uintptr_t base, void *s)` | existing name | unchanged (`start_prepare`) |
| immediate start | `void u3x_start_game_now(uintptr_t base, void *s)` | existing name (stray, in no inventory list) | unchanged (`start_game_now`) |
| late join entry | `void u3x_jip_enter(uintptr_t base)` | existing name | unchanged (`jip_enter`) |
| late-join gate | `int u3x_latejoin_closed(uintptr_t base, void *s, uint64_t *r)` | new (C `latejoin_closed119`) | its three arguments unchanged; returns the result |
| wait set | `void u3x_elim_wait_set(uintptr_t base)` | new (C `elim_wait_set`) | `base` |
| round end hold | `int u3x_elim_round_end_hold(uintptr_t base, uint64_t *r)` | new (C `elim_round_end_hold`) | `base` and the register block (its state argument is not used); returns the result |
| wait text | `void u3x_elim_wait_text(uintptr_t base)` | new (C `elim_wait_text`) | `base` |

Parameters and results:

- `base`: game image base. `s`: the helper's state object (3.6); never null.
- `type`: a net game type number, any value. `most`, `fewest`: where the two limits are stored; never null.
- `text`: text id of the popup. `seconds`: how long the popup stays (taken as an unsigned 32-bit number). `ok`,
  `cancel`: the action ids the menu runs when the popup's time is over / when the player backs out. Result of
  `u3x_popup_timed`: 0 or the low 8 bits of what the game's show-page function returned (non-zero = shown).
- `u3x_start_checks`: 1 = the party may start, 0 = refused (a dialog was opened).
- `r`: the stub's block of nine saved registers, 8 bytes each, in the order r11, r10, r9, r8, rdi, rsi, rdx, rcx,
  rax (as in G11 2.1). `r[4]` (rdi) is the interrupted game function's first argument, `r[8]` (rax) is the value
  the stub hands back when the function returns 1. Never null.
- `u3x_latejoin_closed`, `u3x_elim_round_end_hold`: 1 = the helper answered (the value is in `r[8]`), 0 = the game
  goes on by itself.

Calls between the functions of one part may be direct; nothing outside the part looks at them.

### 2.2 Functions the new code calls (declare them; do not reimplement them)

| Name | Signature | What it does |
|---|---|---|
| `u3h_ext` | `uint8_t *u3h_ext(void)` | the helper's own state block (3.7). In CONVENTIONS.h |
| `u3h_mp_code_is` | `int u3h_mp_code_is(uintptr_t base, uint32_t off, const char *hex12)` | 1 when the 12 bytes at image offset `off` equal the 24 lower-case hex digits of `hex12`, else 0; reads only |
| `u3h_show_dialog` | `void u3h_show_dialog(uintptr_t base, void *s, uint32_t body, uint32_t title, uint32_t action, int32_t buttons, uint32_t extra, uint32_t close)` | opens the game's message dialog with these text and action ids |
| `u3h_cg_log` | `void u3h_cg_log(const char *text, int32_t value)` | writes the log line "custom game: " + `text` (+ `value` in decimal when it is 0 or more) |
| `u3h_fj_note` | `void u3h_fj_note(const char *before, const uint8_t *id, const char *after, int32_t value)` | writes a log line: `before`, the name at `id` (skipped when null), `after` (skipped when null), `value` in decimal when it is 0 or more |
| `u3h_jip_started` | `void u3h_jip_started(void)` | tells the C part that the late-join flow has begun (it clears its "join in progress" request and starts watching the late-join states) |
| `u3h_pt_state` | `uint32_t *u3h_pt_state(void)` | the five u32 words the C part keeps for the timed popup (3.8) |
| `u3h_cg_t0_ptr` | `uint32_t *u3h_cg_t0_ptr(void)` | address of the C part's "custom game start pressed at" clock value (0 = not watching) |
| `u3h_onb_startmatch` | `int u3h_onb_startmatch(uintptr_t base, void *s)` | G36: the Start press of the online Find Game flow; 1 = it handled the press, 0 = not its case |
| `u3h_m03_startmatch` | `int u3h_m03_startmatch(uintptr_t base, void *s)` | G36: the same for the offline matchmaking stand-in; 1 = handled |
| `u3h_cg_commit_selection` | `void u3h_cg_commit_selection(uintptr_t base, void *s)` | makes the net game chosen in the lobby the match's net game (only in net menu mode 2; it may call native 2 and write log lines) |
| `u3h_start_calls_inc` | `void u3h_start_calls_inc(void *s)` | adds 1 to the probe counter "start calls" |
| `u3h_probe_pad2_inc` | `void u3h_probe_pad2_inc(void *s)` | adds 1 to another probe counter |
| `u3h_elim_received` | `volatile uint8_t *u3h_elim_received(void)` | address of the helper's flag "the host announced an Elimination wait" (set by the LAN room client, G08, message 0xd3) |
| `u3h_elim_send` | `volatile uint8_t *u3h_elim_send(void)` | **to add (integrator)**: address of the C part's flag `lnb_elim_tx`, "announce the Elimination wait to the party". The C part's follow tick sends message 0xd3 (G08 / G09) and clears the flag |
| `u3x_netgame_type` | `int32_t u3x_netgame_type(uintptr_t base, void *s)` | existing: the type (s32 at +0xc) of the net game the match uses (entry number = s16 at manager + 0x661c of the game's net game table), 0 when there is no such entry. Calls the state object's lookup function. (`u3h_netgame_type` is the C export that forwards to it; either may be called.) |
| `u3x_lan_party_size` | `int u3x_lan_party_size(uintptr_t base)` | G10 4.10: number of players in the party (1, or 2 in split-screen, without a party room) |
| `u3x_lan_reattach` | `void u3x_lan_reattach(uintptr_t base)` | existing one-liner: when the LAN backend object exists, stores its address as the match session's backend (session + 8) |
| `u3x_mp_lan_or_online` | `int u3x_mp_lan_or_online(uintptr_t base)` | existing: non-zero when the LAN byte (image 0x17d5c60) is set or the online session manager exists (it asks `u3h_onb_mgr(base)` only when the byte is 0) |
| `u3x_party_guest` | `int u3x_party_guest(const uint8_t *party)` | existing, pure read: 1 when the session has a room (u64 at +0x18 not 0) and our member id (+0x12e4) differs from the host's (+0x12e8) |
| `u3x_round_mostly_over` | `int u3x_round_mostly_over(int32_t limit, int32_t elapsed)` | existing, pure: 0 when `limit` is 0 or less; otherwise 1 when four times (`limit` − `elapsed`, a wrapping 32-bit difference taken as signed; the product in 64 bits) is below `limit`, i.e. less than a quarter of the time is left |
| `u3x_match_nearly_decided` | `int u3x_match_nearly_decided(int32_t lead_wins, int32_t rounds, int32_t round)` | existing, pure: 1 when `lead_wins` is above 1 or `round` is at least `rounds` − 1 |
| `u3x_waiting_joiner` | `int u3x_waiting_joiner(const uint8_t *net_player)` | existing, pure read: non-zero for a net player that is in the game (u8 +0xe1 set, u8 +0x5e4 clear), has joined late (u8 +0x5f9 set) and has not spawned in this round yet (u8 +0x5fa clear) |

### 2.3 What changes in the C part (integrator)

- `latejoin_closed119`, `elim_wait_set`, `elim_round_end_hold`, `elim_wait_text` become one-line wrappers (2.1).
- New export `u3h_elim_send` (2.2).
- `u3h_ng_limits` has no `base` argument and its only caller is `u3x_start_checks`, which now calls
  `u3x_ng_limits`. The export and the two literal tables `ng_max_players` / `ng_min_players` are deleted (section
  6).

## 3. Game and helper facts

### 3.1 Clocks and net state

| Image offset | Type | Meaning |
|---|---|---|
| 0x17b4ba4 | u32 / s32 | the game clock (0x17b4a90 + 0x114), 300 ticks per second; "game clock" below |
| 0x17b4ca4 | s32 | the net clock, 300 ticks per second (the time base of match and round times); "net clock" below |
| 0x17d5c64 | s32 | start time of the running match or round, net ticks (G11) |
| 0x17d5c61 | u8 | net-active flag |
| 0x17d603c | u32 | net menu mode; 2 = the custom game / party flow |
| 0x1b68810 | u32 | the game's net state. Values named here: 3 and 0xb = the network / session is starting; 0x12 = idle in the lobby flow; 0x31 = client late join, first state |
| 0x1b68814 | u32 | game clock value at which the net state was entered |
| 0x1b69530 | u32 | the first word of the game's late-join status array (only printed) |
| 0x16e7170 | u8 | "abort the connection states" flag |
| 0x17d5c80 | u8 | net error flag |
| 0x17c3580 | pointer | the net clock object in use (G11), or null |
| 0x17d167c | s16 | number of the net game selected in the lobby |

The net object at image 0x17d5c58: s32 at +0x2c = a deadline (game clock) for the current net step; u32 at +0x408
= the net step.

### 3.2 Net game manager (image 0x17cb060)

| Offset | Type | Meaning |
|---|---|---|
| 0x354 | u8 | "waiting for new players" (Elimination); the game clears it at each round start |
| 0x358 | s32 | deadline of that wait, net ticks |
| 0x390 + i·0x9c0, i = 0..9 | record | the 10 net player records (fields used: see `u3x_waiting_joiner`, 2.2) |
| 0x661c | s16 | number of the net game the match uses |
| 0x66fc | s32 | current round |
| 0x6a44 | s32 | number of rounds |

Team records: the s16 at image 0x17e338e + t·0x130 is the number of rounds team t has won (t = 0, 1, ...; the
record size is 0x130).

### 3.3 Sessions

Match session: image 0x17d6420. Party session: image 0x17dc298. Layout as in G12 3.2 / G10 3.2; used here:

| Offset | Type | Meaning |
|---|---|---|
| 8 | pointer | backend object (null = none) |
| 0x18 | u64 | room id (0 = no room) |
| 0x680 + i·0x120, i = 0..9 | member slot | u8 at 0x25 = in use; u32 at 0x2c = member id; u32 at 0x3c = length of the member's data; the data at 0x40 |
| 0x12e4, 0x12e8 | u32 | own member id, host's member id |
| 0x12f0 | u32 | length of the own member data |
| 0x12f4 | 0x40 bytes | the own member data |
| 0x1338 | u8 | match session: the "start loading" flag (G14) |

Member data (0x40 bytes): the u8 at offset 9 is the player's team (1..4; other values = no team).

### 3.4 The menu (image 0x17bd080)

| Offset | Type | Meaning |
|---|---|---|
| 0x898 | pointer | the popup that is up, or null; the u32 at +0x10 of it is its page id (`u3x_popup_id`, G31) |
| 0x14f0 | pointer | the data object of the timed popup, or null: u32 at +8 text id, u32 at +0x10 start, u32 at +0x14 end (game clock) |
| 0x1544 | u32 | action id the menu runs when the timed popup's time is over |
| 0x1548 | u32 | action id the menu runs when the player backs out of it |
| 0x269a | u8 | "the countdown was cancelled" |

Ids: timed popup page 0x453470dd; late-join page 0x69e10dad; countdown text 0x73a73dd8, with the actions
0xe2707dbe (countdown done) and 0xbf6ce4d5 (cancelled); dialog title 0xc6467f2d and close action 0xe1e040aa; the
three refusals (body text, action): too many players (0x4a936499, 0x13cabc15), too few players (0x4a93e6b9,
0x13cafe1f), teams (0x101c7f10, 0x5350146e); the Elimination wait text 0x02277bd7.

### 3.5 Game functions and data called or read by address

| Image offset | Arguments | Result | Meaning |
|---|---|---|---|
| 0x74ba60 | none | none | the game's client set-up for a late join |
| 0x72b570 | (pointer: net game manager, int 1, int 1, int 0, int 0, int 0) | ignored | the match set-up of the net game manager |
| 0x651940 | (pointer: the menu, u32 page id, int 0, int 0) | ignored | show a menu page |
| 0x726b00 | (pointer to an s32 result, pointer: net game manager) | ignored | stores the time limit of the round in net ticks, 0 when there is none. Give it 8 zeroed bytes |
| 0x737eb0 | (pointer: net game manager) | s32 | number of the leading team, −1 when the lead is tied |
| 0xf32ae0 | (address of image 0x17026b8 = the text table object, u32 text id) | pointer | the text, or null. The pointer stored at image 0x17026b8 + 0x10 is the table's stand-in text (G04) |
| 0x139c740 | (pointer destination, pointer source, u64 count) | ignored | the C library's bounded string copy (copies up to `count` bytes and fills the rest of the `count` bytes with zeros) |
| 0x13a61e0 | data | 20 × s32 | **the most players per net game type** (index = type 0..19), section 6 |

Four of them are called only after `u3h_mp_code_is` confirms their first 12 bytes (the texts must be kept in
`U3X_RO` character arrays):

| Image offset | Expected bytes as text |
|---|---|
| 0x726b00 | `554889e54157415641545349` |
| 0x737eb0 | `554889e54157415641545348` |
| 0xf32ae0 | `554889e541565389f34989fe` |
| 0x139c740 | `ff25826c3400681a000000e9` |

0x74ba60, 0x72b570 and 0x651940 are called without a check.

The HUD: the object at image 0x1805fa0. The pointer at +0x238 of it is its element block (may be null); the
message element is at +0x25f0 of that block: text (0x80 bytes, a first byte of 0 = no message up), u32 at +0x80
start time (game clock), u32 at +0x84 colour, u32 at +0x88, u32 at +0x8c, u8 at +0x90. Two pointers name "the HUD
in use": image 0x1806480 and image 0x1b24bd8.

### 3.6 The state object `s`

| Offset | Type | Meaning |
|---|---|---|
| 648 | pointer to function | show-page: (pointer: the menu, u32 page id, int 0, int silent) → the low 8 bits non-zero = shown. May be null |
| 824 | pointer to function | native 7: the game's "countdown done": no arguments; starts the match |
| 864 | pointer to function | native 12: the game's session initialisation: (pointer: a session); result ignored |
| 872 | pointer to function | native 13: symbol lookup: (u32 symbol id) → pointer to the symbol's table, or null |
| 0x2000 | object | the helper's own net clock object: u64 at +0xc, u32 at +0x20, u32 at +0x28 are reset in 4.5 |
| 0x20c0 | u32 | switch word; bit 2 (value 4) = "teams are not known" (U3MP_TEAMCHK_GUESS) |
| 0x3d78 | u32 | lobby switches: 8 = keep the kill-volume level; 0x40 = no countdown; 0x80 = a lone player may start; 0x20000 = that also holds for type 18; 0x80000 = no minimum; 0x20000000 = no reset at start |

The map list is the table of symbol 0x0dec97a4: u32 count at +0, pointer to the entries at +8; an entry is 0x98
bytes: pointer to the name of the map's kill-volume level at +0x38 (may be null), u32 at +0x40 = that level's id
(0 = the game does not load it).

### 3.7 ExtState (address from `u3h_ext()`)

| Offset | Words | Meaning |
|---|---|---|
| 0x2238 | cd[0..7], u32 | start counters: [0] Start presses, [1] / [2] start time and text of the last timed popup, [3] party told, [4] countdown shown, [5] countdown not shown (started at once), [6] result of the last checks (4.3), [7] their inputs |
| 0x2ff0 + 4 | sc[1], u32 | minimum checks skipped by the "no minimum" switch |
| 0x2070 + 44 | oob[11], u32 | 1 = the kill-volume level is kept |
| 0x4450 | ec[0..255], u32 | ec[0] switches: bit 1 (value 2) = the Elimination late-join rule is off; bit 2 (value 4) = the Elimination wait is off. ec[18] gate calls, ec[19] gate answers, ec[20] last answer, ec[21] last leading team, ec[22] its round wins, ec[23] round and rounds; ec[24] hold calls, ec[25] round ends held, ec[26] waits set, ec[27] texts shown |

### 3.8 The timed popup's words (`u3h_pt_state()`)

Five u32: [0] "taken over" (the menu's two action ids are ours), [1] and [2] the menu's own two action ids as they
were before, [3] the ok id we set, [4] "the popup has been seen up". The C part's `pt_tick` (stays) puts the
menu's ids back once the popup has been up and is gone, and clears [0].

### 3.9 Own data of the part

Part A keeps one u32 of its own, 0 when the helper is loaded: **P**, the game clock value of the last start this
function issued (never 0 once set).

Texts (each in a `U3X_RO` array, exactly as written, no line end):

- L1 `Start pressed, net mode `
- L2 `only the party leader can start (this PC is a party member)`
- L3 `already starting, net state `
- L4 `a start check refused it (2 party too big, 3 too few players, 4 teams): `
- L5 `checks passed, countdown next; party size `
- L6 `join in progress: late join started (0x31), status `
- K `san-upper-multi-killvis`

## 4. Behaviour, part A

### 4.1 `u3x_ng_limits(base, type, most, fewest)`

- `type` below 0 or above 19: both results are 0.
- Otherwise `*most` = the s32 at image 0x13a61e0 + 4·`type`; `*fewest` = 2 when that value is 3 or 4 or `type` is
  10, else 1.

Reads only.

### 4.2 `u3x_popup_timed(base, s, text, seconds, ok, cancel)`

1. D = the pointer at menu + 0x14f0; F = the show-page function (s + 648). When either is null: result 0, nothing
   written, nothing called.
2. now = the game clock (u32), read once.
3. D: text id (+8) = `text`; start (+0x10) = now; end (+0x14) = now + `seconds`·300 (32-bit, wrapping).
4. W = `u3h_pt_state()`. When W[0] is 0: W[1] = the menu's u32 at 0x1544, W[2] = the menu's u32 at 0x1548 (read
   before step 5). Then W[0] = 1, W[3] = `ok`, W[4] = 0.
5. Menu: u32 at 0x1544 = `ok`; u32 at 0x1548 = `cancel`.
6. cd[1] = now; cd[2] = `text`.
7. Call F(address of the menu, 0x453470dd, 0, 0). Result: the low 8 bits of what it returns.

### 4.3 `u3x_start_checks(base, s)`

"Refuse (code, body, action)" means: cd[6] = cd[6] OR code; `u3h_show_dialog(base, s, body, 0xc6467f2d, action, 0,
0, 0xe1e040aa)`; result 0.

1. type = `u3x_netgame_type(base, s)`; N = `u3x_lan_party_size(base)`; most and fewest from `u3x_ng_limits(base,
   type, ...)`; in this order.
2. cd[7] = N OR ((type AND 0xff) shifted left 8) OR (fewest shifted left 16) OR (most shifted left 24), as u32.
   cd[6] = 0.
3. N above most: refuse (2, 0x4a936499, 0x13cabc15).
4. Z = the lobby switches (u32 at s + 0x3d78). "Alone" = N is 1 and Z has 0x80 and (type is not 18 or Z has
   0x20000). "No minimum" = Z has 0x80000.
5. When "no minimum" and N is below fewest: sc[1] plus 1.
6. When not alone, not "no minimum" and N below fewest: refuse (3, 0x4a93e6b9, 0x13cafe1f).
7. When type is not 10, or alone: result 1.
8. The team check (type 10). "Teams unknown" = the switch word (u32 at s + 0x20c0) has bit 2. For each of the 10
   member slots of the party session that is in use:
   - teams unknown: the member is counted and belongs to no team;
   - otherwise its data is looked up: when the party has no room (room id 0) or the slot's member id equals the
     session's own member id, the data is the session's own member data (3.3), valid only when its length word
     (+0x12f0) is exactly 0x40; for any other member it is the slot's data, valid only when the slot's length word
     (+0x3c) is exactly 0x40. A member without valid data is not counted at all. A member with valid data is
     counted, and when the data's team byte (offset 9) is 1..4 that team gets one more.
9. cd[6] = (members counted shifted left 8) OR ((team 1 count OR team 2 count shifted left 4 OR team 3 count
   shifted left 8 OR team 4 count shifted left 12) shifted left 16).
10. When any of the four team counts equals the number of members counted (also when both are 0): refuse (4,
    0x101c7f10, 0x5350146e), one dialog only. Otherwise result 1.

### 4.4 `u3x_start_game(base, s)`

1. cd[0] plus 1. `u3h_cg_log(L1, the net menu mode as s32)`.
2. When `u3x_party_guest(address of the party session)` is non-zero: `u3h_cg_log(L2, −1)`; end.
3. `u3h_onb_startmatch(base, s)`: non-zero → end. Otherwise `u3h_m03_startmatch(base, s)`: non-zero → end.
4. `u3h_cg_commit_selection(base, s)`.
5. When the lobby switches have 0x40: `u3x_start_game_now(base, s)`; end.
6. **The start function** S: native 7, usable only when it is not null, the net menu mode is 2 and
   `u3x_mp_lan_or_online(base)` is non-zero (tested in this order; a later test is not made when an earlier one
   fails). Not usable: end.
7. now = the game clock (u32). When P is not 0 and now − P (wrapping, unsigned) is below 9000 and the net state is
   3 or 0xb: `u3h_cg_log(L3, the net state)`; end.
8. When a popup is up and its page id is 0x453470dd (3.4): end, silently.
9. `u3x_start_checks(base, s)` is 0: `u3h_cg_log(L4, cd[6] AND 0xff)`; end.
10. `u3h_cg_log(L5, u3x_lan_party_size(base))`. Then the word at `u3h_cg_t0_ptr()` = now (the value of step 7),
    or 1 when that is 0.
11. `u3x_start_prepare(base, s)`.
12. **The party is told.** B = the party session's backend pointer. When B is not null and (the party session has
    no room or its own member id equals its host member id): T = the u64 stored at B + 0x40; when T is not 0, the
    function at address T is called with (B, address of the party session, int 1), result ignored, and cd[3] plus
    1. (See section 8, oddity 1.)
13. The menu's u8 at 0x269a = 0.
14. P = the game clock, read again, or 1 when it is 0.
15. `u3x_popup_timed(base, s, 0x73a73dd8, 5, 0xe2707dbe, 0xbf6ce4d5)`: non-zero → cd[4] plus 1. Zero → cd[5] plus
    1 and S is called (no arguments).

### 4.5 `u3x_start_prepare(base, s)`

1. `u3h_start_calls_inc(s)`.
2. When the pointer at image 0x17c3580 equals s + 0x2000 (the game uses the helper's own net clock object): its
   u64 at +0xc, u32 at +0x20 and u32 at +0x28 become 0.
3. Unless the lobby switches have 0x20000000: the u8 at image 0x16e7170 = 0; the u8 at image 0x17d5c80 = 0; then,
   when native 12 is not null, it is called with the address of the match session.
4. When native 13 is not null it is called with 0x0dec97a4. For a non-null answer (the map list, 3.6) whose entry
   pointer is not null: for each of the first min(count, 64) entries that has a name:
   - when the name is exactly K (same length) and the lobby switches do not have 8: the entry's u32 at +0x40 = 0
     and `u3h_probe_pad2_inc(s)`;
   - in any case oob[11] = 1 when the lobby switches have 8, else 0.

   Entries without a name are skipped (oob[11] is not written for them).
5. `u3x_lan_reattach(base)`.

### 4.6 `u3x_start_game_now(base, s)`

S as in 4.4 step 6. Nothing happens when S is not usable or the net state is 3 or 0xb. Otherwise
`u3x_start_prepare(base, s)`, then S is called. P is not touched.

## 5. Behaviour, part B

### 5.1 `u3x_jip_enter(base)`

In this order:

1. Net game manager: the match's net game number (s16 at 0x661c) = the lobby's (s16 at image 0x17d167c).
2. Call 0x74ba60.
3. Match session: "start loading" (u8 at 0x1338) = 0. Net object: net step (u32 at +0x408) = 4.
4. Call 0x72b570(address of the net game manager, 1, 1, 0, 0, 0).
5. now = the game clock (s32), read after step 4. Net object: deadline (s32 at +0x2c) = now + 9000.
6. Call 0x651940(address of the menu, 0x69e10dad, 0, 0).
7. Net state (image 0x1b68810) = 0x31; its enter time (image 0x1b68814) = now.
8. `u3h_jip_started()`.
9. `u3h_fj_note(L6, null, null, the u32 at image 0x1b69530 as s32)`.

### 5.2 `u3x_latejoin_closed(base, s, r)`

1. ec[18] plus 1.
2. Result 0, nothing else, in each of these cases, tested in this order: ec[0] has bit 1 (value 2); `r[4]` is not
   the address of the net game manager; `u3x_netgame_type(base, s)` is not 10; the code check of 0x726b00 fails;
   the code check of 0x737eb0 fails.
3. L = the round's time limit: call 0x726b00 with a zeroed 8-byte result area and the manager; L = its first s32.
4. A = `u3x_round_mostly_over(L, net clock − match start time (image 0x17d5c64), wrapping)`.
5. Only when A is 0: lead = 0x737eb0(manager); W = the s16 at image 0x17e338e + lead·0x130 (lead taken as a signed
   number: −1 reads the s16 0x130 bytes before team 0); rounds = the manager's s32 at 0x6a44; round = its s32 at
   0x66fc. ec[21] = lead (as u32); ec[22] = W sign-extended to 32 bits; ec[23] = (round AND 0xffff) OR (rounds
   shifted left 16). A = `u3x_match_nearly_decided(W, rounds, round)`.
6. ec[19] plus 1; ec[20] = A; `r[8]` = A (64 bits, 0 or 1). Result 1.

### 5.3 `u3x_elim_wait_set(base)`

1. The flag at `u3h_elim_send()` = 1 (always).
2. When the manager's wait flag (u8 at 0x354) is already non-zero: end.
3. Wait flag = 1; wait deadline (s32 at 0x358) = net clock + 18000; ec[26] plus 1.

### 5.4 `u3x_elim_round_end_hold(base, r)`

1. ec[24] plus 1. When ec[0] has bit 2 (value 4): result 0.
2. A net player "holds" when the net-active flag (image 0x17d5c61) is non-zero and `u3x_waiting_joiner(record)` is
   non-zero. The 10 net player records are looked at in order 0..9; at each one that holds, when the manager's
   wait flag (0x354) is still 0, `u3x_elim_wait_set(base)` runs (so at most once per call).
3. No record holds: result 0, `r` untouched.
4. Otherwise ec[25] plus 1; `r[8]` = 0; result 1.

### 5.5 `u3x_elim_wait_text(base)`

1. When ec[0] has bit 2 (value 4): end.
2. When the flag at `u3h_elim_received()` is non-zero: it becomes 0; `u3x_elim_wait_set(base)`; then the flag at
   `u3h_elim_send()` = 0 (a guest does not announce).
3. End when the manager's wait flag (0x354) is 0.
4. End when the HUD's element block pointer (image 0x1805fa0 + 0x238) is null. E = that block + 0x25f0. End when
   the first byte of E is not 0 (a message is up).
5. End when the code check of 0xf32ae0 fails; then end when the code check of 0x139c740 fails.
6. T = 0xf32ae0(address of image 0x17026b8, 0x02277bd7); when null, T = the pointer stored at image 0x17026b8 +
   0x10; when that is null too: end.
7. The two "HUD in use" pointers (image 0x1806480, then image 0x1b24bd8) = the address of image 0x1805fa0.
8. Call 0x139c740(E, T, 0x80). Then E: u8 at 0x7f = 0; u32 at 0x84 = 0xffffffff; u32 at 0x80 = the game clock; u32
   at 0x88 = 0; u32 at 0x8c = 0; u8 at 0x90 = 0.
9. ec[27] plus 1.

## 6. Tables

The old code holds two literal tables of 20 bytes each, indexed by the net game type (PS3 values): the most and
the fewest players.

- **Most players.** The running PS4 game holds the same 20 values: image offset 0x13a61e0, 20 entries of 4 bytes
  (s32), index = net game type 0..19 (read by the game's own "most players" function at image 0x73f5c0 and by its
  session create). Checked in the PS4 image: the 20 values equal the old table. The new code reads them there
  (4.1).
- **Fewest players.** PS3 only: the PS4 image holds no such table (searched as 8-bit and 32-bit arrays; the PS3
  function has no PS4 counterpart). The values follow a rule that the describer checked against all 20 old
  entries: 2 for the types whose most is 3 or 4 (the co-operative types played by more than one player) and for
  type 10 (Elimination), 1 for every other type. The new code uses the rule (4.1). The tester compares all types
  −2..21 with the old function.

## 7. Test scenarios

Everything is testable outside the game on a fake image (zero-filled memory that covers image offsets up to
0x1b69534) with: recording functions written at the image offsets of 3.5 (their first 12 bytes set as the checks
need, or the stand-in for `u3h_mp_code_is` scripted); a fake state object (show-page and natives 7, 12, 13 as
recording functions or null); a fake ExtState; recording stand-ins for the functions of 2.2 (scripted results for
`u3x_netgame_type`, `u3x_lan_party_size`, `u3x_mp_lan_or_online`, `u3h_onb_startmatch`, `u3h_m03_startmatch`; the
pure ones may be the real ones). Compare memory, results and the order and arguments of all calls. The image
values at 0x13a61e0 must be put into the fake image (20 words). Only the meaning of the game calls (the popup
really counting down, the HUD text really showing, the late join really loading) needs the live game.

Part A:

1. Limits: every type from −2 to 21; the fake table filled with the real values and with other values (a type
   whose table value is 3 or 4 gives 2; type 10 gives 2 whatever its table value).
2. Popup: no data object; no show-page function (both: result 0, nothing written); W[0] 0 (the menu's ids are
   saved) and W[0] 1 (they are not saved again); show-page answers 0, 1, 0x100 (result 0), 0x1ff (result 0xff);
   `seconds` 0, 5 and a clock near 2^32 (the end wraps).
3. Checks: N above most; N equal to most; N below fewest with each combination of the switches 0x80, 0x20000,
   0x80000 and type 18 / another type / N = 1 and N = 2 (sc[1] counts only with "no minimum"); type 10 alone (no
   team check); type 10 with members: all on one team (refused), two teams, a member with team 0 or 5 (counted, no
   team), a member whose length word is not 0x40 (not counted), the own member with a room (the session's own
   data is used) and without a room (the session's own data for every member), no member in use (refused: 0
   equals 0), teams unknown with and without members; type out of range (most 0: refused with code 2). cd[6] and
   cd[7] in each case; one dialog per refusal.
4. Start: a party guest; `u3h_onb_startmatch` answers 1 (the other is not asked); `u3h_m03_startmatch` answers 1;
   switch 0x40 (immediate start: with and without a usable S, with net state 3); S not usable for each of its
   three reasons (and `u3x_mp_lan_or_online` not asked when native 7 is null or the mode is not 2); P = 0 with
   net state 3 (goes on); P set 8999 ticks ago with net state 3 and 0xb (stops, L3) and with net state 0x12 (goes
   on); P set 9000 ticks ago; the countdown popup already up; another popup up; checks refuse (L4 with the code);
   the full pass with the popup shown (cd[4]) and not shown (cd[5], S called); the clock 0 at step 10 and at step
   14 (1 is stored); the party step with no backend, with a backend and the u64 at +0x40 zero, non-zero (called
   with (B, party, 1), cd[3]), and with a function table whose entry 8 is a different recording function (never
   called by v7.212, see oddity 1); a clock that the stand-in of `u3x_start_prepare`'s calls moves between steps 7
   and 14.
5. Prepare: the net clock pointer equal / not equal to s + 0x2000; switch 0x20000000 set / clear, native 12 null;
   native 13 null, answering null, a list with a null entry pointer, with 0, 3 and 70 entries (only 64 looked
   at), names null, K, K with one more character, a prefix of K; switch 8 set (nothing cleared, oob[11] = 1) and
   clear (the level id cleared, oob[11] = 0); a list whose entries all have no name (oob[11] unchanged).
6. Immediate start: S not usable; net state 3, 0xb, other.

Part B:

7. Late join entry: the order of the three game calls and of the writes around them (the stand-in of 0x72b570
   changes the game clock: the deadline and the enter time use the new value); a status word of 6 and of
   0x80000000 (passed as a negative number).
8. Gate: switch off; `r[4]` another address (`u3x_netgame_type` not asked); type 9; each code check failing (the
   second not asked after the first); limit 0 (the lead is asked); limit 6000 with elapsed 4500 (0), 4501 (1,
   lead not asked), elapsed negative; lead 0 / 1 / −1 with wins 0, 1, 2; round = rounds − 2, rounds − 1; rounds
   0; the ec words; `r[8]` and the result.
9. Hold: switch off; net-active 0 with waiting players (no hold); no waiting player; one waiting player with the
   wait flag clear (wait set once, deadline, ec[26], send flag) and already set (no second set; send flag
   untouched); three waiting players (wait set once); `r[8]` = 0 only when holding.
10. Wait set: flag clear / set (send flag 1 in both cases); the deadline near the top of the s32 range.
11. Text: switch off; received flag set with the wait flag clear (wait set, send flag back to 0, text shown in the
    same call) and set; wait flag clear; element block null; a message already up; each code check failing; the
    lookup answering a text, null with a stand-in text, null without one; a text longer than 0x7f characters
    (the byte at 0x7f is 0); the two HUD pointers; ec[27].

## 8. Oddities of the old code that are behaviour to keep

1. **4.4 step 12 reads the function address from backend + 0x40.** The C body this was moved from (v7.210) called
   entry 8 of the backend's function table (the address stored at (the pointer at backend + 0) + 0x40): the
   party session's "set attribute" with the value 1, i.e. the party is locked when its leader starts (G14 6.1; for
   the LAN backend entry 8 is the do-nothing slot, G10 3.3). v7.212 lost that indirection: it takes the 8 bytes
   at backend + 0x40 themselves as the address. For the online backend that is its third session binding (G13
   3.4: null unless a third session is attached, then a data pointer), for the LAN backend bytes that no
   description lists. So the shipped helper normally makes no call here (cd[3] stays 0, an online party is not
   locked at a custom game start), and would jump to a data address if that field were ever set. The spec
   describes v7.212 as it is; see open question 1. (The same kind of slip as G21's oddity 1.)
2. 4.3: a refusal ORs its code into cd[6]; for the team refusal cd[6] then holds the counts of step 9 with bit 2
   set as well.
3. 4.3 step 10: a type 10 party in which no member could be counted is refused ("everybody on one team").
4. 4.3: with "teams unknown" every member in use is counted and nobody has a team, so the check only refuses an
   empty party.
5. 4.5 step 4: oob[11] is written once per named entry, not once per call, and not at all when the list has no
   named entry.
6. 4.4: P is set by the countdown path only; the immediate start (4.6) and the matchmaking starts do not set it.
   The value for `u3h_cg_t0_ptr()` comes from the clock read of step 7, P from a second read.
7. 4.2: the menu's own action ids are saved only while W[0] is 0; a second popup before `pt_tick` has restored
   them keeps the first saved pair. `seconds` is not range-checked.
8. 5.2: with a tied lead (−1) the round wins are read 0x130 bytes before team 0's record (inside the game's score
   data; no fault). ec[18] counts even when the switch is off.
9. 5.3: the wait deadline (manager + 0x358) is written but nothing in the helper reads it. The send flag is set
   even when the wait was already set (5.5 step 2 then clears it again on a guest).
10. 5.4: the net-active flag is looked at per record, not once; the result is the same.
11. 5.5: the received flag is consumed even when the wait is already set; the text is copied without a length
    check beyond the 0x80-byte bound and its last byte forced to 0.
12. 5.1 makes its three game calls without a code check and does not look at any result.

## 9. Open questions

1. **The party step of 4.4 (oddity 1).** Keep v7.212 (address read from backend + 0x40, normally no call) or
   restore v7.210 (entry 8 of the backend's function table, called with (backend, party session, 1), cd[3] plus 1
   when the entry is not null; the same effect as `u3x_party_lock(base, 1)` of G31 plus the counter)? The pipeline
   rule says "100% of the old code", so the spec gives v7.212; the owner should decide (G21's same question was
   decided for the v7.210 form). The tester's fake backend can serve both: a function table whose entry 8 and a
   u64 at + 0x40 point to two different recording functions.
2. **The fewest-players rule (section 6)** reproduces the 20 PS3 values but was read from the values, not from a
   PS4 source. If a later PS3 reading shows another rule behind them (for a type above 19 nothing changes: both
   limits are 0), only 4.1 changes.
3. `u3h_elim_send` does not exist yet (2.2, integrator). Until it does, part B cannot link.

## Clarifications

Added by the tester (2026-11-14). Facts only, confirmed by the old-versus-new test (`reimpl2\G35\test_g35.cpp`),
where the text above is silent or can be read two ways. Every statement of sections 4 and 5 that the test checks
against v7.212 holds; nothing above had to be corrected.

1. Open question 1 (the party step, 4.4 step 12). Decided by the project: the new code calls entry 8 of the
   backend's function table, i.e. the function whose address is stored at (the pointer at backend + 0) + 0x40, with
   (backend, address of the party session, 1), and adds 1 to cd[3] when that entry is not null. v7.212 takes the 8
   bytes at backend + 0x40 themselves as the address. With a fake backend that has both, v7.212 always takes the
   second and never the first, the new code the reverse. The conditions (a backend; no room or own member id = host
   member id; all read after `u3x_start_prepare` has returned), the arguments, the place of the call (after
   `u3x_lan_reattach`, before the cancelled byte is cleared and before the popup's show-page call) and everything
   else of the press are the same. With only the table entry set v7.212 makes no call and leaves cd[3]; with only
   the u64 at backend + 0x40 set v7.212 calls it and the new code makes no call. The new code reads the pointer at
   backend + 0, which v7.212 never does: a party backend must have a function table.
2. Open question 2 (the fewest players). With the game's 20 values at image 0x13a61e0 (read from the PS4 image:
   they equal the old literal table) the rule of 4.1 gives the old code's most and fewest for every type; tested
   for the types −5 to 40. With other values at that address the new code follows the image and the rule, v7.212
   its two literal tables.
3. Open question 3 stands: `u3h_elim_send` is the integrator's. The test stands in for it with the address of one
   flag byte; v7.212 writes the C part's flag directly, at the same points.
4. 3.1 / 3.2 / 5.1 step 1: image 0x17d167c is the net game manager's s16 at 0x661c (0x17cb060 + 0x661c). "The
   net game selected in the lobby" and "the net game the match uses" are one field, and step 1 of 5.1 stores the
   value it has just read (no change). Both versions do this.
5. 4.5 step 4, a read point: v7.212 reads the lobby switches twice for a named entry, once for the "keep" test
   (only when the name is K) and once more for oob[11], after `u3h_probe_pad2_inc`. The new code reads them once
   per named entry, before that call. The two can only differ when `u3h_probe_pad2_inc` changes bit 3 of the
   switches; the real one adds 1 to a probe counter and nothing else.
6. 4.3 step 2: N goes into cd[7] with all its 32 bits (a negative N or one above 255 reaches the higher fields);
   only the type is cut to 8 bits; fewest and most are shifted without a mask.
7. 4.4 step 7: the difference now − P is unsigned, so a P that lies after now (difference near 2^32) does not stop
   the press.
8. 4.4 step 15 / 4.2: when the popup cannot be shown because the menu has no data object or the state object no
   show-page function, cd[5] counts and the start function is called at once, as for a show-page answer whose low
   8 bits are 0.
9. 5.2 step 3: the result area is 8 zero bytes at every call; only its first s32 is the limit, also when the game
   function fills all 8 bytes. Step 5: the leading team is the low 32 bits of what 0x737eb0 returns.
10. 5.5 step 7: the two "HUD in use" pointers are written only once a text is known; without any text nothing is
    written at all.
