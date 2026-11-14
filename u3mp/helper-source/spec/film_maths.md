# film_maths: the film player's vector, quaternion and bound-frame maths, and the render-variable blend (b003)

Group of the clean-room pipeline (reimpl2). Old code: `u3_lan_profiles.c` v7.212, hand-written part, the functions
`rp_xaxis`, `rp_zaxis`, `rp_norm_or`, `rp_cross`, `rp_lookat`, `q_mul`, `q_rot`, `bf_world`, `bf_local_pos`,
`bf_local_rot`, `rp_render_vars` and the power function `rp_powf` that only `rp_render_vars` uses. Everything here is
behaviour; nothing is copied from the old statements.

Added to the list given for the group, because they are pure float maths of the same player, still written out in
C next to the others and in the same inventory section (#129): `rp_slerp` with its sine helper `rp_sinq`
(section 4.11). `film_cameras.md` section 4.6 describes the same two functions as "BLEND" and "SIN" for the case that
this group does not deliver them. I checked that text against the old code: it is correct and agrees with 4.11 here.
The blend is to be implemented ONCE, in this group, under the name and signature both specs give
(`u3x_rp_slerp`).

Left out, and why:

* `bf_set_rot`, `bf_set_pos`, `bf_set_loc`: no arithmetic of their own. Each is "refresh the parent
  (`u3x_frame_parent`), then LOCALROT / LOCALPOS" (set-locator: position first, then rotation). `film_cameras.md`
  writes these sequences out inside its own functions; this group delivers LOCALPOS and LOCALROT (4.9, 4.10).
* `rp_follow_dir` (flat direction of the Follow camera): float maths too, but `film_cameras.md` 4.3 already
  describes it as part of the Follow setter. Not repeated here; it uses 4.1 and 4.2 of this spec.
* `kf`, `rp_sqrt`, `rp_fsel`, `fbits`, `bitsf`, `rd_f`: the C part's one-line float helpers (constant load, x87
  square root, select, bit casts). They are tools, not behaviour; section 4.0 says what the new code needs instead.
* None of the listed functions is already a one-line wrapper around a `u3x_` function. `bf_parent`, `bf_make`,
  `bf_identity` are (around `u3x_frame_parent`, `u3x_frame_make`, `u3x_frame_identity`) and are not part of this
  group.

Names used by `film_cameras.md` for this group's functions, and the names proposed here:

| film_cameras.md | this spec | section |
|---|---|---|
| WORLD(frame) | `u3x_rp_frame_world` | 4.8 |
| LOCALPOS(frame, p) | `u3x_rp_frame_local_pos` | 4.9 |
| LOCALROT(frame, q) | `u3x_rp_frame_local_rot` | 4.10 |
| XAXIS(q), ZAXIS(q) | `u3x_rp_axis_x`, `u3x_rp_axis_z` | 4.1, 4.2 |
| NORM(v, default) | `u3x_rp_unit_or` | 4.3 |
| LOOKAT(dir, up) | `u3x_rp_look_at` | 4.5 |
| BLEND(q0, q1, t) | `u3x_rp_slerp` | 4.11 |

## 1. Purpose and callers

The film player (RePlaying state, PS3 1.19 f_279d6c) runs `u3x_rp_update` once per frame. Two parts of what it needs
are in this group.

A. Maths for the film cameras. The camera code (`rp_cam_target`, `rp_follow_cam`, `rp_follow_set`, `rp_fixed_set`,
`rp_death_look`, `rp_cam_mode12`; all described in `film_cameras.md`) works with quaternions, 4-lane vectors and
"bound frames" (a frame given relative to a parent that can be a moving rigid body). In v7.212 the callers are:

| old function | called from (old) | what for |
|---|---|---|
| `bf_world` | `rp_death_look` (twice), `rp_follow_cam` (twice), `rp_cam_target` (twice, body view), `rp_cam_mode12` (twice) | world location of a frame, to spawn or set a camera, or as the start of a correction |
| `bf_local_pos` | `bf_set_pos` (from `rp_cam_target`: frame 3 m behind a dead body), `bf_set_loc` (from `rp_fixed_set`) | give the frame a world position |
| `bf_local_rot` | `bf_set_rot` (from `rp_death_look`), `bf_set_loc` (from `rp_fixed_set`) | give the frame a world rotation |
| `q_mul` | `bf_world`, `bf_local_rot` | quaternion product |
| `q_rot` | `bf_world` | rotate a vector |
| `rp_xaxis` | `rp_follow_dir` | x axis of the Follow camera's rotation |
| `rp_zaxis` | `rp_follow_dir` (fallback), `rp_cam_target` (direction "behind the body") | z axis of a rotation |
| `rp_norm_or` | `rp_death_look` (direction to the place of death), `rp_lookat` (four times) | unit vector with a fallback |
| `rp_cross` | `rp_lookat` (twice) | cross product |
| `rp_lookat` | `rp_death_look` | rotation that looks along a direction |
| `rp_slerp` (+ `rp_sinq`) | `rp_death_look` | blend from the current rotation to the look-at rotation |

B. The film's render settings. `rp_render_vars(base, own)` is called by `u3x_rp_update` on every update (through the
export `u3h_rp_render_vars`; directly after the free-camera step and before the screenshot check). It applies the
film's colour, fog, tint and depth-of-field values as overrides of the game's render-setting variables, keyed by the
film player's owner object, or releases those overrides (PS3 1.19 0x27a00c..0x27a59c and 0x27c520).

## 2. Interface

No float is passed or returned by value. "4 floats" is an array of four 32-bit floats, lanes 0..3; a quaternion is
stored x, y, z, w; a "location" is 8 floats (0x20 bytes): position lanes 0..3, then the rotation quaternion. A
"frame" is a 0x60-byte bound frame (section 3.1). None of the functions needs any alignment beyond 4 bytes.

Exported functions (all `extern "C"`, `U3X_FN`):

| new function | parameters and result | replaces | called by |
|---|---|---|---|
| `void u3x_rp_axis_x(const float *q, float *out)` | q: quaternion (need not be unit); out: 4 floats. | `rp_xaxis` | Follow setter (film_cameras 4.3) |
| `void u3x_rp_axis_z(const float *q, float *out)` | same | `rp_zaxis` | Follow setter, camera-target step (film_cameras 4.1 B2) |
| `void u3x_rp_unit_or(const float *v, const float *fallback, float *out)` | v, fallback: 4 floats each; out: 4 floats | `rp_norm_or` | death look, and 4.5 here |
| `void u3x_rp_look_at(const float *dir, const float *up, float *out)` | dir, up: 4 floats each (lane 3 has no effect); out: quaternion | `rp_lookat` | death look |
| `void u3x_rp_slerp(const float *q0, const float *q1, uint32_t t_bits, float *out)` | two quaternions; the blend factor as the 32-bit pattern of a float; out: quaternion | `rp_slerp`, `rp_sinq` | death look |
| `void u3x_rp_frame_world(uintptr_t base, uint8_t *frame, float *out)` | image base; frame (its parent block may be rewritten by the refresh); out: location, 8 floats | `bf_world` (with `q_rot`, `q_mul`) | the callers of table A |
| `void u3x_rp_frame_local_pos(uint8_t *frame, const float *p)` | frame; p: world position, 4 floats, all lanes used. Writes frame +0x40..+0x4f | `bf_local_pos` | the set-position / set-locator sequences |
| `void u3x_rp_frame_local_rot(uint8_t *frame, const float *q)` | frame; q: world rotation. Writes frame +0x50..+0x5f | `bf_local_rot` (with `q_mul`) | the set-rotation / set-locator sequences |
| `void u3x_rp_render_vars(uintptr_t base, uint8_t *own)` | image base; the film player's owner object | `rp_render_vars`, `rp_powf` | `u3x_rp_update` |

Internal pieces, described in section 4 so that they can be written and tested on their own, but with no caller
outside this group (no export needed; the tester may ask for them to be exported): the cross product (4.4), the
quaternion product (4.6), the rotation of a vector (4.7), the sine (4.11), the power (4.12).

Overlap of buffers: see each function. No caller passes overlapping input and output buffers, except where a
function below says it is allowed.

Names that are already taken in `u3x_fixes.cpp` and must NOT be used or called for this group: `u3x_q_mul`,
`u3x_q_rot`, `u3x_loc_mul`, `u3x_loc_rel`, `u3x_bf_world` (animation-state message, row 15). They do similar maths
but with another grouping of the operations, SSE single-precision steps on bit patterns (the `f_add` / `f_sub` /
`f_mul` helpers), a position lane 3 of zero and another way to find the parent, so their results differ in the last
bits and in lane 3. The same holds for the `f_*` helpers themselves: this group is x87 arithmetic (4.0).

One-line C wrappers while old C callers remain (each passes its arguments through unchanged):

| C function | becomes |
|---|---|
| `rp_xaxis(q, o)` | `u3x_rp_axis_x(q, o)` |
| `rp_zaxis(q, o)` | `u3x_rp_axis_z(q, o)` |
| `rp_norm_or(v, def, o)` | `u3x_rp_unit_or(v, def, o)` |
| `rp_lookat(dir, up, q)` | `u3x_rp_look_at(dir, up, q)` |
| `rp_slerp(q0, q1, t, o)` | `u3x_rp_slerp(q0, q1, fbits(t), o)` (the factor goes over as its pattern) |
| `bf_world(base, bf, o)` | `u3x_rp_frame_world(base, bf, o->v)` |
| `bf_local_pos(bf, p)` | `u3x_rp_frame_local_pos(bf, p)` |
| `bf_local_rot(bf, q)` | `u3x_rp_frame_local_rot(bf, q)` |
| `rp_render_vars(base, own)` | `u3x_rp_render_vars(base, own)`; simpler: `u3x_rp_update` calls it directly and the export `u3h_rp_render_vars` goes away |

`rp_cross`, `q_mul`, `q_rot`, `rp_sinq`, `rp_powf` have no caller left and disappear from C without a wrapper. When
the cameras group is implemented as well, all C callers are gone and none of the wrappers above is needed. See
section 7 for what stays in C.

## 3. Game facts

All addresses are image offsets (add `base`).

### 3.1 Structures

Bound frame, 0x60 bytes (PS3: 0x50 bytes with a 32-bit body handle; PS4 controller constructor 0x7a37c0):

| offset | type | meaning |
|---|---|---|
| +0x00, +0x04 | u32 each | marker 0x7fcccccc (not read here) |
| +0x08 | u64 | rigid-body handle (used only by `u3x_frame_parent`) |
| +0x10 | u32 | body id (used only by `u3x_frame_parent`) |
| +0x20 | 4 floats | parent position P |
| +0x30 | 4 floats | parent rotation Q (x, y, z, w) |
| +0x40 | 4 floats | local position L |
| +0x50 | 4 floats | local rotation R |

`u3x_frame_parent(base, frame)` (exists in `u3x_fixes.cpp`) copies the bound body's position and rotation into
+0x20..+0x3f while the handle is valid, and otherwise leaves the frame alone (PS3 f_9323b0).

Location: 8 floats, position then rotation (the C part's `RpLoc`).

Film writer W = image 0x17c3ba8 (PS3 1.19 fields +0x1e80.. are PS4 +0x1e94..; the depth-of-field fields 1.19
+0x1fa4 / +0x1fa8 / +0x1fac are PS4 +0x1fb8 / +0x1fbc / +0x1fc0):

| W offset | type | use in 4.13 |
|---|---|---|
| +0x1e94 | u32 (a float's pattern) | blend time of the saturation override |
| +0x1e98 | u32 (a float's pattern) | blend time of the three fog overrides |
| +0x1e9c, +0x1ea0, +0x1ea4 | float | first triple, scaled by 0.2 |
| +0x1ea8 | float | passed by address |
| +0x1eac, +0x1eb0, +0x1eb4 | float | second triple, scaled by 0.4 |
| +0x1eb8, +0x1ebc, +0x1ec0 | float | passed by address |
| +0x1ec4 | float | base of the power 2.2 |
| +0x1ec8, +0x1ecc, +0x1ed0, +0x1ed4 | float | four values multiplied by that power |
| +0x1f94, +0x1f98, +0x1f9c | float | added to the first triple |
| +0x1fa0, +0x1fa4, +0x1fa8 | float | added to the second triple |
| +0x1fb8 | i32 | depth of field on (non-zero) |
| +0x1fbc, +0x1fc0 | float | the two focus values (called F1 and F2 in 4.13) |

Render-settings structure: its address is the pointer stored at image 0x16e2fb8 (`u3x_rp_rs(base)` returns it; PS3:
the fixed structure 0x13957f0 of screen 0. PS3 fields below 0x7d0 keep their offset on PS4, 0x860 and above move by
+0x20, 0xb64 became 0xba4). Every field used is one 32-bit float; the overrides are addressed by field address:

| field offset | known meaning |
|---|---|
| 0x898 | saturation |
| 0x240, 0x244, 0x250 | fog (0x240: distance start) |
| 0x230, 0x234, 0x238 | receives the first triple |
| 0x00c | receives W +0x1ea8 |
| 0x000, 0x004, 0x008 | receives the second triple |
| 0x880, 0x884, 0x888 | receive W +0x1eb8, +0x1ebc, +0x1ec0 |
| 0xba4, 0x04c, 0x08c, 0x0cc | receive the four power-scaled values |
| 0x900, 0x904, 0x908, 0x90c | depth of field: near start, near end, far start, far end (PS3 +0x8e0..) |
| 0x8f0, 0x8f4, 0x8f8, 0x8fc | depth of field: four intensities (0x8f0: near start) |

Other data: owner +0x1bf, a byte (PS3 owner +0x12f): "the film's render settings are on". Image 0x17d7ad8 (=
0x17d7a50 + 0x88; PS3 1.19 0x1244404 + 0x88), a byte: a debug switch that turns them off.

### 3.2 Existing C++ functions used (all in `u3x_fixes.cpp`)

* `u3x_frame_parent(uintptr_t base, uint8_t *frame)`: see 3.1.
* `uint8_t *u3x_rp_rs(uintptr_t base)`: the render-settings structure.
* `void u3x_ovr_blend(uintptr_t base, uint32_t t, uint8_t *addr, const void *val, int32_t frames, uint8_t *owner)`:
  adds an override of the 4-byte variable at `addr` for `owner` with the value `val` points to, blend parameter `t`
  (a float's pattern), held for `frames` + 1 frames. It calls the game's 0x12284e0 through `u3h_rp_fcall` and writes
  two fields of the returned object; it does nothing when `owner` is null.
* `void u3x_ovr_release(uintptr_t base, uint8_t *addr, uint8_t *owner)`: releases the owner's override of `addr`
  (game 0x1228b80).
* The x87 helpers `x87_const(pattern)` (loads a float constant into the x87 unit), `x87_float(pattern)` and
  `x87_bits(float)` (bit casts).

No game function is called directly by this group.

### 3.3 Constants

| pattern | value | use |
|---|---|---|
| 0x40000000 | 2.0 | axes, rotation, local position |
| 0x3f800000 | 1.0 | several |
| 0x3f000000 | 0.5 | look-at, sine |
| 0x0da24260 | 1.0000000031710769e-30 | squared-length limit of the unit vector (PS3 1.10 0xd8a040, 1.19 0xd9e3c0) |
| 0x3f7fbe77 | 0.99900001287460327 | blend: limit of the dot product for the linear form |
| 0x40490fdb | 3.1415927410125732 | blend: pi |
| 0x3aa57a2c, 0x3bda90c5, 0x3c8bfc66, 0x3cfd10f8 | 0.0012624911, 0.0066700899, 0.0170881264, 0.0308918804 | blend: first chain of the arc cosine (magnitudes; signs in 4.11) |
| 0x3d4d8392, 0x3db63a9e, 0x3e5bbfca, 0x3fc90fda | 0.0501743034, 0.0889789909, 0.2145988047, 1.5707962513 | blend: second chain of the arc cosine |
| 0x3f22f983 | 0.63661974668502808 | sine: 2/pi |
| 0x4effffff, 0x4f000000 | 2147483520.0, 2147483648.0 | sine: limits of the integer conversion |
| 0x3fc90fda, 0x33a22169 | 1.5707962512969971, 7.5497901264043321e-08 | sine: pi/2 in two parts |
| 0x3ab24993, 0x3d2aa036, 0x3effffdf | 0.0013602249, 0.0416566953, 0.4999990165 | sine: cosine chain (magnitudes) |
| 0x394c8c6e, 0x3c088342, 0x3e2aaaa1 | 0.0001950727, 0.0083320756, 0.1666665226 | sine: sine chain (magnitudes) |
| 0x3e4ccccd | 0.20000000298023224 | render: scale of the first triple (PS3 1.19 0x113cdcc) |
| 0x3ecccccd | 0.40000000596046448 | render: scale of the second triple; depth-of-field amount |
| 0x400ccccd | 2.2000000476837158 | render: exponent (PS3 1.19 0x113ce00; 1.10 used 2.05) |
| 0x3d088889 | 0.033333335071802139 | render: 1/30 |
| 0x42c80000 | 100.0 | render |
| 0x3d4ccccd | 0.05000000074505806 | render: smallest near end |
| 0x41a00000 | 20.0 | render |
| 0x3ccccccd | 0.02500000037252903 | render: 1/40 |
| 200 and 0x18737 (100151) | integers | render: the two "frames" arguments |

The polynomial coefficients are not a table of the game: the PS4 image does not contain them (the old code
evaluates a PS3 library routine that PS4 no longer has), so they cannot be read at run time. They are published
numbers: the arc cosine set is Abramowitz and Stegun 4.4.46, the sine / cosine set and the two-part pi/2 are those of
Sony's openly published SIMD / vector maths library (`sinf4`). They are given here as the patterns the result
depends on.

Origins (facts): x axis PS3 1.10 f_17838 (1.19 f_17630); z axis f_177bc (1.19 f_175d0); look-at f_70ca40(dir, up, 1)
(1.19 f_71c89c) = f_a36d0c + f_a6e37c; blend f_a6e818 (1.19 f_a82dbc); bound frames: world f_932c30 (1.19
f_953e48), set rotation f_932688 (1.19 f_9538a0), set position f_9327ac, set locator f_9328ac (1.19 f_953ac4; PS4
has it inline at 0x4c9100); power f_d41d48 (1.19 f_d562e8). The old code did NOT reproduce the PS3 bits: PS3
normalises with a reciprocal-square-root estimate and one refinement step, the old code with an exact x87 square
root and division. The reference for this group is the old helper code, not the PS3.

## 4. Behaviour

### 4.0 Float rules

* All float arithmetic is x87 arithmetic (the helper is built with `-mno-sse`) on values loaded from 32-bit floats,
  rounding to nearest even, with the precision setting the game thread happens to have (never changed here). Write
  it as plain C++ `float` expressions with `x87_const` constants, the way `u3x_rp_update` does. Do NOT use the
  bit-pattern helpers `f_add` / `f_sub` / `f_mul` / `f_less` (SSE steps): they round every operation to 32 bits and
  overflow at 3.4e38, which this code does not.
* The formulas give the exact sequence and grouping of the elementary operations. Round brackets are evaluated
  first; a sum of three or more terms is always written with brackets. Do not re-associate, do not factor out, do not
  replace "0 - x" by a sign flip or "x + x" by "2 x" (signed zeros and NaN signs would change).
* "0 - x" always means: the constant +0.0 minus x (so +0.0 gives +0.0, not -0.0).
* "F32" marks a value that is a 32-bit float in memory before it is used again (round to nearest even): every value
  a function reads from or writes to a buffer of its interface, and every array that one of the functions below
  hands to another one. Values not marked F32 are intermediates of one formula and are not rounded between its steps
  (scalar temporaries of the old source).
* Square root is the x87 FSQRT instruction applied to the unrounded value; its result is again an unrounded
  intermediate. The C++ part has no helper for it yet: write one like `x87_const` (inline, result taken from the
  x87 register).
* Steps on a float's 32-bit pattern (4.11: "sign bit of", "sign bit cleared", "sign bit flipped", "0.5 carrying
  the sign bit of") are to be written as operations on the pattern (`x87_bits` / `x87_float`), not as comparisons
  with zero or arithmetic negation: -0.0 and NaN behave differently otherwise.
* SEL(a, b, c) means: b when a >= 0, otherwise c; a NaN `a` selects c (the PowerPC fsel).
* Comparisons are ordered: any comparison with a NaN is false. Integer conversion truncates toward zero.
* What the shipped -O2 build really rounds differs from the F32 marks in places (the compiler decides, not the
  source): section 8. On a build where every operation is rounded to 32 bits (SSE float arithmetic, e.g. the test
  harness without `-mno-sse`) the sequences below define every result completely.

Notation: a quaternion q has lanes (x, y, z, w) = (q0, q1, q2, q3).

### 4.1 `u3x_rp_axis_x(q, out)`: the rotated x axis

For a unit quaternion this is the first column of its rotation matrix; the formula is applied to any q as is.

* s = 0 - ((y x y) + (z x z)); m = (x x z) - (w x y); n = (x x y) + (w x z).
* out0 = F32((s x 2.0) + 1.0); out1 = F32(n x 2.0); out2 = F32(m x 2.0); out3 = F32(n x 2.0).

So out = (1 - 2(y^2 + z^2), 2(xy + wz), 2(xz - wy), lane 3 = a copy of lane 1). All four inputs are read before
anything is written, so `out` may be the same buffer as `q`.

### 4.2 `u3x_rp_axis_z(q, out)`: the rotated z axis

* m = (x x z) + (w x y); n = (y x z) - (w x x); s = 0 - ((x x x) + (y x y)).
* out0 = F32(m x 2.0); out1 = F32(n x 2.0); out2 = F32((s x 2.0) + 1.0); out3 = F32(n x 2.0).

So out = (2(xz + wy), 2(yz - wx), 1 - 2(x^2 + y^2), lane 3 = a copy of lane 1). Inputs are read first, as in 4.1.
Lane 3 matters: the camera-target step carries it into a frame's local position.

### 4.3 `u3x_rp_unit_or(v, fallback, out)`: unit vector or a fallback

* len2 = ((v0 x v0) + (v1 x v1)) + (v2 x v2). Lane 3 does not count.
* When len2 > 1.0e-30 [0x0da24260]: r = 1.0 / sqrt(len2); out_i = F32(v_i x r) for i = 0, 1, 2, 3 in this order
  (lane 3 is scaled too).
* Otherwise (also when len2 is a NaN): out_i = fallback_i for i = 0..3, copied unchanged.

len2, the comparison, the square root and r are not rounded: in the helper a vector as long as 1e25 is still
normalised correctly (its squared length does not overflow in the x87 unit), and one with an infinite lane gives NaN
in that lane and zeros in the others. `out` may be the same buffer as `v` (len2 is formed first, then each lane is
read and written in turn).

### 4.4 Cross product (internal; old `rp_cross`)

For 4-float inputs a, b: c0 = F32((a1 x b2) - (a2 x b1)); c1 = F32((a2 x b0) - (a0 x b2));
c2 = F32((a0 x b1) - (a1 x b0)); c3 = the same expression as c0 (the look-at never reads it). Written in the order
c0..c3, the inputs of each lane read when it is computed: the output must not overlap an input.

### 4.5 `u3x_rp_look_at(dir, up, out)`: rotation whose z axis points along dir

1. Axes, each a 4-float F32 array, by 4.3 and 4.4:
   Z = UNIT_OR(dir, fallback (0, 1, 0, 0)); U = UNIT_OR(up, fallback (0, 0, 1, 0));
   X = UNIT_OR(CROSS(U, Z), fallback (1, 0, 0, 0)); Y = UNIT_OR(CROSS(Z, X), fallback (0, 1, 0, 0)).
2. Matrix M with rows X, Y, Z: M(r, c) = lane c of row r (r, c = 0..2), nine F32 values.
3. out = (+0.0, +0.0, +0.0, +0.0) first.
4. tr = (M(0,0) + M(1,1)) + M(2,2).
5. When tr > 0: s = sqrt(tr + 1.0); out3 = F32(s x 0.5); f = 0.5 / s;
   out0 = F32((M(1,2) - M(2,1)) x f); out1 = F32((M(2,0) - M(0,2)) x f); out2 = F32((M(0,1) - M(1,0)) x f).
6. Otherwise (tr <= 0 or NaN): i = 1 when M(1,1) > M(0,0), else 0; then i = 2 when M(2,2) > M(i,i). j = the next
   index after i in the cycle 0, 1, 2, 0; k = the next after j.
   s = sqrt((M(i,i) - (M(j,j) + M(k,k))) + 1.0); out_i = F32(s x 0.5); f = 0.5 / s; then a guard against a zero
   root: when s is +0.0 or -0.0, f = s instead of the quotient (a NaN s keeps the NaN quotient). Precisely:
   f = SEL(m, s, f) where m is the negation of |s| and |s| is "-s when s < 0, else s".
   out3 = F32((M(j,k) - M(k,j)) x f); out_j = F32((M(i,j) + M(j,i)) x f); out_k = F32((M(i,k) + M(k,i)) x f).

`dir` and `up` are completely read (step 1) before `out` is written, so `out` may be the same buffer as either.
Lane 3 of `dir` and `up` only reaches lane 3 of Z and U, which nothing reads.

### 4.6 Quaternion product (internal; old `q_mul`)

PROD(p, l) is the Hamilton product p (x) l ("first l, then p"), lanes x y z w:

* o0 = F32( (((p0 x l3) + (p3 x l0)) - (p2 x l1)) + (p1 x l2) )
* o1 = F32( (((p1 x l3) + (p2 x l0)) + (p3 x l1)) - (p0 x l2) )
* o2 = F32( (((p2 x l3) - (p1 x l0)) + (p0 x l1)) + (p3 x l2) )
* o3 = F32( (((p3 x l3) - (p0 x l0)) - (p1 x l1)) - (p2 x l2) )

Each lane is a chain of four products joined left to right in the order shown; the four products themselves are not
rounded. The lanes are written in the order
o0..o3 and each lane's inputs are read when it is computed: the output must not overlap p or l.

### 4.7 Rotation of a vector by a quaternion (internal; old `q_rot`)

ROT(q, v) for the three lanes of v (v + w t + q x t with t = 2 q x v):

* t0 = 2.0 x ((q1 x v2) - (q2 x v1)); t1 = 2.0 x ((q2 x v0) - (q0 x v2)); t2 = 2.0 x ((q0 x v1) - (q1 x v0)).
* r0 = (v0 + (q3 x t0)) + ((q1 x t2) - (q2 x t1));
  r1 = (v1 + (q3 x t1)) + ((q2 x t0) - (q0 x t2));
  r2 = (v2 + (q3 x t2)) + ((q0 x t1) - (q1 x t0)).

t0..t2 are scalar intermediates. The old source wrote r into a 3-float array of its caller (F32 by the rule of 4.0).

### 4.8 `u3x_rp_frame_world(base, frame, out)`: world location = parent after local (PS3 f_932c30)

1. `u3x_frame_parent(base, frame)`.
2. With P, Q, L, R of the frame (3.1) and r = ROT(Q, L), r F32:
   out0 = F32(P0 + r0); out1 = F32(P1 + r1); out2 = F32(P2 + r2); out3 = F32(P3 + L3).
3. out4..out7 = PROD(Q, R).

Written in the order out0..out7. `out` must not overlap the frame. Only `out` and (through step 1) the frame's
parent block are written.

### 4.9 `u3x_rp_frame_local_pos(frame, p)`: local position for a world position (PS3 f_9327ac, PS4 inline 0x4c9100)

The offset from the parent position is rotated by the conjugate of the parent rotation, in the form
"d + 2 (w e + e x q) with e = d x q". With P, Q of the frame, Q = (qx, qy, qz, qw):

* d_i = p_i - P_i for the four lanes (d = (dx, dy, dz, dw)).
* e = d x q: ex = (qz x dy) - (qy x dz); ey = (qx x dz) - (qz x dx); ez = (qy x dx) - (qx x dy).
* g = e x q: gx = (qz x ey) - (qy x ez); gy = (qx x ez) - (qz x ex); gz = (qy x ex) - (qx x ey).
* local x = F32(dx + (((qw x ex) + gx) x 2.0)); local y = F32(dy + (((qw x ey) + gy) x 2.0));
  local z = F32(dz + (((qw x ez) + gz) x 2.0)); local w = F32(dw + (((qw x ez) + gz) x 2.0)).

Lane 3 of the result is "dw plus the z lane's correction": an artefact of the vector form, to keep. The products
are written with the quaternion lane as the left factor. All of p is read (into d) before the frame's local position
is written; d, e, g are intermediates of this one function (not F32). Writes frame +0x40, +0x44, +0x48, +0x4c in
this order. No parent refresh here.

### 4.10 `u3x_rp_frame_local_rot(frame, q)`: local rotation for a world rotation (PS3 f_932688)

c = (0 - Q0, 0 - Q1, 0 - Q2, Q3) (the parent rotation's conjugate; exact values). Frame +0x50..+0x5f = PROD(c, q).
`q` must not overlap the frame's local rotation. No parent refresh here.

### 4.11 `u3x_rp_slerp(q0, q1, t_bits, out)`: spherical blend in the PS3 form (PS3 f_a6e818, 1.19 f_a82dbc)

t = the float with pattern `t_bits`. The result is not normalised.

1. dot = (((q0_0 x q1_0) + (q0_1 x q1_1)) + (q0_2 x q1_2)) + (q0_3 x q1_3).
2. When 0 > dot: every lane of q1 is replaced by (0 - q1_i) and dot by (0 - dot). (Otherwise q1 is used as given.)
3. linear = not (0.999 [0x3f7fbe77] > dot), so also for a NaN. sign = the sign bit of dot's 32-bit pattern (after
   step 2 it can only be set for -0.0 or a NaN). a = dot with the sign bit of its pattern cleared. u = 1.0 - a.
4. Two chains in a, then one combination:
   hi = (((a x -0.0012624911) + 0.0066700899) x a + -0.0170881264) x a + 0.0308918804;
   lo = (((a x -0.0501743034) + 0.0889789909) x a + -0.2145988047) x a + 1.5707962513;
   each "x a + c" is one product followed by one sum; the negative numbers are the constants of 3.3 with the sign
   bit set. a2 = a x a; a4 = a2 x a2; poly = (hi x a4) + lo.
5. root = u when u == 0, otherwise sqrt(u). angle = root x poly; when the sign of step 3 is set:
   angle = 3.14159274 [0x40490fdb] - (root x poly).
6. v = 1.0 - t. s0 = SIN(angle); s1 = SIN(v x angle); s2 = SIN(t x angle).
7. Weights: when linear, w0 = v and w1 = t; otherwise w0 = s1 / s0 and w1 = s2 / s0.
8. out_i = F32((q0_i x w0) + (q1_i x w1)) for i = 0..3 (q1 after step 2).

SIN(A), sine by quadrant reduction (A and the result are intermediates, not F32):

1. y = A x 0.636619747 [0x3f22f983]; h = 0.5 carrying the sign bit of y's pattern; y = y + h.
2. n (int32) = 0x7fffffff when y >= 2147483520.0 [0x4effffff]; else -2147483648 when y <= -2147483648.0; else y
   truncated toward zero. (A NaN passes both tests and converts to -2147483648 on x86.)
3. nf = n as a float. x = A - (nf x 1.57079625 [0x3fc90fda]); then x = x - (nf x 7.54979e-08 [0x33a22169]).
   x2 = x x x; x3 = x2 x x.
4. cosine = (((x2 x -0.0013602249) + 0.0416566953) x x2 + -0.4999990165) x x2 + 1.0;
   sine = (((x2 x -0.0001950727) + 0.0083320756) x x2 + -0.1666665226) x x3 + x.
5. Result = sine when n is even, cosine when n is odd; when bit 1 of n is set, the sign bit of the result's pattern
   is flipped.

q1 is copied (step 2) before `out` is written, and each output lane reads q0's lane before writing: `out` may be
the same buffer as q0 or as q1.

### 4.12 Power (internal; old `rp_powf`; PS3 f_d41d48, 1.19 f_d562e8)

POW(x, y) for a float x from memory and the constant y = 2.2 [0x400ccccd]:

* x == 0 (either zero): +0.0.
* x < 0: the quotient z / z with z = x - x, a NaN (pattern 0xffc00000 for a finite x).
* otherwise (x > 0 or NaN), with the x87 unit's own instructions, whose results define the bits: p = y x log2(x)
  (FYL2X); n = p rounded to an integer in the current rounding mode, nearest even (FRNDINT); f = p - n;
  result = ((2^f - 1) (F2XM1) + 1.0), scaled by 2^n (FSCALE).
  x = +infinity gives a NaN (f becomes infinity minus infinity); 1.0 gives exactly 1.0.

The result is an intermediate taken straight from the x87 register (like the result of `x87_const`), not F32.

### 4.13 `u3x_rp_render_vars(base, own)` (PS3 1.19 0x27a00c..0x27a59c, 0x27c520)

RS(off) below is the address `u3x_rp_rs(base) + off`, fetched anew for every call that uses it. BLEND(t, off, value,
frames) is `u3x_ovr_blend(base, t, RS(off), pointer to value, frames, own)`; RELEASE(off) is
`u3x_ovr_release(base, RS(off), own)`. ONE is the pattern 0x3f800000. A computed value is stored as F32 in its own
4-byte variable, which stays valid and unchanged until the function returns (the old code never reuses such a slot
for another value; keep that, so it cannot matter whether the game reads a value again later in the call).

Part 1, colour and fog.

A. When the byte at owner +0x1bf is 0, or the byte at image 0x17d7ad8 is not 0: RELEASE of the eighteen fields, in
this order: 0x898, 0x240, 0x244, 0x250, 0x230, 0x234, 0x238, 0x00c, 0x000, 0x004, 0x008, 0x880, 0x884, 0x888,
0xba4, 0x04c, 0x08c, 0x0cc.

B. Otherwise eighteen BLEND calls in this order; every value is read from W, computed and stored directly before
its own call (so the game calls and the reads of W alternate):

1. BLEND(u32 at W +0x1e94, 0x898, 1.0, 200).
2. to 4. BLEND(u32 at W +0x1e98, off, 1.0, 200) for off = 0x240, 0x244, 0x250 (the u32 is read again for each).
5. to 7. for k = 0, 1, 2: c = ((float at W +0x1e9c + 4k) x 0.2 [0x3e4ccccd]) + (float at W +0x1f94 + 4k);
   value = F32(SEL(negated c, +0.0, c)), that is +0.0 when c <= 0 (also for -0.0), c when c > 0 or NaN;
   BLEND(ONE, 0x230 + 4k, value, 0x18737).
8. BLEND(ONE, 0x00c, the float at W +0x1ea8 passed by its address in W, 0x18737).
9. to 11. for k = 0, 1, 2: c = ((float at W +0x1eac + 4k) x 0.4 [0x3ecccccd]) + (float at W +0x1fa0 + 4k); value
   as in 5; BLEND(ONE, 0x000 + 4k, value, 0x18737).
12. to 14. BLEND(ONE, off, address of the float at W + src, 0x18737) for (off, src) = (0x880, 0x1eb8),
   (0x884, 0x1ebc), (0x888, 0x1ec0).
15. to 18. g = POW(float at W +0x1ec4, 2.2), computed once, after call 14. For k = 0..3:
   value = F32((float at W +0x1ec8 + 4k) x g); BLEND(ONE, off_k, value, 0x18737) with off_k = 0xba4, 0x04c, 0x08c,
   0x0cc. g is not rounded to 32 bits between its computation and the four products.

Part 2, depth of field; always done after part 1 (after A as well as after B).

C. When the i32 at W +0x1fb8 is 0: RELEASE of 0x900, 0x904, 0x908, 0x90c, 0x8f0, 0x8f4, 0x8f8, 0x8fc in this order;
done.

D. Otherwise, with F1 = float at W +0x1fbc and F2 = float at W +0x1fc0 (F2 is read first), all of this one formula
without intermediate rounding:

* A1 = ((F1 x k) x (F1 x k)) x 100.0 and A2 = ((F2 x k) x (F2 x k)) x 100.0, with k = 0.0333333351 [0x3d088889].
* sum = A1 + A2; diff = A1 - A2.
* far_start = SEL(negated sum, +0.0, sum): +0.0 when sum <= 0, else sum (NaN stays).
* near_end = SEL(0.05 [0x3d4ccccd] - diff, 0.05, diff): the larger of diff and 0.05 (NaN stays).
* span = far_start + near_end; span = span + span.
* ramp = (far_start - 20.0) x 0.025 [0x3ccccccd]; ramp = SEL(ramp, ramp, +0.0) (a NaN becomes +0.0, -0.0 stays
  -0.0); ramp = SEL(ramp - 1.0, 1.0, ramp) (at most 1).
* rest = 1.0 - ramp.
* width = SEL(1.0 - span, 1.0, span): the larger of span and 1 (NaN stays).
* far_end = far_start + width; amount = (ramp x 0.4 [0x3ecccccd]) + rest.

Then, all four results stored as F32 first, eight calls BLEND(ONE, off, value, 0x18737) in this order:
0x900 <- 0.0 (pattern 0); 0x904 <- near_end; 0x908 <- far_start; 0x90c <- far_end; 0x8f0 <- 1.0; 0x8f4 <- 0.0;
0x8f8 <- 0.0; 0x8fc <- amount.

Nothing else is written; W and the owner are only read. The function has no result.

## 5. Test scenarios

Values in brackets are 32-bit patterns. "exact" results hold on every build; the others are approximate values (the
last bits come from the reference).

Axes (4.1, 4.2):
1. q = (0, 0, 0, 1): x axis (1, 0, 0, 0); z axis (0, 0, 1, 0). exact.
2. q = (0, 1, 0, 0): x axis (-1, 0, 0, 0); z axis (0, 0, -1, 0). exact.
3. q = (0.5, 0.5, 0.5, 0.5): x axis (0, 1, 0, 1) (lane 3 repeats lane 1); z axis (1, 0, 0, 0). exact.
4. Not a unit quaternion, q = (1, 2, 3, 4): x axis (-25, 28, -10, 28); z axis (22, 4, -9, 4). exact.
5. In place (out = q) gives the same as with a separate buffer.

Unit vector (4.3):
6. v = (2, 0, 0, 6) -> (1, 0, 0, 3); v = (0, 0, 4, 8) -> (0, 0, 1, 2). exact (lane 3 scaled, not counted).
7. v = (0, 0, 0, 5), fallback (0, 1, 0, 0) -> (0, 1, 0, 0).
8. Limit: v = (2^-47 [0x28000000], 0, 0, 0) -> (1, 0, 0, 0) (squared length 5e-29 > 1e-30);
   v = (2^-52 [0x25800000], 0, 0, 0) -> the fallback.
9. v with a NaN in lane 0, 1 or 2 -> the fallback. A NaN only in lane 3 -> lanes 0..2 normalised, lane 3 NaN.
10. v = (2^70 [0x62800000], 0, 0, 0): in the helper (x87) -> (1, 0, 0, 0); on a build that rounds every operation
    to 32 bits the squared length is infinite and the result is (0, 0, 0, 0). This is the case that shows whether
    the arithmetic is x87.

Cross product (4.4): 11. a = (1, 2, 3, 9), b = (4, 5, 6, 9) -> (-3, 6, -3, -3). exact.

Look-at (4.5):
12. dir (0, 0, 1, 0), up (0, 1, 0, 0) -> (0, 0, 0, 1) (trace 3; positive-trace branch). exact.
13. dir (0, 0, -1, 0), up (0, 1, 0, 0) -> (0, 1, 0, 0) (trace -1, largest diagonal i = 1). exact, all zeros +0.0.
14. dir (0, 0, -1, 0), up (0, -1, 0, 0) -> (1, 0, 0, 0) (i = 0). exact.
15. dir (0, 0, 1, 0), up (0, -1, 0, 0) -> (0, 0, 1, 0) (i = 2). exact.
16. dir and up both (0, 0, 0, 0): every fallback is used (Z = (0,1,0), U = (0,0,1), X = (-1,0,0), Y = (0,-0,1));
    result (0, 0.70710677, 0.70710677, 0) [0, 0x3f3504f3, 0x3f3504f3, 0].
17. dir parallel to up, dir = up = (0, 1, 0, 0): the first cross product is zero, X falls back to (1, 0, 0, 0);
    result (-0.70710677, 0, 0, 0.70710677) [0xbf3504f3, 0, 0, 0x3f3504f3] (positive-trace branch with trace 1).
18. A general direction, e.g. dir (1, 2, 3, 0), up (0, 1, 0, 0), and one with a NaN lane (result NaN lanes; the
    non-positive-trace branch with i = 0 is taken because every comparison is false).
19. out = dir (same buffer) gives the same as with a separate buffer.

Blend (4.11), q0 = (0, 0, 0, 1):
20. q1 = (1, 0, 0, 0), t = 0 -> q0 exactly; t = 1 -> q1 exactly (the two sine quotients are exactly 1 and 0).
21. q1 = (1, 0, 0, 0), t = 0.5 -> about (0.7071067, 0, 0, 0.7071067) (not normalised; sine form).
22. q1 = q0, t = 0.25 -> (0, 0, 0, 1) exact (linear form: dot = 1 >= 0.999).
23. q1 = (0, 0, 0, -1), t = 0.5 -> (0, 0, 0, 1) exact (negative dot: q1 is negated first).
24. A dot product just below and just above 0.999; a dot product of exactly -0.0 (q0 = (1, 1, 1, 1),
    q1 = (-0.0, -0.0, -0.0, -0.0): no negation, sign bit set, "pi minus" form); a NaN lane (linear form, NaN result
    lanes).
25. SIN alone, if exported: A = 0 -> +0.0; A = 1.57079625 [0x3fc90fda] (n = 1, cosine chain) -> about 1;
    A = 3.14159274 [0x40490fdb] (n = 2, sine chain, sign flipped) -> about -8.74e-08; A = -1.57079625 (n = -1: odd
    and bit 1 set) -> about -1; A = 1e10 and -1e10 (the two saturation limits: n = 0x7fffffff and -2147483648);
    A = NaN -> NaN.

Frames (4.8 to 4.10), no rigid body bound (handle +8 = 0), so step 1 changes nothing:
26. P = (0,0,0,0), Q = (0,0,0,1), L = (1,2,3,7), R = (0,0,0,1) -> world (1, 2, 3, 7; 0, 0, 0, 1). exact.
27. P = (10,20,30,1), Q = (0,1,0,0), L = (1,2,3,7), R = (0,0,0,1) -> world (9, 22, 27, 8; 0, 1, 0, 0). exact
    (lane 3 of the position is P3 + L3).
28. The same frame with a valid rigid-body binding whose body has another position: the world location uses the
    body's position and rotation, and the frame's +0x20..+0x3f hold them afterwards.
29. Local position: P = (10,20,30,1), Q = (0,1,0,0), p = (9, 22, 27, 5) -> local (1, 2, 3, 10). exact (lane 3 = 4 +
    the z lane's correction 6). With Q = (0,0,0,1): local = p - P in all four lanes.
30. Local rotation: Q = (0,1,0,0), q = (0,1,0,0) -> local rotation (0, 0, 0, 1). exact. Q = (0,0,0,1) -> local = q.
31. Round trip with random unit Q, random P, L, R: world, then local position and local rotation from that world
    location, return L and R up to rounding.
32. Random cases for all functions, including large and tiny magnitudes, zeros of both signs, infinities and NaNs.

Render variables (4.13); record the sequence of `u3x_ovr_blend` / `u3x_ovr_release` calls with their arguments and
the 4 bytes behind each value pointer at the time of the call:
33. Owner +0x1bf = 0 -> the eighteen releases in order, then part 2.
34. Owner +0x1bf = 1 and image 0x17d7ad8 = 1 -> the same eighteen releases.
35. Owner +0x1bf = 1, 0x17d7ad8 = 0: eighteen blends. First triple W = (1, -1, 0) with additions (0.5, 0, 0) ->
    values [0x3f333333], +0.0, +0.0. Second triple with the same W values and additions -> [0x3f666666] (an exact
    tie, rounded to even), +0.0, +0.0. Calls 1 to 4 pass the two u32 of W as t and
    200; calls 8 and 12 to 14 pass addresses inside W. Power: W +0x1ec4 = 1.0 -> the four values are the floats at
    W +0x1ec8.. unchanged; = 0.0 -> the four values are zeros with the sign of the W float; = -1.0 -> NaN
    [0xffc00000]; = 4.0 -> each value about 21.1121 times the W float.
36. Depth of field off (W +0x1fb8 = 0) -> eight releases in order, after either part 1.
37. Depth of field on, F1 = F2 = 0: values 0.0, 0.05 [0x3d4ccccd], +0.0, 1.0, 1.0, 0.0, 0.0, 1.0. exact.
38. On, F1 = 60, F2 = 30: far_start about 500, near_end about 300, far_end about 2100, amount 0.4 [0x3ecccccd]
    (ramp clamped to 1).
39. On, F1 = F2 = 13.4164: near_end 0.05, far_start about 40, ramp about 0.5, amount about 0.7, far_end about 120.1.
40. On, F1 = 0, F2 = 30 (diff negative, near_end = 0.05); F1 a NaN (NaN far_start, near_end, far_end; ramp +0.0,
    amount 1.0).
41. The render-settings pointer (image 0x16e2fb8) changed between two runs: all field addresses follow it.

## 6. Bugs, dead code and oddities to keep

1. 4.1 / 4.2: lane 3 of an axis is a copy of lane 1, not zero and not the quaternion's w.
2. 4.3: lane 3 is scaled with the vector although it does not count for the length; the limit test is on the
   squared length; "greater than", so exactly 1e-30 falls back.
3. 4.4: lane 3 of the cross product repeats lane 0.
4. 4.5: the direction handed in by the death look is already a unit vector and is normalised again. The guard
   "f = s when s is zero" cannot trigger for a matrix without NaN (the value under the root is at least 1 in that
   branch); keep it. A NaN anywhere takes the non-positive-trace branch with i = 0.
5. 4.8: the position's lane 3 is P3 + L3 (no rotation); the parent is refreshed on every call, also twice in a row.
6. 4.9: lane 3 of the local position receives the z lane's correction; the old code also computed a fourth lane of
   e and of g and one lane-wise product more that nothing reads (dead; not described).
7. 4.11: the result is not normalised; the three sines are computed in the linear case too (no effect); "root = u
   when u == 0" equals the plain square root for every input (FSQRT keeps both zeros); the "pi minus" form needs a
   set sign bit after the negation, so only -0.0 or a NaN reach it; a dot above 1 (rounding) gives a NaN angle that
   the linear form then ignores. SIN rounds half away from zero by adding 0.5 with the sign of y and truncating.
8. 4.12: POW(+infinity) is a NaN, POW of either zero is +0.0, the exponent is fixed.
9. 4.13: the two blend times of calls 1 to 4 come from W, all other calls use 1.0; the "frames" value is 200 for
   the first four and 100151 for all others; fog uses one W value for three fields; the two triples clamp at zero
   from below only; depth of field is handled even when part 1 released everything; the debug byte only affects
   part 1; near start and two intensities are constant; -0.0 survives the first clamp of ramp.

## 7. Callers and what stays in C

Wrappers: see the table in section 2 (nine at most; none once the cameras group is implemented and `u3x_rp_update`
calls `u3x_rp_render_vars` directly).

What must stay in C from this group: nothing. The C++ part is compiled with the same `-mno-sse` x87 code generation
as the C part and already does native float arithmetic for the film update, and inline x87 assembly (constant
load, and here square root and the power steps) is possible there exactly as `x87_const` shows.

What stays in C for other reasons (not this group): `rp_fcall` (top-level assembly behind `u3h_rp_fcall`, which
`u3x_ovr_blend` needs); the float one-liners `kf`, `rp_sqrt`, `rp_fsel`, `fbits`, `bitsf`, `rd_f` and the `RpLoc`
type for as long as the camera functions are C.

A consequence to know: in v7.212 most of these functions are inlined into their callers (only `bf_world` exists
as a function of its own in the built helper). Once they are separate `u3x_` functions, every vector that passes
between them goes through 32-bit memory. That is the F32 model of section 4 and the old source's meaning, but it is
not always what the shipped build did (section 8).

## 8. Rounding in the shipped -O2 helper (facts for the tester; read from the v7.212 build's generated code)

clang with `-mno-sse` keeps a `float` value in an x87 register at the unit's precision. It rounds to 32 bits when it
stores to memory that really exists, and also whenever it parks a float value in a 4-byte stack slot because it has
run out of registers or a call is in the way; it does not round when it forwards a value it has just stored, and it
parks results of the inline x87 pieces (constants, square root, the power) as 80-bit values. So the same source gives
different last bits depending on what it is inlined into.

* `bf_world` (the one function of this group that the build keeps separate; all eight call sites use it). The
  rotation of 4.7 is inline. Rounded to 32 bits: t0, t1, the sum (v0 + q3 x t0), and r0. NOT rounded: t2, r1, r2 and
  the sum (v1 + q3 x t1), (v2 + q3 x t2); so out1 = F32(P1 + r1) and out2 = F32(P2 + r2) with unrounded r1, r2,
  while out0 = F32(P0 + F32(r0)). The quaternion lanes have no intermediate rounding.
* `rp_render_vars`: as section 4.13 states. The power is carried at full register precision into the four products
  (the compiler parks it as an 80-bit value across the four calls); the depth-of-field chain has no intermediate
  rounding; every value is rounded once, at its store.
* `rp_norm_or`, `rp_lookat`, `rp_slerp` exist only inline in `rp_death_look`, specialised for its constant up vector
  (0, 1, 0) and fallbacks. Values are rounded there only where the compiler parked them. Examples: lane 0 of the
  difference vector is used unrounded for the length and rounded for the scaling; the first unit vector is not
  rounded at all; of the second one (Z of 4.5) the x and z lanes are rounded and the y lane is first used unrounded
  and later rounded; the matrix is in memory (its elements are rounded where the non-positive-trace branch reads them
  back, while the trace is a mix of parked and register values); inside the blend |dot| is taken with FABS from the
  unrounded dot product, the sign bits are tested on stored 32-bit patterns, negations are FCHS on unrounded
  values. There is no single rule to reproduce.
* `bf_local_pos` and `bf_local_rot` exist only inline, in `rp_follow_cam` (set-locator) and `rp_cam_target`
  (set-position), with different parked values in each place; the exact conjugate lanes are unaffected, several
  partial sums are rounded.
* `rp_xaxis` / `rp_zaxis` exist only inline (Follow setter, camera-target step); their lanes are not rounded before
  the caller uses them (`film_cameras.md` section 8).

## 9. Open questions (for the pipeline owner; the implementer should not guess)

1. Which reference is binding for the last bits when old and new are compared with x87 code generation? The F32
   model of section 4 is the meaning of the old source and is what separate functions produce; the shipped build
   differs from it per call site (section 8), and a reference copy of the old functions compiled inside the test
   harness can differ from both (its inlining decides). With SSE float arithmetic in the harness all three coincide.
   Proposal: compare under SSE semantics for the operation sequences, and under x87 with the old sub-functions kept
   out of line (so that their arrays are real memory); accept the section 8 differences from the shipped build as
   the known cost of un-inlining. For `bf_world` alone the shipped rounding could be reproduced exactly (first
   bullet of section 8) if that is wanted: say so, and it becomes a Clarification.
2. `u3x_rp_slerp` is specified both here (4.11) and in `film_cameras.md` (4.6). The texts agree; it should be
   implemented once. This spec assumes: here.
3. The polynomial coefficients of 4.11 are given as literal patterns because the PS4 image does not hold them (three
   of them were searched for in the base image and are absent). They are published numbers (section 3.3). If the
   rule "no constants copied from the game" is read to cover them, the alternative is another sine / arc cosine,
   which cannot be bit-identical.
4. 4.13: nothing shows whether the game's override call copies the 4-byte value during the call or keeps the
   pointer. The spec keeps the old code's guarantee (each computed value in its own slot until the function
   returns); values passed by an address inside W stay valid anyway.

## 10. Clarifications (tester, 2026-11-14)

Facts confirmed by the old-versus-new test `reimpl2\film_block\test_film.cpp` (old = the v7.212 functions, new =
`reimpl2\film_maths\part.cpp`).

1. Arithmetic. The "Float rule" of `reimpl2\PIPELINE.md` replaces the first point of 4.0 and every "F32" / "not
   rounded" mark: each elementary operation of section 4 is one single-precision operation (one rounding per
   operation, on 32-bit patterns), in the order and grouping given there; the square root is the single-precision
   one; SEL, the comparisons and the steps on patterns are as written. With this arithmetic the sequences of section
   4 give, bit for bit, what the old functions give when they are compiled with single-precision arithmetic. This
   holds for every function of section 2 and for the internal pieces 4.4, 4.6, 4.7, SIN and POW.
2. Scenario 10 with this arithmetic: v = (2^70, 0, 0, 0) gives (0, 0, 0, 0): the squared length is infinite and the
   factor 1 / root is zero. The limit of 4.3 is unchanged: a squared length of exactly 0x0da24260 falls back, the
   next float above it is normalised.
3. Look-at and NaN (scenarios 9 and 18 and section 6 item 4, corrected). A NaN in lane 0, 1 or 2 of `dir` or `up`
   never reaches the matrix, because the unit vector of 4.3 falls back (Z = (0, 1, 0), U = (0, 0, 1)). dir with such
   a NaN and up = (0, 1, 0, 0) gives scenario 17's result, without any NaN. A NaN in lane 3 of dir or up has no
   effect. NaN lanes in the result come from an infinite lane (infinity times the zero factor is a NaN):
   dir = (infinity, 0, 1, 0), up = (0, 1, 0, 0) gives [0, 0xffc00000, 0, 0x3f5db3d7].
4. 4.5 step 6, the guard for a zero root: confirmed unreachable. The value under the root is
   1 + 2 M(i,i) - trace, with trace <= 0 and M(i,i) the largest diagonal element, so it is at least 1 for finite
   elements; and the elements are finite or NaN (each row is a unit vector, a fallback, or has NaN lanes). No input
   tells an implementation with the guard from one without.
5. 4.12 and 4.13 steps 15 to 18: by the Float rule the power is rounded to 32 bits before the four products (the old
   code compiled with single-precision arithmetic does the same). The x87 instruction sequence of 4.12 is used as
   given; old and new agree bit for bit with the x87 unit set to 64-bit, 53-bit and 24-bit precision (control words
   0x037f, 0x027f, 0x007f). At run time a negative base gives the NaN 0xffc00000 (-infinity too).
6. 4.13, "each computed value in its own slot until the function returns": the old code does not keep this once it
   is compiled. Built for the host, it reuses the stack slots of part 1's values for part 2's values whenever parts
   B and D both run (the first block's variables are out of scope by then). The new code keeps all 22 values apart
   until it returns, and the test requires that. Whether the game copies a value during the override call could not
   be tested here (open question 4 stays open).
7. NaN bits. When an input is a NaN, a NaN result can carry another payload than the old code's; it is a NaN on both
   sides. Which of two NaN operands comes through a sum or product depends on the operand order the compiler chose
   for the old code (the number of such cases differs between the old code's own -O1 and -O2 builds), so it cannot
   be specified. The new code keeps the operand order of the formulas in section 4. Without a NaN among the inputs
   there is no difference at all (infinities, very large and very small numbers and zeros of both signs included).
8. Internal pieces as delivered: the cross product, the quaternion product, the rotation, the sine and the power are
   the functions `cross`, `prod`, `rot`, `sine`, `power` of the namespace `film_maths` (no exports).
   `u3x_rp_slerp` is implemented here, once (open question 2); it does not compute the three sines in the linear
   case (no observable effect).
9. Against the old code built as the helper builds it (clang -O2 -mno-sse, for the host; information only). With
   the x87 unit at 24-bit precision the old x87 build and the new code agree in every case tried. At 53-bit or
   64-bit precision some result lane differs in half to nine tenths of the cases (by function): mostly by 1 or 2
   units in the last place; by more only where a result is a small difference of larger terms, and then by a few units in the last
   place of those terms (largest absolute differences seen: 1.2e-7 in a lane of an axis or of a rotation product,
   6.1e-5 in a world position of some hundreds, 1.2e-4 in a local position of about a thousand, 0.006 in a
   depth-of-field value of 32700; unit vector at most 3, sine at most 1, power 0 units in the last place). Two
   places are not last-bit matters. (a) The blend at its limit 0.999: the linear and the sine form differ by up to
   about 1.7e-4 per lane there (the formula itself jumps at the limit), and a dot product within one unit in the
   last place of 0.999 can take the other form in the x87 build. (b) The look-at with `dir` exactly parallel to
   `up` but not along an axis: single precision gives an exactly zero cross product and the fallback X = (1, 0, 0);
   the -O2 x87 build mixes rounded and unrounded copies of the two unit vectors, gets a cross product of rounding
   noise and normalises that (another roll about the view axis). The death look passes up = (0, 1, 0), for which
   both give zero, so this cannot happen in the film player.
