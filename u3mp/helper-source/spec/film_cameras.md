# film_cameras: the film player's cameras, scope HUD and the size text (b003)

Group of the clean-room pipeline (reimpl2). Old code: `u3_lan_profiles.c` v7.212, hand-written part, the functions
`rp_cam_target`, `rp_follow_cam`, `rp_follow_set` (with its flat-direction step), `rp_fixed_set`, `rp_death_look`
(with the quaternion blend it uses), `rp_scope_hud`, `rp_cam_mode12`, `cin_fixed` and the body of
`u3h_cin_megabytes`. Everything here is behaviour; nothing is copied from the old statements.

Not part of this group (described by the film maths group, used here as black boxes with float arrays in memory):

| name used below | what it computes |
|---|---|
| WORLD(frame) | world location (position 4 floats, rotation quaternion 4 floats) of a bound frame; it first refreshes the frame's parent through `u3x_frame_parent` |
| LOCALPOS(frame, p) | writes the frame's local position (+0x40, 4 floats) so that its world position becomes p (4 floats in, all 4 lanes used) |
| LOCALROT(frame, q) | writes the frame's local rotation (+0x50) = conjugate(parent rotation) (x) q |
| XAXIS(q), ZAXIS(q) | the x / z axis of a quaternion, 4 lanes each |
| NORM(v, default) | v scaled by 1/length(xyz), all 4 lanes; the default vector when the squared length is <= 1e-30 |
| LOOKAT(dir, up) | the look-at rotation (quaternion) for a direction and an up vector |

Already one-line wrappers around C++ and therefore left out: `rp_cinema_set` (= `u3x_camera_cinema_set`),
`rp_hud_fade` (= `u3x_rp_hud_fade`). Also wrappers and only used here: `rp_hud_flag`, `rp_hud_weapon`, `rp_send`,
`rp_h`, `rp_trk`, `rp_dead`, `rp_trk_by_id`, `rp_isa`, `bf_identity`, `bf_make`, `bf_parent`, `rp_cam_spawn`,
`rp_camtype`, `rp_rerecord_cam`.

Added to the list given for the group, because they sit next to it and still hold logic in C:
`rp_follow_dir` (flat direction of the Follow camera, only used by the Follow setter), `rp_cam_mode12` (free camera
of modes 1 and 2), the three "set world rotation / position / locator" compositions (`bf_set_rot`, `bf_set_pos`,
`bf_set_loc`: parent refresh, then LOCALROT / LOCALPOS), and `rp_slerp` with its sine helper `rp_sinq` (pure maths,
only used by the death look, always inlined into it, and not in the maths group's list: see section 4.6 and the open
questions).

Overlap with other specs in this folder: `G06_cinema_rows.md` section 4.5 specifies `u3x_cin_megabytes` as an
integer rule; section 4.9 here gives the same function as its float steps (the form of the old `cin_fixed`) and both
agree, including the one rounding exception. Implement it once. `G03_film_playback.md` (the caller, `u3x_rp_update`)
counts `rp_cam_mode12` to another group; if that group's spec also describes it, the two must agree with 4.8.

## 1. Purpose and callers

The film player (RePlaying state, PS3 1.19 f_279d6c, PS4 state vtable 0x167e180 slot 5) runs `u3x_rp_update` once per
frame. Two of its steps are still C:

* early in the update, when the camera mode is 1 or 2: `u3h_rp_cam_mode12(base, st, own)` -> the free camera step
  (PS3 1.19 0x279de4..0x27a00c);
* late in the update, when the byte at image 0x17d1d10 is 0: `u3h_rp_cam_target(base, own)` -> the camera that looks
  at the followed player (PS3 1.19 0x27b6b0..0x27c3f0). Directly before, the update sets the followed index
  (int32 at 0x17c5a34) to -1.

`rp_cam_target` is the only caller of the death look, the scope HUD and the follow / fixed camera step; that step is
the only caller of the Follow and the Fixed setter.

The Cinema film list (`u3x_cinema_rows`, column 0x0ed4812e) calls `u3h_cin_megabytes(base, s, b, e)` to print a
film's size in megabytes with two decimals; that export is the only user of `cin_fixed`.

## 2. Interface

No float is passed or returned by value. A "location" is a block of 8 floats (0x20 bytes): position x y z w, then
rotation quaternion x y z w. A "frame" is a 0x60-byte bound frame (section 3), 16-byte aligned.

| new function | parameters / result | called by |
|---|---|---|
| `void u3x_rp_cam_target(uintptr_t base, uint8_t *own)` | image base; the film player's owner object | `u3x_rp_update` (today through `u3h_rp_cam_target`) |
| `void u3x_rp_cam_mode12(uintptr_t base, uint8_t *st, uint8_t *own)` | base; the state object; the owner | `u3x_rp_update` (today through `u3h_rp_cam_mode12`) |
| `void u3x_rp_follow_cam(uintptr_t base, uint8_t *own, uint8_t *target, uint8_t *frame, uint32_t fov_bits, int ok)` | owner; target object (player or body, never null from the caller); the view frame; field of view as a 32-bit float pattern; whether the frame is valid | `u3x_rp_cam_target` |
| `void u3x_rp_follow_set(uintptr_t base, uint8_t *cam, const uint8_t *target)` | a Follow camera; target or null. Reads the three follow sliders itself (the old function got values derived from them as float arguments; its only caller always passed exactly those) | `u3x_rp_follow_cam` |
| `void u3x_rp_fixed_set(uintptr_t base, uint8_t *cam, uint32_t fov_bits, const uint8_t *target, const void *location)` | a Fixed camera; fov pattern; target or null; the world location to take | `u3x_rp_follow_cam` |
| `void u3x_rp_death_look(uintptr_t base, uint8_t *frame, const uint8_t *tracker)` | frame to turn; the followed player's tracker | `u3x_rp_cam_target` |
| `void u3x_rp_scope_hud(uintptr_t base, uint8_t *player, uint32_t fov_bits)` | followed player; the view's fov pattern | `u3x_rp_cam_target` |
| `void u3x_cin_megabytes(uintptr_t base, void *s, char *out, const uint8_t *entry)` | base; helper state (for the text lookup); a 0x40-byte text buffer; a film list entry | `u3x_cinema_rows` (today through `u3h_cin_megabytes`) |
| `void u3x_rp_slerp(const float *q0, const float *q1, uint32_t t_bits, float *out)` | two quaternions, blend factor pattern, 4 floats out. Only if the maths group does not deliver the blend (open question 2) | `u3x_rp_death_look` |

The old code handed the fov both as a float and as its pattern (`fov`, `fixfov`); both always held the same value, so
one pattern is enough. Storing the pattern as a 32-bit word equals the old float store for every value except a
signalling NaN (which the x87 load quiets); not reachable.

One-line C wrappers that remain until the callers are switched (each passes its arguments through unchanged):
`rp_cam_target(base, own)` -> `u3x_rp_cam_target`; `rp_cam_mode12(base, st, own)` -> `u3x_rp_cam_mode12`;
`u3h_cin_megabytes(base, s, b, e)` -> `u3x_cin_megabytes`. Simpler: `u3x_rp_update` and `u3x_cinema_rows` call the
new functions directly and the three exports `u3h_rp_cam_target`, `u3h_rp_cam_mode12`, `u3h_cin_megabytes` go away.
`rp_follow_cam`, `rp_follow_set`, `rp_follow_dir`, `rp_fixed_set`, `rp_death_look`, `rp_scope_hud`, `cin_fixed`,
`rp_slerp`, `rp_sinq`, `bf_set_rot`, `bf_set_pos`, `bf_set_loc` have no caller outside this group and disappear
from C with no wrapper.

What must stay in C: nothing of this group's logic. No game function called here takes or returns a float in a
register (floats only travel through pointers), so `u3h_rp_fcall` is not needed. The new code uses the existing
exports `u3h_text` (text lookup) and, if wanted, `u3h_cin_cat` / `u3h_cin_num`, and the maths group's functions.

## 3. Game facts

All addresses are image offsets (add `base`). Calls use the System V x86-64 convention; none has float arguments.

### 3.1 Game functions

| image | arguments | result | meaning |
|---|---|---|---|
| 0x685b80 | rdi = player + 0x400, rsi = frame (0x60 bytes, 16-aligned, in/out), rdx = pointer to a float (in/out, field of view) | al: non-zero = success | the player's own view frame and fov (PS3 1.19 f_2a0d08, 1.10 f_29a710, called with player + 0x2f0) |
| player vtable slot at byte offset 0x3d8 | rdi = player | rax = weapon or null | GetWeapon (PS3 vtable +0x1e4) |
| 0xd436b0 | rdi = weapon | rax = scope record or null | PS3 f_6cf864 / 1.19 f_6df658 |
| 0x1196230 | rdi = frame, esi = 0 | none | unbind the frame from its rigid body (PS3 1.19 f_953650) |
| 0x7a3e30 | rdi = owner + 0x6c0 (free-camera controller), rsi = frame (in/out) | none | free camera controller playback (PS3 f_385ad4 / f_376cc0) |

### 3.2 Existing C++ functions used (all `extern "C"` in `u3x_fixes.cpp`)

`u3x_rp_h(handle)` (object of a {u64 slot pointer, u32 id at +8} handle, or null), `u3x_rp_trk(base, i)` (tracker i,
null when no net game), `u3x_rp_dead(tracker)`, `u3x_rp_trk_by_id(base, id)`, `u3x_rp_isa(base, object, class hash)`,
`u3x_rp_send(base, message, target)`, `u3x_frame_identity(frame)`, `u3x_frame_make(frame, location)`,
`u3x_frame_parent(base, frame)`, `u3x_camera_type(base, fixed)` (0 = the Follow type object, 1 = the Fixed one),
`u3x_camera_spawn(base, own, type, fov_bits, location or null, target or null)` (spawns a camera and stores its handle
at owner +0x1a0 / +0x1a8), `u3x_camera_cinema_set(cam, location)`, `u3x_camera_rerecord(base, own)`,
`u3x_rp_hud_flag(base, flag)`, `u3x_rp_hud_fade(base, a, b, c, i, byte)`, `u3x_rp_hud_weapon(base, weapon)`.
From the C part: `u3h_text(base, s, id)` (never null; the empty string when the lookup is unavailable).

### 3.3 Globals

| image | type | meaning |
|---|---|---|
| 0x17c5a10 | int32 | camera mode: 0 = the followed player's view (Fixed camera), 1 = free camera, other = orbit (Follow camera) |
| 0x17c5a14 | char[] | film writer: name of the followed player (C string; the next field starts 0x20 bytes later) |
| 0x17c5a34 | int32 | film writer: followed index |
| 0x17c5b54 | float | follow-camera fov slider (PS3 1.19 writer +8088) |
| 0x17c5b58 | float | follow distance slider (+8092) |
| 0x17c5b5c | int32 | follow flag (+8096) |
| 0x17cb3d0 | pointer | net manager: current tracker |
| 0x17b4a68 | u64 | tested for zero only (PS3 1.19 0x11dfadc + 0x18) |
| 0x17b4ca4 | int32 | current time of clock 7, in ticks of 1/300 s |
| 0x16ea550 | location (0x20 bytes) | the screen-0 camera locator (PS3 1.10 0x13291e0) |
| 0x16ea7c0 | 3 floats | second vector of the camera locator (PS3 locator +0x1b0) |
| 0x17c3580 | pointer | the film clock object; int32 at +0xc = its current time |
| 0x17b30e8 | object | the kCameraCinema camera type object (its address is the type argument) |

### 3.4 Structures

Bound frame (0x60 bytes): +0x00 and +0x04 u32 0x7fcccccc, +0x08 rigid-body handle slot, +0x10 u32 id, +0x20 parent
position (4 floats), +0x30 parent rotation, +0x40 local position, +0x50 local rotation.

Owner (film player): +0x198 u32 and +0x19c u32 (float patterns, only 0 or 1.0 are written here), +0x1a0 camera
handle (u64 slot pointer, u32 id at +0x1a8), +0x1b0 u8 "a player is followed", +0x1b8 u32 followed selection,
+0x6c0 free-camera controller, +0xa60 int32 (free camera active when non-zero).

State object: +0x18 int32 = next re-record time.

Tracker (0x9c0 bytes each): +0x00 position (4 floats), +0x10 handle of the player, +0x20 handle of the body,
+0xe1 name (C string), +0x39c int32 death time (clock-7 ticks; PS3 1.19 +644), +0x3a0 used by `u3x_rp_dead`.

Player / body object: +0x70 u64 and +0x7c u32 = its own handle (slot pointer, id). Player: +0x30 u32 class id
0xf2c86c22, +0x3f8 u32 net id, +0x400 the sub-object for 0x685b80. Body: +0xb0 a bound frame (0x60 bytes).

Camera object (PS3 offsets in brackets): +0x30 u32 class id, +0xb0 u64 / +0xb8 u32 target handle [+0x70/+0x74],
+0xc8 u8 flags, bit value 2 [bit 62 of the PS3 u64 +0x80].
* Follow (class hash 0x54aee225) and Cinema (0xb1f56f8c): +0x1a8 u32 [+0x158], +0x1ac 3 floats [+0x15c], +0x1b8
  3 floats [+0x168], +0x1c4 3 floats flat direction [+0x174], +0x1d0 u32 [+0x180], +0x1d4 float distance [+0x184],
  +0x1e0 location (position +0x1e0, rotation +0x1f0) [+0x190], +0x210 float [+0x1c0], +0x214 float fov [+0x1c4],
  +0x218 u8 [+0x1c8], +0x224 u8 [+0x1d4].
* Fixed (class hash 0xb8ffda36): +0x1b0 a bound frame [+0x160], +0x210 float fov [+0x1b0].

Scope record: +0x68 float (scope fov), +0x70 float, +0x144 u32 id, +0x148 u32 id, +0x184 u32.

Film list entry: +0x210 u32 = file size in bytes.

### 3.5 Ids and constants

Class hashes for `u3x_rp_isa`: 0x54aee225 CameraControlCinemaFollow, 0xb8ffda36 CameraControlCinemaFixed,
0xb1f56f8c CameraControlCinema. Scope ids 0x84713f65 and 0x63b89c77. Message 0xd8a6f55a (sent to the player while
zoomed). HUD flags 8 and 0x40. Text id 0xaaa1ef9b = the decimal separator.

Float constants (pattern = value): 0x42960000 = 75.0; 0x41200000 = 10.0; 0x3e800000 = 0.25; 0x40c00000 = 6.0;
0x3c23d70a = 0.01; 0x20000000 = 2^-63 (1.0842022e-19); 0x40400000 = 3.0; 0x3b5a740e = 0.0033333334 (1/300);
0x40a00000 = 5.0; 0x3e19999a = 0.15; 0x3f800000 = 1.0; 0xbf800000 = -1.0; 0x35800000 = 2^-20 (9.536743e-07).
The blend's constants are in section 4.6.

## 4. Behaviour

### 4.0 Float rules

* All float arithmetic is x87 arithmetic on 32-bit float inputs. The formulas below give the exact sequence and
  grouping of the elementary operations (+, -, x, /, square root, conversions); keep that sequence, do not
  re-associate, do not replace a float step by integer arithmetic or the other way round.
* "F32" marks a value that is stored as a 32-bit float (round to nearest even) before it is used again: every value
  written to game memory, to a frame, to a location block or to an array handed to another function (the maths
  group's functions take and return F32 values). Values not marked F32 are intermediates of one formula and are not
  rounded between its steps in the old source (they are scalar temporaries).
* Integer -> float conversions are exact x87 loads (not rounded to 24 bits unless marked F32); float -> integer is
  truncation toward zero.
* SEL(a, b, c) below means: b when a >= 0, otherwise c; a NaN `a` selects c (the PowerPC fsel).
* Comparisons are ordered: any comparison with a NaN is false.
* Section 8 lists where the shipped -O2 build's rounding differs from these source-level marks.

### 4.1 `u3x_rp_cam_target(base, own)`

Let `sel` = (u32 at owner +0x1b8) modulo 10 (unsigned).

A. Nobody is followed (byte owner +0x1b0 is 0):
1. pointer 0x17cb3d0 = null; u32 owner +0x198 = 0; byte at 0x17c5a14 = 0.
2. cam = `u3x_rp_h(owner + 0x1a0)`. When cam exists and `u3x_rp_isa(base, cam, 0xb1f56f8c)`:
   * flag byte cam +0xc8: bit value 2 is set when the u64 at 0x17b4a68 is zero and cleared otherwise; the other
     bits keep their value;
   * float cam +0x214 = F32( slider(0x17c5b54) x 10.0 + 75.0 ) (product, then sum, one rounding at the store);
   * byte cam +0x218 = 1 when the int32 at 0x17c5b5c is non-zero, else 0;
   * float cam +0x1d4 = F32( slider(0x17c5b58) x 10.0 ).
3. Return. No game call is made in this branch.

B. Somebody is followed:
1. tracker = `u3x_rp_trk(base, sel)`. Null: return with nothing written.
2. Copy the C string at tracker +0xe1 to 0x17c5a14, terminator included, without a length limit.
3. int32 0x17c5a34 = sel.
4. player = `u3x_rp_h(tracker + 0x10)`.

B1. The player object exists:
1. pointer 0x17cb3d0 = tracker.
2. A local float fov = 75.0 and a local frame = identity (`u3x_frame_identity`).
3. ok = (game 0x685b80(player + 0x400, frame, &fov) returned a non-zero byte). The call may change frame and fov;
   whatever it leaves is used below, also when it fails.
4. ok: u32 owner +0x198 = 0; then, when `u3x_rp_dead(tracker)`, the death look (4.5) on frame.
   Not ok: u32 owner +0x19c = 0x3f800000 and u32 owner +0x198 = 0x3f800000.
5. The follow / fixed camera step (4.2) with target = player, frame, the pattern of fov, ok.
6. When the camera mode (read again now) is 0: the scope HUD (4.7) with player and the pattern of fov.

B2. No player object; body = `u3x_rp_h(tracker + 0x20)`:
* No body either: u32 owner +0x19c = 0x3f800000; pointer 0x17cb3d0 = null; u32 owner +0x198 = 0x3f800000; return.
* Body:
  1. pointer 0x17cb3d0 = tracker.
  2. The local frame becomes a byte copy of the 0x60 bytes at body +0xb0.
  3. The frame is moved 3 m backwards along its own view axis: W = WORLD(frame); z = ZAXIS(W.rotation) (4 lanes,
     F32); for each of the 4 lanes i: offset_i = F32( ((0.0 - z_i) x 3.0) + 0.0 ) (subtraction from zero, product,
     then the addition of zero, in this order; lane 3 included); W' = WORLD(frame) evaluated a second time;
     p_i = F32( W'.position_i + offset_i ) for the 4 lanes; then the frame's world position is set to p:
     `u3x_frame_parent(base, frame)`, then LOCALPOS(frame, p).
  4. u32 owner +0x198 = 0.
  5. When `u3x_rp_dead(tracker)`: the death look (4.5) on frame.
  6. The follow / fixed camera step (4.2) with target = body, frame, fov pattern 0x42960000, ok = 1.
  No scope HUD in this branch.

### 4.2 `u3x_rp_follow_cam(base, own, target, frame, fov_bits, ok)`

Camera mode (int32 0x17c5a10) not 0 (orbit camera; frame, fov and ok are not used):
* cam = `u3x_rp_h(owner + 0x1a0)`.
* No camera: `u3x_camera_spawn(base, own, u3x_camera_type(base, 0), 0, null, target)` (fov pattern 0 = 0.0, no
  location).
* Camera that is-a 0x54aee225: the Follow setter (4.3) with cam and target. Any other camera: nothing.

Camera mode 0 (the player's view):
1. When ok is non-zero and `u3x_rp_h(owner + 0x1a0)` is null: L = WORLD(frame); then
   `u3x_camera_spawn(base, own, u3x_camera_type(base, 1), fov_bits, &L, target)` (the type is fetched after the
   world location was computed).
2. cam = `u3x_rp_h(owner + 0x1a0)`, resolved again. When it exists and is-a 0xb8ffda36: L = WORLD(frame) (again),
   then the Fixed setter (4.4) with cam, fov_bits, target, L. This also happens when ok is 0.

### 4.3 `u3x_rp_follow_set(base, cam, target)` (PS3 1.19 f_1bc73c, PS4 Follow init 0x4c4810)

Writes, on the camera (no game call):
1. Target handle: u64 cam +0xb0 = u64 at target +0x70 and u32 cam +0xb8 = u32 at target +0x7c; both 0 when target
   is null.
2. u32 cam +0x1a8 = 0.
3. The 3 floats cam +0x1b8.. = the 3 floats at 0x16ea7c0 multiplied by -1.0 (the shipped build flips the sign bit of
   the three words; identical for every non-NaN value).
4. The 3 floats cam +0x1ac.. = the 3 floats at 0x16ea550 (position x y z of the camera locator).
5. Byte cam +0xc8: bit value 2 set.
6. u32 cam +0x1d0 = 0.
7. The 0x20 bytes cam +0x1e0.. = the 0x20 bytes at 0x16ea550 (byte copy of the locator's position and rotation).
8. u32 cam +0x210 = 0x40c00000 (6.0).
9. The flat direction, 3 floats d0 d1 d2 at cam +0x1c4 / +0x1c8 / +0x1cc, from the rotation q just copied to
   cam +0x1f0 (PS3 f_1b928c):
   * a = XAXIS(q); d0 = a_0, d1 = 0.0, d2 = a_2 (stored, F32).
   * len = sqrt( (d0 x d0 + d1 x d1) + d2 x d2 ). When len < 0.01: f = ZAXIS(q); d0 = f_2, d1 = 0.0,
     d2 = F32( 0.0 - f_0 ).
   * l = sqrt( (d1 x d1 + d0 x d0) + d2 x d2 ) from the stored values; m = SEL(2^-63 - l, 1.0, l);
     r = 1.0 / m; then d0 = F32(d0 x r), d2 = F32(d2 x r), d1 = F32(d1 x r) (d1 is 0.0 x r: 0 unless r is not
     finite or NaN).
10. Byte cam +0x224 = 1 when the int32 at 0x17c5b5c is non-zero, else 0.
11. float cam +0x214 = F32( slider(0x17c5b54) x 10.0 + 75.0 ).
12. float cam +0x1d4 = F32( F32( slider(0x17c5b58) x 10.0 ) x 0.25 ) (the product with 10.0 is rounded to a float
    before the multiplication with 0.25; it matters only on overflow and for subnormal results).

### 4.4 `u3x_rp_fixed_set(base, cam, fov_bits, target, location)` (PS3 1.19 f_1b9d00, 1.10 f_1bacc8)

1. u32 cam +0x210 = fov_bits.
2. Target handle as in 4.3 step 1 (zeros for a null target).
3. obj = `u3x_rp_h(cam + 0xb0)` (the handle just written). When obj exists and its u32 at +0x30 is 0xf2c86c22
   (a player): tr = `u3x_rp_trk_by_id(base, u32 at obj + 0x3f8)`; when tr exists and `u3x_rp_dead(tr)`: return
   (a dead player keeps the camera's frame; no game call).
4. Otherwise the camera's own frame cam +0x1b0 takes the world location: `u3x_frame_parent(base, cam + 0x1b0)`,
   LOCALPOS(cam + 0x1b0, location.position), LOCALROT(cam + 0x1b0, location.rotation); then game
   0x1196230(cam + 0x1b0, 0).

### 4.5 `u3x_rp_death_look(base, frame, tracker)` (PS3 1.19 0x27b7cc / 0x27bec8)

Turns the frame toward the place where the player died, starting 3 s after the death and finished 0.5 s later.
1. L = WORLD(frame). v_i = F32( tracker position_i - L.position_i ) for the 4 lanes (lane 3 has no effect on the
   result).
2. d = NORM(v, default (0, 0, 1, 0)). q = LOOKAT(d, up (0, 1, 0, 0)). (The direction is normalised here and again
   inside LOOKAT; keep both.)
3. L' = WORLD(frame), evaluated a second time.
4. Blend factor: n = (int32 at 0x17b4ca4) - (int32 at tracker +0x39c), a 32-bit signed difference;
   t = ( n (converted exactly) x 0.0033333334 ) - 3.0; t = t + t; t = SEL(t, t, 0.0); t = SEL(t - 1.0, 1.0, t).
   So t = 0 up to 900 ticks after the death, 1 from 1050 ticks on, linear in between. No rounding between these
   steps; the result is F32 when it is handed to the blend.
5. o = BLEND(L'.rotation, q, t) (4.6), 4 floats F32.
6. The frame's world rotation is set to o: `u3x_frame_parent(base, frame)`, then LOCALROT(frame, o).
Only the frame is written (parent refresh and local rotation). No game call.

### 4.6 BLEND(q0, q1, t): spherical interpolation in the PS3 form (PS3 f_a6e818, 1.19 f_a82dbc)

The result is not normalised. Inputs are F32 arrays of 4 floats (x y z w) and t.
1. dot = ((q0x x q1x + q0y x q1y) + q0z x q1z) + q0w x q1w.
2. When 0 > dot: q1 is replaced by (0.0 - q1_i) in every lane and dot by (0.0 - dot).
3. linear = not (0.999 [0x3f7fbe77] > dot) (so also for a NaN). sign = the sign bit of dot as a 32-bit float.
   a = |dot| (sign bit cleared). x = 1.0 - a.
4. Arc cosine factor, two Horner chains in a:
   hi = ((a x (-0.0012624911) + 0.0066700901) x a + (-0.0170881256)) x a + 0.0308918810;
   lo = ((a x (-0.0501743046) + 0.0889789874) x a + (-0.2145988016)) x a + 1.5707963050;
   a2 = a x a; a4 = a2 x a2; P = hi x a4 + lo.
   Patterns of the eight magnitudes in this order: 0x3aa57a2c, 0x3bda90c5, 0x3c8bfc66, 0x3cfd10f8, 0x3d4d8392,
   0x3db63a9e, 0x3e5bbfca, 0x3fc90fda (negative ones are the negated positive constant). These are the published
   coefficients of the arc cosine approximation Abramowitz and Stegun 4.4.46 (also used by the open Sony Vector
   Math library), not a table from the game.
5. root = x when x == 0, otherwise sqrt(x). theta = root x P; when the sign bit of step 3 is set:
   theta = 3.14159274 [0x40490fdb] - root x P.
6. u = 1.0 - t. s0 = SIN(theta), s1 = SIN(u x theta), s2 = SIN(t x theta).
7. Weights: w0 = u and w1 = t when linear; otherwise w0 = s1 / s0 and w1 = s2 / s0.
8. out_i = F32( q0_i x w0 + q1_i x w1 ) for the 4 lanes (q1 after step 2).

SIN(A), sine by quadrant reduction:
1. y = A x 0.636619747 [0x3f22f983, 2/pi]; y = y + h, h = 0.5 with the sign of y (sign bit of y as a float).
2. n (int32) = 0x7fffffff when y >= 2147483520.0 [0x4effffff]; else -2147483648 when y <= -2147483648.0
   [0x4f000000 negated]; else y truncated toward zero. (A NaN gives -2147483648 on the x87.)
3. With nf = n converted to float: x = A - nf x 1.57079625 [0x3fc90fda]; x = x - nf x 7.54979e-08 [0x33a22169];
   x2 = x x x; x3 = x2 x x.
4. cosine = ((x2 x (-0.00136022491) + 0.0416566953) x x2 + (-0.499999017)) x x2 + 1.0
   [0x3ab24993, 0x3d2aa036, 0x3effffdf];
   sine = ((x2 x (-0.0001950727) + 0.008332076) x x2 + (-0.166666523)) x x3 + x
   [0x394c8c6e, 0x3c088342, 0x3e2aaaa1].
   (The published sine / cosine coefficients and pi/2 split of the Sony Vector Math library's `sinf4`.)
5. Result = sine when n is even, cosine when odd; when bit 1 of n is set, the result's sign bit (as a 32-bit float)
   is flipped.

### 4.7 `u3x_rp_scope_hud(base, player, fov_bits)` (PS3 1.19 0x27bae4..0x27bce8)

fov = the float of fov_bits.
1. weapon = the player's vtable slot at byte offset 0x3d8, called with the player. Null: return.
2. scope = game 0xd436b0(weapon). Null: return.
3. sf = float scope +0x68; g = float scope +0x70; lim = sf - 5.0 (not rounded).
4. "Zoom" holds when the u32 scope +0x144 is 0x84713f65 and either (not (fov > lim) and g > -1.0) or the u32
   scope +0x184 is non-zero. (With another id at +0x144 there is never a zoom.)
5. No zoom: when fov > sf: return. Otherwise, in this order: `u3x_rp_hud_weapon(base, weapon)`,
   `u3x_rp_hud_flag(base, 8)`, `u3x_rp_hud_fade(base, 0x3f800000, 0, 0x3f800000, 1, 0)`.
6. Zoom: f = (fov - g) / (lim - g); f = SEL(f, f, 0.0); f = SEL(f - 1.0, 1.0, f); k = 1.0 - f (no rounding
   between these steps). Then, in this order:
   * when k > 0.15: `u3x_rp_send(base, 0xd8a6f55a, player)`;
   * `u3x_rp_hud_weapon(base, weapon)`;
   * when not (fov > sf): `u3x_rp_hud_flag(base, flag)` with flag = 0x40 when the u32 scope +0x148 is 0x63b89c77
     or 0x84713f65, else 8;
   * `u3x_rp_hud_fade(base, pattern of F32(k), 0, 0x3f800000, 1, 0)` (always, also when fov > sf).
   A zero divisor gives +-infinity or NaN, which the two selections turn into f = 1, 0 or 0.

### 4.8 `u3x_rp_cam_mode12(base, st, own)` (PS3 1.19 0x279de4..0x27a00c)

1. clock = pointer at 0x17c3580. When int32 st +0x18 <= int32 clock +0xc: `u3x_camera_rerecord(base, own)`, then
   int32 st +0x18 = (int32 clock +0xc, read after the call) + 9.
2. When the int32 owner +0xa60 is 0, or the camera mode (read now) is not 1: return.
3. A local frame = `u3x_frame_make(frame, base + 0x16ea550)` (identity parent, local = the camera locator).
4. When `u3x_rp_h(owner + 0x1a0)` is null: L = WORLD(frame);
   `u3x_camera_spawn(base, own, base + 0x17b30e8, 0x42960000, &L, null)`.
5. Game 0x7a3e30(owner + 0x6c0, frame).
6. cam = `u3x_rp_h(owner + 0x1a0)`; when it exists and its u32 at +0x30 equals 0xb1f56f8c (an exact class id
   comparison here, not is-a): L = WORLD(frame); `u3x_camera_cinema_set(cam, &L)`.
No float arithmetic of its own.

### 4.9 `u3x_cin_megabytes(base, s, out, entry)` and the fixed-point text (PS3 1.19 f_7a6ef8, PS4 inline 0x63b00e)

B = u32 at entry +0x210. The text is "<integer part><separator><two digits>" of B / 2^20, built by float steps in
this order:
1. v = (B converted to float) x 2^-20 [0x35800000].
2. I = v truncated toward zero (unsigned 32-bit).
3. r = v - (I converted to float); then r = r x 10.0, twice (once per decimal).
4. D = r truncated toward zero.

Text, written into `out` with the limit `out + 0x3f`: a character is only stored below the limit, and a zero
terminator is stored after every piece at the current position (at most at `out[0x3f]`):
* I in decimal without leading zeros ("0" for 0);
* the separator `u3h_text(base, s, 0xaaa1ef9b)` (one lookup, made after the integer digits are written), cut at the
  limit;
* D in decimal, padded with zeros on the left to 2 digits, cut at the limit;
* the terminator.

Rounding (this is what the shipped helper does, section 8): steps 1 to 3 are exact (the conversion of B is not
rounded to 24 bits, so I = B >> 20 and r = (B mod 2^20) x 100 / 2^20 exactly); r is then rounded to a 32-bit float
once, before step 4. Consequence: D = floor((B mod 2^20) x 25 / 2^18), except that (B mod 2^20) = 828375 (0xCA3D7)
gives 79 and not 78 (78.999996 rounds to 79.0).

The general old function `cin_fixed` (value v as a float, a digit count): a value that is not >= 0 (negative, or
NaN) is negated and gets a leading "-"; then the steps above with `digits` multiplications by 10 and a D padded to
`digits`. Its only caller passes a non-negative value and 2 digits, so the sign part is dead code, and no separate
general formatter is needed: a formatter that received v as a 32-bit float would round sizes of 16 MiB and more
(see `G06_cinema_rows.md` 4.5), which the old code does not do.

## 5. Test scenarios

`u3x_rp_cam_target`
1. owner +0x1b0 = 0, no camera handle: only the three resets happen.
2. owner +0x1b0 = 0, camera that is-a Cinema, u64 0x17b4a68 = 0 and non-zero, flag byte with other bits set, sliders
   e.g. 0.0 / 0.35 / 1.0 / -2.5 / 3.4e38, flag int 0 and 7: the four camera fields.
3. owner +0x1b0 = 0, camera that is not a Cinema camera: only the resets.
4. Followed, no net game (tracker null): nothing written. Selection values 0, 9, 10, 23, 0xffffffff (index mod 10).
5. Player present, 0x685b80 succeeds, not dead, mode 0, no camera -> Fixed camera spawned with the frame's world
   location and the returned fov, then the Fixed setter, then the scope HUD.
6. Same with 0x685b80 failing: both owner floats 1.0, no spawn, the setter still runs on an existing Fixed camera,
   scope HUD still runs.
7. Player present and dead (time since death 0, 899, 900, 975, 1049, 1050, 100000 ticks, and a negative
   difference): death look before the camera step.
8. Player present, mode 2: Follow camera spawned (fov 0, no location) when none; Follow setter on an existing Follow
   camera; nothing on a camera of another class; no scope HUD.
9. No player, body present (alive and dead tracker): frame copy, 3 m offset, camera step with fov 75 and ok 1; with
   a bound and an unbound body frame; rotations identity, 90 degrees about each axis, random unit quaternions.
10. No player and no body: both owner floats 1.0, current tracker null.
11. Name copy: empty name, 31 characters.

`u3x_rp_follow_set`: null and non-null target; locator rotations whose x axis is flat, nearly vertical (flat length
below and just above 0.01), exactly vertical (the z-axis fallback), and one giving a zero flat vector (length below
2^-63: divisor 1); sliders as in 2; flag 0 / non-zero; locator second vector with zeros, negative values.

`u3x_rp_fixed_set`: null target; target that is not a player; player whose tracker is alive; player whose tracker is
dead (frame untouched, no unbind call); player with a net id that matches no tracker; frame with and without a
valid rigid-body binding.

`u3x_rp_death_look`: target straight ahead, behind, above (direction parallel to up), at the frame's own position
(zero vector -> default direction); t = 0, 0.5, 1; rotations with a negative dot product (lane negation), equal
rotations (dot = 1: linear weights), dot just below and above 0.999.

`u3x_rp_scope_hud`: no weapon; no scope record; id at +0x144 other than 0x84713f65 with fov > sf (return) and
fov <= sf; id 0x84713f65 with: fov <= lim and g > -1; fov <= lim and g = -1 with +0x184 zero and non-zero;
fov > lim with +0x184 zero (falls to the non-zoom test) and non-zero; zoom with k above and below 0.15 (message or
not), with fov > sf (no flag call) and fov <= sf, with the id at +0x148 = 0x63b89c77, 0x84713f65 and another;
lim = g (division by zero); fov = NaN.

`u3x_rp_cam_mode12`: re-record due / not due (equal times count as due); owner +0xa60 zero; mode 2 (return after the
re-record); mode 1 without a camera (spawn), with a Cinema camera, with a camera of another class id.

`u3x_cin_megabytes`, expected text with the separator ".": B = 0 and 1 -> "0.00"; 10485 -> "0.00"; 10486 ->
"0.01"; 1048575 -> "0.99"; 1048576 -> "1.00"; 1572864 -> "1.50"; 828375 -> "0.79" and 1048576 + 828375 -> "1.79"
(the rounding case; 828374 -> "0.78"); 0x01234567 -> "18.20" and 0xffffffff -> "4095.99" (x87: the conversion is
not rounded to 24 bits); random values. Separators ",", empty, and one longer than the buffer (cut at the limit,
terminator at `out[0x3f]`, fraction digits lost).

## 6. Bugs, dead code and oddities to keep

1. 4.1 B2: the old code first makes an identity frame and then overwrites all of it with the body's frame; the
   world location is evaluated twice with the same result; the offset has a "+ 0.0" (it turns a -0.0 product into
   +0.0); lane 3 of the offset and of the position takes part and reaches the frame's local position.
2. 4.1: the name copy has no length limit. A missing tracker returns silently (PS3 would read from a null pointer
   there; not reachable during a film). Owner +0x19c is only ever set to 1.0 here, never back to 0.
3. 4.2: in mode 0 `ok` only decides the spawn; an existing Fixed camera is set also with ok = 0 (with the frame and
   fov the failed game call left). The camera handle is resolved twice. In orbit mode a camera of another class is
   left alone.
4. 4.1 A uses is-a for the Cinema camera, 4.8 an exact class id at +0x30. The Follow setter writes the follow flag to
   +0x224 and a quarter of the distance value to +0x1d4; branch 4.1 A (Cinema camera) writes the flag to +0x218 and
   the full distance value.
5. 4.3: only the x axis' lanes 0 and 2 are used; the fallback direction is (z_2, 0, -z_0); the middle component is
   multiplied by the reciprocal although it is zero. The target may be null in the code (never from its caller).
6. 4.5: the world location is evaluated twice; the direction is normalised twice; lane 3 of the difference is
   computed without effect.
7. 4.6: the result is not normalised; the "linear" case still computes the three sines; a dot product of -0.0 keeps
   its sign bit and takes the pi - ... branch.
8. 4.7: the limit sf - 5.0 is computed in both the test and the zoom formula; in the zoom branch the fade is set
   even when fov > sf (then without the flag call); the message is sent before the weapon call.
9. 4.9: the ".79" rounding case; the sign handling of the general function is dead; the text lookup happens between
   the integer digits and the fraction.

## 7. Callers and what stays in C

See section 2: three one-line wrappers at most (`rp_cam_target`, `rp_cam_mode12`, `u3h_cin_megabytes`), or none when
`u3x_rp_update` / `u3x_cinema_rows` call the new functions directly. Nothing of this group has to stay in C. The
comment at `cinema_rows` ("the MB text stays in C: x87 float") becomes obsolete.

## 8. Rounding in the shipped -O2 helper (checked in the v7.212 `profile-helper.bin`)

The helper is compiled with clang -O2 -mno-sse. The compiler keeps float values in x87 registers at extended
precision and rounds to 32 bits only when it stores them (game memory, arrays that really exist in memory, values
kept across a call). Inlining therefore changes last-bit results. Observed, as facts for the tester:

* 4.9: exactly as stated there. The scaled fraction is rounded because it is kept across the text lookup call; when
  the helper has no text lookup function (never in a running game) it is not rounded and 828375 gives 78.
* 4.1 A and 4.3 steps 11 / 12: as stated (the distance product is rounded before the multiplication with 0.25).
* 4.1 B2: the z axis is computed inline; its lanes are not rounded before the offset is formed; the four offsets and
  the four positions are rounded (as marked).
* 4.3 step 9: the axes are computed inline; the lanes are not rounded before the length tests and the normalisation
  (the stored d0 / d2 are not re-read); in the z-axis fallback the compiler reuses the rounded products y x y and
  x x z of the x-axis computation. Only the three final stores are certain rounding points.
* 4.5 / 4.6: NORM, LOOKAT and the blend are all inline. The difference vector is not rounded; the look-at
  quaternion is (it lives in memory); t is rounded once, after its last step; inside the blend values are rounded
  only where the compiler ran out of registers; |dot| is taken from the unrounded dot product, the sign from the
  rounded one.
* 4.7: lim, f and k are not rounded; k is compared with 0.15 unrounded and rounded only for the fade value.
* On a build with SSE float arithmetic (every operation rounded to 32 bits) the operation sequences of section 4
  define the result completely.

## 9. Clarifications (tester, 2026-11-14)

Facts confirmed by the old-versus-new test `reimpl2\film_block\test_film.cpp` (old = the v7.212 functions, new =
`reimpl2\film_cameras\part.cpp` on top of the new film maths and film helpers parts).

1. Arithmetic. The "Float rule" of `reimpl2\PIPELINE.md` replaces 4.0 and the rounding remarks in section 4 ("one
   rounding at the store", "not rounded", "converted exactly"): every elementary operation is one single-precision
   operation in the order given. With this arithmetic sections 4.1 to 4.8 give, bit for bit, what the old functions
   give when they are compiled with single-precision arithmetic. Consequences that were checked:
   * 4.1 A and 4.3 step 11: slider x 10.0 is rounded, then + 75.0 is rounded (one rounding of the whole expression
     gives other results). 4.3 step 12 is as stated.
   * 4.5 step 4: n is converted to a 32-bit float. That only rounds for more than 2^24 ticks, where t is 0 or 1
     anyway.
2. 4.3 step 3: the old code, compiled for the host, flips the sign bit of the three words (as the shipped build
   does); the new code multiplies by -1.0. The results are the same for every number. For a NaN lane of the vector
   at 0x16ea7c0 the old result has the sign bit flipped and keeps a signalling NaN as it is; the new result keeps
   the sign and is a quiet NaN. This and the NaN payloads of the maths (`film_maths.md` Clarification 7) are the only
   differences the test found, and there is none without a NaN among the inputs.
3. 4.3 step 9 in single precision, exact results (old and new agree): rotation (0.5, 0.5, 0.5, 0.5) gives
   (0, 0, -1); the zero quaternion, the identity, (0, 0.70710677, 0, 0) and the quarter turn about z
   (0, 0, 0.70710677, 0.70710677) give (1, 0, 0). The "divisor 1" case can be reached:
   (2^-12, 0.70710677 [0x3f3504f3], 0, 0) has the first pair (2^-24, 0), which is shorter than 0.01, and the
   fallback pair exactly (0, 0): result (0, 0, 0). With a z lane of 2^-52 the fallback pair is (0, -2^-63); a length
   of exactly 2^-63 still takes the divisor 1: result (0, 0, -2^-63). With a z lane of 2^-51 the length is 2^-62 and
   the result (0, 0, -1). A first pair of length exactly 0.01 is not replaced by the fallback:
   (0.01 [0x3c23d70a], 0.5, 0.5, 0) has the pair (0, 0.01) and gives (0, 0, 1).
4. 4.1 B2 step 3 and section 6 item 1: the "+ 0.0" of the offset has no effect for any input. (0.0 - z) is never
   -0.0 and its product with 3.0 cannot underflow, so there is no -0.0 product to turn into +0.0. With or without
   it the bits are the same.
5. 4.7: in single precision k = 1.0 - f is never exactly 0.15 [0x3e19999a] (for f in [0.5, 1] it is a multiple of
   2^-24, and that pattern is not), so "k > 0.15" and "k >= 0.15" cannot be told apart.
6. 4.8 step 1: the time that 9 is added to is read after the call, from the clock object whose pointer was fetched
   before the call (checked with a game call that replaces the pointer at 0x17c3580 and with one that advances the
   time).
7. Exact signatures of the existing functions of 3.2, as the helper defines them (all `extern "C"`):
   `uint8_t *u3x_rp_h(const uint8_t *h)`, `uint8_t *u3x_rp_trk(uintptr_t base, uint32_t i)`,
   `int u3x_rp_dead(const uint8_t *t)`, `uint8_t *u3x_rp_trk_by_id(uintptr_t base, uint32_t id)`,
   `int u3x_rp_isa(uintptr_t base, uint8_t *obj, uint32_t hash)`,
   `void u3x_rp_send(uintptr_t base, uint32_t msg, uint8_t *target)`, `void u3x_frame_identity(uint8_t *bf)`,
   `void u3x_frame_make(uint8_t *bf, const uint8_t *local)`, `void u3x_frame_parent(uintptr_t base, uint8_t *bf)`,
   `uint64_t u3x_camera_type(uintptr_t base, int fixed)`,
   `void u3x_camera_spawn(uintptr_t base, uint8_t *own, uint64_t type, uint32_t fov, const void *locator, const uint8_t *target)`,
   `void u3x_camera_cinema_set(uint8_t *c, const void *locator)`, `void u3x_camera_rerecord(uintptr_t base, uint8_t *own)`,
   `void u3x_rp_hud_flag(uintptr_t base, uint32_t f)`,
   `void u3x_rp_hud_fade(uintptr_t base, uint32_t a, uint32_t b, uint32_t c, uint32_t i, uint8_t by)`,
   `void u3x_rp_hud_weapon(uintptr_t base, const uint8_t *w)`.
   The cameras part declares six of them with other pointer or integer types (`u3x_rp_isa`, `u3x_frame_make`,
   `u3x_camera_type`, `u3x_camera_spawn`, `u3x_rp_hud_fade`, `u3x_rp_hud_weapon`). The arguments travel in the same
   registers, so it behaves the same when it is linked as a separate file (as in the test); inside one translation
   unit with the definitions the declarations have to be the ones above.
8. Not covered by this block. `u3x_cin_megabytes` (4.9) is delivered and tested with `G06_cinema_rows.md`; the
   cameras part does not contain it. `u3x_rp_hud_fade` (inventory group G01, a C++ function "moved as written": it
   writes five fields of the object at HUD +0x220) is used here as an existing function and is described by none of
   the three film specs.
9. Against the old code built as the helper builds it (clang -O2 -mno-sse, for the host; information only). With
   the x87 unit at 24-bit precision the old x87 build and the new code agree in every case tried. At 53-bit or
   64-bit precision the largest absolute differences seen were: 1.4e-6 in a lane of the death look's rotation,
   7.6e-6 in a Follow camera field, 1.2e-4 in a Fixed camera's local position of several hundred, 2.4e-4 in a
   position of about 2000 (and once in one of 84), 1.4e-7 in the scope's fade value. The calls made were the same
   in every case (no branch went the other way in 135,000 cases with ordinary inputs).
