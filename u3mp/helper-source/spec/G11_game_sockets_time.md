# G11. Game sockets, packet pump, net clock block, time sync

Behaviour description for the clean-room pipeline (reimpl2, describer's document). It says what the functions do, not
how the old ones were written. Source of the facts: helper v7.212, the C functions `gt_open1`, `gt_recvfrom`,
`gt_sendto`, `gt_open`, `gt_entry_reset`, `gt_sync_request`, `gt_pump`, `gt_frame`, `gt_net_time_block`,
`gt_master_channel` and the C++ functions `u3x_context_push`, `u3x_context_pop`, `u3x_time_synced`,
`u3x_round_start`, `u3x_time_sample` (198 code lines), all their callers, and the shipped machine code of the float
parts (for the rounding points of the old build, section 4.16).

Conventions of this document: "image offset X" is the address `base + X` (`base` = game image base). Offsets with 0x
are hexadecimal, others decimal. All values are little endian. "u8 / u16 / u32 / u64 / s32" are unsigned / signed
integers of that width; "pointer" is 8 bytes; "float" is a 32-bit IEEE single. All 32-bit integer arithmetic wraps
modulo 2^32 unless a line says otherwise. Game functions use the System V x86-64 convention. Names of structures and
fields follow `G07_lan_sockets.md` (ExtState, socket import table, socket address, connection table and entry,
session, member slot) and `G12_packets_monitor.md` (sub-object, bit stream, net player record); what those documents
already describe is referred to, not repeated.

The group has 15 functions in four blocks:

| Block | Functions (old names) |
|---|---|
| A. game sockets | C: `gt_open1`, `gt_recvfrom`, `gt_sendto`, `gt_open`, `gt_entry_reset` |
| B. net frame and packet pump | `u3x_context_push`, `u3x_context_pop`; C: `gt_frame`, `gt_pump`, `gt_master_channel` |
| C. net clock block | C: `gt_net_time_block` |
| D. time sync | C: `gt_sync_request`; `u3x_time_sample`, `u3x_time_synced`, `u3x_round_start` |

The group is under 250 lines and can be written by one implementer. If two work at once, the clean split is
blocks A + B (part 1) and blocks C + D (part 2): part 2 uses of part 1 only `u3x_gt_sendto` (2.2), by its signature.

## 1. Purpose and callers

The PS4 image kept the game's connection entries and the game halves of each connection (what a packet carries),
but lost everything that moves packets and the code that drives the net clock. This group is the helper's
replacement: two UDP sockets, a pump that runs once per frame, the per-frame advance of the net clock, and the
time sync by which a guest sets its net clock to the match host's. Everything runs on the game's main thread; the
only other thread that looks at this group's state is the account thread's status line, which reads (never writes)
the sample count and the round-trip words (3.4, 3.5).

**A. Game sockets.**

- `gt_open1`: opens one non-blocking UDP socket bound to a port. Callers: `gt_open` (ports 3233 and 3232) and the
  peer link's open (`pl_open`, C, other group: its own socket record, port tried twice with a close between).
- `gt_recvfrom` / `gt_sendto`: one datagram in / out on a socket record. Callers: the pump, the master channel,
  the sync request (this group); the peer link (`pl_recv`, `pl_send`, `pl_sig_to`, `pl_tick`: always with the peer
  link's own record) and the NAT probe (`onb_probe_tick`: sends 16 bytes on the game socket), all C, other groups.
- `gt_open`: opens the two game sockets and resets the transport's state. Callers: the C export `u3h_gt_open`,
  used by the LAN NetInit (`u3x_lan_netinit`, G08: a non-zero result stops NetInit unless LAN solo) and by the
  online NetInit (other group: non-zero = NetInit failed). NetInit can run twice; the second call must not open
  anything again.
- `gt_entry_reset`: clears the transport's timers of one connection entry. Only caller: the connection table's
  entry reset (`lnb_cm_reset_entry`, G07 4.9; G07 calls it `u3h_gt_entry_reset`).

**B. Net frame and pump.**

- `gt_frame`: the net frame. Caller: the C export `u3h_lnb_frame_parts(base, s, 0)`, called by the LAN tick
  (`u3x_lan_tick`, G08) once per game frame while the net-active byte (image 0x17d5c61) is non-zero; it calls
  `gt_frame` first, then `gt_net_time_block` (block C), then functions of other groups.
- `gt_pump`: receives and answers the waiting datagrams, then sends what each connection has to send. Only
  caller: `gt_frame`.
- `u3x_context_push` / `u3x_context_pop`: put one entry on the game's per-thread allocator context stack and take
  it off again. Only callers: the C wrappers `gt_ctx_push` / `gt_ctx_pop`, which only `gt_frame` uses, around the
  packet work (the game's packet code allocates from the top entry of that stack, and the helper's call point has
  none). The wrappers' own checks become part of the new frame function (4.7); see section 9.
- `gt_master_channel`: empties the second socket. Caller: `u3h_lnb_frame_parts(base, s, 1)`, later in the same LAN
  tick, only while a session manager is registered.

**C. Net clock block.** `gt_net_time_block`: advances the game's net time by the frame time and sets the net clock
to it. Caller: `u3h_lnb_frame_parts(base, s, 0)`, right after `gt_frame`, every frame.

**D. Time sync.**

- `gt_sync_request`: a guest asks the match host for its net time. Caller: the C export `u3h_sync_request`,
  called by the connection monitor (`u3x_connection_monitor`, G12 section 1) once per net frame, between its
  member check and its player check, so before the pump of the same frame.
- `u3x_time_sample`: takes one answer of the host. Caller: the pump (packet type 3), through the C wrapper
  `gt_sample` today.
- `u3x_time_synced`: runs once after each completed sync. Caller: the net clock block (through the C wrapper
  `rs_synced` today).
- `u3x_round_start`: the helper's hook on the game's round start event (stub kind 236: the game is about to call
  its "round started" function with a pointer to the start time). Caller: the C dispatcher (`f4_kind`, kind 236:
  called only with a non-null register block; the dispatcher returns this function's result, and 0 means "the
  game's call runs"), through the C wrapper `gt_round_start`.

## 2. Interface

`Log` is the type of CONVENTIONS.h: `void (*)(uintptr_t base, const char *line)`. No function of this group takes
or returns a float by value.

### 2.1 Functions that keep their exported name and signature

All `extern "C"`. Their C wrappers (`gt_ctx_push`, `gt_ctx_pop`, `rs_synced`, `gt_round_start`, `gt_sample`) and C
declarations stay as they are until the callers have moved.

| Function | Signature | Result |
|---|---|---|
| context push | `int u3x_context_push(uint8_t *context, uintptr_t allocator, uintptr_t name)` | 1 pushed, 0 stack full |
| context pop | `void u3x_context_pop(uint8_t *context)` | none |
| sync completed | `void u3x_time_synced(uintptr_t base, int32_t offset)` | none |
| round start hook | `int u3x_round_start(uintptr_t base, const uint64_t *r)` | always 0 |
| time sample | `void u3x_time_sample(uintptr_t base, int32_t t0, int32_t th)` | none |

`context`: an allocator context (3.9), never null. `allocator`, `name`: two addresses stored as they are. `offset`:
the clock offset of the completed sync, net ticks. `r`: the stub's block of nine saved registers, 8 bytes each, in
the order r11, r10, r9, r8, rdi, rsi, rdx, rcx, rax; only `r[6]` (rdx) is used. `t0`: the net time this machine put
in its request; `th`: the host's net time in the answer.

### 2.2 Functions still in C: proposed new names

| Old C function | Proposed `extern "C"` signature | The one-line C wrapper passes |
|---|---|---|
| `gt_open1(g, port)` | `int32_t u3x_gt_socket_open(void *sock, uint32_t port)` | the record, the port |
| `gt_recvfrom(g, b, n, ip, port)` | `int32_t u3x_gt_recvfrom(void *sock, void *buffer, uint32_t size, uint32_t *ip, uint32_t *port)` | its five arguments unchanged |
| `gt_sendto(g, b, n, ip, port)` | `int32_t u3x_gt_sendto(void *sock, const void *data, uint32_t size, uint32_t ip, uint32_t port)` | its five arguments unchanged |
| `gt_open(base)` | `int32_t u3x_gt_open(uintptr_t base)` | base |
| `gt_entry_reset(e)` | `void u3x_gt_entry_reset(uint8_t *entry)` | the entry pointer |
| `gt_sync_request(base)` | `void u3x_gt_sync_request(uintptr_t base)` | base |
| `gt_pump(base)` | `void u3x_gt_pump(uintptr_t base, Log log)` | base and the C part's boot-log function `ol01_print` |
| `gt_frame(base)` | `void u3x_gt_frame(uintptr_t base, Log log)` | base and `ol01_print` |
| `gt_net_time_block(base)` | `void u3x_gt_net_time(uintptr_t base)` | base |
| `gt_master_channel(base)` | `void u3x_gt_master_channel(uintptr_t base)` | base |

Results: `u3x_gt_socket_open` 0 open, −1 failed; `u3x_gt_recvfrom` / `u3x_gt_sendto` the byte count, or a value
below 1 (4.2, 4.3); `u3x_gt_open` 0 when both sockets are open, else −1. `sock` is a socket record (3.3). `ip` is
an IPv4 address in socket-address form, `port` a port in host order. `log` is only handed on to `u3x_ev_scan`.

Inside the group the new functions call each other directly (`u3x_gt_frame` calls `u3x_gt_pump`, the pump calls
`u3x_time_sample`, the net clock block calls `u3x_time_synced`, and so on): the effects are the same as through the
C wrappers.

### 2.3 Where the state lives

Shared with C code that stays, so it stays in the C part and is reached through one **new export to add**,
`uint8_t *u3h_gt_state(int which)`:

| which | Returns the address of | Other C users |
|---|---|---|
| 0 | the two socket records (3.3), 16 bytes each: [0] the **game socket** (port 3233), [1] the **master socket** (port 3232) | the sockets' close at NetShutdown; the NAT probe |
| 1 | the 32 timer records (3.4), 16 bytes each, index = connection entry index | the status line (reads the round-trip word) |
| any other | the next-request time of the time sync (u32, a float pattern, seconds) | the sync reset (stores 0) |

Already exported: `u3h_time_sync(which)` (3.5) for the sample count, the samples, the random state, the resync
flag and the three round-start words.

Used by this group only; the new code keeps them as its own `U3X_BSS` variables (all zero at start, never reset
except as section 4 says):

- the packet buffer: 0x2400 bytes, aligned for 32-bit words. It is static because the game thread's stack must
  not carry it;
- the table pointer: address of the connection table once `u3x_gt_open` ran, null before;
- the frame toggle (one byte, 0 or 1);
- the "rate was zeroed" flag (one byte);
- the "syncs seen" word (u32).

### 2.4 Functions of the C part and of other groups that the new code calls

Not to be described or reimplemented here. "Exists" = exported today.

| Name | Signature | What it does |
|---|---|---|
| `u3h_ext` | `uint8_t *u3h_ext(void)` | address of ExtState (3.1). Exists. |
| `u3h_net_pick` | `void u3h_net_pick(void)` | decides once which platform fills the socket import table. Exists. |
| `u3x_sock_setopt` | `int32_t u3x_sock_setopt(int fd, int level, int opt, int32_t v)` | one call of the set-option import with a 4-byte value; its result (negative = error). Exists. |
| `u3x_sock_bind` | `int32_t u3x_sock_bind(int fd, uint32_t port)` | one call of the bind import with the "any" address and `port`; its result. Exists. |
| `u3h_gt_state` | `uint8_t *u3h_gt_state(int which)` | 2.3. **New export to add.** |
| `u3h_pl_send` | `int32_t u3h_pl_send(const void *data, uint32_t size, uint32_t ip, uint32_t port)` | the peer link's send: −2 when no peer link is installed or it is not open ("not taken"); otherwise the packet went (or failed) over the peer link and the result is final: bytes of `data` sent, or a value below 1. **New export to add** (one line: call the C part's `pl_send_fn` when it is set, else −2). |
| `u3h_pl_recv` | `int32_t u3h_pl_recv(uintptr_t base, void *buffer, uint32_t size, uint32_t *ip, uint32_t *port)` | the peer link's receive: −2 when no peer link is installed or it is not open; otherwise it runs the peer link's own per-call work and returns the next game datagram (length, with `*ip` / `*port` set) or a value below 1 when none waits. **New export to add** (one line: `pl_recv_fn` when set, else −2). |
| `u3x_gt_write` | `void u3x_gt_write(GtStream *s, uint32_t value, int count)` | appends the `count` low bits of `value` to a bit stream (3.10), most significant first; bits past the stream's end are dropped, the position still advances. Exists. |
| `u3x_gt_read` | `uint32_t u3x_gt_read(GtStream *s, int count)` | reads `count` bits (first bit read = most significant bit of the result); bits past the end read as 0. Exists. |
| `u3x_gt_bytes` | `uint32_t u3x_gt_bytes(const GtStream *s)` | bytes to send: the bits used (at most the stream's size) rounded up to whole 32-bit words, times 4. Exists. |
| `u3x_entry_by_address` | `uint8_t *u3x_entry_by_address(uint8_t *entries, uint32_t ip, uint32_t port)` | given the address of connection entry 0: the first of the 32 entries that is in use and whose address equals `ip` and whose port equals the low 16 bits of `port`; null when none. Exists. |
| `u3x_find_member` | `uint8_t *u3x_find_member(uint8_t *session, uint32_t id)` | the first member slot in use with that member id, or null. Exists. |
| `u3x_connection_monitor` | `void u3x_connection_monitor(uintptr_t base, int arg)` | the connection monitor (G12 section 1); it calls `u3h_sync_request(base)`, which forwards to this group's sync request. Exists. |
| `u3x_gt_base_read` | `int u3x_gt_base_read(uintptr_t base, uint8_t *sub, void *b)` | handles a received data packet whose stream stands behind the type byte: stamps the receive time, reads the reliable header (G12 4.7) and lets the game read the payload. Result unused here. Exists. |
| `u3x_gt_base_write` | `void u3x_gt_base_write(uintptr_t base, uint8_t *sub, void *b)` | G12 4.6. Exists. |
| `u3x_gt_drop_expired` | `void u3x_gt_drop_expired(uintptr_t base, uint8_t *sub)` | G12 4.8. Exists. |
| `u3x_gt_has_data` | `int u3x_gt_has_data(uintptr_t base, uint8_t *sub)` | G12 4.9 (it can move a timer of the sub-object, so WHEN it is called is part of the behaviour). Exists. |
| `u3x_ev_scan` | `void u3x_ev_scan(uintptr_t b, uint32_t peer, int32_t id, const uint8_t *sub, uint32_t queue, uint32_t frame, Log log)` | diagnostic: logs the kinds of events in one queue of a connection (queue 0 reliable, 1 unreliable, 2 received); `peer` = entry index, `id` = the entry's handle word, `frame` = the pump frame counter. Exists. |
| `u3x_sync_random` | `uint32_t u3x_sync_random(uint32_t *state)` | steps the random state (state = state · 1103515245 + 12345) and returns the new state shifted right by 8 bits. Exists. |
| `u3x_net_clock` | `int32_t u3x_net_clock(uintptr_t base)` | the net clock's time: the s32 at offset 0xc of the object the pointer at image 0x17c3580 points to, or 0 when that pointer is null. Exists. |
| `u3h_time_sync` | `uint32_t *u3h_time_sync(int which)` | addresses of the sync state (3.5). Exists. |
| `u3h_sync_reset` | `void u3h_sync_reset(void)` | restarts the time sync: next-request time 0, sample count 0, eb[115] + 1. Exists. |
| `u3h_so_note` | `void u3h_so_note(const char *a, const char *name, int32_t v, const char *b)` | queues one line for the helper's log: its prefix, then `a`, then `name` (when non-null), then `v` in decimal (only when `v` ≥ 0), then `b` (when non-null). Does nothing while no account session memory exists. Exists. |

`GtStream` is the stream type of 3.10, defined in the C++ part before this group's code. The stream functions take
a `GtStream *`; `u3x_gt_base_read` / `u3x_gt_base_write` take the same pointer as `void *`.

## 3. Game, platform and helper facts

### 3.1 ExtState (address from `u3h_ext()`)

The socket import table is at ExtState + 0x200, entry k at + 8·k (G07 3.2); an entry may be null. Entries used
here, all returning s32 (negative = error):

| k | Import | Arguments |
|---|---|---|
| 0 | socket | (pointer to a NUL-terminated debug name, int family, int type, int protocol) → descriptor |
| 2 | bind | used through `u3x_sock_bind`; only tested for null here |
| 3 | send-to | (int descriptor, pointer to data, u32 length, int flags, pointer to socket address, u32 address length) → bytes sent |
| 4 | receive-from | (int descriptor, pointer to buffer, u32 buffer size, int flags, pointer to socket address (output), pointer to u32 address length (in: 16, out: ignored)) → bytes received |
| 5 | set option | used through `u3x_sock_setopt`; only tested for null here |

Diagnostic words eb[k] (u32 at ExtState + 0x4050 + 4·k) of this group:

| k | Meaning |
|---|---|
| 0 | switches; bit 3 (value 8) = "net time off": no net clock block, no time sync |
| 96 | sockets open: bit 0 game socket, bit 1 master socket |
| 97 | last socket open error |
| 98 | datagrams received by the pump |
| 99 | datagrams sent by the send-to import |
| 100 | data packets received |
| 105 | pings sent |
| 106 | pongs received |
| 107 | time requests answered |
| 108 | time samples taken |
| 109 | time syncs completed |
| 110 | offset of the last completed sync (s32 stored as u32) |
| 111 | datagrams dropped (unknown sender while online, or unknown type) |
| 112 | master-channel datagrams dropped |
| 113 | net frames run |
| 114 | net clock block runs |
| 117 | size of the last datagram received by the pump |
| 119 | payload bytes sent by the send-to import |

(eb[101] to [104], [116], [118] belong to G12, eb[102] to `u3x_gt_base_read`, eb[115] to `u3h_sync_reset`.)

### 3.2 Socket API values

Family IPv4 = 2; type datagram = 2; protocol 0; option level "socket" = 0xffff; option "non-blocking" = 0x1200;
flags 0. Socket address (16 bytes, G07 3.2): u8 16; u8 2; u16 port, **big-endian**, from the low 16 bits of the
port number; u32 IPv4 address as stored; 8 zero bytes. Address length 16. The debug name given to the socket
import by this group is the text `u3mp-game`.

Ports: 3233 = game traffic (the port every peer's connection entry carries on LAN, G07 3.5); 3232 = the original
game's master-server channel.

### 3.3 The socket record (16 bytes; the helper's own structure, shared with the C part)

| Offset | Type | Meaning |
|---|---|---|
| 0 | s32 | descriptor; usable when > 0 |
| 4 | u32 | open: 1 after a successful open, else 0 |
| 8 | u32 | bytes sent, with 0x34 added per datagram (an estimate of the headers) |
| 0xc | u32 | bytes received, with 0x34 added per datagram |

The C part's close of a record (stays in C) calls the close import when the descriptor is > 0 and sets descriptor
and open to 0.

### 3.4 The timer record (16 bytes; one per connection entry, same index)

| Offset | Type | Meaning |
|---|---|---|
| 0 | float | game time in seconds at which the next ping is due |
| 4 | float | game time at which the next data packet may go |
| 8 | float | game time at which a data packet goes even when there is nothing to send (keep-alive) |
| 0xc | s32 | last measured round trip, in units of 2 ms (the status line prints twice this value as milliseconds) |

### 3.5 Time sync state (`u3h_time_sync(which)`)

| which | Type at the returned address | Meaning |
|---|---|---|
| 0 | s32 | number of samples taken (0..8) |
| 1 | 8 sample records of 16 bytes | each: 8 bytes never written (zero), s32 at +8 = half round trip, s32 at +0xc = clock difference, both in net ticks |
| 2 | u32 | random state (for `u3x_sync_random`) |
| 3 | u8 | resync flag: the next completed sync also moves the players' time stamps |
| 4 | s32 | round-start fix: the host's start time that was replaced |
| 5 | s32 | round-start fix: this machine's time that was used instead |
| any other (the code uses 6) | u8 | round-start fix pending |

A net tick is 1/300 s.

### 3.6 Game data and functions

| Image offset | Type | Meaning |
|---|---|---|
| 0x16e716c | float | net-time scale (the game's cinema speed; 1.0 in normal play) |
| 0x17c3571 | u8 | net time paused |
| 0x17c3572 | u8 | the value the paused byte takes after each unpaused frame |
| 0x17c3574 | float | net time fraction: the part of a tick not yet counted |
| 0x17c3578 | s32 | net time, in ticks: what the net clock is set to each frame |
| 0x17c3580 | pointer | the net clock object, or null. Its float at +4 is its rate; its s32 at +0xc is its time (what `u3x_net_clock` returns) |
| 0x17cb060 | object | the net game manager. s32 at +0x66ec, +0x66f0, +0x66f4: three time words that the game sets from the round start time (+0x66ec is the round start itself). Net player record i (i = 0..9) at +0x390 + i·0x9c0 |
| 0x17d5c60 | u8 | LAN mode (non-zero) / online (0) |
| 0x17d5c61 | u8 | net-active |
| 0x17d5c64 | s32 | start time of the running match or round, in net ticks (the HUD clock counts from it) |
| 0x17d6420 | object | the match session (3.8) |
| 0x17f31b0 | object | the connection table (G07 3.5); entry i at + 8 + i·0x220 |
| 0x1841850 | pointer | the game's network allocator (entry 0x14 of the allocator table at image 0x18417b0), or null |
| 0x13ac250 | text | the game's empty string (used as the context entry's name) |
| 0x1b24b98 | pointer | the frame clock object. Its float at +0xc is the frame time in seconds; its s32 at +0x114 is the game time in ticks of 1/300 s |

| Image offset | Function | Arguments, result |
|---|---|---|
| 0xda0d60 | this thread's allocator context | none → pointer to the context (3.9), or null |
| 0x1196cb0 | set a clock | (pointer to the clock object, pointer to an s32 time, int flag); no result used. Called with flag 1: it sets the clock's time from the s32 and its rate to 1.0 |

Net player record fields used here (G12 3.1 has the rest): u8 at +0xe1 = joined; s32 at +0x39c, +0x3a0, +0x3a8 =
three time stamps on the net clock; u32 at +0x5f0 and +0x5f4 = two "next send" times.

### 3.7 Connection entry fields used here

Entry layout in G07 3.5; this group uses: s32 at 0 (connection id), u8 at 4 (in use), u32 at 0x2c (peer address),
u16 at 0x30 (peer port), s32 at 0x34 (handle), and the sub-object at 0x40 (G12 3.6), of which this group itself
writes two counters: u32 at entry + 0xf4 (sub + 0xb4) = bytes sent, u32 at entry + 0xf8 (sub + 0xb8) = bytes
received.

### 3.8 Session fields used here

u64 at +0x18 room id (0 = no session); u32 at +0x12e4 own member id; u32 at +0x12e8 host member id. Member slot:
s32 at +0x34 = connection id. "This machine is the match host" = the room id is 0, or the own member id equals the
host member id.

### 3.9 The allocator context

The object the game function 0xda0d60 returns holds a stack of entries of five 8-byte words, entry k at context +
40·k, and the u32 number of entries at context + 0x730. The stack holds at most 0x2e entries. A pushed entry is
{allocator, name, 0, name, 1} (this is the layout the game's own push produces).

### 3.10 The bit stream and the packets on the wire

`struct GtStream { uint32_t *words; uint32_t pos, max, x, pad; }` (0x18 bytes): 32-bit words filled from the most
significant bit; `pos` = bits used, `max` = size in bits. "A stream over a buffer of N bytes" below means: words =
the buffer, pos = 0, max = 8·N, x = 0, pad = 0. A datagram is the stream's words as they lie in memory (each word
little endian), `u3x_gt_bytes` bytes long; so the first 8 stream bits are the FOURTH byte of the datagram.

Every packet starts with 8 bits of type:

| Type | Name | Following bits | Datagram |
|---|---|---|---|
| 2 | time request | 32: the sender's net time (image 0x17c3578) | 8 bytes |
| 3 | time answer | 32: the request's value, echoed; 32: the answering machine's net time | 12 bytes |
| 4 | ping | 32: the sender's game time in seconds (float pattern) | 8 bytes |
| 5 | pong | 32: the ping's value, echoed | 8 bytes |
| 0 | data | the reliable header and payload (`u3x_gt_base_write`), then 1 bit 0 ("no voice data") | up to 0x2400 bytes |

Unused bits of the last word are 0. Example: a time request with net time 0x11223344 is the bytes 33 22 11 02 00 00
00 44.

### 3.11 Float constants

| Pattern | Value | Use |
|---|---|---|
| 0x3b5a740e | 1/300 (0.0033333334) | seconds per tick |
| 0x37800000 | 2^−16 | random scale |
| 0x3e99999a | 0.3 | random spread of the sync request, seconds |
| 0x3dcccccd | 0.1 | least time between sync requests, seconds |
| 0x43fa0000 | 500.0 | seconds to round-trip units |
| 0x41200000 | 10.0 | ping interval, seconds |
| 0x3d4ccccd | 0.05 | data packet interval, seconds |
| 0x40000000 | 2.0 | keep-alive interval, seconds |
| 0x43960000 | 300.0 | ticks per second |
| 0x3f800000 | 1.0 | clock rate |

### 3.12 Texts (the helper's own; each passed as `a` to `u3h_so_note` with `name` and `b` null)

| Id | Text (between the quotes, spaces included) |
|---|---|
| N1 | `"net time synced with the match host, offset (1/300 s) "` |
| N2 | `"round start set back to the host's time "` |
| N3 | `"round start (this PC is the match host), net time "` |
| N4 | `"round start from the match host, its net time "` |
| N5 | `"  ... this PC's net time "` |
| N6 | `"  ... time samples so far "` |
| N7 | `"  ... the start is ahead of this PC's clock (grenades would stay locked): this PC's time is used until the clocks are synced; ticks ahead "` |

## 4. Behaviour

Where an order is given it is observable (through an import, a game function, another group's function or a
counter) and must be kept.

### 4.0 Float rules of the new code

The project's decision for new float code applies: every float is handled as its 32-bit pattern, and **every
operation is one single-precision operation whose result is rounded to a 32-bit float (round to nearest even)
before it is used again**: the `f_add`, `f_sub`, `f_mul`, `f_from_int`, `f_to_int`, `f_less` helpers of
CONVENTIONS.h. Nothing is kept at higher precision between two operations. The formulas below give the order of
the operations; do not regroup them. Where the old build behaved differently in the last bits is listed in 4.16,
not here.

- **NOW** (game time in seconds) = `f_mul(f_from_int(T), 0x3b5a740e)`, T = the s32 at offset 0x114 of the frame
  clock object (pointer at image 0x1b24b98, used without a null test).
- "a < b" is `f_less(a, b)`: false when either is NaN. "a ≤ b" is true when a < b or a equals b as numbers (+0
  equals −0), false when either is NaN. (Neither NOW nor a timer can be NaN: 6.14.)
- "the integer part of x" is `f_to_int(x)`: truncation toward zero; NaN and values outside the s32 range give
  0x80000000.

### 4.1 Open one socket (`gt_open1`)

1. When the record's open field is non-zero and its descriptor is > 0: result 0, nothing else (no call at all).
2. Otherwise set all 16 bytes of the record to 0, then call `u3h_net_pick`.
3. When the socket, set-option or bind import is null: result −1 (the record stays zero).
4. Create a socket: name `u3mp-game`, family 2, type 2, protocol 0. Negative result: eb[97] = that result, result
   −1 (record still zero).
5. Store the descriptor in the record. Set option "non-blocking" to 1 (`u3x_sock_setopt`); when that did not fail,
   bind to `port` (`u3x_sock_bind`). When either returned a negative value: eb[97] = that value, result −1; the
   descriptor stays in the record, the open field stays 0, nothing is closed.
6. Open field = 1, result 0. The two byte counters are 0 after an open.

### 4.2 Receive one datagram (`gt_recvfrom`)

- Descriptor ≤ 0 or receive-from import null: result −1, no call, `*ip` and `*port` untouched.
- Otherwise one call of the import with (descriptor, `buffer`, `size`, flags 0, a 16-byte address area set to
  zero, a u32 holding 16). Its result is the function's result.
- Only when the result is > 0: `*ip` = the address area's u32 address; `*port` = its port converted from big-endian
  (0..65535); the record's bytes-received field grows by the result + 0x34.

### 4.3 Send one datagram (`gt_sendto`)

1. When `sock` is the game socket record (the address `u3h_gt_state(0)` returns; a comparison of addresses):
   call `u3h_pl_send(data, size, ip, port)`. A result other than −2 is the function's result and nothing else
   happens (no counter changes, also when that result is an error).
2. Descriptor ≤ 0 or send-to import null: result −1.
3. One call of the import with (descriptor, `data`, `size`, flags 0, the socket address of (`ip`, `port`), 16). Its
   result is the function's result.
4. Only when the result is > 0: the record's bytes-sent field grows by the result + 0x34; eb[99] + 1; eb[119] grows
   by the result.

For any other record step 1 is skipped (`u3h_pl_send` is not called).

### 4.4 Open the game sockets (`gt_open`)

1. eb[96] = 0.
2. Open the game socket record on port 3233 (4.1). Success: eb[96] bit 0 set.
3. Only when step 2 succeeded: open the master socket record on port 3232. Success: eb[96] bit 1 set.
4. In every case, after that: sample count (`u3h_time_sync(0)`) = 0; next-request time = 0 (pattern 0); all 32
   timer records set to zero (the round-trip words too); the table pointer = image offset 0x17f31b0.
5. Result 0 when both opens succeeded, else −1.

The samples, the random state, the round-start words and eb[115] are not touched.

### 4.5 Reset an entry's timers (`gt_entry_reset`)

Nothing happens while the table pointer is null, or when `entry` is below table + 8 or not below table + 8 +
32·0x220. Otherwise, for the timer record with index (entry − (table + 8)) / 0x220, rounded down: the three float
fields become 0; the round-trip word stays.

### 4.6 Context push and pop

- `u3x_context_push`: when the count (u32 at context + 0x730) is 0x2e or more: result 0, nothing written.
  Otherwise the entry with index = count becomes {allocator, name, 0, name, 1}, the count grows by 1, result 1.
- `u3x_context_pop`: the count shrinks by 1 (no test for 0); the five words of the entry with the new count as
  index become 0.

### 4.7 The net frame (`gt_frame`)

1. The frame toggle flips (0 → 1 → 0 ...; the first call ever makes it 1). This happens on every call, before
   any test.
2. Nothing more when the net clock pointer (image 0x17c3580) is null, or the net-active byte (image 0x17d5c61) is
   0, or the game socket record's open field is 0.
3. eb[113] + 1.
4. Allocator context: call the game function 0xda0d60. The context is "pushed" when all three hold: the result C
   is non-null; C's count (u32 at +0x730) is below 0x2e; the network allocator pointer (image 0x1841850) is
   non-null. Then `u3x_context_push(C, that allocator, base + 0x13ac250)`. When one of them fails, nothing is
   pushed (the allocator pointer is not read when C is null or full) and the frame goes on without.
5. `u3x_connection_monitor(base, toggle)` (toggle as 0 or 1).
6. The pump (4.8).
7. When step 4 pushed: `u3x_context_pop(C)`.

### 4.8 The pump (`gt_pump`)

NOW (4.0) is computed once at the start (after the monitor of 4.7 has run) and used for the whole call.

**Receiving.** At most 256 rounds; each round:

1. The whole packet buffer (0x2400 bytes) is set to zero; address and port start as 0.
2. L = `u3h_pl_recv(base, buffer, 0x23ff, &address, &port)`; when that is −2: L = receive (4.2) on the game
   socket record with the same arguments.
3. L < 1: receiving ends.
4. eb[98] + 1; eb[117] = L.
5. E = `u3x_entry_by_address(entry 0 of the connection table, address, port)` (may be none).
6. When the LAN byte (image 0x17d5c60) is 0 and there is no E: eb[111] + 1 and the round ends (online, only
   known peers are heard).
7. When there is an E: the u32 at E + 0xf8 grows by L.
8. A stream over the buffer of L bytes; the type is its first 8 bits. By type:
   - **2, time request**: read 32 bits V. Send (4.3) on the game socket record, to the datagram's address and
     port, the 12-byte time answer {8 bits 3, V, the s32 at image 0x17c3578} (built in a zeroed 16-byte area,
     length from `u3x_gt_bytes`). Then eb[107] + 1.
   - **3, time answer**: read 32 bits A, then 32 bits B. Unless eb[0] bit 3 is set: `u3x_time_sample(base, A,
     B)`.
   - **4, ping**: read 32 bits V. Send to the datagram's address and port the 8-byte pong {8 bits 5, V}.
   - **5, pong**: read 32 bits V (a float pattern). eb[106] + 1. When there is an E: its timer record's
     round-trip word = the integer part of `f_mul(f_sub(NOW, V), 0x43fa0000)`.
   - **0, data**: eb[100] + 1. When there is an E: `u3x_gt_base_read(base, E + 0x40, the stream)` (the stream
     stands behind the type bits), then `u3x_ev_scan(base, E's index, the s32 at E + 0x34, E + 0x40, 2, eb[113],
     log)`.
   - **any other**: eb[111] + 1.

   On LAN the answers to types 2 and 4 go out, and a type 3 is taken as a sample, whoever sent them (E or not).

**Sending.** Then, for each of the 32 connection entries in ascending order, with its timer record: the entry is
skipped unless its in-use byte is non-zero, its handle (s32 at +0x34) is not 0 and its port (u16 at +0x30) is not
0. For the others, with the entry's address and port as destination and sub = entry + 0x40, in this order:

1. `u3x_ev_scan(base, index, handle, sub, 0, eb[113], log)`, then the same with queue 1.
2. `u3x_gt_drop_expired(base, sub)`.
3. **Ping.** When the ping time ≤ NOW: send (4.3, game socket record) the 8-byte ping {8 bits 4, the pattern of
   NOW} (built in the first 16 bytes of the packet buffer, set to zero first); then ping time = `f_add(NOW,
   0x41200000)` and eb[105] + 1 (whatever the send returned).
4. **Data.** When the data time ≤ NOW: data time = `f_add(NOW, 0x3d4ccccd)` (stored before the next test). Then a
   data packet goes when the keep-alive time ≤ NOW, or else when `u3x_gt_has_data(base, sub)` returns non-zero
   (it is called only when the keep-alive time is not yet reached, at most once). When a packet goes: keep-alive
   time = `f_add(NOW, 0x40000000)` (stored first); the whole packet buffer is set to zero; a stream over the
   buffer of 0x23ff bytes gets 8 bits 0, then `u3x_gt_base_write(base, sub, the stream)`, then 1 bit 0; it is
   sent (4.3, game socket record) with the length `u3x_gt_bytes` gives; when the send result is > 0 the u32 at
   entry + 0xf4 grows by it.

A destination is the pair read from the entry when it passed the test.

### 4.9 The master channel (`gt_master_channel`)

Nothing when the master socket record's open field is 0. Otherwise up to 8 times: receive (4.2) on that record
into a buffer with size 0xff; a result below 1 ends it; otherwise eb[112] + 1 and the datagram is dropped unread.
(The old code used the start of the packet buffer; nothing reads it afterwards.)

### 4.10 The net clock block (`gt_net_time_block`)

1. Nothing at all when eb[0] bit 3 is set, or the net-active byte (image 0x17d5c61) is 0, or the net clock
   pointer (image 0x17c3580) is null, or the frame clock pointer (image 0x1b24b98) is null.
2. eb[114] + 1.
3. When eb[109] differs from the "syncs seen" word: the word takes eb[109]'s value and
   `u3x_time_synced(base, eb[110] as s32)` is called.
4. Only when the paused byte (image 0x17c3571) is 0, with all values read before anything is written:
   - R = the clock's rate (float at clock + 4). When R is zero (+0 or −0) and the "rate was zeroed" flag is set,
     R = 1.0.
   - P = `f_mul(f_mul(D, S), 0x43960000)`, D = the frame time (float at frame clock + 0xc), S = the scale (float
     at image 0x16e716c).
   - A = `f_add(F, f_mul(R, P))`, F = the fraction (float at image 0x17c3574).
   - W = the integer part of A.
   - Stores: the paused byte = the byte at image 0x17c3572; the net time (s32 at image 0x17c3578) grows by W; the
     fraction = `f_sub(A, f_from_int(W))`.
5. In every case that passed step 1 (paused or not): call the game function 0x1196cb0 with (the clock object, the
   address of image 0x17c3578, 1); after it returns store 0.0 (pattern 0) as the clock's rate (clock + 4) and set
   the "rate was zeroed" flag.

So the engine's own clock update never moves the net clock (rate 0); it moves only by step 4, and step 4 treats
the 0 that step 5 left as 1.0.

### 4.11 The time sync request (`gt_sync_request`)

Nothing happens unless ALL of these hold (all are plain reads; nothing is written or called when one fails):

- the next-request time < NOW;
- the sample count (`u3h_time_sync(0)`) is below 8;
- eb[0] bit 3 is clear;
- the match session (image 0x17d6420) has a room id;
- its own member (`u3x_find_member` with the own member id) and its host member (with the host member id) both
  exist and are different slots (so this machine is a guest);
- the host member's connection id (s32 at slot + 0x34) is not negative;
- a connection entry with that id exists: the FIRST of the 32 entries (ascending) whose id field equals it, in use
  or not, is the one looked at, and it must have: in-use byte non-zero, handle (s32 at +0x34) not −1, port not 0.

Then, in this order:

1. r = the low 16 bits of `u3x_sync_random(random state)`.
2. Next-request time = `f_add(f_add(f_mul(f_mul(f_from_int(r), 0x37800000), 0x3e99999a), NOW), 0x3dcccccd)`:
   NOW plus 0.1 s plus a random part below 0.3 s.
3. Send (4.3, game socket record), to that entry's address and port, the 8-byte time request {8 bits 2, the s32
   at image 0x17c3578} (first 16 bytes of the packet buffer set to zero first). The result is ignored.

### 4.12 One time sample (`u3x_time_sample`)

With k = the sample count and C = `u3x_net_clock(base)`:

1. k ≥ 8: nothing.
2. d = C − t0 (32-bit, wrapping); h = d / 2 rounded toward zero. Sample k's half-round-trip word = h. This store
   happens before the next test.
3. h ≤ 0 (that is d < 2): nothing more; the count stays (the answer is not used).
4. Sample k's clock-difference word = (th − C) + h. eb[108] + 1.
5. When k is 7 (this is the eighth sample), the sync completes:
   - the 8 sample records are put in ascending order of their half-round-trip word (signed), records with equal
     words keeping their order (whole 16-byte records move);
   - M = (the sum of the clock-difference words of the first four records, wrapping) / 4, rounded toward zero;
   - the net time (s32 at image 0x17c3578) = C + M; eb[109] + 1; eb[110] = M;
   - when the resync flag (`u3h_time_sync(3)`) is non-zero: it is cleared, and for each of the 10 net player
     records whose joined byte (+0xe1) is non-zero: each of the s32 at +0x39c, +0x3a0, +0x3a8 that is not 0 grows
     by M; then the u32 at +0x5f0 and at +0x5f4 become 0.
6. The count grows by 1 (7 becomes 8: no further sample until `u3h_sync_reset`).

### 4.13 After a completed sync (`u3x_time_synced`)

1. `u3h_so_note(N1, null, offset, null)`.
2. When the pending byte (`u3h_time_sync(6)`) is 0: done. Otherwise it is set to 0 at once.
3. H = the host's start time (`u3h_time_sync(4)`), U = the used time (`u3h_time_sync(5)`). When H is greater than
   `u3x_net_clock(base)` (signed comparison): done (the pending byte stays 0).
4. Each of these four s32 that equals U becomes H, each tested on its own, in this order: net game manager
   +0x66ec, +0x66f0, +0x66f4, then image 0x17d5c64.
5. `u3h_so_note(N2, null, H, null)`.

### 4.14 The round start hook (`u3x_round_start`)

The result is 0 in every case. P = the pointer in `r[6]`.

1. P null: nothing.
2. G = the s32 P points to; C = `u3x_net_clock(base)`.
3. This machine is the match host (3.8, on the session at image 0x17d6420): `u3h_so_note(N3, null, G, null)`;
   done.
4. Otherwise (a guest), in this order: `u3h_so_note(N4, null, G, null)`; `u3h_so_note(N5, null, C, null)`;
   `u3h_so_note(N6, null, the sample count, null)`.
5. When G is greater than C (signed comparison): the s32 at P = C; host start time (`u3h_time_sync(4)`) = G; used
   time (`u3h_time_sync(5)`) = C; pending byte (`u3h_time_sync(6)`) = 1; `u3h_so_note(N7, null, G − C, null)`
   (the wrapped 32-bit difference).
6. Unless eb[0] bit 3 is set: resync flag (`u3h_time_sync(3)`) = 1, then `u3h_sync_reset()`. This is done for
   every guest round start, whether step 5 applied or not.

### 4.15 Edge cases in one place

- Null pointers: only the tests named above exist (clock pointer, frame clock pointer in 4.10, context, allocator,
  P). The frame clock pointer is not tested where NOW is computed; the pump and the sync request run only when the
  game is in a net frame.
- A datagram of 1 to 3 bytes has no type byte of its own (the type is the fourth byte): it reads as type 0 from
  the zeroed buffer and is handled as a data packet (6.7).
- A port value above 0xffff given to 4.3 is cut to its low 16 bits on the wire.

### 4.16 Where the old build kept x87 register precision (for the tester)

The old C functions were compiled without SSE: a float expression ran on the x87 unit and was rounded to 32 bits
only where the compiled code stored it. "Register precision" = the precision of the thread's x87 control word (64
significant bits on PS4; see `film_helpers.md`, open question 4, for the PC build). Old and new must be compared
under the same control word. The points below are read from the shipped v7.212 code. One x87 operation on two
32-bit floats whose result is stored at once equals the single-precision operation exactly; differences can only
come from the chains listed here.

| Where | Old build | New (4.0) | What can differ |
|---|---|---|---|
| NOW in the pump | T (exact integer) times 1/300 at register precision, rounded to 32 bits at once | T converted to float first, then one multiplication | nothing while \|T\| ≤ 2^24 (game time under about 15.5 hours); above that the last bit of NOW |
| NOW in the sync request | the same product, but the test "next-request time < NOW" used it UNROUNDED; after the test it was rounded to 32 bits and that value went into the sum | rounded NOW in the test | the test, only when the next-request time equals the rounded NOW exactly and the product was rounded down: old sends in this frame, new one frame later |
| next-request time (4.11 step 2) | ((r · 2^−16) · 0.3 + NOW) + 0.1 at register precision, one rounding at the store | four single-precision steps (the first exact) | the last one or two units of the stored time |
| round trip (4.8, pong) | (NOW − V) · 500 at register precision, then truncated | subtraction and product each rounded to 32 bits, then truncated | the word by 1 when the product lies within rounding distance of a whole number; NaN and out-of-range give 0x80000000 in both |
| net clock block (4.10 step 4) | ((D · S) · 300) · R, plus F, all at register precision; W truncated from that unrounded sum; the fraction = sum − W rounded once at the store | every step rounded to 32 bits | W by one tick in a frame where the sum lies within rounding distance of a whole number, the stored fraction then by 1.0 the other way (the old build can store a fraction of exactly 1.0, the new one stores 0 and counts the tick at once); otherwise the last bits of the fraction. Over one frame, net time plus fraction of old and new differ by a few float units of the frame's advance (about 10^−6 tick); these differences add up over frames (at worst about one tick per million frames) |
| ping / data / keep-alive times, the ping's pattern, all ≤ tests of timers | single operations on 32-bit values | the same | nothing |

The integer code (4.12 to 4.14) has no float.

## 5. Test scenarios

Three kinds; each scenario says which it needs.

- **[fake]** old against new on fake memory with recording stubs: a fake ExtState (`u3h_ext`) whose import
  entries 0, 2, 3, 4, 5 point at scripted stubs (they record descriptor, lengths, flags, the 16 address bytes, the
  payload bytes, and return scripted results / deliver scripted datagrams); a fake image covering the offsets of
  3.6 (up to 0x1b24ba0) with a frame clock object, a net clock object, a match session, the connection table and
  the net game manager; recording stubs at image 0xda0d60 and 0x1196cb0; recording replacements for every
  function of 2.4 (the stubs for `u3x_gt_base_write` / `u3x_gt_base_read` write / read a known number of bits;
  the stub for `u3x_gt_has_data` returns a scripted value; stubs snapshot the timer record they can see). Compared:
  results, all memory (records, timers, samples, game data, eb words), and the order and arguments of every stub
  call. The pure readers and accessors (`u3h_ext`, `u3h_gt_state`, `u3h_time_sync`, `u3x_net_clock`,
  `u3x_find_member`, `u3x_entry_by_address`, `u3x_gt_bytes`) can be the real ones: how often and when they are
  called is not part of the behaviour. Floats bit for bit, with the exceptions of 4.16 (choose inputs outside
  those cases for the exact comparison and test the cases of 4.16 separately against the table).
- **[pure]** the same without any stub but the state accessors.
- **[live]** needs the running game on two machines.

### 5.1 Sockets [fake]

1. Open, record zero: net-pick, socket(`u3mp-game`, 2, 2, 0), set option (0xffff, 0x1200, 1), bind(port); record =
   {descriptor, 1, 0, 0}; result 0.
2. Open, record already {5, 1, x, y}: result 0, no call, counters kept. Record {0, 1, ..} and {5, 0, ..}: opened
   anew (record zeroed first, the old descriptor not closed).
3. Each of the three imports null: −1, record all zero, only net-pick called.
4. Socket import returns −7: eb[97] = 0xfffffff9, record zero, −1. Returns 0: accepted, record {0, 1, 0, 0}.
5. Set-option fails: no bind, eb[97] = its result, record {descriptor, 0, 0, 0}, −1. Bind fails: the same with the
   bind result.
6. Receive: descriptor 0 / −1 / import null: −1, no call, outputs untouched. Result 20 with address bytes 10 02 0c
   a1 c0 a8 01 02 ...: `*ip` = 0x0201a8c0, `*port` = 3233, bytes-received + 20 + 0x34. Result 0 and result −35:
   returned, outputs and counter untouched. The stub sees flags 0, a zeroed address area and length 16.
7. Send on a record that is not the game socket: `u3h_pl_send` not called; address bytes 10 02 port-high port-low
   address 0...; result > 0: bytes-sent + result + 0x34, eb[99] + 1, eb[119] + result; result 0 / negative:
   returned, no counter. Descriptor ≤ 0 or import null: −1. Port 0x10ca1: wire port bytes 0c a1.
8. Send on the game socket record: `u3h_pl_send` answers 12 → result 12, import not called, no counter; answers −1
   → −1, the same; answers −2 → the import path of scenario 7.
9. Open both: both succeed (eb[96] = 3, result 0, ports 3233 then 3232); first fails (eb[96] = 0, second not
   tried, −1); second fails (eb[96] = 1, −1, first stays open). In all three: sample count 0, next-request time 0,
   512 timer bytes zero (pre-fill them, the round-trip words too), table pointer set; samples and random state
   unchanged. Second call with both open: no import call, eb[96] = 3 again, state reset again.
10. Entry reset before any open (table pointer null): nothing. After an open: entry 0, entry 31, an address in the
    middle of entry 3 (timer 3), table + 7 (nothing), table + 8 + 32·0x220 (nothing); the round-trip word stays.

### 5.2 Context [pure]

11. Push with count 0, 5, 0x2d: entry written at 40·count, count + 1, result 1; other entries untouched. Count
    0x2e and 0xffffffff: result 0, nothing written.
12. Pop with count 6: count 5, entry 5 zero, entry 4 untouched.

### 5.3 Frame and pump [fake]

13. Frame with clock pointer null / net-active 0 / game socket not open (each alone): the toggle still flips;
    no counter, no call.
14. Frame, all present: eb[113] + 1; calls in order: 0xda0d60, (push), monitor with the toggle value (1 on the
    first frame, 0 on the second), the pump's calls, (pop). Context null; count 0x2e; allocator pointer null:
    each alone gives no push and no pop, monitor and pump still run. With a push the entry is {allocator,
    base + 0x13ac250, 0, base + 0x13ac250, 1} during the pump and zero after.
15. Receive, nothing waiting (`u3h_pl_recv` −2, import answers −35): one receive call with size 0x23ff, then the
    send phase. `u3h_pl_recv` answers −1: the game socket's import is NOT asked. 300 datagrams waiting: exactly
    256 are taken.
16. Time request from a known peer on LAN: bytes 33 22 11 02 00 00 00 44 → one send of 12 bytes {03, 0x11223344,
    net time} to the source address / port; eb[98], [107] + 1; eb[117] = 8; entry + 0xf8 + 8.
17. The same from an unknown address: on LAN answered (no entry counter); online (LAN byte 0): eb[111] + 1, no
    answer, eb[98] and eb[117] still updated.
18. Ping {04, V}: pong {05, V} of 8 bytes to the source. Pong {05, V} from a known peer with NOW − V = 0.25 s:
    round-trip word 125, eb[106] + 1; from an unknown address on LAN: eb[106] + 1 only. V = NaN pattern: word
    0x80000000.
19. Time answer {03, A, B}: `u3x_time_sample(base, A, B)` called; with eb[0] bit 3 set: not called.
20. Data packet from a known peer: eb[100] + 1; `u3x_gt_base_read` sees the stream at position 8, its size 8
    times the datagram's length; then `u3x_ev_scan` with queue 2, the entry index and handle, eb[113]. From an
    unknown address on LAN: eb[100] + 1 only.
21. Type 1, 6, 255: eb[111] + 1. A 2-byte datagram: handled as type 0.
22. The buffer is zero before every receive (a long datagram followed by a short one: the stub for
    `u3x_gt_base_read` sees zeros behind the short one).
23. Send phase, entry filters: in-use 0; handle 0; port 0 (each alone: no call for that entry); handle −1 with
    in-use and port set: served.
24. Send phase, fresh timers (0) and game time 3750 ticks (NOW = 12.5): ev-scan 0, ev-scan 1, drop-expired, ping
    {04, pattern of 12.5} sent, ping time 22.5, eb[105] + 1; data time 12.55 (the pattern `f_add` gives);
    keep-alive due → `u3x_gt_has_data` NOT called; keep-alive time 14.5; buffer zeroed; stream size 0x11ff8 bits;
    type bits, base-write stub, one 0 bit; send with `u3x_gt_bytes` length; entry + 0xf4 + result.
25. Next frame, 3756 ticks (12.52): ping not due, data not due: only ev-scans and drop-expired. 3768 ticks
    (12.56): data due, keep-alive not due → `u3x_gt_has_data` called once (it sees the data time already moved to
    12.61): returns 0 → nothing sent, keep-alive time unchanged; returns 1 → packet sent, keep-alive time NOW + 2.
26. Timer exactly equal to NOW: due. Send result 0 or negative: entry + 0xf4 unchanged; ping time and eb[105]
    still updated after a failed ping.
27. Two served entries: all of entry 3's calls come before entry 7's.
28. Master channel: socket not open → no call. Open, 3 datagrams waiting → 4 receive calls with size 0xff,
    eb[112] + 3. 20 waiting → 8 calls, eb[112] + 8.

### 5.4 Net clock block [fake]

29. Each of the four conditions of step 1 alone: nothing, eb[114] unchanged.
30. First run, rate 1.0, D = 1/60 (0x3c888889), S = 1.0, F = 0, net time 1000: net time 1005, fraction near 0,
    paused byte = byte at 0x17c3572, clock set called with (clock, base + 0x17c3578, 1) after the stores, rate 0
    after the call, flag set. Second run (rate now 0, flag set): R = 1.0, the clock advances again.
31. First run with rate 0 (flag still clear): W = 0 from F = 0; fraction unchanged; clock set still called.
32. Rate −0.0 with the flag set: treated as 1.0. Rate 2.0: twice the ticks. Rate NaN: W = 0x80000000 (net time
    grows by that), fraction NaN.
33. Paused byte non-zero: no maths, paused byte NOT rewritten, net time and fraction unchanged, clock set called,
    rate zeroed, flag set.
34. Byte at 0x17c3572 = 1: the unpaused run sets the paused byte; the following runs are paused for good.
35. Fraction carry: F = 0.75, product 4.5 → W 5, fraction 0.25. S = 0: W 0, fraction unchanged. S = −1 with F = 0.25
    and product −4.5: sum −4.25, W −4, fraction −0.25.
36. eb[109] changed since the last run: `u3x_time_synced(base, eb[110])` called once, before the maths, also when
    paused; unchanged: not called. Not called while step 1 blocks, and then called at the first run that passes.
37. Random D, S, R, F (finite, sum within ±2^30, away from the cases of 4.16): bit-exact against the formulas of
    4.10.

### 5.5 Time sync [fake]

38. Sync request, every blocking condition alone: next time ≥ NOW (also equal); count 8; eb[0] bit 3; room id 0;
    own member missing; host member missing; own = host; host connection id −1; no entry with the id; entry found
    but not in use / handle −1 / port 0; an unused entry with the id BEFORE a good one with the same id (blocked).
    In each: no call to `u3x_sync_random`, no send, next time unchanged.
39. All conditions met (handle 0 is fine here): random stepped once; next time = the formula of 4.11 for r = 0,
    0x8000, 0xffff; one send of 8 bytes {02, net time} to the entry's address / port; a failing send changes
    nothing else.
40. Sample with count 8: nothing. Count 3, C = 1000, t0 = 990, th = 5000: half 5, difference 4005, eb[108] + 1,
    count 4. C − t0 = 1, 0, −5: half word written (0, 0, −2), count unchanged, eb[108] unchanged. C − t0 = 2 and 3:
    half 1.
41. Clock pointer null: C = 0 is used.
42. Eighth sample: halves {9, 3, 7, 3, 8, 1, 6, 2} with distinct differences → order by half, the two 3s in
    their old order; M = sum of the first four differences / 4 with a negative sum (rounds toward zero: −7 / 4 =
    −1); net time = C + M; eb[109] + 1; eb[110] = M; count 8.
43. Eighth sample with the resync flag set: flag cleared; joined players: non-zero stamps at +0x39c, +0x3a0,
    +0x3a8 grow by M, zero stamps stay 0, +0x3a4 untouched, +0x5f0 / +0x5f4 zero; players not joined untouched.
    Flag clear: no player touched.
44. Synced, pending 0: only N1 (offset −3: the stub gets −3). Pending 1 and H > clock: pending cleared, nothing
    else. Pending 1 and H ≤ clock (equal too): of the four words those equal to U become H, the others stay; N2
    with H; pending 0.
45. Round start with `r[6]` = 0: result 0, no call. As host (room id 0; own id = host id): N3 with G, start time
    untouched, no reset.
46. Round start as guest, G ≤ C: N4, N5, N6 in order; start time untouched; resync flag 1; `u3h_sync_reset`
    called. G > C: also start time = C, words 4 / 5 / pending = G / C / 1, N7 with G − C (before the flag and the
    reset). With eb[0] bit 3 set: no flag, no reset, the rest the same.

### 5.6 What needs the live game [live]

- Real UDP sockets on both platform layers; the second NetInit not reopening them; NetShutdown closing them.
- Two machines in a LAN match: packets flow (eb[98] / [99] grow on both), the data packets are accepted by the
  game's own packet reader and writer on the other side, pings are answered, the guest reaches 8 samples and its
  net clock jumps to the host's (eb[109], eb[110]; the helper's log line N1).
- The allocator context around the packet work: no fault in the game's record allocator on joining.
- The net clock block against the game's clock object (function 0x1196cb0): the HUD match clock runs at real
  time on host and guest, pauses and cinema speed behave as before.
- The round start fix: a guest whose clock is behind the host's can throw grenades at round start; log lines N4
  to N7 and N2.
- Online play through the peer link (the `u3h_pl_send` / `u3h_pl_recv` path).
- Interoperation with the previous helper version on the other machine (the wire format of 3.10).

## 6. Things in the old code that look like bugs or dead code (behaviour to keep)

1. Socket open: after a set-option or bind failure the descriptor stays in a record that is not open; the next
   open zeroes the record without closing it (the peer link closes the record itself between its two tries; the
   game sockets do not). A descriptor of exactly 0 is accepted and marked open, although every later use refuses
   a descriptor ≤ 0 and the next open opens again.
2. Opening the game sockets: when the first fails the second is not tried; when the second fails the first stays
   open and the result is −1. The state reset (count, next time, timers, table pointer) happens whatever the
   result and on every repeated call; it does not count eb[115] and leaves the samples and the random state.
3. Receive leaves `*ip` / `*port` as they were for a result ≤ 0; the byte counters add a fixed 0x34 per datagram.
4. Send: a packet the peer link took updates no counter of the record and no eb word, whatever its result; the
   entry's bytes-sent word (4.8) then grows by the payload length only.
5. Pump: at most 256 datagrams per frame, the whole 0x2400-byte buffer cleared for each; the receive size is
   0x23ff, one less than the buffer.
6. Pump on LAN: time requests and pings are answered and time answers are taken as samples from any address, also
   on the host, without checking that a request is open or that the echoed value is one this machine sent. Data
   packets and pongs from unknown addresses are counted and ignored. Online, a datagram from an unknown address is
   counted in eb[98] / eb[117] before it is dropped.
7. Pump: the type is the datagram's fourth byte. A datagram whose length is not a multiple of 4 is read through a
   stream of 8 bits per received byte, counted in stream order: part of what arrived is cut off and zero bytes
   are read in its place. A datagram of 1 to 3 bytes is a data packet (type 0) and reaches `u3x_gt_base_read` of
   a known peer.
8. Pump: the entry lookup can only return entries in use; the old code tested the in-use byte again (no effect).
9. Send phase: the handle test is "not 0" while the sync request's is "not −1"; so the request can go to an entry
   whose handle is 0 (which the send phase never serves) and never to one with handle −1 (which it serves).
10. Send phase: after a reset all three timers are 0, so the first served frame sends a ping and a data packet at
    once. The data time moves on also when nothing is sent. `u3x_gt_has_data` is not asked in a frame where the
    keep-alive is due (so its own timer does not move then). The ping time and eb[105] advance when the send
    failed.
11. Net clock block: while the paused byte is set the block never clears it (the copy from 0x17c3572 is part of
    the unpaused path only) and still sets the clock and zeroes its rate every frame. The "rate was zeroed" flag
    is never cleared (not at NetShutdown, not for another clock object). In the very first run a rate of 0 is
    used as 0.
12. Net clock block: a NaN or out-of-range sum adds 0x80000000 to the net time.
13. Frame: the toggle flips also when the frame does nothing; the result of the push is not looked at (it cannot
    fail after the frame's own test); the pop has no test for an empty stack.
14. Timers and NOW cannot be NaN (NOW is a product of an integer and a constant; the timers hold 0 or NOW plus a
    constant); the old comparisons treat a NaN as "not due" and "not yet time to ask".
15. Sync request: only the first entry with the host's connection id is looked at, in use or not. While a
    condition blocks, the next-request time is not moved, so every frame tests again. The time moves before the
    send and stays moved when the send fails.
16. Time sample: the half-round-trip word is written before the answer is judged, so a rejected answer leaves its
    value in the next free record. Round trips under 2 ticks are rejected. There is no test for a negative count.
    Sums and differences wrap.
17. Completed sync: the note N1 shows no number for a negative offset (the note function prints only values ≥ 0),
    and N7 none for a wrapped negative difference. The note is made by the net clock block of a later call, not
    when the sync completes.
18. `u3x_time_synced` clears the pending byte before it tests whether the host's start is still ahead; when it
    is, the fix is dropped for good. The comparisons "H greater than the clock" and "G greater than C" are plain
    signed comparisons in the shipped code (the source wrote them as a difference greater than 0).
19. Round start: every guest round start requests a resync (flag and reset), also with the start not ahead; a
    second round start while a fix is pending overwrites the two words.
20. Master channel: the datagrams are read into the shared packet buffer and never looked at.

## 7. What stays as it is

- The exported names and signatures of 2.1 and the C wrappers and exports that other groups call today:
  `u3h_gt_open`, `u3h_sync_request`, `u3h_lnb_frame_parts`, `u3h_time_sync`, `u3h_sync_reset`, the kind 236
  dispatch.
- The trivial C functions of this section of the helper that are not part of the group: the sockets' close and
  `u3h_gt_close`, the sync reset, the float stamp and the timeout test of the sub-object, kinds 75 / 76.
- The socket record (3.3), the timer record (3.4) and the sync state (3.5): C code of other groups reads and
  writes them (the peer link's own record, the NAT probe, the status line, the party follow code).
- The socket import table, its calling convention and constants (the platform's API), the ports 3233 and 3232,
  the debug name `u3mp-game`.
- The wire format of 3.10 and the timing constants of 3.11: the other machine runs the same code, and older
  helper versions must still interoperate.
- Every game offset of 3.6 to 3.9 and the two game functions.
- The diagnostic words of 3.1 (the boot log, the status line and the test rigs read them) and the seven texts of
  3.12.
- The order "monitor, then pump" inside the context push / pop, and "frame, then net clock block" (the caller's).
- The neighbours of 2.4.
- Everything in section 6.

## 8. Open questions

1. `u3h_gt_state`, `u3h_pl_send` and `u3h_pl_recv` do not exist in v7.212 (the old C code uses the C statics
   directly). Either the integrator adds these three one-line exports (as assumed above), or the C wrappers pass
   the addresses and the two hook pointers as extra arguments (which would change the signatures of 2.2). To be
   decided before the implementer starts. G07 already asks for `u3h_gt_entry_reset`: with this group done it is a
   forwarder to `u3x_gt_entry_reset`.
2. The pump and the frame need the boot-log function only to hand it to `u3x_ev_scan`. The proposal passes it
   through the wrapper; an export would do as well.
3. The x87 control word of the game thread on the PC build is not known here (as in `film_helpers.md`); the
   differences of 4.16 were derived for 64 and 53 significant bits alike.
4. 4.16 means the new code is not bit-identical to the old in the listed chains. That follows the project's
   decision for new float code; if the owner wants the old last bits for the net clock fraction (the only one of
   them that feeds the game), that chain would have to be kept at register precision instead.

## 9. Notes for the integrator (not behaviour)

- Not in the inventory's list for this group, but folded in so that nothing of the old port stays behind once the
  callers move: the tests of the C wrapper `gt_ctx_push` (context non-null, count below 0x2e, allocator non-null)
  and of `gt_ctx_pop` (only after a push) are part of 4.7; the wrappers `rs_synced`, `gt_sample` and
  `gt_round_start` have no logic of their own.
- The group has no table of game constants.

## Clarifications

Added by the tester (2026-11-14). Facts only, confirmed by the old-versus-new test (reimpl2\G11\test_g11.cpp) or
read from the shipped v7.212 code, where the text above is silent, too wide or can be made more exact. Nothing
above was found wrong. "Old (x87)" = the old C functions built without SSE at -O2: the x87 instructions of the sync
request, of the pump (inside the frame) and of the net clock block in that build are the shipped helper's, one for
one (compared by disassembly). All figures were the same, within a few per mille, with the x87 control word at 64
and at 53 significant bits; so open question 3 of section 8 does not change the picture.

1. 4.10 and scenario 32, NaN. When two NaN values meet in one operation of the net clock block (two of rate, frame
   time, scale, fraction are NaN, or one is NaN and another pair gives infinity times zero), WHICH NaN the stored
   fraction is (payload, sign) is not defined by this description: it depends on the operand order the compiler
   chose and, on the x87 unit, on another rule. Defined and equal in every build: the fraction is a NaN and the net
   time grows by 0x80000000. With at most one NaN or infinite input, old (single precision) and new agree bit for
   bit.
2. 4.16, NOW. For game times above 2^24 ticks the old (x87) NOW differs from the new one by exactly one unit of the
   last place in 25 % of the game times, never more; up to 2^24 ticks never. The ping, data and keep-alive times are
   the single-precision sum of that version's NOW and the interval in both, and a timer one unit below, at and one
   unit above NOW is "due, due, not due" in both.
3. 4.16, the sync request's test. With the next-request time exactly equal to the rounded NOW, old (x87) sends in
   50 % of the game times (those whose product was rounded down) and the new code in none; no other case of the
   test differs (game time up to 2^24 ticks).
4. 4.16, the stored next-request time. Old (x87) minus new, in units of the last place: -1 in 19.6 %, 0 in 67.7 %,
   +1 in 12.6 % of 500,000 random requests; two units were never seen.
5. 4.16, round trip. For a pong that echoes a real earlier NOW (0 to 1200 ticks back) the word differs in about 1
   of 100,000 pongs, and then old = new - 1. The "by 1" of the table holds while the product is below 2^24 (an
   apparent delay under about 9 hours). The echoed value comes from the network and can be anything: for larger
   products the two differ by more (up to 32 units for delays up to 10^6 s); NaN and out-of-range give 0x80000000
   in both.
6. 4.16, net clock block, one frame from the same state (2,000,000 random frames: frame time 1/20 to 1/240 s, scale
   1 or 0.05 to 1.55, fraction 0 to 1, rate 1): the tick count W differs in 2 frames (old (x87) counts one tick less
   and keeps a fraction about 1.0 higher); the old build stored a fraction of exactly 1.0 in 1 frame at 64
   significant bits and in none at 53; net time plus fraction never differ by more than 2.8 millionths of a tick.
7. 4.16, net clock block over many frames (each version on its own net time and fraction). With a frame time that
   varies from frame to frame nothing adds up: after 10^7 frames the net times are equal, and they were one tick
   apart during single frames only, 200 to 430 frames per million. With a CONSTANT frame time the same rounding
   repeats every frame and the difference adds up, always the same way (the new clock ahead): 0.22 tick per million
   frames at 1/60 s (pattern 0x3c888889: 2 ticks after 10^7 frames, about 46 hours) and 0.42 tick per million
   frames at 1/30 s (0x3d088889: 4 ticks after 10^7 frames). This is inside the bound the table gives ("at worst
   about one tick per million frames").
8. 6.18, the two comparisons, read from the shipped code: `u3x_time_synced` compares the host's start with the
   clock by a plain signed comparison; `u3x_round_start` subtracts and branches on the signed flags of that
   subtraction, which is the same plain signed comparison (so a start of 0x7fffffff against a clock of -1 is
   "ahead"), and N7 gets the wrapped difference (0x80000000 there). `u3x_time_sample` halves the wrapped difference
   toward zero and rejects it when the wrapped difference is below 2. The new code does the same in all three.
9. 4.6, pop above the limit. A pop with a count of 0x2f (not reachable through this group: the push refuses at
   0x2e) clears the entry with index 0x2e, whose first word lies on the count itself: the count ends as 0. Old and
   new agree. A pop with a count of 0 writes far outside the context in both and was not run.
10. 4.12, negative count. With a sample count of -1 or -2 both versions write the half-round-trip word (and, when
    it is positive, the clock-difference word, eb[108] and the count + 1) into the record before the table; old and
    new agree byte for byte.
11. 4.8 and 4.9, where packets are built (not observable from outside, but equal in old and new): the answers to
    types 2 and 4 in a 16-byte area of the function's own; ping, sync request and data packet in the packet
    buffer; the master channel receives into the packet buffer. After every call the packet buffer of old and new
    held the same bytes in all cases.
