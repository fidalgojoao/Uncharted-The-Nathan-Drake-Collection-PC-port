// G43 part A: co-op hunter spectate (entity tag value, "spoiler" net message, camera kill, spectate).
// Written from spec G43_coop_spectate.md, sections 3.1 to 3.4, 3.7, 3.9, 4.1 to 4.4.

extern "C" {
uint8_t *u3h_ext(void);
uint32_t u3h_crc_sid(const char *p);
int u3h_cam_fixed(uintptr_t base, void *s, uint8_t *p, int32_t idx);
uint32_t u3x_net_player_id(const uint8_t *np);
uint32_t u3x_hunter_count(uintptr_t base);
int32_t u3x_hunter_id(uintptr_t base, uint32_t i);
uint32_t u3x_hunter_index(uintptr_t base, int32_t id);
uint32_t u3x_pad_pressed(uintptr_t base, int32_t screen);
int u3x_spawner_occupied(uintptr_t base, void *s, int32_t id);
int u3x_menu_open(const uint8_t *m);
uint32_t u3x_menu_page(const uint8_t *m);
void u3x_menu_show(uintptr_t base, uint8_t *m, uint32_t page);
int u3x_menu_back(uintptr_t base, uint8_t *m, int silent);
void u3x_menu_close(uintptr_t base, uint8_t *m);

uint32_t u3x_entity_tag(const uint8_t *e, uint32_t tag, uint32_t fallback);
void u3x_send_spoiler(uintptr_t base, uint32_t code);
void u3x_camera_kill(uint8_t *p);
int u3x_coop_spectate(uintptr_t base, void *s, uint8_t *p, uint8_t *menu);
}

namespace g43a {
namespace {

constexpr uintptr_t MANAGER = 0x17cb060;        // the net game manager
constexpr uintptr_t MSG_TABLE = 0x16d3770;      // function table of net message 0x5e
constexpr uintptr_t FN_MSG_ALLOC = 0xd9e990;
constexpr uintptr_t FN_MSG_SEND = 0x733d90;
constexpr uintptr_t FN_MSG_HANDLER = 0x6d2e80;
constexpr uint32_t CLASS_PAGE = 0xd3a938b7u;    // the hunter class page
constexpr uint32_t PAD_RIGHT = 0x20, PAD_LEFT = 0x80;

// net player record fields
constexpr uintptr_t P_SCREEN = 0x1e8, P_CAMERA = 0x388, P_ASKED = 0x5fd, P_WATCHED = 0x600, P_CLASS_POS = 0x608,
                    P_CLASS_PICKED = 0x60c;

// the first 12 code bytes the three game functions must begin with
U3X_RO const uint8_t code_alloc[12] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54};
U3X_RO const uint8_t code_send[12] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x54, 0x53, 0x49};
U3X_RO const uint8_t code_handler[12] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x56, 0x53, 0x44, 0x8B, 0x4F, 0x14, 0x41};

template <typename T> [[gnu::always_inline]] inline T &field(uint8_t *p, uintptr_t off) {
    return *reinterpret_cast<T *>(p + off);
}

// co-op counter c[k] of the extension state
[[gnu::always_inline]] inline uint32_t &counter(int k) { return field<uint32_t>(u3h_ext(), 0x100 + 4 * k); }

U3X_FN bool begins_with(uintptr_t address, const uint8_t *bytes) {
    const uint8_t *code = reinterpret_cast<const uint8_t *>(address);
    for (int i = 0; i < 12; i++)
        if (code[i] != bytes[i]) return false;
    return true;
}

// the object of a handle (pointer, then u32 id 8 bytes later), or null when the handle is not valid
U3X_FN uint8_t *handle_object(const uint8_t *handle) {
    const uint8_t *const *holder = rd<const uint8_t *const *>(handle, 0);
    const uint32_t id = rd<uint32_t>(handle, 8);
    if (!holder || !id) return nullptr;
    const uint8_t *object = *holder;
    if (!object || rd<uint32_t>(object, 0x7c) != id) return nullptr;
    return const_cast<uint8_t *>(object);
}

// the function in slot `slot` (a byte offset) of an object's function table
[[gnu::always_inline]] inline uintptr_t slot_of(const uint8_t *object, uintptr_t slot) {
    return rd<uintptr_t>(rd<const uint8_t *>(object, 0), slot);
}

}  // namespace
}  // namespace g43a

// The value of tag `tag` in an entity's sorted tag list, or `fallback`.
extern "C" U3X_FN uint32_t u3x_entity_tag(const uint8_t *e, uint32_t tag, uint32_t fallback) {
    if (!e) return fallback;
    const uint8_t *list = rd<const uint8_t *>(e, 0x68);
    if (!list) return fallback;
    const uint8_t *entries = rd<const uint8_t *>(list, 0);
    const uint32_t count = rd<uint32_t>(list, 8);
    if (!entries || !count) return fallback;

    // lower bound: the first entry whose tag is not below the wanted one
    uint32_t first = 0, len = count;
    while (len) {
        const uint32_t half = len / 2;
        if (rd<uint32_t>(entries, (first + half) * 0x10ull) < tag) {
            first += half + 1;
            len -= half + 1;
        } else {
            len = half;
        }
    }
    if (first >= count) return fallback;
    const uint32_t offset = first * 0x10u;   // 32-bit product (wraps), then added to the 64-bit address
    const uint8_t *entry = entries + offset;
    if (rd<uint32_t>(entry, 0) != tag) return fallback;

    const uint8_t *value = rd<const uint8_t *>(entry, 8);
    if (!value) return fallback;
    const uint8_t *data = rd<const uint8_t *>(value, 8);
    if (!data) return fallback;
    const int16_t type = rd<int16_t>(value, 0);
    if (type == 0xd || type == 1) return rd<uint32_t>(data, 0);   // name id, integer
    if (type == 5) {                                               // text: its name checksum
        const char *text = rd<const char *>(data, 0);
        if (!text || !*text) return fallback;
        return u3h_crc_sid(text);
    }
    return fallback;
}

// Sends net message 0x5e (a hunter asks the host for an AI) and lets this machine handle it too.
extern "C" U3X_FN void u3x_send_spoiler(uintptr_t base, uint32_t code) {
    using namespace g43a;
    if (!begins_with(base + FN_MSG_ALLOC, code_alloc) || !begins_with(base + FN_MSG_SEND, code_send) ||
        !begins_with(base + FN_MSG_HANDLER, code_handler))
        return;

    int32_t words[2];
    words[0] = 0x14;
    words[1] = 0;
    uint8_t *msg = game_call<uint8_t *>(base + FN_MSG_ALLOC, uint64_t{0x28}, words, uint64_t{8});
    if (!msg) return;

    // the defaults first, then what the player with that id picked (the first of the ten records with the id)
    field<uint32_t>(msg, 0x18) = 0;
    field<uint32_t>(msg, 0x1c) = 0;
    msg[0x20] = 0;
    uint8_t *manager = reinterpret_cast<uint8_t *>(base + MANAGER);
    for (int i = 0; i < 10; i++) {
        uint8_t *rec = manager + 0x390 + 0x9c0 * i;
        if (u3x_net_player_id(rec) != code) continue;
        field<uint32_t>(msg, 0x18) = field<uint32_t>(rec, P_CLASS_POS);
        field<uint32_t>(msg, 0x1c) = field<uint32_t>(rec, P_WATCHED);
        msg[0x20] = field<uint8_t>(rec, P_ASKED);
        break;
    }

    field<uintptr_t>(msg, 0) = base + MSG_TABLE;
    msg[8] = 0x5e;   // message number
    msg[9] = 0;
    msg[0xa] = 1;    // reference count
    msg[0xb] = 1;
    msg[0xc] = 0;
    msg[0xd] = 0;
    field<uint32_t>(msg, 0x14) = code;

    game_call<void>(base + FN_MSG_SEND, manager, msg);
    game_call<void>(slot_of(msg, 0x20), msg, uint64_t{0});        // the game's own handler
    if (--msg[0xa] == 0) game_call<void>(slot_of(msg, 8), msg);   // the game releases it
    counter(5) += 1;
}

// Ends the death / spectate camera of a net player, when it has one.
extern "C" U3X_FN void u3x_camera_kill(uint8_t *p) {
    using namespace g43a;
    uint8_t *camera = handle_object(p + P_CAMERA);
    if (camera) game_call<void>(slot_of(camera, 0x158), camera);
}

// One frame of a dead co-op hunter: 1 = spectating a hunter AI, 0 = not.
extern "C" U3X_FN int u3x_coop_spectate(uintptr_t base, void *s, uint8_t *p, uint8_t *menu) {
    using namespace g43a;
    const uint32_t n = u3x_hunter_count(base);
    const int32_t screen = field<int32_t>(p, P_SCREEN);
    int32_t &sel = field<int32_t>(p, P_WATCHED);
    counter(13) = n;

    if (rd<int32_t>(reinterpret_cast<const uint8_t *>(base + MANAGER), 0x6808) != 2 || !n) {
        if (u3x_menu_page(menu) == CLASS_PAGE) {
            u3x_menu_back(base, menu, 0);
            u3x_menu_close(base, menu);
        }
        if (sel) u3x_camera_kill(p);
        sel = 0;
        return 0;
    }

    counter(3) += 1;
    if (!sel || u3x_hunter_index(base, sel) == n) {
        sel = u3x_hunter_id(base, 0);
        u3x_camera_kill(p);
    }
    if (!handle_object(p + P_CAMERA)) u3h_cam_fixed(base, s, p, 0);

    if (u3x_menu_page(menu) == CLASS_PAGE && u3x_menu_open(menu) != 0) {
        // cycle through the hunter AIs with left / right
        const uint32_t pressed = u3x_pad_pressed(base, screen);
        const int step = pressed & PAD_RIGHT ? 1 : pressed & PAD_LEFT ? -1 : 0;
        if (step) {
            int64_t target = int64_t{u3x_hunter_index(base, sel)} + step;
            if (target < 0) target = int64_t{n} - 1;
            else if (target >= int64_t{n}) target = 0;
            sel = u3x_hunter_id(base, static_cast<uint32_t>(target));
            counter(6) += 1;
            u3h_cam_fixed(base, s, p, 0);
        }
    } else if (!p[P_CLASS_PICKED] && menu && u3x_menu_open(menu) == 0) {
        // open the class page
        u3x_menu_show(base, menu, CLASS_PAGE);
        field<int32_t>(menu, 0x2694) = screen;
        counter(4) += 1;
        u3h_cam_fixed(base, s, p, 0);
    } else if (u3x_menu_open(menu) == 0 && !p[P_ASKED] && p[P_CLASS_PICKED] &&
               u3x_spawner_occupied(base, s, sel) == 0) {
        // ask the host for the AI, once per pick
        p[P_ASKED] = 1;
        u3x_send_spoiler(base, u3x_net_player_id(p));
    }

    counter(12) = static_cast<uint32_t>(sel);
    return 1;
}
