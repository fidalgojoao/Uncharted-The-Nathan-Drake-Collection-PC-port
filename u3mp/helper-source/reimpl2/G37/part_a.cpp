// G37 part A: the record table of the Game Type / Map / Variation menus (game type -> maps -> net game records).
// Written from spec G37_find_game.md, sections 3 and 4. The table is built at run time from the game's script data.

extern "C" {
uint8_t *u3x_sym_table(void *s, uint32_t id);        // data of a script symbol (one lookup per call)
int u3h_m04_lan(uintptr_t base);                     // non-zero: the game type lists follow the LAN rule
volatile uint32_t *u3h_m04_diag(void);               // the diagnostic words m[0] .. m[7]

uint8_t *u3x_m04_map(void *s, uint32_t id);
void u3x_m04_info(void *s, uint8_t *info, uint32_t type, const uint8_t *rec, int32_t n, uint8_t *hi);
void u3x_m04_refresh(uintptr_t base, void *s);
int32_t u3x_m04_rec(const uint8_t *t, uint32_t k, uint32_t j, uint32_t i);
uint32_t u3x_m04_cur_type(const uint8_t *t, const uint8_t *ng, int32_t cur);
uint32_t u3x_m04_cur_map(const uint8_t *t, uint32_t k, int32_t cur);
void u3x_m04_select_var(uintptr_t base, void *s, int32_t v);
uint32_t u3x_m04_var_value(uintptr_t base, int solo);
}

namespace g37a {

constexpr uint32_t SYM_NET_GAMES = 0x3b9a067du;   // script symbol: the net game table
constexpr uint32_t SYM_MAPS = 0x0dec97a4u;        // script symbol: the map table

constexpr uintptr_t NET_GAME_SIZE = 0xd0;         // a net game record: +4 map id, +8 name text id, +0xc game type
constexpr uintptr_t MAP_ENTRY_SIZE = 0x98;        // a map entry: +0 map id, +0x94 non-zero = not listed

constexpr uint32_t MAX_TYPES = 11, MAX_MAPS = 32, MAX_RECORDS = 8;
constexpr uint32_t TYPE_SLOT_SIZE = 0x308, MAP_SLOT_SIZE = 0x18;

constexpr uintptr_t IMG_SELECTED_NET_GAME = 0x17d167c;   // s16
constexpr uintptr_t IMG_COOP_BYTE = 0x17d6065;           // u8
constexpr uintptr_t IMG_SELECT_BY_KEY = 0x7511f0;        // game function: select net game by type, map and name

// diagnostic words
constexpr int M_BUILDS = 0, M_TYPES = 1, M_MAPS = 2, M_BIG_RECORDS = 5, M_VARIATIONS = 6;

template <typename T> [[gnu::always_inline]] inline void wr(uint8_t *p, uintptr_t off, T v) {
    *reinterpret_cast<T *>(p + off) = v;
}

// the helper's table T, its high bytes H, and the switch "the helper's table is in use"
[[gnu::always_inline]] inline uint8_t *table() { return u3h_ext() + 0xed000; }
[[gnu::always_inline]] inline uint8_t *high_bytes() { return u3h_ext() + 0xef200; }
[[gnu::always_inline]] inline bool table_on() { return !(ext_eb()[2] & 0x8000u); }

[[gnu::always_inline]] inline void count_up(int word) {
    volatile uint32_t *m = u3h_m04_diag();
    m[word] = m[word] + 1;
}

[[gnu::always_inline]] inline const uint8_t *record(const uint8_t *rec, int32_t i) {
    return rec + NET_GAME_SIZE * static_cast<uintptr_t>(i);
}
[[gnu::always_inline]] inline uint32_t rec_map(const uint8_t *r) { return rd<uint32_t>(r, 4); }
[[gnu::always_inline]] inline uint32_t rec_name(const uint8_t *r) { return rd<uint32_t>(r, 8); }
[[gnu::always_inline]] inline uint32_t rec_type(const uint8_t *r) { return rd<uint32_t>(r, 0xc); }

// positions as seen from a table address: formed in 32-bit unsigned arithmetic (wrapping), then added to the pointer
[[gnu::always_inline]] inline uintptr_t type_slot_at(uint32_t k) { return static_cast<uint32_t>(TYPE_SLOT_SIZE * k + 8u); }
[[gnu::always_inline]] inline uintptr_t map_count_at(uint32_t k) { return static_cast<uint32_t>(TYPE_SLOT_SIZE * k + 0xcu); }
[[gnu::always_inline]] inline uintptr_t record_count_at(uint32_t k, uint32_t j) {
    return static_cast<uint32_t>(TYPE_SLOT_SIZE * k + MAP_SLOT_SIZE * j + 0x18u);
}
[[gnu::always_inline]] inline uintptr_t record_at(uint32_t k, uint32_t j, uint32_t i) {
    return static_cast<uint32_t>(TYPE_SLOT_SIZE * k + MAP_SLOT_SIZE * j + i + 0x19u);
}

// counts as seen from a table address
[[gnu::always_inline]] inline uint32_t type_count(const uint8_t *t) { return rd<uint8_t>(t, 0); }
[[gnu::always_inline]] inline uint32_t map_count(const uint8_t *t, uint32_t k) {
    return rd<uint8_t>(t, map_count_at(k));
}
[[gnu::always_inline]] inline uint32_t record_count(const uint8_t *t, uint32_t k, uint32_t j) {
    return rd<uint8_t>(t, record_count_at(k, j));
}
[[gnu::always_inline]] inline uint32_t lower(uint32_t a, uint32_t b) { return a < b ? a : b; }

[[gnu::always_inline]] inline int32_t selected_net_game(uintptr_t base) {
    return at<int16_t>(base + IMG_SELECTED_NET_GAME);
}

// the co-op side's game types on LAN
[[gnu::always_inline]] inline bool coop_type(uint32_t g) { return g == 8 || g == 11 || g == 17; }

}  // namespace g37a

// 4.1: the first map entry with this id, or null
extern "C" U3X_FN uint8_t *u3x_m04_map(void *s, uint32_t id) {
    uint8_t *maps = u3x_sym_table(s, g37a::SYM_MAPS);
    if (!maps) return nullptr;
    uint8_t *entry = rd<uint8_t *>(maps, 8);
    if (!entry) return nullptr;
    const int32_t count = rd<int32_t>(maps, 0);
    for (int32_t i = 0; i < count; i++, entry += g37a::MAP_ENTRY_SIZE)
        if (rd<uint32_t>(entry, 0) == id) return entry;
    return nullptr;
}

// 4.2: one game type slot: its maps, and for each map its records (variations)
extern "C" U3X_FN void u3x_m04_info(void *s, uint8_t *info, uint32_t type, const uint8_t *rec, int32_t n, uint8_t *hi) {
    using namespace g37a;
    uint32_t maps = 0;
    wr<uint32_t>(info, 0, type);
    wr<uint8_t>(info, 4, 0);

    for (int32_t i = 0; i < n; i++) {
        const uint8_t *r = record(rec, i);
        if (rec_type(r) != type) continue;
        const uint32_t map_id = rec_map(r);

        // each map is listed once
        bool listed = false;
        for (uint32_t j = 0; j < maps && !listed; j++)
            listed = rd<uint32_t>(rd<const uint8_t *>(info, 8 + MAP_SLOT_SIZE * j), 0) == map_id;
        if (listed) continue;

        uint8_t *entry = u3x_m04_map(s, map_id);
        if (entry && rd<uint32_t>(entry, 0x94) != 0) continue;   // a hidden map (tested before the "full" test)
        if (maps == MAX_MAPS) break;

        // the slot is written even for an unknown map; it is counted only for a known one
        uint8_t *slot = info + 8 + MAP_SLOT_SIZE * maps;
        wr<uint8_t *>(slot, 0, entry);
        wr<uint8_t>(slot, 8, 0);
        if (!entry) continue;

        // collect the records of this type and map over the whole table (16-bit record numbers, at most 8)
        uint32_t held = 0;
        for (int32_t x = 0; x < n && x <= 0xffff && held < MAX_RECORDS; x++) {
            const uint8_t *o = record(rec, x);
            if (rec_type(o) != type || rec_map(o) != map_id) continue;
            wr<uint8_t>(slot, 9 + held, static_cast<uint8_t>(x));
            hi[8 * maps + held] = static_cast<uint8_t>(x >> 8);
            held++;
            wr<uint8_t>(slot, 8, static_cast<uint8_t>(held));
            if (x >= 256) count_up(M_BIG_RECORDS);
        }

        maps++;
        wr<uint8_t>(info, 4, static_cast<uint8_t>(maps));
        count_up(M_MAPS);
    }
}

// 4.3: build the helper's table (whatever the switch says)
extern "C" U3X_FN void u3x_m04_refresh(uintptr_t base, void *s) {
    using namespace g37a;
    uint8_t *t = table();
    uint8_t *h = high_bytes();
    wr<uint8_t>(t, 0, 0);
    count_up(M_BUILDS);

    const uint8_t *games = u3x_sym_table(s, SYM_NET_GAMES);
    if (!games) return;
    const uint8_t *rec = rd<const uint8_t *>(games, 8);
    const int32_t n = rd<int32_t>(games, 0);
    if (!rec || n <= 0) return;

    const bool lan = u3h_m04_lan(base) != 0;
    const bool coop = at<uint8_t>(base + IMG_COOP_BYTE) != 0;

    // the game types in order of first appearance
    uint32_t types[MAX_TYPES];
    uint32_t count = 0;
    for (int32_t i = 0; i < n; i++) {
        const uint32_t g = rec_type(record(rec, i));
        if (lan && coop_type(g) != coop) continue;
        if (g == 16) continue;
        bool known = false;
        for (uint32_t k = 0; k < count && !known; k++) known = types[k] == g;
        if (known) continue;
        if (count == MAX_TYPES) break;
        types[count++] = g;
    }

    // type 10 comes directly after type 19 when it stands further back
    uint32_t at10 = count, at19 = count;
    for (uint32_t k = 0; k < count; k++) {
        if (types[k] == 10) at10 = k;
        if (types[k] == 19) at19 = k;
    }
    if (at10 < count && at19 < count && at10 > at19 + 1) {
        for (uint32_t k = at10; k > at19 + 1; k--) types[k] = types[k - 1];
        types[at19 + 1] = 10;
    }

    u3h_m04_diag()[M_BIG_RECORDS] = 0;
    for (uint32_t k = 0; k < count; k++)
        u3x_m04_info(s, t + 8 + static_cast<uintptr_t>(TYPE_SLOT_SIZE) * k, types[k], rec, n, h + 0x100 * k);

    wr<uint8_t>(t, 0, static_cast<uint8_t>(count));
    u3h_m04_diag()[M_TYPES] = count;
}

// 4.4: a record number: 8 bits from the table, 8 more from the high bytes for the helper's own table
extern "C" U3X_FN int32_t u3x_m04_rec(const uint8_t *t, uint32_t k, uint32_t j, uint32_t i) {
    using namespace g37a;
    uint32_t number = rd<uint8_t>(t, record_at(k, j, i));   // the whole offset is a 32-bit unsigned sum
    if (table_on() && t == table() && k < MAX_TYPES && j < MAX_MAPS && i < MAX_RECORDS)
        number |= static_cast<uint32_t>(high_bytes()[0x100 * k + 8 * j + i]) << 8;
    return static_cast<int32_t>(number);
}

// 4.5: the type slot of a net game (0 also when there is none)
extern "C" U3X_FN uint32_t u3x_m04_cur_type(const uint8_t *t, const uint8_t *ng, int32_t cur) {
    using namespace g37a;
    if (!ng) return 0;
    const uint32_t types = type_count(t);
    const uint8_t *rec = rd<const uint8_t *>(ng, 8);
    if (!types || !rec || cur < 0 || cur >= rd<int32_t>(ng, 0)) return 0;
    const uint32_t g = rec_type(record(rec, cur));
    for (uint32_t k = 0; k < types; k++)
        if (rd<uint32_t>(t, type_slot_at(k)) == g) return k;
    return 0;
}

// 4.6: the map slot of type slot k that lists a net game (0 also when there is none)
extern "C" U3X_FN uint32_t u3x_m04_cur_map(const uint8_t *t, uint32_t k, int32_t cur) {
    using namespace g37a;
    const uint32_t maps = lower(map_count(t, k), MAX_MAPS);
    for (uint32_t j = 0; j < maps; j++) {
        const uint32_t records = lower(record_count(t, k, j), MAX_RECORDS);
        for (uint32_t i = 0; i < records; i++)
            if (u3x_m04_rec(t, k, j, i) == cur) return j;
    }
    return 0;
}

// 4.7: select variation v of the selected map, then rebuild the table
extern "C" U3X_FN void u3x_m04_select_var(uintptr_t base, void *s, int32_t v) {
    using namespace g37a;
    const uint8_t *games = u3x_sym_table(s, SYM_NET_GAMES);
    if (!games) return;
    const uint8_t *rec = rd<const uint8_t *>(games, 8);
    const int32_t n = rd<int32_t>(games, 0);
    const int32_t cur = selected_net_game(base);
    if (!rec || cur < 0 || cur >= n) return;

    const uint8_t *t = table();
    const uint32_t k = u3x_m04_cur_type(t, games, cur);
    const uint32_t j = u3x_m04_cur_map(t, k, cur);
    if (j >= map_count(t, k)) return;
    if (v < 0 || v >= static_cast<int32_t>(record_count(t, k, j)) || v >= static_cast<int32_t>(MAX_RECORDS)) return;

    const int32_t chosen = u3x_m04_rec(t, k, j, static_cast<uint32_t>(v));
    if (chosen >= n) return;

    // the game finds the record by game type, map and name
    const uint8_t *now = record(rec, cur);
    uint32_t key[3];
    key[0] = rec_type(now);
    key[1] = rec_map(now);
    key[2] = rec_name(record(rec, chosen));
    game_call<void, uint32_t *>(base + IMG_SELECT_BY_KEY, key);

    count_up(M_VARIATIONS);
    u3x_m04_refresh(base, s);
}

// 4.8: the row of the selected variation: the first slot in table order that holds the selected net game
extern "C" U3X_FN uint32_t u3x_m04_var_value(uintptr_t base, int solo) {
    using namespace g37a;
    const uint32_t front = solo ? 1 : 0;
    const int32_t cur = selected_net_game(base);
    const uint8_t *t = table();
    const uint32_t types = lower(type_count(t), MAX_TYPES);
    for (uint32_t k = 0; k < types; k++) {
        const uint32_t maps = lower(map_count(t, k), MAX_MAPS);
        for (uint32_t j = 0; j < maps; j++) {
            const uint32_t records = lower(record_count(t, k, j), MAX_RECORDS);
            for (uint32_t i = 0; i < records; i++)
                if (u3x_m04_rec(t, k, j, i) == cur) return i + front;
        }
    }
    return front;
}
