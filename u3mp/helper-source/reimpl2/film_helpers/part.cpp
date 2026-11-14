// film_helpers (reimpl2), written from spec/film_helpers.md: camera spawn, camera re-record, the two rebuilt camera
// types, HUD flag, tracker and class lookups, message send, script-variable overrides, film bookmark, screenshot step.
// None of these functions does float arithmetic: float values only travel as 32-bit patterns.

extern "C" {
// functions of the C part and of other groups (contracts: spec 3.2)
uint64_t u3h_rp_fcall(uint64_t *r);                                // call bridge, r = 14 quadwords (spec 4.2)
uint8_t *u3h_camera_manager(uintptr_t base);                       // screen-0 camera manager, null after its assert
void u3x_frame_make(uint8_t *frame, const uint8_t *location);      // unbound frame from a location
void u3x_stream_write(uintptr_t base, uint8_t *stream, uint32_t value, uint32_t n);
uint32_t *u3x_rp_pad(uintptr_t base);                              // pad 0's pressed-buttons word

// this group's entry points
void u3x_camera_spawn(uintptr_t base, uint8_t *own, uint64_t type, uint32_t fov, const void *locator, const uint8_t *target);
void u3x_camera_rerecord(uintptr_t base, uint8_t *own);
uint64_t u3x_camera_type(uintptr_t base, int fixed);
void u3x_rp_hud_flag(uintptr_t base, uint32_t f);
uint8_t *u3x_rp_trk_by_id(uintptr_t base, uint32_t id);
int u3x_rp_isa(uintptr_t base, uint8_t *obj, uint32_t hash);
void u3x_rp_send(uintptr_t base, uint32_t msg, uint8_t *target);
void u3x_ovr_blend(uintptr_t base, uint32_t t, uint8_t *addr, const void *val, int32_t frames, uint8_t *owner);
void u3x_ovr_scale(uintptr_t base, uint8_t *addr, const void *val);
void u3x_ovr_byte(uintptr_t base, uint32_t hash, uint8_t *addr, const uint8_t *val, int32_t frames, int32_t time);
void u3x_bookmark_create(uintptr_t base, uint8_t *bm, int32_t t);
int u3x_rp_screenshot_step(uintptr_t base, uint8_t *st, uint8_t *own);
}

namespace film_helpers {
namespace {

// ---- image offsets (spec 3.1, 3.3)
constexpr uintptr_t FN_ADD_CAMERA = 0x101b530, FN_RECORD_CAMERA = 0x6c1d60, FN_NAME_HASH = 0xd94550,
                    FN_SEND = 0x1197380, FN_ADD_OVERRIDE = 0x12284e0, FN_ALLOC = 0xd9e990,
                    FN_SNAP_1 = 0x6b1b90, FN_SNAP_2 = 0x6b1dc0, FN_SNAP_3 = 0x7427d0, FN_SNAP_4 = 0x72e560,
                    FN_CMP_4 = 0x1228b20, FN_SET_4_BLEND = 0x1228b30, FN_SET_4_SCALE = 0x1228ca0,
                    FN_CMP_1 = 0x12284c0, FN_SET_1 = 0x12284d0;
constexpr uintptr_t G_CAMERA_MODE = 0x17c5a10, G_PLAY_SPEED = 0x16e716c, G_CAMERA_LOCATOR = 0x16ea550,
                    G_FOLLOW_PARAMS = 0x1b5b4d0, G_FIXED_PARAMS = 0x1b5b6c0, G_RERECORDING = 0x17c5d00,
                    G_RECORD_STREAM = 0x1b68840, G_RERECORD_STREAM = 0x17c3850, G_READ_POSITION = 0x17c3840,
                    G_WRITER_STAMP = 0x17c3c58, G_NET_MANAGER = 0x17cb060, G_NET_MANAGER_PTR = 0x16e2f50,
                    G_HUD = 0x1805fa0, G_GAME = 0x1b24b98, G_CLASS_REGISTRY = 0x1b3efd8,
                    G_PAUSE_REQUEST = 0x17c3571, G_PAUSE_COMPANION = 0x17c3572,
                    G_CLOCK0_SCALE = 0x17b4adc, G_CLOCK1_SCALE = 0x17b4b1c;

constexpr uintptr_t TRACKERS = 0x390, TRACKER_SIZE = 0x9c0;
constexpr int TRACKER_COUNT = 10;
constexpr uint32_t OVERRIDE_SCALE_ID = 0x309861e2u;

// ---- the helper's own data: names of the two rebuilt camera types and of their classes, and the type objects
U3X_RO const char follow_type_name[] = "kCameraCinemaFollow";
U3X_RO const char fixed_type_name[] = "kCameraCinemaFixed";
U3X_RO const char follow_class_name[] = "CameraControlCinemaFollow";
U3X_RO const char fixed_class_name[] = "CameraControlCinemaFixed";

struct alignas(16) CameraType {   // the game's camera type object: 0x28 bytes used
    const char *name;             // +0x00
    uint32_t class_hash;          // +0x08
    uint32_t unused;
    uint64_t parameters;          // +0x10 parameter descriptor of the class
    uint32_t parameter_size;      // +0x18
    uint32_t flags;               // +0x1c
    uint32_t kind;                // +0x20
};
U3X_BSS CameraType camera_types[2];   // 0 = Follow, 1 = Fixed; filled on first use

// ---- small tools
template <typename T> [[gnu::always_inline]] inline void put(void *p, uintptr_t off, T value) {
    *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + off) = value;
}
[[gnu::always_inline]] inline void clear_bytes(uint8_t *p, int n) {
    for (int i = 0; i < n; ++i) p[i] = 0;
}
[[gnu::always_inline]] inline void clear_words(uint64_t *p, int n) {
    for (int i = 0; i < n; ++i) p[i] = 0;
}
[[gnu::always_inline]] inline uint64_t word(const void *p) { return reinterpret_cast<uint64_t>(p); }

// slots of the call bridge's record (spec 4.2)
enum : int { CALL_FN = 0, CALL_ARG = 1, CALL_XMM = 7, CALL_STACK = 11, CALL_SLOTS = 14 };

// the game frame time: i32 +0x94 of the game object
[[gnu::always_inline]] inline int32_t game_frame_time(uintptr_t base) {
    return rd<int32_t>(at<const uint8_t *>(base + G_GAME), 0x94);
}

// network id of a tracker (spec 3.4)
[[gnu::always_inline]] inline uint32_t network_id(const uint8_t *tracker) {
    const uint32_t local = rd<uint32_t>(tracker, 0x1e4) & 0xfffu;
    uint32_t id = local + rd<uint32_t>(tracker, 0x1e8) * 0x1000u;
    if (rd<uint8_t>(tracker, 0x1ec) && local) id += 0x800u;
    return id;
}

// adds a script-variable override through the bridge; returns the override object or null
U3X_FN uint8_t *add_override(uintptr_t base, uint32_t weight, uint32_t id, const uint8_t *addr, const void *val,
                             uint32_t size, uintptr_t compare, uintptr_t set, const uint8_t *owner) {
    uint64_t r[CALL_SLOTS];
    clear_words(r, CALL_SLOTS);
    r[CALL_FN] = base + FN_ADD_OVERRIDE;
    r[CALL_ARG + 0] = id;
    r[CALL_ARG + 1] = word(addr);
    r[CALL_ARG + 2] = word(val);
    r[CALL_ARG + 3] = size;
    r[CALL_ARG + 4] = base + compare;
    r[CALL_ARG + 5] = base + set;
    r[CALL_XMM] = weight;
    r[CALL_STACK + 0] = word(owner);
    return reinterpret_cast<uint8_t *>(u3h_rp_fcall(r));
}

}  // namespace
}  // namespace film_helpers

// ---- cameras (spec 4.7)

// Asks the camera manager for a new camera of the given type and keeps its handle in the owner.
U3X_FN void u3x_camera_spawn(uintptr_t base, uint8_t *own, uint64_t type, uint32_t fov, const void *locator,
                             const uint8_t *target) {
    using namespace film_helpers;
    alignas(16) uint8_t block[0x60];
    clear_bytes(block, 0x60);
    put<uint32_t>(block, 0x00, 0x30000000u);
    put<uint32_t>(block, 0x0c, fov);
    put<uint32_t>(block, 0x10, 0xbf800000u);
    put<uint32_t>(block, 0x14, 0xbf800000u);
    if (locator) {
        for (int i = 0; i < 0x20; ++i) block[0x20 + i] = rd<uint8_t>(locator, i);
        block[0x50] = 1;
    }
    if (target) {
        put<uint64_t>(block, 0x40, rd<uint64_t>(target, 0x70));
        put<uint32_t>(block, 0x48, rd<uint32_t>(target, 0x7c));
    }

    uint64_t type_word = type;
    uint64_t two_word = 0x0000000200000000ull;
    uint8_t *manager = u3h_camera_manager(base);   // the call below is made also without a manager
    const uint8_t *cam = game_call<uint8_t *>(base + FN_ADD_CAMERA, manager, &type_word, &two_word, &block[0]);

    put<uint64_t>(own, 0x1a0, cam ? rd<uint64_t>(cam, 0x70) : 0);
    put<uint32_t>(own, 0x1a8, cam ? rd<uint32_t>(cam, 0x7c) : 0);
}

// While a re-recording runs: writes a camera record (speed, mode, followed player, locator frame) into its stream.
U3X_FN void u3x_camera_rerecord(uintptr_t base, uint8_t *own) {
    using namespace film_helpers;
    if (at<uint32_t>(base + G_RERECORDING) == 0) return;

    alignas(16) uint8_t frame[0x60];
    u3x_frame_make(frame, reinterpret_cast<const uint8_t *>(base + G_CAMERA_LOCATOR));

    at<uint64_t>(base + G_RECORD_STREAM) = base + G_RERECORD_STREAM;
    uint64_t r[CALL_SLOTS];
    clear_words(r, CALL_SLOTS);
    r[CALL_FN] = base + FN_RECORD_CAMERA;
    r[CALL_ARG + 0] = at<uint32_t>(base + G_CAMERA_MODE) & 0xffu;   // low byte of the mode only
    r[CALL_ARG + 1] = rd<uint8_t>(own, 0x1b8);                      // low byte of the followed selection
    r[CALL_ARG + 2] = word(frame);
    r[CALL_XMM] = at<uint32_t>(base + G_PLAY_SPEED);
    u3h_rp_fcall(r);
    at<uint64_t>(base + G_RECORD_STREAM) = 0;
}

// The type object of the Follow (0) or Fixed (1) cinema camera, built on first use.
U3X_FN uint64_t u3x_camera_type(uintptr_t base, int fixed) {
    using namespace film_helpers;
    CameraType &type = camera_types[fixed];
    if (!type.name) {
        type.name = fixed ? fixed_type_name : follow_type_name;   // written before the hash call
        type.class_hash = game_call<uint32_t>(base + FN_NAME_HASH, uint32_t{0},
                                              fixed ? fixed_class_name : follow_class_name, uint32_t{1});
        type.parameters = base + (fixed ? G_FIXED_PARAMS : G_FOLLOW_PARAMS);
        type.parameter_size = 0x60;
        type.flags = 0x20000000u;
        type.kind = 2;
    }
    return word(&type);
}

// ---- HUD flag (spec 4.8): sets the flag and shows every element that carries exactly this flag value
U3X_FN void u3x_rp_hud_flag(uintptr_t base, uint32_t f) {
    using namespace film_helpers;
    uint8_t *hud = reinterpret_cast<uint8_t *>(base + G_HUD);
    put<uint32_t>(hud, 8, rd<uint32_t>(hud, 8) | f);
    const uint32_t count = rd<uint32_t>(hud, 0x38);
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t *element = rd<uint8_t *>(hud, 0x40 + 8 * static_cast<uintptr_t>(i));
        if (!element || rd<uint32_t>(element, 8) != f) continue;
        put<uint32_t>(element, 0xc, 1);
        if (rd<uint32_t>(element, 0x10) != 1) put<int32_t>(element, 0x18, game_frame_time(base));
    }
}

// ---- lookups (spec 4.9)

// The first tracker with this network id, or null. No null test of the manager pointer, no net-game test.
U3X_FN uint8_t *u3x_rp_trk_by_id(uintptr_t base, uint32_t id) {
    using namespace film_helpers;
    if (id == 0) return nullptr;
    uint8_t *tracker = at<uint8_t *>(base + G_NET_MANAGER_PTR) + TRACKERS;
    for (int i = 0; i < TRACKER_COUNT; ++i, tracker += TRACKER_SIZE)
        if (network_id(tracker) == id) return tracker;
    return nullptr;
}

// 1 when the object's class bit set has the bit of the class with this name hash.
U3X_FN int u3x_rp_isa(uintptr_t base, uint8_t *obj, uint32_t hash) {
    using namespace film_helpers;
    const uint8_t *registry = reinterpret_cast<const uint8_t *>(base + G_CLASS_REGISTRY);
    if (rd<uint8_t>(registry, 0) == 0) return 0;
    const uint8_t *head = registry + 8 + 16 * static_cast<uintptr_t>(hash % 257u);
    for (const uint8_t *entry = rd<const uint8_t *>(head, 0); entry != head; entry = rd<const uint8_t *>(entry, 0)) {
        if (rd<uint32_t>(entry, 0x10) != hash) continue;
        const uint32_t bit = rd<uint32_t>(entry, 0x14);
        const uint8_t *type_info = rd<const uint8_t *>(obj, 0x38);
        return static_cast<int>((rd<uint64_t>(type_info, 0x20 + 8 * static_cast<uintptr_t>(bit / 64)) >> (bit % 64)) & 1);
    }
    return 0;
}

// ---- message send and overrides (spec 4.10), all through the bridge

// Sends a message without arguments to an object.
U3X_FN void u3x_rp_send(uintptr_t base, uint32_t msg, uint8_t *target) {
    using namespace film_helpers;
    uint64_t result[4];        // 0x20 bytes
    uint64_t arguments[4][3];  // four blocks of 0x18 bytes
    uint64_t r[CALL_SLOTS];
    clear_words(result, 4);
    for (int i = 0; i < 4; ++i) clear_words(arguments[i], 3);
    clear_words(r, CALL_SLOTS);
    r[CALL_FN] = base + FN_SEND;
    r[CALL_ARG + 0] = word(result);
    r[CALL_ARG + 2] = msg;
    r[CALL_ARG + 3] = word(target);
    r[CALL_ARG + 4] = word(arguments[0]);
    r[CALL_ARG + 5] = word(arguments[1]);
    r[CALL_STACK + 0] = word(arguments[2]);
    r[CALL_STACK + 1] = word(arguments[3]);
    u3h_rp_fcall(r);
}

// Blends a 4-byte script variable towards a value for some frames, keyed by an owner.
U3X_FN void u3x_ovr_blend(uintptr_t base, uint32_t t, uint8_t *addr, const void *val, int32_t frames, uint8_t *owner) {
    using namespace film_helpers;
    if (!owner) return;
    uint8_t *o = add_override(base, t, 0, addr, val, 4, FN_CMP_4, FN_SET_4_BLEND, owner);
    if (!o) return;
    put<int32_t>(o, 0x28, static_cast<int32_t>(static_cast<uint32_t>(frames) + 1u));
    put<uint8_t>(o, 0x49, rd<uint8_t>(o, 0x49) | 2);
}

// Scales a 4-byte script variable (a clock's time scale) by a value.
U3X_FN void u3x_ovr_scale(uintptr_t base, uint8_t *addr, const void *val) {
    using namespace film_helpers;
    uint8_t *o = add_override(base, F_ONE, OVERRIDE_SCALE_ID, addr, val, 4, FN_CMP_4, FN_SET_4_SCALE, nullptr);
    if (!o) return;
    put<int32_t>(o, 0x28, 0);
    put<uint8_t>(o, 0x49, rd<uint8_t>(o, 0x49) | 8);
}

// Overrides a 1-byte script variable for some frames, starting at a time offset from now.
U3X_FN void u3x_ovr_byte(uintptr_t base, uint32_t hash, uint8_t *addr, const uint8_t *val, int32_t frames, int32_t time) {
    using namespace film_helpers;
    uint8_t *o = add_override(base, F_ONE, hash, addr, val, 1, FN_CMP_1, FN_SET_1, nullptr);
    if (!o) return;
    put<uint8_t>(o, 0x49, (rd<uint8_t>(o, 0x49) & 0xdb) | 4);
    put<int32_t>(o, 0x28, static_cast<int32_t>(static_cast<uint32_t>(frames) + 1u));
    put<int32_t>(o, 0x10, static_cast<int32_t>(static_cast<uint32_t>(game_frame_time(base)) + 9u + static_cast<uint32_t>(time)));
}

// ---- film bookmark (spec 4.11): a snapshot of the game state at film time t, written into the slot's own stream.
// Needs a net game (always there while a film plays): the first tracker is read without a test.
U3X_FN void u3x_bookmark_create(uintptr_t base, uint8_t *bm, int32_t t) {
    using namespace film_helpers;
    uint8_t *buffer = rd<uint8_t *>(bm, 0x20);
    if (!buffer) {
        uint32_t heap_tag = 0x14;
        buffer = game_call<uint8_t *>(base + FN_ALLOC, uint64_t{0x2800}, &heap_tag, uint64_t{8});
        put<uint8_t *>(bm, 0x20, buffer);
    }
    put<uint8_t *>(bm, 0x00, buffer);
    put<uint32_t>(bm, 0x08, 0);
    put<uint32_t>(bm, 0x0c, 0x14000u);
    put<uint32_t>(bm, 0x10, 0);
    put<uint64_t>(bm, 0x28, at<uint64_t>(base + G_WRITER_STAMP));
    put<uint32_t>(bm, 0x1c, at<uint32_t>(base + G_READ_POSITION));
    put<int32_t>(bm, 0x18, t);
    at<uint8_t *>(base + G_RECORD_STREAM) = bm;

    uint8_t *net = reinterpret_cast<uint8_t *>(base + G_NET_MANAGER);
    const uint32_t id = network_id(net + TRACKERS);
    game_call<void>(base + FN_SNAP_1, id);
    game_call<void>(base + FN_SNAP_2);
    game_call<void>(base + FN_SNAP_3, net);
    game_call<void>(base + FN_SNAP_4, net, id);
    u3x_stream_write(base, bm, 0, 32);   // a 32-bit zero ends the snapshot
    at<uint8_t *>(base + G_RECORD_STREAM) = nullptr;
}

// ---- screenshot request (spec 4.12): no photo utility here, so every step takes the failure path.
U3X_FN int u3x_rp_screenshot_step(uintptr_t base, uint8_t *st, uint8_t *own) {
    using namespace film_helpers;
    static_cast<void>(st);
    put<uint8_t>(own, 0x1bc, 0);
    at<uint8_t>(base + G_PAUSE_COMPANION) = 1;
    at<uint8_t>(base + G_PAUSE_REQUEST) = 1;

    uint32_t stopped = 0;   // time scale 0.0 for clock 1, then clock 0
    u3x_ovr_scale(base, reinterpret_cast<uint8_t *>(base + G_CLOCK1_SCALE), &stopped);
    u3x_ovr_scale(base, reinterpret_cast<uint8_t *>(base + G_CLOCK0_SCALE), &stopped);

    const int32_t state = rd<int32_t>(own, 0x1b4);
    if (state == 0) {
        put<int32_t>(own, 0x1b4, 1);
    } else if (state == 1) {
        put<int32_t>(own, 0x1b4, 10);
    } else if (state == 10 || state == 12 || state == 14) {
        put<int32_t>(own, 0x1b4, state + 1);
        put<uint8_t>(own, 0x1b2, 0);
    }

    *u3x_rp_pad(base) = 0;
    return 1;
}
