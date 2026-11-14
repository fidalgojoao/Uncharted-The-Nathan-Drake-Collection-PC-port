# G32. Menu actions: the catch-all dispatcher, lobby rows, Custom Game entry, LAN choice, MOTD OK, quit count

Behaviour description for a reimplementation (reimpl2 pipeline, describer's document). It says what the functions
do, not how the old ones were written. All offsets are hexadecimal when prefixed with 0x. "Image offset X" means the
address `base + X`, where `base` is the game image base. All multi-byte values are little-endian; u8 / u16 / u32 /
u64 / s32 / s64 are integers of that width, "pointer" is 8 bytes. "Word n" of an event or request is the 32-bit word
at byte offset 4·n. Names follow `G13_online_backend_in.md` (menu, sessions, backends), `G21_party_join.md` (the
party session "SES") and `G23_boosters.md` ("native k", ea[n] / eb[n], data-source requests, "text answer").

This group has no float arithmetic, no file or socket access, no platform call and no exchange with a server: every
effect is a field written, a helper function called or a game function called. Nothing here reads or sends a
message of the day: the "MOTD OK" function only answers the button of the game's message-of-the-day page.

## 1. Purpose and callers

The PS4 game kept its multiplayer menu pages but lost most of the code behind their buttons: its handler for menu
actions that have no handler of their own ("catch-all", image 0x672450), its handler for the rows of the lobby's
main menu, the callback of the "LAN" / "Splitscreen" choice (image 0x66e510) and the list source of the LAN profile
list are empty functions. PS3 1.19 has all of them. This group is the helper's replacement for the dispatching part:

- **The catch-all** (`u3x_menu_action`): one menu action id comes in; the helper's own handlers get the first
  chance, then a fixed set of actions opens a page or does a small thing (lock / unlock the party, kick, promote,
  report, statistics, take a mission, delete a profile, MOTD OK).
- **A lobby row was pressed** (`u3x_lobby_row`): finds the row's action in the current row list and runs it:
  Custom Game (`u3x_custom_game`), Find a Party, Profile / Options (handed to the game's button handler), Cinema,
  Splitscreen, and for the online menu Find Game / Community (`u3x_lobby_action`).
- **Start Match in an online party** (`u3x_start_match_catch`), one of the handlers the catch-all asks first.
- **The LAN / Splitscreen choice** (`u3x_lan_choice`): sets the co-op flag and opens the LAN profile page.
- **The LAN profile list's operations** (`u3x_profile_list_op`): row text, selection, row count.
- **MOTD OK** (`u3x_motd_ok`, new entry for C `motd_ok119`) and **the quit count** (`u3x_mp_quit`, new entry for C
  `mp_quit119`): a player who is back on the multiplayer page while the record still says "in a match" gets a quit
  counted.

The row list of the lobby menu itself (which rows the LAN, the LAN co-op and the online lobby have) is not part of
this group: `G31_mp_page_events.md` covers it (its sections 3.7 and 4.7, `u3x_lobby_menu_rows`). This group only
reads the rows through the C export `u3h_lobby_rows` (2.2).

Callers (all stay as they are):

- The helper's event entry `u3_profile_event(base, event, kind, s, a5)` (hand-written C glue). It returns 1 for
  each of these kinds after the call:
  - kind 12 (the game's catch-all at image 0x672450, called by the menu's button handler for every action without
    a registered handler): only when the state's show-page pointer (3.2) is not null, C `catch_all` (a one-line
    wrapper) calls `u3x_menu_action(base, event, s)`.
  - kind 42 (the game's choice callback at image 0x66e510): only when `base` is not 0, C `lan_choice` (one-line
    wrapper) calls `u3x_lan_choice(base, event, s)`.
  - kind 0 (the game's click callback at image 0x672c60, which the helper registers for its lists): when the page
    on top of the menu is the multiplayer page (id 0x53cbb5f2) it calls `u3x_lobby_row(base, event, s)` and does
    nothing else.
  - kind 2 (every data-source request that reaches the game's list source at image 0x76f4d0): after all other
    list hooks have declined the request, the result of `u3x_profile_list_op(base, event, s)` is the result of
    the entry (0 = the game answers the request itself).
- `u3x_start_match_catch`: C `onb_catch` (one-line wrapper) behind the C export `u3h_start_match_catch`, which
  `u3x_menu_action` calls (4.5).
- `u3x_lobby_action`: `u3x_lobby_row` (4.7); C `onb_lobby_action` (one-line wrapper, no caller in v7.212).
- `u3x_custom_game`: `u3x_lobby_row` (4.7); C `custom_game` (one-line wrapper, no caller in v7.212).
- `u3x_motd_ok`: C `motd_ok119` (becomes a one-line wrapper) behind the C export `u3h_motd_ok`, which
  `u3x_menu_action` calls for action 0xab58f9da.
- `u3x_mp_quit`: C `mp_quit119` (becomes a one-line wrapper) behind the C export `u3h_mp_quit`, which the handler
  of the multiplayer page (`u3x_multiplayer_page`, group G31) calls on every "frame" event of that page (event
  number 6), whether its own port is switched on or off.

Everything runs on the game thread that updates the menus. There is no locking and no timing rule (4.3 copies one
clock value).

## 2. Interface

No function takes or returns a float. All are `extern "C"`. The group is small enough for one part (161 old code
lines); if two people work on it, the split is: (a) 4.1 to 4.5 and 4.8, 4.9; (b) 4.6 and 4.7, with (b) calling
(a)'s `u3x_custom_game` and `u3x_lobby_action` by the signatures below.

### 2.1 Functions to deliver

| Function | Signature | Status | Called by |
|---|---|---|---|
| LAN / Splitscreen choice | `void u3x_lan_choice(uintptr_t base, const int32_t *ev, void *s)` | existing name and signature | C `lan_choice` |
| Find Game / Community rows | `void u3x_lobby_action(uintptr_t base, void *s, uint32_t action)` | existing | `u3x_lobby_row`; C `onb_lobby_action` |
| Start Match | `int u3x_start_match_catch(uintptr_t base, void *s, uint32_t id)` | existing | C `onb_catch` (export `u3h_start_match_catch`) |
| Custom Game | `void u3x_custom_game(uintptr_t base, void *s)` | existing | `u3x_lobby_row`; C `custom_game` |
| catch-all dispatcher | `void u3x_menu_action(uintptr_t base, const int32_t *ev, void *s)` | existing | C `catch_all` |
| profile list operations | `int u3x_profile_list_op(uintptr_t base, int32_t *ev, void *s)` | existing | C `u3_profile_event` |
| lobby row pressed | `void u3x_lobby_row(uintptr_t base, const int32_t *ev, void *s)` | existing | C `u3_profile_event` |
| MOTD OK | `void u3x_motd_ok(uintptr_t base, void *s)` | **new** entry point | C `motd_ok119`, which becomes `u3x_motd_ok(base, s);` |
| quit count | `void u3x_mp_quit(uintptr_t base, void *s)` | **new** entry point | C `mp_quit119`, which becomes `u3x_mp_quit(base, s);` |

Parameters: `base` = game image base (never 0 when these are called). `s` = the helper's state object (opaque;
fields in 3.2). `ev` = a menu action event, a row click event or a data-source request (3.3). `action`, `id` = a
menu action id.

Results: `u3x_start_match_catch` returns 1 when `id` is the Start Match action, else 0. `u3x_profile_list_op`
returns 1 when it answered the request, 0 when the game has to.

The C exports `u3h_start_match_catch` and `u3h_motd_ok` stay; the dispatcher keeps calling through them.

### 2.2 Functions of the C part and of other groups that the new code calls

Declare them (`extern "C"`); do not reimplement them. "Result ignored" means the old code does not look at it.

| Name | Signature | What it does |
|---|---|---|
| `u3h_ext` | `uint8_t *u3h_ext(void)` | the helper's own state block (3.1). In CONVENTIONS.h, with `ext_ea()` and `ext_eb()` |
| `u3h_show` | `int u3h_show(uintptr_t base, void *s, uint32_t page, int silent)` | opens a menu page by id through the state object's show-page function (which it does not test for null); 0 = the page did not open |
| `u3h_show_dialog` | `void u3h_show_dialog(uintptr_t base, void *s, uint32_t body, uint32_t title, uint32_t action, int32_t buttons, uint32_t extra, uint32_t close)` | fills the game's message dialog record with these text and action ids and opens the dialog page (does nothing without a dialog record or without the show-page function) |
| `u3h_lan_chosen` | `void u3h_lan_chosen(void)` | the player chose LAN play: forwards to `u3x_lan_chosen` (G13 4.2) |
| `u3h_offline_find` | `int u3h_offline_find(uintptr_t base, void *s)` | Find Game without a server: in the helper's offline mode (and unless the matchmaking test aid is on) it shows the "network failure" dialog and returns 1; otherwise 0 and no effect |
| `u3h_party_send` | `int u3h_party_send(uintptr_t base, uint32_t message, const void *data, uint32_t length)` | sends a message to the members of the party through the online program; 0 when there is no sender. Result ignored |
| `u3h_nmh` | `uint8_t *u3h_nmh(uintptr_t base)` | address of the game-type table of the custom-game menu ("NetMenuHost"): the game's own at image 0x17d7b30 or the helper's larger replacement. Pure address accessor |
| `u3h_lan_backend` | `int u3h_lan_backend(void)` | non-zero when sessions run on the LAN backend (LAN play, and the helper's offline mode); 0 = online play. Pure read |
| `u3h_online_backend` | `void *u3h_online_backend(uintptr_t base)` | the registered online backend object, or null (G14 4.1). Pure read |
| `u3h_rpm_catch` | `int u3h_rpm_catch(uintptr_t base, void *s, uint32_t id)` | the helper's own option rows (their action ids are the helper's): 1 = handled |
| `u3h_fr_catch` | `int u3h_fr_catch(uintptr_t base, void *s, uint32_t id, uintptr_t report_callback)` | the friends menu's actions (group G18 and around): 1 = handled |
| `u3h_start_match_catch` | `int u3h_start_match_catch(uintptr_t base, void *s, uint32_t id)` | forwards to `u3x_start_match_catch` (4.3) |
| `u3h_film_action` | `int u3h_film_action(uintptr_t base, const int32_t *ev)` | the film player's tab actions: 1 = handled |
| `u3h_community_catch` | `int u3h_community_catch(uintptr_t base, void *s, uint32_t id)` | the Community page's Cinema and Leaderboards buttons: 1 = handled |
| `u3h_treasure_first_row` | `void u3h_treasure_first_row(void *s)` | the treasure sets list starts at its first row again (G27, `u3x_treasure_first_row`) |
| `u3h_motd_ok` | `void u3h_motd_ok(uintptr_t base, void *s)` | forwards to `u3x_motd_ok` (4.8) |
| `u3h_mission_take` | `void u3h_mission_take(uintptr_t base, void *s)` | takes the mission the "take" dialog was opened for (G26 section 1) |
| `u3h_party_kick_promote` | `void u3h_party_kick_promote(uintptr_t base, int promote)` | kicks (0) the selected player out of the party or makes them its leader (1) |
| `u3h_report_popup` | `void u3h_report_popup(uintptr_t base, void *s)` | shows the list of report reasons |
| `u3h_report_pick` | `void u3h_report_pick(uintptr_t base, void *s, uint32_t row)` | reports the selected player for reason `row` |
| `u3h_view_stats` | `void u3h_view_stats(uintptr_t base, void *s)` | opens the statistics page of the selected player |
| `u3h_lan_delete_profile` | `void u3h_lan_delete_profile(uintptr_t base, void *s, uint32_t action)` | Delete Profile on the LAN profile page: the confirm dialog, or the deletion itself |
| `u3h_lobby_rows` | `const uint32_t *u3h_lobby_rows(uintptr_t base, uint32_t *n)` | the rows of the lobby menu that is current now (LAN, LAN co-op or online): stores the count in `*n` and returns the rows, three u32 each: text id, icon id, action id. It ends in `uint32_t u3x_lobby_menu_rows(uintptr_t base, int online, int coop, uint32_t *out)` of group G31 (G31 3.7 and 4.7: the rows come from the game's own menu tables); the buffer is the C part's and every call fills it again |
| `u3h_lnb_find_party` | `void u3h_lnb_find_party(uintptr_t base)` | starts a search for LAN parties (`u3x_lan_find_party`, G08 / G10) |
| `u3h_splitscreen` | `void u3h_splitscreen(uintptr_t base, void *s)` | the lobby's Splitscreen row: `u3h_show_dialog(base, s, 0x7c31f4d0, 0xc6467f2d, 0, 0, 0, 0)` |
| `u3h_lg_profile` | `uint8_t *u3h_lg_profile(uintptr_t base, uint32_t user)` | profile object of a local user: the pointer at image 0x17dd610 plus `user` · 0x7b00, or null when that pointer is null |
| `u3h_lg_rec` | `uint8_t *u3h_lg_rec(uintptr_t base, void *s, uint8_t *profile)` | the statistics record of a profile (group G22); never null for a non-null profile; it can fill in the record's defaults |
| `u3h_publish` | `int u3h_publish(uintptr_t base, void *s, uint8_t *profile)` | saves the multiplayer record of a profile object (the helper's profile publish). Result ignored |
| `u3x_cinema` | `void u3x_cinema(uintptr_t base, void *s)` | the lobby's Cinema entry (G05) |
| `u3x_count_quit` | `int u3x_count_quit(uint8_t *record)` | existing C++ function that stays (2.3): when the u32 at record + 0x558 is not 0, it becomes 0, the u32 at record + 0x5b0 and the u32 at record + 0x1f24 each go up by one, and the result is 1; otherwise nothing changes and the result is 0 |

`u3h_ext`, `u3h_nmh`, `u3h_lan_backend`, `u3h_online_backend` and `u3h_lg_profile` have no side effect; how often
they are called is not part of the behaviour, except where section 4 says "asked".

### 2.3 Strays

- `u3x_lobby_rows` and its literal list `lobby_entries` (in no work group; it is what `u3h_lobby_rows` runs today):
  covered by G31 as `u3x_lobby_menu_rows`, not here.
- `u3x_count_quit` (exported, in none of the inventory's lists; one of the early functions written from a
  description): not delivered again. Its effect is stated in 2.2 so that 4.9 is complete.
- The file-private accessors the old functions use (show-page pointer, error word, switch word, "native k",
  co-op words, profile count, text answer) are one-field reads or writes; section 3 gives the fields.

## 3. Game and helper facts

### 3.1 The helper's state block (address from `u3h_ext()`)

u32 words; counters are plain 32-bit increments (wrapping).

| Where | Meaning |
|---|---|
| block + 0x100 | co-op word 0: counter of LAN / Splitscreen choices taken |
| block + 0x104 | co-op word 1: the context of the last choice (its low 32 bits) |
| block + 0x108 | co-op word 2: 1 when the last choice was Splitscreen, else 0 |
| ea[0] bit 1 (value 2) | set = "Delete Profile" is switched off; then the profile list does not copy its selection to the game (4.6) |
| ea[1] bit 1 (value 2) | set = the 1.19 MOTD OK steps and the quit count are switched off (4.8, 4.9) |
| ea[36] | counter: MOTD OK answers |
| ea[37] | counter: quits counted |
| ea[96] | image offset of the report popup's page handler (the builder stores it); 0 = the friends menu handler is not asked |
| eb[168] | the last action given to `u3x_lobby_action` |
| eb[169] | counter: Start Match handled |

ea[] starts at block + 0x3c50 (`ext_ea()`), eb[] at block + 0x4050 (`ext_eb()`).

### 3.2 The state object `s`

| Offset | Type | Meaning |
|---|---|---|
| 84 | u32 | diagnostic: the lobby row pressed last, plus one |
| 128 | u32 | number of saved LAN profiles ("count") |
| 132 | u32 | the selected profile, 1-based; 0 = none |
| 136 + 64·k | 64 bytes of text | name of profile k (k = 0 is the first), NUL-terminated |
| 648 | pointer to function | the show-page function; null = menu pages cannot be opened |
| 728 | u32 | diagnostic: the result of the last Custom Game entry (4.4) |
| 768 + 8·k | pointer to function | "native k": a game function the helper found at start. Any of them may be null |
| 0x20c0 | u32 | the builder's switch word; bit 4 (value 0x10) = "co-op needs a second user" (default clear) |
| 0x2428 | u64 | diagnostic counter of the catch-all (4.5) |

Natives used here (the game function each one normally holds is for orientation only; always call through the
pointer in the state object):

| k | Arguments | Result | Game function |
|---|---|---|---|
| 1 | (pointer: the game-type table from `u3h_nmh`) | none | 0x7503c0: rebuilds the list of game types of the custom-game menu |
| 2 | (pointer: image 0x17cb060, s32 net game index, pointer null, u32 0xc) | none | 0x720610: selects a net game (map and mode entry) in the net game manager |
| 3 | (pointer: the game-type table, s32 index) | none | 0x750a90: selects a game type of the custom-game menu |
| 14 | (pointer: the menu = image 0x17bd080, u32 action id, s32 0) | none | 0x64ce90: the menu's own button handler |

### 3.3 Events and requests

All are arrays of 32-bit words handed in by the game.

- **Menu action event** (`u3x_menu_action`): word 0 = the action id. The whole event is passed on to
  `u3h_film_action`.
- **Choice event** (`u3x_lan_choice`): word 0 = action id (not used); the u64 at byte offset 8 = the "context"
  the game registered with the callback: 0 for "LAN" on the multiplayer choice page, 1 for "Splitscreen" on the
  co-op choice page.
- **Row click event** (`u3x_lobby_row`): word 1 = the row pressed, counted from 0.
- **Data-source request** (`u3x_profile_list_op`; G23 3.3): word 0 = operation; word 1 = row index (in) or the
  answer of operations 5 and 6 (out); word 0x15 = the text slot asked (operation 2); the pointer at byte offset
  0x40 = the text answer.

### 3.4 Game data

| Image offset | Type | Meaning |
|---|---|---|
| 0x17b4ba4 | s32 | the game clock, 300 ticks per second |
| 0x17bd080 | object | the menu (first argument of the menu functions); its u32 at +8 (image 0x17bd088) is the UI state, 7 and 8 = the menu is changing pages |
| 0x17bf714 | u32 | the menu's user: which local user drives the menus |
| 0x17c3570 | u8 | the "party locked" setting (G18 3.7) |
| 0x17cb060 | object | the net game manager (only its address is passed) |
| 0x17d603c | u32 | net menu mode: 1 = matchmaking (Find Game), 2 = Custom Game |
| 0x17d6065 | u8 | "split-screen co-op" flag: 1 = the co-op menus |
| 0x17d6420 | object | the match session (3.5) |
| 0x17dc298 | object | the party session, "SES" (3.5) |
| 0x17dd610 | pointer | the profile objects of the local users (0x7b00 bytes each); null = the game has no profiles yet |
| 0x1b68810 | u32 | the game's net state word; 20 = "matchmaking starts" (the game's net state machine goes on from there) |
| 0x1b68814 | s32 | the clock value at which the net state was set |
| 0x1b6a400 | u32 | the game's "highlighted LAN profile" word, 1-based, 0 = none: the game shows the Delete Profile prompt only while it is not 0, and the delete action reads it |

The game-type table (from `u3h_nmh`): its u8 at +0 is the number of game types it holds (0 = none).

### 3.5 Sessions and backend objects

Session fields used (same offsets in the match session and in SES; G21 3.3, G13 3.5):

| Offset | Type | Meaning |
|---|---|---|
| 0x08 | pointer | the backend object the session is attached to; null = none |
| 0x18 | u64 | room id; 0 = no room |
| 0xc8 | u8 | "leave done": while it is non-zero and the room id is not 0, the session is in its room |
| 0x12e4 | u32 | our own member id |
| 0x12e8 | u32 | the host's member id |
| 0x1334 | u8 | SES only: "leave asked"; writing 1 makes the game's party machine leave the party room |

A backend object starts with a pointer to its function table; slot k is the pointer at table + 8·k. Slots used
here, both called as (pointer: the backend, pointer: the session) with an s64 result that is ignored: slot 3
"AttachSession" and slot 7 "Leave" (G10 4.6 / 5.4 for the LAN backend, G14 4.12 / 5.5 for the online one).

### 3.6 Game functions called directly

| Image offset | Arguments | Result | Meaning |
|---|---|---|---|
| 0x651fa0 | (s32 sound number) | none | plays a menu sound (the game limits how often) |
| 0x6530e0 | (pointer: the menu) | none | rebuilds the menu's "back" history from its page stack |
| 0x6b7b30 | (s32 local user: 0 or 1) | none | refreshes the player info of that user from the profile |
| 0x751aa0 | (u32 page id, u32 tab id) | none | makes that tab the current tab of the page in the game's tab table (for the current play kind, LAN or online); no effect when the page or the tab is not in the table |
| 0x652200 | (pointer: the menu, s32 0) | ignored | leaves the current page or popup (back) |

### 3.7 Ids

Pages: 0x4608c1ad the LAN profile page; 0x53cbb5f2 the multiplayer page; 0x0b7714b5 the playlists page;
0xdb53e8ff the Community page; 0xe35f0f1a the matchmaking page; 0xb99a3235 the lobby; 0xbb3595bc the LAN parties
page; 0xc98ee52a, 0xda577fac, 0x6281f21f, 0x0c3a3716, 0xdfdb36fa the pages of the catch-all table in 4.5.

Dialog "no second user": body text 0x7c31f4d0, title 0xc6467f2d, all four other arguments 0.

Row actions of the lobby menu: 0x7d593924 Custom Game; 0x52f3d9f6 Find a Party; 0xc4acc827 Profile; 0xe7d7dbbb
Options; 0xb85cb12a Cinema; 0xcd127c29 Splitscreen; 0x649e809a Find Game; 0x8891e664 Community (which menu has
which rows: G31 3.7). Catch-all actions: see 4.5. Start Match: 0xa0448333; the tab of the matchmaking page that it
makes current: 0xea2119f2.

Party message 0x118 (leader to members, "matchmaking starts"): 8 bytes, the u64 room id of the match (0 = none).

## 4. Behaviour

Rule for conditions: a condition that names several tests is evaluated from left to right and stops as soon as its
outcome is known; a function named in a part that is not reached is not called.

### 4.1 `u3x_lan_choice(base, ev, s)`

1. If the menu's UI state (u32 at image 0x17bd088) is 7 or 8: nothing at all.
2. C = the context (u64 at byte 8 of the event).
3. If C is not 0 and bit 4 of the switch word (s + 0x20c0) is set: the "no second user" dialog,
   `u3h_show_dialog(base, s, 0x7c31f4d0, 0xc6467f2d, 0, 0, 0, 0)`, and nothing else (no counter, no flag).
4. Otherwise, in this order: `u3h_lan_chosen()`; co-op word 0 incremented; co-op word 1 = the low 32 bits of C;
   the split-screen co-op flag (u8 at image 0x17d6065) = 1 when C is not 0, else 0; co-op word 2 = the same 1 or 0;
   then, when the show-page pointer (s + 648) is not null, `u3h_show(base, s, 0x4608c1ad, 0)`.

### 4.2 `u3x_lobby_action(base, s, action)`

1. eb[168] = `action` (always).
2. If the show-page pointer is null: nothing more.
3. `action` = 0x649e809a (Find Game): `u3h_offline_find(base, s)` is asked once; non-zero: nothing more.
   Otherwise the net menu mode (image 0x17d603c) = 1, the menu's user (image 0x17bf714) = 0, then
   `u3h_show(base, s, 0x0b7714b5, 0)`.
4. `action` = 0x8891e664 (Community): `u3h_show(base, s, 0xdb53e8ff, 0)`.
5. Any other action: nothing more (`u3h_offline_find` is not asked).

### 4.3 `u3x_start_match_catch(base, s, id)`

`id` other than 0xa0448333: result 0, no effect at all. Otherwise, result 1 and:

1. eb[169] incremented.
2. "We lead the party" = SES has no room (u64 at SES + 0x18 is 0) or our member id equals the host's (u32 at
   SES + 0x12e4 equals the u32 at SES + 0x12e8). (The same test as the existing `u3x_party_leader(base)`.)
3. Leader, in this order:
   - R = the match session's room id (u64 at + 0x18) when its "leave done" byte (+ 0xc8) is not 0, else 0;
   - `u3h_party_send(base, 0x118, pointer to R as 8 bytes, 8)`;
   - the net state word (image 0x1b68810) = 20; the s32 at image 0x1b68814 = the game clock (s32 at image
     0x17b4ba4);
   - when the show-page pointer is not null: `u3h_show(base, s, 0xe35f0f1a, 0)`;
   - game function 0x751aa0 (0xe35f0f1a, 0xea2119f2), also when the show-page pointer is null.
4. Member (not leader), in this order: the u8 at SES + 0x1334 = 1; game function 0x652200 (menu, 0).
5. In both cases, last: B = the match session's backend pointer (+ 8). When B is not null and its function table
   pointer is not null: slot 7 of B, called with (B, the match session).

### 4.4 `u3x_custom_game(base, s)`

E below is the diagnostic word at s + 728. The steps, in order; a step that ends the function leaves everything
after it undone:

1. Natives 1, 2 and 3 are read. Any of them null: E = 1, end.
2. The profiles pointer (image 0x17dd610) is null: E = 2, end.
3. The net menu mode (image 0x17d603c) = 2.
4. T = `u3h_nmh(base)`; native 1 (T).
5. The u8 at T + 0 is 0 (no game types): E = 3, end. (The net menu mode stays 2.)
6. Native 2 (address of image 0x17cb060, N, null, 0xc), with N = 0 when the split-screen co-op flag (u8 at image
   0x17d6065) is not 0, else N = 6.
7. Native 3 (T, 0).
8. O = `u3h_online_backend(base)` (asked first, always), then `u3h_lan_backend()` (asked always). When the latter
   is 0, O is not null and the match session's backend pointer (u64 at image 0x17d6420 + 8) is 0: slot 3 of O,
   called with (O, the match session). O's function table pointer is not tested.
9. E = 0. Then `u3h_show(base, s, 0xb99a3235, 0)` (the show-page pointer is not tested here); when it answers 0:
   E = 4.

### 4.5 `u3x_menu_action(base, ev, s)`

A = word 0 of the event, as u32. In this order; "handled" ends the function:

1. `u3h_rpm_catch(base, s, A)` non-zero: handled (the counter of step 2 is not touched).
2. The u64 at s + 0x2428 grows by 1 when A is 0x3406420c (the row click id of the party list), by 0x10000 for
   every other A.
3. When ea[96] is not 0: `u3h_fr_catch(base, s, A, base + ea[96])`; non-zero: handled. When ea[96] is 0 the
   function is not called.
4. `u3h_start_match_catch(base, s, A)` non-zero: handled.
5. `u3h_film_action(base, ev)` non-zero: handled.
6. `u3h_community_catch(base, s, A)` non-zero: handled.
7. By A:

| A | Effect |
|---|---|
| 0x9db19163 (Medals) | `u3h_show(base, s, 0xc98ee52a, 0)` |
| 0xe9bce205 (Treasures) | `u3h_treasure_first_row(s)`, then `u3h_show(base, s, 0xda577fac, 0)` |
| 0x420f5e96 (Records) | `u3h_show(base, s, 0x6281f21f, 0)` |
| 0x58dda3cd (Unlocks) | `u3h_show(base, s, 0x0c3a3716, 0)` |
| 0xe4794ab9 (Missions) | `u3h_show(base, s, 0xdfdb36fa, 0)` |
| 0xab58f9da (MOTD OK) | `u3h_motd_ok(base, s)` |
| 0xea1f40c5 (take the mission) | `u3h_mission_take(base, s)` |
| 0xccbf832e (Lock Party) | the u8 at image 0x17c3570 = 1 |
| 0x0d0c91cd (Unlock Party) | the u8 at image 0x17c3570 = 0 |
| 0x7cc46b94 (Kick) | `u3h_party_kick_promote(base, 0)` |
| 0x2d32c8b0 (Promote to leader) | `u3h_party_kick_promote(base, 1)` |
| 0x0ecbd5bd (Report Player) | `u3h_report_popup(base, s)` |
| 0x089143ea (View Stats) | `u3h_view_stats(base, s)` |
| 0x6d6ad418, 0xda18d7d3 (Delete Profile, and its "yes") | `u3h_lan_delete_profile(base, s, A)` |
| 0x7ebae89c .. 0x7ebae8a3 (a report reason) | `u3h_report_pick(base, s, A − 0x7ebae89c)` (row 0 .. 7) |
| anything else | nothing |

The results of `u3h_show` are ignored. The show-page pointer is not tested here (the caller did).

### 4.6 `u3x_profile_list_op(base, ev, s)`

The list of the LAN profile page. Row 0 is the game's own "new profile" row; rows 1 .. count are the saved
profiles. The function does not look at the request's source: it is the last hook of the chain.

Read at entry: count (u32 at s + 128); "follow" = bit 1 of ea[0] is clear; I = word 1 of the request as u32.
SEL below is the u32 at s + 132, G the game's word at image 0x1b6a400. By the operation (word 0):

| Operation | Effect | Result |
|---|---|---|
| 2 (row text) | when I is not 0, I ≤ count and word 0x15 is 0: the text answer = the address of the name of profile I − 1 (s + 136 + 64·(I − 1)); result 1. Otherwise nothing is written | 1 or 0 |
| 5 (selected row?) | word 1 = SEL; then, when following and SEL ≤ count: G = SEL | 1 |
| 6 (row count?) | word 1 = count | 1 |
| 10 (row selected) | when I ≤ count: SEL = I. Then, when following: G = I (also when I is above the count) | 1 |
| 23 | when SEL > count: SEL = 0. When following and G > count: G = 0 | 1 |
| any other | nothing | 0 |

All comparisons are unsigned (a negative row index is a large number). Nothing is saved to a file here.

### 4.7 `u3x_lobby_row(base, ev, s)`

1. The current rows: `u3h_lobby_rows(base, &n)`. R = word 1 of the event as u32. R ≥ n: nothing.
2. The u32 at s + 84 = R + 1.
3. A = the action id of row R (third word of the row). By A:

| A | Effect |
|---|---|
| 0x7d593924 (Custom Game) | `u3x_custom_game(base, s)` (4.4) |
| 0x52f3d9f6 (Find a Party) | `u3h_lnb_find_party(base)`; then, when the show-page pointer is not null, `u3h_show(base, s, 0xbb3595bc, 0)` |
| 0xc4acc827 (Profile), 0xe7d7dbbb (Options) | when native 14 is not null: native 14 (address of image 0x17bd080, A, 0). Null: nothing |
| 0xb85cb12a (Cinema) | `u3x_cinema(base, s)` |
| 0xcd127c29 (Splitscreen) | `u3h_splitscreen(base, s)` |
| anything else | `u3x_lobby_action(base, s, A)` (4.2) |

### 4.8 `u3x_motd_ok(base, s)`

"On" = bit 1 of ea[1] is clear. In this order:

1. When on: game function 0x651fa0 (0), then ea[36] incremented.
2. `u3h_show(base, s, 0x53cbb5f2, 0)` (always; result ignored; the show-page pointer is not tested).
3. When on: game function 0x6530e0 (address of image 0x17bd080).

### 4.9 `u3x_mp_quit(base, s)`

1. Bit 1 of ea[1] set: nothing.
2. P = `u3h_lg_profile(base, 0)` (always local user 0). Null: nothing.
3. Q = `u3h_lg_rec(base, s, P)` (asked whenever P is not null), then `u3x_count_quit(Q)`: when the record's
   "in a match" word (u32 at Q + 0x558) is 0, nothing more. Otherwise that word becomes 0 and the two quit counters
   (u32 at Q + 0x5b0 and at Q + 0x1f24) each go up by one.
4. `u3h_publish(base, s, P)`; then game function 0x6b7b30 (0), then 0x6b7b30 (1); then ea[37] incremented.

## 5. Test scenarios

All of this group can be tested without the live game: a fake image (zeroed memory that covers image offsets up to
0x1b6a404), a fake state
object (at least 0x2430 bytes), a fake helper state block (at least 0x4050 + 4·170 bytes), recording stubs for the
five game functions of 3.6, for natives 1, 2, 3 and 14, for the backend slots 3 and 7 (objects with a function
table of at least 8 slots) and for every function of 2.2. The comparison is: memory of image, state object and
helper block afterwards, the result, and the order and arguments of all recorded calls. In an old-against-new
harness `u3h_start_match_catch` and `u3h_motd_ok` forward to the function under test (old or new), and the stub
of `u3h_lobby_rows` hands out a row list the test supplies (rows of three u32; the row lists themselves are G31's).

`u3x_lan_choice`
1. UI state 7, and 8: nothing recorded, nothing written (also with context 1 and the switch bit set).
2. Context 0, UI state 0, show-page set: `u3h_lan_chosen`, co-op words {+1, 0, 0}, flag byte 0 (start it at 1 to see
   the write), `u3h_show(0x4608c1ad, 0)`.
3. Context 1, switch bit clear: co-op words {+1, 1, 1}, flag 1, page shown.
4. Context 1, switch bit set: only the dialog; co-op words and flag unchanged; `u3h_lan_chosen` not called.
5. Context 0 with the switch bit set: as 2. Context 0x100000000 (low half 0): treated as non-zero: word 1 = 0,
   word 2 = 1, flag 1; with the switch bit set: the dialog.
6. Show-page null: everything of step 4 of 4.1 except `u3h_show`.

`u3x_lobby_action`
7. Show-page null, each of the three kinds of action: only eb[168] written.
8. Find Game, `u3h_offline_find` answers 1: eb[168], the one question, nothing else (mode and user unchanged: start
   them at 7 and 3).
9. Find Game, answer 0: mode 1, user 0, `u3h_show(0x0b7714b5, 0)`, in that order.
10. Community: `u3h_show(0xdb53e8ff, 0)`, `u3h_offline_find` not asked. Another action: only eb[168].

`u3x_start_match_catch`
11. Another id: 0, nothing recorded or written (eb[169] unchanged).
12. Leader by "no room" (SES room 0, member ids different), match "leave done" 0, match room 0x1122334455667788:
    message 0x118 with 8 zero bytes; state 20; clock copied (plant 0x12345); page shown; 0x751aa0 with its two ids;
    slot 7 on the match backend; result 1; eb[169] + 1. SES + 0x1334 unchanged.
13. Leader by equal member ids with a room, match "leave done" 1: the message carries the match room id.
14. Leader with show-page null: no `u3h_show`, 0x751aa0 still called.
15. Member (SES room not 0, ids differ): SES + 0x1334 = 1, 0x652200 (menu, 0), slot 7; no message, state word and
    clock copy unchanged, no page.
16. Match backend pointer null; backend with a null function table pointer: no slot call, everything else as
    before.
17. `u3h_party_send` answering 0 and 1: same effects.

`u3x_custom_game`
18. Native 1 null; native 2 null; native 3 null: E = 1, nothing else (mode unchanged).
19. Profiles pointer null: E = 2, mode unchanged, no native called.
20. Game-type count 0 after the refresh (the native 1 stub leaves it 0): mode 2, native 1 called, E = 3, natives 2
    and 3 not called.
21. Count set by the native 1 stub, competitive (flag 0): native 2 with index 6, null, 0xc; native 3 (T, 0);
    `u3h_online_backend` then `u3h_lan_backend` asked; `u3h_show(0xb99a3235, 0)` answering 1: E = 0.
22. Co-op flag 1: native 2 with index 0.
23. `u3h_show` answering 0: E = 4.
24. Attach: LAN backend flag 0, online object present, match backend pointer 0: slot 3 (O, match session) between
    native 3 and `u3h_show`. Each of the three conditions failing in turn (LAN flag 1; object null; match backend
    pointer not 0): no slot call.

`u3x_menu_action`
25. `u3h_rpm_catch` answers 1: nothing else called, counter unchanged.
26. A = 0x3406420c: counter + 1; any other A: + 0x10000 (check a carry from bit 31 into the upper half).
27. ea[96] = 0: `u3h_fr_catch` not called. ea[96] = 0x2051234: called with `base + 0x2051234`; answering 1 ends the
    function.
28. `u3h_start_match_catch`, `u3h_film_action` (gets the event pointer), `u3h_community_catch` answering 1 in turn:
    the later ones are not called.
29. Each row of the table in 4.5 once, with all handlers answering 0: the call or the byte write named there. Lock
    and Unlock with the byte started at the opposite value. Both Delete Profile ids pass their own id on.
30. Report reasons: 0x7ebae89c → row 0, 0x7ebae8a3 → row 7; 0x7ebae89b and 0x7ebae8a4 → nothing.
31. An unknown id: only the handler questions and the counter.

`u3x_profile_list_op`
32. Operation 2: I = 0 → 0, no text; I = 1 .. count with word 0x15 = 0 → the name's address, 1; word 0x15 = 1 → 0;
    I = count + 1 → 0; I = −1 → 0; count = 0 → 0.
33. Operation 5: SEL ≤ count, following → word 1 = SEL, G = SEL; SEL > count → word 1 = SEL, G unchanged; not
    following (ea[0] = 2) → G unchanged.
34. Operation 6: word 1 = count.
35. Operation 10: I ≤ count → SEL = I, G = I; I = count + 1, following → SEL unchanged, G = count + 1; not following
    → G unchanged; I = 0 → both 0.
36. Operation 23: SEL > count → 0; G > count and following → 0; G > count, not following → G unchanged; both in
    range → unchanged.
37. Operations 0, 1, 4, 11, 22: result 0, nothing written.

`u3x_lobby_row`
38. Row index equal to the count, above it, and −1: nothing (s + 84 unchanged).
39. A row list with the six actions of the table in 4.7 (Custom Game, Find a Party, Profile, Options, Cinema,
    Splitscreen): each row → its effect; s + 84 = row + 1. Find a Party with show-page null: the search only.
    Profile / Options with native 14 null: nothing after the s + 84 write.
40. A row list with Find Game and Community: both reach `u3x_lobby_action` (eb[168] holds the action).
41. A row list with an unknown action: `u3x_lobby_action` records it in eb[168] and does nothing else.

`u3x_motd_ok`
42. Switch clear: sound 0, ea[36] + 1, page 0x53cbb5f2, history rebuild, in that order.
43. Switch set (ea[1] = 2): only the page.

`u3x_mp_quit`
44. Switch set: nothing called. Profiles pointer null: nothing after `u3h_lg_profile`.
45. "In a match" word 0: `u3h_lg_rec` asked, no publish, no game call, ea[37] unchanged.
46. Word 1 (and another non-zero value such as 0x100): word 0, both counters + 1 (start one at 0xffffffff to see the
    wrap), publish with user 0's profile, 0x6b7b30 (0) then (1), ea[37] + 1. Called again at once: nothing (45).

Live game (one pass, after integration): the three lobbies show their rows and each row opens what it did;
Start Match in an online party of two (leader and member); the LAN profile list shows names, keeps its selection
and offers Delete Profile only on a saved profile.

## 6. Things in the old code that look like bugs or dead code (behaviour to keep)

1. `u3x_custom_game` calls `u3h_show` without testing the show-page pointer; on the "no game types" exit the net
   menu mode is already 2; E is a diagnostic nobody reads.
2. `u3x_custom_game` asks `u3h_online_backend` and `u3h_lan_backend` even when it then attaches nothing, and does
   not test the online object's function table pointer (4.3 tests the match backend's).
3. `u3x_start_match_catch`: the Leave slot of the match backend is called for leader and member alike; the state
   word is set to 20 even when no message could be sent; 0x751aa0 is called even when the page could not be shown;
   the room id in the message is 0 unless the match session's "leave done" byte is set.
4. `u3x_lobby_action` writes eb[168] before every test, and asks `u3h_offline_find` only for Find Game and only
   with a show-page function.
5. `u3x_lan_choice`: the dialog branch counts nothing; co-op word 2 is a flag, not a counter; the context is tested
   as 64 bits but stored as 32.
6. `u3x_menu_action`: an action that `u3h_rpm_catch` takes is not counted; the low half of the counter counts the
   party list's row click id only.
7. `u3x_profile_list_op`: operation 10 copies a row above the count to the game's word although it does not take
   it as the selection; operation 5 leaves the game's word alone when the selection is above the count; the
   function answers operations 5, 6, 10 and 23 for every request that reaches it, whatever list it is from.
8. `u3x_lobby_row` stores R + 1 before it knows whether the action does anything. The Splitscreen row always
   shows the "no second user" dialog, while the Splitscreen choice of 4.1 goes on unless the switch is set.
9. `u3x_motd_ok`: on PS4 no page shows the MOTD button in LAN play, so this is reached only online; the switch
   is tested twice (before the sound and before the history rebuild).
10. `u3x_mp_quit` looks at local user 0 only, ignores the result of the publish, and refreshes both users.
11. Cinema is handled twice: as a lobby row (4.7) and, from the Community page, through `u3h_community_catch`.

## 7. What stays as it is

- C `u3_profile_event` (the event entry and its order of hooks) and the one-line C wrappers `catch_all`,
  `lan_choice`, `custom_game`, `onb_lobby_action`, `onb_catch`, and the exports `u3h_motd_ok`,
  `u3h_mp_quit`, `u3h_start_match_catch`: glue; the builder's hooks point at them.
- C `splitscreen` (one dialog call) and every function of 2.2: other groups or the helper's own code.
- `u3x_count_quit`: an early function written from a description; called as it is.

## 8. Open questions

1. The byte at image 0x17c3570 is the "party locked" setting here and in G18 / G19 / G21; `G03_film_playback.md`
   uses the same byte as a pause state of the film player. Both uses exist in the old code; nothing to change
   here, but the two documents name it differently.
2. `u3x_count_quit` and the wrappers without callers (`onb_lobby_action`, `custom_game`) are for the final sweep.

## 9. Clarifications (tester, 2026-11-14)

Facts confirmed by the old-against-new test (`reimpl2\G32\test_g32.cpp`). Each one states what the old code does.

1. 4.6, operation 2: the offset of the name is (I − 1) · 64 computed as a u32 (it wraps at 2^32) and only then added
   to s + 136. For I up to 0x4000000 this is the plain s + 136 + 64·(I − 1). For a larger I (possible only when the
   count word at s + 128 is above 0x4000000) the offset is taken modulo 2^32: I = 0x4000001 gives the name of
   profile 0, I = 0x4000003 that of profile 2, I = 0xffffffff the address s + 136 + 0xffffff80. The state object
   has room for 8 names (136 .. 647), so a real count never comes near; the rule matters only for an exact match.
2. 4.1, order of reads: the UI state, the context and the switch word are read before `u3h_lan_chosen()` (a change
   made by that call has no effect on the branch taken or on the values stored). The helper block (`u3h_ext()`),
   co-op word 0 (for the increment) and the show-page pointer are read after `u3h_lan_chosen()` has returned.
3. 4.3, order of reads: "we lead the party" is decided before `u3h_party_send`; the game clock and the show-page
   pointer are read after `u3h_party_send` has returned; the match session's backend pointer (step 5) is read after
   the game call of step 3 or 4. In step 5 only the backend pointer and its function table pointer are tested: slot
   7 itself is called even when it is null, and it is called on whatever object the match session holds.
4. 4.4, order of reads: natives 1, 2 and 3 are read once, at entry (a change made later, by `u3h_nmh` or by native
   1, is not seen); the profiles pointer is read before `u3h_nmh` and native 1; the split-screen co-op flag is read
   after native 1 has returned; the match session's backend pointer (step 8) is read after `u3h_lan_backend()` has
   returned. Step 8 tests neither O's function table pointer nor slot 3 for null. Any non-zero value of the flag
   byte counts as co-op, and E = 4 only when `u3h_show` answers exactly 0.
5. 4.5: the counter of step 2 is written before `u3h_fr_catch` is asked; `u3h_treasure_first_row` comes before the
   page.
6. 4.8: bit 1 of ea[1] is read twice, once at the start and once after `u3h_show` has returned: a change made by
   `u3h_show` decides the history rebuild alone. ea[36] is read for the increment after the sound call has returned.
7. 4.9: the "in a match" word is tested after `u3h_lg_rec` has returned (a record that `u3h_lg_rec` fills in with
   a non-zero word is counted as a quit at once).
8. 4.2 and 4.7: eb[168] is written before the show-page test and before `u3h_offline_find`; the u32 at s + 84 is
   written before the row's action runs; native 14 is tested for null only for the Profile and Options rows.
