# G10. LAN backend: the backend object's slots (discovery, create, join, leave, kick)

Behaviour description for a reimplementation (reimpl2 pipeline, describer's document). It says what the functions
do, not how the old ones were written. Offsets are hexadecimal when prefixed with 0x. "Image offset X" means the
address `base + X`, where `base` is the game image base (the backend object holds it, 3.3). All multi-byte values
in memory are little-endian unless a section says otherwise; "u8 / u16 / s16 / u32 / s32 / u64 / s64" are unsigned
/ signed integers of that width, "pointer" is 8 bytes. No function of this group uses floating point.

Read with: `G07_lan_sockets.md` (socket record, byte stream, member slots, join message, slot 0) and
`G08_lan_room_client.md` (room client, NetInit / frame / shutdown). Names and layouts given there are used here
and not repeated in full. The room server is group G09 (`G09_lan_room_server.md`): all of its functions,
including the small ones this group calls, are described there.

The document is written as **two independent parts**, so that two implementers can work at the same time:

| Part | Subject | Functions it delivers |
|---|---|---|
| **A** | the object and its table, discovery, the two datagram functions, the party size | `u3x_lnb_note` (new), `u3x_lnb_bound` (new), `u3x_lnb_ctor`, `u3x_lnb_vtable`, `u3x_lnb_s1`, `u3x_lnb_s2`, `u3x_lnb_s3`, `u3x_lnb_s5`, `u3x_sock_recvfrom`, `u3x_sock_sendto`, `u3x_lan_party_size` |
| **B** | the life of a room: create, join, leave, kick, member data, the solo room | `u3x_lnb_solo_create`, `u3x_lnb_s4`, `u3x_lnb_s6`, `u3x_lnb_s7`, `u3x_lnb_s9`, `u3x_lnb_s11`, `u3x_lnb_s12` |

The split: part B calls two functions of part A by their exported names (`u3x_lnb_note`, `u3x_lnb_bound`) and
nothing else of it; part A only takes the addresses of part B's slot functions (in `u3x_lnb_vtable`). Sections 1 to 3 are common; section 4 is part A, section 5 part B; the test scenarios (6) are
given per part.

Of these, the inventory lists for G10: `u3x_lnb_s1`, `u3x_lnb_s3`, `u3x_lnb_solo_create`, `u3x_lnb_s4`,
`u3x_lnb_s6`, `u3x_lnb_s7`, `u3x_lnb_s9`, `u3x_lnb_s11`. All the others are strays (2.4).

## 1. Purpose and callers

The game's session code talks to a "session manager" object through a table of function pointers ("slots"). PS4
lost the LAN manager class; the helper supplies it as "the LAN backend object" (3.3). Slot 0 (Init) is in G07; this
group holds the other slots, the object's constructor and the fill of its table.

All of it runs on the game's main thread (the per-frame network update and menu / session actions). Nothing is
locked. Create and Join block that thread for up to 3 seconds.

**Who calls the slots.** Through the object's table, always with the object as the first argument:

| Slot | Name | Called by |
|---|---|---|
| 1 | Update | `u3x_lan_tick` (G08) once per frame while multiplayer is active; and slots 4 and 6 of this group while they wait (through `u3h_lnb_call(object, 1)`) |
| 2 | Shutdown | `u3x_lan_shutdown` (G08) through the table; the backend setup (G08, `u3h_lnb_setup` / `u3x_lan_setup`) calls `u3x_lnb_s2` directly on an object that is about to be rebuilt (the C part's forwarder `lnb_s2`) |
| 3 | AttachSession | `u3x_lan_netinit` (G08): the match session, then the party session |
| 4 | Create | the game: the "countdown done" routine at image offset 0x66e9b0 (match session) and the party machine at image offset 0x77d270 (party session) |
| 5 | Search | the game's match search (image offset 0x6a6690); `u3x_lan_find_party` and `u3x_lan_refresh` (helper, G08's source block) |
| 6 | Join | the game: the party machine (0x77d270) and the match join state handler (0x6a6900), with the room id taken from the Find a Party list or from the helper's party-follow logic |
| 7 | Leave | the game's session leave (image offset 0x7a2470), also for both sessions at NetShutdown; the party machine |
| 9 | Kick | the game's kick routine (image offset 0x78fb20; the connection monitor ends there); `u3x_add_member` (G07) for a member over capacity |
| 11 | (match lock) | the game's session code when a session is locked for the match start (old notes); argument = the lock value |
| 12 | MemberData | the game's "set member data" (image offset 0x7a32d0); slot 4 of this group; the helper's party-follow logic (`u3x_fl_md`, another group, which calls `u3x_lnb_s12` directly); the C part's forwarder `lnb_s12` |

(The image offsets of the game's callers come from the old code's notes; they are context, the new code does not
use them.)

**Other callers.**

- `u3x_lnb_ctor` and `u3x_lnb_vtable`: the backend setup at NetInit (G08, 4.5 of its document) through the C
  part's one-line forwarders `lnb_ctor` / `lnb_vtable`.
- `u3x_lnb_solo_create`: slot 4 only.
- `u3x_lnb_note`, `u3x_lnb_bound`: the slots of this group (new exported names for two internal helpers, so that
  both parts use the same ones).
- `u3x_sock_recvfrom`: slot 1 only. `u3x_sock_sendto`: slots 1 and 5 only.
- `u3x_lan_party_size`: the lobby's start checks (another group, two places), and the C part's
  `lnb_party_size` → `lan_party_size` → export `u3h_lan_party_size` (used by the Cinema row, G05, and by C code).

## 2. Interface

### 2.1 Part A: functions to deliver

All `extern "C"`. Existing names and signatures stay.

| Function | Signature | Result |
|---|---|---|
| slot note (new name) | `void u3x_lnb_note(void *be, int slot)` | none |
| binding of a session (new name) | `int u3x_lnb_bound(void *be, const uint8_t *ss)` | binding index 0..3, or −1 |
| constructor | `void u3x_lnb_ctor(void *be, void **vt)` | none |
| table fill | `void u3x_lnb_vtable(void **vt, void *slot0)` | none |
| slot 1, Update | `int64_t u3x_lnb_s1(void *be)` | always 0 |
| slot 2, Shutdown | `int64_t u3x_lnb_s2(void *be)` | result of the last close done (4.5) |
| slot 3, AttachSession | `int64_t u3x_lnb_s3(void *be, uint8_t *ss)` | binding index 0..3, or −1 |
| slot 5, Search | `int64_t u3x_lnb_s5(void *be)` | always 0 |
| datagram receive | `int32_t u3x_sock_recvfrom(const void *s, void *b, uint32_t n, uint32_t *ip, uint32_t *port)` | the import's result |
| datagram send | `int32_t u3x_sock_sendto(const void *s, const void *b, uint32_t n, uint32_t ip, uint32_t port)` | the import's result |
| party size | `int u3x_lan_party_size(uintptr_t base)` | number of players |

Parameters: `be` the backend object (3.3); `ss` a session object (3.2); `vt` a table of 32 pointers; `slot0` the
address of the slot 0 function; `s` a socket record (G07 3.3); `b`, `n` a buffer and its size; `ip` an IPv4 address
in socket-address form (G07 3.2), `port` a port in host order (for `u3x_sock_recvfrom` both are pointers: in and
out).

### 2.2 Part B: functions to deliver

All `extern "C"`; names and signatures stay.

| Function | Signature | Result |
|---|---|---|
| solo room | `void u3x_lnb_solo_create(void *be, uint8_t *ss, uint32_t maxp)` | none |
| slot 4, Create | `int64_t u3x_lnb_s4(void *be, uint8_t *ss, uint32_t maxp)` | see 5.2 |
| slot 6, Join | `int64_t u3x_lnb_s6(void *be, uint8_t *ss, uint64_t room)` | see 5.3 |
| slot 7, Leave | `int64_t u3x_lnb_s7(void *be, uint8_t *ss)` | 0, or −1 when the session is not attached |
| slot 9, Kick | `int64_t u3x_lnb_s9(void *be, uint8_t *ss, uint32_t id)` | 0, or −1 when the session is not attached |
| slot 11 | `int64_t u3x_lnb_s11(void *be, uint8_t *ss, int32_t lock)` | always 0 |
| slot 12, MemberData | `int64_t u3x_lnb_s12(void *be, uint8_t *ss, const uint8_t *data, uint32_t len)` | 0, or −1 (5.7) |

Parameters: `maxp` the room's capacity; `room` a room id (3.6); `id` a member id; `lock` a truth value; `data`,
`len` a member data block and its length. The game passes a fourth argument (0) to Kick; it is not used.

A 32-bit signed result that is returned from a slot is widened to 64 bits with its sign.

### 2.3 Functions of other groups, of the C part and existing ones that the new code calls

Not to be described or reimplemented here. "Exists" = exported today under this name. The tools of
`CONVENTIONS.h` (`rd`, `at`, `game_call`, `u3h_ext`, `ext_eb`) are used as given there.

| Name | Signature | What it does | Used by part |
|---|---|---|---|
| `u3h_lnb_call` | `int64_t u3h_lnb_call(void *object, int slot)` | calls entry `slot` of the object's function table (table pointer = the object's first 8 bytes, 8-byte entries) with the object as the only argument; returns the callee's 64-bit result. Exists. | A, B |
| `u3h_lnb_follow_tick` | `void u3h_lnb_follow_tick(void *be, uintptr_t base, void *s)` | the helper's own party-follow logic, once per backend Update. Exists. | A |
| `u3h_lnb_udpbuf` | `uint8_t *u3h_lnb_udpbuf(void)` | address of the C part's static 0x1000-byte datagram buffer. Exists. | A |
| `u3h_lnb_cli_update` | `void u3h_lnb_cli_update(uintptr_t base, void *bind)` | the room client update of one binding (G08: builds the client view and calls `u3x_lan_client_update`). Exists. | A |
| `u3x_lnb_srv_update` | `void u3x_lnb_srv_update(void *v)` | G09: the room server's update (accept, read, handle messages); does nothing unless the server is running. Exists. | A |
| `u3x_lnb_srv_running` | `int u3x_lnb_srv_running(const void *v)` | G09 4.2: non-zero when the room server's state field is 1 (3.5). Exists. | A |
| `u3x_lnb_srv_count` | `uint32_t u3x_lnb_srv_count(const void *v)` | G09 4.2: the number of the room server's 16 connection slots that are in use (connections, joined or not). Reads only. Exists. | A |
| `u3x_lnb_srv_ctor` | `void u3x_lnb_srv_ctor(void *v)` | G09 4.1: constructs a room server on zeroed memory (socket records reset, 16 slots unused, member id counter 1, kick list filled with −1). Exists. | A |
| `u3x_lnb_srv_stop` | `void u3x_lnb_srv_stop(void *v)` | G09 4.4: state = 0, closes the connection of every slot in use (ascending) and marks it unused, then closes the listening socket; safe on a stopped server. Exists. | B |
| `u3x_lnb_srv_start` | `int32_t u3x_lnb_srv_start(void *v, uint64_t arg, uint32_t port, uint32_t maxp)` | G09: makes the room id (3.6), stores `arg` and the capacity, opens the listening socket on `port`; 0 and state "running", or a negative value (listening socket closed, state unchanged). Exists. | B |
| `u3x_stream_put` / `u3x_stream_get` | `void u3x_stream_put(void *w, uint64_t v, int bytes)` / `uint64_t u3x_stream_get(void *r, int bytes)` | G07: big-endian field write / read on a stream object (G07 3.4, 4.7, 4.8). Exist. | A |
| `u3x_sock_open` | `int u3x_sock_open(const void *s)` | non-zero when the record's descriptor is > 0. Exists. | A |
| `u3x_sock_init` | `void u3x_sock_init(void *s)` | sets the record's descriptor, address and port to 0. Exists. | A |
| `u3x_sock_close` | `int32_t u3x_sock_close(void *s)` | when the descriptor is > 0 and the close import is present: calls the close import and returns its result, else returns 0; in every case the descriptor field becomes 0. Exists. | A, B |
| `u3x_sock_connect` | `int32_t u3x_sock_connect(void *s, uint32_t ip, uint32_t port, int nodelay, int nonblocking)` | G07 4.3: 0 connected, −1 failed. Exists. | B |
| `u3x_sock_send` | `int32_t u3x_sock_send(const void *s, const void *b, uint32_t n, int block)` | G07 2.3: −1 when not open or no send import, else the send import's result. Exists. | B |
| `u3h_lnb_send_join` | `void u3h_lnb_send_join(void *be, void *bind, uint8_t *ss, int host, int32_t *res)` | G07 4.15: builds and sends the join message (id 0xcf) on the binding's client socket; `*res` = the send result (0x80 when all bytes went out, a negative value or −1 otherwise). Exists (forwarder to what G07 calls `u3x_lnb_send_join`). | B |
| `u3h_lnb_rank_stamp` | `void u3h_lnb_rank_stamp(void *be, uint8_t *md, uint32_t len)` | G07 4.14: writes the helper's fields into the last 16 bytes of a member data block when `len` is exactly 0x40. Exists. | B |
| `u3h_lnb_match_room` | `uint64_t *u3h_lnb_match_room(void)` | address of the helper's "match room id" variable (the room the party should follow). Exists. | B |
| `u3h_onp_id` | `const char *u3h_onp_id(void *s)` | the player's name as a NUL-terminated text (online id, or the selected LAN profile's name, or a default); never null. Exists. | B |
| `u3x_party_players` | `int u3x_party_players(const uint8_t *session)` | number of players in a session that has a room (0 when the room id is 0). Exists. | A |
| `u3x_lnb_s14` | `int64_t u3x_lnb_s14(void *be, uint8_t *ss)` | slot 14 (helper's own, stays): notes the slot; with U3MP_LAN_SOLO on, a non-null session whose own member id equals its host member id gets its u8 at 0x1338 set to 1; returns 0. Exists. | A (address only) |
| `u3x_lnb_s_nop` | `int64_t u3x_lnb_s_nop(void *)` | the do-nothing slot (stays): eb[67] plus 1, returns 0. Exists. | A (address only) |

Between the parts: part B declares `u3x_lnb_note` and `u3x_lnb_bound` (2.1); part A declares the six slot
functions of 2.2 (it only takes their addresses).

### 2.4 Strays covered by this document

Functions and helpers of the old C++ file that are in no work group of the inventory:

- the internal slot-call note → `u3x_lnb_note` (4.1). G07 describes the same note inside slot 0 (its 4.16 step 1);
- the internal "binding of a session" search (in the inventory's trivial list) → `u3x_lnb_bound` (4.2);
- `u3x_lnb_ctor` (4.3), `u3x_lnb_vtable` (4.4), `u3x_lnb_s2` (4.5), `u3x_lnb_s5` (4.8), `u3x_lnb_s12` (5.7);
- `u3x_sock_recvfrom`, `u3x_sock_sendto` (4.9): the two socket functions only this group uses;
- `u3x_lan_party_size` (4.10);
- the structures of the backend object and of a binding (3.3), and seven one-line internal helpers whose facts are
  in section 3: "is U3MP_LAN_SOLO on" (3.4), "is this a party session" (3.2), the addresses of the two sessions
  (3.1), the game's "clear members" call (3.1), the wait (3.4), the object cast, the loopback constant.

Covered elsewhere: the room server's small functions `u3x_lnb_srv_ctor`, `u3x_lnb_srv_count`, `u3x_lnb_srv_stop`
(also in no work group) are strays of `G09_lan_room_server.md` (its 4.1, 4.2, 4.4), together with
`u3x_lnb_srv_running` and `u3x_sock_accept`.

Not covered (stay as they are): `u3x_lnb_s14` and `u3x_lnb_s_nop` (2.3; the first is in the inventory's trivial
list, both are the helper's own), `u3x_lan_find_party` and `u3x_lan_reattach` (inventory's trivial list; one table
call / one pointer store of the helper's own).

## 3. Game and helper facts

### 3.1 Game data and functions

| Image offset | Kind | Meaning |
|---|---|---|
| 0x17d6420 | object | the match session (3.2) |
| 0x17dc298 | object | the party session (3.2) |
| 0x7a25f0 | game function | "clear the session's members". One argument: the session pointer. Result unused. |
| 0x17cb060 | object | the net game manager |
| 0x17cb3e0 | pointer | field 0x380 of the net game manager: a net-player pointer that is null as long as the match's net players do not exist |
| 0x17d7b06 | u8 | split-screen: non-zero = a second local player is present |
| 0x17f2fc0 | s32 | split-screen: the value 3 also means a second local player is present |

The game function "create a net player for a session member" (image offset 0x72eca0 according to the old notes) is
**not** called by its address here but through the helper's state object (3.4, native 8). Arguments: (pointer to
the net game manager = image offset 0x17cb060, session pointer, member slot pointer, u32 0, u32 0); result: pointer
to the new net player, or null. Two fields of a net player are written here: s32 at 0x21c = team (−1 = none), u8 at
0x365 = "ready".

### 3.2 The session object (additions to G07 3.6 and G08 3.2)

| Offset | Type | Meaning |
|---|---|---|
| 0 | pointer | function table. Entry 0, called with the session as the only argument (through `u3h_lnb_call(session, 0)`): "is this a party session": the low 8 bits of the result are non-zero for the party session's table and zero for the match session's (old notes; see "Open questions" 4). The other 56 bits of the result are not looked at. |
| 8 | pointer | the backend the session is attached to |
| 0x18 | u64 | room id; 0 = no room |
| 0xa1 | u8 | "create done" |
| 0xa4 | s32 | create result |
| 0xb8 | u8 | "join done" |
| 0xbc | s32 | join result |
| 0x20c | u32 | capacity |
| 0x680 + i·0x120 | member slot i, i = 0..9 | G07 3.6: name at 0 (0x24 bytes), u8 "in use" at 0x25, s32 index at 0x28, u32 member id at 0x2c, s32 connection id at 0x34 |
| 0x12e4 | u32 | own member id |
| 0x12e8 | u32 | the host's member id |
| 0x12f0 | u32 | length of the own member data |
| 0x12f4 | 0x40 bytes | the own member data |

The room client (G08) is what sets "create done" and "join done" to 1 and writes the room id and capacity when the
room server's room state message arrives; it sets the room id to 0 when the connection ends.

### 3.3 The backend object (0x249d8 bytes) and a binding

The layout is asserted by the C part and read by the game (room list) and must stay.

| Offset | Type | Meaning |
|---|---|---|
| 0 | pointer | function table (32 entries of 8 bytes) |
| 8 | 0x24 bytes | this player's network name (filled by the backend setup, G08) |
| 0x50 | s32 | number of rooms in the room list |
| 0x58 + i·0x38 | 20 entries of 0x38 bytes | the room list ("Find a Party"), entry layout below. The game reads count and entries itself. |
| 0x4b8 + i·0x9140 | 4 bindings of 0x9140 bytes | i = 0..3, layout below |
| 0x249b8 | u32 | the discovery port in use (9012, set by slot 0) |
| 0x249bc | socket record | the discovery (UDP) socket; its address field is this machine's own address |
| 0x249c8 | u64 | the game image base |
| 0x249d0 | pointer | the helper's state object, or null |

Room list entry:

| Offset | Type | Meaning |
|---|---|---|
| 0 | u64 | room id |
| 8 | u32 | address of the machine that answered, socket-address form |
| 0xc | u16 | the room's TCP port |
| 0xe | u16 | number of connections the room has |
| 0x10 | u16 | capacity |
| 0x12 | 0x24 bytes | the host's name |
| 0x36 | 2 bytes | not written |

Binding:

| Offset | Type | Meaning |
|---|---|---|
| 0 | pointer | the session this binding belongs to; null = free |
| 8 | room server object (0x8928 bytes) | 3.5 |
| 0x8930 | socket record | the client socket (TCP connection to the room server) |
| 0x893c | u32 | number of received bytes waiting in the receive buffer |
| 0x8940 | 0x800 bytes | the client's receive buffer |

**The function table.** 32 entries. Entry 0 = Init (G07); entries 1, 2, 3, 4, 5, 6, 7, 9, 11, 12, 14 = the slots of
this group and `u3x_lnb_s14`; every other entry (8, 10, 13, 15..31) = the do-nothing slot `u3x_lnb_s_nop`.

### 3.4 The helper's own state

ExtState (address from `u3h_ext()`); eb[k] is the u32 at 0x4050 + 4·k (`ext_eb()[k]`):

| Word | Meaning |
|---|---|
| eb[0] bit 1 (value 2) | set = U3MP_LAN_SOLO is off; clear = on (the default) |
| eb[10], eb[11] (8 bytes at 0x4078) | one pointer: the platform's "sleep for N microseconds" function, signature `int (uint32_t microseconds)`, or null. **The wait** used in section 5: read this pointer; when it is non-null call it with the number of microseconds (result ignored); when null, no wait at all. |
| eb[67], eb[68] | slot calls (counter), last slot called |
| eb[69], eb[70] | Create calls, last Create result |
| eb[71], eb[72] | Join calls, last Join result |
| eb[73] | Search broadcasts sent |
| eb[74] | discovery queries answered |
| eb[75] | rooms added to the room list |
| eb[82] | solo rooms made |
| eb[83] | discovery datagrams with an unknown id |
| eb[84] | Update calls |
| eb[88] | kicks sent |
| eb[89] | member data messages sent |
| eb[92] | send result of the last Create's join message |
| eb[93] | net players created by slot 11 |

A second diagnostic array, "room words": u32 array at ExtState + 0xbc0; word k at 0xbc0 + 4·k. Used here: words
16 to 23 (0xc00..0xc1c) by Kick, word 24 (0xc20) by Create. (The party-follow logic of the C part uses words of
the same array, some with the same indices.)

The socket import table (ExtState 0x200, entry k at 0x200 + 8·k, G07 3.2). Two entries are used by this group:

| k | Import | Arguments | Result |
|---|---|---|---|
| 3 | send-to | (int descriptor, pointer to data, u32 length, int flags, pointer to a socket address, u32 address length) | s32: bytes sent or negative |
| 4 | receive-from | (int descriptor, pointer to buffer, u32 size, int flags, pointer to a socket address (out), pointer to a u32 address length (in / out)) | s32: bytes received or negative |

The socket address (16 bytes) is as in G07 3.2: u8 length = 16, u8 family = 2, u16 port big-endian, u32 address,
8 zero bytes.

The state object `s` (backend + 0x249d0), fields used here:

| Offset | Type | Meaning |
|---|---|---|
| 832 | pointer to function | "native 8" (natives are pointers at 768 + 8·k): the game's "create a net player for a session member" (3.1). May be null. |
| 1452 | u32 | probe counter: session creates |
| 1456 | u32 | probe counter: backend slot calls |
| 1460 | u32 | probe: last backend slot |
| 1900 | u32 | slot history: number of slot calls recorded |
| 1904 | 32 × u8 | slot history ring |

### 3.5 The room server object (G09's; only what this group touches)

0x8928 bytes, at offset 8 of a binding. Full layout: G09 3.4 and 3.5. This group reads three of its fields
directly (capacity, room id, the listening socket's port field) when it answers a discovery query; everything else
goes through G09's functions.

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | the member id the next connection gets |
| 4 | u32 | capacity |
| 0x10 | u64 | room id |
| 0x18 | socket record | the listening socket (so its port field is at 0x20) |
| 0x24 | u32 | state: 1 = running, 0 = stopped |
| 0x28 | 16 × s32 | addresses of kicked machines |
| 0x68 + j·0x88c | 16 connection slots of 0x88c bytes | j = 0..15; in each: u8 "active" at 0x78, socket record at 0x7c, u32 received count at 0x88 |

### 3.6 Numbers and wire formats

- Discovery (UDP) port 9012; party rooms listen on TCP port 0x2333 (9011), match rooms on 0x2332 (9010); the
  loopback address in socket-address form is 0x0100007f; the broadcast address is 0xffffffff.
- **Room id** (made by the room server start, G09): bits 63..32 = the host's IPv4 address as a number whose most
  significant byte is the first octet (= the socket-address form with its four bytes reversed); bits 31..16 = the
  room's TCP port; bits 15..0 = a running number. 0 = no room.
- **Discovery datagrams**, written and read with the G07 byte stream (every field most significant byte first, no
  padding). PS3 consoles and older helpers use the same layout; it must stay.
  - Query: one byte, 0xc8.
  - Answer: u8 0xc9; u32 number of room records; then per room record (0x8e bytes): u16 TCP port of the room; u64
    room id; u16 number of connections of the room server; u16 capacity; a 0x80-byte block whose first 0x24 bytes
    are the host's name field and whose other bytes are 0.
- **Room messages sent by this group** (TCP, raw little-endian structures, to the room server over the client
  socket):
  - Kick, 0x10 bytes: u32 0xd1 at 0; u32 member id at 4; u64 room id at 8.
  - Member data, 0x50 bytes: u32 0xcd at 0; u32 data length at 4; u64 room id at 8; the data at 0x10 (0x40 bytes
    of room, unused part zero).
  (The join message 0xcf is G07's.)
- Waiting for the room: at most 30 rounds of 100 000 microseconds.

---

# Part A

## 4. Behaviour of part A

Where an order is given it is observable (import calls, game or session calls, other groups' functions, counters)
and must be kept.

### 4.1 `u3x_lnb_note(be, slot)`

The helper's own record of a slot call: eb[67] plus 1; eb[68] = `slot`. When the backend's state object is
non-null: its u32 at 1456 plus 1; its u32 at 1460 = `slot`; with H = its u32 at 1900: the byte at 1904 + (H mod 32)
= the low byte of `slot`, then H plus 1 is stored back. With a null state object only the two eb words change.

### 4.2 `u3x_lnb_bound(be, ss)`

The index (0..3) of the first binding, in ascending order, whose session pointer equals `ss`; −1 when there is
none. Only pointers are compared; nothing is written. (With `ss` null it therefore answers the first free binding.)

### 4.3 `u3x_lnb_ctor(be, vt)`

Called on an object whose 0x249d8 bytes are all zero. For each of the four bindings, in ascending order:
construct its room server (`u3x_lnb_srv_ctor`, G09), reset its client socket record (`u3x_sock_init`), set its
session pointer to null and its received count to 0. Then reset the discovery socket record and store `vt` as the
object's function table. Nothing else is written (name, room list, port field, image base and state pointer keep
their values).

### 4.4 `u3x_lnb_vtable(vt, slot0)`

Fills the 32 entries of `vt` as 3.3 says: entry 0 = `slot0`; entry 1 = `u3x_lnb_s1`, 2 = `u3x_lnb_s2`, 3 =
`u3x_lnb_s3`, 4 = `u3x_lnb_s4`, 5 = `u3x_lnb_s5`, 6 = `u3x_lnb_s6`, 7 = `u3x_lnb_s7`, 9 = `u3x_lnb_s9`, 11 =
`u3x_lnb_s11`, 12 = `u3x_lnb_s12`, 14 = `u3x_lnb_s14`; all the others = `u3x_lnb_s_nop`. After the call no entry
is null. The order of the stores is not observable.

Build constraint (not behaviour): the helper is linked without absolute relocations, so the addresses of the slot
functions have to be produced by code at run time, relative to the instruction pointer; a static array of function
pointers in read-only data is not possible.

### 4.5 `u3x_lnb_s2(be)`: slot 2, Shutdown

1. Note the slot (`u3x_lnb_note(be, 2)`).
2. Close the discovery socket (`u3x_sock_close`); remember its result.
3. For each binding in ascending order whose session pointer is non-null and whose client socket is open
   (`u3x_sock_open`): close the client socket; its result replaces the remembered one.
4. Return the remembered result (s32 widened with its sign): the result of the last close that was done.

Room servers are not stopped here, bindings stay attached, nothing else is written.

### 4.6 `u3x_lnb_s3(be, ss)`: slot 3, AttachSession

1. Note the slot (3).
2. When a binding already has this session (4.2): return its index. Nothing is written.
3. Otherwise take the first free binding (session pointer null) in ascending order: its session pointer becomes
   `ss`, and the backend pointer of the session (session + 8) becomes `be`; return the binding's index.
4. No free binding: return −1.

Consequences of step 2 to keep: an already attached session does not get its backend pointer rewritten; a null
`ss` "is found" in the first free binding and that index is returned without any write (when no binding is free:
−1).

### 4.7 `u3x_lnb_s1(be)`: slot 1, Update

In this order:

1. eb[84] plus 1. (The slot note of 4.1 is **not** made for slot 1.)
2. `u3h_lnb_follow_tick(be, image base, state object)`: both taken from the backend object; the state object may
   be null.
3. Take the datagram buffer: `u3h_lnb_udpbuf()` (0x1000 bytes).
4. **Discovery.** Only when the discovery socket is open (`u3x_sock_open`) and the receive-from import (3.4, k =
   4) is non-null. Repeat, with no limit on the number of rounds:
   - set all 0x1000 bytes of the buffer to 0;
   - receive one datagram: `u3x_sock_recvfrom(discovery socket, buffer, 0xfff, &address, &port)` with `address`
     and `port` both 0 before the call;
   - when the result N is less than 1: the discovery part ends;
   - otherwise read the datagram through a stream over the buffer with position 0 and limit N (so anything read
     past the N received bytes is 0, G07 4.8). Its first byte is the id:
     - **0xc8, a query.** When the sender's address equals the address field of the discovery socket (the own
       broadcast): nothing; go on with the next datagram. Otherwise build the answer of 3.6 in a local buffer of
       0x800 bytes (stream limit 0x800) and send it:
       - A binding is **advertised** when its session pointer is non-null, and the session answers "party
         session" (3.2: `u3h_lnb_call(session, 0)`, low 8 bits non-zero), and its room server is running. The
         three tests are made in this order and stop at the first that fails.
       - First pass over the four bindings in ascending order: count the advertised ones. Write the id byte
         0xc9, then the count as a 4-byte field.
       - Second pass over the four bindings in ascending order, testing "advertised" again (so the session's
         entry 0 is called twice per attached session and query: once in each pass). For each advertised binding
         write one room record: the low 16 bits of the listening socket's port field (2 bytes); the room
         server's room id (8 bytes); `u3x_lnb_srv_count` of the room server (2 bytes); the low 16 bits of the
         room server's capacity (2 bytes); then the 0x80 bytes of the name block (the 0x24 bytes of the backend's
         name field, then zeros), one byte at a time.
       - Send: `u3x_sock_sendto(discovery socket, the answer, its length, the sender's address, the sender's
         port)`; the length is the stream position, limited to the stream limit (5 + 0x8e per record; 0x23d at
         most, so the limit never applies). The result is ignored. The send-to import is not tested for null
         here.
       - eb[74] plus 1 (also when the answer holds no room, and whatever the send returned).
     - **0xc9, an answer.** Read the 4-byte record count C. Then for record number 0, 1, 2, … while the record
       number is below C **and** the stream position is below 0x1000 (the buffer size, not the datagram length):
       read one room record (port, room id, connections, capacity as 2 / 8 / 2 / 2 bytes, then the 0x80 block byte
       by byte). When the list count (s32 at backend + 0x50, signed compare) is 20 or more: the record is read and
       dropped. Otherwise it is stored in the list entry with the index "list count": room id; the sender's
       address (as `u3x_sock_recvfrom` gave it); port, connections and capacity as u16; the first 0x24 bytes of
       the block as the name; bytes 0x36 and 0x37 of the entry are not written. Then the list count plus 1 and
       eb[75] plus 1.
       Consequences to keep: a count field larger than what the datagram holds makes records that lie partly or
       wholly behind the received bytes; they are read as zeros and stored like real ones (room id 0, port 0,
       empty name, the sender's address), up to 29 records per datagram (record k starts at stream position 5 +
       0x8e·k, which must be below 0x1000). Answers are not compared with rooms already in the list (the same room
       can be listed several times until the next Search empties the list). Answers from the own address are
       taken like any other.
     - **Any other id:** eb[83] plus 1; the datagram is dropped.
5. **Bindings.** For each binding in ascending order whose session pointer is non-null: first the room server's
   update (`u3x_lnb_srv_update(binding's room server)`), then the room client's update (`u3h_lnb_cli_update(image
   base, binding)`).
6. Return 0.

Step 5 runs whether or not the discovery socket is open.

### 4.8 `u3x_lnb_s5(be)`: slot 5, Search

1. Note the slot (5).
2. The room list count (s32 at backend + 0x50) becomes 0. The entries themselves are not cleared.
3. When the discovery socket is open and the send-to import (k = 3) is non-null: send the one-byte query (0xc8)
   with `u3x_sock_sendto(discovery socket, the byte, 1, 0xffffffff, low 16 bits of the backend's discovery port
   field)`; then eb[73] plus 1 (whatever the send returned).
4. Return 0.

### 4.9 `u3x_sock_recvfrom` and `u3x_sock_sendto`

Neither tests the record's descriptor or the import for null: the callers do (slot 1's answer excepted, 4.7).
Neither notes an error or closes the socket.

`u3x_sock_recvfrom(s, b, n, ip, port)`: one call of the receive-from import with (the record's descriptor, `b`,
`n`, flags 0, a socket address, a u32 address length). Before the call the socket address is: length 16, family 2,
port = the low 16 bits of the value `*port` holds on entry (big-endian), address 0, the rest 0; the address length
is 16. When the import's result is greater than 0: `*ip` = the address field of the socket address after the call,
`*port` = its port field converted to host order (0..65535). When the result is 0 or negative, `*ip` and `*port`
are left as they were. Returns the import's result.

`u3x_sock_sendto(s, b, n, ip, port)`: one call of the send-to import with (the record's descriptor, `b`, `n`,
flags 0, the socket address of (`ip`, low 16 bits of `port`) as in G07 3.2, address length 16). Returns the
import's result.

### 4.10 `u3x_lan_party_size(base)`

With the party session (image offset 0x17dc298): when its room id is 0, the result is 1, or 2 when a second local
player is present (the u8 at image offset 0x17d7b06 is non-zero, or the s32 at image offset 0x17f2fc0 equals 3).
Otherwise the result is `u3x_party_players(party session)`. Reads only.

---

# Part B

## 5. Behaviour of part B

"The binding" of a session is the one `u3x_lnb_bound(be, ss)` answers (4.2); "note the slot (n)" is
`u3x_lnb_note(be, n)` (4.1); "the wait" and "U3MP_LAN_SOLO is on" are defined in 3.4; "clear the members" is the
game function at image offset 0x7a25f0 called with the session (3.1), with the image base taken from the backend
object; "run Update" is `u3h_lnb_call(be, 1)`: the backend's slot 1 called through the object's table (not a
direct call of `u3x_lnb_s1`), result ignored. "The room id" of a session is always the full u64 at session + 0x18
unless said otherwise.

### 5.1 `u3x_lnb_solo_create(be, ss, maxp)`: a one-member room without a room server

Makes the session look created, with this player as its only member and host:

- eb[82] plus 1; when the backend's state object is non-null, its u32 at 1452 plus 1;
- the session's backend pointer (session + 8) = `be`;
- member slot 0 (session + 0x680): all 0x120 bytes set to 0; then its name = the first characters of the player's
  name, at most 31, stopping at the NUL (the name is `u3h_onp_id(state object)`, or the text `Player` when the
  state object is null; `u3h_onp_id` is not called then); "in use" (0x25) = 1; member id (0x2c) = 1; connection
  id (0x34) = 0;
- the **low 32 bits** of the room id field (u32 at session + 0x18) = 1; the high 32 bits (session + 0x1c) are not
  written;
- own member id (0x12e4) = 1; host member id (0x12e8) = 1; capacity (0x20c) = `maxp`; create result (s32 at 0xa4)
  = 0; "create done" (0xa1) = 1.

Nothing else is touched: the other nine member slots, the session's next-member index, the member data, the
binding, the sockets. No session event is raised and no game function is called.

### 5.2 `u3x_lnb_s4(be, ss, maxp)`: slot 4, Create

1. Note the slot (4); eb[69] plus 1.
2. No binding for `ss`: eb[70] = 0xffffffff; return −1. Nothing else.
3. The room's port: ask the session "party session?" (3.2, one call of `u3h_lnb_call(ss, 0)`): 0x2333 when the
   low 8 bits of the answer are non-zero, else 0x2332.
4. Start the binding's room server: `u3x_lnb_srv_start(room server, 1, port, maxp)`.
5. When that result is negative and U3MP_LAN_SOLO is on: make the solo room (5.1); eb[70] = 0; return 0.
   (When it is negative and U3MP_LAN_SOLO is off, the function goes on with step 6 all the same.)
6. Clear the members (game call). Session capacity (u32 at 0x20c) = `maxp`. The binding's received count = 0.
7. Connect the binding's client socket to the own room server: `u3x_sock_connect(client socket, 0x0100007f, port,
   0, 0)`. The result is ignored. The client socket is not closed first.
8. Run Update once.
9. Send the join message as host: `u3h_lnb_send_join(be, binding, ss, 1, &R)`; eb[92] = R.
10. "Create done" (u8 at 0xa1) = 0.
11. Up to 30 rounds. Each round: the wait of 100 000 microseconds; run Update; then look at "create done":
    - still 0: next round;
    - non-zero: **the room answered.** In this order: "create done" = 1; when the room id is 0, create result (s32
      at 0xa4) = −1 (it is not written otherwise); eb[70] = R; then, only when `ss` is the match session (its
      address equals image offset 0x17d6420) and its room id is not 0, **the party learns the match room**: the
      helper's match room variable (`*u3h_lnb_match_room()`) = the room id; and when the party session (image
      offset 0x17dc298) has a room (room id not 0), this player is the party's host (its own member id equals its
      host member id) and its own member data length (u32 at 0x12f0) is exactly 0x40: send the party's member data
      again with `u3x_lnb_s12(be, party session, party session + 0x12f4, 0x40)` (a direct call; result ignored),
      then room word 24 (ExtState 0xc20) plus 1. Return R (widened with its sign).
12. After 30 rounds without an answer: stop the room server (`u3x_lnb_srv_stop`). Then:
    - U3MP_LAN_SOLO on: close the client socket (`u3x_sock_close`), make the solo room (5.1), eb[70] = 0, return 0;
    - off: eb[70] = 0xffffffff, return −1 (the client socket is left as it is; "create done" stays 0).

Note on the result: on the normal path the slot returns the join message's send result R, which is 0x80 (the
number of bytes sent) when everything worked, or a negative value / −1 when the send failed but the room state
still arrived.

### 5.3 `u3x_lnb_s6(be, ss, room)`: slot 6, Join

1. Note the slot (6); eb[71] plus 1.
2. No binding for `ss`: eb[72] = 0xffffffff; return −1. Nothing else.
3. In this order: close the binding's client socket; stop its room server (`u3x_lnb_srv_stop`); clear the members
   (game call); received count = 0.
4. Connect: `u3x_sock_connect(client socket, address, port, 0, 0)` where the address is bits 63..32 of `room`
   with their four bytes reversed (socket-address form, 3.6) and the port is bits 31..16 of `room`.
5. When the connect result is negative: join result (s32 at 0xbc) = that result; "join done" (u8 at 0xb8) = 1;
   eb[72] = that result; return it (widened with its sign).
6. Run Update once. Send the join message as guest: `u3h_lnb_send_join(be, binding, ss, 0, &R)`. Then the
   session's room id = `room` (u64); "join done" = 0; join result = 0.
7. Wait for the room. Repeat:
   a. when the room id is 0 (the room client dropped the connection, or `room` was 0): join result = −1; go to
      step 8;
   b. when 30 rounds have been done: **timeout**: stop the room server again (`u3x_lnb_srv_stop`); eb[72] =
      0xffffffff; return −1. ("Join done" stays 0, the join result stays 0, the room id stays `room`, the client
      socket stays as it is.)
   c. one round: the wait of 100 000 microseconds; run Update; when "join done" is now non-zero: when the room
      id is 0, join result = −1; go to step 8. Otherwise back to a.
   (So test a comes before every round and once more after the 30th; at most 30 waits and 30 Updates.)
8. "Join done" = 1; eb[72] = R; return R (the send result of the join message, widened with its sign: 0x80 when
   the message went out, also when the join result field says −1).

### 5.4 `u3x_lnb_s7(be, ss)`: slot 7, Leave

1. Note the slot (7).
2. No binding for `ss`: return −1.
3. In this order: when `ss` is the match session (image offset 0x17d6420), the helper's match room variable
   (`*u3h_lnb_match_room()`) = 0 (`u3h_lnb_match_room` is called only then); close the binding's client socket;
   stop its room server; the session's room id = 0 (u64); clear the members (game call).
4. Return 0.

The binding stays attached.

### 5.5 `u3x_lnb_s9(be, ss, id)`: slot 9, Kick

1. Note the slot (9).
2. No binding for `ss`: return −1 (before anything of step 3).
3. The helper's "who asked for a kick" record, in the room words (3.4): word 16 plus 1; word 17 = `id`; then the
   chain of return addresses, at most six, into words 18 to 23: start with this function's own stack frame record
   (the place where the caller's frame pointer was saved on entry; the return address lies one word above it). For
   k = 0 to 5: stop when the frame record address is null or not a multiple of 8; word 18 + k = the low 32 bits of
   (the return address of that frame record minus the image base); the next frame record address is the saved
   frame pointer stored in this one. Words not reached keep their old values. The frame record addresses are
   followed without any other check. (This needs the function to be built with a frame pointer; word 18 is always
   the slot's own return address.)
4. When the session's room id is not 0: send the 0x10-byte kick message of 3.6 (id 0xd1, `id`, the room id) on
   the binding's client socket with `u3x_sock_send(…, 0x10, 1)`; the result is ignored; eb[88] plus 1 (also when
   the send failed or the socket is not open).
5. Return 0.

### 5.6 `u3x_lnb_s11(be, ss, lock)`: slot 11, net players for a solo match

1. Note the slot (11).
2. Return 0 without doing anything more when any of these holds, tested in this order: U3MP_LAN_SOLO is off; the
   backend's state object is null; `lock` is 0; `ss` is null; native 8 of the state object (3.4) is null; the
   pointer at image offset 0x17cb3e0 is non-null (the net players already exist).
3. Otherwise for each of the session's ten member slots in ascending order that is in use: call native 8 with
   (image offset 0x17cb060 as a pointer, `ss`, the member slot, u32 0, u32 0). When it returns a net player: its
   team (s32 at 0x21c) = −1, its "ready" byte (0x365) = 0, eb[93] plus 1. A null result: nothing for that member.
4. Return 0.

### 5.7 `u3x_lnb_s12(be, ss, data, len)`: slot 12, MemberData

1. Note the slot (12).
2. `len` greater than 0x40: return −1. Nothing else.
3. Store the data in the session: own member data length (u32 at 0x12f0) = `len`; `len` bytes copied from `data`
   to session + 0x12f4, in ascending order (`data` may be session + 0x12f4 itself; then the bytes do not change).
   This happens before the binding is looked for.
4. No binding for `ss`: return −1.
5. When the session's room id is not 0: build the 0x50-byte member data message of 3.6 (all bytes 0 first; id
   0xcd; `len`; the room id; `len` bytes of `data` at 0x10); stamp it: `u3h_lnb_rank_stamp(be, message + 0x10,
   len)`; send it on the binding's client socket with `u3x_sock_send(…, 0x50, 1)`, result ignored; eb[89] plus 1
   (also when the send failed).
6. Return 0.

The data stored in the session (step 3) is the caller's, without the stamp; only the message carries the stamp.

## 6. Test scenarios

Kinds of test, as in G07: **[fake]** = old against new on fake memory with recording stubs; **[live]** = needs the
running game.

Common fake set-up: a fake ExtState (`u3h_ext`) with the eb words and room words; a fake backend object of
0x249d8 bytes with a fake state object (or null); fake session objects with recording function tables; a fake
image so that image offsets 0x17d6420, 0x17dc298, 0x17cb060 / 0x17cb3e0, 0x17d7b06 and 0x17f2fc0 are memory and
0x7a25f0 is a recording stub (or a base chosen so that only the used pages are mapped; the two sessions can be the
fake sessions themselves). Compared: results, all memory, counters, and the sequence and arguments of every stub
call.

### 6.1 Part A [fake]

Stubs: the socket imports 1 (close), 3 and 4 with scripted datagrams and recorded arguments (including the bytes
of socket addresses and of sent data); `u3h_lnb_follow_tick`, `u3h_lnb_udpbuf` (a 0x1000-byte array),
`u3h_lnb_cli_update`, `u3x_lnb_srv_update`, `u3x_party_players`; `u3h_lnb_call` either real or a stub that
answers per session; the G07 stream functions and G09's `u3x_lnb_srv_ctor` / `u3x_lnb_srv_count` /
`u3x_lnb_srv_running` real (or stubs that give the same values to old and new).

Slot note and binding:
1. Note with a state object: eb[67] + 1, eb[68] = slot, state 1456 + 1, 1460 = slot, ring byte at (old H mod 32) =
   slot, H + 1; try H = 0, 31, 32, 0xffffffff. With a null state: only eb[67], eb[68].
2. Bound: session in binding 0, in binding 3, in none (−1); null session with binding 1 free → 1; null session
   with no free binding → −1.

Constructor and table:
3. Constructor on a zeroed object with marker values in name, port field, base and state: four bindings with
   session null, received count 0, sockets zero, room servers constructed (member id counter 1, kicked words −1,
   16 slots inactive); table pointer stored; the marker values unchanged. Run it also on an object filled with
   0xff to see exactly which bytes are written.
4. Table fill: 32 non-null entries; entry 0 = the given pointer; entries 1..7, 9, 11, 12, 14 = the named functions;
   entries 8, 10, 13, 15..31 all equal `u3x_lnb_s_nop`.

Shutdown (slot 2):
5. Discovery socket open, bindings 0 and 2 attached with open client sockets, binding 1 attached with a closed
   client socket, binding 3 free but with an "open" socket record: close import called for the discovery socket,
   binding 0, binding 2, in this order; binding 3's record untouched; result = the stub's result for binding 2.
6. Nothing open: no close call, result 0. Only the discovery socket open and the close stub answering −5: result
   −5 (64-bit: sign-extended). Slot note made in every case.

AttachSession (slot 3):
7. First attach → 0 and session + 8 = backend; a second session → 1; the first session again → 0, and its
   backend pointer is not rewritten (set it to another value before the call: it stays); four sessions attached,
   a fifth → −1 with nothing written; null session per scenario 2.

Update (slot 1):
8. Discovery socket closed, or receive-from import null: no receive call; eb[84] + 1; follow tick stub called with
   (backend, base, state); for each attached binding the server update then the client update, in binding order;
   free bindings skipped; result 0; no slot note (eb[67], eb[68] unchanged).
9. Socket open, no datagram (stub answers −1, and in another run 0): one receive call with size 0xfff, flags 0, a
   socket address with port bytes 0 and address 0, length 16; the buffer is all zero afterwards (pre-fill it with
   0xff).
10. A 0xc8 query from another address with no attached session: answer of 5 bytes (c9 00 00 00 00) sent to the
    sender's address and port; eb[74] + 1.
11. A 0xc8 query with: binding 0 = a party-type session with a running server (port field 0x12333, capacity
    0x10004, 3 active connections, a known room id), binding 1 = a match-type session with a running server,
    binding 2 = a party-type session whose server is stopped, binding 3 free: the answer is c9, count 1, then one
    record: 23 33, the room id in 8 big-endian bytes, 00 03, 00 04, the backend's 0x24 name bytes, 0x5c zero
    bytes; total 0x93 bytes; session entry 0 called for bindings 0, 1, 2 and then again for 0, 1, 2. With two
    advertised bindings: two records in binding order.
12. A 0xc8 query whose sender address equals the discovery socket's address field: no answer, no counter; the
    next datagram is still processed.
13. A 0xc9 answer with two full records while the list holds 0 rooms: two entries exactly as 3.3 (sender
    address, u16 fields, name), bytes 0x36..0x37 of each entry unchanged, list count 2, eb[75] + 2.
14. A 0xc9 answer with the list at 19 rooms and three records: one added (count 20), two dropped; eb[75] + 1.
    With the list at 20: nothing added. With the list count at −1 (signed): a record is stored in "entry −1",
    which lies inside the object at backend + 0x20 (old and new must agree), and the count becomes 0.
15. A 0xc9 answer of 5 bytes with count 0: nothing. A 0xc9 answer of 5 bytes with count 0xffffffff and an empty
    list: 20 entries added, all with room id 0, port 0, empty name and the sender's address (29 records are read,
    9 dropped); eb[75] + 20.
16. A 0xc9 answer cut in the middle of the first record: the entry holds the received bytes followed by zeros.
17. A datagram with id 0x00 and one with id 0xff: eb[83] + 1 each, nothing else.
18. Three datagrams in a row (0xc9, unknown, 0xc8) then "nothing": all three handled in order in one call, four
    receive calls.
19. A one-byte 0xc9 datagram (no count field): count reads as 0, nothing added.

Search (slot 5):
20. Socket open: list count 0 (entries unchanged), one send-to call with one byte c8 to address ff ff ff ff and
    the port field's low 16 bits (set the port field to 0x12334: the wire port is 23 34), flags 0, address length
    16; eb[73] + 1 even when the stub answers −1; slot note; result 0.
21. Socket closed, or send-to import null: list count still set to 0, no send, eb[73] unchanged.

Datagram functions:
22. `u3x_sock_recvfrom` with `*port` = 0x1234 on entry: the address template carries 12 34; stub answers 5 with
    address a.b.c.d port 9012: `*ip`, `*port` = that address, 9012; result 5. Stub answers 0 and −1: `*ip`,
    `*port` unchanged; result passed through.
23. `u3x_sock_sendto` with port 0x12334: wire port 23 34, family 2, length 16, 8 zero bytes; result passed
    through.

Party size:
24. Party room id 0: 1; with the u8 non-zero → 2; with the s32 = 3 → 2; with the s32 = 2 and the u8 0 → 1. Room
    id non-zero: the stub's `u3x_party_players` result (try 0, 1, 5).

### 6.2 Part B [fake]

Stubs: `u3x_lnb_note`, `u3x_lnb_bound` (recording, or the real ones of part A); `u3x_lnb_srv_stop`,
`u3x_lnb_srv_start`, `u3x_sock_connect`, `u3x_sock_close`, `u3x_sock_send` (recording the bytes sent),
`u3h_lnb_send_join` (writes a scripted R), `u3h_lnb_rank_stamp` (recording, may change the block),
`u3h_lnb_match_room` (a variable), `u3h_onp_id`; the sleep pointer at eb[10..11] = a recording stub (or null);
the backend's table entry 1 = a scripted Update stub (for example: at its n-th call set "create done" / "join
done" / the room id of a session); the session's table entry 0 scripted; game stub at 0x7a25f0; native 8 = a
scripted stub returning fake net players.

Solo room:
1. State object present, name of 5 characters: slot 0 zeroed then name, in use, id 1, connection id 0; room id
   low half 1 with the high half unchanged (pre-fill 0xffffffff: stays); own / host id 1; capacity; create result
   0; create done 1; backend pointer; eb[82] + 1; state 1452 + 1; slots 1..9 and the next-member index unchanged.
2. Name of 31, 32 and 40 characters: exactly 31 copied, byte 31 and the rest of the name field 0.
3. State object null: name `Player`, `u3h_onp_id` not called, no probe counter.

Create (slot 4):
4. Session not attached: −1, eb[69] + 1, eb[70] = 0xffffffff, slot note, no other call.
5. Party-type session, server start 0, Update stub sets "create done" (and a room id) at its 3rd call: sequence
   = note, session entry 0, server start (arg 1, port 0x2333, maxp), clear members, connect (0x0100007f, 0x2333,
   0, 0), Update, send join (host 1), then (sleep 100000, Update) twice; capacity and received count written
   before the connect; eb[92] = R; "create done" = 1; create result untouched; eb[70] = R; result R (try R = 0x80
   and R = −1 → 0xffffffffffffffff). Match-type session: port 0x2332.
6. Answer with room id 0: create result = −1; no match-room action.
7. `ss` = the match session with a room id: match room variable = room id; party with a room, own id = host id,
   data length 0x40 → `u3x_lnb_s12` effects on the party session (a 0xcd message on the party's binding) and room
   word 24 + 1; each of the three party conditions false in turn → no resend, no room word; `ss` = the party
   session → no match-room action at all.
8. "Create done" set by the Update that runs before the join is sent (step 8 of 5.2): it is cleared again in
   step 10, so the answer is only seen if a later Update sets it.
9. No answer in 30 rounds, U3MP_LAN_SOLO on: exactly 30 sleeps and 31 Updates in all; server stop; client close;
   solo room; eb[70] = 0; result 0. U3MP_LAN_SOLO off: server stop, no close, no solo room, eb[70] = 0xffffffff,
   −1, "create done" 0.
10. Answer exactly at the 30th round: taken as an answer (no solo room).
11. Server start fails (−1), U3MP_LAN_SOLO on: solo room at once, eb[70] = 0, result 0; no clear-members call, no
    connect, no Update. U3MP_LAN_SOLO off: the function goes on as in scenario 5 / 9 (connect, join, wait).
12. Sleep pointer null: 30 Updates without any wait call.

Join (slot 6):
13. Session not attached: −1, eb[71] + 1, eb[72] = 0xffffffff.
14. Room id 0xc0a8010523330007: sequence = note, client close, server stop, clear members, connect with address
    0x0501a8c0 (192.168.1.5 in socket-address form) and port 0x2333; received count 0.
15. Connect fails (−1): join result −1, join done 1, eb[72] = 0xffffffff, result −1; no Update, no join message,
    the session's room id unchanged.
16. Connect ok, Update stub sets "join done" at its 2nd call, room id intact: sequence Update, send join (host
    0), then (sleep, Update) once; room id = `room`, join done 1, join result 0, eb[72] = R, result R.
17. The Update stub sets "join done" and clears the room id (name taken / room gone): join result −1, join done
    1, result R (not −1).
18. The Update stub clears the room id without setting "join done" at round 3: detected at the top of round 4
    (no further sleep): join result −1, join done 1, result R; exactly 3 sleeps.
19. `room` = 0 with a connect stub that answers 0: no wait at all; join result −1; join done 1; result R.
20. No answer: exactly 30 sleeps, 31 Updates; server stop called a second time; eb[72] = 0xffffffff; result −1;
    join done 0, join result 0, room id = `room`; no client close. The room id cleared during the 30th round's
    Update: taken as "room gone" (join result −1, result R), not as a timeout.

Leave (slot 7):
21. Not attached: −1 (only the slot note). Attached party session: client close, server stop, room id 0 (all 8
    bytes), clear members, in this order; match room variable untouched and its accessor not called; result 0.
    Attached match session: the match room variable is set to 0 first.

Kick (slot 9):
22. Not attached: −1; room words unchanged. Attached, room id non-zero: message d1 00 00 00, id, room id sent
    (0x10 bytes, block 1); eb[88] + 1 also when the send stub answers −1; room word 16 + 1, word 17 = id. Room id
    0: no send, eb[88] unchanged, room words 16 / 17 still written; result 0.
23. Room words 18..23: cannot be compared old against new by value (they hold code addresses of the callers, and
    old and new are different functions with different callers). Check instead: word 18 = (the slot's return
    address − base) low 32 bits when the test calls the slot from a known place; the walk stops at a null or
    unaligned frame pointer (run the slot from a caller whose saved frame pointer was set to 0 or to an odd value
    in a small assembly shim) and leaves the remaining words unchanged.

Slot 11:
24. Each of the six conditions of 5.6 step 2 in turn: no native call, result 0, slot note made. In particular
    with a null state object nothing is read from it.
25. All conditions passed, members in slots 0, 3, 9 in use, native answering a player for slots 0 and 9 and null
    for slot 3: three native calls with (base + 0x17cb060, session, slot, 0, 0) in slot order; team −1 and ready
    0 written in two players; eb[93] + 2.

MemberData (slot 12):
26. `len` 0x41: −1; session data unchanged; only the slot note.
27. `len` 0x40, attached, room id non-zero: session length and data stored; message cd 00 00 00, 40 00 00 00,
    room id, data; the stamp stub called with (backend, message + 0x10, 0x40) before the send and its changes are
    in the sent bytes but not in the session's copy; 0x50 bytes sent; eb[89] + 1; result 0.
28. `len` 0x10: 0x10 bytes stored and sent, the rest of the message zero; the stamp stub still called (with
    0x10). `len` 0: length 0 stored, nothing copied, message with length 0 sent.
29. `data` = session + 0x12f4 (the resend case): data unchanged, message carries it.
30. Not attached: the length and data are stored all the same, then −1; no send.
31. Room id 0: stored, no message, no stamp call, eb[89] unchanged, result 0.

### 6.3 What needs the live game [live]

- Discovery between two machines (and between a PC and a console build): Find a Party shows the other machine's
  party, with name, member count and capacity; the game's own reader of the room list accepts the entries.
- Create on one machine (party and match): the room appears, the host is its first member, the lobby goes on;
  the party's guests follow the host into the match room (the member data resend of 5.2 step 11).
- Join from a second machine; join of a full room, of a room that closed (timeout path) and with a name already
  in the room.
- Leave, and leaving by NetShutdown; a second NetInit afterwards.
- Kick of a silent member by the connection monitor; the kicked machine returns to the menu.
- Solo: with no network adapter (or the room ports in use) Create still gives a one-member room, and a solo match
  starts (slot 11 makes the net player).
- The session's table entry 0 really distinguishing party and match (ports 0x2333 / 0x2332).

## 7. Things in the old code that look like bugs or dead code (behaviour to keep)

Part A:

1. Slot 1 makes no slot note (only eb[84]), unlike every other slot.
2. Slot 1 empties the socket in one call without a limit: as long as datagrams keep arriving the frame does not
   end. The buffer is zeroed (0x1000 bytes) before every receive, including the last, fruitless one.
3. Slot 1 tests the receive-from import but not the send-to import before it answers a query.
4. Slot 1 calls the session's entry 0 twice per attached session for every query (count pass and write pass);
   only party-type sessions are advertised; the "members" field is the number of connections, joined or not.
5. Slot 1, answers: the record loop is bounded by the buffer size 0x1000, not by the datagram length, so a large
   count field makes up to 29 records, most of them zero-filled, and up to 20 empty rooms; partial records are
   stored; no test for duplicates; own answers are accepted; the list count is compared signed; bytes 0x36..0x37
   of an entry are never written.
6. Slot 2 does not stop room servers and returns the result of whichever close came last.
7. Slot 3: an attached session does not get its backend pointer rewritten (the helper has `u3x_lan_reattach` for
   the case that the game cleared it); a null session matches a free binding.
8. Slot 5 leaves the old entries in the list (only the count is reset) and counts a broadcast even when the send
   failed.
9. `u3x_sock_recvfrom` puts the caller's `*port` into the address template (it is always 0); neither datagram
   function tests the descriptor or the import, notes an error or closes anything.
10. The constructor relies on its caller having zeroed the object: it does not clear the name, the room list and
    its count, the port field or the receive buffers.
11. `u3x_lan_party_size` without a room does not look at the member slots at all.

Part B:

12. Solo room: only the low half of the room id is written; member slot 0 gets connection id 0 (not −1) and
    index 0 while the session's next-member index is not advanced; the other member slots are not cleared; the name
    is cut at 31 characters although the field holds 0x24 bytes.
13. Create: the client socket is not closed before the connect (a descriptor still open in the record is lost);
    the connect result is ignored; with U3MP_LAN_SOLO off a failed server start does not end the function; the
    result on success is the join message's send result (0x80), not 0, and a failed send is not treated as a
    failure; the create result field is only written in the "room id 0" case; "create done" set by the first
    Update (before the join is sent) is thrown away; on a timeout with U3MP_LAN_SOLO off the client socket stays
    open; with a null sleep pointer the 30 rounds run without any pause.
14. Create: the resend of the party's member data uses room word 24, which the party-follow logic also counts in.
15. Join: the result on success is the send result (0x80), also when the join result field is −1; on a timeout
    the room id stays set, "join done" stays 0, the client socket stays open and the (already stopped) room
    server is stopped a second time; a `room` of 0 with a successful connect ends at once with join result −1.
16. Leave keeps the binding and does not touch "create done" / "join done".
17. Kick: room words 16 to 23 are also written by the party-follow logic (as counters and a packed value), so
    the two overwrite each other; the frame walk follows saved frame pointers with only a null and an alignment
    test; eb[88] counts kicks whose send failed; the fourth argument the game passes is ignored.
18. Slot 11 does nothing with U3MP_LAN_SOLO off; it does not look at the result of earlier calls and creates a
    player for every member in use, also for members that already have one, as long as the pointer at image offset
    0x17cb3e0 is null.
19. MemberData stores the data in the session before it finds out that the session is not attached; a length
    above 0x40 is refused with −1 (PS3 traps); the stamp function is called for every length (it acts only on
    0x40); eb[89] counts messages whose send failed.

Outside the group, noticed while reading: the upper half of the sleep pointer (eb[11]) is also written by a
diagnostic of another part of the C code (the record of a refused material texture). After that diagnostic has
fired, "the wait" would call a wrong address. Not to be changed here; see "Open questions" 3.

## 8. What stays as it is

- The exported names and signatures of 2.1 and 2.2 (the game calls the slots through the table; the C part, G08
  and other groups call the rest), and the neighbours of 2.3.
- The backend object, the binding and the room list entry (3.3): asserted by the C part, embedded socket records
  and room servers, and the game reads the room list itself.
- The function table's shape (3.3): 32 entries, the slot numbers, the do-nothing slot in every unused entry
  (NetInit calls entry 8).
- The wire formats and numbers of 3.6: the big-endian discovery datagrams (PS3 consoles and installed helpers
  read them), the 0xd1 and 0xcd messages (the room server of G09 and installed helpers read them), the ports, the
  room id layout, the 30 × 100 ms waits.
- The session offsets of 3.2 and the game function 0x7a25f0: the game's own code works on the same fields.
- The eb words and room words of 3.4 and the probe / history fields of the state object: the boot log and the
  test rigs read them.
- The sleep through the pointer at eb[10..11] (the platform layer puts it there) and the use of native 8 through
  the state object.
- `u3x_lnb_s14`, `u3x_lnb_s_nop`, `u3x_lan_find_party`, `u3x_lan_reattach`: not part of this work.
- Everything in section 7.

## 9. Open questions

1. `u3x_sock_recvfrom` and `u3x_sock_sendto` belong to the socket class (G07 block A) by their place in the file,
   but G07's document leaves them out and they are in no work group; they are described here because this group
   is their only caller. The integrator decides in which part file they land.
2. Kick's return-address chain (5.5 step 3) cannot be compared old against new by value and reads the stack
   without validation. It is the helper's own diagnostic. Keep as described unless the owner decides to drop it.
3. The sleep pointer shares eb[11] with another diagnostic (section 7, last paragraph). To be looked at by the
   integrator; not a matter for the implementer of this group.
4. "Entry 0 of a session's table answers non-zero for the party session" is taken from the old code's notes and
   from the two port numbers; it was not checked against the game here. The new code only needs the call and the
   test of the low 8 bits.
5. `u3x_lnb_note` is the same note G07 describes inside slot 0 (G07 "Open questions" 2): the integrator may let
   slot 0 call `u3x_lnb_note(be, 0)`.
6. `u3x_lnb_vtable` has to take function addresses in code (4.4). If the implementer's rules do not allow the
   construct needed for that, the old table fill can stay: it is the helper's own plumbing, not game code.

## 10. Clarifications (tester, 2026-11-14)

Facts confirmed by the old-against-new test (`reimpl2\G10\test_g10.cpp`, results in `reimpl2\G10\REPORT.md`). Points
1 to 3 correct or complete sections 4 and 6; the others answer questions the text left open. Every statement of the
scenarios in 6.1 and 6.2 was checked against the old code and holds, except the one corrected in point 3.

Part A

1. **Update, the answer to a query (4.7): where the id byte is written.** The id byte 0xc9 is written into the answer
   (one `u3x_stream_put` of 1 byte) before the first pass over the bindings, not after it. The order of the calls is:
   the id byte; the first pass (the session's entry 0 and `u3x_lnb_srv_running` per binding); the 4-byte count; the
   second pass with the records. The answer's bytes are the same either way, and the stream writes go to a buffer
   only the function itself sees: the difference shows only in the order of the calls.
2. **Search (4.8): the query byte goes through the byte stream.** The query is made with one call of
   `u3x_stream_put` (value 0xc8, 1 byte) on a stream over a local buffer of 0x10 bytes (position 0, limit 0x10). The
   call is made on every Search, after the list count is set to 0 and before the socket and import are tested, also
   when nothing is sent. What is sent is that buffer, with the stream position (1) as its length.
3. **Update, an answer with the list count at −1 (4.7, scenario 14): the count does not become 0.** "Entry −1"
   (backend + 0x20 to 0x57) covers the count field: bytes 0x1e to 0x21 of the entry's name are stored on backend +
   0x50 to 0x53. The count is read again from the object after the entry has been stored, and that value plus 1 is
   stored. So after a record is stored with the count at −1, the count is (the s32 formed by bytes 0x1e..0x21 of the
   record's name block, least significant byte first) + 1: 1 when those four bytes are zero (a name shorter than
   0x1e characters), not 0. For every other count the entry does not touch the count field, and "read again, plus
   1" and "the count plus 1" are the same. The old code built with the helper's own flags does the same (it
   increments the field in memory after the entry's stores). In normal use the count is 0 to 20 (zeroed object,
   Search, the limit of 20).
4. **Update (4.7): when the guards are tested.** The discovery socket (`u3x_sock_open`) and the receive-from import
   are tested once per Update, before the first receive, never again inside the loop: when the descriptor field
   becomes 0 while datagrams are handled, the loop goes on receiving with descriptor 0 until a receive answers less
   than 1. The import pointer itself is read from the table again for every receive (inside `u3x_sock_recvfrom`).
   With the socket closed or the import null, `u3h_lnb_udpbuf()` is still called once and the buffer is not written.
5. **Update (4.7): the two passes of an answer are independent.** When the session's entry 0 changes what a binding
   answers between the passes (a room server stopped, a session detached), the count field holds the number of the
   first pass and the records are those of the second.
6. **Negative list counts other than −1.** A count of −2 stores the record at backend − 0x18 (its name ends on the
   object's table pointer), −3 at backend − 0x50, and so on, each time with the count plus 1 afterwards; old and new
   agree. Not reachable in normal use.

Part B

7. **Slot 11 (5.6): the conditions are tested once.** All six conditions of step 2 are tested once, before the first
   member; native 8 is read once and that pointer is used for every member. Nothing is tested again between
   members: when a native call turns U3MP_LAN_SOLO off, makes the pointer at image offset 0x17cb3e0 non-null or
   replaces / clears native 8 in the state object, the remaining members still get their call to the first pointer.
   A member's "in use" byte is read when its turn comes (a byte cleared by an earlier native call skips that
   member).
8. **Create (5.2): what is read when.** U3MP_LAN_SOLO is read at each of its two tests (after the server start, and
   again after the 30 rounds): a switch turned off during the wait gives the "off" ending (−1, no solo room). The
   sleep pointer is read again in every round (set to null during the wait: no further wait calls, the Updates go
   on). "Create done" and "join done" count as set for every non-zero value.
9. **Create and Join: the join message's result.** `u3h_lnb_send_join` writes `*res` on every call; the old code
   gives R no value of its own before the call.
10. **Kick (5.5): the frame record.** Built with the helper's flags (CONVENTIONS.h), old and new both start with the
    standard frame (saved frame pointer pushed first, frame pointer = its address), read word 18 from one word above
    it and follow the saved pointers as 5.5 says. On the Windows host of the test the compiler's frame address is
    not such a record, so the test hands both sides the same fake chain of records instead: null and unaligned
    starts, chains of 1 to 8 records, a loop; the words written, their places and their values agree, and words not
    reached keep their values.

Both parts (added by the tester that finished the run, 2026-11-14)

11. **The image base field.** The backend's image base (object + 0x249c8) is written only by the backend setup, always
    with the same value, and never during a slot call. Update, Create and Join read it once at the start of the call
    and use that value to the end (Update for every binding's room client update; Create and Join for "clear the
    members" and for the addresses of the two sessions). Reading it at each use gives the same result in every real
    run; a difference could only show if a function called during the slot rewrote the field, and none does. It is
    not counted as a difference.
12. **Join (5.3): R before the join message.** In Join the variable that becomes R holds the connect result (0 or
    more) until `u3h_lnb_send_join` replaces it; in Create it has no value before that call. Because the call always
    writes `*res` (point 9), no starting value is observable.
13. **The three differences found** (behaviour reports in the tester's report): points 1 and 2 (the order / number of
    `u3x_stream_put` calls in Update's answer and in Search) and point 3 (the list count after a record stored with
    the count at −1). Everything else in sections 4 and 5 was found equal, at -O1 and -O2.
14. **Table fill (4.4), build note.** Built alone with the helper's flags, a part that only declares `u3x_lnb_s14`
    and `u3x_lnb_s_nop` reaches their addresses through the global offset table; with the two functions defined in
    the same file (as in `u3x_fixes.cpp`) every address in the table fill is taken relative to the instruction
    pointer, as 4.4 requires.
