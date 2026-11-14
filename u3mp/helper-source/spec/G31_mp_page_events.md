# G31. PAGE_MULTIPLAYER events, lobby menu and party list sources

Behaviour description for a reimplementation (reimpl2 pipeline, describer's document). It says what the functions
do, not how the old ones were written. All offsets are hexadecimal when prefixed with 0x. "Image offset X" means the
address `base + X`, where `base` is the game image base every function receives. All multi-byte values are
little-endian; "u32 / s32 / u16 / u64 / u8" are unsigned / signed integers of that width, "pointer" is 8 bytes.
"Word n" of a request or of a page event means the 32-bit word at byte offset 4·n. Names and conventions follow
`G23_boosters.md` (list requests, image answers, "native k", "menu user", ea[n] / eb[n]) and `G08_lan_room_client.md`
(the party session "SES").

This group has no float arithmetic. One float pattern (0x3f800000 = 1.0) is stored as a plain 32-bit word.

## 1. Purpose and callers

The PS4 game kept the multiplayer lobby page (PAGE_MULTIPLAYER, page id 0x53cbb5f2) and its controls, but the code
behind them is empty: the page's event handler (image 0x66e560), the source of the lobby's main menu list (source
number 0xb3, image 0x76c4b0) and the source of the party list (source number 0xad, image 0x76c4c0) are functions
that return at once. PS3 1.19 has all three. This group is the helper's replacement:

- **The page events** (`u3x_multiplayer_page`): what happens when the lobby page opens, is shown, is asked whether
  it may be left, and on every frame while it is up: the welcome dialog, the "Leave Multiplayer?" dialog, the three
  content notices (`u3x_content_notice`), the server's notice, the chat restriction notice, the disconnect handling,
  and party members following their leader to the game lobby.
- **The party lock** (`u3x_party_lock`): the party leader locks or unlocks the party session through its backend.
- **The lobby's main menu list and the party list** (`u3x_lobby_source`): every list request of the two lists.
- **The two game-lobby buttons** (`u3x_lobby_buttons`, C today): label and visibility of the "set up the game"
  button and of the "start" button for the party leader and for a guest.
- **The plain icon answer** (`u3x_set_icon`, C today): makes a list request an icon answer.
- **The rows of the main menu** (`u3x_lobby_menu_rows`, replaces the stray `u3x_lobby_rows` and its literal table):
  the menu's rows are read from the running game.

Callers (all stay as they are):

- C `pagemp119` (a one-line wrapper around `u3x_multiplayer_page`) is called by the helper's event entry
  `u3_profile_event` for stub kind 6: the builder puts a stub on the game's empty PAGE_MULTIPLAYER handler, so every
  page event of that page arrives here. Before the call the C caller counts the event and, for event 6, runs its
  focus fix (`lobby_focus`); it calls only with a non-zero `base`.
- C `pm_party_lock` (a one-line wrapper around `u3x_party_lock`) is called by two C functions that start a search
  for a match (`m03_startmatch`, `onb_startmatch`), each with the value 0. `u3x_multiplayer_page` calls
  `u3x_party_lock` directly.
- C `pm_notice` (a one-line wrapper around `u3x_content_notice`) has no caller left; only `u3x_multiplayer_page`
  calls `u3x_content_notice`.
- C `lobby_source` (a one-line wrapper around `u3x_lobby_source`) and C `lobby_buttons` are called by
  `u3_profile_event` for stub kind 2 (every list request of the menus): `lobby_buttons` is the first hook of that
  chain, `lobby_source` one of the last; a result of 0 passes the request on to the next hook. After
  `lobby_source` returned non-zero for operation 6 of the main menu list, the C caller runs `lobby_focus`.
- C `set_icon` is called by C `set_icon_safe` (which first tests that the icon is in the UI texture table; reached
  from the C++ part through the export `u3h_set_icon`) and by the C export `u3h_set_icon_plain` (callers:
  `u3x_lobby_source` of this group and the friends rows, G19).
- C `lobby_rows` (the function behind the C export `u3h_lobby_rows`) is the only caller of the rows function.
  `u3h_lobby_rows` is called by `u3x_lobby_source` and by the row click handler `u3x_lobby_row` (G32).

Everything runs on the game thread that updates the menus. There is no locking and no timing rule.

## 2. Interface

No function takes or returns a float. All are `extern "C"`. "existing" = exported today under this name, keeps name
and signature. "new" = replaces a C body; the C function of the old name stays as a one-line wrapper.

The group is delivered in three independent parts.

### Part A: the page events

| Function | Signature | Status | Called by |
|---|---|---|---|
| party lock | `void u3x_party_lock(uintptr_t base, int v)` | existing | part A; C `pm_party_lock` |
| content notice | `void u3x_content_notice(uintptr_t base, void *s, uint32_t pack, uint32_t dc, uint32_t bit, uint32_t txt, int items)` | existing | part A; C `pm_notice` |
| page events | `void u3x_multiplayer_page(uintptr_t base, void *s, int32_t *ev)` | existing | C `pagemp119` |

### Part B: the two lists

| Function | Signature | Status | Called by |
|---|---|---|---|
| list source | `int u3x_lobby_source(uintptr_t base, int32_t *ev, void *s)` | existing | C `lobby_source` |

### Part C: buttons, icon answer, menu rows

| Function | Signature | Status | Called by | C wrapper passes |
|---|---|---|---|---|
| lobby buttons | `int u3x_lobby_buttons(uintptr_t base, int32_t *ev, void *s)` | new (C `lobby_buttons`) | C `lobby_buttons` | its three arguments (event second, state third); returns the result |
| plain icon answer | `void u3x_set_icon(uintptr_t base, int32_t *ev, uint32_t id)` | new (C `set_icon`) | C `set_icon` | its three arguments |
| menu rows | `uint32_t u3x_lobby_menu_rows(uintptr_t base, int online, int coop, uint32_t *out)` | new name; replaces the existing `u3x_lobby_rows(int online, int coop, uint32_t *out)` (stray, see section 7), which is removed together with its literal table | C `lobby_rows` | `base`, then the three arguments it passes to `u3x_lobby_rows` today (the "online menu" test, the co-op byte, its 8-row buffer); it stores the result as the row count as today |

The rows function gets a new name because it needs `base`, which the old signature does not have; with a new name
a call that was not updated cannot link.

Parameters:

- `base`: game image base. `s`: the helper's state object (opaque; fields in 3.2).
- `v`: 1 = lock the party session, 0 = unlock it.
- `pack`: a content pack number (index of a flag byte, 3.4). `dc`: a script symbol id. `bit`: the bit of the
  profile's notice word that says "already given". `txt`: a text id. `items`: 0 = the symbol's data is a cash
  amount, non-zero = it is a list of item ids.
- `ev` of `u3x_multiplayer_page`: one page event (3.3). `ev` of the other functions: one list request (3.5).
- `id`: a UI icon id.
- `online`: non-zero = the online lobby's menu. `coop`: non-zero = the co-op lobby's menu (only looked at when
  `online` is 0). `out`: room for 8 rows of three u32 words.

Results: `u3x_lobby_source` and `u3x_lobby_buttons` return 1 when the request belongs to one of their sources (tag
match), else 0. `u3x_lobby_menu_rows` returns the number of rows written (4 or 6).

Functions of the C part, of other groups and existing exported C++ functions that the new code calls (declare them;
do not reimplement them). "Part" says which part needs the function.

| Name | Signature | What it does | Part |
|---|---|---|---|
| `u3h_ext` | `uint8_t *u3h_ext(void)` | the helper's own state block (3.1). In CONVENTIONS.h, with `ext_ea()` / `ext_eb()` | A, B |
| `u3h_lg_profile` | `uint8_t *u3h_lg_profile(uintptr_t base, uint32_t user)` | profile object of a local user (3.6), or null when the game has no profiles | A |
| `u3h_lg_rec` | `uint8_t *u3h_lg_rec(uintptr_t base, void *s, uint8_t *profile)` | the statistics record of a profile object: always `profile` + 0x248. The first call for a just-loaded profile can reset the record and change the profile's load state from 3 to 2 (G22 4.1); a repeated call changes nothing more. It does not accept a null profile | A |
| `u3h_mp_quit` | `void u3h_mp_quit(uintptr_t base, void *s)` | the quit check of G32: counts a match that was left unfinished in user 0's record, saves it and sends the player info. Often does nothing | A |
| `u3h_show_dialog` | `void u3h_show_dialog(uintptr_t base, void *s, uint32_t body, uint32_t title, uint32_t action, int32_t buttons, uint32_t extra, uint32_t close)` | fills the game's message dialog record with these text and action ids and opens the dialog page; `buttons` 0 = no buttons; `close` 0 = the game's default close label | A |
| `u3h_party_send` | `int u3h_party_send(uintptr_t base, uint32_t message, const void *data, uint32_t length)` | hands a party message to the online layer; result not used here | A |
| `u3h_server_notice` | `int32_t u3h_server_notice(uintptr_t base, int32_t *arg)` | asks the online layer for a notice of the server: −1 = none, otherwise the notice kind (0, 1, 2, ...); `*arg` receives a value this group does not use. **A notice is handed over once**: the call that returns it also consumes it | A |
| `u3h_chat_restricted` | `int u3h_chat_restricted(uintptr_t base)` | non-zero when the account's chat is restricted | A |
| `u3x_sym_table` | `uint8_t *u3x_sym_table(void *s, uint32_t id)` | existing C++ function: data of a script symbol through the state's lookup function (the pointer at `s` + 656); null when that pointer, the returned handle or the pointer the handle holds is null. One lookup call per call | A |
| `u3x_ms_text` | `const char *u3x_ms_text(uintptr_t base, uint32_t id)` | existing C++ function: the localized text of a text id (the game's fallback text when there is none) | A |
| `u3x_ms_hud` | `int u3x_ms_hud(uintptr_t base, int user, const char *txt, uint32_t type, int32_t level, const uint32_t *ic)` | C++ function of the mission-HUD group: queues a HUD message for a user's menu with message type, level and three icon words; 1 = queued, 0 = not | A |
| `u3x_publish` | `int u3x_publish(uintptr_t base, void *s, uint8_t *profile)` | C++ function of G33: saves the multiplayer record of a profile object; result not used here | A |
| `u3x_party_leader` | `int u3x_party_leader(uintptr_t base)` | existing C++ function, pure read: 1 when SES has no room (u64 at SES + 0x18 is 0) or our member id equals the host's (SES + 0x12e4 = SES + 0x12e8); else 0 | A |
| `u3x_split_screen` | `int u3x_split_screen(uintptr_t base)` | existing C++ function, pure read: non-zero when a second local player is present (u8 at image 0x17d7b06 non-zero, or s32 at image 0x17f2fc0 equal to 3) | A |
| `u3x_popup_id` | `uint32_t u3x_popup_id(uintptr_t base)` | existing C++ function, pure read: the page id of the popup that is up (u32 at +0x10 of the object the menu's pointer at image 0x17bd080 + 0x898 points to), 0 when that pointer is null | A |
| `u3x_send_player_info` | `void u3x_send_player_info(uintptr_t base)` | existing C++ function: sends the local player info: game function 0x6b7b30 called with (s32 0), then with (s32 1) | A |
| `u3x_page_show` | `void u3x_page_show(uintptr_t base, void *s, uint32_t page, int a)` | existing C++ function: when the state's "show page" pointer (`s` + 648) is not null, calls it with (address of image 0x17bd080, `page`, `a`, s32 0); result ignored. Nothing when the pointer is null | A |
| `u3h_gate_closed` | `int u3h_gate_closed(uintptr_t base, int32_t *ev)` | 0 while multiplayer is active (u8 at image 0x17d5c61 non-zero); otherwise stores the game's empty string (image 0x13ac250) as the request's text answer, counts, and returns 1 | B, C |
| `u3h_text` | `const char *u3h_text(uintptr_t base, void *s, uint32_t id)` | localized text of a text id through the game's text lookup; never null (empty text when there is none) | B, C |
| `u3h_icon_index` | `int32_t u3h_icon_index(uintptr_t base, uint32_t id)` | pure read: index of UI icon `id` in the game's UI texture table, or −1 (also when the table is not there) | B, C |
| `u3h_set_icon_plain` | `void u3h_set_icon_plain(uintptr_t base, int32_t *ev, uint32_t id)` | C export that ends in `u3x_set_icon` (part C): the plain icon answer, whatever the icon's index is | B |
| `u3h_lobby_rows` | `const uint32_t *u3h_lobby_rows(uintptr_t base, uint32_t *n)` | C export that ends in `u3x_lobby_menu_rows` (part C): the rows of the lobby menu that applies now (online, co-op or LAN; the C part decides), three u32 per row {text id, icon id, action id}; `*n` = the number of rows. The rows are in a buffer of the C part that every call fills again | B |
| `u3h_profile_new_marker` | `int u3h_profile_new_marker(uintptr_t base, int32_t *ev)` | C export (logic in G28): when the menu user has unseen unlocks, makes the request the "new" marker answer (word 2 = 0x2f, words 4 to 8 = 0, word 9 = −1, word 10 = 0, word 11 = −1, word 12 = 0x3f800000) and returns 1; otherwise returns 0 and leaves the request as it is. Counts its calls | B |
| `u3h_onp_id` | `const char *u3h_onp_id(void *s)` | pure read: the player's name as a NUL-terminated text (the account name online, else the selected LAN profile's name, else a default); never null | B |
| `u3h_player_profile` | `uint8_t *u3h_player_profile(uintptr_t base)` | pure read: the profile object of the menu user (3.6), or null when the profile array pointer is null or the menu user is 4 or more | B |
| `u3h_onm_players` | `int u3h_onm_players(uintptr_t base, int32_t *ev, void *s, const char *name)` | the party list while the real matchmaking runs (other group): answers the request and returns 1, or returns 0 and leaves the request as it is when that matchmaking is not running. `name` = the text for the own row | B |
| `u3x_m03_players` | `int u3x_m03_players(uintptr_t base, int32_t *ev, void *s, const char *self_name)` | the same for the offline stand-in matchmaking (C++ function of another group): 1 = answered, 0 = not running | B |
| `u3h_i01_party` | `int u3h_i01_party(uintptr_t base, int32_t *ev, void *s, const char *name)` | the full party member list (G34): non-zero = it answered the request | B |
| `u3x_lan_row_name` | `const char *u3x_lan_row_name(uintptr_t base, int row)` | pure read (G08 4.10): the name of the `row`-th other party member (first other member = row 1), or null | B |
| `u3x_lan_rows` | `int u3x_lan_rows(uintptr_t base)` | pure read (G08 4.11): the number of party rows, at least 1 | B |
| `u3h_rank_icon` | `void u3h_rank_icon(uintptr_t base, void *s, int32_t *ev)` | turns a rank badge answer (word 2 = 0x29, the rank word in word 6) into a picture answer of the rank art when that art is loaded (G22 and the C part); otherwise leaves the request as it is | B |
| `u3x_clan_tag_poll` | `void u3x_clan_tag_poll(uintptr_t base, void *s)` | C++ function of G33: the per-frame poll of the clan tag keyboard | B |
| `u3h_hud_alias_names` | `void u3h_hud_alias_names(uintptr_t base, void *s)` | per-frame step of the C part: the HUD material names | B |
| `u3h_stage_tick` | `void u3h_stage_tick(uintptr_t base, void *s)` | per-frame step: the lobby stage | B |
| `u3h_loadout_tick` | `void u3h_loadout_tick(uintptr_t base, void *s)` | per-frame step: the loadout kits | B |
| `u3x_icon_answer` | `void u3x_icon_answer(int32_t *ev, int32_t kind, int32_t a, int32_t b, int32_t c, int32_t index)` | existing C++ function, the image answer: word 2 = `kind`, words 4, 5, 10 = 0, words 6, 7, 8 = `a`, `b`, `c`, word 9 = `index`, word 11 = −1, word 12 = 0x3f800000 (1.0); word 3 is left as it is | B, C |
| `u3x_party_guest` | `int u3x_party_guest(const uint8_t *party)` | existing C++ function, pure read: 1 when the session at `party` has a room (u64 at +0x18 not 0) and our member id (+0x12e4) differs from the host's (+0x12e8); else 0 | C |
| `u3x_lobby_button_shown` | `int u3x_lobby_button_shown(int setup_button, int guest)` | existing C++ function: 0 for the set-up button of a guest, else 1 | C |
| `u3x_lobby_button_label` | `uint32_t u3x_lobby_button_label(int setup_button, int guest, int custom_game, uint32_t own_label)` | existing C++ function: the label of a lobby button: 0 = no text, 1 = the "waiting" text, any other value = a text id (for the leader's set-up button it is `own_label`) | C |

`u3h_ext`, `u3h_lg_profile`, `u3h_player_profile`, `u3h_onp_id`, `u3h_icon_index`, `u3x_party_leader`,
`u3x_party_guest`, `u3x_split_screen`, `u3x_popup_id`, `u3x_lan_row_name` and `u3x_lan_rows` have no side effect:
how often they are called is not part of the behaviour (section 4 still says when the old code asks, so a tester
who records them can compare). Every other call of the table and every game call is made exactly as section 4
says, in that order.

## 3. Game and helper facts

### 3.1 The helper's state block (address from `u3h_ext()`)

u32 words; counters are plain 32-bit increments (wrapping).

| Where | Meaning |
|---|---|
| ea[1] bit 3 (value 8) | set = the page events are switched off (4.3 step 1) |
| ea[40] | counter: page opened (event 0) |
| ea[41] | counter: welcome dialogs |
| ea[42] | counter: content notices given |
| ea[43] | counter: server notices shown |
| ea[44] | counter: disconnects handled |
| ea[45] | counter: frames in which a party member was sent to the game lobby |
| eb[1] bit 16 (value 0x10000) | set = the full party member list (`u3h_i01_party`) is off |
| block + 0x3a70, words F[0] .. F[2] | F[0] bit 2 (value 4) = the main menu answers value queries (4.4.2, operation 11); F[1] = the id of the last value query of the main menu; F[2] = counter of those queries |
| block + 0x2130, words W[8] .. W[15] | diagnostics of the simple party view (4.4.4): W[8] counter of image requests, W[9] last image slot, W[10] last row, W[11] .. W[14] the first four different non-zero image slots seen, W[15] the rank badge note |
| block + 0x31b0, words R[4], R[7] | R[4] counter of own rank badges; R[7] goes up by 0x10000 per row colour answered |

### 3.2 The state object `s`

| Offset | Type | Meaning |
|---|---|---|
| 128 | u32 | number of LAN profiles |
| 132 | u32 | the selected LAN profile, counted from 1; 0 = none |
| 648 | pointer to function | "show page" (used through `u3x_page_show`) |
| 656 | pointer to function | script symbol lookup (used through `u3x_sym_table`) |
| 692 | u32 | the selected row of the lobby's main menu |
| 768 + 8·k | pointer to function | "native k". Used here: native 21 (offset 936), the game's "rank word of a player profile": one argument, the profile object pointer; returns u32. May be null |
| 0x2300, words D[0] .. D[12] | u32 | diagnostics of the main menu's image requests (4.4.2, operation 4) |
| 0x2b80 | 101 bytes | text buffer for the own party row with the clan tag (4.4.3) |
| 0x3d78 | u32 | a switch word of the C part; bit 25 (value 0x2000000) set = no row colour in the simple party view |

### 3.3 Page events

A page event is an array of 32-bit words. Word 0 = the event number: 0 = the page was opened, 1 = the page is
shown, 3 = the menu asks whether the page may be left, 6 = once per frame while the page is up; other numbers
exist. The u8 at byte offset 0x10 is the "handled" answer: for event 3 the value 1 refuses the page change.

### 3.4 Game data

| Image offset | Type | Meaning |
|---|---|---|
| 0x13a63f0 | 6 rows of 12 bytes | the rows of the lobby's main menu, LAN version (3.7) |
| 0x13a6440 | 6 rows of 12 bytes | the rows of the lobby's main menu, online version (3.7) |
| 0x17bd080 | object | the menu (first argument of the menu functions) |
| 0x17c3a40 + n | u8 | content flag of pack number n: non-zero = the player owns that content |
| 0x17cb060 | object | the net game manager "NGM" (only its address is passed) |
| 0x17d5c61 | u8 | "multiplayer active" (net state 0x17d5c58 + 9) |
| 0x17d5c80 | u8 | "network error" flag (net state + 0x28) |
| 0x17d603c | u32 | net menu mode (net state + 0x3e4); 2 = Custom Game |
| 0x17d6040 | u8 | a flag of the net state (+0x3e8) that server notice kind 2 sets |
| 0x17dc298 | object | the party session "SES" (G08 3.2): +8 pointer to its backend (null = none); +0x18 u64 room id; +0x12e4 / +0x12e8 own and host member id |
| 0x17f2fc0 | s32 | the second-player state; the values 3 (second local player present) and 1 matter here |
| 0x1b24bc8 | pointer | the UI object; its pointer at +0x28 is the UI texture table, whose u16 at +2 is the number of icons |
| 0x1b68a48 | u8 | a "busy" flag of the game: non-zero = no disconnect handling now |
| 0x1b68a80 | u32, then u8 at 0x1b68a84 | the first row of the game's own party row model: member number and "local" flag |

A backend object (the pointer at SES + 8) starts with a pointer to its function table (8-byte entries). Entry 8
(table offset 0x40) is "lock": called with (backend pointer, SES pointer, s32 value: 1 lock, 0 unlock); its result
is not used.

Script symbols (data through `u3x_sym_table`; read at run time, never copied):

| Symbol | Data |
|---|---|
| 0x1ab455f4, 0xf6bd7840 | a cash amount: s32 at +0 |
| 0xad9e0b32 | an item list: u32 count at +0, pointer at +8 to that many u32 item ids |
| 0xffac56f2 | the server notice texts; here only its presence is tested |

### 3.5 List requests

The request layout of G23 3.3 applies (word 0 operation, word 1 row index in / answer out, the image answer in
words 2 and 4 to 12, the text answer = the pointer at byte 0x40, word 0xe the answer of a value query, word 0x13
the id of the value asked, word 0x14 the image slot, word 0x15 the text slot, the source descriptor = the pointer
at byte 0x98 with the source tag in its u32 at +0x1c). Used in addition:

| Field | Meaning |
|---|---|
| word 0xf | the answer of operation 20 (a row colour, 0xAARRGGBB-style word) |
| word 0x16 | what operation 20 asks; 2 = the row's colour |
| source descriptor + 8 | u32: the control's own label text id |

Operations met here: 1 title, 2 row text, 4 row image, 5 get selection, 6 get last row index, 7, 8 "is the control
shown", 10 row selected, 11 value query, 20 (0x14) row colour, 21 (0x15), 23 (0x17). An operation not named in a
rule of section 4 gets no answer (the request stays as it is).

"The text answer becomes T" means the pointer at byte 0x40 is set to T. "The empty text" is an empty
NUL-terminated text of the helper's own (not the game's empty string of the gate); only its content matters.

Source tags: 0x4c414eb3 = the lobby's main menu list; 0x4c414ead = the party list; 0x4c414e32 = the game lobby's
set-up buttons (Game Type / Map / Settings); 0x4c414e1e = the game lobby's start button.

Image slot 0xf5a294f3 = the row's icon; image slot 0 = the row's status picture.

### 3.6 Profile objects

As G23 3.5: the pointer at image 0x17dd610 is an array of profile objects of 0x7b00 bytes, one per local user.
Fields used here:

| Offset | Type | Meaning |
|---|---|---|
| 0xac0 | 4 bytes | the clan tag: up to four characters, ended by a zero byte when shorter; first byte 0 = no tag |
| 0x7ae8 | u32 | load state; 3 = loaded |

The statistics record R of a profile object is `u3h_lg_rec(base, s, profile)` (= profile + 0x248):

| Record offset | Type | Meaning |
|---|---|---|
| 0x884 + i, i < 0x400 | u8 | number of treasure i the player holds |
| 0x12e8 | u32 | bit 0 = the welcome dialog was shown |
| 0x1ad0 | u32 | content notice bits: 1, 2 and 4 = the three notices of 4.3 were given |

### 3.7 The main menu rows: read from the game, not a table of the helper

A row is 12 bytes: u32 text id, u32 icon id, u32 action id. The running PS4 game still holds the rows of its
(unused) main menu list as read-only data in its image, in menu order:

- **LAN table**, image 0x13a63f0, 6 rows: Custom Game, Find a Party, Profile, Cinema, Options, Splitscreen.
- **Online table**, image 0x13a6440, 6 rows: Find Game, Custom Game, Profile, Community, Options, Splitscreen.
  (A seventh row follows it in the image; it is not part of the menu the helper shows and is never delivered.)

The three menus are:

- online menu: rows 0 to 5 of the online table;
- LAN menu: rows 0 to 5 of the LAN table;
- co-op menu: rows 0, 2, 4 and 5 of the LAN table (no Find a Party, no Cinema).

Nothing of these tables is copied into the helper's source. One action id is a fact this group needs by value:
0xc4acc827 is the action of the Profile row.

### 3.8 Game functions called directly

| Image offset | Arguments | Result | Meaning |
|---|---|---|---|
| 0x660fb0 | (pointer null, s32 local user, s32 amount) | none | gives the user cash |
| 0x65a000 | (pointer: the menu, address of image 0x17bd080; u32 popup page id) | int, ignored | shows a popup page |
| 0x7418e0 | (pointer to a 32-byte output block; pointer: NGM, address of image 0x17cb060; s32 index; s32 0) | pointer, ignored | fills the block with the look of team / player `index`; the u32 at block + 0xc is its colour |

None of them uses a float register. Native 21 and the backend's "lock" entry are called through their pointers
(3.2, 3.4).

Ids used: pages 0x37656388 (the root main page), 0xb99a3235 (the game lobby, PAGE_LOBBY); popup page 0x587a7162
(the server notice popup); party message 0x110; the dialog text and action ids are given where they are used.

## 4. Behaviour

### 4.1 `u3x_party_lock(base, v)` (part A)

Nothing happens unless SES has a backend (pointer at SES + 8 not null) and `u3x_party_leader(base)` is non-zero.
Then: when entry 8 of the backend's function table is not null, it is called once with (backend, address of SES,
`v`); its result is ignored. (The backend's table pointer itself is not tested.)

### 4.2 `u3x_content_notice(base, s, pack, dc, bit, txt, items)` (part A)

One notice: owners of a content pack get its bonus once per profile.

1. When the u8 at image 0x17c3a40 + `pack` is 0: nothing happens (no lookup).
2. T = `u3x_sym_table(s, dc)` (one lookup per call). Null: nothing more.
3. For local user u = 0, then u = 1:
   1. P = `u3h_lg_profile(base, u)`. This user is skipped when P is null or P's load state (3.6) is not 3.
   2. R = `u3h_lg_rec(base, s, P)`: asked after the load state test and before anything of the record is read.
      f = the u32 at R + 0x1ad0. This user is skipped when (f AND `bit`) is not 0.
   3. The bonus. `items` = 0: game function 0x660fb0 with (null, u, the s32 at T + 0). `items` not 0: n = the u32
      at T + 0, L = the pointer at T + 8; when L is not null, for each of its first n ids in order: when the id is
      below 0x400, the u8 at R + 0x884 + id goes up by 1 (8-bit, wrapping). Ids of 0x400 or more are skipped. A
      null L gives nothing.
   4. The u32 at R + 0x1ad0 becomes (f OR `bit`), with the f read in step 2.
   5. HUD line: `u3x_ms_hud(base, 0, u3x_ms_text(base, txt), 0, 0, icon)` with the three icon words 0, 0,
      0xffffffff (no icon); the result is ignored. The first argument after `base` is 0 for both users.
   6. `u3x_publish(base, s, P)`; result ignored.
   7. ea[42] += 1.

How many times `u3h_lg_rec` is asked for the same profile after the first time is not part of the behaviour (it
always returns P + 0x248 and a repeated call changes nothing). The old code asks twice per user with cash, and
twice plus once per counted id with items.

### 4.3 `u3x_multiplayer_page(base, s, ev)` (part A)

E = word 0 of the page event.

1. **Switched off.** When ea[1] bit 3 is set: for E = 6, `u3h_mp_quit(base, s)`; for every E nothing else.
2. **E = 0, page opened.** `u3x_party_lock(base, 0)`; `u3x_send_player_info(base)`; ea[40] += 1.
3. **E = 1, page shown.**
   1. P = `u3h_lg_profile(base, 0)`. When P is not null: R = `u3h_lg_rec(base, s, P)`; f = the u32 at R + 0x12e8;
      when bit 0 of f is clear: the u32 at R + 0x12e8 becomes f OR 1, then the welcome dialog
      `u3h_show_dialog(base, s, 0xa2a50fec, 0xc6467f2d, 0, 0, 0, 0)`, then ea[41] += 1. (The load state is not
      looked at here. As in 4.2, only the first `u3h_lg_rec` call counts; the old code asks a second time before
      the store.)
   2. In every case: the net menu mode (u32 at image 0x17d603c) becomes 0.
   3. When `u3x_party_leader(base)` is non-zero: `u3h_party_send(base, 0x110, D, 4)` where D points to a u32 of
      value 0; result ignored.
4. **E = 3, may the page be left?** When multiplayer is not active (u8 at image 0x17d5c61 is 0): nothing.
   Otherwise the u8 at byte 0x10 of the event becomes 1 and the "Leave Multiplayer?" dialog is shown:
   `u3h_show_dialog(base, s, body, 0x0260a263, 0xafb7834c, 0x075bc82d, 0x375737e9, 0)` with body = 0x98ec2aa5
   when `u3x_split_screen(base)` is non-zero, else 0xd1dab365.
5. **E = 6, every frame**, in this order:
   1. The three content notices:
      `u3x_content_notice(base, s, 69, 0x1ab455f4, 1, 0x43263d81, 0)` (a cash bonus),
      `u3x_content_notice(base, s, 82, 0xf6bd7840, 2, 0x7c862ff1, 0)` (a cash bonus),
      `u3x_content_notice(base, s, 81, 0xad9e0b32, 4, 0xce81cfb2, 1)` (a pack of treasures).
   2. The server's notice, while the private "server notice shown" flag is 0 (4.3.1):
      1. v = `u3h_server_notice(base, A)` with A pointing to an s32 that holds 0 (the value it receives is not
         used).
      2. T = `u3x_sym_table(s, 0xffac56f2)` (asked whatever v is).
      3. When no popup is up (`u3x_popup_id(base)` is 0), v is 0 or more and T is not null: the flag becomes 1;
         ea[43] += 1; then for v = 2 the u8 at image 0x17d6040 becomes 1, for v = 0
         `u3x_page_show(base, s, 0x37656388, 1)`, for any other v nothing; then game function 0x65a000 with (the
         menu, 0x587a7162). Step 3 below is skipped in this frame.
      4. Otherwise nothing: a notice that was handed over in such a frame is not shown and not asked for again by
         this group (the online layer has given it away).
   3. The chat restriction notice, unless step 2.3 just showed a notice: while the private "chat notice shown"
      flag is 0, `u3h_chat_restricted(base)` is asked; when it is non-zero and no popup is up
      (`u3x_popup_id(base)` is 0): the flag becomes 1 and
      `u3h_show_dialog(base, s, 0x62501513, 0xc6467f2d, 0, 0, 0, 0xe1e040aa)`. (`u3h_chat_restricted` is not
      asked once the flag is 1, nor in a frame in which step 2.3 ran.)
   4. `u3h_mp_quit(base, s)`.
   5. Disconnected: when the network error flag (u8 at image 0x17d5c80) is non-zero, the busy flag (u8 at image
      0x1b68a48) is 0 and the s32 at image 0x17f2fc0 is not 1: `u3x_page_show(base, s, 0x37656388, 1)`; then
      `u3h_show_dialog(base, s, 0x61a3662e, 0xc6467f2d, 0xd4326d90, 0, 0, 0xe1e040aa)`; ea[44] += 1; nothing
      more in this frame.
   6. Otherwise, when `u3x_party_leader(base)` is 0 (a party member follows the leader into the game lobby):
      `u3x_page_show(base, s, 0xb99a3235, 0)`; ea[45] += 1.
6. **Any other E**: nothing.

#### 4.3.1 The two private flags

"Server notice shown" and "chat notice shown" are two variables of the new code (one byte each is enough), zero
when the helper is loaded and never cleared: each notice is shown at most once per run of the game. Nothing else
reads them.

### 4.4 `u3x_lobby_source(base, ev, s)` (part B)

#### 4.4.1 Entry

1. The source descriptor (pointer at byte 0x98) is null: result 0, nothing else.
2. Its tag is neither 0x4c414eb3 nor 0x4c414ead: result 0, nothing else.
3. `u3h_gate_closed(base, ev)` is non-zero: result 1, nothing else.
4. The result is 1 from here on. `row` = word 1 taken as u32. Tag 0x4c414eb3 goes to 4.4.2, tag 0x4c414ead to
   4.4.3.

#### 4.4.2 The main menu list (tag 0x4c414eb3)

M = `u3h_lobby_rows(base, &n)`: asked once per request, for every operation; M[i] = row i {text id, icon id, action
id}, n = the number of rows. By operation (word 0):

- **1**: the text answer becomes the empty text (the list has no title).
- **2**: when word 0x15 is 0 and `row` < n: the text answer becomes `u3h_text(base, s, text id of M[row])`.
  Otherwise nothing (the text answer is not touched).
- **4**: first the diagnostics D (u32 words at `s` + 0x2300): D[0] += 1; D[1] = word 0x14; D[2] = `row`; D[4] =
  the number of icons of the UI texture table (3.4), or 0xffffffff when the UI object pointer or its table pointer
  is null; when `row` < n: D[3] = `u3h_icon_index(base, icon id of M[row])` (as a u32; not written otherwise);
  D[5 + (D[0] AND 7)] = word 0x14, with the D[0] just counted. Then the answer, the first rule that applies:
  1. word 0x14 is 0xf5a294f3 and `row` < n: `u3h_set_icon_plain(base, ev, icon id of M[row])` (the icon is
     answered also when it is not in the UI texture table: index −1).
  2. word 0x14 is 0, `row` < n and the action id of M[row] is 0xc4acc827 (the Profile row):
     `u3h_profile_new_marker(base, ev)` is called; when it returns non-zero, nothing more.
  3. In every other case (also when the call of rule 2 returned 0): word 2 = 0 (no picture).
- **5**: word 1 = the selected row (u32 at `s` + 692).
- **6**: word 1 = n − 1.
- **7**: word 1 = 0.
- **10**: when `row` < n: the selected row (`s` + 692) = `row`.
- **11**: F[1] = word 0x13; F[2] += 1 (3.1); when F[0] bit 2 is set: word 0xe = 1 when `row` < n, else 0. With
  the bit clear word 0xe is not touched.
- **23**: when the selected row (`s` + 692) is n or more, it becomes 0.
- any other operation: nothing.

#### 4.4.3 The party list (tag 0x4c414ead): who answers

1. The first row of the game's party row model is set: u32 at image 0x1b68a80 = 0, u8 at image 0x1b68a84 = 1 (for
   every request that got here).
2. The own name N:
   - operation 1: the text `PARTY` (five characters, not localized);
   - otherwise, when a LAN profile is selected (the u32 at `s` + 132 is not 0 and not above the u32 at `s` + 128):
     N = `u3h_onp_id(s)`; then, when the menu user has a profile object (`u3h_player_profile(base)` not null) with
     a clan tag (first tag byte not 0), N becomes a text composed in the buffer at `s` + 0x2b80: `[`, the tag
     characters (at most four, up to the first zero byte), `] ` (bracket and one space), then the name. The
     composed text is cut after 100 characters and NUL-terminated. The buffer is written in this case for every
     operation other than 1, whether or not the request uses the name;
   - otherwise the empty text (`u3h_onp_id` is not asked).
3. `u3h_onm_players(base, ev, s, N)` non-zero: done.
4. `u3x_m03_players(base, ev, s, N)` non-zero: done.
5. When eb[1] bit 16 is clear: `u3h_i01_party(base, ev, s, N)` non-zero: done. With the bit set it is not called.
6. Otherwise the simple party view (4.4.4) answers.

#### 4.4.4 The simple party view

`op` = word 0. In this order:

1. When `op` is not 1 and `row` is not 0: N = `u3x_lan_row_name(base, row)` (row as an int), or the empty text when
   that is null.
2. When `op` is 4, the diagnostics W (3.1): W[8] += 1; W[9] = word 0x14; W[10] = `row`; then, when none of W[11] ..
   W[14] equals word 0x14, the first of them that holds 0 (if any) becomes word 0x14.
3. **Own rank badge**: `op` is 4, word 0x14 is 0xf5a294f3 and `row` is 0. With K = native 21 and P =
   `u3h_player_profile(base)`:
   - W[15] = (1 when K is not null) + (2 when P is not null) + (when both are there: the result of one call K(P),
     shifted left by 8 bits, as a 32-bit value);
   - when both are there: K(P) is called **a second time** and its result is the rank word of the answer:
     `u3x_icon_answer(ev, 0x29, rank word, 0, 0, −1)` (word 2 = 0x29, words 4, 5 = 0, word 6 = the rank word, words
     7, 8 = 0, word 9 = −1, word 10 = 0, word 11 = −1, word 12 = 0x3f800000); R[4] += 1;
     `u3h_rank_icon(base, s, ev)`;
   - when one is missing: no answer and no call.
   Done in both cases.
4. **Row colour**: `op` is 20, the net menu mode (u32 at image 0x17d603c) is 2, word 0x16 is 2, bit 25 of the
   switch word at `s` + 0x3d78 is clear and `row` < 5: a 32-byte block of zeros is handed to game function
   0x7418e0 with (block, address of image 0x17cb060, `row` as s32, 0); word 0xf = (the u32 at block + 0xc AND
   0xffffff) OR 0x80000000; R[7] += 0x10000. Done. Every other request of operation 20 gets no answer.
5. The remaining operations:
   - **1**: the text answer becomes N (`PARTY`).
   - **2**: the text answer becomes N when word 0x15 is 0, else the empty text.
   - **5**: word 1 = 0.
   - **6**: word 1 = `u3x_lan_rows(base)` − 1; then, in this order, `u3x_clan_tag_poll(base, s)`,
     `u3h_hud_alias_names(base, s)`, `u3h_stage_tick(base, s)`, `u3h_loadout_tick(base, s)`.
   - **7**: word 1 = 0.
   - **11**: word 0xe = 1 when `row` is 0, else 0.
   - **21**: word 1 = 0.
   - any other operation (also 4 outside rule 3): nothing.

### 4.5 `u3x_lobby_buttons(base, ev, s)` (part C)

1. The source descriptor (pointer at byte 0x98) is null: result 0. Its tag is neither 0x4c414e32 nor 0x4c414e1e:
   result 0. Nothing else in both cases.
2. `u3h_gate_closed(base, ev)` is non-zero: result 1, nothing else.
3. setup = 1 for tag 0x4c414e32, 0 for tag 0x4c414e1e. guest = `u3x_party_guest(address of image 0x17dc298)`.
4. Operation 1: L = `u3x_lobby_button_label(setup, guest, custom, own)` with custom = 1 when the net menu mode
   (u32 at image 0x17d603c) is 2, else 0, and own = the u32 at source descriptor + 8. The text answer becomes: the
   empty text for L = 0; the text `Waiting on party leader` (not localized) for L = 1; `u3h_text(base, s, L)`
   for any other L.
5. Operation 8: word 1 = `u3x_lobby_button_shown(setup, guest)`.
6. Any other operation: nothing. Result 1.

### 4.6 `u3x_set_icon(base, ev, id)` (part C)

The plain icon answer of UI icon `id`: exactly the effect of
`u3x_icon_answer(ev, 3, 0, id, 1, u3h_icon_index(base, id))`: word 2 = 3, words 4, 5, 6 = 0, word 7 = `id`, word 8
= 1, word 9 = the icon's index in the UI texture table (−1 when it is not there: the answer is made all the same),
word 10 = 0, word 11 = −1, word 12 = 0x3f800000. Word 3 and every other word stay as they are.

### 4.7 `u3x_lobby_menu_rows(base, online, coop, out)` (part C)

Copies the rows of one menu from the game's tables (3.7) to `out`, three u32 per row in the order text id, icon
id, action id, and returns the number of rows:

- `online` non-zero: the six rows of the online table (image 0x13a6440); result 6.
- otherwise `coop` non-zero: rows 0, 2, 4 and 5 of the LAN table (image 0x13a63f0), in this order; result 4.
- otherwise: the six rows of the LAN table; result 6.

Words of `out` after the rows returned are not written. The tables are read on every call (nothing is kept).

## 5. Test scenarios

Everything in this group can be tested old against new without the game, on a fake image (a memory block
addressed through `base`) with: call-recording stubs at image 0x660fb0, 0x65a000 and 0x7418e0 (the last one fills
its output block from a script); a fake SES whose backend's function table points at a recording stub; a fake
state object with recording stand-ins for native 21 (able to answer a different value on each call) and for the
functions behind `u3x_page_show` and `u3x_sym_table`; recording stubs with settable results for the functions of
section 2; a fake helper state block behind `u3h_ext()`. For the old `u3x_lobby_rows` against the new
`u3x_lobby_menu_rows`, the tester fills image 0x13a63f0 and 0x13a6440 of the fake image with the two tables of the
real game image. The two private flags of 4.3.1 are one-way: scenarios that need them clear must run first (or on
a freshly loaded copy of the code). Compare memory (the request, the state object, the state block, the fake
image), results, and the order and arguments of all recorded calls; texts are compared by content, not by pointer.

Party lock:

1. No backend; backend and leader (no room; own id = host id): one call (backend, SES, v) for v = 0 and v = 1;
   backend and not leader: no call; table entry 8 null: no call.

Content notice:

2. Pack flag 0: no lookup, nothing. Flag set, symbol missing: nothing.
3. Cash: both users loaded and without the bit: two awards (user 0, then 1, the amount of the symbol), bit set in
   both records, two HUD lines (both for menu 0), two saves, ea[42] + 2; the order of the calls per user.
4. User 0 has the bit, user 1 has no profile / load state 2: nothing for them.
5. Items: ids below, at and above 0x400; a treasure byte at 255 wraps to 0; count 0; list pointer null (bit set,
   HUD line and save all the same).
6. `bit` with other bits already set in the word (they stay).

Page events:

7. ea[1] bit 3 set: event 6 calls only `u3h_mp_quit`; events 0, 1, 3 do nothing.
8. Event 0: lock (leader with backend) or not, the two player info calls, ea[40].
9. Event 1: no profile; profile with bit 0 set (no dialog); bit clear (flag, dialog, ea[41]); mode word becomes 0
   in every case; party message only for the leader, with a 4-byte zero.
10. Event 3: multiplayer not active (nothing, handled byte untouched); active with and without split-screen (the
    two body ids, handled byte 1).
11. Event 6, server notice: v = −1; v = 0 / 1 / 2 with no popup and the symbol present (flag byte for 2, root page
    for 0, the popup in all three, ea[43], no chat question in that frame); v ≥ 0 with a popup up or the symbol
    missing (nothing, and the next frame asks again); after a shown notice no further `u3h_server_notice` call.
12. Event 6, chat notice: restricted with and without a popup; not restricted; shown once only; no
    `u3h_chat_restricted` call afterwards.
13. Event 6, the end: `u3h_mp_quit` every frame; disconnect with each of the three conditions failing in turn;
    disconnect taken (root page, dialog, ea[44], no follow step); member (page 0xb99a3235 with 0, ea[45]) and
    leader (nothing).
14. Events 2, 4, 5, 7: nothing.

List source:

15. No descriptor; other tag; gate closed (result 1, only the gate's effect).
16. Main menu, each operation of 4.4.2 with `row` below, at and above n: operation 2 with word 0x15 = 0 and 1;
    operation 4 with slot 0xf5a294f3 (icon in the table and missing), slot 0 on the Profile row with the marker
    function answering 1 and 0, slot 0 on another row, another slot; the D words including the wrap of D[5 + ...]
    and a null UI object / table; operations 5, 6, 7, 10, 23 (selected row at n − 1 and at n); operation 11 with
    F[0] bit 2 clear and set; an unknown operation.
17. Party list, the chain: each of the three sources answering (the later ones not called); eb[1] bit 16 set
    (`u3h_i01_party` not called); the model row written in every case.
18. Party list, the name: operation 1; no profile selected / selection above the count; selected without a profile
    object; with a profile object without a tag; tags of 1, 2 and 4 characters; a name long enough to be cut at
    100 characters.
19. Simple view: row 0 and rows above 0 with and without a member name; operation 4 diagnostics (a new slot, a
    known slot, slot 0, four slots already stored); the rank badge with native 21 null, the profile null, both
    there (two native calls, a stand-in that answers 3 then 5: W[15] = 0x303, word 6 = 5); a row above 0 with the
    badge slot (nothing); operation 20 in each combination of mode, word 0x16, switch bit and `row` 4 / 5;
    operations 1, 2 (word 0x15 = 0 and 1), 5, 6 (the four calls in order), 7, 11 (`row` 0 and 1), 21, an unknown
    one.

Buttons, icon, rows:

20. `u3x_lobby_buttons`: no descriptor, other tag, gate closed; both tags × leader / guest × mode 2 / other ×
    operations 1, 8 and another one; the three label kinds.
21. `u3x_set_icon`: an icon in the table and a missing one; word 3 and the words above 12 untouched.
22. `u3x_lobby_menu_rows`: the three menus against the old function; words after the returned rows untouched.

Needs the live game: nothing for the comparison. In the game the visible checks are: the lobby menu and its icons
in the LAN, co-op and online lobby; the "!" on the Profile row; the party box with the own name, clan tag and rank
badge; the welcome dialog once per profile; Back in the lobby asking "Leave Multiplayer?"; a guest's game lobby
buttons.

## 6. What stays as it is

- The C wrappers `pagemp119`, `pm_party_lock`, `pm_notice`, `lobby_source`, and the new one-line bodies of
  `lobby_buttons`, `set_icon` and `lobby_rows`; `set_icon_safe`, `u3h_set_icon`, `u3h_set_icon_plain`,
  `u3h_lobby_rows` with the C part's 8-row buffer, and the C tests that choose the menu ("online menu", co-op
  byte): callers and the helper's own decisions.
- `u3_profile_event` (the dispatch by stub kind, the hook chain, its focus fix after operation 6).
- The existing small C++ functions of section 2 (`u3x_party_leader`, `u3x_split_screen`, `u3x_popup_id`,
  `u3x_send_player_info`, `u3x_page_show`, `u3x_icon_answer`, `u3x_party_guest`, `u3x_lobby_button_shown`,
  `u3x_lobby_button_label`, `u3x_sym_table`, `u3x_ms_text`): used by other groups too.
- The diagnostics words (D, F, W, R, ea[40..45]): the helper's logs and the maintainers' tools read them.

## 7. Notes: strays, oddities to keep, open questions

Strays (functions outside every work group of the inventory that this group's code uses):

- `u3x_lobby_rows` with its literal table `lobby_entries` (8 rows of text / icon / action ids, a "menus" mask per
  row): covered here as `u3x_lobby_menu_rows` (4.7); the table goes away because the game holds the rows (3.7).
- `u3x_page_show`, `u3x_party_guest`, `u3x_lobby_button_shown`, `u3x_lobby_button_label`, `u3x_icon_answer`: short
  existing functions, in no list of the inventory; called as they are, described in section 2.
- File-private pieces of the old file that the new code replaces by what section 2 names: the symbol lookup
  (`u3x_sym_table`), the menu user's profile (`u3h_player_profile`), the text answer store, the bounded text
  append, the accessors of the state object.

Behaviour that looks like a mistake or dead code, to keep:

1. The HUD line of a content notice always goes to menu 0, also when user 1 got the bonus (4.2 step 5).
2. A server notice that is handed over while a popup is up, or while symbol 0xffac56f2 is missing, is lost
   (4.3 step 5.2.4): the online layer gives a notice once.
3. After a shown server notice of kind 0 the same frame still runs the quit check, the disconnect test and the
   follow step (a member is sent to the game lobby right after the root page was asked for).
4. The two notice flags are never cleared (once per run of the game, not once per sign-in).
5. Event 1 does not look at the profile's load state before it reads and writes the welcome bit, and sets the net
   menu mode to 0 and sends party message 0x110 on every event 1.
6. The party lock does not test the backend's table pointer.
7. Native 21 is called twice for one rank badge (once for the diagnostic word, once for the answer).
8. The main menu answers its row icon also when the icon is not in the UI texture table (index −1), unlike the
   lists that use `u3h_set_icon`; its operation 2 leaves the text answer untouched for another text slot or a row
   out of range, while the party view answers the empty text.
9. The clan tag text is composed for every party request other than the title, also for requests that use no name.
10. The simple party view colours rows by their row number (not by the member's team) and only rows 0 to 4.
11. D[12] of the main menu diagnostics (written when D[0] AND 7 is 7) is also a counter of another C function
    (the profile tab source); both keep writing it.
12. A content notice with items and a null list pointer still sets the bit, shows the line and saves.

Open questions:

1. The three content notices are separate steps of the PS3 page handler, each with its own pack number, script
   symbol, record bit and text id (4.3 step 5.1). The PS4 image holds none of the three symbol ids and none of the
   three text ids (no occurrence of those 32-bit patterns), so they cannot be read from the running game, and their
   names (for the string id rule of G23 3.9) are not known. They are given as ids. If the owner wants them
   generated, the symbol and text names must be found first.
2. What the value 1 of the s32 at image 0x17f2fc0 means (it suppresses the disconnect handling) is not named in
   the old source; the spec gives the test only.
3. What the game does with the party row model at image 0x1b68a80 / 0x1b68a84 afterwards is not described in the
   old source ("player 0, local"); the spec gives the two stores only.
4. The seventh row after the online table (image 0x13a6488) is presumably the store entry of the original menu;
   the helper's online menu never had it.

## 8. Clarifications (tester, 2026-11-14)

Facts confirmed by the differential test (`reimpl2\G31\test_g31.cpp`, old v7.212 against the new parts A, B and C).
Nothing in sections 1 to 6 was found wrong, and the implementers left no open question; these items make precise
what the text leaves open.

1. **The game's row tables give the old rows.** With the bytes of image 0x13a63f0 to 0x13a6497 of the PS4 image
   (in the unpacked ELF: file offset = image offset + 0x4000) in the fake image, `u3x_lobby_menu_rows` delivers for
   the online, the LAN and the co-op menu exactly the rows, the order and the row count that the old
   `u3x_lobby_rows` delivered from its own table. `online` and `coop` are tested for "not 0" only (negative values
   count as set); with `online` set, `coop` is not looked at.
2. **Rank badge, order of the stores (4.4.4 rule 3).** The old code stored word 2 (0x29) and words 4 and 5 (0) of
   the answer before the second call of native 21 and the remaining words after it; W[15] is stored before the
   second call in both. Native 21 receives the profile pointer only and cannot see the request, so the order of
   these stores relative to the second call is not part of the behaviour: storing the whole answer after the
   second call is equal. R[4] is counted after the answer and before `u3h_rank_icon`.
3. **Plain icon answer, order (4.6).** The old code stored words 2 and 4 to 8 before it asked `u3h_icon_index` and
   word 9 to 12 after it. `u3h_icon_index` is a pure read of the UI texture table, so asking it first is equal.
4. **Requests are read once.** The old code read word 0 and word 0x14 of the request again after calls of the
   functions of section 2. None of these calls changes those words when it does not answer the request
   (`u3h_onm_players`, `u3x_m03_players`, `u3h_i01_party` and `u3h_profile_new_marker` leave the request as it is
   when they return 0; `u3h_gate_closed` returning 0 changes nothing), so reading each word once is equal. `row`
   (word 1) is read once, before `u3h_lobby_rows` and before the party chain, on both sides.
5. **Main menu with 0 rows.** Operation 6 then answers word 1 = 0xffffffff (n − 1 as a 32-bit value), operation 23
   sets the selected row to 0 whatever it holds, and no row passes `row` < n. All comparisons of `row`, of the
   selected row and of the LAN profile selection are unsigned 32-bit.
6. **`u3x_lan_row_name` gets the row as a signed int**: a `row` of 0x80000000 or more is passed as a negative
   number; the function is asked for every `row` other than 0 (operation other than 1), also for operations that
   use no name.
7. **Simple party view, operation 4.** After the diagnostics, an image request never gets an answer except the own
   rank badge (slot 0xf5a294f3 on row 0 with native 21 and the profile both there); word 2 is left as it is in
   every other case (unlike the main menu, which stores 0).
8. **Simple party view, operation 20 outside the colour rule.** No answer and no call, whatever the net menu mode,
   word 0x16, the switch bit and the row are; the result is 1.
9. **The own row text (4.4.3).** The composed text holds at most 100 characters in all (bracket, tag, bracket,
   space and name together) plus the NUL; a tag character may be any non-zero byte. The four tag bytes are read up
   to the first zero byte only (a fifth byte is never read). The buffer is not touched when no LAN profile is
   selected, when the menu user has no profile object or when the tag is empty.
10. **`u3h_lg_rec` call counts.** Per profile and call of `u3x_content_notice` / event 1 the old code asked up to
    (2 + number of counted ids) times, the new code once; only the first call per profile can have an effect, and
    it comes at the same point on both sides (after the load state test, before the record is read).
11. **`u3x_content_notice` with unusual arguments.** `bit` = 0 gives the bonus on every call (the test "already
    given" never holds); `items` is tested for "not 0" only; `pack` indexes the flag bytes without a range test.
12. **Button results.** `u3x_lobby_buttons` stores the result of `u3x_lobby_button_shown` as it is (any value) in
    word 1 and tests the label for exactly 0 and exactly 1.
