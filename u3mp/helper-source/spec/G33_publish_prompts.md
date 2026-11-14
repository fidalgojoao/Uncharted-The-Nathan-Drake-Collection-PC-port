# G33. Profile publish, clan tag, front-end prompts, screen menus, two script natives, spotted icon

Behaviour description for a reimplementation (reimpl2 pipeline, describer's document). It says what the functions
do, not how the old ones were written. All offsets are hexadecimal when prefixed with 0x. "Image offset X" means the
address `base + X`, where `base` is the game image base. All multi-byte values are little-endian; "u32 / s32 / u16 /
u64 / u8" are unsigned / signed integers of that width, "pointer" is 8 bytes. Names and conventions follow
`G22_rank_xp.md` and `G23_boosters.md` ("menu user", "profile object", ea[n] / eb[n]); the call block for game
functions that take float registers is the one of `G04_film_hud.md` section G.2 (`struct GameCall` and
`u3x_k26_call` of CONVENTIONS.h).

This group has **no float arithmetic**. Float values are only moved as 32-bit patterns (one is stored, a few are
handed to game functions in float registers). Nothing has to be rounded and no `f_*` helper is needed.

## 1. Purpose and callers

Six small things the PS4 game dropped or left empty and PS3 1.19 has; the helper supplies them.

**Part A: profile and prompts**

- **Profile publish** (`u3x_publish`): saves a player's multiplayer record. It is the LAN branch of the game's
  profile publish, done at once on the calling thread: the live record is copied to the profile object's save copy,
  written to the profile's file and, on the online path, handed to the upload queue.
  Callers: the C function `mp_publish` (a one-line wrapper today), which is called by the C functions `x71_grant`,
  `mission_take`, `mp_quit119`, `mp_profile_start`, `mp_page_event`, `mp_match_saved` and is the body of the C export
  `u3h_publish` (used by the boosters list G23, the invites G20, the post-match handler, this group's clan tag and
  others); and the C++ function `u3x_content_notice` directly.
- **Clan tag** (`u3x_clan_tag_open`, `u3x_clan_tag_poll`): the 4-character clan tag of the menu user's profile is
  typed in the system keyboard. Open is called by C `clan_tag_open` for stub kind 11 of the helper's event entry
  `u3_profile_event` (the click on the profile list's clan tag row). Poll is called once per frame while a lobby
  list is on screen: by C `clan_tag_poll` (itself reached through the C export `u3h_clan_tag_poll` from the party
  member list, and from the C lobby code), and directly by the C++ list sources `u3x_lobby_source` and
  `u3x_m03_players` on their "row count" request.
- **Front-end prompts** (`u3x_prompt_value`, `u3x_prompt_text`): on two front-end pages PS3 shows the prompts
  "Options" (Square) and "Credits" (Triangle); the PS4 game replaced them by one "Title Menu" prompt. The builder
  plants register-saving stubs on the four methods of the game's prompt source (text, button, action, count); the
  helper's event entry calls C `prompt_value` for stub kinds 45 (text), 46 (button), 47 (action) and 48 (count; for
  48 another group's page filter `k33_trim` runs first and may answer instead). A result of 1 means "the answer is
  in the register array, do not run the game's method"; 0 means "run the game's method" (with the register array
  as the helper left it). `u3x_prompt_text` composes one prompt text; its only user is the prompt answer.

**Part B: screen menus**

- `u3x_screen_menu`, `u3x_menu_open`, `u3x_menu_page`, `u3x_menu_calls_ok`, `u3x_menu_set_state`, `u3x_menu_show`,
  `u3x_menu_back`, `u3x_menu_close`: the PS3 menu operations "state change", "show a page", "back" and "close" on a
  per-screen menu object, which the PS4 game has only inlined in its own callers. Callers: the C one-line wrappers
  `screen_menu`, `menu_open`, `menu_page`, `menu_natives_ok`, `menu_set_state`, `menu_show`, `menu_back`,
  `menu_close`; and C++ functions of other groups: `u3x_page_tab_text` (G19: `u3x_menu_page`), `u3x_spoiler_list`
  (G25: back, close), `u3x_coop_spectate` (page, open, show, back, close), `u3x_camera_tick` (screen menu, page,
  back, close).

**Part C: script natives and the spotted icon**

- **Two script natives** (`u3x_native_underwater`, `u3x_native_disable_zipline`): "is-camera-underwater?" and
  "player-disable-zipline", which PS3 registers and PS4 does not (two level scripts call them). The C functions
  `nat_cam_underwater` and `nat_disable_zipline` (one-line wrappers with the game's native signature: argument
  count, argument array, context) are put into the game's list of script natives by the C function `natives_link`;
  the game's script interpreter calls them.
- **Spotted icon** (`u3x_hud_spotted`): PS3 draws a HUD image while the local player is "spotted"; the PS4 game
  kept only the checks. C `hud_spotted_draw` calls it for stub kind 52 (a stub at the game's HUD slot), only with a
  non-zero `base`.

Threading: everything except the two natives runs on the game thread that updates and draws the menus and the HUD.
The natives run on whatever thread runs the game's scripts. There is no locking and no timing rule in this group.

## 2. Interface

No function takes or returns a float. All are `extern "C"`. "existing" = exported today under this name; name and
signature stay. "new" = replaces a C body; the C function of the old name stays as a one-line wrapper.

The group is delivered in **three independent parts** (no part calls a function of another part directly; part A's
clan tag reaches the publish through the C export `u3h_publish`).

### Part A: profile and prompts

| Function | Signature | Status | Called by | C wrapper passes |
|---|---|---|---|---|
| profile publish | `int u3x_publish(uintptr_t base, void *s, uint8_t *profile)` | existing | C `mp_publish`; `u3x_content_notice` | - |
| clan tag: open the keyboard | `void u3x_clan_tag_open(uintptr_t base, void *s)` | existing | C `clan_tag_open` | - |
| clan tag: poll the keyboard | `void u3x_clan_tag_poll(uintptr_t base, void *s)` | existing | C `clan_tag_poll`; `u3x_lobby_source`; `u3x_m03_players` | - |
| prompt text | `void u3x_prompt_text(char *out, const char *token, const char *label)` | existing | `u3x_prompt_value` (and the C wrapper `ptxt_build`, which loses its caller) | - |
| prompt answer | `int u3x_prompt_value(uintptr_t base, uint64_t *ev, int kind, void *s)` | new (C `prompt_value`) | C `prompt_value` | its four arguments unchanged (base, register array, kind, state) |

### Part B: screen menus

| Function | Signature | Status | Called by |
|---|---|---|---|
| menu of a screen | `uint8_t *u3x_screen_menu(uintptr_t base, int32_t screen)` | existing | C `screen_menu`; `u3x_camera_tick` |
| menu is open | `int u3x_menu_open(const uint8_t *m)` | existing | C `menu_open`; `u3x_coop_spectate` |
| top page | `uint32_t u3x_menu_page(const uint8_t *m)` | existing | C `menu_page`; G19; `u3x_coop_spectate`; `u3x_camera_tick` |
| game functions as expected | `int u3x_menu_calls_ok(uintptr_t base)` | existing | C `menu_natives_ok`; this part |
| state change | `void u3x_menu_set_state(uintptr_t base, uint8_t *m, uint32_t state)` | existing | C `menu_set_state`; this part |
| show a page | `void u3x_menu_show(uintptr_t base, uint8_t *m, uint32_t page)` | existing | C `menu_show`; `u3x_coop_spectate` |
| back | `int u3x_menu_back(uintptr_t base, uint8_t *m, int silent)` | existing | C `menu_back`; G25; `u3x_coop_spectate`; `u3x_camera_tick` |
| close | `void u3x_menu_close(uintptr_t base, uint8_t *m)` | existing | C `menu_close`; G25; `u3x_coop_spectate`; `u3x_camera_tick` |

### Part C: script natives and the spotted icon

| Function | Signature | Status | Called by |
|---|---|---|---|
| is-camera-underwater? | `int64_t u3x_native_underwater(int32_t argc, uint64_t *argv)` | existing | C `nat_cam_underwater` (passes its first two arguments) |
| player-disable-zipline | `int64_t u3x_native_disable_zipline(int32_t argc, uint64_t *argv)` | existing | C `nat_disable_zipline` (passes its first two arguments) |
| spotted icon | `void u3x_hud_spotted(uintptr_t base)` | existing | C `hud_spotted_draw` |

Parameters:

- `base`: game image base. `s`: the helper's state object (opaque; fields in 3.2).
- `profile`: a profile object (3.3), or null.
- `out`: a buffer of at least 87 bytes. `token`: a NUL-terminated text, never null. `label`: a NUL-terminated text
  or null.
- `ev` (prompt answer): the stub's register array, u64 words. Word 5 = the prompt index the game asks for (its low
  32 bits, signed); word 6 = the second argument of the text method (its low 32 bits; 0 = "give the text"); word 8 =
  the value the game's method is to return (the helper's answer). Other words are not used.
- `kind`: 45, 46, 47 or 48 (the caller passes nothing else).
- `m`: a menu object (3.5), or null where section 4 says so.
- `screen`: 0 or non-zero. `page`: a page id. `state`: a menu state number. `silent`: passed on to the game.
- `argc`, `argv`: the script call's argument count and argument words (`argv` may be null).

Results: see section 4 (`u3x_publish` 1 saved / 0 not; `u3x_prompt_value` 1 answered / 0 not; `u3x_menu_back` 0 to
255; both natives a script value).

Functions of the C part and of the shared tools that the new code calls (declare them; do not reimplement them):

| Name | Signature | What it does | Part |
|---|---|---|---|
| `u3h_ext` | `uint8_t *u3h_ext(void)` | the helper's own state block (3.1). In CONVENTIONS.h. A plain address accessor: how often it is called is not part of the behaviour | A, B, C |
| `u3h_text` | `const char *u3h_text(uintptr_t base, void *s, uint32_t id)` | localized text of a text id through the game's text lookup; never null (empty text when there is none) | A |
| `u3h_publish` | `int u3h_publish(uintptr_t base, void *s, uint8_t *profile)` | C export whose body is `u3x_publish` of this group (through C `mp_publish`) | A |
| `u3h_rank_fields` | `void u3h_rank_fields(uint8_t *profile, uint32_t slot, int after)` | the helper's rank display changes five words of the live record in memory. `after` = 0: puts the profile's own values of those words into the save copy (called between the copy and the write). `after` = 1: copies the live values of those words back into the save copy. Does nothing when no such change is active for `slot` | A |
| `u3h_mp_write` | `int u3h_mp_write(uintptr_t base, void *s, uint32_t slot, const uint8_t *record)` | seals the 0x2828-byte record and writes it to the file of profile `slot` (or of the online account); 1 = written, 0 = failed | A |
| `u3h_online_profile` | `int u3h_online_profile(void)` | non-zero while the loaded profile is the online account's | A |
| `u3h_server_queue` | `void u3h_server_queue(const uint8_t *record)` | hands a saved record to the upload queue (copies it; G17) | A |
| `u3h_active_page` | `uint32_t u3h_active_page(uintptr_t base)` | the id of the page on top of the main menu (image 0x17bd080); 0 for a null `base`, while the menu changes pages, or without a page | A |
| `u3h_rp_tick` | `void u3h_rp_tick(uintptr_t base, void *s)` | **C export to add** (one line that calls the C function `rp_tick`): the per-frame work of the helper's online sign-in (settings, keyboard, connection). It is driven from the count request of the prompts because that request arrives every frame on the front-end pages | A |
| `u3h_spot_flag` | `uint8_t u3h_spot_flag(uintptr_t base, uint8_t *np)` | non-zero when net player `np` counts as spotted on the screen being drawn (another group) | C |
| `u3x_k26_call` | `uint64_t u3x_k26_call(void *game_call_block)` | performs a `GameCall` block (CONVENTIONS.h; layout in `G04_film_hud.md` G.2): six integer arguments, the low 8 bytes of xmm0 to xmm4 (each slot is loaded whole: two packed singles, the first in the low 4 bytes; the upper half of the register is zero), two stack arguments; returns rax | C |

## 3. Game and helper facts

### 3.1 The helper's state block (address from `u3h_ext()`)

| Offset | Type | Meaning | Part |
|---|---|---|---|
| 0x128 | u32 | menu counter: "back" calls that reached a page are counted in steps of 1, closes in steps of 0x10000 (it is word 10 of the co-op diagnostic array at 0x100) | B |
| 0x21b0 | u64 | the image base for the script natives (set by `natives_link`; 0 before) | C |
| 0x2218 + 4·k | u32 | native counters n[k]: n[1] underwater calls, n[2] last underwater answer, n[3] zipline calls, n[4] last player index, n[5] zipline settings queued (n[0] belongs to `natives_link`) | C |
| 0x2258 | 88 bytes | prompt text buffer 0 ("Options") | A |
| 0x22b0 | 88 bytes | prompt text buffer 1 ("Credits") | A |
| 0x2308 + 4·k | u32 | prompt counters p[k]: p[0] text requests, p[1] button requests, p[2] action requests, p[3] PS3 answers given, p[4] last page id, p[5] last prompt index, p[6] count requests, p[7] last count answer | A |
| 0x2fe0 + 4·k | u32 | spotted icon: h[0] calls, h[1] draws, h[2] last texture id, h[3] last icon index | C |

Counters are plain 32-bit increments (wrapping).

### 3.2 The state object `s`

| Offset | Type | Meaning | Part |
|---|---|---|---|
| 0 | pointer to function | system keyboard "open": (pointer to a request block, null pointer) → int; negative = could not open. May be null (no keyboard) | A |
| 8 | pointer to function | system keyboard "status": () → int; 2 = the dialog has finished | A |
| 16 | pointer to function | system keyboard "result": (pointer to a u32) → int; negative = error; it stores 0 in the u32 when the player confirmed, another value when the dialog was cancelled | A |
| 24 | pointer to function | system keyboard "close": () → int (ignored) | A |
| 0x4c | u32 | keyboard phase: 0 = free; 3 = the clan tag dialog is up (other values belong to other dialogs of the helper) | A |
| 0x50 | u32 | keyboard error; 6 = the dialog could not be opened | A |
| 0x58 | 16 × u16 | the keyboard's text buffer (UTF-16 units) | A |
| 128 | u32 | number of LAN profiles | A |
| 132 | u32 | the selected LAN profile, counted from 1 (0 = none) | A |
| 0x2d80 | 32 × u16 | title buffer of the clan tag dialog | A |
| 0x2f00 + 4·k | u32 | save probe q[k]: q[4] saves tried, q[5] saves written, q[6] saves failed, q[7] saves refused, q[8] slot of the last save, q[12] the record's save count after the last save, q[15] the slot whose file was loaded into the profile object ("bound slot", written by the C part when a profile is started) | A |
| 0x3d78 | u32 | switch word; bit 8 (value 0x100) set = the game's "Title Menu" prompt is kept as an extra prompt | A |

The four keyboard functions are the platform's system keyboard dialog (on the console the public IME dialog
functions, on the PC the runtime's stand-ins); they are called only through these pointers.

**Keyboard request block**: 96 bytes, all zero except: s32 at +0 = 1 (the user); u32 at +0x24 = the maximum number
of characters; pointer at +0x28 = the text buffer (in: the start text, out: the typed text, UTF-16, NUL
terminated); pointer at +0x48 = the title (UTF-16, NUL terminated). (It is the public layout of the console's IME
dialog parameters: user, type, languages, enter label, input method, filter, option, maximum length, buffer,
position, alignment, placeholder, title, 16 reserved bytes.)

### 3.3 Profile objects

The pointer at image 0x17dd610 is an array of profile objects of 0x7b00 bytes, one per local user (G22 3.4,
G23 3.5). "Menu user" = 1 when the u8 at image 0x17bde75 is non-zero, else the u32 at image 0x17bf714. "The menu
user's profile" = array + 0x7b00 · menu user; null when the array pointer is null or the menu user is 4 or more.

| Offset | Type | Meaning |
|---|---|---|
| 0x248 | 0x2828 bytes | the live multiplayer record |
| 0x8bc | u32 | the record's save count (record offset 0x674) |
| 0xac0 | 4 × u8 | the clan tag (record offset 0x878): up to 4 characters, NUL padded |
| 0x5298 | 0x2828 bytes | the save copy of the record (what is written to the file) |
| 0x7ae4 | u32 | publish state: 0 = saved, 2 = failed |
| 0x7ae8 | u32 | load state; 3 = loaded |
| 0x7afc | u8 | non-zero: a slave profile (never saved) |
| 0x7afd | u8 | non-zero: saving is suppressed |

### 3.4 Game data

| Image offset | Type | Meaning | Part |
|---|---|---|---|
| 0x17d5c61 | u8 | non-zero while multiplayer is active | A |
| 0x17b9d10 | u32 | a page / mode id of the front end (the game's own "Title Menu" prompt is hidden while it is 0xc0e2aff8 or 0x2c3d7d0f) | A |
| 0x13ac250 | text | the game's empty string | A |
| 0x16e3188 | pointer | the menu object of screen 0 (may be null) | B |
| 0x17bfa10 | object | the menu object of screen 1 | B |
| 0x17fad00 + 0x20·i | 0x20 bytes | script settings of player i; the u8 at +0xf is "zipline disabled" | C |
| 0x14481f0 | u32 | a float constant of the game, 1.0 (pattern 0x3f800000): the blend weight of a queued setting. Read it at run time | C |
| 0x1b24b98 | pointer | the game clock object (may be null); its s32 at +0x94 is the current tick count | C |
| 0x1806480 | pointer | the HUD being drawn (may be null); its s32 at +0x1d0 is the screen number | C |
| 0x17cb3d0 + 8·n | pointer | the local net player of screen n (n = 0, 1); may be null | C |
| 0x1b24bc8 | pointer | the UI image directory (may be null); the pointer at +0x20 of it is the HUD image table: u16 at +2 = entry count, pointer at +8 = the entries, each 0x28 bytes with its u32 image id at +8 | C |

### 3.5 Menu objects (part B)

| Offset | Type | Meaning |
|---|---|---|
| 8 | u32 | menu state: 0 = idle, 5 = closing, 6 = closed, 0xc = shown (other values exist) |
| 0xc | u32 | 1 = a root menu (one that closes when "back" is used on its first page) |
| 0x898 | u64 | the popup that is up; 0 = none |
| 0x8b0 + 8·d | pointer | the page stack, entry d; a page object has its u32 page id at +0x10 |
| 0xdb0 | s32 | the stack depth = index of the top entry; valid 0 to 159 |
| 0xdb4 | u32 | the transition timer |
| 0xdb8 | u32 | the transition speed, a float pattern |

### 3.6 Game functions called directly

| Image offset | Call | Result | Meaning | Part |
|---|---|---|---|---|
| 0xf23970 | (pointer to an event block of 160 bytes, int 0, int 0) | none | makes the block an empty event | B |
| 0x6534e0 | (menu, pointer to the event block, int phase) | none | tells the menu's listeners about a state change; phase 3 = before, 0 = after | B |
| 0x64c0a0 | (menu, int 1, int 1) | none | activates an idle menu | B |
| 0x651940 | (menu, u32 page id, int fresh, int silent) | int | shows a page on the menu (G18 3.x uses the same function) | B |
| 0x1221430 | (s32 screen) | u8 | non-zero when the camera of that screen is under water | C |
| 0x12284e0 | float register 0 = blend weight; (u32 event id, pointer to the setting, pointer to the value, u32 value size in bytes, pointer compare function, pointer set function); stack argument 1 = owner key, stack argument 2 = 0 | pointer or null | queues a change of a script setting and returns the queue entry (`film_helpers.md` 3.1 calls it "add a script-variable override") | C |
| 0x12284c0, 0x1228ca0, 0x12284d0 | never called here: their addresses are the compare / set arguments of 0x12284e0 (1-byte compare, 4-byte scaled set, 1-byte set) | - | - | C |
| 0x11f2e30 | (u32 symbol id) | pointer or null | the data of a script symbol | C |
| 0x11b8dd0 | integers (0, u64 image id, u32 index, pointer to four colour float patterns in a 16-byte aligned block, int flag, int 1); float registers 0 to 4 = position pair, size pair, direction pair, zero, scale pair | none | the game's HUD sprite draw (`G04_film_hud.md` A.3) | C |

Queue entry of 0x12284e0 (as in `film_helpers.md` 3.4): s32 at +0x10 = the time it becomes due, u32 at +0x28 =
priority / frame count, u8 at +0x49 = flags.

**Expected function starts (part B).** Before part B calls any game function it makes sure that each of the four
functions at image 0xf23970, 0x6534e0, 0x64c0a0 and 0x651940 begins with the twelve code bytes
55 48 89 E5 41 57 41 56 41 55 41 54 (the usual function entry: save the frame pointer, set it, save r15, r14, r13,
r12). This is a guard against a different game build, not a table of game values.

### 3.7 Ids and texts

| Value | Meaning | Part |
|---|---|---|
| text id 0x639e0fa8 | the title of the clan tag keyboard | A |
| page ids 0xd45dfcca, 0x82d5c00b | the two front-end pages whose prompts are answered | A |
| text ids 0xfb08c743, 0x2a6891c3 | "Options", "Credits" | A |
| button numbers 8, 9 | Square, Triangle | A |
| action ids 0xfa872aa5, 0x2be77c25 | open the options page, open the credits page | A |
| event ids 0x309861e2, 0x4d95edb1 | "cancel the pending change", "set the value" of a script setting | C |
| symbol id 0x9353ed16 | the HUD settings | C |

Texts of the helper's own (read-only helper data): the two button tokens "|S| " and "|T| " (four characters each:
bar, letter, bar, space).

There is **no literal table of game values** in this group. The six ids of the two PS3 prompts are single ids of
the kind G23 3.8 lists; the twelve code bytes of 3.6 are a build guard.

## 4. Behaviour

### 4.1 `u3x_publish` (part A)

1. Return 0 with nothing touched when: `profile` is null; or its "suppressed" byte (+0x7afd) is non-zero; or its
   "slave" byte (+0x7afc) is non-zero; or multiplayer is not active (u8 at image 0x17d5c61 is 0). (Tested in this
   order; nothing is read from a null `profile`.)
2. slot = q[15] (the bound slot). The save is **refused** when the profile's load state (+0x7ae8) is not 3, or slot
   is 0, or slot differs from the selected LAN profile (u32 at s + 132), or slot is above the number of LAN profiles
   (u32 at s + 128, unsigned comparison). Refused: publish state (+0x7ae4) = 2; q[7] += 1; return 0.
3. The record's save count (u32 at profile + 0x8bc) += 1.
4. The 0x2828 bytes of the live record (profile + 0x248) are copied to the save copy (profile + 0x5298).
5. `u3h_rank_fields(profile, slot, 0)`.
6. ok = `u3h_mp_write(base, s, slot, profile + 0x5298)`.
7. When ok is non-zero and `u3h_online_profile()` is non-zero (asked only when ok is non-zero):
   `u3h_server_queue(profile + 0x5298)`.
8. `u3h_rank_fields(profile, slot, 1)`.
9. Publish state (+0x7ae4) = 0 when ok is non-zero, else 2. q[4] += 1; q[5] += 1 when ok is non-zero, else
   q[6] += 1; q[8] = slot; q[12] = the record's save count (as incremented in step 3).
10. Return ok (the value `u3h_mp_write` returned).

### 4.2 `u3x_clan_tag_open` (part A)

1. Nothing when the keyboard phase (s + 0x4c) is not 0, or the keyboard "open" pointer (s + 0) is null.
2. profile = the menu user's profile (3.3); null: nothing (no text is looked up).
3. Title: T = `u3h_text(base, s, 0x639e0fa8)`. The title buffer (s + 0x2d80) receives the first 31 bytes of T (fewer
   when T is shorter), each byte as one UTF-16 unit with the value 0 .. 255 of the byte, then a 0 unit.
4. Start text: all 16 units of the text buffer (s + 0x58) are set to 0; then the clan tag's bytes (profile + 0xac0),
   up to 4 and only up to the first 0 byte, are stored as units 0 .. 3 (byte value 0 .. 255).
5. A request block (3.2) is made: maximum length 4, buffer = the text buffer, title = the title buffer, and the
   "open" function is called with (the block, null).
6. Result negative: keyboard error (s + 0x50) = 6; the phase stays 0. Otherwise: phase = 3.

### 4.3 `u3x_clan_tag_poll` (part A)

1. Nothing when the phase is not 3.
2. The "status" function is called; a result other than 2: nothing more (the dialog is still up).
3. A u32 preset to 1 is handed to the "result" function; then the "close" function is called; then phase = 0. (In
   this order, whatever the result function returned.)
4. Nothing more when the result function returned a negative value or the u32 is not 0 (cancelled).
5. profile = the menu user's profile; null: nothing more.
6. The typed text becomes the clan tag. For position j = 0, 1, 2, 3 of the text buffer, in this order:
   - c = unit j; a backslash (0x5c) counts as a space (0x20);
   - the tag byte j (profile + 0xac0 + j) = c when c is a printable ASCII character (0x20 .. 0x7e), else 0;
   - when c is 0 (the end of the text): the tag bytes of all later positions are set to 0 too and the walk ends.
   So a character outside 0x20 .. 0x7e that is not the end stores a 0 byte at its position and the following
   positions are still taken from the text.
7. `u3h_publish(base, s, profile)` (result unused).

The three keyboard pointers used here are not tested for null (phase 3 is only ever set after a successful open).

### 4.4 `u3x_prompt_text` (part A)

`out` receives the first 80 characters of `token` (all of it when shorter), then characters of `label` (when
`label` is not null) until `label` ends or `out` holds 86 characters in all, then a NUL. Nothing else is written.

### 4.5 `u3x_prompt_value` (part A)

"idx" below is a signed number. "The answer" is word 8 of `ev` (the whole u64 is written; 32-bit ids are
zero-extended).

1. Count the request: `kind` 48: p[6] += 1; otherwise p[`kind` − 45] += 1 (45 → p[0], 46 → p[1], 47 → p[2]).
2. `base` is 0: return 0.
3. page = `u3h_active_page(base)`; p[4] = page. When page is neither 0xd45dfcca nor 0x82d5c00b: return 0.
4. keep = bit 8 of the switch word (u32 at s + 0x3d78).
5. **Kind 48 (count: the highest prompt index the page draws).** v = the u32 at image 0x17b9d10, read first; then
   `u3h_rp_tick(base, s)`; then the answer = 2 when keep is set and v is neither 0xc0e2aff8 nor 0x2c3d7d0f, else 1;
   p[7] = the answer; return 1.
6. **Kinds 45, 46, 47.** idx = the low 32 bits of word 5, as s32; p[5] = idx (as u32).
7. When keep is set: idx 0 is the game's own "Title Menu" prompt: word 5 of `ev` = 1 (the whole u64) and return 0
   (the game's method then answers for its own index 1); for every other idx, idx is lowered by 1 (word 5 is left
   as it is).
8. p[3] += 1. Then, by kind, and return 1:

| Kind | idx = 0 ("Options") | idx = 1 ("Credits") | any other idx |
|---|---|---|---|
| 45 (text), flag (low 32 bits of word 6) = 0 | `u3x_prompt_text(buffer 0, "\|S\| ", u3h_text(base, s, 0xfb08c743))`; answer = address of buffer 0 | `u3x_prompt_text(buffer 1, "\|T\| ", u3h_text(base, s, 0x2a6891c3))`; answer = address of buffer 1 | answer = address of image 0x13ac250 |
| 45 (text), flag ≠ 0 | answer = address of image 0x13ac250 (no text looked up, no buffer written) | the same | the same |
| 46 (button) | answer = 8 | answer = 9 | answer = 0 |
| 47 (action) | answer = 0xfa872aa5 | answer = 0x2be77c25 | answer = 0 |

The prompts are drawn from the highest index down to 0 from left to right, so index 0 is the rightmost: with keep
set the order on screen is Credits, Options, Title Menu.

### 4.6 The menu accessors (part B)

- **`u3x_screen_menu`.** `screen` non-zero: the address of image 0x17bfa10. `screen` 0: the pointer stored at image
  0x16e3188 (may be null).
- **`u3x_menu_open`.** 1 when `m` is not null, its state (u32 at +8) is not 0 and its u32 at +0xc is 1; else 0.
- **`u3x_menu_page`.** 0 when `m` is null, or the depth (s32 at +0xdb0) is negative or 160 or more, or the stack
  entry of that depth is null; otherwise the page id (u32 at +0x10) of that entry. The menu state is not looked at.
- **`u3x_menu_calls_ok`.** 1 when all four functions of 3.6 "Expected function starts" begin with the twelve bytes,
  else 0. It only reads; whether the answer is computed every time or kept is not observable as long as the image
  does not change (the old code compares every time).

### 4.7 `u3x_menu_set_state` (part B)

No test of `m` and no test of the game functions (the callers do both). In this order:

1. An event block of 160 bytes, 16-byte aligned, all zero.
2. Game function 0xf23970(block, 0, 0).
3. Game function 0x6534e0(`m`, block, 3).
4. The menu state (u32 at `m` + 8) = `state`; the transition timer (u32 at +0xdb4) = 0; the transition speed (u32
   at +0xdb8) = 0x3f800000 (1.0).
5. Game function 0x6534e0(`m`, block, 0), with the same block as the game left it.

### 4.8 `u3x_menu_show` (part B)

Nothing when `m` is null or `u3x_menu_calls_ok(base)` is 0. Otherwise:

1. idle = the menu state is 0. When idle: game function 0x64c0a0(`m`, 1, 1).
2. Game function 0x651940(`m`, `page`, 1 when idle else 0, 0); result ignored.
3. `u3x_menu_set_state(base, m, 0xc)`.

### 4.9 `u3x_menu_back` (part B)

Return 0 with nothing done when `m` is null, `u3x_menu_calls_ok(base)` is 0, or the menu state is 0. Otherwise,
with depth = the s32 at +0xdb0:

- **depth within 1 .. 159** (a page lies under the top): the target is the top entry itself while a popup is up
  (u64 at +0x898 non-zero), else the entry below the top (depth − 1). The menu counter (ext + 0x128) += 1, also
  when the target entry is null. Target entry null: return 0. Otherwise return the low 8 bits (0 .. 255) of game
  function 0x651940(`m`, the target's page id, 0, `silent`).
- **any other depth** (0, negative, 160 or more): when the menu's u32 at +0xc is 1 (a root menu): unless the state
  is already 5 or 6, `u3x_menu_set_state(base, m, 5)`; return 1 in both cases. When it is not 1: return 0.

### 4.10 `u3x_menu_close` (part B)

Nothing when `m` is null or `u3x_menu_calls_ok(base)` is 0. When the menu state is neither 0 nor 5 nor 6:
`u3x_menu_set_state(base, m, 5)`, then the menu counter (ext + 0x128) += 0x10000. Otherwise nothing.

### 4.11 The two script natives (part C)

"First argument" = the low 32 bits of `argv[0]` as s32 when `argc` is above 0 and `argv` is not null; else 0.
B = the image base stored in the helper's state block (u64 at ext + 0x21b0).

**`u3x_native_underwater`.** n[1] += 1. B is 0: return 0. Otherwise r = the u8 result of game function
0x1221430(first argument); n[2] = r (0 .. 255); return 1 when r is non-zero, else 0.

**`u3x_native_disable_zipline`.** n[3] += 1. B is 0: return 0. Otherwise:

1. player = first argument; n[4] = player (as u32). The setting = the byte at image 0x17fad00 + 0x20 · player +
   0xf (player as a signed 64-bit number; it is not range checked).
2. weight = the u32 at image 0x14481f0.
3. **Cancel the pending change.** Game function 0x12284e0 through a call block: float slot 0 = weight; integers
   (0x309861e2, address of the setting, address of a byte that holds 0, 1, B + 0x12284c0, B + 0x1228ca0); both
   stack slots 0; every other slot 0. When it returns an entry: its u32 at +0x28 = 0 and bit 3 (value 8) of its u8
   at +0x49 is set (the other bits kept).
4. **Queue the value 1.** Game function 0x12284e0 again: float slot 0 = weight; integers (0x4d95edb1, address of
   the setting, address of a byte that holds 1, 1, B + 0x12284c0, B + 0x12284d0); both stack slots 0. When it
   returns an entry: its u8 at +0x49 = (old value AND 0xdb) OR 4; its u32 at +0x28 = 101; its s32 at +0x10 = 9 +
   the current tick count (the s32 at +0x94 of the clock object whose pointer is at image 0x1b24b98; 0 when that
   pointer is null); n[5] += 1.
5. Return 0.

The two value bytes only have to be valid during their call. A null entry from either call: that call's
after-work is skipped, the rest goes on.

### 4.12 `u3x_hud_spotted` (part C)

h[0] += 1. Then, each test ending the function with nothing more when it fails, in this order:

1. settings = game function 0x11f2e30(0x9353ed16); null: end.
2. object = the pointer at settings + 0x78; hud = the pointer at image 0x1806480; either null: end.
3. screen = the s32 at hud + 0x1d0; not 0 and not 1: end.
4. player = the pointer at image 0x17cb3d0 + 8 · screen; null: end. `u3h_spot_flag(base, player)` is 0: end.
5. element = the pointer at object + 0x158; null: end.

Then the element is drawn:

- image = the u32 at element + 4.
- index = the position (from 0) of the first entry of the HUD image table (3.4) whose image id equals image; −1
  when there is none, when the directory pointer is null, when the table pointer is null, when the count is 0 or
  when the entries pointer is null.
- h[2] = image; h[3] = index (as u32); h[1] += 1.
- One call of game function 0x11b8dd0 through a call block: integers (0, image zero-extended to 64 bits, index
  as an unsigned 32-bit value (so −1 is 0xffffffff), the address of a 16-byte aligned copy of the four u32 at
  element + 0x30 .. + 0x3f (the colour), 1 when the s32 at element + 0x24 is non-zero else 0, 1); float slot 0 =
  the 8 bytes at element + 0x1c (position x, y); float slot 1 = the 8 bytes at element + 0x14 (width, height);
  float slot 2 = the 8 bytes at element + 0x28 (direction pair); float slot 3 = 0; float slot 4 = 0x3f800000 in
  both halves (1.0, 1.0); stack slots 0. All values are copied as they are.

The element is drawn even when its image is not in the table (index −1).

## 5. Test scenarios

Everything except the points at the end can be tested on a fake image with recording stubs: for the `u3h_`
functions of section 2, for the keyboard function pointers in the state object, for `u3x_k26_call` (recording the
whole call block) and for the plain game functions of 3.6 (part B: 0xf23970, 0x6534e0, 0x64c0a0, 0x651940; part
C: 0x1221430, 0x11f2e30). Compare memory (profile object, state object, helper state block, register array, menu
object, queue entries), results, and the order and arguments of all calls.

Part A, publish:
1. Null profile; suppressed; slave; multiplayer byte 0: result 0, nothing written, no call.
2. Refused: load state 2; bound slot 0; bound slot ≠ selected; bound slot above the count: publish state 2, q[7],
   result 0, no call, save count unchanged.
3. Saved offline (`u3h_mp_write` 1, online 0): order rank fields(0) → write → rank fields(1); the save copy equals
   the live record with the incremented save count; no queue call; publish state 0; q[4], q[5], q[8], q[12].
4. Saved online (write 1, online 1): the queue call with the save copy between the write and rank fields(1).
5. Write fails (0): `u3h_online_profile` not asked, no queue call, publish state 2, q[4] and q[6], result 0.
6. A write result of 2: counts as saved and is returned as 2.

Part A, clan tag:
7. Open: phase 1 (nothing); "open" pointer null (nothing); profile array null and menu user 4 (nothing, no text
   call); normal open with tag "AB" (units 'A', 'B', 0 .. and all 16 units defined), a 4-byte tag without NUL, a
   title of 0, 5, 31 and 40 bytes (cut at 31 units + 0), a title byte 0xe9 (unit 0x00e9); request block bytes
   (96 bytes, the four fields, all else 0); "open" returning −1 (error 6, phase 0) and 0 (phase 3); the menu user
   taken as 1 when the u8 at 0x17bde75 is set.
8. Poll: phase 0 and 1 (no call); status 1 (no further call, phase stays 3); status 2 with result function −1,
   with the u32 left at 1, with u32 = 0: close always called, phase 0; texts "ABCD", "A" (bytes A, 0, 0, 0), ""
   (all 0), "A\\B" (A, space, B), "A" + 0x7f + "B" (A, 0, B, ...), "A" + 0x1f + "B", a unit 0x0141, a 5-unit text
   (only 4 used); no profile (nothing stored, no publish); publish called once with the profile.
9. `u3x_prompt_text`: token of 4 and of 90 characters (cut at 80, then the label up to 86), label null, empty,
   long (cut at 86 in all); the NUL.

Part A, prompts:
10. `base` 0 (0; the kind counter still moves). Page neither id (0, p[4] = page).
11. Kind 48: keep clear (1); keep set with v = another value (2), v = 0xc0e2aff8 and 0x2c3d7d0f (1); the tick call
    made once, after v was read; p[6], p[7].
12. Kinds 45 / 46 / 47 with keep clear: idx 0, 1, 2, −1; kind 45 with flag 0 and 1 (buffers untouched for flag 1);
    the answers as u64 (upper half 0); both pages.
13. Keep set: idx 0 (word 5 becomes 1, result 0, p[3] unchanged), idx 1 (Options), idx 2 (Credits), idx 3 (empty /
    0); word 5 unchanged for idx ≠ 0.
14. Word 5 with a non-zero upper half (only the low 32 bits count).

Part B:
15. `u3x_screen_menu` 0 / 1 / −1. `u3x_menu_open`: null; state 0; state 0xc with +0xc = 1 and = 0.
    `u3x_menu_page`: null; depth −1, 0, 159, 160; null entry.
16. `u3x_menu_calls_ok`: all four right; each one of the four with one changed byte (first and twelfth).
17. `u3x_menu_set_state`: the call order, the block all zero at the first call and 16-byte aligned, the same block
    address in all three calls, the three fields written between the two notifications.
18. `u3x_menu_show`: null; guard failing (no call at all); idle menu (activate, show with fresh 1, state 0xc);
    non-idle menu (no activate, fresh 0).
19. `u3x_menu_back`: null; guard failing; state 0; depth 1 and 159 without popup (entry depth − 1) and with popup
    (entry depth); null target entry (0, counter still + 1); the game's result 0x1ff (0xff); `silent` passed on;
    depth 0 / −1 / 160 on a root menu in state 0xc (state change to 5, result 1), in state 5 and 6 (no change,
    result 1), on a non-root menu (0).
20. `u3x_menu_close`: null; guard failing; state 0, 5, 6 (nothing); state 0xc (state change, counter + 0x10000).

Part C:
21. Underwater: B = 0 (0, n[1] only); `argc` 0, `argv` null (argument 0); result byte 0, 1, 0x80; n[2].
22. Zipline: B = 0; player 0, 3, −1; both entries returned (fields of both, the clock present and null, n[5]);
    first entry null; second entry null (n[5] unchanged); the flags byte starting at 0xff (first: 0xff; second:
    0xdf); both call blocks slot by slot (weight pattern taken from the image, value bytes 0 and 1 behind the
    pointers at call time).
23. Spotted: each of the five early ends (h[0] only); screen 2 and −1; flag 0; draw with the image at entry 0, at
    the last entry, absent (index 0xffffffff in the block), with a null directory, a null table, count 0, null
    entries; the flag at +0x24 = 0, 1, 0x100; the call block slot by slot (the colour copy's address 16-byte
    aligned, its four words; the three pairs as 64-bit values); h[1], h[2], h[3].

Needs the live game: that the keyboard really opens with the title and the tag and the new tag shows in the
profile list and survives a restart; that the profile file is written; that Options / Credits show and work on the
two pages; that the spoiler page opens and closes in co-op; that the zipline is disabled by the script; that the
spotted icon appears at the right place.

## 6. Things that look like bugs or dead code (behaviour to keep)

1. `u3x_publish` returns `u3h_mp_write`'s value as it is and tests it only for non-zero.
2. `u3x_publish` increments the save count before the write and does not take it back when the write fails.
3. `u3x_clan_tag_open` passes user number 1 in the request whatever the menu user is, and copies the title byte by
   byte (a multi-byte character of a translated title becomes several units).
4. `u3x_clan_tag_open` leaves the phase at 0 after a failed open and only sets the error word; it never clears
   that word on success.
5. `u3x_clan_tag_poll`: a non-printable character inside the text leaves a 0 byte in the middle of the tag (4.3
   step 6); the profile is published also when the tag did not change.
6. The keyboard "result" function gets a pointer to one u32 (4 bytes), as everywhere in the helper; the console's
   public result structure is larger. Kept as it is (see 7.3).
7. `u3x_prompt_value`: with `base` 0 or another page the kind counter has already moved. A `kind` outside
   45 .. 48 would count outside the counter array (the caller never passes one).
8. `u3x_prompt_value`: the old C body holds a block that is switched off at compile time (an extra "RPCN" prompt
   at index 0, removed in v7.105). It is dead and not delivered.
9. `u3x_prompt_value`: the count answer ignores whether the "Title Menu" prompt is really shown by the game beyond
   the two ids (the game's further title checks are not repeated).
10. `u3x_menu_set_state` tests nothing (a null menu would fault); `u3x_menu_open` and `u3x_menu_page` do not ask
    `u3x_menu_calls_ok`.
11. `u3x_menu_back` counts in the menu counter before it knows whether the target page exists; `u3x_menu_close`
    counts only when it changed the state. For depth 160 or more on a root menu it closes the menu.
12. `u3x_menu_back` with a popup up shows the top page again (that is how the popup is dismissed).
13. `u3x_native_disable_zipline` does not range check the player index, and its first call names the 4-byte
    "scaled set" function (0x1228ca0) for a 1-byte setting; that entry is cancelled at once (priority 0, flag 8).
14. `u3x_native_disable_zipline`: the old code left the second stack argument of 0x12284e0 undefined; the new
    code passes 0 (as `film_helpers.md` does for the same function). The register that holds the vector count
    (al) was undefined / the function address in the old calls of 0x12284e0 and 0x11b8dd0; neither function is
    variadic, the call block's `al` slot is 0.
15. `u3x_hud_spotted` draws with index −1 when the image is not in the table, and counts the draw before the call.
16. `u3x_native_underwater` and the zipline native read the image base from the helper's state block, not from a
    parameter (natives have none).

## 7. Open questions

1. `u3h_rp_tick` does not exist yet: the integrator adds this one-line C export (`rp_tick` stays in C). Until then
   `u3x_prompt_value` cannot be linked.
2. The meaning of the fifth integer argument of 0x11b8dd0: the old notes of this function call the element's field
   at +0x24 "rotate", `G04_film_hud.md` calls the argument "centred". It is passed through (0 / 1).
3. The keyboard result block (6.6): whether the console's dialog writes more than the first 4 bytes was never
   observed as a problem; if it does, every keyboard user of the helper has the same exposure. Not changed here.
4. Which of the two page ids is which front-end page (the old notes name PAGE_ROOTPROFILE and PAGE_PROFILE as the
   users of the prompt source and call 0x82d5c00b the single-player campaign page): not needed for the behaviour.
5. What the u32 at image 0x17b9d10 is exactly (the old notes: a field of the game's front-end object at image
   0x17b6368, offset 0x39a8): only compared with the two ids.

## 8. What stays as it is

- The C wrappers (`mp_publish`, `clan_tag_open`, `clan_tag_poll`, `prompt_value`, `screen_menu`, `menu_open`,
  `menu_page`, `menu_natives_ok`, `menu_set_state`, `menu_show`, `menu_back`, `menu_close`, `nat_cam_underwater`,
  `nat_disable_zipline`, `hud_spotted_draw`) and the C exports `u3h_publish`, `u3h_clan_tag_poll`: the callers.
- C `natives_link` (it puts the two natives into the game's native list and stores the image base), C `rp_tick`,
  `k33_trim`, `active_page`, the profile file functions behind `u3h_mp_write` and the rank display behind
  `u3h_rank_fields`: other code, called or calling only.
- `u3x_k26_call` and `struct GameCall` (shared; G04).
- For the integrator: some file-private names of the old C++ blocks of this group are also used by functions of
  other groups in the same file and must not disappear with the old bodies: the menu user's profile accessor
  (`player_profile`), the state function-pointer accessor (`state_fn`), the LAN profile count / selection accessors
  (`profile_count`, `profile_selected`), the natives' image base accessor (`native_base`), the twelve entry bytes
  (`usual_prologue`) and the co-op diagnostic array accessor (`ext_coop`). The new parts define what they need
  under names of their own.

## 9. Clarifications (tester, 2026-11-14)

Facts confirmed by the old-against-new test (v7.212 against the three parts). None of them changes what the new
code has to do; 1 to 4 are points where the description above is silent or narrower than the old code.

1. `u3x_publish`, step 9: the old code takes the value for q[12] by reading the record's save count (profile +
   0x8bc) again after the second `u3h_rank_fields` call; the description says "as incremented in step 3". Both are
   the same number as long as none of `u3h_rank_fields`, `u3h_mp_write`, `u3h_online_profile`, `u3h_server_queue`
   changes the live record's save count, and none of them does (the rank display's five words are record offsets
   0x4fc, 0x500, 0x1618, 0x1624, 0x1f1c and only the save copy is written; the write and the queue take the record
   read-only). No other difference in the order of reads and calls was found in this function.
2. `u3x_clan_tag_open`: the old code reads the keyboard "open" pointer (s + 0) twice: for the null test of step 1
   and again for the call of step 5, after `u3h_text`. The same pointer as long as `u3h_text` does not change the
   state object's first word (it does not).
3. `u3x_hud_spotted`: the old code copies the four colour words before it stores h[2] and h[3] and counts h[1];
   the image id is read before all three, the flag (+0x24) and the three pairs after them, at the call. The order
   could only be seen if the element lay over the helper's own counters.
4. `u3x_prompt_value` with a `kind` outside 45 .. 48 (never passed): the old code treats every kind that is not 45,
   46 or 48 like 47 (it writes the action answer into word 8); result and counters are as for 47. Not required of
   the new code.
5. `u3x_clan_tag_poll`: a positive value from the "result" function is not an error (only negative is); the tag
   is stored and published.
6. `u3x_native_disable_zipline`: the weight is read once, before the first call of 0x12284e0 (both calls get the
   same pattern); the clock pointer and the tick count are read after the second call. 9 + tick is a wrapping
   32-bit sum. When both calls return the same entry, the second call's after-work is applied on top of the
   first's (flags = ((old OR 8) AND 0xdb) OR 4).
7. `u3x_hud_spotted`: the first matching entry of the image table wins; an entry at or past the count is never
   looked at.
8. `u3x_menu_set_state` in v7.212 clears all 160 bytes of its event block (an earlier note said that 0x80 bytes of
   the old block were left uncleared: that is not so in v7.212), so old and new hand identical blocks to the three
   game calls.
9. The old assembly routine for 0x12284e0 loads float register 0 with the weight's 32 bits (the rest of the
   register zero), stores one zero stack word and leaves the second stack word and al undefined; the one for
   0x11b8dd0 loads float registers 0 to 4 (8 bytes each, register 3 zero), stores no stack word and has the
   function address in rax. The new call blocks agree in everything the old routines define and are 0 elsewhere.
