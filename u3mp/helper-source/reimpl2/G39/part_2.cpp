// G39 part 2: the lobby stage and leaving multiplayer (spec G39_mp_startup.md, sections 5.1 to 5.5).

extern "C" {
// part 1 of this group
uint8_t *u3x_level_want_entry(uintptr_t base, uint32_t sid);
int u3x_actor_want(uintptr_t base, void *s, const char *name);
void u3x_title_unwant(uintptr_t base, void *s);
void u3x_net_shutdown_levels(uintptr_t base, void *s);
// other groups and the C part
uint32_t u3h_crc_sid(const char *text);
int u3h_stage_unwant(uintptr_t base, uint32_t sid);
void u3h_multi_all(uintptr_t base, void *s, int keep);
int u3h_stage_logout_hold(uintptr_t base, void *s, const char *stage);
void u3h_match_stage_cleanup(uintptr_t base, void *s, void *find, void *show);
int u3x_mp_lan_or_online(uintptr_t base);
uint32_t u3h_active_page(uintptr_t base);
void u3x_emblem_shutdown(uintptr_t base);
void u3x_lan_shutdown(uintptr_t base);
void u3x_friend_shutdown(uintptr_t base, void *s);
void u3x_lobby_scene_shutdown(uintptr_t base);
// this part
void u3x_stage_tick(uintptr_t base, void *s);
void u3x_net_shutdown_parts(uintptr_t base, void *s);
void u3x_net_shutdown_do(uintptr_t base, void *s);
void u3x_net_shutdown_tick(uintptr_t base, void *s);
int u3x_leave_mp_yes(uintptr_t base, void *s);
}

namespace g39_part2 {

// game data (image offsets)
constexpr uintptr_t MENU_SCENE = 0x17bba90;
constexpr uintptr_t MATCH_STATE = 0x17d1868;   // u32, 2 = in a match
constexpr uintptr_t STAGE_NAME = 0x17d2518;    // text, empty = none named
constexpr uintptr_t LAN_FLAG = 0x17d5c60;
constexpr uintptr_t MP_ACTIVE = 0x17d5c61;
constexpr uintptr_t LEVEL_MANAGER = 0x1afcf10;

// the helper's state object
constexpr uintptr_t S_PHASE = 680;             // startup phase
constexpr uintptr_t S_NATIVES = 768;
constexpr uintptr_t S_SWITCHES = 0x20c0;       // bit 8 = NetShutdown switched off
constexpr uintptr_t S_SHUTDOWNS = 0x20c4;      // NetShutdowns run
constexpr uintptr_t S_TITLE_CAMERA = 0x26c0;   // [0] title state object, [1] hand the camera back
constexpr uintptr_t S_SWITCHES2 = 0x3d78;      // bit 22 = no stage cleanup in a match
constexpr uintptr_t S_STAGE_WORDS = 0x3e10;

constexpr uint32_t ST_DEFAULT_NAMED = 1u;
constexpr uint32_t ST_SCENE_ENTERED = 0x10000u;
constexpr uint32_t ST_PROTO_WANTED = 0x20000u;

// the helper's state block: the LOB words
constexpr uintptr_t EXT_LOB = 0x1ff0;

// a level object
constexpr uintptr_t LEVEL_FLAGS = 2;
constexpr uintptr_t LEVEL_STATE = 0x30;
constexpr uint32_t LEVEL_LOADED = 8;

// the front-end pages that lie before NetInit
constexpr uint32_t PAGE_ROOT_TILES = 0xd45dfccau;
constexpr uint32_t PAGE_ROOT_MAIN = 0x37656388u;
constexpr uint32_t PAGE_MP_CHOICE = 0x1e66a744u;
constexpr uint32_t PAGE_COOP_CHOICE = 0xb0e67b0eu;

// the helper's own fallback stage, and the multiplayer proto actor package
U3X_RO const char default_stage[] = {'s','a','n','-','u','p','p','e','r','-','m','u','l','t','i','-','s','t','a','g','e',0};
U3X_RO const char proto_actor[] = {'a','n','i','m','-','p','r','o','t','o','-','m','u','l','t','i','p','l','a','y','e','r',0};

// diagnostics only
U3X_BSS uint32_t leave_yes_calls;
U3X_BSS uint32_t leave_yes_shutdowns;

// a level request, as native 41 takes it
struct Request {
    uint64_t hash;
    uint32_t sid;
    uint16_t number;
    uint8_t flags;
    uint8_t zero_f;
    uint64_t zero_10;
};

template <typename T> [[gnu::always_inline]] inline T &field(void *p, uintptr_t off) {
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + off);
}

[[gnu::always_inline]] inline void *native(void *s, int k) { return field<void *>(s, S_NATIVES + 8u * k); }

[[gnu::always_inline]] inline uint32_t *lob() { return reinterpret_cast<uint32_t *>(u3h_ext() + EXT_LOB); }

// NetShutdown must not run now: switched off, startup under way, or in a match
[[gnu::always_inline]] inline bool shutdown_blocked(uintptr_t base, void *s) {
    if (field<uint32_t>(s, S_SWITCHES) & 0x100u) return true;
    if (field<uint32_t>(s, S_PHASE) == 1) return true;
    if (field<uint32_t>(s, S_PHASE) == 2) return true;
    return at<uint32_t>(base + MATCH_STATE) == 2;
}

// multiplayer is over: let the stage go and give the title its camera back
U3X_FN void stage_over(uintptr_t base, void *s) {
    uint32_t *st = &field<uint32_t>(s, S_STAGE_WORDS);
    if (st[0] != 0) {
        char *stage = &at<char>(base + STAGE_NAME);
        if (stage[0] != 0) {
            lob()[14] += static_cast<uint32_t>(u3h_stage_unwant(base, u3h_crc_sid(stage)));
            stage[0] = 0;
        }
        u3h_multi_all(base, s, 0);
        u3x_net_shutdown_levels(base, s);
        st[0] = 0;
        st[1] = 0;
    }
    uint64_t *camera = &field<uint64_t>(s, S_TITLE_CAMERA);
    if (camera[1] != 0 && camera[0] != 0 && native(s, 52)) {
        camera[1] = 0;
        reinterpret_cast<void (*)(uint64_t)>(native(s, 52))(camera[0]);
    }
}

// multiplayer is active: keep the lobby stage wanted, visible and shown
// (find = native 42 as it was before the caller asked whether multiplayer is active)
U3X_FN void stage_keep(uintptr_t base, void *s, void *find) {
    using Find = void *(*)(void *manager, uint32_t sid);
    using Hash = uint64_t (*)(const char *name);
    using Want = void (*)(void *manager, void *request);
    using Show = void (*)(void *level, int on);
    using Scene = void (*)(void *scene);

    void *const hash = native(s, 40);
    void *const want = native(s, 41);
    void *const show = native(s, 45);
    if (!find || !hash || !want || !show) return;

    char *stage = &at<char>(base + STAGE_NAME);
    void *const manager = reinterpret_cast<void *>(base + LEVEL_MANAGER);
    void *const scene = reinterpret_cast<void *>(base + MENU_SCENE);
    uint32_t *st = &field<uint32_t>(s, S_STAGE_WORDS);

    u3h_multi_all(base, s, 1);
    if (u3h_stage_logout_hold(base, s, stage)) return;
    if (at<uint32_t>(base + MATCH_STATE) == 2) {
        if (!(field<uint32_t>(s, S_SWITCHES2) & 0x400000u)) u3h_match_stage_cleanup(base, s, find, show);
        return;
    }

    // once per multiplayer session: the menu scene and the proto actor package
    if (!(st[0] & ST_SCENE_ENTERED) && native(s, 46)) {
        reinterpret_cast<Scene>(native(s, 46))(scene);
        st[0] |= ST_SCENE_ENTERED;
    }
    if (!(st[0] & ST_PROTO_WANTED)) {
        u3x_actor_want(base, s, proto_actor);
        st[0] |= ST_PROTO_WANTED;
    }
    u3x_title_unwant(base, s);

    // no stage named: the helper's default
    if (stage[0] == 0) {
        for (unsigned i = 0; i < sizeof default_stage; i++) stage[i] = default_stage[i];
        st[0] |= ST_DEFAULT_NAMED;
    }

    // wanted and shown in the level want list
    const uint32_t sid = u3h_crc_sid(stage);
    uint8_t *entry = u3x_level_want_entry(base, sid);
    if (!entry) {
        Request r;
        r.hash = 0; r.sid = 0; r.number = 0; r.flags = 0; r.zero_f = 0; r.zero_10 = 0;
        r.hash = reinterpret_cast<Hash>(hash)(stage);
        r.sid = sid;
        r.flags = 3;
        reinterpret_cast<Want>(want)(manager, &r);
        entry = u3x_level_want_entry(base, sid);
    }
    uint32_t *const l = lob();
    if (entry && (entry[0xe] & 3) != 3) {
        entry[0xe] |= 3;
        l[12]++;
    }
    l[13] = entry ? entry[0xe] : 0xffu;

    // show the level once it is loaded
    void *const level = reinterpret_cast<Find>(find)(manager, sid);
    if (level && field<uint32_t>(level, LEVEL_STATE) == LEVEL_LOADED && st[1] != sid) {
        reinterpret_cast<Show>(show)(level, 1);
        field<uint8_t>(level, LEVEL_FLAGS) |= 0x10;
    }
    st[4] = level ? (field<uint16_t>(level, LEVEL_FLAGS) | (field<uint32_t>(level, LEVEL_STATE) << 16)) : 0xffffffffu;
    st[5]++;

    // the lobby camera; the stage counts as shown from here on
    if (level && field<uint32_t>(level, LEVEL_STATE) == LEVEL_LOADED && native(s, 47)) {
        if (ext_eb()[1] & 8u) reinterpret_cast<Scene>(native(s, 47))(scene);
        st[1] = sid;
    }
}

}  // namespace g39_part2

// 5.1: the lobby stage
extern "C" U3X_FN void u3x_stage_tick(uintptr_t base, void *s) {
    void *const find = g39_part2::native(s, 42);   // read first: the value of before the next call is the one used
    if (u3x_mp_lan_or_online(base)) g39_part2::stage_keep(base, s, find);
    else g39_part2::stage_over(base, s);
}

// 5.2: the four service shutdowns of NetShutdown
extern "C" U3X_FN void u3x_net_shutdown_parts(uintptr_t base, void *s) {
    u3x_emblem_shutdown(base);
    u3x_lan_shutdown(base);
    u3x_friend_shutdown(base, s);
    u3x_lobby_scene_shutdown(base);
}

// 5.3: the NetShutdown body
extern "C" U3X_FN void u3x_net_shutdown_do(uintptr_t base, void *s) {
    using namespace g39_part2;
    u3x_net_shutdown_parts(base, s);
    at<uint8_t>(base + MP_ACTIVE) = 0;
    at<uint8_t>(base + LAN_FLAG) = 0;
    field<uint32_t>(s, S_PHASE) = 0;
    field<uint32_t>(s, S_SHUTDOWNS)++;
    u3x_stage_tick(base, s);
}

// 5.4: NetShutdown when a front-end page before NetInit is on screen
extern "C" U3X_FN void u3x_net_shutdown_tick(uintptr_t base, void *s) {
    using namespace g39_part2;
    if (base == 0 || !u3x_mp_lan_or_online(base) || shutdown_blocked(base, s)) return;
    const uint32_t page = u3h_active_page(base);
    if (page != PAGE_ROOT_TILES && page != PAGE_ROOT_MAIN && page != PAGE_MP_CHOICE && page != PAGE_COOP_CHOICE) return;
    u3x_net_shutdown_do(base, s);
}

// 5.5: "Leave Multiplayer?" Yes
extern "C" U3X_FN int u3x_leave_mp_yes(uintptr_t base, void *s) {
    using namespace g39_part2;
    leave_yes_calls++;
    if (!u3x_mp_lan_or_online(base) || shutdown_blocked(base, s)) return 0;
    u3x_net_shutdown_do(base, s);
    leave_yes_shutdowns++;
    return 0;
}
