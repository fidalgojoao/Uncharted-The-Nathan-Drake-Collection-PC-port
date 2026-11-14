# G13. Online backend: Play Online, NetInit, room messages in (Update)

Behaviour description for a reimplementation (reimpl2 pipeline, describer's document). It says what the functions
do, not how the old ones were written. Offsets are hexadecimal when prefixed with 0x, decimal otherwise. "Image
offset X" means the address `base + X`, where `base` is the game image base. All multi-byte values are little-endian
unless a section says otherwise; "u8 / u16 / s16 / u32 / s32 / u64 / s64" are unsigned / signed integers of that
width, "pointer" is 8 bytes. Only the ping clock (5.15) uses floating point.

Names used for the same things in the related documents are kept: sessions, member slots, the member description and
`u3x_add_member` / `u3x_remove_member` are in `G07_lan_sockets.md` (3.6, 3.7, 4.17, 4.18); the connection table and
its entries in G07 (3.5, 4.9 to 4.12); the frame tick that calls the backend's Update, the session events and the
party state fields in `G08_lan_room_client.md` (3.2, 3.6, 4.4, 4.6); the connection sub-object in
`G12_packets_monitor.md` (3.6). They are referred to, not repeated.

The group is written as **two independent parts**. Nothing of one part calls the other; two implementers can work at
the same time.

| Part | Subject | Exported names to deliver |
|---|---|---|
| 1 | Play Online entry, sign-in wait, online NetInit, backend object setup, connection table reset | `u3x_play_online`, `u3x_lan_chosen`, `u3x_signin_wait`, `u3x_online_netinit`, `u3x_onb_setup` (new), `u3x_cm_online_reset` (new) |
| 2 | Room messages in: member link, member record, one received message, the Update slot, the ping clock | `u3x_onb_member_link`, `u3x_onb_peer_begin`, `u3x_onb_info`, `u3x_onb_msg_in`, `u3x_onb_s1`, `u3x_onb_ping_restart`, `u3x_onb_ping_due` |

## 1. Purpose and callers

The game's multiplayer code works on a "session manager" object (a class with a table of function pointers) for
everything that concerns rooms. On PS3 the online manager talked to the publisher's matchmaking server. The helper
supplies that class again ("the online backend object"): its room messages travel either over the session socket
(a framed, big-endian message stream to the configured session server) or, as the fallback, as frames of the
community-server link. The slots that send messages are group G14; this group holds how online play is entered and
initialised (part 1) and how received room messages are applied to the game's sessions (part 2).

**Part 1.**

- `u3x_play_online`: the "Play Online" menu action. Called through the C wrapper `onb_play_online(base, s)` from the
  helper's hook dispatcher for hook kind 90 (the game's named menu callback for the Play Online buttons), whenever
  the image base is known. Result unused.
- `u3x_lan_chosen`: the player chose LAN play instead. Called through the C wrapper `onb_lan_chosen` and its export
  `u3h_lan_chosen()` from the LAN choice menu action (another group).
- `u3x_signin_wait`: first thing of the multiplayer startup's phase 1, once per frame while that phase lasts. Called
  through the C wrapper `onb_signin_wait` and its export `u3h_onb_signin_wait(base, s)` from the startup's NetInit
  step (another group): a non-zero result makes that step return at once (1 = try again next frame, −1 = the
  startup was stopped here).
- `u3x_online_netinit`: the online branch of NetInit. Called through the C wrapper `onb_netinit` and its export
  `u3h_onb_netinit(base, s)` from the same NetInit step, only when `u3h_onb_lan_flag()` is 0 (the LAN branch is
  `u3x_lan_netinit`, G08). Before the call that step has stored 0 in the u8 at image offset 0x17d5c60 (LAN flag),
  1 in the u8 at image offset 0x17d5c61 ("multiplayer active") and has run the game's session initialisation on the
  match session. The caller ignores the result.
- `u3h_onb_setup` (C): builds the online backend object and registers it. Called by `u3x_online_netinit` only.
- `u3h_cm_online_reset` (C): sets the game's connection table up for online play. Called by `u3x_online_netinit`
  only.

**Part 2.**

- `u3x_onb_s1`: entry 1 ("Update") of the online backend's function table. Called once per frame by `u3x_lan_tick`
  (G08 4.6 step 3) while the online object is the registered manager; a non-zero result makes that caller set the
  game's network error flag. The C wrapper `onb_s1` exists; nothing else calls it.
- `u3x_onb_msg_in`: applies the one message at the start of the backend's receive buffer and says how many bytes
  it used. Called by `u3x_onb_s1` only (the C wrapper `onb_msg_in` has no caller).
- `u3x_onb_member_link`: gets the connection to one room member ready. Called by `u3x_onb_msg_in` (member joined)
  and by `u3x_onb_peer_begin` (the C wrapper `onb_member_link` has no caller).
- `u3x_onb_peer_begin`: that for every other member of a session. Called by `u3x_onb_msg_in` (room joined, owner
  changed) (the C wrapper `onb_peer_begin` has no caller).
- `u3x_onb_info`: turns a wire member record into a member description. Used by `u3x_onb_msg_in` (the C wrapper
  `onb_info` has no caller).
- `u3x_onb_ping_restart`, `u3x_onb_ping_due`: the 30-second ping clock of the backend. `u3x_onb_ping_due` and
  `u3x_onb_ping_restart` are called by `u3x_onb_s1`; `u3x_onb_ping_restart` also by the backend's Init slot
  (`u3x_onb_s0`, not in this group).

Everything of this group runs on the game's main thread (menu callback, startup tick, frame event). The helper
state that other threads change is only reached through the `u3h_` functions of 2.3.

## 2. Interface

All functions are `extern "C"`. `base` is the game image base; `s` is the helper's state object (3.2); `be` is the
online backend object (3.4); `ss` is a session object.

### 2.1 Part 1

| Function | C++ signature | Result | The C wrapper passes |
|---|---|---|---|
| Play Online | `void u3x_play_online(uintptr_t base, void *s)` | none | base, state object (unchanged) |
| LAN chosen (stray) | `void u3x_lan_chosen(void)` | none | nothing |
| sign-in wait | `int u3x_signin_wait(uintptr_t base, void *s)` | 1 = still waiting, 0 = go on with NetInit, −1 = failed (dialog shown, startup stopped) | base, state object |
| online NetInit | `int u3x_online_netinit(uintptr_t base, void *s)` | 0 = done, −1 = failed | base, state object |
| backend setup | `void *u3x_onb_setup(uintptr_t base)` (new name; replaces the C body of `u3h_onb_setup`) | the backend object | `u3h_onb_setup(base)` becomes "return `u3x_onb_setup(base)`" |
| table reset | `void u3x_cm_online_reset(uintptr_t base)` (new name; replaces the C body of `u3h_cm_online_reset`) | none | `u3h_cm_online_reset(base)` becomes "call `u3x_cm_online_reset(base)`" |

`u3x_online_netinit` may call the two new functions directly or through the kept exports; the effects are the same.

### 2.2 Part 2

| Function | C++ signature | Result |
|---|---|---|
| member link | `void u3x_onb_member_link(uintptr_t base, uint8_t *ss, const char *name)` | none |
| peers begin (stray) | `void u3x_onb_peer_begin(uintptr_t base, uint8_t *ss)` | none |
| member record | `void u3x_onb_info(void *in, const uint8_t *r)` | none; `in` = a member description to fill (G07 3.7, 0x28 bytes), `r` = a wire member record (3.9) |
| one message (stray, see 2.4) | `uint32_t u3x_onb_msg_in(void *be)` | bytes of the buffer the message used; 0 = nothing handled (incomplete or unknown) |
| Update slot | `int64_t u3x_onb_s1(void *be)` | 0 = fine, −1 = no connection to the service |
| ping restart (stray) | `void u3x_onb_ping_restart(void *be)` | none |
| ping due (stray) | `int u3x_onb_ping_due(void *be)` | 1 = a ping is due, 0 = not |

`name` is a player name field (0x24 bytes, G07 3.1). The C wrappers pass their arguments unchanged.

### 2.3 Functions of other groups, of the C part and of the shared tools that this group calls

Not to be described or reimplemented here (exception: the rows marked "stray, behaviour complete here", see 2.4).
The tools of `CONVENTIONS.h` (`rd`, `at`, `game_call`, `u3h_ext`, `ext_eb`, `Line`, the `f_` helpers) are used as
given there. "Pure read" = the function only reads helper state and has no effect; such state can change between
two calls (other threads), so the behaviour sections give the order and number of these calls.

Part 1:

| Name | Signature | What it does |
|---|---|---|
| `u3h_onb_shell_route` | `int u3h_onb_shell_route(uintptr_t base)` | when the active menu page is neither the multiplayer choice page nor the co-op choice page: hands the call to the game's original menu code and answers 1 (nothing more to do); otherwise 0 |
| `u3h_online_config` | `int u3h_online_config(void *s)` | reads the sign-in settings once (first call); non-zero = settings present and usable |
| `u3h_online_cfg_ok` | `int u3h_online_cfg_ok(void)` | pure read: the same answer without reading anything |
| `u3h_online_status` | `int32_t u3h_online_status(void)` | pure read: state of the community-server link: 0 = idle, 1 = connecting, 2 = signed in, 3 = refused (no retry), 4 = waiting to retry |
| `u3h_smn_up` | `int u3h_smn_up(void)` | pure read: the session socket: 0 = none, 1 = live, 2 = connecting or reconnecting |
| `u3h_rc_signing_in` | `int u3h_rc_signing_in(void)` | pure read: non-zero while an account sign-in or the session handshake is running |
| `u3h_rc_state` | `int32_t u3h_rc_state(void)` | pure read: the account sign-in's state number (only printed) |
| `u3h_rc_server_ready` | `int u3h_rc_server_ready(void)` | pure read: non-zero when the account sign-in runs and the server sign-in succeeded |
| `u3h_srv_retry_pending` | `int u3h_srv_retry_pending(void)` | pure read: non-zero while the server sign-in is being retried |
| `u3h_profile_state` | `uint32_t u3h_profile_state(int *has_memory)` | stores 1 or 0 in `*has_memory` (the online memory block exists or not) and returns the profile download state: 0 = not finished, 3 = failed, other values = other states; 1 when there is no memory block |
| `u3h_onp_pending` | `int u3h_onp_pending(void)` | pure read: non-zero while the profile download has not finished |
| `u3h_profile_retry` | `void u3h_profile_retry(void)` | asks for the profile download to be tried again |
| `u3h_offline_ok` | `int u3h_offline_ok(void)` | non-zero when the offline lobby is allowed (0 in the current helper) |
| `u3h_go_offline` | `void u3h_go_offline(void)` | sets the Play Online mode to 2 (offline) and counts it |
| `u3h_set_onb_mode` | `void u3h_set_onb_mode(int mode)` | sets the Play Online mode: 0 = LAN, 1 = online, 2 = offline |
| `u3h_onb_mode_get` | `int u3h_onb_mode_get(void)` | pure read: that mode |
| `u3h_k01_play_online` | `int u3h_k01_play_online(uintptr_t base, void *s)` | the "skip the profile page" shortcut: when a saved profile can be used it starts the multiplayer startup itself and answers non-zero; otherwise 0 |
| `u3h_k01_lan` | `void u3h_k01_lan(void)` | clears that shortcut's "skipped" mark |
| `u3h_k01_back_page` | `uint32_t u3h_k01_back_page(void)` | id of the menu page to go back to when the startup fails |
| `u3h_show` | `int u3h_show(uintptr_t base, void *s, uint32_t page, int silent)` | opens a menu page through the state object's show-page function; result unused here |
| `u3h_show_dialog` | `void u3h_show_dialog(uintptr_t base, void *s, uint32_t body, uint32_t title, uint32_t action, int32_t buttons, uint32_t extra, uint32_t close)` | opens the game's message dialog with these text and action ids |
| `u3x_online_failed` | `void u3x_online_failed(uintptr_t base, void *s)` | exists (a one-line function, stays): the "network failure" dialog = `u3h_show_dialog(base, s, 0x88a082af, 0xc6467f2d, 0, 0, 0, 0xe1e040aa)` |
| `u3h_so_note` | `void u3h_so_note(const char *a, const char *name, int32_t v, const char *b)` | one line for the session log: text `a`, then `name` if not null, then `v` in decimal if it is ≥ 0, then `b` if not null |
| `u3h_onp_name_apply` | `void u3h_onp_name_apply(uintptr_t base)` | online only: when the account's name is known, writes it over the own network name (image offset 0x17d6010) |
| `u3h_k43_step` | `void u3h_k43_step(uintptr_t base, uint32_t v)` | sets the progress step shown on the "Connecting" page |
| `u3h_gt_open` | `int32_t u3h_gt_open(uintptr_t base)` | G11: opens the game UDP sockets and resets the time-sync state; 0 = open, other = failure |
| `u3h_pl_open` | `void u3h_pl_open(uintptr_t base)` | opens the peer link (does nothing when none is installed) |
| `u3h_onb_object` | `void *u3h_onb_object(void)` | address of the C part's storage for the backend object (0x1060 bytes) |
| `u3h_onb_vt` | `void **u3h_onb_vt(void)` | address of the C part's table of 32 function pointers for the backend |
| `u3x_onb_vtable` | `void u3x_onb_vtable(void)` | exists (stays): fills that table: a do-nothing function answering 0 (`u3x_onb_s_nop`) in all 32 entries, then the backend's slots 0 to 16 in entries 0 to 16 |
| `u3h_onb_addr_reset` | `void u3h_onb_addr_reset(void)` | forgets the peer addresses the server announced and restarts the address probe timer. **New export to add** (one line around the C function `onb_addr_reset`) |
| `u3x_cm_reset_entry` | `void u3x_cm_reset_entry(uint8_t *entry)` | G07 4.9 (old C `lnb_cm_reset_entry`): resets one connection entry and its transport timers |

Part 2:

| Name | Signature | What it does |
|---|---|---|
| `u3x_add_member` | `int32_t u3x_add_member(uintptr_t base, uint8_t *session, const void *info, int you, int host)` | G07 4.17 |
| `u3x_remove_member` | `int32_t u3x_remove_member(uintptr_t base, uint8_t *session, const char *name)` | G07 4.18 |
| `u3x_find_member` | `uint8_t *u3x_find_member(uint8_t *session, uint32_t id)` | the used member slot whose member id is `id` (first of the ten), or null |
| `u3h_gt_stamp` | `void u3h_gt_stamp(uintptr_t base, uint8_t *sub, int off)` | stores the current game time in seconds, as a 32-bit float, at `sub + off` |
| `u3h_onb_pl_np` | `int u3h_onb_pl_np(uintptr_t base, uint8_t *entry, const char *name)` | asks the peer link to start the link to `name` for this connection entry; 0 when no peer link is installed. Result unused here |
| `u3h_onb_pj` | `uint64_t *u3h_onb_pj(void)` | address of four u64: for each binding the room id its pending Join asked for (0 = none) |
| `u3h_lnb_match_room` | `uint64_t *u3h_lnb_match_room(void)` | address of the helper's "match room id" variable (the room the party should follow) |
| `u3h_fw_sess` | `const char *u3h_fw_sess(uintptr_t base, uint8_t *ss)` | the text `match`, `party` or `other` for a session |
| `u3h_fw_line` | `void u3h_fw_line(const char *a, uint32_t hex, int digits, const char *b, int32_t v)` | one line for the session log: `a`; when `digits` is not 0, `0x` and that many hex digits of `hex`; `b` if not null; `v` in decimal if it is ≥ 0 |
| `u3h_cg_log` | `void u3h_cg_log(const char *a, int32_t v)` | one custom-game line for the session log: `a`, then `v` in decimal if it is ≥ 0 |
| `u3x_fl_md` | `void u3x_fl_md(void *lan_be, uint8_t *ps)` | sends this machine's member data to the party: with `lan_be` null through the online backend the party session is attached to |
| `u3x_onb_by_cookie` | `uint8_t *u3x_onb_by_cookie(void *be, uint32_t cookie)` | exists (stays): the session of binding `cookie − 1` when `cookie` is 1..4 and that binding has one; otherwise adds 1 to eb[148] and answers null |
| `u3x_onb_by_room` | `uint8_t *u3x_onb_by_room(void *be, uint64_t room)` | exists (stays): the first binding's session (bindings 0..3) that is not null and whose room id (u64 at session + 0x18) equals `room`, or null. A `room` of 0 matches a session without a room |
| `u3x_onb_strcpy` | `void u3x_onb_strcpy(char *d, const char *s, uint32_t n)` | stray, behaviour complete here: copies the text `s` up to its NUL, at most `n − 1` characters, and ends `d` with a NUL |
| `u3x_onb_note` | `void u3x_onb_note(int slot)` | stray, behaviour complete here: eb[131] plus 1; eb[132] = `slot` |
| `u3x_onb_wire` | `void u3x_onb_wire(uintptr_t base, const char *what, uint8_t *ss, uint64_t room, const char *b, int32_t v)` | stray, behaviour complete here: builds a text of at most 120 characters from `what`, `u3h_fw_sess(base, ss)` and the text ` session, room ` (each character only while fewer than 120 are in the text) and calls `u3h_fw_line(text, low 32 bits of room, 8, b, v)` |
| `u3x_onb_send` | `int32_t u3x_onb_send(const void *msg, uint32_t n)` | stray, behaviour complete here: sends one room message (its u32 type at offset 0). Type below 300 or above 0x3ff: −1. While `u3h_smn_up()` is non-zero: through `u3h_smn_send(msg, n)`, −1 when that answers a negative value. Otherwise through the community link: −1 unless `u3h_ol_up()` is non-zero and `u3h_ol_frame(0x400 + (type − 300), msg, n)` answers non-zero. On success eb[133] plus 1, eb[134] = type, result `n` |
| `u3h_smn_pull` | `int32_t u3h_smn_pull(uint8_t *rx, uint32_t *len, uint32_t cap)` | appends what the session socket has received to `rx` behind the `*len` bytes already there, never beyond `cap`, and updates `*len`; a negative result = the socket is not live. Result unused here |
| `u3h_smn_len` | `uint32_t u3h_smn_len(const uint8_t *m, uint32_t n)` | G15: number of bytes the big-endian message at `m` (of which `n` have arrived) needs in all; 0 = unknown message id |
| `u3h_smn_swap` | `uint32_t u3h_smn_swap(uint8_t *m, uint32_t len)` | G15: converts the big-endian message of `len` bytes at `m` to little-endian in place; returns its id |
| `u3h_smn_got` | `void u3h_smn_got(const uint8_t *m, uint32_t id)` | counts a received message (statistics) |
| `u3h_smn_unknown` | `void u3h_smn_unknown(const uint8_t *m, uint32_t n)` | records an unknown message (statistics) |
| `u3h_ol_up` | `int u3h_ol_up(void)` | pure read: non-zero when the community-server link is signed in |

### 2.4 Strays (old functions in no work group that this document covers or points at)

- **Covered here, part 2:** the message dispatcher behind `u3x_onb_msg_in` (the inventory lists only its two pieces
  "room joined" and "search results": the dispatcher itself, with the handlers for 306, 308, 312, 313, 315, 317,
  319, 321 and 324, is in no group); `u3x_onb_peer_begin`; the internal log line builder and the "every bound
  session in that room" rule (5.1, 5.5); the removal of handled bytes from the buffer (5.14);
  `u3x_onb_ping_restart` and `u3x_onb_ping_due` (5.15).
- **Covered here, part 1:** `u3x_lan_chosen` (4.2).
- **Not covered, behaviour given in 2.3 so that whoever gets them can write them:** `u3x_onb_send`, `u3x_onb_wire`,
  `u3x_onb_note`, `u3x_onb_strcpy`. They are the shared helpers of the sending slots (G14 uses them far more than
  this group) and are in no work group.
- **Not covered, stay as they are:** `u3x_onb_vtable` and `u3x_onb_s_nop` (the table fill: the helper's own
  plumbing, no game logic); `u3x_online_failed`, `u3x_onb_by_cookie`, `u3x_onb_by_room` (the inventory's list of
  one-line functions).
- The inventory puts a `member_record` (LAN record to member description) into this group: it is the LAN routine
  and is covered by G08 (4.1). The online one is `u3x_onb_info` (5.4).

## 3. Game and helper facts

### 3.1 Game data

| Image offset | Type | Meaning |
|---|---|---|
| 0x17b4ba4 | s32 | the engine's local clock in ticks of 1/300 second |
| 0x17bd080 + 8 | u32 | the menu object's UI state; 7 and 8 = the menu is changing pages |
| 0x17d603c | u32 | net menu mode; 2 = Custom Game |
| 0x17d6010 | 0x24 bytes | the player's own network name |
| 0x17d6065 | u8 | "split-screen co-op" flag |
| 0x17d6420 | object | the match session |
| 0x17dc298 | object | the party session |
| 0x17dd758 | pointer | the LAN backend object |
| 0x17dd760 | pointer | the online backend object |
| 0x17dd768 | pointer | the registered session manager |
| 0x17f31b0 | table | the connection table (G07 3.5): next id s32 at +0; 32 entries of 0x220 bytes from +8; blocked-name count u32 at +0x4408; own name (0x24 bytes) at +0x4d90; mode flag u8 at +0x4db4 (1 = LAN, 0 = online) |
| 0x1b68810 | u32 | a net state word of the game (only printed, its low 8 bits) |

Connection entry fields used here (G07 3.5): u8 "in use" at 4; name (0x24 bytes) at 7; s32 handle at 0x34; the
entry's packet object ("sub-object", G12 3.6) at 0x40. Sub-object + 0xa8 is a 32-bit float: the time, in seconds,
at which the last packet from that peer was received (the connection monitor measures the peer's silence from it).

### 3.2 The helper's state object `s`

| Offset | Type | Meaning |
|---|---|---|
| 128 | u32 | number of saved profiles |
| 132 | u32 | the selected profile, 1-based; 0 = none |
| 136 + 64·k | 64 bytes of text | name of profile k (k = 0 is the first) |
| 648 | pointer | the show-page function; null = menu pages cannot be opened |
| 656 | pointer to function | script symbol lookup: one argument (u32 symbol id), result a pointer to a pointer to the symbol's data, or null. The field may be null |
| 680 | u32 | the multiplayer startup phase |

### 3.3 The helper's counter and switch words eb[] (`ext_eb()`)

| Word | Meaning |
|---|---|
| eb[0] bit 4 (value 0x10) | online matchmaking is switched on |
| eb[128] | low 32 bits of the backend object's address |
| eb[129] | backend setups |
| eb[130] | result of the backend's Init |
| eb[131], eb[132] | slot calls, last slot (written by `u3x_onb_note`) |
| eb[135] | room messages handled |
| eb[136] | type of the last one |
| eb[137] | pings sent |
| eb[142] | search rows taken |
| eb[143] | member records applied |
| eb[144] | members removed |
| eb[145] | rooms lost (kicked or destroyed) |
| eb[146] | owner changes |
| eb[148] | messages with a cookie that has no session |
| eb[149] | Updates without a connection |
| eb[150] | online startup state: 1 waiting for the sign-in, 2 signed in, 3 sign-in failed, 4 Init failed, 5 game sockets failed, 6 NetInit done, 7 went offline (8 is written by another group) |
| eb[151] | sign-in wait calls |

### 3.4 The online backend object (0x1060 bytes, the C part owns the storage)

| Offset | Type | Meaning |
|---|---|---|
| 0 | pointer | function table (32 entries of 8 bytes) |
| 8 | 0x24 bytes | the player's network name |
| 0x30 + 8·k, k = 0..3 | pointer | binding k: the session attached under cookie k + 1, or null |
| 0x50 | 32-bit float (as its pattern) | time of the next ping, in seconds |
| 0x54 | u32 | number of received bytes waiting in the buffer |
| 0x58 | 0x1000 bytes | the receive buffer |
| 0x1058 | u64 | the game image base |

The C part asserts this layout (size, bindings at 0x30, ping time at 0x50, base at 0x1058); it must stay.

Entries of the backend's function table called in part 1 (object first; results s64):

| Entry | Arguments after the object | Meaning |
|---|---|---|
| 0 | none | Init; 0 = success |
| 3 | a session | AttachSession |
| 8 | a session, an s32 | set the room attribute |

### 3.5 Session fields used here

Beyond G07 3.6 (member slots at 0x680 + i·0x120: name at 0, u8 "in use" at 0x25, u32 member id at 0x2c, u32 data
length at 0x3c, data at 0x40; u32 own member id at 0x12e4; u32 host member id at 0x12e8; u64 room id at 0x18; u32
capacity at 0x20c):

| Offset | Type | Meaning |
|---|---|---|
| 0x14 | u32 | the game pool id sent when a room is created |
| 0x20 | 128 bytes of text | room name |
| 0xa1 | u8 | create done |
| 0xb0 | u8 | search done |
| 0xb4 | u32 | search result code (0 = fine) |
| 0xb8 | u8 | join done |
| 0xbc | s32 | join result code (0 = fine, −1 = failed) |
| 0xc0 | u32 | joined (1 = yes) |
| 0x204 | u32 | room flags |
| 0x218 | u32 | number of search results |
| 0x220 + 56·i | search result i (room for 20) | u64 room id at 0; host name (36 bytes) at 8; u32 at 44; u32 at 52 |
| 0x12ec | u8 | room attribute flag |

"This machine leads the session" = own member id equals host member id.

Session events (entries of the session's function table, each called with the session as the only argument, result
unused): 1 = create finished, 2 = join finished, 5 = disconnected from the room, 6 = kicked, 7 = owner changed.

Party state fields zeroed at NetInit (G08 3.2): u8 at 0x1334, u32 at 0x1338, u64 at 0x1340, u8 at 0x1348, u32 at
0x1370, u8 at 0x1374, u8 at 0x1375.

### 3.6 Game functions and script data

| Image offset | Arguments | Meaning |
|---|---|---|
| 0x7a2360 | (session) | session initialisation; result unused |
| 0x7a25f0 | (session) | clear the session's members; result unused |

Script symbol 0x8653fbe2 (through the lookup of 3.2): the list of game pool ids: u32 count at 0, pointer at 8 to
`count` u32 values. Read at run time; nothing of it is in the helper.

### 3.7 Ids, timings and constants

| What | Value |
|---|---|
| "choose profile" menu page | 0x4608c1ad |
| network failure dialog (through `u3x_online_failed`) | body 0x88a082af, title 0xc6467f2d, close 0xe1e040aa |
| ticks per second of the local clock | 300 |
| a wait that was not continued for more than | 900 ticks (3 s) starts over |
| progress step unit | 900 ticks; steps 8 to 18 |
| profile retry at most every | 1500 ticks (5 s) |
| wait limits | 3000 ticks (10 s), 9000 ticks (30 s), 45000 ticks (150 s) |
| ping message type | 325 (4 bytes: the type only) |
| seconds per tick, 32-bit float | pattern 0x3b5a740e (0.0033333334, = 1/300) |
| ping interval, 32-bit float | pattern 0x41f00000 (30.0) |
| random step for the pool pick | the linear congruential generator of the C standard's example `rand`: multiplier 1103515245, increment 12345, modulo 2^32 |

### 3.8 The helper's own texts

Session log notes of part 1 (given to `u3h_so_note`):

- N1: `Play Online stopped (No connection found): no sign-in settings and no sign-in running`
- N2: `Play Online: the profile download failed earlier; trying it again (waits up to 30 s)`
- N3: `Play Online stopped (No connection found): sign-in not finished in time, RPCN state ` (ends with a space; the
  state number follows)

Log fragments of part 2 (spaces at the start and end belong to the texts):

| Name | Text |
|---|---|
| T-moved | `room joined (305): the cookie points at another session; given to the one that asked: ` |
| T-joined | `room joined (305): ` |
| T-members | `, members ` |
| T-owner | `, owner id ` |
| T-our | `, our id ` |
| T-cookie | `, cookie ` |
| T-room | `, room ` |
| T-member | `member joined (306): ` |
| T-in | ` in the ` |
| T-left | `room left (308) by member ` |
| T-of | ` of the ` |
| T-roomend | ` room` |
| T-kicked | `we were kicked out of the ` |
| T-destroyed | `the server destroyed the ` |
| T-open | ` room (` |
| T-state | `), net state 0x` |
| T-newowner | `room owner changed (317): ` |
| T-ownerid | `, new owner id ` |
| T-follow | `match room made: party told to follow (member data)` |

### 3.9 The room messages (server to client)

Every message starts with its u32 type. When `u3x_onb_msg_in` sees a message it is little-endian (the session
socket's big-endian stream has been converted in place before, 5.14). Bytes not named are not read.

Member record (104 bytes): name at 0 (36 bytes); u16 member id at 36; u8 NAT type at 38; u8 data length at 39;
member data at 40 (64 bytes).

| Type | Size | Content |
|---|---|---|
| 305 room joined (also the answer to a create) | 160 + 104 · records | u32 cookie at 8; u16 the owner's member id at 12; u16 this machine's member id at 14; u64 room id at 16; u16 capacity at 24; u16 number of member records at 26; member records from 160 |
| 306 member joined | 120 | u64 room id at 8; one member record at 16 |
| 308 member left | 24 | u64 room id at 8; u16 member id at 16 |
| 310 search results | 16 + 56 · rows | u32 cookie at 8; u32 number of rows at 12; rows from 16. Row: u64 room id at 0; u16 at 12; u16 at 16; host name (36 bytes) at 20 |
| 312 kicked | 16 | u64 room id at 8 |
| 313 room destroyed | 16 | u64 room id at 8 |
| 315 member data | 80 | u16 member id at 4; u8 data length at 6; u64 room id at 8; data at 16 (64 bytes) |
| 317 owner changed | 16 | u16 the new owner's member id at 4; u64 room id at 8 |
| 319 room attribute | 16 | u8 at 4 (bit 0 = the flag); u64 room id at 8 |
| 321 room flags | 16 | u16 flags at 4; u64 room id at 8 |
| 324 room name | 144 | u64 room id at 8; the name as text at 16 (128 bytes) |

The cookie is the binding number plus 1 (1..4) that this machine put into its create, join or search request.

## 4. Behaviour, part 1

Rule for conditions: where a condition names several functions, it is evaluated from left to right and stops as
soon as its outcome is known ("and" at the first false part, "or" at the first true part); a function is called
again each time it is named. "Elapsed" comparisons are differences of two s32 clock values, taken as signed 32-bit
numbers.

Part 1 keeps four variables of its own, all zero when the helper is loaded: three s32 clock values (W-start = when
the current wait began, W-last = clock at the last wait call, R-asked = when the profile retry was last asked) and
one u32 (the random state).

### 4.1 `u3x_play_online(base, s)`

1. If the menu's UI state (3.1) is 7 or 8: nothing.
2. If `u3h_onb_shell_route(base)` answers non-zero: nothing more.
3. C = `u3h_online_config(s)` (always called).
4. Online play is **not possible** when: (C is 0 and `u3h_smn_up()` is not 1 and `u3h_rc_signing_in()` is 0), or
   online matchmaking is switched off (eb[0] bit 4 clear). The switch is tested last.
   - Not possible and `u3h_offline_ok()` is 0: `u3h_so_note(N1, null, −1, null)`, then `u3x_online_failed(base, s)`,
     and the function ends.
   - Not possible and `u3h_offline_ok()` is non-zero: `u3h_go_offline()`.
   - Possible: `u3h_set_onb_mode(1)`.
5. Store 0 in the split-screen co-op flag (u8 at image offset 0x17d6065).
6. If `u3h_k01_play_online(base, s)` answers non-zero: the function ends.
7. If the state object's show-page function (s + 648) is not null: `u3h_show(base, s, 0x4608c1ad, 0)`.

### 4.2 `u3x_lan_chosen()`

`u3h_set_onb_mode(0)`, then `u3h_k01_lan()`.

### 4.3 `u3x_signin_wait(base, s)`

1. **Gate.** If `u3h_onb_mode_get()` is not 1, or online matchmaking is switched off (eb[0] bit 4 clear): result 0,
   nothing else happens.
2. **Name.** If the selected profile number (s + 132) is not 0 and not above the profile count (s + 128): copy that
   profile's name (3.2) to the own network name at image offset 0x17d6010: its characters up to the first NUL, at
   most 16, then one NUL. Bytes behind that NUL keep their old content.
3. `u3h_onp_name_apply(base)`.
4. **Clock.** T = the local clock (image offset 0x17b4ba4), read once. eb[151] plus 1. If eb[150] is not 1, or
   T − W-last is more than 900: eb[150] = 1 and W-start = T. Then W-last = T. E = T − W-start in what follows.
5. **Profile sync hold.** P = `u3h_profile_state(&M)` (always called). If all of the following hold, in this
   order: (`u3h_online_status()` is 2 or `u3h_smn_up()` is 1); M is non-zero; `u3h_rc_server_ready()` is non-zero;
   P is 0 or 3; E is less than 9000. Then:
   - if P is 3 and T − R-asked is more than 1500: R-asked = T, `u3h_profile_retry()`, `u3h_so_note(N2, null, −1,
     null)`;
   - `u3h_k43_step(base, 8 + min(E′ / 900, 10))`, where E′ is E taken as an unsigned 32-bit number and the
     division drops the remainder;
   - result 1.
6. **Signed in.** If `u3h_online_status()` is 2, or (`u3h_smn_up()` is 1 and `u3h_onp_pending()` is 0): eb[150] = 2,
   result 0.
7. **Still coming.** Result 1 (nothing else changes) when one of these holds, tested in this order:
   - ((`u3h_online_cfg_ok()` is non-zero and `u3h_online_status()` is not 3 and `u3h_online_status()` is not 4) or
     `u3h_smn_up()` is 2) and E is less than 3000;
   - (`u3h_rc_signing_in()` is non-zero or (`u3h_smn_up()` is 1 and `u3h_onp_pending()` is non-zero)) and E is less
     than 9000;
   - `u3h_srv_retry_pending()` is non-zero and E is less than 45000.
8. **Offline.** If `u3h_offline_ok()` is non-zero: eb[150] = 7, `u3h_go_offline()`, result 0.
9. **Failed.** eb[150] = 3; the startup phase (u32 at s + 680) = 0; `u3h_so_note(N3, null, u3h_rc_state(), null)`;
   if the show-page function (s + 648) is not null: `u3h_show(base, s, u3h_k01_back_page(), 1)`; then
   `u3x_online_failed(base, s)`; result −1.

### 4.4 `u3x_online_netinit(base, s)`

1. The backend object = `u3h_onb_setup(base)` (4.5).
2. Call the backend's Init (entry 0). eb[130] = the low 32 bits of its result. If the result is not 0: eb[150] = 4,
   result −1.
3. Call the game's session initialisation (image offset 0x7a2360) with the party session. Then zero the seven party
   state fields of 3.5, each with its own width.
4. Call the backend's AttachSession (entry 3) with the match session, then with the party session. Then call the
   backend's entry 8 with the party session and the s32 value 1. Results unused.
5. **Pool pick.** If the lookup function of the state object (s + 656) is not null, call it with the symbol id
   0x8653fbe2; if it answers a non-null pointer, the list is what that pointer points to. If there is a list, its
   count is not 0 and its values pointer is not null:
   - if the random state is 0, it becomes the local clock (image offset 0x17b4ba4, as u32) with bit 0 set;
   - the random state advances one step of the generator of 3.7;
   - the pick index is (random state shifted right by 8 bits) modulo the count;
   - the u32 at party session + 0x14 = the list value at that index.
   Otherwise nothing of this step happens (the random state does not advance).
6. If `u3h_gt_open(base)` is not 0: eb[150] = 5, result −1.
7. `u3h_cm_online_reset(base)` (4.6), then `u3h_pl_open(base)`.
8. eb[150] = 6, result 0.

### 4.5 Backend setup: `u3x_onb_setup(base)` (old `u3h_onb_setup`)

In this order:

1. eb[129] plus 1.
2. `u3x_onb_vtable()` (fills the function table; done on every setup).
3. `u3h_onb_addr_reset()`.
4. The object is the C part's storage (`u3h_onb_object()`). Zero all its 0x1060 bytes; store the table address
   (`u3h_onb_vt()`) at 0 and `base` at 0x1058. eb[128] = low 32 bits of the object's address.
5. Game pointers: LAN backend object (image offset 0x17dd758) = null; online backend object (0x17dd760) = the
   object; registered manager (0x17dd768) = the object.
6. Copy the 0x24 bytes of the own network name (image offset 0x17d6010) to object + 8.
7. Result: the object.

An object that was set up before is not shut down first: its bindings, buffer and ping time are simply cleared.

### 4.6 Connection table reset: `u3x_cm_online_reset(base)` (old `u3h_cm_online_reset`)

On the table at image offset 0x17f31b0, in this order: mode flag (+0x4db4) = 0; copy the own network name (0x24
bytes from image offset 0x17d6010) to +0x4d90; reset the 32 entries in ascending order, each with
`u3x_cm_reset_entry` (G07 4.9); blocked-name count (+0x4408) = 0; next id (+0) = 0x1234. Nothing else is touched.
(It is G07's LAN init, 4.10, with the mode flag 0.)

## 5. Behaviour, part 2

### 5.1 Log lines

A log text of this part is built from fragments into at most 120 characters plus the ending NUL. A fragment is
appended character by character; a character is taken only while fewer than 120 are in the text (the rest of that
fragment, and everything after, is dropped). Three kinds of fragment:

- a text of 3.8 or the result of `u3h_fw_sess`;
- a number in decimal, without leading zeros (0 is `0`), most significant digit first;
- a member name: its characters up to the first NUL, at most 16, and each only while fewer than a given limit (60
  or 100, stated below) are in the text.

### 5.2 `u3x_onb_member_link(base, ss, name)`

Nothing happens when the connection table's mode flag is non-zero (LAN), or when `name` equals the table's own name
(table + 0x4d90; name equality as G07 3.1). Otherwise take the first entry, in ascending order, that is in use and
whose name equals `name`. None: nothing. For that entry:

1. if `ss` is the match session: `u3h_gt_stamp(base, entry + 0x40, 0xa8)` (the peer counts as heard now);
2. if the entry's handle (s32 at entry + 0x34) is 0: `u3h_onb_pl_np(base, entry, name)`.

### 5.3 `u3x_onb_peer_begin(base, ss)`

For each of the ten member slots of `ss`, in order: when the slot is in use and its member id differs from the
session's own member id (read anew for each slot): `u3x_onb_member_link(base, ss, the slot's name)`.

### 5.4 `u3x_onb_info(in, r)`

Fills the member description `in` (G07 3.7) from the record `r` (3.9): name pointer = `r`; member id = the record's
u16; NAT type = the record's u8; data length = the record's u8; data pointer = `r` + 40; "carries data" = 1. The
two padding words of the description (at 0x14 and 0x24) are not written.

### 5.5 `u3x_onb_msg_in(be)`: general rules

M = the backend's receive buffer, N = its received count, `base` = the backend's image base field. If N is below
4: result 0. The type is the u32 at the start of M. Every known message has a size (3.9); when N is below that
size the result is 0 and nothing else happens. An unknown type: result 0. A handled message's result is its size.
`u3x_onb_msg_in` never changes the buffer or the count.

"**Every bound session in room R**" = go through the four bindings in order 0..3; a binding takes part when its
session pointer is not null, R is not 0 and the session's room id (u64 at session + 0x18), read when that binding
is reached, equals R. Usually one session matches; all that match are handled, each completely before the next.

### 5.6 Type 305, room joined

1. Size: needs N ≥ 160 first; then K = the number of member records (u16 at 26), size = 160 + 104·K; needs N ≥ size.
2. Read cookie, room id R, owner id, own id (3.9).
3. **Which session.** S = the session of binding cookie − 1 when the cookie is 1..4 (may be null), otherwise none.
   When R is not 0, look through the bindings 0..3 in order for the first that has a session and whose pending
   join room (`u3h_onb_pj()`, same index) equals R:
   - found (binding j): if its session is not S: `u3x_onb_wire(base, T-moved, that session, R, null, −1)` and S =
     that session. Then pending room j = 0;
   - not found (or R is 0): if the cookie is 1..4: pending room (cookie − 1) = 0 (also when that binding has no
     session).
4. If S is none / null: eb[148] plus 1; result = size (the message is consumed).
5. Log: the line T-joined, `u3h_fw_sess(base, S)`, T-members, K, T-owner, owner id, T-our, own id, T-cookie,
   cookie, T-room (5.1) goes to `u3h_fw_line(line, low 32 bits of R, 8, null, −1)`.
6. S's capacity (u32 at 0x20c) = the message's capacity (u16 widened); S's room id = R.
7. For each of the K member records, in order: build its member description (5.4) and call `u3x_add_member(base,
   S, description, you, host)` with you = 1 when the record's member id equals the message's own id, else 0, and
   host = 1 when it equals the message's owner id, else 0; then eb[143] plus 1 (also when the add answered −1).
8. **Join finished.** If S's "join done" byte (0xb8) is 0:
   - when S's room id (read again) is not 0: join result (u32 at 0xbc) = 0; joined (u32 at 0xc0) = 1; raise the
     session event 2;
   - when it is 0: joined = 0; join result = −1; no event;
   - then "join done" = 1.
9. **Create finished.** If S's "create done" byte (0xa1) is 0: raise the session event 1, then "create done" = 1.
10. **Party follows.** If S is the match session, and its room id (read again) is not 0, and this machine leads it
    (3.5), and the net menu mode (image offset 0x17d603c) is 2: store S's room id through `u3h_lnb_match_room()`;
    then, if the party session's room id is not 0 and this machine leads the party: `u3x_fl_md(null, party
    session)`, then `u3h_cg_log(T-follow, −1)`. The tests are made in this order and stop at the first that fails.
11. `u3x_onb_peer_begin(base, S)`.
12. Result = size.

### 5.7 Type 306, member joined (120 bytes)

For every bound session in the message's room (5.5), in this order: build the member description of the record at
16 (5.4); log `u3x_onb_wire(base, line, session, room id, null, −1)` where the line is T-member, the member's name
(limit 60), T-in; `u3x_add_member(base, session, description, 0, 0)`; eb[143] plus 1; `u3x_onb_member_link(base,
session, the record's name)`.

### 5.8 Type 308, member left (24 bytes)

For every bound session in the message's room: look the member up with `u3x_find_member(session, the message's
member id)`; log `u3h_fw_line(line, 0, 0, null, −1)` where the line is T-left, the member slot's name (limit 100;
left out when the member was not found), T-of, `u3h_fw_sess(base, session)`, T-roomend; then, only when the member
was found: `u3x_remove_member(base, session, the member slot's address as the name)` and eb[144] plus 1.

### 5.9 Type 310, search results

1. Size: needs N ≥ 16; then R = the number of rows (u32 at 12), size = 16 + 56·R computed in 32 bits (it wraps for
   huge R); needs N ≥ size.
2. S = `u3x_onb_by_cookie(be, the message's cookie)`. Null: result = size (that function has counted eb[148]).
3. S's search result code (u32 at 0xb4) = 0; S's "search done" byte (0xb0) = 1.
4. For row i = 0 up to min(R, 20) − 1: the session's search result i (3.5) gets: the row's room id; the row's 36
   name bytes; the row's u16 at 12 widened into the u32 at 44; the row's u16 at 16 widened into the u32 at 52. The
   other bytes of the result record are not written. After each row: S's result count (u32 at 0x218) plus 1 and
   eb[142] plus 1.
5. Result = size.

The result count is only incremented here (the Search slot of G14 resets it when the search is sent); row i always
goes to result record i.

### 5.10 Types 312 kicked and 313 room destroyed (16 bytes)

For every bound session in the message's room, in this order: log `u3h_fw_line(line, the u32 at image offset
0x1b68810, 2, null, −1)` where the line is T-kicked (312) or T-destroyed (313), `u3h_fw_sess(base, session)`,
T-open, the type in decimal, T-state; raise the session event 5; for 312 also the session event 6; set the session's
room id to 0; call the game's clear-members function (image offset 0x7a25f0) with the session; eb[145] plus 1.

### 5.11 Type 315, member data (80 bytes)

For every bound session in the message's room: look the member up with `u3x_find_member(session, the message's
member id)`; not found: nothing for that session. Found: the member's data length (u32 at slot + 0x3c) = the
message's length byte as it is (0..255); the first min(length byte, 64) data bytes are copied to slot + 0x40; the
slot's remaining data bytes keep their content.

### 5.12 Types 317 owner changed, 319 room attribute, 321 room flags (16 bytes each)

S = `u3x_onb_by_room(be, the message's room id)`. Null: result 16, nothing else. V = the u16 at 4.

- 317: `u3x_onb_wire(base, T-newowner, S, S's room id, T-ownerid, V)`; S's host member id (u32 at 0x12e8) = V; raise
  the session event 7; eb[146] plus 1; `u3x_onb_peer_begin(base, S)`.
- 319: S's attribute flag (u8 at 0x12ec) = bit 0 of the message's byte at 4.
- 321: S's room flags (u32 at 0x204) = V.

### 5.13 Type 324, room name (144 bytes)

S = `u3x_onb_by_room(be, the message's room id)`. If not null: `u3x_onb_strcpy(S + 0x20, the message's name at 16,
128)`. Result 144 in both cases.

### 5.14 `u3x_onb_s1(be)`, the Update slot

"Remove U bytes from the front" = the bytes behind the first U move to the start of the buffer and the received
count shrinks by U; buffer bytes beyond the new count keep what they held.

1. `u3x_onb_note(1)`.
2. Q = `u3h_smn_up()` (called once).
3. **Session socket** (Q is not 0):
   - if Q is 1: `u3h_smn_pull(buffer, address of the received count, 0x1000)`; its result is ignored;
   - then, while the received count is at least 4:
     - L = `u3h_smn_len(buffer, received count)`. If L is 0 or above 0x1000: `u3h_smn_unknown(buffer, received
       count)`, the received count becomes 0, and the loop ends;
     - if the received count is below L: the loop ends (the rest arrives later);
     - id = `u3h_smn_swap(buffer, L)`; `u3h_smn_got(buffer, id)`; U = `u3x_onb_msg_in(be)`; eb[135] plus 1; eb[136]
       = id; remove U bytes from the front, or L bytes when U is 0 (a message `u3x_onb_msg_in` does not know is
       skipped);
   - result 0. (No ping is sent on this path.)
4. **Community link** (Q is 0):
   - if `u3h_ol_up()` is 0: eb[149] plus 1, result −1;
   - if `u3x_onb_ping_due(be)` is non-zero: `u3x_onb_send` with a 4-byte message holding the u32 325 (result
     ignored); eb[137] plus 1; `u3x_onb_ping_restart(be)`;
   - then, while the received count is not 0: U = `u3x_onb_msg_in(be)`; U = 0 ends the loop (nothing removed);
     otherwise eb[135] plus 1, eb[136] = the type of that message (the u32 at the start of the buffer), remove U
     bytes from the front;
   - result 0.

The buffer is filled between Updates by `u3x_onb_rx` (G14: appends community-link pushes on the same thread, and
refuses while the session socket is in use) or by `u3h_smn_pull` above.

### 5.15 The ping clock: `u3x_onb_ping_restart(be)`, `u3x_onb_ping_due(be)`

Now, in seconds: NOW = (local clock, the s32 at image offset 0x17b4ba4 of the backend's image base, converted to
float) × 0x3b5a740e.

- restart: the ping time (backend + 0x50) = NOW + 30.0 (0x41f00000), stored as a 32-bit float pattern.
- due: 1 when the stored ping time is less than NOW, else 0 (0 when either is not a number).

Float rule (PIPELINE.md): every step in single precision on 32-bit patterns: `f_from_int` of the clock, `f_mul` by
0x3b5a740e, for restart `f_add` of 0x41f00000, for due `f_less(stored, NOW)`. The old build converted the clock
exactly, kept the product unrounded on the x87 unit and rounded only the stored sum; the comparison was made
against the unrounded product. The difference is at most the last place of NOW (a ping moves by at most a frame):
an accepted deviation.

## 6. Test scenarios

Everything here can be tested old against new without the game, with: a fake image addressed through `base` (the
highest offset used is 0x1b68810; the connection table needs 0x17f31b0 + 0x4db8); recording stubs at the two game
function offsets; fake session objects and a fake backend object whose function tables point at recording stubs;
a fake state object with a recording show-page function and a scripted lookup function; a fake state block behind
`u3h_ext()`; and scripted, recording stubs for every function of 2.3 (the pure reads answer from a script that can
change per call; the log stubs record their texts and numbers). Compared: results, all memory (image, sessions,
backend object, eb words), and the order and arguments of all recorded calls. No fake socket or file layer is
needed: this group reaches the network only through the stubs. Because part 1 keeps variables between calls, old
and new are driven with the same call sequences from a fresh load.

**Part 1.**

Play Online:
1. UI state 7, 8: no call at all. UI state 0 with the shell route answering 1: only that call.
2. Settings present, matchmaking on: mode set to 1 (no `u3h_smn_up` / `u3h_rc_signing_in` call), flag 0x17d6065
   cleared, shortcut asked; shortcut 0 with a show-page function: page 0x4608c1ad, silent 0; shortcut 0 without one:
   no page; shortcut 1: no page.
3. No settings: socket live (1) → online; socket 0 or 2 but sign-in running → online; neither → not possible.
4. Matchmaking off with settings present → not possible.
5. Not possible, offline not allowed: N1 noted (v = −1), failure dialog, flag 0x17d6065 unchanged, no shortcut call.
   Not possible, offline allowed: `u3h_go_offline`, then as scenario 2 from the flag on.

LAN chosen: mode 0, then `u3h_k01_lan`.

Sign-in wait (clock values chosen by the test; eb[150] and the three variables carry over):
6. Mode 0 or 2, or matchmaking off: result 0, nothing written, no name copy, eb[151] unchanged.
7. Name copy: selected 0; selected above the count; selected = count; names of 0, 5, 16 and 20 characters (16
   copied, NUL at index 16); bytes behind the NUL unchanged.
8. First call (eb[150] = 0): eb[150] = 1, wait starts. Calls 300 ticks apart keep the start; a gap of 900 keeps it,
   a gap of 901 restarts; eb[150] set to 2 by an earlier success restarts.
9. Profile hold: each of its five conditions failing in turn (status 2 against socket 1; no memory; server not
   ready; P = 1, 2; E = 8999 against 9000); step values for E = 0, 899, 900, 8999 (8, 8, 9, 17) and the cap at 18
   (reachable only through a negative E, e.g. the clock set back: E′ is then huge); P = 3 with retry gaps 1500 and
   1501 (note written, `u3h_profile_retry` called, only for 1501) and a second retry 5 s later.
10. Signed in: status 2; socket 1 with nothing pending; socket 1 with the profile pending (not signed in).
11. Each of the three "still coming" lines true and false, at its limit minus one tick and at its limit (2999 /
    3000, 8999 / 9000, 44999 / 45000); status 3 and 4 with settings fine; socket 2 without settings; a status
    script that changes between the two status calls of the first line.
12. Offline allowed: eb[150] = 7, `u3h_go_offline`, result 0.
13. Failure: eb[150] = 3, phase word 0, N3 with the state number, back page shown silently (silent = 1) only with a
    show-page function, failure dialog, result −1; order of these calls.

Online NetInit:
14. Init result 0: full order: setup, Init, session init (party), seven fields zero (pre-fill them with 0xff: only
    their own widths are cleared), attach match, attach party, entry 8 (party, 1), pool pick, game sockets, table
    reset, peer link; eb[130] = 0, eb[150] = 6, result 0.
15. Init result 1 and −1: eb[130] = low 32 bits, eb[150] = 4, result −1, nothing after Init.
16. Pool pick: lookup function null; lookup answering null; count 0; values pointer null (in all four: session +
    0x14 unchanged, random state unchanged); count 1; count 7 over several NetInits (same sequence as the old code
    for the same first clock value); first clock value even (bit 0 gets set) and 0 (state 1).
17. Game sockets failing: eb[150] = 5, result −1, no table reset, no peer link.

Setup: counters eb[128], eb[129]; table fill and address reset called in that order before the object is cleared;
an object full of 0xff is zero except table pointer, name and base; the three game pointers; exactly 0x24 name
bytes.

Table reset: a table full of 0xff: flag 0, own name, 32 `u3x_cm_reset_entry` calls in ascending order (the stub
sees flag and name already set and the next id not yet 0x1234), blocked count 0, next id 0x1234, all else unchanged.

**Part 2.**

Member link: LAN flag set; own name; no entry with that name; an unused entry with that name (skipped); two used
entries with the name (first only); match session against party session (stamp only for the match session, at
entry + 0x40 with offset 0xa8); handle 0 against non-zero (peer link asked only for 0); order stamp, then peer link.

Peers begin: ten slots mixed (unused, own id, others): one member link call per other used slot, in slot order.

Member record: every field of the description; padding untouched.

One message (buffer and count set directly):
18. N = 0..3: 0. Each known type with N one below its size and at its size; unknown types (0, 302, 307, 325, 326,
    0xffffffff): 0.
19. 305 size: N = 159; N = 160 with 1 record (0); complete with 0, 1, 10, 11 and 37 records.
20. 305 session choice: cookie 1..4 bound; cookie 0 and 5; cookie of an empty binding (eb[148], consumed, pending
    of that cookie cleared); a pending join of another binding asking for the room (T-moved logged for that
    session, that session used, its pending cleared, the cookie's pending kept); pending match on the cookie's own
    binding (no T-moved); room 0 (pending never matched, even when a pending value is 0).
21. 305 effects: capacity and room written before the adds; the you / host flags for each record; eb[143] per
    record also when the add answers −1; the log line's text and number; join done 0 / 1 and create done 0 / 1 (four
    combinations: events 2 and 1, their order, the field values the event stubs see); room 0 with join done 0
    (joined 0, result −1, no event 2); an event stub that clears the room id (later steps read it again).
22. 305 party follow: each of its conditions failing in turn (party session; room 0; not leading; menu mode not 2);
    then the party without a room / not led (match room stored, no send); all true (`u3x_fl_md(null, party)`, then
    T-follow). Peers begin always last.
23. 306: no binding in the room; one; two bindings in the same room (both handled, order 0..3); room 0 in the
    message with a room-less session bound (not handled); name of 16 characters without NUL; the log text.
24. 308: member found (name in the line, remove called with the slot address, eb[144]); not found (line without
    name, no remove, no count).
25. 310: N = 15; rows 0, 1, 20, 21 (20 taken); cookie without session (consumed, eb[148] from `u3x_onb_by_cookie`);
    result count starting at 0 and at 5 (incremented, rows still written from record 0); each field of a result
    record, its unwritten bytes.
26. 312 against 313: text, events (5, then 6 only for 312), room 0, clear-members, eb[145]; the stubs see the old
    room id during the events; two bindings in the room.
27. 315: member not found; length 0, 10, 64, 65, 255 (length stored as it is, at most 64 bytes copied).
28. 317 / 319 / 321: no session for the room (16, nothing); 317 order (log, host id, event 7, counter, peers
    begin); 319 with byte 4 = 0, 1, 2, 3; 321 value; a message with room 0 and a bound session without room (it is
    taken).
29. 324: name of 0, 127 and 128 characters without NUL (127 copied plus NUL); no session.
30. Long log lines: fragments that reach 120 characters are cut there.

Update:
31. Socket live: pull called with the buffer, the count's address and 0x1000; then: count below 4; length 0
    (unknown recorded, count 0); length 0x1001 (same); message incomplete; one complete message; three messages in
    the buffer; a complete message of a type `u3x_onb_msg_in` does not know (L bytes removed, counted, eb[136] =
    id); buffer tail unchanged after a removal. Socket state 2: no pull, the buffer is still worked through.
    Result 0 always; no ping, `u3h_ol_up` not called.
32. Link down: eb[149], result −1, nothing else.
33. Link up: ping due (send of 4 bytes 325, eb[137], restart) and not due; a failing send still counts and
    restarts; several messages; an incomplete or unknown message at the front (loop ends, nothing removed, not
    counted); eb[135] / eb[136].

Ping clock: clock 0, 1, 300, 9000, 2^24 + 1, negative; ping time 0 (after setup: due as soon as the clock is
positive); due exactly at the boundary (stored = NOW: not due); a NaN pattern stored (not due). Old and new are
compared bit for bit with single-precision arithmetic; against the helper build of the old code the largest
difference is reported in units of the last place.

**Needs the live game** (two PCs signed in to the configured servers): that the Play Online menu path, the
Connecting page steps and the failure dialog look right; that real room traffic from the server (create, join, a
second member joining and leaving, kick, owner change after the host leaves, member data, room name, search) is
applied so that the game's party and match machines go on; the peer link and the connection monitor reacting to
the member link; both transports (session socket and community link); a second NetInit after NetShutdown.

## 7. Oddities of the old code, to be kept

1. Play Online tests the matchmaking switch last, after the three sign-in questions; the split-screen flag is not
   cleared on the "not possible" exit.
2. The sign-in wait copies the profile name and applies the account name on every call; it copies at most 16
   characters although the name field holds 0x24 bytes and profile names 64.
3. The progress step's cap at 18 cannot be reached in an ordinary run (the hold ends at 30 s = step 17).
4. In the "still coming" test the link status is asked twice in a row; the profile state is asked even when the
   hold cannot apply.
5. With eb[150] left at 2 (signed in) or 7, the next startup's first wait call starts a new wait: intended.
6. NetInit: the caller ignores the result; a failed Init leaves the backend object registered as the manager, and
   failed game sockets leave in addition both sessions attached to it. Entry 8 is called with the value 1 before the
   party has a room.
7. Pool pick: the shift by 8 bits already leaves fewer than 31 bits, so no further limiting is needed; the random
   state is seeded from the clock only once per helper load.
8. Setup clears the LAN backend pointer but does not shut a previous online object down, and fills the function
   table on every call.
9. `u3x_onb_msg_in`: the number of 305 member records is not limited to the ten member slots (`u3x_add_member`
   refuses the rest, eb[143] still counts them); over the community link a 305 with more than 37 records can never
   be complete in the 0x1000-byte buffer and blocks it.
10. 305: the pending join of the cookie's binding is cleared even when that binding has no session; the capacity
    and room id are written before the members are added; "joined" and the join result are written as 32-bit
    values (the LAN room client writes "joined" as 64 bits, G08 4.1).
11. 305 / 306: eb[143] counts records, not members really added.
12. 306, 308, 312, 313, 315 are applied to every bound session in the room; 317, 319, 321, 324 only to the first,
    and these four also match a room id of 0 against a session without a room.
13. 308 logs also when the member is unknown; 312 / 313 lines end in `net state 0x` and the log function adds its
    own `0x` (the text shows `0x0x..`).
14. 310: the size is computed in 32 bits; rows beyond 20 are skipped but consumed; the result count is incremented
    while the rows are always written from record 0; an unknown cookie is counted in eb[148] by the lookup.
15. 315 stores a data length above 64 unchanged.
16. Update over the community link never skips an unknown or incomplete message: it stays at the front until more
    bytes arrive (an unknown type blocks the buffer until the next setup or Shutdown clears it). Over the session
    socket unknown types are skipped by their framed length, and an unknown id empties the whole buffer.
17. Update sends pings only over the community link; a failed ping send still restarts the 30 seconds.
18. In socket state 2 (reconnecting) Update still answers 0, whatever the community link's state.
19. The C wrappers `onb_member_link`, `onb_peer_begin`, `onb_info` and `onb_msg_in` have no caller in the C part.

## 8. What stays as it is

- Every exported name and signature of 2.1 and 2.2 and the C exports `u3h_onb_setup`, `u3h_cm_online_reset`,
  `u3h_onb_signin_wait`, `u3h_onb_netinit`, `u3h_lan_chosen` (other groups call them).
- The backend object's storage, its function table and the pending-join array in the C part; the object's layout
  (3.4), which the C part asserts.
- `u3x_onb_vtable`, `u3x_onb_s_nop`, `u3x_online_failed`, `u3x_onb_by_cookie`, `u3x_onb_by_room`, and all
  functions of 2.3.
- The message layouts of 3.9 (the servers and the sending slots of G14 use them), the cookie rule, the eb[] indices
  (diagnostic tools read them), the log texts of 3.8 (support reads the session log).
- All game offsets of section 3, the session events and the backend entries 0, 3, 8.
- Everything in section 7.

## 9. Open questions

1. `u3h_onb_addr_reset` does not exist as an export in v7.212 (the C body calls the file-private `onb_addr_reset`).
   Either the integrator adds this one-line export (assumed above), or the C wrapper `u3h_onb_setup` keeps that
   call and makes it before calling `u3x_onb_setup` (same effects: nothing between steps 1 and 3 of 4.5 depends on
   it).
2. `u3x_cm_reset_entry` is the new name G07 proposes for the C function `lnb_cm_reset_entry`; until G07 is
   integrated the C wrapper of 4.6 can pass that function's address, or `u3x_cm_online_reset` is integrated
   together with G07's block C.
3. `u3x_onb_send`, `u3x_onb_wire`, `u3x_onb_note` and `u3x_onb_strcpy` (behaviour in 2.3) and the backend slots 0, 2,
   3, 15 and 16 are in no work group; to be assigned (G14 is the natural place).
4. The meaning of the two 16-bit row values of a search result (u16 at 12 and 16 of a row, stored at 44 and 52 of
   a result record) is not needed here and was not determined; the game's search list reads them.

## 10. Clarifications (tester, 2026-11-14)

Facts confirmed by the differential test of the old helper (v7.212) against the reimplementation. Nothing above
was found wrong; these points make it more exact. Point 11 names three places where the old code reads a value again
after a call it makes; they can only be seen when that call changes the value, which no function of the helper does.

1. **4.1, the offline question.** `u3h_offline_ok()` is asked exactly once when online play is not possible,
   whatever it answers, and not at all when online play is possible.
2. **4.3 / 4.4, clock differences.** They wrap as 32-bit two's-complement numbers: with the clock near the top of the
   s32 range at one call and negative at the next, the elapsed time is the small positive difference. A clock that
   was set back gives a negative elapsed time (every "less than" limit then holds, and the progress step is 18).
3. **4.4 step 5, the list.** The lookup function is called once per NetInit that gets past Init, when its field is
   not null (after entry 8, before `u3h_gt_open`). "There is a list" needs three things: the lookup answers a
   non-null pointer, the pointer it points at is not null, and that list has a count other than 0 and a non-null
   values pointer. In every other case the party's pool id and the random state stay as they are.
4. **4.5, order inside the setup.** The two calls (`u3x_onb_vtable`, then `u3h_onb_addr_reset`) are made before
   anything of the object or of the three game pointers is changed. Steps 4 to 6 make no call, so only their end
   state can be observed.
5. **5.1, numbers.** A number is written as an unsigned 32-bit decimal: a cookie of 0xffffffff is `4294967295`.
6. **5.1, the limits in practice.** With the three texts `u3h_fw_sess` answers, the longest line of this part has 95
   characters (the 305 line with a ten-digit cookie), so the cut at 120 is never reached in the helper; a member
   name starts at character 21 (306) or 26 (308) and has at most 16 characters, so the name limits 60 and 100 never
   decide anything. The cuts were tested with longer session texts given by the test's stub: a line is cut after
   exactly 120 characters and ends with a NUL there.
7. **5.4 / 5.6 / 5.7, the padding words.** `u3x_onb_info` leaves the description's two padding words (0x14, 0x24)
   as they were. In the descriptions that `u3x_onb_msg_in` builds itself for `u3x_add_member` these two words have
   no defined value in the old helper (they differ with the build); nothing may read them. Zeros are fine.
8. **5.5 / 5.6 / 5.9, incomplete messages.** A 305 below 160 bytes or below 160 + 104 · K, and a 310 below 16 bytes
   or below 16 + 56 · R, answer 0 without any call (`u3h_onb_pj` and `u3x_onb_by_cookie` are not called) and
   without writing anything: the pending joins and eb[148] stay.
9. **5.14, what is not part of the behaviour.** How often the helper-state accessor (`u3h_ext`, behind `ext_eb()`)
   is called differs between the old and the new code and is not observable.
10. **5.15, the measured deviation of the ping clock.** Old and new are equal bit for bit when both run in single
    precision. Against the old code built as the helper builds it (x87; the same with 64-bit and 53-bit precision
    control), over every clock value from −70000 to 4000000 and three million values of the whole s32 range:
    - restart: the stored ping time differs in about 11 % of the values, never by more than one unit of its last
      place;
    - due, clock below 2^24 ticks (15.5 hours; the clock is then an exact float): the answers differ only when the
      stored time equals the single-precision NOW exactly (the old code answered "due" in about half of those
      cases, the new one never does; the ping then goes out one Update later);
    - due, clock of 2^24 ticks or more (or below −2^24): the conversion of the clock is rounded as well, so NOW can
      be off by more than one but less than two units of its last place: answers also differ, rarely (about 0.6 %
      of such clock values each), for a stored time one unit below or one unit above NOW, never for two units. The
      statement of 5.15 ("at most the last place of NOW") therefore holds below 2^24 ticks; beyond it the bound is
      two units (at 2^24 ticks one unit is 1/256 second). Accepted under the float rule.
11. **Values read again after a call.** Beyond the "read again" / "read anew" notes of sections 4 and 5:
    - 5.6 step 3, room given to binding j's session: the comparison with S and the session passed to
      `u3x_onb_wire` use what binding j holds before that call; after the call binding j is read again, and S is
      what it holds then (if it is null by then, the message ends as in step 4: eb[148] plus 1, result = size).
      The pending room j is set to 0 in both cases.
    - 5.8: the message's member id (u16 at 16) is read from the buffer for each session, at the moment of that
      session's `u3x_find_member` call (not once for all sessions).
    - 5.11: the message's member id (u16 at 4) is read for each session at the moment of its `u3x_find_member`
      call; the length byte (at 6) is read after that call returned, for each session that has the member.
    In the helper `u3x_onb_wire`, `u3x_find_member`, `u3x_remove_member` and the log functions never change the
    bindings or the receive buffer, so these three points have no effect there; the test shows them only with stubs
    that change the value during the call. All other values of a message that are used after a call (the 305
    capacity, the member records, the 306 record, the value of 317 / 319 / 321) are read at the place the sections
    above give them, in old and new alike.
