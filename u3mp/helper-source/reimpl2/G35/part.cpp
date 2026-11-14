// G35: custom game start (checks, countdown, preparation, immediate start), late join entry, the Elimination
// late-join gate and the Elimination "waiting for new players" hold and text. Written from
// spec/G35_custom_game_start.md.

extern "C" {
// other groups and the C part (spec 2.2)
int u3h_mp_code_is(uintptr_t base, uint32_t off, const char *hex12);
void u3h_show_dialog(uintptr_t base, void *s, uint32_t body, uint32_t title, uint32_t action, int32_t buttons,
                     uint32_t extra, uint32_t close);
void u3h_cg_log(const char *text, int32_t value);
void u3h_fj_note(const char *before, const uint8_t *id, const char *after, int32_t value);
void u3h_jip_started(void);
uint32_t *u3h_pt_state(void);
uint32_t *u3h_cg_t0_ptr(void);
int u3h_onb_startmatch(uintptr_t base, void *s);
int u3h_m03_startmatch(uintptr_t base, void *s);
void u3h_cg_commit_selection(uintptr_t base, void *s);
void u3h_start_calls_inc(void *s);
void u3h_probe_pad2_inc(void *s);
volatile uint8_t *u3h_elim_received(void);
volatile uint8_t *u3h_elim_send(void);   // new C export (integrator)
int32_t u3x_netgame_type(uintptr_t base, void *s);
int u3x_lan_party_size(uintptr_t base);
void u3x_lan_reattach(uintptr_t base);
int u3x_mp_lan_or_online(uintptr_t base);
int u3x_party_guest(const uint8_t *party);
int u3x_round_mostly_over(int32_t limit, int32_t elapsed);
int u3x_match_nearly_decided(int32_t lead_wins, int32_t rounds, int32_t round);
int u3x_waiting_joiner(const uint8_t *net_player);

// this group
void u3x_ng_limits(uintptr_t base, int32_t type, int32_t *most, int32_t *fewest);
int u3x_popup_timed(uintptr_t base, void *s, uint32_t text, int seconds, uint32_t ok, uint32_t cancel);
int u3x_start_checks(uintptr_t base, void *s);
void u3x_start_game(uintptr_t base, void *s);
void u3x_start_prepare(uintptr_t base, void *s);
void u3x_start_game_now(uintptr_t base, void *s);
void u3x_jip_enter(uintptr_t base);
int u3x_latejoin_closed(uintptr_t base, void *s, uint64_t *r);
void u3x_elim_wait_set(uintptr_t base);
int u3x_elim_round_end_hold(uintptr_t base, uint64_t *r);
void u3x_elim_wait_text(uintptr_t base);
}

namespace g35 {

// ---- image offsets
constexpr uintptr_t GAME_CLOCK = 0x17b4ba4, NET_CLOCK = 0x17b4ca4, MATCH_START = 0x17d5c64, NET_ACTIVE = 0x17d5c61;
constexpr uintptr_t NET_MENU_MODE = 0x17d603c, NET_STATE = 0x1b68810, NET_STATE_SINCE = 0x1b68814;
constexpr uintptr_t JIP_STATUS = 0x1b69530, ABORT_FLAG = 0x16e7170, NET_ERROR = 0x17d5c80, NET_CLOCK_OBJ = 0x17c3580;
constexpr uintptr_t LOBBY_NETGAME = 0x17d167c, NET_OBJECT = 0x17d5c58, NG_MANAGER = 0x17cb060;
constexpr uintptr_t TEAM_WINS = 0x17e338e, MATCH_SESSION = 0x17d6420, PARTY_SESSION = 0x17dc298, MENU = 0x17bd080;
constexpr uintptr_t MOST_PLAYERS = 0x13a61e0, TEXT_TABLE = 0x17026b8, HUD = 0x1805fa0;
constexpr uintptr_t HUD_IN_USE_A = 0x1806480, HUD_IN_USE_B = 0x1b24bd8;
// game functions
constexpr uint32_t FN_JIP_SETUP = 0x74ba60, FN_MATCH_SETUP = 0x72b570, FN_SHOW_PAGE = 0x651940;
constexpr uint32_t FN_ROUND_LIMIT = 0x726b00, FN_LEADING_TEAM = 0x737eb0, FN_TEXT = 0xf32ae0, FN_STRNCPY = 0x139c740;
// ids
constexpr uint32_t PAGE_TIMED = 0x453470dd, PAGE_LATE_JOIN = 0x69e10dad;
constexpr uint32_t TEXT_COUNTDOWN = 0x73a73dd8, ACT_COUNTDOWN_DONE = 0xe2707dbe, ACT_COUNTDOWN_CANCEL = 0xbf6ce4d5;
constexpr uint32_t DIALOG_TITLE = 0xc6467f2d, DIALOG_CLOSE = 0xe1e040aa, TEXT_ELIM_WAIT = 0x02277bd7;
constexpr uint32_t MAP_LIST_SYMBOL = 0x0dec97a4;
constexpr int32_t ELIMINATION = 10;
// lobby switches (s + 0x3d78)
constexpr uint32_t SW_KEEP_KILLVIS = 8, SW_NO_COUNTDOWN = 0x40, SW_LONE_START = 0x80, SW_LONE_START_18 = 0x20000;
constexpr uint32_t SW_NO_MINIMUM = 0x80000, SW_NO_RESET = 0x20000000;

U3X_RO const char L1[] = "Start pressed, net mode ";
U3X_RO const char L2[] = "only the party leader can start (this PC is a party member)";
U3X_RO const char L3[] = "already starting, net state ";
U3X_RO const char L4[] = "a start check refused it (2 party too big, 3 too few players, 4 teams): ";
U3X_RO const char L5[] = "checks passed, countdown next; party size ";
U3X_RO const char L6[] = "join in progress: late join started (0x31), status ";
U3X_RO const char K[] = "san-upper-multi-killvis";
U3X_RO const char CODE_ROUND_LIMIT[] = "554889e54157415641545349";
U3X_RO const char CODE_LEADING_TEAM[] = "554889e54157415641545348";
U3X_RO const char CODE_TEXT[] = "554889e541565389f34989fe";
U3X_RO const char CODE_STRNCPY[] = "ff25826c3400681a000000e9";

// P: game clock of the last start the countdown path issued (0 = none yet)
U3X_BSS uint32_t last_start;

template <typename T> [[gnu::always_inline]] inline T &fld(void *p, uintptr_t off) {
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + off);
}
[[gnu::always_inline]] inline uint8_t *img(uintptr_t base, uintptr_t off) { return reinterpret_cast<uint8_t *>(base + off); }

[[gnu::always_inline]] inline uint32_t *cd() { return reinterpret_cast<uint32_t *>(u3h_ext() + 0x2238); }
[[gnu::always_inline]] inline uint32_t *ec() { return reinterpret_cast<uint32_t *>(u3h_ext() + 0x4450); }
[[gnu::always_inline]] inline uint32_t lobby_switches(void *s) { return fld<uint32_t>(s, 0x3d78); }
[[gnu::always_inline]] inline bool starting(uintptr_t base) {
    const uint32_t st = at<uint32_t>(base + NET_STATE);
    return st == 3 || st == 0xb;
}

using StartFn = void (*)();

// native 7 when a custom game may be started with it, else null
U3X_FN StartFn start_function(uintptr_t base, void *s) {
    const StartFn fn = fld<StartFn>(s, 824);
    if (!fn) return nullptr;
    if (at<uint32_t>(base + NET_MENU_MODE) != 2) return nullptr;
    if (!u3x_mp_lan_or_online(base)) return nullptr;
    return fn;
}

U3X_FN int refuse(uintptr_t base, void *s, uint32_t code, uint32_t body, uint32_t action) {
    cd()[6] |= code;
    u3h_show_dialog(base, s, body, DIALOG_TITLE, action, 0, 0, DIALOG_CLOSE);
    return 0;
}

U3X_FN bool same_text(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

}  // namespace g35

// ---------------------------------------------------------------------------------------------- part A

// 4.1: most players from the game's own table, fewest from the rule
extern "C" U3X_FN void u3x_ng_limits(uintptr_t base, int32_t type, int32_t *most, int32_t *fewest) {
    if (type < 0 || type > 19) {
        *most = 0;
        *fewest = 0;
        return;
    }
    const int32_t m = at<int32_t>(base + g35::MOST_PLAYERS + 4u * static_cast<uint32_t>(type));
    *most = m;
    *fewest = (m == 3 || m == 4 || type == g35::ELIMINATION) ? 2 : 1;
}

// 4.2: the menu's timed popup with our two action ids
extern "C" U3X_FN int u3x_popup_timed(uintptr_t base, void *s, uint32_t text, int seconds, uint32_t ok, uint32_t cancel) {
    using namespace g35;
    using ShowPage = uint64_t (*)(void *menu, uint32_t page, int a, int silent);
    uint8_t *const menu = img(base, MENU);
    uint8_t *const data = fld<uint8_t *>(menu, 0x14f0);
    const ShowPage show = fld<ShowPage>(s, 648);
    if (!data || !show) return 0;

    const uint32_t now = at<uint32_t>(base + GAME_CLOCK);
    fld<uint32_t>(data, 8) = text;
    fld<uint32_t>(data, 0x10) = now;
    fld<uint32_t>(data, 0x14) = now + static_cast<uint32_t>(seconds) * 300u;

    uint32_t *const w = u3h_pt_state();
    if (w[0] == 0) {   // keep the menu's own ids for pt_tick
        w[1] = fld<uint32_t>(menu, 0x1544);
        w[2] = fld<uint32_t>(menu, 0x1548);
    }
    w[0] = 1;
    w[3] = ok;
    w[4] = 0;

    fld<uint32_t>(menu, 0x1544) = ok;
    fld<uint32_t>(menu, 0x1548) = cancel;

    uint32_t *const c = cd();
    c[1] = now;
    c[2] = text;
    return static_cast<int>(show(menu, PAGE_TIMED, 0, 0) & 0xff);
}

// 4.3: may the party start the selected net game
extern "C" U3X_FN int u3x_start_checks(uintptr_t base, void *s) {
    using namespace g35;
    const int32_t type = u3x_netgame_type(base, s);
    const int32_t n = u3x_lan_party_size(base);
    int32_t most = 0, fewest = 0;
    u3x_ng_limits(base, type, &most, &fewest);

    uint32_t *const c = cd();
    c[7] = static_cast<uint32_t>(n) | ((static_cast<uint32_t>(type) & 0xff) << 8) | (static_cast<uint32_t>(fewest) << 16) |
           (static_cast<uint32_t>(most) << 24);
    c[6] = 0;

    if (n > most) return refuse(base, s, 2, 0x4a936499, 0x13cabc15);

    const uint32_t z = lobby_switches(s);
    const bool alone = n == 1 && (z & SW_LONE_START) && (type != 18 || (z & SW_LONE_START_18));
    const bool no_minimum = (z & SW_NO_MINIMUM) != 0;
    if (no_minimum && n < fewest) ++*reinterpret_cast<uint32_t *>(u3h_ext() + 0x2ff0 + 4);
    if (!alone && !no_minimum && n < fewest) return refuse(base, s, 3, 0x4a93e6b9, 0x13cafe1f);
    if (type != ELIMINATION || alone) return 1;

    // Elimination: the members must not all be on one team
    uint8_t *const party = img(base, PARTY_SESSION);
    const bool teams_unknown = (fld<uint32_t>(s, 0x20c0) & 4) != 0;
    uint32_t members = 0, t1 = 0, t2 = 0, t3 = 0, t4 = 0;
    for (int i = 0; i < 10; ++i) {
        uint8_t *const slot = party + 0x680 + static_cast<uintptr_t>(i) * 0x120;
        if (!fld<uint8_t>(slot, 0x25)) continue;
        if (teams_unknown) {
            ++members;
            continue;
        }
        const uint8_t *data;
        if (fld<uint64_t>(party, 0x18) == 0 || fld<uint32_t>(slot, 0x2c) == fld<uint32_t>(party, 0x12e4)) {
            if (fld<uint32_t>(party, 0x12f0) != 0x40) continue;
            data = party + 0x12f4;
        } else {
            if (fld<uint32_t>(slot, 0x3c) != 0x40) continue;
            data = slot + 0x40;
        }
        ++members;
        switch (data[9]) {
            case 1: ++t1; break;
            case 2: ++t2; break;
            case 3: ++t3; break;
            case 4: ++t4; break;
            default: break;
        }
    }
    c[6] = (members << 8) | ((t1 | (t2 << 4) | (t3 << 8) | (t4 << 12)) << 16);
    if (t1 == members || t2 == members || t3 == members || t4 == members)
        return refuse(base, s, 4, 0x101c7f10, 0x5350146e);
    return 1;
}

// 4.5: gets the game ready for a match start
extern "C" U3X_FN void u3x_start_prepare(uintptr_t base, void *s) {
    using namespace g35;
    u3h_start_calls_inc(s);

    // the helper's own net clock object, when the game uses it, starts again
    uint8_t *const own_clock = static_cast<uint8_t *>(s) + 0x2000;
    if (at<uint8_t *>(base + NET_CLOCK_OBJ) == own_clock) {
        fld<uint64_t>(own_clock, 0xc) = 0;
        fld<uint32_t>(own_clock, 0x20) = 0;
        fld<uint32_t>(own_clock, 0x28) = 0;
    }

    if (!(lobby_switches(s) & SW_NO_RESET)) {
        at<uint8_t>(base + ABORT_FLAG) = 0;
        at<uint8_t>(base + NET_ERROR) = 0;
        using SessionInit = uint64_t (*)(void *session);
        if (const SessionInit init = fld<SessionInit>(s, 864)) init(img(base, MATCH_SESSION));
    }

    // the kill-volume level of the maps is switched off unless the switch keeps it
    using Lookup = uint8_t *(*)(uint32_t symbol);
    if (const Lookup lookup = fld<Lookup>(s, 872)) {
        uint8_t *const list = lookup(MAP_LIST_SYMBOL);
        uint8_t *const entries = list ? fld<uint8_t *>(list, 8) : nullptr;
        if (entries) {
            uint32_t count = fld<uint32_t>(list, 0);
            if (count > 64) count = 64;
            for (uint32_t i = 0; i < count; ++i) {
                uint8_t *const e = entries + static_cast<uintptr_t>(i) * 0x98;
                const char *const name = fld<const char *>(e, 0x38);
                if (!name) continue;
                if (same_text(name, K) && !(lobby_switches(s) & SW_KEEP_KILLVIS)) {
                    fld<uint32_t>(e, 0x40) = 0;
                    u3h_probe_pad2_inc(s);
                }
                // the switches are read again here, after the call above
                uint32_t *const kept = reinterpret_cast<uint32_t *>(u3h_ext() + 0x2070 + 44);
                *kept = (lobby_switches(s) & SW_KEEP_KILLVIS) ? 1 : 0;
            }
        }
    }

    u3x_lan_reattach(base);
}

// 4.6: start without checks and countdown
extern "C" U3X_FN void u3x_start_game_now(uintptr_t base, void *s) {
    using namespace g35;
    const StartFn start = start_function(base, s);
    if (!start || starting(base)) return;
    u3x_start_prepare(base, s);
    start();
}

// 4.4: the lobby's Start button
extern "C" U3X_FN void u3x_start_game(uintptr_t base, void *s) {
    using namespace g35;
    uint8_t *const party = img(base, PARTY_SESSION);
    uint8_t *const menu = img(base, MENU);

    ++cd()[0];
    u3h_cg_log(L1, at<int32_t>(base + NET_MENU_MODE));
    if (u3x_party_guest(party)) {
        u3h_cg_log(L2, -1);
        return;
    }
    if (u3h_onb_startmatch(base, s)) return;
    if (u3h_m03_startmatch(base, s)) return;
    u3h_cg_commit_selection(base, s);
    if (lobby_switches(s) & SW_NO_COUNTDOWN) {
        u3x_start_game_now(base, s);
        return;
    }

    const StartFn start = start_function(base, s);
    if (!start) return;

    const uint32_t now = at<uint32_t>(base + GAME_CLOCK);
    if (last_start != 0 && now - last_start < 9000u && starting(base)) {
        u3h_cg_log(L3, at<int32_t>(base + NET_STATE));
        return;
    }
    if (uint8_t *const popup = fld<uint8_t *>(menu, 0x898))
        if (fld<uint32_t>(popup, 0x10) == PAGE_TIMED) return;   // the countdown is already up

    if (!u3x_start_checks(base, s)) {
        u3h_cg_log(L4, static_cast<int32_t>(cd()[6] & 0xff));
        return;
    }
    u3h_cg_log(L5, u3x_lan_party_size(base));
    *u3h_cg_t0_ptr() = now ? now : 1;

    u3x_start_prepare(base, s);

    // the party leader locks the party: entry 8 of the backend's function table (coordinator's decision)
    if (uint8_t *const backend = fld<uint8_t *>(party, 8)) {
        if (fld<uint64_t>(party, 0x18) == 0 || fld<uint32_t>(party, 0x12e4) == fld<uint32_t>(party, 0x12e8)) {
            using SetAttribute = uint64_t (*)(void *backend, void *session, int value);
            uint8_t *const table = fld<uint8_t *>(backend, 0);
            if (const SetAttribute lock = fld<SetAttribute>(table, 8 * 8)) {
                lock(backend, party, 1);
                ++cd()[3];
            }
        }
    }

    fld<uint8_t>(menu, 0x269a) = 0;
    const uint32_t again = at<uint32_t>(base + GAME_CLOCK);
    last_start = again ? again : 1;

    if (u3x_popup_timed(base, s, TEXT_COUNTDOWN, 5, ACT_COUNTDOWN_DONE, ACT_COUNTDOWN_CANCEL)) {
        ++cd()[4];
    } else {
        ++cd()[5];
        start();
    }
}

// ---------------------------------------------------------------------------------------------- part B

// 5.1: puts the game into its late-join flow
extern "C" U3X_FN void u3x_jip_enter(uintptr_t base) {
    using namespace g35;
    uint8_t *const manager = img(base, NG_MANAGER);
    uint8_t *const net = img(base, NET_OBJECT);

    fld<int16_t>(manager, 0x661c) = at<int16_t>(base + LOBBY_NETGAME);
    game_call<void>(base + FN_JIP_SETUP);
    at<uint8_t>(base + MATCH_SESSION + 0x1338) = 0;
    fld<uint32_t>(net, 0x408) = 4;
    game_call<void, void *, int, int, int, int, int>(base + FN_MATCH_SETUP, manager, 1, 1, 0, 0, 0);

    const uint32_t now = at<uint32_t>(base + GAME_CLOCK);
    fld<int32_t>(net, 0x2c) = static_cast<int32_t>(now + 9000u);
    game_call<void, void *, uint32_t, int, int>(base + FN_SHOW_PAGE, img(base, MENU), PAGE_LATE_JOIN, 0, 0);
    at<uint32_t>(base + NET_STATE) = 0x31;
    at<uint32_t>(base + NET_STATE_SINCE) = now;

    u3h_jip_started();
    u3h_fj_note(L6, nullptr, nullptr, at<int32_t>(base + JIP_STATUS));
}

// 5.2: "is late join closed?" for Elimination; every other type is left to the game
extern "C" U3X_FN int u3x_latejoin_closed(uintptr_t base, void *s, uint64_t *r) {
    using namespace g35;
    uint32_t *const e = ec();
    ++e[18];
    if (e[0] & 2) return 0;
    uint8_t *const manager = img(base, NG_MANAGER);
    if (r[4] != reinterpret_cast<uintptr_t>(manager)) return 0;
    if (u3x_netgame_type(base, s) != ELIMINATION) return 0;
    if (!u3h_mp_code_is(base, FN_ROUND_LIMIT, CODE_ROUND_LIMIT)) return 0;
    if (!u3h_mp_code_is(base, FN_LEADING_TEAM, CODE_LEADING_TEAM)) return 0;

    // closed when less than a quarter of the round is left ...
    uint64_t area = 0;
    game_call<void, void *, void *>(base + FN_ROUND_LIMIT, &area, manager);
    const int32_t limit = static_cast<int32_t>(static_cast<uint32_t>(area));
    const int32_t elapsed =
        static_cast<int32_t>(at<uint32_t>(base + NET_CLOCK) - at<uint32_t>(base + MATCH_START));
    int closed = u3x_round_mostly_over(limit, elapsed);

    // ... or when the match is nearly decided
    if (!closed) {
        const int32_t lead = game_call<int32_t, void *>(base + FN_LEADING_TEAM, manager);
        const int16_t wins = at<int16_t>(
            static_cast<uintptr_t>(static_cast<int64_t>(base + TEAM_WINS) + static_cast<int64_t>(lead) * 0x130));
        const int32_t rounds = fld<int32_t>(manager, 0x6a44);
        const int32_t round = fld<int32_t>(manager, 0x66fc);
        e[21] = static_cast<uint32_t>(lead);
        e[22] = static_cast<uint32_t>(static_cast<int32_t>(wins));
        e[23] = (static_cast<uint32_t>(round) & 0xffff) | (static_cast<uint32_t>(rounds) << 16);
        closed = u3x_match_nearly_decided(wins, rounds, round);
    }

    ++e[19];
    e[20] = static_cast<uint32_t>(closed);
    r[8] = static_cast<uint64_t>(static_cast<uint32_t>(closed));
    return 1;
}

// 5.3: "waiting for new players" on, and the party is to be told
extern "C" U3X_FN void u3x_elim_wait_set(uintptr_t base) {
    using namespace g35;
    *u3h_elim_send() = 1;
    uint8_t *const manager = img(base, NG_MANAGER);
    if (fld<uint8_t>(manager, 0x354)) return;
    fld<uint8_t>(manager, 0x354) = 1;
    fld<int32_t>(manager, 0x358) = static_cast<int32_t>(at<uint32_t>(base + NET_CLOCK) + 18000u);
    ++ec()[26];
}

// 5.4: the round end is held back while a late joiner waits to spawn
extern "C" U3X_FN int u3x_elim_round_end_hold(uintptr_t base, uint64_t *r) {
    using namespace g35;
    uint32_t *const e = ec();
    ++e[24];
    if (e[0] & 4) return 0;

    uint8_t *const manager = img(base, NG_MANAGER);
    bool held = false;
    for (int i = 0; i < 10; ++i) {
        if (!at<uint8_t>(base + NET_ACTIVE)) continue;
        if (!u3x_waiting_joiner(manager + 0x390 + static_cast<uintptr_t>(i) * 0x9c0)) continue;
        held = true;
        if (!fld<uint8_t>(manager, 0x354)) u3x_elim_wait_set(base);
    }
    if (!held) return 0;
    ++e[25];
    r[8] = 0;
    return 1;
}

// 5.5: the HUD message of the Elimination wait
extern "C" U3X_FN void u3x_elim_wait_text(uintptr_t base) {
    using namespace g35;
    if (ec()[0] & 4) return;

    volatile uint8_t *const received = u3h_elim_received();
    if (*received) {   // the host announced the wait: a guest sets it and does not announce it again
        *received = 0;
        u3x_elim_wait_set(base);
        *u3h_elim_send() = 0;
    }
    if (!at<uint8_t>(base + NG_MANAGER + 0x354)) return;

    uint8_t *const block = at<uint8_t *>(base + HUD + 0x238);
    if (!block) return;
    uint8_t *const msg = block + 0x25f0;
    if (msg[0]) return;   // a message is up

    if (!u3h_mp_code_is(base, FN_TEXT, CODE_TEXT)) return;
    if (!u3h_mp_code_is(base, FN_STRNCPY, CODE_STRNCPY)) return;

    uint8_t *const table = img(base, TEXT_TABLE);
    const char *text = game_call<const char *, void *, uint32_t>(base + FN_TEXT, table, TEXT_ELIM_WAIT);
    if (!text) text = fld<const char *>(table, 0x10);
    if (!text) return;

    at<uint8_t *>(base + HUD_IN_USE_A) = img(base, HUD);
    at<uint8_t *>(base + HUD_IN_USE_B) = img(base, HUD);
    game_call<void, void *, const char *, uint64_t>(base + FN_STRNCPY, msg, text, 0x80);
    fld<uint8_t>(msg, 0x7f) = 0;
    fld<uint32_t>(msg, 0x84) = 0xffffffffu;
    fld<uint32_t>(msg, 0x80) = at<uint32_t>(base + GAME_CLOCK);
    fld<uint32_t>(msg, 0x88) = 0;
    fld<uint32_t>(msg, 0x8c) = 0;
    fld<uint8_t>(msg, 0x90) = 0;
    ++ec()[27];
}
