# G43. Co-op: hunter spectate, NPC melee native, hero slots and look descs

Behaviour description for a reimplementation (reimpl2 pipeline, describer's document). It says what the functions
do, not how the old ones were written. All offsets are hexadecimal when prefixed with 0x. "Image offset X" means the
address `base + X`, where `base` is the game image base. All multi-byte values are little-endian; "u8 / u16 / u32 /
s16 / s32 / u64" are integers of that width, "pointer" is 8 bytes. Names follow `G25_coop_lists.md` (net player
record `p`, state object `s`, natives, hunter classes), `G40_respawn.md` (manager, handles, net clock) and
`G33_publish_prompts.md` (screen menus, script natives).

**Float.** This group has no float arithmetic. Four words of a hit record are float PATTERNS stored as plain 32-bit
words (4.5). Nothing here uses the x87 unit or the `f_*` tools.

**Tables.** Section C of the inventory lists no literal table of game values for this group, and the new code needs
none. The only literal game bytes are three 12-byte code checks (3.3), kept as in G25 3.10 and G33 3.6. Two
constants of a public algorithm occur: the 32-bit FNV-1a offset basis and prime (3.6).

## 1. Purpose and callers

Three independent parts. No part calls a function of another part, except that part A's spectate function calls
part A's own two helpers.

| Part | Content | Exported names |
|---|---|---|
| **A** | Co-op hunter spectate: a dead hunter (team 1) of a co-op game watches the hunter AIs, cycles them, gets the class page and asks the host for the AI. With it: an entity's tag value, the "spoiler" net message, the camera kill | `u3x_entity_tag`, `u3x_send_spoiler`, `u3x_camera_kill`, `u3x_coop_spectate` |
| **B** | The script native "net-apply-npc-fight-damage" (the PS4 game's own function for it is empty): melee damage of a net player on a tracked NPC | `u3x_native_npc_fight` |
| **C** | Co-op Adventure story heroes: the co-op slot of a net player, the hero look of that slot from the net game table, the repair of that table's looks, and the two places that take the look (lobby placement, spawn) | `u3x_hero_fix_all`, `u3x_hero_desc`, `u3x_hero_slot`, `u3x_hero_kind` |

Callers (all stay as they are):

- `u3x_coop_spectate`: the C one-line wrapper `coop_spectate` (no caller left in C) and `u3x_camera_tick` of G42,
  once per frame for a local net player, on the game thread, only when all of these hold: the player is not alive,
  no late-join fade, the manager's u8 at +0x6a24 is 0, the game is a co-op game, the player's team (s32 at
  `p` + 0x21c) is 1, and its time stamp (s32 at `p` + 0x39c) is 0 or more than 0x5dc ticks (5 s) behind the net
  clock. `menu` is `u3x_screen_menu(base, screen of p)` and may be null. A result of 1 makes the camera function
  stop for this frame (the normal death cameras are skipped); 0 lets it go on.
- `u3x_entity_tag`: the C one-line wrapper `ent_tag_u32`, called by the C function `cam_fixed` (G42) with a
  spawner object, tag 0x24f280a0 and the AI id as fallback.
- `u3x_send_spoiler`: `u3x_coop_spectate`, and the C one-line wrapper `net_send_spoiler` (no caller left).
- `u3x_camera_kill`: `u3x_coop_spectate`, and the C one-line wrapper `cam_kill` (no caller left).
- `u3x_native_npc_fight`: the C one-line wrapper `nat_apply_npc_fight` (the game's native signature: argument
  count, argument array, context; it passes all three). The C function `natives_link` stores that wrapper's address
  in the game's native record at image 0x1b7e270, so the game's scripts call it. It runs on whatever thread runs
  the game's scripts. It has no parameter for the image base (4.5).
- Part C: the C function `w09_kind` is called by `k_kind` for kind 248, which two code stubs of the builder raise:
  one in the lobby's character placement (image 0x5b69aa, argument = that function's frame pointer with bit 0 set)
  and one in the spawn's co-op branch (image 0x7887df, argument = the net player record). `k_kind` ignores the
  result and then calls `lg01_hold` (not of this group). `w11_fixall`, `w09_desc`, `w09_slot` are called only by
  `w09_kind`. Game thread.

Wrappers after the change (part C, the functions still in C):

| C function | becomes the one line |
|---|---|
| `w11_fixall(s)` | `u3x_hero_fix_all(s, w11_d)` |
| `w09_desc(base, s, slot, gi)` | returns `u3x_hero_desc(base, s, slot, gi, w11_d)` |
| `w09_slot(base, pl)` | returns `u3x_hero_slot(base, pl)` |
| `w09_kind(base, s, r)` | returns `u3x_hero_kind(base, s, r, w09_d, w11_d)` |

`w09_d` (8 words) and `w11_d` (4 words) are the C part's two diagnostic arrays; they stay where they are (their own
data section) and are passed as plain `uint32_t *`. `u3x_hero_kind` calls the three other new functions directly,
so the first three wrappers and the C function `w11_fixdesc` have no caller left (the integrator may drop them).

Nothing in this group is locked, and there is no timing rule besides the caller's.

## 2. Interface

### 2.1 Functions to deliver

All `extern "C"`.

| Part | Function | Signature | Result |
|---|---|---|---|
| A | tag value | `uint32_t u3x_entity_tag(const uint8_t *e, uint32_t tag, uint32_t fallback)` | the value, or `fallback` |
| A | spoiler message | `void u3x_send_spoiler(uintptr_t base, uint32_t code)` | none; `code` = a net player id (3.2) |
| A | camera kill | `void u3x_camera_kill(uint8_t *p)` | none |
| A | spectate | `int u3x_coop_spectate(uintptr_t base, void *s, uint8_t *p, uint8_t *menu)` | 1 = spectating (skip the death cameras), 0 = not |
| B | native | `int64_t u3x_native_npc_fight(int32_t argc, uint64_t *argv, void *ctx)` | always 0 |
| C | repair all | `void u3x_hero_fix_all(void *s, uint32_t *w11)` | none |
| C | look of a slot | `const uint8_t *u3x_hero_desc(uintptr_t base, void *s, int slot, int32_t *gi, uint32_t *w11)` | the look (0x28 bytes of game data) or null; `*gi` see 4.7 |
| C | slot of a player | `int u3x_hero_slot(uintptr_t base, const uint8_t *pl)` | 0, 1, 2 or −1 |
| C | kind 248 | `int u3x_hero_kind(uintptr_t base, void *s, uint64_t *r, uint32_t *w09, uint32_t *w11)` | always 0 |

`r` of `u3x_hero_kind` is not a pointer to read: its VALUE is the stub's argument (4.9).

### 2.2 Functions of other groups, of the C part and shared ones (declare, do not write)

| Name | Signature | What it does | Part |
|---|---|---|---|
| `u3h_ext` | `uint8_t *u3h_ext(void)` | the helper's extension state (CONVENTIONS.h; `ext_ea()`, `ext_eb()` are there) | A, B, C |
| `u3h_crc_sid` | `uint32_t u3h_crc_sid(const char *p)` | the game's 32-bit name checksum of a zero-terminated text (pure) | A |
| `u3h_cam_fixed` | `int u3h_cam_fixed(uintptr_t base, void *s, uint8_t *p, int32_t idx)` | G42: gives net player `p` a fixed camera (for a co-op hunter with a selected AI: at that AI's spawner), replacing the one it has; stores the camera's handle at `p` + 0x388. Result not used here | A |
| `u3x_net_player_id` | `uint32_t u3x_net_player_id(const uint8_t *np)` | existing one-liner, the net player id of a record (3.2); reads only | A, B |
| `u3x_hunter_count` | `uint32_t u3x_hunter_count(uintptr_t base)` | existing: the number of hunter AIs (3.4); reads only | A |
| `u3x_hunter_id` | `int32_t u3x_hunter_id(uintptr_t base, uint32_t i)` | existing: the id of hunter AI `i` (3.4); no range check; reads only | A |
| `u3x_hunter_index` | `uint32_t u3x_hunter_index(uintptr_t base, int32_t id)` | existing: the position of the first hunter AI with that id among the first min(count, 32); when there is none: min(count, 32). Reads only | A |
| `u3x_pad_pressed` | `uint32_t u3x_pad_pressed(uintptr_t base, int32_t screen)` | existing: the buttons newly pressed this frame on the pad of that screen (3.1), 0 when the game has no pad object; reads only | A |
| `u3x_spawner_occupied` | `int u3x_spawner_occupied(uintptr_t base, void *s, int32_t id)` | existing: one call of native 58 of `s` with (image 0x1afcf10, `id` as u32), result dropped; returns 0 always | A |
| `u3x_menu_open` | `int u3x_menu_open(const uint8_t *m)` | G33 4.7: 1 when the menu is not null and open, else 0; reads only | A |
| `u3x_menu_page` | `uint32_t u3x_menu_page(const uint8_t *m)` | G33 4.7: the id of the menu's top page, 0 for a null menu or none; reads only | A |
| `u3x_menu_show` | `void u3x_menu_show(uintptr_t base, uint8_t *m, uint32_t page)` | G33 4.8: opens the menu on that page | A |
| `u3x_menu_back` | `int u3x_menu_back(uintptr_t base, uint8_t *m, int silent)` | G33 4.9: back one page; accepts a null menu | A |
| `u3x_menu_close` | `void u3x_menu_close(uintptr_t base, uint8_t *m)` | G33 4.10: closes the menu; accepts a null menu | A |
| `u3x_sym_table` | `uint8_t *u3x_sym_table(void *s, uint32_t id)` | data of a script symbol (G40 2.2), or null | C |
| `u3x_fix_hero_look` | `int u3x_fix_hero_look(uint8_t *look)` | existing (the helper's own rule, 3.8): turns the first four bytes of a look back when they are reversed; 1 when it changed them, else 0 | C |

The old spectate code used file-private twins of the five "existing" hunter / pad helpers; the exported forms do
the same and are what the new code calls. All of them only read memory, except `u3x_spawner_occupied` (one native
call) — a tester's call record therefore shows no difference.

## 3. Game facts

### 3.1 Image data

| Image offset | Type | Meaning |
|---|---|---|
| 0x17cb060 | object | the net game manager ("manager") |
| manager + 0x18 + 0x18·i | 0x18 bytes | hunter AI i (3.4); count: u32 at manager + 0x318 |
| manager + 0x370 | pointer | the net player record of local user 0 (may be null) |
| manager + 0x390 + 0x9c0·i, i = 0 .. 9 | record | the ten net player records (3.2) |
| manager + 0x6520 | pointer | bucket array of the tracked NPCs (3.6); number of buckets: s32 at manager + 0x655c |
| manager + 0x661c | s16 | number of the current net game record |
| manager + 0x6808 | s32 | round state; 2 = the round is running |
| 0x17b4ca4 | s32 | the net clock, ticks of 1/300 s |
| 0x17d5c61 | u8 | non-zero while multiplayer is active |
| 0x17d6420 | object | the match session: u64 at +0x18 non-zero = it has a room; u32 at +0x12e4 and +0x12e8 equal = this machine leads it. "This machine is the host" = no room, or leads it |
| 0x17d7ae0 | s32 | forced co-op slot (0 .. 2), or a value outside that range for none |
| 0x16e2fa0 | pointer | the game's pad object, or null: pressed buttons of screen 0 = u32 at +0x7e0 + 0x94, of screen 1 = u32 at +0x9b8 + 0x94 (any screen number other than 1 reads screen 0's). Button bits used: 0x20 = right, 0x80 = left |
| 0x16d3770 | function table | the game's own table of net message 0x5e (its slot at +0x20 is the handler, image 0x6d2e80) |
| 0x16f0ae0 | u32 | first word of the game's "argument is a name" descriptor of the script argument reader |
| 0x16f0ae8 | u64 | second word of that descriptor; also the reader's "step to the next argument" switch (non-zero in the shipped game) |
| 0x1b7e270 | pointer | the native record slot of "net-apply-npc-fight-damage" (written by C `natives_link`, not by this group) |
| 0x1afcf10 | object | the level manager (first argument of native 58) |

Both descriptor words are read from the image on every call of the native; they are never literals of the helper.

### 3.2 Net player record (0x9c0 bytes)

| Offset | Type | Meaning |
|---|---|---|
| 0x10 | handle (G40 3.4) | the player's character |
| 0xe1 | u8 | non-zero: joined |
| 0x1e4 | u32 | id bits |
| 0x1e8 | s32 | user / screen number (0 or 1) |
| 0x1ec | u8 | guest flag |
| 0x21c | s32 | team; 1 = the hunters |
| 0x388 | handle | the player's death / spectate camera (pointer at +0x388, id u32 at +0x390) |
| 0x42c | s32 | co-op slot given by the game's match setup (0 .. 2 when given) |
| 0x5e4 | u8 | non-zero: leaving |
| 0x5fd | u8 | 1 = the AI has been asked for (once per pick) |
| 0x600 | s32 | id of the hunter AI the player watches; 0 = none |
| 0x608 | u32 | table position of the picked hunter class (G25) |
| 0x60c | u8 | 1 = a hunter class has been picked (G25) |
| 0x620 | 0x28 bytes | the player's hero look (3.8) |

**Net player id** (`u3x_net_player_id`): the low 12 bits of the u32 at +0x1e4, plus 0x800 when the u8 at +0x1ec is
non-zero and those 12 bits are not 0, plus 0x1000 times the s32 at +0x1e8. An unused record gives 0.

**In the game**: u8 at +0xe1 non-zero and u8 at +0x5e4 zero.

**Handle**: as G40 3.4 (pointer H, u32 id 8 bytes later; valid when H is not null, the id is not 0, the pointer O
at H is not null and the u32 at O + 0x7c equals the id; its object is O).

**Virtual call**: "slot X of object O" = the function whose address is the pointer at (pointer at O + 0) + X bytes,
called with O as first argument and the further arguments named.

### 3.3 Game functions called directly

System V AMD64 convention, integer arguments only.

| Image offset | Arguments | Result | Meaning | Part |
|---|---|---|---|---|
| 0xd9e990 | (u64 size, pointer to two s32 words, u64 alignment) | pointer or null | the net message allocator | A |
| 0x733d90 | (manager, message) | none | sends a net message to the host | A |
| 0x11cc560 | (reader block, s32 argc, argv, ctx, s32 expected count) | none | prepares the script argument reader (3.5) | B |
| 0x11cc9d0 | (reader block, u32 descriptor word 1, u64 descriptor word 2) | u32 | takes the next argument as a name id | B |
| 0x55cb20 | (hit record, character object) | none | fills the hit record's attacker part | B |
| 0x6ae7a0 | ten arguments, see 4.5 | none | sends "this NPC was killed" | B |
| 0x996a60 | (null, look) | u8 | non-zero when every art package the look needs is loaded | C |

Part A calls nothing unless ALL THREE of these begin with the expected 12 code bytes (read-only helper data):

- 0xd9e990: 55 48 89 E5 41 57 41 56 41 55 41 54
- 0x733d90: 55 48 89 E5 41 57 41 56 41 54 53 49
- 0x6d2e80 (the message's handler, reached through the table at 0x16d3770): 55 48 89 E5 41 56 53 44 8B 4F 14 41

Parts B and C make their calls without such a check.

Native of the state object used (through `u3x_spawner_occupied` only): 58, (level manager, u32 spawner id) →
pointer.

### 3.4 Hunter AI list

Entry i at manager + 0x18 + 0x18·i: a handle at +0, the AI's id (s32) at +0x10. Count: u32 at manager + 0x318. The
list has room for 32 entries. The id of an AI is also the id of its spawner.

### 3.5 The script argument reader block

40 bytes, on the caller's stack, filled by 0x11cc560:

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | number of arguments |
| 8 | pointer | the argument values, u64 each |
| 0x10 | u32 | position of the next argument |
| 0x14, 0x18, 0x20 | u32, pointer, u64 | the reader's own (expected count, context, error state) |

**Taking a number**: when the position is below the number of arguments, the value is the low 32 bits of the
argument at the position, and the position goes up by 1 when descriptor word 2 (image 0x16f0ae8) is not 0;
otherwise the value is 0 and the position stays. **Taking a name**: one call of 0x11cc9d0 with the block and the
two descriptor words; it moves the position itself.

### 3.6 Tracked NPCs (manager + 0x6520)

The game keeps one tracker per scripted NPC, keyed by the NPC's name id (u32), in a hash table whose nodes form one
chain: the bucket array holds one pointer per bucket plus one more after the last bucket (the chain's end marker).
Bucket of a name = hash mod number of buckets (unsigned); hash = 32-bit FNV-1a (public algorithm: offset basis
0x811c9dc5, prime 0x01000193) over the name's four bytes, lowest byte first.

Node: pointer to the next node at +0; u32 name at +0x10; the tracker from +0x18.

Tracker: handle of the NPC at +0 (pointer at +0, id at +8); three u32 attacker ids at +0x50; three u32 damage sums
at +0x80.

NPC object: pointer at +0x310 = its health part (slot 0x78 of it, no further argument → s32 health); slot 0x3a8 (no
further argument) → u8, non-zero = dead; slot 0x5e0 (u32 damage) = take damage; u8 at +0x70a, bit 0 = "kill sent".

### 3.7 Net message 0x5e ("spoiler": a hunter asks for an AI)

0x28 bytes from the allocator:

| Offset | Type | Value written |
|---|---|---|
| 0 | pointer | image 0x16d3770 |
| 8 | u8 | 0x5e (the message number) |
| 9 | u8 | 0 |
| 0xa | u8 | 1 (reference count) |
| 0xb | u8 | 1 |
| 0xc, 0xd | u8 | 0, 0 |
| 0x14 | u32 | the net player id |
| 0x18 | u32 | class position |
| 0x1c | u32 | AI id |
| 0x20 | u8 | "asked" flag |

The other bytes are left as the allocator returned them.

### 3.8 Net game table and hero looks (part C)

Data of script symbol 0x3b9a067d (`u3x_sym_table`): s32 at +0 = number of records, pointer at +8 = the records,
0xd0 bytes each. In a record: s32 at +0xc = game type (11 = Co-op Adventure); pointer at +0x70 = an array of three
pointers, one look per co-op slot 0, 1, 2 (only type-11 records are looked at).

A look is 0x28 bytes; byte 0 = character type (0 = the player's custom hero, other values = story characters).
This group only reads byte 0, compares and copies whole looks.

**The repair** (`u3x_fix_hero_look`, the helper's own rule for a conversion fault of the PS4 game data, which
stores the first four bytes of these looks in reverse order): when bytes 0, 1, 2 are all 0 and byte 3 is between 1
and 0x3f, byte 0 takes byte 3's value and byte 3 becomes 0, result 1; in every other case nothing changes, result
0. It is idempotent. The table is loaded game data and is changed in place.

### 3.9 Helper state

| Where | Meaning |
|---|---|
| extension state + 0x21b0, u64 | the image base for script natives (set by C `natives_link`; 0 before) |
| extension state + 0x100 + 4·k, u32 | co-op counters c[k]: c[3] spectate frames, c[4] class page opened, c[5] messages sent, c[6] cycles, c[12] last selected AI, c[13] last AI count |
| `ext_ea()[46]`, `[47]` | NPC hits applied; kills sent |
| `ext_eb()[3]`, bit 31 | switch: set = part C does nothing |
| `w09[0..7]`, `w11[0..3]` | part C diagnostics (4.6 to 4.9) |

"x += 1" on these is a plain, unlocked increment.

## 4. Behaviour

### 4.1 `u3x_entity_tag` (part A)

An entity carries a sorted tag list: pointer T at `e` + 0x68; pointer to the entries at T + 0, u32 count at T + 8;
an entry is 0x10 bytes: u32 tag at +0, pointer to its value at +8; entries ascend by tag (unsigned). A value: s16
type at +0, pointer D to its data at +8.

Result `fallback` when: `e` is null; T is null; the entries pointer is null or the count is 0; no entry has the
tag; the entry's value pointer is null; D is null; the type is none of the three below; or (type 5) the text
pointer is null or the text is empty.

The entry is found by lower-bound bisection over the count (the first position whose tag is not below the wanted
one, probing the middle of the remaining range, lower half kept when the probed tag is not below; exactly the
probing of the standard library's lower bound), and it counts only when its tag equals the wanted one. On a list
that is not sorted the result is whatever that bisection gives.

Type 0xd (name id) and type 1 (integer): the u32 at D. Type 5 (text): `u3h_crc_sid` of the text whose pointer is
at D. Reads only.

### 4.2 `u3x_send_spoiler` (part A)

1. Any of the three code checks of 3.3 fails: nothing at all.
2. Message = 0xd9e990(0x28, pointer to the two s32 words {0x14, 0}, 8). Null: nothing more.
3. The fields of 3.7 are written: id = `code`; class position, AI id and flag are 0, unless one of the ten net
   player records has the id `code` (`u3x_net_player_id`; records in order 0 .. 9, the first match; no "in the
   game" test): then they are that record's u32 at +0x608, u32 at +0x600 and u8 at +0x5fd.
4. 0x733d90(manager, message).
5. Slot 0x20 of the message, with one further argument 0 (the game's own handler: the message is also handled on
   this machine).
6. The reference count (u8 at message + 0xa) goes down by 1; when it is then 0: slot 8 of the message, no further
   argument (the game releases it).
7. c[5] += 1.

### 4.3 `u3x_camera_kill` (part A)

When the camera handle of `p` (3.2, +0x388) is valid: slot 0x158 of the camera object, no further argument.
Otherwise nothing. The handle words are not changed.

### 4.4 `u3x_coop_spectate` (part A)

N = `u3x_hunter_count(base)`; screen = s32 at `p` + 0x1e8; "sel" = the s32 at `p` + 0x600; PAGE = 0xd3a938b7 (the
hunter class page). First c[13] = N, always.

**Not spectating** (the round state is not 2, or N is 0), in this order:
1. `u3x_menu_page(menu)` is PAGE: `u3x_menu_back(base, menu, 0)`, then `u3x_menu_close(base, menu)`.
2. sel is not 0 and the camera handle is valid: the camera kill of 4.3.
3. sel = 0. Result 0.

**Spectating** (round state 2 and N not 0), in this order:
1. c[3] += 1.
2. sel is 0, or `u3x_hunter_index(base, sel)` equals N: sel = `u3x_hunter_id(base, 0)`, then the camera kill of
   4.3.
3. The camera handle is not valid (also right after step 2): `u3h_cam_fixed(base, s, p, 0)`.
4. Exactly one of these, the first whose condition holds (none may hold):
   - **Cycle**: `u3x_menu_page(menu)` is PAGE and `u3x_menu_open(menu)` is 1. Pressed =
     `u3x_pad_pressed(base, screen)`. Right (0x20) set: target = index of sel + 1. Else left (0x80) set: target =
     index of sel − 1. Neither: nothing. With a target: below 0 becomes N − 1; N or above becomes 0; sel =
     `u3x_hunter_id(base, target)`; c[6] += 1; `u3h_cam_fixed(base, s, p, 0)`. Right wins when both are set.
     "Index of sel" is `u3x_hunter_index(base, sel)` (with more than 32 AIs and sel not among the first 32 it is
     32: kept as it is).
   - **Open the page**: the u8 at `p` + 0x60c is 0, `menu` is not null and `u3x_menu_open(menu)` is 0:
     `u3x_menu_show(base, menu, PAGE)`; the s32 at `menu` + 0x2694 (the menu's user) = screen; c[4] += 1;
     `u3h_cam_fixed(base, s, p, 0)`.
   - **Ask the host**: `u3x_menu_open(menu)` is 0, the u8 at `p` + 0x5fd is 0, the u8 at `p` + 0x60c is not 0, and
     then (called only when the three before hold) `u3x_spawner_occupied(base, s, sel)` is 0: the u8 at
     `p` + 0x5fd = 1, then `u3x_send_spoiler(base, u3x_net_player_id(p))`.
5. c[12] = sel. Result 1.

`menu` may be null: then only "ask the host" can apply. `p` is never null.

### 4.5 `u3x_native_npc_fight` (part B)

Script arguments: (net player id, NPC name, damage, attack name). Result always 0; "stop" below = return 0.

1. B = the u64 at extension state + 0x21b0. 0: stop (nothing called). The u8 at image 0x17d5c61 is 0: stop.
2. The reader block (3.5): 0x11cc560(block, `argc`, `argv`, `ctx`, 4). The two descriptor words are read. Then, in
   this order: player id = take a number; NPC name = take a name; damage = take a number; attack = take a name.
   All four are always taken (two calls of 0x11cc9d0), whatever their values.
3. The attacker's record: player id 0: stop. Otherwise the first of the ten net player records (order 0 .. 9)
   whose `u3x_net_player_id` equals the player id; none: stop. Its character handle (+0x10) not valid: stop.
   "attacker" = that character object.
4. The tracker (3.6): number of buckets ≤ 0 or bucket array null: stop. Start at the bucket of the NPC name and
   follow the "next" pointers until a node with that name or a null pointer (the walk is not limited to the
   bucket). Null, or the node is the end marker (the pointer in the array right after the last bucket): stop. The
   NPC handle of the tracker not valid: stop. "npc" = its object.
5. Only when this machine is the host (3.1): the first of the tracker's three attacker slots, in order, that
   holds the player id or 0 is used and the search ends there: a slot with the id gets the damage added to its sum
   (32-bit wrap); an empty slot gets the id, and its sum is SET to the damage. All three held by others: nothing.
6. The health part of the npc (pointer at +0x310) is null: stop (the note of step 5 stays). health = slot 0x78 of
   the health part.
7. Not the host and damage (unsigned 32-bit, as a 64-bit number) below health (signed): stop. So a guest applies
   only a hit of at least the remaining health; a health of 0 or less never stops it.
8. dead before = slot 0x3a8 of the npc (non-zero?); slot 0x5e0 of the npc with the damage; dead after = slot 0x3a8
   again. `ext_ea()[46]` += 1.
9. Dead before equals dead after, or not the host: stop.
10. A hit record: 0x90 bytes, 16-byte aligned, all zero, then the u32 at +0x10 and +0x14 = 0x7fcccccc (the game's
    "not set" pattern, a NaN), at +0x4c and +0x6c = 0x3f800000 (1.0). 0x55cb20(record, attacker).
11. A local s32 T = the net clock (image 0x17b4ca4). 0x6ae7a0 with ten arguments: (u32 attack, attacker, npc,
    pointer to T, 0, 0, and on the stack 0, 0, 0, pointer to the hit record); the five zeros are full 64-bit
    zeros.
12. Bit 0 of the npc's u8 at +0x70a is set; `ext_ea()[47]` += 1.

### 4.6 `u3x_hero_fix_all` (part C)

G = `u3x_sym_table(s, 0x3b9a067d)`; null: nothing. Count = s32 at G, records = pointer at G + 8. Nothing unless the
records pointer passes `ptr_ok` and 1 ≤ count ≤ 1024. For every record of type 11, in order, whose look array
pointer (+0x70) passes `ptr_ok`: each of its three looks, in order 0, 1, 2, gets the **look repair step**:

- The look pointer does not pass `ptr_ok`: nothing.
- Otherwise remember the look's first u32, call `u3x_fix_hero_look(look)`; when it returns 1: `w11[3]` = the
  remembered word, `w11[0]` += 1.

### 4.7 `u3x_hero_desc` (part C)

In this order; each failing test gives null:
1. G = `u3x_sym_table(s, 0x3b9a067d)` not null. (Null here: `*gi` is not written.)
2. Number = the s16 at manager + 0x661c, sign-extended; `*gi` = number. 0 ≤ number < count (s32 at G).
3. The records pointer (G + 8) not null; the record = number-th; its type is 11.
4. Its look array pointer not null, and 0 ≤ `slot` ≤ 2.
5. The look pointer of the slot not null.

Then the look repair step of 4.6 on that look (with `w11`), and the look pointer is the result. No `ptr_ok` test is
made here except the one inside the repair step (a look pointer failing it is returned unrepaired).

### 4.8 `u3x_hero_slot` (part C)

The first that applies:
1. The s32 at `pl` + 0x42c is 0, 1 or 2: that value.
2. The forced slot (s32 at image 0x17d7ae0) is 0, 1 or 2: that value.
3. Counting the net player records that are in the game (3.2) in order 0 .. 9: when `pl` is such a record, the
   number of in-game records before it, if that is 0, 1 or 2, else −1.
4. `pl` is not one of the in-game records: 0.

`pl` is compared by address. Reads only.

### 4.9 `u3x_hero_kind` (part C)

Result always 0. A = the value of `r` as a number. "Summary word" = byte 0 of the look, plus the slot shifted left
by 8, plus the low 16 bits of the net game number (the `gi` output of 4.7, preset to −1) shifted left by 16 (all
as u32 arithmetic).

1. Bit 31 of `ext_eb()[3]` set: return, nothing else happens.
2. `u3x_hero_fix_all(s, w11)`.

**A odd (lobby placement).** F = A with bit 0 cleared (the placement function's frame pointer; its look is the
0x28 bytes at F − 0x58).
3. `w09[0]` += 1. F fails `ptr_ok`: return.
4. pl = the pointer at manager + 0x370. Fails `ptr_ok`: `w09[6]` += 1, return.
5. slot = `u3x_hero_slot(base, pl)`; look = `u3x_hero_desc(base, s, slot, &gi, w11)`. Null: `w09[6]` += 1, return.
6. `w09[7]` = (u8 at pl + 0x620) + (byte 0 of the look shifted left by 8) + 0x10000.
7. 0x996a60(null, look) returns 0: `w11[1]` += 1, return (the game's own look stays this frame; the placement
   comes again every frame).
8. `w11[2]` += 1; the 0x28 bytes of the look are copied to F − 0x58; `w09[1]` += 1; `w09[5]` = the summary word.

**A even (spawn).** pl = A (the net player record).
3. pl fails `ptr_ok` (this includes A = 0): return.
4. slot and look as in step 5 above. Null: `w09[6]` += 1, return.
5. `w09[2]` += 1; `w09[7]` = (u8 at pl + 0x620) + (byte 0 of the look shifted left by 8) + 0x20000.
6. One of three:
   - the 0x28 bytes at pl + 0x620 equal the look: `w09[3]` += 1 (0x996a60 is not called);
   - else 0x996a60(null, look) returns 0: `w09[6]` += 0x10000 (the player keeps its look: no spawn with art that is
     not loaded);
   - else the look is copied to pl + 0x620 and `w09[4]` += 1.
7. `w09[5]` = the summary word (in all three cases).

A slot of −1 simply makes 4.7 return null.

## 5. Test scenarios

Everything can be tested on a fake image with call-recording stand-ins (the functions of 3.3 written at their image
offsets, with the 12 bytes of the three checked ones in place; fake objects whose function tables record slot
calls; stand-ins for the functions of 2.2 that are not "reads only"; a fake state object with native 58). Nothing
needs the live game for the old-versus-new comparison; that the AI is really handed over, the NPC really dies and
the story heroes really show is a live check for the integrator (a co-op Adventure and a co-op Arena match).

Part A:
1. `u3x_entity_tag`: null entity; null list; null entries; count 0; tag below all, above all, between two, equal
   to the first, a middle and the last entry (counts 1, 2, 3, 7, 8); null value; null data; types 0xd, 1, 5 (text,
   empty text, null text pointer), 0, 2, −1; random sorted lists.
2. `u3x_send_spoiler`: each of the three checks failing alone (no call at all, c[5] unchanged); allocator returns
   null; `code` matching no record (zeros), record 0, record 9, two records (first wins), `code` 0 with unused
   records; reference count reaching 0 (release called) and not reaching 0 (the handler stand-in raises it).
   Compare the message bytes of 3.7, the bytes not written, and the call order.
3. `u3x_camera_kill`: valid handle; null pointer; id 0; id mismatch.
4. `u3x_coop_spectate`: round state 1 and 2; N = 0, 1, 3, 32, 40. Not spectating: page PAGE / other / null menu;
   sel 0 / set with valid / invalid camera. Spectating: sel 0; sel unknown; sel known first / middle / last; camera
   valid / invalid; page open with right, left, both, none, wrap at both ends; page not PAGE with class not picked
   (menu closed, menu open, menu null); class picked with flag 0 / 1, menu open / closed. Compare `p`, the menu
   word +0x2694, c[3], c[4], c[5], c[6], c[12], c[13], the result and the call record.

Part B:
5. Base 0; multiplayer off; argc 0, 2, 4, 6 with descriptor word 2 zero and non-zero; player id 0; unknown id; id
   of record 0 and 9; character handle invalid; buckets 0 and −1; null bucket array; name in its own bucket, later
   in the chain, absent with the chain ending in null, absent ending at the end marker, name equal to the end
   marker's own name word; NPC handle invalid.
6. Host by "no room" and by "leads"; guest. Attacker slots: empty; own id in slot 0, 1, 2; all three others; sum
   wrap. Health part null (host: slots already changed). Guest with damage below, equal, above health, health
   negative, damage 0x80000000 and above. Dead before / after in all four combinations, host and guest. Compare
   the slots, the hit record contents as seen by the 0x55cb20 stand-in, the ten arguments of 0x6ae7a0 and the
   value of T, the +0x70a byte, ea[46], ea[47].

Part C:
7. `u3x_hero_fix_all`: no table; records pointer odd / null; count 0, 1, 1024, 1025, negative; types 10, 11, 12;
   look array pointer null / unaligned; look pointers null / unaligned; looks with first bytes reversed (type 1,
   0x3f), 0x40, already right, all zero, byte 1 or 2 set. `w11[0]`, `w11[3]`.
8. `u3x_hero_desc`: no table (`*gi` untouched); number −1, 0, count − 1, count; null records; type not 11; null
   look array; slot −1, 0, 2, 3; null look; a look needing repair; an unaligned look pointer (returned as it is).
9. `u3x_hero_slot`: +0x42c = 0, 2, 3, −1; forced 0, 2, 3, −1; `pl` the 1st, 3rd, 4th in-game record; `pl` a record
   that is not in the game; `pl` outside the ten records; leaving / not joined records in between.
10. `u3x_hero_kind`: switch bit set (nothing, not even the repair); `r` = 0; odd with a bad frame pointer; null
    and unaligned local player; no look; 0x996a60 answering 0 and 1 (frame bytes, `w09`, `w11`); even with a bad
    pointer; no look; look equal (no 0x996a60 call); different with 0x996a60 answering 0 and 1; slot −1.

## 6. Kept as it is: things that look like faults or dead code

1. `u3x_send_spoiler` looks the record up by id without an "in the game" test, and `code` 0 matches the first
   unused record.
2. `u3x_send_spoiler` writes only the bytes of 3.7; bytes 0xe .. 0x13 and 0x21 .. 0x27 of the message keep what
   the allocator left.
3. `u3x_coop_spectate`: with more than 32 AIs the "not found" answer of `u3x_hunter_index` (32) differs from N,
   so an unknown selection is not reset and cycling starts from position 32. The game's list holds 32 at most.
4. `u3x_coop_spectate`: the "asked" flag (+0x5fd) is set even when `u3x_send_spoiler` sends nothing (a failed code
   check or allocation); it is never cleared here.
5. `u3x_spawner_occupied` always answers 0 (the PS3 test "a living character stands on the spawner" has no PS4
   counterpart); its native call is still made.
6. `u3x_coop_spectate` does not look at the results of `u3h_cam_fixed`, `u3x_menu_back`.
7. `u3x_native_npc_fight`: the host notes the damage (step 5) before it knows that the NPC has a health part; a
   guest applies only hits of at least the remaining health (the comment of the old code says the opposite; the
   behaviour is what counts); the chain walk can leave the bucket; a first-time attacker's sum is set, not added;
   the record search has no "in the game" test.
8. `u3x_native_npc_fight` reads its image base from the helper's state, not from a parameter (natives have none),
   and reads the context only to pass it on.
9. `u3x_hero_slot` answers 0 for a player that is not among the in-game records, but −1 for the fourth and later
   in-game ones.
10. `u3x_hero_kind`: the unloaded-art case of the spawn adds 0x10000 to `w09[6]`, the word that otherwise counts
    "no look" by ones; `w09[5]` is written in that case too. The lobby branch does not compare before copying.
11. `u3x_hero_desc` tests its pointers for null only, `u3x_hero_fix_all` with `ptr_ok`; the count limit of 1024
    exists only in `u3x_hero_fix_all`.
12. The results of `u3x_hero_kind` and of the C `w09_kind` are always 0 and unused.

## 7. What stays outside this spec

- C `natives_link` (stores the native's address; final sweep, see G33), the builder's two kind-248 stubs, C
  `k_kind`, the arrays `w09_d` / `w11_d`.
- The existing one-liners `u3x_hunter_count`, `u3x_hunter_id`, `u3x_hunter_index`, `u3x_pad_pressed`,
  `u3x_spawner_occupied`, `u3x_net_player_id`, and `u3x_fix_hero_look` (the helper's own rule, not game code).
- C `lg01_hold` / `lg01_slot0` (the lobby placement guard that `k_kind` runs after kind 248): in no work group, not
  described here.
- `u3h_cam_fixed` and `u3x_camera_tick`: G42.

## 8. Open questions

1. The meaning of the two descriptor words at image 0x16f0ae0 / 0x16f0ae8 is taken from how the game's own
   natives use them (passed to 0x11cc9d0; the second also steps the reader); the new code only reads and passes
   them, so nothing depends on the interpretation.
2. Byte 0xb of message 0x5e (value 1) and the allocator's two words {0x14, 0} are written as the game's own sender
   of the neighbouring message does; their meaning is not known beyond that.

## 9. Clarifications (tester, 2026-11-14)

Facts only, confirmed by the old-versus-new test (`reimpl2\G43\test_g43.cpp`) and, for the two points about the
width of arguments, by the machine code of the shipped v7.212 helper. Every statement of sections 4 to 6 that the
test checks holds; nothing above had to be corrected. "Read point" facts can only be seen with a stand-in that
changes the value inside the call named; the real functions behind those calls do not change it.

1. **The third code check of 3.3.** It is made on the 12 bytes at image 0x6d2e80 itself. The address stored in the
   table's slot is not looked at for the check: with a table whose slot +0x20 points at other code the message is
   still sent and that other code is called; with wrong bytes at 0x6d2e80 nothing is called even when the slot
   points at code that begins with the 12 bytes.
2. **The message's function table at call time (4.2, steps 5 and 6).** The table pointer is read from the message
   again after 0x733d90 returned (for the handler) and again after the handler returned (for the release); the
   reference count is read after the handler returned.
3. **The handler's further argument (4.2, step 5)** is a full 64-bit zero. The five zeros of 0x6ae7a0 (4.5, step
   11) are full 64-bit zeros too (two cleared registers, three 64-bit stack words).
4. **The record lookup of 4.2, step 3** happens after the allocator returned (a record changed inside the
   allocator call is the one that goes into the message) and all fields of 3.7 are complete before 0x733d90 is
   called. The old code writes the zero defaults of the three record fields before it reads the records; this
   order shows only when the message memory lies inside the matching net player record, which the game's allocator
   never returns.
5. **4.4, the cycle with an index of 32** (more than 32 AIs, the selection not among the first 32): right takes
   AI 33 (`u3x_hunter_id(base, 33)`; with N = 33 it wraps to AI 0), left takes AI 31; the selection is not reset
   and no camera kill is made.
6. **4.4, read points.** N and the screen number are read once at the start, before any call; the screen value
   read then is the one used for the pad and for the menu's user word. The selection, the "class picked" byte and
   the "asked" byte are read where their step needs them (after the calls before it).
7. **4.4, the result of `u3x_menu_open`.** The old code takes any non-zero result as "open" in the cycle test and
   only 0 as "closed" in the two other tests; with the contract of 2.2 (0 or 1) that is what 4.4 says. For a result
   of 2 the old code would cycle and the new code would do none of the three things.
8. **4.5, the two descriptor words** are read once per call, after 0x11cc560 returned and before the first
   argument is taken. Both name calls and both number steps use those values, also when the image words change
   during the name calls.
9. **4.5, results of game calls.** Only the low byte of a slot 0x3a8 result counts, only the low 32 bits of a slot
   0x78 result and of a 0x11cc9d0 result; of 0x996a60 (4.9) only the low byte.
10. **4.5, read points.** "Host" is decided before slot 0x78 is called; the health part pointer is read after the
    note of step 5; the net clock is read after 0x55cb20 returned; the function table of the NPC is read at each of
    its three slot calls; the hit record given to 0x6ae7a0 is the one 0x55cb20 filled (same memory, with what that
    function wrote).
11. **4.5, the end marker of step 4.** The shipped old code reads the end marker word when the bucket's own
    pointer is not null, before it walks; when the bucket is empty it is not read. The new code reads it only when
    the walk has found a node. The word is only read, so the result is the same; the difference is one memory
    read less in the case "bucket not empty, name not found".
12. **4.7, `*gi`.** It is written as soon as the table is not null: also for a negative number, a number equal
    to or above the count, a null records pointer and a record of another type. Only a null table leaves it.
13. **4.7, the look pointer** is read from the slot once in the new code; the old code reads it again after the
    repair. That shows only if the three look pointers themselves lie where a look is repaired.
14. **4.9, the frame test of the lobby branch.** F (A with bit 0 cleared) must pass `ptr_ok`: an A with bit 1 or
    bit 2 set, an F of 0, below 0x10000 or from 2^47 on gives `w09[0]` += 1 and nothing else (after the repair of
    all looks of step 2, which is made first in both branches). In the spawn branch an A with bit 1 or 2 set does
    nothing after step 2.
15. **4.9, read points.** The player's look byte of `w09[7]` and the comparison of the looks use the memory as it
    is after the repair; `w09[7]` is written before 0x996a60 is called, the other words after it.
16. **4.1, very long lists.** The old code multiplies the entry position by 0x10 in 32-bit arithmetic, the new code
    in 64-bit arithmetic. The results differ only for a list of more than 0x0fffffff entries (4 GB of entries),
    which no game data has.
