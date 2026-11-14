# G09. LAN backend: room server

Behaviour description for a reimplementation (reimpl2 pipeline, describer's document). It says what the functions
do, not how the old ones were written. All offsets are hexadecimal when prefixed with 0x. All multi-byte values, in
memory and on the wire of this group, are little-endian; "u8 / u16 / u32 / u64 / s32" are unsigned / signed integers
of that width. No function of this group uses floating point, calls a game function or touches game data: the group
works only on its own object, on the socket class of group G07 and on two diagnostic counters.

The group has 7 functions from the inventory and 5 strays (section 2.1 says which):

| Block | Functions |
|---|---|
| A. life cycle | `u3x_lnb_srv_ctor`, `u3x_lnb_srv_start`, `u3x_lnb_srv_stop`, `u3x_lnb_srv_running`, `u3x_lnb_srv_count` |
| B. per-frame work | `u3x_lnb_srv_update`, `u3x_sock_accept` |
| C. messages | `u3x_lnb_srv_msg` and its four internal operations: member record, broadcast, drop, join (old file-private names `room_record`, `room_bcast`, `room_drop`, `room_join`) |

The group is about 220 code lines: one part, one implementer.

## 1. Purpose and callers

The LAN backend (see `G07_lan_sockets.md`, section 1) lets one machine host a room: a party or a match that other
machines on the same network join. The host runs a small TCP **room server**; every member, the host included,
holds one TCP connection to it (the host connects to itself through the loopback address). The server keeps the
list of members, gives each connection a member id, tells every member who joined, who left and whose member data
changed, relays two small notices between members and passes a kick on to its target. It is the helper's
replacement for the room server the PS3 game had and the PS4 image lost; its messages keep the PS3 structure
layouts.

There is one server object inside each of the backend's four session bindings (binding + 8; the binding and the
backend object are described in G07 3.8 and belong to group G10). Everything runs on the game's main thread, from
the backend's function table; there is no locking and no timer in this group.

Callers (all outside the group):

- `u3x_lnb_srv_ctor`: the backend object's constructor (`u3x_lnb_ctor`, G10), once per binding, on an object the
  caller has filled with zero bytes just before.
- `u3x_lnb_srv_start`: backend slot 4 "Create" (`u3x_lnb_s4`, G10) with `arg` = 1, the room port (0x2333 or 0x2332)
  and the session's capacity. A negative result makes the caller fall back or fail; on success the caller connects
  its own client socket to the loopback address and the same port, sends the join message and then runs slot 1 up
  to 30 times, 100 ms apart, until the room reply has arrived.
- `u3x_lnb_srv_update`: backend slot 1 "Update" (`u3x_lnb_s1`, G10), once per call for every binding that has a
  session, right before that binding's room client is updated. Slot 1 runs once per frame and also inside the
  waits of slots 4 and 6.
- `u3x_lnb_srv_stop`: slot 4 when no room reply came, slot 6 "Join", slot 7 "Leave" (G10), and the C part when the
  backend object is rebuilt at NetInit (`u3h_lnb_setup`: stop on all four bindings, then the object is zeroed and
  constructed again).
- `u3x_lnb_srv_running`, `u3x_lnb_srv_count`: slot 1 (G10) when it answers a discovery datagram: a binding's room
  is listed only while its server is running, with the count as "members". Slot 1 also reads three fields of the
  server object directly (3.4: listening port, room id, capacity).
- `u3x_lnb_srv_msg`: only `u3x_lnb_srv_update`.
- `u3x_sock_accept`: only `u3x_lnb_srv_update`.

The C part has one-line wrappers `lnb_srv_ctor`, `lnb_srv_running`, `lnb_srv_count`, `lnb_srv_start`,
`lnb_srv_stop`, `lnb_srv_update` that pass their arguments unchanged; only `lnb_srv_stop` is still used there.

## 2. Interface

### 2.1 Functions to deliver

All `extern "C"`, names and signatures unchanged (no function of this group is still in C, so there is no new
wrapper to write). `v` is a server object (3.4).

| Function | Signature | Result | Origin |
|---|---|---|---|
| construct | `void u3x_lnb_srv_ctor(void *v)` | none | stray |
| running? | `int u3x_lnb_srv_running(const void *v)` | 1 when the state field is exactly 1, else 0 | stray (trivial) |
| connections | `uint32_t u3x_lnb_srv_count(const void *v)` | number of client slots in use (joined or not), 0..16 | stray |
| start | `int32_t u3x_lnb_srv_start(void *v, uint64_t arg, uint32_t port, uint32_t maxp)` | the result of the listen call: 0 listening, negative failed | inventory |
| stop | `void u3x_lnb_srv_stop(void *v)` | none | stray |
| one message | `uint32_t u3x_lnb_srv_msg(void *v, int k)` | number of bytes of client slot `k`'s receive buffer the message used; 0 = nothing consumed (incomplete or unknown message) | inventory |
| per-frame update | `void u3x_lnb_srv_update(void *v)` | none | inventory |
| accept | `int32_t u3x_sock_accept(const void *s, uint32_t *ip, uint32_t *port)` | the new connection's descriptor (> 0), or 0 = none | stray |

Parameters: `arg` a caller value that is only stored; `port` the TCP port to listen on, host order; `maxp` the
room's capacity (largest number of connections); `k` a client slot index 0..15 (not validated); `s` a socket
record (3.3) that is listening; `ip`, `port` of the accept: where the peer's IPv4 address (socket-address form) and
port (host order) are stored.

The four internal operations of block C (4.6 to 4.9) are not exported and nobody outside the group calls them: the
implementer is free in how the inside is cut. Only their effects, as seen through the functions above, count.

### 2.2 Functions of other groups and of the C part that the new code calls

Not to be described or reimplemented here; all exist and are exported today. G07 gives their full behaviour.

| Name | Signature | What it does |
|---|---|---|
| `u3h_ext` | `uint8_t *u3h_ext(void)` | address of the helper's state block "ExtState" (3.2). `ext_eb()` of CONVENTIONS.h is the diagnostic word array in it. |
| `u3x_sock_init` | `void u3x_sock_init(void *s)` | sets the three fields of a socket record to 0 (closed, no address, no port). |
| `u3x_sock_close` | `int32_t u3x_sock_close(void *s)` | when the record's descriptor is > 0 and the close import is present: calls the close import with the descriptor; in every case sets the descriptor field to 0. Address and port fields stay. |
| `u3x_sock_local_ip` | `uint32_t u3x_sock_local_ip(void)` | this machine's own IPv4 address in socket-address form, or 0 when it is not known. |
| `u3x_sock_listen` | `int32_t u3x_sock_listen(void *s, uint32_t port)` | G07 4.2: when the record is already open on `port`: 0 and nothing else; otherwise opens a non-blocking listening TCP socket on `port` in the record (own address and port stored): 0, or −1 on failure. |
| `u3x_sock_adopt` | `void u3x_sock_adopt(void *s, int32_t fd, uint32_t ip, uint32_t port)` | G07 4.6: record = {`fd`, `ip`, `port`}; when `ip` is the loopback address 0x0100007f or 0 the record's address becomes this machine's own address instead. |
| `u3x_sock_poll` | `int32_t u3x_sock_poll(const void *s)` | G07 4.4: 1 = something to read (data or end of stream), 0 = nothing, −1 = broken or not open. |
| `u3x_sock_recv` | `int32_t u3x_sock_recv(void *s, void *b, uint32_t n, int block)` | G07 4.5: one receive of at most `n` bytes into `b`: bytes received, 0 = the peer closed (record untouched), −1 = error (the record has been closed: descriptor 0). |
| `u3x_sock_send` | `int32_t u3x_sock_send(const void *s, const void *b, uint32_t n, int block)` | −1 without any call when the record is not open or the send import is missing; else one call of the send import for `n` bytes (waiting when `block` is non-zero) and its result. |

Every send of this group is one `u3x_sock_send` call with `block` = 1 for the whole message; its result is never
looked at (no retry, no partial-send handling, no reaction to an error).

## 3. Facts

### 3.1 Player names

A player name is a field of 0x24 bytes holding a NUL-terminated text. **Name equality** (the same rule as G07 3.1):
compare byte by byte from the start for at most 0x24 bytes; the names differ at the first position where the bytes
differ; they are equal when a NUL is reached at the same position in both (what follows the NUL is not looked at)
or when all 0x24 bytes are equal. **Name copies** always copy all 0x24 bytes, whatever follows the NUL.

### 3.2 ExtState (address from `u3h_ext()`)

| Offset | Type | Meaning |
|---|---|---|
| 0x200 + 8·9 | pointer | socket import 9 "accept"; may be null. Arguments: (int descriptor of a listening socket, pointer to a 16-byte socket address that receives the peer, pointer to a u32 that holds the address length). Result s32: the new connection's descriptor, negative = error or no connection waiting (the listening socket is non-blocking). |
| 0x4180 | u32 | eb[76]: connections accepted (counter) |
| 0x4184 | u32 | eb[77]: calls of the message handler (counter) |

Socket address (16 bytes, G07 3.2): u8 length; u8 family; u16 port in big-endian (network) byte order at offset 2;
u32 IPv4 address at offset 4 with its bytes in network order (127.0.0.1 read as a little-endian u32 is
0x0100007f: "socket-address form"); 8 more bytes.

### 3.3 The socket record (12 bytes, G07 3.3)

s32 descriptor at 0 (open when > 0, 0 = closed); u32 IPv4 address at 4 (socket-address form); u32 port at 8 (host
order).

### 3.4 The server object (0x8928 bytes)

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | the member id the next accepted connection gets |
| 4 | u32 | capacity (`maxp` of the last start) |
| 8 | u64 | `arg` of the last start (stored, never read) |
| 0x10 | u64 | room id (3.6) |
| 0x18 | socket record | the listening socket |
| 0x24 | u32 | state: 1 = running; anything else = not running (0 after a stop or on a fresh object) |
| 0x28 | 16 × s32 | kick list: IPv4 addresses (socket-address form, held as signed 32-bit values) that are refused |
| 0x68 + k·0x88c | 16 client slots of 0x88c bytes | k = 0..15 |

Group G10 reads the listening socket's port field (object + 0x20), the room id (0x10) and the capacity (4) directly
and embeds the object at binding + 8; the C part asserts the object's size, the client slot's size and the offsets
0x28 and 0x68. The layout must stay.

### 3.5 A client slot (0x88c bytes)

| Offset | Type | Meaning |
|---|---|---|
| 0 | name, 0x24 bytes | the member's name (from its join message) |
| 0x24 | u32 | member id |
| 0x28 | u32 | NAT type (from the join message) |
| 0x2c | 8 bytes | unused, never written |
| 0x34 | u32 | member data length |
| 0x38 | 0x40 bytes | member data |
| 0x78 | u8 | in use: a connection occupies the slot |
| 0x79 | u8 | joined: the connection has sent a valid join message |
| 0x7a | u8 | host byte (from the join message) |
| 0x7b | u8 | unused, never written |
| 0x7c | socket record | the connection: descriptor, the peer's address, the peer's port |
| 0x88 | u32 | number of bytes waiting in the receive buffer (0..0x800) |
| 0x8c | 0x800 bytes | receive buffer |

A slot is a **member** when both "in use" and "joined" are non-zero. Only members receive broadcasts and appear in
member lists.

### 3.6 The room id and the room counter

The group owns one u32 variable, the **room counter**: 0 when the helper is loaded, shared by all server objects,
only ever incremented (by every start), never reset.

A room id is a u64. Bits 63..32: this machine's IPv4 address with the first octet in the most significant byte
(the socket-address-form u32 with its four bytes reversed). Bits 31..0: the bitwise OR of (port · 0x10000, cut to
32 bits) and the room counter's value at the time of the start. So bits 31..16 are the port and bits 15..0 a
running number, as long as the counter is below 0x10000 (it is not masked: section 6). Joiners take the address
and the port back out of these bits.

### 3.7 Messages from a client (TCP stream, no framing: the u32 id at the start decides the length)

| Id | Meaning | Length | Fields the server uses |
|---|---|---|---|
| 0xcf | join | 0x80 | u32 NAT type at 4; name (0x24) at 0x10; u8 host at 0x34; u32 member data length at 0x38; member data (0x40) at 0x3c. Full layout: G07 3.9. |
| 0xcd | my member data changed | 0x50 | u32 length at 4; member data (0x40) at 0x10. (The u64 at 8 is the sender's room id; not looked at.) |
| 0xd1 | kick a member | 0x10 | u32 member id at 4; u64 at 8 (the sender's room id), passed on as it is |
| 0xd2 | the host's advanced settings | 0x20 | relayed; the u32 at 4 is replaced |
| 0xd3 | the host's elimination notice | 0x10 | relayed unchanged |

### 3.8 Messages to clients

Every byte not named is zero.

| Id | Meaning | Length | Layout |
|---|---|---|---|
| 0xc9 | join reply | 0x478 | u32 0xc9 at 0; u64 room id at 8; u16 capacity (low 16 bits) at 0x10; u16 number of member records at 0x12; member records of 0x70 bytes from 0x18 (room for 10) |
| 0xca | a member joined | 0x80 | u32 0xca at 0; u64 room id at 8; one member record at 0x10 (its 0x6e written bytes end at 0x7e) |
| 0xcb | a member left | 0x38 | u32 0xcb at 0; u16 member id (low 16 bits) at 4; u64 room id at 8; name (0x24) at 0x10 |
| 0xce | a member's data changed | 0x58 | u32 0xce at 0; u16 member id (low 16 bits) at 4; u64 room id at 8; u32 length at 0x10; member data (0x40) at 0x14 |
| 0xcc | you were kicked | 0x10 | u32 0xcc at 0; u64 at 8 = the u64 at 8 of the kick message |
| 0xd2, 0xd3 | relayed | 0x20, 0x10 | as received (0xd2 with the u32 at 4 = the sender's member id, all 32 bits) |

### 3.9 The member record (0x70 bytes; 0x6e are written)

| Offset | Type | Content |
|---|---|---|
| 0 | name, 0x24 bytes | the slot's name |
| 0x24 | u32 | the address field of the slot's socket record (the member's LAN address, socket-address form) |
| 0x28 | u16 | member id, low 16 bits |
| 0x2a | u8 | NAT type, low 8 bits |
| 0x2b | u8 | member data length, low 8 bits |
| 0x2c | u8 | "you": 1 in the copy sent to the member the record is about, else 0 |
| 0x2d | u8 | the slot's host byte |
| 0x2e | 0x40 bytes | the slot's member data (always all 0x40 bytes) |
| 0x6e | 2 bytes | not written (zero in every message) |

These layouts are read by the room client of every member (group G08) and by older helper versions: they must
stay byte for byte.

## 4. Behaviour

Where an order is given it is observable (through the socket functions or a counter) and must be kept. "Broadcast
X except k" always means: for every client slot j = 0..15 in ascending order that is a member (3.5) and is not
slot k, one send (2.2) of X on that slot's connection.

### 4.1 `u3x_lnb_srv_ctor`

Initialise the listening socket record (`u3x_sock_init`); for each of the 16 client slots: initialise its socket
record, clear "in use", set the receive count to 0; set the next member id (object + 0) to 1; set all 16 kick list
entries to −1. Nothing else is written: capacity, `arg`, room id, state and the other slot fields (name, id,
joined, data, buffer) keep what the memory held (zero, with today's caller).

### 4.2 `u3x_lnb_srv_running`, `u3x_lnb_srv_count`

Running: 1 when the state field equals 1, else 0. Count: how many of the 16 slots have a non-zero "in use" byte
(connections that have not joined yet count too). Both are pure reads.

### 4.3 `u3x_lnb_srv_start`

1. Take this machine's address (`u3x_sock_local_ip()`, called exactly once, before the listen call) and the room
   counter's value; add 1 to the room counter.
2. Store: the room id built from that address, `port` and that counter value (3.6); `arg`; `maxp` as the capacity.
3. Call `u3x_sock_listen` on the listening socket record with `port`.
4. Negative result: call `u3x_sock_close` on the listening socket record; the state field is **not** changed.
   Otherwise: state = 1.
5. Return the listen result.

Nothing else is touched: client slots, the kick list and the next member id stay as they are (a start on a server
that still has connections keeps them; member ids go on counting over stops and starts).

### 4.4 `u3x_lnb_srv_stop`

In this order: state = 0; for each client slot in ascending order that is in use: close its connection
(`u3x_sock_close`), clear "in use"; then close the listening socket (`u3x_sock_close`, also when it is not open).
No message is sent to anybody. Not touched: "joined", names, receive counts and buffers, the kick list, the next
member id, room id, capacity.

### 4.5 `u3x_sock_accept`

- When the accept import (3.2) is null: return 0; nothing is written.
- Otherwise call it once with: the record's descriptor (not tested: it may be 0), a 16-byte socket address whose
  bytes are all zero, and a u32 holding 16.
- Negative result: return 0; `*ip` and `*port` are not written.
- Otherwise: `*ip` = the u32 address field of the returned socket address (unchanged: socket-address form);
  `*port` = its port converted from big-endian to a host-order number (0..0xffff); return the import's result.
  (A result of exactly 0 is returned as 0 with both outputs written; the caller takes it for "none".)

Nothing is noted in the "last socket error" word and `u3h_net_pick` is not called here.

### 4.6 Internal: the member record of a slot

Given a slot and a "you" value, the 0x6e bytes of 3.9 are produced from the slot's current fields. Bytes 0x6e and
0x6f of the record's place are left as the message buffer has them (zero).

### 4.7 Internal: a connection is gone ("drop" of slot k)

In this order: build the 0xcb message (3.8) with slot k's member id, the server's room id and slot k's name;
broadcast it except k; close slot k's connection (`u3x_sock_close`); clear slot k's "in use". The "joined" byte, the
name and everything else in the slot stay. The 0xcb is sent whether or not slot k had joined (with the name the
slot happens to hold: section 6).

### 4.8 Internal: join (message 0xcf complete in slot k's buffer)

1. **Name taken.** Search the slots in ascending order (slot k itself included) for a member whose name equals
   (3.1) the name in the message. Found: send slot k a 0xc9 reply of 0x478 bytes that holds only the id 0xc9 and
   the capacity at 0x10 (room id 0, record count 0, no records). Slot k is not changed (it is neither made a
   member nor dropped). Done; the message counts as used (0x80 bytes).
2. **Accepted.** Otherwise store in slot k from the message: NAT type (u32 at 4); name (0x24 bytes at 0x10);
   joined = 1; member data length (u32 at 0x38, not limited); host byte (u8 at 0x34, as it is); member data (0x40
   bytes at 0x3c).
3. Build the 0xc9 reply: room id, capacity, then the member records (4.6) of the first 10 members in ascending
   slot order (slot k is a member now), each with "you" = 1 only for slot k's own record; the record count is the
   number written (at most 10). Send the 0x478 bytes to slot k.
4. Build the 0xca message with the room id and slot k's member record with "you" = 0; broadcast it except k.
5. The message used 0x80 bytes.

The reply is sent before the broadcast. The reply may be built in a static buffer of the group (all calls are on
one thread; where the bytes are built is not observable).

### 4.9 `u3x_lnb_srv_msg`

Add 1 to eb[77] first, on every call. Then look at slot k's receive buffer: N = its receive count, id = the u32
at the buffer's start (taken from the buffer whatever N is). A message is handled only when N is at least its
length (3.7); otherwise, and for every other id, return 0 with nothing else changed.

- **0xcf** (N ≥ 0x80): join (4.8). Return 0x80.
- **0xcd** (N ≥ 0x50): store in slot k the member data length (u32 at 4 of the message, not limited) and the 0x40
  data bytes at 0x10 of the message; build the 0xce message with slot k's member id, the room id, that length and
  those 0x40 bytes; broadcast it except k. Return 0x50. Whether slot k has joined is not tested.
- **0xd3** (N ≥ 0x10): broadcast the message's first 0x10 bytes, unchanged, except k. Return 0x10.
- **0xd2** (N ≥ 0x20): broadcast a copy of the message's first 0x20 bytes in which the u32 at 4 is slot k's member
  id, except k. The receive buffer itself is not changed. Return 0x20.
- **0xd1** (N ≥ 0x10): look in ascending order for the first member whose member id (all 32 bits) equals the u32
  at 4 of the message. Found: the first kick list entry whose value is 0 receives that member's socket address
  (when no entry is 0 nothing is stored); then that member is sent the 0xcc message carrying the u64 at 8 of the
  kick message. The member's slot is not changed and its connection is not closed here (its own client closes
  after the 0xcc, and the next update then drops the slot). Not found: nothing. Return 0x10 in both cases. Neither
  the sender's membership nor its host byte is tested.

The function never changes the receive count or moves buffer bytes: its caller does.

### 4.10 `u3x_lnb_srv_update`

Does nothing at all unless the state field is 1.

**Step 1: at most one new connection.** Call `u3x_sock_accept` on the listening socket record with two outputs
preset to 0. Result 0: go to step 2. Otherwise, in this order:

1. Turn the descriptor, address and port into a socket record with `u3x_sock_adopt` (a scratch record; a peer
   address of loopback or 0 becomes this machine's own address there: that is how the host's own connection gets
   the host's LAN address).
2. Add 1 to eb[76].
3. Decide. The connection is **refused** when one of these holds: (a) a slot in use already has the same address
   and the same port field as the adopted record; (b) the number of slots in use (4.2) is not below the capacity
   (unsigned compare); (c) one of the 16 kick list entries equals the adopted address (as a signed 32-bit value).
   Refused: call `u3x_sock_close` on the scratch record (the close import gets the new descriptor); nothing else.
4. Not refused: the first slot (ascending) that is not in use takes it: its socket record becomes a copy of the
   scratch record (descriptor, address, port); its member id = the server's next member id, which then grows by
   1; in use = 1; joined = 0; receive count = 0. Name, NAT type, data, host byte and buffer bytes of the slot stay
   as its last occupant left them. When no slot is free (possible only with a capacity above 16) nothing happens:
   the descriptor is neither stored nor closed.

**Step 2: every connection.** For k = 0..15 in ascending order, slots not in use skipped (a slot filled in step 1
of this call is included; a slot dropped earlier in this call is skipped):

1. `u3x_sock_poll` on the slot's connection. Negative: drop slot k (4.7), next slot. 0: next slot.
2. `u3x_sock_recv` once, into the receive buffer behind the bytes already waiting, for at most the free space
   (0x800 − receive count; the call is also made when that is 0), with `block` = 1.
   - Result 0: drop slot k (4.7), next slot (bytes still waiting are not handled).
   - Result > 0: the receive count grows by it.
   - Result < 0: nothing here (the receive function has closed the connection; the slot stays in use and is
     dropped by the poll of the next update).
3. Then, also after a negative result: as long as the receive count is not 0, call `u3x_lnb_srv_msg(v, k)`; a
   result of 0 ends the loop; otherwise remove that many bytes from the front of the buffer: the bytes behind
   them move down to the start in ascending order and the receive count shrinks by the result. The buffer bytes at
   and behind the new receive count are left as they are (not cleared).

So one update accepts at most one connection and makes at most one receive per connection, but handles every
complete message a connection has in its buffer.

## 5. Test scenarios

Two kinds:

- **[fake]** old against new with a fake socket layer, no game: either recording stubs for the nine functions of
  2.2 (scripted results for listen / poll / recv / local address; send records slot socket, length and a copy of
  the bytes; close and adopt recorded with the record's contents), or the real G07 functions over a fake ExtState
  whose import table (0x200) holds scripted recording stubs. `u3x_sock_accept` is tested at import level (a stub
  in entry 9). Compared: results, the whole server object, the room counter's effect (through the next room id),
  eb[76] / eb[77], and the sequence, target and bytes of every stub call. Old and new each need their own fresh
  copy of the room counter (0) at the start of a run.
- **[live]** needs two machines (or two copies) running the game: listed in 5.6.

### 5.1 Life cycle [fake]

1. Constructor on an object filled with 0xa5: listening record {0, 0, 0}; every slot's record {0, 0, 0}, in use 0,
   receive count 0; next member id 1; 16 kick entries −1; every other byte still 0xa5 (capacity, `arg`, room id,
   state, names, ids, joined, host, data, buffers).
2. Running: state 1 → 1; state 0, 2, 0xa5a5a5a5 → 0. Count: 0, 1 and 16 slots in use; a slot in use but not
   joined counts; a slot joined but not in use does not; an "in use" byte of 0x80 counts as 1.
3. Start, own address 192.168.1.5 (u32 0x0501a8c0), port 0x2333, capacity 5, `arg` 1, room counter 0, listen
   stub answers 0: room id 0xc0a8010523330000, `arg` 1, capacity 5, state 1, result 0; local address asked once,
   listen called with the listening record and 0x2333; no close.
4. Start again on the same object: room id 0xc0a8010523330001; slots, kick list and next member id unchanged.
5. Start with the listen stub answering −1 on a stopped object (state 0): result −1, close called on the listening
   record, state still 0; room id, `arg`, capacity written all the same; room counter still incremented (the next
   successful start shows it).
6. Start with listen answering −1 on an object whose state is 1: state stays 1.
7. Start with own address 0: high half of the room id 0. Port 0x12333: bits 31..16 are 0x2333 (the port cut to 32
   bits after the shift). Room counter 0x10000 (after that many starts, or preset when the harness can): the low
   32 bits are (port·0x10000) OR 0x10000.
8. Stop with slots 2 and 7 in use (7 joined): state 0; close on slot 2, then slot 7, then the listening record;
   both "in use" 0; "joined" of slot 7 still 1; names, receive counts, kick list, next member id, room id
   unchanged; no send. Stop on a fresh object: only the listening-record close.

### 5.2 Accept [fake, import level]

9. Accept import null: 0, outputs untouched, no call.
10. Import answers −1 (and another negative value): 0, outputs untouched; the stub saw the record's descriptor, a
    16-byte all-zero address and a length of 16.
11. Import answers 9 and fills the address with family 2, port bytes 0x23 0x33, address bytes c0 a8 01 07: result
    9, `*ip` = 0x0701a8c0, `*port` = 0x2333.
12. Import answers 0: result 0, outputs written from the address.
13. Record with descriptor 0: the import is still called (with 0).

### 5.3 Update, connections [fake]

14. State not 1: no stub call, nothing changes.
15. Running, accept gives nothing, no slot in use: only the accept call; eb[76] unchanged.
16. Accept gives descriptor 9 from 192.168.1.7 port 50000, capacity 5: adopt called, eb[76] + 1, slot 0 = {record
    {9, 0x0701a8c0, 50000}, member id = old next id, in use 1, joined 0, receive count 0}; next id + 1; then slot
    0 is polled in the same call.
17. Peer address loopback (0x0100007f) and peer address 0: the slot's address is the stubbed own address.
18. Slot 0 in use, slot 1 free, slot 2 in use: the new connection takes slot 1. A slot reused keeps its old name,
    data and buffer bytes; joined becomes 0.
19. Same address and port as a slot in use: refused: close on the scratch record with the new descriptor, no slot
    changed, next id unchanged, eb[76] still + 1. Same address, other port: accepted.
20. Slots in use = capacity (and capacity 0): refused the same way.
21. Kick list entry (index 0, and index 15) equal to the peer address: refused. On a constructed object (entries
    −1) a peer is never refused for this reason.
22. Capacity 20 with all 16 slots in use: no close call, no slot changed, eb[76] + 1.
23. Only one accept call per update even when the stub would deliver more.
24. Poll −1 on a member slot: 0xcb (0x38 bytes: id, the slot's id as u16, room id, name) sent to every other
    member in ascending slot order, not to non-joined connections and not to the slot itself; then close on the
    slot's record; in use 0; joined and name unchanged; no receive call for it.
25. Poll −1 on a slot that never joined (name left by a previous occupant): the same 0xcb with that stale name
    and the new id.
26. Poll 0: no receive, no message call (eb[77] unchanged), also when bytes are waiting.
27. Poll 1, receive answers 0: dropped as in 24; a complete message waiting in the buffer is not handled.
28. Poll 1, receive answers −1 while a complete 0xd3 waits in the buffer (left from before): the message is still
    handled (relayed); the slot stays in use; no drop in this call.
29. Receive is called with buffer start + receive count and length 0x800 − receive count, `block` 1; with a full
    buffer the length is 0 and a result of 0 drops the slot.
30. Two slots readable in one update: handled in ascending order; a broadcast caused by slot 1's message reaches
    slot 0 and slot 5 in that order.

### 5.4 Messages [fake]

Set-up for most: running server, room id R, capacity 4, slot 0 = member "ALPHA" (id 1, host byte 1, address A0),
slot 1 = member "BRAVO" (id 2), slot 2 in use but not joined (id 3), slot 3 free.

31. Slot 2 receives a full 0xcf (NAT 2, name "CHARLIE", host 0, data length 0x40, data pattern): slot 2 gets NAT,
    name (all 0x24 bytes, also those behind the NUL), joined 1, length, host byte, data; first send: to slot 2,
    0x478 bytes = 0xc9, R, capacity 4, count 3, records of ALPHA, BRAVO, CHARLIE in that order with "you" 0, 0, 1,
    each with the slot's address at 0x24, id as u16, NAT and length as u8, host byte, 0x40 data bytes, bytes 0x6e /
    0x6f zero, the seven unused record places zero; then 0xca (0x80 bytes: R at 8, CHARLIE's record with "you" 0 at
    0x10, bytes 0x7e / 0x7f zero) to slot 0, then slot 1. Result 0x80; eb[77] + 1; the receive count is reduced by
    the update, not by the handler.
32. The same with the name "BRAVO" (taken): one send to slot 2: 0x478 bytes, 0xc9, capacity at 0x10, everything
    else zero; slot 2 unchanged (joined 0); no broadcast; result 0x80. A name equal up to the NUL but different
    behind it is taken; names of 0x24 bytes without NUL differing in the last byte are different.
33. A member sends 0xcf again with its own name: "taken" reply, slot unchanged. With a new free name: fields
    overwritten, full reply and 0xca again.
34. 0xcf from a slot while 11 members exist with lower slot numbers (capacity 16): the reply holds 10 records
    (count 10), the joiner's own record is not among them; 0xca goes to all 11.
35. Join with data length 0x123 and NAT 0x1ff: slot holds the full u32 values; the records carry 0x23 and 0xff.
36. 0xcf with only 0x7f bytes waiting: result 0, nothing changed but eb[77] + 1. Likewise 0xcd with 0x4f, 0xd3 /
    0xd1 with 0xf, 0xd2 with 0x1f.
37. 0xcd from slot 1 (length 0x40, data D): slot 1's length and data updated; 0xce (0x58 bytes: id 2 as u16 at 4,
    R, length, D, last 4 bytes zero) to slot 0 only (slot 2 has not joined, slot 1 is the sender). Result 0x50.
    From slot 2 (not joined): stored and relayed the same way, to slots 0 and 1.
38. 0xd3 from slot 0: its 0x10 bytes unchanged to slot 1. Result 0x10.
39. 0xd2 from slot 1 with the u32 at 4 = 0x99: slot 0 receives 0x20 bytes with the u32 at 4 = 2 and the rest
    unchanged; the receive buffer still holds 0x99. Result 0x20.
40. 0xd1 from slot 0 with member id 2 and the u64 Q at 8: slot 1 is sent 0x10 bytes {0xcc, 0, Q}; slot 1 stays in
    use and joined; kick list: with entries all −1 unchanged; with entry 3 the first that is 0 → entry 3 = slot
    1's address. Result 0x10.
41. 0xd1 with an unknown id, with the id of the non-joined slot 2, and with an id whose low 16 bits match but
    whose high bits differ: no send, kick list unchanged, result 0x10. 0xd1 with the sender's own id: the sender
    gets the 0xcc.
42. Unknown id (0, 0xc9, 0xd0, 0xffffffff) with a full buffer: result 0, nothing changed but eb[77].
43. Update with two complete messages and half of a third in one receive (0xd3, 0xcd, 0x30 bytes of a 0xcf): both
    handled in order, three handler calls (eb[77] + 3), receive count 0x30, the 0x30 bytes at the buffer's start,
    the bytes behind them unchanged from before the last move.
44. Update with an unknown id at the front followed by a valid message: nothing is consumed, now and in later
    updates.
45. A message whose first 2 bytes arrived (receive count 2, the id bytes 3 and 4 stale from an earlier message):
    result 0 whatever the stale bytes are.

### 5.5 Sends that fail [fake]

46. Broadcast with one member's record closed (descriptor 0): the send function is still called for it (and
    answers −1); the other members get their copy; nothing else changes.

### 5.6 What needs the live game [live]

- Real sockets on both platform layers: the listening socket accepts without blocking the frame, the host's own
  loopback connection is accepted and shows the host's LAN address in its member record.
- One machine hosts, two join: party rows appear on all three (0xc9 / 0xca), member data (rank badge, map choice)
  updates on change (0xcd → 0xce), a member leaving or losing its connection disappears everywhere (0xcb), a kick
  removes the member (0xd1 → 0xcc → 0xcb), the host's advanced settings and elimination notice reach the guests
  (0xd2 / 0xd3).
- A second player with a name already in the room is turned away (0xc9 with room 0).
- A join from a machine running the previous helper version (wire compatibility of 3.7 to 3.9).
- Create → leave → create again on the same binding: the room id changes, member ids go on counting.

## 6. Things in the old code that look like bugs or dead code (behaviour to keep)

1. **The kick list never fills.** The constructor sets its 16 entries to −1, a kick stores an address only in an
   entry that is 0, and nothing ever sets an entry to 0: on a constructed server no address is stored and a kicked
   machine can connect again. The test (c) of 4.10 is therefore dead in practice; an entry could only be filled on
   an object used with zeroed memory. Nothing ever removes an entry either. Keep both rules exactly.
2. A dropped connection that never joined still causes a 0xcb with the name its slot holds from an earlier
   occupant (or an empty name); receivers remove a member of that name if they have one.
3. With a capacity above 16 and all slots in use, an accepted descriptor is neither stored nor closed.
4. A descriptor of exactly 0 from the accept import counts as "no connection" and is not closed.
5. `u3x_sock_accept` does not test the listening descriptor and reports every error as "none" without noting it.
6. An unknown message id is never consumed: the connection's buffer stalls for good; once it is full, each
   readable update makes a receive of length 0 whose result decides (0 = dropped).
7. The message id is read from the buffer's first four bytes even when fewer have arrived; harmless because every
   length test then fails.
8. A failed start leaves the state field as it was (a running server stays "running" with a closed listening
   socket and then calls accept on descriptor 0 every update); it still changes room id, `arg` and capacity and
   uses up a room counter value.
9. A start never clears client slots; a start on a running server with the same port keeps listening socket and
   connections and only renews room id and capacity.
10. The room counter is not limited to 16 bits: from the 65537th start on it runs into the port bits of the room
    id (bitwise OR).
11. `arg` is stored and never used.
12. Stop does not clear "joined", names, receive counts, the kick list or the next member id, and tells nobody.
13. The join reply lists only the first 10 members in slot order although 16 connections fit; the joiner itself
    can be missing from its own reply. All members still get the 0xca.
14. A member that sends a second join is told "name taken" when it uses its own name (while staying a member), or
    is renamed and announced again when it uses a free name.
15. Member data length is stored unlimited (u32) but leaves as u8 in member records and as u32 in 0xce; the data
    copy is always 0x40 bytes.
16. 0xcd, 0xd2, 0xd3 and 0xd1 are accepted from a connection that has not joined; 0xd1 from anyone (no host test,
    self-kick possible); the u64 of the 0xcc is the sender's, unchecked. The server does not close a kicked
    connection itself.
17. After a receive error the slot stays in use (with a closed record) until the next update's poll drops it;
    messages already in its buffer are handled in between, and broadcasts skip nothing (a send to its closed
    record simply fails).
18. When the peer closes (receive result 0) complete messages still in the buffer are discarded.
19. Send results are ignored everywhere, and every send waits (`block` = 1) on the game's main thread; no option
    (timeout, no-delay) is set on an accepted connection.
20. Only one connection is accepted per update.

## 7. What stays as it is

- The exported names and signatures of 2.1: group G10 and the C part call them.
- The server object and client slot layouts (3.4, 3.5): G10 embeds the object in its binding and reads three of
  its fields directly; the C part asserts sizes and offsets. In today's file the type is shared by name with
  G10's binding structure: the integrator keeps one definition.
- The wire formats of 3.7 to 3.9 and the room id's bit layout (3.6): the room client (G08), the join / discovery
  code (G07, G10) and older helper versions on other machines depend on them.
- The diagnostic words eb[76] and eb[77] (boot log, test rigs).
- The neighbours of 2.2 (group G07 / existing functions).
- Everything in section 6.

## 8. Open questions

1. `u3x_sock_accept` belongs to the socket class (G07 block A) by its place in the file, but G07's spec leaves it
   out ("accept: not used by this group") and it is in no work group; it is described here because this group is
   its only caller. The integrator decides in which part file it lands.
2. `u3x_lnb_srv_ctor`, `u3x_lnb_srv_stop` and `u3x_lnb_srv_count` are in no work group and `u3x_lnb_srv_running` is
   in the inventory's "trivial" list; all four are included here so that the whole room server is written from
   this description. If the integrator wants the trivial one left alone, 4.2's first sentence is all it does.
3. A receive of length 0 (full buffer, 4.10 step 2) has no rule of its own in the old code; what the platform's
   receive import answers for it was not looked into (both platform layers pass it through). The new code makes
   the same call.
4. Whether the 0x478-byte reply is built in a static buffer (as today) or on the stack is left to the implementer;
   if the helper's stack budget on the game's main thread is a concern, use a U3X_BSS buffer.

## 9. Clarifications (tester, 2026-11-14)

Facts settled by the old-against-new test (`reimpl2/G09/test_g09.cpp`), in words. Each one holds for the old code;
the new part does the same. Nothing in sections 1 to 7 was found wrong.

1. **The kick list and the address 255.255.255.255.** The kick list test of 4.10 (c) is a plain compare of two 32-bit
   values, and the entries of a constructed list are −1: a connection whose adopted address is 0xffffffff equals them
   and is refused. Scenario 21's "on a constructed object a peer is never refused for this reason" holds for every
   other address. (No real peer has this address.)
2. **The refusal tests look at the adopted record.** Both the duplicate test (a) and the kick list test (c) of 4.10
   use the address as `u3x_sock_adopt` left it (a loopback or empty peer address already replaced by this machine's
   own), never the address the accept returned. So a loopback connection is a duplicate of a slot in use that holds
   this machine's address and the same port, and a kick list entry that holds the loopback address itself never
   matches a loopback connection. A slot that is not in use never makes a duplicate, whatever its record still holds.
3. **Ranges of the neighbours' results.** Listen (4.3): every result of 0 or more counts as success (state = 1; the
   result is returned as it is), every negative one as failure. Poll (4.10): every result above 0 means "readable",
   every negative one drops the slot. Receive (4.10): every negative result, not only −1, is "nothing here".
4. **Open question 3 (a receive into a full buffer).** With 0x800 bytes waiting, the receive call is made with the
   address just behind the buffer's last byte, a length of 0 and `block` = 1, and its result is treated like any
   other: 0 drops the slot, a negative one leaves it in use. The new part makes the identical call. What the
   platform's receive import answers to a length of 0 is not decided by this test (live game).
5. **Open question 4 (where the reply is built).** Old and new both build the 0x478-byte join reply in a static buffer
   of the group. The test compares its bytes at the time of each send only; they are equal.
6. **Open questions 1 and 2 (the strays).** The test covers all eight exported functions, the four strays and
   `u3x_sock_accept` included, so the comparison holds wherever the integrator places them.
7. **The handler on a slot that is not in use** (reachable only by calling `u3x_lnb_srv_msg` directly; the update
   never does): every message is handled as for a slot in use. A join stores the fields and sets "joined", but the
   slot is not a member, so it is missing from its own reply's member list; the reply is still sent to the slot's
   record and the 0xca goes to the members.
8. **Counters wrap.** The room counter, the next member id and the two diagnostic counters are plain 32-bit values:
   0xffffffff + 1 is 0, with no special case.
9. **The accept's length word.** The import is given a length of 16 on every call; what it stores there (and in the
   first two bytes of the address) is ignored.
