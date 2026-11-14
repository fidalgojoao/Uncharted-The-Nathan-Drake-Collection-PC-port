// G18: the friend list (set-up, fetch, add, refresh, tear-down), the own presence data, the quick-action menus of
// the friend and players-met rows, the wait popup. Written from spec/G18_friends_list.md.

// ---- functions and types of other groups, of the C part and of the shared tools (spec 2.2)
struct QuickActions { uint32_t argument, page; int32_t count; uint32_t pairs[16]; };   // existing type of the C++ part

extern "C" {
uint8_t *u3h_fr_ent(uintptr_t base, uint32_t i);
int u3h_fr_strcmp(uintptr_t base, const void *a, const void *b);
void u3h_fr_sort(uint8_t *entries, int64_t n);
uint8_t *u3h_friend_storage(int which);
int32_t u3h_friend_count(uintptr_t base);
int32_t u3h_friend_get(uintptr_t base, uint32_t i, uint8_t *id, uint8_t *presence);
int32_t u3h_presence(uintptr_t base, const uint8_t *id, uint8_t *presence);
int32_t u3h_presence_set(uintptr_t base, const uint8_t *details, uint32_t options);
uint32_t u3h_fr_key(uintptr_t base);
uint8_t *u3h_fr_pdata(uintptr_t base, uint8_t *e);
void u3h_fr_qa_push(uintptr_t base, void *q);
int u3x_quick_action_add(QuickActions *q, uint32_t text, uint32_t action);
const char *u3h_text(uintptr_t base, void *s, uint32_t id);
uint8_t *u3h_lg_profile(uintptr_t base, uint32_t user);
uint8_t *u3h_lg_rec(uintptr_t base, void *s, uint8_t *profile);
uint8_t *u3x_sym_table(void *s, uint32_t id);

// ---- this group
int u3x_strcasecmp(const char *a, const char *b);
int u3x_friend_in_session(uintptr_t base, const uint8_t *e);
uint8_t *u3x_friend_find(uintptr_t base, const void *id);
void u3x_friend_add(uintptr_t base, const uint8_t *id, const uint8_t *presence);
void u3x_friend_refresh_one(uintptr_t base, uint8_t *e);
void u3x_friend_refresh(uintptr_t base);
void u3x_friend_list_thread(uintptr_t base);
void u3x_friend_init(uintptr_t base);
void u3x_presence_clear(uintptr_t base, void *s);
void u3x_friend_shutdown(uintptr_t base, void *s);
void u3x_presence_tick(uintptr_t base, void *s);
int32_t u3x_match_time_left(uintptr_t base, void *s);
int u3x_match_end_time(uintptr_t base, void *s, int64_t *out);
void u3x_friend_click(uintptr_t base, const int32_t *ev);
void u3x_met_click(uintptr_t base, const int32_t *ev, uint8_t *target);
void u3x_wait_popup(uintptr_t base, uint32_t text_id, uintptr_t callback);
}

namespace g18 {

// image offsets of the game objects
constexpr uintptr_t FRIENDS = 0x17caeb0;        // the friend object
constexpr uintptr_t NET = 0x17d5c58;            // the net state
constexpr uintptr_t SESSION = 0x17dc298;        // the party session
constexpr uintptr_t MATCH = 0x17cb060;          // the net game manager
constexpr uintptr_t MENU = 0x17bd080;
constexpr uintptr_t GAME_CLOCK = 0x17b4ba4;
constexpr uintptr_t SECOND_CLOCK = 0x17b4ca4;
constexpr uintptr_t LOCK_SETTING = 0x17c3570;
constexpr uintptr_t SELECTED_ID = 0x17c3871;
constexpr uintptr_t SELECTED_OTHER_ID = 0x17c3898;
constexpr uintptr_t SCORE_FIRST = 0x17e338e;
constexpr uintptr_t SCORE_SECOND = 0x17e34be;

// game functions
constexpr uintptr_t FN_PARTY_SIZE = 0x7a2b10;
constexpr uintptr_t FN_MATCH_BYTE = 0x727290;
constexpr uintptr_t FN_SHOW_PAGE = 0x651940;

// sizes
constexpr uint32_t ID_SIZE = 0x24;
constexpr uint32_t ENTRY_SIZE = 0x240;
constexpr uint32_t MET_SIZE = 0x28;
constexpr uint32_t DETAILS_SIZE = 0x190;
constexpr uint32_t DATA_SIZE = 0x60;

// ids
constexpr uint32_t TEXT_TITLE = 0xfd2de693u;
constexpr uint32_t PAGE_POPUP = 0x471007a1u;
constexpr uint32_t PAGE_FRIEND_ACTIONS = 0xecc3219du;
constexpr uint32_t PAGE_DEFAULT_ACTIONS = 0x99ab57c2u;
constexpr uint32_t NET_GAME_TABLE = 0x3b9a067du;

// one clock tick in seconds is TICK_NUMERATOR / 2^31 (float pattern 0x3b5a740e); a second has 300 ticks
constexpr int64_t TICK_NUMERATOR = 7158279;
constexpr int64_t TICKS_PER_SECOND = 300;
constexpr int32_t NO_INT32 = INT32_MIN;         // what a float-to-integer conversion gives when the value does not fit

// the presence data record
struct PresenceData {
    uint32_t version;
    uint8_t activity, party_size, match_byte, locked, menu_mode, unused_9[3];
    int32_t score_first, score_second, game_index, rules;
    uint32_t unused_1c;
    int64_t end_time;
    uint64_t party_id;
    uint32_t rank, experience, statistics[8];
    uint64_t unused_58;
};
static_assert(sizeof(PresenceData) == DATA_SIZE);

template <typename T> [[gnu::always_inline]] inline T &friends(uintptr_t base, uintptr_t offset) {
    return at<T>(base + FRIENDS + offset);
}
[[gnu::always_inline]] inline uint32_t &friend_count(uintptr_t base) { return friends<uint32_t>(base, 0); }
[[gnu::always_inline]] inline uint8_t *&friend_entries(uintptr_t base) { return friends<uint8_t *>(base, 8); }
[[gnu::always_inline]] inline bool busy(uintptr_t base) { return friends<uint64_t>(base, 0xc8) != 0; }
[[gnu::always_inline]] inline uint8_t *published(uintptr_t base) { return &friends<uint8_t>(base, 0x60); }
[[gnu::always_inline]] inline bool on_lan(uintptr_t base) { return at<uint8_t>(base + NET + 8) != 0; }
[[gnu::always_inline]] inline bool friends_off() { return (ext_ea()[1] & 0x20) != 0; }

U3X_FN static void zero(uint8_t *to, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) to[i] = 0;
}

U3X_FN static void copy(uint8_t *to, const uint8_t *from, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) to[i] = from[i];
}

U3X_FN static bool same(const uint8_t *a, const uint8_t *b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++)
        if (a[i] != b[i]) return false;
    return true;
}

// a presence details block that holds nothing but the game's title; false (block all zero) when there is no text
U3X_FN static bool title_details(uintptr_t base, void *s, uint8_t *details) {
    zero(details, DETAILS_SIZE);
    const char *title = u3h_text(base, s, TEXT_TITLE);
    if (!title) return false;
    for (uint32_t i = 0; i < 0x7f && title[i]; i++) details[i] = static_cast<uint8_t>(title[i]);
    return true;
}

// rank, experience and the eight statistics words of the own profile
U3X_FN static void own_statistics(uintptr_t base, void *s, PresenceData &d) {
    uint8_t *profile = u3h_lg_profile(base, 0);
    if (!profile) return;
    const uint8_t *record = u3h_lg_rec(base, s, profile);
    if (rd<uint32_t>(profile, 0x7ae8) == 3) {   // loaded: read after the record was asked for
        d.rank = rd<uint32_t>(record, 0x4fc);
        d.experience = rd<uint32_t>(record, 0x504);
    }
    // eight consecutive words of the record, the last two in swapped order
    for (uint32_t k = 0; k < 6; k++) d.statistics[k] = rd<uint32_t>(record, 0x5a4 + 4 * k);
    d.statistics[6] = rd<uint32_t>(record, 0x5c0);
    d.statistics[7] = rd<uint32_t>(record, 0x5bc);
}

// a clock difference scaled by tick length x 300, truncated toward zero (exact: the product fits in 64 bits)
U3X_FN static int32_t ticks_from_difference(int32_t d) {
    const int64_t ticks = d * TICK_NUMERATOR * TICKS_PER_SECOND / (int64_t{1} << 31);
    return ticks < INT32_MIN || ticks > INT32_MAX ? NO_INT32 : static_cast<int32_t>(ticks);
}

// a 32-bit float (given as its pattern) of seconds x 300, truncated toward zero
U3X_FN static int32_t ticks_from_seconds(uint32_t pattern) {
    const uint32_t exponent = (pattern >> 23) & 0xff;
    uint64_t product = pattern & 0x7fffff;                 // the mantissa ...
    if (exponent == 0xff) return NO_INT32;                 // infinity, NaN
    if (exponent) product |= 0x800000;
    product *= static_cast<uint64_t>(TICKS_PER_SECOND);    // ... x 300: below 2^33
    const int shift = static_cast<int>(exponent ? exponent : 1) - 150;
    uint64_t ticks;
    if (shift >= 0) ticks = shift > 30 ? UINT64_MAX : product << shift;
    else ticks = shift < -63 ? 0 : product >> -shift;
    if (ticks > static_cast<uint64_t>(INT32_MAX)) return NO_INT32;
    return pattern >> 31 ? -static_cast<int32_t>(ticks) : static_cast<int32_t>(ticks);
}

U3X_FN static bool has_no_time_limit(int32_t type) {
    return type == 4 || type == 9 || type == 11 || type == 13 || type == 16 || type == 18;
}

// may the player join this friend's party? (the party size is asked once, and only when the first three tests hold)
U3X_FN static bool can_join(uintptr_t base, uint8_t *e) {
    const uint8_t *data = u3h_fr_pdata(base, e);
    if (!data) return false;
    if (rd<int32_t>(e, 0x1b0) != 1) return false;          // not in this game
    if (data[7]) return false;                             // party locked
    if (game_call<int>(base + FN_PARTY_SIZE, base + SESSION) >= 2 && data[4] == 2) return false;
    return data[8] != 1 && data[8] != 4;
}

}  // namespace g18

extern "C" {

// -1, 0 or 1; the letters A..Z count as a..z
U3X_FN int u3x_strcasecmp(const char *a, const char *b) {
    for (;; a++, b++) {
        uint8_t x = static_cast<uint8_t>(*a), y = static_cast<uint8_t>(*b);
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return x < y ? -1 : 1;
        if (!x) return 0;
    }
}

// 1 when the friend is a member of the own party session
U3X_FN int u3x_friend_in_session(uintptr_t base, const uint8_t *e) {
    for (uint32_t i = 0; i < 10; i++) {
        const uint8_t *member = &at<uint8_t>(base + g18::SESSION + 0x680 + i * 0x120);
        if (member[0x25] && u3h_fr_strcmp(base, member, e + 1) == 0) return 1;
    }
    return 0;
}

// the entry of the friend with this id, or null
U3X_FN uint8_t *u3x_friend_find(uintptr_t base, const void *id) {
    if (g18::busy(base)) return nullptr;
    const uint32_t n = g18::friend_count(base);
    for (uint32_t i = 0; i < n; i++) {
        uint8_t *e = g18::friend_entries(base) + static_cast<uintptr_t>(i) * g18::ENTRY_SIZE;
        if (u3h_fr_strcmp(base, e + 1, id) == 0) return e;
    }
    return nullptr;
}

// a known friend is flagged for a refresh; a new one is appended while there is room
U3X_FN void u3x_friend_add(uintptr_t base, const uint8_t *id, const uint8_t *presence) {
    const uint32_t n = g18::friend_count(base);
    for (uint32_t i = 0; i < n; i++) {
        uint8_t *e = g18::friend_entries(base) + static_cast<uintptr_t>(i) * g18::ENTRY_SIZE;
        if (u3h_fr_strcmp(base, id, e + 1) == 0) {
            e[0] = 1;
            return;
        }
    }
    if (n >= g18::friends<uint32_t>(base, 4)) return;
    g18::friend_count(base) = n + 1;
    uint8_t *e = g18::friend_entries(base) + static_cast<uintptr_t>(n) * g18::ENTRY_SIZE;
    g18::copy(e + 1, id, g18::ID_SIZE);
    if (presence) g18::copy(e + 0x28, presence, g18::DETAILS_SIZE);
    e[0] = presence ? 0 : 1;                               // without presence: read it at the next refresh
    ext_ea()[65]++;
}

// reads the presence of one friend again
U3X_FN void u3x_friend_refresh_one(uintptr_t base, uint8_t *e) {
    g18::zero(e + 0x28, g18::DETAILS_SIZE);
    if (u3h_presence(base, e + 1, e + 0x28) < 0) ext_ea()[66]++;
    e[0] = 0;
}

// re-reads every flagged friend, then sorts the list
U3X_FN void u3x_friend_refresh(uintptr_t base) {
    if (g18::busy(base)) return;
    const uint32_t n = g18::friend_count(base);
    uint8_t *entries = g18::friend_entries(base);
    for (uint32_t i = 0; i < n; i++) {
        uint8_t *e = entries + static_cast<uintptr_t>(i) * g18::ENTRY_SIZE;
        if (e[0]) u3x_friend_refresh_one(base, e);
    }
    u3h_fr_sort(entries, n);
    ext_ea()[67]++;
}

// the one-time fetch of all friends (only while the list is empty); ends the busy state
U3X_FN void u3x_friend_list_thread(uintptr_t base) {
    if (g18::friend_count(base) == 0) {
        int32_t reported = u3h_friend_count(base);
        if (reported < 0) {
            ext_ea()[68]++;
            reported = 0;
        }
        uint32_t n = static_cast<uint32_t>(reported);
        const uint32_t capacity = g18::friends<uint32_t>(base, 4);
        if (n > capacity) n = capacity;
        for (uint32_t i = 0; i < n; i++) {
            uint8_t id[g18::ID_SIZE], presence[g18::DETAILS_SIZE];
            g18::zero(id, g18::ID_SIZE);
            g18::zero(presence, g18::DETAILS_SIZE);
            u3h_friend_get(base, i, id, presence);         // a failed read still adds a friend (with an empty id)
            u3x_friend_add(base, id, presence);
        }
    }
    g18::friends<uint64_t>(base, 0xc8) = 0;
}

// net start: hooks the C part's storage into the friend object and asks for the fetch
U3X_FN void u3x_friend_init(uintptr_t base) {
    if (g18::friends_off()) return;
    uint8_t *entries = u3h_friend_storage(0);
    uint8_t *met = u3h_friend_storage(1);
    uint8_t *new_players = u3h_friend_storage(2);
    uint8_t *block0 = u3h_friend_storage(3);
    uint8_t *block1 = u3h_friend_storage(4);

    g18::friends<uint32_t>(base, 0x00) = 0;                // friends
    g18::friends<uint32_t>(base, 0x04) = 100;
    g18::friends<uint8_t *>(base, 0x08) = entries;
    g18::friends<uint32_t>(base, 0x10) = 0;                // players met
    g18::friends<uint32_t>(base, 0x14) = 50;
    g18::friends<uint8_t *>(base, 0x18) = met;
    g18::friends<uint8_t>(base, 0x20) = 1;                 // history changed
    g18::friends<uint8_t *>(base, 0x48) = block0;
    g18::friends<uint8_t *>(base, 0x50) = block1;
    g18::friends<uint32_t>(base, 0x58) = 0;
    g18::friends<uint32_t>(base, 0x5c) = 0;
    g18::zero(g18::published(base), g18::DATA_SIZE);
    g18::friends<uint32_t>(base, 0xc0) = 0;
    g18::friends<uint64_t>(base, 0xc8) = 1;                // the fetch runs at the next tick
    g18::friends<uint64_t>(base, 0xe0) = 0;
    g18::friends<uint32_t>(base, 0xe8) = 0x10;             // new players
    g18::friends<uint32_t>(base, 0xec) = 0;
    g18::friends<uint8_t *>(base, 0xf0) = new_players;
    g18::friends<uint64_t>(base, 0xf8) = 0;

    for (uint32_t i = 0; i < 100; i++) {
        uint8_t *e = entries + i * g18::ENTRY_SIZE;
        *reinterpret_cast<uint64_t *>(e + 0x1b8) = 0;
        e[0x1c0] = 0;
    }
    for (uint32_t i = 0; i < 50; i++) *reinterpret_cast<uint32_t *>(met + i * g18::MET_SIZE + 0x24) = 0;
    *reinterpret_cast<uint32_t *>(block0 + 0x1b0) = 0;
    *reinterpret_cast<uint32_t *>(block1 + 0x1b0) = 0;
    ext_ea()[64]++;
}

// forgets the published presence data and publishes the title alone
U3X_FN void u3x_presence_clear(uintptr_t base, void *s) {
    g18::zero(g18::published(base), g18::DATA_SIZE);
    alignas(8) uint8_t details[g18::DETAILS_SIZE];
    if (g18::title_details(base, s, details)) u3h_presence_set(base, details, 3);
}

// net shutdown: gives the lists back
U3X_FN void u3x_friend_shutdown(uintptr_t base, void *s) {
    if (g18::friends_off() || !g18::friend_entries(base)) return;
    if (g18::busy(base)) u3x_friend_list_thread(base);     // a pending fetch runs first
    u3x_presence_clear(base, s);
    g18::friends<uint64_t>(base, 0x08) = 0;
    g18::friends<uint32_t>(base, 0x04) = 0;
    g18::friends<uint32_t>(base, 0x00) = 0;
    g18::friends<uint64_t>(base, 0x18) = 0;
    g18::friends<uint32_t>(base, 0x14) = 0;
    g18::friends<uint32_t>(base, 0x10) = 0;
    g18::friends<uint64_t>(base, 0x48) = 0;
    g18::friends<uint64_t>(base, 0x50) = 0;
    g18::zero(&g18::friends<uint8_t>(base, 0x21), g18::ID_SIZE);
    g18::friends<uint32_t>(base, 0x5c) = 0;
    ext_ea()[69]++;
}

// every 1500 clock ticks: builds the own presence data and publishes it when it changed
U3X_FN void u3x_presence_tick(uintptr_t base, void *s) {
    using namespace g18;
    if (on_lan(base)) return;
    const int32_t now = at<int32_t>(base + GAME_CLOCK);
    if (now < friends<int32_t>(base, 0xc0)) return;
    friends<uint32_t>(base, 0xc0) = static_cast<uint32_t>(now) + 1500;

    PresenceData d;
    zero(reinterpret_cast<uint8_t *>(&d), DATA_SIZE);
    d.version = u3h_fr_key(base);
    d.locked = (at<uint32_t>(base + SESSION + 0x204) & 1) ? 1 : at<uint8_t>(base + LOCK_SETTING);
    d.party_id = at<uint64_t>(base + SESSION + 0x18);
    d.party_size = static_cast<uint8_t>(game_call<int>(base + FN_PARTY_SIZE, base + SESSION));
    d.menu_mode = at<uint8_t>(base + NET + 0x3e4);
    own_statistics(base, s, d);

    d.game_index = -1;
    d.rules = -1;
    if (at<int32_t>(base + MATCH + 0x6808) == 0) {         // no match: in the menus or searching
        d.activity = at<int32_t>(base + NET + 0x40) != 0 ? 1 : 0;
    } else {
        const bool over = at<uint8_t>(base + MATCH + 0x6a24) != 0;
        d.activity = over ? 3 : 2;
        if (!over) {
            d.match_byte = static_cast<uint8_t>(game_call<int>(base + FN_MATCH_BYTE, base + MATCH));
            d.score_second = at<int16_t>(base + SCORE_SECOND);
            d.score_first = at<int16_t>(base + SCORE_FIRST);
        }
        d.game_index = at<int16_t>(base + MATCH + 0x661c);
        d.rules = at<uint8_t>(base + MATCH + 0x6a48);
        int64_t end = 0;
        if (!over && u3x_match_end_time(base, s, &end)) d.end_time = end;
    }

    const uint8_t *data = reinterpret_cast<const uint8_t *>(&d);
    if (same(data, published(base), DATA_SIZE)) return;
    copy(published(base), data, DATA_SIZE);

    alignas(8) uint8_t details[DETAILS_SIZE];
    title_details(base, s, details);
    copy(details + 0x100, data, DATA_SIZE);
    *reinterpret_cast<uint64_t *>(details + 0x180) = DATA_SIZE;
    ext_ea()[70]++;
    if (u3h_presence_set(base, details, 3) < 0) {          // failed: publish again in 3000 ticks
        zero(published(base), DATA_SIZE);
        friends<uint32_t>(base, 0xc0) = at<uint32_t>(base + GAME_CLOCK) + 3000;
        ext_ea()[71]++;
    }
}

// the time left in the running match, in clock ticks; 0 = the game type has no time limit (or is unknown)
U3X_FN int32_t u3x_match_time_left(uintptr_t base, void *s) {
    using namespace g18;
    const int32_t index = at<int16_t>(base + MATCH + 0x661c);
    const uint8_t *table = u3x_sym_table(s, NET_GAME_TABLE);
    if (!table || index < 0 || index >= rd<int32_t>(table, 0)) return 0;
    const uint8_t *games = rd<const uint8_t *>(table, 8);
    if (!games) return 0;
    if (has_no_time_limit(rd<int32_t>(games + static_cast<uintptr_t>(index) * 0xd0, 0xc))) return 0;

    const uint32_t clock = at<uint32_t>(base + NET + 0xc);
    if (at<uint8_t>(base + MATCH + 0x34e))
        return ticks_from_difference(static_cast<int32_t>(at<uint32_t>(base + MATCH + 0x6a80) - clock));
    if (at<uint8_t>(base + MATCH + 0x34d))
        return ticks_from_difference(static_cast<int32_t>(at<uint32_t>(base + MATCH + 0x6a84) - clock));
    return ticks_from_seconds(at<uint32_t>(base + MATCH + 0x6a3c));   // the time limit
}

// the end of the running match as a "seconds now" time; 0 when there is none
U3X_FN int u3x_match_end_time(uintptr_t base, void *s, int64_t *out) {
    using namespace g18;
    const int32_t left = u3x_match_time_left(base, s);
    if (left == 0) return 0;
    const uint32_t end = at<uint32_t>(base + NET + 0xc) + static_cast<uint32_t>(left);
    const uintptr_t seconds_now = rd<uintptr_t>(ext_eb(), 4 * 4);
    const int64_t now = seconds_now ? game_call<int64_t>(seconds_now, static_cast<void *>(nullptr)) : 0;
    const int32_t passed = static_cast<int32_t>(at<uint32_t>(base + SECOND_CLOCK) - end);
    *out = now - passed * TICK_NUMERATOR / (int64_t{1} << 31);   // ticks to seconds, truncated toward zero
    return 1;
}

// a friend row was clicked: remembers the friend and shows the actions for it
U3X_FN void u3x_friend_click(uintptr_t base, const int32_t *ev) {
    using namespace g18;
    at<uint64_t>(base + SELECTED_OTHER_ID) = 0;
    const int32_t row = ev[1];
    if (busy(base) || row < 0 || static_cast<uint32_t>(row) >= friend_count(base) || !friend_entries(base)) return;
    uint8_t *e = u3h_fr_ent(base, static_cast<uint32_t>(row));
    copy(&at<uint8_t>(base + SELECTED_ID), e + 1, ID_SIZE);
    ext_ea()[78]++;

    QuickActions q;
    q.argument = 0;
    q.page = PAGE_FRIEND_ACTIONS;
    q.count = 0;
    if (!u3x_friend_in_session(base, e) && at<int32_t>(base + NET + 0x40) == 0 && at<int32_t>(base + NET + 0x408) == 0) {
        if (can_join(base, e)) u3x_quick_action_add(&q, 0xb1600ce3u, 0x0ace8463u);   // Join Party
        u3x_quick_action_add(&q, 0x4a555973u, 0x45a9c65bu);                          // Invite to Party
    }
    u3x_quick_action_add(&q, 0x4040b706u, 0x3fe6f70eu);                              // View Profile
    u3x_quick_action_add(&q, 0x2962edc6u, 0x089143eau);                              // Statistics
    if (!on_lan(base)) u3x_quick_action_add(&q, 0xc5790997u, 0x0ecbd5bdu);           // Report Player
    u3h_fr_qa_push(base, &q);
}

// a players-met row was clicked: remembers the player and shows the actions for it
U3X_FN void u3x_met_click(uintptr_t base, const int32_t *ev, uint8_t *target) {
    using namespace g18;
    const int32_t row = ev[1];
    const uint8_t *met = friends<const uint8_t *>(base, 0x18);
    if (busy(base) || row < 0 || static_cast<uint32_t>(row) >= friends<uint32_t>(base, 0x10) || !met) return;
    const uint8_t *id = met + static_cast<uintptr_t>(row) * MET_SIZE;
    copy(target, id, ID_SIZE);
    copy(&at<uint8_t>(base + SELECTED_ID), id, ID_SIZE);
    ext_ea()[79]++;

    QuickActions q;
    q.argument = 0;
    q.page = PAGE_DEFAULT_ACTIONS;
    q.count = 0;
    u3x_quick_action_add(&q, 0x910fb4f9u, 0x20d51153u);                              // Add Friend
    u3x_quick_action_add(&q, 0x4040b706u, 0xe1810202u);                              // View Profile
    u3x_quick_action_add(&q, 0x2962edc6u, 0x089143eau);                              // Statistics
    if (!on_lan(base)) u3x_quick_action_add(&q, 0xc5790997u, 0x0ecbd5bdu);           // Report Player
    u3h_fr_qa_push(base, &q);
}

// shows the wait popup with this text; the popup page's events go to the callback
U3X_FN void u3x_wait_popup(uintptr_t base, uint32_t text_id, uintptr_t callback) {
    using namespace g18;
    const uintptr_t menu = base + MENU;
    at<uint32_t>(at<uintptr_t>(menu + 0x1498) + 8) = text_id;

    // the page's handler: the last one registered for it, else a new one at the end (the table holds 0x37)
    const int32_t n = at<int32_t>(menu + 0x2160);
    int64_t slot = n;
    bool found = false;
    for (int32_t i = 0; i < n; i++) {
        if (at<uint32_t>(menu + 0x2168 + static_cast<uintptr_t>(i) * 0x18) == PAGE_POPUP) {
            slot = i;
            found = true;
        }
    }
    if (!found) {
        if (n > 0x36) return;
        at<int32_t>(menu + 0x2160) = n + 1;
    }
    const uintptr_t handler = menu + 0x2168 + static_cast<uintptr_t>(slot * 0x18);
    at<uint32_t>(handler) = PAGE_POPUP;
    at<uintptr_t>(handler + 8) = callback;
    at<uint32_t>(handler + 0x10) = 0;
    at<uint32_t>(handler + 0x14) = 0;

    game_call<int>(base + FN_SHOW_PAGE, menu, PAGE_POPUP, 0, 0);
}

}  // extern "C"
