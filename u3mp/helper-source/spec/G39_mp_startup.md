# G39. Multiplayer startup and shutdown: NetInit, level wants, leave sequence, lobby stage

Behaviour description for a reimplementation (reimpl2 pipeline, describer's document). It says what the functions
do, not how the old ones were written. Offsets are hexadecimal when prefixed with 0x, decimal otherwise. "Image
offset X" means the address `base + X`, where `base` is the game image base. All multi-byte values are little-endian;
"u8 / u16 / u32 / s32 / u64" are unsigned / signed integers of that width, "pointer" is 8 bytes. There is no
floating point in this group.

Names used for the same things in the related documents are kept: the LAN flag and the "multiplayer active" byte,
the sessions and the backend objects are in `G08_lan_room_client.md` (3.1, 4.4, 4.8) and `G13_online_backend_in.md`
(3.1, 4.3, 4.4); the registered online object in `G14_online_backend_out.md` (4.1); the net clock in
`G11_game_sockets_time.md` (3.6) and `G12_packets_monitor.md` (3.5); the leaderboard set-up in `G29_leaderboards.md`
(4.3); the friends set-up and shutdown in `G18_friends_list.md` (4.8, 4.16); the invites in `G20_invites.md`. They are referred
to, not repeated.

The group is written as **three parts**. A part reaches another part only through the exported names of section 2,
so three implementers can work at the same time.

| Part | Subject | Exported names to deliver |
|---|---|---|
| 1 | The level manager's want lists: find an entry, want an actor package, take the title scene out, the NetInit and NetShutdown level steps | `u3x_level_want_entry`, `u3x_actor_want`, `u3x_title_unwant`, `u3x_net_init_levels`, `u3x_net_shutdown_levels` |
| 2 | The lobby stage and leaving multiplayer: the stage tick, the NetShutdown body, the automatic NetShutdown, "Leave Multiplayer? Yes" | `u3x_stage_tick`, `u3x_net_shutdown_parts` (new), `u3x_net_shutdown_do`, `u3x_net_shutdown_tick`, `u3x_leave_mp_yes` |
| 3 | The multiplayer startup: begin, the per-frame startup tick with the NetInit step (allocators, net clock, backend, profile), the three net services | `u3x_begin_startup`, `u3x_startup_tick`, `u3x_netinit_services` (new) |

## 1. Purpose and callers

The PS4 game kept its multiplayer code but lost the code that starts and ends it (the PS3 "NetInit" and
"NetShutdown") and the level bookkeeping around them. The helper supplies both: when the player has picked a
profile it shows the connecting page, runs NetInit over one or more menu frames, waits for the "net" level and
opens the lobby; while multiplayer is active it keeps the lobby's 3D stage wanted, visible and shown; when the
player backs out to a page that lies before NetInit, or answers "Leave Multiplayer?" with Yes, it runs
NetShutdown, drops the stage and wants the single-player boot levels again.

Everything of this group runs on the game's main thread (menu callbacks, menu data-source events, the helper's
per-frame event). No other thread touches what it writes.

**Part 1.**

- `u3x_level_want_entry`: finds a level in the level want list. Called through the C wrapper `level_want_entry`
  (used by the C functions `stage_logout_hold`, `m01_keep`, `m01_drop`) and directly by parts 1 and 2.
- `u3x_actor_want`: wants an actor package without waiting for it. Called through the C wrapper `actor_want` (used
  by the C functions `k38_keep`, `g01_keep` and one hook branch that wants a package by name) and by part 2.
- `u3x_title_unwant` (stray): takes the title scene's two levels out of the want lists. Called by
  `u3x_net_init_levels` and by `u3x_stage_tick`; the C wrapper `title_unwant` exists, nothing calls it.
- `u3x_net_init_levels`: the level step of NetInit. Called by the NetInit step of part 3 (the C wrapper
  `net_init_levels` exists, nothing calls it).
- `u3x_net_shutdown_levels`: the level step of NetShutdown. Called by `u3x_stage_tick` (part 2) when multiplayer
  is over (the C wrapper `net_shutdown_levels` exists, nothing calls it).

**Part 2.**

- `u3x_stage_tick`: the lobby stage. Called through the C wrapper `stage_tick` and its export
  `u3h_stage_tick(base, s)`: from the helper's hook dispatcher for hook kind 3 (a menu data-source event of the
  startup pages) when the image base is known and the LAN flag byte is 0; from the player-list sources (the
  C `onm_players` and the C++ list sources of other groups) on their "last row index" request, i.e. on every draw
  of a page that lists players; and by `u3x_net_shutdown_do`.
- `u3x_net_shutdown_parts` (new entry for the C function `u3h_net_shutdown_parts`): the four service shutdowns of
  NetShutdown, in order. Called by `u3x_net_shutdown_do` only.
- `u3x_net_shutdown_do` (stray): the NetShutdown body. Called by `u3x_net_shutdown_tick`, `u3x_leave_mp_yes`, the
  NetInit step of part 3 (failed profile download) and through the C wrapper `net_shutdown_do` (no C caller left).
- `u3x_net_shutdown_tick`: NetShutdown when a front-end page before NetInit is on screen. Called through the C
  wrapper `net_shutdown_tick` from the helper's per-frame event (hook kind 16), first of that event's calls, when
  the image base is known.
- `u3x_leave_mp_yes` (stray): the "Leave Multiplayer?" Yes button. Called through the C wrapper `leave_mp_yes` from
  the hook dispatcher for hook kind 130 (a stub at the start of the game's own callback at image offset 0x66e520),
  when the image base is known. The stub returns the result to nobody; afterwards the game's own code of that
  callback runs (it opens the root page).

**Part 3.**

- `u3x_begin_startup`: starts the multiplayer startup. Called through the C wrapper `begin_startup`: from the
  dispatcher for hook kind 1 (the profile dialog's periodic event, once the dialog has finished with a profile) and
  from the C function `k01_play_online` (the "skip the profile page" shortcut, G13 2.3).
- `u3x_startup_tick`: one step of the startup per menu frame. Called through the C wrapper `startup_tick` from the
  dispatcher for hook kind 3 on that data source's periodic request (operation 6). The image base is not checked
  by that caller.
- `u3x_netinit_services` (new entry for the C function `u3h_netinit_services`): friends, leaderboards, invites, in
  this order. Called by the NetInit step only.
- The NetInit step itself, the network allocator step and the net clock step are private pieces of
  `u3x_startup_tick` (old private names `netinit_step`, `alias_net_heaps`, `set_net_clock`); they need no exported
  name.

## 2. Interface

All functions are `extern "C"`. `base` is the game image base; `s` is the helper's state object (3.4).

### 2.1 Part 1

| Function | C++ signature | Result | The C wrapper passes |
|---|---|---|---|
| find a want entry | `uint8_t *u3x_level_want_entry(uintptr_t base, uint32_t sid)` | address of the entry in the level want list, or null | base, sid (unchanged) |
| want an actor package | `int u3x_actor_want(uintptr_t base, void *s, const char *name)` | 1 = already wanted, 2 = added, 0 = cannot | base, state object, name |
| title scene out (stray) | `void u3x_title_unwant(uintptr_t base, void *s)` | none | base, state object |
| NetInit level step | `void u3x_net_init_levels(uintptr_t base, void *s)` | none | base, state object |
| NetShutdown level step | `void u3x_net_shutdown_levels(uintptr_t base, void *s)` | none | base, state object |

### 2.2 Part 2

| Function | C++ signature | Result | The C wrapper passes |
|---|---|---|---|
| stage tick | `void u3x_stage_tick(uintptr_t base, void *s)` | none | base, state object |
| shutdown parts (new) | `void u3x_net_shutdown_parts(uintptr_t base, void *s)` | none | `u3h_net_shutdown_parts(base, s)` becomes "call `u3x_net_shutdown_parts(base, s)`" |
| NetShutdown body (stray) | `void u3x_net_shutdown_do(uintptr_t base, void *s)` | none | base, state object |
| automatic NetShutdown | `void u3x_net_shutdown_tick(uintptr_t base, void *s)` | none | base, state object |
| Leave Multiplayer Yes (stray) | `int u3x_leave_mp_yes(uintptr_t base, void *s)` | always 0 | base, state object |

`u3x_net_shutdown_do` may call `u3x_net_shutdown_parts` directly or through the kept export; the effects are the
same.

### 2.3 Part 3

| Function | C++ signature | Result | The C wrapper passes |
|---|---|---|---|
| begin | `void u3x_begin_startup(uintptr_t base, void *s)` | none | base, state object |
| startup tick | `void u3x_startup_tick(uintptr_t base, void *s)` | none | base, state object |
| net services (new) | `void u3x_netinit_services(uintptr_t base, void *s)` | none | `u3h_netinit_services(base, s)` becomes "call `u3x_netinit_services(base, s)`" |

### 2.4 Functions of other groups, of the C part and of the shared tools that this group calls

Not to be described or reimplemented here. The tools of `CONVENTIONS.h` (`rd`, `at`, `ptr_ok`, `u3h_ext`,
`ext_eb`) are used as given there. Below, a "plausible pointer" is exactly `ptr_ok`: value at least 0x10000, below
2^47 and a multiple of 8.

| Name | Signature | What it does |
|---|---|---|
| `u3h_crc_sid` | `uint32_t u3h_crc_sid(const char *text)` | the game's 32-bit name id of a NUL-terminated text (a CRC-32 with polynomial 0x04C11DB7, most significant bit first, start value 0, no final inversion) |
| `u3h_stage_unwant` | `int u3h_stage_unwant(uintptr_t base, uint32_t sid)` | removes every entry with this sid from the level want list and from the actor want list (3.2), closing the gaps; result = number of entries removed |
| `u3h_multi_all` | `void u3h_multi_all(uintptr_t base, void *s, int keep)` | `keep` non-zero: keeps the packages that PS3's "multi-all" level carried wanted; 0: lets them go |
| `u3h_stage_logout_hold` | `int u3h_stage_logout_hold(uintptr_t base, void *s, const char *stage)` | non-zero = the game itself has just logged the stage out: leave the stage alone in this call |
| `u3h_match_stage_cleanup` | `void u3h_match_stage_cleanup(uintptr_t base, void *s, void *find, void *show)` | in a match: hides and unwants the lobby stages of other maps; `find` and `show` are natives 42 and 45 (3.5) as plain pointers |
| `u3h_onb_mgr` | `void *u3h_onb_mgr(uintptr_t base)` | the online backend object while it is the registered session manager, else null (G14 4.1) |
| `u3x_mp_lan_or_online` | `int u3x_mp_lan_or_online(uintptr_t base)` | exists (a one-line function, stays): non-zero when the LAN flag byte (image offset 0x17d5c60) is non-zero or, only if that byte is 0, `u3h_onb_mgr(base)` is not null |
| `u3x_level_state` | `uint32_t u3x_level_state(uintptr_t base, void *s, uint32_t sid)` | exists (stays): when native 42 is not null it is called with (level manager, sid); if the answer is a plausible pointer the result is the low 8 bits of the level's state (3.2), otherwise (or without native 42) 0xff |
| `u3h_active_page` | `uint32_t u3h_active_page(uintptr_t base)` | id of the menu page on top of the page stack; 0 while the menu changes pages or has none |
| `u3x_emblem_shutdown` | `void u3x_emblem_shutdown(uintptr_t base)` | the emblem group's NetShutdown (C wrapper `emb_shutdown119`) |
| `u3x_lan_shutdown` | `void u3x_lan_shutdown(uintptr_t base)` | G08 4.8 (C wrapper `lnb_shutdown`) |
| `u3x_friend_shutdown` | `void u3x_friend_shutdown(uintptr_t base, void *s)` | G18 (C wrapper `fr_shutdown`) |
| `u3x_lobby_scene_shutdown` | `void u3x_lobby_scene_shutdown(uintptr_t base)` | exists (stays): leaves the lobby's menu scene (C wrapper `k34_shutdown`) |
| `u3h_register_lobby_click` | `int u3h_register_lobby_click(uintptr_t base)` | adds the lobby list's click callback to the game's callback table when it is missing; result unused here |
| `u3h_show` | `int u3h_show(uintptr_t base, void *s, uint32_t page, int silent)` | opens a menu page through the state object's show-page function; result = the low 8 bits of that function's answer, non-zero = the page is shown |
| `u3h_k01_back_page` | `uint32_t u3h_k01_back_page(void)` | id of the menu page the startup goes back to (G13 2.3) |
| `u3h_k43_step` | `void u3h_k43_step(uintptr_t base, uint32_t v)` | sets the progress step shown on the connecting page (G13 2.3) |
| `u3h_k43_gate` | `int u3h_k43_gate(uintptr_t base, void *s)` | non-zero = the "net" level is still loading: stay on the connecting page |
| `u3h_onb_signin_wait` | `int u3h_onb_signin_wait(uintptr_t base, void *s)` | G13 4.3 (`u3x_signin_wait`): 1 = still waiting, 0 = go on, -1 = failed (dialog shown, startup phase set to 0) |
| `u3h_onb_lan_flag` | `int u3h_onb_lan_flag(void)` | 1 = this multiplayer session uses the LAN backend (LAN or offline mode), 0 = the online backend |
| `u3h_ngm_init` | `void u3h_ngm_init(uintptr_t base)` | sets the net game manager's match defaults |
| `u3h_onp_name_apply` | `void u3h_onp_name_apply(uintptr_t base)` | G13 2.3: online only, writes the account's name over the own network name |
| `u3h_cin_netinit` | `void u3h_cin_netinit(uintptr_t base, void *s)` | the cinema group's NetInit (film lists, directories, buffers) |
| `u3x_friend_init` | `void u3x_friend_init(uintptr_t base)` | G18: the friends set-up (C wrapper `fr_init`) |
| `u3x_lb_init` | `void u3x_lb_init(uintptr_t base, void *s)` | G29 4.3: the leaderboard set-up (C wrapper `lb_init119`) |
| `u3h_fi_netinit` | `void u3h_fi_netinit(uintptr_t base, void *s)` | the invites' net start (C function `fi_netinit`, which stays, G20): nothing on LAN or with friends switched off; otherwise forgets both request tickets and asks for a new list with notification. **New export to add** (one line around the C function) |
| `u3h_lnb_netinit` | `void u3h_lnb_netinit(uintptr_t base, void *s)` | G08 4.4: the LAN branch of NetInit |
| `u3h_onb_netinit` | `int u3h_onb_netinit(uintptr_t base, void *s)` | G13 4.4: the online branch of NetInit; result unused here |
| `u3h_mp_profile_start` | `void u3h_mp_profile_start(uintptr_t base, void *s)` | loads the selected multiplayer profile (LAN: the slot file; online: the downloaded record) into the game's profile object |
| `u3h_onp_fail_take` | `int u3h_onp_fail_take(void)` | non-zero once when the online profile could not be downloaded; reading it clears the mark |
| `u3h_so_note` | `void u3h_so_note(const char *a, const char *name, int32_t v, const char *b)` | one line for the session log (G13 2.3) |
| `u3x_online_failed` | `void u3x_online_failed(uintptr_t base, void *s)` | exists (stays): the "network failure" dialog (G13 2.3) |
| `u3h_emb_netinit` | `int u3h_emb_netinit(uintptr_t base, void *s)` | the emblem group's NetInit; non-zero = it has set the emblem tables up itself |

Three functions of the state object (3.4, pointers at 656, 664, 672) and ten "natives" (3.5) are called through
their pointers.

### 2.5 Strays (old functions in no work group that this document covers or points at)

- **Covered here, to deliver:** `u3x_title_unwant` (4.3); `u3x_net_shutdown_do` (5.3; the inventory has it in its
  list of one-line functions, but it holds the order of the NetShutdown); `u3x_leave_mp_yes` (5.5); the net clock
  step of NetInit (6.5, old private `set_net_clock`); the private tests "name pointer is usable" (4.4), "shutdown
  is blocked" (5.4) and the net symbol lookup (6.2 step 3).
- **Not covered, stay as they are, behaviour given in 2.4:** `u3x_level_state`, `u3x_mp_lan_or_online`,
  `u3x_lobby_scene_shutdown`, `u3x_online_failed`.
- **C export to add (integrator):** `u3h_fi_netinit(uintptr_t base, void *s)`.

## 3. Game and helper facts

### 3.1 Game data

| Image offset | Type | Meaning |
|---|---|---|
| 0x16e7170 | u8 | the game's "abort" flag |
| 0x17b2a94 | u8 | "give all store items" |
| 0x17b4b98 | 0x40 bytes | engine clock 3 (the local clock); its time is the 8 bytes at +0xc |
| 0x17b4c98 | 0x40 bytes | engine clock 7 |
| 0x17bba90 | object | the menu scene (argument of natives 46 and 47) |
| 0x17bd080 + 8 | u32 | the menu object's UI state; 7 and 8 = the menu is changing pages |
| 0x17bde75 | u8 | non-zero = the menu acts for the second local user |
| 0x17bf714 | u32 | index of the menu's local user |
| 0x17c3440 | object | the emblem state; its first byte is set to 1 before native 11 |
| 0x17c3580 | pointer | the net clock object (G11, G12) |
| 0x17c3b40 | object | the multiplayer character manager; s32 at +0x28, u32 at +0x2c, u64 at +0x30 form the handle of its script data: +0x2c the symbol id, +0x30 the cached pointer, +0x28 the cached index |
| 0x17cb060 | object | the net game manager |
| 0x17d1868 | u32 | the net game manager's match state (manager + 0x6808); 2 = in a match |
| 0x17d2518 | text | the lobby stage's level name (manager + 0x74b8), NUL-terminated; empty = none named |
| 0x17d5c60 | u8 | the LAN flag (G08 3.1) |
| 0x17d5c61 | u8 | "multiplayer active" (G08 3.1) |
| 0x17d6010 | text | the player's own network name (G13 3.1) |
| 0x17d6420 | object | the match session |
| 0x17dd610 | pointer | the array of the local users' multiplayer profile objects (0x7b00 bytes each), or null |
| 0x18417b0 | 64 pointers | the game's allocator table; entries 0x14 and 0x15 are the network allocators (G11 3.6 reads entry 0x14) |
| 0x1afcf10 | object | the level manager (3.2) |

The profile object of the menu's user: none when the pointer at 0x17dd610 is null; otherwise user = 1 when the u8
at 0x17bde75 is non-zero, else the u32 at 0x17bf714; none when user is 4 or more; else the array pointer plus
user x 0x7b00. Its four bytes at +0xac0 are the clan tag.

### 3.2 The level manager, its lists, a request, a level

Level manager fields (image offset 0x1afcf10 + ...):

| Offset | Type | Meaning |
|---|---|---|
| 0x28 | u32 | number of entries of the level want list |
| 0x30 | pointer | its entries |
| 0x38 | u32 | number of entries of the actor want list |
| 0x40 | pointer | its entries |
| 0x48 | u32 | number of entries of the third list (requests the manager has not taken over yet) |
| 0x4c | u32 | capacity of the third list |
| 0x50 | pointer | its entries |

An entry of any of the three lists, and a request block handed to native 41, is 0x18 bytes:

| Offset | Type | Meaning |
|---|---|---|
| 0 | u64 | the name's 64-bit hash (native 40 of the name) |
| 8 | u32 | the name's sid (`u3h_crc_sid` of the name) |
| 0xc | u16 | a small number the boot code sets to 4 for its levels, 0 otherwise |
| 0xe | u8 | flags. Values used here: 3 (bits 0 and 1) = a level that is wanted and shown, as the game's own lobby stage loader and the boot code request it; 1 = an actor package wanted in the foreground; 0x0a = what the game uses for its own menu level |
| 0xf, 0x10..0x17 | - | zero |

A level object (answer of native 42): u16 at +2 = flags (the helper sets bit 4, value 0x10, in the byte at +2 after
showing a level); u32 at +0x30 = state, 8 = loaded.

### 3.3 Script data

- **The boot level list** (the single-player world levels that the boot code wants): value of script symbol
  0x1020810b, obtained with native 13. Layout: s32 count at +0; pointer at +8 to `count` records of 0x10 bytes; a
  record's first 8 bytes are a pointer to the level's NUL-terminated name. The game owns this list; it is read at
  run time on every call (never copied).
- **The net script module**: symbol 0xbd3bb3b9 exists once the game's net script file is loaded. The state
  object's lookup function (3.4) answers a pointer to a pointer; the symbol counts as present when neither is null.

### 3.4 The helper's state object `s`

| Offset | Type | Meaning |
|---|---|---|
| 76 | u32 | phase of the profile dialog |
| 128 | u32 | number of saved profiles |
| 132 | u32 | the selected profile, 1-based; 0 = none |
| 136 + 64·k | 64 bytes of text | name of profile k (k = 0 is the first) |
| 648 | pointer | the show-page function; null = menu pages cannot be opened |
| 656 | pointer to function | script symbol lookup: (u32 symbol id), result a pointer to a pointer to the symbol's data, or null |
| 664 | pointer to function | script module loader: (u32 module name id, pointer to a u32 that holds the load tag); result unused |
| 672 | pointer to function | net pool set-up: (image base), result int, 0 = failed |
| 680 | u32 | the startup phase: 0 idle, 1 NetInit pending, 2 waiting for the "net" level, 3 lobby open, 4 stopped with an error |
| 684 | u32 | the startup error: 0 none, 1 no connecting page, 2 no net script module, 3 no pools, 4 no lobby page |
| 688 | u32 | startup ticks |
| 768 + 8·k | pointer | native k (3.5) |
| 1280 | u32 | probe block: marker |
| 1284 | u32 | probe block: run mark |
| 1472, 1476 | u32, u32 | probe block: allocator mask, low and high word (bit i = table entry i is not null) |
| 1480 | u32 | probe block: index of the allocator that became network allocator 0x14 |
| 0x2000 | 0x40 bytes | the helper's own net clock object |
| 0x20c0 | u32 | switch word (set by the builder): bit 3 (value 8) = do not fill the network allocators; bit 8 (value 0x100) = NetShutdown switched off |
| 0x20c4 | u32 | NetShutdowns run |
| 0x26c0 | u64, u64 | title camera hand-back: [0] the recorded title state object, [1] non-zero = hand the camera back |
| 0x3d78 | u32 | second switch word (set by the builder): bit 22 (0x400000) = no stage cleanup in a match; bit 30 (0x40000000) = no game emblem init; bit 31 (0x80000000) = use the helper's own net clock |
| 0x3e10 | six u32 | stage words ST[0..5]: ST[0] bits: bit 0 = the helper named the default stage, bit 16 (0x10000) = the multiplayer scene was entered, bit 17 (0x20000) = the proto actor package was wanted; ST[1] = sid of the stage that is shown; ST[4] = the stage level's flags (low 16 bits) and state (shifted left 16), 0xffffffff = no level; ST[5] = full stage ticks. ST[2], ST[3] are not used here |

### 3.5 Natives (pointers at `s` + 768 + 8·k; any may be null)

| k | Arguments | Result | Meaning (game function it points to) |
|---|---|---|---|
| 11 | (pointer: emblem state, image offset 0x17c3440) | - | resets the emblem state and fills the emblem table (0x66b510) |
| 12 | (pointer: match session, image offset 0x17d6420) | ignored | the game's session initialisation (0x7a2360) |
| 13 | (u32 symbol id) | pointer to the symbol's value, or null | script symbol value (0x11f2e30) |
| 40 | (pointer to a NUL-terminated name) | u64 | the 64-bit name hash of level requests (0x10e95b0) |
| 41 | (pointer: level manager, pointer to a request block) | - | requests a level (0x10eb990) |
| 42 | (pointer: level manager, u32 sid) | pointer to the level object, or null | finds a level (0x10eb1d0) |
| 45 | (pointer: level object, int 1) | - | shows a level (0x10f25b0) |
| 46 | (pointer: menu scene, image offset 0x17bba90) | - | enters the multiplayer menu scene (0x5b7020) |
| 47 | (pointer: menu scene) | - | runs the lobby camera at the stage's menu spawner (0x5b4e60) |
| 52 | (u64: the recorded title state object) | - | the title state's enter: gives the title its camera (0x5752a0) |

All are plain calls with integer or pointer arguments (no float registers). "Level manager" is the address of image
offset 0x1afcf10.

### 3.6 The helper's state block (`u3h_ext()`)

| Where | Meaning |
|---|---|
| LOB = sixteen u32 at block + 0x1ff0 | [0] NetInit level steps; [1] title level entries removed; [2] size of the boot list, 0xffffffff = no list; [3..10] sids of its first eight names; [11] bit i = boot list name i was in the level want list at NetInit; [12] stage entries made visible; [13] the stage entry's flags byte, 0xff = no entry; [14] stage entries removed when multiplayer ended; [15] state of the first title level, plus the state of the second shifted left 8 (0xff = no level) |
| LOB2 = sixteen u32 at block + 0x2030 | [0] boot level entries removed at NetInit; [1] boot levels wanted again; [2] switches (default 0): bit 0 = keep the boot levels wanted, bit 1 = also remove the single-player proto package; [3] proto package entries removed; [4] 1 = NetInit removed boot levels and NetShutdown has not wanted them again yet |
| eb[1] bit 3 (value 8), `ext_eb()` | the per-frame lobby scene block of another group is switched off: the stage tick runs the lobby camera itself |
| eb[150] | online startup state (G13 3.3); this group writes 8 = the profile download failed |
| ec[101] = u32 at block + 0x4450 + 4·101 | the profile download's state, left there by the profile load; only printed |

### 3.7 Ids and names, and where the game holds them

**The title scene's two levels** (a list of two level ids): the game holds their names as two NUL-terminated texts
in its read-only data, one right after the other: the first at image offset 0x145ba00 (12 characters), the second
at image offset 0x145ba0d (17 characters). The sid of each is `u3h_crc_sid` of that text. The new code takes the
two sids that way at run time and keeps no copy of names or sids. (Cross-check for the tester: the game's title
task loads the same two sids as 32-bit immediates of two "load constant into a register" instructions; the u32 at
image offset 0x575718 is the first level's sid, the u32 at image offset 0x575728 the second's.) "First title level"
and "second title level" below mean this order.

Single names and ids (interoperability facts, each used at one place):

| What | Value | Note |
|---|---|---|
| boot level list symbol | 0x1020810b | 3.3 |
| net script symbol | 0xbd3bb3b9 | 3.3 |
| the net module / the "net" level | name `net`, sid 0x4240ef2e = `u3h_crc_sid("net")` | the same id names the script module for the loader and the level |
| load tag of the net module | 0x12 | passed to the loader through a pointer |
| multiplayer character list symbol | 0x916e03b0 | written into the character manager's handle |
| single-player proto package | sid 0xe0fdf8f6 (= `u3h_crc_sid` of the text at image offset 0x145b905) | only removed when LOB2[2] bit 1 is set |
| multiplayer proto actor package | name `anim-proto-multiplayer` | the game holds the same text at image offset 0x145f915 |
| default lobby stage | name `san-upper-multi-stage` | the helper's own fallback choice (the same text is the C part's stage-guard fallback); see open question 1 |
| connecting page | 0x9e1863b1 | |
| lobby page | 0x53cbb5f2 | |
| pages that lie before NetInit | 0xd45dfcca (root tiles), 0x37656388 (root main / title), 0x1e66a744 (multiplayer choice), 0xb0e67b0e (co-op choice) | 5.4 |
| probe marker | 0x424f5250 | the characters `PROB` |

### 3.8 The helper's own text

N-fail, for the session log (no NUL inside, one trailing blank):

`Play Online stopped (No connection found): the online profile could not be downloaded, state `

### 3.9 File-private variables of the new code

Part 2 keeps two u32 counters (zero at start; diagnostics only, nothing reads them): Leave-Yes calls, and
NetShutdowns run from the Leave-Yes button.

## 4. Behaviour, part 1

Rule for all parts: where a condition names several tests, they are evaluated from left to right and evaluation
stops as soon as the outcome is known; a function is called again each time it is named.

### 4.1 `u3x_level_want_entry(base, sid)`

Reads the level want list's count and entries pointer (3.2). Null when the entries pointer is not a plausible
pointer or the count is above 4096. Otherwise the address of the first entry, in list order, whose sid (u32 at +8)
equals `sid`; null when there is none. Only the level want list is searched. Nothing is written, nothing is called.

### 4.2 `u3x_actor_want(base, s, name)`

1. If native 40 is null: result 0, nothing else.
2. S = `u3h_crc_sid(name)`.
3. Look at the level want list, then at the actor want list. For each: if its entries pointer is not a plausible
   pointer or its count is above 4096, the result is 0 at once (also when this happens for the first list); if an
   entry of it has the sid S, the result is 1 at once.
4. The third list: read its count, capacity and entries pointer. Result 0 when the pointer is not a plausible
   pointer, or the count is not below the capacity, or the capacity is above 4096.
5. Otherwise the entry at index "count" becomes: all 0x18 bytes zero, then hash = native 40(name), sid = S, flags
   byte = 1. The count is raised by 1. Result 2.

The third list itself is not searched for S.

### 4.3 `u3x_title_unwant(base, s)`

1. For the first title level, then for the second (3.7): if `u3x_level_want_entry(base, sid)` is not null, LOB[1]
   grows by the result of `u3h_stage_unwant(base, sid)`.
2. LOB[15] = `u3x_level_state(base, s, first sid)` + 256 x `u3x_level_state(base, s, second sid)`, the first call
   made first.

Safe to repeat: without entries only LOB[15] is refreshed.

### 4.4 `u3x_net_init_levels(base, s)`

A "usable name pointer" is a pointer value of at least 0x10000 and below 2^47 (no alignment condition).

1. LOB[0] plus 1.
2. If native 13 is not null, T = native 13(0x1020810b); else T = null. The list is **valid** when T is a plausible
   pointer and its count (s32 at +0) is from 0 to 256.
3. Not valid: LOB[2] = 0xffffffff; LOB[3..11] keep their values.
4. Valid: LOB[2] = the count; LOB[11] = 0; E = the records pointer (T + 8). If E is a plausible pointer, for
   i = 0, 1, ... while i is below the count and below 32, with the name pointer of record i:
   - not a usable name pointer: this i is skipped entirely;
   - S = `u3h_crc_sid(name)`;
   - when i is below 8: LOB[3 + i] = S;
   - when `u3x_level_want_entry(base, S)` is not null: bit i of LOB[11] is set;
   - when LOB2[2] bit 0 is clear: R = `u3h_stage_unwant(base, S)`; LOB2[0] grows by R; when R is not 0, LOB2[4] = 1.
5. When LOB2[2] bit 1 is set: LOB2[3] grows by `u3h_stage_unwant(base, 0xe0fdf8f6)`.
6. `u3x_title_unwant(base, s)`.

### 4.5 `u3x_net_shutdown_levels(base, s)`

1. If LOB2[4] is 0: nothing. Otherwise LOB2[4] = 0 (before anything else).
2. If native 13 is not null, T = native 13(0x1020810b) (called even when natives 40 or 41 are missing); else null.
3. Nothing more when native 40 is null, or native 41 is null, or the list is not valid (4.4 step 2).
4. E = the records pointer. If E is a plausible pointer, for i = 0, 1, ... while i is below the count and below 32:
   - skip a record whose name pointer is not usable;
   - S = `u3h_crc_sid(name)`; skip when `u3x_level_want_entry(base, S)` is not null (already wanted);
   - otherwise build a request block (3.2) on the stack: all zero, hash = native 40(name), sid = S, the u16 at
     0xc = 4, flags = 3; call native 41(level manager, the block); LOB2[1] plus 1.

## 5. Behaviour, part 2

### 5.1 `u3x_stage_tick(base, s)`

"Stage name" is the text at image offset 0x17d2518; ST are the stage words (3.4).

**A. Multiplayer is over** (`u3x_mp_lan_or_online(base)` answers 0):

1. If ST[0] is not 0:
   - if the stage name is not empty: LOB[14] grows by `u3h_stage_unwant(base, u3h_crc_sid(stage name))`; then the
     stage name's first byte = 0;
   - `u3h_multi_all(base, s, 0)`;
   - `u3x_net_shutdown_levels(base, s)`;
   - ST[0] = 0 and ST[1] = 0.
2. The title camera: if the second u64 at `s` + 0x26c0 is not 0, and the first is not 0, and native 52 is not null:
   the second u64 = 0, then native 52(the first u64).
3. End.

**B. Multiplayer is active:**

1. If native 42, 40, 41 or 45 is null: nothing (not even step 2).
2. `u3h_multi_all(base, s, 1)`.
3. If `u3h_stage_logout_hold(base, s, stage name)` answers non-zero: end.
4. If the match state (u32 at image offset 0x17d1868) is 2: unless bit 22 of the second switch word (`s` + 0x3d78)
   is set, `u3h_match_stage_cleanup(base, s, native 42, native 45)`. End.
5. If ST[0] bit 16 is clear and native 46 is not null: native 46(menu scene); set bit 16. (Without native 46 the bit
   stays clear.)
6. If ST[0] bit 17 is clear: `u3x_actor_want(base, s, "anim-proto-multiplayer")` (result unused); set bit 17.
7. `u3x_title_unwant(base, s)`.
8. If the stage name is empty: write the default stage name (3.7) with its NUL there; set ST[0] bit 0.
9. S = `u3h_crc_sid(stage name)`; ENT = `u3x_level_want_entry(base, S)`. If ENT is null: build a request block: all
   zero, hash = native 40(stage name), sid = S, flags = 3 (the u16 at 0xc stays 0); native 41(level manager, the
   block); ENT = `u3x_level_want_entry(base, S)` again.
10. If ENT is not null and its flags byte does not have both bit 0 and bit 1: set both; LOB[12] plus 1.
11. LOB[13] = ENT's flags byte, or 0xff without ENT.
12. L = native 42(level manager, S). (Only null is tested, not plausibility.)
13. If L is not null, its state is 8 and ST[1] is not S: native 45(L, 1); then set bit 4 (value 0x10) in the byte
    at L + 2.
14. ST[4] = the bitwise OR of the u16 at L + 2 and the state shifted left by 16 (a 32-bit result), read now (after step 13); or
    0xffffffff when L is null. ST[5] plus 1.
15. If L is not null, its state (read again) is 8 and native 47 is not null: when eb[1] bit 3 is set, native
    47(menu scene); in both cases ST[1] = S.

### 5.2 `u3x_net_shutdown_parts(base, s)` (old `u3h_net_shutdown_parts`)

In this order: `u3x_emblem_shutdown(base)`, `u3x_lan_shutdown(base)`, `u3x_friend_shutdown(base, s)`,
`u3x_lobby_scene_shutdown(base)`. Nothing else.

### 5.3 `u3x_net_shutdown_do(base, s)`

In this order: `u3x_net_shutdown_parts(base, s)`; the "multiplayer active" byte (image offset 0x17d5c61) = 0; the
LAN flag (image offset 0x17d5c60) = 0; the startup phase (`s` + 680) = 0; the u32 at `s` + 0x20c4 plus 1;
`u3x_stage_tick(base, s)` (the pages on which this runs have no player list, so nothing else would run it).

### 5.4 `u3x_net_shutdown_tick(base, s)`

NetShutdown is **blocked** when, tested in this order: bit 8 of the switch word (`s` + 0x20c0) is set; or the
startup phase is 1; or it is 2; or the match state (image offset 0x17d1868) is 2.

1. Nothing when `base` is 0, or `u3x_mp_lan_or_online(base)` answers 0, or NetShutdown is blocked (tested in this
   order).
2. P = `u3h_active_page(base)`. Nothing unless P is one of the four pages that lie before NetInit (3.7).
3. `u3x_net_shutdown_do(base, s)`.

### 5.5 `u3x_leave_mp_yes(base, s)`

1. The first private counter plus 1.
2. If `u3x_mp_lan_or_online(base)` answers 0, or NetShutdown is blocked (5.4): result 0.
3. `u3x_net_shutdown_do(base, s)`; the second private counter plus 1; result 0.

## 6. Behaviour, part 3

### 6.1 `u3x_begin_startup(base, s)`

1. Nothing when the show-page function (`s` + 648) is null, or the startup phase is not 0, or the selected profile
   (`s` + 132) is 0 or above the profile count (`s` + 128).
2. `u3h_register_lobby_click(base)` (result unused).
3. Startup phase = 1; startup ticks = 0; startup error = 0.
4. `u3h_k43_step(base, 0)`.
5. If `u3h_show(base, s, 0x9e1863b1, 0)` answers 0: startup phase = 0, startup error = 1.

### 6.2 `u3x_startup_tick(base, s)`

1. Nothing when the startup phase is 0, or 3 or more.
2. Nothing when the menu's UI state (u32 at image offset 0x17bd080 + 8) is 7 or 8.
3. Startup ticks plus 1.
4. Phase 1: the NetInit step (6.3); end.
5. Phase 2: if `u3h_k43_gate(base, s)` answers non-zero: end. Otherwise startup phase = 3; the profile dialog phase
   (u32 at `s` + 76) = 0; then `u3h_show(base, s, u3h_k01_back_page(), 1)` and, only if that answered non-zero,
   `u3h_show(base, s, 0x53cbb5f2, 0)`. If either answered 0: startup error = 4, startup phase = 4.

### 6.3 The NetInit step (phase 1)

Each numbered item happens in this order; "stop" ends the step for this frame.

1. If `u3h_onb_signin_wait(base, s)` answers non-zero: stop (it is asked again next frame, or it has ended the
   startup itself).
2. `u3h_k43_step(base, 0)`.
3. **The net script module.** Call the lookup function (`s` + 656; not tested for null) with 0xbd3bb3b9. If the
   symbol is not present (3.3): put the load tag 0x12 into a u32 variable, call the loader (`s` + 664) with
   (0x4240ef2e, address of that variable), and look the symbol up again. If it is still not present: startup
   error = 2, startup phase = 4, stop.
4. `u3h_k43_step(base, 5)`.
5. **Pools.** Call the pool set-up function (`s` + 672) with `base`. If it answers 0: startup error = 3, startup
   phase = 4, stop.
6. **Flags.** The LAN flag (u8 at image offset 0x17d5c60) = the low 8 bits of `u3h_onb_lan_flag()`; "multiplayer
   active" (image offset 0x17d5c61) = 1; "give all store items" (image offset 0x17b2a94) = 1.
7. **The "net" level.** If natives 42, 40 and 41 are all not null and native 42(level manager, 0x4240ef2e) answers
   null: build a request block: all zero, hash = native 40("net"), sid = 0x4240ef2e, flags = 0x0a; native 41(level
   manager, the block).
8. `u3h_ngm_init(base)`.
9. `u3x_net_init_levels(base, s)` (4.4).
10. **Name.** If the selected profile is not 0 and not above the profile count: copy that profile's name (3.4) to
    the own network name at image offset 0x17d6010: its characters up to the first NUL, at most 16, then one NUL.
    Bytes behind that NUL keep their old content.
11. `u3h_onp_name_apply(base)`.
12. `u3h_cin_netinit(base, s)`.
13. `u3x_netinit_services(base, s)` (6.6).
14. **Clan tag.** If the menu user's profile object exists (3.1): each of its four clan tag bytes that is a
    backslash (0x5c) becomes a blank (0x20).
15. **Network allocators.** Unless bit 3 of the switch word (`s` + 0x20c0) is set: 6.4.
16. **Net clock.** 6.5.
17. The abort flag (u8 at image offset 0x16e7170) = 0.
18. If native 12 is not null: native 12(match session). Result unused.
19. **Character list handle.** If the u32 at image offset 0x17c3b40 + 0x2c is 0: it becomes 0x916e03b0, the u64 at
    +0x30 becomes 0 and the s32 at +0x28 becomes -1 (in this order).
20. `u3h_k43_step(base, 7)`.
21. **Backend.** If `u3h_onb_lan_flag()` (asked again) is non-zero: `u3h_lnb_netinit(base, s)`; otherwise
    `u3h_onb_netinit(base, s)` (result unused).
22. `u3h_k43_step(base, 43)`.
23. `u3h_mp_profile_start(base, s)`.
24. **Failed profile download.** If `u3h_onp_fail_take()` answers non-zero: eb[150] = 8;
    `u3h_so_note(N-fail, null, ec[101] as s32, null)`; `u3x_net_shutdown_do(base, s)` (5.3; it leaves the startup
    phase 0); if the show-page function is not null, `u3h_show(base, s, u3h_k01_back_page(), 1)`;
    `u3x_online_failed(base, s)`; stop.
25. **Emblems.** If `u3h_emb_netinit(base, s)` answers 0 and bit 30 of the second switch word (`s` + 0x3d78) is
    clear and native 11 is not null: the u8 at image offset 0x17c3440 = 1, then native 11(emblem state).
    (`u3h_emb_netinit` is always called.)
26. `u3h_k43_step(base, 44)`.
27. Startup phase = 2.

### 6.4 The network allocator step

The PS4 game never registers its two network allocators; they are made to share a general one. T = the allocator
table (64 pointers at image offset 0x18417b0); the probe words are those of 3.4.

1. Both mask words = 0; marker = 0x424f5250; run mark = 1.
2. For every table index i from 0 to 63 whose pointer is not null: set bit (i modulo 32) of the low mask word
   (i below 32) or of the high one.
3. Take the first of the table entries 9, 5, 7, 0x13 (tried in this order) that is not null; if none, nothing more.
   With that entry P at index K: if entry 0x14 is null, entry 0x14 = P and the probe's allocator index (`s` + 1480)
   = K; then, if entry 0x15 is null, entry 0x15 = P. No other candidate is looked at.

The mask shows the table before step 3.

### 6.5 The net clock step

- Bit 31 of the second switch word (`s` + 0x3d78) clear: the net clock pointer (image offset 0x17c3580) = the
  address of engine clock 7 (image offset 0x17b4c98). Nothing else.
- Set: the 0x40 bytes at `s` + 0x2000 become a copy of engine clock 3 (image offset 0x17b4b98) in which these
  fields are zero: the 8 bytes at +0xc, the u32 at +0x18, the u16 at +0x1c, the four u32 at +0x20, +0x24, +0x28,
  +0x2c. Then the net clock pointer = `s` + 0x2000.

### 6.6 `u3x_netinit_services(base, s)` (old `u3h_netinit_services`)

In this order: `u3x_friend_init(base)`, `u3x_lb_init(base, s)`, `u3h_fi_netinit(base, s)`. Nothing else.

## 7. Test scenarios

Everything can be tested without the game with call-recording stand-ins: a fake image in which the offsets of 3.1
and 3.2 are memory (the three lists with room for a few entries, the two title names at image offsets 0x145ba00 and
0x145ba0d, the stage name field, the allocator table, the two engine clocks, the menu's UI state), a fake state
object whose natives and three functions are recording stubs that answer from a script, a fake state block for
`u3h_ext`, and recording stand-ins for every function of 2.4. Compare the image, the state object, the state
block, results, and the order and arguments of the recorded calls. For the old code the fake image needs nothing
at 0x145ba00 (it carries the two sids); the tester checks once that `u3h_crc_sid` of the two names equals the two
immediates named in 3.7. Only the look (stage visible, camera, levels really streaming, a real sign-in) needs the
live game.

Part 1:

1. Want entry: entries pointer null / 0x1000 / odd / fine; count 0, 3, 4096, 4097; the sid in the first, the last
   and in two entries (the first is returned); a sid that is only in the actor list (null).
2. Actor want: native 40 null (0, no call); the sid in the level list, in the actor list (1); level list pointer
   bad (0 although the third list is fine); actor list count 4097 (0); third list with room (2: entry content, the
   0x18 bytes that were 0xff before, count plus 1); count equal to capacity, capacity 4097, pointer bad (0, nothing
   written); the same name twice in a row with the manager not taking the third list over (two entries).
3. Title unwant: neither level wanted (no unwant call, LOB[15] from two state calls in order); only the second
   wanted; both wanted with unwant answers 1 and 2 (LOB[1] plus 3); native 42 null (LOB[15] = 0xffff).
4. NetInit levels: native 13 null; T null; T odd; count -1, 0, 3, 8, 9, 33, 256, 257; records pointer bad (LOB[2]
   and LOB[11] set, no unwant); a null and a low name pointer in the middle (skipped, its LOB[3 + i] unchanged, its
   bit clear); an unaligned but usable name pointer (taken); names partly in the want list (LOB[11]); LOB2[2] = 0,
   1, 2, 3 (boot unwants or not, proto unwant or not; LOB2[4] only when an unwant removed something); order of
   the recorded calls; the title step last.
5. NetShutdown levels: LOB2[4] = 0 (no call at all); LOB2[4] = 1 with native 40 null (LOB2[4] cleared, lookup
   still called, nothing wanted) and with native 13 null; a list of 5 with two names still wanted (three requests
   with u16 4 and flags 3, LOB2[1] plus 3); count 40 (only 32 looked at).

Part 2:

6. Stage tick, multiplayer over: ST[0] = 0 (only the camera test); ST[0] set with a stage name (unwant, name
   cleared, `u3h_multi_all(…, 0)`, level step, ST[0] and ST[1] zero) and with an empty name; the camera hand-back
   with both u64 set (flag cleared before the call), with either zero, with native 52 null; LAN flag 0 and online
   object present (branch B).
7. Stage tick, active: each of natives 42, 40, 41, 45 null in turn (no call at all); logout hold answers 1; match
   state 2 with and without bit 22; first call (natives 46 called once, proto actor wanted once, bits 16 and 17);
   native 46 null (bit 16 stays clear, asked again next call); empty stage name (default written, bit 0) and a
   given name (kept); entry missing (request with flags 3, second search) and still missing afterwards (LOB[13] =
   0xff); entry present with flags 1 (becomes 3, LOB[12]) and with 3; level null, level in state 3, level loaded
   with ST[1] different (show, bit 4 of the byte at +2, ST[4] read afterwards) and equal (no show); a show stub
   that changes the state away from 8 (no camera, ST[1] unchanged); native 47 null (ST[1] unchanged, the show
   repeats on the next call); eb[1] bit 3 set and clear (camera call or not, ST[1] = sid in both).
8. Shutdown parts: the four calls in order with their arguments.
9. NetShutdown body: order parts, active byte, LAN flag, phase, counter, stage tick; with the stage tick's branch
   A following when the online object is gone and branch B when the stand-in still reports one.
10. Automatic NetShutdown: base 0; not active; switch bit 8; phase 1, 2, 3; match state 2; each of the four pages
    and another page; `u3h_active_page` 0.
11. Leave Yes: not active; blocked; runs (result 0 in all cases).

Part 3:

12. Begin: show-page null; phase 1; selected 0; selected above the count; success (click registered, phase 1,
    ticks and error 0, step 0, page shown); the page not shown (phase 0, error 1).
13. Startup tick: phase 0, 3, 4 (nothing); UI state 7 and 8 (no tick counted); phase 2 held by the gate; phase 2
    released with both pages shown (phase 3, dialog phase 0), the first not shown (second not asked, error 4, phase
    4), the second not shown.
14. NetInit step: sign-in wait 1 and -1 (nothing else); symbol present at once (no loader call); present after the
    loader (tag 0x12 behind the pointer); never present (error 2, phase 4, no later call); lookup answering a
    pointer to null; pools fail (error 3, phase 4); LAN and online (flag byte 1 / 0, `u3h_lnb_netinit` or
    `u3h_onb_netinit`); "net" level found (no request) and not found (request with flags 0x0a); a native of 42,
    40, 41 missing (no request); profile names of 0, 5, 16 and 20 characters (20: 16 copied); selected 0 (no copy);
    no profile object, second user, user index 4; clan tag with 0, 1 and 4 backslashes; switch bit 3 (allocators
    untouched, probe untouched); native 12 null; character handle id 0 and non-zero; download failed (eb[150],
    the log line with ec[101], shutdown body, back page only with a show-page function, dialog, phase 0, no emblem
    step, no step 44); emblem NetInit answering 1; answering 0 with bit 30, with native 11 null, and normal (byte
    set before the call); the whole call order of a good run; phase 2 at the end.
15. Allocators: empty table (masks 0, nothing written); only entry 5 (0x14 and 0x15 = entry 5, index 5); entries
    9 and 7 (9 wins); 0x14 already set (only 0x15 filled, index word unchanged); both set; only entry 0x13; mask
    bits for entries 0, 31, 32, 63.
16. Net clock: bit 31 clear (pointer to clock 7, the helper's block untouched); set (copy with the named fields
    zero, every other byte equal to clock 3, pointer to the block).
17. Net services: the three calls in order.

## 8. Oddities of the old code, to be kept

1. `u3x_actor_want` answers 0 when the level want list's pointer or count is implausible, although only the third
   list is written; it does not look for the name in the third list, so two calls in quick succession add two
   entries (the callers guard with their own flags).
2. `u3x_title_unwant` tests only the level want list for presence, while `u3h_stage_unwant` removes from both
   lists.
3. `u3x_net_init_levels`: a skipped name leaves its LOB[3 + i] from an earlier run; LOB[11] and LOB[3..10] are
   not reset when the list is not valid; lists of 33 to 256 names are accepted but only 32 are handled.
4. `u3x_net_shutdown_levels` clears LOB2[4] before it knows whether it can want anything: with a native missing
   the boot levels are not wanted again later either. The symbol lookup is made before the natives are tested.
5. Stage tick: the "loaded" test is made twice (before and after the show); ST[1] is only stored when native 47
   exists, so without it the level is shown again on every call; with native 46 missing the scene enter is retried
   on every call; branch B does nothing at all when a native is missing, even `u3h_multi_all`.
6. Stage tick, branch A: the title camera hand-back is dead in the shipped helper (nothing sets the second u64);
   it is kept.
7. `u3x_net_shutdown_do` clears "multiplayer active" before the LAN flag, and calls the stage tick itself.
8. `u3x_net_shutdown_tick` tests `base` for 0; `u3x_leave_mp_yes`, the stage tick and the startup functions do
   not.
9. `u3x_leave_mp_yes` answers 0 on every path.
10. `u3x_begin_startup` registers the lobby click and writes step 0 before it knows whether the connecting page
    can be shown.
11. `u3x_startup_tick` sets phase 3 and clears the dialog phase before the two pages are shown; when the back page
    is not shown the lobby page is not tried.
12. NetInit step: the three functions of the state object are called without a null test; the profile name is
    copied here although the online sign-in wait has already done so; `u3h_onb_lan_flag()` is asked twice (flag
    byte and backend choice); the session initialisation runs on the match session only (the backends do the
    party session); the character list handle is only written when its id is 0.
13. NetInit step, failed download: the full NetShutdown body runs although the emblem step has not run; the state
    printed is whatever ec[101] holds.
14. Allocator step: the mask is taken before the table is changed; the index word is only written when entry 0x14
    was empty; when the first present candidate exists but both network entries are set, nothing changes.
15. Net clock step with bit 31: the copy keeps every field of clock 3 that is not named in 6.5.

## 9. What stays as it is

- The C wrappers (`level_want_entry`, `level_state_byte`, `actor_want`, `title_unwant`, `net_init_levels`,
  `net_shutdown_levels`, `net_shutdown_tick`, `net_shutdown_do`, `mp_lan_or_online`, `leave_mp_yes`, `stage_tick`,
  `begin_startup`, `startup_tick`), the hook dispatcher and the exports `u3h_stage_tick`,
  `u3h_net_shutdown_parts`, `u3h_netinit_services` (the last two become one-line wrappers of the new entries): the
  call sites stay.
- `u3x_level_state`, `u3x_mp_lan_or_online`, `u3x_lobby_scene_shutdown`, `u3x_online_failed`: one-line functions
  of the inventory's trivial list.
- Every function of 2.4: other groups or the C part.
- The layouts of 3.4 and 3.6: the builder writes the switch words into the image and the maintainer tools read
  the LOB, LOB2, ST and probe words of a running game; offsets and meanings must not move.
- The values of the startup phase and error: the C part reads and writes the phase (profile dialog, the "skip the
  profile page" shortcut, the sign-in wait).

## 10. Open questions

1. The default lobby stage name is a text of the helper (its own fallback choice, also kept by the C part's stage
   guard). The game's executable does not hold it; the game's map list (script symbol 0xdec97a4, records of 0x98
   bytes with the stage sid at +0x88) holds the stage of every map, but which record is the fallback's map is not
   settled here. Kept as one own text; if the owner wants it read from the map list, the map's id has to be named.
2. The four "pages before NetInit" are the helper's own choice of where NetShutdown runs (PS3 runs it from its
   page handlers); kept as four ids.
3. `u3h_fi_netinit` does not exist yet (integrator). Until it does, `u3x_netinit_services` cannot be linked; the
   tester uses a stand-in.
4. LOB2[2] (keep the boot levels / remove the proto package) is not set by the builder; it is 0 unless changed in
   a running game. Both branches are described and to be kept.

## 11. Clarifications (tester, 2026-11-14)

Facts confirmed by the old-against-new test (`reimpl2\G39\test_g39.cpp`). Where a point contradicts a section
above, this section is right.

1. **3.2, 4.2, 7 (scenario 2), 8.1: there is no third list.** The level manager holds two arrays, each as count
   (u32), capacity (u32), entries pointer: the level want list at +0x28 / +0x2c / +0x30 and the actor want list at
   +0x38 / +0x3c / +0x40. (The numbers 0x48, 0x4c, 0x50 of 3.2 are the last digits of the image offsets 0x1afcf48,
   0x1afcf4c, 0x1afcf50 of the actor want list's three fields, not offsets in the manager.) Steps 4 and 5 of
   `u3x_actor_want` therefore work on the **actor want list itself**: read its count (+0x38), its capacity (+0x3c)
   and its entries pointer (+0x40); result 0 when the pointer is not a plausible pointer, or the count is not below
   the capacity, or the capacity is above 4096; otherwise the entry at index "count" of the actor want list becomes
   all zero, hash, sid, flags 1, the count at +0x38 is raised by 1, result 2. Consequences: a second call with the
   same name finds the entry in the actor want list in step 3 and answers 1; no second entry is made. The sentence
   "The third list itself is not searched", the second half of oddity 8.1 and the scenario "the same name twice in a
   row (two entries)" do not apply. The bytes at manager + 0x48 to + 0x57 are never read or written by this group.
   The capacity of the level want list (+0x2c) is not used by this group.
2. 4.2 step 5, read point: the count is read again from the list after native 40 has returned, and that value
   plus 1 is stored. One read before the call gives the same unless native 40 changes the actor want list.
3. 4.4 and 4.5, read points of the boot list: the count is read again from the list at the start of every loop pass
   (the loop ends when i has reached the count as it is then, or 32); the records pointer is read once, before the
   loop; a name pointer is read when its record is reached. One read of the count before the loop gives the same
   unless `u3h_crc_sid`, `u3h_stage_unwant`, native 40 or native 41 changes the game's list.
4. 4.4 step 4, LOB2[2]: bit 0 is read on every loop pass (after that name's sid and its want-list test, before its
   `u3h_stage_unwant`); bit 1 is read once, after the loop. LOB2[0] and LOB[1] are read after the
   `u3h_stage_unwant` call whose result is added.
5. 4.5, natives: natives 13, 40 and 41 are all read before native 13 is called. Reading 40 and 41 after that call
   gives the same unless native 13 changes them. The order in which 40 and 41 are tested cannot be observed
   (nothing is called or written between the two tests).
6. 5.1, natives: native 42 is read before `u3x_mp_lan_or_online` is called; natives 40, 41 and 45 after it and
   before `u3h_multi_all`. The values read there are the ones called in steps 9, 12, 13 and handed to
   `u3h_match_stage_cleanup`. Natives 46, 47 and 52 are read at their own step (once for the test, once more for
   the call). Reading native 42 together with the other three gives the same unless `u3h_onb_mgr` changes it.
7. 5.1 step 6: the name handed to `u3x_actor_want` is the helper's own text; the image's copy at 0x145f915 has the
   same 22 characters and NUL, and only the content reaches `u3h_crc_sid` and native 40.
8. 5.1 step 8: exactly 22 bytes are written (21 characters and the NUL); the bytes behind keep their content.
9. 5.1 step 9 and 6.3 step 7: the request's u16 at 0xc is 0 in both (only 4.5's request has 4).
10. 5.1 steps 13 and 15: "its state is 8" compares the whole u32 at +0x30 (0x108 is not "loaded"), unlike
    `u3x_level_state`, which answers the low 8 bits.
11. 4.3 step 2: both `u3x_level_state` answers are at most 0xff, so "plus 256 times" and a bitwise OR with the
    second shifted left by 8 are the same value.
12. 3.7 cross-check, done: `u3h_crc_sid` of the texts at 0x145ba00 and 0x145ba0d is 0xeef9ed7b and 0xa69a074d, the
    two immediates at 0x575718 and 0x575728; the text at 0x145b905 is `proto-sp` and gives 0xe0fdf8f6; `net` gives
    0x4240ef2e.
13. 6.3 step 24: when the stage tick has not run since the startup began (ST[0] is 0), the NetShutdown body's own
    stage tick does branch A's camera test only (no `u3h_multi_all`, no level step).
14. 5.5 and 3.9: the two counters are raised exactly as 5.5 says on all paths (first counter also when multiplayer
    is not active or NetShutdown is blocked).
