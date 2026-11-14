// ---- G26: the co-op missions list, taking a mission, the time-left texts (written from spec/G26_missions.md)

extern "C" {
// the C part and other groups
int u3h_gate_closed(uintptr_t base, int32_t *ev);
int u3h_sym_ready(uintptr_t base, void *s, uint32_t id);
uint32_t u3h_active_page(uintptr_t base);
uint32_t u3h_current_tab(uintptr_t base, void *s);
const char *u3h_text(uintptr_t base, void *s, uint32_t id);
void u3h_text_num(uintptr_t base, void *s, char *buf, uint32_t size, uint32_t id, int32_t value);
int32_t u3h_icon_index(uintptr_t base, uint32_t id);
void u3h_set_icon(uintptr_t base, int32_t *ev, uint32_t id);
void u3h_show_dialog(uintptr_t base, void *s, uint32_t body, uint32_t title, uint32_t action, int32_t buttons,
                     uint32_t extra, uint32_t close);
uint32_t u3h_crc_sid(const char *name);

// this group
int u3x_missions_source(uintptr_t base, int32_t *ev, void *s);
int u3x_take_mission(uint8_t *owner, uint8_t *profile, const uint8_t *set, uint32_t row,
                     uint8_t (*can)(uint8_t *owner, uint8_t *mission, int flag),
                     int (*done)(uint8_t *profile, uint32_t id), int64_t (*clock)(int64_t *out));
uint8_t *u3x_mission_set(uintptr_t base, void *s, int32_t set);
int32_t u3x_mission_seconds_left(const uint8_t *pr);
void u3x_x79_num(char *o, int32_t v);
void u3x_mission_time_text(uintptr_t base, void *s, int32_t t, char *o);
uint8_t *u3x_mission_obj(uintptr_t base);
uint8_t *u3x_mission_profile(uintptr_t base);
int u3x_mission_finished(const uint8_t *pr, uint32_t id);
int32_t u3x_player_level(uintptr_t base, void *s, uint8_t *pr);
}

namespace g26 {
namespace {

// the helper's own texts and the texture names of the three row icons
U3X_RO const char COLOUR_MET[] = "|@Cff7cce96|";
U3X_RO const char COLOUR_NOT_MET[] = "|@Cfffe7c66|";
U3X_RO const char DOLLAR[] = "$";
U3X_RO const char SPACE[] = " ";
U3X_RO const char ICON_COMPLETED[] = "general/net/menu-icons/icon-check";
U3X_RO const char ICON_ACTIVE[] = "general/net/message-star/default";
U3X_RO const char ICON_LOCKED[] = "general/net/menu-icons/icon-locked";

// game data (image offsets)
constexpr uintptr_t IMG_MISSION_USER = 0x17c23a0;
constexpr uintptr_t IMG_MISSION_OBJECTS = 0x17da200;
constexpr uintptr_t IMG_PROFILES = 0x17dd610;
constexpr uintptr_t IMG_LEVEL_OVERRIDE = 0x17d7abc;
constexpr uintptr_t IMG_LAN = 0x17d5c60;
constexpr uintptr_t IMG_TEXT_DB = 0x17026b8;

// state object
constexpr uintptr_t S_BASE = 760;
constexpr uintptr_t S_TEXT = 0x2c80;
constexpr uintptr_t S_SHOWN_SET = 0x3904;

// mission object, profile object, mission set, mission entry
constexpr uintptr_t OBJ_SET = 0x100c, OBJ_SELECTED = 0x1010;
constexpr uintptr_t PR_CASH = 0x74c, PR_COMPLETED = 0xecc, PR_PROGRESS = 0xf4c, PR_ACTIVE = 0x1864, PR_START = 0x1868;
constexpr uintptr_t SET_COUNT = 8, SET_MISSIONS = 0x18;
constexpr uintptr_t M_ID = 0, M_NAME = 4, M_GOAL_TEXT = 8, M_GOAL_KIND = 0x10, M_GOAL_COUNT = 0x14, M_DURATION = 0x18,
                    M_LEVEL = 0x20, M_REWARD = 0x24, M_COST = 0x44, M_SIZE = 0x4c;

constexpr uint32_t SOURCE_TAG = 0x4c414ed9;
constexpr uint32_t SYM_MISSION_TABLE = 0x4360c988;
constexpr uint32_t PAGE_MISSIONS = 0xdfdb36fa;
constexpr uint32_t GOAL_KIND_TEXT = 0x5c494554;
constexpr int32_t WINDOW_SECONDS = 86400;   // a mission runs 24 hours
constexpr int32_t LAN_LEVEL = 74;           // LAN play counts as at least the top level

// texts with number placeholders
constexpr uint32_t TEXT_MINUTE = 0x9d59ec8a, TEXT_MINUTES = 0x38e8ba93, TEXT_HOUR = 0x669b8ea8, TEXT_HOURS = 0x5879aefb;
constexpr uint32_t TEXT_HOUR_MINUTE = 0x7ce24100, TEXT_HOUR_MINUTES = 0x422d0b5d, TEXT_HOURS_MINUTE = 0xe8f6e6ab,
                   TEXT_HOURS_MINUTES = 0x6093ed1f;
constexpr uint32_t TEXT_GOAL_COUNT = 0x4afc6214;

using SymbolFn = uint8_t *(*)(uint32_t id);                                                     // native 13
using RankFn = uint32_t (*)(uint8_t *profile);                                                  // native 21
using NumberTextFn = void (*)(uintptr_t db, char *buf, uint32_t size, uint32_t id, int64_t first, int64_t second);   // 38
using CanFn = uint8_t (*)(uint8_t *owner, uint8_t *mission, int flag);                          // native 39
using ClockFn = int64_t (*)(int64_t *out);

// native k of the state object, as it is now (may be null)
template <typename F> [[gnu::always_inline]] inline F native(const void *s, int k) { return rd<F>(s, 768 + 8 * k); }

// a field to write
template <typename T> [[gnu::always_inline]] inline T &field(void *p, uintptr_t off) {
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + off);
}

U3X_FN char *put_text(char *p, const char *t) {
    while (*t) *p++ = *t++;
    return p;
}

// decimal digits of a magnitude, with a minus sign in front and a comma between groups of three when asked
U3X_FN char *put_digits(char *p, uint32_t magnitude, bool negative, bool grouped) {
    char digit[10];
    int n = 0;
    do {
        digit[n++] = static_cast<char>('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude);
    if (negative) *p++ = '-';
    while (n) {
        *p++ = digit[--n];
        if (grouped && n && n % 3 == 0) *p++ = ',';
    }
    return p;
}

U3X_FN char *put_signed(char *p, int32_t v, bool grouped) {
    const uint32_t u = static_cast<uint32_t>(v);
    return put_digits(p, v < 0 ? 0u - u : u, v < 0, grouped);
}

// one call of native 21 (the caller has tested the pointer): the whole rank word as a signed number
U3X_FN int32_t rank_word(void *s, uint8_t *profile) { return static_cast<int32_t>(native<RankFn>(s, 21)(profile)); }

// what a request needs to know about the player
struct Player {
    uint8_t *profile;
    uint32_t active;   // id of the active mission
    int32_t level;
    uint32_t cash;
};

U3X_FN bool level_too_low(const Player &pl, const uint8_t *mission) {
    return static_cast<int32_t>(static_cast<uint32_t>(pl.level) + 1) < rd<int32_t>(mission, M_LEVEL);
}

U3X_FN void show_icon(uintptr_t base, int32_t *ev, const char *texture) {
    const uint32_t id = u3h_crc_sid(texture);
    if (u3h_icon_index(base, id) >= 0) u3h_set_icon(base, ev, id);
}

// operation 2
U3X_FN void row_text(uintptr_t base, int32_t *ev, void *s, const Player &pl, const uint8_t *mission) {
    char *const buf = &field<char>(s, S_TEXT);
    const char *answer = buf;
    char *p = buf;
    switch (static_cast<uint32_t>(ev[0x15])) {
    case 0:   // name
        answer = u3h_text(base, s, rd<uint32_t>(mission, M_NAME));
        break;
    case 0xbee56a68:   // required level
        p = put_text(p, level_too_low(pl, mission) ? COLOUR_NOT_MET : COLOUR_MET);
        *put_signed(p, rd<int32_t>(mission, M_LEVEL), false) = 0;
        break;
    case 0xed9203d6: {   // cost
        const uint32_t cost = rd<uint32_t>(mission, M_COST);
        p = put_text(p, pl.cash < cost ? COLOUR_NOT_MET : COLOUR_MET);
        p = put_text(p, DOLLAR);
        *put_digits(p, cost, false, true) = 0;
        break;
    }
    case 0xa6ed5ba5:   // reward
        p = put_text(p, COLOUR_MET);
        p = put_text(p, DOLLAR);
        *put_signed(p, rd<int32_t>(mission, M_REWARD), true) = 0;
        break;
    case 0x8dc65186: {   // duration
        const int32_t n = rd<int32_t>(mission, M_DURATION);
        u3h_text_num(base, s, buf, 0x40, n == 1 ? TEXT_MINUTE : TEXT_MINUTES, n);
        u3x_x79_num(buf, n);
        break;
    }
    case 0x7046d624:   // goal
        u3h_text_num(base, s, buf, 0x100, rd<uint32_t>(mission, M_GOAL_TEXT), rd<int32_t>(mission, M_GOAL_COUNT));
        break;
    case 0x4bdd7c07:   // goal count
        if (rd<uint32_t>(mission, M_GOAL_KIND) == GOAL_KIND_TEXT)
            u3h_text_num(base, s, buf, 0x80, TEXT_GOAL_COUNT, rd<int32_t>(mission, M_GOAL_COUNT));
        else
            *put_signed(p, rd<int32_t>(mission, M_GOAL_COUNT), false) = 0;
        break;
    case 0xdda1fdb4: {   // time left, only for the active mission
        if (rd<uint32_t>(mission, M_ID) != pl.active) return;
        const int32_t seconds = u3x_mission_seconds_left(pl.profile);
        u3x_mission_time_text(base, s, seconds, buf);
        break;
    }
    default:
        return;
    }
    field<const char *>(ev, 0x40) = answer;
}

// operation 4 (image slot 0 only)
U3X_FN void row_icon(uintptr_t base, int32_t *ev, const Player &pl, const uint8_t *mission) {
    const uint32_t id = rd<uint32_t>(mission, M_ID);
    if (u3x_mission_finished(pl.profile, id)) show_icon(base, ev, ICON_COMPLETED);
    else if (id == pl.active) show_icon(base, ev, ICON_ACTIVE);
    else if (level_too_low(pl, mission)) show_icon(base, ev, ICON_LOCKED);
}

// operation 11
U3X_FN void row_value(int32_t *ev, const Player &pl, const uint8_t *mission) {
    const uint32_t id = rd<uint32_t>(mission, M_ID);
    switch (static_cast<uint32_t>(ev[0x13])) {
    case 0xe0bf7062:   // is the active mission
        ev[0xe] = id == pl.active;
        break;
    case 0x4bdd7c07:   // goal count
        ev[0xe] = rd<int32_t>(mission, M_GOAL_COUNT);
        break;
    case 0x63f35e57:   // progress
        ev[0xe] = id == pl.active && pl.active ? rd<int32_t>(pl.profile, PR_PROGRESS) : 0;
        break;
    case 0x464129ab:   // is completed
        ev[0xe] = u3x_mission_finished(pl.profile, id);
        break;
    }
}

// operation 0x16: the dialog for the selected mission
U3X_FN void click(uintptr_t base, void *s, uint8_t *object, uint32_t active, uint8_t *mission) {
    if (rd<uint32_t>(mission, M_ID) == active) {
        u3h_show_dialog(base, s, 0x84538677, 0x0260a263, 0xe6452a77, 0x075bc82d, 0x1bc6f0e5, 0);   // cancel it
        return;
    }
    const CanFn can = native<CanFn>(s, 39);
    if (!can || !can(object, mission, 0)) return;
    u3h_show_dialog(base, s, active ? 0x2aac16e8 : 0x6748cb03, 0x0260a263, 0xea1f40c5, 0x075bc82d, 0x1dd02cc2, 0);
}

}  // namespace
}  // namespace g26

extern "C" {

// ---- the four small accessors

U3X_FN uint8_t *u3x_mission_obj(uintptr_t base) {
    const uint32_t user = at<uint32_t>(base + g26::IMG_MISSION_USER) & 3;
    return reinterpret_cast<uint8_t *>(base + g26::IMG_MISSION_OBJECTS + 0x1018 * user);
}

U3X_FN uint8_t *u3x_mission_profile(uintptr_t base) {
    uint8_t *const profiles = at<uint8_t *>(base + g26::IMG_PROFILES);
    if (!profiles) return nullptr;
    return profiles + 0x7b00 * (at<uint32_t>(base + g26::IMG_MISSION_USER) & 3);
}

U3X_FN int u3x_mission_finished(const uint8_t *pr, uint32_t id) {
    if (!pr || id >= 0x400) return 0;
    return rd<uint8_t>(pr, g26::PR_COMPLETED + id / 8) >> (id & 7) & 1;
}

U3X_FN int32_t u3x_player_level(uintptr_t base, void *s, uint8_t *pr) {
    const int32_t override_level = at<int32_t>(base + g26::IMG_LEVEL_OVERRIDE);
    if (override_level) return override_level - 1;
    if (!pr || !g26::native<g26::RankFn>(s, 21)) return 0;
    return g26::rank_word(s, pr);
}

// ---- the mission set of a number, out of the game's mission table

U3X_FN uint8_t *u3x_mission_set(uintptr_t base, void *s, int32_t set) {
    if (!u3h_sym_ready(base, s, g26::SYM_MISSION_TABLE)) return nullptr;
    const g26::SymbolFn symbol = g26::native<g26::SymbolFn>(s, 13);
    const uint8_t *const table = symbol ? symbol(g26::SYM_MISSION_TABLE) : nullptr;
    if (!table || set < 0 || static_cast<uint32_t>(set) >= rd<uint32_t>(table, 0)) return nullptr;
    return rd<uint8_t *>(table, 8) + 0x20 * static_cast<uintptr_t>(set);
}

// ---- seconds left of the active mission's 24 hours

U3X_FN int32_t u3x_mission_seconds_left(const uint8_t *pr) {
    const uint32_t start = pr ? rd<uint32_t>(pr, g26::PR_START) : 0;
    if (!start) return 0;
    const g26::ClockFn clock = rd<g26::ClockFn>(ext_eb(), 4 * 4);
    if (!clock) return 0;
    const uint64_t now = static_cast<uint64_t>(clock(nullptr));
    const int64_t left = static_cast<int64_t>(g26::WINDOW_SECONDS - (now - start));
    if (left < 0) return 0;
    if (left > g26::WINDOW_SECONDS) return g26::WINDOW_SECONDS;
    return static_cast<int32_t>(left);
}

// ---- a text without any digit gets the number put in front

U3X_FN void u3x_x79_num(char *o, int32_t v) {
    for (const char *c = o; *c; ++c)
        if (*c >= '0' && *c <= '9') return;
    char old[40];
    int n = 0;
    for (; n < 40 && o[n]; ++n) old[n] = o[n];
    char *p = g26::put_signed(o, v, false);
    p = g26::put_text(p, g26::SPACE);
    for (int i = 0; i < n; ++i) *p++ = old[i];
    *p = 0;
}

// ---- "h hours m minutes" of a time in seconds

U3X_FN void u3x_mission_time_text(uintptr_t base, void *s, int32_t t, char *o) {
    o[0] = 0;
    const g26::NumberTextFn write = g26::native<g26::NumberTextFn>(s, 38);
    if (!write) return;
    const int32_t hours = t / 3600, minutes = t / 60 % 60;
    const bool one_number = hours == 0 || minutes == 0;
    uint32_t id;
    if (hours == 0) id = minutes == 1 ? g26::TEXT_MINUTE : g26::TEXT_MINUTES;
    else if (minutes == 0) id = hours == 1 ? g26::TEXT_HOUR : g26::TEXT_HOURS;
    else if (hours == 1) id = minutes == 1 ? g26::TEXT_HOUR_MINUTE : g26::TEXT_HOUR_MINUTES;
    else id = minutes == 1 ? g26::TEXT_HOURS_MINUTE : g26::TEXT_HOURS_MINUTES;
    const int32_t first = hours == 0 ? minutes : hours;
    write(base + g26::IMG_TEXT_DB, o, 0x40, id, first, one_number ? 0 : minutes);
    if (one_number) u3x_x79_num(o, first);
}

// ---- the missions list

U3X_FN int u3x_missions_source(uintptr_t base, int32_t *ev, void *s) {
    const uint8_t *const descriptor = rd<const uint8_t *>(ev, 0x98);
    if (!descriptor || rd<uint32_t>(descriptor, 0x1c) != g26::SOURCE_TAG) return 0;
    if (u3h_gate_closed(base, ev)) return 1;
    g26::field<uint64_t>(s, g26::S_BASE) = base;

    uint8_t *const object = u3x_mission_obj(base);
    uint8_t *const profile = u3x_mission_profile(base);
    int32_t &selected = g26::field<int32_t>(object, g26::OBJ_SELECTED);
    int32_t &shown = g26::field<int32_t>(s, g26::S_SHOWN_SET);

    // the page's tab picks the set; a new set starts at its first row
    if (u3h_active_page(base) == g26::PAGE_MISSIONS) {
        const uint32_t tab = u3h_current_tab(base, s);
        const int32_t wanted = tab == 0x102fc82e ? 1 : tab == 0x35fa8d17 ? 2 : 0;
        if (wanted != shown) {
            selected = 0;
            shown = wanted;
        }
    }
    g26::field<int32_t>(object, g26::OBJ_SET) = shown;

    // the operation (word 0) is read from the request where it is used: here after the set lookup, and again below
    const uint8_t *const set = u3x_mission_set(base, s, shown);
    if (!set) {
        if (static_cast<uint32_t>(ev[0]) == 6) ev[1] = -1;
        return 1;
    }

    const uint32_t count = rd<uint32_t>(set, g26::SET_COUNT);
    uint8_t *const missions = rd<uint8_t *>(set, g26::SET_MISSIONS);
    const int32_t row = ev[1];
    const uint8_t *const mission = missions && row >= 0 && static_cast<uint32_t>(row) < count
                                       ? missions + g26::M_SIZE * static_cast<uintptr_t>(row) : nullptr;

    g26::Player pl;
    pl.profile = profile;
    pl.active = profile ? rd<uint32_t>(profile, g26::PR_ACTIVE) : 0;
    // the level: the override or the rank, then the rank when it is higher (asked again), then the LAN minimum
    pl.level = u3x_player_level(base, s, profile);
    // native 21 is read once here; the second and the third call both go through this value
    const g26::RankFn rank = g26::native<g26::RankFn>(s, 21);
    if (profile && rank && static_cast<int32_t>(rank(profile)) > pl.level)
        pl.level = static_cast<int32_t>(rank(profile));
    if (at<uint8_t>(base + g26::IMG_LAN) && pl.level < g26::LAN_LEVEL) pl.level = g26::LAN_LEVEL;
    pl.cash = profile ? rd<uint32_t>(profile, g26::PR_CASH) : 0;
    g26::field<char>(s, g26::S_TEXT) = 0;

    switch (static_cast<uint32_t>(ev[0])) {   // read again, after the native 21 calls
    case 2:
        if (mission) g26::row_text(base, ev, s, pl, mission);
        break;
    case 4:
        if (mission && ev[0x14] == 0) g26::row_icon(base, ev, pl, mission);
        break;
    case 5:
        ev[1] = selected;
        break;
    case 6:
        ev[1] = static_cast<int32_t>(count - 1);
        break;
    case 10:
        selected = row;
        break;
    case 11:
        if (mission) g26::row_value(ev, pl, mission);
        break;
    case 0x16: {   // the click works on the selected row, not on the request's
        const uint32_t pick = static_cast<uint32_t>(selected);
        if (missions && pick < count) g26::click(base, s, object, pl.active, missions + g26::M_SIZE * static_cast<uintptr_t>(pick));
        break;
    }
    }
    return 1;
}

// ---- make a row of a set the player's active mission

U3X_FN int u3x_take_mission(uint8_t *owner, uint8_t *profile, const uint8_t *set, uint32_t row,
                            uint8_t (*can)(uint8_t *owner, uint8_t *mission, int flag),
                            int (*done)(uint8_t *profile, uint32_t id), int64_t (*clock)(int64_t *out)) {
    if (!profile || !set || !can) return 0;
    uint8_t *const missions = rd<uint8_t *>(set, g26::SET_MISSIONS);
    if (!missions || row >= rd<uint32_t>(set, g26::SET_COUNT)) return 0;
    uint8_t *const mission = missions + g26::M_SIZE * static_cast<uintptr_t>(row);
    const uint32_t id = rd<uint32_t>(mission, g26::M_ID);
    if (!can(owner, mission, 0)) return 0;
    if (done(profile, id)) return 0;
    g26::field<uint32_t>(profile, g26::PR_ACTIVE) = id;
    g26::field<uint32_t>(profile, g26::PR_PROGRESS) = 0;
    if (clock) g26::field<uint32_t>(profile, g26::PR_START) = static_cast<uint32_t>(clock(nullptr));
    return 1;
}

}  // extern "C"
