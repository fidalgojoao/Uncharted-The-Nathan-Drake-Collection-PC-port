# G07. LAN backend: socket class, byte stream, connection table, member slots

Behaviour description for a reimplementation (reimpl2 pipeline, describer's document). It says what the functions
do, not how the old ones were written. All offsets are hexadecimal when prefixed with 0x. "Image offset X" means the
address `base + X`, where `base` is the game image base. All multi-byte values in memory are little-endian unless a
section says otherwise; "u8 / u16 / u32 / u64 / s8 / s16 / s32 / s64" are unsigned / signed integers of that width,
"pointer" is 8 bytes. No function of this group uses floating point.

The group has 18 functions in five blocks:

| Block | Functions (old names) |
|---|---|
| A. socket class | `u3x_sock_udp`, `u3x_sock_listen`, `u3x_sock_connect`, `u3x_sock_poll`, `u3x_sock_recv`, `u3x_sock_adopt` |
| B. byte stream | `u3x_stream_put`, `u3x_stream_get` |
| C. connection table | C: `lnb_cm_reset_entry`, `lnb_cm_init_lan`, `lnb_cm_get`, `lnb_cm_release`, `lnb_cm_shutdown` |
| D. session member slots | `u3x_add_member`, `u3x_remove_member` |
| E. join message and backend slot 0 | C: `lnb_rank_stamp`, `lnb_send_join`, `lnb_s0` |

## 1. Purpose and callers

The LAN backend is the helper's replacement for the session backend the PS4 image lost: it finds rooms with UDP
broadcasts, runs a small TCP "room server" on the host and a TCP client on every member, and keeps the game's own
session and connection structures filled so that the game's session code works on top of it. This group is the
bottom layer of that backend.

**A. Socket class.** A socket is a 12-byte record (3.3) over the platform's socket imports (3.2).

- `u3x_sock_udp`: opens the backend's discovery socket. Only caller: backend slot 0 (`lnb_s0`, block E), through
  the C wrapper `lnb_udp_open`.
- `u3x_sock_listen`: opens a listening TCP socket. Only caller: the room server start (`u3x_lnb_srv_start`, group
  G09) for the room ports; C wrapper `lnb_listen` (no other user).
- `u3x_sock_connect`: connects a TCP client socket. Callers: backend slot 4 "Create" (connects to the own room
  server through the loopback address; result ignored) and slot 6 "Join" (connects to the address held in the room
  id), both group G10, both with `nodelay` = 0 and `nonblocking` = 0; C wrapper `lnb_connect` (no other user).
- `u3x_sock_poll` and `u3x_sock_recv`: "is there something to read" and "read it". Callers: the room client update
  (`u3x_lan_client_update`, G08) on the binding's client socket, and the room server update (`u3x_lnb_srv_update`,
  G09) on each client slot's socket. Both always call poll first and call recv only when poll answered 1, always
  with `block` = 1.
- `u3x_sock_adopt`: turns an accepted descriptor into a socket record. Only caller: the room server update (G09),
  right after an accept.

**B. Byte stream.** The discovery datagrams (ids 0xc8 "who has rooms" and 0xc9 "these rooms") are written and
read field by field through a small stream object (3.4). Callers: backend slot 1 "Update" and slot 5 "Search"
(G10), with field widths 1, 2, 4 and 8 only; C wrappers `lnb_bput` / `lnb_bget` (no other user).

**C. Connection table.** The game keeps one table of 32 connection entries (one per remote player, shared by party
and match session, reference counted) at a fixed place in its image (3.5). The PS4 image still reads this table
(the transport, the session leave code) but no longer fills it for LAN; these functions do.

- `lnb_cm_init_lan`: at NetInit of the LAN backend (`u3x_lan_netinit`, G08, through the export `u3h_cm_init_lan`),
  after the game sockets are open.
- `lnb_cm_reset_entry`: by `lnb_cm_init_lan`, by `lnb_cm_get`, and by the online twin of the init
  (`u3h_cm_online_reset`, another group, still in C: it resets the 32 entries the same way).
- `lnb_cm_get`: by `u3x_add_member` (block D), through the export `u3h_cm_get`. No other caller.
- `lnb_cm_release`: by `u3x_remove_member` (block D), through the export `u3h_cm_release`. No other caller. (The
  game's own session leave code releases entries itself, inline.)
- `lnb_cm_shutdown`: at NetShutdown (`u3x_lan_shutdown`, G08, through the export `u3h_cm_shutdown`), last step.

**D. Session member slots.** A session (party or match) has 10 member slots (3.6).

- `u3x_add_member`: when a room message says a member is in the room: the LAN room client (G08: reply to the
  join, "member joined" message 0xca) and the online backend (other group: room joined, member joined). C wrapper
  `lnb_add_member`.
- `u3x_remove_member`: when a room message says a member left: LAN room client message 0xcb (G08), online member
  left (other group; there the `name` argument points at the member slot itself). C wrapper `lnb_remove_member`.

**E. Join message and slot 0.**

- `lnb_send_join`: builds and sends the room join message (id 0xcf). Callers: backend slot 4 "Create" (host = 1)
  and slot 6 "Join" (host = 0), G10, through the export `u3h_lnb_send_join`, right after the client socket was
  connected.
- `lnb_rank_stamp`: writes the helper's own extra fields into the last 16 bytes of a 0x40-byte member data block
  before it is sent. Callers: `lnb_send_join` and backend slot 12 "MemberData" (G10, through the export
  `u3h_lnb_rank_stamp`).
- `lnb_s0`: slot 0 ("Init") of the backend object's function table. The C part takes its address and gives it to
  the table fill (`u3x_lnb_vtable(table, slot0)`); NetInit (`u3x_lan_netinit`, G08) calls it once through the
  table, right after the backend object was built, and treats a non-zero result as "Init failed".

## 2. Interface

### 2.1 Functions that keep their exported name and signature

All `extern "C"`. The C wrappers named in section 1 pass their arguments unchanged.

| Function | Signature | Result |
|---|---|---|
| UDP open | `int32_t u3x_sock_udp(void *s, uint32_t port, int broadcast, int32_t receive_buffer)` | 0 open, −1 failed |
| TCP listen | `int32_t u3x_sock_listen(void *s, uint32_t port)` | 0 listening, −1 failed |
| TCP connect | `int32_t u3x_sock_connect(void *s, uint32_t ip, uint32_t port, int nodelay, int nonblocking)` | 0 connected (or connecting), −1 failed |
| readable? | `int32_t u3x_sock_poll(const void *s)` | 1 readable (data or end of stream), 0 nothing, −1 broken / not open |
| receive | `int32_t u3x_sock_recv(void *s, void *b, uint32_t n, int block)` | bytes received (0 = peer closed), −1 = error (socket closed), or a raw negative "try again" code (4.5) |
| adopt | `void u3x_sock_adopt(void *s, int32_t fd, uint32_t ip, uint32_t port)` | none |
| stream write | `void u3x_stream_put(void *w, uint64_t v, int bytes)` | none |
| stream read | `uint64_t u3x_stream_get(void *r, int bytes)` | the value read |
| add member | `int32_t u3x_add_member(uintptr_t base, uint8_t *session, const void *info, int you, int host)` | the member's index, or −1 when all 10 slots are in use |
| remove member | `int32_t u3x_remove_member(uintptr_t base, uint8_t *session, const char *name)` | 0 removed, −1 no such member |

Parameters: `s` a socket record (3.3); `port` a port number in host order (only its low 16 bits go on the wire);
`ip` an IPv4 address in the form a socket address holds it (3.2: 127.0.0.1 is the u32 0x0100007f); `broadcast`,
`nodelay`, `nonblocking`, `block`, `you`, `host` are truth values (0 / non-zero); `receive_buffer` a buffer size or 0;
`b`, `n` a receive buffer and its size; `fd` a descriptor returned by an accept; `w` / `r` a stream object (3.4);
`v` the value to write; `bytes` the field width (1 to 8); `session` a session object (3.6); `info` a member
description (3.7); `name` a player name (3.1).

### 2.2 Functions still in C: proposed new names

| Old C function | Proposed `extern "C"` signature | The one-line C wrapper passes |
|---|---|---|
| `lnb_cm_reset_entry(e)` | `void u3x_cm_reset_entry(uint8_t *entry)` | the entry pointer |
| `lnb_cm_init_lan(base)` | `void u3x_cm_init_lan(uintptr_t base)` | base |
| `lnb_cm_get(base, name)` | `int32_t u3x_cm_get(uintptr_t base, const char *name)` | base, name |
| `lnb_cm_release(base, id)` | `int32_t u3x_cm_release(uintptr_t base, int32_t id)` | base, id |
| `lnb_cm_shutdown(base)` | `void u3x_cm_shutdown(uintptr_t base)` | base |
| `lnb_rank_stamp(be, md, len)` | `void u3x_lnb_rank_stamp(void *be, uint8_t *md, uint32_t len)` | backend object, data block, its length |
| `lnb_send_join(be, b, ss, host, res)` | `void u3x_lnb_send_join(void *be, void *bind, uint8_t *ss, int host, int32_t *res)` | backend object, binding, session, host flag, result pointer |
| `lnb_s0(be)` | `int64_t u3x_lnb_s0(void *be)` | the backend object. The C part must keep giving a slot-0 function with this behaviour to the table fill: either `lnb_s0` stays as a one-line forwarder whose address is taken as today, or the address of `u3x_lnb_s0` is taken instead. |

Results: `u3x_cm_get` a connection id or −1; `u3x_cm_release` 0 or −1; `u3x_lnb_s0` the UDP open result (0 or −1)
widened to 64 bits; `u3x_lnb_send_join` stores the send result in `*res`.

The C exports `u3h_cm_get`, `u3h_cm_release`, `u3h_cm_init_lan`, `u3h_cm_shutdown`, `u3h_lnb_send_join`,
`u3h_lnb_rank_stamp` exist for the C++ callers of today; they can stay as forwarders. Inside this group
`u3x_add_member` / `u3x_remove_member` may call `u3x_cm_get` / `u3x_cm_release` directly: the effects are the same.

### 2.3 Functions of the C part and of other groups that the new code calls

Not to be described or reimplemented here. "Exists" = exported today.

| Name | Signature | What it does |
|---|---|---|
| `u3h_ext` | `uint8_t *u3h_ext(void)` | address of the helper's own state block ("ExtState", 3.2). Exists. |
| `u3h_net_pick` | `void u3h_net_pick(void)` | decides once which platform fills the socket import table; must run before the first socket call of a function, as stated per function below. Exists. |
| `u3x_sock_error` | `int32_t u3x_sock_error(int32_t r)` | stores `r` in the diagnostic word "last socket error" (ExtState 0x41bc) and returns `r`. Exists. |
| `u3x_sock_close` | `int32_t u3x_sock_close(void *s)` | when the record's descriptor is > 0 and the close import is present: calls the close import with the descriptor; in every case sets the descriptor field to 0 (address and port fields untouched). Exists. |
| `u3x_sock_setopt` | `int32_t u3x_sock_setopt(int fd, int level, int opt, int32_t v)` | one call of the set-option import with a 4-byte value; returns its result (negative = error). Exists. |
| `u3x_sock_bind` | `int32_t u3x_sock_bind(int fd, uint32_t port)` | one call of the bind import with the "any" address and `port`; returns its result. Exists. |
| `u3x_sock_local_ip` | `uint32_t u3x_sock_local_ip(void)` | this machine's own IPv4 address (in socket-address form), or 0: calls `u3h_net_pick`, then the network-information import. Exists. |
| `u3x_sock_local` | `void u3x_sock_local(void *s, uint32_t port)` | sets the record's address to `u3x_sock_local_ip()` and its port to `port`. Exists. |
| `u3x_sock_send` | `int32_t u3x_sock_send(const void *s, const void *b, uint32_t n, int block)` | −1 when the send import is missing or the record's descriptor is not > 0; else one call of the send import (waiting when `block` is non-zero) and its result. Exists. |
| `u3x_party_players` | `int u3x_party_players(const uint8_t *session)` | number of players in a session that has a room (members in use, a member with a second local player counts twice). Exists. |
| `u3x_member_status` | `uint8_t u3x_member_status(int custom_game, int guest_waiting, int host_playing)` | the helper's member status byte from three truth values. Exists. |
| `u3x_member_start` | `uint8_t u3x_member_start(int at_start, uint64_t followed, uint64_t room)` | the helper's "at the start" byte. Exists. |
| `u3h_player_profile` | `uint8_t *u3h_player_profile(uintptr_t base)` | the active local user's profile object, or null. Exists. |
| `u3h_lnb_match_room` | `uint64_t *u3h_lnb_match_room(void)` | address of the helper's "match room id" variable. Exists. |
| `u3h_lnb_marks` | `void u3h_lnb_marks(uint8_t *guest_wait, uint8_t *host_playing, uint8_t *at_start, uint64_t *followed)` | copies four of the helper's party-follow variables to the given places. Exists. |
| `u3h_ng_stamp` | `void u3h_ng_stamp(uintptr_t base, void *s, uint8_t *md)` | the helper's late-join / map-name stamp: may set more bits in byte 0x32 and change bits 16..27 of the u32 at 0x3c of the data block. Exists. |
| `u3h_gt_entry_reset` | `void u3h_gt_entry_reset(uint8_t *entry)` | clears the transport's three timers of a connection entry (group "game transport"; does nothing while the transport does not know the table). **New export to add** (one line around the C function `gt_entry_reset`). |
| `u3h_onb_cm_np` | `void u3h_onb_cm_np(uintptr_t base, uint8_t *entry, const char *name)` | online only: fills the entry's address, port and handle from the peer address the account server gave, or starts the peer link. **New export to add** (one line around the C function `onb_cm_np`). |
| `u3h_pl_drop` | `void u3h_pl_drop(const char *name)` | online only: tells the peer link that the connection to `name` is no longer needed; does nothing while no peer link is installed. **New export to add** (one line: call the C part's `pl_drop_fn` when it is set). |

If the integrator prefers not to add the three new exports, the alternative with the same effects is that the C
wrappers keep those calls: see "Open questions".

## 3. Game and platform facts

### 3.1 Player names

A player name is a field of 0x24 bytes holding a NUL-terminated text. **Name equality** (the rule the game uses on
these fields, used everywhere below): compare byte by byte from the start for at most 0x24 bytes; the names differ
at the first position where the bytes differ; they are equal when a NUL is reached at the same position in both
(what follows the NUL is not looked at) or when all 0x24 bytes are equal. An empty name equals an empty name.
**Name copies** below always copy all 0x24 bytes of the source field, whatever follows the NUL: every name source
is a 0x24-byte field.

### 3.2 The helper's state block (ExtState, address from `u3h_ext()`)

| Offset | Type | Meaning |
|---|---|---|
| 0x200 | 12 pointers | the socket import table, entry k at 0x200 + 8·k (below). An entry may be null (import not available). |
| 0x4050 | u32 array "eb" | diagnostic words; eb[k] is at 0x4050 + 4·k |
| 0x415c | u32 | eb[67]: backend slot calls (counter) |
| 0x4160 | u32 | eb[68]: last backend slot called |
| 0x4190 | u32 | eb[80]: members added (counter) |
| 0x4194 | u32 | eb[81]: members removed (counter) |
| 0x41a8 | u32 | eb[86]: connection entries created (counter) |
| 0x41bc | u32 | eb[91]: last socket error (written by `u3x_sock_error`) |

Socket import table (the platform's socket API; all functions return s32, negative = error; arguments in the
normal calling convention of the helper):

| k | Import | Arguments |
|---|---|---|
| 0 | socket | (pointer to a NUL-terminated debug name, int family, int type, int protocol) → descriptor |
| 1 | close | (int descriptor) |
| 2 | bind | (int descriptor, pointer to socket address, u32 address length) |
| 5 | set option | (int descriptor, int level, int option, pointer to value, u32 value length) |
| 6 | network information | (int code, pointer to output) |
| 7 | connect | (int descriptor, pointer to socket address, u32 address length) |
| 8 | listen | (int descriptor, int backlog) |
| 9 | accept | not used by this group |
| 10 | send | (int descriptor, pointer, u32 length, int flags) |
| 11 | receive | (int descriptor, pointer, u32 length, int flags) → bytes received, 0 = end of stream |
| 3, 4 | send-to, receive-from | not used by this group |

Constants of that API used here:

| What | Value |
|---|---|
| family IPv4 | 2 |
| type stream (TCP) / datagram (UDP) | 1 / 2 |
| protocol | 0 |
| option level "socket" | 0xffff |
| option "non-blocking" | 0x1200 |
| option "broadcast" | 0x20 |
| option "reuse address" | 4 |
| option used for the `receive_buffer` argument | 0x1001 (see section 6) |
| option "receive timeout" / "send timeout" | 0x1006 / 0x1005, value in microseconds |
| option level TCP, option "no delay" | 6, 1 |
| receive flag "do not wait" | 0x80 |
| receive flag "peek" | 2 |
| listen backlog | 12 |
| error code = low 8 bits of a negative result | 35 try again, 36 in progress, 37 already in progress, 56 is connected, 57 not connected |

Socket address (16 bytes): u8 length = 16; u8 family = 2; u16 port in **big-endian** (network) byte order, taken
from the low 16 bits of the port number; u32 IPv4 address, bytes in network order (so the u32 read little-endian
has the first octet in its lowest byte: 127.0.0.1 = 0x0100007f); 8 zero bytes. The address length argument is 16.

Debug names given to the socket import (the helper's own texts): `u3mp-lan` for the UDP socket, `u3mp-room` for
both kinds of TCP socket.

### 3.3 The socket record (12 bytes)

| Offset | Type | Meaning |
|---|---|---|
| 0 | s32 | descriptor; the record is "open" when it is > 0; 0 = closed |
| 4 | u32 | IPv4 address in socket-address form: the own address for a UDP or listening socket, the peer for a connected or adopted one |
| 8 | u32 | port, host order |

The layout is shared with the room server, the room client and the backend object (which embed such records) and
must stay.

### 3.4 The stream object (16 bytes)

| Offset | Type | Meaning |
|---|---|---|
| 0 | pointer | the byte buffer |
| 8 | u32 | position: index of the next byte |
| 0xc | u32 | limit: number of usable bytes in the buffer |

The callers build it on their stack and read the position and limit back themselves. Wire format: a field of N
bytes is the value's N low-order bytes, **most significant byte first** (big-endian), with no padding between
fields. This is the layout PS3 consoles use for the same datagrams and must stay.

### 3.5 The connection table (game data at image offset 0x17f31b0)

Offsets relative to image offset 0x17f31b0 ("table"):

| Offset | Type | Meaning |
|---|---|---|
| 0 | s32 | the next connection id to give out |
| 8 + i·0x220 | 32 entries of 0x220 bytes | connection entry i (i = 0..31), layout below |
| 0x4408 | u32 | number of blocked names (always 0 on LAN) |
| 0x440c + i·0x24 | names of 0x24 bytes | the blocked names |
| 0x488c | u32 | number of LAN address records stored (written by another group; not read here) |
| 0x4890 + k·0x28 | 32 records of 0x28 bytes | LAN address records: name (0x24 bytes) at +0, u32 IPv4 address (socket-address form) at +0x24 |
| 0x4d90 | name, 0x24 bytes | this machine's player name |
| 0x4db4 | u8 | mode flag: 1 = LAN, 0 = online |

Connection entry:

| Offset | Type | Meaning |
|---|---|---|
| 0 | s32 | connection id |
| 4 | u8 | in use |
| 5, 6 | u8, u8 | two state bytes (only cleared here) |
| 7 | name, 0x24 bytes | the remote player's name (ends at +0x2a) |
| 0x2c | u32 | peer IPv4 address, socket-address form |
| 0x30 | u16 | peer port, host order |
| 0x34 | u32 | handle / "address known" (1 = set) |
| 0x38 | s32 | reference count |
| 0x40 | object | the entry's packet object |

Other game facts of this block:

| Image offset | Kind | Meaning |
|---|---|---|
| 0x6aa150 | game function | resets a packet object. One argument: pointer to the packet object (entry + 0x40). Result unused. |
| 0x17d6010 | name, 0x24 bytes | this machine's player name as the net code holds it ("own name") |

The game port of the peer-to-peer transport is 3233 (0xca1): the port stored for a LAN peer.

### 3.6 The session object

The first 8 bytes of a session object point to its table of function pointers (entry k at 8·k). Two entries are
called here, both with (session pointer, member slot pointer), result unused: entry 3 = "member joined", entry 4 =
"member left".

| Offset | Type | Meaning |
|---|---|---|
| 8 | pointer | the session's backend object, or null. Its first 8 bytes point to a table of function pointers; entry 9 is "Kick", called with (backend pointer, session pointer, u32 member id, u32 0), result unused. |
| 0x18 | u64 | room id; 0 = the session has no room |
| 0x20c | u32 | capacity (number of players the session may hold); 0 = not set |
| 0x680 + i·0x120 | 10 member slots of 0x120 bytes | i = 0..9, layout below |
| 0x12e0 | s32 | the index the next added member gets |
| 0x12e4 | u32 | member id of this machine's player |
| 0x12e8 | u32 | member id of the host |
| 0x12f0 | u32 | length of this machine's member data |
| 0x12f4 | 0x40 bytes | this machine's member data |

Member slot:

| Offset | Type | Meaning |
|---|---|---|
| 0 | name, 0x24 bytes | the member's name |
| 0x25 | u8 | in use |
| 0x28 | s32 | member index |
| 0x2c | u32 | member id |
| 0x30 | u32 | NAT type |
| 0x34 | s32 | connection id (block C), −1 = none |
| 0x3c | u32 | member data length (0..0x40) |
| 0x40 | 0x40 bytes | member data |

Split-screen facts (used when the session has no room): "a second local player is present" is true when the u8 at
image offset 0x17d7b06 is non-zero or the s32 at image offset 0x17f2fc0 equals 3.

### 3.7 The member description (`info` of `u3x_add_member`, 0x28 bytes, built by the callers)

| Offset | Type | Meaning |
|---|---|---|
| 0 | pointer | the name (a 0x24-byte name field) |
| 8 | u32 | member id |
| 0xc | u32 | NAT type |
| 0x10 | u32 | member data length |
| 0x18 | pointer | member data |
| 0x20 | u32 | 1 = the description carries member data |

### 3.8 The backend object, the binding and the helper state

Backend object (the helper's own class, 0x249d8 bytes; only these fields are used here):

| Offset | Type | Meaning |
|---|---|---|
| 0x249b8 | u32 | the discovery port in use |
| 0x249bc | socket record | the discovery (UDP) socket |
| 0x249c8 | u64 | the game image base |
| 0x249d0 | pointer | the helper's state object `s`, or null |

Binding (one of the backend's four session bindings, 0x9140 bytes): the client socket record is at offset 0x8930.

State object `s`:

| Offset | Type | Meaning |
|---|---|---|
| 936 | pointer to function | "native 21" (natives are pointers at 768 + 8·k): the game's "rank word of a player profile". One argument: the profile object pointer; returns u32. May be null. |
| 1456 | u32 | probe counter: backend slot calls |
| 1460 | u32 | probe: last backend slot |
| 1900 | u32 | slot history: number of slot calls recorded |
| 1904 | 32 × u8 | slot history ring |

Game data read by block E:

| Image offset | Type | Meaning |
|---|---|---|
| 0x17d5c90 | u32 | this machine's NAT type |
| 0x17d6010 | name, 0x24 bytes | own name (as in 3.5) |
| 0x17d167c | s16 | the custom-game selection (index of the selected net game) |
| 0x17d603c | u32 | net menu mode; 2 = Custom Game |

The discovery port is 9012 (0x2334).

### 3.9 The join message (id 0xcf), 0x80 bytes, sent raw over the room's TCP connection

All fields little-endian (both ends are this helper on the same kind of machine; the layout is PS3's structure).

| Offset | Type | Content |
|---|---|---|
| 0 | u32 | 0xcf |
| 4 | u32 | the sender's NAT type |
| 8 | 8 bytes | zero |
| 0x10 | name, 0x24 bytes | the sender's name |
| 0x34 | u8 | 1 when the sender is the room's host, else 0 |
| 0x35 | 3 bytes | zero |
| 0x38 | u32 | member data length |
| 0x3c | 0x40 bytes | member data (unused part zero) |
| 0x7c | 4 bytes | zero |

Member data, the helper's own fields in its last 16 bytes (written by the rank stamp, 4.14):

| Offset in the data block | Type | Content |
|---|---|---|
| 0x30 | s16 | the sender's custom-game selection |
| 0x32 | u8 | status byte |
| 0x33 | u8 | "at the start" byte |
| 0x34 | u64 | the match room id the party should follow |
| 0x3c | u32 | the sender's rank word with bit 29 set ("stamped") |

## 4. Behaviour

Where an order is given it is observable (through the import table, a game function, another group's function or
a counter) and must be kept. "Note the error r" = call `u3x_sock_error(r)`.

### 4.1 `u3x_sock_udp`

1. When the record is open (descriptor > 0) and its port field equals `port`: return 0. Nothing else happens (no
   call at all).
2. Call `u3h_net_pick`.
3. When the socket, set-option or bind import is null: return −1; the record is untouched.
4. Create a socket: debug name `u3mp-lan`, family 2, type 2, protocol 0. Store the result in the descriptor field.
   When it is negative: note the error (the result), set the descriptor field to 0, return −1. (A result of 0 is
   not an error here.)
5. Set option "non-blocking" to 1. When that succeeded and `broadcast` is non-zero, set option "broadcast" to 1.
   When either call returned a negative value: note the error −1 (not the call's result) and return −1. The
   descriptor stays in the record and is not closed; address and port fields are untouched.
6. When `receive_buffer` is non-zero: set option 0x1001 to `receive_buffer`; the result is ignored.
7. Bind the descriptor to `port` (`u3x_sock_bind`). When the result is negative: note that result, return −1; the
   descriptor stays in the record and is not closed; address and port fields are untouched.
8. Set the record's address to the machine's own address and its port to `port` (`u3x_sock_local`). Return 0.

### 4.2 `u3x_sock_listen`

1. When the record is open and its port field equals `port`: return 0, nothing else.
2. Call `u3h_net_pick`.
3. When the socket or the listen import is null: return −1; record untouched. (The set-option and bind imports
   are not tested.)
4. Create a socket: debug name `u3mp-room`, family 2, type 1, protocol 0. Store the result in the descriptor
   field. Negative: note it, descriptor field 0, return −1.
5. In this order, each step only when the one before did not fail: set option "reuse address" to 1; bind to
   `port`; listen with backlog 12. When one of them returned a negative value: note that value, close the record
   (`u3x_sock_close`: close import called, descriptor field 0), return −1.
6. Set option "non-blocking" to 1; the result is ignored.
7. Set the record's address to the machine's own address and its port to `port`. Return 0.

### 4.3 `u3x_sock_connect`

There is no "already open" test: whatever descriptor the record held is overwritten without being closed (the
callers close first where needed).

1. Call `u3h_net_pick`.
2. When the socket or the connect import is null: return −1; record untouched.
3. Create a socket: debug name `u3mp-room`, family 2, type 1, protocol 0. Store the result in the descriptor
   field. Negative: note it, descriptor field 0, return −1.
4. When `nodelay` is non-zero: set option (level 6, option 1) to 1; result ignored.
5. Set option "receive timeout" to 10 000 000, then option "send timeout" to 10 000 000 (10 seconds); both
   results ignored.
6. When `nonblocking` is non-zero: set option "non-blocking" to 1; when that returns a negative value: close the
   record and return −1 (the error is **not** noted).
7. Call the connect import with the socket address of (`ip`, `port`) and length 16. While the result is negative
   with error code 36 or 37, call it again, immediately and with the same arguments; at most 100 000 calls in
   total.
8. When the last result is ≥ 0, or negative with error code 56: store `ip` in the address field and `port` (the
   full u32) in the port field; return 0.
9. Otherwise (any other error, or still 36 / 37 after the 100 000th call): note the last result, close the record,
   return −1.

### 4.4 `u3x_sock_poll`

- When the record is not open (descriptor ≤ 0) or the receive import is null: return −1, no call.
- Otherwise call the receive import once for 1 byte into a scratch byte with flags 0x82 (peek, do not wait).
  Result ≥ 0 (a byte is waiting, or 0 = the peer closed): return 1. Negative with error code 35, 36, 37 or 57:
  return 0. Any other negative result: return −1.
- Nothing is consumed, nothing is closed, no error is noted.

### 4.5 `u3x_sock_recv`

- Call the receive import once with (descriptor, `b`, `n`, flags): flags 0 when `block` is non-zero, 0x80 when it
  is 0. The descriptor and the import are not tested first (the callers poll first).
- Result ≥ 0: return it (0 = the peer closed; the record stays as it is).
- Negative, `block` is 0 and the error code is 35: return the negative result unchanged; nothing else.
- Any other negative result: note it, close the record, return −1.

### 4.6 `u3x_sock_adopt`

Store `fd` in the descriptor field, `port` in the port field and `ip` in the address field; when `ip` is the
loopback address 0x0100007f or 0, the address field gets the machine's own address (`u3x_sock_local_ip()`)
instead. `u3x_sock_local_ip` is called only in that case. No validation of `fd`.

### 4.7 `u3x_stream_put`

Writes the `bytes` low-order bytes of `v`, most significant of them first. For each of these bytes in turn: when
the position is below the limit (unsigned compare), the byte is stored at buffer[position]; in every case the
position then grows by 1. So a field that does not fit is dropped (wholly or its tail) while the position still
advances by `bytes`; the limit and the buffer pointer are never changed. `bytes` ≤ 0: nothing happens. Defined for
`bytes` 1..8 (the callers use 1, 2, 4, 8).

### 4.8 `u3x_stream_get`

Reads `bytes` bytes as one big-endian number: starting from 0, for each byte in turn the result is shifted left by
8 bits and the byte at buffer[position] is added when the position is below the limit (unsigned compare), else 0;
the position grows by 1 in every case. Returns the result (zero-extended to 64 bits). `bytes` ≤ 0: returns 0, the
position does not move. Callers use 1, 2, 4, 8.

### 4.9 Reset one connection entry (`lnb_cm_reset_entry`)

Set to 0: the id (s32 at 0), the in-use byte (4), the two state bytes (5, 6), the 0x24 name bytes (7..0x2a), the
port (u16 at 0x30), the handle (u32 at 0x34), the reference count (s32 at 0x38). Then call `u3h_gt_entry_reset`
with the entry. Not touched: byte 0x2b, the address (u32 at 0x2c), the packet object and everything from 0x3c on.

### 4.10 LAN init of the table (`lnb_cm_init_lan`)

In this order: set the mode flag (table + 0x4db4) to 1; copy the own name (0x24 bytes from image offset
0x17d6010) to table + 0x4d90; reset the 32 entries (4.9) in ascending order; set the blocked-name count (table +
0x4408) to 0; set the next id (table + 0) to 0x1234. The LAN address records and their count are not touched.

### 4.11 Get a connection for a name (`lnb_cm_get`)

1. **Blocked.** Take the blocked-name count, at most 32. When `name` equals (3.1) one of that many blocked
   names: return −1, nothing changed.
2. **Known.** Search the entries in ascending order for the first one that is in use and whose name equals
   `name`. Found: its reference count grows by 1; return its id. Nothing else changes.
3. **New.** Otherwise take the first entry (ascending) that is not in use. None: return −1, nothing changed.
   For that entry, in this order:
   - reset it (4.9);
   - give it the table's next id, and add 1 to the table's next id; mark it in use; reference count 1; copy the
     name (0x24 bytes) into it; handle 0; port 0;
   - call the game function 0x6aa150 with the entry's packet object (entry + 0x40);
   - add 1 to the counter eb[86];
   - then fill the address, by mode:
     - **online** (mode flag 0): when `name` differs from the table's own name (table + 0x4d90), call
       `u3h_onb_cm_np(base, entry, name)`; when it is the own name, nothing. Return the id.
     - **LAN** (mode flag non-zero), `name` equals the own name: nothing more (address, port 0 and handle 0 stay as
       they are after the steps above). Return the id.
     - **LAN**, another name: look through all 32 LAN address records in ascending order (whatever the record
       count says) for the first whose name equals `name`. Found: the entry's address is that record's address
       and its port is 3233. Not found: address 0 and port 0. In both cases the handle becomes 1. Return the id.

### 4.12 Release a connection (`lnb_cm_release`)

Search the entries in ascending order for the first one whose id field equals `id`, in use or not. None: return
−1. Found but not in use: return −1, nothing changed. Found and in use: take the reference count, store it minus
1; when the count before was less than 2 (the last reference): call the game function 0x6aa150 with the entry's
packet object, then clear the in-use byte, then by mode: LAN (mode flag non-zero): handle = 0; online: call
`u3h_pl_drop` with the entry's name (entry + 7). Return 0. The id, the name, the address and the port stay in the
released entry.

### 4.13 Shutdown of the table (`lnb_cm_shutdown`)

Call the game function 0x6aa150 with the packet object of each of the 32 entries, in ascending order. Nothing
else (no field is changed).

### 4.14 Rank stamp (`lnb_rank_stamp`)

Does nothing at all when the backend's state object pointer (backend + 0x249d0) is null or when `len` is not
exactly 0x40. Otherwise, with `base` = the backend's image base (backend + 0x249c8) and `md` the data block:

1. When native 21 of the state object is non-null and `u3h_player_profile(base)` is non-null: call native 21
   with the profile and store its result with bit 29 (0x20000000) set as the u32 at `md` + 0x3c. Otherwise that
   u32 keeps its value.
2. Store: the helper's match room id (the value `u3h_lnb_match_room()` points to) as the u64 at `md` + 0x34; the
   s16 at image offset 0x17d167c as the s16 at `md` + 0x30; at `md` + 0x32 the byte
   `u3x_member_status(net menu mode is 2, guest_wait, host_playing)`; at `md` + 0x33 the byte
   `u3x_member_start(at_start, followed, match room id)`: the four values guest_wait, host_playing, at_start and
   followed are those `u3h_lnb_marks` gives.
3. Last, call `u3h_ng_stamp(base, state object, md)` (it reads and changes what steps 1 and 2 wrote).

### 4.15 Send the join message (`lnb_send_join`)

Build the 0x80-byte message of 3.9 in a local buffer, all bytes zero first: id 0xcf; NAT type from image offset
0x17d5c90; the own name (0x24 bytes from image offset 0x17d6010); the low byte of `host` at 0x34. Member data: let
L be the session's own member data length (u32 at session + 0x12f0); copy min(L, 0x40) bytes from session +
0x12f4 to message + 0x3c; the length field (message + 0x38) is L, except that L = 0 is sent as 0x40 (an all-zero
block that only carries the stamp). The length field is not limited to 0x40. Then apply the rank stamp (4.14) to
the message's data block with that length value (so it stamps only when the value is exactly 0x40). Then send the
0x80 bytes on the binding's client socket (binding + 0x8930) with `u3x_sock_send`, waiting (`block` = 1), and
store its result in `*res` (always written: bytes sent, a negative error, or −1 when the socket is not open).

### 4.16 Backend slot 0, Init (`lnb_s0`)

In this order:

1. Note the slot call (the helper's own diagnostics): eb[67] grows by 1; eb[68] = 0 (the slot number); and when
   the backend's state object is non-null: the u32 at state + 1456 grows by 1; the u32 at state + 1460 = 0; with H
   the u32 at state + 1900, the byte at state + 1904 + (H mod 32) = 0 (the slot number) and H grows by 1.
2. Set the backend's discovery port field (backend + 0x249b8) to 9012.
3. Return `u3x_sock_udp(backend + 0x249bc, 9012, 1, 0)` widened to 64 bits (0 or −1). The port field keeps 9012
   also when the open fails.

### 4.17 `u3x_add_member`

1. **Already a member.** Search the 10 slots in ascending order for the first one that is in use and whose name
   equals the description's name (3.1). Found: return its member index (s32 at slot + 0x28); nothing changes.
2. **Free slot.** Otherwise take the first slot (ascending) that is not in use. None: return −1; nothing changes
   (no counter, no connection).
3. For that slot, in this order:
   - add 1 to the counter eb[80];
   - get a connection for the name (block C, 4.11; today through `u3h_cm_get(base, name)`) while the slot is
     still unused and unwritten; its result (an id or −1) becomes the slot's connection id;
   - write the slot: member id = the description's id limited to its low 16 bits; NAT type = the description's
     NAT type limited to its low 8 bits; name = the 0x24 bytes of the description's name; member index = the
     session's next index (session + 0x12e0), which then grows by 1; in use = 1; member data length 0, or, when
     the description's "carries data" field is exactly 1: length = min(description's length, 0x40) and that many
     bytes copied from the description's data to slot + 0x40. Byte 0x24 of the slot and the data bytes beyond
     the copied length are not touched;
   - when `you` is non-zero the session's own member id (session + 0x12e4) becomes this member's id; when `host`
     is non-zero the session's host member id (session + 0x12e8) becomes this member's id;
   - count the players: when the session has no room (room id 0): 1, or 2 when a second local player is present
     (3.6); otherwise `u3x_party_players(session)` (the new member is already included);
   - with C = the session's capacity: when C is not 0 and C is less than the player count (unsigned): the room
     is over capacity: when the session's backend pointer is non-null, call its "Kick" (entry 9) with (backend,
     session, the new member's id, 0); no "member joined" call. When C is not 0 and not exceeded: call the
     session's "member joined" (entry 3) with (session, slot). When C is 0: neither call;
   - return the member's index. (Also in the over-capacity case: the member stays in its slot; the kick removes
     it later through the room.)

### 4.18 `u3x_remove_member`

Search the 10 slots in ascending order for the first one that is in use and whose name equals `name`. None: return
−1, nothing changes. Found, in this order: add 1 to the counter eb[81]; call the session's "member left" (entry 4)
with (session, slot) while the slot is still in use and unchanged; release the slot's connection id (block C, 4.12;
today through `u3h_cm_release(base, id)`; the result is ignored, the id may be −1); clear the in-use byte and set
the slot's connection id to −1. Return 0. `name` may point at the slot's own name. The other fields of the slot
(name, index, id, data) stay.

## 5. Test scenarios

Three kinds of test are possible; each scenario says which one it needs.

- **[buffer]** plain memory only: no stub at all.
- **[fake]** old against new on fake memory with recording stubs: a fake ExtState (so `u3h_ext`), whose socket
  import table (0x200) points at scripted stubs that record every call (import, arguments, the bytes of socket
  addresses and option values, the buffer contents on send) and return scripted results; stubs for `u3h_net_pick`
  and for the network-information import (so `u3x_sock_local_ip` gives a chosen address); for blocks C to E a fake
  game image (the table at image offset 0x17f31b0 needs about 0x17f8000 bytes of image, or a base chosen so that
  only the used pages are mapped) with a recording stub at image offset 0x6aa150, fake session and backend objects
  with recording function tables, and recording stubs for the `u3h_` functions of 2.3 that snapshot the entry /
  slot / data block they are given. Compared: results, all memory, eb counters, and the sequence and arguments of
  every stub call.
- **[live]** needs the running game (two machines or two copies): listed at the end.

### 5.1 Byte stream [buffer]

1. Put 0x1122334455667788 with widths 1, 2, 4, 8 into a 16-byte buffer from position 0: bytes 88 / 77 88 / 55 66 77
   88 / 11 .. 88 in that order; position 15.
2. Put a 4-byte field at position limit − 2: the first two bytes are stored, the last two dropped, position =
   limit + 2; the bytes behind the limit are unchanged.
3. Put with position ≥ limit: nothing stored, position grows by the width. Limit 0: same.
4. Put with width 0 and with a negative width: nothing changes.
5. Get widths 1, 2, 4, 8 from known bytes: big-endian values; position grows by the width.
6. Get a 4-byte field at limit − 1: first byte real, then three zero bytes (value = byte << 24); position = limit + 3.
7. Get with width 0 / negative: 0, position unchanged.
8. Round trip: random values and widths 1..8 written then read give value mod 2^(8·width).

### 5.2 Socket class [fake]

UDP open:
1. Record open on the same port: returns 0, no stub called (not even `u3h_net_pick`).
2. Record open on another port, and record closed: full sequence net-pick, socket("u3mp-lan", 2, 2, 0), non-blocking,
   [broadcast], bind (address bytes 10 02 port-high port-low 00 00 00 00 + 8 zero), net-pick + network information;
   record = {descriptor, own address, port}; result 0. With `broadcast` 0 the broadcast option is not set; with
   `receive_buffer` non-zero option 0x1001 with that value is set between broadcast and bind, and its failure
   changes nothing.
3. Each of socket / set-option / bind import null: −1, record untouched, only net-pick called.
4. Socket import fails (negative): eb[91] = that value, descriptor 0, −1.
5. Non-blocking fails: eb[91] = 0xffffffff, −1, descriptor kept, broadcast not attempted, no close call. Broadcast
   fails: same.
6. Bind fails: eb[91] = the bind result, −1, descriptor kept, address / port unchanged, no close call.
7. Port with bits above 16 set: the wire port is the low 16 bits, the record's port field the full value.

Listen:
8. Open on the same port: 0, no calls. Otherwise: net-pick, socket("u3mp-room", 2, 1, 0), reuse-address, bind,
   listen(12), non-blocking, own address; result 0.
9. Socket or listen import null: −1. Socket fails: eb[91], descriptor 0, −1.
10. Reuse-address fails: no bind, no listen; eb[91] = its result; close called; descriptor 0; −1. Bind fails: no
    listen; same. Listen fails: same.
11. Non-blocking fails after a good listen: result still 0.

Connect:
12. Success at the first connect call: sequence net-pick, socket, [no-delay when asked], receive timeout 10 000 000,
    send timeout 10 000 000, [non-blocking when asked], connect(address bytes of ip / port, 16); record = {descriptor,
    ip, port}; 0.
13. Socket or connect import null: −1, record untouched. Socket fails: eb[91], descriptor 0, −1.
14. Non-blocking asked and failing: close called, descriptor 0, −1, eb[91] unchanged.
15. Connect answers code 36 three times, then 0: four connect calls, result 0. Code 37 likewise. Code 36 then 56:
    result 0, address / port stored.
16. Connect answers 56 at once: 0.
17. Connect answers another error (for example code 61): one call, eb[91] = it, close, descriptor 0, −1.
18. Connect answers 36 for ever: exactly 100 000 connect calls, then eb[91] = that result, close, −1.
19. Record held an open descriptor before the call: it is overwritten, no close call for the old one.

Poll:
20. Descriptor 0 / negative, or receive import null: −1, no call.
21. Receive stub answers 1 → 1; answers 0 → 1; error codes 35, 36, 37, 57 → 0; another code → −1. In all cases one
    receive call with length 1 and flags 0x82; record and eb[91] unchanged.

Receive:
22. `block` 1: flags 0; positive result returned and bytes in the caller's buffer; 0 returned as 0 with the record
    untouched.
23. `block` 1 and any negative result (also code 35): eb[91] = it, close called, descriptor 0, −1.
24. `block` 0: flags 0x80; code 35 returned unchanged (negative), record untouched, eb[91] unchanged; another error:
    as scenario 23.

Adopt:
25. Ordinary address: record = {fd, ip, port}, no stub call. Address 0x0100007f and address 0: address field = the
    stubbed own address (net-pick and network information called once).

### 5.3 Connection table [fake]

26. Reset: an entry filled with 0xff: afterwards bytes 0..6, 7..0x2a, 0x30..0x31, 0x34..0x3b are 0, bytes 0x2b,
    0x2c..0x2f, 0x32..0x33, 0x3c.. unchanged; one `u3h_gt_entry_reset` call that sees the cleared entry.
27. LAN init on a table filled with 0xff: flag 1, own name copied, 32 entries reset in ascending order (the
    `u3h_gt_entry_reset` stub sees the flag and name already set and the next id not yet 0x1234), blocked count 0,
    next id 0x1234; LAN address records unchanged.
28. Get, new LAN peer with an address record: id 0x1234 returned, next id 0x1235, entry 0 = {id, in use, name,
    address of the record, port 3233, handle 1, references 1}; the 0x6aa150 stub is called once with entry + 0x40
    and sees name / id / in use / references set, handle 0 and port 0; eb[86] + 1.
29. Get, new LAN peer without a record: address 0, port 0, handle 1. With the record count (table + 0x488c) at 0
    but a matching record present in the array: the record is still found.
30. Get, own name on LAN: entry created, handle 0, port 0, address unchanged from before (pre-fill it with a
    value: it stays).
31. Get the same name again: same id, references 2, no game call, eb[86] unchanged, next id unchanged.
32. Get with a blocked name (count 1): −1, nothing changed. Count 40 with the name only at index 31: blocked.
    Count 40 with the name only where a 33rd name would lie (table + 0x440c + 32·0x24): not blocked (only the
    first 32 names are looked at). Count 0 with a matching name in the list: not blocked.
33. Get when 32 entries are in use with other names: −1. Get when entry 0 is in use and entry 1 free: entry 1
    taken.
34. Names: two names equal up to the NUL but different after it are the same connection; two 0x24-byte names
    without NUL differing in the last byte are different connections.
35. Online mode (flag 0): new peer → `u3h_onb_cm_np(base, entry, name)` called once after the game call, address
    fields left to it; own name → not called; in both cases the LAN records are not consulted.
36. Release with references 2: references 1, entry stays in use, no game call, 0. Release again: game call with
    entry + 0x40 (the stub sees in use still 1 and references 0), then in use 0, handle 0 (LAN), id / name /
    address / port kept, 0. Release a third time: −1, nothing changed.
37. Release in online mode, last reference: `u3h_pl_drop` called with the entry's name; handle unchanged.
38. Release of an unknown id, of −1, and of 0 on a freshly initialised table: −1, nothing changed.
39. Shutdown: 32 game calls with entry i + 0x40 in ascending order; table unchanged.

### 5.4 Member slots [fake]

40. Add to an empty session with room id 0, capacity 4, no second local player: slot 0 filled (name 0x24 bytes,
    id & 0xffff, NAT & 0xff, index = old next index, in use, connection id from the stub), next index + 1, eb[80] +
    1, "member joined" called once with (session, slot 0) after the slot is complete; the connection stub is called
    before the slot is written. Returns the index.
41. `you` / `host` set: the own / host member id fields get the member's id; both 0: fields unchanged.
42. Description with data flag 1 and length 0x10, 0x40, 0x41, 0xff: length min(·, 0x40) and that many bytes
    copied; data flag 0 and data flag 2: length 0, data bytes untouched.
43. Same name again: returns the first index, nothing changes, no stub call.
44. Slots 0..2 in use, slot 1 then freed: the next add takes slot 1.
45. All 10 in use with other names: −1, nothing changes.
46. Capacity 0: no "joined" call, no kick, index returned.
47. Room id 0, capacity 1, second local player present (each of the two facts of 3.6 in turn): count 2 > 1 → kick
    called with (backend, session, id, 0), no "joined" call; with the backend pointer null: no call at all. The
    member stays in its slot and its index is returned.
48. Room id non-zero, capacity 2, two members already in use → third add: kick. Capacity 3: "joined". A member
    whose data byte 0x19 is non-zero counts as two players (through `u3x_party_players`).
49. Connection stub answers −1: stored as −1, everything else as usual.
50. Remove an existing member: eb[81] + 1; "member left" called with the slot still in use; then the release stub
    with the slot's connection id; then in use 0 and connection id −1; name, index, id, data unchanged; 0.
51. Remove with `name` pointing at the slot itself: same result.
52. Remove an unknown name, and the name of a slot that is not in use: −1, nothing changes.

### 5.5 Join message, rank stamp, slot 0 [fake]

53. Rank stamp with state null, and with length 0x3f / 0x41 / 0: data block unchanged, no stub call.
54. Rank stamp, length 0x40, native 21 and profile present: native called once with the profile; u32 at 0x3c =
    result | 0x20000000; fields 0x30, 0x32, 0x33, 0x34 as 4.14; `u3h_ng_stamp` called last and sees all of them;
    bytes 0..0x2f unchanged.
55. Rank stamp with native 21 null, or profile null: u32 at 0x3c unchanged, the other fields written,
    `u3h_ng_stamp` called.
56. Net menu mode 2 and not 2; marks all zero and all set: the two bytes are what `u3x_member_status` /
    `u3x_member_start` give for those inputs.
57. Send join, session data length 0x40: the send stub sees 0x80 bytes exactly as 3.9 (stamp applied), flags 0;
    `*res` = the stub's result. `host` 0 / 1 / 0x101 → byte 0 / 1 / 1.
58. Session data length 0: length field 0x40, data block zero plus the stamp.
59. Session data length 0x20: 0x20 bytes copied, length field 0x20, no stamp (no native call, no `u3h_ng_stamp`).
60. Session data length 0x50: 0x40 bytes copied, length field 0x50, no stamp.
61. Client socket closed (descriptor 0) or send import null: `*res` = −1, no send call; the message is still
    built (the stamp's stubs are still called).
62. Slot 0 with a state object: eb[67] + 1, eb[68] = 0, the two probe words and the history ring updated (ring
    index = old count mod 32; try counts 0, 31, 32), port field 9012, then the UDP open sequence of scenario 2 with
    port 9012, broadcast on, no receive-buffer option; result 0. With state null: only the eb words. With the open
    failing: result −1 (64-bit: 0xffffffffffffffff), port field still 9012.

### 5.6 What needs the live game [live]

- Real sockets: that a UDP socket opened this way receives LAN broadcasts on port 9012 and that the listening and
  connecting sockets work on both platform layers (native imports and the PC socket shims, which map their own
  error numbers onto the codes of 3.2, e.g. "would block" on connect arrives as code 36). Check: two machines see
  each other's rooms in Find a Party, one hosts, the other joins.
- The 0x6aa150 game function on real packet objects, and the game's transport and session-leave code reading the
  entries (address, port 3233, handle, references): a match between two machines starts and both leave cleanly;
  a second NetInit after NetShutdown works.
- The session's "member joined" / "member left" entries and the backend "Kick": party rows appear and disappear,
  an over-capacity join is kicked.
- The rank stamp with the real native 21 and `u3h_ng_stamp`: other players' rank badges in the party rows, the
  guest following the host into a match.
- Interoperation of the 0xcf message and of the big-endian discovery stream with the previous helper version on
  the other machine.

## 6. Things in the old code that look like bugs or dead code (behaviour to keep)

1. `u3x_sock_udp`: after a set-option or bind failure the descriptor stays open in the record without address and
   port, so the record tests as "open"; a later open with the same port then creates a second socket and the first
   descriptor is lost. The error noted for a set-option failure is −1, not the call's result.
2. `u3x_sock_udp`: the option set for `receive_buffer` is 0x1001, which on this socket API is the send-buffer
   size; the receive-buffer size would be 0x1002. No caller passes a non-zero `receive_buffer` in v7.212, so the
   branch is dead in practice. Keep 0x1001.
3. `u3x_sock_udp`: a descriptor of exactly 0 from the socket import is accepted and configured although the
   record then tests as closed.
4. `u3x_sock_listen`: the set-option and bind imports are used without a null test (only socket and listen are
   tested); a failing "non-blocking" call is ignored.
5. `u3x_sock_connect`: an open descriptor already in the record is overwritten without a close; a failing
   "non-blocking" call closes without noting the error; the retry on codes 36 / 37 is a busy loop without a pause
   (up to 100 000 calls); the port field receives the unmasked `port`. `nodelay` and `nonblocking` are never
   non-zero in v7.212 (dead branches, keep them).
6. `u3x_sock_poll`: "end of stream" counts as readable (the following receive then returns 0 and the caller
   drops the connection); codes 36, 37 and 57 count as "nothing yet".
7. `u3x_sock_recv`: no test of the descriptor or of the import; with `block` 0 a "try again" comes back as the raw
   negative code instead of −1. Both callers pass `block` = 1, so that branch is dead in practice.
8. Stream: the position runs past the limit; overflow is only visible to a caller that compares the two.
9. `lnb_cm_reset_entry` leaves the address (0x2c) of the previous user in the entry; `lnb_cm_get` for the own
   name on LAN therefore leaves a stale address with handle 0.
10. `lnb_cm_get` on LAN sets handle 1 even when no address record was found (address 0, port 0), and scans all 32
    address records whatever the stored count is (an all-zero record matches an empty name).
11. `lnb_cm_get`: the next-id counter is never checked for wrap-around.
12. `lnb_cm_release`: the search by id also stops at an entry that is not in use (a released entry keeps its id,
    freshly reset entries have id 0); the "last reference" test is "count before < 2", so counts of 0 or below
    also free the entry.
13. `lnb_cm_shutdown` resets the packet objects but leaves every entry's fields (in use, references) as they are.
14. `u3x_add_member`: an over-capacity member is added, gets no "joined" call, and its index is still returned;
    with a null backend nobody is told. With capacity 0 neither call happens. A failed connection (−1) is stored
    without any reaction. The data flag must be exactly 1.
15. `u3x_remove_member` ignores the release result.
16. `lnb_send_join`: a session data length above 0x40 is sent as it is in the length field although only 0x40
    bytes are copied (the room server copies 0x40 bytes and keeps the length); the send result is not checked.
17. `lnb_rank_stamp`: for any length other than 0x40 nothing is stamped (not even the room id); the last 16
    bytes of the game's member data are overwritten by the helper's fields.
18. `lnb_s0`: the port field is set before the open and stays set when the open fails.

## 7. What stays as it is

- The exported names and signatures of 2.1 (the room server, the room client, the backend slots and the online
  backend call them) and the C exports `u3h_cm_get`, `u3h_cm_release`, `u3h_cm_init_lan`, `u3h_cm_shutdown`,
  `u3h_lnb_send_join`, `u3h_lnb_rank_stamp` as long as other groups' code uses them.
- The socket record (3.3) and the stream object (3.4): other groups embed and build them; the backend object's size
  and offsets are asserted on both sides.
- The socket import table, its calling convention, the option numbers, flags and error codes (3.2): they are the
  platform's API; the PC shims translate to exactly these values.
- The wire formats: the big-endian stream (PS3 consoles and older helper versions read it), the 0x80-byte 0xcf
  message and the helper's fields in the member data (3.9; the room server and every other member read them), the
  ports 9012 and 3233.
- Every game offset of section 3 (the game's own code reads the connection table and the member slots), the game
  function 0x6aa150, the session and backend function-table entries 3, 4 and 9.
- The diagnostic words eb[67], [68], [80], [81], [86], [91] and the probe / history fields of the state object: the
  boot log and the test rigs read them.
- Slot 0 of the backend's function table must be a function with the behaviour of 4.16.
- The neighbours of 2.3 (not part of this group).
- Everything in section 6.

## 8. Open questions

1. `u3h_gt_entry_reset`, `u3h_onb_cm_np` and `u3h_pl_drop` do not exist as exports in v7.212 (the old code calls
   the C statics directly). Either the integrator adds these three one-line exports (as assumed above), or the C
   wrappers keep those calls and the new functions report what to do (which would change the proposed signatures).
   To be decided before the implementer starts on block C.
2. The slot-call note of 4.16 step 1 exists a second time in the C++ part (used by slots 1..14, G10). The
   implementer of this group writes it from 4.16; the integrator may make slot 0 use the existing one.
3. `u3x_stream_put` / `u3x_stream_get` for widths above 8 are left undefined (no caller).

## 9. Clarifications (tester, 2026-11-14)

Facts settled by the old-against-new test (`reimpl2/G07/test_g07.cpp`), in words. All 70 scenarios of section 5 hold
for the old code as written there; nothing in sections 1 to 7 was found wrong. The points below add what the text
left open.

1. **Rank stamp (4.14), the profile lookup.** In every call that stamps (state object present, length exactly 0x40)
   the active profile is looked up (`u3h_player_profile(base)`) exactly once, as the first call of all, whether
   native 21 is null or not. Native 21 is then called only when it is non-null and the profile is non-null. The
   calls of a stamping call are therefore, in this order: profile lookup; native 21 (only when both are present);
   `u3x_member_status`; `u3x_member_start`; `u3h_ng_stamp`. In a call that does not stamp there is no call at all.
   The lookup only reads game memory, so making it or not changes nothing but the sequence of calls.

   | native 21 | profile | calls |
   |---|---|---|
   | present | present | lookup, native 21, status, start, ng-stamp |
   | present | null | lookup, status, start, ng-stamp |
   | null | present or null | lookup, status, start, ng-stamp |

2. **Rank stamp, order of the stores.** When `u3x_member_status` is called the block already holds the rank word
   (when it is written at all), the room id (0x34) and the selection (0x30); when `u3x_member_start` is called it
   also holds the status byte (0x32); `u3h_ng_stamp` sees all five fields. The arguments are the plain values: the
   truth value "net menu mode is 2" as 0 / 1, and the three mark bytes as they are (0..255, not reduced to 0 / 1).

3. **Join message (4.15), order.** The whole message (id, NAT type, name, host byte, member data, length field) is
   built before the first call. The calls are those of the rank stamp (point 1; none when the length field is not
   0x40) and then, last, the one `u3x_sock_send`. `u3h_ng_stamp` is given the data block inside the message (message
   + 0x3c) and the message around it is complete at that moment. The stamp is applied also when the client socket
   is closed or the send import is missing.

4. **Add member (4.17), which id.** The own member id field, the host member id field and the "Kick" argument all
   get the id as it was stored in the slot: the description's id limited to its low 16 bits. (Description id
   0x12345678 with `you` and `host` set and the room over capacity: both fields 0x5678, Kick argument 0x5678.)

5. **Add member, the result.** The result is the member index read back from the slot at the very end, after the
   "member joined" or "Kick" call. It is the index the function stored (the session's next index before the call)
   unless the called function changes the slot's index field (slot + 0x28) during that call: then the result is
   the changed value. No caller in v7.212 uses the result (the LAN room client and the online backend ignore it).

6. **Get a connection (4.11), own-name test and mode test.** Their order cannot be observed: both only read the
   table. What counts is the outcome per case, for a new entry (all with the entry reset, the game function called
   once and eb[86] + 1 before):

   | Mode flag | Name | Address | Port | Handle | Call after the game function |
   |---|---|---|---|---|---|
   | 0 | own name | unchanged | 0 | 0 | none |
   | 0 | another | left to `u3h_onb_cm_np` | left to it | left to it | `u3h_onb_cm_np(base, entry, name)` |
   | non-zero (1, 0x80, ...) | own name | unchanged | 0 | 0 | none |
   | non-zero | another, record found | the record's | 3233 | 1 | none |
   | non-zero | another, no record | 0 | 0 | 1 | none |

   The LAN address records are looked at with the name only in the last two rows; a record that carries the own
   name plays no part.

7. **LAN init (4.10), what the entry resets see.** At the first `u3h_gt_entry_reset` call the mode flag is already
   1 and the own name already copied; the blocked-name count and the next id still have their old values until all
   32 entries are reset.

8. **`u3x_sock_adopt` (4.6), order of the stores.** Descriptor, port and the given address are stored first; when
   the address is the loopback address or 0, `u3x_sock_local_ip()` is then called and its answer replaces the
   address field. So during that call the record already holds the descriptor, the port and the address as given.
   `u3x_sock_local_ip` is not given the record and nothing it calls can reach it, so storing the address field only
   once, after the call, gives the same record and the same calls.

9. **Scenario 32, the place "where a 33rd name would lie".** Table + 0x440c + 32·0x24 is table + 0x488c: the LAN
   address record count followed by the start of the first address record. A test that writes a name there
   overwrites those; the name is not looked at as a blocked name whatever the count says.

10. **Counters that run over.** A reference count of 0x7fffffff that grows by 1 becomes 0x80000000, one of
    0x80000000 that shrinks by 1 becomes 0x7fffffff; the table's next id and the session's next index run over
    the same way (plain 32-bit wrap-around, no test).

11. **Not settled here.** `u3x_stream_put` with a width above 8 (the old code's bytes above the 8th are not
    defined; no caller); `u3x_sock_listen` / `u3x_sock_connect` with the set-option or bind import missing and
    `u3x_sock_recv` with the receive import missing (the old code calls the missing import; no caller does that).
