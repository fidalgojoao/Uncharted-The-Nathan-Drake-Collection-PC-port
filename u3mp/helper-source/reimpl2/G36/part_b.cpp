// G36 part B: the matchmaking flow (Start Match, the online search, the end of the map vote, the co-op difficulty
// vote, the event-6 handler call, the per-frame start / join step, the page stack test).
// Written from spec/G36_matchmaking.md, sections 2, 3 and 5.

// ---- functions of the C part, of other groups and of the shared tools (spec 2.2)
extern "C" {
uint32_t *u3h_m03(void);
volatile uint32_t *u3h_m03_diag(void);
int u3h_onb_mode(void);
int u3h_party_send(uintptr_t base, uint32_t msg, const void *data, uint32_t len);
void u3h_n09_reset(uintptr_t base);
void u3h_n02_arm(uint32_t now);
void u3h_onm_arm(uintptr_t base);
volatile uint32_t *u3h_k44_diag(void);
int32_t *u3h_n04_idx(void);
volatile uint32_t *u3h_n04_diag(void);
uint32_t *u3h_m05_vt(void);
uint8_t *u3h_mm_bg(void);
uint8_t *u3h_mm_quiet(void);
void u3h_onm_state(uintptr_t base, uint32_t state);
uint32_t u3h_active_page(uintptr_t base);
int u3h_show(uintptr_t base, void *s, uint32_t page, int silent);
uint32_t u3h_crc_sid(const char *name);
uint8_t *u3x_sym_table(void *s, uint32_t id);
const uint8_t *u3x_pick_playlist(const uint8_t *table, uint32_t picked);
uint32_t u3x_playlist_pool(const uint8_t *playlist);
void u3x_party_lock(uintptr_t base, int v);
int u3x_popup_timed(uintptr_t base, void *s, uint32_t text, int seconds, uint32_t ok, uint32_t cancel);
int32_t u3x_net_clock(uintptr_t base);
uint8_t *u3x_mmv_netgame(void *s, int16_t i);
int u3x_has_difficulty_vote(uint32_t game_type);
void u3x_vote_open(uint8_t *vote);
int u3x_stand_in_vote(uint32_t now);

// ---- delivered here
int u3x_m03_startmatch(uintptr_t base, void *s);
int u3x_onb_startmatch(uintptr_t base, void *s);
int u3x_online_search(uintptr_t base);
int u3x_n04_close(uintptr_t base, void *s);
int u3x_n09_open(uintptr_t base, void *s);
int u3x_n27_crush_locked(uintptr_t base);
int u3x_n09_tick(uintptr_t base, uint32_t now);
void u3x_mm_ev6(uintptr_t base, uint32_t fn, uint32_t page);
void u3x_mm_start_tick(uintptr_t base, void *s);
int u3x_page_in_stack(const uint8_t *menu, uint32_t page);
}

namespace g36b {

// image offsets of game data (spec 3.5)
constexpr uintptr_t GAME_CLOCK = 0x17b4ba4;
constexpr uintptr_t LOBBY_SCENE = 0x17bba90;
constexpr uintptr_t MENU = 0x17bd080;
constexpr uintptr_t NGM = 0x17cb060;
constexpr uintptr_t NET = 0x17d5c58;
constexpr uintptr_t NET_MENU_MODE = 0x17d603c;
constexpr uintptr_t MATCH_SESSION = 0x17d6420;
constexpr uintptr_t MAP_VOTE = 0x17d7760;
constexpr uintptr_t DIFF_VOTE = 0x17d7784;
constexpr uintptr_t LEAVE_MARK = 0x17d79e4;
constexpr uintptr_t PROFILES = 0x17dd610;
constexpr uintptr_t SESSION_MANAGER = 0x17dd768;
constexpr uintptr_t LEVEL_MANAGER = 0x1afcf10;
constexpr uintptr_t NET_STATE = 0x1b68810;

// pages, texts and actions (spec 3.7)
constexpr uint32_t PAGE_LOADING = 0x69e10dad;
constexpr uint32_t PAGE_UNDER_LOADING = 0x82e9ddd3;
constexpr uint32_t PAGE_LOBBY = 0xb99a3235;
constexpr uint32_t PAGE_MULTIPLAYER = 0x53cbb5f2;
constexpr uint32_t PAGE_JOINING = 0x1af4bdd2;
constexpr uint32_t PAGE_POPUP = 0x471007a1;
constexpr uint32_t TEXT_STARTING_IN = 0x73a73dd8;
constexpr uint32_t ACTION_POPUP_OK = 0x4055f45f;
constexpr uint32_t ACTION_POPUP_CANCEL = 0xbbb649ef;
constexpr uint32_t MSG_LEADER_SEARCHES = 0x114;
constexpr uint32_t SYM_PLAYLISTS = 0xc1b0be0d;
constexpr uint32_t SYM_NET_GAMES = 0x3b9a067d;
constexpr uint32_t TICKS_PER_SECOND = 300;

[[gnu::always_inline]] inline uint32_t *ext_ec() { return reinterpret_cast<uint32_t *>(u3h_ext() + 0x4450); }

// the selected net game index of the net game manager (negative = none)
[[gnu::always_inline]] inline int16_t selected_net_game(uintptr_t base) { return at<int16_t>(base + NGM + 0x661c); }

// the playlist the player picked, taken from the first profile object (one symbol lookup)
U3X_FN const uint8_t *picked_playlist(uintptr_t base, void *s) {
    const uint8_t *profiles = at<const uint8_t *>(base + PROFILES);
    const uint32_t id = profiles ? rd<uint32_t>(profiles, 0x774) : 0;
    return u3x_pick_playlist(u3x_sym_table(s, SYM_PLAYLISTS), id);
}

// matchmaking status + its countdown deadline in net clock ticks
U3X_FN void set_status(uintptr_t base, int32_t status, uint32_t seconds) {
    at<int32_t>(base + NET + 0x40) = status;
    const uint32_t deadline = static_cast<uint32_t>(u3x_net_clock(base)) + TICKS_PER_SECOND * seconds;
    at<int32_t>(base + NET + 0x2c) = static_cast<int32_t>(deadline);
}

// both votes back to "not open, not closed"
U3X_FN void clear_votes(uintptr_t base) {
    at<uint8_t>(base + MAP_VOTE + 0) = 0;
    at<uint8_t>(base + MAP_VOTE + 1) = 0;
    u3h_n09_reset(base);
}

// the party is unlocked and told that its leader starts a search
U3X_FN void tell_party(uintptr_t base) {
    u3x_party_lock(base, 0);
    u3h_party_send(base, MSG_LEADER_SEARCHES, nullptr, 0);
}

U3X_FN int starting_popup(uintptr_t base, void *s) {
    return u3x_popup_timed(base, s, TEXT_STARTING_IN, 2, ACTION_POPUP_OK, ACTION_POPUP_CANCEL);
}

// the event record a page handler takes (24 bytes)
struct Event {
    uint32_t number;
    uint32_t page;
    uint64_t zero;
    uint8_t flag0;
    uint8_t flag1;
    uint8_t rest[6];
};

}  // namespace g36b

// 5.10: is a page with this id on the menu's page stack (index 0 .. top, at most 160 entries)
U3X_FN int u3x_page_in_stack(const uint8_t *menu, uint32_t page) {
    const int32_t top = rd<int32_t>(menu, 0xdb0);
    for (int32_t k = 0; k <= top && k < 160; k++) {
        const uint8_t *p = rd<const uint8_t *>(menu, 0x8b0 + 8 * static_cast<uintptr_t>(k));
        if (p && rd<uint32_t>(p, 0x10) == page) return 1;
    }
    return 0;
}

// 5.8: runs the game function at image offset fn with an event-6 record for the page
U3X_FN void u3x_mm_ev6(uintptr_t base, uint32_t fn, uint32_t page) {
    g36b::Event e;
    e.number = 6;
    e.page = page;
    e.zero = 0;
    e.flag0 = 0;
    e.flag1 = 1;
    for (int i = 0; i < 6; i++) e.rest[i] = 0;
    game_call<void, g36b::Event *>(base + fn, &e);
}

// 5.1: Start Match in offline mode (the stand-in)
U3X_FN int u3x_m03_startmatch(uintptr_t base, void *s) {
    using namespace g36b;
    if (ext_eb()[2] & 0x2000) return 0;
    if (u3h_onb_mode() != 2) return 0;
    if (at<uint32_t>(base + NET_MENU_MODE) != 1) return 0;

    volatile uint32_t *diag = u3h_m03_diag();
    diag[0] = diag[0] + 1;
    clear_votes(base);
    uint32_t *m = u3h_m03();
    *reinterpret_cast<const uint8_t **>(reinterpret_cast<uint8_t *>(m) + 0x20) = picked_playlist(base, s);
    tell_party(base);
    if (starting_popup(base, s)) {
        m[0] = 1;
        m[1] = 0;
        u3h_n02_arm(static_cast<uint32_t>(at<int32_t>(base + GAME_CLOCK)));
        at<uint8_t>(base + LEAVE_MARK) = 0;
    }
    return 1;   // also when the popup was not shown
}

// 5.2: Start Match with the online backend
U3X_FN int u3x_onb_startmatch(uintptr_t base, void *s) {
    using namespace g36b;
    if (u3h_onb_mode() != 1) return 0;
    if (at<uint32_t>(base + NET_MENU_MODE) != 1) return 0;

    volatile uint32_t *diag = u3h_k44_diag();
    diag[3] = diag[3] + 1;
    u3h_onm_arm(base);
    clear_votes(base);
    tell_party(base);
    const uint32_t pool = u3x_playlist_pool(picked_playlist(base, s));
    game_call<void, uint8_t *, uint32_t>(base + 0x7a2790, &at<uint8_t>(base + MATCH_SESSION), pool);
    at<uint8_t>(base + LEAVE_MARK) = 0;
    starting_popup(base, s);   // the result is not looked at
    return 1;
}

// 5.3: leaves the current room and sends the search through the registered session manager
U3X_FN int u3x_online_search(uintptr_t base) {
    using namespace g36b;
    using Leave = int64_t (*)(uint8_t *, uint8_t *);
    using Search = int64_t (*)(uint8_t *, uint8_t *, uint32_t, uint32_t, int32_t, int32_t, int32_t, uint32_t);

    uint8_t *manager = at<uint8_t *>(base + SESSION_MANAGER);
    uint8_t *net = &at<uint8_t>(base + NET);
    uint8_t *ms = &at<uint8_t>(base + MATCH_SESSION);

    at<uint8_t>(base + NET + 0x18) = 0;
    at<int32_t>(base + NET + 0x40) = 0;

    uint8_t *backend = rd<uint8_t *>(ms, 8);
    if (backend) {
        const uint8_t *table = rd<const uint8_t *>(backend, 0);
        if (table) rd<Leave>(table, 7 * 8)(backend, ms);
    }

    game_call<void, uint8_t *>(base + 0x720500, &at<uint8_t>(base + NGM));

    at<uint32_t>(base + MATCH_SESSION + 0xf8) = rd<uint32_t>(net, 0x10);
    at<uint32_t>(base + MATCH_SESSION + 0xfc) = rd<uint64_t>(net, 0x10) != 0 ? 1 : 0;

    if (!manager) return -1;
    const uint8_t *table = rd<const uint8_t *>(manager, 0);
    if (!table) return -1;
    if (rd<Search>(table, 5 * 8)(manager, ms, 0, 0, -1, 0, 0, 0) < 0) return -1;

    u3h_onm_state(base, 6);
    return 0;
}

// 5.4: the map vote is over: its winner becomes the selected net game and the lobby stage follows
U3X_FN int u3x_n04_close(uintptr_t base, void *s) {
    using namespace g36b;
    using SelectNetGame = void (*)(uint8_t *, int32_t, void *, uint32_t);

    const uint8_t *vote = &at<uint8_t>(base + MAP_VOTE);
    const int32_t winner = rd<int32_t>(vote, 0x14) > rd<int32_t>(vote, 0x10) ? rd<int32_t>(vote, 0xc)
                                                                             : rd<int32_t>(vote, 8);
    int32_t *chosen = u3h_n04_idx();
    *chosen = -1;
    const SelectNetGame select = rd<SelectNetGame>(s, 768 + 8 * 2);
    if (winner < 0 || !select) return 0;

    uint8_t *ngm = &at<uint8_t>(base + NGM);
    select(ngm, winner, nullptr, 0xc);
    *chosen = winner;
    volatile uint32_t *diag = u3h_n04_diag();
    diag[0] = diag[0] + 1;
    diag[1] = static_cast<uint32_t>(winner);

    set_status(base, 6, 45);
    at<uint8_t>(base + MAP_VOTE + 1) = 1;

    if (!(ext_ec()[2] & 8)) {
        uint8_t *scene = &at<uint8_t>(base + LOBBY_SCENE);
        game_call<void, uint8_t *, int>(base + 0x5b7040, scene, 1);
        game_call<void, uint8_t *, int, int, int, int, int>(base + 0x72b570, ngm, 1, 1, 1, 0, 0);
        diag[2] = diag[2] + 1;
        game_call<void, uint8_t *>(base + 0x5b34f0, scene);
        diag[4] = at<uint32_t>(base + NET_STATE);
        const char *stage = reinterpret_cast<const char *>(ngm + 0x74b8);
        diag[5] = *stage ? u3h_crc_sid(stage) : 0;
        diag[6] = (rd<uint32_t>(scene, 0) & 0xff) | ((rd<uint32_t>(scene, 4) & 0xff) << 8);
    }
    return 1;
}

// 5.5: opens the difficulty vote when the selected net game is of a co-op type that has one
U3X_FN int u3x_n09_open(uintptr_t base, void *s) {
    using namespace g36b;
    uint32_t *ec = ext_ec();
    if (ec[2] & 0x80) return 0;
    const uint8_t *record = u3x_mmv_netgame(s, selected_net_game(base));
    if (!record) return 0;
    const uint32_t type = rd<uint32_t>(record, 0xc);
    ec[254] = type & 0xff;
    if (!u3x_has_difficulty_vote(type)) return 0;

    u3x_vote_open(&at<uint8_t>(base + DIFF_VOTE));
    set_status(base, 4, 10);
    ec[252] = ec[252] + 1;
    return 1;
}

// 5.6: 1 when Crushing is not unlocked for the local player on the selected net game's level
U3X_FN int u3x_n27_crush_locked(uintptr_t base) {
    using namespace g36b;
    const uint8_t *const *handle = game_call<const uint8_t *const *, uint32_t>(base + 0x11f2bf0, SYM_NET_GAMES);
    if (!handle) return 0;
    const uint8_t *table = *handle;
    if (!table) return 0;
    const int16_t i = selected_net_game(base);
    if (i < 0 || i >= rd<int32_t>(table, 0)) return 0;
    const uint8_t *records = rd<const uint8_t *>(table, 8);
    if (!records) return 0;
    const uint32_t unlock = rd<uint32_t>(records, static_cast<uintptr_t>(i) * 0xd0 + 0xb8);
    if (unlock >= 0x20) return 0;
    const uint8_t *profiles = at<const uint8_t *>(base + PROFILES);
    if (!ptr_ok(profiles)) return 0;
    return rd<int32_t>(profiles, 0x250 + 4 * (static_cast<uintptr_t>(unlock) + 0x58a)) <= 0 ? 1 : 0;
}

// 5.7: the difficulty vote while it is open: a stand-in vote now and then, and the close at the deadline
U3X_FN int u3x_n09_tick(uintptr_t base, uint32_t now) {
    using namespace g36b;
    int32_t *count = &at<int32_t>(base + DIFF_VOTE + 8);   // Easy, Normal, Hard, Crushing
    uint32_t *last_vote = u3h_m05_vt();

    // a stand-in player votes, at most once every 300 ticks
    if (now - *last_vote >= TICKS_PER_SECOND) {
        const uint32_t total = static_cast<uint32_t>(count[0]) + static_cast<uint32_t>(count[1]) +
                               static_cast<uint32_t>(count[2]) + static_cast<uint32_t>(count[3]);
        const uint32_t rows = u3h_m03()[5];
        const uint32_t others = rows > 1 ? rows - 1 : 1;
        const uint32_t mine = at<uint32_t>(base + DIFF_VOTE + 4) < 4 ? 1 : 0;
        if (total < others + mine) {
            int k = u3x_stand_in_vote(now);
            if (k == 3 && u3x_n27_crush_locked(base)) {
                k = 2;
                uint32_t *ea = ext_ea();
                ea[169] = ea[169] + 1;
            }
            count[k] = count[k] + 1;
            uint32_t *ec = ext_ec();
            ec[255] = ec[255] + 1;
        }
        *last_vote = now;
    }

    // still time left: a plain signed comparison of the net clock with the deadline
    const int32_t clock = static_cast<int32_t>(u3x_net_clock(base));
    if (clock < at<int32_t>(base + NET + 0x2c)) return 0;

    // the winner: a single Crushing vote counts for Hard (on a copy only); no vote at all = Normal
    const int32_t c0 = count[0], c1 = count[1], c2 = count[2], c3 = count[3];
    int32_t tally[4] = {c0, c1, c2, c3};
    if (tally[3] == 1) {
        tally[2] = static_cast<int32_t>(static_cast<uint32_t>(tally[2]) + 1);
        tally[3] = 0;
    }
    int32_t winner = 1;
    int32_t best = 0;
    for (int32_t row = 0; row < 4; row++) {
        if (tally[row] > best) {
            best = tally[row];
            winner = row;
        }
    }

    at<uint8_t>(base + NGM + 0x6a48) = static_cast<uint8_t>(winner + 1);
    at<uint8_t>(base + DIFF_VOTE + 1) = 1;
    set_status(base, 6, 25);

    uint32_t *ec = ext_ec();
    ec[252] = ec[252] + 0x10000;
    ec[253] = static_cast<uint32_t>(winner) | (static_cast<uint32_t>(c0) & 15) << 8 |
              (static_cast<uint32_t>(c1) & 15) << 12 | (static_cast<uint32_t>(c2) & 15) << 16 |
              (static_cast<uint32_t>(c3) & 15) << 20 | static_cast<uint32_t>(winner + 1) << 28;
    ec[254] = (ec[254] & 0xff) | ((at<uint32_t>(base + DIFF_VOTE + 4) & 0xff) << 8);
    return 1;
}

// 5.9: runs the game's join / create / match start handlers whatever page is open
U3X_FN void u3x_mm_start_tick(uintptr_t base, void *s) {
    using namespace g36b;
    uint8_t *bg = u3h_mm_bg();
    uint8_t *quiet = u3h_mm_quiet();

    const uint32_t ns = at<uint32_t>(base + NET_STATE);
    const uint32_t pg = u3h_active_page(base);

    const bool starting = ns >= 0x2c && ns <= 0x30;
    const bool joining = *bg && (ns == 0xb || ns == 0xf || ns == 0x11 || ns == 0x28);
    if (!starting && !joining) {
        if (ns != 0x12) *bg = 0;
        return;
    }
    if (pg == 0) return;

    // the player backed out to the lobby or the multiplayer page while the room was made
    if (*bg && ns >= 0x11 && (pg == PAGE_LOBBY || pg == PAGE_MULTIPLAYER)) {
        *bg = 0;
        u3h_show(base, s, PAGE_UNDER_LOADING, 0);
        u3h_show(base, s, PAGE_LOADING, 0);
        return;
    }

    if (joining) {
        *quiet = 1;
        if (ns == 0x11 && selected_net_game(base) < 0) {   // the create handler needs a selected net game
            *quiet = 0;
            return;
        }
        if (ns == 0xb || ns == 0xf) u3x_mm_ev6(base, 0x6a6900, PAGE_JOINING);
        else u3x_mm_ev6(base, 0x6a5fa0, PAGE_POPUP);
        *quiet = 0;
        return;
    }

    // starting
    if (pg == PAGE_LOADING) return;   // the page's own flow runs the handler
    if (!*bg && !u3x_page_in_stack(&at<uint8_t>(base + MENU), PAGE_LOADING)) return;

    if (ns == 0x30) {
        const uint8_t *net = &at<uint8_t>(base + NET);
        const uint8_t *ms = &at<uint8_t>(base + MATCH_SESSION);
        // the deadline is compared with the game clock here (kept as it is)
        // (a plain signed comparison)
        const bool due = rd<int32_t>(net, 0x2c) <= at<int32_t>(base + GAME_CLOCK);
        const bool open = due &&
                          (rd<uint8_t>(ms, 0x1338) != 0 || rd<uint64_t>(ms, 0x18) == 0 ||
                           rd<uint32_t>(ms, 0x12e4) == rd<uint32_t>(ms, 0x12e8)) &&
                          rd<uint32_t>(net, 0x408) == 2 &&
                          (game_call<int, uint8_t *, int>(base + 0x10ee0e0, &at<uint8_t>(base + LEVEL_MANAGER), 1) & 0xff) != 0;
        if (open) {
            if (*bg) u3h_show(base, s, PAGE_UNDER_LOADING, 0);
            u3h_show(base, s, PAGE_LOADING, 0);
            *bg = 0;
        }
    }

    if (selected_net_game(base) < 0) return;
    u3x_mm_ev6(base, 0x6a7310, PAGE_LOADING);
}
