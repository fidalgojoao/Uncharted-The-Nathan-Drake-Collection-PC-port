# G20. Invites: list fetch / filter / delete / send, invite rows, invite notices

Behaviour description for a reimplementation (reimpl2 pipeline, describer's document). It says what the functions
do, not how the old ones were written. Conventions as in `G18_friends_list.md` (read it first; its sections 3.1,
3.3 to 3.5 and 3.7 are used here and not repeated): offsets are hexadecimal when prefixed with 0x; "image offset X"
is the address `base + X`; all multi-byte values are little-endian; u8 / u16 / u32 / u64 / s32 / s64 are integers
of that width, "pointer" is 8 bytes; 32-bit additions and subtractions wrap. "Word n" of a list request is the s32
at byte offset 4 x n. There is no float code in this group.

## 1. Purpose and callers

On PS3 other players can leave a party invite for the player on the community's invite service. The PS4 game kept
the invite list object and the "Invites" tab of the friends page but lost the code that fills and uses them. The
helper owns that code. This group is all of it:

- **the invite manager** (part B): asking the service for the invites left for us, filtering the answer (right
  game, less than a day old, not sent by ourselves), filling the game's invite list, a HUD notice for each new
  invite, the "unseen" count and the "seen" mark in the profile, deleting an invite, leaving our own invite for
  another player, and the per-tick work (polling the two requests, reacting to "you were invited" messages,
  releasing a stuck busy flag);
- **the menu side** (part A): the rows of the Invites tab (inviter name, state icon, "... ago" text), the click on
  an invite row (Accept / Decline menu), accepting an invite (ask the inviter's party to let us in), the "join
  party" request itself, and the end of the "Creating Party..." popup (which sends our invite).

Callers (all stay as they are):

| Function | Called by | When |
|---|---|---|
| `u3x_invite_list` | C wrapper `fi_list_source`, last link of the C list-source chain `fr_sources` | every list request of a helper list source |
| `u3x_invite_click` | C wrapper `fi_click`, from the C dispatcher `fi_kind` (helper stub kind 68, sub id 4) with the click's list request | an invite row is clicked |
| `u3x_invite_accept` | C wrapper `fi_accept`, from `fi_kind` (sub id 5), only when ea[126] is non-zero; `callback` = `base` + ea[126], the address of the "Joining Party..." page handler | "Accept" chosen |
| `u3x_send_join` | `u3x_invite_accept`; `u3x_join_party` (C++, group G21); C wrapper `fj_send_join` (no caller left) | a join is asked |
| `u3x_party_created` | C wrapper `fj_create_cb`, from `fi_kind` (sub id 9) | events of the "Creating Party..." popup |
| `u3x_fi_tick` | C wrapper `fi_tick`, from the C per-frame `fr_tick` (only while friends are on and the friend list is set up; after the presence tick of G18) | every helper tick |
| `u3x_fi_refresh` | C wrapper `fi_refresh`, used by: the C `fi_netinit` (net start, not LAN, friends on: it sets both tickets of 3.2 to -1 and asks with `notify` = 1); the C export `u3h_fi_refresh` (called by `u3x_invite_list`, operation 0x17, `notify` = 0); `u3x_fi_tick` (`notify` = 0) | a new list is wanted |
| `u3x_fi_finish` | `u3x_fi_refresh` (request refused) and `u3x_fi_tick` (answer arrived); C wrapper `fi_finish` (no caller left) | the service answered |
| `u3x_fi_delete` | C wrapper `fi_delete`, used by `fi_kind` sub id 6 ("Decline": C sets the busy byte to 1 and passes the clicked row of 3.2) and by the C export `u3h_fi_delete` (called by `u3x_invite_accept`) | an invite is to be deleted |
| `u3x_fi_delete_done` | `u3x_fi_tick`; C wrapper `fi_delete_done` (no caller left) | the service confirmed the delete |
| `u3x_fi_mark_seen` | C wrapper `fi_mark_seen`, used by the C export `u3h_fi_mark_seen` (called by `u3x_invite_list` on every request while the Invites tab is shown) | the player looks at the list |
| `u3x_fi_send` | C wrapper `fi_send`, used by the C export `u3h_fi_send` (called by `u3x_party_created`) | "Invite to Party" once the party exists |
| `u3x_fi_rec_seen` | `u3x_fi_refresh`, `u3x_fi_mark_seen`; C wrapper `fi_rec_seen` (no caller left) | - |
| `u3x_fi_row_why`, `u3x_fi_is_friend` | `u3x_fi_finish`; C wrappers `fi_row_why`, `fi_is_friend` (no caller left) | per row of an answer |

Threading: every function runs on the thread that runs the helper's hooks (the per-frame tick, the list-source hook
and the menu click thunks). No thread is started, no lock is taken, no function is re-entered. The service requests
run elsewhere (C part); this group only starts them and polls their tickets.

## 2. Interface

All functions are `extern "C"`; names and signatures stay exactly as they are (every one already is a C++ function;
the C wrappers already pass their arguments unchanged, so nothing changes in C). No float is passed or returned.

### 2.1 Functions to deliver

The group is written as two independent parts. Part A calls part B only through the exported names below (or the
`u3h_fi_...` exports that forward to them), so each part can be written and tested with the other one stubbed.

**Part A, menu side**

| Function | Signature | Result |
|---|---|---|
| invite rows | `int u3x_invite_list(uintptr_t base, int32_t *ev, void *s)` | 1 = request answered, 0 = not this list |
| row click | `void u3x_invite_click(uintptr_t base, void *s, const int32_t *ev)` | - |
| accept | `void u3x_invite_accept(uintptr_t base, void *s, uintptr_t callback)` | - |
| ask to join a party (stray) | `void u3x_send_join(uintptr_t base, const uint8_t *id, uint64_t party, uintptr_t callback)` | - |
| "Creating Party..." popup events (stray) | `void u3x_party_created(uintptr_t base, const int32_t *ev)` | - |

**Part B, invite manager**

| Function | Signature | Result |
|---|---|---|
| answer of the list request | `void u3x_fi_finish(uintptr_t base, void *s, int32_t rows)` | - |
| per-tick work | `void u3x_fi_tick(uintptr_t base, void *s)` | - |
| "seen" time of the profile (stray) | `uint64_t u3x_fi_rec_seen(uintptr_t base, void *s)` | time, microseconds |
| why a row is not listed (stray) | `uint32_t u3x_fi_row_why(uintptr_t base, const uint8_t *b, uint64_t now)` | 0 listed, 1 / 2 / 3 reason |
| is the sender a friend (stray) | `int u3x_fi_is_friend(uintptr_t base, const uint8_t *nm20)` | 1 / 0 |
| ask for the list (stray) | `void u3x_fi_refresh(uintptr_t base, void *s, int32_t notify)` | - |
| ask for a delete (stray) | `void u3x_fi_delete(uintptr_t base, int32_t i)` | - |
| delete confirmed (stray) | `void u3x_fi_delete_done(uintptr_t base)` | - |
| mark as seen (stray) | `void u3x_fi_mark_seen(uintptr_t base, void *s)` | - |
| leave our invite (stray) | `void u3x_fi_send(uintptr_t base)` | - |

Parameters: `base` game image base; `s` the helper's state object (opaque: only handed on); `ev` a list request
(3.4); `callback` the address of a page event handler; `id` a player id (NUL-terminated text, at most 0x24 bytes);
`party` a party id; `rows` the number of invite records the service delivered into the answer buffer of 3.2 (0 .. 12);
`b` an invite record (3.3); `now` the network time in microseconds; `nm20` a name field of 0x14 bytes (not
necessarily NUL-terminated); `notify` see 4.6; `i` a row of the invite list.

The old file-private pieces behind these exports (the notice builder, the two-request poll, the name comparison,
the key test, the "empty list has nothing unseen" rule) are free helpers of the new code; their behaviour is in
section 4. Part A keeps three file-private variables and part B two (3.5); all start as zero.

### 2.2 Functions of other groups, of the C part and of the shared tools that the new code calls

Not to be described or reimplemented here. `u3h_` are exports of the C part.

| Name | Signature | What it does |
|---|---|---|
| `u3h_ext` | `uint8_t *u3h_ext(void)` | the helper's state block; CONVENTIONS.h has `ext_ea()` for the counters of 3.1 |
| `u3h_fi_state` | `void *u3h_fi_state(void)` | the invite state (3.2), a static 0x4a0-byte block of the C part, all zero at start |
| `u3h_fi_selected` / `u3h_fi_select` | `int32_t u3h_fi_selected(void)` / `void u3h_fi_select(int32_t row)` | read / write the "clicked row" of 3.2 (+0x470) |
| `u3h_fi_list_selection` | `int32_t *u3h_fi_list_selection(void)` | address of the "list selection" of 3.2 (+0x474) |
| `u3h_fi_time` | `uint64_t u3h_fi_time(int32_t i)` | the send time of listed invite i (3.2, +0 + 8 x i); no range check |
| `u3h_fi_delete` | `void u3h_fi_delete(uintptr_t base, int32_t row)` | forwards to `u3x_fi_delete(base, row)` |
| `u3h_fi_mark_seen` | `void u3h_fi_mark_seen(uintptr_t base, void *s)` | forwards to `u3x_fi_mark_seen(base, s)` |
| `u3h_fi_refresh` | `void u3h_fi_refresh(uintptr_t base, void *s, int notify)` | forwards to `u3x_fi_refresh(base, s, notify)` |
| `u3h_fi_send` | `void u3h_fi_send(uintptr_t base)` | forwards to `u3x_fi_send(base)` |
| `u3h_fi_ago` | `const char *u3h_fi_ago(uintptr_t base, void *s, uint32_t seconds)` | the localized "N seconds / minutes / hours / days / weeks ago" text for an age, written into the next of the C part's four rotating text buffers; never null |
| `u3h_fr_buffer` | `char *u3h_fr_buffer(void)` | the next of those four rotating buffers (0x100 bytes each) |
| `u3h_inv_list` | `int32_t u3h_inv_list(uintptr_t base, const uint8_t *me, uint8_t *blobs, uint32_t max)` | starts "list the invites left for the player `me`"; result = a ticket (>= 0) or negative = not started. The records (0x30 bytes each, at most `max`) are written to `blobs` by the poll that reports the answer |
| `u3h_inv_delete` | `int32_t u3h_inv_delete(uintptr_t base, const uint8_t *me, const uint8_t *blob48)` | starts "delete this invite record of player `me`"; ticket or negative |
| `u3h_inv_poll` | `int32_t u3h_inv_poll(uintptr_t base, int32_t ticket)` | state of a ticket: -2 still running; -1 failed (also for a ticket the service does not know); otherwise done: the number of records delivered (list) or 0 (delete). A ticket is finished by the first answer other than -2 |
| `u3h_inv_send` | `int32_t u3h_inv_send(uintptr_t base, const uint8_t *target, const uint8_t *blob48)` | leaves the invite record for the player `target`; 0, or negative on failure |
| `u3h_so_inv_diag` | `void u3h_so_inv_diag(uintptr_t base, uint32_t rows, uint32_t kept, uint32_t w1, uint32_t w2, uint32_t w3, uint32_t wf, uint32_t bad, const uint8_t *first, uint32_t fw)` | one log line with the statistics of a list answer: rows delivered, rows listed, dropped for the key (`w1`), for age (`w2`), as our own (`w3`), listed but not a friend (`wf`), a rejected key (`bad`), the first dropped record or null (`first`) and its reason (`fw`) |
| `u3h_so_note` | `void u3h_so_note(const char *a, const char *nm, int32_t v, const char *b)` | one log line made of: `a` (when not null), `nm` (when not null; at most 20 characters), `v` in decimal (when >= 0), `b` (when not null) |
| `u3h_fj_note` | `void u3h_fj_note(const char *a, const uint8_t *id, const char *b, int32_t v)` | one log line made of `a`, the id (when not null; at most 16 characters), `b` (when not null), `v` in decimal (when >= 0) |
| `u3h_show` | `int u3h_show(uintptr_t base, void *s, uint32_t page, int silent)` | shows a menu page; 0 = failed |
| `u3h_show_dialog` | `void u3h_show_dialog(uintptr_t base, void *s, uint32_t body, uint32_t title, uint32_t action, int32_t buttons, uint32_t extra, uint32_t close)` | shows the game's message dialog with these text and action ids |
| `u3h_key_ok` | `int u3h_key_ok(uintptr_t base, uint32_t key)` | non-zero when `key` is a network version word of this game: any version (low byte) of the retail family or of the beta / demo family; the families are known to the C part only. Like `u3h_fr_key` it also keeps the game's copy of our own word current |
| `u3h_fr_key` | `uint32_t u3h_fr_key(uintptr_t base)` | our own network version word (G18); it also keeps the game's copy of it current |
| `u3h_gate_closed` | `int u3h_gate_closed(uintptr_t base, int32_t *ev)` | 1 when the helper's list sources may not answer now (it has then already given the request an empty text answer); else 0 |
| `u3h_fr_tab` | `uint32_t u3h_fr_tab(uintptr_t base, void *s)` | the text id of the selected tab of the top menu page (0 when unknown); it reads game data, so each call shows in a recording |
| `u3h_fr_ent` | `uint8_t *u3h_fr_ent(uintptr_t base, uint32_t i)` | address of friend entry i (G18 3.4); no checks |
| `u3h_fr_strcmp` | `int u3h_fr_strcmp(uintptr_t base, const void *a, const void *b)` | the game's text compare (image offset 0x139c710) of two NUL-terminated texts; 0 = equal |
| `u3h_fr_state_icon` | `int32_t u3h_fr_state_icon(uintptr_t base, uint8_t *e)` | state icon number of a friend entry: 2 in our party session, else 1 online, 0 offline |
| `u3h_fr_icon22` | `void u3h_fr_icon22(int32_t *ev, int32_t v)` | writes the image answer "icon type 0x22, value v" into a list request |
| `u3h_text` | `const char *u3h_text(uintptr_t base, void *s, uint32_t id)` | localized text of a text id; never null |
| `u3h_lg_profile` | `uint8_t *u3h_lg_profile(uintptr_t base, uint32_t user)` | the profile object of a local user (null when the game has no profiles) |
| `u3h_lg_rec` | `uint8_t *u3h_lg_rec(uintptr_t base, void *s, uint8_t *profile)` | the statistics record of a profile (G22). The first call can reset a just-loaded record to its defaults; repeating the call changes nothing more. It does not accept a null profile |
| `u3h_publish` | `int u3h_publish(uintptr_t base, void *s, uint8_t *profile)` | saves / uploads the profile; result not used here |
| `u3h_mq_send` | `int32_t u3h_mq_send(uintptr_t base, const uint8_t *id, uint32_t type, const void *data, uint32_t length)` | forwards to `u3x_queue_send` |
| `u3h_join_reset` | `void u3h_join_reset(void)` | the previous join attempt is forgotten (C part's join state) |
| `u3h_join_waiting` | `void u3h_join_waiting(uintptr_t base)` | the join watchdog starts waiting |
| `u3h_wait_popup` | `void u3h_wait_popup(uintptr_t base, uint32_t text, uintptr_t callback)` | shows the "wait" popup with this text id and page handler (G18 4.15) |
| `u3x_queue_send` | `int32_t u3x_queue_send(uintptr_t base, const uint8_t *id, uint32_t type, const void *data, uint32_t length)` | existing C++ function: sends the message (`type`, `length` data bytes) to the player `id`; 0, or negative |
| `u3x_queue_find` | `int32_t u3x_queue_find(uint8_t *entries, uint32_t *count, uint32_t type, uint32_t *index, int remove)` | existing C++ function on the received-message queue: finds the first entry of `type`; with `remove` non-zero it takes it out of the queue; 0 = found, -1 = none. `index` is only written when `remove` is 0 |
| `u3x_network_usec` | `uint64_t u3x_network_usec()` | existing C++ function: the network time in microseconds since 0001-01-01 (0 when there is no clock) |
| `u3x_ms_hud` | `int u3x_ms_hud(uintptr_t base, int user, const char *txt, uint32_t type, int32_t level, const uint32_t *ic)` | C++ function of the mission-HUD group: queues a HUD message for a user's menu with message type, level and three icon words; 1 = queued, 0 = text of 0x80 characters or more, or no free slot |
| `u3x_quick_action_add`, `QuickActions` | see G18 2.2 | appends a (text id, action id) pair to a quick-action list (at most 8) |

## 3. Game and helper facts

### 3.1 Counters (the "ea" words, G18 3.1)

u32 at `u3h_ext() + 0x3c50 + 4 x k`; plain increments (wrap) unless noted.

| Word | Offset | Meaning |
|---|---|---|
| ea[99] | 0x3ddc | list requests made |
| ea[100] | 0x3de0 | number of invites listed by the last answer (assigned, not incremented) |
| ea[101] | 0x3de4 | HUD notices tried |
| ea[102] | 0x3de8 | delete requests made |
| ea[103] | 0x3dec | "seen" marks saved |
| ea[104] | 0x3df0 | list requests caused by an invite message |
| ea[105] | 0x3df4 | invites sent |
| ea[106] | 0x3df8 | invites the service refused |
| ea[107] | 0x3dfc | join requests sent |
| ea[111] | 0x3e0c | invite row clicks |
| ea[112] | 0x3e10 | invites accepted |
| ea[113] | 0x3e14 | answered requests of the invite list source |

(ea[97] and ea[98] belong to the message queue functions; ea[126], read by the C dispatcher, is the image offset of
the "Joining Party..." page handler.)

### 3.2 The helper's invite state (`u3h_fi_state()`, 0x4a0 bytes, "ST" below)

The game's list rows have no room for the send time, so the helper keeps the full records next to the game's list.

| Offset | Type | Meaning |
|---|---|---|
| 0x000 | u64 x 10 | send time of listed invite 0 .. 9 (microseconds) |
| 0x050 | 0x30 bytes x 10 | the full record (3.3) of listed invite 0 .. 9 |
| 0x230 | 0x30 bytes x 12 | answer buffer: the records the service delivered for the last list request |
| 0x470 | s32 | clicked row (the invite the Accept / Decline menu is about) |
| 0x474 | s32 | selection of the list widget (kept apart from the clicked row) |
| 0x478 | s32 | ticket of the running list request; negative = none |
| 0x47c | s32 | ticket of the running delete request; negative = none |
| 0x480 | s32 | the `notify` argument of the running / last list request |
| 0x484 | s32 | number of invites listed when that request was made |
| 0x488 | s32 | row being deleted |
| 0x490 | u64 | send time of listed invite 0 when that request was made (0 when the list was empty) |
| 0x498 | u64 | the profile's "seen" time when that request was made |

Listed invite k (k below the count of 3.6) = row k of the game's list + ST time k + ST record k: the three always
describe the same invite.

### 3.3 An invite record (0x30 bytes, as the service delivers and takes it)

| Offset | Type | Meaning |
|---|---|---|
| 0x00 | u32 | network version word of the sender's game (the "key") |
| 0x08 | u64 | the sender's party id (0 = none) |
| 0x10 | 0x14 bytes | the sender's name; NUL-terminated when shorter than 0x14 characters |
| 0x28 | u64 | send time: network time in microseconds |

Bytes 0x04 .. 0x07 and 0x24 .. 0x27 carry no meaning; a record we build has them zero, a delivered record is copied
as it is.

A key **is an invite key** when `u3h_key_ok(base, key)` is non-zero: the test ignores the low byte (the version)
and accepts the two families of this game. The family values are not repeated in the new code or in this
document; the C part holds them.

### 3.4 The list request `ev` (same structure as in `G06_cinema_rows.md` 3.3)

| Byte offset | Word | Type | Meaning |
|---|---|---|---|
| 0x00 | 0 | s32 | operation: 1 title, 2 row text, 4 row image, 5 get selection, 6 get last index, 8 a yes / no question, 10 row picked, 0x17 the list is (re)entered |
| 0x04 | 1 | s32 | row index (in), or the answer (out) |
| 0x38 | 14 | s32 | operation 0x17: non-zero = act on it |
| 0x40 | 16..17 | pointer | text answer (pointer to a NUL-terminated string) |
| 0x50 | 0x14 | u32 | image slot id (operation 4); 0xf5a294f3 = the row icon |
| 0x54 | 0x15 | u32 | column (operation 2): 0 = name, 1 = age |
| 0x98 | - | pointer | the list widget; its u32 at +0x1c is the source tag |

The image answer (words 2, 4 .. 12) is written only through `u3h_fr_icon22`.

### 3.5 File-private variables of the new code (all zero at start)

Part A (helper diagnostics of the list source, "the list log"): the last tab marker logged (s32); a 32-bit set of
operations already logged; up to 8 row-question keys already logged and their number.

Part B: "an invite message is waiting" (flag); "busy without a request since" (s32 game clock value, 0 = not
measuring).

### 3.6 Game data

| Image offset | Type | Meaning |
|---|---|---|
| 0x17d6088 | object | the game's invite list ("IO", = net state +0x430): +0 u32 number of invites listed; +4 u32 number of unseen invites; +8 ten rows of 0x28 bytes; +0x198 u8 busy (non-zero = a request is out, the list must not be used) |
| IO row | 0x28 bytes | +0 u32 key; +8 u64 party id; +0x10 the inviter's name, 0x14 bytes; everything else zero |
| 0x17d6010 | text | our own online name (0x14 bytes are used; NUL-terminated when shorter) |
| 0x17d603c | u32 | the net menu mode (net state +0x3e4, G18 3.7); here only compared with 0 and with 1 (1 = a match is running) |
| 0x17c3871 | 0x24 bytes | the selected player's id (G18 3.7) |
| 0x17c3898 | u64 | written as 0 (G18 3.7) |
| 0x17caeb0 | object | the friend object "NF" (G18 3.3): +0 u32 friend count, +8 pointer entries, +0xc8 u64 busy marker; entry +1 = the friend's id |
| 0x17dc298 | object | the party session "SES": +0x18 u64 party id; +0xc8 u8 non-zero = we are in a party |
| 0x17bd080 | object | the menu: +0x898 pointer to the popup that is up (null = none); that popup's u32 at +0x10 is its page id |
| 0x1b6d3c0 | u32 | number of received messages in the queue |
| 0x1b6d3d0 | array | the received-message queue (0x40 bytes per entry; only handed to `u3x_queue_find`) |
| 0x17b4ba4 | s32 | the game clock, 300 ticks per second |
| 0x17026b8 | object | the game's text system (only its address is passed) |

The profile's "seen" time: in the statistics record `u3h_lg_rec(base, s, u3h_lg_profile(base, 0))`, the u32 at
+0x1610 is its upper half and the u32 at +0x1614 its lower half.

### 3.7 Game functions called

| Image offset | Arguments | Result | Meaning |
|---|---|---|---|
| 0x6528d0 | (pointer: the menu, address of image offset 0x17bd080; pointer: a `QuickActions` list; int 1) | ignored | shows a quick-action menu |
| 0x139c710 | (text a, text b) | int, 0 = equal | text compare; reached through `u3h_fr_strcmp` (the old accept code calls it directly: the same call) |
| 0xf32bd0 | (pointer: the text system; pointer: output buffer; int 0x80, the buffer size; u32 text id; pointer: text argument 1; null; null) | none | writes the localized text of the id, with its placeholder replaced by argument 1, into the buffer (NUL-terminated) |
| 0x652200 | (pointer: the menu; int 0) | ignored | closes the popup / goes back |

### 3.8 Ids, texts and constants

- Source tag of the invite list: 0x4c414eb2. Text id of the Invites tab: 0x885ef7e9.
- Pages: 0xecc3219d (friend actions, the quick-action page), 0xb99a3235 (the lobby).
- Quick-action pairs of an invite row (text id, action id): Join Party (0xb1600ce3, 0xd79bd5ed), then Decline
  (0xc6b89b8c, 0x39764828).
- "Not now" dialog: body 0xed3b546f, title 0xc6467f2d, action 0xa8cda6e0, buttons 0, extra 0, close label
  0xe1e040aa.
- Texts: 0x7f845fbd "You received an invite from [name]" (formatted by game function 0xf32bd0); 0x82c61354 the
  short label used when that is too long; 0x52237405 the "no invites" row text; 0xe4e03363 "Joining Party...".
- Messages: type 0x11d = "you were invited" (no data); type 0x10b = "let me join your party" (0x10 data bytes).
- One day = 86400 seconds; 30 seconds = 9000 game clock ticks; the answer buffer holds 12 records, the list 10.
- Log texts (the helper's own; exact):
  `"invite in the list from "`, `"invite notice: getting the invite list"`,
  `"invite notice: invite list busy, getting it when free"`,
  `"invite list was busy for 30 s with no request: cleared"`,
  `"Invites list: "`, `" rows"`, `"Invites list: not the Invites tab (tab text "`, `")"`, `"Invites list: op "`,
  `" asked (first time)"`, `"Invites list asks op "`, `", row "`, `"Invites list row from "`, `" (a friend)"`,
  `" (not found among friends)"`, `"join: asked "`, `" (0x10b), party id known"`, `" (0x10b), no party id"`.

## 4. Behaviour

"Count" = the u32 at IO +0; "unseen" = the u32 at IO +4; "busy" = the u8 at IO +0x198; "row k" = IO + 8 + 0x28 x k.
"Friends busy" = the u64 at NF +0xc8 is non-zero.

**Finding the inviter among the friends** (used by 4.1, 4.3 and 4.12 with the same rules). Input: a name field of
0x14 bytes, taken as a text of at most 0x14 characters (a copy with a NUL after the 0x14th byte). Result "none"
without any comparison when the name is empty or when friends are busy. Otherwise the friend entries 0, 1, ... are
examined in index order while the index is below the friend count (the count is read again for every entry); an
entry whose id is empty (byte +1 is 0) is skipped without a comparison; each other entry is compared with
`u3h_fr_strcmp(base, entry + 1, name)` (this argument order); the first entry that compares equal is the result
and ends the search. The entries pointer is not tested for null.

**Rule E** ("an empty list has nothing unseen"): when count is 0, unseen becomes 0.

### Part A

### 4.1 `u3x_invite_list(base, ev, s)`: the rows of the Invites tab

1. The widget pointer (ev byte 0x98) is null, or its source tag is not 0x4c414eb2: result 0, nothing else.
2. `u3h_gate_closed(base, ev)` non-zero: result 1, nothing else.
3. Tab log (only for operation 6): T = `u3h_fr_tab(base, s)`. v = count taken as s32 when T is 0x885ef7e9, else
   -1 - (T & 0xffff). The marker is v + 0x10002. When the marker differs from the last tab marker logged (3.5;
   initially 0, a value no marker has for a count below 0x80000000), it is remembered and one line is logged:
   for v >= 0 `u3h_so_note("Invites list: ", null, v, " rows")`, else
   `u3h_so_note("Invites list: not the Invites tab (tab text ", null, T & 0xffff, ")")`.
4. Tab test (every operation; `u3h_fr_tab` is asked again here, so operation 6 asks twice): when the tab text is
   not 0x885ef7e9: for operation 6 word 1 becomes -1; result 1; nothing else.
5. `u3h_fi_mark_seen(base, s)` (4.9). Then n = count (taken as s32), read after that call; ea[113] is incremented.
6. Operation log: when the operation is in 0 .. 31 and not yet in the set of logged operations, it is added and
   `u3h_so_note("Invites list: op ", null, operation, " asked (first time)")` is logged.
7. The operation (word 0):
   - 6: word 1 = n - 1, but 0 when that is negative. Result 1.
   - 1: the text answer points to an empty string. Result 1.
   - 5: word 1 = the list selection (ST +0x474). Result 1.
   - 10: the list selection = word 1. Result 1.
   - 8: word 1 = 1. Result 1.
   - 0x17: nothing when a popup is up with a non-zero page id (menu +0x898 not null and the u32 at +0x10 of it not
     0), or when word 14 is 0, or when busy is non-zero (tested in this order). Otherwise busy becomes 1 and
     `u3h_fi_refresh(base, s, 0)` is called. Result 1 in every case.
   - any other operation: the row part below.
8. Row part. i = word 1. When i < 0 or i >= 10: result 1. Otherwise the name field of row i (row i +0x10, 0x14
   bytes) is taken, whatever n is (also for rows at or beyond n: their old bytes are used), and the inviter is
   looked for among the friends (rule above).
9. Row log: a 32-bit key is formed: bit 0 = inviter found; bit 7 = 1; bits 8 .. 15 = the low byte of i; the
   operation shifted left by 16 bits (32-bit, upper bits lost) OR-ed in; the low byte of word 0x15 shifted left by
   24 bits OR-ed in. When the key is not among the keys already logged and fewer than 8 are logged, it is added and
   three lines are logged, in this order: `u3h_so_note("Invites list asks op ", null, operation, ", row ")`;
   `u3h_so_note(null, null, i, null)`; `u3h_so_note("Invites list row from ", name, -1, t)` with t =
   `" (a friend)"` when the inviter was found, else `" (not found among friends)"` (name = the NUL-terminated copy).
10. Inviter not found: result 1, nothing else (the row stays empty).
11. Operation 4: when word 0x14 is 0xf5a294f3 and n is not 0:
    `u3h_fr_icon22(ev, u3h_fr_state_icon(base, inviter entry))`. Result 1.
12. Operation 2, by column (word 0x15 as u32):
    - column 0: when n is 0 the text answer = `u3h_text(base, s, 0x52237405)`; otherwise b = `u3h_fr_buffer()`, the
      0x15 bytes of the NUL-terminated name copy are written to b and the text answer = b.
    - column 1 and n not 0: age = (the low 32 bits of `u3x_network_usec()` / 1000000) - (the low 32 bits of
      `u3h_fi_time(i)` / 1000000), both divisions unsigned 64-bit and truncating, the difference a wrapping 32-bit
      one taken as signed; a negative age counts as 0; the text answer = `u3h_fi_ago(base, s, age)`. The calls are
      made in this order: `u3x_network_usec`, `u3h_fi_time`, `u3h_fi_ago`.
    - any other column, or column 1 with n = 0: nothing.
13. Every other operation: nothing. Result 1.

### 4.2 `u3x_invite_click(base, s, ev)`: an invite row clicked

1. Nothing when count is 0.
2. When the net menu mode (image 0x17d603c) is 1: the "not now" dialog
   `u3h_show_dialog(base, s, 0xed3b546f, 0xc6467f2d, 0xa8cda6e0, 0, 0, 0xe1e040aa)`; nothing else.
3. Otherwise, with row = ev word 1 (not range-checked): `u3h_fi_select(row)`; the 0x14 name bytes of that row are
   copied to image 0x17c3871 (the bytes 0x14 .. 0x23 of the selected player's id keep their old values); the u64 at
   image 0x17c3898 becomes 0.
4. A quick-action list with argument 0, page 0xecc3219d and the two pairs of 3.8 (Join Party, then Decline) is
   shown with game function 0x6528d0 (menu, list, 1) directly (not through `u3h_fr_qa_push`). ea[111] is
   incremented.

### 4.3 `u3x_invite_accept(base, s, callback)`: Accept on the clicked invite

1. row = `u3h_fi_selected()` (asked in every case). Nothing more when count is 0 or row < 0. (No upper limit is
   tested here.)
2. When the net menu mode is not 0: `u3h_show(base, s, 0xb99a3235, 0)` (the lobby page; result ignored).
3. When `u3h_key_ok(base, key of the row)` is non-zero:
   - the row's name is not empty and friends are busy: the function ends here (the invite stays; no delete, no
     counter; step 2 has already happened);
   - otherwise the inviter is looked for among the friends (rule above, with the row's name field); when found:
     `u3x_send_join(base, inviter entry + 1, the row's party id (u64 at row +8), callback)`.
4. In every other case (key not ours, inviter not found, or join asked): busy becomes 1, then
   `u3h_fi_delete(base, row)`, then ea[112] is incremented.

### 4.4 `u3x_send_join(base, id, party, callback)` (stray): ask a player to let us join

In this order: `u3h_join_reset()`; `u3h_mq_send(base, id, 0x10b, m, 0x10)` with the 0x10-byte message m = the u64
`party` at +0, the byte 0x4a at +9, the byte 1 at +10, every other byte 0 (result ignored); ea[107] incremented;
`u3h_fj_note("join: asked ", id, t, -1)` with t = `" (0x10b), party id known"` when `party` is not 0, else
`" (0x10b), no party id"`; `u3h_join_waiting(base)`; `u3h_wait_popup(base, 0xe4e03363, callback)`.

### 4.5 `u3x_party_created(base, ev)` (stray): events of the "Creating Party..." popup

Only for event 6 (ev word 0), and only when we are in a party (SES +0xc8 non-zero) whose id (SES +0x18) is not 0:
`u3h_fi_send(base)` (4.10), then game function 0x652200 (menu, 0). Otherwise nothing.

### Part B

### 4.6 `u3x_fi_refresh(base, s, notify)` (stray): ask for the list

`notify` non-zero marks the net-start request, whose answer shows no HUD notices when the list was empty before
(4.8); every other caller passes 0.

In this order: ST seen (+0x498) = `u3x_fi_rec_seen(base, s)`; ST previous count (+0x484) = count; ST previous
newest (+0x490) = ST time 0 when count is not 0, else 0; ST notify (+0x480) = `notify`; unseen = 0; busy = 1; ST
list ticket (+0x478) = `u3h_inv_list(base, address of image 0x17d6010, ST +0x230, 12)`; ea[99] incremented. When
that result is negative: the ticket becomes -1 and the answer is handled at once as `u3x_fi_finish(base, s, 0)`.

The count and the rows are not touched by the request.

### 4.7 `u3x_fi_rec_seen`, `u3x_fi_row_why`, `u3x_fi_is_friend` (strays)

`u3x_fi_rec_seen(base, s)`: the profile's "seen" time (3.6): upper half x 2^32 + lower half, from the record of
profile 0. The profile is not tested for null.

`u3x_fi_row_why(base, b, now)`: why the record b is not listed. The key test `u3h_key_ok(base, key of the record)`
is made first, exactly once, in every case (it also keeps the game's copy of our own key current, which must happen
for every record examined). The old code calls `u3h_fr_key(base)`, drops its result and tests the two families
itself; `u3h_key_ok` does exactly these two things, so the new code uses it and holds no family value. Then, in
this order:
- 1 when the record's key is not an invite key (3.3);
- 2 when the record is a day old or older: age = (low 32 bits of `now` / 1000000) - (low 32 bits of the record's
  send time / 1000000), divisions unsigned 64-bit and truncating, difference wrapping 32-bit taken as signed; a
  day old = age >= 86400 (a negative age, i.e. a send time in the future, is not old);
- 3 when the sender is ourselves: our own name (image 0x17d6010) is not empty and equals the record's name field:
  byte for byte over at most 0x14 bytes, equal when all bytes up to and including the first NUL of our name
  match, or when all 0x14 bytes match;
- else 0.

`u3x_fi_is_friend(base, nm20)`: 1 when the inviter is found among the friends (rule at the top of this section),
else 0.

### 4.8 `u3x_fi_finish(base, s, rows)`: the service's answer

1. now = `u3x_network_usec()`, read once.
2. The delivered records ST +0x230 + 0x30 x j, j = 0, 1, ... are examined in order while j < `rows` (signed; a
   negative or zero `rows` examines none) and fewer than 10 are listed. For each: w =
   `u3x_fi_row_why(base, record, now)`.
   - w not 0: the record is dropped and counted by reason (key / age / own). The first dropped record and its
     reason are remembered. For the key reason the rejected key is remembered when no non-zero key has been
     remembered yet (so the value reported is the first non-zero rejected key, or 0).
   - w = 0: the record is listed as the next invite k (k = number listed so far):
     `u3x_fi_is_friend(base, record + 0x10)` is asked and a 0 counted ("not a friend"; such an invite is listed all
     the same); ST record k = the 0x30 bytes; ST time k = the record's send time; row k of IO = 0x28 zero bytes
     with the key at +0, the party id at +8 and the 0x14 name bytes at +0x10.
   Records after the tenth listed one are not examined (and not counted).
3. count = the number listed (K); ea[100] = K. Rows, records and times from K on keep their old contents.
4. `u3h_so_inv_diag(base, rows (0 when negative), K, dropped for key, dropped for age, dropped as own, listed
   non-friends, the rejected key, the first dropped record (its address in the answer buffer) or null, its reason
   (0 when none))`.
5. For each listed invite 0 .. K-1 in order: `u3h_so_note("invite in the list from ", name, -1, null)`, name = the
   sender's name as a NUL-terminated text of at most 0x14 characters.
6. HUD notices, skipped altogether when ST previous count is 0 and ST notify is not 0: for each listed invite
   0 .. K-1 in order whose time is greater than ST previous newest (unsigned 64-bit): the notice of 4.8.1 with that
   sender's name.
7. unseen = the number of listed invites whose time is greater than ST seen (the value stored when the request was
   made). busy = 0.

#### 4.8.1 The HUD notice for one sender

1. A 0x80-byte text buffer whose first byte is 0 is filled by game function 0xf32bd0 (text system, buffer, 0x80,
   0x7f845fbd, sender name, null, null).
2. When the resulting text is longer than 34 bytes, it is replaced by: the first 60 bytes at most of
   `u3h_text(base, s, 0x82c61354)` (nothing when that pointer is null), then `": "`, then the sender's name
   (as long as the text stays below 0x7e bytes), NUL-terminated.
3. `u3x_ms_hud(base, 0, text, 0, 0, icon)` with the three icon words 0, 0, 0xffffffff (no icon); result ignored.
   ea[101] is incremented whether or not the message was queued.

### 4.9 `u3x_fi_mark_seen(base, s)` (stray)

Nothing when busy is non-zero, when count is 0, or when `u3x_fi_rec_seen(base, s)` >= ST time 0 (unsigned 64-bit;
the three tests in this order, the "seen" time being read only when the first two passed). Otherwise, with p =
`u3h_lg_profile(base, 0)` and r = `u3h_lg_rec(base, s, p)`: the u32 at r +0x1610 = the upper half of ST time 0, the
u32 at r +0x1614 = its lower half; unseen = 0; `u3h_publish(base, s, p)`; ea[103] incremented. (The old code asks
for the profile twice and for the record three times in all; how often is not part of the behaviour, but the
record must have been asked for before its two words are written.)

### 4.10 `u3x_fi_send(base)` (stray): leave our invite for the selected player

A record (3.3) is built from zeros: key = `u3h_fr_key(base)`; party id = SES +0x18 when SES +0xc8 is non-zero (else
it stays 0); name = the 0x14 bytes at image 0x17d6010; send time = `u3x_network_usec()` (asked after the key).
Then, with target = the address of image 0x17c3871: r = `u3h_inv_send(base, target, record)`; ea[106] is
incremented when r is negative, else ea[105]; then, in both cases,
`u3x_queue_send(base, target, 0x11d, null, 0)` (result ignored).

### 4.11 `u3x_fi_delete(base, i)` and `u3x_fi_delete_done(base)` (strays)

`u3x_fi_delete`: when i < 0 or i >= count (count taken as s32): busy = 0, nothing else. Otherwise busy = 1; ST
deleting (+0x488) = i; ST delete ticket (+0x47c) = `u3h_inv_delete(base, address of image 0x17d6010, ST record i)`;
ea[102] incremented; when that result is negative the ticket becomes -1, rule E is applied and busy = 0.

`u3x_fi_delete_done`: with g = ST deleting and n = count: when 0 <= g < n, the listed invites g+1 .. n-1 each move
one place down (row of IO 0x28 bytes, ST record 0x30 bytes, ST time) and count becomes n - 1 (the old last place
keeps its bytes). In every case rule E is then applied and busy = 0. The unseen number is not recounted.

### 4.12 `u3x_fi_tick(base, s)`: once per helper tick

In this order:

1. The list request: when ST list ticket >= 0, r = `u3h_inv_poll(base, ticket)`; unless r is -2 the ticket becomes
   -1 and then `u3x_fi_finish(base, s, r)` is called, with 0 in place of a negative r.
2. The delete request: when ST delete ticket >= 0, r = `u3h_inv_poll(base, ticket)`; unless r is -2 the ticket
   becomes -1 and then: r >= 0: `u3x_fi_delete_done(base)`; r < 0: rule E, busy = 0.
3. Invite messages: `u3x_queue_find(address of image 0x1b6d3d0, address of image 0x1b6d3c0, 0x11d, &index, 1)` is
   repeated until it returns non-zero, so every queued message of type 0x11d is taken out. When at least one was
   found: if busy is non-zero and the "waiting" flag (3.5) is not yet set,
   `u3h_so_note("invite notice: invite list busy, getting it when free", null, -1, null)`; the flag is set.
4. Stuck busy flag: when busy is 0, "since" (3.5) becomes 0. When busy is non-zero and both tickets are negative:
   t = the game clock (image 0x17b4ba4); if "since" is 0 it becomes t; otherwise, if t - "since" (wrapping, signed)
   is greater than 9000: busy = 0, "since" = 0 and
   `u3h_so_note("invite list was busy for 30 s with no request: cleared", null, -1, null)`. When busy is non-zero
   and a ticket is out, "since" is left as it is.
5. When the "waiting" flag is set and busy is (now) 0: the flag is cleared;
   `u3h_so_note("invite notice: getting the invite list", null, -1, null)`; busy = 1;
   `u3x_fi_refresh(base, s, 0)`; ea[104] incremented.

## 5. Test scenarios

Everything can be tested old against new on a fake image (IO, NF with a few friend entries, SES, the menu, the
queue, the name, the clock, a profile record) with scripted `u3h_` exports (the service: `u3h_inv_list`,
`u3h_inv_poll`, `u3h_inv_delete`, `u3h_inv_send`; the others recording), recording stubs for game functions
0x6528d0, 0x139c710, 0xf32bd0, 0x652200, and the shared `u3x_` functions of 2.2 either real (queue, HUD) or
recorded. Compare memory (image, ST, ea words), results and the order and arguments of all calls. Nothing needs the
live game; the live check is only that an invite sent from a second player shows up, can be accepted and declined.
Each part's private variables start at zero: scenarios that depend on them (list log, waiting flag, "since") run
from a fresh start.

Part A

1. List source, not ours: widget null; tag 0x4c414eae (result 0, nothing touched).
2. Gate closed (result 1, no tab question).
3. Operation 6 on another tab (word 1 = -1, the "not the Invites tab" line once, not again on the next request;
   two tab questions); operation 2 on another tab (result 1, nothing, one tab question).
4. Operation 6 on the Invites tab with count 0, 1 and 3 (word 1 = 0, 0, 2; "Invites list: N rows" logged when N
   changed; the mark-seen call; ea[113]); operation logged the first time only.
5. Operations 1, 5, 10, 8 (answers; selection stored and read back; never the clicked row).
6. Operation 0x17: popup up with page id not 0; popup pointer set with page id 0 (refresh happens); word 14 = 0;
   busy already 1; the free case (busy 1, refresh with notify 0).
7. Row part: row -1 and row 10 (nothing); name empty; friends busy (no comparison, "not found" logged); inviter
   is friend 2 with friend 0 having an empty id (one skipped, two comparisons, argument order); a ninth distinct
   row question is not logged, a repeated one neither.
8. Operation 4: slot 0xf5a294f3 with n > 0 (icon 0, 1, 2 by the stub); another slot; n = 0; not a friend.
9. Operation 2: column 0 with n > 0 (name in the rotating buffer, 0x14-character name without NUL in the row);
   column 0 with n = 0 and a stale row naming a friend ("no invites" text); column 1 with ages 0, 59, a send time
   in the future (0), network clock 0 (age negative -> 0), a time difference that only shows above 2^32 seconds
   (low 32 bits decide); column 1 with n = 0; column 7.
10. Click: count 0; mode 1 (dialog only); row 0 and row 3 (clicked row, 0x14 bytes copied, bytes beyond kept,
    0x17c3898 zero, the list with two pairs, page, argument, the menu call with 1, ea[111]).
11. Accept: count 0; clicked row -1; mode 0 and mode 2 (lobby page shown first); key not ours (no search; busy 1,
    delete, ea[112]); key ours, name empty (no search, delete); friends busy with a name (page shown, then
    nothing: no delete, no counter); inviter found (join with entry +1, the row's party id and the callback, then
    delete); inviter not found (delete only).
12. Send join: party 0 and not 0 (message bytes, both log texts, the order of the six calls, ea[107]).
13. Party created: event 5; event 6 not in a party; in a party with id 0; in a party with an id (send, then
    0x652200).

Part B

14. Row filter: a key the `u3h_key_ok` stub accepts and one it refuses (the stub is called once per record, before
    anything else; with the real export: both families with several low bytes accepted, a key that differs in any
    of the upper 24 bits and key 0 refused); age 86399
    (kept), 86400 (2), a send time one second in the future (kept), microsecond values just below and above a
    second boundary; own name equal (3), equal on a full 0x14-byte name, different after the NUL of our name
    (still 3), record name longer than ours (not ours), our name empty (0); the key test made in all cases; key
    reason wins over age, age over own. (A tester who records C exports sees `u3h_fr_key` in the old code where
    the new code shows `u3h_key_ok`: an accepted difference, both keep the game's key copy current.)
15. Is friend: empty name; friends busy; found at index 0 / last; entries with empty ids skipped; not found;
    count 0.
16. Seen time: both halves set (order of the halves).
17. Refresh: count 0 (previous newest 0) and count 2 (previous newest = time 0); ticket 0x5100 (stored, busy 1,
    unseen 0, count untouched); ticket -5 (stored as -1, finish with 0 rows: count 0, diag with zeros, unseen 0,
    busy 0).
18. Finish: rows -1 and 0; 12 good rows (10 listed, the last two not examined or counted); a mix of all three drop
    reasons (counters, first dropped record and reason, first non-zero rejected key when the first rejected key is
    0); a non-friend listed and counted; rows, ST records and times written, row padding zero, places from K on
    untouched; the "in the list" notes; notices: previous count 0 with notify 1 (none), previous count 0 with
    notify 0 (all), previous count 2 (only those newer than previous newest; equal time = none); unseen against ST
    seen (equal = seen); busy 0; ea[100].
19. Notice: a formatted text of 34 bytes (kept) and of 35 (short form "label: name"); a label longer than 60
    bytes (cut at 60); the HUD stub returning 0 (ea[101] still counted); icon words.
20. Mark seen: busy; count 0; seen >= time 0 (nothing, also no profile write); seen < time 0 (both words, unseen
    0, publish, ea[103]).
21. Send: in a party (party id in the record) and not; record bytes (padding zero); service result 0 (ea[105])
    and -1 (ea[106]); the 0x11d message in both; call order.
22. Delete: i = -1 and i = count (busy 0 only); valid with a ticket; valid with a refused request (ticket -1,
    busy 0). Delete done: deleting the first, a middle and the last of three (moves, count, last place stale);
    deleting index out of range (count unchanged, busy 0); the list becoming empty (unseen 0); unseen not
    recounted otherwise.
23. Tick: no tickets, nothing queued, not busy (no call but the queue search); list ticket pending (-2: stays);
    list answer 3 and -1 (finish with 3 / 0, ticket -1); delete answer 0 (done) and -1 (rule E, busy 0); both in
    one tick (list first); ST all zero as at helper start (ticket 0 is polled); two 0x11d messages among others
    (both removed, the others kept in order); message while busy (the waiting note once, not again for a second message
    later), then a refresh in the tick that finds busy 0; message while free (refresh in the same tick, busy 1,
    ea[104]); busy with no ticket: armed at the first tick, cleared with the note after more than 9000 ticks, not
    at exactly 9000; clock value 0 at the first tick (not armed); busy with a ticket out ("since" unchanged);
    stuck flag cleared and a waiting message in the same tick (refresh follows at once).

## 6. Oddities of the old code that are behaviour to keep

1. List source, operation 6 answers the last index (n - 1, at least 0), so an empty list still has one row; that
   row shows "no invites" only when the old bytes of row 0 still name a friend (the name is read from the row
   whatever the count is). Rows at or beyond the count are answered from their old bytes in the same way, with
   the time of that old place.
2. An invite whose sender is not a friend is listed and counted, but its row stays empty (no name, icon or age):
   only friends' invites are visible. The accept of such a row deletes it without joining.
3. The tab is asked twice for operation 6; the tab log uses the marker rule of 4.1 (a count change logs again).
4. The row log key mixes the operation (shifted) with the column byte, so operations of 0x100 and above could
   collide; only 8 keys are ever logged.
5. Click and accept do not range-check the row (accept only refuses a negative clicked row); the delete does.
   Click copies 0x14 bytes of the id only.
6. Accept shows the lobby page before it knows whether it will do anything, and returns without deleting while
   the friend list is being rebuilt (the player accepts again).
7. Accept sets busy to 1 before the delete request, which sets it again or clears it.
8. Only listed invite 0 is taken as "the newest" (previous newest, mark seen): the service is expected to deliver
   the newest first. The unseen number uses the "seen" time read when the request was made, not when the answer
   arrives, and is not recounted after a delete.
9. `notify` works inverted to its name: non-zero suppresses the notices for a list that was empty before.
10. The row filter accepts any version of the two key families, not only our own key (an invite from another game
    version is listed); it compares seconds in 32-bit arithmetic; a future send time is accepted.
11. The first rejected key is not remembered when it is 0.
12. The answer is trusted to hold at most 12 records; `rows` is not clamped.
13. The notice counter and the "in the list" note do not depend on success; the notice length test is in bytes,
    not characters.
14. The stuck-busy timer uses 0 as "not measuring", so it does not arm in a tick whose clock is 0, and it keeps an
    old start value while a ticket is out.
15. Before the net start sets the tickets to -1 (or in LAN mode, where it never does), the zeroed state has ticket
    0 for both requests: the first tick polls ticket 0 twice, gets "failed" and runs an empty answer.
16. Send: the 0x11d message goes out even when the service refused the invite; the invite carries party id 0 when
    we are not in a party.
17. Send join ignores the result of the message send and shows the popup anyway.
18. Mark seen and the seen-time getter do not test the profile for null.
19. The C wrappers `fi_finish`, `fi_delete_done`, `fi_rec_seen`, `fi_row_why`, `fi_is_friend` and `fj_send_join`
    have no caller left in v7.212.

## 7. Strays, and what stays as it is

Strays added to this group (in no work group of the inventory; they are the rest of the same invite module and are
called by this group's functions directly or through the C exports): `u3x_fi_refresh`, `u3x_fi_delete`,
`u3x_fi_delete_done`, `u3x_fi_mark_seen`, `u3x_fi_send`, `u3x_fi_rec_seen`, `u3x_fi_row_why`, `u3x_fi_is_friend`
with their file-private bodies and helpers (old names `request_list`, `request_delete`, `remove_deleted`,
`mark_seen`, `send_invite`, `poll_requests`, `same_name`, `invite_key`, `if_empty_none_unseen`, `name_of`, the
`InviteState` layout), `u3x_send_join` (also called by G21's `u3x_join_party`: G21 refers to 4.4) and
`u3x_party_created` (the only caller of the invite send).

Stays as it is:

- The C wrappers, the dispatcher `fi_kind`, `fi_netinit`, `fr_tick`, `fr_sources` and all `u3h_` exports of 2.2;
  the invite state block itself (a static of the C part, reached through `u3h_fi_state`); `fi_ago` with its
  rotating buffers.
- `u3x_queue_find`, `u3x_queue_send`, `u3x_network_usec`, `u3x_time_ago`, `u3x_quick_action_add` (existing C++
  functions outside this group), `u3x_ms_hud` (mission-HUD group), `u3x_friend_icon` / `u3x_friend_state_icon` /
  `u3x_page_tab_text` (G19, reached through the exports).
- Nothing of this group has to stay in C; there is no float code.

Tables: the group holds no table of game values. The ids of 3.8 are single text / action / page ids.

## 8. Open questions

1. The meaning of word 14 of the list request in operation 0x17 is not named in the old source (other lists use it
   the same way: "act on the re-entry"); the spec gives the test only.
2. The texts behind 0x82c61354, 0xc6b89b8c and the dialog ids are known only by their use (label of the short
   notice, second entry of the row menu, "not now" dialog); an implementation needs the ids only.
3. The number of `u3h_lg_profile` / `u3h_lg_rec` calls in 4.9 and of `u3h_fr_ent` calls in the friend search is
   declared not observable. If the tester records them, the old numbers are: mark seen = profile twice, record
   three times (one of each inside the seen-time getter); friend search = one `u3h_fr_ent` per entry examined.
4. `u3x_send_join` and `u3x_party_created` sit between this group and G21; they are described here once. If G21's
   spec describes `u3x_send_join` too, the two texts must agree (4.4 is the reference for the message bytes).

## 9. Clarifications (tester, 2026-11-14)

Facts confirmed by the old-against-new test (`reimpl2\G20\REPORT.md`). None changes the behaviour described above.

1. **No network clock (scenarios 9 and 14).** The ages of 4.1 and 4.7 are differences of whole seconds in 32-bit
   arithmetic, so with a clock of 0 the age is negative only when the low 32 bits of the send time's seconds are
   below 2^31. For a send time of today they are above it (about 3.8 x 10^9; this holds until July 2042), and
   0 minus that wraps to a positive age of about 4.9 x 10^8 seconds. So with a clock of 0 the age column shows
   that age (not 0), and the row filter drops every such record as a day old (reason 2): without a network clock no
   delivered invite is listed. "Age negative -> 0" in scenario 9 holds only for send times whose seconds have low
   32 bits below 2^31.
2. **Send join (4.4, scenario 12).** There are five calls (`u3h_join_reset`, `u3h_mq_send`, `u3h_fj_note`,
   `u3h_join_waiting`, `u3h_wait_popup`). The "sixth" step of scenario 12 is the ea[107] increment; it happens
   after the message is sent and before the note is logged.
3. **A count of 2^31 or more** (never the case: the count is written only by 4.8 and 4.11 and is at most 10).
   The old code computes "count - 1" (4.1 step 7) and "v + 0x10002" (step 3) as signed 32-bit values, whose
   overflow the language leaves open; the new code wraps, as the introduction says. Tested with counts
   0x7ffeffff, 0x7fffffff, 0x80000000, 0x80000005 and 0xffffffff: old and new agree in all of them (tab log line,
   marker, word 1) except word 1 for a count of exactly 0x80000000, where the old build answers 0 and the wrapping
   rule gives 0x7fffffff. On the Invites tab such a count makes v negative, so the "not the Invites tab" line is
   logged with the low 16 bits of the tab text id (old and new alike).
4. **Delete done (4.11).** The old code compares the row being deleted with the count as an unsigned value, the
   new code as a signed one. They agree for every count below 2^31; above it the old code would move rows far
   beyond the list.
5. **Click (4.2 step 4).** The quick-action list has room for 8 pairs. The old code leaves the 12 words behind
   the two pairs as they happen to be; their content is not behaviour (the list says it holds 2 pairs).
6. **Short notice (4.8.1 step 2).** The limit of 0x7e bytes cannot be reached: the label is cut at 60 bytes, the
   separator has 2 and a name at most 0x14.
7. **Steps without an effect of their own.** In `u3x_fi_delete` rule E after a refused request can act only if
   the list was emptied while `u3h_inv_delete` ran (the count was above i just before), which the real export
   never does. In 4.12 step 1 the 0 put in place of a negative poll answer cannot be told from the negative
   value itself: 4.8 examines no record for either and reports 0 rows.
8. **The operation word (4.1).** The old code reads word 0 of the request again at each use. Of the functions
   called in between only `u3h_gate_closed` gets the request, and it runs before the first use, so reading the
   word once after the gate call is the same.
9. **Exports and tools (2.2), as v7.212 has them.** `u3h_fr_ent(base, i)` is the entries pointer (NF +8) plus
   0x240 x i. `u3h_fr_strcmp` only calls game function 0x139c710 with the same two arguments. `u3h_key_ok` does to
   the game's copy of our key exactly what `u3h_fr_key` does (the u32 at image offset 0x17d5c68 is set to our key
   when it differs) and then tests the family of the key it was given. `u3x_queue_find`, `u3x_queue_send`,
   `u3x_network_usec`, `u3x_ms_hud` and `u3x_quick_action_add` are `extern "C"` functions of the C++ part with
   exactly the signatures of 2.2; `QuickActions` is a type of the C++ part with the layout of G18 2.2.
10. **Open question 3, counted.** Mark seen: the old code asks for the profile twice and for the record three
    times (the new code twice and twice). The friend search of the list source and of `u3x_fi_is_friend` asks
    `u3h_fr_ent` once per entry examined, entries without an id included. The old accept step takes the entry
    addresses itself (no `u3h_fr_ent`, the game's text compare called directly); the new one goes through
    `u3h_fr_ent` and `u3h_fr_strcmp`, with the same compares in the same order.
