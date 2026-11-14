# film_helpers: float helpers, the call bridge, blend and power maths, camera spawn, overrides, bookmarks (b003)

Group of the clean-room pipeline (reimpl2): the parts of work groups G01 and G02 that `film_maths.md` and
`film_cameras.md` do not cover. Old code: v7.212, `u3_lan_profiles.c` hand-written part (lines 5100 to 5456) and
`u3x_fixes.cpp` (lines 3812, 6080 to 6125, 18489, 18550 to 18649, 18955). Everything here is behaviour; nothing is
copied from the old statements.

Covered here:

| old name | where | what |
|---|---|---|
| `fbits`, `bitsf`, `rd_f`, `rp_fsel`, `kf`, `rp_sqrt` | C, inline one-liners | float helpers (4.1) |
| `rp_fcall` | C file, top-level assembly | call bridge for game functions with float register arguments (4.2) |
| `rp_powf` | C, inline | x87 power (4.3) |
| `rp_sinq`, `rp_slerp` | C, inline | sine by quadrant reduction, quaternion blend (4.4) |
| `rp_follow_dir` | C | flat direction of the Follow camera (4.5) |
| `rp_cam_mode12` | C | free camera step of camera modes 1 and 2 (4.6) |
| `u3x_camera_spawn`, `u3x_camera_rerecord`, `u3x_camera_type` | C++ | camera spawn, camera re-record, the two rebuilt camera types (4.7) |
| `u3x_rp_hud_flag` | C++ | HUD flag with element stamping (4.8) |
| `u3x_rp_trk_by_id`, `u3x_rp_isa` | C++ | tracker by network id, class test (4.9) |
| `u3x_rp_send`, `u3x_ovr_blend`, `u3x_ovr_scale`, `u3x_ovr_byte` | C++ | message send and script-variable overrides through the bridge (4.10) |
| `u3x_bookmark_create` | C++ | film bookmark snapshot (4.11) |
| `rp_screenshot_step` | C++, file-private | screenshot request step (4.12) |

Overlap, to be settled by whoever merges the specs (see section 8): `film_cameras.md` also describes
`rp_cam_mode12` (its 4.8), the flat direction (its 4.3 step 9) and the blend with its sine (its 4.6);
`film_maths.md` also describes the blend with its sine (its 4.11) and the power (its 4.12, as an internal step of
the render-variable function). Sections 4.3 to 4.6 here describe the same behaviour and agree with them in the
operation sequence; they add the exact rounding of the shipped build, value by value (it differs between the three
inlined copies of the sine). Implement each of them once.

## 1. Purpose and callers

All of this serves the film player (RePlaying state; PS3 1.19 f_279d6c, PS4 state vtable 0x167e180 slot 5), whose
per-frame function is `u3x_rp_update`.

| piece | called from | when |
|---|---|---|
| float one-liners | every float function of the film block (C); `kf`, `fbits`, `bitsf` also from other C sections (game time, colour and exposure code of other groups) | - |
| bridge `rp_fcall` | only through the export `u3h_rp_fcall`, from `u3x_camera_rerecord`, `u3x_rp_send`, `u3x_ovr_blend`, `u3x_ovr_scale`, `u3x_ovr_byte` | whenever a game function needs a float in xmm0 |
| power | the render-variable function (`rp_render_vars`, see `film_maths.md`), once per frame while the film's colour settings are applied | exponent always 2.2 |
| blend, sine | the death look (`film_cameras.md` 4.5), once per frame while the followed player is dead | - |
| flat direction | the Follow setter (`film_cameras.md` 4.3) | every frame an orbit camera exists |
| free camera step | `u3x_rp_update` through the export `u3h_rp_cam_mode12`, at the start of the update | camera mode 1 or 2 |
| `u3x_camera_spawn` | free camera step; follow / fixed camera step (`film_cameras.md` 4.2); C wrapper `rp_cam_spawn` | no live camera yet |
| `u3x_camera_rerecord` | free camera step; C wrapper `rp_rerecord_cam` | every 9 clock ticks |
| `u3x_camera_type` | follow / fixed camera step; C wrapper `rp_camtype` | before a Follow / Fixed spawn |
| `u3x_rp_hud_flag` | scope HUD (`film_cameras.md` 4.7); C wrapper `rp_hud_flag` | flags 8 and 0x40 |
| `u3x_rp_trk_by_id` | Fixed setter (`film_cameras.md` 4.4); C wrapper `rp_trk_by_id` | - |
| `u3x_rp_isa` | camera steps (C wrapper `rp_isa`); also the C code at line 3616 (class 0xa5a63e9b, another group) | - |
| `u3x_rp_send` | scope HUD (C wrapper `rp_send`); `u3x_rp_update` (play, pause, camera mode messages) | - |
| `u3x_ovr_blend` | render-variable function (C wrapper `ovr_blend`), up to 26 times per frame | - |
| `u3x_ovr_scale` | `u3x_rp_update` (play speed onto clocks 1 and 0), screenshot step | - |
| `u3x_ovr_byte` | `rp_rumble_off` in `u3x_rp_update` (`G03_film_playback.md`) | - |
| `u3x_bookmark_create` | `u3x_rp_update` | every 3000 clock ticks |
| screenshot step | `u3x_rp_update`, instead of the rest of the update | owner byte +0x1b2 non-zero |

## 2. Interface

No float is passed or returned by value. A "location" is 0x20 bytes: position x y z w, rotation quaternion x y z w
(8 floats). A "frame" is a 0x60-byte bound frame, 16-byte aligned.

Functions that keep their name and signature (they already are `extern "C"` entry points of the C++ part; the C
wrappers and the C++ callers stay as they are):

| function | parameters / result |
|---|---|
| `void u3x_camera_spawn(uintptr_t base, uint8_t *own, uint64_t type, uint32_t fov, const void *locator, const uint8_t *target)` | image base; owner; address of a camera type object; fov as a float pattern; a location or null; target object or null |
| `void u3x_camera_rerecord(uintptr_t base, uint8_t *own)` | base; owner |
| `uint64_t u3x_camera_type(uintptr_t base, int fixed)` | 0 = Follow, 1 = Fixed; returns the address of the type object |
| `void u3x_rp_hud_flag(uintptr_t base, uint32_t f)` | HUD flag value |
| `uint8_t *u3x_rp_trk_by_id(uintptr_t base, uint32_t id)` | network id; tracker or null |
| `int u3x_rp_isa(uintptr_t base, uint8_t *obj, uint32_t hash)` | object; class name hash; 1 or 0 |
| `void u3x_rp_send(uintptr_t base, uint32_t msg, uint8_t *target)` | message id; receiving object |
| `void u3x_ovr_blend(uintptr_t base, uint32_t t, uint8_t *addr, const void *val, int32_t frames, uint8_t *owner)` | blend weight as a float pattern; address of the variable; pointer to the 4-byte value; hold frames; owner key |
| `void u3x_ovr_scale(uintptr_t base, uint8_t *addr, const void *val)` | address of the variable; pointer to the 4-byte value |
| `void u3x_ovr_byte(uintptr_t base, uint32_t hash, uint8_t *addr, const uint8_t *val, int32_t frames, int32_t time)` | override id; address of the byte; pointer to the value byte; hold frames; time offset |
| `void u3x_bookmark_create(uintptr_t base, uint8_t *bm, int32_t t)` | a bookmark slot (0x30 bytes); the film clock time |
| `int rp_screenshot_step(uintptr_t base, uint8_t *st, uint8_t *own)` | file-private (anonymous namespace), called only by `u3x_rp_update`; returns 1 |

New functions for what is still C (proposed names; the first two are the names `film_cameras.md` already uses):

| new function | replaces | parameters / result | C wrapper that remains |
|---|---|---|---|
| `void u3x_rp_cam_mode12(uintptr_t base, uint8_t *st, uint8_t *own)` | `rp_cam_mode12` | base; the state object; the owner | `rp_cam_mode12(base, st, own)` forwards unchanged (reached through `u3h_rp_cam_mode12`); or `u3x_rp_update` calls it directly and the export goes away |
| `void u3x_rp_slerp(const float *q0, const float *q1, uint32_t t_bits, float *out)` | `rp_slerp` + `rp_sinq` | two quaternions (4 floats each, in memory), the blend factor as a float pattern, 4 floats out (may be the buffer of q0 or q1: all inputs are read before the first output is written). Same signature as in `film_maths.md` and `film_cameras.md` | none: its only caller (the death look) moves to C++ with it |
| `void u3x_rp_follow_dir(uint8_t *cam)` | `rp_follow_dir` | a Follow camera object: reads the rotation at +0x1f0, writes the 3 floats at +0x1c4 | none: only the Follow setter calls it |
| `uint32_t u3x_rp_pow_mul(uint32_t x_bits, uint32_t y_bits, uint32_t factor_bits)` | `rp_powf` and the product its caller forms | patterns of base, exponent and a factor; returns the pattern of F32(factor x POW(x, y)), the power not rounded before the product (4.3). Only needed when the power is not written inside the render-variable function, as `film_maths.md` 4.12 has it | none: only the render-variable function uses it |

The float one-liners get no exported form (they take or return floats): the C++ part's `x87_bits`, `x87_float`,
`x87_const` and `rd<float>` are the same contracts as `fbits`, `bitsf`, `kf`, `rd_f`; SEL and the x87 square root
are stated in 4.1. The C one-liners stay in the C file as long as any C function still uses them.

`rp_fcall` and its export `uint64_t u3h_rp_fcall(uint64_t *r)` stay exactly as they are (section 7).

## 3. Game facts

All addresses are image offsets (add `base`). Calls use the System V x86-64 convention.

### 3.1 Game functions

| image | arguments | result | meaning |
|---|---|---|---|
| 0x101b530 | rdi = camera manager, rsi = pointer to a u64 holding the type object's address, rdx = pointer to a u64 0x0000000200000000 (two u32: 0, then 2), rcx = parameter block (below) | rax = the new camera object or null | camera manager "add camera" (PS3 f_81ee8c) |
| 0x6c1d60 | xmm0 = play speed (float), dil = camera mode (byte), sil = followed index (byte), rdx = frame | none | writes a camera record into the current record stream (PS3 1.10 f_2ec7c8) |
| 0xd94550 | edi = 0, rsi = C string, edx = 1 | eax = 32-bit name hash | the game's string hash |
| 0x7a3e30 | rdi = owner + 0x6c0 (free-camera controller), rsi = frame (in/out) | none | free camera controller playback (PS3 f_385ad4) |
| 0x1197380 | rdi = result block (0x20 bytes), rsi = 0, edx = message id, rcx = target object, r8, r9, [rsp], [rsp+8] = four argument blocks (0x18 bytes each) | ignored | send a message to an object (PS3 f_9555d0) |
| 0x12284e0 | xmm0 = blend weight (float), edi = override id, rsi = address of the variable, rdx = pointer to the value, ecx = value size in bytes, r8 = compare function, r9 = set function, [rsp] = owner key, [rsp+8] = 0 | rax = override object or null | add a script-variable override (PS3 f_9c28dc / f_9c3904) |
| 0x1228b20, 0x1228b30, 0x1228ca0, 0x12284c0, 0x12284d0 | never called here; their addresses are the compare / set arguments of 0x12284e0 | - | 4-byte compare, 4-byte blended set, 4-byte scaled set, 1-byte compare, 1-byte set |
| 0xd9e990 | rdi = size (u64), rsi = pointer to a 32-bit heap tag, rdx = alignment (u64) | rax = memory | the game allocator (PS3 f_7124e0) |
| 0x6b1b90 | edi = network id | none | bookmark snapshot, step 1 (PS3 f_2ebe58) |
| 0x6b1dc0 | none | none | step 2 (PS3 f_2ebce0) |
| 0x7427d0 | rdi = net manager (image 0x17cb060) | none | step 3 (PS3 f_3073a8) |
| 0x72e560 | rdi = net manager, esi = network id | none | step 4 (PS3 f_30a040) |

Only 0x6c1d60 and 0x12284e0 take a float in a register; 0x1197380 is called through the bridge too (with xmm0 to
xmm3 = 0 and al = 4), and that is kept.

### 3.2 Existing functions the new code uses

| function | contract |
|---|---|
| `uint64_t u3h_rp_fcall(uint64_t *r)` (C part) | the call bridge, 4.2 |
| `uint8_t *u3h_camera_manager(uintptr_t base)` (C part) | the screen-0 camera manager: the object of the handle at image 0x1d52b80; when there is none it raises the game's assert (handler at 0x1bf27b8, then interrupt 0x41) and returns null |
| `uint8_t *u3x_rp_h(const uint8_t *handle)` | a handle is {u64 pointer to an object slot, u32 id at +8}; returns the object in the slot when slot pointer, id and object are non-zero and the object's u32 at +0x7c equals the id, else null |
| `void u3x_frame_make(uint8_t *frame, const uint8_t *location)` | writes a frame without binding: all zero, the two marker words 0x7fcccccc, parent rotation (0, 0, 0, 1), local position and rotation = the 0x20 bytes of the location |
| WORLD(frame) = `void u3x_rp_frame_world(uintptr_t base, uint8_t *frame, float *out)` | `film_maths.md` 4.8 (old `bf_world`): refreshes the frame's parent, then world location (8 floats) = parent after local |
| `void u3x_camera_cinema_set(uint8_t *cam, const void *location)` | Cinema camera: position +0x1e0, rotation +0x1f0, byte +0x224 = 1 |
| `void u3x_stream_write(uintptr_t base, uint8_t *stream, uint32_t value, uint32_t n)` | writes the low n bits of value at the stream's bit position (most significant bit first, 32-bit words), adds n to the position (u32 +8) and raises the game's assert when the new position is past the capacity (u32 +0xc) |
| `uint32_t *u3x_rp_pad(uintptr_t base)` | address of pad 0's "pressed buttons" word: (pointer at image 0x16e2fa0) + 0x874 |

### 3.3 Globals

| image | type | meaning |
|---|---|---|
| 0x17c3580 | pointer | film clock object; i32 +0xc = current time in ticks |
| 0x17c5a10 | i32 | camera mode (1 = free camera) |
| 0x16e716c | float | play speed |
| 0x16ea550 | location | the screen-0 camera locator |
| 0x17b30e8 | object | the kCameraCinema type object |
| 0x1b5b4d0, 0x1b5b6c0 | objects | parameter descriptors of the classes CameraControlCinemaFollow and CameraControlCinemaFixed |
| 0x17c5d00 | u32 | non-zero while a re-recording runs (film reader 0x17c5cf8, field +8) |
| 0x1b68840 | pointer | the bit stream the game's record functions currently write to |
| 0x17c3850 | bit stream | the re-record stream |
| 0x17c3840 | u32 | bit position of the film read stream (stream 0x17c3838, field +8) |
| 0x17c3c58 | u64 | the film writer's stamp (writer 0x17c3ba8, field +0xb0) |
| 0x17d5c61 | u8 | non-zero while a net game exists |
| 0x17cb060 | object | net manager; trackers at +0x390 + i x 0x9c0 (10 of them; the first is image 0x17cb3f0) |
| 0x16e2f50 | pointer | the net manager again, as a pointer (used by the tracker search) |
| 0x1805fa0 | object | HUD of screen 0 |
| 0x1b24b98 | pointer | game object; i32 +0x94 = game frame time |
| 0x1b3efd8 | object | class registry |
| 0x17c3571, 0x17c3572 | u8 | pause request, and its companion flag |
| 0x17b4adc, 0x17b4b1c | float | time scale of clock 0 and of clock 1 |

### 3.4 Structures

* Camera spawn parameter block, 0x60 bytes, 16-aligned: u32 +0 = 0x30000000; u32 +0xc = fov pattern; u32 +0x10 and
  +0x14 = 0xbf800000 (-1.0); +0x20 location (0x20 bytes); +0x40 target handle (u64 slot pointer, u32 id at +0x48);
  u8 +0x50 = "location given". Everything else zero.
* Any game object: u64 +0x70 and u32 +0x7c = its own handle (slot pointer, id); pointer +0x38 = type info, whose
  u64 words from +0x20 are the class bit set.
* Camera type object, 0x28 bytes: u64 +0 = name (C string), u32 +8 = hash of the class name, u64 +0x10 = parameter
  descriptor, u32 +0x18 = 0x60 (parameter size), u32 +0x1c = 0x20000000, u32 +0x20 = 2. This is how the game's own
  initialiser builds kCameraCinema (PS4 0x4ca66a).
* Owner: +0x1a0 camera handle (u64, u32 id at +0x1a8); u8 +0x1b2 screenshot request; i32 +0x1b4 screenshot state;
  u8 +0x1b8 = low byte of the followed selection; u8 +0x1bc controls-help toggle; +0x6c0 free-camera controller;
  i32 +0xa60 (free camera active when non-zero).
* State object: i32 +0x18 = next re-record time.
* Follow camera: rotation quaternion at +0x1f0 (x y z w); flat direction 3 floats at +0x1c4, +0x1c8, +0x1cc.
* HUD: u32 +8 flags; u32 +0x38 element count; from +0x40 an array of element pointers. Element: u32 +8 flag value,
  u32 +0xc "shown", u32 +0x10, i32 +0x18 time stamp.
* Class registry (0x1b3efd8): u8 +0 = initialised; from +8, 257 list heads of 16 bytes (first field = pointer to the
  first entry; an empty or finished chain points back at the head's own address). Entry: pointer +0 = next,
  u32 +0x10 = class name hash, u32 +0x14 = bit number.
* Tracker (0x9c0 bytes): u32 +0x1e4 (low 12 bits = local id), u32 +0x1e8 (user number), u8 +0x1ec (guest flag).
  Network id = L + (u32 +0x1e8) x 0x1000 (32-bit wrap), with L = low 12 bits, plus 0x800 when the guest flag is
  non-zero and the low 12 bits are not zero.
* Bookmark (0x30 bytes): bit stream {u64 +0 word pointer, u32 +8 bit position, u32 +0xc capacity in bits,
  u32 +0x10 length}, i32 +0x18 time, u32 +0x1c read-stream position, u64 +0x20 buffer, u64 +0x28 writer stamp.
* Override object (result of 0x12284e0): i32 +0x10 time, i32 +0x28 frame count, u8 +0x49 flags.

### 3.5 Ids, strings, constants

Type names "kCameraCinemaFollow", "kCameraCinemaFixed"; class names "CameraControlCinemaFollow",
"CameraControlCinemaFixed" (zero-terminated; they are the game's own names and must be spelled exactly, the hash of
the class name selects the class). Class id of the plain Cinema camera at object +0x30: 0xb1f56f8c. Override id
0x309861e2 (scale). Heap tag 0x14. Bookmark buffer 0x2800 bytes = 0x14000 bits.

Float constants (pattern = value): 0x3f800000 = 1.0; 0x3f000000 = 0.5; 0x40000000 = 2.0; 0x42960000 = 75.0;
0x3c23d70a = 0.01; 0x20000000 = 2^-63; 0x3f7fbe77 = 0.999; 0x40490fdb = 3.14159274 (pi); 0x3f22f983 = 0.636619747
(2/pi); 0x4effffff = 2147483520.0; 0x4f000000 = 2147483648.0; 0x3fc90fda = 1.57079625 and 0x33a22169 = 7.54979e-08
(pi/2 split in a high and a low part); 0x400ccccd = 2.2 (the only exponent used).

Polynomial coefficients. They are published mathematical constants, each the single-precision float nearest to the
decimal given, not game data, so the rule "nearest float to the published decimal" generates them (PS3 1.19 keeps
them at 0xdc6050..0xdc60b0 and 0xd9da00..0xd9da50; no place in the PS4 image is known from which they could be
read at run time):
* arc cosine, Abramowitz and Stegun 4.4.46 (acos a = sqrt(1 - a) x polynomial of degree 7), by power of a:
  a^0 1.5707963050 (0x3fc90fda), a^1 -0.2145988016 (0x3e5bbfca), a^2 0.0889789874 (0x3db63a9e),
  a^3 -0.0501743046 (0x3d4d8392), a^4 0.0308918810 (0x3cfd10f8), a^5 -0.0170881256 (0x3c8bfc66),
  a^6 0.0066700901 (0x3bda90c5), a^7 -0.0012624911 (0x3aa57a2c). Patterns are those of the magnitudes.
* sine on [-pi/4, pi/4] (the coefficients of the open Sony SIMD math library's `sinf4`):
  S3 = 0.1666665247 (0x3e2aaaa1), S5 = 0.0083320758 (0x3c088342), S7 = 0.0001950727 (0x394c8c6e), for
  sin x = x - S3 x^3 + S5 x^5 - S7 x^7.
* cosine on the same range: C2 = 0.4999990225 (0x3effffdf), C4 = 0.0416566950 (0x3d2aa036),
  C6 = 0.0013602249 (0x3ab24993), for cos x = 1 - C2 x^2 + C4 x^4 - C6 x^6.

## 4. Behaviour

### 4.0 Float rules and notation

* The helper is compiled without SSE: float arithmetic runs on the x87 unit with the rounding mode and precision
  control the calling game thread has (round to nearest even; the helper never changes the precision; on PS4 the
  significand is 64 bits). "Register precision" below means one x87 operation with that setting, its result not
  rounded to 32 bits.
* F32(v): v rounded to a 32-bit float (what a 4-byte store does). Every input read from memory or passed as a
  pattern is a 32-bit float; every result written to memory or returned as a pattern is F32.
* The formulas give the exact sequence and grouping of the elementary operations. Each operation is one x87
  operation at register precision unless F32 is written around it. Do not re-associate.
* Because the old helpers are inlined into their callers, the compiler decided where intermediates are rounded
  (it rounds a value only when it puts it into a 4-byte stack slot). Sections 4.3 to 4.5 state the rounding of the
  shipped v7.212 object code (read from its assembly). Where this differs from what the old source text suggests,
  the place is marked "(shipped)"; section 8 says what that means for the test.
* SEL(a, b, c): b when a >= 0, otherwise c; a NaN `a` gives c; -0.0 counts as >= 0.
* Comparisons are ordered: any comparison with a NaN is false.
* An integer converted to float is loaded exactly (not rounded to 24 bits) unless F32 is written. Float to integer
  is truncation toward zero; a NaN or a value outside the 32-bit range gives 0x80000000.

### 4.1 The float one-liners

| old name | exact contract |
|---|---|
| `fbits(f)` | the 32-bit pattern of f as a 32-bit float; forces F32 on a register value |
| `bitsf(u)` | the float whose pattern is u |
| `rd_f(p)` | the 32-bit float at address p (no alignment needed) |
| `rp_fsel(a, b, c)` | SEL(a, b, c) |
| `kf(u)` | the float constant with pattern u, loaded from a stack word by an x87 load, so that no constant lands in the helper's read-only constant section; value identical to `bitsf(u)` for every pattern used (none is a NaN) |
| `rp_sqrt(x)` | the x87 square root of x at register precision (correctly rounded at that precision); sqrt(-0.0) = -0.0; a negative x gives the default NaN |

### 4.2 The call bridge `rp_fcall` (stays as it is)

`uint64_t rp_fcall(uint64_t *r)`: r points to 14 quadwords.

| slot | use |
|---|---|
| r[0] | address of the function to call |
| r[1] .. r[6] | integer arguments 1 to 6 (rdi, rsi, rdx, rcx, r8, r9), full 64 bits |
| r[7] .. r[10] | low 32 bits = float patterns for xmm0 .. xmm3 (the rest of each register is zero) |
| r[11], r[12] | two stack arguments, at [rsp] and [rsp+8] when the function is entered by the call |
| r[13] | after the call: its low 32 bits hold the low 32 bits of xmm0 (a float result); its high 32 bits are left untouched |

The stack pointer is 16-byte aligned at the call; al = 4 (number of vector registers used, for variadic callees).
The result is the callee's rax, unchanged. The bridge reads all of r before the call and writes only r[13]
afterwards; it keeps the callee-saved registers. Every caller in this group zeroes all 14 slots first and never
reads r[13].

### 4.3 POW(x, y) and `u3x_rp_pow_mul` (old `rp_powf`)

POW takes two 32-bit floats and yields a register-precision value:
* x == 0 (either sign): +0.0.
* x < 0: z = x - x, result z / z: a NaN (for a finite x this is 0 / 0, the x87 default NaN; stored as a float it is
  0xffc00000).
* otherwise (x > 0, or x a NaN), with the x87 transcendental operations: L = y x log2(x) (one operation, the x87
  "y log2 x" instruction); I = L rounded to an integer in the current rounding mode (nearest, ties to even);
  F = L - I; R = (2^F - 1) + 1.0 (the x87 "2^x - 1" instruction, then the addition); result = R x 2^I (the x87 scale
  instruction). Consequences to keep: POW(1, y) = 1.0 exactly; POW(+infinity, y) for y > 0 is a NaN (infinity minus
  infinity in F); a NaN x gives a NaN.
* The result is not rounded to 32 bits. (shipped) The only caller multiplies four 32-bit floats with it, and each
  product is formed with the unrounded power and then rounded: value_k = F32(factor_k x POW(x, 2.2)).

`u3x_rp_pow_mul(x_bits, y_bits, factor_bits)` returns the pattern of F32(factor x POW(x, y)). Calling it four times
with the same x and y gives the four values of the old code (the power is a pure function of x and y).

The four x87 transcendental instructions define the result; their last bits are a property of the processor, so
old and new can only be compared on the same machine, and the new code has to use the same four operations in the
same order. This is the standard x87 exponentiation and carries no game-specific expression.

### 4.4 SIN and the blend `u3x_rp_slerp(q0, q1, t_bits, out)` (old `rp_sinq`, `rp_slerp`; PS3 f_a6e818 / 1.19 f_a82dbc)

Spherical interpolation of two quaternions in the branch-free PS3 form. The result is not normalised. Inputs:
q0 and q1 (4 floats x y z w each, F32 in memory), t = the float of t_bits. All inputs are read before the first
output is written.

1. D = ((q0x x q1x + q0y x q1y) + q0z x q1z) + q0w x q1w, register precision throughout.
2. When 0 > D: every lane of q1 is replaced by (0.0 - q1_i) (a subtraction from +0.0, not a sign flip: a zero lane
   becomes +0.0). Call the result q1'.
3. d = F32(D). When 0 > d: d = 0.0 - d. (shipped: the test of step 2 uses the unrounded D, this one the rounded
   value. They differ only when D is negative and rounds to -0.0: then q1 is negated and d stays -0.0.)
4. linear = not (0.999 > d) (true also for a NaN d). sign = the sign bit of the pattern of d (set only for -0.0 or
   a NaN with the sign bit). a = |d|. x = 1.0 - a.
5. Arc cosine factor, with the A&S coefficients k0 .. k7 of 3.5 (magnitudes):
   u = ((k6 - k7 x a) x a - k5) x a + k4; v = ((k2 - k3 x a) x a - k1) x a + k0; a2 = a x a; a4 = a2 x a2;
   P = u x a4 + v. No rounding to 32 bits anywhere in this step.
6. root = sqrt(x) (x itself when x == 0, which is the same value). theta = root x P; when the sign of step 4 is
   set: theta = pi - root x P (pi = 0x40490fdb). theta stays at register precision.
7. u1 = 1.0 - t (register precision). Three sines (below): s0 = SIN of theta, s1 = SIN of (u1 x theta),
   s2 = SIN of (t x theta); both products use the unrounded theta and u1.
8. Weights: linear: w0 = F32(u1), w1 = t. Otherwise w0 = s1 / s0 and w1 = s2 / s0, with s1 rounded to 32 bits
   before the division (shipped), s0 and s2 not rounded, the quotients not rounded. The three sines and both
   quotients are computed in the linear case too (their results are then unused; a zero divisor has no visible
   effect).
9. out_i = F32( q0_i x w0 + q1'_i x w1 ) for the 4 lanes (two products and the sum at register precision, one
   rounding at the store).

SIN(A), sine by quadrant reduction (A is a register-precision value):
1. y0 = A x (2/pi) [0x3f22f983]. h = -0.5 when the sign bit of F32(y0) is set, else +0.5. y = y0 + h (y0 unrounded).
2. n (int32) = 0x7fffffff when y >= 2147483520.0; otherwise 0x80000000 when y <= -2147483648.0; otherwise y
   truncated toward zero (a NaN gives 0x80000000).
3. x = (A* - n x 1.57079625) - n x 7.54979e-08, with n as an exact integer; x2 = x x x; x3 = x x x2.
4. sine = ((S5 - S7 x x2) x x2 - S3) x x3 + x; cosine = ((C4 - C6 x x2) x x2 - C2) x x2 + 1.0 (coefficients of 3.5).
5. Result = sine when n is even, cosine when n is odd; negated (sign flip) when bit 1 of n is set.
No step rounds to 32 bits except (shipped) the following, which differ between the three uses:

| use | A in step 1 | A* in step 3 | x3 | result |
|---|---|---|---|---|
| s0 (theta) | theta, unrounded | F32(theta) | unrounded | unrounded |
| s1 (u1 x theta) | the product, unrounded | F32(product) | unrounded | F32 |
| s2 (t x theta) | the product, unrounded | the same unrounded product | F32(x x x2) | unrounded |

The old source text has none of these six roundings, and would round n to a float in step 3 (no difference for
|n| < 2^24); they are the compiler's register spills in the shipped build.

For theta in [0, pi] the quadrant n is 0, 1 or 2.

### 4.5 Flat direction `u3x_rp_follow_dir(cam)` (old `rp_follow_dir`; PS3 f_1b928c, inlined in the PS4 Follow init 0x4c4810)

Input: the rotation q = (x, y, z, w), 4 floats at cam +0x1f0. Output: 3 floats d0, d1, d2 at cam +0x1c4, +0x1c8,
+0x1cc: the camera's x axis flattened to the ground plane and normalised; when that is shorter than 0.01, the z
axis turned by 90 degrees instead.

1. X axis, lanes 0 and 2 only: e0 = (0.0 - (y x y + z x z)) x 2.0 + 1.0; e2 = (x x z - w x y) x 2.0. Register
   precision, nothing rounded (shipped: the values the source stores in between are not re-read).
2. len = sqrt(e0 x e0 + e2 x e2). When len < 0.01 (0x3c23d70a; false for a NaN), the pair is replaced, from the z
   axis: g = (F32(x x z) + w x y) x 2.0; e0 = (0.0 - (x x x + F32(y x y))) x 2.0 + 1.0; e2 = 0.0 - g.
   (shipped: only the two products x x z and y x y are rounded to 32 bits here, because the compiler reuses the
   copies it kept from step 1; in step 1 itself they are not rounded.)
3. l = sqrt(e0 x e0 + e2 x e2); m = SEL(2^-63 - l, 1.0, l); r = 1.0 / m (register precision; exactly 1.0 when the
   first branch of SEL is taken).
4. Stores: d0 = F32(e0 x r), d2 = F32(e2 x r), d1 = F32(0.0 x r) (so +0.0 unless r is a NaN).
Nothing else is written; no game call. Before step 4 the old code also stores the unnormalised values of steps 1 and
2 into the same three fields (d1 = 0); only the final values can be observed.

### 4.6 Free camera step `u3x_rp_cam_mode12(base, st, own)` (old `rp_cam_mode12`; PS3 1.19 0x279de4..0x27a00c)

1. clock = the pointer at 0x17c3580. When the i32 at st +0x18 is <= the i32 at clock +0xc:
   `u3x_camera_rerecord(base, own)`; then st +0x18 = (clock +0xc, read again after the call) + 9.
2. When the i32 at owner +0xa60 is 0 or the camera mode (0x17c5a10, read now) is not 1: return.
3. A local frame, made with `u3x_frame_make(frame, base + 0x16ea550)`.
4. When `u3x_rp_h(owner + 0x1a0)` is null: L = WORLD(frame);
   `u3x_camera_spawn(base, own, base + 0x17b30e8, 0x42960000, &L, null)`.
5. Game 0x7a3e30(owner + 0x6c0, frame).
6. cam = `u3x_rp_h(owner + 0x1a0)` (resolved again). When it exists and its u32 at +0x30 equals 0xb1f56f8c (an exact
   class id, not the is-a test): L = WORLD(frame) (after the game call changed the frame);
   `u3x_camera_cinema_set(cam, &L)`.
No float arithmetic of its own. Identical to `film_cameras.md` 4.8.

### 4.7 Cameras

`u3x_camera_spawn(base, own, type, fov, locator, target)`:
1. Builds the parameter block of 3.4: fov pattern; the location copied when `locator` is given (else zeros) and
   the byte +0x50 = 1 / 0 accordingly; the target handle = the target's own handle (u64 at target +0x70, u32 at
   target +0x7c) when `target` is given, else zeros.
2. mgr = `u3h_camera_manager(base)`; cam = game 0x101b530(mgr, &type word, &0x0000000200000000 word, block). The
   call is made also when mgr is null (after its assert).
3. Owner handle: u64 owner +0x1a0 = u64 at cam +0x70 and u32 owner +0x1a8 = u32 at cam +0x7c; both 0 when cam is
   null.

`u3x_camera_rerecord(base, own)`:
1. When the u32 at 0x17c5d00 is 0: return with nothing done.
2. A local frame = `u3x_frame_make(frame, base + 0x16ea550)`.
3. Pointer 0x1b68840 = base + 0x17c3850.
4. Through the bridge: game 0x6c1d60 with xmm0 = the 32-bit pattern at 0x16e716c, first argument = the low byte of
   the camera mode (0x17c5a10) zero-extended, second = the byte at owner +0x1b8, third = the frame; every other
   bridge slot 0.
5. Pointer 0x1b68840 = null.

`u3x_camera_type(base, fixed)`: the helper owns two type objects (0x28 bytes each, zero at load, 16-aligned, in its
own zero-initialised storage; index 0 = Follow, 1 = Fixed; `fixed` is used as the index, callers pass 0 or 1). When
the selected object's first u64 is still 0 it is filled as in 3.4: name = the type name string (in the helper's
read-only data), hash = game 0xd94550(0, class name string, 1), descriptor = base + 0x1b5b4d0 (Follow) or
base + 0x1b5b6c0 (Fixed), then 0x60, 0x20000000, 2. The name pointer is written before the hash call. Returns the
object's address; later calls return the same address without a game call.

### 4.8 `u3x_rp_hud_flag(base, f)` (PS3 1.19 f_99479c as PS4 inlines it at 0xb83d3f)

HUD = image 0x1805fa0. u32 HUD +8 |= f. Then for each of the (u32 HUD +0x38) element pointers from HUD +0x40, in
order: skip a null pointer and an element whose u32 +8 is not f; otherwise u32 element +0xc = 1 and, when its u32
+0x10 is not 1, i32 element +0x18 = the i32 at (pointer 0x1b24b98) +0x94. The count is read once.

### 4.9 Lookups

`u3x_rp_trk_by_id(base, id)`: null for id 0. Otherwise the first of the 10 trackers at (pointer at 0x16e2f50)
+0x390 + i x 0x9c0, i = 0..9, whose network id (3.4) equals id; null when none. No test whether a net game exists.

`u3x_rp_isa(base, obj, hash)`: 0 when the registry's first byte is 0. Otherwise walk the chain of list head number
(hash modulo 257, unsigned) until it returns to the head's address; at the first entry whose hash equals `hash`:
b = its bit number, result = bit (b mod 64) of the u64 at (type info of obj) + 0x20 + 8 x (b / 64), as 0 or 1. No
entry: 0. `obj` is only touched when an entry is found.

### 4.10 Message send and overrides (through the bridge, every unused slot 0)

`u3x_rp_send(base, msg, target)`: game 0x1197380 with a zero-filled result block of 0x20 bytes as the first
argument, 0 as the second, msg, target, and four separate zero-filled argument blocks of 0x18 bytes (fifth and sixth
argument, then the two stack arguments, in this order). 8-byte aligned blocks; the result is ignored.

`u3x_ovr_blend(base, t, addr, val, frames, owner)`: nothing when owner is null. Otherwise game 0x12284e0 with
xmm0 = t, override id 0, addr, val, size 4, compare = base + 0x1228b20, set = base + 0x1228b30, stack arguments
owner and 0. When it returns an object o: i32 o +0x28 = frames + 1; u8 o +0x49 |= 2.

`u3x_ovr_scale(base, addr, val)`: game 0x12284e0 with xmm0 = 1.0, id 0x309861e2, addr, val, size 4,
compare = base + 0x1228b20, set = base + 0x1228ca0, stack arguments 0 and 0. Object o: i32 o +0x28 = 0;
u8 o +0x49 |= 8.

`u3x_ovr_byte(base, hash, addr, val, frames, time)`: game 0x12284e0 with xmm0 = 1.0, id = hash, addr, val, size 1,
compare = base + 0x12284c0, set = base + 0x12284d0, stack arguments 0 and 0. Object o:
u8 o +0x49 = (old value AND 0xdb) OR 4; i32 o +0x28 = frames + 1;
i32 o +0x10 = (i32 at (pointer 0x1b24b98) +0x94) + 9 + time.

A null result: nothing further. The value behind `val` is only handed to the game.

### 4.11 `u3x_bookmark_create(base, bm, t)` (PS3 1.19 f_275d48 + f_2712bc)

1. When the bookmark's buffer pointer (+0x20) is null: buffer = game 0xd9e990(0x2800, pointer to a 32-bit word
   0x14, 8), stored at +0x20 (also when null). An existing buffer is reused.
2. The bookmark's stream is emptied over the buffer: word pointer +0 = buffer, position +8 = 0, capacity
   +0xc = 0x14000, length +0x10 = 0. Stamp +0x28 = the u64 at 0x17c3c58. Read-stream position +0x1c = the u32 at
   0x17c3840. Time +0x18 = t. Pointer 0x1b68840 = bm. All of this before the first of the following calls.
3. first = the tracker at 0x17cb3f0; id = its network id (3.4).
4. Game calls in this order: 0x6b1b90(id); 0x6b1dc0(); 0x7427d0(base + 0x17cb060); 0x72e560(base + 0x17cb060, id).
   They write the snapshot into the stream 0x1b68840 points to.
5. `u3x_stream_write(base, bm, 0, 32)`: a 32-bit zero ends the snapshot.
6. Pointer 0x1b68840 = null.

### 4.12 `rp_screenshot_step(base, st, own)` (PS3 1.19 0x27a5ac..0x27a888)

PS4 has no photo utility; every step takes PS3's failure path.
1. u8 owner +0x1bc = 0; u8 0x17c3572 = 1; u8 0x17c3571 = 1.
2. With a local 32-bit word 0: `u3x_ovr_scale(base, base + 0x17b4b1c, &word)`, then
   `u3x_ovr_scale(base, base + 0x17b4adc, &word)` (clocks 1 and 0 stop).
3. The state, i32 owner +0x1b4: 0 becomes 1; 1 becomes 10; 10, 12 and 14 become 11, 13 and 15 and clear the request
   byte owner +0x1b2; every other value (11, 13, 15 included) stays, and nothing else changes.
4. The pressed-buttons word of pad 0 (`*u3x_rp_pad(base)`) = 0.
5. Returns 1. `st` is not used.

## 5. Test scenarios

Float code: compare patterns bit for bit, old and new built with the helper's command line and run on the same
machine (4.3) with the same x87 control word.

* POW / `u3x_rp_pow_mul`: x = +0.0 and -0.0 (products are zeros with the factor's sign rule of a multiplication by
  +0.0); x = 1.0 (product = factor exactly); negative x and -infinity (NaN, 0xffc00000 for a non-NaN factor);
  +infinity (NaN); a NaN x; x = 0.5, 0.18, 2.0, 100.0, the smallest subnormal, the largest float (overflow of the
  product to infinity only at the F32 store); factors 0, 1, -3.5, subnormal; thousands of random x in (0, 4] with
  y = 2.2 and random factors. One case that separates "power rounded first" from the required behaviour must be
  included (search random inputs for a pair where F32(factor x F32(p)) differs from F32(factor x p)).
* `u3x_rp_slerp`: equal quaternions (linear, w0 = 1 - t); dot just below and just above 0.999; opposite
  quaternions (negation of q1); orthogonal ones (dot 0: theta = pi/2, quadrant 1 for s0); dot exactly -0.0 (q0 all
  zero lanes against negative lanes: sign set, theta = pi - ...); t = 0, 1, 0.5, 1e-10 (F32(1 - t) = 1), 0.25, a
  NaN; a NaN lane; non-unit quaternions with a dot above 1 (x negative: NaN root; linear by the 0.999 test);
  thousands of random unit pairs and t in [0, 1]. Mutations to catch: rounding theta before the two products,
  rounding s0 or s2, not rounding s1, taking |D| instead of |F32(D)|, rounding n to 24 bits, swapping the
  quadrant rule.
* `u3x_rp_follow_dir`: identity rotation (direction (1, 0, 0)); rotations about the up axis by several angles; a
  rotation whose x axis is vertical (fallback, e.g. 90 degrees about z); flat length just below and just above
  0.01; a zero quaternion (e0 = 1 in step 1: no fallback, result (1, 0, 0)); (0.5, 0.5, 0.5, 0.5) (step 1 gives
  exactly (0, 0), the fallback (0, -1)); (0, 0.70710677, 0, 0) (both pairs tiny but not zero: divisor l, result
  (1, 0, 0) or (-1, 0, 0)); NaN lanes (all three outputs NaN); infinite lanes; random unit and non-unit
  quaternions. The first branch of SEL in step 3 (divisor 1) needs a final pair shorter than 2^-63; no plain
  rotation reaches it (see section 6).
* `u3x_rp_cam_mode12`: re-record due / not due (equal times are due; the new time is clock + 9 with the clock value
  after the call); owner +0xa60 zero; mode 2 (return after the re-record part); mode 1 without a camera (spawn with
  the kCameraCinema type, fov 75, the frame's world location, no target), with a Cinema camera (location set after
  the controller call), with a camera of another class id (controller call only).
* `u3x_camera_spawn`: locator null / given, target null / given (block bytes compared in full), manager returning
  null (owner handle zeroed) and an object; the two words passed by address.
* `u3x_camera_rerecord`: reader word zero (no call, 0x1b68840 untouched); non-zero: bridge record (speed pattern in
  slot 7, mode low byte with mode values 1, 2, 0x101, -1; owner byte), 0x1b68840 during and after the call, frame
  contents.
* `u3x_camera_type`: first call per kind (one hash call, fields), second call (no game call, same address), both
  kinds distinct.
* `u3x_rp_hud_flag`: count 0; elements null, with another flag value, matching with +0x10 = 1 (no stamp) and other
  values (stamp); flag bits already set.
* `u3x_rp_trk_by_id`: id 0; id of tracker 0, 9; an id present twice (first wins); guest flag with zero and non-zero
  low bits; no match.
* `u3x_rp_isa`: registry not initialised; empty chain; hash in the first / a later entry; other hashes in the same
  chain; bit numbers 0, 63, 64, 200 with the bit set and clear; hash 0 and 0xffffffff (chain index).
* `u3x_rp_send`: the bridge record (slot contents, the four blocks distinct and zero, result block zero).
* `u3x_ovr_blend`: null owner (no call); object returned / null; frames -1, 0, 200, 0x18737; flag byte with other
  bits. `u3x_ovr_scale`, `u3x_ovr_byte`: object returned / null; flag byte 0xff and 0x00 before; negative time.
* `u3x_bookmark_create`: slot without buffer (allocator called with 0x2800, tag 0x14, 8) and with one (not called);
  allocator returning null; all slot fields; 0x1b68840 seen by each of the four game calls and null afterwards; the
  order and arguments of the calls; the 32 zero bits written at the position the game calls left; the assert when
  the position passes the capacity.
* Screenshot step: every state 0, 1, 10 .. 15 and an unlisted one (e.g. 5, -1): state after, request byte, the two
  override calls and their order, pause bytes, pad word.
* Bridge: a stub function that records its six integer registers, xmm0..3, the two stack words, al and the stack
  alignment, and returns values in rax and xmm0.

## 6. Bugs, dead code and oddities to keep

1. 4.4: the "linear" case still computes three sines and two quotients; a dot product of -0.0 keeps its sign bit and
   takes the pi - ... branch; q1 is negated by subtraction from +0.0; the negation test and the later sign handling
   look at differently rounded values (4.4 step 3); the result is not normalised.
2. 4.4: the three copies of the sine round at different places (table in 4.4). This is an accident of the shipped
   build, but it is what the shipped helper computes.
3. 4.5: lane 1 of the x axis and lane 1 of the z axis are never needed; the middle output is 0.0 x r, not a plain
   zero; the length is computed twice with the same value on the main path. The "divisor 1" branch (final length
   <= 2^-63) is practically dead: without the fallback the length is at least 0.01, and the fallback pair is only
   that short for specially constructed lanes. Keep the branch.
4. 4.3: POW(+infinity) is a NaN; a negative base gives a NaN whatever the exponent.
5. 4.7 `u3x_camera_spawn`: the camera manager call is made even when the manager is missing (after the assert).
6. 4.7 `u3x_camera_rerecord` passes only the low bytes of the mode and of the followed selection.
7. 4.9 `u3x_rp_trk_by_id` reads the trackers through the pointer at 0x16e2f50 without a null test and without the
   net-game test the other tracker accessor has.
8. 4.10 `u3x_rp_send` goes through the bridge although it has no float argument (so xmm0..3 are zero and al is 4 at
   the call). `u3x_ovr_blend` tests its owner for null, the other two override functions have no owner at all.
9. 4.11: the first tracker is taken as null when the byte at 0x17d5c61 is 0, and the network id is then read from
   that null pointer (addresses 0x1e4, 0x1e8, 0x1ec): a fault. Not reachable while a film plays (a net game always
   exists). The new code needs the same precondition and must not give this case a meaning of its own. The heap tag
   is a single 32-bit word (other callers of the same allocator pass a second word 0 after it; either form is
   compatible).
10. 4.12: the branches that would follow a photo-utility callback (states 11, 13: advance; state 15: controls help
    back on, request byte cleared, both pause bytes = the byte at st +0x20) can never run: the callback flag is a
    constant false. Dead; no code is needed for them, and `st` is unused. Once the state reaches 11 a later request
    changes nothing but the pause bytes, the overrides and the pad word.
11. 4.2: the bridge saves one register it does not use and always sets al = 4.

## 7. What stays as it is

* `rp_fcall` (top-level assembly in the C file) and its export `u3h_rp_fcall`. It is the only way for code built
  without SSE to load xmm registers and place stack arguments for a game call; it is a pure calling-convention
  bridge with no game logic, fully determined by the System V convention and the record layout of 4.2. A rewrite
  from 4.2 would be the same instructions.
* The four transcendental operations of POW (4.3): dictated by the processor's instruction set.
* The C one-liners `fbits`, `bitsf`, `rd_f`, `rp_fsel`, `kf`, `rp_sqrt` while C callers exist (other groups use
  `kf`, `fbits`, `bitsf`); the C++ part has its own equivalents.
* The C wrappers `rp_trk_by_id`, `rp_isa`, `rp_send`, `ovr_blend`, `rp_cam_spawn`, `rp_rerecord_cam`, `rp_camtype`,
  `rp_hud_flag` and the exports `u3h_camera_manager`, `u3h_rp_cam_mode12` (one-line forwards) until their C callers
  are gone.

## 8. Open questions

1. Which rounding is the reference for 4.3 to 4.5. The values marked "(shipped)" come from the assembly the
   helper's own command line produces for v7.212 (`rp_death_look`, `rp_follow_cam`, `u3h_rp_render_vars`, where the
   old inline helpers end up). The same old source compiled in another context (another caller, a test harness,
   another target) can round at other places, because the compiler rounds a `float` exactly where it happens to put
   it into a 4-byte stack slot. Proposal: the reference of the test is the old code in its original inlining context
   built with the helper's exact command line, and the new code makes every rounding explicit. For that the new
   code needs (a) a way to keep a value at register precision across statements without the compiler rounding it
   and (b) a way to force F32 that the optimiser does not remove (a pattern round trip through `x87_bits` /
   `x87_float` is folded away). `CONVENTIONS.h` offers neither today.
2. `film_cameras.md` (4.6, section 8) and `film_maths.md` (4.11, section 8) describe the blend at source level and
   state for the shipped build that |dot| is taken from the unrounded dot product. The assembly read for this spec
   shows otherwise (4.4 steps 2 and 3): the unrounded dot product is only used for the test that negates q1 and is
   then discarded; the value whose sign is tested again, whose absolute value is taken and which is compared with
   0.999 is the copy reloaded from a 4-byte slot, so it is F32. The table of the three sines in 4.4 is likewise
   more specific than their "rounded where the compiler parked them". The three texts must be made to agree before
   an implementer starts.
3. The power's result must reach its products unrounded (4.3; `film_maths.md` 4.12 and its section 8 say the same).
   Either the power is written inside the render-variable function, or the four values are taken from
   `u3x_rp_pow_mul`; a function that returns the power as a 32-bit pattern would change last bits.
4. The x87 precision control of the game threads on the PC build is not known here (PS4: 64-bit significand). Old
   and new must be compared under the same control word; with a 53-bit setting the "register precision" above is
   53 bits.
5. `u3x_rp_cam_mode12` and the flat direction are described here and in `film_cameras.md`; the blend here, in
   `film_cameras.md` and in `film_maths.md` (which assumes it implements it); the power here and in `film_maths.md`.
   They agree in the operation sequence; decide which group implements each.
6. `film_maths.md` (open question 1) proposes to accept differences from the shipped build's last bits as the cost
   of un-inlining. Question 1 here proposes the opposite for the pieces of this spec. One rule is needed for the
   whole film block.

## 9. Clarifications (tester, 2026-11-14)

Facts confirmed by the old-versus-new test `reimpl2\film_block\test_film.cpp` (old = the v7.212 functions, new =
`reimpl2\film_helpers\part.cpp`, `reimpl2\film_maths\part.cpp`, `reimpl2\film_cameras\part.cpp`).

1. Who delivers what (open questions 3 and 5). The helpers part holds 4.7 to 4.12. The power (4.3), the sine and
   the blend (4.4) are in the film maths part; the flat direction (4.5) and the free camera step (4.6) are in the
   film cameras part. `u3x_rp_pow_mul` and `u3x_rp_follow_dir` are not delivered: the power is a private function
   of the maths part and the flat direction is inside `u3x_rp_follow_set`.
2. Arithmetic of 4.3 to 4.5 (open questions 1 and 6). The "Float rule" of `reimpl2\PIPELINE.md` decides it: every
   elementary operation is one single-precision operation in the order given, and the power is rounded to 32 bits
   before its products. The rounding places marked "(shipped)" are not reproduced. With single-precision arithmetic
   old and new agree bit for bit (see the Clarifications of `film_maths.md` and `film_cameras.md`, which also give
   the measured differences from the old x87 build and the exact results of the flat direction). In single
   precision the blend's step 3 has no special case: the dot product is one rounded value.
3. Section 5, flat direction, corrected for single precision: (0, 0.70710677, 0, 0) gives (1, 0, 0). The "divisor 1"
   case is reached by (2^-12, 0.70710677 [0x3f3504f3], 0, 0): result (0, 0, 0).
4. 4.12: the screenshot step is delivered as
   `extern "C" int u3x_rp_screenshot_step(uintptr_t base, uint8_t *st, uint8_t *own)`, not as the file-private
   `rp_screenshot_step`; its caller `u3x_rp_update` has to use that name. Behaviour as described.
5. 4.7, camera types: the new part keeps one 0x28-byte object per kind in its zero-initialised storage, 16-aligned
   and apart from each other; the first call per kind makes exactly one hash call, later calls none (checked on the
   new part's own objects as loaded).
6. 4.11 and section 6 item 9, without a net game (byte 0x17d5c61 = 0): the old code compiled for the host does not
   fault. The compiler drops the null case (a read through a null pointer cannot happen in a valid program), so
   the network id is read from the first tracker at 0x17cb3f0 whatever the byte says. The new code reads it the
   same way, without looking at the byte. What the shipped helper does in that case was not checked; it cannot
   occur while a film plays.
7. 4.10, `u3x_ovr_byte`: "(old value AND 0xdb) OR 4" clears bit 0x20 and sets bit 0x04; the other bits stay.
   `frames + 1` and the time sum wrap as 32-bit values in all three override functions.
8. Everything else of 4.7 to 4.12 is confirmed as described: block contents, bridge slots, the order and the
   arguments of the game calls, the fields written.
9. Not covered: `u3x_rp_hud_fade` (inventory group G01, C++ "moved as written", next to `u3x_rp_hud_flag` in the
   old source) is described by none of the three film specs.
