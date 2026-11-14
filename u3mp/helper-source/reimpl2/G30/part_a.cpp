// G30 part A: the small queries and tools of the post-match handler (spec G30_post_match.md, section 4).
// Written from the behaviour description only.

extern "C" {
// other groups and the C part (spec 2.4)
uint8_t *u3h_lg_profile(uintptr_t base, uint32_t user);
uint8_t *u3h_lg_rec(uintptr_t base, void *s, uint8_t *profile);
uint8_t *u3x_sym_table(void *s, uint32_t id);
int u3x_lg_beta(uintptr_t base);
int u3x_session_host(const uint8_t *session);
void u3x_menu_set_state(uintptr_t base, uint8_t *menu, uint32_t state);

// this part (spec 2.1)
uint8_t *u3x_pm_ngentry(uintptr_t base, void *s);
int32_t u3x_pm_type(uintptr_t base, void *s);
int u3x_pm_coop(uintptr_t base, void *s);
int u3x_pm_ranked(uintptr_t base, void *s);
int u3x_pm_leader_of(uintptr_t base, uintptr_t obj);
void u3x_pm_party_vcall(uintptr_t base, uint32_t off, int arg, int three);
void u3x_pm_pg_clear(uintptr_t base);
void u3x_pm_pg_push(uintptr_t base, uint32_t id);
void u3x_pm_menu_reset(uintptr_t base, uintptr_t menu);
uint32_t u3x_pm_map(uintptr_t base, void *s);
int u3x_pm_map_is(uintptr_t base, void *s, uint32_t id);
int u3x_n13_on(void);
}

namespace g30_a {

// image offsets (spec 3.2, 3.6, 3.8)
constexpr uintptr_t NGM = 0x17cb060;              // net game manager
constexpr uintptr_t MATCH_SESSION = 0x17d6420;
constexpr uintptr_t MENU = 0x17bd080;             // menu of screen 0
constexpr uintptr_t LAN_BYTE = 0x17d5c60;
constexpr uintptr_t LAN_COOP_BYTE = 0x17d6065;
constexpr uintptr_t NET_MENU_MODE = 0x17d603c;
constexpr uintptr_t DEBUG_BYTE = 0x17d7b27;
constexpr uintptr_t FIXED_KEY_BYTE = 0x17bb670;
constexpr uintptr_t PLAYLIST_ENTRY_FN = 0x73f7d0;

constexpr uint32_t NET_GAME_DEFS = 0x3b9a067du;   // script symbol: the net game definitions
constexpr uint32_t FIXED_KEY = 0x0b70ed28u;       // the playlist key while the key is fixed

// page id stack of a menu
constexpr uintptr_t STACK_DEPTH = 0xef8;
constexpr uintptr_t STACK_IDS = 0xefc;
constexpr int32_t STACK_LAST = 0x9f;

// the co-op game types (PS3 1.19's rule)
U3X_FN static bool coop_type(uint32_t type) {
    switch (type) {
    case 4: case 8: case 9: case 11: case 13: case 16: case 17: case 18:
        return true;
    default:
        return false;
    }
}

}  // namespace g30_a

// 4.1 the definition record of the current net game, or null
extern "C" U3X_FN uint8_t *u3x_pm_ngentry(uintptr_t base, void *s) {
    const int32_t index = at<int16_t>(base + g30_a::NGM + 0x661c);
    const uint8_t *table = u3x_sym_table(s, g30_a::NET_GAME_DEFS);
    if (!table || index < 0 || index >= rd<int32_t>(table, 0)) return nullptr;
    uint8_t *records = rd<uint8_t *>(table, 8);
    if (!records) return nullptr;
    return records + static_cast<uintptr_t>(index) * 0xd0;
}

// 4.2 the game type, 0 without a record
extern "C" U3X_FN int32_t u3x_pm_type(uintptr_t base, void *s) {
    const uint8_t *entry = u3x_pm_ngentry(base, s);
    return entry ? rd<int32_t>(entry, 0xc) : 0;
}

// 4.3 is the current net game a co-op game
extern "C" U3X_FN int u3x_pm_coop(uintptr_t base, void *s) {
    const uint8_t *entry = u3x_pm_ngentry(base, s);
    return entry && g30_a::coop_type(rd<uint32_t>(entry, 0xc)) ? 1 : 0;
}

// 4.4 do the results of this match count
extern "C" U3X_FN int u3x_pm_ranked(uintptr_t base, void *s) {
    const uint8_t *entry = u3x_pm_ngentry(base, s);
    const uint32_t type = entry ? rd<uint32_t>(entry, 0xc) : 0;
    const bool coop = entry && g30_a::coop_type(type);

    // a counted kind of match; the title is asked last and only when nothing else decided
    const bool counted = (coop && type != 0x12)
                      || at<int32_t>(base + g30_a::NET_MENU_MODE) == 1
                      || at<uint8_t>(base + g30_a::DEBUG_BYTE) != 0
                      || u3x_lg_beta(base) != 0;
    if (!counted) return 0;

    if (at<uint8_t>(base + g30_a::NGM + 0x6a24)) return 0;   // film playback
    if (at<uint8_t>(base + g30_a::LAN_BYTE)) return at<uint8_t>(base + g30_a::LAN_COOP_BYTE) ? 1 : 0;
    return 1;
}

// 4.5 does this session lead
extern "C" U3X_FN int u3x_pm_leader_of(uintptr_t base, uintptr_t obj) {
    return u3x_session_host(reinterpret_cast<const uint8_t *>(base + obj));
}

// 4.6 a call into the backend of the match session (always the match session; the table pointer is not tested)
extern "C" U3X_FN void u3x_pm_party_vcall(uintptr_t base, uint32_t off, int arg, int three) {
    uint8_t *session = reinterpret_cast<uint8_t *>(base + g30_a::MATCH_SESSION);
    uint8_t *backend = rd<uint8_t *>(session, 8);
    if (!backend) return;
    // the entry number is off / 8, rounded down
    const uintptr_t entry = rd<uintptr_t>(rd<const uint8_t *>(backend, 0), 8 * static_cast<uintptr_t>(off / 8));
    if (!entry) return;
    if (three) game_call<void>(entry, backend, session, arg);
    else game_call<void>(entry, backend, session);
}

// 4.7 empty the page id stack of screen 0's menu
extern "C" U3X_FN void u3x_pm_pg_clear(uintptr_t base) {
    at<int32_t>(base + g30_a::MENU + g30_a::STACK_DEPTH) = 0;
}

// 4.8 put a page id on the stack, unless the stack is full or the id is already on top
extern "C" U3X_FN void u3x_pm_pg_push(uintptr_t base, uint32_t id) {
    const uintptr_t menu = base + g30_a::MENU;
    const int32_t depth = at<int32_t>(menu + g30_a::STACK_DEPTH);
    if (depth > g30_a::STACK_LAST) return;
    // the depth counts as a signed number, also in the address of the top id
    const uintptr_t top = menu + g30_a::STACK_IDS + static_cast<uintptr_t>(static_cast<intptr_t>(depth) * 4);
    if (at<uint32_t>(top) == id) return;
    at<int32_t>(menu + g30_a::STACK_DEPTH) = depth + 1;
    at<uint32_t>(top + 4) = id;
}

// 4.9 put a menu back to state 5, unless it is in state 0, 5 or 6
extern "C" U3X_FN void u3x_pm_menu_reset(uintptr_t base, uintptr_t menu) {
    uint8_t *m = reinterpret_cast<uint8_t *>(base + menu);
    const uint32_t state = rd<uint32_t>(m, 8);
    if (state == 0 || state == 5 || state == 6) return;
    u3x_menu_set_state(base, m, 5);
}

// 4.10 the playlist key: the fixed one, or the one user 0's profile picked (always user 0)
extern "C" U3X_FN uint32_t u3x_pm_map(uintptr_t base, void *s) {
    if (at<uint8_t>(base + g30_a::FIXED_KEY_BYTE)) return g30_a::FIXED_KEY;
    const uint8_t *record = u3h_lg_rec(base, s, u3h_lg_profile(base, 0));
    return rd<uint32_t>(record, 0x52c);
}

// 4.11 is the playlist entry of the current key the one with this key
extern "C" U3X_FN int u3x_pm_map_is(uintptr_t base, void *s, uint32_t id) {
    const uint32_t key = u3x_pm_map(base, s);
    const uint8_t *entry = game_call<const uint8_t *>(base + g30_a::PLAYLIST_ENTRY_FN,
                                                      reinterpret_cast<uint8_t *>(base + g30_a::NGM), key);
    return entry && rd<uint32_t>(entry, 0) == id ? 1 : 0;
}

// 4.12 the search re-arm is on unless bit 8 of switch word ec[3] is set
extern "C" U3X_FN int u3x_n13_on(void) {
    const uint32_t switches = rd<uint32_t>(u3h_ext(), 0x4450 + 4 * 3);
    return (switches & 0x100u) ? 0 : 1;
}
