# G03. Film playback frame

Behaviour description for the clean-room pipeline (reimpl2). Source of the facts: helper v7.212, the two functions
`rp_cycle_player` and `u3x_rp_update` of the C++ part (208 code lines), their caller in the C part and the generated
x87 code. All addresses are image offsets (add `base`). All integers are little endian. "u8 / u16 / u32 / u64 / i32"
are the field types. Clock times are in ticks of 1/300 second.

## 1. Purpose and callers

While a saved film ("Cinema") is played back, the game is in a state object called RePlaying. The PS4 build's
per-frame update of that state is an empty shell (it only jumps to the generic state update). The helper replaces
it: once per frame it reads the pad, keeps the film HUD flags, the play speed and the pause state, feeds the film's
recorded events into the game up to the playback clock, copies them into a re-recording when one is open, drops a
bookmark every 10 seconds, keeps the camera, and finally runs the generic state update.

Call chain (stays as it is):

- The helper's value stub of kind 74 sits on the game's RePlaying update (image 0x667410). The C dispatcher
  `cinema_kind(base, kind, s, r)` handles kind 74 by calling the C wrapper `rp_update(base, st)` with `st` = the
  saved first argument register of the hooked function (the state object).
- `rp_update` is one line: it returns `u3x_rp_update(base, st)`.
- The result 1 tells the stub to return to the game's caller without running the game's own body. That is why the
  generic state update is called from inside this function.

The second function (cycling the followed player) has one caller: `u3x_rp_update`.

## 2. Interface

### 2.1 `u3x_rp_update` (name and signature kept)

```
extern "C" int u3x_rp_update(uintptr_t base, uint8_t *st);
```

- `base`: image base. `st`: the RePlaying state object; its u64 at +8 is the pointer to the film player object,
  called the owner (`own`) below.
- Result: always 1.
- Caller: the C wrapper `rp_update`, which passes its two arguments unchanged. The C part declares it as
  `__attribute__((visibility("hidden"))) int u3x_rp_update(uintptr_t base,uint8_t *st);`.

### 2.2 `u3x_rp_cycle_player` (new name for the file-private `rp_cycle_player`, same parameters)

```
extern "C" void u3x_rp_cycle_player(uintptr_t base, uint8_t *own, int32_t dir, const uint8_t *cur);
```

- `own`: the owner. `dir`: step through the player trackers (+1, -1; any value is accepted). `cur`: the tracker
  followed up to now (may be null); only compared by address.
- No result.
- Caller: `u3x_rp_update` only (section 4.2, phase 8). It is given as an entry point so that the test can drive it
  directly.

### 2.3 Functions of the C++ part that exist already and stay (call them, do not rewrite them)

All `extern "C"` unless noted. Functions of other groups are named here with what they do; their own
descriptions cover them.

| Function | What it does |
|---|---|
| `uint8_t *u3x_rp_h(const uint8_t *h)` | resolves a handle {u64 pointer to an object pointer at +0, u32 id at +8}: the object when both are set and the object's u32 at +0x7c equals the id, else null |
| `uint8_t *u3x_rp_self(uint8_t *own)` | the object of the owner's own handle (owner +0x70), or null |
| `uint8_t *u3x_rp_cammgr(uintptr_t base)` | the camera manager of screen 0 (handle at 0x1d52b80); raises the game's assert when there is none, then returns null. Call it only where section 4 says so |
| `uint8_t *u3x_rp_trk(uintptr_t base, uint32_t i)` | player tracker i: `base + 0x17cb3f0 + i * 0x9c0` while the u8 at 0x17d5c61 (a net game is active) is non-zero, else null. No range check on i |
| `int u3x_rp_trk_live(uint8_t *t)` | non-zero when the tracker's handle at +0x10 or its handle at +0x20 resolves to an object. Reads through `t` without a null check |
| `uint32_t *u3x_rp_pad(uintptr_t base)` | address of pad 0's "pressed this frame" button word |
| `int u3x_rp_enter_cross(uintptr_t base)` | non-zero when the system's enter button is cross |
| `int u3x_rp_menu_up(uintptr_t base)` | non-zero when a menu is open over the film (object 0x17bd080: u32 +8 non-zero and u32 +0xc equal to 1) |
| `void u3x_rp_send(uintptr_t base, uint32_t msg, uint8_t *target)` | G02: sends game message `msg` without arguments to `target` (a game call, 0x1197380) |
| `void u3x_ovr_scale(uintptr_t base, uint8_t *addr, const void *val)` | G02: installs a script override of the 32-bit value at `addr` with the 32-bit pattern `val` points to (a game call, 0x12284e0) |
| `void u3x_render_defaults(uintptr_t base)` | copies the current render settings into the film writer as the film render defaults (memory only) |
| `void u3x_clock_reset(uint8_t *clock, uint32_t game_time, int32_t t)` | rewrites a playback clock so that it runs from time `t` (memory only; afterwards the clock's i32 at +0xc is `t`) |
| `uint32_t u3x_stream_read(uintptr_t base, uint8_t *bs, uint32_t n)` | reads n bits (most significant first) at the position of bit stream `bs`, moves the position on by n; raises the game's assert when the new position is past the capacity |
| `void u3x_stream_write(uintptr_t base, uint8_t *bs, uint32_t value, uint32_t n)` | the same for writing the low n bits of `value` |
| `void u3x_bookmark_move(uint8_t *d, const uint8_t *s)` | bookmark `s` takes the place of bookmark `d` (d keeps its own buffer) |
| `void u3x_bookmark_create(uintptr_t base, uint8_t *bm, int32_t t)` | G02: fills bookmark `bm` with a snapshot for time `t` (game calls) |
| `void rp_rumble_off(uintptr_t base, int k)` (file-private, anonymous namespace) | switches the rumble of pad k off for two frames (through G02's `u3x_ovr_byte`, a game call 0x12284e0) |
| `int rp_screenshot_step(uintptr_t base, uint8_t *st, uint8_t *own)` (file-private, G02) | one step of the screenshot request; returns 1 |
| `uint32_t *ext_eb()` (file-private) | the helper's own array of diagnostic counters |

The C part's exports used (declared just above this part of the file with hidden visibility; a second identical
declaration is harmless):

```
extern "C" void u3h_rp_cam_mode12(uintptr_t base, uint8_t *st, uint8_t *own);   // G02: frame work of camera modes 1 and 2
extern "C" void u3h_rp_render_vars(uintptr_t base, uint8_t *own);               // G02: applies the film render settings
extern "C" void u3h_rp_cam_target(uintptr_t base, uint8_t *own);                // G01: camera on the followed player
```

Other small accessors exist in the same anonymous namespace. Do not rely on them: use `at<>` / `rd<>` with the
offsets of section 3, and give your own private helpers names that start with `g03_`, so nothing clashes.

No float or double crosses a function boundary anywhere in this group.

## 3. Game facts

### 3.1 Game functions called directly

System V AMD64 convention; all arguments integer or pointer. "-" = no result used.

| Offset | Arguments | Result | Meaning |
|---|---|---|---|
| 0x7230f0 | (address `base + 0x17cb060` (the net manager), `own`, int 0) | - | net manager frame step for the film player |
| 0x6a28c0 | (address of a film stream object) | - | per-frame update of a film stream object |
| 0x1019550 | (camera manager pointer) | - | camera manager frame step |
| 0x560280 | (u64 0, int v) | - | tells the game that the pause state became v (0 or 1) |
| 0x6a2700 | (address `base + 0x17c3ba8`) | i32 | elapsed film time, same unit as the film duration (seconds) |
| 0xda1590 | (pointer to a u32 holding 0x14, address `base + 0x13ac250`, u64 0, address `base + 0x13ac250`) | - | enters a memory context (heap id 0x14; 0x13ac250 is a source-file name text in the image) |
| 0xda18b0 | () | - | leaves the memory context |
| 0x6a3130 | (address `base + 0x17c5cf8`) | - | closes the re-record reader |
| 0x6c2b50 | (u32 event type, 0..255) | pointer | an event object of that type (never checked for null) |
| 0x101b530 | (camera manager pointer, pointer to a u64 holding the address `base + 0x1ae95e0`, pointer to a u64 holding 0, null) | pointer, ignored | sets the camera type whose descriptor is at 0x1ae95e0 ("no camera") |
| 0x125c770 | (`st`) | - | the generic state update |

### 3.2 Virtual calls

A virtual call on object X at "table offset T" means: the object's u64 at +0 points to a table of function
pointers; call the pointer at byte offset T of that table.

| Object | Table offset | Arguments | Meaning |
|---|---|---|---|
| camera object (from the owner's camera handle) | 0x160 | (object, pointer to a u32 holding 0, int 2) | the camera is told to go away |
| owner | 0x1d8 | (owner) | leave the film (end of an exported film, or the leave flag) |
| owner | 0x1c0 | (owner) | the armed timeout ran out |
| owner | 0x1c8 | (owner) | the owner's timed action is due |
| event object | 0x10 | (event, address of the film read stream) | the event reads its data |
| event object | 0x20 | (event, int 0) | the event is applied to the game |
| event object | 0x18 | (event, address of the re-record write stream) | the event writes its data |
| event object | 0x08 | (event) | the event is released |

### 3.3 Game data

| Offset | Type | Name used below | Meaning |
|---|---|---|---|
| 0x17c5a10 | i32 | MODE | camera mode of the film: 0, 1 or 2 |
| 0x17c3570 | u8 | PAUSE_NOW | pause state the game was last told |
| 0x17c3571 | u8 | PAUSE_WANT | pause state wanted (1 = paused) |
| 0x17c3572 | u8 | PAUSE_AUX | set to 1 whenever this code changes or forces PAUSE_WANT |
| 0x16e716c | u32 (float bits) | SPEED | play speed |
| 0x16e7170 | u8 | LEAVE | non-zero: the film is to be left |
| 0x17c3580 | u64 pointer | CLOCK | the playback clock object; its i32 at +0xc is the current playback time |
| 0x17c5b70 | u8 | EXPORT | non-zero while the film runs as a video export (the old code calls it MPEG): pad controls are off |
| 0x17d1d10 | u8 | B2C | net manager restriction flag: no camera, speed at most 1.0, no bookmarks, no player cycling |
| 0x17d1d11 | u8 | B2D | net manager restriction flag: speed at most 3.0, camera mode 1 not allowed |
| 0x1b24b98 | u64 pointer | GAME | game state object: u32 +0x20 (base time for a clock reset), i32 +0x114 (a time, see phase 20) |
| 0x1805fa8 | u32 | HUD | HUD element flag word |
| 0x17b4a68 | u64 | HOLD | a qword of the clock system object (the object at 0x17b4a50, +0x18); while it is non-zero this code treats the frame like one with a menu up |
| 0x17b4a5d | u8 | - | pause byte of the clock system |
| 0x17b4b34, 0x17b4af4 | u16 each | - | pause counters of world clock 1 and world clock 0 |
| 0x17b4b1c, 0x17b4adc | u32 (float bits) each | - | time scale of world clock 1 and world clock 0 (targets of the scale override) |
| 0x1b377d8 | u64 pointer | - | an object whose i32 at +0x90 takes a speed-derived value (phase 11) |
| 0x17c5a34 | i32 | - | the film writer's "followed player index" |
| 0x17c3ba8 | object | W | the film writer: i32 +0x158 film duration (seconds), i32 +0x1dbc clock time of the film's start, i32 +0x1fc4 bookmark count |
| 0x17c5cf8 | object | R | the re-record reader: its u32 at +8 (0x17c5d00) is non-zero while a re-recording is open |
| 0x17c3838 | bit stream | S | the film's event stream being read: u64 word pointer +0, u32 bit position +8, u32 capacity +0xc |
| 0x17c3850 | bit stream | RR | the re-recording's event stream being written, same layout |
| 0x1ae95e0 | - | - | descriptor of the camera type "none" (only its address is used) |
| 0x13ac250 | text | - | a source-file name (only its address is used) |

Owner (`own`) fields:

| Offset | Type | Meaning |
|---|---|---|
| +0x198, +0x19c | u32 (float bits) each | two camera blend values |
| +0x1a0 | handle: u64 at +0x1a0, u32 id at +0x1a8 | the film camera |
| +0x1b0 | u8 | non-zero: the film follows a player |
| +0x1b1 | u8 | one-shot "resume playing" request |
| +0x1b2 | u8 | non-zero: a screenshot request is running |
| +0x1b8 | i32 | index of the followed player tracker (0..9 in normal use) |
| +0x1bc | u8 | toggle: controls help shown |
| +0x1bd | u8 | toggle: film HUD shown |
| +0x1be | u8 | one-shot "pause" request |
| +0x1bf | u8 | non-zero once the render defaults were taken |
| +0x1c4 | i32 | clock time at which the timeout was armed |
| +0x1c8 | u8 | non-zero: the timeout is armed |
| +0x1f8 | 25 bookmarks of 0x30 bytes (slots 0..24) | per bookmark: u32 +8, u32 +0x10, u32 +0x18, u32 +0x1c, u64 +0x28 are the fields this group clears; +0, +0xc and +0x20 are left alone |
| +0x6a8 | i32 | number of bookmarks |
| +0x6ac | i32 | clock time of the last bookmark |
| +0x6b0 | i32 | slot of the last bookmark |
| +0x6b4 | i32 | clock time of the owner's timed action (0 or negative = none) |

Player tracker fields: u8 +0xe1 (non-zero: the player is usable as a film target), u8 +0x5e4 (non-zero: not
usable), handles at +0x10 and +0x20 (used by `u3x_rp_trk_live`).

Event object fields: u8 +8 (the event's type byte as it is written into a re-recording), u8 +0xa (use count).

### 3.4 Pad button bits (in the word `u3x_rp_pad` points to)

up 0x10, down 0x40, R2 0x200, triangle 0x1000, circle 0x2000, cross 0x4000, square 0x8000.

### 3.5 Message ids (sent with `u3x_rp_send` to `u3x_rp_self(own)`)

| Id | When |
|---|---|
| 0x953b8f2a | play (sent while PAUSE_WANT is non-zero) |
| 0x43348860 | pause (sent while PAUSE_WANT is zero) |
| 0x887c7d6e | next camera mode, sent while MODE is 0 |
| 0x806c9824 | next camera mode, sent while MODE is 2 and B2D is zero |
| 0xff502160 | next camera mode in every other case; also the forced change away from mode 1 under B2D |

### 3.6 HUD flag bits

0x70000: always on during playback. 0x72800 (which contains 0x70000): on only while the HUD toggle is on.
0x80000: the controls help.

### 3.7 Numbers

| Pattern | Value | Use |
|---|---|---|
| 0x41a00000 | 20.0 | speed factor of phase 11 |
| 0x3f800000 | 1.0 | speed cap under B2C; value written to the owner's blend fields |
| 0x40400000 | 3.0 | speed cap under B2D |
| 0x43960000 | 300.0 | clock ticks per second |
| 0x3b5a740e | 0.0033333334 (1/300) | seconds per tick (phase 20, see there) |

Integers: 3000 ticks (10 s) between bookmarks; 750 ticks (2.5 s) timeout; 24 = last bookmark slot; 0x1fc00 bytes
(130048) = fill limit of the re-record stream; 10 trackers; helper counters 40, 41, 42.

## 4. Behaviour

### 4.1 `u3x_rp_cycle_player(base, own, dir, cur)`

Moves the followed-player index (owner i32 +0x1b8) in steps of `dir` to the next tracker that is live, trying at
most 10 times.

One try:

1. Add `dir` to the stored index (32-bit, stored back). If the sum is negative, 10 is added once and stored (a sum
   below -10 stays negative).
2. Candidate number = the stored value taken as an unsigned 32-bit number, modulo 10 (for a negative stored value
   this is the remainder of its two's complement pattern, e.g. -5 gives 1).
3. Candidate tracker = `u3x_rp_trk(base, candidate number)`.
4. If `u3x_rp_trk_live(candidate)` is zero, the try is over; the stored index stays as step 1 left it (it is not
   reduced modulo 10) and the next try continues from it.
5. Otherwise the search ends:
   - If the candidate's address differs from `cur`: when the owner's camera handle (+0x1a0) resolves to an object
     (`u3x_rp_h(own + 0x1a0)`), the virtual call at table offset 0x160 is made on it with a pointer to a 32-bit
     zero and the integer 2. Then, whether or not there was a camera object, the handle is cleared: u64 +0x1a0 = 0
     and u32 +0x1a8 = 0.
   - If the candidate is `cur`, nothing happens to the camera.
   - The stored index becomes the candidate number (0..9).

After 10 tries without a live tracker the function returns with the index as the tries left it. Examples: start 3,
dir +1, nothing live: 13. Start 3, dir -1, nothing live: 3 (2, 1, 0, then -1 becomes 9, 8, ... 3). dir 0: the same
candidate is tested 10 times.

No float maths. The net-game flag is read anew on every try (inside `u3x_rp_trk`).

### 4.2 `u3x_rp_update(base, st)`

General rules:

- `own` is read once from `st` +8.
- Two values are sampled once, in phase 4, and used for the whole call: MENU (is a menu up) and PAD (the pressed
  button word). CONTROLS below means "MENU is false and EXPORT is zero"; MENU is the sample, EXPORT is read when
  the condition is evaluated.
- Every other global and every owner field is read at the moment it is used, never earlier: game calls made in
  between (messages, event applies, overrides) may change them, and the later phases must see the new values.
  Where a phase says "sampled", that value is kept instead.
- The phases run in this order. Each game call, virtual call and call of a function of section 2.3 that reaches the
  game is observable, in order and with its arguments.

Phases:

1. Helper counter 40 is incremented (`ext_eb()[40]`).

2. If MODE is 1 or 2: `u3h_rp_cam_mode12(base, st, own)`. Then, always: `u3h_rp_render_vars(base, own)`.

3. If the owner's screenshot byte (+0x1b2) is non-zero: the result of `rp_screenshot_step(base, st, own)` is
   returned and nothing else happens in this call.

4. Game call 0x7230f0 (net manager, `own`, 0). After it, MENU = `u3x_rp_menu_up(base)` non-zero, and PAD = the word
   `u3x_rp_pad(base)` points to.

5. Toggles and HUD flags. No game call happens in this phase, so only the end values count:
   - If CONTROLS: the HUD toggle (+0x1bd) flips its lowest bit when PAD has circle (enter button is cross) or cross
     (enter button is not cross); the help toggle (+0x1bc) flips its lowest bit when PAD has square. Both can
     happen in the same frame.
   - If not CONTROLS and the byte +0x1bf is zero: `u3x_render_defaults(base)`, then +0x1bf = 1.
   - HUD word: bits 0x70000 are set; then, if the HUD toggle (after the flip) is zero, bits 0x72800 are cleared,
     otherwise set. All other bits keep their value.
   - Help bit: if the help toggle (after the flip) is non-zero and EXPORT is zero, HUD bit 0x80000 is set and phase
     6 is skipped. Otherwise bit 0x80000 is cleared and phase 6 runs.
   The HUD word has its final value before any message of phase 6 is sent.

6. Play / pause and camera mode keys (only when the help is not shown, see phase 5, and only if CONTROLS):
   - If PAD has cross (enter button is cross) or circle (enter button is not cross): send the play message when
     PAUSE_WANT is non-zero, else the pause message (ids in 3.5), to `u3x_rp_self(own)`.
   - Then, if PAD has triangle: send one camera-mode message chosen by the table in 3.5 from MODE and B2D as they
     are now (after the first message was sent).

7. If MODE is 1 and B2D is non-zero (both read now): send 0xff502160 to `u3x_rp_self(own)`. This is independent of
   MENU, EXPORT and the help toggle.

8. Followed player. Only if the owner's follow byte (+0x1b0) is non-zero and B2C is zero:
   - Direction: 0; if CONTROLS: -1 when PAD has down, else +1 when PAD has up (down wins over up).
   - Current tracker `cur` = `u3x_rp_trk(base, index)` with the owner's index (+0x1b8) taken as unsigned. It is
     "usable" when it is not null, its byte +0xe1 is non-zero and its byte +0x5e4 is zero.
   - Not usable: `u3x_rp_cycle_player(base, own, 1, cur)`. Usable and direction not 0:
     `u3x_rp_cycle_player(base, own, direction, cur)`. Usable and direction 0: nothing.

9. Stream updates: game call 0x6a28c0 on W, then 0x6a28c0 on R.

10. If HOLD is zero: game call 0x1019550 with `u3x_rp_cammgr(base)` (the manager is asked for only in this case).

11. Speed. Let s be the float whose pattern is SPEED, read once here. (Float details in 4.3.)
    - n = s x 20.0 cut to a 32-bit integer toward zero. The i32 at +0x90 of the object 0x1b377d8 points to becomes
      n + 20 (32-bit wrap).
    - v = s. If B2C is non-zero: v = s when s <= 1.0, otherwise 1.0 (also when s is not a number). SPEED is not
      changed by this cap.
    - If B2D is non-zero and v > 3.0: SPEED is set to the pattern of 3.0 and v = 3.0.
    - With a local 32-bit word holding the pattern of v: `u3x_ovr_scale(base, base + 0x17b4b1c, &word)`, then
      `u3x_ovr_scale(base, base + 0x17b4adc, &word)`.

12. Pause wish.
    - If HOLD is zero (read now) and MENU is false: `rp_rumble_off(base, 2)`, then `rp_rumble_off(base, 0)`. Then,
      only if PAUSE_WANT is non-zero: when the owner's resume byte (+0x1b1) is non-zero, PAUSE_WANT = 0,
      PAUSE_AUX = 1 and the resume byte = 0; otherwise, when PAD has R2, PAUSE_WANT = 0 and PAUSE_AUX = 1.
      (While PAUSE_WANT is zero the resume byte is left as it is.)
    - Otherwise (HOLD non-zero or MENU): PAUSE_WANT = 1 and PAUSE_AUX = 1.

13. Pause change. If PAUSE_NOW differs from PAUSE_WANT, with v = PAUSE_WANT sampled:
    - the u8 at 0x17b4a5d = v;
    - the two u16 pause counters, 0x17b4b34 first, then 0x17b4af4: v non-zero adds 1 (16-bit wrap); v zero
      subtracts 1 unless the counter is already 0;
    - game call 0x560280 (0, v);
    - PAUSE_NOW = v (the sampled value).

14. End of film. e = game call 0x6a2700 (W). After the call, d = the film duration (W +0x158). If d < e (signed):
    t = film start (W +0x1dbc) + (d x 300.0 cut to a 32-bit integer toward zero), 32-bit wrap (4.3), and
    `u3x_clock_reset(CLOCK, GAME u32 +0x20, t)`.

15. NOW = the playback time (CLOCK i32 +0xc), sampled here (after a reset of phase 14). Then the memory context is
    entered: game call 0xda1590 with the arguments of 3.1.

16. Events. Repeat:
    - Remember the bit position of S (u32 +8). Read a 32-bit time T from S (`u3x_stream_read(base, S, 32)`),
      taken as signed.
    - T = 0 (end of the stream): if EXPORT is non-zero, the owner's virtual call 0x1d8 is made and the position is
      left where the read put it (32 bits further). Otherwise the remembered position is written back,
      PAUSE_WANT = 1, PAUSE_AUX = 1 and game call 0x6a3130 (R) is made. The loop ends.
    - T > NOW (signed): the remembered position is written back; the loop ends.
    - Otherwise the event is due. Read its 8-bit type from S. E = game call 0x6c2b50 (type). Virtual call 0x10 on E
      with S; virtual call 0x20 on E with 0.
    - Then, if a re-recording is open (R u32 +8 non-zero, read now):
      - if the number of bytes RR has in use, (bit position + 7) / 8 with the sum taken in 32 bits, is greater than
        0x1fc00: game call 0xda18b0, helper counter 41 is incremented, and the function returns 1 at once. Nothing
        else of this phase or of the later phases happens (the event's use count is not touched either);
      - otherwise write T as 32 bits to RR, then the event's byte +8 as 8 bits (read from the event after the two
        virtual calls), then make virtual call 0x18 on E with RR.
    - The event's use count (u8 +0xa) is lowered by 1 and stored; if it is now 0, virtual call 0x08 on E.

17. Bookmark. Only if B2C is zero (read now) and NOW - (owner i32 +0x6ac) > 3000 (32-bit wrapping difference,
    signed comparison):
    - If the bookmark count (owner +0x6a8) is greater than 24, the list is thinned first: the count becomes 12;
      then, in this order, slot 1 takes slot 2, slot 2 takes slot 4, slot 3 takes slot 6, slot 4 takes slot 8,
      slot 5 takes slot 10 (slot 0 stays; taking over = `u3x_bookmark_move(target, source)`); then slots 6, 7, 8,
      9, 10, 11 take slots 13, 14, 15, 16, 17, 18; then in each of the slots 12..24 the fields u32 +8, u32 +0x10,
      u32 +0x18, u32 +0x1c and u64 +0x28 are set to 0 (the fields +0, +0xc and +0x20 keep their values).
    - c = the count now. Owner +0x6b0 = c. `u3x_bookmark_create(base, slot c, playback time)` with the playback
      time read from CLOCK +0xc at this moment (not NOW). Slot c is at owner + 0x1f8 + c x 0x30; c is not checked
      against a lower bound.
    - The count is read again, 1 is added, and the result is stored in owner +0x6a8 and in W +0x1fc4. Owner +0x6ac
      = NOW. Helper counter 42 is incremented.

18. Camera. The i32 at 0x17c5a34 = -1. Then, if B2C is zero (read now): `u3h_rp_cam_target(base, own)`. Otherwise:
    game call 0x101b530 with `u3x_rp_cammgr(base)` and the arguments of 3.1, then owner u32 +0x198 = 0x3f800000.

19. If the owner's pause byte (+0x1be) is non-zero: PAUSE_WANT = 1, PAUSE_AUX = 1, the byte = 0. (This takes effect
    in the game at phase 13 of the next frame.)

20. One of three, the first that applies:
    - LEAVE non-zero: owner virtual call 0x1d8.
    - The owner's timeout byte (+0x1c8) is non-zero and playback time (CLOCK +0xc, read now) - (owner i32 +0x1c4)
      > 750 (32-bit wrapping difference, signed comparison): owner u32 +0x19c = 0x3f800000 and u32 +0x198 =
      0x3f800000, then owner virtual call 0x1c0.
    - Otherwise, with a = owner i32 +0x6b4: if a > 0 and a <= GAME i32 +0x114 (signed): owner virtual call 0x1c8.
      (The old code tests "a x (1/300) > 0.0" on the x87; for every 32-bit integer that is exactly a > 0.)
    When the timeout byte is set but the 750 ticks are not over, the third case is tested.

21. Game call 0x125c770 (`st`), then game call 0xda18b0. Result 1.

### 4.3 Float maths (x87) and rounding points

The helper is built without SSE; the three float computations run on the x87 unit. No value is passed to or
returned from a function as float or double.

Speed (phase 11):

- s is loaded from the 32-bit pattern of SPEED.
- n: the product s x 20.0 is NOT stored as a 32-bit float before the conversion. It goes straight from the x87
  register to a 32-bit integer store with rounding toward zero (the C++ conversion of a float expression to
  int32_t does this). Results: for a product whose integer part fits in a signed 32-bit integer, that integer part;
  for anything else (too large either way, infinite, not a number) the pattern 0x80000000, so the stored value is
  then 0x80000014. With the x87 precision at 53 or 64 bits the product is exact (24-bit x 5-bit), so n is the
  integer part of the exact product.
- v involves no arithmetic, only comparisons: "s <= 1.0" and "v > 3.0" are ordered comparisons, false when the
  value is not a number. So under B2C a NaN gives 1.0; without B2C a NaN stays, and is never above 3.0. (The old
  source writes the first test as "1.0 - s >= 0.0 selects s, else 1.0"; the two forms give the same result for
  every pattern.)
- The pattern handed to the two overrides is the 32-bit pattern of v. v is always a value that is exact in 32 bits
  (s itself, 1.0 or 3.0), so there is no rounding. One detail of the shipped build: v travels through an x87
  register, so a signalling NaN in SPEED (exponent all ones, bit 22 clear, mantissa not zero) arrives with bit 22
  set and all other bits unchanged when B2C is zero. See the open questions.
- SPEED itself is written only by the 3.0 cap.

Film end (phase 14):

- d (a 32-bit integer) is converted to the x87 without rounding to 32 bits, multiplied by 300.0, and the product
  goes straight to a 32-bit integer store with rounding toward zero. There is no 32-bit float store in between.
  Result: d x 300 when that fits in a signed 32-bit integer (|d| <= 7158278), else the pattern 0x80000000. Then the
  film start is added with 32-bit wrap.

Timed action (phase 20): the comparison there has the exact integer form given in the phase; no float is needed.

### 4.4 Edge cases

- `u3x_rp_trk` returns null while the net-game byte 0x17d5c61 is zero. In phase 8 a null current tracker counts as
  not usable, and `u3x_rp_cycle_player` then passes the null candidate to `u3x_rp_trk_live`, which reads through
  it: the old code faults there. Film playback always has the net game active; nothing needs to be added for this
  case, and tests keep the byte non-zero whenever phase 8 runs.
- The index passed to `u3x_rp_trk` in phase 8 is not range checked (after a failed cycle it can be 10 or more).
- `u3x_rp_self(own)` may be null; the message is sent with the null target all the same.
- `u3x_rp_cammgr` may return null (after raising the game's assert); the game call is made with null all the same.
- The pointer 0x6c2b50 returns is used without a null check.
- A bookmark count below 0 is not guarded.
- The byte count of RR in phase 16 is taken from a 32-bit sum, so the limit applies to bit positions 0xfe001 to
  0xfffffff8; the positions 0xfffffff9 and above wrap and count as empty. This cannot be reached with a real
  stream (the write would land far outside it) and is not a test case.
- Bookmark count exactly 24: slot 24 is filled and the count becomes 25; the thinning happens at the next bookmark.
- Phase 3 returns before the memory context is entered, so no 0xda18b0 is called there. The early return of phase
  16 leaves the context exactly once.

## 5. Test scenarios

Fake image with the globals of 3.3, an owner with a function table, a state object pointing to it, recording
stubs for the 11 game functions, the virtual slots, the three `u3h_` exports and the game calls behind
`u3x_rp_send`, `u3x_ovr_scale`, `rp_rumble_off`, `u3x_bookmark_create` (0x1197380, 0x12284e0, 0xd9e990, 0x6b1b90,
0x6b1dc0, 0x7427d0, 0x72e560). Compare memory, the result and the sequence and arguments of the calls. The
existing functions of 2.3 also read: the pad object pointer 0x16e2fa0, the system settings pointer 0x1b24b80, the
menu object 0x17bd080, the camera manager handle 0x1d52b80, the render settings pointer 0x16e2fb8, the assert
handler pointer 0x1bf27b8 (keep it 0 and keep the streams within capacity and the camera manager present, or stub
the assert), and the helper's ExtState.

`u3x_rp_cycle_player`:

1. dir +1 from index 3, tracker 4 live and equal to `cur`: index 4, no camera call, handle untouched.
2. Same, tracker 4 live and not `cur`, camera handle valid: virtual call 0x160 (zero word, 2), handle cleared,
   index 4.
3. Same, camera handle not valid (null pointer; id 0; id mismatch): no virtual call, handle cleared.
4. dir -1 from index 0: candidate 9; dir -1 with only tracker 7 live from index 2: passes -1 -> 9, ends at 7.
5. dir +1 from index 9 with tracker 0 dead and tracker 1 live: stored 10, 11 on the way, ends at 1.
6. No tracker live, dir +1 from 3: ends 13. dir -1 from 3: ends 3. dir 0: index unchanged, 10 tests.
7. Start values -1, -5, -15, 10, 25, 0x7fffffff with dir +1, -1, 0, +3, -12 (random): candidate numbers and the end
   index as in 4.1.
8. Only one handle of the candidate valid (+0x10 only, +0x20 only).

`u3x_rp_update`:

9. MODE 0, 1, 2, 3, -1: `u3h_rp_cam_mode12` only for 1 and 2; `u3h_rp_render_vars` always, after it.
10. Screenshot byte set: only phases 1 to 3; result = what the step returns; no other game call.
11. Controls: every combination of MENU, EXPORT, enter button, and PAD bits circle / cross / square, with both
    toggle start values: flips, render defaults once (byte +0x1bf 0 and 1), HUD word from several start patterns
    (0, 0xffffffff, random).
12. Help shown (toggle 1, EXPORT 0): bit 0x80000 set, no message even with cross / circle / triangle pressed.
    Help toggle 1 with EXPORT 1: bit cleared, no message (controls off).
13. Play / pause: PAUSE_WANT 0 and 1, both enter settings; triangle with MODE 0, 1, 2 and B2D 0 / 1; both keys in
    one frame (two messages in order); a message stub that changes MODE, B2D or PAUSE_WANT (the second message and
    phase 7 use the new values).
14. Phase 7: MODE 1 with B2D 1 sends 0xff502160 also with a menu up, during an export and with the help shown.
15. Phase 8: follow byte 0; B2C 1; current tracker usable / +0xe1 zero / +0x5e4 set; PAD up, down, both, none;
    controls off (direction 0); cycle called exactly when 4.2 says, with the right direction and `cur`.
16. HOLD zero / non-zero: 0x1019550 and the rumble calls present or absent; MENU with HOLD zero: no rumble, pause
    forced.
17. Speed patterns: 0, 0.25, 0.5, 1.0, the next float above 1.0, 2.0, 3.0, the next float above 3.0, 8.0, negative
    values, -0.0, denormals, 1e9 (n out of range), -1e9, +inf, -inf, quiet NaN; each with B2C / B2D in all four
    combinations: the i32 at +0x90, SPEED afterwards, the two override patterns. Thousands of random patterns.
18. Pause wish: PAUSE_WANT 0 / 1, resume byte 0 / 1, R2 pressed or not: end values of PAUSE_WANT, PAUSE_AUX, resume
    byte (resume byte kept while PAUSE_WANT is 0; resume byte and R2 together).
19. Pause change: PAUSE_NOW equal / different; v 0 with counters 0, 1, 0xffff; v 1 with counters 0, 0xffff (wrap to
    0); order: byte, counters, call, PAUSE_NOW.
20. Film end: d < e, d = e, d > e; d = 0, 1, 7158278, 7158279, negative, 0x7fffffff; start values that wrap; clock
    contents after the reset and NOW taken from it.
21. Events: empty stream (T = 0 first) with EXPORT 0 (position restored, pause forced, 0x6a3130) and 1 (owner slot
    0x1d8, position advanced); first T > NOW (position restored, nothing else); T = NOW; negative T; several due
    events of different types followed by a later one; use count 1 (released), 2 (not released), 0 (wraps to 255,
    not released).
22. Re-recording open: RR position giving 0x1fc00 bytes exactly (written) and one bit more (early return: one
    0xda18b0, counter 41, no bookmark, no camera phase, no 0x125c770, use count untouched); the event's byte +8
    different from the type read and changed by the apply stub (the changed byte is written); R +8 changed by an
    apply stub between two events.
23. Bookmarks: B2C 1; difference 3000 (no) and 3001 (yes); wrapping difference; count 0, 5, 24, 25, 30: slot used,
    +0x6b0, counts in owner and W, +0x6ac = NOW, counter 42; thinning: which slots were moved in which order and
    which fields of slots 12..24 are zero, the others untouched; a create stub or event stub that changes the
    clock time (the create gets the new time, +0x6ac gets NOW).
24. Camera: B2C 0 (`u3h_rp_cam_target`), B2C 1 (0x101b530 with its two local words and null, +0x198 = 1.0);
    0x17c5a34 = -1 in both.
25. Owner pause byte 0 / 1.
26. Last choice: LEAVE 1 (slot 0x1d8 even when the timeout is due); timeout byte 1 with difference 750 (not yet)
    and 751 (both blend fields 1.0, slot 0x1c0); timeout byte 1 not due with a due timed action (slot 0x1c8);
    a = 0, -1, 1, equal to GAME +0x114, one above it, GAME +0x114 negative.
27. A full normal frame: order of all calls from 0x7230f0 to the final 0x125c770 and 0xda18b0.

Mutations a test should catch: swapped circle / cross roles, up winning over down, a bookmark period of 3000
inclusive, slots 6..11 fed from 12, 14, ..., a restored stream position at the end of an export, PAUSE_NOW read
back after the game call, NOW used for the bookmark snapshot, the product of phase 11 rounded to 32 bits or to
nearest, the cap of B2C written back to SPEED.

## 6. Things in the old code that look like bugs or dead code (behaviour to keep)

1. Thinning the bookmarks: slots 1..5 take every other one (2, 4, 6, 8, 10), but slots 6..11 take 13..18, not
   12, 14, ..., 22. The bookmarks kept are therefore the old 0, 2, 4, 6, 8, 10, 13, 14, 15, 16, 17, 18.
2. The old thinning also "moves" slot 0 onto itself (same address as target and source). That changes no memory and
   makes no game call; it need not be repeated.
3. `u3x_rp_cycle_player` leaves the index outside 0..9 when no tracker is live (start + 10 x dir for dir > 0), and
   the next frame uses it unchecked.
4. The null-tracker fault of 4.4 (net-game byte zero while a player is followed).
5. At the end of the stream during an export the read position is not put back.
6. The early return on a full re-record stream skips the release of the event that was just read and applied,
   the bookmark, the camera phase and the generic state update of that frame.
7. The old byte-count test of the re-record stream masks the shifted value with 0x1fffffff, which changes nothing.
8. PAUSE_AUX is only ever set to 1 here.
9. Phase 20 compares in float what is an integer sign test; the B2C cap is written as a subtraction and a select.
   Both have exact simple forms (given above).
10. `u3x_rp_cammgr` can be asked twice in one frame (phases 10 and 18); each may raise the assert.
11. The speed-derived integer of phase 11 is written even when it is the out-of-range pattern plus 20.
12. Helper counters 40 to 42 are the helper's own diagnostics (frames, re-record overflows, bookmarks); a comment
    elsewhere in the C part names counters 40 and 41 for another diagnostic, but nothing else writes them in v7.212.

## 7. What stays as it is

- The C wrapper `rp_update` and the kind 74 branch of `cinema_kind` (the helper's own dispatch; one forwarding
  line).
- The exports `u3h_rp_cam_mode12`, `u3h_rp_render_vars`, `u3h_rp_cam_target` (their bodies belong to G01 / G02).
- All functions of 2.3 (trivial accessors, earlier rewrites, or other groups' work).
- The x87 helpers and the build without SSE.

## 8. Open questions

1. Signalling NaN in SPEED (4.3): the shipped build quiets it (bit 22 set) on the way to the overrides because the
   value passes through an x87 register; the old source text itself only copies the pattern. A new implementation
   may or may not be compiled the same way. The game writes SPEED from a few fixed speeds, so this is not a real
   input. Proposal: the test leaves signalling NaNs out (or accepts both patterns).
2. `rp_screenshot_step` and `rp_rumble_off` are file-private names; if G02's new code renames the screenshot step,
   phase 3 has to call the new name (same arguments, same result).
3. The meaning of some fields is known only by use (owner +0x1c4 / +0x1c8 timeout, +0x6b4 timed action, GAME
   +0x114, the object behind 0x1b377d8). The description gives what is done with them, which is all the code needs.

## 9. Clarifications (tester, 2026-11-14)

Facts settled by the old-against-new test (`reimpl2/G03/test_g03.cpp`), in words. Where a number of calls is given,
the test counts exactly that number.

1. **The enter-button query (`u3x_rp_enter_cross`) is made by position, not by need.**
   - Phase 5, when CONTROLS holds: exactly one query, before either toggle is flipped, whatever PAD holds (also when
     PAD has neither cross nor circle). Its answer says which of circle / cross flips the HUD toggle.
   - Phase 6, when it runs (the help is not shown and CONTROLS holds): exactly one more query, whatever PAD holds. It
     is made after the HUD word has its final value and before PAUSE_WANT is read and `u3x_rp_self` is asked. Its
     answer says which of cross / circle is the play / pause key.
   - Per frame therefore: 2 queries with the controls on and the help not shown, 1 with the controls on and the help
     shown, 0 with a menu up, during an export, or when phase 3 returns. PAD has no influence on the number.

   | Frame | PAD none | PAD cross | PAD circle | PAD triangle |
   |---|---|---|---|---|
   | controls on, help not shown | 2 | 2 | 2 | 2 |
   | controls on, help shown | 1 | 1 | 1 | 1 |
   | menu up, or export running | 0 | 0 | 0 | 0 |

   ("Help shown / not shown" is the state after the square key of this frame was applied: a square press that
   switches the help off gives 2 queries, one that switches it on gives 1.)

2. **HUD word.** With the HUD toggle zero all of 0x72800 ends up cleared, the "always on" bits 0x70000 included
   (start 0xffffffff, help off: 0xfff0d7ff; start 0x00070000: 0). With the toggle non-zero all of 0x72800 is set
   (start 0: 0x00072800). No other bit than 0x72800 and 0x80000 ever changes. A key flips only the lowest bit of a
   toggle byte (2 becomes 3, 0xff becomes 0xfe); "zero / non-zero" always looks at the whole byte.

3. **Phase 6 is decided once.** "Help not shown" and CONTROLS are evaluated before the play / pause message and not
   again: when the play / pause message's receiver sets EXPORT, opens a menu, sets the help toggle or clears the pad
   word, the triangle message of the same frame is still sent. What IS read at its own moment: PAUSE_WANT (before
   the first message), MODE and B2D for the triangle message (after the first message), MODE and B2D again for
   phase 7 (after both), and the owner's own handle (`u3x_rp_self`) once per message.
   Nothing between the start of phase 5 and the CONTROLS test of phase 6 can change EXPORT (only the two
   memory-only helpers `u3x_rp_enter_cross` and `u3x_render_defaults` run there), so it cannot be told apart
   whether phase 6 reads EXPORT again or uses the value of phase 5. Phase 8 reads EXPORT at its own moment (a
   message that sets EXPORT gives direction 0 there).

4. **The pause value is not normalised.** In phase 13 the byte v of PAUSE_WANT goes unchanged (5 stays 5, 0xff stays
   0xff) into the byte 0x17b4a5d, into the second argument of 0x560280 (as an unsigned value) and into PAUSE_NOW;
   the counters count up for every non-zero v. The test "PAUSE_NOW differs from PAUSE_WANT" compares the two bytes
   as they are (NOW 1 with WANT 2 is a change). If the game call changes PAUSE_WANT, PAUSE_NOW still gets the value
   sampled before the call.

5. **Order of reads in phases 2 and 3.** `own` is read from the state once, before the first call; a call that puts
   another owner into the state has no effect on the rest of the frame. MODE is read once, before the mode work
   (mode work that sets MODE to 0 does not stop the render settings call, which is made in every case). The
   screenshot byte is read after both exports: when either of them sets it, the screenshot step runs in this frame.

6. **Order of reads in phase 8.** Follow byte and B2C first (read after the messages of phases 6 and 7), then the
   direction (EXPORT read now, MENU and PAD from the samples), then the tracker of the index (the index is handed
   over as an unsigned 32-bit value exactly as stored, also 10, 1000, 0x7fffffff or a negative value's pattern),
   then that tracker's bytes +0xe1 and +0x5e4. The current tracker handed to the cycle is the pointer just obtained.

7. **Order of reads in phase 20.** LEAVE, then the timeout byte, then (only when it is set) the clock time and
   +0x1c4, then +0x6b4, then (only when that is above 0) the GAME pointer and its +0x114. With a timed-action value
   of 0 or below the GAME pointer is not read at all (a null GAME pointer is harmless then). All of them are read
   after the camera phase: a camera call that sets LEAVE, arms a timeout that is already due, or moves the clock
   past an armed one takes effect in the same frame. The two blend fields are written before the owner's call
   0x1c0.

8. **Bookmark slot address.** Slot c is at owner + 0x1f8 + c x 0x30 with c taken as a signed value extended to 64
   bits: c = -1 gives owner + 0x1c8, c = -44739243 gives owner - 0x7ffffe18, c = -0x80000000 gives
   owner - 0x17fffffe08. Owner +0x6b0 gets c before the snapshot call. After the call the count is read again
   (a snapshot call that changes the count is honoured), 1 is added with 32-bit wrap, and the sum goes to the owner
   and to W +0x1fc4. Counts 25 and above (also 0x7fffffff) are thinned to 12 first, so slot 12 is used and the count
   becomes 13.

9. **Thinning: the moves.** Eleven `u3x_bookmark_move` calls, in this order (target, source): (1,2) (2,4) (3,6)
   (4,8) (5,10) (6,13) (7,14) (8,15) (9,16) (10,17) (11,18). The count 12 is already stored when the first move is
   made; the fields of slots 12..24 are cleared after the last move and before the snapshot call.

10. **Cycle: stores around the camera's call.** At the moment of the camera's virtual call (table offset 0x160) the
    owner's index holds the stepped value of step 1 (not yet reduced: e.g. 11 on the way from 9 to 1) and the
    camera handle is still complete. After the call the handle (u64 and u32) is cleared and the index gets the
    candidate number, also when the call itself wrote other values into these fields. The word the call's second
    argument points to holds 0; the third argument is 2.

11. **Triangle message for every MODE / B2D.** MODE 0: 0x887c7d6e (any B2D). MODE 2 with B2D zero: 0x806c9824.
    Everything else (MODE 1, MODE 2 with B2D non-zero, MODE 3, -1, any other value): 0xff502160. B2D counts as set
    for every non-zero byte. With MODE 1 and B2D non-zero the triangle message 0xff502160 is followed by the
    phase 7 message 0xff502160 (two messages), unless the first one's receiver changed MODE or B2D.

12. **Phase 16 details.** The value handed to 0x6c2b50 is the 8-bit type as read (0..255). The event's function
    table is looked up anew for each of its calls. The re-record byte written is the event's byte +8 as it is after
    the "read" and "apply" calls. A use count of 0 wraps to 255 and does not release. The "re-recording open" word
    is read again for every event (non-zero in any bit counts as open).

13. **Float conversions (4.3), checked on the x87 unit.** Old and new were compared with x87 code (same instruction
    sequences in a host build and in a build with the helper's flags) at precision 24, 53 and 64 bits and all four
    rounding modes: in every one of these settings old and new give the same results in phase 11 and phase 14; the
    conversion to the integer always cuts toward zero whatever the rounding mode. (At 53 and 64 bits the product is
    exact, as 4.3 says; at 24 bits the x87 unit rounds the product itself to 24 bits before the cut, in old and new
    alike.) Signalling NaN in SPEED (open question 1): with B2C zero the old code
    hands the overrides the quiet pattern (bit 22 set, e.g. 0x7f800001 becomes 0x7fc00001); a new implementation
    that copies the pattern hands over 0x7f800001. With B2C non-zero both give 1.0. SPEED itself and the integer at
    +0x90 (0x80000014) are the same. The comparison leaves signalling NaNs out.

14. **Wrapping differences.** "NOW - (+0x6ac) > 3000" and "clock - (+0x1c4) > 750" are 32-bit wrapping
    subtractions followed by a signed comparison in the old code as compiled (NOW 0x80000000 with +0x6ac
    0x7fffffff gives the difference 1: no bookmark).

15. **Phase 5 has nothing observable in between (checked on the two helpers it calls).** `u3x_rp_enter_cross` only
    reads one byte of the system settings. `u3x_render_defaults` reads the render settings and writes only the film
    writer's fields +0x1e94 to +0x1fab; EXPORT (which is the writer's byte +0x1fc8), the writer's bookmark count
    (+0x1fc4), the HUD word, the toggles and the pad word are not among them. v7.212 writes the bits 0x70000 into
    the HUD word before the toggles are handled and the rest afterwards; because neither helper looks at the HUD
    word, only its value at the end of phase 5 can be seen from outside (a single write of the final value is the
    same). What can be seen of phase 5 from outside is therefore: the number and the moments of the enter-button
    queries (item 1), the render-defaults call, and the end values of the two toggles, of +0x1bf and of the HUD
    word.

16. **Item 1 is the whole difference between "ask by need" and v7.212.** A frame code that asks for the enter
    button only when PAD has cross or circle makes 0 queries where v7.212 makes 2 (help not shown) or 1 (help
    shown); with cross or circle in PAD it makes the same queries at the same moments. Nothing else in a frame
    depends on it (memory, result and every other call are the same either way).

17. **Bookmark thinning loop bounds.** The moves of slots 6..11 and the clearing of slots 12..24 are 6 moves and 13
    slots in every case: nothing that runs in between (the eleven `u3x_bookmark_move` calls, which only write the
    target bookmark handed to them and that bookmark's own buffer) can change the count that was just set to 12.
