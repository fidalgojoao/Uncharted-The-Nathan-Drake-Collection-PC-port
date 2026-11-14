# G21. Party join: the join request and answer exchange, join hold queue, join state machine

Behaviour description for a reimplementation (reimpl2 pipeline, describer's document). It says what the functions
do, not how the old ones were written. Conventions as in `G18_friends_list.md` and `G20_invites.md` (read both
first; G18 sections 3.1, 3.5 to 3.7 and G20 sections 2.2, 3.7 and 4.4 are used here and not repeated): offsets are
hexadecimal when prefixed with 0x; "image offset X" is the address `base + X`; all multi-byte values are
little-endian; u8 / u16 / u32 / u64 / s16 / s32 / s64 are integers of that width, "pointer" is 8 bytes. Game clock
values are s32; their additions and subtractions wrap (two's complement) and a difference is taken as signed.
There is no float code in this group.

## 1. Purpose and callers

On PS3 a player can follow a friend into the friend's party: the joiner sends the friend message 0x10b ("let me
join your party"), the friend's game answers with message 0x10c (the party's room id, or 0 = refused), the joiner's
party session joins that room, and the join is complete once the connection to the party leader is up. The PS4
game kept the party session and its state machine but lost the menu code around it, and its machine drops some
steps. The helper owns the missing code. This group is all of it that is in the C++ part:

- **Part A, requests and messages**: "Join Party" on the selected player (asks the join); the pump that takes the
  received messages from the online service and sorts them (join requests and the helper's own join messages are
  handled by the helper, everything else goes to the game's message queue); the **hold queue** for join requests
  that the host cannot answer yet; the late 0x10c answer for a request for which the game first had to create a
  party.
- **Part B, the joiner's state machine**: the page handler of the "Joining Party..." popup (waits for the answer,
  hands the room id to the party session, watches the room join, checks that the friend really is in the room,
  waits for the link to the party leader, opens the lobby; one retry; 20 s timeout), the step that hands a room id
  to the game's party machine, the watcher that keeps that machine from getting stuck with a room id it never
  reads, giving a join up, and the diagnostic lines.

`u3x_send_join` (the 0x10b request with the popup) belongs to this exchange but is described in `G20_invites.md`
4.4 and is not part of this group.

Callers (all stay as they are):

| Function | Called by | When |
|---|---|---|
| `u3x_join_party` | C wrapper `fj_join`, from the C dispatcher `fi_kind` (helper stub kind 68, sub id 7), only when ea[126] is non-zero; `callback` = `base` + ea[126], the address of the "Joining Party..." page handler | "Join Party" chosen in a quick-action menu |
| `u3x_message_events` | C wrapper `mq_events`, from the C per-frame `fr_tick` (only while friends are on and the friend list is set up; after the friend events, before the presence tick) | every helper tick |
| `u3x_join_hold` (stray) | C wrapper `fj_hold`, from the C function behind `u3h_join_host_answer` (so: from inside `u3x_message_events`) | a join request has to wait |
| `u3x_join_hold_tick` | `u3x_join_reply_tick` only | every helper tick while requests are held |
| `u3x_join_reply_tick` | C wrapper `fj_reply_tick`, from `fr_tick` (after the invite tick of G20) | every helper tick |
| `u3x_fj_cb` | C wrapper `fj_cb`, from `fi_kind` (sub id 8) with the popup page's event record | events of the "Joining Party..." popup |
| `u3x_pj_tick` | C wrapper `pj_tick`, last call of `fr_tick` | every helper tick |
| `u3x_pj_giveup` | `u3x_fj_cb` (timeout); C wrapper `pj_giveup`, used by the C watchdog `fj_watch` (the popup did not end the join within 25 s) | a join is given up |
| `u3x_pj_accept` | `u3x_fj_cb` (the answer arrived); C wrapper `pj_accept` (no caller left) | the 0x10c answer |
| `u3x_pj_line` (stray) | `u3x_pj_accept`, `u3x_pj_tick`; C wrapper `pj_line` (no caller left) | diagnostics |
| `u3x_fj_member`, `u3x_fj_fail` (strays) | `u3x_fj_cb`; C wrappers `fj_member`, `fj_fail` (no caller left) | - |

Threading: every function runs on the thread that runs the helper's hooks (the per-frame tick and the menu click
and page thunks). No thread is started, no lock is taken. One nesting exists: `u3x_message_events` calls
`u3h_join_host_answer`, which can call `u3x_join_hold`; the hold tick never runs inside that.

## 2. Interface

All functions are `extern "C"`; names and signatures stay exactly as they are (every one already is a C++
function; the C wrappers already pass their arguments unchanged, so nothing changes in C). No float is passed or
returned.

### 2.1 Functions to deliver

The group is written as two independent parts. Neither part calls the other; they share no variable (part B's
state is a block of the C part, part A's hold queue is private to part A). Each can be written and tested alone.

**Part A, requests and messages**

| Function | Signature | Result |
|---|---|---|
| Join Party on the selected player | `void u3x_join_party(uintptr_t base, void *s, uintptr_t callback)` | - |
| received messages | `void u3x_message_events(uintptr_t base)` | - |
| hold a join request (stray) | `void u3x_join_hold(uintptr_t base, const uint8_t *id, const uint8_t *d)` | - |
| held requests, per tick | `void u3x_join_hold_tick(uintptr_t base)` | - |
| late answer, per tick | `void u3x_join_reply_tick(uintptr_t base)` | - |

**Part B, the joiner's state machine**

| Function | Signature | Result |
|---|---|---|
| member by name (stray) | `uint8_t *u3x_fj_member(uint8_t *ps, const uint8_t *name)` | the member slot, or null |
| the join failed (stray) | `void u3x_fj_fail(uintptr_t base, void *s, const char *why)` | - |
| party state line (stray) | `void u3x_pj_line(uintptr_t base, const char *what)` | - |
| hand the room id to the party machine | `void u3x_pj_accept(uintptr_t base, uint64_t id, int flag)` | - |
| give the join up | `void u3x_pj_giveup(uintptr_t base)` | - |
| watch the waiting room id | `void u3x_pj_tick(uintptr_t base)` | - |
| "Joining Party..." popup events | `void u3x_fj_cb(uintptr_t base, void *s, int32_t *ev)` | - |

Parameters: `base` game image base; `s` the helper's state object (opaque: only handed on); `callback` the address
of a page event handler; `id` (of `u3x_join_hold`) a player id of 0x24 bytes; `d` the data of a 0x10b message, of
which 0x10 bytes are read; `ps` a session object (party or match session, 3.3); `name` a player name (text; at
most 16 characters are compared); `why`, `what` NUL-terminated texts for the log; `id` (of `u3x_pj_accept`) a room
id; `flag` the flag byte of the answer; `ev` the event record of a page handler, of which only the s32 at +0 (the
event number) is read.

The old file-private pieces (the timeout step, the answer step, the line builder, "this machine leads the
session", the room status) are free helpers of the new code; their behaviour is in sections 4 and 5.

### 2.2 Functions of other groups, of the C part and of the shared tools that the new code calls

Not to be described or reimplemented here. `u3h_` are exports of the C part. "Pure read" = no effect; how often
such a function is called is not part of the behaviour unless a step says so.

| Name | Signature | What it does | Part |
|---|---|---|---|
| `u3h_ext`, `ext_ea()` | CONVENTIONS.h | the helper's counters ea[] (3.1) | A, B |
| `u3x_send_join` | `void u3x_send_join(uintptr_t base, const uint8_t *id, uint64_t party, uintptr_t callback)` | G20 4.4: forgets the previous join attempt, sends 0x10b to `id`, arms the watchdog and shows the "Joining Party..." popup with `callback` as its page handler | A |
| `u3h_presence` | `int32_t u3h_presence(uintptr_t base, const uint8_t *id, uint8_t *presence)` | the presence details (0x190 bytes, G18 3.5) of the friend `id`; 0, or negative on failure | A |
| `u3h_show_dialog` | `void u3h_show_dialog(uintptr_t base, void *s, uint32_t body, uint32_t title, uint32_t action, int32_t buttons, uint32_t extra, uint32_t close)` | shows the game's message dialog with these text and action ids | A, B |
| `u3h_show` | `int u3h_show(uintptr_t base, void *s, uint32_t page, int silent)` | shows a menu page; result not used here | B |
| `u3h_fj_note` | `void u3h_fj_note(const char *a, const uint8_t *id, const char *b, int32_t v)` | one log line made of `a`, the id (when not null; its characters up to a NUL, at most 16), `b` (when not null), `v` in decimal (when >= 0) | A, B |
| `u3h_so_note` | `void u3h_so_note(const char *a, const char *nm, int32_t v, const char *b)` | one log line made of `a` (when not null), `nm` (when not null; at most 20 characters), `v` in decimal (when >= 0), `b` (when not null) | B |
| `u3h_msg_event` | `int32_t u3h_msg_event(uintptr_t base, uint8_t *id, uint32_t *type, uint8_t *data, uint32_t max)` | takes the next message another player sent us from the online service: the sender's id into `id` (0x24 bytes), the message type into `*type`, at most `max` data bytes into `data`. Result: the message's data length (can be reported larger than `max`); negative = no message waiting | A |
| `u3h_join_host_answer` | `int u3h_join_host_answer(uintptr_t base, const uint8_t *id, const uint8_t *d, int32_t n)` | the host's decision about a 0x10b request (`n` data bytes at `d`) from `id`: it answers it at once (0x10c), holds it (through `u3x_join_hold`) or leaves it to the game. Non-zero = dealt with, must not be queued; 0 = queue it for the game | A |
| `u3h_join_msg_in` | `int u3h_join_msg_in(uintptr_t base, const uint8_t *id, uint32_t type, const uint8_t *d, int32_t n)` | takes the helper's own join messages 0x1f0 (join info; it fills the "hint" of 3.2) and 0x1f1; non-zero = taken | A |
| `u3h_join_locked` | `int u3h_join_locked(uintptr_t base)` | pure read: non-zero when our party is locked (bit 0 of the u32 at SES +0x204, or the u8 at image 0x17c3570) | A |
| `u3h_join_can_accept` | `int u3h_join_can_accept(uint8_t *ps, uint32_t state)` | pure read: non-zero when a request can be answered with our party now: we are in a party (u8 at `ps` +0xc8 non-zero and u64 at `ps` +0x18 not 0) and `state` is none of 0, 1, 2, 5, 7 | A |
| `u3h_join_party_reply` | `uint64_t u3h_join_party_reply(uintptr_t base)` | pure read: the party id to answer with: the u64 at SES +0x18, or 0 while the net menu mode (image 0x17d603c) is 1 | A |
| `u3h_join_info_send` | `void u3h_join_info_send(uintptr_t base, const uint8_t *id)` | sends the helper's join info message 0x1f0 to `id` and logs it | A |
| `u3h_join_answer` | `void u3h_join_answer(uintptr_t base, const uint8_t *id, uint64_t party, uint8_t flag)` | sends message 0x10c to `id`: 0x10 bytes, the u64 `party` at +0, `flag` at +8, the rest 0 | A |
| `u3h_join_request` | `uint8_t *u3h_join_request(void)` | address of a 0x24-byte buffer of the C part: the id of the player whose request the game is serving (3.4) | A |
| `u3h_join_request_on` | `uint8_t *u3h_join_request_on(void)` | address of a u8 of the C part: "a request is being served" | A |
| `u3h_join_request_pc` | `uint8_t *u3h_join_request_pc(void)` | address of a u8 of the C part: "the requester whose request went to the game runs this helper" (also written by `u3h_join_host_answer`) | A |
| `u3x_queue_post` | `int32_t u3x_queue_post(uintptr_t base, const uint8_t *id, uint32_t type, const void *data, uint32_t length)` | existing C++ function: appends a received message to the game's message queue (3.5) with a copy of its data in the game's heap, and counts it in ea[97]. -1 and nothing queued or counted when `length` is 0x1fc or more, when the queue holds 64 entries or when the heap has no room; else 0 | A |
| `u3x_queue_send` | `int32_t u3x_queue_send(uintptr_t base, const uint8_t *id, uint32_t type, const void *data, uint32_t length)` | existing C++ function (G20 2.2): sends a message to the player `id`; 0, or negative | A |
| `u3h_mq_send` | `int32_t u3h_mq_send(uintptr_t base, const uint8_t *id, uint32_t type, const void *data, uint32_t length)` | forwards to `u3x_queue_send` | B |
| `u3h_mq_find` | `int32_t u3h_mq_find(uintptr_t base, uint32_t type, uint32_t *index, int remove)` | `u3x_queue_find` (G20 2.2) on the game's message queue: 0 = an entry of `type` exists, its index in `*index` (with `remove` 0); else -1 | B |
| `u3h_mq_get_np` | `int32_t u3h_mq_get_np(uintptr_t base, uint32_t index, uint8_t *npid, void *data, uint32_t *len)` | calls game function 0x776530 (3.6) with (index, npid, data, len): takes entry `index` out of the game's message queue | B |
| `u3h_join_state` | `void *u3h_join_state(void)` | pure read: address of the join state (3.2), a static 0x50-byte block of the C part, all zero at start | B |
| `u3h_members` | `uint32_t u3h_members(uint8_t *session, uint8_t **out)` | `u3x_session_members` (G12 4.1): lists the member slots in use of a session that has a backend and a room, our own member first; result = how many (0 .. 10) | B |
| `u3x_party_leader` | `int u3x_party_leader(uintptr_t base)` | existing C++ function, pure read: 1 when the party session has no room (u64 at SES +0x18 is 0) or our member id equals the host's (SES +0x12e4 = SES +0x12e8); else 0 | B |
| `u3h_gt_entry_by_id` | `uint8_t *u3h_gt_entry_by_id(uintptr_t base, int32_t id)` | pure read (`u3x_entry_by_id`, G12): the first of the 32 connection entries whose id (s32 at +0) equals `id`, in use or not; null when none | B |
| `u3h_gt_conn_state` | `int32_t u3h_gt_conn_state(uintptr_t base, int32_t id)` | pure read (`u3x_connection_state`): 2 when the connection entry with that id exists, is in use and has a peer link handle; else -1 | B |

`u3h_join_reset()` and `u3h_join_waiting(base)` (G20 2.2; called by `u3x_send_join`) write the join state of 3.2:
the first clears "retried", "hint ok" and "direct since"; the second sets "waiting" to 1, "last link" to 0xff and
"wait since" to the game clock. They are named here because part B relies on those values.

## 3. Game and helper facts

### 3.1 Counters (the "ea" words, G18 3.1)

u32 at `u3h_ext() + 0x3c50 + 4 x k`; plain increments (wrap).

| Word | Offset | Meaning |
|---|---|---|
| ea[108] | 0x3e00 | answers accepted (a room id was handed to the party machine) |
| ea[109] | 0x3e04 | joins completed (the lobby was opened) |
| ea[110] | 0x3e08 | joins failed with the dialog |

(ea[107], join requests sent, is counted by `u3x_send_join`; ea[97] / ea[98] by the queue functions; ea[126], read
by the C dispatcher, is the image offset of the "Joining Party..." page handler.)

### 3.2 The join state (`u3h_join_state()`, 0x50 bytes, "JS" below; part B)

The C part owns the block and reads and writes it too (its watchdog, its "follow the host into the match" step,
`u3h_join_reset`, `u3h_join_waiting`, `u3h_join_msg_in`). The layout must stay; bytes not listed are padding and
are never written.

| Offset | Type | Name here | Meaning |
|---|---|---|---|
| 0x00 | s32 | timeout | game clock value after which the join is given up |
| 0x04 | u8 | answered | 1 = the friend's 0x10c answer was taken and its room id handed on |
| 0x05 | u8 | waiting | 1 = a join is running (the C watchdog closes the popup 25 s after "wait since" while this is set) |
| 0x06 | u8 | last link | the link value last logged (5.7 step 9); 0xff = none yet |
| 0x07 | u8 | retried | 1 = a fresh party has been asked for once |
| 0x08 | u8 | seen | bit 0 (1) "waiting for the room" logged; bit 1 (2) "in a room" logged; bit 2 (4) "the match session got the room" logged |
| 0x0c | s32 | wait since | start of the watchdog's 25 s |
| 0x10 | s32 | noted at | game clock value of the last "waiting" / "in a room" line |
| 0x14 | s32 | direct since | 0 = off; else the clock value from which the C part tries to follow the host into its match |
| 0x18 | 0x14 bytes | from | the first 0x14 bytes of the id of the player who answered |
| 0x30 | u8 | hint flags | from the host's 0x1f0 message; bit 0 = the host is playing as the host of a match |
| 0x31 | u8 | hint ok | 1 = a 0x1f0 message arrived for this attempt |
| 0x32, 0x34, 0x38 | s16, u32, u64 | - | rest of the hint (not used by this group) |
| 0x40 | u64 | pending id | the room id last seen waiting at SES +0x1340 |
| 0x48 | s32 | pending since | game clock value when it was first seen |
| 0x4c | u8 | pending phase | 0 = no id is waiting; 1 = an id waits; 2 = it waited 5 s and was reported |
| 0x4d | u8 | pending dropped | 1 = a room join was abandoned; a room that still arrives must be left |

### 3.3 Game data

| Image offset | Type | Meaning |
|---|---|---|
| 0x17b4ba4 | s32 | the game clock, 300 ticks per second |
| 0x17bd080 | object | the menu (only its address is passed) |
| 0x17c3871 | 0x24 bytes | the selected player's id (G18 3.7) |
| 0x17d5c60 | u8 | the net block's LAN flag (non-zero = the net was started for LAN) |
| 0x17d5c61 | u8 | "multiplayer active" (set at net start, cleared at net shutdown) |
| 0x17d5c80 | u8 | "network error" flag (only printed) |
| 0x17d6420 | object | the match session (same layout as the party session up to +0x12e8) |
| 0x17dc298 | object | the party session, "SES" below |

Session fields (G07 3.6, G13 3.5; the party-only fields from +0x1334 on exist only in SES):

| Offset | Type | Meaning |
|---|---|---|
| 0x08 | pointer | the backend object the session is attached to; null = none |
| 0x18 | u64 | room id (= the party id); 0 = no room |
| 0xa1 | u8 | "create done" |
| 0xb8 | u8 | "join done" |
| 0xbc | s32 | join result; negative = refused |
| 0xc0 | u64 as read here | "joined": non-zero = the join gave a room |
| 0xc8 | u8 | "leave done"; while it is non-zero and the room id is not 0, we are in a party |
| 0x680 + 0x120 x i, i = 0 .. 9 | member slot | +0 the member's name / id (text, up to 0x24 bytes); +0x25 u8 in use; +0x28 s32 member index; +0x2c u32 member id; +0x34 s32 connection id |
| 0x12e4 | u32 | our own member id |
| 0x12e8 | u32 | the host's member id |
| 0x1334 | u8 | "leave asked": writing 1 makes the game's party machine leave the room |
| 0x1338 | u32 | the party state (below) |
| 0x1340 | u64 | the room id waiting to be joined; 0 = none. The game's machine reads and clears it |
| 0x1348 | u8 | non-zero while the game is creating a party for a join request it took from its queue |
| 0x1349 | 0x24 bytes | the id of that requester |
| 0x1374 | u8 | the flag byte that came with the room id |

"This machine leads the session" = the u32 at +0x12e4 equals the u32 at +0x12e8 (a different test from
`u3x_party_leader`, which is also true when there is no room).

The party state, as far as this group relies on it: 0 = no party (a join request that reaches the game's queue in
this state makes the game create one); 1 = the start state, the only one in which the machine reads the waiting
room id; 2 = waiting for "create done"; 5 = waiting for "join done"; 6 = in a party; 7 = waiting for "leave done";
3, 4 and everything above 7 have no code in the game.

### 3.4 Helper state used by part A

- The hold queue, private to part A (all zero at start): at most 4 entries, in arrival order. An entry holds the
  requester's id (0x24 bytes), the request's flag byte, the game clock value when it was first held, and the 0x10
  bytes of the request.
- Three variables of the C part, reached through their accessors: the served request's id (0x24 bytes), "a request
  is being served" (u8), "the requester runs this helper" (u8).

### 3.5 The game's message queue and the messages

The queue of received messages: u32 count at image 0x1b6d3c0, 64 entries of 0x40 bytes from image 0x1b6d3d0 (+0
sender id, 0x24 bytes; +0x28 u32 type; +0x30 pointer to the data, null = none; +0x38 u32 data length). This group
touches it only through `u3x_queue_post`, `u3h_mq_find` and `u3h_mq_get_np`.

| Type | Direction | Data |
|---|---|---|
| 0x10b | joiner to host: "let me join your party" | 0x10 bytes from this helper: u64 party id as the joiner knows it (0 = unknown) at +0; u8 flag at +8; u8 0x4a at +9 = "sent by this helper" (a console sends 0 or nothing there); u8 at +10: 1 = first request, 2 = "that room was not yours: make a fresh party". A console's request has at least 9 bytes |
| 0x10c | host to joiner: the answer | 0x10 bytes: u64 room id of the party at +0, 0 = refused; u8 flag at +8 (the request's flag byte) |
| 0x116 | party leader to its members: "follow me into this room" | 8 bytes: u64 room id |
| 0x1f0, 0x1f1 | the helper's own join messages | handled by the C part (`u3h_join_msg_in`) |

### 3.6 Game functions called

| Image offset | Arguments | Result | Meaning |
|---|---|---|---|
| 0x652200 | (pointer: the menu, address of image 0x17bd080; int 0) | ignored | closes the popup / goes back |
| 0x77d270 | (pointer: SES) | ignored | one step of the game's party machine (its per-frame update) |
| 0x776530 (through `u3h_mq_get_np`) | (u32 index, pointer id out, pointer data out, pointer to u32 length) | s32 | -1 when the index is not below the queue count (nothing written). Otherwise the entry's 0x24 id bytes are copied out; when the entry has data and both pointers are given: -1 when the u32 at the length pointer is smaller than the entry's data length (the entry stays in the queue), else the data is copied out and the u32 becomes its length; the entry's data is freed, the entry is removed (later ones move down) and the result is 0. An entry without data leaves the data buffer and the length untouched |
| the u64 stored at backend + 0x38 | (pointer: the backend, pointer: SES) | ignored | see 5.5 step 3 and oddity 1 |

### 3.7 Ids, timings and texts

- Dialog "Unable to join party": body 0x4d972420, title 0xc6467f2d. Two forms: the **locked** form with action
  0x52a9f855, buttons 0, extra 0, close label 0; the **failed** form with action 0xa8cda6e0, buttons 0, extra 0,
  close label 0xe1e040aa.
- Pages: 0x53cbb5f2 (the multiplayer page), 0xb99a3235 (the lobby).
- Timings in game clock ticks: 5 s = 1500; 20 s = 6000; 2 s = 600; 1 s = 300.
- Log texts (the helper's own; exact; the leading and trailing spaces belong to them).
  Part A: `"join: "`, `" party locked (presence): not asked"`, `"held join request from "`, `" accepted"`,
  `" refused: locked"`, `" refused after 5 s, state "`, `" to the game (state 0)"`, `"join request from "`,
  `" answered: new party up"`.
  Part B, state line: `": party state "`, `", room "`, `", pending id "`, `", join done/result/joined "`,
  `", create done "`, `", leave done/asked "`, `", net error "`, `", we lead "`, `"/"`, `"-"`.
  Part B, others:
  `"join: 0x10c in"`, `"join: after the first step"`, `"join: party state "`,
  `" at the answer: an earlier join was never answered; dropped, the new id is joined now"`,
  `" at the answer: a state without code; reset to 0"`,
  `"join: the party room join was never answered: dropped (party state 5 -> 0); a room that still comes is left"`,
  `"join: given up with the id still waiting: id dropped, party state "`,
  `"join: id set by the game itself (its own 0x10c step or a 0x116)"`, `"join: id taken"`,
  `"join: id NOT taken in 5 s"`, `" for 5 s with an id waiting: forced on"`,
  `"join: a party room came after its join was dropped: left"`,
  `"join: gave up after 20 s (PS3 timeout): "`, `"no 0x10c answer"`,
  `"the party room was not joined (no 305 for the party session)"`, `"the friend is not in the room we got"`,
  `"the peer link to the party leader did not come up (see the peer link lines of both PCs)"`,
  `"join: waiting for the party room (Join 304 sent)"`, `"join: in a party room: members "`, `", we lead it "`,
  `", the friend is in it "`, `"join: the room we got does not hold "`,
  `": leaving it, asking for a fresh party (0x10b again)"`, `"join: failed (again not in the friend's room)"`,
  `"join: failed (the server refused the party room)"`, `"join: peer link to "`, `": up"`, `": not up yet"`,
  `": no connection entry"`,
  `"join: our MATCH session got a room with the friend while the party session waits (see the 305 lines)"`,
  `"join: leaving the party room we got (nobody stays half joined)"`, `"join: answer from "`, `": accepted"`,
  `": refused"`, `"join: failed (refused)"`, `"join: in the party lobby"`.

## 4. Behaviour, part A

"State" = the u32 at SES +0x1338; "party id" = the u64 at SES +0x18; "clock" = the s32 at image 0x17b4ba4.

### 4.1 `u3x_join_party(base, s, callback)`: Join Party on the selected player

T = the address of image 0x17c3871 (the selected player's id).

1. A 0x190-byte presence buffer is cleared to zeros and `u3h_presence(base, T, buffer)` is asked.
2. When the result is not 0 (no presence): `u3x_send_join(base, T, 0, callback)`; nothing else.
3. When it is 0, the presence data is taken at buffer +0x100 (G18 3.6), without looking at its size or version
   word:
   - its byte +7 (party locked) non-zero: the locked dialog
     `u3h_show_dialog(base, s, 0x4d972420, 0xc6467f2d, 0x52a9f855, 0, 0, 0)`, then
     `u3h_fj_note("join: ", T, " party locked (presence): not asked", -1)`; nothing else (no request, no popup);
   - otherwise P = its u64 at +0x28 (the friend's party id). When P is not 0 and we are in exactly that party (the
     u8 at SES +0xc8 is non-zero and the party id equals P): nothing happens at all;
   - otherwise `u3x_send_join(base, T, P, callback)`.

### 4.2 `u3x_message_events(base)`: the received messages of this tick

At most 64 messages are taken per call. For each: an id buffer of 0x24 zero bytes, a type variable set to 0 and a
data buffer of 0x200 bytes are given to `n = u3h_msg_event(base, id, &type, data, 0x1fc)`.

- n negative: the function ends (no more messages).
- n greater than 0x1fc counts as 0x1fc.
- type 0x10b with n >= 9: `u3h_join_host_answer(base, id, data, n)`; a non-zero result ends this message.
- type 0x1f0 or 0x1f1: `u3h_join_msg_in(base, id, type, data, n)`; a non-zero result ends this message.
- every message not ended that way (also a 0x10b shorter than 9 bytes, and one the two functions answered 0 for):
  `u3x_queue_post(base, id, type, data, n)`; result ignored.

The data buffer: the old code does not clear it, so the bytes behind the n received are undefined there (stack
contents, in practice left-overs of earlier messages). The new code clears the whole buffer to zeros before every
`u3h_msg_event` call. This matters only to `u3x_join_hold`, which reads 0x10 bytes of a request that may be
shorter (open question 2).

### 4.3 `u3x_join_hold(base, id, d)` (stray): hold a request

1. The held entries are searched in order for one whose 0x24 id bytes all equal the 0x24 bytes at `id` (a plain
   byte comparison of all 0x24 bytes, not a text comparison).
2. None found: when 4 entries are held, nothing happens at all. Otherwise a new entry is appended: its id = the
   0x24 bytes, its "held since" = the clock.
3. The entry found or appended gets: flag = the byte `d`[8]; request = the 0x10 bytes at `d`. (A sender that is
   already held keeps its place and its "held since"; only flag and request are replaced.)

### 4.4 `u3x_join_hold_tick(base)`: decide the held requests

The state and the clock ("now") are read once, at the start. The entries are examined in order; an entry that is
decided is removed (the later ones move one place down, keeping their order; the count goes down) and the entry
now at that place is examined next; an entry that waits stays.

For one entry (id, flag, since, request): "helper request" = request byte +9 is 0x4a; "fresh" = helper request and
request byte +10 is 2. The decision is the first of these that applies, tested in this order:

1. `u3h_join_locked(base)` non-zero: **refuse as locked**.
2. fresh, and the state is 6, and now - since <= 1500: **wait**.
3. `u3h_join_can_accept(SES, state)` non-zero: **accept**.
4. the state is 0: **hand to the game**.
5. now - since > 1500: **refuse as too late**.
6. otherwise: **wait**.

(`u3h_join_locked` is asked for every entry examined; `u3h_join_can_accept` only when tests 1 and 2 did not apply.)

Then, in this order:

- **accept**: when it is a helper request and `u3h_join_party_reply(base)` is not 0:
  `u3h_join_info_send(base, id)`. Then `u3h_join_answer(base, id, u3h_join_party_reply(base), flag)` (the party id
  is asked again for this call) and `u3h_fj_note("held join request from ", id, " accepted", -1)`.
- **hand to the game**: the u8 at `u3h_join_request_pc()` = 1 for a helper request, else 0; then
  `u3x_queue_post(base, id, 0x10b, request, 0x10)` (result ignored) and
  `u3h_fj_note("held join request from ", id, " to the game (state 0)", -1)`.
- **refuse as locked**: `u3h_join_answer(base, id, 0, flag)`, then
  `u3h_fj_note("held join request from ", id, " refused: locked", -1)`.
- **refuse as too late**: `u3h_join_answer(base, id, 0, flag)`, then
  `u3h_fj_note("held join request from ", id, " refused after 5 s, state ", state)`.
- **wait**: nothing.

The place freed at the end of the queue keeps its old bytes (not observable).

### 4.5 `u3x_join_reply_tick(base)`: the late answer

1. When at least one request is held: `u3x_join_hold_tick(base)`.
2. R = the buffer `u3h_join_request()`, ON = the u8 at `u3h_join_request_on()`.
3. When the u8 at SES +0x1348 is non-zero (the game is creating a party for a request): the 0x24 bytes at SES
   +0x1349 are copied to R, ON = 1; nothing else.
4. Otherwise, when ON is 0: nothing.
5. Otherwise ON = 0 (the game has finished that step). Nothing more when the state is not 6, or the party id is 0,
   or the first byte of R is 0.
6. Otherwise: PC = the u8 at `u3h_join_request_pc()`; when it is non-zero it becomes 0 and
   `u3h_join_info_send(base, R)` is called. Then `u3x_queue_send(base, R, 0x10c, m, 0x10)` with m = 0x10 bytes:
   the party id at +0, everything else 0 (result ignored), and
   `u3h_fj_note("join request from ", R, " answered: new party up", -1)`.

## 5. Behaviour, part B

"State", "party id", "clock" as in section 4; "waiting id" = the u64 at SES +0x1340; JS fields by their names of
3.2; "pending" = the four pending fields of JS. "Note(t)" = `u3h_so_note(t, null, -1, null)`. "The party step" =
game function 0x77d270 (SES). "The failed dialog" =
`u3h_show_dialog(base, s, 0x4d972420, 0xc6467f2d, 0xa8cda6e0, 0, 0, 0xe1e040aa)`.

### 5.1 `u3x_fj_member(ps, name)` (stray)

The ten member slots of the session `ps` are examined in slot order; a slot not in use (byte +0x25 is 0) is
skipped. A slot matches when its name equals `name` as texts of at most 16 characters: the bytes 0 .. 15 are
compared in order; a difference means no match; a NUL reached in the slot's name (equal in both) ends the
comparison as a match; 16 equal bytes are a match. Result: the first matching slot, or null.

### 5.2 `u3x_fj_fail(base, s, why)` (stray)

In this order: ea[110] incremented; the failed dialog; Note(`why`); JS waiting = 0.

### 5.3 `u3x_pj_line(base, what)` (stray): the party state line

One log line is built and given to Note. The builder of this line has its own limits (not those of the `Line` of
CONVENTIONS.h; they are reached in practice, see scenario 13): a text is appended character by character, each
only while the line holds fewer than 196 characters; a decimal number is written without leading zeros, each
digit only while the line holds fewer than 196 characters; a hex word is left out completely when the line already
holds more than 186 characters, and is otherwise written in full as `0x` and 8 lower-case hex digits.

The line, in this order: `what`; `": party state "`; the state in decimal; `", room "`; the low 32 bits of the
party id as a hex word; `", pending id "`; the low 32 bits of the waiting id as a hex word;
`", join done/result/joined "`; the u8 at SES +0xb8 in decimal; `"/"`; the s32 at SES +0xbc in decimal, as `"-"`
and its magnitude when negative (the magnitude as an unsigned 32-bit value: -2147483648 prints 2147483648); `"/"`;
1 when the u64 at SES +0xc0 is not 0, else 0; `", create done "`; the u8 at SES +0xa1; `", leave done/asked "`; the
u8 at SES +0xc8; `"/"`; the u8 at SES +0x1334; `", net error "`; the u8 at image 0x17d5c80; `", we lead "`;
`u3x_party_leader(base)` (asked last, after every field was read).

### 5.4 `u3x_pj_accept(base, id, flag)`: hand a room id to the party machine

1. `u3x_pj_line(base, "join: 0x10c in")`.
2. When the state is 5 and "join done" (SES +0xb8) is non-zero: the party step (a join that has just finished is
   taken in first).
3. The state is read (again). When it is 5, 3, 4 or greater than 7: the state becomes 1 when it was 5, else 0, and
   `u3h_so_note("join: party state ", null, old state, t)` with t =
   `" at the answer: an earlier join was never answered; dropped, the new id is joined now"` when it was 5, else
   `" at the answer: a state without code; reset to 0"`.
4. The waiting id = `id`; "join done" = 0; the u8 at SES +0x1374 = 1 when `flag` is not 0, else 0.
5. Pending: id = `id`, since = the clock (read now), phase = 1, dropped = 0.
6. The party step.
7. `u3x_pj_line(base, "join: after the first step")`.

### 5.5 `u3x_pj_tick(base)`: watch the waiting room id

Nothing when SES has no backend (pointer at SES +8 null) or "multiplayer active" (image 0x17d5c61) is 0. The
waiting id W, the state and the clock ("now") are read once, at the start.

When W is 0:

1. When pending phase is not 0: it becomes 0 and `u3x_pj_line(base, "join: id taken")`.
2. When pending dropped is set and the state is not 0: dropped = 0 (a party is being made or joined in the normal
   way).
3. Otherwise, when pending dropped is set and the party id is not 0 (the abandoned join's room came after all):
   dropped = 0; Note(`"join: a party room came after its join was dropped: left"`); then **the leave call**: the
   function whose address is the u64 stored at backend + 0x38 is called with (backend, SES); result ignored. (The
   backend pointer was read at the start. See oddity 1: keep this call in one small private function.)

When W is not 0:

4. When pending phase is 0 or W differs from pending id (the game put an id there itself): pending id = W, since =
   now, phase = 1, dropped = 0; `u3x_pj_line(base, "join: id set by the game itself (its own 0x10c step or a
   0x116)")`; nothing else.
5. Nothing when pending phase is not 1, or now - pending since < 1500.
6. Otherwise (the id has waited 5 s): phase = 2; `u3x_pj_line(base, "join: id NOT taken in 5 s")`; then by the
   state read at the start:
   - 5: state = 1 (the join was never answered: the new id goes out);
   - 2: "create done" (SES +0xa1) = 1 and state = 1;
   - 7: "leave done" (SES +0xc8) = 1;
   - 3, 4 or greater than 7: state = 0;
   - 0, 1, 6: nothing more (no further line).
   In the four forcing cases a last line follows:
   `u3h_so_note("join: party state ", null, the state read at the start, " for 5 s with an id waiting: forced on")`.

### 5.6 `u3x_pj_giveup(base)`: give the join up

1. When the waiting id is not 0: it becomes 0 and
   `u3h_so_note("join: given up with the id still waiting: id dropped, party state ", null, state, null)`.
2. When the state is 5 and "join done" (SES +0xb8) is 0 (a join that was never answered): "join done" = 1; join
   result (s32 at SES +0xbc) = 0; "joined" (u64 at SES +0xc0) = 0; the state = 1 when the LAN flag (image
   0x17d5c60) is non-zero, else 0; pending dropped = 1; Note(`"join: the party room join was never answered:
   dropped (party state 5 -> 0); a room that still comes is left"`).
3. Pending phase = 0 (in every case).

### 5.7 `u3x_fj_cb(base, s, ev)`: the "Joining Party..." popup

E = the s32 at `ev` +0; "now" = the clock, read once at the start.

**E = 0** (the popup opens): answered = 0; seen = 0; timeout = now + 6000. Nothing else.

**Any E other than 0 and 6**: nothing.

**E = 6** (every frame while the popup is up):

1. The room status RS of the party session: "pending" when "join done" (SES +0xb8) is 0; else "refused" when the
   join result (s32 at SES +0xbc) is negative; else "joined" when the u64 at SES +0xc0 is not 0; else "none".
2. F = `u3x_fj_member(SES, JS from)` when answered is set, else null (the friend's member slot).
3. **Timeout**, when now > timeout (a plain signed comparison of the two values): see 5.8; nothing else.
4. **Not answered yet**: see 5.9; nothing else.
5. RS = refused: `u3x_fj_fail(base, s, "join: failed (the server refused the party room)")`; nothing else.
6. RS = pending or none (still joining):
   - when seen bit 0 is clear: it is set, noted at = now, Note(`"join: waiting for the party room (Join 304
     sent)"`);
   - when seen bit 2 is clear and now - noted at > 600: if the match session (image 0x17d6420) has a room (its u64
     at +0x18 is not 0) and `u3x_fj_member(match session, JS from)` is not null: seen bit 2 is set and
     Note(`"join: our MATCH session got a room with the friend while the party session waits (see the 305
     lines)"`);
   - nothing else.
7. RS = joined. When seen bit 1 is clear: it is set, noted at = now, and one line is logged with Note:
   `"join: in a party room: members "`, the number of member slots in use (of the ten) in decimal,
   `", we lead it "`, 1 when this machine leads the party session, else 0, `", the friend is in it "`, 1 when F is
   not null, else 0. (No length limit can be reached by this line.)
8. F null (not the friend's room):
   - nothing when now - noted at < 300 (one second for a member that is announced late);
   - "leave asked" (SES +0x1334) = 1;
   - when retried is set: `u3x_fj_fail(base, s, "join: failed (again not in the friend's room)")`; nothing else;
   - otherwise retried = 1; `u3h_fj_note("join: the room we got does not hold ", JS from, ": leaving it, asking
     for a fresh party (0x10b again)", -1)`; `u3h_mq_send(base, to, 0x10b, m, 0x10)` with to = 0x24 bytes: the
     0x14 bytes of JS from, then zeros, and m = 0x10 zero bytes except 0x4a at +9 and 2 at +10 (result ignored);
     answered = 0; seen = 0; timeout = now + 6000; wait since = now. Nothing else.
9. F not null: the leader's slot L = F; but when this machine does not lead the party session: the first slot in
   use (slot order) whose member id (+0x2c) equals the host's member id (SES +0x12e8), when there is one.
   Then C = `u3h_gt_entry_by_id(base, the s32 at L +0x34)` and the link value K =
   `u3h_gt_conn_state(base, the s32 at C +0)` when C is not null, else -9.
   When the low byte of K + 16 differs from last link: last link = that byte, and
   `u3h_fj_note("join: peer link to ", L, t, -1)` with t = `": up"` when K is 2, else `": not up yet"` when C is
   not null, else `": no connection entry"`.
10. K not 2: nothing else (keep waiting).
11. K = 2 (done): `u3h_show(base, s, 0x53cbb5f2, 0)`; `u3h_show(base, s, 0xb99a3235, 0)`; ea[109] incremented;
    Note(`"join: in the party lobby"`); waiting = 0; and when hint ok is not 0 and bit 0 of hint flags is set:
    direct since = now, or 1 when now is 0.

### 5.8 The timeout step (5.7 step 3)

In this order:

1. Game function 0x652200 (menu, 0): the popup closes.
2. One line with Note: `"join: gave up after 20 s (PS3 timeout): "` followed by the reason: `"no 0x10c answer"`
   when answered is 0; else `"the party room was not joined (no 305 for the party session)"` when RS is not
   joined; else `"the friend is not in the room we got"` when F is null; else `"the peer link to the party leader
   did not come up (see the peer link lines of both PCs)"`. (No length limit can be reached.)
3. waiting = 0.
4. `u3x_pj_giveup(base)`.
5. When answered is set, RS is joined, the party id is not 0 and this machine does not lead the party session (a
   room was joined but the link never came up): "leave asked" (SES +0x1334) = 1; Note(`"join: leaving the party
   room we got (nobody stays half joined)"`); the failed dialog. (The party id and the two member ids are read
   after step 4.)

### 5.9 The answer step (5.7 step 4)

1. `u3h_mq_find(base, 0x10c, &index, 0)`; a non-zero result (no answer in the queue yet) ends the step.
2. `u3h_mq_get_np(base, index, sender, data, &length)` with a 0x24-byte sender buffer, a data buffer of 0x10 zero
   bytes and length = 0x10; a non-zero result ends the step.
3. R = the u64 at data +0. `u3h_fj_note("join: answer from ", sender, t, -1)` with t = `": accepted"` when R is not
   0, else `": refused"`.
4. R = 0: `u3x_fj_fail(base, s, "join: failed (refused)")`; the step ends (answered stays 0).
5. When `u3x_party_leader(base)` is non-zero (our own party follows us): n = `u3h_members(SES, list)` with a list
   of 10 pointers. "Me" = the first member slot in use (slot order) whose member id (+0x2c) equals our own member
   id (SES +0x12e4), looked for only while the party id is not 0. When Me exists: for k = 0 .. n-1 in order, every
   listed member whose member index (the s32 at +0x28) differs from Me's gets
   `u3h_mq_send(base, that member slot (its name is the id), 0x116, &R, 8)` (the 8 bytes of R; result ignored).
6. answered = 1; seen = 0; JS from = the first 0x14 bytes of the sender buffer.
7. `u3x_pj_accept(base, R, the data byte +8)`; then ea[108] incremented.

## 6. Test scenarios

Everything can be tested old against new on a fake image (SES and the match session with member slots, the
selected player's id, the clock, the three net bytes, a fake backend object) with scripted or recording `u3h_`
exports and `u3x_` functions of 2.2 and recording stubs for game functions 0x652200, 0x77d270 and the function
whose address is stored at backend + 0x38. The stub of 0x77d270 should be able to change SES (the state, "join
done", the waiting id) so that the "read again" points of 5.4 show. Compare memory (image, JS, the three C
variables of part A, ea words), and the order and arguments of all calls. Nothing needs the live game; the live
check is a real join between two machines (and one where the host is in state 6 alone, for the fresh-party
path). Part A's hold queue starts empty: each scenario runs from a fresh start. Pure reads of 2.2 are not compared
by number of calls, except where a step gives it.

Part A

1. Join Party: presence fails (request with party 0); presence with the locked byte set (locked dialog with close
   label 0, the note, no request); party id 0 in the presence (request with 0); party id set and we are in another
   party / in no party (SES +0xc8 zero with the same id) (request with that id); we are in exactly that party
   (nothing at all); the presence buffer is zero before the call (a stub that writes nothing and answers 0: request
   with party 0).
2. Message pump: no message (one call of `u3h_msg_event`, nothing else); three messages then none; 64 messages
   waiting (exactly 64 taken); a 0x10b of 8 bytes (queued, no host decision); a 0x10b of 9 bytes with the host
   stub answering 1 (not queued) and 0 (queued with its length); 0x1f0 / 0x1f1 with the stub answering 1 and 0; type
   0x10c (queued); a reported length of 0x300 (handled as 0x1fc; the post is called with 0x1fc); id buffer and
   type reset for every message (a stub that writes neither).
3. Hold: first request (entry with clock); the same sender again later (flag and request replaced, "held since"
   kept); a sender whose id differs only in byte 0x23 (a second entry); a fifth sender (ignored); visible through
   the hold tick's calls.
4. Hold tick, one entry each: locked (answer 0 with the flag, the "locked" note; the lock wins over everything);
   accept of a console request (answer with the party id, no info message); accept of a helper request (info
   message first, then the answer); accept of a helper request while the party reply is 0 (no info message,
   answer 0, still the "accepted" note); state 0 (the C flag set to 1 / 0 by the request's marker, the post with
   the held 0x10 bytes, the note); too late at 1501 ticks, not at 1500 (note with the state number); a fresh
   request in state 6 at 1500 ticks (waits although it could be accepted) and at 1501 (accepted); a fresh request
   in state 0 (to the game at once); waiting in state 2 before 5 s.
5. Hold tick, several entries: four entries of which the second and fourth are decided (order of the calls, the
   remaining two keep their order and their own flag, clock and request: shown by a later tick); all four decided
   in one tick; the clock wrapping between hold and tick.
6. Reply tick: nothing held and nothing pending (no call but the accessors); SES +0x1348 set (id copied, ON 1, no
   send; repeated ticks the same); then cleared with state 6 and a party id: console requester (send only) and
   helper requester (info message, PC flag cleared, then the send; message bytes); cleared with state 2, with
   party id 0, with an empty id (ON cleared, nothing sent, PC flag left as it was); hold tick called first when
   something is held and not called when nothing is.

Part B

7. Member by name: found in slot 0 / slot 9; a slot with the same name but not in use (skipped); two slots with
   the name (the first); names equal in 16 characters and different in the 17th (match); a shorter slot name that
   is a prefix of `name` (no match) and the reverse (no match); no member.
8. Fail: counter, dialog arguments, the note, waiting cleared; order.
9. Accept: state 0 (no reset; fields written; pending; one party step between the two lines); state 5 with "join
   done" set and a step stub that moves the state to 6 (first step called; no reset) or leaves it at 5 (reset to 1
   with the first text); state 5 with "join done" clear (no first step, reset to 1); state 3, 4, 8 (reset to 0, the
   second text); state 7 (nothing reset); flag 0, 1 and 0x80 (SES +0x1374 = 0, 1, 1); the pending clock.
10. Give up: waiting id set (cleared, the note with the state); state 5 with "join done" clear, online and LAN
    (fields, state 0 / 1, dropped, the note); state 5 with "join done" set (only phase 0); nothing to do (only
    phase 0).
11. Tick, gates: no backend; "multiplayer active" 0 (nothing in either case, JS untouched).
12. Tick, no id waiting: phase 1 (phase 0 and the "taken" line); dropped with state 1 (cleared silently); dropped
    with state 0 and no room (stays set); dropped with state 0 and a room (cleared, the note, the call of the
    function stored at backend + 0x38 with (backend, SES)); phase 1 and dropped with a room in the same tick (both).
13. Tick, an id waiting: phase 0 (pending taken over, the "set by the game" line: with one-digit numbers this
    line is longer than 196 characters, so it must end inside `", net error "`; compare the text exactly); another
    id than pending (the same); phase 1 before 1500 ticks (nothing), at 1500 with state 5 / 2 / 7 / 3 / 9 (phase 2,
    the "NOT taken" line, the forced field, the "forced on" line) and with state 0 / 1 / 6 (phase 2 and the line
    only); phase 2 (nothing ever again until the id changes).
14. State line: every field non-zero; join result -1 and -2147483648; a `what` of 150 characters (hex words left
    out or kept by the 186 rule, texts cut at 196, digits dropped at 196).
15. Popup, E = 0 (three fields; retried, last link and waiting untouched); E = 3 (nothing).
16. Popup, answer step: no 0x10c in the queue; the take failing (stub -1); answer 0 (the "refused" note, fail,
    answered still 0); answer with an id while we lead a party of three (two 0x116 messages to the others, not to
    ourselves, order as listed), while we lead without a room (no member search, no message), while we are a
    member of somebody's party (no message); answered, seen, the 0x14 name bytes, the accept call with the
    answer's flag byte, ea[108] after it.
17. Popup, waiting for the room: first frame (bit 0, noted at, the note); later frames silent; after more than
    600 ticks with the match session holding the friend (bit 2 and the note once), with a match room without the
    friend, without a match room; join refused (fail with the "server refused" text).
18. Popup, in a room: first frame (bit 1, the members line with all three numbers); friend missing before 300
    ticks (nothing), at 300 with retried 0 (leave asked, the note, the 0x10b bytes and the padded id, the four
    fields reset, noted at kept) and with retried 1 (leave asked, fail); friend present and we lead (leader = the
    friend) or not (leader = the host's slot; no slot with the host's id: the friend); no connection entry (K = -9,
    one "no connection entry" line, not repeated next frame); entry not linked (one "not up yet" line); linked (the
    "up" line, both pages in order, ea[109], the note, waiting 0); direct since with hint ok and bit 0 at clock
    5000 and at clock 0 (5000 / 1), without hint (unchanged).
19. Popup, timeout: now = timeout (not yet), now = timeout + 1 with each of the four reasons; the order popup
    close, line, waiting, give-up; the half-joined case (leave asked, the note, the dialog) and its three
    negatives (we lead; no room id; not answered).

## 7. Oddities of the old code that are behaviour to keep

1. **The leave call of 5.5 step 3 reads the function address from backend + 0x38.** The C body this was moved from
   (v7.210) called entry 7 (Leave, `G14_online_backend_out.md` 5.5) of the backend's function table, i.e. the
   address stored at (the pointer at backend +0) + 0x38. v7.212 lost that one indirection: for the online backend
   the u64 at +0x38 is its second session binding (a data pointer or null, G13 3.4), so in the rare case this step
   is reached the shipped helper jumps to a data address. The spec describes v7.212 as it is; see open question 1.
2. Join Party trusts the presence data without checking its size or version word (a friend in another title gives
   whatever bytes are there), and does nothing at all, without a message, when we are already in that party.
3. The hold queue compares all 0x24 id bytes, so the same name with different trailing bytes is a second entry; a
   fifth sender is dropped without an answer.
4. The hold tick takes "sent by this helper" from request byte +9 without looking at the request's length (the C
   decision that held it required at least 11 bytes for that), and reads the state and the clock once for all
   entries.
5. A held helper request that is accepted while the party reply is 0 (net menu mode 1) is answered with 0 and
   still logged as accepted.
6. A held request handed to the game is removed from the hold queue even when the game's queue was full (it is
   lost); the same holds for every message in 4.2 (the post's result is ignored; a message of 0x1fc bytes or more
   is never queued).
7. The late answer forgets a served request without answering when the party is not up in state 6 at that tick,
   and then leaves the "requester runs this helper" flag as it was.
8. Member names are compared over 16 characters only, although ids have up to 0x24 bytes and JS from keeps 0x14.
9. The state line has its own limits (196 / 186) and is really cut for the longest `what`.
10. The room id waiting at SES +0x1340 is watched by phase: after the one report at 5 s nothing more happens until
    the id changes or is taken; states 0, 1 and 6 are reported but not forced.
11. Give-up sets the state to 1 on LAN and 0 online, although its log text says "5 -> 0".
12. The popup's timeout test is a plain comparison of clock values (no wrap handling), and E = 0 does not reset
    retried, last link or waiting (the request function did that before the popup opened).
13. A refused answer (id 0) shows the failed dialog but leaves "answered" at 0 and the timeout running: as long as
    the handler keeps getting event 6, a later answer is still taken, and the timeout step still runs (popup
    close, line, give-up).
14. An answer longer than 0x10 bytes can never be taken (the game function refuses it and leaves it in the
    queue): the popup then runs into the timeout with "no 0x10c answer".
15. The 0x116 messages go out before the room id is handed to our own party machine, and only when we have a
    party room; "our own slot" is found by member id, the others are told apart by member index.
16. The retry clears "answered" but not JS from, and restarts both the 20 s and the watchdog's time.
17. After the timeout a half-joined room is left only when we do not lead it.
18. `u3x_pj_line`, `u3x_pj_accept`, `u3x_fj_member`, `u3x_fj_fail` have C wrappers without callers in v7.212, and
    `u3x_join_hold_tick` is exported although only `u3x_join_reply_tick` calls it.

## 8. Strays, tables, and what stays as it is

Strays added to this group (in no work group of the inventory; they hold the rest of the same logic):

- `u3x_join_hold` with the hold queue's layout and its id comparison (part A);
- `u3x_fj_member`, `u3x_fj_fail`, `u3x_pj_line` with their file-private bodies (old names `member_named`,
  `join_failed`, `party_line`), the file-private line builder with its limits, and the small helpers of the state
  machine (`start_pending`, `party_leads`, `note`, the C-layout structs `JoinState`, `PendingJoin`, `FollowHint`)
  (part B).

Stays as it is:

- `u3x_send_join` (G20 4.4), `u3x_queue_post`, `u3x_queue_send`, `u3x_queue_find`, `u3x_party_leader`,
  `u3x_session_members`, `u3x_entry_by_id`, `u3x_connection_state` (existing C++ functions outside this group).
- The C part: the wrappers, `fi_kind`, `fr_tick`, the join state block and the three request variables, the host's
  decision (`u3h_join_host_answer`), the 0x1f0 / 0x1f1 messages, the 25 s watchdog, the "follow the host into the
  match" step, and all `u3h_` exports of 2.2.
- Nothing of this group has to stay in C; there is no float code.

Tables: the group holds no table of game values. The ids of 3.7 are single text / action / page ids; the message
types and timings are single values.

## 9. Open questions

1. **The leave call (oddity 1).** Should the new code keep v7.212's behaviour (address read from backend + 0x38,
   which cannot work) or restore v7.210's (entry 7 of the backend's function table, called with (backend, SES))?
   The pipeline rule says "100% of the old code", so the spec gives v7.212; the owner should decide. The tester's
   fake backend can serve both: a function table whose entry 7 and a u64 at +0x38 point to two different recording
   stubs.
2. The data buffer of 4.2 is uninitialised in the old code; the spec makes it zero before every message. For a
   0x10b request of 9 to 15 bytes this decides what the hold queue stores behind the received bytes (and so
   whether a console's short request can look like a helper request, oddity 4). The old-versus-new test cannot
   compare that case bit for bit; requests of 0x10 bytes compare exactly.
3. The party state numbers (3.3) are given as the helper's notes know them; states 3, 4 and above 7 "have no code"
   by those notes. Nothing here depends on more than the numbers.
4. `u3x_queue_post` is in no work group and in none of the inventory's lists (an exported C++ function of v7.144,
   not file-private, so not taken as a stray here). If the final sweep finds it to be old port, it needs its own
   description; 2.2 gives its effects as this group relies on them.

## Clarifications

Added by the tester (2026-11-14). Facts only, confirmed by the old-versus-new test (`reimpl2\G21\test_g21.cpp`),
where the text above is silent or can be read two ways. Every statement of sections 4 to 6 that the test checks
against v7.212 holds; nothing above had to be corrected. "Read point" facts (items 3 to 7) can only be seen with a
stand-in that changes the value inside the call named; the real functions behind those calls do not change it.

1. Open question 1 (the leave call of 5.5 step 3). Decided by the project: the new code calls entry 7 (Leave) of the
   backend's function table, i.e. the function whose address is stored at (the pointer at backend +0) + 0x38, with
   (backend, SES). v7.212 calls the address stored at backend + 0x38 itself. With a fake backend that has both,
   v7.212 always takes the second and never the first; the arguments, the place of the call in the sequence (after
   the "left" note, with "dropped" already 0) and everything else of that tick are the same.
2. Open question 2 (the data buffer of 4.2). v7.212 really keeps the bytes of an earlier message of the same call
   behind a shorter one: a 9-byte request that is held after a 16-byte helper request (marker 0x4a, version 2) is
   stored with that request's bytes +9 .. +15, and the hold tick treats it as a helper request (in state 0 it sets
   the "requester runs this helper" flag to 1). With the buffer cleared before every message it is stored with
   zeros there and is a console's request (flag 0). Requests of 11 bytes or more are decided the same way in both.
3. 4.5 step 6, the party id of the late answer. v7.212 reads the party id (SES +0x18) for the test of step 5 and
   reads it again for the message, after the `u3h_join_info_send` call. One read before that call gives the same
   message unless that call changes the party id.
4. 5.5, read points. The backend pointer, the waiting id, the state and the clock are read at the start (the clock
   is not touched when one of the two gates ends the call). Pending "dropped" and the party id (SES +0x18) of steps
   2 and 3 are read after the "id taken" line of step 1 was logged: a room id or a "dropped" that appears during
   that log call is acted on in the same tick, a state that changes during it is not. The leave call uses the
   backend pointer read at the start, also when the session's pointer changed since.
5. 5.8, "answered" in the timeout step. v7.212 reads "answered" for the reason text after game function 0x652200
   has returned, and reads it again for step 5 after `u3x_pj_giveup` has returned. RS and F are the values from
   before the step. One read at the start gives the same result unless the popup-close call, a log call or the
   give-up changes "answered" (none of them writes it: only `u3x_fj_cb` does).
6. 5.9 step 5, our own member index. v7.212 reads Me's member index (the s32 at Me +0x28) again for every listed
   member, i.e. after the `u3h_mq_send` of the member before. One read before the first message gives the same
   messages unless a send changes Me's slot.
7. 5.4, the clock of "pending since": read at step 5, i.e. after the first party step (when there is one) and
   before the second.
8. 5.9 step 5, the member list. `u3h_members(SES, list)` is called whenever `u3x_party_leader(base)` answered
   non-zero, also when the party id is 0 (we lead without a room). In that case nobody is told, whatever the list
   holds. Members are told in the list's order; a slot listed twice is told twice.
9. 5.9 step 2. v7.212 does not clear the sender buffer and does not preset the index (game function 0x776530
   writes all 0x24 id bytes whenever it answers 0, and the find function writes the index whenever it answers 0);
   clearing them first changes nothing. An answer entry without data leaves the data buffer zero and so counts as a
   refusal.
10. 4.2. The length handed to `u3h_join_host_answer`, `u3h_join_msg_in` and `u3x_queue_post` is the limited one (at
    most 0x1fc). Any negative result of `u3h_msg_event` ends the function, not only -1. The id buffer is zero and
    the type is 0 before every `u3h_msg_event` call, also after a message whose stand-in wrote neither.
11. 4.4. On an accept `u3h_join_party_reply` is asked once for a console's request (for the answer) and twice for
    a helper request (before the info message and again for the answer; the answer carries the second value). The
    "requester runs this helper" flag is written before `u3x_queue_post` is called. The marker test is byte +9 =
    0x4a exactly; "fresh" needs the marker.
12. 5.7. For an event other than 0 and 6 nothing is written and nothing is called. Whether the clock is read for
    such an event cannot be told from outside; both versions as compiled for the host (-O1, -O2) do read it at
    the start, before the event number is looked at.
13. 5.7 step 9 to 11. Only K = 2 ends the join: a K whose low byte of K + 16 equals that of 2 + 16 (258) logs no
    new link line when "last link" is 18 and keeps waiting. "Hint ok" counts when it is non-zero (any value);
    of the hint flags only bit 0 is looked at.
14. The number of calls of `u3h_join_state`, `u3h_ext` and the three request accessors is not behaviour (v7.212
    calls `u3h_join_state` more often than needed, e.g. once in every state line).
