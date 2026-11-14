# G14. Online backend: room messages out (slots 0, 2 to 16)

Behaviour description for a reimplementation (reimpl2 pipeline, describer's document). It says what the functions
do, not how the old ones were written. Offsets are hexadecimal when prefixed with 0x, decimal otherwise. "Image
offset X" means the address `base + X`, where `base` is the game image base. All multi-byte values are little-endian;
"u8 / u16 / s16 / u32 / s32 / u64 / s64" are unsigned / signed integers of that width, "pointer" is 8 bytes. Only
`u3x_onb_geo` (4.9) touches floating point (two conversions to integer, exact).

Read with `G13_online_backend_in.md` (the same object seen from the receiving side). Its names are kept: the online
backend object and its bindings (G13 3.4), the session fields (G13 3.5), the cookie rule, the eb[] words, the
server-to-client messages (G13 3.9), the ping clock `u3x_onb_ping_restart` / `u3x_onb_ping_due` (G13 5.15), the
Update slot `u3x_onb_s1` and the table fill `u3x_onb_vtable`. Member slots and the member description are in
`G07_lan_sockets.md` (3.6, 3.7), the connection table in G07 3.5 and `G12_packets_monitor.md` 3.4, the LAN twin of
these slots in `G10_lan_backend_slots.md`. What this group needs of them is restated in section 3 in compact form.

The group is written as **three parts**, so that three implementers can work at the same time:

| Part | Subject | Exported names to deliver |
|---|---|---|
| 1 | the shared helpers of the sending slots, the receive append, Init / Shutdown / AttachSession | `u3x_onb_obj`, `u3x_onb_strcpy`, `u3x_onb_note`, `u3x_onb_bound`, `u3x_onb_cookie`, `u3x_onb_send`, `u3x_onb_wire`, `u3x_onb_rx`, `u3x_onb_name_time`, `u3x_onb_geo`, `u3x_onb_s0`, `u3x_onb_s2`, `u3x_onb_s3` |
| 2 | the life of a room: Create, Search, Join, Leave, Kick | `u3x_onb_s4`, `u3x_onb_s5`, `u3x_onb_s6`, `u3x_onb_s7`, `u3x_onb_s9` |
| 3 | room settings and member data: attribute, Promote, room flags / match start, member data, host rank, room name | `u3x_onb_s8`, `u3x_onb_s10`, `u3x_onb_s11`, `u3x_onb_md_stamp`, `u3x_onb_s12`, `u3x_fl_md`, `u3x_onb_s13`, `u3x_onb_s14`, `u3x_onb_s15`, `u3x_onb_s16` |

The split: parts 2 and 3 call part 1 only through eight of its exported functions (`u3x_onb_note`,
`u3x_onb_bound`, `u3x_onb_cookie`, `u3x_onb_send`, `u3x_onb_wire`, `u3x_onb_name_time`, `u3x_onb_geo`,
`u3x_onb_strcpy`), whose contracts are complete in 2.1 and section 4; part 1 calls nothing of parts 2 and 3; parts 2
and 3 do not call each other. Sections 1 to 3 are common, section 4 is part 1, section 5 part 2, section 6 part 3;
the test scenarios (7) are given per part.

Of these names the inventory lists for G14: `u3x_onb_rx`, `u3x_onb_name_time`, `u3x_onb_s4` to `u3x_onb_s14` and
the internal "send the room name" routine (6.9). All the others are strays (2.5).

## 1. Purpose and callers

The game's session code talks to a "session manager" object through a table of function pointers ("slots"). For
online play the helper supplies that object ("the online backend object", G13 section 1). Each sending slot turns
one request of the game into one room message for the session service and sets the session's "done" and result
fields the game polls; the answers come back as messages handled by the Update slot (G13 part 2). A message
travels over the session socket while that is in use, otherwise as a frame of the community-server link
(`u3x_onb_send`, 4.5).

Everything of this group runs on the game's main thread. Nothing is locked. No function blocks. Helper state that
other threads change is only reached through the `u3h_` functions of 2.4.

**Who calls the slots.** Through the object's table (filled by `u3x_onb_vtable`, which stays as it is), always with
the object as the first argument. None of the slots of this group has a C wrapper. The game's callers are the same
routines that call the LAN backend's slots (G10 section 1); they are context, the new code does not use them.

| Slot | Name | Called by |
|---|---|---|
| 0 | Init | `u3x_online_netinit` (G13 4.4) |
| 2 | Shutdown | the helper's NetShutdown (`u3x_lan_shutdown`, G08 4.8) through the table, while the online object is the registered manager |
| 3 | AttachSession | `u3x_online_netinit`: the match session, then the party session |
| 4 | Create | the game: the "countdown done" routine (match session) and the party machine (party session) |
| 5 | Search | the game's match search |
| 6 | Join | the game: the party machine and the match join state handler; the helper's party-follow logic |
| 7 | Leave | the game's session leave, also for both sessions at NetShutdown; the party machine; the helper's post-match and follow code |
| 8 | SetAttr | `u3x_online_netinit` (party session, value 1); the game's session code; the helper's post-match code |
| 9 | Kick | the game's kick routine; `u3x_session_kick` (G12); `u3x_add_member` (G07) for a member over capacity |
| 10 | Promote | the game's session code |
| 11 | RoomFlags | the game's session code when a session is locked / unlocked for a match |
| 12 | MemberData | the game's "set member data"; `u3x_fl_md` of this group (a direct call) |
| 13 | SetAttr4 | the game's session code; the helper's post-match code |
| 14 | HostRank | the game's match start code (the host, when every player is connected) |
| 15 | RoomNameAuto | the game's session code |
| 16 | RoomName | the game's session code |

**Other callers.**

- `u3x_onb_obj`: the C part's one-line wrappers `onb_obj` / `onb_mgr` (behind the exports `u3h_online_backend`,
  `u3h_onb_mgr`, `u3h_online_manager` and the address probe tick).
- `u3x_onb_rx`: the C wrapper `onb_rx(p, n)`, called by the community link's frame dispatcher for every pushed
  frame whose type has the high byte 0xc4 (a room message from the service; the frame's payload is the message),
  on the main thread, between two Updates.
- `u3x_onb_send`: every sending slot, and `u3x_onb_s1` (G13 5.14: the ping). C wrapper `onb_send` (no caller left).
- `u3x_onb_wire`: slots 4 and 6, and `u3x_onb_msg_in` (G13). C wrapper `onb_wire` (no caller left).
- `u3x_onb_note`: every slot, also `u3x_onb_s1`. C wrapper `onb_note` (no caller left).
- `u3x_onb_strcpy`: the room name message (6.9) and `u3x_onb_msg_in` (G13 5.13). C wrapper `onb_strcpy`.
- `u3x_onb_bound`, `u3x_onb_cookie`, `u3x_onb_name_time`, `u3x_onb_geo`, `u3x_onb_md_stamp`: only functions of
  this group.
- `u3x_fl_md`: `u3x_onb_msg_in` (G13 5.6 step 10, with a null first argument) and, through the C wrapper `fl_md`,
  the C part's party-follow tick (several places; first argument = the LAN backend object on LAN, null online).

## 2. Interface

All functions are `extern "C"`; every name and signature stays as it is today. `base` is the game image base; `be`
is the online backend object (3.2); `ss` is a session object (3.3). A slot's result is an s64 (a negative 32-bit
value is widened with its sign).

### 2.1 Part 1

| Function | C++ signature | Result |
|---|---|---|
| registered online object | `void *u3x_onb_obj(uintptr_t base)` | the object, or null (4.1) |
| bounded text copy | `void u3x_onb_strcpy(char *d, const char *s, uint32_t n)` | none |
| slot note | `void u3x_onb_note(int slot)` | none |
| binding of a session | `int u3x_onb_bound(void *be, uint8_t *ss)` | binding index 0..3, or −1 |
| cookie of a session | `uint32_t u3x_onb_cookie(void *be, uint8_t *ss)` | binding index + 1, or 0 |
| send one room message | `int32_t u3x_onb_send(const void *msg, uint32_t n)` | `n`, or −1 = not sent |
| room log line | `void u3x_onb_wire(uintptr_t base, const char *what, uint8_t *ss, uint64_t room, const char *b, int32_t v)` | none |
| receive append | `void u3x_onb_rx(const uint8_t *p, uint32_t n)` | none |
| automatic room name | `void u3x_onb_name_time(uintptr_t base, char *out)` | none; `out` gets a NUL-terminated text of at most 48 bytes with the NUL |
| location words | `void u3x_onb_geo(uintptr_t base, uint16_t *a, uint16_t *b)` | none; two u16 stored |
| slot 0, Init | `int64_t u3x_onb_s0(void *be)` | 0 = success, −1 = no connection to the service |
| slot 2, Shutdown | `int64_t u3x_onb_s2(void *be)` | always 0 |
| slot 3, AttachSession | `int64_t u3x_onb_s3(void *be, uint8_t *ss)` | binding index 0..3, or −1 = no free binding |

The C wrappers (`onb_obj`, `onb_strcpy`, `onb_note`, `onb_send`, `onb_rx`, `onb_wire`) pass their arguments
unchanged.

### 2.2 Part 2

| Function | C++ signature | Result |
|---|---|---|
| slot 4, Create | `int64_t u3x_onb_s4(void *be, uint8_t *ss, uint32_t cap, uint32_t a4, uint32_t a5, uint32_t a6)` | 0, or −1 = session not attached |
| slot 5, Search | `int64_t u3x_onb_s5(void *be, uint8_t *ss, uint32_t a3, uint32_t a4, int32_t level, int32_t below, int32_t above, uint32_t a8)` | always 0 |
| slot 6, Join | `int64_t u3x_onb_s6(void *be, uint8_t *ss, uint64_t room)` | 0, or −1 = session not attached |
| slot 7, Leave | `int64_t u3x_onb_s7(void *be, uint8_t *ss)` | 0, or −1 = session not attached |
| slot 9, Kick | `int64_t u3x_onb_s9(void *be, uint8_t *ss, uint32_t id, uint32_t flag)` | 0, or −1 = session not attached |

`cap` = the room's capacity; `room` = a room id; `id` = a member id. `a4`, `a5`, `a6` of Create and `a3`, `a4`,
`level`, `below`, `above`, `a8` of Search are the game's further arguments, used exactly as 5.2 and 5.3 say (the
names are this document's; `a4` of Search is not used). The seventh and eighth argument of Search arrive on the
stack (ordinary calling convention; the signature takes care of it).

### 2.3 Part 3

| Function | C++ signature | Result |
|---|---|---|
| slot 8, SetAttr | `int64_t u3x_onb_s8(void *be, uint8_t *ss, uint32_t v)` | 0, or −1 = not attached or no change |
| slot 10, Promote | `int64_t u3x_onb_s10(void *be, uint8_t *ss, uint32_t id)` | 0, or −1 = not attached |
| slot 11, RoomFlags | `int64_t u3x_onb_s11(void *be, uint8_t *ss, uint32_t v)` | 0, or −1 = not attached |
| member data stamp | `void u3x_onb_md_stamp(uintptr_t base, uint8_t *md)` | none; `md` = a 0x40-byte member data block |
| slot 12, MemberData | `int64_t u3x_onb_s12(void *be, uint8_t *ss, const uint8_t *data, uint32_t len)` | 0, or −1 = too long or not attached |
| party member data | `void u3x_fl_md(void *lan_be, uint8_t *ps)` | none; `lan_be` = the LAN backend object or null, `ps` = the party session |
| slot 13, SetAttr4 | `int64_t u3x_onb_s13(void *be, uint8_t *ss, uint32_t v)` | 0, or −1 = not attached |
| slot 14, HostRank | `int64_t u3x_onb_s14(void *be, uint8_t *ss, const uint16_t *rank, uint32_t n)` | 0, or −1 = not attached |
| slot 15, RoomNameAuto | `int64_t u3x_onb_s15(void *be, uint8_t *ss)` | always 0 |
| slot 16, RoomName | `int64_t u3x_onb_s16(void *be, uint8_t *ss)` | always 0 (`be` is not used) |

The C wrapper `fl_md(be, ps)` passes its two arguments unchanged. The "send the room name" routine shared by slots
15 and 16 (6.9) is internal to part 3 and needs no exported name.

### 2.4 Functions of other groups, of the C part and of the shared tools that this group calls

Not to be described or reimplemented here. The tools of `CONVENTIONS.h` (`rd`, `at`, `game_call`, `u3h_ext`,
`ext_eb`, `f_to_int`) are used as given there. "Accessor" = answers a fixed address or a stored value and has no
effect; how often and when an accessor is called is not part of the behaviour. "Pure read" = no effect, but the
state behind it can change between two calls (other threads), so the behaviour sections give the order and number
of these calls. All of them exist today under these names.

| Name | Signature | What it does | Part |
|---|---|---|---|
| `u3h_onb_object` | `void *u3h_onb_object(void)` | accessor: the C part's storage for the backend object (0x1060 bytes) | 1 |
| `u3h_onb_vt` | `void **u3h_onb_vt(void)` | accessor: the C part's table of 32 function pointers of the online backend | 3 |
| `u3h_onb_msg` | `uint8_t *u3h_onb_msg(void)` | accessor: the C part's message buffer (0x100 bytes) in which the slots build their messages | 2, 3 |
| `u3h_onb_pj` | `uint64_t *u3h_onb_pj(void)` | accessor: four u64, for each binding the room id its pending Join asked for (0 = none) | 2 |
| `u3h_lnb_match_room` | `uint64_t *u3h_lnb_match_room(void)` | accessor: the helper's "match room id" variable (the room the party should follow) | 2, 3 |
| `u3h_smn_up` | `int u3h_smn_up(void)` | pure read: the session socket: 0 = none, 1 = live, 2 = connecting or reconnecting | 1 |
| `u3h_smn_send` | `int u3h_smn_send(const void *m, uint32_t n)` | hands one room message (as built here) to the session socket; a negative result = not sent | 1 |
| `u3h_smn_attach` | `void u3h_smn_attach(int on)` | tells the session socket's status record that the backend is attached (1) or not (0) | 1 |
| `u3h_ol_up` | `int u3h_ol_up(void)` | pure read: non-zero when the community-server link is signed in | 1 |
| `u3h_ol_frame` | `int u3h_ol_frame(uint32_t type, const void *m, uint32_t n)` | queues one frame of the community-server link with this frame type and the `n` payload bytes; 0 = no room for it, non-zero = queued | 1 |
| `u3h_fw_sess` | `const char *u3h_fw_sess(uintptr_t base, uint8_t *ss)` | the text `match`, `party` or `other` for a session (no effect) | 1, 2 |
| `u3h_fw_line` | `void u3h_fw_line(const char *a, uint32_t hex, int digits, const char *b, int32_t v)` | one line for the session log: `a`; when `digits` is not 0, `0x` and that many hex digits of `hex`; `b` if not null; `v` in decimal if it is ≥ 0 | 1, 2 |
| `u3h_fw_game_ra` | `uint32_t u3h_fw_game_ra(uintptr_t base, uintptr_t ra, uintptr_t fp)` | looks at up to ten return addresses, starting with `ra` and going on through the chain of saved frames that starts at `fp`: the first one inside the game image's code, as an image offset; 0 = none found. No effect | 2 |
| `u3h_fw_host_link` | `int32_t u3h_fw_host_link(uintptr_t base)` | pure read: state of the link to the match host: 2 = up, −1 = none, −2 = the match session has no room, −3 = the host is not among its members | 2 |
| `u3h_cg_log` | `void u3h_cg_log(const char *a, int32_t v)` | one custom-game line for the session log: `a`, then `v` in decimal if it is ≥ 0 | 3 |
| `u3h_fl_state` | `void *u3h_fl_state(void)` | accessor: the helper's state object as the party-follow tick last got it, or null before its first run | 3 |
| `u3h_player_profile` | `uint8_t *u3h_player_profile(uintptr_t base)` | pure read: the active local user's profile object, or null | 3 |
| `u3h_lnb_marks` | `void u3h_lnb_marks(uint8_t *guest_wait, uint8_t *host_playing, uint8_t *at_start, uint64_t *followed)` | copies four of the helper's party-follow variables to the given places | 3 |
| `u3h_ng_stamp` | `void u3h_ng_stamp(uintptr_t base, void *s, uint8_t *md)` | the helper's late-join / net-game stamp: may set more bits in byte 0x32 and change bits 16..27 of the u32 at 0x3c of the data block; accepts a null `s` | 3 |
| `u3x_member_status` | `uint8_t u3x_member_status(int custom_game, int guest_waiting, int host_playing)` | the helper's member status byte from three truth values (G07 2.3) | 3 |
| `u3x_member_start` | `uint8_t u3x_member_start(int at_start, uint64_t followed, uint64_t room)` | the helper's "at the start" byte (G07 2.3) | 3 |
| `u3x_find_member` | `uint8_t *u3x_find_member(uint8_t *session, uint32_t id)` | the first member slot in use whose member id equals `id`, or null | 2 |
| `u3x_entry_by_id` | `uint8_t *u3x_entry_by_id(uintptr_t base, int32_t id)` | G12: the first of the 32 connection entries whose id field equals `id`, in use or not; null when none | 2 |
| `u3x_connection_state` | `int32_t u3x_connection_state(uintptr_t base, int32_t id)` | 2 when that entry exists, is in use and has a peer link handle; otherwise −1. No effect | 2 |
| `u3x_session_host` | `int u3x_session_host(const uint8_t *s)` | G12: non-zero when the session has no room id or its own member id equals its host member id | 3 |
| `u3x_lnb_s12` | `int64_t u3x_lnb_s12(void *be, uint8_t *ss, const uint8_t *data, uint32_t len)` | G10 5.7: the LAN backend's MemberData slot | 3 |
| `u3x_onb_ping_restart` | `void u3x_onb_ping_restart(void *be)` | G13 5.15: the backend's next ping time = now + 30 s | 1 |

### 2.5 Strays (old functions in no work group that this document covers or points at)

- **Covered here, part 1:** `u3x_onb_send` (4.5), `u3x_onb_wire` (4.6), `u3x_onb_note` (4.3), `u3x_onb_strcpy`
  (4.2), `u3x_onb_geo` (4.9), the slots `u3x_onb_s0` (4.10), `u3x_onb_s2` (4.11), `u3x_onb_s3` (4.12), and three
  one-line functions of the inventory's "trivial" list whose behaviour is given in full so that they can be written
  anew with the rest: `u3x_onb_obj` (4.1), `u3x_onb_bound`, `u3x_onb_cookie` (4.4).
- **Covered here, part 3:** `u3x_onb_md_stamp` (6.4), `u3x_fl_md` (6.6), the slots `u3x_onb_s15`, `u3x_onb_s16`
  (6.9).
- **Covered here as rules, not as functions:** the old file's internal log text builder (3.8), its "zeroed message
  in the shared buffer" step (3.9) and its one-line session accessors (3.1, 3.3).
- **Not covered, stay as they are:** `u3x_onb_by_room`, `u3x_onb_by_cookie`, `u3x_onb_vtable`, `u3x_onb_s_nop`
  (G13 2.3 / 2.4); `u3x_onb_ping_restart`, `u3x_onb_ping_due` and `u3x_onb_s1` are G13's.

## 3. Game and helper facts

### 3.1 Game data and game functions

| Image offset | Type | Meaning |
|---|---|---|
| 0x17b4ba4 | s32 | the engine's local clock in ticks |
| 0x17d5c88 | 32-bit float | location word A of the game's net object (net object 0x17d5c58 + 0x30) |
| 0x17d5c8c | 32-bit float | location word B of the net object (+ 0x34) |
| 0x17d6010 | 0x24 bytes | the player's own network name |
| 0x17d603c | u32 | net menu mode; 2 = Custom Game |
| 0x17d167c | s16 | index of the selected net game |
| 0x17d6420 | object | the match session |
| 0x17dc298 | object | the party session |
| 0x17dd760 | pointer | the online backend object |
| 0x17dd768 | pointer | the registered session manager |
| 0x17cb060 | object | the net game manager |
| 0x17cb3e0 | pointer | field 0x380 of the net game manager: the local net player; null as long as the match's net players do not exist |
| 0x1b68810 | u32 | the game's net state word; the value 0x30 is the state in which the match host waits for everyone before loading |

| Image offset | Arguments | Meaning |
|---|---|---|
| 0x7a25f0 | (session) | clear the session's members; result unused |
| 0x72eca0 | (net game manager = image offset 0x17cb060 as a pointer, session, member slot, u32 0, u32 0) | create a net player for a session member; result: pointer to the net player, or null |

Net player fields written here: s32 at 0x21c = team (−1 = none); u8 at 0x365 = "ready".

### 3.2 The online backend object (G13 3.4; the C part owns the storage and asserts the layout)

| Offset | Type | Meaning |
|---|---|---|
| 0 | pointer | function table |
| 8 | 0x24 bytes | the player's network name |
| 0x30 + 8·k, k = 0..3 | pointer | binding k: the session attached under cookie k + 1, or null |
| 0x50 | u32 | time of the next ping (a float pattern; only `u3x_onb_ping_restart` writes it) |
| 0x54 | u32 | number of received bytes waiting in the buffer |
| 0x58 | 0x1000 bytes | the receive buffer |
| 0x1058 | u64 | the game image base ("the object's base" below) |

"The session is attached" = one of the four bindings holds its address. The **cookie** of an attached session is
its binding index + 1.

### 3.3 Session fields used here

| Offset | Type | Meaning |
|---|---|---|
| 8 | pointer | the backend the session is attached to |
| 0x14 | u32 | game pool id |
| 0x18 | u64 | room id; 0 = no room |
| 0x20 | 128 bytes of text | room name |
| 0xa1 / 0xa4 | u8 / u32 | create done / create result |
| 0xb0 / 0xb4 | u8 / u32 | search done / search result |
| 0xb8 / 0xbc | u8 / u32 | join done / join result |
| 0xc8 / 0xcc | u8 / u32 | leave done / leave result |
| 0xe0 | u8 | host rank done |
| 0xe8 | u8 | room name done |
| 0xf0 | u8 | room flags set |
| 0xf8 | u32 | game type word |
| 0x204 | u32 | room flags |
| 0x20c | u32 | capacity |
| 0x218 | u32 | number of search results |
| 0x680 + i·0x120, i = 0..9 | member slot | name at 0 (text, up to 0x24 bytes); u8 "in use" at 0x25; u32 member id at 0x2c; s32 connection id at 0x34 |
| 0x12e4 | u32 | own member id |
| 0x12e8 | u32 | the host's member id |
| 0x12ec | u8 | room attribute flag |
| 0x12f0 | u32 | length of the own member data |
| 0x12f4 | 0x40 bytes | the own member data |
| 0x1338 | u8 | (match session) "start loading" flag |

"This machine leads the session" = own member id equals host member id. "The session's room" = the u64 at 0x18,
read at the moment the step says.

Connection entry fields (G12 3.4) that the Kick line prints: u8 at 4 ("in use"), u8 at 5, u8 at 6.

### 3.4 The helper's own state

Counter and diagnostic words eb[] (`ext_eb()`):

| Word | Meaning | Written by |
|---|---|---|
| eb[93] | net players created at a match start (shared with the LAN backend) | slot 11 |
| eb[130] | result of Init, low 32 bits | slot 0 |
| eb[131], eb[132] | slot calls, last slot | `u3x_onb_note` |
| eb[133], eb[134] | room messages sent, type of the last one | `u3x_onb_send` |
| eb[138] | creates | slot 4 |
| eb[139] | joins | slot 6 |
| eb[140] | searches | slot 5 |
| eb[141] | leaves | slot 7 |
| eb[147] | received pushes dropped because the buffer was full | `u3x_onb_rx` |

Variables of this group, all zero when the helper is loaded, kept for the whole run: two u32 counters of part 2
(K-gone, K-lead: refused kicks, 5.6); one u32 counter (MD-count) and one u8 mark (MD-once) of part 3 (6.6).

The helper's state object `s` (from `u3h_fl_state()`): "native 21" = the pointer to a function at s + 936 (natives
are pointers at 768 + 8·k): the game's "rank word of a player profile"; one argument (the profile object), result
u32. May be null.

### 3.5 Room messages, client to server

Every message starts with its u32 type. Bytes not named are 0. These are the messages as this group builds them
(little-endian); the session socket path converts them to its wire order inside `u3h_smn_send`, the community link
carries them as they are.

| Type | Size | Content |
|---|---|---|
| 303 create | 232 | u32 cookie at 8; u32 pool id at 12; u32 0 at 16; u32 game type word at 20; u16 at 28; u16 location word B at 32; u16 location word A at 34; u16 capacity at 36; u8 member data length at 38; u8 at 39; room name text at 40 (128 bytes); member data at 168 (64 bytes) |
| 304 join | 88 | u32 cookie at 8; u8 member data length at 12; u64 room id at 16; member data at 24 (64 bytes) |
| 307 leave | 16 | u64 room id at 8 |
| 309 search | 36 | u32 cookie at 8; u32 pool id at 12; u32 game type word at 16; u16 location word B at 20; u16 location word A at 22; u16 at 24; u16 lowest level at 28; u16 highest level at 30; u32 0 at 32 |
| 311 kick | 16 | u16 member id at 4; u16 at 6; u64 room id at 8 |
| 314 member data | 80 | u8 data length at 4; u64 room id at 8; data at 16 (64 bytes) |
| 316 promote | 16 | u16 member id at 4; u64 room id at 8 |
| 318 attribute flags | 16 | u8 value at 4; u8 mask at 5; u64 room id at 8 |
| 320 room flags | 16 | u16 flags at 4; u64 room id at 8 |
| 322 host rank | 16 + 2·n | u16 n at 4; u64 room id at 8; n u16 values from 16 |
| 323 room name | 144 | u64 room id at 8; name text at 16 (128 bytes) |

The type numbers, sizes and offsets are the wire format the session service and the community server expect; they
are interface facts of the helper's own protocol, not a game table. (The big-endian field list of the session
socket is G15's and is not needed here.)

### 3.6 Community-link frame types

A room message of type T sent over the community link is a frame of type 0x400 + (T − 300). Pushed room messages
arrive as frames 0xc400 + (T − 300) (their dispatch to `u3x_onb_rx` is in the C part).

### 3.7 The helper's own log texts

Spaces at the start and end belong to the texts; the length column counts every character.

| Name | Length | Text |
|---|---|---|
| T-session | 15 | ` session, room ` |
| T-create | 14 | `Create (303): ` |
| T-old | 23 | ` (the old one), cookie ` |
| T-join | 24 | `Join (304) asked by the ` |
| T-cookie | 9 | `, cookie ` |
| T-leave | 19 | `Leave (307) of the ` |
| T-game | 27 | ` room asked by the game at ` |
| T-outside | 74 | ` room asked from outside the game image (helper or runtime), first return ` |
| T-state | 21 | `  ... net state then ` (two spaces first) |
| T-link | 31 | `, link to the match host + 10: ` |
| T-kick | 21 | `Kick (311) of member ` |
| T-from | 10 | ` from the ` |
| T-entry | 24 | ` room, its link state+1 ` |
| T-flags | 27 | `, entry/active/flag5/flag6 ` |
| T-slash | 1 | `/` |
| T-asked | 11 | `, asked at ` |
| T-gone | 69 | `Kick (311) asked for an id that is not in the room: not sent; so far ` |
| T-lead | 87 | `Kick (311) not sent: this PC does not lead that room, or the member is this PC; so far ` |
| T-made | 43 | `match start (slot 11): net players created ` |
| T-local | 31 | `  ... local net player present ` (two spaces first) |
| T-there | 53 | `match start (slot 11): local net player already there` |
| T-loaded | 66 | `every player connected (slot 14): start-load flag raised, players ` |
| T-notonb | 64 | `party session is not on the online backend: member data not sent` |
| T-mdsent | 52 | `party member data sent: room-follow marks, at start ` |

### 3.8 Built log lines

Where a section says "the line F1, F2, ...", the text is built from these fragments into at most 120 characters
plus the ending NUL. A fragment is appended character by character; a character is taken only while fewer than 120
are in the text (the rest of that fragment and everything after it is dropped). A fragment is a text of 3.7, the
result of `u3h_fw_sess`, a caller's text, or a number in decimal without leading zeros (0 is `0`), most significant
digit first. (The same rule as G13 5.1.)

### 3.9 Building a message

"Build the message T (L bytes)" in sections 5 and 6 means: in the C part's message buffer (`u3h_onb_msg()`), set the
first L bytes to 0, store the u32 T at 0 and the fields the section names; bytes of the buffer beyond L keep what
they held. "Send it" = `u3x_onb_send(buffer, L)`; its result is always ignored by the slots. The buffer is shared by
all slots (one thread, no slot runs inside another's building) and holds the last message built when the slot
returns. The largest message (322 with 120 values) is exactly the buffer's 0x100 bytes.

## 4. Behaviour, part 1

### 4.1 `u3x_onb_obj(base)`

The pointer at image offset 0x17dd760 when it is not null and the pointer at image offset 0x17dd768 equals it;
otherwise null. (The online object, but only while it is the registered manager.)

### 4.2 `u3x_onb_strcpy(d, s, n)`

Copies the characters of `s` up to its first NUL, at most `n` − 1 of them, to `d`, and puts a NUL behind the last
one copied. Bytes of `d` behind that NUL keep their content. With `n` = 1 only the NUL is written; with `n` = 0 the
function behaves as with `n` = 1 (one NUL at `d`).

### 4.3 `u3x_onb_note(slot)`

eb[131] plus 1; eb[132] = `slot`.

### 4.4 `u3x_onb_bound(be, ss)`, `u3x_onb_cookie(be, ss)`

`u3x_onb_bound`: the lowest binding index (0..3) whose pointer equals `ss`; −1 when none does. `u3x_onb_cookie`:
that index + 1, or 0 when the session is not attached. No effect.

### 4.5 `u3x_onb_send(msg, n)`

T = the u32 at the start of `msg`.

1. T below 300 or above 0x3ff: result −1, nothing else.
2. Ask `u3h_smn_up()` once.
   - Non-zero (the session socket is in use, live or reconnecting): `u3h_smn_send(msg, n)`; a negative answer:
     result −1. The community link is not tried.
   - Zero: if `u3h_ol_up()` is 0: result −1 (no frame is queued). Otherwise `u3h_ol_frame(0x400 + (T − 300), msg,
     n)`; an answer of 0: result −1.
3. Sent: eb[133] plus 1; eb[134] = T; result = `n` as an s32.

`n` is not checked against anything.

### 4.6 `u3x_onb_wire(base, what, ss, room, b, v)`

Build the line `what`, `u3h_fw_sess(base, ss)`, T-session (3.8) and call `u3h_fw_line(line, low 32 bits of room, 8,
b, v)`. (The log then reads: `what`, the session's kind, ` session, room 0x` and eight hex digits, `b`, `v`.)

### 4.7 `u3x_onb_rx(p, n)`

Works on the C part's object storage (`u3h_onb_object()`), whatever the game's pointers hold.

1. If `u3h_smn_up()` is not 0: nothing (while the session socket is in use, the buffer belongs to its stream).
2. If the received count + `n` (computed in 32 bits) is above 0x1000: eb[147] plus 1, nothing else (the whole push
   is dropped).
3. Otherwise copy the `n` bytes behind the bytes already in the receive buffer and add `n` to the received count.

### 4.8 `u3x_onb_name_time(base, out)`

Writes to `out`: the characters of the own network name (image offset 0x17d6010) up to its first NUL, at most 0x24;
then `.`; then the local clock (the s32 at image offset 0x17b4ba4, taken as an unsigned 32-bit number) in decimal
without leading zeros; then a NUL. At most 36 + 1 + 10 + 1 bytes. Bytes of `out` behind the NUL keep their content.

### 4.9 `u3x_onb_geo(base, a, b)`

Reads both location words first, then stores: `*a` = the low 16 bits of location word **B** (the float at image
offset 0x17d5c8c) converted to an s32 by truncation toward zero; `*b` = the same for location word **A** (image
offset 0x17d5c88). A value that does not fit an s32, or a NaN, converts to 0x80000000 (low 16 bits 0).

Float rule (PIPELINE.md): `f_to_int` on each 32-bit pattern gives exactly this, bit for bit (the old build
converted with the x87 unit set to truncate, 32-bit result: the same values in every case).

### 4.10 `u3x_onb_s0(be)`: slot 0, Init

1. `u3x_onb_note(0)`.
2. Received count (be + 0x54) = 0.
3. `u3x_onb_ping_restart(be)`.
4. Connected = `u3h_smn_up()` is 1, or else `u3h_ol_up()` is non-zero (`u3h_ol_up` is asked only when the first
   answer is not 1). Result = 0 when connected, −1 when not.
5. eb[130] = the low 32 bits of the result (0 or 0xffffffff).
6. On success: `u3h_smn_attach(1)`.

Bindings, name and buffer content are not touched.

### 4.11 `u3x_onb_s2(be)`: slot 2, Shutdown

`u3x_onb_note(2)`; received count = 0; `u3h_smn_attach(0)`; result 0. The bindings stay, no session is changed, no
connection is closed.

### 4.12 `u3x_onb_s3(be, ss)`: slot 3, AttachSession

1. `u3x_onb_note(3)`.
2. The binding = the one that already holds `ss` (4.4), otherwise the lowest-numbered binding that is null. None
   (four other sessions attached): result −1, nothing written.
3. That binding = `ss`; the session's backend pointer (ss + 8) = `be`. Result = the binding index.

Attaching an attached session again changes nothing but restores the session's backend pointer. `ss` is never null.

## 5. Behaviour, part 2

Every slot of this part first notes itself with `u3x_onb_note(k)` (k = its slot number). "Not attached" =
`u3x_onb_bound(be, ss)` answers −1. `base` = the object's base (3.2). "Clear the members" = the game function at
image offset 0x7a25f0 called with the session. Results of `u3x_onb_send` are ignored.

### 5.1 The caller's address (slots 7 and 9)

Two slots log who asked. RA = the address the exported slot function itself will return to; FP = that function's
own frame address (the compiler's built-in "return address of this function" and "frame address of this function",
taken in the exported function, not in a helper it calls). G = `u3h_fw_game_ra(base, RA, FP)`: the image offset of
the game code that (directly or through helper code) made the call, or 0.

### 5.2 `u3x_onb_s4(be, ss, cap, a4, a5, a6)`: slot 4, Create

1. Note (4); eb[138] plus 1.
2. Not attached: result −1. Otherwise k = the binding index.
3. The pending join room of binding k (`u3h_onb_pj()`) = 0.
4. `u3x_onb_wire(base, T-create, ss, the session's room, T-old, k + 1)`.
5. Clear the members.
6. Build the message 303 (232 bytes): cookie = k + 1; pool id = the session's (u32 at 0x14); game type word = the
   session's (u32 at 0xf8), with bit 15 (0x8000) set in addition when `a6` is not 0; u16 at 28 = the low 16 bits of
   `a5`; the u16 at 32 and at 34 = what `u3x_onb_geo(base, …)` stores through its first and its second pointer;
   capacity = the low 16 bits of `cap`; member data length = the low 8 bits of the session's own member data length
   (u32 at 0x12f0); u8 at 39 = 4 when `a4` is not 0, else 0; room name = the text `u3x_onb_name_time(base, …)`
   writes at 40; member data = all 64 bytes of the session's own member data (0x12f4), whatever the length says.
7. Session fields, written before the send: attribute flag (0x12ec) = 0; capacity (u32 at 0x20c) = `cap` (all 32
   bits); create done (0xa1) = 0; create result (u32 at 0xa4) = 0.
8. Send it. Result 0.

The session's room id and room name field are not changed (the answer 305 brings the new room).

### 5.3 `u3x_onb_s5(be, ss, a3, a4, level, below, above, a8)`: slot 5, Search

1. Note (5); eb[140] plus 1.
2. Build the message 309 (36 bytes): cookie = `u3x_onb_cookie(be, ss)` (0 when the session is not attached: the
   search is sent all the same); pool id = the session's; game type word = the session's (u32 at 0xf8), with 0x8000
   set in addition when `a8` is not 0; the u16 at 20 and at 22 = what `u3x_onb_geo(base, …)` stores through its
   first and second pointer; u16 at 24 = the low 16 bits of `a3`; when `level` is not negative: lowest level (u16
   at 28) = the low 16 bits of max(`level` − `below`, 0) and highest level (u16 at 30) = the low 16 bits of `level`
   + `above` (both sums as s32); when `level` is negative both stay 0.
3. The session's number of search results (u32 at 0x218) = 0; this is written before the send.
4. Send it.
5. After the send: search done (0xb0) = 0; search result (u32 at 0xb4) = 0. Result 0.

### 5.4 `u3x_onb_s6(be, ss, room)`: slot 6, Join

1. Note (6); eb[139] plus 1.
2. Not attached: result −1. Otherwise k = the binding index.
3. The pending join room of binding k = `room`.
4. `u3x_onb_wire(base, T-join, ss, room, T-cookie, k + 1)`.
5. Clear the members.
6. Build the message 304 (88 bytes): cookie = k + 1; member data length = the low 8 bits of the session's own
   member data length; room id = `room`; member data = all 64 bytes of the session's own member data.
7. Before the send: join result (u32 at 0xbc) = 0; join done (0xb8) = 0.
8. Send it. Result 0.

The session's room id is not changed here.

### 5.5 `u3x_onb_s7(be, ss)`: slot 7, Leave

1. Note (7); eb[141] plus 1.
2. **Log, in every case** (also when not attached), in this order:
   - G as in 5.1;
   - the line T-leave, `u3h_fw_sess(base, ss)`, then T-game when G is not 0, else T-outside;
   - G not 0: `u3h_fw_line(line, G, 7, null, −1)`; G = 0: `u3h_fw_line(line, the low 32 bits of RA − base, 8, null,
     −1)`;
   - then `u3h_fw_line(T-state, the u32 at image offset 0x1b68810, 2, T-link, u3h_fw_host_link(base) + 10)` (the
     host link is asked after the first line was written).
3. Not attached: result −1. Otherwise k = the binding index.
4. The pending join room of binding k = 0.
5. R = the session's room. Leave done (0xc8) = 1. If `ss` is the match session (image offset 0x17d6420): the
   helper's match room id (`u3h_lnb_match_room()`) = 0.
6. R is 0: result 0 (nothing sent, leave result untouched, members kept).
7. Leave result (u32 at 0xcc) = 0. Build the message 307 (16 bytes) with room id R and send it. Then the session's
   room id = 0; then clear the members. Result 0.

### 5.6 `u3x_onb_s9(be, ss, id, flag)`: slot 9, Kick

1. Note (9). Not attached: result −1.
2. M = `u3x_find_member(ss, id)`. Null (the member is not in the room): take K-gone's value, add 1 to K-gone, and
   when the value taken was a multiple of 1800 (the first such call, the 1801st, ...): `u3h_fw_line(T-gone, 0, 0,
   null, the new K-gone)`. Result 0, nothing sent.
3. If this machine does not lead the session, or `id` is the session's own member id: the same with K-lead and
   T-lead. Result 0, nothing sent.
4. **Diagnostic line.** C = the member's connection id (s32 at M + 0x34); E = `u3x_entry_by_id(base, C)` (may be
   null). Build the line: T-kick; the member's name (the characters at M up to the first NUL, at most 16); T-from;
   `u3h_fw_sess(base, ss)`; T-entry; the number `u3x_connection_state(base, C)` + 1 (0 or 3); T-flags; `1` when E
   exists, else `0`; then three times T-slash and a number: the u8 at E + 4, at E + 5, at E + 6 (each `0` when E is
   null); T-asked. The 120-character limit of 3.8 can cut the end of this line (a 16-character name leaves room for
   only part of T-asked). Then G as in 5.1 and `u3h_fw_line(line, G, 7, null, −1)`. The calls are made in the
   order in which they are named here.
5. R = the session's room. R is 0: result 0 (the line was written, nothing sent).
6. Build the message 311 (16 bytes): member id = the low 16 bits of `id`; u16 at 6 = the low 8 bits of `flag`; room
   id R. Send it. Result 0.

Nothing of the session is changed by Kick; the member leaves when the service's answer arrives (G13).

## 6. Behaviour, part 3

Every slot of this part first notes itself with `u3x_onb_note(k)`. "Not attached", `base`, "build" and "send" as
in section 5 and 3.9.

### 6.1 `u3x_onb_s8(be, ss, v)`: slot 8, SetAttr

1. Note (8). Not attached: result −1.
2. V = the low 8 bits of `v`. If "V is not 0" and "the session's attribute flag (u8 at 0x12ec) is not 0" are both
   true or both false: result −1 (no change), nothing else.
3. The attribute flag = V (the byte itself, not only 0 or 1).
4. The session's room is 0: result 0.
5. Build the message 318 (16 bytes): room id; mask (u8 at 5) = 3; value (u8 at 4) = V when the net menu mode
   (image offset 0x17d603c) is 2, otherwise 3 when V is not 0 and 0 when it is. Send it. Result 0.

### 6.2 `u3x_onb_s10(be, ss, id)`: slot 10, Promote

Note (10). Not attached: −1. The session's room is 0: result 0. Otherwise build the message 316 (16 bytes): member
id = the low 16 bits of `id`; room id. Send it. Result 0.

### 6.3 `u3x_onb_s11(be, ss, v)`: slot 11, RoomFlags (and the net players of a starting match)

1. Note (11).
2. **Match start.** Only when `v` is not 0 and `ss` is the match session (before the attach test):
   - if the local net player pointer (image offset 0x17cb3e0) is null: for each of the session's ten member slots
     in ascending order that is in use, call the game function at image offset 0x72eca0 with (image offset
     0x17cb060 as a pointer, `ss`, the member slot, u32 0, u32 0). When it returns a net player: its team (s32 at
     0x21c) = −1, its "ready" byte (0x365) = 0, eb[93] plus 1; a null result: nothing for that member. Afterwards
     `u3h_cg_log(T-made, number of net players made)`, then `u3h_cg_log(T-local, 1 when the pointer at image
     offset 0x17cb3e0 is now not null, else 0)`;
   - otherwise `u3h_cg_log(T-there, −1)`.
3. Not attached: result −1.
4. The session's room is not 0: build the message 320 (16 bytes): flags = the low 16 bits of `v`; room id. Send it.
5. The session has no room: ask `u3x_session_host(ss)` (for a session without a room its answer is always
   non-zero); on a non-zero answer the session's room flags (u32 at 0x204) = `v` and "room flags set" (0xf0) = 1.
6. Result 0.

### 6.4 `u3x_onb_md_stamp(base, md)`

Writes the helper's own fields into the last 16 bytes of a 0x40-byte member data block (the online twin of G07
4.14):

1. S = `u3h_fl_state()`; P = `u3h_player_profile(base)`. When S is not null, its native 21 (3.4) is not null and P
   is not null: call native 21 with P and store its result with bit 29 (0x20000000) set as the u32 at `md` + 0x3c.
   Otherwise that u32 keeps its value.
2. Store: the helper's match room id (the value `u3h_lnb_match_room()` points to) as the u64 at `md` + 0x34; the
   s16 at image offset 0x17d167c as the s16 at `md` + 0x30; at `md` + 0x32 the byte `u3x_member_status(net menu
   mode is 2, guest_wait, host_playing)`; at `md` + 0x33 the byte `u3x_member_start(at_start, followed, match room
   id)`; the four values guest_wait, host_playing, at_start and followed are those one call of `u3h_lnb_marks`
   gives.
3. Last, `u3h_ng_stamp(base, S, md)` (S may be null; it reads and changes what steps 1 and 2 wrote).

### 6.5 `u3x_onb_s12(be, ss, data, len)`: slot 12, MemberData

1. Note (12).
2. `len` above 64: result −1, nothing else.
3. Store the data in the session, before the attach test: own member data length (u32 at 0x12f0) = `len`; `len`
   bytes copied from `data` to session + 0x12f4 in ascending order (`data` may be session + 0x12f4 itself; then
   the bytes do not change).
4. Not attached: result −1.
5. The session's room is 0: result 0.
6. Build the message 314 (80 bytes): data length = `len`; room id; `len` bytes of `data` at 16. When `ss` is the
   party session (image offset 0x17dc298) and `len` is exactly 0x40: `u3x_onb_md_stamp(base, message + 16)`. Send
   it. Result 0.

The data kept in the session is the caller's, without the stamp; only the message carries the stamp.

### 6.6 `u3x_fl_md(lan_be, ps)`

Sends this machine's party member data again (the helper's party-follow marks travel in it).

1. `lan_be` not null (LAN): `u3x_lnb_s12(lan_be, ps, ps + 0x12f4, 0x40)`; nothing else.
2. B = the backend the session is attached to (pointer at ps + 8). When B is null or B's function table pointer is
   not the online backend's table (`u3h_onb_vt()`): if MD-once is 0, set it to 1 and `u3h_cg_log(T-notonb, −1)`
   (so this line appears once per helper load); nothing else.
3. The session's own member data length (u32 at 0x12f0) = 0x40.
4. `u3x_onb_s12(B, ps, ps + 0x12f4, 0x40)` (a direct call with all its effects, the slot note included; result
   ignored).
5. Take MD-count's value and add 1 to MD-count (on every call that reaches this step). When the value taken was
   below 40: read the marks with `u3h_lnb_marks` and `u3h_cg_log(T-mdsent, at_start)`.

### 6.7 `u3x_onb_s13(be, ss, v)`: slot 13, SetAttr4

Note (13). Not attached: −1. The session's room is 0: result 0. Otherwise build the message 318 (16 bytes): value
(u8 at 4) = 4 when the low 8 bits of `v` are not 0, else 0; mask (u8 at 5) = 4; room id. Send it. Result 0. No
session field is read or written besides the room id.

### 6.8 `u3x_onb_s14(be, ss, rank, n)`: slot 14, HostRank

1. Note (14).
2. **Start loading**, before the attach test: when `ss` is the match session, the net state word (image offset
   0x1b68810) is 0x30, the session's "start loading" flag (u8 at 0x1338) is 0 and this machine leads the session:
   the flag = 1 and `u3h_cg_log(T-loaded, n)` (`n` as it was passed).
3. Not attached: result −1.
4. The session's room is 0: host rank done (0xe0) = 1; result 0.
5. N = min(`n`, 120). Build the message 322 (16 + 2·N bytes): count = N; room id; the first N u16 values of `rank`
   copied unchanged from 16. Send it (16 + 2·N bytes). Result 0. (With a room, "host rank done" is not written.)

### 6.9 The room name: `u3x_onb_s15(be, ss)`, `u3x_onb_s16(be, ss)`

"Send the room name" (the internal routine of the inventory): when the session's room is 0 nothing; otherwise
build the message 323 (144 bytes): room id; name = `u3x_onb_strcpy(message + 16, session + 0x20, 128)`; send it.

- Slot 15, RoomNameAuto: note (15); room name done (0xe8) = 1; `u3x_onb_name_time(base, session + 0x20)` (the
  session's name becomes "own name, dot, clock"); send the room name; result 0.
- Slot 16, RoomName: note (16); room name done = 1; send the room name; result 0.

Neither tests whether the session is attached; slot 16 does not use `be` at all.

## 7. Test scenarios

Everything can be tested old against new without the game, with: a fake image addressed through `base` (highest
offset used 0x1b68810); recording stubs at the two game function offsets (0x7a25f0; 0x72eca0 scripted to return a
fake net player or null); fake session objects; a fake backend object (or the stubbed `u3h_onb_object()` storage);
a fake state block behind `u3h_ext()`; a fake state object with a recording native 21; scripted, recording stubs
for the functions of 2.4 that are not accessors (the pure reads answer from a script that can change per call; the
log stubs record their texts and numbers; `u3h_smn_send` and `u3h_ol_frame` record type, length and a copy of the
bytes, and see the session and helper state as it is at that moment). Compared: results, all memory (image,
sessions, backend object, the message buffer's first 0x100 bytes, the pending-join array, the match room variable,
eb words) and the order and arguments of all recorded calls. No fake socket or file layer is needed. Parts 2 and
3 can be tested against stubs of part 1's eight functions or against the real ones. Because K-gone, K-lead,
MD-count and MD-once carry over between calls, old and new are driven with the same call sequences from a fresh
load.

Slots 7 and 9 pass their own return and frame address to `u3h_fw_game_ra`: old and new must be called from the
same call instruction (one indirect call in the test loop) so that both pass the same values; the stub's answer is
scripted (0 and non-zero).

**Part 1.**

1. `u3x_onb_obj`: online pointer null; non-null but another manager registered; both equal.
2. `u3x_onb_strcpy`: `n` = 0, 1, 2, 128; source shorter than, equal to and longer than `n` − 1; a source without
   NUL inside `n` bytes; destination bytes behind the NUL unchanged.
3. `u3x_onb_note`: two calls (counter 2, last slot).
4. `u3x_onb_bound` / `u3x_onb_cookie`: session in binding 0, 3, in two bindings (lowest wins), in none.
5. `u3x_onb_send`: types 299, 300, 0x3ff, 0x400 (range); socket state 1 with send answers `n`, 0 and −1; socket
   state 2 with a failing send (−1, community link not asked); socket 0 with link down (no frame), link up with
   frame answers 1 and 0; frame type for 303 (0x403) and 325 (0x419); eb[133] / eb[134] only on success; `n` = 0.
6. `u3x_onb_wire`: each session kind; `what` of 0, 100 (the line fits exactly), 110 (T-session cut) and 130
   characters (cut at 120, session kind and T-session dropped); room above 32 bits (low half passed); `b` null
   and not, `v` −1 and positive (passed through).
7. `u3x_onb_rx`: socket state 1 and 2 (nothing); empty buffer + 16 bytes; buffer at 0xff0 + 16 (fits exactly) and
   + 17 (eb[147], nothing copied); `n` = 0.
8. `u3x_onb_name_time`: names of 0, 5 and 36 characters without NUL; clock 0, 9, 10, 4294967295 (a negative s32
   prints as the large unsigned number); bytes behind the NUL unchanged.
9. `u3x_onb_geo`: floats 0.0, 1.9, −1.9, 65535.5, 65536.0, 70000.25, −0.0, 3e9, −3e9, NaN, infinity; `*a` comes
   from B and `*b` from A. Bit for bit equal to the old code in every case.
10. Init: socket 1 (link not asked); socket 0 or 2 with the link up; both down (−1, eb[130] = 0xffffffff, no
    attach call); received count cleared and ping restarted in all four; bindings untouched.
11. Shutdown: count cleared, `u3h_smn_attach(0)`, bindings and sessions untouched.
12. AttachSession: empty object (index 0, backend pointer written); second session (1); the first again (0, pointer
    restored after the test overwrote it); a fifth session (−1, nothing written); a hole at binding 1 is filled
    before binding 3.

**Part 2.**

13. Create: not attached (−1; note and eb[138] counted, nothing else); attached in binding 2: order (pending
    cleared, wire line with the OLD room id and cookie 3, clear-members, send) and what the send stub sees
    (attribute flag 0, capacity, create done 0, create result 0 already written); every field of the 232 bytes;
    `a4` 0 / 1 / 0x100; `a6` 0 / 1 with a game type word that already has bit 15; `a5` and `cap` above 16 bits
    (message low 16 bits, session capacity all 32); member data length 0, 0x40, 0x140 (low byte); member data
    always 64 bytes; a message buffer pre-filled with 0xff (first 232 bytes rebuilt, the rest unchanged); the room
    id and name of the session unchanged; a failing send still returns 0.
14. Search: attached (cookie = binding + 1) and not attached (cookie 0, still sent); `level` −1 (both level fields
    0), 0, 5 with `below` 10 (lowest 0) and `below` 2; `level` + `above` above 16 bits; `a8` 0 / 1; `a3` above 16
    bits; the result count is 0 when the send stub runs while search done / result are cleared only afterwards
    (pre-set them to 1 / 7 and let the stub look).
15. Join: not attached; attached: pending room stored, wire line with the asked room, clear-members, fields of the
    88 bytes, join result / done cleared before the send, room id of the session unchanged.
16. Leave: G non-zero (line with T-game, 7 digits) and 0 (T-outside, RA − base with 8 digits); the second line with
    host link answers 2, −1, −2, −3 (12, 9, 8, 7); not attached (both lines, then −1, nothing written); attached
    without a room (pending cleared, leave done 1, leave result untouched, no send, no clear-members, result 0);
    match session (match room variable cleared, also without a room) against party session (kept); with a room:
    leave result 0, message, then room id 0, then clear-members (the send stub still sees the room id, the
    clear-members stub sees 0).
17. Kick: not attached; unknown member: calls 1, 2, 1800, 1801 (lines on the 1st and the 1801st, numbers 1 and
    1801); not leading, and `id` = own id while leading: the same with K-lead, independent counter; the diagnostic
    line with entry found / not found, entry bytes 0, 1 and 255, link state 2 and −1, names of 3, 14, 15 and 16
    characters (the cut at 120); no room (line written, nothing sent); the message: `id` above 16 bits, `flag`
    0, 1, 0x1ff.

**Part 3.**

18. SetAttr: not attached; flag 0 with `v` 0 (−1), flag 0 with `v` 1 (stored 1), flag 1 with `v` 0x100 (low byte 0:
    stored 0), flag 1 with `v` 2 (−1, no change), flag 0 with `v` 2 (stored 2); no room (stored, nothing sent); with
    a room: menu mode 2 (value = V) against another mode (3 or 0); mask 3.
19. Promote, SetAttr4: not attached; no room; message fields (`id` above 16 bits; `v` 0, 1, 0x100, 0x101).
20. RoomFlags: `v` 0 or the party session (no match-start step); match session with `v` 1 and the local net player
    present (T-there only); absent with members in slots 0, 3, 9 and the game stub answering a player for 0 and 9
    and null for 3 (three calls with the five arguments, two players written, eb[93] + 2, T-made 2, T-local 0 or 1
    depending on whether the stub set the pointer); the same on a session that is not attached (step runs, then
    −1); with a room (message, flags field and 0xf0 untouched); without a room (flags = `v` in 32 bits, 0xf0 = 1).
21. Stamp: state null; native 21 null; profile null (rank word kept in all three, the other fields written,
    `u3h_ng_stamp` called with the same state pointer); all present (native called once with the profile, bit 29
    set); menu mode 2 and not 2; marks all zero and all set; a `u3h_ng_stamp` stub that looks at the block (sees
    all fields).
22. MemberData: `len` 65 (−1, nothing stored); `len` 0, 10, 64; `data` = session + 0x12f4; not attached (stored,
    −1); no room (stored, 0); party session with `len` 0x40 (stamped message, unstamped session copy), with `len`
    0x3f (no stamp), match session with `len` 0x40 (no stamp).
23. `u3x_fl_md`: LAN object given (only the LAN slot stub is called); session without backend and session on a
    foreign table: T-notonb on the first of several calls only; on the online backend: length forced to 0x40, slot
    12's effects, T-mdsent with the at-start mark for the calls 1 to 40 and not for the 41st; the count also
    advances when the session has no room.
24. HostRank: each of the four "start loading" conditions failing in turn; all true (flag set, T-loaded with `n`,
    also when not attached, also with `n` above 120); a second call (flag already 1: no line); no room (0xe0 = 1);
    `n` = 0, 1, 120, 121, 1000 (N capped, message length, values copied).
25. Room name: slot 16 without a room (name done 1, nothing sent) and with one (144 bytes; a name of 127 and of
    128 characters without NUL: 127 copied); slot 15: the session's name rewritten in both cases, then as slot
    16; both on a session that is not attached (still sent).

**Needs the live game** (two PCs signed in to the configured servers): that the session service and the community
server accept each message and answer it (create and join giving a 305, search giving a 310, kick, promote,
attribute, flags, host rank and room name changing the room for the other members); the party following the host
into a match room through the stamped member data; the match start with net players made by slot 11 and the load
start by slot 14; Leave at NetShutdown and after a match; both transports (session socket and community link); the
Kick and Leave lines naming a real game caller.

## 8. Oddities of the old code, to be kept

1. No slot looks at the result of `u3x_onb_send`: a slot answers 0 also when nothing could be sent, and the
   session's done / result fields are written all the same.
2. `u3x_onb_send`: while the session socket is in state 2 (reconnecting) the message is lost, the community link
   is not used; the length is not checked; the type range 300..0x3ff is wider than the messages that exist.
3. `u3x_onb_rx` works on the C part's storage even when no online object is registered; it refuses while the
   session socket is in any non-zero state; an overflow drops the whole push (a later Update then parses from a
   message boundary all the same, since pushes are whole messages).
4. `u3x_onb_geo` crosses the two words (first output from word B); both Create and Search rely on it.
5. `u3x_onb_strcpy` with `n` = 0 still writes one byte.
6. Init does not clear the bindings; Shutdown neither clears them nor detaches the sessions (the next setup clears
   the whole object, G13 4.5).
7. Create logs and keeps the old room id; it sends 64 member data bytes whatever the length field says, and only
   the low byte of that length; the capacity goes into the message with 16 bits and into the session with 32.
8. Search does not test the attachment (cookie 0 is sent; the answer is then dropped by the receiving side, G13
   5.9) and clears "search done" after the send, where Create and Join clear their fields before it.
9. Leave writes its two log lines before it knows whether the session is attached; it sets "leave done" and
   clears the match room variable also without a room, but clears the leave result only with one.
10. Kick: the two refusals are counted separately and logged every 1800th time; the diagnostic line is written
    even when the session has no room (nothing sent); the line can lose the end of its last text; `flag` is cut to
    8 bits and sent as 16.
11. SetAttr answers −1 for "no change" exactly as for "not attached"; it compares zero / non-zero but stores the
    byte; the value sent depends on the menu mode (the byte itself in Custom Game, else 3 or 0).
12. RoomFlags makes the net players before the attach test, calls the game function by its image offset (the LAN
    twin goes through the state object), and asks `u3x_session_host` in a case where the answer is fixed.
13. MemberData stores into the session before the attach test (a session that is not attached keeps the data and
    gets −1). `u3x_fl_md` writes the length 0x40 itself and then calls the slot, which writes it again; MD-count
    advances whether or not anything was sent.
14. HostRank raises the "start loading" flag before the attach test and logs `n` before it is capped; "host rank
    done" is written only without a room.
15. RoomNameAuto / RoomName do not test the attachment and set "name done" also when nothing is sent; RoomNameAuto
    rewrites the session's name even without a room. The automatic name uses the local clock (the name only has
    to be unique per player).
16. The stamp of 6.4 writes its fields also when the state object is null (the LAN twin, G07 4.14, then does
    nothing at all).

## 9. What stays as it is

- Every exported name and signature of 2.1 to 2.3 (`u3x_onb_vtable` takes the slots' addresses; G13's code and
  the C wrappers call the helpers).
- The backend object's storage, its function table, the message buffer and the pending-join array in the C part;
  the object's layout (3.2), which the C part asserts.
- `u3x_onb_vtable`, `u3x_onb_s_nop`, `u3x_onb_by_room`, `u3x_onb_by_cookie`, and all functions of 2.4.
- The message layouts of 3.5 and the frame type rule of 3.6 (the servers and G13's receiving side use them), the
  cookie rule, the eb[] indices (diagnostic tools read them), the log texts of 3.7 (support reads the session
  log).
- The slot functions must take their own return and frame address themselves (5.1): a line that names the wrong
  caller would mislead the diagnosis it exists for.
- All game offsets of 3.1 and the session fields of 3.3.
- Everything in section 8.

## 10. Open questions

1. The meaning of Create's `a4`, `a5`, `a6` and of Search's `a3`, `a4`, `a8` (and of the message fields they
   fill: u8 at 39 and u16 at 28 of 303, bit 15 of the game type word, u16 at 24 of 309) was not determined; the
   game passes them and the service interprets them. `level` / `below` / `above` are named after what the code does
   with them (a range around a level). Not needed for the behaviour.
2. `u3x_onb_md_stamp` reached the profile through a file-private function of the old C++ part; the export
   `u3h_player_profile` (same result, G07 2.3) is named here instead.
3. `u3x_onb_obj`, `u3x_onb_bound` and `u3x_onb_cookie` are in the inventory's "trivial" list (one-line functions,
   not counted). Their behaviour is complete in 4.1 and 4.4; whether they are written anew with part 1 or kept is
   the integrator's choice (the effects are the same).

## 11. Clarifications (tester, 2026-11-14)

Facts confirmed by the differential test of the old helper (v7.212) against the reimplementation (parts 1, 2 and
3). The test found no difference, so nothing above is corrected; these points only make it more exact, and they
answer the open questions of section 10 that a test can answer.

1. **4.5, the answer of `u3x_onb_send`.** An answer of 0 from `u3h_smn_send` counts as sent (only a negative answer
   is "not sent"). The result is `n` taken as an s32: with a length of 2^31 or more a message that was sent gives a
   negative result (with 0xffffffff the same −1 as "not sent"), and eb[133] / eb[134] are written all the same. No
   caller passes such a length (the slots send at most 0x100 bytes).
2. **4.7, a count that is already above 0x1000.** The function never produces one; if the field holds one, every
   push is dropped and counted in eb[147], a push of 0 bytes included.
3. **5.3, the two level sums.** `level` − `below` and `level` + `above` are 32-bit sums that wrap (two's
   complement); the wrapped difference is then held at 0 from below, and the low 16 bits of each are stored.
   Examples: level 5, below −3, above −9 gives lowest 8 and highest 0xfffc; level 70000, below 3, above 4 gives
   0x116d and 0x1174. A difference above the s32 range therefore wraps to a negative number and is stored as 0. (In
   the old source the sums were signed additions, whose overflow the language leaves open; built with the helper's
   compiler flags they wrap.)
4. **5.1, 5.5, 5.6, the caller's address.** RA is the address the exported slot returns to, so two calls made by
   the same call instruction pass the same RA. When G is 0 the Leave line prints the low 32 bits of that RA − `base`
   with 8 digits. `u3h_fw_game_ra` is called once per Leave (before `u3h_fw_sess`) and once per Kick that reaches
   the diagnostic line (after `u3x_connection_state`, directly before `u3h_fw_line`).
5. **5.6, 6.6, the counters.** K-gone, K-lead and MD-count are u32 and wrap. A refusal line is written when the
   value taken (before 1 is added) is a multiple of 1800; the number passed to `u3h_fw_line` is the value after 1
   was added, as an s32. After a wrap the next line therefore comes with the counter at 0 again (number 1), and
   `u3x_fl_md` writes its line again for the next 40 calls.
6. **5.6 step 4, the numbers of the Kick line.** `u3x_connection_state` + 1 is printed as an unsigned 32-bit number
   (an answer other than 2 or −1, which the real function never gives, prints accordingly: −2 prints 4294967295).
   The three entry bytes print as 0 to 255. Up to 16 bytes of the member's name are taken as they are.
7. **6.3.** The fourth and fifth argument of the game function at image offset 0x72eca0 are the u32 0. If
   `u3x_session_host` answered 0 for a session without a room (the real function cannot), nothing would be written
   and the result would still be 0.
8. **6.4, the stamp's calls.** In this order: `u3h_player_profile` (exactly once in every case, also when the state
   object or its native 21 is null), native 21 (only when all three exist), `u3h_lnb_marks` (once),
   `u3x_member_status`, `u3x_member_start`, `u3h_ng_stamp`. `u3x_member_status` gets exactly 1 or 0 as its first
   argument and the two mark bytes as their values (0 to 255, not reduced to 0 / 1); `u3x_member_start` gets the
   at-start byte as its value. When `u3h_ng_stamp` runs, the block already holds all fields of steps 1 and 2.
9. **6.5, data that overlaps the session's block.** When `data` points a few bytes below or above session +
   0x12f4, the copy into the session (ascending, byte by byte) changes the bytes `data` points at, and the message
   is then filled from `data` as it is after that copy: not from the caller's bytes as they were, and not from the
   session's block. No caller of today passes such a pointer.
10. **6.6.** T-mdsent gets the at-start mark as its byte value (0 to 255). The function's own store of the length
    0x40 (step 3) cannot be told apart from slot 12's store of the same value: nothing reads the field between
    the two.
11. **6.8.** T-loaded gets `n` as an s32 (a count of 2^31 or more arrives negative).
12. **Open question 10.2.** The export `u3h_player_profile` follows the same rule as the old file's private lookup
    (the pointer at image offset 0x17dd610; the user index is 1 when the byte at image offset 0x17bde75 is not 0,
    otherwise the u32 at image offset 0x17bf714; an index of 4 or more gives null; a profile is 0x7b00 bytes) and
    gave the same profile in every case tested.
13. **Open question 10.3.** `u3x_onb_obj`, `u3x_onb_bound` and `u3x_onb_cookie` written anew behave like the old
    ones in every case tested, a null session pointer included (it matches the lowest empty binding).
14. **4.9, the float step.** The reimplementation's `f_to_int` gives the same two u16 as the old function built
    without SSE (the x87 conversion with truncation) for every 32-bit pattern.
