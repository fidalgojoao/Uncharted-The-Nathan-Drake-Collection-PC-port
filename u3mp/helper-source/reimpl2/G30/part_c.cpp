// G30 part C: the post-match handler (u3x_pm119) and the search re-arm (u3x_n13_rearm).
// Written from spec G30_post_match.md, section 6 and the facts of section 3 it needs.

extern "C" {
// the C part
uint8_t *u3h_lg_profile(uintptr_t base, uint32_t user);
uint8_t *u3h_lg_rec(uintptr_t base, void *s, uint8_t *profile);
int u3h_publish(uintptr_t base, void *s, uint8_t *profile);
int u3h_party_send(uintptr_t base, uint32_t msg, const void *data, uint32_t len);
uint32_t *u3h_m03(void);
void u3h_n09_reset(uintptr_t base);      // new export (integrator)
void u3h_n02_arm(uint32_t now);          // new export (integrator)
// other groups
int u3x_session_host(const uint8_t *session);
uint32_t u3x_net_player_id(const uint8_t *np);
int u3x_shows_match_stats(uint32_t mode, int private_party);
// parts A and B of this group
int u3x_pm_ranked(uintptr_t base, void *s);
void u3x_pm_party_vcall(uintptr_t base, uint32_t off, int arg, int three);
void u3x_pm_pg_clear(uintptr_t base);
void u3x_pm_pg_push(uintptr_t base, uint32_t id);
void u3x_pm_menu_reset(uintptr_t base, uintptr_t menu);
int u3x_n13_on(void);
void u3x_pm_user(uintptr_t base, void *s, uint32_t u);
// this part
void u3x_pm119(uintptr_t base, void *s, uint8_t *param);
void u3x_n13_rearm(uintptr_t base, void *s);
}

namespace g30c {

// game objects (image offsets)
constexpr uintptr_t NET = 0x17d5c58, NGM = 0x17cb060, MS = 0x17d6420, PS = 0x17dc298;
constexpr uintptr_t MENU = 0x17bd080, MENU2 = 0x17bfa10;
constexpr uintptr_t CLOCK = 0x17b4ba4, ABORT = 0x16e7170;
constexpr uintptr_t NET_SEARCH = NET + 0x40;       // u32, non-zero = the match came from matchmaking
constexpr uintptr_t NET_ROOM = NET + 0x78;         // u64, the match room id
constexpr uintptr_t MATCH_STATE = NGM + 0x6808;    // u32
constexpr uintptr_t MATCH_MODE = NGM + 0x680c;     // u32
constexpr uintptr_t NET_STATE = 0x1b68810;         // u32 state, u32 clock of its last change at +4
constexpr uintptr_t LEFT_MATCH = 0x1b68a20;        // u8

// session fields
constexpr uintptr_t S_ROOM = 0x18, S_IN_ROOM = 0xc8, S_LEAVE_ASKED = 0x1334, S_ABORTED = 0x1339;

// backend function table entries
constexpr uint32_t BK_LEAVE = 0x38, BK_SETATTR = 0x40, BK_ROOMFLAGS = 0x58, BK_SETATTR4 = 0x68;

// page ids
constexpr uint32_t PG_ROOT = 0x37656388u, PG_MULTIPLAYER = 0x53cbb5f2u, PG_LOBBY = 0xb99a3235u;
constexpr uint32_t PG_MATCHMAKING = 0xe35f0f1au, PG_MATCH_STATS = 0x0c3a3716u, PG_LEAVING = 0xb808d03cu;
constexpr uint32_t SHOW_PAGE_ARG = 0x5ef96af1u;

// diagnostic words
constexpr int EA_RUNS = 59, EA_ROUTED = 61, EA_LEAVE_REARM = 91, EA_REARMS = 92, EA_ROUTE_WORD = 93, EA_REARM_MARK = 94;

// the request handed to the level manager
struct LevelRequest {
    uint64_t name_hash;
    uint32_t name_id;
    uint8_t unused_c[2];
    uint8_t kind;          // +0xe
    uint8_t unused_f;
    uint64_t unused_10;
};
static_assert(sizeof(LevelRequest) == 0x18);

// game message 0x105
struct MatchOverMsg {
    uint32_t player_id;
    uint32_t zero;
    uint64_t room;
};
static_assert(sizeof(MatchOverMsg) == 0x10);

[[gnu::always_inline]] inline uint8_t *img(uintptr_t base, uintptr_t off) { return reinterpret_cast<uint8_t *>(base + off); }
[[gnu::always_inline]] inline uint32_t clock_now(uintptr_t base) { return static_cast<uint32_t>(at<int32_t>(base + CLOCK)); }
[[gnu::always_inline]] inline int lead(uintptr_t base, uintptr_t session) { return u3x_session_host(img(base, session)); }

// signed 64-bit integer to single precision (one rounding)
[[gnu::always_inline]] inline uint32_t f_from_int64(int64_t v) {
    uint32_t r;
    __asm__("cvtsi2ssq %1,%%xmm0\n\tmovd %%xmm0,%0" : "=r"(r) : "r"(v) : "xmm0");
    return r;
}

[[gnu::always_inline]] inline uint64_t time_stamp() {
    uint32_t lo, hi;
    __asm__("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

U3X_FN void set_net_state(uintptr_t base, uint32_t state, uint32_t clock) {
    at<uint32_t>(base + NET_STATE) = state;
    at<uint32_t>(base + NET_STATE + 4) = clock;
}

// the page stack becomes root, multiplayer, `page`, and `page` is shown
U3X_FN void go_to_page(uintptr_t base, uint32_t page) {
    u3x_pm_pg_clear(base);
    u3x_pm_pg_push(base, PG_ROOT);
    u3x_pm_pg_push(base, PG_MULTIPLAYER);
    u3x_pm_pg_push(base, page);
    game_call<void>(base + 0x751aa0, page, SHOW_PAGE_ARG);
}

U3X_FN int stats_page_wanted(uintptr_t base) {
    return u3x_shows_match_stats(at<uint32_t>(base + MATCH_MODE), at<uint8_t>(base + MS + S_ABORTED));
}

// step 8: the block at 0x17bb860 gets its two times once
U3X_FN void start_timer_block(uintptr_t base) {
    uint8_t *blk = img(base, 0x17bb860);
    if (blk[0] == 1) return;
    blk[0] = 1;
    const uint32_t c = f_div(f_from_int(at<int32_t>(base + CLOCK)), at<uint32_t>(base + 0x1443b34));
    *reinterpret_cast<uint32_t *>(blk + 4) = f_add(c, at<uint32_t>(base + 0x17b9d18));
    *reinterpret_cast<uint32_t *>(blk + 8) = f_add(c, at<uint32_t>(base + 0x1443b38));
}

// step 11: ask the level manager for the menu level, by the game's own name text
U3X_FN void request_menu_level(uintptr_t base) {
    const char *name = reinterpret_cast<const char *>(base + 0x145e7f2);
    LevelRequest req{};
    req.name_hash = game_call<uint64_t>(base + 0x10e95b0, name);
    req.name_id = game_call<uint32_t>(at<uintptr_t>(base + 0x1702720), 0, name, 0);
    req.kind = 0x0a;
    game_call<void>(base + 0x10eb990, img(base, 0x1afcf10), &req);
}

// step 12, ABORT set: page stack for leaving, the party, the matchmaking code, the backend
U3X_FN void leave_match(uintptr_t base, const uint8_t *param, bool rearm) {
    u3x_pm_pg_clear(base);
    u3x_pm_pg_push(base, PG_ROOT);
    u3x_pm_pg_push(base, PG_MULTIPLAYER);
    if (rearm)
        ext_ea()[EA_LEAVE_REARM] += 1;          // no lobby page, whatever the search word holds
    else if (at<uint32_t>(base + NET_SEARCH) == 0)
        u3x_pm_pg_push(base, PG_LOBBY);
    u3x_pm_pg_push(base, PG_LEAVING);

    uint8_t *ps = img(base, PS);
    if (u3x_session_host(ps)) {
        if (at<uint32_t>(base + NET_SEARCH) == 0)
            u3h_party_send(base, 0x117, img(base, NET_ROOM), 8);
        else
            ps[S_LEAVE_ASKED] = 1;
    } else if (rd<uint8_t>(rd<const uint8_t *>(param, 8), 0x198) == 0) {   // the object param + 8 points to now
        ps[S_LEAVE_ASKED] = 1;
    }

    const uint8_t *ms = img(base, MS);
    const uint64_t room = ms[S_IN_ROOM] ? rd<uint64_t>(ms, S_ROOM) : 0;
    game_call<void>(base + 0x74d1f0, room);
    u3x_pm_party_vcall(base, BK_LEAVE, 0, 0);

    at<uint8_t>(base + LEFT_MATCH) = 1;
    at<uint32_t>(base + NET_SEARCH) = 0;
    set_net_state(base, 0x26, clock_now(base));
}

// step 12, ABORT clear: room flags, and message 0x105 with the local player and the match room
U3X_FN void stay_after_match(uintptr_t base) {
    u3x_pm_party_vcall(base, BK_ROOMFLAGS, 0, 1);
    const uint8_t *local = at<const uint8_t *>(base + NGM + 0x370);
    const uint8_t *target = at<const uint8_t *>(base + NGM + 0x380);
    if (!local || !target) return;
    MatchOverMsg msg;
    msg.player_id = u3x_net_player_id(local);
    msg.zero = 0;
    msg.room = at<uint64_t>(base + NET_ROOM);
    game_call<void>(base + 0x6b85a0, rd<uint32_t>(target, 0x5b4), 0x105, &msg, 0x10);
}

// step 14, lobby: leave the room, lobby page (and the statistics page for a counted match)
U3X_FN void back_to_lobby(uintptr_t base, void *s) {
    at<uint8_t>(base + 0x16e7171) = 1;
    at<uint8_t>(base + 0x16e7172) = 1;
    u3x_pm_party_vcall(base, BK_LEAVE, 0, 0);
    go_to_page(base, PG_LOBBY);
    if (u3x_pm_ranked(base, s) && stats_page_wanted(base)) u3x_pm_pg_push(base, PG_MATCH_STATS);
    const uint32_t state = lead(base, MS) ? 3 : 0x12;
    set_net_state(base, state, clock_now(base));
}

// seconds since the time-stamp counter started, plus the game's constant: (whole + rest / rate) + constant
U3X_FN uint32_t matchmaking_time(uintptr_t base) {
    const uint64_t ticks = time_stamp();
    const uint64_t rate = at<uint64_t>(base + 0x16e7918);
    const uint64_t whole = rate ? ticks / rate : 0;
    const uint64_t rest = ticks - whole * rate;
    const uint32_t fraction = f_div(f_from_int64(static_cast<int64_t>(rest)), f_from_int64(static_cast<int64_t>(rate)));
    const uint32_t seconds = f_add(f_from_int64(static_cast<int64_t>(whole)), fraction);
    return f_add(seconds, at<uint32_t>(base + 0x1443b3c));
}

// step 14, matchmaking: time, matchmaking page (and statistics page), the session lead resets two attributes
U3X_FN void back_to_matchmaking(uintptr_t base) {
    at<uint32_t>(base + 0x17d79e0) = matchmaking_time(base);
    go_to_page(base, PG_MATCHMAKING);
    if (stats_page_wanted(base)) u3x_pm_pg_push(base, PG_MATCH_STATS);
    if (lead(base, MS)) {
        u3x_pm_party_vcall(base, BK_SETATTR4, 0, 1);
        u3x_pm_party_vcall(base, BK_SETATTR, 0, 1);
    }
}

}  // namespace g30c

// 6.2: back to the matchmaking page with a new search armed
extern "C" U3X_FN void u3x_n13_rearm(uintptr_t base, void *) {
    using namespace g30c;
    go_to_page(base, PG_MATCHMAKING);
    if (stats_page_wanted(base)) u3x_pm_pg_push(base, PG_MATCH_STATS);

    at<uint8_t>(base + 0x17d7760) = 0;      // map vote: open, closed
    at<uint8_t>(base + 0x17d7761) = 0;
    u3h_n09_reset(base);                    // difficulty vote
    at<uint8_t>(base + 0x17d79e4) = 0;      // "leave Find Game" mark

    const uint32_t now = clock_now(base);
    uint32_t *standin = u3h_m03();
    standin[0] = 1;                         // armed
    standin[1] = 0;                         // not running
    u3h_n02_arm(now);
    set_net_state(base, 20, now);
}

// 6.1: the post-match handler
extern "C" U3X_FN void u3x_pm119(uintptr_t base, void *s, uint8_t *param) {
    using namespace g30c;
    uint32_t *ea = ext_ea();

    // 1, 2: counters, the re-arm decision, the routing word
    ea[EA_RUNS] += 1;
    const bool rearm = u3x_n13_on() != 0 && ea[EA_REARM_MARK] != 0;
    ea[EA_REARM_MARK] = 0;
    ea[EA_ROUTE_WORD] = (at<uint32_t>(base + NET_SEARCH) * 0x100u) | (rearm ? 0x10u : 0u) | at<uint8_t>(base + ABORT);

    // 3: user 0 is no longer in a match; save
    uint8_t *profile0 = u3h_lg_profile(base, 0);
    uint8_t *rec0 = u3h_lg_rec(base, s, profile0);
    *reinterpret_cast<uint32_t *>(rec0 + 0x558) = 0;
    u3h_publish(base, s, profile0);

    // 4 .. 6
    *reinterpret_cast<uint32_t *>(param + 0x1c) = 0;
    game_call<void>(base + 0x74b290);
    game_call<void>(base + 0x6a3130, img(base, 0x17c3ba8));
    uint8_t *q = rd<uint8_t *>(param, 8);
    *reinterpret_cast<uint32_t *>(q + 0x1a0) = F_ONE;
    *reinterpret_cast<uint32_t *>(param + 0x18) = clock_now(base);
    *reinterpret_cast<uint32_t *>(q + 0x19c) = F_ONE;   // last of the three stores

    // 7, 8: menus, the timer block
    u3x_pm_menu_reset(base, MENU);
    u3x_pm_menu_reset(base, MENU2);
    at<uint8_t>(base + 0x17fa983) = 0;
    start_timer_block(base);

    // 9: a match aborted for the whole session is left (and the party room with it)
    if (at<uint8_t>(base + MS + S_ABORTED) != 0) {
        if (at<uint64_t>(base + PS + S_ROOM) != 0) at<uint8_t>(base + PS + S_LEAVE_ASKED) = 1;
        at<uint8_t>(base + ABORT) = 1;
    }

    // 10: no match, booster manager words, "left the match" mark
    at<uint32_t>(base + MATCH_STATE) = 0;
    at<uint32_t>(base + 0x17c3b40) = 0;
    at<uint32_t>(base + 0x17c3b44) = 0;
    at<uint32_t>(base + 0x17c3b54) = 0;
    at<uint8_t>(base + LEFT_MATCH) = 0;

    // 11, 12
    request_menu_level(base);
    if (at<uint8_t>(base + ABORT) != 0)
        leave_match(base, param, rearm);
    else
        stay_after_match(base);

    // 13: the local users' records
    u3x_pm_user(base, s, 0);
    u3x_pm_user(base, s, 1);

    // 14: where the menus go
    if (at<uint8_t>(base + ABORT) != 0) return;
    if (rearm) {
        u3x_n13_rearm(base, s);
        ea[EA_REARMS] += 1;
    } else if (at<uint32_t>(base + NET_SEARCH) == 0) {
        back_to_lobby(base, s);
    } else {
        back_to_matchmaking(base);
    }
    ea[EA_ROUTED] += 1;
}
