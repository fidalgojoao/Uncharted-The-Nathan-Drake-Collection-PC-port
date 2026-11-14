// G13 part 1: how online play is entered and initialised. Play Online menu action, LAN chosen, the sign-in wait of
// the multiplayer startup, the online NetInit, the backend object's setup and the connection table reset.
// Written from the behaviour description spec/G13_online_backend_in.md (sections 4.1 to 4.6).

extern "C" {
// the C part and other groups (spec 2.3)
int u3h_onb_shell_route(uintptr_t base);
int u3h_online_config(void *s);
int u3h_online_cfg_ok(void);
int32_t u3h_online_status(void);
int u3h_smn_up(void);
int u3h_rc_signing_in(void);
int32_t u3h_rc_state(void);
int u3h_rc_server_ready(void);
int u3h_srv_retry_pending(void);
uint32_t u3h_profile_state(int *has_memory);
int u3h_onp_pending(void);
void u3h_profile_retry(void);
int u3h_offline_ok(void);
void u3h_go_offline(void);
void u3h_set_onb_mode(int mode);
int u3h_onb_mode_get(void);
int u3h_k01_play_online(uintptr_t base, void *s);
void u3h_k01_lan(void);
uint32_t u3h_k01_back_page(void);
int u3h_show(uintptr_t base, void *s, uint32_t page, int silent);
void u3x_online_failed(uintptr_t base, void *s);
void u3h_so_note(const char *a, const char *name, int32_t v, const char *b);
void u3h_onp_name_apply(uintptr_t base);
void u3h_k43_step(uintptr_t base, uint32_t v);
int32_t u3h_gt_open(uintptr_t base);
void u3h_pl_open(uintptr_t base);
void *u3h_onb_object(void);
void **u3h_onb_vt(void);
void u3x_onb_vtable(void);
void u3h_onb_addr_reset(void);
void u3x_cm_reset_entry(uint8_t *entry);

// this part
void u3x_play_online(uintptr_t base, void *s);
void u3x_lan_chosen(void);
int u3x_signin_wait(uintptr_t base, void *s);
int u3x_online_netinit(uintptr_t base, void *s);
void *u3x_onb_setup(uintptr_t base);
void u3x_cm_online_reset(uintptr_t base);
}

namespace g13_p1 {

// game data (image offsets)
constexpr uintptr_t LOCAL_CLOCK = 0x17b4ba4;      // s32, ticks of 1/300 second
constexpr uintptr_t MENU_UI_STATE = 0x17bd080 + 8;
constexpr uintptr_t OWN_NAME = 0x17d6010;         // 0x24 bytes
constexpr uintptr_t SPLIT_COOP = 0x17d6065;
constexpr uintptr_t MATCH_SESSION = 0x17d6420;
constexpr uintptr_t PARTY_SESSION = 0x17dc298;
constexpr uintptr_t LAN_BACKEND = 0x17dd758;
constexpr uintptr_t ONLINE_BACKEND = 0x17dd760;
constexpr uintptr_t SESSION_MANAGER = 0x17dd768;
constexpr uintptr_t CONN_TABLE = 0x17f31b0;
constexpr uintptr_t SESSION_INIT = 0x7a2360;      // game function (session)

constexpr uint32_t NAME_SIZE = 0x24;
constexpr uint32_t BACKEND_SIZE = 0x1060;
constexpr uint32_t PAGE_CHOOSE_PROFILE = 0x4608c1ad;
constexpr uint32_t SYM_POOL_IDS = 0x8653fbe2;

// state object fields
constexpr uintptr_t S_PROFILE_COUNT = 128, S_PROFILE_SELECTED = 132, S_PROFILE_NAMES = 136, S_SHOW_PAGE = 648,
                    S_SYMBOL_LOOKUP = 656, S_STARTUP_PHASE = 680;

// eb[] words
constexpr int EB_OBJECT = 128, EB_SETUPS = 129, EB_INIT_RESULT = 130, EB_STARTUP = 150, EB_WAIT_CALLS = 151;

U3X_RO static const char NOTE_NO_SETTINGS[] =
    "Play Online stopped (No connection found): no sign-in settings and no sign-in running";
U3X_RO static const char NOTE_PROFILE_RETRY[] =
    "Play Online: the profile download failed earlier; trying it again (waits up to 30 s)";
U3X_RO static const char NOTE_TIMED_OUT[] =
    "Play Online stopped (No connection found): sign-in not finished in time, RPCN state ";

// kept between calls, zero when the helper is loaded
U3X_BSS static int32_t wait_start;    // clock when the current wait began
U3X_BSS static int32_t wait_last;     // clock at the last wait call
U3X_BSS static int32_t retry_asked;   // clock when the profile retry was last asked
U3X_BSS static uint32_t pool_random;  // state of the pool pick's generator

[[gnu::always_inline]] inline bool matchmaking_on() { return (ext_eb()[0] & 0x10u) != 0; }

// ticks from `then` to `now`, as a signed 32-bit difference
[[gnu::always_inline]] inline int32_t ticks_since(int32_t now, int32_t then) {
    return static_cast<int32_t>(static_cast<uint32_t>(now) - static_cast<uint32_t>(then));
}

// entry `index` of the backend object's function table
[[gnu::always_inline]] inline uintptr_t backend_entry(const void *be, int index) {
    return rd<uintptr_t>(rd<const void *>(be, 0), static_cast<uintptr_t>(index) * 8);
}

U3X_FN static void copy_bytes(uint8_t *to, const uint8_t *from, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) to[i] = from[i];
}

// the selected profile's name becomes the own network name: at most 16 characters and one NUL
U3X_FN static void apply_profile_name(uintptr_t base, const void *s) {
    const uint32_t selected = rd<uint32_t>(s, S_PROFILE_SELECTED);
    if (selected == 0 || selected > rd<uint32_t>(s, S_PROFILE_COUNT)) return;
    const char *profile = static_cast<const char *>(s) + S_PROFILE_NAMES + 64 * static_cast<uintptr_t>(selected - 1);
    char *own = &at<char>(base + OWN_NAME);
    int n = 0;
    while (n < 16 && profile[n]) { own[n] = profile[n]; ++n; }
    own[n] = 0;
}

// the party session gets a game pool id picked at random from the script's list
U3X_FN static void pick_pool(uintptr_t base, const void *s, uint8_t *party) {
    const uintptr_t lookup = rd<uintptr_t>(s, S_SYMBOL_LOOKUP);
    if (!lookup) return;
    const void *const *symbol = game_call<const void *const *>(lookup, SYM_POOL_IDS);
    if (!symbol || !*symbol) return;
    const uint32_t count = rd<uint32_t>(*symbol, 0);
    const uint32_t *ids = rd<const uint32_t *>(*symbol, 8);
    if (count == 0 || !ids) return;
    if (pool_random == 0) pool_random = static_cast<uint32_t>(at<int32_t>(base + LOCAL_CLOCK)) | 1u;
    pool_random = pool_random * 1103515245u + 12345u;
    *reinterpret_cast<uint32_t *>(party + 0x14) = ids[(pool_random >> 8) % count];
}

}  // namespace g13_p1

extern "C" {

// 4.1 the Play Online menu action
U3X_FN void u3x_play_online(uintptr_t base, void *s) {
    using namespace g13_p1;
    const uint32_t ui_state = at<uint32_t>(base + MENU_UI_STATE);
    if (ui_state == 7 || ui_state == 8) return;   // the menu is changing pages
    if (u3h_onb_shell_route(base)) return;

    const int settings = u3h_online_config(s);
    const bool not_possible = (!settings && u3h_smn_up() != 1 && !u3h_rc_signing_in()) || !matchmaking_on();
    if (!not_possible) {
        u3h_set_onb_mode(1);
    } else if (u3h_offline_ok()) {
        u3h_go_offline();
    } else {
        u3h_so_note(NOTE_NO_SETTINGS, nullptr, -1, nullptr);
        u3x_online_failed(base, s);
        return;
    }

    at<uint8_t>(base + SPLIT_COOP) = 0;
    if (u3h_k01_play_online(base, s)) return;     // the shortcut started the startup itself
    if (rd<const void *>(s, S_SHOW_PAGE)) u3h_show(base, s, PAGE_CHOOSE_PROFILE, 0);
}

// 4.2 the player chose LAN play
U3X_FN void u3x_lan_chosen(void) {
    u3h_set_onb_mode(0);
    u3h_k01_lan();
}

// 4.3 once per frame in the startup's phase 1: 1 = still waiting, 0 = go on with NetInit, -1 = failed
U3X_FN int u3x_signin_wait(uintptr_t base, void *s) {
    using namespace g13_p1;
    if (u3h_onb_mode_get() != 1 || !matchmaking_on()) return 0;

    apply_profile_name(base, s);
    u3h_onp_name_apply(base);

    // the wait's clock: a wait that was not continued for more than 3 s starts over
    uint32_t *eb = ext_eb();
    const int32_t now = at<int32_t>(base + LOCAL_CLOCK);
    eb[EB_WAIT_CALLS] += 1;
    if (eb[EB_STARTUP] != 1 || ticks_since(now, wait_last) > 900) {
        eb[EB_STARTUP] = 1;
        wait_start = now;
    }
    wait_last = now;
    const int32_t waited = ticks_since(now, wait_start);

    // signed in, but the profile download has not finished (or failed): hold up to 30 s
    int has_memory = 0;
    const uint32_t profile = u3h_profile_state(&has_memory);
    if ((u3h_online_status() == 2 || u3h_smn_up() == 1) && has_memory && u3h_rc_server_ready() &&
        (profile == 0 || profile == 3) && waited < 9000) {
        if (profile == 3 && ticks_since(now, retry_asked) > 1500) {
            retry_asked = now;
            u3h_profile_retry();
            u3h_so_note(NOTE_PROFILE_RETRY, nullptr, -1, nullptr);
        }
        const uint32_t steps = static_cast<uint32_t>(waited) / 900;
        u3h_k43_step(base, 8 + (steps < 10 ? steps : 10));
        return 1;
    }

    // signed in
    if (u3h_online_status() == 2 || (u3h_smn_up() == 1 && !u3h_onp_pending())) {
        eb[EB_STARTUP] = 2;
        return 0;
    }

    // still coming
    if (((u3h_online_cfg_ok() && u3h_online_status() != 3 && u3h_online_status() != 4) || u3h_smn_up() == 2) &&
        waited < 3000)
        return 1;
    if ((u3h_rc_signing_in() || (u3h_smn_up() == 1 && u3h_onp_pending())) && waited < 9000) return 1;
    if (u3h_srv_retry_pending() && waited < 45000) return 1;

    // offline lobby instead
    if (u3h_offline_ok()) {
        eb[EB_STARTUP] = 7;
        u3h_go_offline();
        return 0;
    }

    // failed: stop the startup, go back and show the network failure dialog
    eb[EB_STARTUP] = 3;
    *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(s) + S_STARTUP_PHASE) = 0;
    u3h_so_note(NOTE_TIMED_OUT, nullptr, u3h_rc_state(), nullptr);
    if (rd<const void *>(s, S_SHOW_PAGE)) u3h_show(base, s, u3h_k01_back_page(), 1);
    u3x_online_failed(base, s);
    return -1;
}

// 4.5 builds the online backend object and registers it as the session manager
U3X_FN void *u3x_onb_setup(uintptr_t base) {
    using namespace g13_p1;
    uint32_t *eb = ext_eb();
    eb[EB_SETUPS] += 1;
    u3x_onb_vtable();
    u3h_onb_addr_reset();

    uint8_t *object = static_cast<uint8_t *>(u3h_onb_object());
    for (uint32_t i = 0; i < BACKEND_SIZE; ++i) object[i] = 0;
    *reinterpret_cast<void ***>(object) = u3h_onb_vt();
    *reinterpret_cast<uint64_t *>(object + 0x1058) = base;
    eb[EB_OBJECT] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(object));

    at<void *>(base + LAN_BACKEND) = nullptr;
    at<void *>(base + ONLINE_BACKEND) = object;
    at<void *>(base + SESSION_MANAGER) = object;
    copy_bytes(object + 8, &at<uint8_t>(base + OWN_NAME), NAME_SIZE);
    return object;
}

// 4.6 sets the game's connection table up for online play
U3X_FN void u3x_cm_online_reset(uintptr_t base) {
    using namespace g13_p1;
    uint8_t *table = &at<uint8_t>(base + CONN_TABLE);
    table[0x4db4] = 0;                                              // mode: online
    copy_bytes(table + 0x4d90, &at<uint8_t>(base + OWN_NAME), NAME_SIZE);
    for (uint32_t i = 0; i < 32; ++i) u3x_cm_reset_entry(table + 8 + i * 0x220);
    *reinterpret_cast<uint32_t *>(table + 0x4408) = 0;              // blocked names
    *reinterpret_cast<int32_t *>(table) = 0x1234;                   // next id
}

// 4.4 the online branch of NetInit: 0 = done, -1 = failed
U3X_FN int u3x_online_netinit(uintptr_t base, void *s) {
    using namespace g13_p1;
    void *be = u3x_onb_setup(base);
    uint32_t *eb = ext_eb();

    const int64_t init = game_call<int64_t>(backend_entry(be, 0), be);
    eb[EB_INIT_RESULT] = static_cast<uint32_t>(init);
    if (init != 0) {
        eb[EB_STARTUP] = 4;
        return -1;
    }

    // a fresh party session and party state
    uint8_t *match = &at<uint8_t>(base + MATCH_SESSION);
    uint8_t *party = &at<uint8_t>(base + PARTY_SESSION);
    game_call<void>(base + SESSION_INIT, party);
    *reinterpret_cast<uint8_t *>(party + 0x1334) = 0;
    *reinterpret_cast<uint32_t *>(party + 0x1338) = 0;
    *reinterpret_cast<uint64_t *>(party + 0x1340) = 0;
    *reinterpret_cast<uint8_t *>(party + 0x1348) = 0;
    *reinterpret_cast<uint32_t *>(party + 0x1370) = 0;
    *reinterpret_cast<uint8_t *>(party + 0x1374) = 0;
    *reinterpret_cast<uint8_t *>(party + 0x1375) = 0;

    // both sessions attached to the backend; the party's room attribute set
    game_call<int64_t>(backend_entry(be, 3), be, match);
    game_call<int64_t>(backend_entry(be, 3), be, party);
    game_call<int64_t>(backend_entry(be, 8), be, party, static_cast<int32_t>(1));

    pick_pool(base, s, party);

    if (u3h_gt_open(base) != 0) {
        eb[EB_STARTUP] = 5;
        return -1;
    }
    u3x_cm_online_reset(base);
    u3h_pl_open(base);
    eb[EB_STARTUP] = 6;
    return 0;
}

}  // extern "C"
