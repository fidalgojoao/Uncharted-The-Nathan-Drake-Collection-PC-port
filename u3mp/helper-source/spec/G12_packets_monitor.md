# G12. Reliable packet header, connection monitor, state-enter kicks

Behaviour description for the clean-room pipeline (reimpl2, describer's document). It says what the functions do, not
how the old ones were written. Source of the facts: helper v7.212, the C functions `ec_find_np` and `ec_enter` and the
C++ functions `u3x_session_members`, `u3x_members_check`, `u3x_players_check`, `u3x_gt_base_write`,
`u3x_gt_base_header`, `u3x_gt_drop_expired`, `u3x_gt_has_data` (185 code lines), and all their callers.

Conventions of this document: "image offset X" is the address `base + X` (`base` = game image base). Offsets with 0x
are hexadecimal, others decimal. All values are little endian. "u8 / u16 / u32 / u64 / s32" are unsigned / signed
integers of that width; "pointer" is 8 bytes. All 32-bit sequence arithmetic is unsigned and wraps modulo 2^32 unless
a line says otherwise. Game functions use the System V x86-64 convention (integer and pointer arguments in order,
result in rax); "virtual slot n of an object" is the function pointer stored at (the object's first 8 bytes, its
function table) + 8·n, always called with the object itself as first argument.

No function of this group takes, returns or computes a float.

## 1. Purpose and callers

The group holds three independent pieces of the helper's network layer. All of it runs on the game's main thread.

**A. Session member list and the EstablishConnection Enter (the "state-enter kicks").**

- `u3x_session_members` builds the list of the members of a game session (match session or party session), the
  player's own member first. Callers: the C wrapper `ec_members` (one line; used by `ec_enter`, by `mm_tell_party`
  and by the C export `u3h_members`, which the C++ part calls from `u3x_members_check` and from a friend-join
  function of another group).
- `ec_find_np` finds the net player record that belongs to a session member and a user number. Callers: `ec_enter`
  and the C export `u3h_ec_find_np` (used by the lobby ordering code of another group).
- `ec_enter` is the body the PS4 build lacks for "Enter" of the net game process state EstablishConnection (the game
  function at image 0x67fe20 is an empty return; the helper's builder redirects it to a stub of kind 55). The C
  dispatcher `u3_profile_event` handles kind 55: when `base` and the event pointer (the state object) are both
  non-null it calls `ec_enter(base, state)`; it returns 1 in any case. So this runs once each time the state
  EstablishConnection is entered.

**B. The connection monitor's two checks.** Once per net frame (C function `gt_frame`, only while the net clock
pointer at image 0x17c3580 is set, the net-active byte at image 0x17d5c61 is non-zero and the helper's game socket
is open) the function `u3x_connection_monitor(base, arg)` (other group, stays) runs, in this order: counter,
`u3x_members_check(base, arg)`, the time-sync request (other group), `u3x_players_check(base)`. `arg` is a byte that
`gt_frame` flips on every call before its own checks (1, 0, 1, ...), so the member check works on every second call.
The C wrappers `gt_members_check` / `gt_players_check` (one line each) exist too.

**C. The reliable header of game packets.** Every connection entry has a "sub-object" (entry + 0x40) that numbers
the data packets it sends, acknowledges those it receives and keeps a record per sent packet until the other side
has acknowledged it or it is known lost. The packet pump `gt_pump` (C, other group) uses:

- per received data packet (type 0): `u3x_gt_base_read` (other group, stays), which stamps the receive time, calls
  `u3x_gt_base_header`, and reads the payload when that returns 1;
- per active entry and frame: `u3x_gt_drop_expired`; then, when a data packet is due (every 0.05 s), it asks
  `u3x_gt_has_data` (only when the 2 s keep-alive is not due) and, to send, writes the type byte, calls
  `u3x_gt_base_write`, adds one 0 bit and sends.

  The C wrappers `gt_base_write`, `gt_base_header`, `gt_drop_expired`, `gt_has_data` are one line each.

## 2. Interface

Names and signatures of the seven C++ functions stay exactly as they are (their C declarations and wrappers stay).
The two C functions get new C++ bodies under new names; their C functions become one-line wrappers.

| Function | C++ signature (`extern "C"`) | Called by | C wrapper passes |
|---|---|---|---|
| member list | `uint32_t u3x_session_members(uint8_t *session, uint8_t **out)` | C `ec_members`; may be called directly inside this part | its two arguments unchanged |
| find net player (new name) | `uint8_t *u3x_ec_find_np(uintptr_t base, const uint8_t *member, int32_t idx)` | C `ec_find_np` (which `u3h_ec_find_np` forwards to); `u3x_ec_enter` | its three arguments unchanged |
| state Enter (new name) | `void u3x_ec_enter(uintptr_t base, uint8_t *st)` | C `ec_enter` (from `u3_profile_event`, kind 55) | its two arguments unchanged |
| member check | `void u3x_members_check(uintptr_t base, int arg)` | `u3x_connection_monitor`; C `gt_members_check` | unchanged |
| player check | `void u3x_players_check(uintptr_t base)` | `u3x_connection_monitor`; C `gt_players_check` | unchanged |
| header write | `void u3x_gt_base_write(uintptr_t base, uint8_t *sub, void *b)` | C `gt_base_write` (from `gt_pump`) | unchanged |
| header read | `int u3x_gt_base_header(uintptr_t base, uint8_t *sub, void *b)` | `u3x_gt_base_read`; C `gt_base_header` | unchanged |
| drop expired | `void u3x_gt_drop_expired(uintptr_t base, uint8_t *sub)` | C `gt_drop_expired` (from `gt_pump`) | unchanged |
| has data | `int u3x_gt_has_data(uintptr_t base, uint8_t *sub)` | C `gt_has_data` (from `gt_pump`) | unchanged |

Parameters and results:

- `session`: a session object (3.2). `out`: room for 10 pointers; receives member slot pointers. Result: how many
  were written (0..10).
- `member`: a member slot (3.2); only its key (the bytes from offset 0) is used. `idx`: user number wanted (0 = the
  member's first player, 1 = its second). Result: the net player record or null.
- `st`: the EstablishConnection state object (3.5). Never null (the caller checks).
- `arg`: non-zero = do the member check on this call.
- `sub`: a connection sub-object (3.6). `b`: the packet's bit stream (3.8), opaque here.
- `u3x_gt_base_header` returns 1 when the packet is a new reliable packet whose payload must be read next, else 0.
- `u3x_gt_has_data` returns 1 or 0.

Helpers of the C part and of other groups that the new code calls (not described or reimplemented here):

| Name | Signature | What it does |
|---|---|---|
| `u3h_ext` | `uint8_t *u3h_ext(void)` | address of the helper's own state block (3.9) |
| `u3h_mp_code_is` | `int u3h_mp_code_is(uintptr_t base, uint32_t off, const char *hex12)` | 1 when the 12 bytes at image offset `off` equal the 24 lower-case hex digits of `hex12`, else 0; reads only |
| `u3x_key_equal` | `int u3x_key_equal(const uint8_t *a, const uint8_t *b)` | 1 when two player keys are the same text: bytes compared from the start, equal at the first common NUL byte or after 32 equal bytes |
| `u3x_find_member` | `uint8_t *u3x_find_member(uint8_t *session, uint32_t id)` | the first member slot in use whose member id equals `id`, or null |
| `u3x_session_host` | `int u3x_session_host(const uint8_t *s)` | non-zero when the session has no room id (u64 +0x18 is 0) or its own member id (+0x12e4) equals its host member id (+0x12e8) |
| `u3x_session_kick` | `void u3x_session_kick(uint8_t *s, uint32_t id)` | the kick request of 3.3 with flag 0 and no counter (nothing when the session has no usable backend) |
| `u3x_entry_by_id` | `uint8_t *u3x_entry_by_id(uintptr_t base, int32_t id)` | the first of the 32 connection entries (3.4) whose id field equals `id`, in use or not; null when none |
| `u3x_net_player_id` | `uint32_t u3x_net_player_id(const uint8_t *np)` | a net player's id (rule in 3.1) |
| `u3h_link_timeout` | `int u3h_link_timeout(uintptr_t base, uint8_t *entry)` | 1 when the entry's link has been silent for more than 20 s (at most one report per entry every 10 s); it keeps its own per-entry state, so WHEN it is called is part of the behaviour (4.5) |
| `u3h_gt_stamp` | `void u3h_gt_stamp(uintptr_t base, uint8_t *sub, int off)` | stores the current game time in seconds, as a 32-bit float, at `sub + off` |
| `u3h_gt_free` | `void u3h_gt_free(uintptr_t base, void *p)` | gives a block back to the game's allocator (one game call) |
| `u3x_gt_header` | `void u3x_gt_header(const uint8_t *sub, void *b, uint32_t flag)` | writes the 54 header bits (3.7) to the stream from `flag` and the sub-object's current fields |
| `u3x_gt_read` | `uint32_t u3x_gt_read(GtStream *s, int count)` | reads `count` bits (first bit read = most significant bit of the result); bits past the stream's end read as 0 |

(`GtStream` is the stream type of 3.8; `b` is a pointer to one. `u3h_members(session, out)` is the C export that
forwards to `u3x_session_members`; new code may call either, the effect is the same.)

## 3. Game and helper facts

### 3.1 Net game manager and net players

The net game manager is the object at image offset 0x17cb060.

| Manager offset | Type | Meaning |
|---|---|---|
| 0x370, 0x378 | pointer ×2 | the two local players' net player records (null when none) |
| 0x380 | pointer | the "main" player's record (for a guest: the player whose loss means the host left); may be null |
| 0x390 + i·0x9c0, i = 0..9 | record | the 10 net player records |

| Net player offset | Type | Meaning |
|---|---|---|
| 0xe1 | u8 | joined (non-zero) |
| 0x1e4 | u32 | id word; its low 12 bits are the id's low part |
| 0x1e8 | u32 (read as s32 in comparisons) | user number: 0 = first player of a member, 1 = second |
| 0x1ec | u8 | second-screen / guest flag |
| 0x5b0 | u32 | member id the player was created for |
| 0x5b4 | s32 | connection id of its member; −1 = none |
| 0x5b8 | bytes | player key (text, compared with `u3x_key_equal`) |
| 0x5e4 | u8 | leaving / gone |
| 0x5ee | u8 | readiness flag of EstablishConnection |

**Net player id** (also `u3x_net_player_id`): low = (u32 at +0x1e4) AND 0xfff; when the byte at +0x1ec is non-zero
and low is not 0, low is raised by 0x800; id = low + (u32 at +0x1e8) · 0x1000, modulo 2^32.

### 3.2 Sessions and members

Match session: image offset 0x17d6420. Party session: image offset 0x17dc298. Same layout:

| Session offset | Type | Meaning |
|---|---|---|
| 0x08 | pointer | backend object (null = none) |
| 0x18 | u64 | room / session id (0 = no session) |
| 0x680 + k·0x120, k = 0..9 | record | the 10 member slots |
| 0x12e4 | u32 | own member id |
| 0x12e8 | u32 | host member id |

| Member slot offset | Type | Meaning |
|---|---|---|
| 0x00 | bytes | the member's key |
| 0x25 | u8 | slot in use (non-zero) |
| 0x2c | u32 | member id |
| 0x34 | s32 | connection id |

### 3.3 Backend kick request

Virtual slot 9 of a session's backend object: arguments (backend, session, member id as u32, flag as int), result a
64-bit integer that is ignored. "Kick request (session, id, flag)" below means: read the backend pointer at
session +8; when it is null, or its first 8 bytes (the function table pointer) are null, nothing happens at all;
otherwise that call is made. `u3x_session_kick` is this with flag 0; it does not tell whether the call was made, so
4.3 (flag 1, and a counter per call made) needs the request done by the new code itself.

### 3.4 Connection entries

32 entries of 0x220 bytes; entry i is at image offset 0x17f31b0 + 8 + i·0x220 (`u3x_entry_by_id` searches them).

| Entry offset | Type | Meaning |
|---|---|---|
| 0x00 | s32 | connection id |
| 0x04 | u8 | active |
| 0x05 | u8 | dropped (the connection was closed / flagged) |
| 0x06 | u8 | lost (set when the link timed out on a guest) |
| 0x40 | object | the connection sub-object (3.6); so sub +0xa0 = entry +0xe0 |

### 3.5 Other game data and functions

| Image offset | Type | Meaning |
|---|---|---|
| 0x17c3580 | pointer | net clock object, may be null; its s32 at +0xc is the net time (ticks of 1/300 s) |
| 0x17d603c | u32 | net menu mode; the value 2 matters here (party / custom-game flow) |
| 0x17d5c61 | u8 | net-active flag (multiplayer network is up) |
| 0x17d5c98 | s32 | a net flag word (0x17d5c58 + 0x40); see 4.5 |
| 0x1b24b80 | pointer | the game object, may be null; its u8 at +0x3a04 is used in 4.5 |
| 0x1b24b98 | pointer | frame clock object (used without a null test); its s32 at +0x114 is the game time in ticks of 1/300 s |
| 0x74b290 | function | no arguments, no result: the game's reset of its pending player-state sends |
| 0x7a3400 | function | arguments (session, member slot); result: pointer to that member's lobby data record or null. The u8 at offset 25 (0x19) of the record is non-zero when the member has a second player |

Both functions are called only when `u3h_mp_code_is` confirms their first 12 bytes: 55 48 89 E5 41 57 41 56 41 55 41
54 for each of the two (the same usual frame prologue; as text for `u3h_mp_code_is`: `554889e54157415641554154`,
which the new code has to keep in a `U3X_RO` character array). A failed check simply leaves the call out.

EstablishConnection state object (`st`): u8 at +0x18 = "settings sent" flag; s32 at +0x1c = the state's start time
(net time).

### 3.6 The connection sub-object and its lists

| Sub-object offset | Type | Meaning |
|---|---|---|
| 0x00 | pointer | function table (virtual slots below) |
| 0x08 | u32 | out sequence: number of the last reliable packet sent |
| 0x0c | u32 | in sequence: number of the newest packet received |
| 0x14 | u32 | ack bits: bit j set = the packet numbered (in sequence − j) was a reliable packet that arrived |
| 0x18 | u32 | last acked: highest own packet number the other side has reported on |
| 0x1c + 4·n, n = 0..31 | u32 | per sent packet (index = its number AND 31): the in sequence at the time it was sent |
| 0xa0 | s32 | last-heard net time (written by 4.5; copied into each sent record as its tag) |
| 0xa4 | float | time of the last reliable send (written through `u3h_gt_stamp`) |
| 0xac | u32 | count of records reported lost |
| 0xb0 | u32 | count of records reported acked |
| 0x100 | pointer | in-flight records: oldest (head); null = none |
| 0x108 | pointer | in-flight records: newest (tail); meaningful only while the head is non-null |
| 0x110 | pointer | head of the queue of reliable events waiting to be sent |
| 0x120 | pointer | head of the queue of unreliable events waiting to be sent |
| 0x128 | pointer | tail of that unreliable queue |
| 0x164 | s32 | game-tick time at which the next "something to send" keep-alive is due |

Virtual slots of the sub-object used by this group:

| Slot | Arguments | Result | Meaning |
|---|---|---|---|
| 0 | (sub) | pointer | allocates a sent record; null when none is available |
| 2 | (sub, stream, record) | ignored | writes the packet's payload to the stream and attaches the events it carries to the record |
| 3 | (sub, record, null) | ignored | the record's packet was acknowledged |
| 4 | (sub, record, null) | ignored | the record's packet was lost |

(Slot 1, the payload reader, is called by `u3x_gt_base_read`, not here.)

Sent record: u32 at +0 tag; u32 at +4 packet number; pointer at +8 next (newer) record; pointer at +0x10 first event
node carried by the packet.

Event node (used in a record's event list and in the unreliable queue): pointer at +0 the event (may be null);
pointer at +0x10 an extra block owned by the node (may be null); pointer at +0x20 next node.

Event object: function table at +0; u8 at +9 event type (type 2 does not count as pending data); u8 at +10 reference
count. Virtual slot 1: (event), no result: called when the count reaches 0 (the event goes away). Virtual slot 7:
(event), result a boolean in the low 8 bits of rax (the upper bits are undefined and must be ignored): "this event
has expired".

### 3.7 The header on the wire

54 bits, in stream order: 1 bit reliable flag; 11 bits = the sender's out sequence AND 0x7ff; 10 bits = the sender's
in sequence AND 0x3ff; 32 bits = the sender's ack bits (sub +0x14). `u3x_gt_header(sub, b, flag)` writes exactly
this from the sub-object's fields at the moment it is called.

### 3.8 The bit stream

`struct GtStream { uint32_t *words; uint32_t pos, max, x, pad; }` (0x18 bytes, defined in the C++ part before this
group's code): 32-bit words filled from the most significant bit, `pos` bits used of `max`. This group reads and
writes it only through `u3x_gt_read` and `u3x_gt_header`.

### 3.9 The helper's state block (address from `u3h_ext()`)

Diagnostic counters, all u32, all only ever incremented by 1 (or stored) by this group:

| Address | Old name | Meaning |
|---|---|---|
| +0x3a80 + 4·32 | nd[32] | kick requests made by the state Enter |
| +0x3a80 + 4·33 | nd[33] | state Enter runs |
| +0x3a80 + 4·34 | nd[34] | last start time the Enter left in the state object |
| +0x4050 + 4·101 | eb[101] | reliable packets written |
| +0x4050 + 4·103 | eb[103] | records acked |
| +0x4050 + 4·104 | eb[104] | records lost |
| +0x4050 + 4·116 | eb[116] | expired events dropped |
| +0x4050 + 4·118 | eb[118] | packets written without the reliable flag (window full) |
| +0x4050 + 4·121 | eb[121] | members the host check asked to kick |
| +0x4050 + 4·122 | eb[122] | "main player gone" marks |
| +0x4050 + 4·123 | eb[123] | last-heard stamps |
| +0x4050 + 4·124 | eb[124] | silent players the host asked to kick |
| +0x4050 + 4·125 | eb[125] | silent links a guest flagged |

## 4. Behaviour

### 4.1 `u3x_session_members`

1. When the session has no backend (pointer at +8 null) or no room id (u64 at +0x18 is 0): result 0, `out` untouched.
2. Otherwise the 10 member slots are visited in slot order and every slot in use (byte +0x25 non-zero) is listed.
   A slot is normally added at the end of the list. Exception: when the list already holds at least one member and
   the slot's member id (+0x2c) equals the session's own member id (+0x12e4), the slot takes the FIRST place and the
   member that was first goes to the end (the new last place); the members between keep their places. (Slots in
   use A, B, Self, D give Self, B, A, D. An own slot that is the first one in use is simply first. If more than one
   slot carries the own id, the rule applies to each in turn.)
3. Result: the number of slots listed. Nothing but `out[0 .. result−1]` is written.

### 4.2 `u3x_ec_find_np` (old `ec_find_np`)

Result: the first of the 10 net player records (index order, 3.1) for which both hold: its key (+0x5b8) equals the
member's key (member +0) by `u3x_key_equal(player key, member key)`, and its user number (s32 at +0x1e8) equals
`idx`. Null when there is none. The joined / leaving bytes are NOT looked at. Reads only.

### 4.3 `u3x_ec_enter` (old `ec_enter`)

In this order:

1. Start time: when the net clock pointer (image 0x17c3580) is non-null, the s32 at clock +0xc is stored at
   `st` +0x1c. When it is null the field keeps its value.
2. When the code check of 3.5 passes for image 0x74b290, that function is called (no arguments).
3. The byte at `st` +0x18 is set to 0.
4. Readiness reset. The member list of the MATCH session is built (4.1). For each listed member, in list order:
   - the net player of that member with user number 0 (4.2), when there is one, gets its byte +0x5ee set to 0;
   - then, when the code check passes for image 0x7a3400, that function is called with (match session, member).
     This call is made for every listed member, whether or not a net player was found. When it returns a record
     whose byte at offset 25 is non-zero, the net player of that member with user number 1, when there is one, gets
     its byte +0x5ee set to 0. (Check fails, null result or byte 0: nothing for user 1.)
5. Party members without a player. Only when the u32 at image 0x17d603c equals 2: the member list of the PARTY
   session is built and replaces the list of step 4 (also when it is empty). For each of its members, in list
   order, that has no net player with user number 0: kick request (3.3) on the party session with that member's id
   (member +0x2c) and flag 0; nd[32] is incremented for each request whose backend call was really made (not when
   3.3 does nothing).
6. Players without a member. Only when the net-active byte (image 0x17d5c61) is non-zero: for each of the 10 net
   player records in index order that has user number 0 (s32 at +0x1e8 equals 0) and a non-zero net player id
   (with user number 0 that is: the low 12 bits of the u32 at +0x1e4 are not all 0): when no member of the CURRENT
   list (the party list when step 5 built one, else the match list of step 4) has this very record as its user-0
   net player (4.2), a kick request is made on the MATCH session (always the match session) with the u32 at
   player +0x5b0 as id and flag 1; nd[32] counts it as in step 5. With an empty current list every such player is
   requested to be kicked.
7. nd[33] is incremented and nd[34] is set to the u32 now at `st` +0x1c (read back from the state object, so it is
   the old content when step 1 stored nothing).

The record contents are read when they are needed (after the earlier steps' game calls), not before step 2.

### 4.4 `u3x_members_check`

Works on the match session (image 0x17d6420). Nothing happens unless `u3x_session_host` is true for it and `arg` is
non-zero. Then:

1. The member list is built (4.1; it is empty without backend or room id).
2. "Self" is the result of `u3x_find_member(session, own member id)` when the room id is non-zero (else none).
3. For each listed member other than Self (same slot address), in list order: take its connection id (s32 at
   +0x34) and look for the connection entry with that id (`u3x_entry_by_id`). The member is asked to be kicked when
   NO entry carries that id, or when the entry found has its dropped byte (+5) non-zero. Asking = 
   `u3x_session_kick(session, member id at +0x2c)` followed by eb[121] + 1. The counter is incremented for every
   member asked, also when the kick request itself does nothing (backend without function table).

### 4.5 `u3x_players_check`

The net clock pointer (image 0x17c3580) is read once at the start; "net time now" below is the s32 at that object
+0xc, or 0 when the pointer is null. The 10 net player records are handled in index order, each on its own. A
record is skipped entirely (no effect) when any of these holds, tested on the state before anything is changed:

- it is one of the two local players (its address equals the pointer at manager +0x370 or +0x378);
- its joined byte (+0xe1) is 0, or its leaving byte (+0x5e4) is non-zero;
- its net player id (3.1) is 0;
- its connection id (s32 at +0x5b4) is −1.

For the others, let E be the connection entry with that connection id (`u3x_entry_by_id`), possibly none, and "main"
mean: the main player pointer (manager +0x380) is non-null and the net player id of the record it points to equals
this record's id. Exactly one of the following cases applies:

| Case | Condition | Effects |
|---|---|---|
| live | E exists and its lost byte (+6) is 0 | stamp, then the silence test below |
| gone | (no E, or E's lost byte non-zero) and main | byte +0x5e4 of the player = 1; u32 at player +0x1e4 = 0; eb[122] + 1 |
| lost, not main | E exists, lost byte non-zero, not main | stamp |
| none | no E and not main | nothing |

"Stamp" = the s32 at E +0xe0 (sub-object +0xa0) is set to net time now, and eb[123] + 1.

Silence test (only in the case "live", after the stamp):

1. Gate: it continues only when the s32 at image 0x17d5c98 is 0, or the game object pointer (image 0x1b24b80) is
   non-null and its byte at +0x3a04 is non-zero. Otherwise nothing more for this player, and `u3h_link_timeout` is
   NOT called.
2. `u3h_link_timeout(base, E)` is called (exactly once for this player in this run). Result 0: nothing more.
3. Result non-zero: when the player's byte +0x1ec is non-zero, nothing more. Otherwise, when `u3x_session_host` is
   true for the match session: `u3x_session_kick(match session, the u32 at player +0x1e4)` (the whole word, not
   only its low 12 bits) and eb[124] + 1 (counted also when the kick request does nothing); when it is false: E's
   lost byte (+6) is set to 1 and eb[125] + 1.

`u3h_link_timeout` is called in no other situation (not for skipped players, not in the other three cases).

### 4.6 `u3x_gt_base_write`

Let out = u32 at sub +8 and acked = u32 at sub +0x18 on entry. The window is open when (out − acked) modulo 2^32 is
less than 31.

**Window full** (not open): `u3x_gt_header(sub, b, 0)` is called (header with flag 0 and the unchanged sequence
numbers), eb[118] + 1. Nothing else.

**Window open**, in this order:

1. sub +8 = out + 1 (call it N).
2. `u3x_gt_header(sub, b, 1)`: it therefore writes N AND 0x7ff as the sequence.
3. The u32 at sub +0x1c + 4·(N AND 31) is set to the in sequence (u32 at sub +0xc).
4. `u3h_gt_stamp(base, sub, 0xa4)`.
5. Virtual slot 0 of the sub-object is called (sub). A null result ends the function here: no record, no payload,
   no counter (steps 1 to 4 stay done).
6. The record is filled and appended to the in-flight list, all before step 7, using the sub-object's fields as
   they are after step 5: record +4 = N; when the list head (sub +0x100) is null the record becomes the head,
   otherwise the pointer at (current tail, sub +0x108) +8 is set to the record; the tail becomes the record;
   record +0 = the u32 at sub +0xa0; record +8 = null.
7. Virtual slot 2 is called with (sub, b, record); result ignored.
8. eb[101] + 1.

### 4.7 `u3x_gt_base_header`

1. Read from the stream, in this order: flag (1 bit), seq (11 bits), ack (10 bits).
2. Let in = u32 at sub +0xc. d = (seq − in) modulo 2048, and S = in + d (modulo 2^32): the smallest number not
   below `in` whose low 11 bits are seq. When d ≥ 32 the packet is too far ahead or old: result 0, nothing written,
   and the 32 ack bits are NOT read from the stream (22 bits consumed).
3. Let acked = u32 at sub +0x18. c = (ack − acked) modulo 1024, and A = acked + c (modulo 2^32). When A is greater
   than the out sequence (u32 at sub +8; unsigned comparison of the full 32-bit values) the packet acknowledges
   something never sent: result 0, nothing written, the 32 ack bits not read.
4. Read `bits` (32 bits) from the stream.
5. "New" = d is not 0. The ack bits at sub +0x14 become (old value shifted left by d places, modulo 2^32), with bit
   0 set when the packet is new and its flag is 1. This is stored before step 6. (d = 0: the value is unchanged.)
6. Records. At most c records are taken, one per step j = 1 .. c; step j examines bit (c − j) of `bits` (the
   first, oldest record goes with bit c − 1, the last with bit 0; bit n of `bits` stands for packet number A − n).
   For each step: read the list head (sub +0x100) afresh; when it is null, stop taking records. Otherwise, in this
   order: the head becomes the record's next pointer (record +8); then, when the step's bit is set, virtual slot 3
   is called with (sub, record, null), the u32 at sub +0xb0 is incremented and eb[103] + 1; when it is clear,
   virtual slot 4 is called with (sub, record, null), the u32 at sub +0xac is incremented and eb[104] + 1; then
   `u3h_gt_free(base, record)`. The tail pointer (sub +0x108) is never touched here, and the records' own numbers
   are not looked at. (c is at most 31 whenever the object is consistent; see section 6, item 7.)
7. After all records: sub +0xc = S, then sub +0x18 = A. (During the calls of step 6 these two fields still hold
   their old values, while +0x14 already holds the new one.)
8. Result: 1 when the packet is new and its flag is 1, else 0.

`base` is used only for `u3h_gt_free`.

### 4.8 `u3x_gt_drop_expired`

The unreliable queue (head pointer at sub +0x120, nodes linked by +0x20) is walked once from head to end. For every
node, its next pointer and its event pointer are taken first. A node is REMOVED when its event pointer is non-null
and virtual slot 7 of the event answers true (low 8 bits of the result non-zero); the question is asked exactly once
per node that has an event, in queue order. A node without event is kept and nothing is called for it.

Removing a node, in this order:

1. Unlink: the head pointer (when the node is the head) or the +0x20 pointer of the last kept node before it is set
   to the node's next pointer.
2. The event's reference count (u8 at event +10) is decremented by 1 and stored; when it is now 0, virtual slot 1
   of the event is called (event).
3. When the node's pointer at +0x10 is non-null: `u3h_gt_free(base, that pointer)`.
4. `u3h_gt_free(base, node)`.
5. eb[116] + 1.

After the walk the tail pointer (sub +0x128) is set to the last node kept, or to null when no node was kept. This
store happens on every call, also for an empty queue.

### 4.9 `u3x_gt_has_data`

Result 1 on the first of these tests that holds, else 0:

1. Some in-flight record (from the head at sub +0x100, following record +8) carries, in its event list (first node
   at record +0x10, following node +0x20), a node whose event pointer is non-null and whose event type (u8 at
   event +9) is not 2. In this case nothing is written.
2. The s32 at sub +0x164 is less than or equal to the game time in ticks (3.5; signed comparison). In this case
   sub +0x164 is first set to that game time + 150 (150 ticks = 0.5 s).
3. The pointer at sub +0x110 is non-null, or the pointer at sub +0x120 is non-null.

Tests 2 and 3 are not reached when an earlier one holds (so test 1 leaves the timer alone).

## 5. Test scenarios

A fake image is needed (memory that covers the image offsets of section 3), recording stubs for image 0x74b290 and
0x7a3400 (with and without the 12-byte prologue in place), fake backends, sub-objects, events (function tables whose
entries record their arguments), and recording replacements for the `u3h_` helpers. Compare memory, results, counter
values and the order and arguments of every call.

`u3x_session_members`
- backend null; room id 0 (each alone): result 0, `out` untouched.
- no slot in use: 0. All 10 in use: 10.
- own slot first in use; own slot in the middle (A, B, Self, D → Self, B, A, D); own slot last; own id in no slot;
  own id in two slots; slots in use with gaps.

`u3x_ec_find_np`
- key matches in record 3 with user number 0 and in record 5 with user number 1: idx 0 → record 3, idx 1 → record 5,
  idx 2 → null.
- two records match: the lower index wins. Keys equal only up to a NUL (different bytes after it) match. A record
  whose joined byte is 0 still matches. No match → null.

`u3x_ec_enter`
- clock present / null (start time stored / kept; nd[34] in both cases).
- prologue of 0x74b290 right / wrong (called once before the byte +0x18 store / not called).
- match list of three members: one with players for user 0 and 1 and lobby data byte 25 set (both +0x5ee cleared),
  one with byte 25 clear (only user 0 cleared), one without any net player (0x7a3400 still called for it), one for
  which 0x7a3400 returns null; prologue of 0x7a3400 wrong (never called, no user 1 reset).
- mode word 2 with party members with and without net player (kick requests on the party session, flag 0, ids from
  +0x2c, order); party backend null and backend with null function table (no call, nd[32] unchanged); mode word
  not 2 (party session not read).
- net-active byte 0 (no player loop) / non-zero: players with user number 1, with id word low bits 0, with a
  matching member (not kicked), without (kick request on the match session, id from +0x5b0, flag 1); mode 2 so the
  player loop uses the party list; empty list (every user-0 player with an id is requested).
- nd[33] + 1 per run.

`u3x_members_check`
- arg 0; not host (room id set, own id ≠ host id): nothing, no call.
- host with members: Self skipped; member whose entry exists with dropped 0 (kept); dropped non-zero (kicked);
  no entry with its connection id (kicked); entry exists but inactive with dropped 0 (kept). Order of the kick
  calls = list order (so the own-first reordering of 4.1 shows). eb[121] per kick, also with a backend whose
  function table is null.
- room id 0 (host by definition): empty list, nothing.

`u3x_players_check`
- every skip reason alone (local as first / second local pointer, not joined, leaving, id 0, connection −1).
- live entry: stamp with clock present and with clock null (0); gate closed (flag word non-zero and game object
  null, or game byte 0): `u3h_link_timeout` not called; gate open by flag word 0 and by game byte non-zero:
  called; result 0: nothing; result 1 with +0x1ec set: nothing; result 1 as host: kick with the full +0x1e4 word,
  eb[124]; as guest: lost byte set, eb[125].
- lost entry and main: gone mark (+0x5e4 = 1, +0x1e4 = 0, eb[122]), no stamp, no timeout call; lost entry not main:
  stamp only; no entry and main: gone mark; no entry not main: nothing. Main pointer null. Main pointer to another
  record with the same id / a different id.
- several players in one run (independent handling, index order of the calls).

`u3x_gt_base_write`
- out − acked = 0, 30 (open) and 31, 32 (full); acked greater than out (difference wraps: full).
- open with empty list (record becomes head and tail) and with records (linked behind the tail); slot 0 returns null
  (sequence advanced, header written with flag 1, slot store and stamp done, no slot 2 call, eb[101] unchanged).
- the stored slot index wraps (N AND 31); header sequence at N = 0x7ff → 0x800 (11 bits written = 0).
- order: header call, stamp, slot 0, slot 2 (the record already complete when slot 2 sees it).

`u3x_gt_base_header`
- d = 0 (repeat: ack bits unchanged, result 0 even with flag 1), d = 1 with flag 1 / 0, d = 31, d = 32 and 2047
  (rejected, 22 bits consumed), seq below the low bits of `in` (wrap by 2048), `in` near 2^32.
- A = acked (c = 0, no record), A = out (accepted), A = out + 1 (rejected, nothing written), ack below the low
  bits of acked (wrap by 1024).
- c = 3 with bits 0b101 and three records (acked, lost, acked in list order; counters +0xb0 / +0xac; frees in
  order); c larger than the number of records (stops at the empty list; fields still stored); c > 0 with an empty
  list.
- stream shorter than the header (missing bits read as 0).
- field values seen by the slot 3 / 4 stubs (+0x14 new, +0xc and +0x18 old).

`u3x_gt_drop_expired`
- empty queue (tail set to null); nothing expired (tail = last node); head expired; middle expired; last expired
  (tail = the node before); all expired (head null, tail null); node without event (kept, no call); reference
  count 1 (slot 1 called) and 2 (not called, count 1 left); node with and without extra block; slot 7 result with
  garbage in the upper bits and a zero low byte (kept).

`u3x_gt_has_data`
- no records, timer in the future, both queues empty → 0. Each of the two queue heads alone → 1.
- timer equal to now and below now → 1 and timer = now + 150; timer = now + 1 → not touched.
- record with only type-2 events and null events → falls through to the timer; a second record or a later node with
  an event of another type → 1 and timer untouched even when due.

## 6. Old-code peculiarities to keep (or known to be without effect)

1. `u3x_session_members` does not keep the slot order behind the own member: the displaced first member lands at the
   end (4.1). Callers that send kicks or messages in list order show this order. Keep.
2. `ec_enter`: the last loop compares against the party list when the mode word is 2 but always kicks from the match
   session; with an empty list it requests a kick for every user-0 player that has an id; it uses player +0x5b0 as
   the id while the monitor's silence kick (4.5) uses the whole word at player +0x1e4. Keep all three.
3. `ec_enter` counts nd[32] only for backend calls really made; the monitor's counters eb[121] and eb[124] count
   every attempt. Keep both.
4. `ec_enter` kept comparing all members after a match was found, and tested "id non-zero" with the full id formula
   before the user-number test. Both have no observable effect; the conditions of 4.3 step 6 are equivalent.
5. `u3x_members_check` also computed the connection state of each member (2 when the entry is active and has a link
   handle, else −1) and used it only when no entry exists, where it is always −1. No observable effect; the rule of
   4.4 is complete.
6. `u3x_players_check` repeated, after the stamp / gone mark, the tests "joined", "id non-zero" (with the id word
   just cleared, without re-testing the leaving byte), "entry exists", "lost byte 0", and later "is a local
   player". After a gone mark or a lost-entry stamp the lost-byte / entry tests always end the player's handling,
   and after a live stamp none of them can fail, so the table and the silence test of 4.5 are the complete
   behaviour.
7. `u3x_gt_base_header`: for a bit index of 32 or more (c ≥ 33) the old code shifts a 32-bit one by that amount,
   which the language leaves undefined (on x86 the index is taken modulo 32). This cannot occur while the object is
   consistent: step 3 limits A to the out sequence and 4.6 keeps (out − acked) at 31 or less, so c ≤ 31. Tests keep
   c ≤ 32; new code may treat larger indices either way.
8. `u3x_gt_base_header` had a branch for a shift of 32 or more of the ack bits; d is below 32 there, so it never
   runs.
9. `u3x_gt_base_header` leaves a stale tail pointer when the list becomes empty; `u3x_gt_base_write` copes because
   it looks at the head first. Keep (do not clear the tail).
10. `u3x_gt_base_write`: when no record can be allocated the packet number is already used up and its header says
    "reliable", but no payload follows and no record exists; the next acknowledgement then takes the wrong record.
    Keep.
11. `u3x_gt_drop_expired` writes the tail on every call, and reads the node's extra-block pointer after the event's
    release call. Keep the order of 4.8.
12. `u3x_gt_has_data` read the game time twice (compare, then store); one value.

## Clarifications

Added by the tester (2026-11-14). Facts only, confirmed by the old-versus-new test (reimpl2\G12\test_g12.cpp), where
the text above is silent or can be read two ways. A call to a read-only function of another group or of the C part
counts as behaviour: how many such calls are made, and in which order, is part of what the test compares.

1. 4.2, order of the two tests. For each record, in index order, the keys are compared first, by exactly one call
   `u3x_key_equal(record + 0x5b8, member)` (player key as first argument, member key as second). The user number is
   looked at only when that call answered non-zero. So the function makes (index of the record returned + 1) calls,
   and 10 calls when it returns null, whatever the user numbers are.
2. 4.3, code checks. `u3h_mp_code_is` is called once for image 0x74b290 (step 2) and then once per listed match
   member for image 0x7a3400: after that member's user-0 lookup and readiness reset, immediately before the call it
   guards. Each answer is used for that one member (it is not remembered: with answers pass, fail, pass the function
   is called for the first and third member). No such call for an empty list and none in steps 5 to 7. In total
   1 + (number of listed match members) calls.
3. 4.3 step 6, id test. The Enter makes no call to `u3x_net_player_id`; the test is made in place on the record's
   fields.
4. 4.3 step 6, member search. For every examined player (user number 0, id not 0) the user-0 lookup of 4.2 is made
   for EVERY member of the current list, in list order, also after one of them has returned this record (section
   6, item 4). The outcome is the same as stopping at the first match, but the `u3x_key_equal` calls for the
   remaining members are made.
5. 4.3, read moments. The mode word and the net-active byte are read once each, when their step starts. A session's
   backend pointer and that backend's function table pointer are read afresh at every kick request; a member's id
   and a player's fields are read when that member / player is handled (after the earlier game calls). nd[33] is
   incremented and nd[34] stored at the very end; nd[34] is the word found at `st` +0x1c at that moment, so a value
   a game call left there wins over the net time stored in step 1.
6. 4.4, gate. `u3x_session_host(match session)` is called first on every call, also when `arg` is 0; `arg` is tested
   after it.
7. 4.4, own member. The lookup `u3x_find_member(session, own member id)` is made (once) whenever the room id is
   non-zero, also when the list is empty (no backend). It is not made when the room id is 0.
8. 4.4 step 3, calls per member. For each listed member other than Self two calls are made, in this order:
   `u3x_connection_state(base, connection id)` and then `u3x_entry_by_id(base, connection id)`. The first one is a
   function of another group that stays: `int32_t u3x_connection_state(uintptr_t base, int32_t id)`; it answers 2
   when an entry with that id exists, is active (byte +4) and has a non-zero s32 at +0x34, else -1, and makes its own
   `u3x_entry_by_id` call. Its answer never changes the outcome (section 6, item 5). The connection id is read once
   per member; the member id is read at the moment of the kick.
9. 4.5, net time. The clock POINTER (image 0x17c3580) is read once, at the start. The time (s32 at +0xc of that
   object) is read at each stamp. A stamp made after an earlier player's `u3h_link_timeout` or `u3x_session_kick`
   call therefore carries the time as it is at that moment; a change of the pointer during the run is not seen.
10. 4.5, manager pointers. The two local-player pointers (manager +0x370, +0x378) are read afresh for each record
    when its turn comes. The main pointer (+0x380) is read afresh for each record that reaches the cases without a
    usable link (no entry, or lost byte set); it is not read for a live link. The repeated "is a local player" test
    of section 6, item 6 comes after a non-zero `u3h_link_timeout` answer and reads the two pointers again; it can
    only matter if `u3h_link_timeout` itself made that very record a local player (the real one only keeps its own
    per-entry times).
11. 4.5, ids. The old players check makes NO call to `u3x_net_player_id`: the rule of 3.1 is evaluated in place for
    the record and for the main record. The values are the ones `u3x_net_player_id` gives.
12. 4.5, order for a live link: `u3x_entry_by_id`; stamp; gate; `u3h_link_timeout`; byte +0x1ec (read after the
    timeout call); `u3x_session_host` (asked only for a player that timed out and has byte +0x1ec = 0, once per such
    player); then the word at +0x1e4 is read and `u3x_session_kick` called; then eb[124].
13. 4.6 holds as written. In addition: the function table pointer of the sub-object is read afresh for slot 0 and
    for slot 2; the list head, the tail and the tag source (+0xa0) are read after slot 0 has returned.
14. 4.7 holds as written. In addition: the function table pointer is read afresh at each slot 3 / 4 call; the
    counter at +0xb0 / +0xac is read and incremented after the slot call has returned; because the list head is
    read afresh at each step, a head changed by a slot 3 / 4 callee or by the allocator is the one taken next.
15. 4.8 holds as written. A node's next and event pointers are read before the expiry question, the event's
    reference count after it, the node's extra-block pointer after the release call; the event's function table
    pointer is read afresh for the release call. ("The node is the head" was decided by comparing the node with the
    head pointer as it is after the expiry question; that is the same as "no node was kept before it" as long as the
    expiry question does not change the head pointer.)
16. 4.9 holds as written. The addition "game time + 150" was a signed 32-bit addition: for a game time above
    2^31 - 151 ticks its result was undefined in the old code (2^31 ticks are 82 days; not tested).
17. Free hops, confirmed without effect: the member list through `u3h_members` or directly; the code check through
    the C function or through `u3h_mp_code_is`; the number of `u3h_ext` calls (a plain address getter).
18. Integration facts (v7.212 `u3x_fixes.cpp`): `GtStream` is a struct at file scope, defined before this group's
    code with the fields of 3.8, and `u3x_gt_read` is defined there as `extern "C"` with the signature of section
    2; a forward declaration `struct GtStream;` and a repeated declaration of `u3x_gt_read` after them are accepted
    by the compiler (checked with the helper's flags).
