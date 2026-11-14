// film_cameras: the film player's cameras and the scope HUD (spec: film_cameras.md), written from the spec.
// Every float step is single precision on 32-bit patterns, one rounding per operation, in the spec's order.
// u3x_rp_slerp (film maths group) and u3x_cin_megabytes (cinema rows group) are delivered elsewhere.

extern "C" {
// the existing C++ part (spec 3.2)
uint8_t *u3x_rp_h(const uint8_t *handle);
uint8_t *u3x_rp_trk(uintptr_t base, uint32_t index);
int u3x_rp_dead(const uint8_t *tracker);
uint8_t *u3x_rp_trk_by_id(uintptr_t base, uint32_t net_id);
int u3x_rp_isa(uintptr_t base, const uint8_t *object, uint32_t class_hash);
void u3x_rp_send(uintptr_t base, uint32_t message, uint8_t *target);
void u3x_frame_identity(uint8_t *frame);
void u3x_frame_make(uint8_t *frame, const void *location);
void u3x_frame_parent(uintptr_t base, uint8_t *frame);
uint8_t *u3x_camera_type(uintptr_t base, int fixed);
void u3x_camera_spawn(uintptr_t base, uint8_t *own, uint8_t *type, uint32_t fov_bits, const void *location,
                      const uint8_t *target);
void u3x_camera_cinema_set(uint8_t *cam, const void *location);
void u3x_camera_rerecord(uintptr_t base, uint8_t *own);
void u3x_rp_hud_flag(uintptr_t base, uint32_t flag);
void u3x_rp_hud_fade(uintptr_t base, uint32_t a, uint32_t b, uint32_t c, int i, int byte);
void u3x_rp_hud_weapon(uintptr_t base, uint8_t *weapon);

// the film maths group (film_maths.md, section 2)
void u3x_rp_axis_x(const float *q, float *out);
void u3x_rp_axis_z(const float *q, float *out);
void u3x_rp_unit_or(const float *v, const float *fallback, float *out);
void u3x_rp_look_at(const float *dir, const float *up, float *out);
void u3x_rp_slerp(const float *q0, const float *q1, uint32_t t_bits, float *out);
void u3x_rp_frame_world(uintptr_t base, uint8_t *frame, float *out);
void u3x_rp_frame_local_pos(uint8_t *frame, const float *p);
void u3x_rp_frame_local_rot(uint8_t *frame, const float *q);

// this group
void u3x_rp_cam_target(uintptr_t base, uint8_t *own);
void u3x_rp_cam_mode12(uintptr_t base, uint8_t *st, uint8_t *own);
void u3x_rp_follow_cam(uintptr_t base, uint8_t *own, uint8_t *target, uint8_t *frame, uint32_t fov_bits, int ok);
void u3x_rp_follow_set(uintptr_t base, uint8_t *cam, const uint8_t *target);
void u3x_rp_fixed_set(uintptr_t base, uint8_t *cam, uint32_t fov_bits, const uint8_t *target, const void *location);
void u3x_rp_death_look(uintptr_t base, uint8_t *frame, const uint8_t *tracker);
void u3x_rp_scope_hud(uintptr_t base, uint8_t *player, uint32_t fov_bits);
}

namespace film_cameras {

// float patterns
constexpr uint32_t F_ZERO = 0x00000000u;        // 0.0
constexpr uint32_t F_MINUS_ONE = 0xbf800000u;   // -1.0
constexpr uint32_t F_75 = 0x42960000u;          // default field of view
constexpr uint32_t F_10 = 0x41200000u;          // slider scale
constexpr uint32_t F_QUARTER = 0x3e800000u;     // 0.25
constexpr uint32_t F_6 = 0x40c00000u;           // 6.0
constexpr uint32_t F_HUNDREDTH = 0x3c23d70au;   // 0.01: shortest usable flat x axis
constexpr uint32_t F_TINY = 0x20000000u;        // 2^-63: below it a length counts as zero
constexpr uint32_t F_3 = 0x40400000u;           // 3.0 (metres behind a body; seconds before the death look)
constexpr uint32_t F_TICK = 0x3b5a740eu;        // 1/300 s
constexpr uint32_t F_5 = 0x40a00000u;           // 5.0
constexpr uint32_t F_015 = 0x3e19999au;         // 0.15

// game data (image offsets)
constexpr uintptr_t G_CAMERA_MODE = 0x17c5a10;      // int32: 0 = player's view, 1 = free, other = orbit
constexpr uintptr_t G_FOLLOWED_NAME = 0x17c5a14;    // C string
constexpr uintptr_t G_FOLLOWED_INDEX = 0x17c5a34;   // int32
constexpr uintptr_t G_SLIDER_FOV = 0x17c5b54;       // float
constexpr uintptr_t G_SLIDER_DISTANCE = 0x17c5b58;  // float
constexpr uintptr_t G_FOLLOW_FLAG = 0x17c5b5c;      // int32
constexpr uintptr_t G_CURRENT_TRACKER = 0x17cb3d0;  // pointer
constexpr uintptr_t G_CINEMA_BIT_OFF = 0x17b4a68;   // u64, tested for zero
constexpr uintptr_t G_CLOCK7 = 0x17b4ca4;           // int32, ticks of 1/300 s
constexpr uintptr_t G_LOCATOR = 0x16ea550;          // location of the screen-0 camera
constexpr uintptr_t G_LOCATOR_SECOND = 0x16ea7c0;   // 3 floats
constexpr uintptr_t G_FILM_CLOCK = 0x17c3580;       // pointer; int32 at +0xc = time
constexpr uintptr_t G_CINEMA_TYPE = 0x17b30e8;      // the cinema camera type object

// game functions (image offsets)
constexpr uintptr_t FN_PLAYER_VIEW = 0x685b80;
constexpr uintptr_t FN_WEAPON_SCOPE = 0xd436b0;
constexpr uintptr_t FN_FRAME_UNBIND = 0x1196230;
constexpr uintptr_t FN_FREE_CAMERA = 0x7a3e30;

// ids
constexpr uint32_t CLASS_FOLLOW = 0x54aee225u, CLASS_FIXED = 0xb8ffda36u, CLASS_CINEMA = 0xb1f56f8cu;
constexpr uint32_t CLASS_PLAYER = 0xf2c86c22u;
constexpr uint32_t SCOPE_A = 0x84713f65u, SCOPE_B = 0x63b89c77u;
constexpr uint32_t MSG_ZOOMED = 0xd8a6f55au;

template <typename T> [[gnu::always_inline]] inline void wr(uint8_t *p, uintptr_t off, T v) {
    *reinterpret_cast<T *>(p + off) = v;
}
template <typename T> [[gnu::always_inline]] inline T &global(uintptr_t base, uintptr_t off) {
    return *reinterpret_cast<T *>(base + off);
}
// one lane of a float array, as its pattern
[[gnu::always_inline]] inline uint32_t lane(const float *v, int i) {
    uint32_t u;
    __builtin_memcpy(&u, v + i, 4);
    return u;
}
[[gnu::always_inline]] inline void set_lane(float *v, int i, uint32_t u) { __builtin_memcpy(v + i, &u, 4); }

[[gnu::always_inline]] inline bool f_le(uint32_t a, uint32_t b) {   // a <= b as floats (false when either is NaN)
    int r;
    __asm__("movd %1,%%xmm0\n\tmovd %2,%%xmm1\n\txorl %0,%0\n\tucomiss %%xmm0,%%xmm1\n\tsetae %b0" : "=&r"(r) : "r"(a), "r"(b) : "xmm0", "xmm1", "cc");
    return r != 0;
}
// b when a >= 0, otherwise (also for a NaN) c
[[gnu::always_inline]] inline uint32_t f_sel(uint32_t a, uint32_t b, uint32_t c) { return f_le(F_ZERO, a) ? b : c; }
// v limited to 0..1 (a NaN gives 0)
[[gnu::always_inline]] inline uint32_t f_unit_range(uint32_t v) {
    v = f_sel(v, v, F_ZERO);
    return f_sel(f_sub(v, F_ONE), F_ONE, v);
}
// slider * 10 + 75: the field of view of the Follow / Cinema camera
[[gnu::always_inline]] inline uint32_t slider_fov(uintptr_t base) {
    return f_add(f_mul(global<uint32_t>(base, G_SLIDER_FOV), F_10), F_75);
}
[[gnu::always_inline]] inline uint32_t slider_distance(uintptr_t base) {
    return f_mul(global<uint32_t>(base, G_SLIDER_DISTANCE), F_10);
}
// the camera's target handle = the object's own handle, or nothing
[[gnu::always_inline]] inline void set_target(uint8_t *cam, const uint8_t *target) {
    wr<uint64_t>(cam, 0xb0, target ? rd<uint64_t>(target, 0x70) : 0);
    wr<uint32_t>(cam, 0xb8, target ? rd<uint32_t>(target, 0x7c) : 0);
}
// length of (first, second, third), summed in this order
[[gnu::always_inline]] inline uint32_t length3(uint32_t first, uint32_t second, uint32_t third) {
    return f_root(f_add(f_add(f_mul(first, first), f_mul(second, second)), f_mul(third, third)));
}

// Follow camera, flat direction at +0x1c4: the rotation's x axis without its height, or (z2, 0, -z0) of the z axis
// when the x axis is nearly vertical; scaled to length 1.
[[gnu::always_inline]] inline void flat_direction(uint8_t *cam) {
    const float *rotation = reinterpret_cast<const float *>(cam + 0x1f0);
    float axis[4];
    u3x_rp_axis_x(rotation, axis);
    uint32_t d0 = lane(axis, 0), d2 = lane(axis, 2);
    const uint32_t d1 = F_ZERO;
    if (f_less(length3(d0, d1, d2), F_HUNDREDTH)) {
        u3x_rp_axis_z(rotation, axis);
        d0 = lane(axis, 2);
        d2 = f_sub(F_ZERO, lane(axis, 0));
    }
    const uint32_t length = length3(d1, d0, d2);
    const uint32_t scale = f_div(F_ONE, f_sel(f_sub(F_TINY, length), F_ONE, length));
    wr<uint32_t>(cam, 0x1c4, f_mul(d0, scale));
    wr<uint32_t>(cam, 0x1cc, f_mul(d2, scale));
    wr<uint32_t>(cam, 0x1c8, f_mul(d1, scale));
}

// how far the death look has turned: 0 until 3 s after the death, 1 from 3.5 s on
[[gnu::always_inline]] inline uint32_t death_look_factor(uintptr_t base, const uint8_t *tracker) {
    const int32_t ticks = static_cast<int32_t>(global<uint32_t>(base, G_CLOCK7) - rd<uint32_t>(tracker, 0x39c));
    const uint32_t late = f_sub(f_mul(f_from_int(ticks), F_TICK), F_3);
    return f_unit_range(f_add(late, late));
}

// the frame of a body, moved 3 m backwards along its own view axis
[[gnu::always_inline]] inline void behind_body(uintptr_t base, uint8_t *frame) {
    float world[8], axis[4], position[4];
    uint32_t offset[4];
    u3x_rp_frame_world(base, frame, world);
    u3x_rp_axis_z(world + 4, axis);
    for (int i = 0; i < 4; i++) offset[i] = f_add(f_mul(f_sub(F_ZERO, lane(axis, i)), F_3), F_ZERO);
    u3x_rp_frame_world(base, frame, world);
    for (int i = 0; i < 4; i++) set_lane(position, i, f_add(lane(world, i), offset[i]));
    u3x_frame_parent(base, frame);
    u3x_rp_frame_local_pos(frame, position);
}

}  // namespace film_cameras

// 4.3: (re)start a Follow camera at the current camera locator, looking at target
extern "C" U3X_FN void u3x_rp_follow_set(uintptr_t base, uint8_t *cam, const uint8_t *target) {
    using namespace film_cameras;
    set_target(cam, target);
    wr<uint32_t>(cam, 0x1a8, 0);
    for (int i = 0; i < 3; i++)
        wr<uint32_t>(cam, 0x1b8 + 4 * i, f_mul(global<uint32_t>(base, G_LOCATOR_SECOND + 4 * i), F_MINUS_ONE));
    for (int i = 0; i < 3; i++) wr<uint32_t>(cam, 0x1ac + 4 * i, global<uint32_t>(base, G_LOCATOR + 4 * i));
    cam[0xc8] |= 2;
    wr<uint32_t>(cam, 0x1d0, 0);
    for (int i = 0; i < 0x20; i += 8) wr<uint64_t>(cam, 0x1e0 + i, global<uint64_t>(base, G_LOCATOR + i));
    wr<uint32_t>(cam, 0x210, F_6);
    flat_direction(cam);
    cam[0x224] = global<int32_t>(base, G_FOLLOW_FLAG) != 0;
    wr<uint32_t>(cam, 0x214, slider_fov(base));
    wr<uint32_t>(cam, 0x1d4, f_mul(slider_distance(base), F_QUARTER));
}

// 4.4: a Fixed camera takes a world location, unless it looks through a dead player
extern "C" U3X_FN void u3x_rp_fixed_set(uintptr_t base, uint8_t *cam, uint32_t fov_bits, const uint8_t *target,
                                        const void *location) {
    using namespace film_cameras;
    wr<uint32_t>(cam, 0x210, fov_bits);
    set_target(cam, target);
    const uint8_t *object = u3x_rp_h(cam + 0xb0);
    if (object && rd<uint32_t>(object, 0x30) == CLASS_PLAYER) {
        const uint8_t *tracker = u3x_rp_trk_by_id(base, rd<uint32_t>(object, 0x3f8));
        if (tracker && u3x_rp_dead(tracker)) return;
    }
    uint8_t *frame = cam + 0x1b0;
    const float *where = static_cast<const float *>(location);
    u3x_frame_parent(base, frame);
    u3x_rp_frame_local_pos(frame, where);
    u3x_rp_frame_local_rot(frame, where + 4);
    game_call<void>(base + FN_FRAME_UNBIND, frame, 0);
}

// 4.5: turn the frame toward the place where the player died
extern "C" U3X_FN void u3x_rp_death_look(uintptr_t base, uint8_t *frame, const uint8_t *tracker) {
    using namespace film_cameras;
    float world[8], to_death[4], direction[4], ahead[4], up[4], look[4], turned[4];
    u3x_rp_frame_world(base, frame, world);
    for (int i = 0; i < 4; i++) set_lane(to_death, i, f_sub(rd<uint32_t>(tracker, 4 * i), lane(world, i)));
    for (int i = 0; i < 4; i++) {
        set_lane(ahead, i, i == 2 ? F_ONE : F_ZERO);
        set_lane(up, i, i == 1 ? F_ONE : F_ZERO);
    }
    u3x_rp_unit_or(to_death, ahead, direction);
    u3x_rp_look_at(direction, up, look);
    u3x_rp_frame_world(base, frame, world);
    u3x_rp_slerp(world + 4, look, death_look_factor(base, tracker), turned);
    u3x_frame_parent(base, frame);
    u3x_rp_frame_local_rot(frame, turned);
}

// 4.7: the HUD of the followed player's weapon, with the scope fading in while the view zooms
extern "C" U3X_FN void u3x_rp_scope_hud(uintptr_t base, uint8_t *player, uint32_t fov_bits) {
    using namespace film_cameras;
    const uintptr_t get_weapon = rd<uintptr_t>(rd<const uint8_t *>(player, 0), 0x3d8);
    uint8_t *weapon = game_call<uint8_t *>(get_weapon, player);
    if (!weapon) return;
    const uint8_t *scope = game_call<const uint8_t *>(base + FN_WEAPON_SCOPE, weapon);
    if (!scope) return;
    const uint32_t scope_fov = rd<uint32_t>(scope, 0x68), scope_near = rd<uint32_t>(scope, 0x70);
    const uint32_t limit = f_sub(scope_fov, F_5);
    const bool wider = f_less(scope_fov, fov_bits);   // the view is wider than the scope's
    const bool zoom = rd<uint32_t>(scope, 0x144) == SCOPE_A &&
                      ((!f_less(limit, fov_bits) && f_less(F_MINUS_ONE, scope_near)) || rd<uint32_t>(scope, 0x184) != 0);
    if (!zoom) {
        if (wider) return;
        u3x_rp_hud_weapon(base, weapon);
        u3x_rp_hud_flag(base, 8);
        u3x_rp_hud_fade(base, F_ONE, 0, F_ONE, 1, 0);
        return;
    }
    const uint32_t zoomed = f_unit_range(f_div(f_sub(fov_bits, scope_near), f_sub(limit, scope_near)));
    const uint32_t fade = f_sub(F_ONE, zoomed);
    if (f_less(F_015, fade)) u3x_rp_send(base, MSG_ZOOMED, player);
    u3x_rp_hud_weapon(base, weapon);
    if (!wider) {
        const uint32_t kind = rd<uint32_t>(scope, 0x148);
        u3x_rp_hud_flag(base, kind == SCOPE_B || kind == SCOPE_A ? 0x40 : 8);
    }
    u3x_rp_hud_fade(base, fade, 0, F_ONE, 1, 0);
}

// 4.2: keep the camera of the current mode on the target: orbit (Follow) or the player's own view (Fixed)
extern "C" U3X_FN void u3x_rp_follow_cam(uintptr_t base, uint8_t *own, uint8_t *target, uint8_t *frame,
                                         uint32_t fov_bits, int ok) {
    using namespace film_cameras;
    float world[8];
    if (global<int32_t>(base, G_CAMERA_MODE) != 0) {
        uint8_t *cam = u3x_rp_h(own + 0x1a0);
        if (!cam)
            u3x_camera_spawn(base, own, u3x_camera_type(base, 0), F_ZERO, nullptr, target);
        else if (u3x_rp_isa(base, cam, CLASS_FOLLOW))
            u3x_rp_follow_set(base, cam, target);
        return;
    }
    if (ok && !u3x_rp_h(own + 0x1a0)) {
        u3x_rp_frame_world(base, frame, world);
        u3x_camera_spawn(base, own, u3x_camera_type(base, 1), fov_bits, world, target);
    }
    uint8_t *cam = u3x_rp_h(own + 0x1a0);
    if (cam && u3x_rp_isa(base, cam, CLASS_FIXED)) {
        u3x_rp_frame_world(base, frame, world);
        u3x_rp_fixed_set(base, cam, fov_bits, target, world);
    }
}

// 4.1: the camera that looks at the followed player (or, with nobody followed, the Cinema camera's sliders)
extern "C" U3X_FN void u3x_rp_cam_target(uintptr_t base, uint8_t *own) {
    using namespace film_cameras;
    const uint32_t selection = rd<uint32_t>(own, 0x1b8) % 10;

    if (!own[0x1b0]) {   // nobody is followed
        global<uint8_t *>(base, G_CURRENT_TRACKER) = nullptr;
        wr<uint32_t>(own, 0x198, F_ZERO);
        global<char>(base, G_FOLLOWED_NAME) = 0;
        uint8_t *cam = u3x_rp_h(own + 0x1a0);
        if (cam && u3x_rp_isa(base, cam, CLASS_CINEMA)) {
            const uint8_t others = cam[0xc8] & static_cast<uint8_t>(~2u);
            cam[0xc8] = global<uint64_t>(base, G_CINEMA_BIT_OFF) == 0 ? others | 2 : others;
            wr<uint32_t>(cam, 0x214, slider_fov(base));
            cam[0x218] = global<int32_t>(base, G_FOLLOW_FLAG) != 0;
            wr<uint32_t>(cam, 0x1d4, slider_distance(base));
        }
        return;
    }

    uint8_t *tracker = u3x_rp_trk(base, selection);
    if (!tracker) return;
    const char *name = reinterpret_cast<const char *>(tracker + 0xe1);
    char *shown = &global<char>(base, G_FOLLOWED_NAME);
    do *shown++ = *name; while (*name++);   // no length limit
    global<int32_t>(base, G_FOLLOWED_INDEX) = static_cast<int32_t>(selection);

    alignas(16) uint8_t frame[0x60];
    if (uint8_t *player = u3x_rp_h(tracker + 0x10)) {
        global<uint8_t *>(base, G_CURRENT_TRACKER) = tracker;
        uint32_t fov = F_75;
        u3x_frame_identity(frame);
        // the game may change frame and fov also when it fails; what it leaves is used
        const bool ok = game_call<uint8_t>(base + FN_PLAYER_VIEW, player + 0x400, frame, &fov) != 0;
        if (ok) {
            wr<uint32_t>(own, 0x198, F_ZERO);
            if (u3x_rp_dead(tracker)) u3x_rp_death_look(base, frame, tracker);
        } else {
            wr<uint32_t>(own, 0x19c, F_ONE);
            wr<uint32_t>(own, 0x198, F_ONE);
        }
        u3x_rp_follow_cam(base, own, player, frame, fov, ok);
        if (global<int32_t>(base, G_CAMERA_MODE) == 0) u3x_rp_scope_hud(base, player, fov);
        return;
    }

    uint8_t *body = u3x_rp_h(tracker + 0x20);
    if (!body) {
        wr<uint32_t>(own, 0x19c, F_ONE);
        global<uint8_t *>(base, G_CURRENT_TRACKER) = nullptr;
        wr<uint32_t>(own, 0x198, F_ONE);
        return;
    }
    global<uint8_t *>(base, G_CURRENT_TRACKER) = tracker;
    u3x_frame_identity(frame);   // overwritten at once by the copy of the body's frame (kept from the spec's oddities)
    for (int i = 0; i < 0x60; i += 8) wr<uint64_t>(frame, i, rd<uint64_t>(body, 0xb0 + i));
    behind_body(base, frame);
    wr<uint32_t>(own, 0x198, F_ZERO);
    if (u3x_rp_dead(tracker)) u3x_rp_death_look(base, frame, tracker);
    u3x_rp_follow_cam(base, own, body, frame, F_75, 1);
}

// 4.8: camera modes 1 and 2: the periodic re-record, and in mode 1 the free camera driving the Cinema camera
extern "C" U3X_FN void u3x_rp_cam_mode12(uintptr_t base, uint8_t *st, uint8_t *own) {
    using namespace film_cameras;
    const uint8_t *clock = global<const uint8_t *>(base, G_FILM_CLOCK);
    if (rd<int32_t>(st, 0x18) <= rd<int32_t>(clock, 0xc)) {
        u3x_camera_rerecord(base, own);
        wr<uint32_t>(st, 0x18, rd<uint32_t>(clock, 0xc) + 9);
    }
    if (rd<int32_t>(own, 0xa60) == 0 || global<int32_t>(base, G_CAMERA_MODE) != 1) return;

    alignas(16) uint8_t frame[0x60];
    float world[8];
    u3x_frame_make(frame, reinterpret_cast<const void *>(base + G_LOCATOR));
    if (!u3x_rp_h(own + 0x1a0)) {
        u3x_rp_frame_world(base, frame, world);
        u3x_camera_spawn(base, own, reinterpret_cast<uint8_t *>(base + G_CINEMA_TYPE), F_75, world, nullptr);
    }
    game_call<void>(base + FN_FREE_CAMERA, own + 0x6c0, frame);
    uint8_t *cam = u3x_rp_h(own + 0x1a0);
    if (cam && rd<uint32_t>(cam, 0x30) == CLASS_CINEMA) {   // the exact class here, not is-a
        u3x_rp_frame_world(base, frame, world);
        u3x_camera_cinema_set(cam, world);
    }
}
