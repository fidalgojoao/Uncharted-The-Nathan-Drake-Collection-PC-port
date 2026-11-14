// G32: menu actions (catch-all dispatcher, lobby rows, Custom Game entry, LAN choice, profile list, MOTD OK, quit
// count). Written from spec G32_menu_actions.md.

extern "C" {
// C part and other groups (spec 2.2)
int u3h_show(uintptr_t base, void *s, uint32_t page, int silent);
void u3h_show_dialog(uintptr_t base, void *s, uint32_t body, uint32_t title, uint32_t action, int32_t buttons,
                     uint32_t extra, uint32_t close);
void u3h_lan_chosen(void);
int u3h_offline_find(uintptr_t base, void *s);
int u3h_party_send(uintptr_t base, uint32_t message, const void *data, uint32_t length);
uint8_t *u3h_nmh(uintptr_t base);
int u3h_lan_backend(void);
void *u3h_online_backend(uintptr_t base);
int u3h_rpm_catch(uintptr_t base, void *s, uint32_t id);
int u3h_fr_catch(uintptr_t base, void *s, uint32_t id, uintptr_t report_callback);
int u3h_start_match_catch(uintptr_t base, void *s, uint32_t id);
int u3h_film_action(uintptr_t base, const int32_t *ev);
int u3h_community_catch(uintptr_t base, void *s, uint32_t id);
void u3h_treasure_first_row(void *s);
void u3h_motd_ok(uintptr_t base, void *s);
void u3h_mission_take(uintptr_t base, void *s);
void u3h_party_kick_promote(uintptr_t base, int promote);
void u3h_report_popup(uintptr_t base, void *s);
void u3h_report_pick(uintptr_t base, void *s, uint32_t row);
void u3h_view_stats(uintptr_t base, void *s);
void u3h_lan_delete_profile(uintptr_t base, void *s, uint32_t action);
const uint32_t *u3h_lobby_rows(uintptr_t base, uint32_t *n);
void u3h_lnb_find_party(uintptr_t base);
void u3h_splitscreen(uintptr_t base, void *s);
uint8_t *u3h_lg_profile(uintptr_t base, uint32_t user);
uint8_t *u3h_lg_rec(uintptr_t base, void *s, uint8_t *profile);
int u3h_publish(uintptr_t base, void *s, uint8_t *profile);
void u3x_cinema(uintptr_t base, void *s);
int u3x_count_quit(uint8_t *record);

// this group
void u3x_lan_choice(uintptr_t base, const int32_t *ev, void *s);
void u3x_lobby_action(uintptr_t base, void *s, uint32_t action);
int u3x_start_match_catch(uintptr_t base, void *s, uint32_t id);
void u3x_custom_game(uintptr_t base, void *s);
void u3x_menu_action(uintptr_t base, const int32_t *ev, void *s);
int u3x_profile_list_op(uintptr_t base, int32_t *ev, void *s);
void u3x_lobby_row(uintptr_t base, const int32_t *ev, void *s);
void u3x_motd_ok(uintptr_t base, void *s);
void u3x_mp_quit(uintptr_t base, void *s);
}

namespace g32 {

// ---- pages
constexpr uint32_t PAGE_LAN_PROFILES = 0x4608c1adu;
constexpr uint32_t PAGE_MULTIPLAYER = 0x53cbb5f2u;
constexpr uint32_t PAGE_PLAYLISTS = 0x0b7714b5u;
constexpr uint32_t PAGE_COMMUNITY = 0xdb53e8ffu;
constexpr uint32_t PAGE_MATCHMAKING = 0xe35f0f1au;
constexpr uint32_t PAGE_LOBBY = 0xb99a3235u;
constexpr uint32_t PAGE_LAN_PARTIES = 0xbb3595bcu;
constexpr uint32_t PAGE_MEDALS = 0xc98ee52au;
constexpr uint32_t PAGE_TREASURES = 0xda577facu;
constexpr uint32_t PAGE_RECORDS = 0x6281f21fu;
constexpr uint32_t PAGE_UNLOCKS = 0x0c3a3716u;
constexpr uint32_t PAGE_MISSIONS = 0xdfdb36fau;
constexpr uint32_t TAB_MATCHMAKING = 0xea2119f2u;

// ---- the "no second user" dialog
constexpr uint32_t TEXT_NO_SECOND_USER = 0x7c31f4d0u;
constexpr uint32_t TITLE_NO_SECOND_USER = 0xc6467f2du;

// ---- lobby row actions
constexpr uint32_t ROW_CUSTOM_GAME = 0x7d593924u;
constexpr uint32_t ROW_FIND_PARTY = 0x52f3d9f6u;
constexpr uint32_t ROW_PROFILE = 0xc4acc827u;
constexpr uint32_t ROW_OPTIONS = 0xe7d7dbbbu;
constexpr uint32_t ROW_CINEMA = 0xb85cb12au;
constexpr uint32_t ROW_SPLITSCREEN = 0xcd127c29u;
constexpr uint32_t ROW_FIND_GAME = 0x649e809au;
constexpr uint32_t ROW_COMMUNITY = 0x8891e664u;

// ---- catch-all actions
constexpr uint32_t ACT_START_MATCH = 0xa0448333u;
constexpr uint32_t ACT_PARTY_ROW_CLICK = 0x3406420cu;
constexpr uint32_t ACT_MEDALS = 0x9db19163u;
constexpr uint32_t ACT_TREASURES = 0xe9bce205u;
constexpr uint32_t ACT_RECORDS = 0x420f5e96u;
constexpr uint32_t ACT_UNLOCKS = 0x58dda3cdu;
constexpr uint32_t ACT_MISSIONS = 0xe4794ab9u;
constexpr uint32_t ACT_MOTD_OK = 0xab58f9dau;
constexpr uint32_t ACT_MISSION_TAKE = 0xea1f40c5u;
constexpr uint32_t ACT_LOCK_PARTY = 0xccbf832eu;
constexpr uint32_t ACT_UNLOCK_PARTY = 0x0d0c91cdu;
constexpr uint32_t ACT_KICK = 0x7cc46b94u;
constexpr uint32_t ACT_PROMOTE = 0x2d32c8b0u;
constexpr uint32_t ACT_REPORT = 0x0ecbd5bdu;
constexpr uint32_t ACT_VIEW_STATS = 0x089143eau;
constexpr uint32_t ACT_DELETE_PROFILE = 0x6d6ad418u;
constexpr uint32_t ACT_DELETE_PROFILE_YES = 0xda18d7d3u;
constexpr uint32_t ACT_REPORT_REASON_FIRST = 0x7ebae89cu;
constexpr uint32_t REPORT_REASONS = 8;

constexpr uint32_t MSG_MATCHMAKING_STARTS = 0x118;
constexpr uint32_t NET_STATE_MATCHMAKING_STARTS = 20;

// ---- game data (image offsets)
constexpr uintptr_t G_CLOCK = 0x17b4ba4;
constexpr uintptr_t G_MENU = 0x17bd080;
constexpr uintptr_t G_UI_STATE = 0x17bd088;
constexpr uintptr_t G_MENU_USER = 0x17bf714;
constexpr uintptr_t G_PARTY_LOCKED = 0x17c3570;
constexpr uintptr_t G_NET_GAME_MANAGER = 0x17cb060;
constexpr uintptr_t G_NET_MENU_MODE = 0x17d603c;
constexpr uintptr_t G_COOP_FLAG = 0x17d6065;
constexpr uintptr_t G_MATCH_SESSION = 0x17d6420;
constexpr uintptr_t G_PARTY_SESSION = 0x17dc298;
constexpr uintptr_t G_PROFILES = 0x17dd610;
constexpr uintptr_t G_NET_STATE = 0x1b68810;
constexpr uintptr_t G_NET_STATE_TIME = 0x1b68814;
constexpr uintptr_t G_HIGHLIGHTED_PROFILE = 0x1b6a400;

// ---- game functions (image offsets)
constexpr uintptr_t FN_MENU_SOUND = 0x651fa0;
constexpr uintptr_t FN_REBUILD_HISTORY = 0x6530e0;
constexpr uintptr_t FN_REFRESH_PLAYER = 0x6b7b30;
constexpr uintptr_t FN_SET_TAB = 0x751aa0;
constexpr uintptr_t FN_MENU_BACK = 0x652200;

// ---- session fields and backend slots
constexpr uintptr_t SES_BACKEND = 0x08;
constexpr uintptr_t SES_ROOM = 0x18;
constexpr uintptr_t SES_LEAVE_DONE = 0xc8;
constexpr uintptr_t SES_OWN_MEMBER = 0x12e4;
constexpr uintptr_t SES_HOST_MEMBER = 0x12e8;
constexpr uintptr_t SES_LEAVE_ASKED = 0x1334;
constexpr int SLOT_ATTACH = 3;
constexpr int SLOT_LEAVE = 7;

// ---- helper state block words
constexpr uintptr_t EXT_COOP_WORDS = 0x100;
constexpr uint32_t SWITCH_OFF = 2;   // bit 1 of ea[0] / ea[1]
constexpr int EA_DELETE_SWITCH = 0, EA_119_SWITCH = 1, EA_MOTD_OK = 36, EA_QUITS = 37, EA_REPORT_HANDLER = 96;
constexpr int EB_LOBBY_ACTION = 168, EB_START_MATCH = 169;

// ---- state object fields
constexpr uintptr_t S_ROW_PRESSED = 84;
constexpr uintptr_t S_PROFILE_COUNT = 128;
constexpr uintptr_t S_PROFILE_SELECTED = 132;
constexpr uintptr_t S_PROFILE_NAMES = 136;
constexpr uintptr_t S_SHOW_PAGE = 648;
constexpr uintptr_t S_CUSTOM_GAME_RESULT = 728;
constexpr uintptr_t S_NATIVES = 768;
constexpr uintptr_t S_SWITCH_WORD = 0x20c0;
constexpr uintptr_t S_CATCH_COUNTER = 0x2428;
constexpr uint32_t SWITCH_COOP_NEEDS_USER = 0x10;

template <typename T> [[gnu::always_inline]] inline T &field(void *p, uintptr_t off) {
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + off);
}
[[gnu::always_inline]] inline bool can_show(void *s) { return rd<uintptr_t>(s, S_SHOW_PAGE) != 0; }
[[gnu::always_inline]] inline uintptr_t native(void *s, int k) { return rd<uintptr_t>(s, S_NATIVES + 8 * k); }

// calls slot k of a backend object's function table with (backend, session)
[[gnu::always_inline]] inline void backend_slot(void *backend, int k, void *session) {
    const uintptr_t table = rd<uintptr_t>(backend, 0);
    game_call<int64_t>(rd<uintptr_t>(reinterpret_cast<void *>(table), 8 * k), backend, session);
}

}  // namespace g32

// 4.1 the LAN / Splitscreen choice
U3X_FN void u3x_lan_choice(uintptr_t base, const int32_t *ev, void *s) {
    using namespace g32;
    const uint32_t ui = at<uint32_t>(base + G_UI_STATE);
    if (ui == 7 || ui == 8) return;   // the menu is changing pages
    const uint64_t context = rd<uint64_t>(ev, 8);
    if (context != 0 && (rd<uint32_t>(s, S_SWITCH_WORD) & SWITCH_COOP_NEEDS_USER)) {
        u3h_show_dialog(base, s, TEXT_NO_SECOND_USER, TITLE_NO_SECOND_USER, 0, 0, 0, 0);
        return;
    }
    u3h_lan_chosen();
    uint32_t *coop = reinterpret_cast<uint32_t *>(u3h_ext() + EXT_COOP_WORDS);
    const uint32_t split = context != 0 ? 1 : 0;
    coop[0]++;
    coop[1] = static_cast<uint32_t>(context);
    at<uint8_t>(base + G_COOP_FLAG) = static_cast<uint8_t>(split);
    coop[2] = split;
    if (can_show(s)) u3h_show(base, s, PAGE_LAN_PROFILES, 0);
}

// 4.2 Find Game / Community rows of the online lobby
U3X_FN void u3x_lobby_action(uintptr_t base, void *s, uint32_t action) {
    using namespace g32;
    ext_eb()[EB_LOBBY_ACTION] = action;
    if (!can_show(s)) return;
    if (action == ROW_FIND_GAME) {
        if (u3h_offline_find(base, s)) return;
        at<uint32_t>(base + G_NET_MENU_MODE) = 1;
        at<uint32_t>(base + G_MENU_USER) = 0;
        u3h_show(base, s, PAGE_PLAYLISTS, 0);
    } else if (action == ROW_COMMUNITY) {
        u3h_show(base, s, PAGE_COMMUNITY, 0);
    }
}

// 4.3 Start Match in an online party
U3X_FN int u3x_start_match_catch(uintptr_t base, void *s, uint32_t id) {
    using namespace g32;
    if (id != ACT_START_MATCH) return 0;
    ext_eb()[EB_START_MATCH]++;
    uint8_t *party = reinterpret_cast<uint8_t *>(base + G_PARTY_SESSION);
    uint8_t *match = reinterpret_cast<uint8_t *>(base + G_MATCH_SESSION);
    const bool leader = rd<uint64_t>(party, SES_ROOM) == 0 ||
                        rd<uint32_t>(party, SES_OWN_MEMBER) == rd<uint32_t>(party, SES_HOST_MEMBER);
    if (leader) {
        // tell the members which match room to follow, then let the game's net machine start matchmaking
        uint64_t room = rd<uint8_t>(match, SES_LEAVE_DONE) != 0 ? rd<uint64_t>(match, SES_ROOM) : 0;
        u3h_party_send(base, MSG_MATCHMAKING_STARTS, &room, 8);
        at<uint32_t>(base + G_NET_STATE) = NET_STATE_MATCHMAKING_STARTS;
        at<int32_t>(base + G_NET_STATE_TIME) = at<int32_t>(base + G_CLOCK);
        if (can_show(s)) u3h_show(base, s, PAGE_MATCHMAKING, 0);
        game_call<void>(base + FN_SET_TAB, PAGE_MATCHMAKING, TAB_MATCHMAKING);
    } else {
        party[SES_LEAVE_ASKED] = 1;
        game_call<void>(base + FN_MENU_BACK, reinterpret_cast<void *>(base + G_MENU), static_cast<int32_t>(0));
    }
    void *backend = rd<void *>(match, SES_BACKEND);
    if (backend && rd<uintptr_t>(backend, 0) != 0) backend_slot(backend, SLOT_LEAVE, match);
    return 1;
}

// 4.4 the lobby's Custom Game row
U3X_FN void u3x_custom_game(uintptr_t base, void *s) {
    using namespace g32;
    uint32_t &result = field<uint32_t>(s, S_CUSTOM_GAME_RESULT);
    const uintptr_t refresh_types = native(s, 1), select_net_game = native(s, 2), select_type = native(s, 3);
    if (!refresh_types || !select_net_game || !select_type) { result = 1; return; }
    if (at<uintptr_t>(base + G_PROFILES) == 0) { result = 2; return; }
    at<uint32_t>(base + G_NET_MENU_MODE) = 2;
    uint8_t *types = u3h_nmh(base);
    game_call<void>(refresh_types, types);
    if (types[0] == 0) { result = 3; return; }
    const int32_t net_game = at<uint8_t>(base + G_COOP_FLAG) != 0 ? 0 : 6;
    game_call<void>(select_net_game, reinterpret_cast<void *>(base + G_NET_GAME_MANAGER), net_game,
                    static_cast<void *>(nullptr), static_cast<uint32_t>(0xc));
    game_call<void>(select_type, types, static_cast<int32_t>(0));
    // online play: the match session joins the online backend when it has none yet
    void *online = u3h_online_backend(base);
    const int lan = u3h_lan_backend();
    uint8_t *match = reinterpret_cast<uint8_t *>(base + G_MATCH_SESSION);
    if (lan == 0 && online && rd<uint64_t>(match, SES_BACKEND) == 0) backend_slot(online, SLOT_ATTACH, match);
    result = 0;
    if (u3h_show(base, s, PAGE_LOBBY, 0) == 0) result = 4;
}

// 4.5 the catch-all for menu actions without a handler of their own
U3X_FN void u3x_menu_action(uintptr_t base, const int32_t *ev, void *s) {
    using namespace g32;
    const uint32_t action = static_cast<uint32_t>(ev[0]);
    if (u3h_rpm_catch(base, s, action)) return;
    field<uint64_t>(s, S_CATCH_COUNTER) += action == ACT_PARTY_ROW_CLICK ? 1u : 0x10000u;
    const uint32_t report_handler = ext_ea()[EA_REPORT_HANDLER];
    if (report_handler != 0 && u3h_fr_catch(base, s, action, base + report_handler)) return;
    if (u3h_start_match_catch(base, s, action)) return;
    if (u3h_film_action(base, ev)) return;
    if (u3h_community_catch(base, s, action)) return;

    switch (action) {
    case ACT_MEDALS: u3h_show(base, s, PAGE_MEDALS, 0); break;
    case ACT_TREASURES:
        u3h_treasure_first_row(s);
        u3h_show(base, s, PAGE_TREASURES, 0);
        break;
    case ACT_RECORDS: u3h_show(base, s, PAGE_RECORDS, 0); break;
    case ACT_UNLOCKS: u3h_show(base, s, PAGE_UNLOCKS, 0); break;
    case ACT_MISSIONS: u3h_show(base, s, PAGE_MISSIONS, 0); break;
    case ACT_MOTD_OK: u3h_motd_ok(base, s); break;
    case ACT_MISSION_TAKE: u3h_mission_take(base, s); break;
    case ACT_LOCK_PARTY: at<uint8_t>(base + G_PARTY_LOCKED) = 1; break;
    case ACT_UNLOCK_PARTY: at<uint8_t>(base + G_PARTY_LOCKED) = 0; break;
    case ACT_KICK: u3h_party_kick_promote(base, 0); break;
    case ACT_PROMOTE: u3h_party_kick_promote(base, 1); break;
    case ACT_REPORT: u3h_report_popup(base, s); break;
    case ACT_VIEW_STATS: u3h_view_stats(base, s); break;
    case ACT_DELETE_PROFILE:
    case ACT_DELETE_PROFILE_YES: u3h_lan_delete_profile(base, s, action); break;
    default: {
        const uint32_t reason = action - ACT_REPORT_REASON_FIRST;
        if (reason < REPORT_REASONS) u3h_report_pick(base, s, reason);
        break;
    }
    }
}

// 4.6 the list of the LAN profile page: row 0 is the game's "new profile" row, rows 1 .. count the saved profiles
U3X_FN int u3x_profile_list_op(uintptr_t base, int32_t *ev, void *s) {
    using namespace g32;
    const uint32_t count = rd<uint32_t>(s, S_PROFILE_COUNT);
    const bool follow = (ext_ea()[EA_DELETE_SWITCH] & SWITCH_OFF) == 0;
    const uint32_t row = static_cast<uint32_t>(ev[1]);
    uint32_t &selected = field<uint32_t>(s, S_PROFILE_SELECTED);
    uint32_t &highlighted = at<uint32_t>(base + G_HIGHLIGHTED_PROFILE);

    switch (ev[0]) {
    case 2:   // row text
        if (row == 0 || row > count || ev[0x15] != 0) return 0;
        // the name offset is a 32-bit unsigned product (wraps at 2^32), added to the names afterwards
        field<const char *>(ev, 0x40) = static_cast<const char *>(s) + S_PROFILE_NAMES + static_cast<uintptr_t>(static_cast<uint32_t>((row - 1) * 64u));
        return 1;
    case 5:   // which row is selected
        ev[1] = static_cast<int32_t>(selected);
        if (follow && selected <= count) highlighted = selected;
        return 1;
    case 6:   // how many rows
        ev[1] = static_cast<int32_t>(count);
        return 1;
    case 10:   // a row was selected
        if (row <= count) selected = row;
        if (follow) highlighted = row;
        return 1;
    case 23:   // drop a selection that no longer exists
        if (selected > count) selected = 0;
        if (follow && highlighted > count) highlighted = 0;
        return 1;
    default:
        return 0;
    }
}

// 4.7 a row of the lobby menu was pressed
U3X_FN void u3x_lobby_row(uintptr_t base, const int32_t *ev, void *s) {
    using namespace g32;
    uint32_t n = 0;
    const uint32_t *rows = u3h_lobby_rows(base, &n);
    const uint32_t row = static_cast<uint32_t>(ev[1]);
    if (row >= n) return;
    field<uint32_t>(s, S_ROW_PRESSED) = row + 1;
    const uint32_t action = rows[3 * row + 2];

    switch (action) {
    case ROW_CUSTOM_GAME: u3x_custom_game(base, s); break;
    case ROW_FIND_PARTY:
        u3h_lnb_find_party(base);
        if (can_show(s)) u3h_show(base, s, PAGE_LAN_PARTIES, 0);
        break;
    case ROW_PROFILE:
    case ROW_OPTIONS: {   // the game's own button handler knows these two
        const uintptr_t button = native(s, 14);
        if (button) game_call<void>(button, reinterpret_cast<void *>(base + G_MENU), action, static_cast<int32_t>(0));
        break;
    }
    case ROW_CINEMA: u3x_cinema(base, s); break;
    case ROW_SPLITSCREEN: u3h_splitscreen(base, s); break;
    default: u3x_lobby_action(base, s, action); break;
    }
}

// 4.8 the OK button of the message-of-the-day page
U3X_FN void u3x_motd_ok(uintptr_t base, void *s) {
    using namespace g32;
    if ((ext_ea()[EA_119_SWITCH] & SWITCH_OFF) == 0) {
        game_call<void>(base + FN_MENU_SOUND, static_cast<int32_t>(0));
        ext_ea()[EA_MOTD_OK]++;
    }
    u3h_show(base, s, PAGE_MULTIPLAYER, 0);
    if ((ext_ea()[EA_119_SWITCH] & SWITCH_OFF) == 0)
        game_call<void>(base + FN_REBUILD_HISTORY, reinterpret_cast<void *>(base + G_MENU));
}

// 4.9 back on the multiplayer page while the record still says "in a match": count a quit
U3X_FN void u3x_mp_quit(uintptr_t base, void *s) {
    using namespace g32;
    if (ext_ea()[EA_119_SWITCH] & SWITCH_OFF) return;
    uint8_t *profile = u3h_lg_profile(base, 0);
    if (!profile) return;
    if (!u3x_count_quit(u3h_lg_rec(base, s, profile))) return;
    u3h_publish(base, s, profile);
    game_call<void>(base + FN_REFRESH_PLAYER, static_cast<int32_t>(0));
    game_call<void>(base + FN_REFRESH_PLAYER, static_cast<int32_t>(1));
    ext_ea()[EA_QUITS]++;
}
