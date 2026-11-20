// G44 part A: kill reward accessors and points (written from spec G44_kill_rewards.md, sections 4.1 to 4.14).

extern "C" {
// other groups / the C part (spec 2.4, 4.13)
uint32_t u3x_net_player_id(const uint8_t *np);
int u3x_score_member(const uint8_t *S, uint32_t id);
int32_t u3h_kr_mode_type(uintptr_t base);
uint8_t *u3h_kr_prec(uint8_t *S, uint8_t *np);
int u3x_kr_30ffa8(uintptr_t base, const uint8_t *np, int b);

// this part
uint8_t *u3x_kr_np_by_id(uintptr_t base, uint32_t id);
int u3x_kr_friendly(uintptr_t base, const uint8_t *a, const uint8_t *b);
int u3x_kr_30feec(uintptr_t base);
uint8_t *u3x_kr_prec(uint8_t *S, const uint8_t *np);
void u3x_kr_stat(uintptr_t base, void *s, uint32_t id, uint32_t amount, uint8_t *np);
uint32_t u3x_kr_sid_cat(uintptr_t base, uint32_t h, const char *text);
void u3x_kr_prof_add(uintptr_t base, const uint8_t *np, uint32_t n, uint32_t amount);
int u3x_med_np_index(uintptr_t base, uint32_t id);
uint32_t u3x_med_distance2(const uint8_t *record);
void u3x_kill_popup(uintptr_t base, void *s, uint8_t *np, uint8_t *other, const uint8_t *info, int32_t pts,
                    uint32_t type, uint32_t dbl);
void u3x_add_score(uintptr_t base, void *s, uint8_t *S, uint8_t *np, int32_t pts);
void u3x_team_score(uintptr_t base, void *s, uint8_t *S, uint32_t team, int32_t pts);
void u3x_award(uintptr_t base, void *s, uint8_t *S, uint8_t *np, int32_t pts, uint32_t type, uint32_t flag);
int32_t u3x_ai_kill_points(uintptr_t base, uint32_t w, uint32_t id, uint8_t *knp, int b);
}

namespace g44a {

// image offsets: data
constexpr uintptr_t NET_GAME_MANAGER = 0x17cb060;
constexpr uintptr_t NET_PLAYERS = 0x390;          // in the manager: ten records
constexpr uintptr_t NET_PLAYER_SIZE = 0x9c0;
constexpr int NET_PLAYER_COUNT = 10;
constexpr uintptr_t COOP_DIFFICULTY = 0x6a48;     // in the manager, u8
constexpr uintptr_t SID_STEP_TABLE = 0x13a6ff0;   // 256 u32
constexpr uintptr_t PROFILE_OBJECTS = 0x17dd610;  // pointer
constexpr uintptr_t HUD_OBJECTS = 0x1805fa0;      // 0x270 per local user
constexpr uintptr_t HUD_CLOCK = 0x17b4a90 + 0x114;
constexpr uintptr_t HUD_CURRENT_1 = 0x1806480;
constexpr uintptr_t HUD_CURRENT_2 = 0x1b24bd8;

// image offsets: game functions
constexpr uintptr_t FN_MODE_ID = 0x7424a0;
constexpr uintptr_t FN_IS_ONLINE = 0x7270f0;
constexpr uintptr_t FN_STAT_EVENT = 0x5ae4e0;
constexpr uintptr_t FN_PLACES = 0x7aa560;
constexpr uintptr_t FN_AWARD = 0x7abb70;
constexpr uintptr_t FN_SCRIPT_SYMBOL = 0x11f2e30;

// ids
constexpr int32_t MODE_TYPE_COOP = 0xd;
constexpr int32_t MODE_TYPE_FREE_FOR_ALL = 0xe;
constexpr uint32_t MODE_ID_COOP_STYLE = 0x4b720b62;
constexpr uint32_t SYMBOL_AI_KINDS = 0xa977dad3;
constexpr uint32_t SYMBOL_WEAPON_BONUSES = 0x829f99c6;

// net player record fields
constexpr uintptr_t NP_HANDLE = 0x10, NP_HANDLE_ID = 0x18, NP_USER = 0x1e8, NP_TEAM = 0x21c;

// the helper's state block and the switch word of the state object
constexpr uintptr_t X_STAT_COUNT = 0x2bc8, X_STAT_RING_INDEX = 0x2bcc, X_PROFILE_ADDS = 0x2be4, X_STAT_RING = 0x2c00;
constexpr uintptr_t X_DUMMY_RECORD = 0x2c40;   // score record of every player who is not in the member table
constexpr uintptr_t X_SCORE_ADDS = 0x2d70, X_TEAM_ADDS = 0x2d74, X_AWARDS = 0x2d78, X_AWARD_SUM = 0x2d7c;
constexpr uintptr_t X_POPUPS_ASKED = 0x2d80, X_POPUP_POINTS = 0x2d84, X_POPUPS_SHOWN = 0x2d88;
constexpr uintptr_t STATE_SWITCHES = 0x3d78;
constexpr uint32_t NO_STATS = 0x1000, NO_POINTS = 0x4000, NO_POPUPS = 0x8000;

// score keeper
constexpr uintptr_t TEAM_RECORDS = 0x574c, PLAYER_RECORDS = 0x60cc, SCORE_RECORD_SIZE = 0x130;

// popup slots and placement frames
constexpr int FRAME_WORDS = 24;
constexpr uintptr_t CHARACTER_FRAME = 0xb0, POPUP_OWNER = 0x238, POPUP_SLOTS = 0x2690, POPUP_SLOT_SIZE = 0x70;
constexpr int POPUP_SLOT_COUNT = 12;

constexpr uint32_t F_SIXTEENTH = 0x3d800000u, F_ONE_AND_HALF = 0x3fc00000u, F_TWO = 0x40000000u;


[[gnu::always_inline]] inline uint32_t &word(uint8_t *p, uintptr_t off) { return *reinterpret_cast<uint32_t *>(p + off); }
[[gnu::always_inline]] inline uint32_t switches(const void *s) { return rd<uint32_t>(s, STATE_SWITCHES); }
[[gnu::always_inline]] inline uint8_t *net_player(uintptr_t base, int i) {
    return reinterpret_cast<uint8_t *>(base + NET_GAME_MANAGER + NET_PLAYERS + NET_PLAYER_SIZE * static_cast<uintptr_t>(i));
}

// index of the first net player record with this id, or -1
U3X_FN int find_net_player(uintptr_t base, uint32_t id) {
    if (!id) return -1;
    for (int i = 0; i < NET_PLAYER_COUNT; i++)
        if (u3x_net_player_id(net_player(base, i)) == id) return i;
    return -1;
}

// the character a net player's handle points at, or null when the handle is empty or stale
U3X_FN const uint8_t *character_of(const uint8_t *np) {
    const uint8_t *const *handle = rd<const uint8_t *const *>(np, NP_HANDLE);
    const uint32_t handle_id = rd<uint32_t>(np, NP_HANDLE_ID);
    if (!handle || !handle_id) return nullptr;
    const uint8_t *character = *handle;
    if (!character || rd<uint32_t>(character, 0x7c) != handle_id) return nullptr;
    return character;
}

// adds the low 16 bits of the points to both score copies of a record
[[gnu::always_inline]] inline void add_to_scores(uint8_t *record, int32_t pts) {
    uint16_t *score = reinterpret_cast<uint16_t *>(record + 2);
    score[0] = static_cast<uint16_t>(score[0] + static_cast<uint16_t>(pts));
    score[1] = static_cast<uint16_t>(score[1] + static_cast<uint16_t>(pts));
}

// one coordinate pair of a kill record: (killer - victim) in metres, squared
[[gnu::always_inline]] inline uint32_t axis_square(const uint8_t *record, uintptr_t axis) {
    const uint32_t killer = f_from_int(rd<int16_t>(record, 0xe + 2 * axis));
    const uint32_t victim = f_from_int(rd<int16_t>(record, 0x14 + 2 * axis));
    const uint32_t d = f_mul(f_sub(killer, victim), F_SIXTEENTH);
    return f_mul(d, d);
}

}  // namespace g44a

// 4.1: the net player record with this id
extern "C" U3X_FN uint8_t *u3x_kr_np_by_id(uintptr_t base, uint32_t id) {
    const int i = g44a::find_net_player(base, id);
    return i < 0 ? nullptr : g44a::net_player(base, i);
}

// 4.8: its index
extern "C" U3X_FN int u3x_med_np_index(uintptr_t base, uint32_t id) {
    return g44a::find_net_player(base, id);
}

// 4.2: are two players on the same side
extern "C" U3X_FN int u3x_kr_friendly(uintptr_t base, const uint8_t *a, const uint8_t *b) {
    if (a == b) return 1;
    if (u3h_kr_mode_type(base) == g44a::MODE_TYPE_FREE_FOR_ALL) return 0;
    return a && b && rd<int32_t>(a, g44a::NP_TEAM) == rd<int32_t>(b, g44a::NP_TEAM);
}

// 4.3: is the current mode a co-op-style one
extern "C" U3X_FN int u3x_kr_30feec(uintptr_t base) {
    if (u3h_kr_mode_type(base) == g44a::MODE_TYPE_COOP) return 1;
    return game_call<uint32_t>(base + g44a::FN_MODE_ID, base + g44a::NET_GAME_MANAGER) == g44a::MODE_ID_COOP_STYLE;
}

// 4.4: a player's score record; the shared dummy record for anyone the score keeper does not know
extern "C" U3X_FN uint8_t *u3x_kr_prec(uint8_t *S, const uint8_t *np) {
    if (np) {
        const int member = u3x_score_member(S, u3x_net_player_id(np));
        if (member >= 0) return S + g44a::PLAYER_RECORDS + g44a::SCORE_RECORD_SIZE * static_cast<uintptr_t>(member);
    }
    return u3h_ext() + g44a::X_DUMMY_RECORD;
}

// 4.5: a stat event, noted in the diagnostics ring first
extern "C" U3X_FN void u3x_kr_stat(uintptr_t base, void *s, uint32_t id, uint32_t amount, uint8_t *np) {
    uint8_t *ext = u3h_ext();
    g44a::word(ext, g44a::X_STAT_COUNT)++;
    const uint32_t r = g44a::word(ext, g44a::X_STAT_RING_INDEX);
    g44a::word(ext, g44a::X_STAT_RING_INDEX) = r + 1;
    g44a::word(ext, g44a::X_STAT_RING + 4 * (r & 15)) = id;
    if (g44a::switches(s) & g44a::NO_STATS) return;
    game_call<void, void *, uint32_t, uint32_t, uint8_t *, uint32_t>(base + g44a::FN_STAT_EVENT, nullptr, id, amount, np, 0u);
}

// 4.6: continues a string id over more characters, with the game's own step table
extern "C" U3X_FN uint32_t u3x_kr_sid_cat(uintptr_t base, uint32_t h, const char *text) {
    const uint32_t *table = reinterpret_cast<const uint32_t *>(base + g44a::SID_STEP_TABLE);
    for (; *text; text++) h = (h << 8) ^ table[(h >> 24) ^ static_cast<uint8_t>(*text)];
    return h;
}

// 4.7: adds to counter n of the player's profile (online games only)
extern "C" U3X_FN void u3x_kr_prof_add(uintptr_t base, const uint8_t *np, uint32_t n, uint32_t amount) {
    if (!game_call<uint8_t>(base + g44a::FN_IS_ONLINE, base + g44a::NET_GAME_MANAGER)) return;
    uint8_t *profiles = at<uint8_t *>(base + g44a::PROFILE_OBJECTS);
    if (!profiles) return;
    const uintptr_t user = rd<uint32_t>(np, g44a::NP_USER);
    // 4 * n wraps in 32 bits before it is widened
    g44a::word(profiles, 0x7b00 * user + 0x250 + static_cast<uintptr_t>(4u * n)) += amount;
    g44a::word(u3h_ext(), g44a::X_PROFILE_ADDS)++;
}

// 4.9: squared distance between killer and victim of a kill record, in metres (float pattern)
extern "C" U3X_FN uint32_t u3x_med_distance2(const uint8_t *record) {
    uint32_t sum = 0;
    for (uintptr_t axis = 0; axis < 3; axis++) sum = f_add(sum, g44a::axis_square(record, axis));
    return sum;
}

// 4.10: the "+points" popup of local player np, placed over other's character (or at the kill message's position)
extern "C" U3X_FN void u3x_kill_popup(uintptr_t base, void *s, uint8_t *np, uint8_t *other, const uint8_t *info,
                                      int32_t pts, uint32_t type, uint32_t dbl) {
    uint8_t *ext = u3h_ext();
    g44a::word(ext, g44a::X_POPUPS_ASKED)++;
    g44a::word(ext, g44a::X_POPUP_POINTS) = static_cast<uint32_t>(pts);
    if ((g44a::switches(s) & (g44a::NO_POPUPS | g44a::NO_POINTS)) || !np) return;

    uint8_t *hud = reinterpret_cast<uint8_t *>(base + g44a::HUD_OBJECTS + 0x270 * static_cast<uintptr_t>(rd<uint32_t>(np, g44a::NP_USER)));
    uint8_t *owner = rd<uint8_t *>(hud, g44a::POPUP_OWNER);
    if (!owner) return;

    const uint8_t *source = other ? g44a::character_of(other) : nullptr;
    const bool moved = !source;
    if (moved) source = g44a::character_of(np);
    if (!source) return;

    // the placement frame: the character's, at the kill position when moved, raised by 1.5
    uint32_t frame[g44a::FRAME_WORDS];
    for (int i = 0; i < g44a::FRAME_WORDS; i++) frame[i] = rd<uint32_t>(source, g44a::CHARACTER_FRAME + 4 * i);
    if (moved) {
        // v7.218 (from the v7.217 post-link fix): the victim's position is the attack info's +0x10 when that vector is
        // the victim's (its w repeats its y and its x is not 0), else +0x20
        const uintptr_t at_info = rd<uint32_t>(info, 0x14) == rd<uint32_t>(info, 0x1c) && rd<uint32_t>(info, 0x10) ? 0x10 : 0x20;
        for (int i = 0; i < 3; i++) frame[8 + i] = rd<uint32_t>(info, at_info + 4 * i);
    }
    frame[9] = f_add(frame[9], g44a::F_ONE_AND_HALF);
    // v7.218 (from the v7.217 post-link fix): the popup is not bound to the character (words 2..3, which the HUD would
    // follow every frame) and has no local offset (words 16..19, which pushed it off screen)
    frame[2] = frame[3] = 0;
    for (int i = 16; i < 20; i++) frame[i] = 0;

    for (int k = 0; k < g44a::POPUP_SLOT_COUNT; k++) {
        uint8_t *slot = owner + g44a::POPUP_SLOTS + g44a::POPUP_SLOT_SIZE * k;
        if (rd<int32_t>(slot, 0x60) != -1) continue;
        at<uint8_t *>(base + g44a::HUD_CURRENT_1) = hud;
        at<uint8_t *>(base + g44a::HUD_CURRENT_2) = hud;
        for (int i = 0; i < g44a::FRAME_WORDS; i++) g44a::word(slot, 4 * i) = frame[i];
        *reinterpret_cast<int32_t *>(slot + 0x60) = pts;
        g44a::word(slot, 0x64) = at<uint32_t>(base + g44a::HUD_CLOCK);
        slot[0x68] = static_cast<uint8_t>(type);
        slot[0x69] = static_cast<uint8_t>(dbl);
        g44a::word(ext, g44a::X_POPUPS_SHOWN)++;
        return;
    }
}

// 4.11: points for a player
extern "C" U3X_FN void u3x_add_score(uintptr_t base, void *s, uint8_t *S, uint8_t *np, int32_t pts) {
    if (!np || (g44a::switches(s) & g44a::NO_POINTS)) return;
    g44a::add_to_scores(u3h_kr_prec(S, np), pts);
    g44a::word(u3h_ext(), g44a::X_SCORE_ADDS)++;
    game_call<void>(base + g44a::FN_PLACES, S);
}

// 4.12: points for a team (only the lowest bit of the team number counts)
extern "C" U3X_FN void u3x_team_score(uintptr_t base, void *s, uint8_t *S, uint32_t team, int32_t pts) {
    if (g44a::switches(s) & g44a::NO_POINTS) return;
    g44a::add_to_scores(S + g44a::TEAM_RECORDS + g44a::SCORE_RECORD_SIZE * (team & 1), pts);
    g44a::word(u3h_ext(), g44a::X_TEAM_ADDS)++;
    game_call<void>(base + g44a::FN_PLACES, S);
}

// 4.13: the game's award (cash, XP, missions) for a player; member index -1 for a null or unknown player
extern "C" U3X_FN void u3x_award(uintptr_t base, void *s, uint8_t *S, uint8_t *np, int32_t pts, uint32_t type, uint32_t flag) {
    if (g44a::switches(s) & g44a::NO_POINTS) return;
    const int member = np ? u3x_score_member(S, u3x_net_player_id(np)) : -1;
    uint8_t *ext = u3h_ext();
    g44a::word(ext, g44a::X_AWARDS)++;
    g44a::word(ext, g44a::X_AWARD_SUM) += static_cast<uint32_t>(pts);
    const uint32_t points16 = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(pts)));
    game_call<void>(base + g44a::FN_AWARD, S, static_cast<uint32_t>(member), points16, type, flag);
}

// 4.14: points for a killed AI of kind id, killed with weapon w
extern "C" U3X_FN int32_t u3x_ai_kill_points(uintptr_t base, uint32_t w, uint32_t id, uint8_t *knp, int b) {
    // the kind's points (entries of 0xc bytes: kind id, ., points)
    const uint8_t *kinds = game_call<const uint8_t *>(base + g44a::FN_SCRIPT_SYMBOL, g44a::SYMBOL_AI_KINDS);
    const uint8_t *kind = nullptr;
    if (kinds) {
        const int32_t count = rd<int32_t>(kinds, 0);
        const uint8_t *entries = rd<const uint8_t *>(kinds, 8);
        for (int32_t i = 0; i < count && !kind; i++)
            if (rd<int32_t>(entries, 0xc * static_cast<uintptr_t>(i)) == static_cast<int32_t>(id)) kind = entries + 0xc * static_cast<uintptr_t>(i);
    }
    if (!kind) return 100;
    const uint32_t points = rd<uint32_t>(kind, 8);

    // the weapon's bonus (entries of 0x24 bytes: weapon id, bonus points, ...)
    uint32_t bonus = 0;
    const uint8_t *bonuses = game_call<const uint8_t *>(base + g44a::FN_SCRIPT_SYMBOL, g44a::SYMBOL_WEAPON_BONUSES);
    if (bonuses) {
        const int32_t count = rd<int32_t>(bonuses, 0);
        const uint8_t *entries = rd<const uint8_t *>(bonuses, 8);
        for (int32_t i = 0; i < count; i++) {
            const uint8_t *entry = entries + 0x24 * static_cast<uintptr_t>(i);
            if (rd<uint32_t>(entry, 0) == w) { bonus = rd<uint32_t>(entry, 4); break; }
        }
    }

    const int counts = u3x_kr_30ffa8(base, knp, b);
    const uint8_t difficulty = at<uint8_t>(base + g44a::NET_GAME_MANAGER + g44a::COOP_DIFFICULTY);
    const uint32_t factor = difficulty == 3 ? g44a::F_ONE_AND_HALF : difficulty == 4 ? g44a::F_TWO : F_ONE;
    const uint32_t total = f_from_int(static_cast<int32_t>(points + bonus));
    return f_to_int(f_mul(total, f_mul(f_from_int(counts), factor)));
}
