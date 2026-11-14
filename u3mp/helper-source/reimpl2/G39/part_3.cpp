// G39 part 3: the multiplayer startup (begin, the per-frame startup tick with the NetInit step, the net services).
// Written from spec G39_mp_startup.md, sections 6.1 to 6.6 (facts: section 3).

extern "C" {
// part 1 and part 2 of this group
void u3x_net_init_levels(uintptr_t base, void *s);
void u3x_net_shutdown_do(uintptr_t base, void *s);
// other groups and the C part (spec 2.4)
int u3h_register_lobby_click(uintptr_t base);
int u3h_show(uintptr_t base, void *s, uint32_t page, int silent);
uint32_t u3h_k01_back_page(void);
void u3h_k43_step(uintptr_t base, uint32_t v);
int u3h_k43_gate(uintptr_t base, void *s);
int u3h_onb_signin_wait(uintptr_t base, void *s);
int u3h_onb_lan_flag(void);
void u3h_ngm_init(uintptr_t base);
void u3h_onp_name_apply(uintptr_t base);
void u3h_cin_netinit(uintptr_t base, void *s);
void u3x_friend_init(uintptr_t base);
void u3x_lb_init(uintptr_t base, void *s);
void u3h_fi_netinit(uintptr_t base, void *s);
void u3h_lnb_netinit(uintptr_t base, void *s);
int u3h_onb_netinit(uintptr_t base, void *s);
void u3h_mp_profile_start(uintptr_t base, void *s);
int u3h_onp_fail_take(void);
void u3h_so_note(const char *a, const char *name, int32_t v, const char *b);
void u3x_online_failed(uintptr_t base, void *s);
int u3h_emb_netinit(uintptr_t base, void *s);

void u3x_begin_startup(uintptr_t base, void *s);
void u3x_startup_tick(uintptr_t base, void *s);
void u3x_netinit_services(uintptr_t base, void *s);
}

namespace g39p3 {

// ---- the helper's state object (spec 3.4)
constexpr uintptr_t S_DIALOG_PHASE = 76, S_PROFILE_COUNT = 128, S_PROFILE_SELECTED = 132, S_PROFILE_NAMES = 136;
constexpr uintptr_t S_SHOW_PAGE = 648, S_SYMBOL_LOOKUP = 656, S_MODULE_LOADER = 664, S_POOL_SETUP = 672;
constexpr uintptr_t S_PHASE = 680, S_ERROR = 684, S_TICKS = 688, S_NATIVES = 768;
constexpr uintptr_t S_PROBE_MARKER = 1280, S_PROBE_RUN = 1284, S_PROBE_MASK_LOW = 1472, S_PROBE_MASK_HIGH = 1476;
constexpr uintptr_t S_PROBE_ALLOCATOR = 1480, S_OWN_CLOCK = 0x2000, S_SWITCHES = 0x20c0, S_SWITCHES_2 = 0x3d78;

enum : uint32_t { PHASE_IDLE = 0, PHASE_NETINIT = 1, PHASE_NET_LEVEL = 2, PHASE_LOBBY = 3, PHASE_ERROR = 4 };
enum : uint32_t { ERR_NO_CONNECTING_PAGE = 1, ERR_NO_NET_MODULE = 2, ERR_NO_POOLS = 3, ERR_NO_LOBBY_PAGE = 4 };

// ---- game data (spec 3.1)
constexpr uintptr_t G_ABORT = 0x16e7170, G_ALL_STORE_ITEMS = 0x17b2a94, G_CLOCK_3 = 0x17b4b98, G_CLOCK_7 = 0x17b4c98;
constexpr uintptr_t G_MENU_UI_STATE = 0x17bd080 + 8, G_MENU_SECOND_USER = 0x17bde75, G_MENU_USER = 0x17bf714;
constexpr uintptr_t G_EMBLEM_STATE = 0x17c3440, G_NET_CLOCK = 0x17c3580, G_CHARACTER_MANAGER = 0x17c3b40;
constexpr uintptr_t G_LAN_FLAG = 0x17d5c60, G_MP_ACTIVE = 0x17d5c61, G_OWN_NAME = 0x17d6010, G_MATCH_SESSION = 0x17d6420;
constexpr uintptr_t G_PROFILES = 0x17dd610, G_ALLOCATORS = 0x18417b0, G_LEVEL_MANAGER = 0x1afcf10;

// ---- ids (spec 3.7)
constexpr uint32_t PAGE_CONNECTING = 0x9e1863b1, PAGE_LOBBY = 0x53cbb5f2;
constexpr uint32_t SYM_NET_SCRIPT = 0xbd3bb3b9, SID_NET = 0x4240ef2e, NET_LOAD_TAG = 0x12;
constexpr uint32_t SYM_MP_CHARACTERS = 0x916e03b0, PROBE_MARKER = 0x424f5250;

U3X_RO const char net_name[] = "net";
U3X_RO const char note_download_failed[] =
    "Play Online stopped (No connection found): the online profile could not be downloaded, state ";

// a level request block (spec 3.2), 0x18 bytes
struct LevelRequest {
    uint64_t hash;
    uint32_t sid;
    uint16_t number;
    uint8_t flags;
    uint8_t zero[9];
};

template <typename T> [[gnu::always_inline]] inline T &field(void *s, uintptr_t off) {
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(s) + off);
}
[[gnu::always_inline]] inline uintptr_t native(void *s, int k) { return field<uintptr_t>(s, S_NATIVES + 8 * k); }

// the net script symbol is there when the lookup answers a pointer to a non-null pointer
U3X_FN bool net_script_present(void *s) {
    const void *const *slot = game_call<const void *const *>(field<uintptr_t>(s, S_SYMBOL_LOOKUP), SYM_NET_SCRIPT);
    return slot && *slot;
}

// 6.3 step 7: asks for the "net" level unless the level manager already knows it
U3X_FN void want_net_level(uintptr_t base, void *s) {
    const uintptr_t find = native(s, 42), hash = native(s, 40), request = native(s, 41);
    void *manager = reinterpret_cast<void *>(base + G_LEVEL_MANAGER);
    if (!find || !hash || !request) return;
    if (game_call<void *>(find, manager, SID_NET)) return;
    LevelRequest block;
    uint8_t *bytes = reinterpret_cast<uint8_t *>(&block);
    for (unsigned i = 0; i < sizeof block; i++) bytes[i] = 0;
    block.hash = game_call<uint64_t>(hash, static_cast<const char *>(net_name));
    block.sid = SID_NET;
    block.flags = 0x0a;
    game_call<void>(request, manager, &block);
}

// 6.3 step 10: the selected profile's name becomes the own network name (16 characters at most)
U3X_FN void copy_profile_name(uintptr_t base, void *s) {
    const uint32_t selected = field<uint32_t>(s, S_PROFILE_SELECTED);
    if (selected == 0 || selected > field<uint32_t>(s, S_PROFILE_COUNT)) return;
    const char *from = &field<char>(s, S_PROFILE_NAMES + 64 * static_cast<uintptr_t>(selected - 1));
    char *to = &at<char>(base + G_OWN_NAME);
    int n = 0;
    while (n < 16 && from[n]) { to[n] = from[n]; n++; }
    to[n] = 0;
}

// 6.3 step 14: a backslash in the menu user's clan tag becomes a blank
U3X_FN void clean_clan_tag(uintptr_t base) {
    uint8_t *profiles = at<uint8_t *>(base + G_PROFILES);
    if (!profiles) return;
    const uint32_t user = at<uint8_t>(base + G_MENU_SECOND_USER) ? 1u : at<uint32_t>(base + G_MENU_USER);
    if (user >= 4) return;
    uint8_t *tag = profiles + static_cast<uintptr_t>(user) * 0x7b00 + 0xac0;
    for (int i = 0; i < 4; i++)
        if (tag[i] == 0x5c) tag[i] = 0x20;
}

// 6.4: the two network allocators (table entries 0x14, 0x15) share the first general one that exists
U3X_FN void fill_net_allocators(uintptr_t base, void *s) {
    uintptr_t *table = &at<uintptr_t>(base + G_ALLOCATORS);
    field<uint32_t>(s, S_PROBE_MASK_LOW) = 0;
    field<uint32_t>(s, S_PROBE_MASK_HIGH) = 0;
    field<uint32_t>(s, S_PROBE_MARKER) = PROBE_MARKER;
    field<uint32_t>(s, S_PROBE_RUN) = 1;
    for (uint32_t i = 0; i < 64; i++)
        if (table[i]) field<uint32_t>(s, i < 32 ? S_PROBE_MASK_LOW : S_PROBE_MASK_HIGH) |= 1u << (i & 31);

    // the helper's own order of preference: 9, 5, 7, 0x13
    uint32_t pick = 9;
    if (!table[pick]) pick = 5;
    if (!table[pick]) pick = 7;
    if (!table[pick]) pick = 0x13;
    if (!table[pick]) return;
    if (!table[0x14]) {
        table[0x14] = table[pick];
        field<uint32_t>(s, S_PROBE_ALLOCATOR) = pick;
    }
    if (!table[0x15]) table[0x15] = table[pick];
}

// 6.5: the net clock is engine clock 7, or the helper's own clock started from the local clock
U3X_FN void set_net_clock(uintptr_t base, void *s) {
    if (!(field<uint32_t>(s, S_SWITCHES_2) & 0x80000000u)) {
        at<uintptr_t>(base + G_NET_CLOCK) = base + G_CLOCK_7;
        return;
    }
    uint8_t *own = &field<uint8_t>(s, S_OWN_CLOCK);
    const uint8_t *local = &at<uint8_t>(base + G_CLOCK_3);
    for (int i = 0; i < 0x40; i++) {
        // zero: the time (8 bytes at 0xc), u32 at 0x18, u16 at 0x1c, the four u32 from 0x20
        const bool cleared = (i >= 0xc && i < 0x14) || (i >= 0x18 && i < 0x1e) || (i >= 0x20 && i < 0x30);
        own[i] = cleared ? 0 : local[i];
    }
    at<uintptr_t>(base + G_NET_CLOCK) = reinterpret_cast<uintptr_t>(own);
}

U3X_FN void stop_with_error(void *s, uint32_t error) {
    field<uint32_t>(s, S_ERROR) = error;
    field<uint32_t>(s, S_PHASE) = PHASE_ERROR;
}

// 6.3: NetInit, one attempt per menu frame while the startup phase is 1
U3X_FN void netinit_step(uintptr_t base, void *s) {
    if (u3h_onb_signin_wait(base, s)) return;
    u3h_k43_step(base, 0);

    // the net script module
    if (!net_script_present(s)) {
        uint32_t tag = NET_LOAD_TAG;
        game_call<void>(field<uintptr_t>(s, S_MODULE_LOADER), SID_NET, &tag);
        if (!net_script_present(s)) { stop_with_error(s, ERR_NO_NET_MODULE); return; }
    }
    u3h_k43_step(base, 5);

    // the net pools
    if (!game_call<int>(field<uintptr_t>(s, S_POOL_SETUP), base)) { stop_with_error(s, ERR_NO_POOLS); return; }

    at<uint8_t>(base + G_LAN_FLAG) = static_cast<uint8_t>(u3h_onb_lan_flag());
    at<uint8_t>(base + G_MP_ACTIVE) = 1;
    at<uint8_t>(base + G_ALL_STORE_ITEMS) = 1;

    want_net_level(base, s);
    u3h_ngm_init(base);
    u3x_net_init_levels(base, s);
    copy_profile_name(base, s);
    u3h_onp_name_apply(base);
    u3h_cin_netinit(base, s);
    u3x_netinit_services(base, s);
    clean_clan_tag(base);
    if (!(field<uint32_t>(s, S_SWITCHES) & 8)) fill_net_allocators(base, s);
    set_net_clock(base, s);
    at<uint8_t>(base + G_ABORT) = 0;

    // the game's session initialisation, on the match session
    if (const uintptr_t session_init = native(s, 12))
        game_call<void>(session_init, reinterpret_cast<void *>(base + G_MATCH_SESSION));

    // the handle of the multiplayer character list, when it has no symbol yet
    const uintptr_t characters = base + G_CHARACTER_MANAGER;
    if (at<uint32_t>(characters + 0x2c) == 0) {
        at<uint32_t>(characters + 0x2c) = SYM_MP_CHARACTERS;
        at<uint64_t>(characters + 0x30) = 0;
        at<int32_t>(characters + 0x28) = -1;
    }
    u3h_k43_step(base, 7);

    // the backend
    if (u3h_onb_lan_flag()) u3h_lnb_netinit(base, s);
    else u3h_onb_netinit(base, s);
    u3h_k43_step(base, 43);

    u3h_mp_profile_start(base, s);
    if (u3h_onp_fail_take()) {
        // the online profile could not be downloaded: leave multiplayer again and say so
        ext_eb()[150] = 8;
        const int32_t state = static_cast<int32_t>(rd<uint32_t>(u3h_ext(), 0x4450 + 4 * 101));
        u3h_so_note(note_download_failed, nullptr, state, nullptr);
        u3x_net_shutdown_do(base, s);
        if (field<uintptr_t>(s, S_SHOW_PAGE)) u3h_show(base, s, u3h_k01_back_page(), 1);
        u3x_online_failed(base, s);
        return;
    }

    // emblems: the game's own init unless the emblem group has done it or it is switched off
    if (!u3h_emb_netinit(base, s) && !(field<uint32_t>(s, S_SWITCHES_2) & 0x40000000u)) {
        if (const uintptr_t emblem_init = native(s, 11)) {
            at<uint8_t>(base + G_EMBLEM_STATE) = 1;
            game_call<void>(emblem_init, reinterpret_cast<void *>(base + G_EMBLEM_STATE));
        }
    }
    u3h_k43_step(base, 44);
    field<uint32_t>(s, S_PHASE) = PHASE_NET_LEVEL;
}

}  // namespace g39p3

// 6.1: the player has a profile: show the connecting page and let the startup tick run NetInit
extern "C" U3X_FN void u3x_begin_startup(uintptr_t base, void *s) {
    using namespace g39p3;
    if (!field<uintptr_t>(s, S_SHOW_PAGE)) return;
    if (field<uint32_t>(s, S_PHASE) != PHASE_IDLE) return;
    const uint32_t selected = field<uint32_t>(s, S_PROFILE_SELECTED);
    if (selected == 0 || selected > field<uint32_t>(s, S_PROFILE_COUNT)) return;

    u3h_register_lobby_click(base);
    field<uint32_t>(s, S_PHASE) = PHASE_NETINIT;
    field<uint32_t>(s, S_TICKS) = 0;
    field<uint32_t>(s, S_ERROR) = 0;
    u3h_k43_step(base, 0);
    if (!u3h_show(base, s, PAGE_CONNECTING, 0)) {
        field<uint32_t>(s, S_PHASE) = PHASE_IDLE;
        field<uint32_t>(s, S_ERROR) = ERR_NO_CONNECTING_PAGE;
    }
}

// 6.2: one step of the startup per menu frame
extern "C" U3X_FN void u3x_startup_tick(uintptr_t base, void *s) {
    using namespace g39p3;
    const uint32_t phase = field<uint32_t>(s, S_PHASE);
    if (phase == PHASE_IDLE || phase >= PHASE_LOBBY) return;
    const uint32_t ui_state = at<uint32_t>(base + G_MENU_UI_STATE);
    if (ui_state == 7 || ui_state == 8) return;   // the menu is changing pages
    field<uint32_t>(s, S_TICKS) += 1;

    if (phase == PHASE_NETINIT) {
        netinit_step(base, s);
        return;
    }
    // waiting for the "net" level, then the lobby opens over the page the startup goes back to
    if (u3h_k43_gate(base, s)) return;
    field<uint32_t>(s, S_PHASE) = PHASE_LOBBY;
    field<uint32_t>(s, S_DIALOG_PHASE) = 0;
    if (!u3h_show(base, s, u3h_k01_back_page(), 1) || !u3h_show(base, s, PAGE_LOBBY, 0)) {
        field<uint32_t>(s, S_ERROR) = ERR_NO_LOBBY_PAGE;
        field<uint32_t>(s, S_PHASE) = PHASE_ERROR;
    }
}

// 6.6: friends, leaderboards, invites
extern "C" U3X_FN void u3x_netinit_services(uintptr_t base, void *s) {
    u3x_friend_init(base);
    u3x_lb_init(base, s);
    u3h_fi_netinit(base, s);
}
