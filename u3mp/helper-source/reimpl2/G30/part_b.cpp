// G30 part B: one local user's match at the end of a multiplayer match: the counters of the multiplayer record,
// the weapon statistics block, the leaderboard blob and submits. Written from spec G30_post_match.md, section 5.

extern "C" {
// the helper's C part
uint8_t *u3h_lg_profile(uintptr_t base, uint32_t user);
uint8_t *u3h_lg_rec(uintptr_t base, void *s, uint8_t *profile);
int u3h_publish(uintptr_t base, void *s, uint8_t *profile);
// other groups
uint32_t u3x_net_player_id(const uint8_t *np);
int u3x_session_host(const uint8_t *session);
int u3x_lb_off(void);
int u3x_lb_submit(uintptr_t base, void *s, uint32_t board, int64_t score, const uint8_t *blob);
void u3x_pm_challenge(uintptr_t base, void *s, uint32_t u, uint32_t id, int32_t v, int bit);
void u3x_pm_365c5c(uintptr_t base, void *s, uint32_t u, int32_t mine, int32_t theirs);
void u3x_pm_365da8(uintptr_t base, void *s, uint32_t u, uint32_t map, int32_t a3);
// part A of this group
uint8_t *u3x_pm_ngentry(uintptr_t base, void *s);
int32_t u3x_pm_type(uintptr_t base, void *s);
int u3x_pm_coop(uintptr_t base, void *s);
int u3x_pm_ranked(uintptr_t base, void *s);
uint32_t u3x_pm_map(uintptr_t base, void *s);
int u3x_pm_map_is(uintptr_t base, void *s, uint32_t id);
// this part
void u3x_pm_user(uintptr_t base, void *s, uint32_t u);
void u3x_pm_weapon_stats(uintptr_t base);
void u3x_pm_lb_submits(uintptr_t base, void *s, uint32_t u);
}

namespace g30b {

// image offsets of the game objects used here
constexpr uintptr_t NGM = 0x17cb060;        // net game manager
constexpr uintptr_t MS = 0x17d6420;         // match session
constexpr uintptr_t SM = 0x17dd778;         // match score object
constexpr uintptr_t WS = 0x17d3630;         // weapon statistics object
constexpr uintptr_t BOOSTERS = 0x17c3b40;   // booster manager
constexpr uintptr_t KICKBACKS = 0x17d79f0;  // argument block of the kickback query
constexpr uint32_t PLAYLIST_NO_RESULT = 0x5242014b;   // the playlist whose matches give no totals and no result

U3X_RO const char weapon_stats_name[14] = {'_', 'w', 'e', 'a', 'p', 'o', 'n', '-', 's', 't', 'a', 't', 's', 0};

enum class Outcome { none, win, tie, loss };

// the multiplayer record of local user u (the first fetch for a user may reset it, so it is fetched at each use)
U3X_FN static uint8_t *record(uintptr_t base, void *s, uint32_t u) {
    return u3h_lg_rec(base, s, u3h_lg_profile(base, u));
}

// field `off` of the record of user u grows by v (32 bits, wrapping)
U3X_FN static void add(uintptr_t base, void *s, uint32_t u, uint32_t off, uint32_t v) {
    *reinterpret_cast<uint32_t *>(record(base, s, u) + off) += v;
}

// one more use of booster / kickback r; `first` is 0x4fd for boosters, 0x53d for kickbacks (offset in 32-bit maths)
U3X_FN static void count_use(uintptr_t base, void *s, uint32_t u, int32_t r, uint32_t first) {
    add(base, s, u, (static_cast<uint32_t>(r) + first) * 4u + 8u, 1);
}

// the result of a match of n teams for a member of `team`; mine = the team block of that team
U3X_FN static Outcome outcome(int32_t n, uint32_t team, const uint8_t *mine, const int16_t *score, uint32_t c0, uint32_t c1) {
    if (n == 2) {
        if (!c0 || !c1) return Outcome::win;   // a team without players
        if (score[0] == score[1]) return Outcome::tie;
        return rd<int16_t>(mine, 0) == 1 ? Outcome::win : Outcome::loss;   // place 1 = first
    }
    if (n == 3 && team <= 2) {
        if (rd<int16_t>(mine, 0) != 1) return Outcome::loss;
        for (uint32_t t = 0; t < 3; t++)
            if (t != team && score[t] == score[team]) return Outcome::tie;
        return Outcome::win;
    }
    return Outcome::none;
}

// the totals of a co-op match
U3X_FN static void coop_match(uintptr_t base, void *s, uint32_t u, const uint8_t *stat, const uint8_t *member, uint32_t total) {
    const uint32_t v14 = rd<uint32_t>(member, 0x14);
    add(base, s, u, 0x5bc, total);
    add(base, s, u, 0x1f2c, total);
    add(base, s, u, 0x5c0, v14);
    add(base, s, u, 0x1f28, v14);
    at<uint32_t>(base + 0x1b68a18 + 4 * u) = 1;   // match recorded
    if (at<uint8_t>(base + MS + 0x1339)) return;   // aborted for the whole session
    add(base, s, u, 0x15fc, 1);
    add(base, s, u, 0x65c, rd<uint16_t>(stat, 0xa));
    if (at<uint32_t>(base + NGM + 0x680c) != 4) return;
    add(base, s, u, 0x1600, 1);
    if (const uint8_t *entry = u3x_pm_ngentry(base, s))
        u3x_pm_365da8(base, s, u, rd<uint32_t>(entry, 0xb8), rd<uint16_t>(stat, 0xc));
    const uint32_t step = at<uint32_t>(base + NGM + 0x6a48);   // difficulty step
    if (step >= 1 && step <= 4) add(base, s, u, 0x65c + 4 * step, 1);
}

// the values of the match taken before any game call
struct Start {
    const uint8_t *stat, *member;
    uint32_t aborted, team, c0, c1;
    int16_t score[3];
};

// the totals and the result of a competitive match
U3X_FN static void competitive_match(uintptr_t base, void *s, uint32_t u, const Start &m, int32_t type, int coop) {
    const uint8_t *stat = m.stat;
    if (u3x_session_host(reinterpret_cast<const uint8_t *>(base + MS)) && u == 0) u3x_pm_weapon_stats(base);
    at<uint32_t>(base + 0x1b68a18 + 4 * u) = 1;   // match recorded
    if ((at<uint8_t>(base + MS + 0x1339) || m.aborted) && !coop) {
        add(base, s, u, 0x5b0, 1);
        add(base, s, u, 0x1f24, 1);
    }
    add(base, s, u, 0x1604, 1);
    if (!m.aborted && u3x_pm_map_is(base, s, PLAYLIST_NO_RESULT)) u3x_pm_challenge(base, s, u, 0x78b, 1, 0);

    if (type != 0x12 && type != 0xf && type != 2) {
        add(base, s, u, 0x5a4, rd<uint16_t>(stat, 8));
        add(base, s, u, 0x5a8, rd<uint16_t>(stat, 0xc));
    }
    if (u3x_pm_map(base, s) != PLAYLIST_NO_RESULT) {   // leaderboard totals
        const bool counts = game_call<int>(base + 0x729b10, base + NGM) != 0 || type == 10 || type == 0xe;
        if (counts && type != 0xf && type != 2) {
            add(base, s, u, 0x1c54, rd<uint16_t>(stat, 8));
            add(base, s, u, 0x1c58, rd<uint16_t>(stat, 0xc));
        }
    }
    const uint32_t v14 = rd<uint32_t>(m.member, 0x14);
    add(base, s, u, 0x5b4, rd<uint16_t>(stat, 0x48));
    add(base, s, u, 0x5b8, v14);
    add(base, s, u, 0x1f30, v14);
    add(base, s, u, 0x65c, rd<uint16_t>(stat, 0xa));
    if (type == 0xf) {
        const int32_t points = rd<int16_t>(stat, 2);
        add(base, s, u, 0x658, static_cast<uint32_t>(points));
        u3x_pm_challenge(base, s, u, 0x765, points, 0);
    } else if (type == 2) {
        add(base, s, u, 0x654, static_cast<uint32_t>(static_cast<int32_t>(rd<int16_t>(stat, 2))));
    }

    // the result of the match
    if (u3x_pm_map(base, s) == PLAYLIST_NO_RESULT) return;
    const uint32_t mode = at<uint32_t>(base + NGM + 0x680c);
    if (mode != 2 && mode != 6 && mode != 9) return;
    const int32_t teams = game_call<int32_t>(base + 0x7210a0, base + NGM);
    const uint8_t *blocks = reinterpret_cast<const uint8_t *>(base + SM + 0x5c14);
    const uint8_t *mine = blocks + 0x130 * static_cast<uintptr_t>(m.team);   // no range test of the team
    const Outcome result = outcome(teams, m.team, mine, m.score, m.c0, m.c1);
    if (teams == 2 && (result == Outcome::win || result == Outcome::loss)) {
        const bool win = result == Outcome::win;
        if (at<uint8_t>(base + NGM + 0x34e)) add(base, s, u, win ? 0x61c : 0x620, 1);
        else if (at<uint8_t>(base + NGM + 0x34d)) add(base, s, u, win ? 0x614 : 0x618, 1);
    }
    if (result == Outcome::win) {
        add(base, s, u, 0x5ac, 1);
        add(base, s, u, 0x1f34, 1);
        const uint8_t *other = blocks + (m.team == 0 ? 0x130 : 0);
        u3x_pm_365c5c(base, s, u, rd<int16_t>(mine, 2), rd<int16_t>(other, 2));
    } else if (result == Outcome::tie) {
        add(base, s, u, 0x60c, 1);
    } else if (result == Outcome::loss) {
        add(base, s, u, 0x5b0, 1);
        add(base, s, u, 0x1f24, 1);
    }
}

}  // namespace g30b

// One local user's match: booster and kickback use, the totals of the kind of match, save, leaderboard submits.
extern "C" U3X_FN void u3x_pm_user(uintptr_t base, void *s, uint32_t u) {
    using namespace g30b;
    const uint8_t *player = at<const uint8_t *>(base + NGM + 0x370 + 8 * static_cast<uintptr_t>(u));
    if (!player) return;

    // taken before any game call
    Start m;
    m.aborted = at<uint8_t>(base + 0x16e7170);
    m.stat = reinterpret_cast<const uint8_t *>(base + 0x1b7f114);     // the game's all-default stat block
    m.member = reinterpret_cast<const uint8_t *>(base + 0x1b7f070);   // and member record
    if (const uint32_t members = at<uint32_t>(base + SM + 8)) {
        const uint32_t id = u3x_net_player_id(player);
        for (uint32_t i = 0; i < members; i++) {
            const uint8_t *rec = reinterpret_cast<const uint8_t *>(base + SM + 0x50ec) + 0xa4 * static_cast<uintptr_t>(i);
            if (rd<uint32_t>(rec, 0) != id) continue;
            m.member = rec;
            m.stat = reinterpret_cast<const uint8_t *>(base + SM + 0x60d4) + 0x130 * static_cast<uintptr_t>(i);
            break;
        }
    }
    m.team = rd<uint8_t>(m.member, 7);
    for (uint32_t t = 0; t < 3; t++) m.score[t] = at<int16_t>(base + SM + 0x5c14 + 0x130 * t + 2);
    m.c0 = m.c1 = 0;   // players per team
    if (at<uint8_t>(base + 0x17d5c61))
        for (uint32_t i = 0; i < 10; i++) {
            const uint8_t *slot = reinterpret_cast<const uint8_t *>(base + NGM + 0x390) + 0x9c0 * i;
            if (!rd<uint8_t>(slot, 0xe1)) continue;
            const uint32_t team = rd<uint32_t>(slot, 0x21c);
            if (team == 0) m.c0++;
            else if (team == 1) m.c1++;
        }
    const int32_t type = u3x_pm_type(base, s);
    const int coop = u3x_pm_coop(base, s);

    for (int slot = 0; slot < 2; slot++)
        count_use(base, s, u, game_call<int32_t>(base + 0x688160, base + BOOSTERS, static_cast<uint64_t>(u), slot, -1), 0x4fd);
    count_use(base, s, u, game_call<int32_t>(base + 0x74e6d0, base + KICKBACKS + 0xc * static_cast<uintptr_t>(u), -1, coop), 0x53d);

    const uint32_t total = rd<uint32_t>(m.stat, 0x24);
    bool save = true;
    if (coop && type != 0x12) {
        coop_match(base, s, u, m.stat, m.member, total);
    } else if (u3x_pm_ranked(base, s)) {
        competitive_match(base, s, u, m, type, coop);
        save = false;   // competitive results are not saved here
    }
    if (save) u3h_publish(base, s, u3h_lg_profile(base, u));
    u3x_pm_lb_submits(base, s, u);
}

// The weapon statistics block of the match: the weapons that were used, each with its name, number and value.
extern "C" U3X_FN void u3x_pm_weapon_stats(uintptr_t base) {
    using namespace g30b;
    uint8_t *ws = reinterpret_cast<uint8_t *>(base + WS);
    uint32_t &count = *reinterpret_cast<uint32_t *>(ws + 0x24a0);
    count = 0;
    const uint32_t entries = at<uint32_t>(base + SM + 0xe8);   // taken once: later changes of the word do not count
    for (uint32_t i = 0; i < entries; i++) {
        const uint8_t *entry = reinterpret_cast<const uint8_t *>(base + SM + 0xec) + 0x28 * static_cast<uintptr_t>(i);
        if (!rd<uint16_t>(entry, 2) || !rd<uint16_t>(entry, 4)) continue;
        const uint32_t index = rd<uint8_t>(entry, 0xc);
        const uint8_t *extra = game_call<const uint8_t *>(base + 0x11f2e30, 0x9e354807u);   // looked up for every entry
        uint32_t id;
        if (index < 0x13) {
            id = at<uint32_t>(base + 0x13a6170 + 4 * index);   // the game's own weapon ids
        } else {
            if (!extra || index - 0x13 >= rd<uint32_t>(extra, 0)) continue;
            id = rd<const uint32_t *>(extra, 8)[index - 0x13];
        }
        game_call<void>(base + 0x748ef0, ws, id);
    }
    const uint32_t weapons = count;
    for (uint32_t i = 0; i < weapons; i++) {
        uint8_t *out = ws + 0xa0 * static_cast<uintptr_t>(i);
        for (uint32_t k = 0; k < sizeof weapon_stats_name; k++) out[0x1118 + k] = static_cast<uint8_t>(weapon_stats_name[k]);
        *reinterpret_cast<uint32_t *>(out + 0x112c) = rd<uint8_t>(ws, 0x24a4 + 4 * static_cast<uintptr_t>(i)) + 100u;
        *reinterpret_cast<uint64_t *>(out + 0x1130) = rd<uint64_t>(ws, 0x2528 + 8 * static_cast<uintptr_t>(i));
    }
}

// The leaderboard blob of local user u and the scores of the kind of match.
extern "C" U3X_FN void u3x_pm_lb_submits(uintptr_t base, void *s, uint32_t u) {
    uint8_t *profile = u3h_lg_profile(base, u);
    const uint8_t *r = u3h_lg_rec(base, s, profile);
    uint32_t words[16];   // the 0x40-byte blob
    for (uint32_t i = 0; i < 16; i++) words[i] = 0;
    words[0] = game_call<uint32_t>(base + 0x781ea0, profile);   // rank word
    words[1] = rd<uint32_t>(r, 0x1f30) + rd<uint32_t>(r, 0x1f28);
    words[2] = rd<uint32_t>(r, 0x1c58);
    words[3] = rd<uint32_t>(r, 0x1f24);
    words[4] = rd<uint32_t>(r, 0x1eac);
    const uint8_t *blob = reinterpret_cast<const uint8_t *>(words);

    if (!u3x_lb_off()) {
        if (u3x_pm_coop(base, s)) {   // type 0x12 counts as co-op here
            u3x_lb_submit(base, s, 0x517, rd<int32_t>(r, 0x1f2c), blob);
            u3x_lb_submit(base, s, 0x515, rd<int32_t>(r, 0x1f38), blob);
        } else if (u3x_pm_ranked(base, s)) {
            u3x_lb_submit(base, s, 0x514, rd<int32_t>(r, 0x1c54), blob);
            u3x_lb_submit(base, s, 0x515, rd<int32_t>(r, 0x1f38), blob);
            u3x_lb_submit(base, s, 0x516, rd<int32_t>(r, 0x1f34), blob);
        }
    }
    ext_ea()[60] += 1;   // submit rounds
}
