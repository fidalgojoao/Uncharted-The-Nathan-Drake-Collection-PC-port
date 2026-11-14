# G34. Party member list: rows, colours, click, focus; and the report popup

Behaviour description for a reimplementation (reimpl2 pipeline, describer's document). It says what the functions
do, not how the old ones were written. Offsets are hexadecimal when prefixed with 0x. "Image offset X" means the
address `base + X`, where `base` is the game image base every function receives. All multi-byte values are
little-endian; "u8 / u16 / u32 / u64 / s16 / s32" are unsigned / signed integers of that width, "pointer" is 8
bytes. Counters are plain u32 increments (they wrap). There is no float arithmetic in this group (one float
constant is stored as a bit pattern).

## 1. Purpose and callers

The multiplayer lobby shows a box "PARTY" with one row per party member. The PS4 game kept the box but not the code
behind it: its data source is an empty stub and its click handler reads a table that nothing fills. The helper owns
both. This group is that code:

- **the row model**: which members are shown and in which order (the host first, then the host's team, then the
  others), with an extra row for a member's second (split-screen) player;
- **the answers to the box's requests**: number of rows, selection, row text, rank badge, connection state, the
  row's team colour, and whether the box may take the focus;
- **the row click**: the quick-action menu of the clicked member (voice, promote, kick, statistics, report; on the
  own row lock / unlock party and statistics);
- **the focus keeper**: once per game tick it moves the menu focus between the page's left controls and the party
  box the way the PS3 game does (Right goes into the box, Left goes back);
- **the report popup**: the list of reasons shown after "Report Player".

Callers (all stay as they are):

| Function | Called by | When |
|---|---|---|
| `u3x_np_by_id` | the C++ animation packer of another group (the player an actor is attached to); C wrapper `np_by_id119` (in `agc_port.inc`), used by the C export `u3h_np_by_id` and by C code of other groups | in a match |
| `u3x_i01_party` | C wrapper `i01_party`, used by the C export `u3h_i01_party`, which `u3x_lobby_source` (C++, other group) calls for every request of the party box (source tag 0x4c414ead) after its own two special cases, while switch bit 16 of eb[1] is clear. Result 1 = answered; 0 = the caller answers itself (it does so for the title) | every request of the box |
| `u3x_i01_rows` | this group; C wrapper `i01_rows`, called by the C function `ox_party_tick` (which then reads the member list of 3.3) | list requests; rank fetch tick |
| `u3x_i01_self` | this group; C wrapper `i01_self`, called by `ox_party_tick` | as above |
| `u3x_i01_colour` | this group; C wrapper `i01_colour`, used by the C export `u3h_team_colour` (called by `u3x_m03_players`, C++, other group, with team 0 or 1) | row colours |
| `u3x_i01_click` | C wrapper `i01_click`, called by the C dispatcher `i_kind` (helper stub kind 221, which replaces the game's click handler of the party box, control id 0x3406420c) with the request the game's handler received, when that pointer is not null | a party row is clicked |
| `u3x_i01_focus` | `u3x_i01_party` (operation 6); C wrapper `i01_focus` (no caller left) | every frame while the box is shown |
| `u3x_i01_user_party` | C wrapper `i01_user_party`, called by the C function `lobby_focus` (its "first 120 frames" guard does not pull the focus back when the user went into the box) | every frame on the lobby page |
| `u3x_report_popup` (new) | C function `rpt_popup` (becomes a wrapper), used by the C export `u3h_report_popup`, which `u3x_menu_action` (C++, other group) calls for action 0x0ecbd5bd | "Report Player" chosen in a quick-action menu |
| `u3x_i01_type`, `u3x_i01_np_by_code`, `u3x_i01_strcmp`, `u3x_i01_row_colour`, `u3x_i01_canfocus`, `u3x_i01_leftctl` | this group only; their C wrappers (`i01_type`, ...) have no caller left | - |

Threading: everything runs on the game thread that runs the menu (data-source requests, the click stub, the menu
action). No thread is started, no lock is taken, nothing is atomic. No function of the group is re-entered: the
focus keeper sets a "busy" word around its one game call (5.4) so that the helper's other focus code stays out.

## 2. Interface

No function takes or returns a float. All are `extern "C"`. `base` = game image base; `s` = the helper's state
object (3.2); `ev` = a list request (3.5); `m` = a member slot (3.6) or the pseudo member (3.3).

The group is delivered in two independent parts. Each part calls the other only through the exported names below.

### 2.1 Part A: row model and request answers

Existing names; name and signature stay; the C wrappers already pass their arguments unchanged.

| Function | Signature | Result |
|---|---|---|
| net player by code | `uint8_t *u3x_np_by_id(uintptr_t base, uint32_t id)` | net player record or null (4.1) |
| the same, second name | `uint8_t *u3x_i01_np_by_code(uintptr_t base, uint32_t id)` | identical to `u3x_np_by_id` |
| "is this PC" (stray) | `int u3x_i01_self(uintptr_t base, uint8_t *m)` | 1 or 0 (4.2) |
| rebuild the rows | `void u3x_i01_rows(uintptr_t base)` | - (4.3) |
| shown game type (stray) | `int32_t u3x_i01_type(uintptr_t base, void *s)` | game type, -1 for none (4.4) |
| team colour | `uint32_t u3x_i01_colour(uintptr_t base, int32_t idx)` | colour word, 0 for a team outside 0..4 (4.5) |
| text compare (stray) | `int u3x_i01_strcmp(const uint8_t *a, const uint8_t *b)` | 0 = equal (4.6) |
| row colour | `void u3x_i01_row_colour(uintptr_t base, void *s, int32_t *event, uint8_t *m, int guest, uint32_t row)` | - (4.7) |
| the list source | `int u3x_i01_party(uintptr_t base, int32_t *event, void *s, const char *selfname)` | 1 answered, 0 not (4.8) |

`selfname`: the text shown for this PC's own row (the caller composes it, with the clan tag). `guest`: 1 for a
second-player row, else 0. `row`: the row index (only used in a diagnostic word).

File-private pieces of part A (free in shape): the rank badge answer (4.8, old private `rank_icon`), the colour
mask (4.5).

### 2.2 Part B: click, focus, report popup

| Function | Signature | Result |
|---|---|---|
| row click | `void u3x_i01_click(uintptr_t base, int32_t *ev)` | - (5.1) |
| control can take focus (stray) | `int u3x_i01_canfocus(uint8_t *c)` | 1 or 0 (5.2) |
| left control test | `int u3x_i01_leftctl(uint32_t pid, uint32_t id)` | 1 or 0 (5.3) |
| focus keeper | `void u3x_i01_focus(uintptr_t base, void *s)` | - (5.4) |
| user is in the party box (stray) | `int u3x_i01_user_party(void)` | 1 or 0 (5.5) |
| report popup (new name) | `void u3x_report_popup(uintptr_t base, void *s)` | - (5.6) |

The C function `rpt_popup(base, s)` becomes one statement that calls `u3x_report_popup(base, s)`.

### 2.3 Functions of this section that are not delivered again

| Function | Signature | Status |
|---|---|---|
| `u3x_i01_md` | `uint8_t *u3x_i01_md(uintptr_t base, uint8_t *m)` | stays (one game call): game function 0x7a3400 with (party session, m): the member's lobby data (3.6) or null. "Lobby data of m" below means this call; each mention is one call |
| `u3x_i01_ngi_push` | `int16_t u3x_i01_ngi_push(uintptr_t base)` | stays (one read): the s16 at NGM +0x661c |
| `u3x_i01_ngi_pop` | `void u3x_i01_ngi_pop(uintptr_t base, int16_t keep)` | stays (one write): stores `keep` into the s16 at NGM +0x661c |
| `u3x_i01_coop` | `int u3x_i01_coop(int32_t t)` | **to be removed** together with its unused C wrapper `i01_coop` and the C declaration (integrator). It holds a literal set of game type numbers; the new row colour asks `u3x_pm_coop` instead (4.7, section 8) |

"Bracketed" below means: the s16 at NGM +0x661c is read before the bracketed call (`u3x_i01_ngi_push`) and the
value read is written back after it (`u3x_i01_ngi_pop`). On PS4 the lobby's choice is that field itself, so the
bracket changes nothing unless the bracketed function changed the field; it is kept so that such a change is undone.

### 2.4 Functions of other groups, of the C part and of the shared tools that the new code calls

Not to be described or reimplemented here. `u3h_` are exports of the C part.

| Name | Signature | What it does | Part |
|---|---|---|---|
| `u3h_ext` | `uint8_t *u3h_ext(void)` | the helper's own state block (3.1); in CONVENTIONS.h, with `ext_eb()` | A, B |
| `u3h_i01_state` | `void *u3h_i01_state(void)` | address of the party list state (3.3), zero at start | A, B |
| `u3h_i05_state` | `void *u3h_i05_state(void)` | address of the statistics target (3.4) | B |
| `u3h_np_code` | `uint32_t u3h_np_code(uint8_t *np)` | pure read: the code of a net player record (the "net player id" of G12 3.1): low = (u32 at +0x1e4) AND 0xfff; when the u8 at +0x1ec is non-zero and low is not 0, low + 0x800; plus (s32 at +0x1e8) x 0x1000, modulo 2^32 | A |
| `u3h_ec_find_np` | `uint8_t *u3h_ec_find_np(uintptr_t base, const uint8_t *m, int32_t idx)` | G12 4.2: the first net player record whose key equals the member's and whose user number is `idx`, or null | A |
| `u3h_set_text` | `void u3h_set_text(int32_t *ev, const char *t)` | stores `t` as the request's text answer (pointer at byte 0x40) | A |
| `u3h_fmt_append` | `char *u3h_fmt_append(char *o, char *e, const char *t)` | copies the text `t` to `o` up to its NUL but never to `e` or beyond; returns the position after the last byte written (no NUL is written) | A |
| `u3h_player_profile` | `uint8_t *u3h_player_profile(uintptr_t base)` | pure read: the menu user's profile object, or null (G07 2.3) | A |
| `u3h_ox_rank_of` | `int32_t u3h_ox_rank_of(const char *nm)` | pure read: the rank word fetched from the account service for the player of that name, or -1 when there is none (yet) | A |
| `u3h_rank_icon` | `void u3h_rank_icon(uintptr_t base, void *s, int32_t *ev)` | turns a rank icon answer (word 2 = 0x29, word 6 = rank word) into the badge picture answer (other group; G22) | A |
| `u3h_gt_conn_state` | `int32_t u3h_gt_conn_state(uintptr_t base, int32_t id)` | pure read (G21 2.2): 2 when the connection entry with that id exists, is in use and has a peer link handle; else -1 | A |
| `u3h_clan_tag_poll` | `void u3h_clan_tag_poll(uintptr_t base, void *s)` | per-frame job of another group (clan tag keyboard) | A |
| `u3h_hud_alias_names` | `void u3h_hud_alias_names(uintptr_t base, void *s)` | per-frame job of another group | A |
| `u3h_stage_tick` | `void u3h_stage_tick(uintptr_t base, void *s)` | per-frame job of another group | A |
| `u3h_loadout_tick` | `void u3h_loadout_tick(uintptr_t base, void *s)` | per-frame job of another group | A |
| `u3x_netgame_cur` | `uint8_t *u3x_netgame_cur(uintptr_t base, void *s)` | existing C++ function: the definition record (0xd0 bytes) of the net game whose index is the s16 at NGM +0x661c, from script symbol 0x3b9a067d (one call of the state's symbol lookup); null when there is no table, the index is negative or not below the table's count, or the table has no entries | A |
| `u3x_netgame_type` | `int32_t u3x_netgame_type(uintptr_t base, void *s)` | existing C++ function: the s32 at +0xc of `u3x_netgame_cur(base, s)` (it calls it once more), 0 when that is null | A |
| `u3x_pm_coop` | `int u3x_pm_coop(uintptr_t base, void *s)` | existing C++ function of G30: non-zero when the net game at index NGM +0x661c exists and is of a co-op game type (one symbol lookup) | A |
| `u3x_session_host` | `int u3x_session_host(const uint8_t *s)` | existing C++ function: 1 when the session has no room (u64 at +0x18 is 0) or its own member id (+0x12e4) equals the host's (+0x12e8); else 0 | B |
| `u3x_quick_action_add`, `QuickActions` | see G18 2.2 | appends a (text id, action id) pair to a quick-action list (at most 8) | B |
| `u3h_fr_qa_push` | `void u3h_fr_qa_push(uintptr_t base, void *q)` | G18 2.2: shows a quick-action list (game function 0x6528d0 with (menu, list, 1)) unless it is empty | B |
| `u3h_pad_pressed` | `uint32_t u3h_pad_pressed(uintptr_t base, int32_t screen)` | pure read: the buttons newly pressed this frame on the pad of that screen, 0 when the game has no pad object | B |
| `u3h_active_page_object` | `uint8_t *u3h_active_page_object(uintptr_t base)` | pure read: the page object on top of the menu's page stack (pointer at menu +0x8b0 + 8 x depth, depth = u32 at menu +0xdb0), null when the depth is 160 or more; the slot itself may hold null | B |
| `u3x_sym_table` | `uint8_t *u3x_sym_table(void *s, uint32_t id)` | existing C++ function: the data of a script symbol, or null | B |
| `u3x_report_rows` | `uint32_t u3x_report_rows(const uint8_t *table)` | existing C++ function: 0 when `table` is null or its pointer at +8 is null; else the u32 at +0, but at most 8 | B |

Part A also calls `u3x_i01_focus` (part B); part B calls `u3x_i01_rows`, `u3x_i01_self` (part A) and `u3x_i01_md`.

## 3. Game and helper facts

### 3.1 The helper's state block (address from `u3h_ext()`)

| Word | Offset | Meaning |
|---|---|---|
| eb[1] | 0x4054 | switches; bit 17 (0x20000) set = the party box never takes the focus and the focus keeper does not run. (Bit 16 = the whole 1.19 party list off: tested by the caller, not here.) |
| rank counter | 0x31c0 | u32, counts rank badge answers of lists (shared with other lists) |

### 3.2 The state object `s`

| Offset | Type | Meaning |
|---|---|---|
| 708 | u32 | "own focus change" flag: non-zero while helper code is inside the game's focus setter. Set and cleared by 5.4; also read at its start |
| 768 | pointer to function | native 0, the game's "set the focused control of a page" (normally image 0x623660): arguments (page object, control object), no result used. May be null |
| 936 | pointer to function | native 21, "rank of a profile" (normally image 0x781ea0): argument (profile object), result u32 rank word. May be null |
| 0x3d78 | u32 | switches; bit 25 (0x2000000) set = no row colours |

### 3.3 The party list state (`u3h_i01_state()`, 0x280 bytes, "L" below; lives in the C part, same layout)

| Offset | Type | Name here | Meaning |
|---|---|---|---|
| 0x000 | u32 x 12 | d[0..11] | diagnostics: d[0] row rebuilds; d[1] rows of the last rebuild; d[2] colour answers; d[3] last colour {team byte, team used, net player team, row}; d[4] clicks; d[5] last click {row, pairs, own, LAN}; d[6] moves into the box; d[7] moves out of the box and profile-box hand-ons; d[8] colour requests without lobby data; d[9] last colour; d[10] corrections; d[11] last {focused id, buttons} |
| 0x030 | pointer x 10 | members | the members shown, in list order (member slots, or the pseudo member) |
| 0x080 | u32 | member count | |
| 0x084 | u32 x 20 | rows | per row: bits 0..7 = index into members, bit 8 (0x100) = the member's second player ("guest row") |
| 0x0d4 | u32 | row count | |
| 0x0d8 | s32 | selected row | |
| 0x0dc | 0x120 bytes | pseudo member | a member slot image that stands for this PC while there is no party |
| 0x1fc | 64 chars | name buffer | the composed text of a guest row |
| 0x23c | u32 | in-box | 1 = the user's focus belongs in the party box |
| 0x240 | u32 | alone | 1 = the page has no other control that can take the focus |
| 0x244 | u32 | page | id of the page the keeper last saw (0 at start) |
| 0x248 | u32 | popup | 1 = a popup was up at the last look |
| 0x24c | u32 | tick | game clock value of the last look |
| 0x250 | pointer | previous | the last focused control that was neither box |
| 0x258 | pointer | last | the control focused at the end of the last look |
| 0x260 | pointer | party box | the party box control of the page |
| 0x268 | u32 x 4 | remembered | ids of pages the user left while in the party box (0 = free) |
| 0x278 | u32 | remembered next | running write position for `remembered` (used modulo 4) |

The C function `ox_party_tick` reads members, member count and the pseudo member's address after a rebuild; the
layout must not change.

### 3.4 The statistics target (`u3h_i05_state()`, 0x25 bytes)

u8 at +0: whose statistics the next "Statistics" action shows: 0 unknown, 1 this PC's own row, 2 another player.
0x24 bytes at +1: the selected player id that belongs to that value. (Read by the statistics page code, other
group.)

### 3.5 The list request `ev`

The structure of G19 3.3 (array of 32-bit words; "word n" is at byte 4 x n). Used here:

| Word | Meaning |
|---|---|
| 0 | operation: 1 title, 2 row text, 4 row image, 5 get selection, 6 get last row index, 8 "is the list shown", 9 refresh, 10 row selected, 11 value query, 20 row colour, 21 "can the list take the focus" |
| 1 | row index (in); the answer of operations 5, 6, 8, 21 (out) |
| 2, 4 .. 12 | image answer. "The image answer of kind K with value V" is that of G19 3.3: word 2 = K, words 4 and 5 = 0, word 6 = V, words 7 and 8 = 0, word 9 = -1, word 10 = 0, word 11 = -1, word 12 = 0x3f800000 (1.0); word 3 stays |
| 0xe | the answer of a value query (operation 11) |
| 0xf | the answer of operation 20: a colour word |
| 0x13 | which value operation 11 asks: 0 = "is the row valid", 1 = connection state |
| 0x14 | image slot of operation 4; 0xf5a294f3 = the rank badge column |
| 0x15 | column of operation 2; 0 = the name column |
| 0x16 | which colour operation 20 asks; 2 = the row's team tint |
| pointer at byte 0x40 | the text answer |

For the click (5.1) only word 1, the clicked row (s32), is read.

A colour word: bits 0..23 the colour as the game's function 0x7418e0 delivers it, top byte 0x80 for every colour
this group answers. "Shown colour of c" = (c AND 0xffffff) OR 0x80000000.

### 3.6 Game data

| Image offset | Type | Meaning |
|---|---|---|
| 0x17b4ba4 | u32 | the game clock (0x17b4a90 + 0x114), one value per game tick |
| 0x17bd080 | object | the menu: +0x898 pointer to the popup that is up (null = none; its u32 at +0x10 is its page id); +0x8b0 the page stack and +0xdb0 its depth (through `u3h_active_page_object`) |
| 0x17c3570 | u8 | "party locked" setting |
| 0x17c3871 | 0x24 bytes | the selected player's id (G18 3.7) |
| 0x17c3898 | u64 | written as 0 by the click (G18 3.7) |
| 0x17cb060 | object | the net game manager ("NGM"): +0x661c s16 net game index; net player record i (i = 0..9) at +0x390 + i x 0x9c0 |
| 0x17d5c58 | object | the net state ("NET"): +8 u8 non-zero = LAN (image 0x17d5c60); +0x3e4 u32 the net menu mode (image 0x17d603c), 2 = custom game; +0x408 s32 (image 0x17d6060) |
| 0x17d6420 | object | the match session |
| 0x17dc298 | object | the party session ("SES") |

A session (G08 3.2): +0x18 u64 room id (0 = none); member slot i (i = 0..9) at +0x680 + i x 0x120; +0x12e4 u32 own
member id; +0x12e8 u32 the host's member id.

A member slot: +0 the name / id (NUL-terminated text in 0x24 bytes); +0x25 u8 "in use"; +0x2c u32 member id; +0x34
s32 connection id (-1 = none); +0x3c u32 length of the member data; +0x40 the member data. In the member data of
length 0x40: u8 at slot +0x72 (bit 3, value 8, used in 4.8) and u32 at slot +0x7c, the member's rank word with bit
29 (0x20000000) as "rank present".

A member's lobby data (result of game function 0x7a3400): u8 at +9 the team number (0 = no team, n = team index
n - 1); 0x24 bytes at +0x19 the id of the member's second player (first byte non-zero = there is one).

A net player record (G12 3.1): u32 at +0x1e4 id word; s32 at +0x1e8 user number; s32 at +0x21c team index (-1 =
none).

A menu page object: u8 at +8 non-zero = the page is active; u32 at +0x10 page id; u32 at +0xac number of controls;
pointer at +0xb0 to that many control pointers (entries may be null). A control object: pointer at +0 its function
table; u8 at +8 non-zero = it has the focus; u32 at +0x10 control id. Function table entry at byte 0x40 (entry 8):
"can take the focus", called with the control as the only argument; the low 8 bits of the result non-zero = yes.

The report reasons: script symbol 0xffac56f2 (through `u3x_sym_table`): u32 number of entries at +0, pointer to the
entries at +8; an entry is 0x10 bytes: u32 text id of the reason's label at +0, pointer to the reason's text at +8.
The game holds the table; nothing of it is in the helper.

### 3.7 Game functions called

Normal calling convention, through `game_call`.

| Image offset | Arguments | Result | Meaning |
|---|---|---|---|
| 0x7a3400 | (pointer: session, pointer: member slot) | pointer or null | the member's lobby data (only through `u3x_i01_md`, with the party session) |
| 0x7418e0 | (pointer: a 0x20-byte result record, pointer: NGM, s32 team index, s32 0) | ignored | writes the team's colours of the net game at index NGM +0x661c; the u32 at +0xc of the record is the colour used here |
| 0x7a2b10 | (pointer: SES) | s32 | number of players in the party |
| 0x7210a0 | (pointer: NGM) | s32 | number of teams of the net game at index NGM +0x661c |
| 0x6528d0 | (pointer: the menu, pointer: a `QuickActions` list, s32 1) | ignored | shows a quick-action menu (directly only in 5.6) |

Also called through pointers: natives 0 and 21 (3.2), the controls' "can take the focus" entry (3.6), the state's
symbol lookup (inside `u3x_netgame_cur`, `u3x_netgame_type`, `u3x_pm_coop`, `u3x_sym_table`).

### 3.8 Ids

Controls: 0x3406420c the party box; 0x649e809a the profile box. Pages and their left controls (5.3):

| Page id | Left control ids |
|---|---|
| 0x53cbb5f2 | 0xf75ffe57 (the main menu list) |
| 0xb99a3235 (the lobby) | 0x70e80076, 0xef031de1, 0x9247257a, 0x382fa1fd, 0x002b2bd6 |

Quick-action page: 0xecc3219d. Quick-action pairs (text id, action id):

| Entry | Text id | Action id |
|---|---|---|
| Voice | 0xffbbac12 | 0x6547d63c |
| Promote to leader | 0x9fdf8887 | 0x2d32c8b0 |
| Kick | 0x1e2955af | 0x7cc46b94 |
| Statistics | 0x2962edc6 | 0x089143ea |
| Report Player | 0xc5790997 | 0x0ecbd5bd |
| Unlock Party | 0x6253c96a | 0x0d0c91cd |
| Lock Party | 0xa095f551 | 0xccbf832e |
| report reason k (k = 0..7) | the label id of reason k | 0x7ebae89c + k |

Pad bits (`u3h_pad_pressed`): Right = 0x20 or 0x1000000; Left = 0x80 or 0x800000.

Game types used as numbers (4.7): 0, 1, 10, 18, 19. Texts: the guest suffix `" (1)"` (space, parenthesis, digit
one, parenthesis); an empty text. Both are constants of the new code (the empty text must be a real address that
stays valid).

## 4. Behaviour, part A

### 4.1 `u3x_np_by_id(base, id)` and `u3x_i01_np_by_code(base, id)`

Null when `id` is 0. Otherwise the first of the ten net player records, in index order, whose code
(`u3h_np_code`) equals `id`; null when none does. No other effect.

### 4.2 `u3x_i01_self(base, m)`

1 when the u32 at m +0x2c equals the party session's own member id (SES +0x12e4), else 0. `m` is not checked.

### 4.3 `u3x_i01_rows(base)`: rebuild the rows

1. d[0] is incremented. The member list and the row list are built anew (their old contents beyond the new counts
   stay in memory, unread).
2. **No party** (the u64 at SES +0x18 is 0): the pseudo member is cleared to 0x120 zero bytes, its u32 at +0x2c
   becomes SES's own member id, its s32 at +0x34 becomes -1. The member list is that one pseudo member.
3. **Party**: the member slots 0..9 that are in use are taken in slot order. For each one, in this order: its lobby
   data is asked (one call); its team value is the lobby data's team number byte (+9, unsigned), or 0 without
   lobby data; then `u3h_ec_find_np(base, slot, 0)` is called, and when it returns a record whose team (s32 at
   +0x21c) is not -1, that value replaces the team value (the two scales are mixed as they are: the lobby byte is
   "index + 1", the net player's value is the index). A slot is "the host" when its member id (+0x2c) equals the
   host's member id (SES +0x12e8). The host's team = the team value of the last slot that is the host; 0 when no
   slot is.
   The member list is: the host slot(s); then the other slots whose team value equals the host's team; then the
   rest. Inside each of the three groups the slot order is kept.
4. **Rows**: the members are gone through in list order, as long as fewer than 20 rows exist. Each gets a row
   holding its list index. Then its lobby data is asked again (one call, also for the pseudo member); when there is
   lobby data, its byte +0x19 is non-zero and fewer than 20 rows exist, a second row follows: the same index with
   bit 8 set (the guest row).
5. d[1] = the row count.

Calls made, in order: for a party, per in-use slot `u3x_i01_md` then `u3h_ec_find_np`; then per listed member one
`u3x_i01_md`.

### 4.4 `u3x_i01_type(base, s)`: the shown game type

Bracketed (2.3): when `u3x_netgame_cur(base, s)` is not null the result is `u3x_netgame_type(base, s)`, else -1.
(Read index, the one or two calls, write index back.)

### 4.5 `u3x_i01_colour(base, idx)`: a team's colour

0 when `idx` is below 0 or above 4, with no call. Otherwise a 0x20-byte record is zeroed, game function 0x7418e0
(record, NGM, idx, 0) is called bracketed (2.3), and the result is the u32 at +0xc of the record, unchanged (the
callers apply the "shown colour" mask).

### 4.6 `u3x_i01_strcmp(a, b)`

Compares two NUL-terminated texts byte by byte (bytes as unsigned, no length limit). 0 when they are equal;
otherwise (byte of a) - (byte of b) at the first position where they differ (the end of one text counts as byte 0).

### 4.7 `u3x_i01_row_colour(base, s, event, m, guest, row)`: the answer of operation 20

Steps in order; "stop" ends the function.

1. When the net menu mode (u32 at image 0x17d603c) is not 2: the text answer becomes the empty text and word 1
   becomes 0; stop.
2. T = `u3x_i01_type(base, s)` (4.4).
3. When `u3x_pm_coop(base, s)` is non-zero: game function 0x7a2b10 (SES) is called, and when its result is below 2,
   stop. (0x7a2b10 is not called here when the type is not co-op.)
4. When word 0x16 is not 2: stop.
5. D = the lobby data of `m` (one call). Null: d[8] is incremented; stop.
6. The colour starts as 0x80000000 and the team as "none". When D's team number byte (+9) is not 0:
   - team = that byte - 1;
   - only for a guest row: game function 0x7a2b10 (SES) is called (again); when its result is exactly 2, the number
     of teams is read with game function 0x7210a0 (NGM), bracketed; when that is exactly 2 and T is negative or
     one of 0, 1, 19, 18, 10, the team becomes the other one (0 becomes 1, anything else becomes 0);
   - colour = shown colour of `u3x_i01_colour(base, team)`.
7. Word 0xf = the colour; d[2] is incremented.
8. The member's own net player overrides it once the match knows its team. Unless `m` is the pseudo member of L,
   the match session's member slots 0..9 are searched in order for the first one that is in use and whose name
   equals `m`'s name (`u3x_i01_strcmp` of the two slots gives 0). When there is one:
   - P = `u3x_i01_np_by_code(base, its member id (u32 at +0x2c))`;
   - for a guest row with P not null, P is looked up again: `u3x_i01_np_by_code(base, C)` with C = (u32 at P
     +0x1e4 AND 0xfff), plus 0x1000 when the s32 at P +0x1e8 is not 0;
   - when P is not null and its team (s32 at +0x21c) is 0 or more: word 0xf = shown colour of
     `u3x_i01_colour(base, that team)`, and "net player team" = that team.
9. d[3] = (D's team number byte) OR ((team AND 0xff) shifted left 8) OR ((net player team AND 0xff) shifted left
   16) OR (`row` shifted left 24), where "team" is 0xff when none and "net player team" is 0xff when step 8 found
   none. d[9] = word 0xf.

The new code does not hold the list of co-op game types: step 3 asks `u3x_pm_coop`, which reads the same net game
entry as step 2 (the bracket of step 2 has put the index back). Compared with the old code this is one more
read-only symbol lookup per request (accepted deviation, section 8).

### 4.8 `u3x_i01_party(base, event, s, selfname)`: the list source

"Focus on" = bit 17 of eb[1] is clear. op = word 0.

First, when op is 9 or 6: the rows are rebuilt (4.3). Then:

| op | Effect | Result |
|---|---|---|
| 6 | word 1 = row count - 1 (so -1 for no rows). Then, in this order: `u3h_clan_tag_poll(base, s)`, `u3h_hud_alias_names(base, s)`, `u3h_stage_tick(base, s)`, `u3h_loadout_tick(base, s)`, and, when focus is on, `u3x_i01_focus(base, s)` | 1 |
| 5 | word 1 = the selected row of L | 1 |
| 10 | the selected row of L = word 1 (no range check) | 1 |
| 21 | word 1 = 1 when focus is on and (in-box or alone of L is non-zero), else 0 | 1 |
| 8 | word 1 = 1 when focus is on, else 0 | 1 |
| 1 | nothing | 0 |

Every other op (2, 4, 11, 20, 9 and unknown ones) is a row request:

1. When the row count is 0, the rows are rebuilt (again, for op 9).
2. r = word 1 as unsigned. When r is not below the row count, or the member index of row r is not below the member
   count: result 1, nothing else.
3. `m` = the member of row r, guest = bit 8 of the row, own = `u3x_i01_self(base, m)`.
4. By op; the result is always 1:
   - **2 (text)**: the name is `selfname` for an own row, else the member's name (slot +0). For a guest row the
     name buffer of L is filled with that name followed by `" (1)"`, both through `u3h_fmt_append` with the limit
     at buffer + 60, then a NUL, and the buffer is the text. Afterwards, when word 0x15 is not 0, the text is the
     empty text instead (the buffer has been written all the same). The text answer is stored with `u3h_set_text`.
   - **4 (image)**: nothing for a guest row or when word 0x14 is not 0xf5a294f3. Otherwise a rank word R is
     looked for:
     - own row: native 21 is read from the state, then `u3h_player_profile(base)` is called; when both are not
       null, R = native 21 (profile); else no answer;
     - another member: when `u3h_ox_rank_of(name of m)` is 0 or more, R = that value (the function is a pure
       read; it may be asked once or twice); else, when the slot's member data length (u32 at +0x3c) is 0x40 and
       the u32 W at slot +0x7c has bit 29 set: R = W AND 0xffff when bit 3 of the u8 at slot +0x72 is set, else
       R = W with bit 29 cleared; else no answer.
     With an R: the request becomes the image answer of kind 0x29 with value R (3.5), the rank counter (3.1) is
     incremented, and `u3h_rank_icon(base, s, event)` is called.
   - **11 (value)**: word 0x13 = 0: word 0xe = 1. Word 0x13 = 1 and the slot's connection id (s32 at +0x34) is
     not -1: word 0xe = `u3h_gt_conn_state(base, that id)`. Anything else: no answer.
   - **20 (colour)**: unless bit 25 of the switches at state +0x3d78 is set, `u3x_i01_row_colour(base, s, event,
     m, guest, r)`.
   - others (9 included): nothing.

## 5. Behaviour, part B

### 5.1 `u3x_i01_click(base, ev)`: a party row clicked

1. The statistics target's "whose" byte (3.4) becomes 0; d[4] is incremented; the rows are rebuilt
   (`u3x_i01_rows`); the u64 at image 0x17c3898 becomes 0. All four in every case.
2. row = word 1 (s32). Nothing more when row is negative, not below the row count, or its member index is not
   below the member count.
3. `m` = the member of the row, guest = bit 8 of the row. The selected player's id (image 0x17c3871, 0x24 bytes):
   for a member row, the first 0x24 bytes of the slot; for a guest row, the 0x24 bytes at +0x19 of the member's
   lobby data (one `u3x_i01_md` call); when that lobby data is null the id is left as it was.
4. LAN = the u8 at NET +8 is non-zero. own = `u3x_i01_self(base, m)`.
5. The statistics target: "whose" = 1 for an own member row (own and not guest), else 2; its 0x24 id bytes = the
   bytes now at image 0x17c3871.
6. A quick-action list: argument 0, page 0xecc3219d, pairs in this order (3.8):
   - another member's row (own = 0, guest rows of other members included):
     - **Voice**;
     - when `u3x_session_host(SES)`: **Promote to leader** when not LAN and not a guest row; then **Kick** when the
       s32 at NET +0x408 is 0;
     - **Statistics**;
     - **Report Player** when not LAN.
   - own row (own = 1, this PC's guest row included):
     - when `u3x_session_host(SES)` or the party has exactly one member (no room: counts as one; else the number
       of member slots in use): **Unlock Party** when the u8 at image 0x17c3570 is non-zero, else **Lock Party**;
     - **Statistics**.
7. d[5] = row OR (number of pairs shifted left 8) OR (own shifted left 16) OR (LAN shifted left 24).
8. `u3h_fr_qa_push(base, list)`.

### 5.2 `u3x_i01_canfocus(c)`

0 for a null `c`. Otherwise the control's "can take the focus" entry (3.6) is called with `c`; 1 when the low 8
bits of the result are non-zero, else 0.

### 5.3 `u3x_i01_leftctl(pid, id)`

1 when control `id` is listed for page `pid` in the table of 3.8, else 0 (any other page: 0).

### 5.4 `u3x_i01_focus(base, s)`: the focus keeper

Names: BOX = the control with id 0x3406420c, PROFILE = the control with id 0x649e809a. Steps in order; "stop" ends
the function, and everything written before it stays.

1. page = `u3h_active_page_object(base)`; F = native 0. Stop when the page is null, F is null, or the u32 at state
   +708 is non-zero.
2. Once per tick: stop when the game clock (u32 at image 0x17b4ba4) equals L's tick; else L's tick = the clock.
3. **Page change**: pid = the page's id. When it differs from L's page:
   - when L's page is not 0 and L's last control is not null and equals L's party box (the user left the old page
     while in the box): the old page id is stored in `remembered` at position (remembered next AND 3), and
     remembered next is incremented;
   - L's page = pid; last, previous and party box become null; popup, in-box and alone become 0;
   - every `remembered` entry equal to pid is set to 0, and when there was at least one, in-box = 1. (A page id of
     0 matches the free entries.)
4. **Popup**: when the menu's popup pointer (menu +0x898) is not null and the popup's id (u32 at +0x10) is not 0:
   L's popup = 1; stop. Otherwise L's popup = 0.
5. n = the page's control count, list = its control pointers. Stop when the list pointer is null or n is above 64.
6. One pass over the n controls, skipping null entries: the last one with id 0x3406420c is BOX; the last one with
   id 0x649e809a is PROFILE; the last one whose focus byte (+8) is non-zero is "focused" (null when none).
7. Stop when there is no BOX or the page is not active (u8 at page +8 is 0). Otherwise L's party box = BOX.
8. "first" = the first control in list order that is not null, is neither BOX nor PROFILE, has a non-zero id and
   can take the focus (5.2; asked only for controls that pass the other tests, in order, until one says yes);
   null when there is none. L's alone = 1 when there is none, else 0.
9. "previous" = L's previous control when it is still in the list and can take the focus (5.2 is asked once for
   every list position that holds it); else null.
10. pressed = `u3h_pad_pressed(base, 0)`. d[11] = (the focused control's id AND 0xffff, or 0xffff when nothing is
    focused) OR (pressed shifted left 16).
11. "back" = previous when not null, else first. One of these cases, the first that applies, picks the target:
    - **into the box**: something is focused that is neither BOX nor PROFILE, a Right bit is in `pressed`, and
      `u3x_i01_leftctl(pid, the focused control's id)` is 1: in-box = 1; target = BOX; d[6] is incremented;
    - **out of the box**: BOX is focused, first is not null and a Left bit is in `pressed`: in-box = 0; target =
      back; d[7] is incremented;
    - **the profile box never keeps the focus**: PROFILE is focused: when first is not null, in-box = 0 and target
      = back; otherwise target = BOX; d[7] is incremented in both cases;
    - **correction**: BOX is focused, in-box is 0 and first is not null (the game put the focus there by itself):
      target = back; d[10] is incremented;
    - otherwise no target.
12. When there is a target and it is not the focused control: the u32 at state +708 = 1; F (page, target) is
    called; the u32 at state +708 = 0; "focused" becomes the target, and then, when after the call some control of
    the list has its focus byte set, the last such control in list order.
13. L's last = focused. When focused is not null and neither BOX nor PROFILE: L's previous = focused.

### 5.5 `u3x_i01_user_party()`

1 when L's in-box is non-zero and L's last control is not null and equals L's party box; else 0.

### 5.6 `u3x_report_popup(base, s)`

T = `u3x_sym_table(s, 0xffac56f2)`; n = `u3x_report_rows(T)`. Nothing when n is 0. Otherwise a quick-action list is
built with argument 0, page 0xecc3219d and n pairs: pair k (k = 0 .. n-1) = (the label id of reason k: the u32 at
+0 of entry k of T, 3.6; action 0x7ebae89c + k). It is shown with game function 0x6528d0 (menu = address of image
0x17bd080, list, 1) directly, not through `u3h_fr_qa_push`.

What happens when a reason is chosen is not part of this group: the menu action 0x7ebae89c + k reaches the C
export `void u3h_report_pick(uintptr_t base, void *s, uint32_t row)`, which queues the report for the account
service with the configured value.

## 6. Test scenarios

Everything in this group can be tested old against new on a fake image: one zeroed memory block as the image
(sessions, NGM with ten net player records, NET, menu, selected id), a fake state object with recording stand-ins
as natives 0 and 21, a fake page with control objects whose function tables hold a recording "can take the focus"
stand-in, recording stubs for game functions 0x7a3400, 0x7418e0, 0x7a2b10, 0x7210a0, 0x6528d0, and recording stubs
(or the real functions) for the `u3h_` / `u3x_` functions of 2.4. Compare results, all memory (L, the statistics
target, the request, the image) and the order and arguments of the calls. `u3x_pm_coop` has no counterpart call in
the old code: use the real `u3x_pm_coop` (or a stub that gives the answer of the old code's own test for the type
of the entry `u3x_netgame_cur` returns; the tester sees that test) and leave its call and its symbol lookup out of
the call comparison. Nothing needs the live game for
correctness; only the look (colours on screen, the feel of the focus moves, the guest row, which needs a second
local player) can be judged there.

Part A:

1. 4.1: id 0; an id that matches record 0, record 9, none; two records with the same code (the first wins).
2. 4.3, no party: pseudo member content, one member, one row; lobby data for the pseudo member with byte 0x19 set
   (two rows) and null.
3. 4.3, party: no slot in use (0 members, 0 rows); host in slot 3 with members before and after it; teams from
   lobby data only; a net player team that overrides (including team 0 and a value of -2); net player team -1
   (lobby value kept); no slot is the host (host team 0); two slots with the host's id; lobby data null for some
   slots; ten members each with a guest (20 rows); call order.
4. 4.4: no entry (-1, one call), an entry (two calls), the index field changed by the stub (written back).
5. 4.5: idx -1, 0, 4, 5; the stub changing the index field; the colour passed through unmasked.
6. 4.6: equal, different, prefix either way, empty texts, bytes above 0x7f.
7. 4.7: net mode not 2 (text and word 1); co-op with 1 player (stop) and with 2; not co-op (0x7a2b10 not called);
   word 0x16 not 2; lobby data null (d[8]); team byte 0 (colour 0x80000000, team 0xff in d[3]); team byte 1..5 and
   6 (colour 0x80000000 from the out-of-range team); guest row with 2 players and 2 teams for T = -1, 0, 1, 19,
   18, 10 (swap, from team 0 and from team 3) and T = 2 (no swap); guest row with 3 players or 3 teams; pseudo
   member (no match search); match member found by name with net player team 0, 3, -1 and no net player; guest row
   with the second lookup finding a record and finding none; the name only in a slot that is not in use; d[3], d[9].
8. 4.8: op 6 with 0, 1 and 3 rows, focus on and off (the four jobs and the keeper call, order); op 5, 10, 21 (all
   four combinations of in-box / alone, focus off), 8, 1; op 9 (rebuild once, twice when no rows); a row index
   equal to the row count, a row whose member index is too high (prepared state with a stub rebuild); op 2 own /
   other / guest / column 1 / a 70-character name on a guest row (cut at 60); op 4: guest row, other slot id, own
   row with native or profile missing, account-service rank present, member data rank with bit 29 set and clear,
   with and without bit 3 at +0x72, data length not 0x40; op 11 with word 0x13 = 0, 1 (connection id -1 and not),
   2; op 20 with the switch set and clear; an unknown op.

Part B:

9. 5.1: row -1, row = count; a member row of another player as host / not host, LAN / online, NET +0x408 zero /
   non-zero; a guest row of another member (no Promote; id from the lobby data; lobby data null: id unchanged);
   own row as host, alone without a room, as a non-host member of a party of two (no lock entry), locked /
   unlocked; own guest row (target 2, own-row entries); d[4], d[5], image 0x17c3898, the statistics target.
10. 5.2: null; result 0, 1, 0x100 (counts as no), 0x101.
11. 5.3: every listed pair, a listed control on the wrong page, an unknown page.
12. 5.4: page null; native null; busy word set; same tick twice; first look (page 0 to a page); page change with
    and without "left in the box", four and five remembered pages (the fifth overwrites the first), return to a
    remembered page (in-box 1, entry freed), change to page id 0; popup up (id non-zero), popup pointer with id 0;
    control list null, 65 controls; no BOX; page not active; no other focusable control (alone 1); previous still
    on the page / gone / no longer focusable; each of the four target cases and none; Right on a control that is
    not a left control; Left in the box without a "first"; PROFILE focused with and without a "first"; target
    equal to the focused control (no call); after the call the focus byte on the target, on another control, on
    none; previous not updated when the box ends focused; d[6], d[7], d[10], d[11].
13. 5.5: the three conditions singly and together.
14. 5.6: table null; entries pointer null; count 0, 1, 8, 9 (8 pairs); the list contents and the direct call.

## 7. Oddities of the old code that are behaviour to keep

1. 4.3: the team value mixes two scales (lobby byte = index + 1, net player team = index); the grouping compares
   them as they are. The net player's team replaces the lobby value for any value other than -1 (also other
   negative values), while 4.7 step 8 takes it only when it is 0 or more.
2. 4.3: the lobby data of every member is asked twice per rebuild (once for the team, once for the guest row), and
   the rebuild runs on every operation 6 and 9, on every click, and from `ox_party_tick`.
3. 4.3: the limit of 20 rows cannot be reached with ten member slots; it is kept.
4. 4.7 step 1: word 1 of the request, which is the row index on input, is overwritten with 0.
5. 4.7 step 6: game function 0x7a2b10 is called again for a guest row although step 3 may have called it. A team
   number byte above 5 gives the colour 0x80000000 (the team colour is 0 outside 0..4) and still counts as an
   answer. The swap turns any team other than 0 into team 0.
6. 4.7 step 8: for a guest row the second lookup builds its code from the first player's own id and adds 0x1000
   only when that first player's user number is not 0. For a first player with user number 0 it therefore finds
   the first player again (or nothing, when that record carries the +0x800 marker). It looks like it was meant to
   find the second player; it is kept. Guest rows need a second local player, which PS4 does not offer, so the
   guest branches of 4.3, 4.7, 4.8 and 5.1 are not reached in practice.
7. 4.7: the first colour (step 6, with its call of 0x7418e0) is computed and stored even when step 8 replaces it.
8. 4.8: operation 9 on an empty list rebuilds twice; after the rebuild it runs the row checks of a row request
   without any effect. Operation 10 stores any value as the selection. Operation 2 fills the name buffer for a
   guest row even when the column is not 0.
9. 4.8, operation 4, own row: `u3h_player_profile` is called even when native 21 is null.
10. 5.1: an invalid row still clears the statistics target's byte and image 0x17c3898, counts the click and
    rebuilds the rows. A guest row without lobby data shows the menu for whatever id was selected before, and
    that id goes into the statistics target.
11. 5.4 step 3: a change to a page with id 0 treats every free `remembered` entry as a match (in-box becomes 1).
    The remembered-next counter is never reset.
12. 5.4 steps 4 to 7: the stops after step 2 leave the tick stored, so the keeper does not run again in that tick.
13. 5.4 step 9: the "can take the focus" entry of the previous control is called once per list position that
    holds it (normally one).
14. The diagnostic words d[6], d[7] carry other meanings in the C part's old comment (written for an earlier
    version of the keeper); the meanings of 3.3 are the ones the code has.

## 8. Strays, tables, and what stays as it is

Strays (in no work group of the inventory), covered here:

- `u3x_i01_self` (4.2), `u3x_i01_type` with its private part `shown_type` (4.4), `u3x_i01_strcmp` (4.6),
  `u3x_i01_canfocus` with `can_focus` (5.2), `u3x_i01_user_party` (5.5), the private `shown_colour` (3.5);
- the private `rank_icon` of the party list (4.8, operation 4). The inventory lists this function under G37 by
  its name; it belongs here (its only caller is the list source);
- `u3x_i01_coop`: removed (2.3).

Named but not delivered: `u3x_i01_md`, `u3x_i01_ngi_push`, `u3x_i01_ngi_pop` (one-line functions, 2.3);
`u3x_netgame_cur`, `u3x_netgame_type`, `u3x_report_rows` (and its sibling `u3x_report_reason`): exported C++
functions that are in none of the inventory's lists; they are called as they are and are left to the final sweep.

Tables of game values:

- **The co-op game types** (eight type numbers, PS3 function f_310c68, old `u3x_i01_coop`): not copied. The new
  code asks `u3x_pm_coop(base, s)` (G30), which answers the same question for the same net game entry. Cost: one
  more read-only symbol lookup per colour request than the old code made. Where that set finally comes from is for
  G30's describer to settle.
- **The free-for-all test of 4.7 step 6** (T negative, 0, 1 or 19) and the two extra types 18 and 10: given as
  numbers, as other specs give single game types. See open question 2.
- **The left controls** (3.8): the helper's own choice of which controls of two pages count as "left of the party
  box"; ids of the game's menu, not game values.
- **The report reasons**: read from the game's script symbol 0xffac56f2 at run time (3.6).

What stays as it is: the C state blocks behind `u3h_i01_state` and `u3h_i05_state` and every C wrapper (`i01_*`);
the C callers `i_kind`, `ox_party_tick`, `lobby_focus`, `u3h_team_colour`, `u3h_i01_party`, `u3h_report_popup`;
`u3h_report_pick` and the report queue behind it; `u3h_fr_qa_push`, `u3x_quick_action_add`, `QuickActions`;
`u3x_session_host`; every function of 2.4.

## 9. Open questions

1. 4.7 step 8, guest rows (oddity 6): the code of the second lookup looks wrong. Kept as it is; a fix would be a
   behaviour change and cannot be seen on PS4 without a second local player.
2. The old notes say the PS4 game has its own function for the free-for-all test (image 0x729b10, argument NGM,
   the counterpart of PS3 f_31089c). The old code does not call it here but tests the type numbers itself, and
   treats "no net game" (T = -1) as free-for-all. Whether 0x729b10 gives the same answers in all these cases was
   not checked; the spec keeps the numbers.
3. `u3x_pm_coop` in place of the old inline test (section 8): is the one extra read-only lookup per request
   accepted, and does G30 keep `u3x_pm_coop` with this meaning?
4. The meaning of the s32 at NET +0x408 (a non-zero value hides Kick, 5.1; G18 4.13 tests the same field) is not
   known here.

## Clarifications

Added by the tester (2026-11-14). Facts only, confirmed by the old-versus-new test (`reimpl2\G34\test_g34.cpp`),
where the text above is silent or can be read two ways. Every statement of sections 4 to 7 that the test checks
against v7.212 holds; nothing above had to be corrected. "Read point" facts (items 1 to 3) can only be seen with a
stand-in that changes the value inside the call named; the real functions behind those calls do not change it.

1. 4.8, "focus on". v7.212 reads switch bit 17 of eb[1] once, on entry to `u3x_i01_party`, before the rebuild of
   operations 6 and 9 and before the four per-frame jobs; that one value decides the keeper call of operation 6
   and the answers of operations 21 and 8. A read after the jobs gives the same unless the rebuild's calls or one
   of the four jobs change the bit during the request.
2. 4.3 step 3, "the host". v7.212 reads the host's member id (SES +0x12e8) again for every in-use slot, after that
   slot's two calls (lobby data, `u3h_ec_find_np`). One read before the first slot gives the same unless one of
   those calls changes the host's member id.
3. 4.7 step 9, d[3]. The low byte is D's team number byte as it is at that moment: v7.212 reads it again after
   the calls of steps 6 and 8. The value read in step 6 gives the same unless one of those calls changes the
   lobby data.
4. 4.8, operation 4, another member. v7.212 asks `u3h_ox_rank_of` twice when the first answer is 0 or more and
   uses the second answer as R; once when the first answer is negative.
5. 4.3. v7.212 sets the member count and the row count of L to 0 at the start of a rebuild (before d[0] is
   incremented) and lets them grow entry by entry, so a function called during the rebuild would find the lists
   half built. Counts written once at the end give the same final state; none of the called functions reads L.
6. 5.1 and 5.6, the quick-action list. v7.212 sets the argument, the page, the count and the pairs it adds; the
   other pair words of the list are whatever was in memory. The game function and `u3h_fr_qa_push` read `count`
   pairs.
7. 5.1 step 3. The second player's id is the 0x24 bytes at +0x19 .. +0x3c of the lobby data, the last byte
   included.
8. 4.7. Every non-zero `guest` value counts as a second-player row (tested with 1, 2 and -1). In step 8 the 0x1000
   is added to a value below 0x1000, so adding and setting the bit are the same.
9. 4.7 step 3 and open question 3. In v7.212 `u3x_pm_coop` reads the same net game entry as `u3x_netgame_cur`
   (same script symbol, same index field, same range checks) and answers for the same eight game types as the old
   inline test; without an entry both say "not co-op". With the index put back by the bracket of step 2 the
   answers are therefore the same; the only difference is the extra lookup. (In the test `u3x_pm_coop` is a
   stand-in that applies the old test to the type of step 2 and is not counted.)
10. Open question 1: the second lookup of 4.7 step 8 is kept exactly; old and new agree in every tested case
    (first player with user number 0 found again; with the +0x800 marker another record or none).
    Open questions 2 and 4 cannot be settled without the game.
