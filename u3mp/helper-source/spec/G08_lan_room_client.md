# G08. LAN backend: room client, NetInit / frame / shutdown, party rows

Behaviour description for a reimplementation (reimpl2 pipeline, describer's document). It says what the functions
do, not how the old ones were written. Offsets are hexadecimal when prefixed with 0x. "Image offset X" means the
address `base + X`, where `base` is the game image base every function receives. All multi-byte values are
little-endian; "u8 / u16 / s16 / u32 / s32 / u64 / s64" are unsigned / signed integers of that width, "pointer" is 8
bytes. No function of this group uses floating point.

The group has ten functions from the inventory (two still in C, eight in C++) and five strays (old internal helpers
that are in no work group): see 2.3.

## 1. Purpose and callers

The game's multiplayer code talks to a "session manager" object (a class with a table of function pointers) for
everything that concerns rooms: create, search, join, leave, member data. PS4 lost the LAN manager class; the helper
supplies it ("the LAN backend object"). The backend's slots themselves, its sockets, its room server and the member
slot functions belong to the groups G07, G09 and G10. This group holds what is around them:

**A. The room client** (the TCP connection of one session to a room server, which may be on this PC).

- `u3x_lan_client_update` reads what the room server sent for one session binding and applies every complete
  message. Called once per backend Update for every binding that has a session: the backend's Update slot
  (`u3x_lnb_s1`, G10) calls the C export `u3h_lnb_cli_update(base, bind)`, which calls the C wrapper
  `lnb_cli_update`, which builds a "client view" (3.3) of the binding and calls `u3x_lan_client_update`. The
  backend's Update runs once per frame from `u3x_lan_tick` (below) and, in addition, up to about 30 times in a row
  (100 ms apart) inside the backend's Create and Join slots while they wait for the room state.
- `u3x_lan_client_message` applies the one message at the start of a binding's receive buffer and says how many
  bytes it used. Called by `u3x_lan_client_update` only (the C wrapper `lnb_cli_msg` exists but nothing calls it in
  v7.212).
- `u3x_lan_client_lost` (stray): what happens when the connection to the room ends. Called by
  `u3x_lan_client_message` for the "kicked" message (the C wrapper `lnb_cli_lost` exists but nothing calls it in
  v7.212).

**B. The backend at NetInit, per frame and at NetShutdown.**

- `u3x_lan_netinit`: the LAN branch of the helper's NetInit. Called through the C wrapper `lnb_netinit` and its
  export `u3h_lnb_netinit(base, s)` from the helper's NetInit step (another group), only when
  `u3h_onb_lan_flag()` is non-zero (LAN or offline mode, not online). Before the call that step has stored the LAN
  flag in the u8 at image offset 0x17d5c60, set the u8 at image offset 0x17d5c61 ("multiplayer active") to 1 and
  run the game's session initialisation on the match session.
- `u3h_lnb_setup` (C): builds (or rebuilds) the LAN backend object and registers it. Called by `u3x_lan_netinit`
  only.
- `u3x_lan_tick`: the LAN parts of the per-frame network update. Called through the C wrapper `lnb_tick` from the
  helper's per-frame event (event kind 16), every frame in which the image base is known, right after the helper's
  net-shutdown tick and right before `lnb_refresh`.
- `u3h_lnb_frame_parts` (C): three fixed groups of per-frame calls into other parts of the helper. Called by
  `u3x_lan_tick` only, with the selector 0, 1 and 2.
- `u3x_lan_shutdown`: the LAN part of NetShutdown. Called through the C wrapper `lnb_shutdown` from
  `u3h_net_shutdown_parts` (second of its four parts: after `emb_shutdown119`, before `fr_shutdown` and
  `k34_shutdown`), which the helper's NetShutdown runs; that caller clears the two bytes at image offsets 0x17d5c61
  and 0x17d5c60 afterwards.
- `u3x_lan_refresh`: repeats the LAN room search every 5 seconds while the "LAN Parties" page is open. Called
  through the C wrapper `lnb_refresh` from the same per-frame event, right after `lnb_tick`.

**C. The party rows of the lobby's player list.**

- `u3x_lan_row_name` and `u3x_lan_rows`: the name of the n-th other party member and the number of rows. Called by
  the lobby list source `u3x_lobby_source` (another group): the row name for every request other than the title
  request when the row index is above 0 (a null result is shown as an empty text), and the row count for the "last
  row index" request (it answers the count minus 1). The C wrappers `lnb_row_name` and `lnb_rows` exist; nothing
  else calls them in v7.212.

## 2. Interface

### 2.1 Functions to deliver

All are `extern "C"`. The eight C++ names and their signatures stay. For the two C functions a new `u3x_` function
is proposed; the C export keeps its name and signature and becomes a one-line wrapper.

| Function | C++ signature | Called by | The C wrapper passes |
|---|---|---|---|
| client message | `uint32_t u3x_lan_client_message(uintptr_t base, const void *view)` | `u3x_lan_client_update`; C `lnb_cli_msg` | base and the address of a client view (3.3) built from the binding |
| client update | `void u3x_lan_client_update(uintptr_t base, const void *view)` | C `lnb_cli_update` | the same |
| client lost (stray) | `void u3x_lan_client_lost(uintptr_t base, const void *view, int kicked)` | `u3x_lan_client_message`; C `lnb_cli_lost` | the same, and its `kicked` argument |
| NetInit | `void u3x_lan_netinit(uintptr_t base, void *s)` | C `lnb_netinit` | base, the helper's state object |
| backend setup | `void *u3x_lan_setup(uintptr_t base, void *s, void **vtable, void *storage, void *slot0)` (new name; replaces the C body of `u3h_lnb_setup`) | `u3x_lan_netinit` (directly, or through the kept export `void *u3h_lnb_setup(uintptr_t base, void *s)`) | base, the state object, and three addresses only the C part knows: its table of 32 function pointers (`lnb_vt`), its static backend object (`lnb_object`), and the address of its slot 0 function (`lnb_s0`, taken instruction-pointer-relative as the C part does today) |
| frame tick | `void u3x_lan_tick(uintptr_t base, void *s)` | C `lnb_tick` | base, the state object |
| frame parts | `void u3x_lan_frame_parts(uintptr_t base, void *s, int which)` (new name; replaces the C body of `u3h_lnb_frame_parts`) | `u3x_lan_tick` (directly, or through the kept export `void u3h_lnb_frame_parts(uintptr_t base, void *s, int which)`) | its three arguments unchanged |
| shutdown | `void u3x_lan_shutdown(uintptr_t base)` | C `lnb_shutdown` | base |
| search refresh | `void u3x_lan_refresh(uintptr_t base)` | C `lnb_refresh` | base |
| row name | `const char *u3x_lan_row_name(uintptr_t base, int row)` | `u3x_lobby_source`; C `lnb_row_name` | base, row |
| row count | `int u3x_lan_rows(uintptr_t base)` | `u3x_lobby_source`; C `lnb_rows` | base |

Parameters and results:

- `base`: game image base.
- `s`: the helper's state object. Opaque here: it is only stored into the backend object and passed on.
- `view`: address of a client view (3.3).
- `kicked`: non-zero when the room server told this client it was kicked.
- `vtable`, `storage`, `slot0`: see 4.5.
- `which`: 0, 1 or anything else (2 in practice).
- `row`: 1-based index among the other party members.
- `u3x_lan_client_message` returns the number of bytes of the message it handled, or 0 for "nothing handled"
  (incomplete or unknown message).
- `u3x_lan_setup` returns the address of the backend object.
- `u3x_lan_row_name` returns the address of a member slot (its first bytes are the name, 3.2) or null.
- `u3x_lan_rows` returns a count of at least 1.

### 2.2 Things of other groups, of the C part and of the shared tools that this group calls

Not to be described or reimplemented here. The tools of `CONVENTIONS.h` (`rd`, `at`, `game_call`, `u3h_ext`,
`ext_eb`) are used as given there.

| Name | Signature | What it does |
|---|---|---|
| `u3h_lnb_call` | `int64_t u3h_lnb_call(void *object, int slot)` | calls entry `slot` of the object's function table (the table pointer is the object's first 8 bytes, entries are 8 bytes) with the object as the only argument; returns the callee's 64-bit result. Used here for session events (3.2) |
| `u3h_cm_lanaddr` | `void u3h_cm_lanaddr(uintptr_t base, const char *name, uint32_t ip)` | notes in the game's connection manager that the player `name` has the LAN address `ip` (and counts it in eb[87]) |
| `u3h_elim_received` | `volatile uint8_t *u3h_elim_received(void)` | address of the helper's flag "the host announced an elimination round" |
| `u3h_adv_rules` | `uint8_t *u3h_adv_rules(void)` | address of the helper's Advanced Settings record (3.5) |
| `u3h_adv_received` | `uint8_t *u3h_adv_received(void)` | address of the helper's flag "the Advanced Settings came from the host" |
| `u3x_add_member` | `int32_t u3x_add_member(uintptr_t base, uint8_t *session, const void *info, int you, int host)` | G07: adds a member described by a member info (3.4) to the session's member slots (or finds it by name), notes it as "you" and / or host, tells the session; returns the member's index or −1 |
| `u3x_remove_member` | `int32_t u3x_remove_member(uintptr_t base, uint8_t *session, const char *name)` | G07: removes the member with that name (0x24-byte name compare), tells the session; 0 or −1 |
| `u3x_find_member` | `uint8_t *u3x_find_member(uint8_t *session, uint32_t id)` | the used member slot whose member id is `id`, or null |
| `u3x_sock_open` | `int u3x_sock_open(const void *socket)` | G07 socket layer: non-zero when the socket has a descriptor |
| `u3x_sock_close` | `int32_t u3x_sock_close(void *socket)` | closes the descriptor if there is one and marks the socket closed; result unused here |
| `u3x_sock_poll` | `int32_t u3x_sock_poll(const void *socket)` | without waiting: 1 = something to read (data or the end of the stream), 0 = nothing, −1 = broken or closed |
| `u3x_sock_recv` | `int32_t u3x_sock_recv(void *socket, void *buffer, uint32_t size, int block)` | receives up to `size` bytes; `block` non-zero = an ordinary (waiting) receive. Result: bytes received, 0 = the peer closed, −1 = error (the layer has then already closed the socket and noted the error). A size of 0 is passed on to the system unchanged |
| `u3h_lnb_object` | `void *u3h_lnb_object(uintptr_t base)` | the pointer stored at image offset 0x17dd758 if it is non-null and that object's function table is the C part's LAN table; otherwise null |
| `u3h_online_manager` | `void *u3h_online_manager(uintptr_t base)` | the online backend object (pointer at image offset 0x17dd760) if it is non-null and is the registered manager (equal to the pointer at image offset 0x17dd768); otherwise null |
| `u3h_onb_lan_flag` | `int u3h_onb_lan_flag(void)` | non-zero when multiplayer runs on the LAN backend (LAN or offline mode), 0 in online mode |
| `u3h_gt_open` | `int32_t u3h_gt_open(uintptr_t base)` | G11: opens the two game UDP sockets and resets the time-sync state; 0 = both open, negative = failure |
| `u3h_gt_close` | `void u3h_gt_close(uintptr_t base)` | closes the game sockets (and the peer link) |
| `u3h_cm_init_lan` | `void u3h_cm_init_lan(uintptr_t base)` | G07: sets the game's connection manager up for LAN (own name, LAN flag, entries reset) |
| `u3h_cm_shutdown` | `void u3h_cm_shutdown(uintptr_t base)` | G07: resets the packet object of every connection entry |
| `u3h_active_page` | `uint32_t u3h_active_page(uintptr_t base)` | id of the menu page on top of the page stack, 0 when there is none |
| `u3x_lnb_vtable` | `void u3x_lnb_vtable(void **vtable, void *slot0)` | fills the 32-entry function table of the LAN backend: `slot0` in entry 0, the backend's slots in their entries, a do-nothing function in the rest |
| `u3x_lnb_ctor` | `void u3x_lnb_ctor(void *object, void **vtable)` | the backend's constructor on a zeroed object: constructs the four bindings' room servers, marks all sockets closed, stores `vtable` as the object's function table |
| `u3x_lnb_s2` | `int64_t u3x_lnb_s2(void *object)` | the backend's Shutdown slot: counts the slot call (eb[67], eb[68] and the state object's slot history), closes the discovery socket and the client sockets of the bindings that have a session |
| `u3x_lnb_srv_stop` | `void u3x_lnb_srv_stop(void *room_server)` | G09: stops a room server (state 0, client and listening sockets closed) |
| `u3x_lobby_scene` | `void u3x_lobby_scene(uintptr_t base, void *s)` | the lobby menu scene's per-frame step (another group; the C `k34_scene` only forwards to it) |

Per-frame parts of the C part that `u3x_lan_frame_parts` has to call. They are file-private C functions in v7.212
(no `u3h_` export exists yet): the C part has to offer each through a one-line export. Proposed export names:

| C function | Proposed export | What it does |
|---|---|---|
| `gt_frame(base)` | `void u3h_gt_frame(uintptr_t base)` | G11: the game-packet frame (connection monitor, packet pump) |
| `gt_net_time_block(base)` | `void u3h_gt_net_time_block(uintptr_t base)` | G11: advances the net clock |
| `e03_side(base, s)` | `void u3h_e03_side(uintptr_t base, void *s)` | lobby character side placement fix |
| `f05_place(base)` | `void u3h_f05_place(uintptr_t base)` | lobby character placement fix |
| `k42_clock(base)` | `void u3h_k42_clock(uintptr_t base)` | net clock diagnostics |
| `k47_hud(base, s)` | `void u3h_k47_hud(uintptr_t base, void *s)` | a HUD layout fix |
| `e01_hud(base, s)` | `void u3h_e01_hud(uintptr_t base, void *s)` | a HUD layout fix |
| `gt_master_channel(base)` | `void u3h_gt_master_channel(uintptr_t base)` | G11: reads and drops what arrived on the second game socket |
| `onb_probe_tick(base)` | `void u3h_onb_probe_tick(uintptr_t base)` | the online NAT probe (does nothing unless the online object is the manager) |

### 2.3 Strays (old internal helpers in no work group, covered by this document)

- `u3x_lan_client_lost` (exported, in none of the inventory's tables): 4.3.
- The internal "leave the room" routine used by the client update: it is `u3x_lan_client_lost` with `kicked` = 0
  minus the socket close, and the update always closes the socket right before it, so the new code can use one
  routine for both (4.3).
- The internal "member joined" routine and the routine that turns a wire member record into a member info: 4.1.
  (The inventory lists a `member_record` at the room client's line under G13; that function is this LAN routine and
  is covered here.)
- The client view structure: 3.3.
- Five one-line helpers of the NetInit block ("is U3MP_LAN_SOLO on", the match session's address, the two
  virtual-call helpers, the local clock): their facts are in 3.1, 3.2 and 3.6.

## 3. Game and helper facts

### 3.1 Game data

| Image offset | Type | Meaning |
|---|---|---|
| 0x17b4ba4 | s32 | the engine's local clock in ticks of 1/300 second |
| 0x17d167c | s16 | the selected net game (map and mode selection) of the custom game menu |
| 0x17d5c60 | u8 | the net block's LAN flag (non-zero while NetInit ran for LAN) |
| 0x17d5c61 | u8 | "multiplayer active" (set by NetInit, cleared by NetShutdown) |
| 0x17d5c80 | u8 | "network error" flag; the game reacts to it |
| 0x17d6010 | 0x24 bytes | the player's own network name |
| 0x17d6420 | object | the match session (3.2) |
| 0x17dc298 | object | the party session (3.2) |
| 0x17dd610 | pointer | the multiplayer profile objects; null while they do not exist |
| 0x17dd758 | pointer | the LAN backend object (PS3's "LAN object" global) |
| 0x17dd768 | pointer | the registered session manager (LAN or online backend), null = none |

### 3.2 A session (match or party)

| Offset | Type | Meaning |
|---|---|---|
| 0 | pointer | function table (8-byte entries). Entries used here, each called with the session as the only argument: 1 = "create finished", 2 = "join finished", 5 = "disconnected from the room", 6 = "kicked from the room" |
| 8 | pointer | the backend this session is attached to (null = none) |
| 0x18 | u64 | room id; 0 = not in a room |
| 0xa1 | u8 | create done |
| 0xb8 | u8 | join done |
| 0xc0 | u64 | joined (1 = yes) |
| 0x20c | u32 | capacity (maximum number of players) |
| 0x680 + i·0x120, i = 0..9 | member slot | see below |
| 0x12e4 | u32 | own member id |
| 0x12e8 | u32 | the host's member id |

Member slot: name at +0 (0x24 bytes of text), u8 "in use" at +0x25, u32 member id at +0x2c, u32
member data length at +0x3c, member data at +0x40 (0x40 bytes).

Party state fields that exist only in the party session and are reset at NetInit (4.4): u8 at 0x1334, u32 at 0x1338,
u64 at 0x1340, u8 at 0x1348, u32 at 0x1370, u8 at 0x1374, u8 at 0x1375.

### 3.3 The client view

What the C wrappers hand over for one session binding of the backend object: four pointers, 0x20 bytes.

| Offset | Type | Meaning |
|---|---|---|
| 0 | pointer | the session of the binding |
| 8 | pointer | the binding's client socket (opaque: only passed to the `u3x_sock_` functions) |
| 0x10 | pointer to u32 | the number of received bytes waiting in the buffer |
| 0x18 | pointer to u8 | the receive buffer, 0x800 bytes |

### 3.4 The member info (input of `u3x_add_member`)

0x28 bytes: pointer to the name at 0; u32 member id at 8; u32 NAT type at 0xc; u32 data length at 0x10; pointer to
the data at 0x18; u32 "has data" at 0x20.

### 3.5 The helper's own state

- The counter and switch words eb[] (`ext_eb()`): eb[0] bit 1 (value 2) set = U3MP_LAN_SOLO is off; eb[0] bit 2
  (value 4) set = the search refresh is off; eb[64] = low 32 bits of the backend object's address; eb[65] = backend
  setups; eb[66] = result of the backend's Init; eb[78] = room client messages; eb[79] = id of the last one; eb[85] =
  party updates; eb[90] = shutdowns; eb[94] = local clock at the last search; eb[230] = online re-attaches of the
  match session.
- The functions switch word: u32 at `u3h_ext()` + 0x3a70; bit 0 set = the Advanced Settings rows are switched off.
- The Advanced Settings record (`u3h_adv_rules()`): u32 at 0 = selection the rules belong to plus 1 (0 = none); u32
  at 4 = respawn rule; u32 at 8 = health rule; u32 at 0xc = weapon rule; u8 at 0x10 and u8 at 0x11 = two flags.

### 3.6 The LAN backend object and its function table

The object is 0x249d8 bytes:

| Offset | Type | Meaning |
|---|---|---|
| 0 | pointer | function table |
| 8 | 0x24 bytes | the player's network name |
| 0x4b8 + i·0x9140, i = 0..3 | binding | session pointer at +0; room server at +8; client socket at +0x8930; received count at +0x893c; receive buffer at +0x8940 |
| 0x249c8 | pointer-sized integer | the image base |
| 0x249d0 | pointer | the helper's state object |

(The fields between, the room list and the discovery socket, belong to G10.)

Entries of a backend's function table that this group calls (8-byte entries; the object is always the first
argument; results are s64):

| Entry | Arguments after the object | Meaning |
|---|---|---|
| 0 | none | Init; 0 = success |
| 1 | none | Update; non-zero = the connection to the service is lost (the online backend answers −1) |
| 2 | none | Shutdown |
| 3 | a session | AttachSession |
| 5 | none | Search |
| 8 | a session, an s32 | not implemented by the LAN backend (answers 0); called once at NetInit |

### 3.7 Game functions

All take one pointer argument (a session) in the first integer register; results are ignored.

| Image offset | Meaning |
|---|---|
| 0x77d270 | the party machine's per-frame update (argument: the party session) |
| 0x7a2360 | session initialisation |
| 0x7a2470 | leave the session's room |
| 0x7a25f0 | clear the session's members |

### 3.8 Ids

0xbb3595bc: the "LAN Parties" page (as `u3h_active_page` reports it).

### 3.9 The room messages (TCP, room server to client)

The room server (G09, on this PC or on another helper of any version) sends fixed-size messages as raw structures
in the machine's byte order (little-endian on both ends). Every message starts with a u32 id at 0; the u64 at 8 is
the room id (the client reads it only in the room state). Bytes of a message that the table does not name are
padding and are not read.

A member record is 0x70 bytes: name at 0 (0x24 bytes); u32 the member's address at 0x24; u16 member id at 0x28; u8
NAT type at 0x2a; u8 data length at 0x2b; u8 "this is you" at 0x2c; u8 "this is the host" at 0x2d; member data at
0x2e (0x40 bytes).

| Id | Size | Content |
|---|---|---|
| 0xc9 room state | 0x478 | u16 capacity at 0x10; u16 number of member records at 0x12; member records from 0x18 (room for 10) |
| 0xca member joined | 0x80 | one member record at 0x10 |
| 0xcb member left | 0x38 | u16 member id at 4 (not read by the client); the member's name at 0x10 (0x24 bytes) |
| 0xce member data | 0x58 | u16 member id at 4; u32 data length at 0x10; the data at 0x14 (0x40 bytes) |
| 0xd3 elimination round notice | 0x10 | nothing else |
| 0xd2 the host's Advanced Settings | 0x20 | u32 the sender's member id at 4 (filled in by the room server); u32 respawn at 0x10; u32 health at 0x14; u32 weapon at 0x18; u8 flags at 0x1c and 0x1d; s16 selection at 0x1e |
| 0xcc kicked | 0x10 | nothing else |

## 4. Behaviour

### 4.1 `u3x_lan_client_message(base, view)`

Works on the view's session, its buffer and the current received count N (read through the view). The message id is
the u32 at the start of the buffer; it is read whatever N is (with N below 4 it contains bytes of earlier traffic).
A message is handled only when N is at least its size; otherwise the result is 0 and nothing else of that message
happens. The result of a handled message is its size.

Counting: for the room state the two counters (eb[78] plus 1, eb[79] = the id) are updated only when the message is
complete. For every other id, known or unknown, they are updated on every call, before the size test.

**Member joined with a member record** (used by 0xc9 and 0xca), in this order:
1. If the record's "this is you" byte is 0: `u3h_cm_lanaddr(base, the record's name, the record's address)`.
2. `u3x_add_member(base, session, info, you, host)` with a member info (3.4) made of: the name pointer = the record
   itself, member id = the record's u16, NAT type = the record's u8, data length = the record's u8, data pointer =
   the record's data, "has data" = 1; `you` and `host` = the record's two flag bytes.

**0xc9, room state** (complete from 0x478 bytes):
1. Counters.
2. Session capacity = the message's capacity (u16 widened); session room id = the message's room id.
3. For each member record, in order, at most 10 (a larger count in the message is treated as 10): member joined.
4. If the session's "join done" byte is 0: set "joined" to 1 (u64), raise the session event "join finished" (entry
   2), then set "join done" to 1.
5. If the session's "create done" byte is 0: raise "create finished" (entry 1), then set "create done" to 1.

**0xca, member joined** (0x80): member joined with the record at 0x10.

**0xcb, member left** (0x38): `u3x_remove_member(base, session, the name at 0x10)`.

**0xce, member data** (0x58): look the member up with `u3x_find_member(session, the message's member id)`. If
found: its data length = the message's u32 length (stored as it is, not limited), and the 0x40 data bytes are
copied into the slot. If not found: nothing.

**0xd3, elimination round notice** (0x10): if the session is the party session and this player is not the host (own
member id differs from the host's member id): store 1 through `u3h_elim_received()`. Otherwise nothing (and that
function is not called).

**0xd2, the host's Advanced Settings** (0x20): taken only when all four hold: the session is the party session; the
message's sender id equals the session's host id; this player is not the host; bit 0 of the functions switch word
is clear. Then: the record from `u3h_adv_rules()` gets respawn, health, weapon and the two flag bytes from the
message; the game's selected net game (s16 at image offset 0x17d167c) = the message's selection; the record's first
word = selection + 1 (computed as a signed value, stored as u32: selection −1 gives 0); 1 is stored through
`u3h_adv_received()`. When the condition fails nothing is written and neither helper function is called.

**0xcc, kicked** (0x10): `u3x_lan_client_lost(base, view, 1)`.

**Any other id**: result 0 (after the counters).

### 4.2 `u3x_lan_client_update(base, view)`

1. If the client socket is not open (`u3x_sock_open`): nothing.
2. `u3x_sock_poll`: negative = the connection is lost (4.3, not kicked) and the function ends; 0 = nothing to read,
   the function ends.
3. One `u3x_sock_recv` into the free part of the buffer: destination buffer + N, size 0x800 − N (also when that is
   0), blocking flag 1.
   - A positive result is added to the received count (in memory, through the view).
   - Result 0: the connection is lost (4.3, not kicked).
   - A negative result: nothing here (the socket layer has closed the socket; no session event is raised; later
     updates end at step 1).
   Whatever the result was, step 4 follows.
4. While the received count is not 0: handle one message (4.1). A result of 0 ends the loop. Otherwise the handled
   bytes (never more than the count) are removed from the front: the bytes behind them move to the start of the
   buffer and the count shrinks by that amount. The buffer bytes beyond the new count are not cleared (they keep
   what they held).

"The connection is lost, not kicked" in steps 2 and 3 = exactly the effects of 4.3 with `kicked` 0 (one socket
close, the "disconnected" event, room id 0, members cleared).

### 4.3 `u3x_lan_client_lost(base, view, kicked)`

In this order: close the view's socket (`u3x_sock_close`); raise the session event "disconnected" (entry 5); if
`kicked` is non-zero raise "kicked" (entry 6); set the session's room id to 0; call the game's clear-members
function (image offset 0x7a25f0) with the session. During the events the room id is still the old one.

After a kick the caller's loop goes on with whatever is still in the buffer.

### 4.4 `u3x_lan_netinit(base, s)`

1. Backend setup (4.5) gives the backend object.
2. Call the backend's Init (entry 0). Store the low 32 bits of the result in eb[66]. If the result is not 0 and
   U3MP_LAN_SOLO is off (eb[0] bit 1 set): stop here.
3. Call the game's session initialisation (image offset 0x7a2360) with the party session. Then zero the seven party
   state fields of 3.2 (each with its own width).
4. Call the backend's AttachSession (entry 3) with the match session, then with the party session.
5. If the LAN flag (u8 at image offset 0x17d5c60) is non-zero: if the party's u32 at 0x1338 is 0, set it to 1; set
   the party's u8 at 0x1374 to 0.
6. Call the backend's entry 8 with the party session and the s32 value 1.
7. `u3h_gt_open(base)`. If its result is not 0 and U3MP_LAN_SOLO is off: stop here.
8. `u3h_cm_init_lan(base)`.

The result of the calls in steps 4 and 6 is ignored.

### 4.5 Backend setup: `u3x_lan_setup(base, s, vtable, storage, slot0)` (old `u3h_lnb_setup(base, s)`)

`vtable` = the C part's table of 32 function pointers, `storage` = the C part's static backend object (0x249d8
bytes), `slot0` = the address of the C part's slot 0 function (the backend's Init; it stays in the C part).

1. eb[65] plus 1.
2. Fill the function table: `u3x_lnb_vtable(vtable, slot0)` (done on every setup).
3. Existing object = `u3h_lnb_object(base)`.
   - None: the object to use is `storage`.
   - There is one: the object to use is that one. First shut it down: call `u3x_lnb_s2` on it directly (not through
     the table); then for each of the four bindings, in order: close its client socket (`u3x_sock_close`), then stop
     its room server (`u3x_lnb_srv_stop`).
4. Zero all 0x249d8 bytes of the object, then construct it: `u3x_lnb_ctor(object, vtable)`.
5. Store `base` and `s` in the object's two helper fields; eb[64] = low 32 bits of the object's address.
6. Register it: the pointer at image offset 0x17dd768 (manager) and the pointer at image offset 0x17dd758 (LAN
   object) both become the object.
7. Copy the 0x24 bytes of the player's network name (image offset 0x17d6010) into the object's name field.
8. Result: the object.

### 4.6 `u3x_lan_tick(base, s)`

Nothing happens unless "multiplayer active" (u8 at image offset 0x17d5c61) is non-zero. Otherwise, in this order:

1. Frame parts 0 (4.7).
2. Take, in this order: the LAN object from `u3h_lnb_object(base)`, the manager pointer from image offset 0x17dd768,
   the online object from `u3h_online_manager(base)`. If the manager is null, or is neither the LAN object nor the
   online object: stop here.
3. Call the manager's Update (entry 1). If its result is not 0: set the network error flag (u8 at image offset
   0x17d5c80) to 1.
4. If the profile objects pointer (image offset 0x17dd610) is not null: eb[85] plus 1, then call the party
   machine's update (image offset 0x77d270) with the party session.
5. Frame parts 1.
6. Online re-attach of the match session: if the online object is not null, and the manager taken in step 2 is the
   online object, and `u3h_onb_lan_flag()` is 0, and the party session's backend pointer is not null, and the match
   session's backend pointer is null: call the online object's AttachSession (entry 3) with the match session, then
   eb[230] plus 1. The conditions are tested in this order and the test stops at the first that fails
   (`u3h_onb_lan_flag` is not called when one of the first two fails).
7. Frame parts 2.

### 4.7 Frame parts: `u3x_lan_frame_parts(base, s, which)` (old `u3h_lnb_frame_parts`)

- `which` = 0: eight calls in this order: the game-packet frame (`gt_frame`), the net time block
  (`gt_net_time_block`), the lobby scene step (`u3x_lobby_scene(base, s)`), the side placement fix (`e03_side`), the
  placement fix (`f05_place`), the clock diagnostics (`k42_clock`), then the two HUD fixes (`k47_hud`, then
  `e01_hud`). Each gets `base`, and `s` where its signature in 2.2 has it.
- `which` = 1: the master channel (`gt_master_channel(base)`).
- any other value: the NAT probe (`onb_probe_tick(base)`).

Nothing else: no conditions, no counters.

### 4.8 `u3x_lan_shutdown(base)`

1. Take the LAN object (`u3h_lnb_object`) and then the online object (`u3h_online_manager`). If both are null:
   nothing.
2. eb[90] plus 1.
3. Call the game's leave function (image offset 0x7a2470) with the party session, then with the match session.
4. Read the manager pointer (image offset 0x17dd768) now. If it is not null and is the LAN object or the online
   object: call its Shutdown (entry 2).
5. Set the manager pointer to null.
6. `u3h_gt_close(base)`, then `u3h_cm_shutdown(base)`.

The LAN object pointer at image offset 0x17dd758 is not cleared.

### 4.9 `u3x_lan_refresh(base)`

Nothing when eb[0] bit 2 is set; nothing when there is no LAN object (`u3h_lnb_object`; the page is then not asked
for); nothing when `u3h_active_page(base)` is not 0xbb3595bc. Otherwise read the local clock (s32 at image offset
0x17b4ba4); if (clock − eb[94]), taken as an unsigned 32-bit difference, is 1500 or more: call the LAN object's
Search (entry 5), then store the local clock, read again after the call, in eb[94].

(eb[94] is also written by `u3x_lan_find_party`, the "Find a Party" action, which stays as it is.)

### 4.10 `u3x_lan_row_name(base, row)`

If the party session's room id is 0: null. Otherwise go through the ten member slots in order and count those that
are in use and whose member id differs from the party's own member id; the result is the address of the slot that
gets the count `row` (the first other member is row 1). Null when there is no such slot (also for `row` ≤ 0).

### 4.11 `u3x_lan_rows(base)`

If the party session's room id is 0: 1. Otherwise the number of member slots in use (the own one included), but at
least 1.

## 5. Test scenarios

Everything in this group can be tested old against new without the game, on a fake image (a memory block addressed
through `base`) with: call-recording stubs at the four game function offsets; fake session objects and fake backend
objects whose function tables point at recording stubs (with settable results); recording stubs for the `u3h_` /
`u3x_` functions of 2.2 (settable results); a fake socket layer behind `u3x_sock_open / poll / recv / close` (a
scripted byte stream with poll results, short reads, end of stream and errors); a fake helper state block behind
`u3h_ext()`. Compare memory (image, sessions, buffers, counters, the fake state), results, and the order and
arguments of all recorded calls. What needs the live game (two PCs or a PC and a console on one LAN): that the
game's session code reacts correctly to the events, that a real room server's traffic is accepted, the NetInit /
NetShutdown cycle, the search refresh on the real page.

Client message (buffer and count set directly):
- Each of the seven ids with N one below its size (result 0; counters: unchanged for 0xc9, changed for the others)
  and N exactly its size and larger.
- N = 1, 2, 3 with a stale id in the buffer.
- 0xc9 with 0, 1, 10 and 11 (and 0xffff) records; with "you" set on none / the first / a later record (address
  noted only for the others, before the add); join done 0 and 1; create done 0 and 1 (the four combinations: which
  events, in which order, the field values seen by the event stubs).
- 0xca with "you" 0 and 1, host 0 and 1; check the member info handed to `u3x_add_member` field by field.
- 0xcb: the name pointer handed over points into the buffer at 0x10.
- 0xce: member found / not found; data length 0, 0x40, above 0x40 (stored unchanged); member id above 0xff.
- 0xd3: party session and guest (flag set); party session and host; the match session.
- 0xd2: the four conditions failing one at a time; selection −1, 0, 0x7fff, −0x8000; check every field of the
  record, the s16 global, the flag.
- 0xcc: order socket close, event 5, event 6, room id 0, clear members; result 0x10.
- Unknown ids (0, 0xc8, 0xcd, 0xcf, 0xd1, 0xffffffff): result 0, counters changed.

Client update:
- Socket closed; poll −1; poll 0; poll 1 with receive results −1, 0, 1 byte, exactly one message, one and a half
  messages, three messages in one read, a message completed by the next update.
- Receive result 0 with a complete message already in the buffer (leave, then the message is still handled).
- A kick followed by another complete message in the same read.
- An unknown id at the front (nothing consumed; later the buffer fills: the receive is asked for 0 bytes).
- The buffer content after the front is removed (the tail beyond the new count is unchanged).
- Receive size and destination for N = 0, 5, 0x7ff, 0x800.

Client lost: kicked 0 and 1.

NetInit: Init result 0; non-zero with LAN solo on (continues) and off (stops after eb[66]); LAN flag 0 and 1; the
party's 0x1338 changed by an AttachSession stub (then left alone); `u3h_gt_open` 0, negative with solo on and off;
check the order: setup, Init, session init, field resets, attach match, attach party, entry 8 with (party, 1), game
sockets, connection manager.

Setup: no object registered (the static storage is used, no shutdown calls); an object registered (Shutdown slot
function, then per binding close and stop, in order, before the object is zeroed); the whole object zeroed except
what the constructor stub and steps 5 to 7 write; both globals; the name copy of exactly 0x24 bytes; eb[64], eb[65];
the table fill on every call.

Tick: multiplayer inactive (no call at all); manager null; manager a foreign pointer; manager = LAN object; manager
= online object; Update result 0 / −1 / 1; profile pointer null and set; the re-attach with each of its five
conditions failing in turn (and `u3h_onb_lan_flag` not called when the first two fail); the manager pointer changed
by frame part 0 (the new value counts) and by the Update stub (the value from step 2 counts); the full call order.

Frame parts: `which` 0, 1, 2, −1, 7: the call lists and arguments.

Shutdown: both objects null; LAN only; online only; manager null / LAN object / online object / foreign; manager
changed by the leave stubs (the value read after them counts); order of calls; manager cleared; LAN pointer kept.

Refresh: switch bit set; no LAN object (page not asked); other page; difference 1499, 1500, a clock that wrapped
(negative difference as a large unsigned value); the clock changed by the Search stub (the second value is stored).

Rows: room id 0; 0, 1, 2, 10 used slots; the own slot first / in the middle / last / absent; two slots with the own
id (both skipped by the name, both counted by the count); `row` 0, −1, 1, the last, one beyond.

## 6. Oddities of the old code, to be kept

1. The message id is read from the buffer even when fewer than 4 bytes are waiting.
2. The message counters count every call for a non-0xc9 id, also when the message is still incomplete or unknown;
   an incomplete 0xc9 is not counted.
3. An unknown id is never consumed: the client stays stuck on it and the buffer fills. With a full buffer the
   receive is still made, for 0 bytes, and its result is treated like any other (a 0 means "the peer closed": the
   room is left).
4. A receive error raises no session event and leaves the room id and the members as they were; only "poll broken"
   and "peer closed" leave the room.
5. After "peer closed" and after a kick the remaining buffer content is still processed.
6. Member data from 0xce stores the length unchecked (the 0x40 data bytes are always copied).
7. Room ids inside the messages are not compared with the session's room; the 0xcb member id is not used.
8. 0xd3 and 0xd2 act only for the party session; they are consumed for any session.
9. The limit of the handled size to the received count in the update loop can never apply (every handler tests the
   size first).
10. NetInit writes the party's u8 at 0x1374 twice (reset, and again under the LAN flag); the "if 0 then 1" on
    0x1338 follows its own reset, so it only stays 0 when the LAN flag is 0.
11. NetInit calls the backend's entry 8, which the LAN backend does not implement.
12. With U3MP_LAN_SOLO on, NetInit continues after a failed Init and after failed game sockets.
13. The tick's re-attach compares the manager value taken before the Update call; `u3h_online_manager` already
    answers null unless the online object is the manager, so that comparison is redundant.
14. Shutdown also runs (leaves both sessions, closes the game sockets) when only the online object exists; it does
    not clear the LAN object pointer.
15. Setup shuts an existing object down by calling the Shutdown slot function directly and then closes the client
    sockets a second time.
16. The refresh reads the clock twice (before and after the Search).
17. The row count includes the own member while the row names skip it (the list's row 0 is the own row, filled by
    the caller); a party whose slots are all free still counts 1.
18. The C wrappers `lnb_cli_msg`, `lnb_cli_lost`, `lnb_row_name` and `lnb_rows` have no caller in the C part.

## 7. What stays as it is

- The C part's storage: the backend object, its function table and slot 0 (Init), and the C wrappers that build the
  client view from a binding (the C part asserts the binding's layout against the C++ part).
- Every exported name and signature of 2.1; the `u3h_` exports of 2.2.
- `u3x_lan_find_party`, `u3x_lan_reattach` and `u3x_lan_party_size` (same source block, in no work group, not
  called by this group): unchanged.
- The wire layouts of 3.9 (shared with the room server of G09 and with helpers already installed on other PCs) and
  the eb[] indices (read by the diagnostic tools).

## 8. Clarifications

Facts confirmed by the tester's old-against-new test (`reimpl2\G08\test_g08.cpp`). Nothing in sections 1 to 7 was
found wrong; these are additions.

Answers to the implementer's questions:

1. **Backend table entries without arguments** (Init 0, Update 1, Shutdown 2, Search 5) are called directly through
   the object's function table, exactly like the entries with arguments (3 and 8). `u3h_lnb_call` is used only for
   the session events (entries 1, 2, 5 and 6 of a session's table). A backend call through `u3h_lnb_call` would be
   one call too many in the call sequence.
2. **Advanced Settings message (0xd2), order of the four conditions:** the functions switch word is looked at last,
   and only when the other three hold (seen in the number of `u3h_ext()` calls). The order of the other three
   (party session, sender is the host, this player is not the host) cannot be observed: they are plain reads of the
   session and of the message.
3. **Poll results:** every positive value means "something to read" (2 and 0x7fffffff act like 1); every negative
   value means "lost" (−2 and −0x80000000 act like −1). **Receive results:** every negative value is "nothing here",
   not only −1. `u3x_sock_open`: every non-zero result means open. `kicked`: every non-zero value raises the
   "kicked" event.
4. **The received count** is always read and written in memory, through the view, and something called in between
   may change it (the backend's Create and Join slots set it to 0; whether the game's reaction to a session event
   ever reaches them was not checked):
   - the update reads it once before the receive call, for the destination and the size; after the call a positive
     result is added to the count as it stands in memory then (not to the value read before the call);
   - it is read again as the loop condition before every message, and the message function reads it once, at its
     start;
   - after the message function returns it is read again, and that value is the one the handled size is limited
     to, the one that says how many bytes behind the message move to the front, and the one the handled size is
     taken from. Examples with a kick (0x10 bytes) followed by 0x38 more bytes, when the "kicked" event changes the
     count: to 0 or to 5: nothing moves, the count ends as 0; to 0x30: 0x20 bytes move to the front, the count
     becomes 0x20.
5. **Padding of the member info:** the four bytes at 0x14 and the four bytes at 0x24 are not written by the old
   code (they hold whatever was on the stack) and `u3x_add_member` reads only the named fields. Any content is
   right; zero is fine.
6. **The slot's in-use byte:** every non-zero value counts as used, in the row name and in the row count (tested
   with 1, 2, 0x80 and 0xff).

Further facts:

7. Results and pointers tested against 0 are tested with all 64 bits: the Init result (0x100000000 counts as a
   failure although eb[66] then gets 0), the Update result, the profile objects pointer, the room id of the party
   session (0x100000000 is "in a room").
8. Member ids are compared with all 32 bits: the 0xd2 sender against the host id, and a slot's member id against
   the own id in the row name. The 0xce member id handed to `u3x_find_member` is the message's 16 bits widened to
   32 (a slot whose id is 0x11234 is not the member 0x1234).
9. Member joined: the old code reads the record's member id, NAT type and data length before `u3h_cm_lanaddr` runs
   and the two flag bytes it hands to `u3x_add_member` after it. This cannot be observed: `u3h_cm_lanaddr` writes
   only the game's connection manager and eb[87], never the receive buffer. Either moment is right.
10. `u3h_ext()` (a getter without side effects) is not part of the call sequence; the old code calls it once per
    message call, including for an incomplete room state.
11. Setup: the function table is filled before the existing object is asked for; the Shutdown slot function, the
    four socket closes and the four room server stops all see the old object unchanged; the constructor is called
    on an object that is zero in all 0x249d8 bytes.
12. NetInit: every non-zero value of the LAN flag byte acts like 1. The second AttachSession call may change the
    party's u32 at 0x1338; a non-zero value is then kept.
13. Frame parts: a negative `which` is "any other value" (the NAT probe).
14. Tick: the manager pointer is read after the `u3h_lnb_object` call has returned and before
    `u3h_online_manager` is called. Shutdown: the manager pointer is read after both leave calls. Refresh: the
    clock value stored in eb[94] is read after the Search call.
