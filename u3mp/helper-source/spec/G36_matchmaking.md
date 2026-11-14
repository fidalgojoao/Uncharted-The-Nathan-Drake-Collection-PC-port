# G36. Matchmaking flow: start, players rows, map and difficulty votes, start / join tick

Behaviour description for a reimplementation (reimpl2 pipeline, describer's document). It says what the functions
do, not how the old ones were written. Conventions as in `G18_friends_list.md`, `G21_party_join.md` and
`G23_boosters.md`: offsets are hexadecimal when prefixed with 0x; "image offset X" is the address `base` + X; all
multi-byte values are little-endian; u8 / u16 / u32 / u64 / s16 / s32 / s64 are integers of that width, "pointer"
is 8 bytes. Clock values are 32 bits; their additions and subtractions wrap, and each step says whether a
difference is taken as signed or unsigned. Counters are plain 32-bit increments (wrapping). Game functions use the
System V x86-64 convention with integer arguments only. There is no float code in this group (the one float
pattern, 0x3f800000 = 1.0, is stored as a u32).

## 1. Purpose and callers

"Find Game" on PS3 is a state machine of the game: Start Match shows a two-second "Starting in" popup, the
matchmaking page comes up, the game searches a room or hosts one, the players list fills, the map vote and (for
co-op) the difficulty vote run, and the match starts. The PS4 game kept the matchmaking page, its status line, the
vote lists and the net state handlers, but nothing in it starts or drives them. The helper does. This group is the
part of that code which still follows the PS3 game:

- **Part A, the players list**: the answers of the lobby's players list while matchmaking runs, once for the
  offline stand-in ("Finding Player..." slots that turn into "Found Player" and then into named players with a rank
  badge) and once for the real online matchmaking (this player, then the members of the match room).
- **Part B, the flow**: what Start Match does for the offline stand-in and for online matchmaking; the online
  search request; the end of the map vote (the winner becomes the selected net game, the lobby stage switches); the
  co-op difficulty vote (open, stand-in votes, close); the event record with which the helper runs one of the
  game's matchmaking page handlers; the per-frame step that runs the game's start and join handlers independently
  of the page that is open; and the test whether a page is on the menu's page stack.

What drives these functions (the stand-in's tick `m03_tick` / `m05_tick`, the online driver `onm_tick`, their stop
and failure steps) is the helper's own logic, stays in the C part and is not described here.

Callers (all stay as they are):

| Function | Called by | When |
|---|---|---|
| `u3x_m03_players` | `u3x_lobby_source` (C++, G31) after `u3h_onm_players` returned 0; C wrapper `m03_players` (no caller left) | every request of the lobby's players list |
| `u3x_onm_players` (old C `onm_players`) | C wrapper `onm_players`, reached through the C export `u3h_onm_players` from `u3x_lobby_source` | every request of the lobby's players list |
| `u3x_m03_startmatch` (old C `m03_startmatch`) | C wrapper, reached through the C export `u3h_m03_startmatch` from the Start Game step of G35 (only when `u3h_onb_startmatch` returned 0) | Start Match pressed |
| `u3x_onb_startmatch` (old C `onb_startmatch`) | C wrapper, reached through the C export `u3h_onb_startmatch` from the Start Game step of G35 | Start Match pressed |
| `u3x_online_search` | C wrapper `onm_search`, from the C online driver `onm_tick` | once, when the matchmaking page is up after the popup |
| `u3x_n04_close` (old C `n04_close`) | C wrapper, from the C stand-in tick `m05_tick` | the map vote's time is over |
| `u3x_n09_open` (old C `n09_open`) | C wrapper, from `m05_tick`, right after a successful `n04_close` | - |
| `u3x_n09_tick` (old C `n09_tick`) | C wrapper, from `m05_tick`, every helper tick while the difficulty vote is open; `now` = the game clock | - |
| `u3x_n27_crush_locked` (old C `n27_crush_locked`) | `u3x_n09_tick` only (the C wrapper is left without a caller) | - |
| `u3x_mm_ev6` (old C `mm_ev6`) | `u3x_mm_start_tick`; C wrapper `mm_ev6`, from `onm_tick` (handler 0x6a6830, page 0) | - |
| `u3x_mm_start_tick` (old C `mm_start_tick`) | C wrapper, from the helper's per-frame tick (`u3_profile_event`, event kind 16, only with a non-zero `base`) | every frame |
| `u3x_page_in_stack` | `u3x_mm_start_tick`; C wrapper `mm_in_stack` (left without a caller) | - |

Threading: every function runs on the thread that runs the helper's hooks (the per-frame tick, the menu's
data-source and button thunks). No thread is started and no lock is taken.

## 2. Interface

All functions are `extern "C"`. No float is passed or returned. `base` = the game image base; `s` = the helper's
state object (3.2); `ev` = a list request (3.3).

### 2.1 Functions to deliver

The group is written as two independent parts. Neither part calls a function of the other and they share no
variable of their own, so each can be written and tested alone.

**Part A, the players list**

| Function | Signature | Result |
|---|---|---|
| players list of the stand-in (existing name) | `int u3x_m03_players(uintptr_t base, int32_t *ev, void *s, const char *self_name)` | 1 = request answered, 0 = the stand-in is not running (request untouched) |
| players list of the online matchmaking (new entry point) | `int u3x_onm_players(uintptr_t base, int32_t *ev, void *s, const char *self_name)` | 1 = answered, 0 = not running (request untouched) |

`self_name` = the text for the player's own row (a NUL-terminated text owned by the caller).

**Part B, the flow**

| Function | Signature | Result |
|---|---|---|
| Start Match, offline stand-in (new) | `int u3x_m03_startmatch(uintptr_t base, void *s)` | 1 = the press was taken, 0 = not this case |
| Start Match, online (new) | `int u3x_onb_startmatch(uintptr_t base, void *s)` | 1 = taken, 0 = not this case |
| the online search (existing name) | `int u3x_online_search(uintptr_t base)` | 0 = the search was sent, -1 = it could not be sent |
| end of the map vote (new) | `int u3x_n04_close(uintptr_t base, void *s)` | 1 = a net game was selected, 0 = nothing selectable |
| open the difficulty vote (new) | `int u3x_n09_open(uintptr_t base, void *s)` | 1 = the vote was opened, 0 = no difficulty vote for this net game |
| "Crushing is locked here" (new) | `int u3x_n27_crush_locked(uintptr_t base)` | 1 = locked, 0 = unlocked or unknown |
| difficulty vote, per tick (new) | `int u3x_n09_tick(uintptr_t base, uint32_t now)` | 1 = the vote was closed in this call, 0 = still open |
| run a page handler with event 6 (new) | `void u3x_mm_ev6(uintptr_t base, uint32_t fn, uint32_t page)` | - |
| start / join step, per frame (new) | `void u3x_mm_start_tick(uintptr_t base, void *s)` | - |
| page on the page stack (existing name) | `int u3x_page_in_stack(const uint8_t *menu, uint32_t page)` | 1 = yes, 0 = no |

`fn` = the image offset of a game function; `page` = a page id; `now` = a game clock value; `menu` = the address of
the menu object.

C wrappers (the integrator's work): the nine C functions `m03_startmatch`, `onb_startmatch`, `n04_close`,
`onm_players`, `n09_open`, `n27_crush_locked`, `n09_tick`, `mm_ev6`, `mm_start_tick` each become one line that
passes its arguments unchanged to the `u3x_` function of the same name and returns its result. `m03_players`,
`onm_search` and `mm_in_stack` already are such wrappers and stay.

### 2.2 Functions of other groups, of the C part and of the shared tools that the new code calls

Declare them; do not reimplement them. `u3h_` are exports of the C part. "Pure read" = no effect; how often such a
function is called is not part of the behaviour. "NEW" = a one-line C export that does not exist yet and that the
integrator adds; its name is a proposal.

| Name | Signature | What it does | Part |
|---|---|---|---|
| `u3h_ext`, `ext_ea()`, `ext_eb()` | CONVENTIONS.h | the helper's state block and its word arrays ea[] and eb[] (3.1) | A, B |
| `u3h_m03` | `uint32_t *u3h_m03(void)` | address of the stand-in's state (3.4) | A, B |
| `u3h_m03_diag` | `volatile uint32_t *u3h_m03_diag(void)` | address of the stand-in's eight diagnostic words | A, B |
| `u3h_onm` (NEW) | `uint32_t *u3h_onm(void)` | address of the online driver's state (3.4; the C static `onm`) | A |
| `u3h_onb_mode` | `int u3h_onb_mode(void)` | pure read: how multiplayer was entered: the play mode word of the C part: 1 = Play Online with the online backend, 2 = offline mode (the LAN backend with the stand-in), 0 = LAN | A, B |
| `u3h_text` | `const char *u3h_text(uintptr_t base, void *s, uint32_t id)` | the game's text for a text id; never null | A |
| `u3h_player_profile` | `uint8_t *u3h_player_profile(uintptr_t base)` | pure read: the profile object of the menu user (G23 3.5), or null | A |
| `u3h_ox_rank_of` | `int32_t u3h_ox_rank_of(const char *name)` | pure read: the rank word of another player as far as the helper has downloaded that player's profile; -1 = not known | A |
| `u3h_rank_icon` | `void u3h_rank_icon(uintptr_t base, void *s, int32_t *ev)` | turns a rank badge answer (word 2 = 0x29, the rank word in word 6) into the picture answer of the rank art when that art is loaded; otherwise leaves the request as it is | A |
| `u3h_team_colour` | `uint32_t u3h_team_colour(uintptr_t base, int32_t team)` | the colour word of a team (G34) | A |
| `u3h_hud_alias_names`, `u3h_stage_tick`, `u3h_loadout_tick` | `void (uintptr_t base, void *s)` each | per-frame jobs of other groups that the lobby list drives | A |
| `u3x_clan_tag_poll` | `void u3x_clan_tag_poll(uintptr_t base, void *s)` | G33: the clan tag keyboard's poll | A |
| `u3x_other_member` | `const char *u3x_other_member(const uint8_t *session, uint32_t row)` | exists: the name of the `row`-th (1-based) member of a session that is in use and is not this machine's own member; null when there is none | A |
| `u3x_icon_answer` | `void u3x_icon_answer(int32_t *ev, int32_t kind, int32_t a, int32_t b, int32_t c, int32_t index)` | G37: writes the image answer of 3.3: word 2 = `kind`, words 4 and 5 = 0, word 6 = `a`, word 7 = `b`, word 8 = `c`, word 9 = `index`, word 10 = 0, word 11 = -1, word 12 = 0x3f800000 | A |
| `u3x_icon_tinted` | `void u3x_icon_tinted(int32_t *ev, int32_t kind, int32_t value, int32_t tint)` | G37 5.11 (new export delivered there): the same answer with word 6 = `value`, words 7 and 8 = 0, word 9 = -1 and word 11 = `tint` | A |
| `u3h_party_send` | `int u3h_party_send(uintptr_t base, uint32_t msg, const void *data, uint32_t len)` | hands a party message to the online interface (offline it does nothing); result not used | B |
| `u3h_n09_reset` (NEW, also asked for by G30) | `void u3h_n09_reset(uintptr_t base)` | the C function `n09_reset`: unless the difficulty vote is switched off (ec[2] bit 7) it clears the two flag bytes of the difficulty vote block (3.5) and adds 0x10000 to ec[255] | B |
| `u3h_n02_arm` (NEW, also asked for by G30) | `void u3h_n02_arm(uint32_t now)` | sets the C part's two search-popup words: "popup gone" count = 0, "armed at" = `now` | B |
| `u3h_onm_arm` (NEW) | `void u3h_onm_arm(uintptr_t base)` | the C function `onm_arm`: arms the online driver (its state words armed = 1, running = 0, phase = 0, "armed at" = the game clock; one diagnostic counter) | B |
| `u3h_k44_diag` (NEW) | `volatile uint32_t *u3h_k44_diag(void)` | address of the four diagnostic words of the Play Online code (C static `k44_d`) | B |
| `u3h_n04_idx` (NEW) | `int32_t *u3h_n04_idx(void)` | address of the s32 "net game selected at the vote's end" of the C part (C static `n04_idx`; the stand-in's tick reads it at the match start) | B |
| `u3h_n04_diag` (NEW) | `volatile uint32_t *u3h_n04_diag(void)` | address of the eight diagnostic words of the vote end (C static `n04_d`) | B |
| `u3h_m05_vt` (NEW) | `uint32_t *u3h_m05_vt(void)` | address of the u32 "time of the last stand-in vote" of the C part (C static `m05_vt`; the stand-in's tick writes it too) | B |
| `u3h_mm_bg` (NEW) | `uint8_t *u3h_mm_bg(void)` | address of the u8 "background" flag (C static `mm_bg`, 3.4) | B |
| `u3h_mm_quiet` (NEW) | `uint8_t *u3h_mm_quiet(void)` | address of the u8 "quiet" flag (C static `mm_quiet`, 3.4) | B |
| `u3h_onm_state` | `void u3h_onm_state(uintptr_t base, uint32_t state)` | sets the game's net state: the u32 at image 0x1b68810 = `state` and the u32 at image 0x1b68814 = the game clock (image 0x17b4ba4) | B |
| `u3h_active_page` | `uint32_t u3h_active_page(uintptr_t base)` | pure read: the id of the page on top of the menu's page stack; 0 while the menu changes pages (UI state 7 or 8) or without a page | B |
| `u3h_show` | `int u3h_show(uintptr_t base, void *s, uint32_t page, int silent)` | shows a menu page through the show-page function of the state object (it does not test that function for null); result not used here | B |
| `u3h_crc_sid` | `uint32_t u3h_crc_sid(const char *name)` | pure: the helper's 32-bit check value of a name (used for one diagnostic word only) | B |
| `u3x_sym_table` | `uint8_t *u3x_sym_table(void *s, uint32_t id)` | data of a script symbol: calls the lookup function of the state object (3.2) with `id`; null when the function pointer, the returned handle or the pointer the handle holds is null | B |
| `u3x_pick_playlist` | `const uint8_t *u3x_pick_playlist(const uint8_t *table, uint32_t picked)` | exists: the entry of the playlist table whose id is `picked`, else the table's first entry; null for a missing or empty table. Pure read | B |
| `u3x_playlist_pool` | `uint32_t u3x_playlist_pool(const uint8_t *playlist)` | exists: the first id of a playlist entry's pool list, 0 when the entry is null or has none. Pure read | B |
| `u3x_party_lock` | `void u3x_party_lock(uintptr_t base, int v)` | G31 4.1: the party leader locks (1) or unlocks (0) the party session through its backend | B |
| `u3x_popup_timed` | `int u3x_popup_timed(uintptr_t base, void *s, uint32_t text, int seconds, uint32_t ok, uint32_t cancel)` | G35: shows the countdown popup with text id `text` for `seconds` seconds; `ok` is the action that runs when the time is over, `cancel` the one of the back button. Result: 0 when the menu has no popup record or the state object has no show-page function (then nothing was changed), else the low 8 bits of the show-page result | B |
| `u3x_net_clock` | `int32_t u3x_net_clock(uintptr_t base)` | G11: the net clock's time: the s32 at +0xc of the object the pointer at image 0x17c3580 points to, or 0 when that pointer is null | B |
| `u3x_mmv_netgame` | `uint8_t *u3x_mmv_netgame(void *s, int16_t i)` | exists: record `i` of the net game table (3.5) through one `u3x_sym_table(s, 0x3b9a067d)` lookup; null when the table is missing or `i` is outside 0 .. count - 1 | B |
| `u3x_has_difficulty_vote` | `int u3x_has_difficulty_vote(uint32_t game_type)` | exists, pure: 1 for the co-op game types that get a difficulty vote (4, 8, 9, 11, 13, 16, 17), else 0 | B |
| `u3x_vote_open` | `void u3x_vote_open(uint8_t *vote)` | exists: prepares a vote block (3.5): own vote (s32 at +4) = -1, "closed" (u8 at +1) = 0, "open" (u8 at +0) = 1, the four counts (s32 at +8 .. +0x14) = 0 | B |
| `u3x_stand_in_vote` | `int u3x_stand_in_vote(uint32_t now)` | exists, pure: the difficulty row 0 .. 3 a stand-in player votes for, derived from the clock value (Easy 1, Normal 4, Hard 4, Crushing 1 in 10) | B |
| `u3x_vote_winner` | `int32_t u3x_vote_winner(const uint8_t *vote)` | exists, pure: the rule of 5.4 step 1 (may be used for it) | B |
| `u3x_mm_starting`, `u3x_mm_joining` | `int u3x_mm_starting(uint32_t state)`, `int u3x_mm_joining(uint32_t state, int background)` | exist, pure: the two state tests of 5.9 step 2 (may be used for them) | B |

### 2.3 Strays

None is delivered here. The image answer with a colour that `u3x_m03_players` writes (old private function
`icon`, which the inventory lists under G37) is delivered by G37 as the export `u3x_icon_tinted` (G37 5.11); part
A calls it. 3.3 names its words only so that the answers of 4.1 can be checked.
The private text setter and the private profile accessor of the old file are one-field operations (3.3, and
`u3h_player_profile`).

## 3. Game and helper facts

### 3.1 The helper's state block (address from `u3h_ext()`)

u32 word arrays: ea[] at offset 0x3c50 (`ext_ea()`), eb[] at 0x4050 (`ext_eb()`), ec[] at 0x4450 (no accessor in
CONVENTIONS.h: `u3h_ext()` + 0x4450).

| Word | Meaning |
|---|---|
| eb[2] bit 13 (value 0x2000) | set = the offline stand-in is switched off |
| eb[3] bit 16 (value 0x10000) | set = the stand-in's "fill" sequence (slots, then votes) is switched off |
| ec[2] bit 3 (value 8) | set = the vote end does not touch the lobby scene and stage (5.4 step 6) |
| ec[2] bit 4 (value 0x10) | set = the stand-in's rows are tinted in team colours (4.1 step 9); clear = no tint |
| ec[2] bit 7 (value 0x80) | set = no difficulty vote |
| ec[252] | low 16 bits: difficulty votes opened; high 16 bits: difficulty votes closed |
| ec[253] | summary of the last close (5.7 step 6) |
| ec[254] | low 8 bits: game type at the last open; bits 8 .. 15: the own vote at the last close |
| ec[255] | low 16 bits: stand-in votes cast; high 16 bits: resets (written by `u3h_n09_reset`) |
| ea[169] | stand-in Crushing votes turned into Hard votes |

Other diagnostic words: word 0 (Start Match presses) and word 7 (list requests) of `u3h_m03_diag()`; word 3 of
`u3h_k44_diag()` (online Start Match presses); words 0, 1, 2, 4, 5, 6 of `u3h_n04_diag()` (5.4).

### 3.2 The state object `s`

| Offset | Type | Meaning |
|---|---|---|
| 648 | pointer to function | the show-page function (used through `u3h_show` only) |
| 656 | pointer to function | script symbol lookup (used through `u3x_sym_table` and `u3x_mmv_netgame` only) |
| 768 + 8·k | pointer to function | "native k": a game function the helper found at start; may be null |

Natives used here:

| k | Arguments | Result | Meaning |
|---|---|---|---|
| 2 | (NGM, s32 index, pointer 0, u32 0xc) | none | makes net game `index` the selected net game |
| 21 | (profile object) | u32 | the rank word of a profile |

### 3.3 The list request `ev`

As G19 3.3 and G23 3.3: an array of 32-bit words, "word n" at byte offset 4·n. Used here:

| Word | Meaning |
|---|---|
| 0 | operation: 1 status, 2 row text, 4 row image, 5 get selection, 6 get the last row index, 10 row selected, 20 (0x14) row colour; others exist |
| 1 | row index (in), taken as u32; the answer of operations 5 and 6 (out) |
| 2, 4 .. 12 | the image answer |
| 0xf | the answer of operation 20 |
| 0x14 | the image slot asked (operation 4), u32: 0xf5a294f3 = the rank badge, 0 = the connection picture |
| 0x15 | the text column asked (operation 2); 0 = the name |
| 0x16 | the sub-question of operation 20; 2 = the row's tint |
| pointer at byte 0x40 | the text answer |

"The text answer becomes T" = the pointer at byte 0x40 is set to T. "The empty text" = a pointer to a NUL byte
that stays valid. "The image answer of kind K with value V" = `u3x_icon_answer(ev, K, V, 0, 0, -1)` (words as in
2.2). "The coloured image answer of kind K with value V and colour C" = `u3x_icon_tinted(ev, K, V, C)`: word 2 = K, words 4 and 5 = 0, word 6 = V,
words 7 and 8 = 0, word 9 = -1, word 10 = 0, word 11 = C, word 12 = 0x3f800000; that is the image answer with
word 11 = C in place of -1. Word 3 and every word not named stay as they are.

Image kinds used: 0x29 = rank badge (value = the rank word); 16 = the "searching" picture; 31 = connection bars
(value = the number of bars).

### 3.4 The helper's own state (all of it stays in the C part)

**Stand-in state** (`u3h_m03()`), u32 words; the stand-in's tick owns it:

| Word | Meaning |
|---|---|
| 0 | armed: a search starts when the matchmaking page is up |
| 1 | running. (The online driver also sets it, with phase 3, once its room has the playlist's minimum of players: from then on the vote and the countdown are the stand-in's, online too.) |
| 3 | phase; in fill mode: 1 searching, 2 slots filling, 3 "Voting Starts in", 4 map vote, 7 difficulty vote, 5 "Game starting in" (without fill mode the tick uses 1 .. 5 for a shorter sequence in which 2 is also "slots filling"). Only the tests of 4.1 read it here |
| 4 | found: the number of rows that have a player (row 0, this player, counts) |
| 5 | max: the number of rows (the playlist's player count, 2 .. 10) |
| 6 | the selected row |
| 7 | the game clock value at which the phase (or the last found slot) started |
| pointer at byte 0x20 | the playlist entry of the search |

Words 2 and beyond byte 0x28 are not used here.

**Online driver state** (`u3h_onm()`), u32 words: word 1 = running, word 5 = the selected row, word 6 = max (the
number of rows). The other words (0 armed, 2 phase, 3 "armed at", 4, 7 .. 12) are not used here.

**Background flag** (`*u3h_mm_bg()`): non-zero while a room join or creation runs "in the background", that is
under whatever page is open, started by the helper's own drivers. Other C code sets and clears it too.

**Quiet flag** (`*u3h_mm_quiet()`): while non-zero, the show-page calls inside the game's join and create
handlers are suppressed (a helper stub on those call sites reads it). Only 5.9 writes it.

### 3.5 Game data

| Image offset | Type | Meaning |
|---|---|---|
| 0x17b4ba4 | s32 | the game clock, 300 ticks per second |
| 0x17bba90 | object | the lobby scene; its u32 at +0 and +4 are a state and a phase (read for a diagnostic word only) |
| 0x17bd080 | object | the menu: pointer at +0x8b0 + 8·k = page k of the page stack; s32 at +0xdb0 = the index of the top page. A page object's u32 at +0x10 is its page id |
| 0x17cb060 | object | the net game manager ("NGM"): s16 at +0x661c = the selected net game index (negative = none); u8 at +0x6a48 = the difficulty (1 Easy .. 4 Crushing); text at +0x74b8 = the lobby stage's name |
| 0x17d5c58 | object | the net state ("NET"): u64 at +0x10 a search setting of the game (its low u32 is the game type word); u8 at +0x18: 0 = the status line names the search phase, 1 = "Searching for players..."; s32 at +0x2c = the deadline of the status countdown, in net clock ticks; s32 at +0x40 = the matchmaking status (0 none, 1 searching, 2 "Voting Starts in", 3 map vote, 4 difficulty vote, 5 waiting for players, 6 "Game starting in"); u32 at +0x408 a start gate word (the value 2 lets the start through) |
| 0x17d603c | u32 | the net menu mode: 1 = Find Game, 2 = Custom Game |
| 0x17d6420 | object | the match session ("MS"), fields as G13 3.5 / G14 3.3: pointer at +8 = the backend it is attached to; u64 at +0x18 = room id (0 = none); u32 at +0xf8 and +0xfc = search words; members from +0x680; u32 at +0x12e4 own member id; u32 at +0x12e8 the host's member id; u8 at +0x1338 the "start loading" flag |
| 0x17d7760 | block | the map vote ("V"): u8 at +0 open, u8 at +1 closed, s32 at +8 and +0xc the two candidate net game indexes, s32 at +0x10 and +0x14 their vote counts |
| 0x17d7784 | block | the difficulty vote ("W"): u8 at +0 open, u8 at +1 closed, s32 at +4 this player's vote (-1 none, 0 .. 3), s32 at +8, +0xc, +0x10, +0x14 the counts for Easy, Normal, Hard, Crushing |
| 0x17d79e4 | u8 | the "leave Find Game" mark |
| 0x17dd610 | pointer | the profile objects (G23 3.5); in the first profile object (the address this pointer holds): u32 at +0x774 = the id of the playlist the player picked; s32 at +0x250 + 4·n = unlock counter n |
| 0x17dd768 | pointer | the registered session manager (an object whose first 8 bytes point to its function table) |
| 0x1afcf10 | object | the level manager |
| 0x1b68810 | u32 | the game's net state ("NS"): 0xb and 0xf = joining a room, 0x11 and 0x28 = creating one, 0x12 = idle in a room, 0x14 (20) = matchmaking page, 0x2c .. 0x30 = the match start, 0x30 = waiting to load |

Function table entries of a backend / session manager object (object first, results s64, as G14): entry 5 =
Search `(object, session, u32, u32, s32, s32, s32, u32)`; entry 7 = Leave `(object, session)`.

Script symbols: 0xc1b0be0d = the playlist table (read through `u3x_pick_playlist` only). 0x3b9a067d = the net
game table: s32 count at +0, pointer at +8 to records of 0xd0 bytes; record + 0xc = game type (u32), record + 0xb8
= the unlock counter index of the net game's level (u32).

"The picked playlist" = `u3x_pick_playlist(u3x_sym_table(s, 0xc1b0be0d), id)` with id = the u32 at +0x774 of the
object the pointer at image 0x17dd610 points to, or 0 when that pointer is null. One symbol lookup per use.

"Set status S for T seconds" = the s32 at NET + 0x40 = S; then the s32 at NET + 0x2c = `u3x_net_clock(base)` +
300·T (wrapping).

### 3.6 Game functions called directly

| Image offset | Arguments | Result | Meaning |
|---|---|---|---|
| 0x75c190 | (list request) | none | the game's own answer for the status line of the players list |
| 0x7a2790 | (MS, u32 pool id) | not used | gives the match session the pool to search in |
| 0x720500 | (NGM) | none | resets the net game manager's match state before a search |
| 0x5b7040 | (lobby scene, int 1) | none | the local player leaves the lobby scene |
| 0x72b570 | (NGM, int 1, int 1, int 1, int 0, int 0) | none | match set-up in "lobby" mode: the lobby stage becomes the one of the selected net game, without the match's level loads |
| 0x5b34f0 | (lobby scene) | none | lines the players up in the lobby scene |
| 0x11f2bf0 | (u32 symbol id) | pointer to a pointer, or null | the script symbol lookup |
| 0x10ee0e0 | (level manager, int 1) | int; only the low 8 bits count | non-zero = the levels the match needs are loaded |

Page handlers that 5.8 runs (each takes the address of an event record and returns nothing that is used):
0x6a6900 = the join handler, 0x6a5fa0 = the create handler, 0x6a7310 = the match start handler (and 0x6a6830, the
result choice, from the C part).

### 3.7 Ids, texts and timings

| What | Value |
|---|---|
| text "Finding Player..." | 0xc0759450 |
| text "Found Player" | 0xa91680a8 |
| popup text "Starting in ..." | 0x73a73dd8, shown for 2 seconds; its ok action 0x4055f45f, its cancel action 0xbbb649ef |
| party message "the leader starts a search" | 0x114, no data |
| the helper's own row text | `Player ` followed by a decimal number |
| pages | 0x69e10dad the match loading page; 0x82e9ddd3 the page shown under it; 0xb99a3235 the lobby; 0x53cbb5f2 the multiplayer page; 0x1af4bdd2 the "joining" page; 0x471007a1 the popup page |
| ticks per second | 300 |
| a just found row shows "Found Player" for | 180 ticks |
| stand-in difficulty votes | at most one every 300 ticks |
| status times | map vote result 45 s, difficulty vote 10 s, after the difficulty vote 25 s |

This group holds no table of game values.

# Part A

## 4. Behaviour of part A

### 4.1 `u3x_m03_players(base, ev, s, self_name)`

M = `u3h_m03()`.

1. When M word 1 (running) is 0, or the s32 at NET + 0x40 is 0 or negative: result 0, nothing else happens.
2. op = word 0, row = word 1 as u32. Word 7 of `u3h_m03_diag()` plus 1.
3. By operation, each with result 1:
   - 1: game call 0x75c190(ev).
   - 6: word 1 = M word 5 - 1; then, in this order, `u3x_clan_tag_poll(base, s)`, `u3h_hud_alias_names(base,
     s)`, `u3h_stage_tick(base, s)`, `u3h_loadout_tick(base, s)`.
   - 5: word 1 = M word 6.
   - 10: M word 6 = word 1.
4. Every other operation: when row >= M word 5 (unsigned): result 1, nothing written.
5. The row's kind:
   - "searching" when row >= M word 4 (found);
   - otherwise "found", except that a found row other than row 0 is "just found" when
     - fill mode (eb[2] bit 13 and eb[3] bit 16 both clear): M word 3 (phase) <= 2;
     - not fill mode: row + 1 equals M word 4, and the phase is 2, and (game clock as u32 - M word 7), an
       unsigned 32-bit difference, is below 180.
   Row 0 is never "just found".
6. Operation 4 with word 0x14 = 0, in fill mode, for a row other than 0 that is not "searching": the coloured
   image answer of kind 31 with value 5 - ((row · 7) mod 3) (so 5, 4 or 3 bars) and colour 0x8000ff00. Result 1.
7. Operation 2 (result 1 in every case):
   - word 0x15 not 0: the text answer becomes the empty text;
   - "searching": `u3h_text(base, s, 0xc0759450)`;
   - "just found": `u3h_text(base, s, 0xa91680a8)`;
   - "found", row 0: `self_name` (the pointer as it is);
   - "found", other rows: the text `Player ` followed by row + 1 in decimal without leading zeros, built in a
     buffer of the new code that stays valid until the next such answer (24 characters are enough).
8. Operation 4 with word 0x14 = 0xf5a294f3 (result 1 in every case):
   - "found": the rank is, for row 0, native 21 called with `u3h_player_profile(base)`; when native 21 or that
     profile is null, nothing is written. For other rows the rank is 1 + ((row · 17 + 5) mod 60). Then the
     coloured image answer of kind 0x29 with the rank as value and colour -1 (= the plain image answer), then
     `u3h_rank_icon(base, s, ev)`.
   - "searching": the image answer of kind 16 with value 0.
   - "just found": nothing is written.
9. Operation 20: when ec[2] bit 4 is clear: result 1, nothing written. Otherwise, when word 0x16 is 2 and the
   row is "found": word 0xf = (`u3h_team_colour(base, row & 1)` AND 0xffffff) OR 0x80000000.
10. Result 1 (also for every operation and slot not named above; nothing is written for them).

### 4.2 `u3x_onm_players(base, ev, s, self_name)`

O = `u3h_onm()`.

1. Result 0, nothing else, unless all three hold: `u3h_onb_mode()` is 1, O word 1 (running) is not 0, the s32 at
   NET + 0x40 is greater than 0.
2. op = word 0, row = word 1 as u32. By operation, each with result 1:
   - 1: game call 0x75c190(ev).
   - 6: word 1 = O word 6 - 1; then `u3x_clan_tag_poll(base, s)`, `u3h_hud_alias_names(base, s)`,
     `u3h_stage_tick(base, s)`, `u3h_loadout_tick(base, s)`, in this order.
   - 5: word 1 = O word 5.
   - 10: O word 5 = word 1.
3. Every other operation: when row >= O word 6 (unsigned): result 1, nothing written.
4. The row's player: row 0 is this player, with the name `self_name`. Another row has a player only when the
   match session has a room (u64 at MS + 0x18 not 0) and `u3x_other_member(MS, row)` returns a name; that name is
   the row's. Otherwise the row is empty.
5. Operation 2 (result 1): word 0x15 not 0: the empty text. Otherwise a row with a player: the text answer becomes
   its name (for row 0 the pointer `self_name` as it is); an empty row: `u3h_text(base, s, 0xc0759450)`.
6. Operation 4 with word 0x14 = 0xf5a294f3 (result 1):
   - empty row: the image answer of kind 16 with value 0;
   - row 0: when native 21 or `u3h_player_profile(base)` is null, nothing is written; otherwise rank = native 21
     called with that profile;
   - another row with a player: rank = `u3h_ox_rank_of(name)` when that is 0 or more, else 1;
   - then (rows with a player): the image answer of kind 0x29 with the rank as value, and `u3h_rank_icon(base, s,
     ev)`.
7. Result 1 for everything else, nothing written (no bars, no tint).

# Part B

## 5. Behaviour of part B

### 5.1 `u3x_m03_startmatch(base, s)`

1. Result 0, nothing else, unless all three hold: eb[2] bit 13 is clear, `u3h_onb_mode()` is 2, the net menu mode
   (image 0x17d603c) is 1.
2. Word 0 of `u3h_m03_diag()` plus 1.
3. V + 0 = 0 and V + 1 = 0 (the two flag bytes of the map vote); `u3h_n09_reset(base)`.
4. The pointer at byte 0x20 of `u3h_m03()` = the picked playlist (3.5).
5. `u3x_party_lock(base, 0)`; then `u3h_party_send(base, 0x114, null, 0)`.
6. `u3x_popup_timed(base, s, 0x73a73dd8, 2, 0x4055f45f, 0xbbb649ef)`. Only when it returns non-zero: word 0 of
   `u3h_m03()` = 1, word 1 = 0; `u3h_n02_arm(the game clock as u32)`; the u8 at image 0x17d79e4 = 0.
7. Result 1 (also when the popup was not shown).

### 5.2 `u3x_onb_startmatch(base, s)`

1. Result 0, nothing else, unless `u3h_onb_mode()` is 1 and the net menu mode is 1.
2. Word 3 of `u3h_k44_diag()` plus 1; `u3h_onm_arm(base)`.
3. V + 0 = 0, V + 1 = 0; `u3h_n09_reset(base)`.
4. `u3x_party_lock(base, 0)`; `u3h_party_send(base, 0x114, null, 0)`.
5. pool = `u3x_playlist_pool(the picked playlist)` (0 without a playlist or a pool list). Game call
   0x7a2790(MS, pool).
6. The u8 at image 0x17d79e4 = 0.
7. `u3x_popup_timed(base, s, 0x73a73dd8, 2, 0x4055f45f, 0xbbb649ef)`; its result is not used.
8. Result 1.

### 5.3 `u3x_online_search(base)`

The session manager pointer (image 0x17dd768) is read once, at the start.

1. The u8 at NET + 0x18 = 0; the s32 at NET + 0x40 = 0.
2. B = the pointer at MS + 8. When B is not null and the function table pointer in its first 8 bytes is not null:
   call the table's entry 7 with (B, MS) (Leave); result not used.
3. Game call 0x720500(NGM).
4. The u32 at MS + 0xf8 = the u32 at NET + 0x10; the u32 at MS + 0xfc = 1 when the u64 at NET + 0x10 is not 0,
   else 0.
5. When the session manager pointer is null or the function table pointer in its first 8 bytes is null: result
   -1.
6. Call the manager's table entry 5 with (manager, MS, 0, 0, -1, 0, 0, 0) (Search). A negative result (s64):
   result -1.
7. `u3h_onm_state(base, 6)`. Result 0.

Steps 1 to 4 happen in every case, also when the search then cannot be sent.

### 5.4 `u3x_n04_close(base, s)`

1. Winner: the s32 at V + 0xc when the s32 at V + 0x14 is greater than the s32 at V + 0x10 (signed), else the s32
   at V + 8. A tie goes to the first candidate.
2. `*u3h_n04_idx()` = -1. When the winner is negative or native 2 is null: result 0.
3. Native 2 called with (NGM, winner, null, 0xc). `*u3h_n04_idx()` = winner. Word 0 of `u3h_n04_diag()` plus 1;
   word 1 = the winner.
4. Set status 6 for 45 seconds (3.5).
5. V + 1 = 1 (the vote is closed).
6. Unless ec[2] bit 3 is set, in this order: game call 0x5b7040(lobby scene, 1); game call 0x72b570(NGM, 1, 1,
   1, 0, 0); word 2 of the diagnostic words plus 1; game call 0x5b34f0(lobby scene); then the diagnostic words 4 =
   the net state NS, 5 = `u3h_crc_sid(text at NGM + 0x74b8)` when that text's first character is not NUL, else 0,
   6 = (the u32 at lobby scene + 0 AND 0xff) OR ((the u32 at lobby scene + 4 AND 0xff) shifted left 8).
7. Result 1.

### 5.5 `u3x_n09_open(base, s)`

1. ec[2] bit 7 set: result 0, nothing else.
2. R = `u3x_mmv_netgame(s, the s16 at NGM + 0x661c)`. Null: result 0.
3. t = the u32 at R + 0xc. ec[254] = t AND 0xff (the whole word is replaced).
4. `u3x_has_difficulty_vote(t)` is 0: result 0.
5. `u3x_vote_open(W)`. Set status 4 for 10 seconds. ec[252] plus 1. Result 1.

### 5.6 `u3x_n27_crush_locked(base)`

A stand-in voter may vote Crushing only when Crushing is unlocked for the local player on the selected net game's
level. Pure read apart from one game call; result 0 in each of these cases, tested in this order:

1. h = game call 0x11f2bf0(0x3b9a067d) is null, or the pointer h holds (the net game table T) is null;
2. i = the s16 at NGM + 0x661c is negative or not below the s32 count at T + 0;
3. the records pointer at T + 8 is null;
4. u = the u32 at +0xb8 of record i is 0x20 or more;
5. P = the pointer at image 0x17dd610 fails `ptr_ok` (CONVENTIONS.h).

Otherwise the result is 1 when the s32 at P + 0x250 + 4·(u + 0x58a) is 0 or negative, else 0.

### 5.7 `u3x_n09_tick(base, now)`

c0 .. c3 = the four counts of W (Easy .. Crushing); T = `*u3h_m05_vt()`.

1. **Stand-in vote.** Only when (`now` - T), an unsigned 32-bit difference, is 300 or more:
   - total = c0 + c1 + c2 + c3 (as u32); others = (word 5 of `u3h_m03()`) - 1 when that word is greater than 1,
     else 1; mine = 1 when the s32 at W + 4, taken as u32, is below 4 (this player has voted), else 0.
   - When total < others + mine: k = `u3x_stand_in_vote(now)`; when k is 3 and `u3x_n27_crush_locked(base)` is
     non-zero: k = 2 and ea[169] plus 1; the count of row k plus 1; ec[255] plus 1.
   - In both cases T = `now`.
2. **Deadline.** When (`u3x_net_clock(base)` - the s32 at NET + 0x2c), a wrapping difference taken as signed, is
   negative: result 0.
3. **Winner.** From the four counts as they are now, with one change made on a copy only: when Crushing has
   exactly one vote, that vote counts for Hard (Hard + 1, Crushing 0). The winner is the row with the highest
   count above 0; among equal counts the lowest row; without any vote row 1 (Normal).
4. The u8 at NGM + 0x6a48 = winner + 1. W + 1 = 1 (closed). Set status 6 for 25 seconds.
5. ec[252] plus 0x10000.
6. ec[253] = winner OR (c0 AND 15) shifted left 8 OR (c1 AND 15) shifted left 12 OR (c2 AND 15) shifted left 16
   OR (c3 AND 15) shifted left 20 OR (winner + 1) shifted left 28, with the real counts (not the copy of step 3).
   ec[254] = (ec[254] AND 0xff) OR ((the u32 at W + 4 AND 0xff) shifted left 8).
7. Result 1.

### 5.8 `u3x_mm_ev6(base, fn, page)`

Builds an event record of 24 bytes on its own stack: u32 at +0 = 6 (the event number); u32 at +4 = `page`; u64 at
+8 = 0; u8 at +0x10 = 0; u8 at +0x11 = 1; the six bytes from +0x12 = 0. Then calls the game function at image
offset `fn` with the record's address as the only argument. Nothing else.

### 5.9 `u3x_mm_start_tick(base, s)`

BG = `*u3h_mm_bg()`, read at the moment a step tests it; Q = `*u3h_mm_quiet()`. "Selected" = the s16 at NGM +
0x661c, read at the moment a step tests it.

1. ns = NS (image 0x1b68810) and pg = `u3h_active_page(base)`, both read once.
2. "Starting" = ns is 0x2c .. 0x30. "Joining" = BG is non-zero and ns is 0xb, 0xf, 0x11 or 0x28. When neither:
   BG = 0 unless ns is 0x12; done.
3. pg is 0: done.
4. **Backed out.** When BG is non-zero, ns >= 0x11 and pg is 0xb99a3235 or 0x53cbb5f2: BG = 0; `u3h_show(base, s,
   0x82e9ddd3, 0)`; `u3h_show(base, s, 0x69e10dad, 0)`; done.
5. **Joining.** Q = 1. When ns is 0x11 and Selected is negative: Q = 0; done (the create handler needs a selected
   net game). Otherwise `u3x_mm_ev6(base, 0x6a6900, 0x1af4bdd2)` when ns is 0xb or 0xf, and `u3x_mm_ev6(base,
   0x6a5fa0, 0x471007a1)` when ns is 0x11 or 0x28. Then Q = 0; done.
6. **Starting.** pg is 0x69e10dad: done (the page's own flow runs the handler).
7. When BG is 0 and `u3x_page_in_stack(base + 0x17bd080, 0x69e10dad)` is 0: done.
8. Only when ns is 0x30: the match loading page is opened when all four hold, tested in this order (a later test
   is not made when an earlier one fails):
   - due: (the s32 at NET + 0x2c) - (the game clock, s32 at image 0x17b4ba4), a wrapping s32 difference, is 0 or
     negative;
   - go: the u8 at MS + 0x1338 is not 0, or the u64 at MS + 0x18 is 0, or the u32 at MS + 0x12e4 equals the u32
     at MS + 0x12e8;
   - the u32 at NET + 0x408 is 2;
   - the low 8 bits of game call 0x10ee0e0(level manager, 1) are not 0.
   Then: when BG is non-zero, `u3h_show(base, s, 0x82e9ddd3, 0)`; `u3h_show(base, s, 0x69e10dad, 0)`; BG = 0.
9. Selected is negative: done.
10. `u3x_mm_ev6(base, 0x6a7310, 0x69e10dad)`.

### 5.10 `u3x_page_in_stack(menu, page)`

d = the s32 at `menu` + 0xdb0. For k from 0 up to and including d, but never beyond k = 159: P = the pointer at
`menu` + 0x8b0 + 8·k; when P is not null and the u32 at P + 0x10 equals `page`: result 1. Otherwise result 0
(also for a negative d). No call, nothing written.

## 6. Test scenarios

[fake] = old against new on a fake image (a zeroed buffer that reaches image offset 0x1b68818) with call-recording
stubs: at the eight game function offsets of 3.6 and at the three handler offsets of 3.6, for natives 2 and 21,
the show-page and symbol-lookup functions of the state object, the function tables of a fake backend and a fake
session manager, and for every function of 2.2 that has an effect. Compare results, the image memory, the
helper's words, and the order and arguments of the calls. Text answers are compared by the text they point to.
[live] = needs the running game.

### 6.1 Part A [fake]

Stand-in list, with M running = 1, NET + 0x40 = 1, max = 6, found = 3 unless said:

1. Not running, or status 0 or -1: result 0, request untouched, diagnostic word 7 unchanged.
2. Operation 1: one call 0x75c190 with the request; operation 6: word 1 = 5, the four jobs in order; max = 0:
   word 1 = -1; operation 5 returns the selected row; operation 10 stores it. Word 7 counts each.
3. Row 6 and row 0xffffffff, operations 2, 4 and 20: result 1, nothing written.
4. Kinds, fill mode, phase 2: rows 1 and 2 "just found" (text 0xa91680a8), row 0 `self_name`, rows 3 .. 5
   text 0xc0759450. Phase 3: rows 1 and 2 `Player 2`, `Player 3`. Found = 10, max = 10, phase 3: row 9 gives
   `Player 10`.
5. Kinds, not fill mode (eb[3] bit 16 set, and again with eb[2] bit 13 set): phase 2, clock - word 7 = 179: only
   row 2 (= found - 1) is "just found", row 1 is `Player 2`; difference 180: row 2 is `Player 3`; phase 3: none
   is "just found". A clock below word 7 (wrapped difference huge): not "just found".
6. Operation 2 with word 0x15 = 1: the empty text for every kind.
7. Bars: fill mode, word 0x14 = 0, rows 1 .. 5 with found = 6: values 5 - (7·row mod 3) = 4, 3, 5, 4, 3; word 11
   = 0x8000ff00; "just found" rows get them too; row 0, a searching row, and every row outside fill mode: nothing
   written.
8. Rank badge (word 0x14 = 0xf5a294f3): row 0 with native 21 returning 0x1234: kind 0x29, value 0x1234, word 11
   = -1, then `u3h_rank_icon`; native 21 null, or no profile: nothing written, no `u3h_rank_icon`. Row 2 in phase
   3: value 1 + (39 mod 60) = 40. A searching row: kind 16, value 0, no `u3h_rank_icon`. A "just found" row:
   nothing.
9. Operation 20: ec[2] bit 4 clear: nothing. Set, word 0x16 = 2, row 1 found, colour stub returning 0x11223344
   for team 1: word 0xf = 0x80223344; row 2: team 0; word 0x16 = 1, a searching row, a "just found" row: nothing.
10. Operation 4 with another slot id, operation 3, operation 0x99: result 1, nothing written.

Online list, with mode 1, O running = 1, max = 4, status 1:

11. Mode 2, or not running, or status 0: result 0, untouched.
12. Operations 1, 6, 5, 10 as in 2 (on O words 6 and 5).
13. No room (MS + 0x18 = 0): row 0 has `self_name` and the own rank; rows 1 .. 3 are empty (text 0xc0759450,
    kind 16); `u3x_other_member` is not consulted.
14. Room set, two other members: rows 1 and 2 show their names; rank from `u3h_ox_rank_of` (stub: 7 for the
    first, -1 for the second gives 7 and 1); row 3 empty. Row 0 with native 21 null: nothing written.
15. Row 4: result 1, nothing written. Operation 20: result 1, nothing written. Word 0x15 = 1: the empty text.

### 6.2 Part B [fake]

16. `u3x_m03_startmatch`: each of the three conditions failing alone gives 0 with nothing changed. Taken, popup
    stub returning 1: counter, V bytes cleared (preset to 1), reset call, playlist pointer stored (playlist
    table stub with two entries and the picked id matching the second; again with no match: the first; again
    with a null profiles pointer: id 0; again with the lookup returning null: null pointer stored), lock(0),
    party message 0x114 with null data and length 0, popup with the five values, armed = 1, running = 0 (preset
    to 1), `u3h_n02_arm` with the clock, leave mark cleared (preset to 1). Popup stub returning 0: result 1,
    armed and running unchanged, no `u3h_n02_arm`, leave mark unchanged.
17. `u3x_onb_startmatch`: mode 2 or net menu mode 2: result 0. Taken: the calls in the order of 5.2; pool from
    the playlist's pool list (a list with ids 77, 78 gives 77; no list gives 0; no playlist gives 0); 0x7a2790
    called with (MS, pool); the leave mark is cleared before the popup; result 1 whatever the popup returns.
18. `u3x_online_search`: (a) manager and backend present, Search returning 0: NET bytes cleared (preset
    non-zero), Leave on the session's backend with (backend, MS), 0x720500(NGM), MS + 0xf8 / + 0xfc from NET +
    0x10 (0x0000000500000000 gives 0 and 1; 7 gives 7 and 1; 0 gives 0 and 0), Search with (manager, MS, 0, 0, -1,
    0, 0, 0), net state 6, result 0. (b) Search returning -1: result -1, the net state untouched. (c) no manager,
    or a manager with a null table: steps 1 to 4 done, result -1, no Search. (d) MS + 8 null, or a backend with a
    null table: no Leave, the rest as (a).
19. `u3x_n04_close`: counts (2, 3) pick the second candidate, (3, 3) and (4, 1) the first; a negative winner or
    native 2 null: result 0 with `*u3h_n04_idx()` = -1 (preset to 5) and nothing else changed. Taken: native 2
    with (NGM, winner, null, 0xc), status 6, deadline = net clock + 13500 (also with the clock pointer null: 13500;
    and with a clock near the s32 limit: wraps), V + 1 = 1, the three game calls in order with their arguments,
    the diagnostic words (stage name `abc` gives its check value, an empty name gives 0). With ec[2] bit 3 set:
    no game call, diagnostic words 2, 4, 5, 6 untouched, result 1.
20. `u3x_n09_open`: switched off; no net game (index -1, index = count, no table); a type without a vote (0, 18,
    0x104); a type with one (4, and 17): W prepared (preset with other values), status 4, deadline = net clock +
    3000, ec[252] + 1, ec[254] = the type's low byte (type 0x104 writes 4 and returns 0).
21. `u3x_n27_crush_locked`: each of the five "result 0" cases; then a counter of 0 and of -1 give 1, a counter of
    1 gives 0; an unlock index of 0x1f is tested, 0x20 is not; a misaligned or small profiles pointer gives 0.
22. `u3x_n09_tick`, voting: `now` - T = 299: no vote and T unchanged; = 300: T = `now`. With max = 4 (three
    others): counts summing to 3 and no own vote: no vote added; own vote set (W + 4 = 2) and counts summing to
    3: one vote added; max = 1 or 0: one other. For `now` values that give each row 0 .. 3 (by
    `u3x_stand_in_vote`): the count of that row + 1 and ec[255] + 1; row 3 with the locked stub returning 1: Hard
    + 1 instead and ea[169] + 1; the locked test is not made for rows 0 .. 2.
23. `u3x_n09_tick`, closing: net clock one tick before the deadline: result 0, nothing closed; at the deadline and
    after it (also across the s32 wrap): closed. Counts (Easy, Normal, Hard, Crushing): (0, 0, 0, 0) gives Normal
    (difficulty byte 2); (1, 0, 0, 0) Easy; (0, 0, 0, 1) Hard (3); (0, 0, 0, 2) Crushing (4); (2, 2, 0, 0) Easy;
    (0, 1, 0, 1) Normal (tie with the moved vote, lower row); (0, 0, 1, 1) Hard; (17, 0, 0, 0): ec[253] holds 1
    in the Easy field. W + 1 = 1, status 6, deadline = net clock + 7500, ec[252] + 0x10000, ec[254] keeps its low
    byte and takes the own vote's low byte (0xff for none). A vote added in the same call counts for the winner.
24. `u3x_mm_ev6`: the handler stub receives a record whose 24 bytes are as in 5.8 (the stub copies them), for
    two different offsets and pages; one call each.
25. `u3x_page_in_stack`: depth -1: 0; depth 0 with the page: 1; the page at index 2 with depth 2: 1, with depth
    1: 0; null entries are skipped; depth 500 with the page at index 159: 1, at index 160: 0.
26. `u3x_mm_start_tick`, table of cases (BG, ns, pg, Selected, other facts -> effects):
    - ns = 0x14, BG = 1: BG cleared, nothing else. ns = 0x12, BG = 1: BG kept. ns = 0xb, BG = 0: nothing
      (0xb is not 0x12, BG stays 0).
    - ns = 0xb, BG = 1, pg = 0: nothing, BG kept.
    - ns = 0x11, BG = 1, pg = the lobby (and again the multiplayer page): BG = 0, the two pages shown in order,
      no handler. ns = 0xf, BG = 1, pg = the lobby: not "backed out" (ns below 0x11): join handler runs.
    - ns = 0xb and 0xf, BG = 1, pg = another page: Q is 1 during the call of 0x6a6900 (the stub reads it) with
      page 0x1af4bdd2, and 0 after. ns = 0x11 and 0x28: 0x6a5fa0 with page 0x471007a1. ns = 0x11 with Selected
      = -1: no handler, Q = 0; ns = 0x28 with Selected = -1: the handler runs.
    - ns = 0x2c, pg = 0x69e10dad: nothing. ns = 0x2c, BG = 0, the loading page not on the stack: nothing; on
      the stack (below the top): 0x6a7310 with page 0x69e10dad; BG = 1 and not on the stack: the same.
      Selected = -1: no handler.
    - ns = 0x30, BG = 1, pg = another page (not the lobby, not the multiplayer page): with all four gates open:
      both pages shown, BG = 0, then the handler; with BG = 0 and the loading page on the stack: only the
      loading page shown. Each gate closed alone (deadline one tick ahead of the game clock; start-loading flag 0
      with a room and a different host id; NET + 0x408 = 1; level call returning 0x100): no page, the handler
      still runs; the level call is made only when the first three gates are open. Each of the three "go"
      alternatives opens the gate alone.
    - ns = 0x30, BG = 1, pg = the lobby: "backed out" (step 4), not step 8.
    - ns = 0x2b and 0x31: neither starting nor joining.

### 6.3 What needs the live game [live]

- Offline: Find Game, a playlist, Start Match: the popup, the matchmaking page with "Finding Player..." rows that
  turn into "Found Player" with bars, then names and rank badges; the map vote; at its end the winning map's card
  and the lobby stage of that map with the character on it; on a co-op playlist the Difficulty list with counts
  going up about one a second and no stand-in vote on a locked Crushing row; "Game starting in"; the match.
- Online: Start Match as party leader and alone: the search is sent, a room is joined or hosted, the list shows
  the room's members with their names and ranks, and the match start runs with another page open (the loading
  page appears once everyone is ready).
- That `u3h_rank_icon` turns the badge answers into the rank art, and what the status line shows for each status.

## 7. Things in the old code that look like bugs or dead code (behaviour to keep)

1. `u3x_m03_startmatch` returns 1 also when the popup could not be shown: the press is swallowed, the party has
   been unlocked and told 0x114, and nothing is armed.
2. `u3x_onb_startmatch` does not look at the popup's result at all: the online driver is armed and the pool set
   before the popup, whatever happens to it.
3. The picked playlist comes from the first profile object (the pointer at image 0x17dd610 itself), not from the
   menu user's profile, while the own rank comes from the menu user's profile.
4. `u3x_online_search` clears the status, leaves the room, resets the net game manager and writes the search
   words even when there is no session manager; it leaves through the session's own backend pointer but searches
   through the registered manager.
5. `u3x_n04_close` sets "selected at the vote's end" to -1 before its tests, so a failed close forgets an earlier
   selection. The winner rule gives a tie to the first candidate.
6. `u3x_n09_tick`: the "last vote" time is renewed every 300 ticks whether or not a vote was added. The limit of
   stand-in votes compares the total of all counts, the player's own vote included, with others + (1 when the
   player voted). The summary word shows each count modulo 16. Counts are never tested for being negative.
7. `u3x_n09_tick`: the "one Crushing vote counts as Hard" change is applied to the winner only; the counts in
   the vote block stay as cast.
8. `u3x_mm_start_tick` step 8 compares the status deadline with the game clock (image 0x17b4ba4), while 3.5's
   "set status" computes that deadline from the net clock (`u3x_net_clock`). Keep both as they are.
9. `u3x_mm_start_tick` shows pages through `u3h_show` without a test for a missing show-page function.
10. `u3x_mm_start_tick` step 5: the "selected net game" guard exists for state 0x11 only, not for 0x28.
11. `u3x_mm_ev6`: the byte at +0x11 of the record is 1; nothing in the helper says what the handlers read there.
12. `u3x_m03_players`: a "just found" row answers nothing for the rank badge slot; row 0 without native 21 or
    without a profile answers nothing; outside fill mode the bars are never answered; in fill mode every found
    row other than row 0 is "just found" for as long as the phase is 1 or 2.
13. `u3x_m03_players`: with max = 0 the row count answer is -1.
14. `u3x_onm_players`: no answer for operation 20 and for the connection picture; a member whose rank is not known
    shows rank word 1.
15. `u3x_page_in_stack` looks at index 0 up to and including the top index, but at no more than 160 entries.
16. `u3x_n09_open` replaces the whole of ec[254] (the own-vote byte of the previous close is lost) also when it
    then returns 0 for a type without a vote.

## 8. What stays as it is

- The callers of section 1 and the C exports `u3h_onb_startmatch`, `u3h_m03_startmatch`, `u3h_onm_players`.
- The helper's own state (3.4), the C statics behind the NEW exports, and all the C code that drives them
  (`m03_tick`, `m05_tick`, `m03_stop`, `onm_tick`, `onm_arm`, `onm_ready`, `n09_reset`, the stub that reads the
  quiet flag): the helper's own logic, not part of this group.
- The existing small C++ functions named in 2.2 as "exists" (`u3x_pick_playlist`, `u3x_playlist_pool`,
  `u3x_other_member`, `u3x_vote_winner`, `u3x_vote_open`, `u3x_stand_in_vote`, `u3x_has_difficulty_vote`,
  `u3x_mmv_netgame`, `u3x_mm_starting`, `u3x_mm_joining`, `u3x_net_clock`): they are not in this group and are
  called as they are.
- Nine one-line C exports have to be added by the integrator (2.2, "NEW"): `u3h_onm`, `u3h_onm_arm`,
  `u3h_k44_diag`, `u3h_n04_idx`, `u3h_n04_diag`, `u3h_m05_vt`, `u3h_mm_bg`, `u3h_mm_quiet`, and the two G30 also
  asks for (`u3h_n09_reset`, `u3h_n02_arm`; one copy of each is enough).

## 9. Open questions

1. The u64 at NET + 0x10 that the search copies into the match session (5.3 step 4): G14 calls MS + 0xf8 the
   "game type word"; what the second word (MS + 0xfc, "the 64-bit value is not 0") means to the game is not
   known. The copy is kept as it is.
2. 7.8: whether the start gate of 5.9 should use the net clock. Not changed here.
3. What the list's operation 20 with sub-question 2 expects exactly in word 0xf beyond "a colour with bit 31
   set" is not known (G27 has the same question for sub-question 1).
4. The names of the NEW exports are proposals. If the integrator prefers fewer exports, `u3h_k44_diag` and
   `u3h_onm_arm` can be one export that counts the press and arms the driver; step 2 of 5.2 then becomes
   one call.
5. The old private function `icon` (the coloured image answer of 3.3) is delivered by G37 as `u3x_icon_tinted`
   and called by `u3x_m03_players`. The export is needed: this spec does not ask part A to write the words itself.

## 10. Clarifications (tester, 2026-11-14)

Facts confirmed by the old-against-new test (`reimpl2\G36\test_g36.cpp`) and, where said, in the shipped v7.212
helper.

1. **The two deadline tests are plain signed comparisons, not wrapping differences** (5.7 step 2, 5.9 step 8
   "due"; corrects both and scenario 23's "also across the s32 wrap"). In the shipped helper:
   - 5.7 step 2: the vote stays open (result 0) when `u3x_net_clock(base)`, as an s32, is less than the s32 at
     NET + 0x2c; otherwise it closes.
   - 5.9 step 8: "due" holds when the s32 at NET + 0x2c is less than or equal to the game clock (s32 at image
     0x17b4ba4).
   The two readings give the same answer unless the two values lie on different sides of the s32 limit (one
   negative, one positive, 2^31 or more apart as numbers). Examples of the shipped behaviour: net clock 0x7fffffff
   with deadline 0x80000000: the vote closes at once (the wrapping difference would say "one tick left"); net
   clock 0x80000005 with deadline 0x7ffff005: the vote stays open (the wrapping difference would say "reached");
   game clock 0x7fffffff with NET + 0x2c = 0x80000000: "due" holds. "Set status" (3.5) still computes the
   deadline with a wrapping addition.
2. Open question 2 / 7.8: confirmed, step 8 of 5.9 compares with the game clock (image 0x17b4ba4), while the
   deadline was computed from the net clock.
3. `u3x_other_member` is a pure read of the session's member slots; how often it is called is not behaviour.
   v7.212 asks it for every request on a row other than 0 that is inside the list and is not operation 1, 5, 6 or
   10 while the match session has a room (also for operation 20, for other image slots and for a text column
   other than 0), and then does not use the answer.
4. 5.1 step 6: `u3h_n02_arm` stands for two direct writes of v7.212 ("popup gone" count = 0, "armed at" = the game
   clock) that sit between "running = 0" and the clearing of the leave mark, with no call in between. Any
   non-zero popup result arms (tested with 1, 2 and 0xff).
5. 5.3 step 6: the Search result is tested as 64 bits: 0x80000000 and 0xffffffff are "sent" (result 0, net state
   6); only a result with bit 63 set gives -1. The session manager pointer read at the start is the one used at
   steps 5 and 6 even when the Leave call or game function 0x720500 changes the pointer at image 0x17dd768; the
   u64 at NET + 0x10 is read after those two calls.
6. 5.4 step 6: the three diagnostic values (net state, stage name, lobby scene words) are read after the three
   game calls (they show what the calls left). Word 2 is counted between the second and the third call.
7. 5.7: all four counts are added as 32-bit values that wrap; the "one Crushing vote" change adds 1 to the Hard
   copy with wrapping. Negative counts never win (the winner needs a count above 0) and a count of exactly 1 in
   Crushing moves also when Hard is negative.
8. 5.9: the background flag and the selected net game index are read again after each call (the level manager
   call, `u3h_show`): step 8 shows the page under loading only if the flag is still set after the level call,
   and step 9 tests the index as it is after the pages were shown. In step 8 v7.212 reads the three "go" values
   before it tests "due"; the level manager call is made only when "due", "go" and the gate word all hold.
9. 4.1 / 4.2: the bytes of the "Player N" buffer behind the terminating NUL are left as they were. The empty
   text and the "Player N" text are the only private texts; every other text answer is the pointer the caller or
   `u3h_text` gave.
