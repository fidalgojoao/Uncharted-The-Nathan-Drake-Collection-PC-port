// film_maths: the film player's vector, quaternion and bound-frame maths and the film's render settings.
// Written from spec/film_maths.md. Every float is handled as its 32-bit pattern and every operation is one
// single-precision step (the f_* tools), in the order and grouping the spec gives. Operand order is kept as written
// too (it decides which NaN comes through). The one exception is the power of the render settings (x87 steps).

extern "C" {
void u3x_frame_parent(uintptr_t base, uint8_t *frame);   // refreshes frame +0x20..+0x3f from the bound body
uint8_t *u3x_rp_rs(uintptr_t base);                      // the render-settings structure
void u3x_ovr_blend(uintptr_t base, uint32_t t, uint8_t *addr, const void *val, int32_t frames, uint8_t *owner);
void u3x_ovr_release(uintptr_t base, uint8_t *addr, uint8_t *owner);
}

namespace film_maths {

constexpr uint32_t ZERO = 0, HALF = 0x3f000000u, TWO = 0x40000000u, SIGN = 0x80000000u;

// fallback axes of the look-at
U3X_RO const uint32_t AXIS_X[4] = {F_ONE, 0, 0, 0};
U3X_RO const uint32_t AXIS_Y[4] = {0, F_ONE, 0, 0};
U3X_RO const uint32_t AXIS_Z[4] = {0, 0, F_ONE, 0};

// render-settings fields of the colour / fog part and of the depth of field, in the order they are set or released
U3X_RO const uint16_t COLOUR_FIELDS[18] = {0x898, 0x240, 0x244, 0x250, 0x230, 0x234, 0x238, 0x00c, 0x000,
                                           0x004, 0x008, 0x880, 0x884, 0x888, 0xba4, 0x04c, 0x08c, 0x0cc};
U3X_RO const uint16_t FOCUS_FIELDS[8] = {0x900, 0x904, 0x908, 0x90c, 0x8f0, 0x8f4, 0x8f8, 0x8fc};

// ---- small tools

[[gnu::always_inline]] inline uint32_t bits(const void *p) {   // the 4 bytes at p
    uint32_t v;
    __builtin_memcpy(&v, p, 4);
    return v;
}
[[gnu::always_inline]] inline uint32_t ld(const void *p, int lane) { return bits(static_cast<const uint8_t *>(p) + 4 * lane); }
[[gnu::always_inline]] inline void st(void *p, int lane, uint32_t v) { __builtin_memcpy(static_cast<uint8_t *>(p) + 4 * lane, &v, 4); }
[[gnu::always_inline]] inline const uint32_t *pat(const void *p) { return static_cast<const uint32_t *>(p); }
[[gnu::always_inline]] inline uint32_t *pat(void *p) { return static_cast<uint32_t *>(p); }

[[gnu::always_inline]] inline bool f_le(uint32_t a, uint32_t b) {   // a <= b as floats (false when either is NaN)
    int r;
    __asm__("movd %1,%%xmm0\n\tmovd %2,%%xmm1\n\txorl %0,%0\n\tucomiss %%xmm0,%%xmm1\n\tsetae %b0" : "=&r"(r) : "r"(a), "r"(b) : "xmm0", "xmm1", "cc");
    return r != 0;
}
[[gnu::always_inline]] inline uint32_t zero_minus(uint32_t x) { return f_sub(ZERO, x); }   // +0.0 - x
[[gnu::always_inline]] inline bool is_zero(uint32_t x) { return !(x & ~SIGN); }             // +0.0 or -0.0
// b when a >= 0, otherwise (also for a NaN a) c
[[gnu::always_inline]] inline uint32_t sel(uint32_t a, uint32_t b, uint32_t c) { return f_le(ZERO, a) ? b : c; }
// ((x * k0) + k1) * x + k2
[[gnu::always_inline]] inline uint32_t chain(uint32_t x, uint32_t k0, uint32_t k1, uint32_t k2) {
    return f_add(f_mul(f_add(f_mul(x, k0), k1), x), k2);
}

// ---- vectors and quaternions (4 patterns each; a quaternion is x, y, z, w)

// v scaled to length 1 (lanes 0..2 count, lane 3 is scaled along), or the fallback when v is too short or not a number
U3X_FN static void unit_or(const uint32_t *v, const uint32_t *fallback, uint32_t *out) {
    const uint32_t v0 = ld(v, 0), v1 = ld(v, 1), v2 = ld(v, 2);
    const uint32_t len2 = f_add(f_add(f_mul(v0, v0), f_mul(v1, v1)), f_mul(v2, v2));
    if (f_less(0x0da24260u, len2)) {   // squared length above 1e-30
        const uint32_t r = f_div(F_ONE, f_root(len2));
        for (int i = 0; i < 4; i++) st(out, i, f_mul(ld(v, i), r));
    } else {
        for (int i = 0; i < 4; i++) st(out, i, ld(fallback, i));
    }
}

// c = a x b; lane 3 repeats lane 0's expression. c must not overlap a or b.
U3X_FN static void cross(const uint32_t *a, const uint32_t *b, uint32_t *c) {
    st(c, 0, f_sub(f_mul(ld(a, 1), ld(b, 2)), f_mul(ld(a, 2), ld(b, 1))));
    st(c, 1, f_sub(f_mul(ld(a, 2), ld(b, 0)), f_mul(ld(a, 0), ld(b, 2))));
    st(c, 2, f_sub(f_mul(ld(a, 0), ld(b, 1)), f_mul(ld(a, 1), ld(b, 0))));
    st(c, 3, f_sub(f_mul(ld(a, 1), ld(b, 2)), f_mul(ld(a, 2), ld(b, 1))));
}

// o = p (x) l, the Hamilton product ("first l, then p"). o must not overlap p or l.
U3X_FN static void prod(const uint32_t *p, const uint32_t *l, uint32_t *o) {
    st(o, 0, f_add(f_sub(f_add(f_mul(ld(p, 0), ld(l, 3)), f_mul(ld(p, 3), ld(l, 0))), f_mul(ld(p, 2), ld(l, 1))), f_mul(ld(p, 1), ld(l, 2))));
    st(o, 1, f_sub(f_add(f_add(f_mul(ld(p, 1), ld(l, 3)), f_mul(ld(p, 2), ld(l, 0))), f_mul(ld(p, 3), ld(l, 1))), f_mul(ld(p, 0), ld(l, 2))));
    st(o, 2, f_add(f_add(f_sub(f_mul(ld(p, 2), ld(l, 3)), f_mul(ld(p, 1), ld(l, 0))), f_mul(ld(p, 0), ld(l, 1))), f_mul(ld(p, 3), ld(l, 2))));
    st(o, 3, f_sub(f_sub(f_sub(f_mul(ld(p, 3), ld(l, 3)), f_mul(ld(p, 0), ld(l, 0))), f_mul(ld(p, 1), ld(l, 1))), f_mul(ld(p, 2), ld(l, 2))));
}

// r (3 lanes) = v turned by q: v + w t + q x t with t = 2 (q x v)
U3X_FN static void rot(const uint32_t *q, const uint32_t *v, uint32_t *r) {
    const uint32_t q0 = ld(q, 0), q1 = ld(q, 1), q2 = ld(q, 2), q3 = ld(q, 3);
    const uint32_t v0 = ld(v, 0), v1 = ld(v, 1), v2 = ld(v, 2);
    const uint32_t t0 = f_mul(TWO, f_sub(f_mul(q1, v2), f_mul(q2, v1)));
    const uint32_t t1 = f_mul(TWO, f_sub(f_mul(q2, v0), f_mul(q0, v2)));
    const uint32_t t2 = f_mul(TWO, f_sub(f_mul(q0, v1), f_mul(q1, v0)));
    st(r, 0, f_add(f_add(v0, f_mul(q3, t0)), f_sub(f_mul(q1, t2), f_mul(q2, t1))));
    st(r, 1, f_add(f_add(v1, f_mul(q3, t1)), f_sub(f_mul(q2, t0), f_mul(q0, t2))));
    st(r, 2, f_add(f_add(v2, f_mul(q3, t2)), f_sub(f_mul(q0, t1), f_mul(q1, t0))));
}

// ---- the blend's sine and arc cosine

// Sine by quadrant reduction. Constants: the published sinf4 of the open-source Sony Vector Math library (2/pi,
// pi/2 in two parts, its cosine and sine polynomials).
U3X_FN static uint32_t sine(uint32_t angle) {
    uint32_t y = f_mul(angle, 0x3f22f983u);   // angle * 2/pi
    y = f_add(y, HALF | (y & SIGN));          // round half away from zero ...
    // ... by truncating; 0x7fffffff from 2147483520.0 up. The conversion itself gives -2147483648 for everything at
    // or below -2147483648.0 and for a NaN.
    const int32_t n = f_le(0x4effffffu, y) ? 0x7fffffff : f_to_int(y);
    const uint32_t nf = f_from_int(n);
    uint32_t x = f_sub(angle, f_mul(nf, 0x3fc90fdau));
    x = f_sub(x, f_mul(nf, 0x33a22169u));
    const uint32_t x2 = f_mul(x, x), x3 = f_mul(x2, x);
    uint32_t r;
    if (n & 1) r = f_add(f_mul(chain(x2, 0x3ab24993u | SIGN, 0x3d2aa036u, 0x3effffdfu | SIGN), x2), F_ONE);   // cosine
    else r = f_add(f_mul(chain(x2, 0x394c8c6eu | SIGN, 0x3c088342u, 0x3e2aaaa1u | SIGN), x3), x);            // sine
    return (n & 2) ? r ^ SIGN : r;
}

// The angle whose cosine is |dot|, or pi minus it when dot's sign bit is set. Constants: Abramowitz and Stegun,
// Handbook of Mathematical Functions, 4.4.46 (arc cosine = root(1 - a) times a polynomial of degree 7).
[[gnu::always_inline]] inline uint32_t arc_cosine(uint32_t dot) {
    const uint32_t a = dot & ~SIGN;
    const uint32_t u = f_sub(F_ONE, a);
    const uint32_t hi = f_add(f_mul(chain(a, 0x3aa57a2cu | SIGN, 0x3bda90c5u, 0x3c8bfc66u | SIGN), a), 0x3cfd10f8u);
    const uint32_t lo = f_add(f_mul(chain(a, 0x3d4d8392u | SIGN, 0x3db63a9eu, 0x3e5bbfcau | SIGN), a), 0x3fc90fdau);
    const uint32_t a2 = f_mul(a, a), a4 = f_mul(a2, a2);
    const uint32_t poly = f_add(f_mul(hi, a4), lo);
    const uint32_t angle = f_mul(f_root(u), poly);   // the root keeps a zero of either sign as it is
    return (dot & SIGN) ? f_sub(0x40490fdbu, angle) : angle;
}

// ---- the power of the render settings

// x to the power 2.2: +0.0 for a zero, a NaN for a negative x, otherwise by the x87 unit's own steps, rounded to
// 32 bits at the end.
U3X_FN static uint32_t power(uint32_t x) {
    if (is_zero(x)) return ZERO;
    if (f_less(x, ZERO)) {
        const uint32_t z = f_sub(x, x);
        return f_div(z, z);
    }
    const uint32_t y = 0x400ccccdu;   // 2.2
    uint32_t r;
    __asm__("flds %[y]\n\t"
            "flds %[x]\n\t"
            "fyl2x\n\t"                        // p = y * log2(x)
            "fld %%st(0)\n\t"
            "frndint\n\t"                      // n = p rounded to an integer
            "fxch %%st(1)\n\t"
            "fsub %%st(1),%%st(0)\n\t"         // f = p - n
            "f2xm1\n\t"
            "fld1\n\t"
            "faddp %%st(0),%%st(1)\n\t"        // (2^f - 1) + 1
            "fscale\n\t"                       // times 2^n
            "fstp %%st(1)\n\t"
            "fstps %[r]"
            : [r] "=m"(r) : [x] "m"(x), [y] "m"(y) : "st", "st(1)", "st(2)");
    return r;
}

// ---- render-settings overrides

// One 4-byte variable per value handed to the game; none is touched again before the function returns.
struct Slots {
    uint32_t v[22];
    int used;
    [[gnu::always_inline]] const uint32_t *keep(uint32_t value) {
        v[used] = value;
        return &v[used++];
    }
};

[[gnu::always_inline]] inline void blend(uintptr_t base, uint8_t *own, uint32_t t, uint32_t field, const void *value, int32_t frames) {
    u3x_ovr_blend(base, t, u3x_rp_rs(base) + field, value, frames, own);
}
[[gnu::always_inline]] inline void release(uintptr_t base, uint8_t *own, const uint16_t *fields, int count) {
    for (int i = 0; i < count; i++) u3x_ovr_release(base, u3x_rp_rs(base) + fields[i], own);
}
// +0.0 when c <= 0 (also for -0.0), otherwise c (a NaN stays)
[[gnu::always_inline]] inline uint32_t not_below_zero(uint32_t c) { return sel(c ^ SIGN, ZERO, c); }
// ((focus / 30)^2) * 100
[[gnu::always_inline]] inline uint32_t focus_term(uint32_t focus) {
    const uint32_t e = f_mul(focus, 0x3d088889u);
    return f_mul(f_mul(e, e), 0x42c80000u);
}

// ---- the functions behind the exports

// The x axis of q's rotation: (1 - 2(yy + zz), 2(xy + wz), 2(xz - wy), lane 3 = lane 1). out may be q.
[[gnu::always_inline]] inline void axis_x(const float *q, float *out) {
    const uint32_t x = ld(q, 0), y = ld(q, 1), z = ld(q, 2), w = ld(q, 3);
    const uint32_t s = zero_minus(f_add(f_mul(y, y), f_mul(z, z)));
    const uint32_t m = f_sub(f_mul(x, z), f_mul(w, y));
    const uint32_t n = f_add(f_mul(x, y), f_mul(w, z));
    st(out, 0, f_add(f_mul(s, TWO), F_ONE));
    st(out, 1, f_mul(n, TWO));
    st(out, 2, f_mul(m, TWO));
    st(out, 3, f_mul(n, TWO));
}

// The z axis of q's rotation: (2(xz + wy), 2(yz - wx), 1 - 2(xx + yy), lane 3 = lane 1). out may be q.
[[gnu::always_inline]] inline void axis_z(const float *q, float *out) {
    const uint32_t x = ld(q, 0), y = ld(q, 1), z = ld(q, 2), w = ld(q, 3);
    const uint32_t m = f_add(f_mul(x, z), f_mul(w, y));
    const uint32_t n = f_sub(f_mul(y, z), f_mul(w, x));
    const uint32_t s = zero_minus(f_add(f_mul(x, x), f_mul(y, y)));
    st(out, 0, f_mul(m, TWO));
    st(out, 1, f_mul(n, TWO));
    st(out, 2, f_add(f_mul(s, TWO), F_ONE));
    st(out, 3, f_mul(n, TWO));
}

// The rotation whose z axis points along dir, with up as the upward hint. out may be dir or up.
[[gnu::always_inline]] inline void look_at(const float *dir, const float *up, float *out) {
    uint32_t m[3][4], u[4], c[4];   // m: the rows X, Y, Z of the rotation matrix
    unit_or(pat(dir), AXIS_Y, m[2]);
    unit_or(pat(up), AXIS_Z, u);
    cross(u, m[2], c);
    unit_or(c, AXIS_X, m[0]);
    cross(m[2], m[0], c);
    unit_or(c, AXIS_Y, m[1]);

    for (int i = 0; i < 4; i++) st(out, i, ZERO);
    const uint32_t trace = f_add(f_add(m[0][0], m[1][1]), m[2][2]);
    if (f_less(ZERO, trace)) {
        const uint32_t s = f_root(f_add(trace, F_ONE));
        st(out, 3, f_mul(s, HALF));
        const uint32_t f = f_div(HALF, s);
        st(out, 0, f_mul(f_sub(m[1][2], m[2][1]), f));
        st(out, 1, f_mul(f_sub(m[2][0], m[0][2]), f));
        st(out, 2, f_mul(f_sub(m[0][1], m[1][0]), f));
    } else {
        // from the largest diagonal element i; j and k follow it in the cycle 0, 1, 2, 0
        int i = f_less(m[0][0], m[1][1]) ? 1 : 0;
        if (f_less(m[i][i], m[2][2])) i = 2;
        const int j = i == 2 ? 0 : i + 1, k = j == 2 ? 0 : j + 1;
        const uint32_t s = f_root(f_add(f_sub(m[i][i], f_add(m[j][j], m[k][k])), F_ONE));
        st(out, i, f_mul(s, HALF));
        const uint32_t f = is_zero(s) ? s : f_div(HALF, s);   // a zero root is used itself instead of 0.5 / root
        st(out, 3, f_mul(f_sub(m[j][k], m[k][j]), f));
        st(out, j, f_mul(f_add(m[i][j], m[j][i]), f));
        st(out, k, f_mul(f_add(m[i][k], m[k][i]), f));
    }
}

// Spherical blend from q0 (t = 0) to q1 (t = 1), the shorter way; not normalised. out may be q0 or q1.
[[gnu::always_inline]] inline void slerp(const float *q0, const float *q1, uint32_t t_bits, float *out) {
    uint32_t to[4];
    for (int i = 0; i < 4; i++) to[i] = ld(q1, i);
    uint32_t dot = f_mul(ld(q0, 0), to[0]);
    for (int i = 1; i < 4; i++) dot = f_add(dot, f_mul(ld(q0, i), to[i]));
    if (f_less(dot, ZERO)) {
        for (int i = 0; i < 4; i++) to[i] = zero_minus(to[i]);
        dot = zero_minus(dot);
    }
    const uint32_t v = f_sub(F_ONE, t_bits);
    uint32_t w0 = v, w1 = t_bits;         // linear weights: for a dot product of 0.999 and above, and for a NaN
    if (f_less(dot, 0x3f7fbe77u)) {       // (the sines would not be used there)
        const uint32_t angle = arc_cosine(dot);
        const uint32_t s0 = sine(angle);
        w0 = f_div(sine(f_mul(v, angle)), s0);
        w1 = f_div(sine(f_mul(t_bits, angle)), s0);
    }
    for (int i = 0; i < 4; i++) st(out, i, f_add(f_mul(ld(q0, i), w0), f_mul(to[i], w1)));
}

// World location (position, rotation: 8 floats) of a bound frame: the parent's location applied after the local one.
// The position's lane 3 is the plain sum of the two lanes 3.
[[gnu::always_inline]] inline void frame_world(uintptr_t base, uint8_t *frame, float *out) {
    u3x_frame_parent(base, frame);
    const uint32_t *parent_pos = pat(frame + 0x20), *parent_rot = pat(frame + 0x30);
    const uint32_t *local_pos = pat(frame + 0x40), *local_rot = pat(frame + 0x50);
    uint32_t r[3];
    rot(parent_rot, local_pos, r);
    for (int i = 0; i < 3; i++) st(out, i, f_add(ld(parent_pos, i), r[i]));
    st(out, 3, f_add(ld(parent_pos, 3), ld(local_pos, 3)));
    prod(parent_rot, local_rot, pat(out) + 4);
}

// Sets the frame's local position so that its world position is p: the offset from the parent, turned back by the
// parent's rotation. Lane 3 gets its own offset plus the z lane's correction.
[[gnu::always_inline]] inline void frame_local_pos(uint8_t *frame, const float *p) {
    uint32_t d[4];
    for (int i = 0; i < 4; i++) d[i] = f_sub(ld(p, i), ld(frame + 0x20, i));
    const uint32_t qx = ld(frame + 0x30, 0), qy = ld(frame + 0x30, 1), qz = ld(frame + 0x30, 2), qw = ld(frame + 0x30, 3);
    const uint32_t ex = f_sub(f_mul(qz, d[1]), f_mul(qy, d[2]));
    const uint32_t ey = f_sub(f_mul(qx, d[2]), f_mul(qz, d[0]));
    const uint32_t ez = f_sub(f_mul(qy, d[0]), f_mul(qx, d[1]));
    const uint32_t gx = f_sub(f_mul(qz, ey), f_mul(qy, ez));
    const uint32_t gy = f_sub(f_mul(qx, ez), f_mul(qz, ex));
    const uint32_t gz = f_sub(f_mul(qy, ex), f_mul(qx, ey));
    const uint32_t cx = f_mul(f_add(f_mul(qw, ex), gx), TWO);
    const uint32_t cy = f_mul(f_add(f_mul(qw, ey), gy), TWO);
    const uint32_t cz = f_mul(f_add(f_mul(qw, ez), gz), TWO);
    st(frame + 0x40, 0, f_add(d[0], cx));
    st(frame + 0x40, 1, f_add(d[1], cy));
    st(frame + 0x40, 2, f_add(d[2], cz));
    st(frame + 0x40, 3, f_add(d[3], cz));
}

// Sets the frame's local rotation so that its world rotation is q: the parent rotation's conjugate times q.
[[gnu::always_inline]] inline void frame_local_rot(uint8_t *frame, const float *q) {
    uint32_t c[4];
    for (int i = 0; i < 3; i++) c[i] = zero_minus(ld(frame + 0x30, i));
    c[3] = ld(frame + 0x30, 3);
    prod(c, pat(q), pat(frame + 0x50));
}

// The film's colour, fog, tint and depth-of-field values as overrides of the game's render settings for the owner
// `own`, or the release of those overrides.
[[gnu::always_inline]] inline void render_vars(uintptr_t base, uint8_t *own) {
    constexpr int32_t SHORT_HOLD = 200, LONG_HOLD = 0x18737;
    const uint8_t *film = reinterpret_cast<const uint8_t *>(base + 0x17c3ba8);   // the film writer
    Slots slots;
    slots.used = 0;

    // part 1: colour and fog; off when the film's settings are off or the debug switch is set
    if (!rd<uint8_t>(own, 0x1bf) || at<uint8_t>(base + 0x17d7ad8)) {
        release(base, own, COLOUR_FIELDS, 18);
    } else {
        const uint16_t *field = COLOUR_FIELDS;
        blend(base, own, bits(film + 0x1e94), *field++, slots.keep(F_ONE), SHORT_HOLD);                           // saturation
        for (int k = 0; k < 3; k++) blend(base, own, bits(film + 0x1e98), *field++, slots.keep(F_ONE), SHORT_HOLD);   // fog
        for (int k = 0; k < 3; k++) {
            const uint32_t c = f_add(f_mul(bits(film + 0x1e9c + 4 * k), 0x3e4ccccdu), bits(film + 0x1f94 + 4 * k));
            blend(base, own, F_ONE, *field++, slots.keep(not_below_zero(c)), LONG_HOLD);
        }
        blend(base, own, F_ONE, *field++, film + 0x1ea8, LONG_HOLD);
        for (int k = 0; k < 3; k++) {
            const uint32_t c = f_add(f_mul(bits(film + 0x1eac + 4 * k), 0x3ecccccdu), bits(film + 0x1fa0 + 4 * k));
            blend(base, own, F_ONE, *field++, slots.keep(not_below_zero(c)), LONG_HOLD);
        }
        for (int k = 0; k < 3; k++) blend(base, own, F_ONE, *field++, film + 0x1eb8 + 4 * k, LONG_HOLD);
        const uint32_t g = power(bits(film + 0x1ec4));
        for (int k = 0; k < 4; k++)
            blend(base, own, F_ONE, *field++, slots.keep(f_mul(bits(film + 0x1ec8 + 4 * k), g)), LONG_HOLD);
    }

    // part 2: depth of field
    if (!rd<int32_t>(film, 0x1fb8)) {
        release(base, own, FOCUS_FIELDS, 8);
        return;
    }
    const uint32_t a2 = focus_term(bits(film + 0x1fc0)), a1 = focus_term(bits(film + 0x1fbc));
    const uint32_t sum = f_add(a1, a2), diff = f_sub(a1, a2);
    const uint32_t far_start = not_below_zero(sum);
    const uint32_t near_end = sel(f_sub(0x3d4ccccdu, diff), 0x3d4ccccdu, diff);   // at least 0.05
    uint32_t span = f_add(far_start, near_end);
    span = f_add(span, span);
    uint32_t ramp = f_mul(f_sub(far_start, 0x41a00000u), 0x3ccccccdu);   // (far start - 20) / 40 ...
    ramp = sel(ramp, ramp, ZERO);                                       // ... below zero or a NaN becomes +0.0
    ramp = sel(f_sub(ramp, F_ONE), F_ONE, ramp);                        // ... at most 1
    const uint32_t rest = f_sub(F_ONE, ramp);
    const uint32_t width = sel(f_sub(F_ONE, span), F_ONE, span);        // at least 1
    const uint32_t far_end = f_add(far_start, width);
    const uint32_t amount = f_add(f_mul(ramp, 0x3ecccccdu), rest);

    const uint32_t *value = slots.keep(ZERO);   // near start
    slots.keep(near_end);
    slots.keep(far_start);
    slots.keep(far_end);
    slots.keep(F_ONE);                          // the four intensities
    slots.keep(ZERO);
    slots.keep(ZERO);
    slots.keep(amount);
    for (int i = 0; i < 8; i++) blend(base, own, F_ONE, FOCUS_FIELDS[i], value + i, LONG_HOLD);
}

}  // namespace film_maths

// ---- exported functions

extern "C" U3X_FN void u3x_rp_axis_x(const float *q, float *out) { film_maths::axis_x(q, out); }
extern "C" U3X_FN void u3x_rp_axis_z(const float *q, float *out) { film_maths::axis_z(q, out); }
extern "C" U3X_FN void u3x_rp_unit_or(const float *v, const float *fallback, float *out) {
    film_maths::unit_or(film_maths::pat(v), film_maths::pat(fallback), film_maths::pat(out));
}
extern "C" U3X_FN void u3x_rp_look_at(const float *dir, const float *up, float *out) { film_maths::look_at(dir, up, out); }
extern "C" U3X_FN void u3x_rp_slerp(const float *q0, const float *q1, uint32_t t_bits, float *out) {
    film_maths::slerp(q0, q1, t_bits, out);
}
extern "C" U3X_FN void u3x_rp_frame_world(uintptr_t base, uint8_t *frame, float *out) { film_maths::frame_world(base, frame, out); }
extern "C" U3X_FN void u3x_rp_frame_local_pos(uint8_t *frame, const float *p) { film_maths::frame_local_pos(frame, p); }
extern "C" U3X_FN void u3x_rp_frame_local_rot(uint8_t *frame, const float *q) { film_maths::frame_local_rot(frame, q); }
extern "C" U3X_FN void u3x_rp_render_vars(uintptr_t base, uint8_t *own) { film_maths::render_vars(base, own); }
