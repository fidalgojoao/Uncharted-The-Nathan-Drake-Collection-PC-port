// G41: out-of-bounds grid (kill volume) reader, the per-frame out-of-bounds check and the death-animation guard.
// Written from spec/G41_out_of_bounds.md (parts A, B and C).

extern "C" {
uint8_t *u3h_netgame(uintptr_t base, void *s);
int32_t u3x_sorted_index(const uint8_t *entries, uint32_t count, uint32_t id);
uint32_t u3x_net_player_id(const uint8_t *np);

int u3x_vis_ok(const void *p);
void u3x_vis_axis(uint32_t p, uint32_t sc, uint32_t o, int64_t out[2]);
int u3x_vis_cell(const uint8_t *g, const int64_t c[3]);
int u3x_vis_cheat_corners(uintptr_t base, const uint32_t pos[3]);
int u3x_vis_find_kill(const uint8_t *g, int32_t c[3]);
uint32_t u3x_vis_cell_point(int32_t c, uint32_t sc, uint32_t o);
void u3x_vis_test_point(uint8_t *o, uint32_t out[3]);
int u3x_obj_state_is(uint8_t *o, uint32_t h);
uint8_t *u3x_map_entry_cur(uintptr_t base, void *s);
int u3x_vis_oob_tick(uintptr_t base, void *s, uint8_t *p, uint8_t *pl, int32_t now, int test);
int u3x_death_anim_check(uintptr_t base, void *s, const uint64_t *blk);
}

namespace g41 {

constexpr uintptr_t GRID_POINTER = 0x1b3ae78;      // image: the loaded kill-volume grid item
constexpr uintptr_t NET_PLAYERS = 0x17cb060 + 0x390;   // image: ten net player records
constexpr uintptr_t NET_PLAYER_SIZE = 0x9c0;
constexpr uint64_t COOP_DAMAGE_CALLER = 0x99cce0;  // image: return address of the co-op damage path's call
constexpr uint32_t MAP_LIST_SYMBOL = 0x0dec97a4;
constexpr uint32_t TYPE_PLAYER = 0xa5a63e9b;
constexpr uint32_t TYPE_NET_CHARACTER = 0xf2c86c22;
constexpr int32_t KILL_DELAY = 1500;               // 5 s in net ticks
constexpr uint32_t NO_TEST_CELL = 0x80000000u;

// a field that need not be aligned for its type (grid parts are only 2-aligned, object vectors unaligned)
template <typename T> [[gnu::always_inline]] inline T ld(const void *p, uintptr_t off) {
    struct [[gnu::packed]] Loose { T v; };
    return reinterpret_cast<const Loose *>(static_cast<const uint8_t *>(p) + off)->v;
}

// the grid pointer test: at least 0x10000, below 2^47, even
[[gnu::always_inline]] inline bool ok(const void *p) {
    const uintptr_t v = reinterpret_cast<uintptr_t>(p);
    return v >= 0x10000u && v < (1ull << 47) && !(v & 1);
}

// single-precision round to an integral value, toward -infinity / +infinity, no exception
[[gnu::always_inline]] inline uint32_t f_floor(uint32_t a) {
    uint32_t r;
    __asm__("movd %1,%%xmm0\n\troundss $9,%%xmm0,%%xmm0\n\tmovd %%xmm0,%0" : "=r"(r) : "r"(a) : "xmm0");
    return r;
}
[[gnu::always_inline]] inline uint32_t f_ceil(uint32_t a) {
    uint32_t r;
    __asm__("movd %1,%%xmm0\n\troundss $10,%%xmm0,%%xmm0\n\tmovd %%xmm0,%0" : "=r"(r) : "r"(a) : "xmm0");
    return r;
}
// float pattern to signed 64-bit, truncated toward zero; NaN or out of range gives -2^63
[[gnu::always_inline]] inline int64_t f_to_int64(uint32_t a) {
    int64_t r;
    __asm__("movd %1,%%xmm0\n\tcvttss2si %%xmm0,%0" : "=r"(r) : "r"(a) : "xmm0");
    return r;
}

// ---- grid header
[[gnu::always_inline]] inline uint32_t grid_type(const uint8_t *g) { return ld<uint32_t>(g, 0); }
[[gnu::always_inline]] inline int32_t grid_low(const uint8_t *g, int axis) { return ld<int32_t>(g, 4 + 8 * axis); }
[[gnu::always_inline]] inline int32_t grid_end(const uint8_t *g, int axis) { return ld<int32_t>(g, 8 + 8 * axis); }
[[gnu::always_inline]] inline uint32_t grid_scale(const uint8_t *g, int axis) { return ld<uint32_t>(g, 0x20 + 4 * axis); }
[[gnu::always_inline]] inline uint32_t grid_origin(const uint8_t *g, int axis) {
    return grid_type(g) == 2 ? ld<uint32_t>(g, 0x30 + 4 * axis) : 0u;
}
// where the row store pointer, the slice count and the slice pointers start
[[gnu::always_inline]] inline uintptr_t grid_tail(const uint8_t *g) { return grid_type(g) == 2 ? 0x40 : 0x30; }
[[gnu::always_inline]] inline bool grid_usable(const uint8_t *g) {
    return ok(g) && (grid_type(g) == 1 || grid_type(g) == 2);
}

inline uint32_t *oob_words() { return reinterpret_cast<uint32_t *>(u3h_ext() + 0x2070); }
inline uint32_t *oob2_words() { return reinterpret_cast<uint32_t *>(u3h_ext() + 0x20b0); }
inline uint32_t *death_words() { return reinterpret_cast<uint32_t *>(u3h_ext() + 0x3b20); }

// a handle (holder, id) resolved to its object, or null
[[gnu::always_inline]] inline const uint8_t *resolve(const uint8_t *holder, uint32_t id) {
    if (!holder || !id) return nullptr;
    const uint8_t *object = rd<const uint8_t *>(holder, 0);
    if (!object || rd<uint32_t>(object, 0x7c) != id) return nullptr;
    return object;
}

// the sorted entry list of an animation table, or null
[[gnu::always_inline]] inline const uint8_t *anim_list(const uint8_t *table) {
    if (!table) return nullptr;
    const uint8_t *inner = rd<const uint8_t *>(table, 0x30);
    if (!inner) return nullptr;
    return rd<const uint8_t *>(inner, 0x10);
}

}  // namespace g41

// ---------------------------------------------------------------------------------------------- part A: grid reader

U3X_FN int u3x_vis_ok(const void *p) { return g41::ok(p) ? 1 : 0; }

// lower and upper cell number of one coordinate
U3X_FN void u3x_vis_axis(uint32_t p, uint32_t sc, uint32_t o, int64_t out[2]) {
    const uint32_t a = f_mul(f_sub(p, o), sc);
    const uint32_t bound[2] = {g41::f_floor(a), g41::f_ceil(a)};
    for (int i = 0; i < 2; i++) {
        // the game's round trip: divide, add the origin, subtract it, multiply
        const uint32_t q = f_mul(f_sub(f_add(f_div(bound[i], sc), o), o), sc);
        out[i] = g41::f_to_int64(q);
    }
}

// state of one cell: 2 = outside the grid, 1 = playable, 0 = out of bounds
U3X_FN int u3x_vis_cell(const uint8_t *g, const int64_t c[3]) {
    using namespace g41;
    uint64_t index[3], size[3];
    for (int i = 0; i < 3; i++) {
        const int64_t low = grid_low(g, i), end = grid_end(g, i);
        if (c[i] < low || c[i] >= end) return 2;
        index[i] = static_cast<uint64_t>(c[i] - low);
        size[i] = static_cast<uint64_t>(end - low);
    }
    const uint8_t column_axis = g[0x1c], row_axis = g[0x1d], slice_axis = g[0x1e];
    if (column_axis > 2 || row_axis > 2 || slice_axis > 2) return 2;

    const uintptr_t tail = grid_tail(g);
    const uint8_t *rows = ld<const uint8_t *>(g, tail);
    if (!ok(rows) || index[slice_axis] >= ld<uint32_t>(g, tail + 8)) return 2;
    const uint8_t *slice = ld<const uint8_t *>(g, tail + 0x10 + 8 * index[slice_axis]);
    if (!ok(slice)) return 2;

    // the slice entry that covers the row position
    const uint32_t entries = ld<uint16_t>(slice, 0);
    if (!entries) return 1;
    const uint32_t row_count = ld<uint32_t>(rows, 0);   // read before the covering entry is looked for
    uint64_t first = 0;
    uint32_t row_number = 0;
    bool covered = false;
    for (uint32_t e = 0; e < entries; e++) {
        const uint32_t entry = ld<uint16_t>(slice, 2 + 2 * e);
        const uint64_t last = first + (entry >> 14);
        if (last >= index[row_axis]) { row_number = entry & 0x3fff; covered = true; break; }
        first = last + 1;
    }
    if (!covered) return 1;
    if (row_number >= row_count) return 1;
    const uint8_t *row = ld<const uint8_t *>(rows, 8 + 8 * static_cast<uintptr_t>(row_number));
    if (!ok(row)) return 1;

    // the run that contains the column position decides
    const uint64_t width = size[column_axis], most = 2 * width + 2, column = index[column_axis];
    uint8_t value = row[0];
    uint64_t at_column = 0;
    for (uint64_t used = 0; at_column < width && used < most; used++) {
        const uint8_t length = row[1 + used];
        if (column < at_column + length) return value ? 0 : 1;
        at_column += length;
        if (length != 255) value = static_cast<uint8_t>(~value);
    }
    return 1;
}

// how many of the eight cells around a point are out of bounds
U3X_FN int u3x_vis_cheat_corners(uintptr_t base, const uint32_t pos[3]) {
    using namespace g41;
    const uint8_t *g = rd<const uint8_t *>(reinterpret_cast<const void *>(base), GRID_POINTER);
    oob_words()[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g));
    if (!grid_usable(g)) return 0;

    int64_t bound[3][2];
    for (int i = 0; i < 3; i++) u3x_vis_axis(pos[i], grid_scale(g, i), grid_origin(g, i), bound[i]);

    int count = 0;
    for (int corner = 0; corner < 8; corner++) {
        const int64_t cell[3] = {bound[0][corner & 1], bound[1][(corner >> 1) & 1], bound[2][(corner >> 2) & 1]};
        if (u3x_vis_cell(g, cell) == 0) count++;
    }
    return count;
}

// the first out-of-bounds run of the grid: slices, then entries, then runs
U3X_FN int u3x_vis_find_kill(const uint8_t *g, int32_t c[3]) {
    using namespace g41;
    uint32_t low[3];
    int32_t size[3];
    for (int i = 0; i < 3; i++) {
        // the size test is the signed comparison of the two fields, not the sign of their wrapped difference
        const int32_t lowest = grid_low(g, i), end = grid_end(g, i);
        if (end <= lowest) return 0;
        low[i] = static_cast<uint32_t>(lowest);
        size[i] = static_cast<int32_t>(static_cast<uint32_t>(end) - low[i]);
    }
    const uint8_t column_axis = g[0x1c], row_axis = g[0x1d], slice_axis = g[0x1e];
    if (column_axis > 2 || row_axis > 2 || slice_axis > 2) return 0;

    const uintptr_t tail = grid_tail(g);
    const uint8_t *rows = ld<const uint8_t *>(g, tail);
    if (!ok(rows)) return 0;
    const uint32_t row_count = ld<uint32_t>(rows, 0);
    uint32_t slices = ld<uint32_t>(g, tail + 8);
    if (slices > 4096) slices = 4096;
    // signed 64-bit: a huge column axis cannot wrap the limit, and a wrapped (negative) size decodes no row
    const int64_t width = size[column_axis], most = 2 * width + 2;

    for (uint32_t k = 0; k < slices; k++) {
        const uint8_t *slice = ld<const uint8_t *>(g, tail + 0x10 + 8 * static_cast<uintptr_t>(k));
        if (!ok(slice)) continue;
        const uint32_t entries = ld<uint16_t>(slice, 0);
        uint32_t next = 0;   // row position after the entries seen so far
        for (uint32_t e = 0; e < entries; e++) {
            const uint32_t entry = ld<uint16_t>(slice, 2 + 2 * e);
            const uint32_t repeat = entry >> 14, row_number = entry & 0x3fff, first = next;
            next += repeat + 1;
            if (row_number >= row_count) continue;
            const uint8_t *row = ld<const uint8_t *>(rows, 8 + 8 * static_cast<uintptr_t>(row_number));
            if (!ok(row)) continue;

            uint8_t value = row[0];
            int64_t at_column = 0;
            for (int64_t used = 0; at_column < width && used < most; used++) {
                const uint8_t length = row[1 + used];
                if (value && length) {
                    // an axis no byte names keeps index 0 (unspecified by the spec; real grids name all three)
                    uint32_t index[3] = {0, 0, 0};
                    index[column_axis] = static_cast<uint32_t>(at_column);
                    index[row_axis] = first + repeat / 2;
                    index[slice_axis] = k;
                    for (int i = 0; i < 3; i++) c[i] = static_cast<int32_t>(index[i] + low[i]);
                    return 1;
                }
                at_column += length;
                if (length != 255) value = static_cast<uint8_t>(~value);
            }
        }
    }
    return 0;
}

// the coordinate of a cell number
U3X_FN uint32_t u3x_vis_cell_point(int32_t c, uint32_t sc, uint32_t o) {
    return f_add(f_div(f_from_int(c), sc), o);
}

// ------------------------------------------------------------------------------------------ part B: per-frame check

// world position of an object (its place in the parent frame, rotated, plus the frame's position) plus 1 m up
U3X_FN void u3x_vis_test_point(uint8_t *o, uint32_t out[3]) {
    using namespace g41;
    uint32_t frame[3], turn[4], local[3];
    for (int i = 0; i < 3; i++) frame[i] = ld<uint32_t>(o, 0xd0 + 4 * i);
    for (int i = 0; i < 4; i++) turn[i] = ld<uint32_t>(o, 0xe0 + 4 * i);
    for (int i = 0; i < 3; i++) local[i] = ld<uint32_t>(o, 0xf0 + 4 * i);

    uint32_t t[3], u[3];
    for (int i = 0; i < 3; i++) {
        const int j = (i + 1) % 3, k = (i + 2) % 3;
        const uint32_t d = f_sub(f_mul(turn[j], local[k]), f_mul(turn[k], local[j]));
        t[i] = f_add(d, d);
    }
    for (int i = 0; i < 3; i++) {
        const int j = (i + 1) % 3, k = (i + 2) % 3;
        u[i] = f_sub(f_mul(turn[j], t[k]), f_mul(turn[k], t[j]));
    }
    for (int i = 0; i < 3; i++)
        out[i] = f_add(frame[i], f_add(f_add(local[i], f_mul(turn[3], t[i])), u[i]));
    out[1] = f_add(out[1], F_ONE);
}

// is the object's current state the one with this id
U3X_FN int u3x_obj_state_is(uint8_t *o, uint32_t h) {
    using namespace g41;
    const uint8_t *machine = rd<const uint8_t *>(o, 0x80);
    if (!ok(machine)) return 0;
    const uint8_t *record = rd<const uint8_t *>(machine, 0);
    if (!ok(record)) return 0;
    const uint8_t *state = rd<const uint8_t *>(record, 0x10);
    if (!ok(state)) return 0;
    return rd<uint32_t>(state, 0x18) == h ? 1 : 0;
}

// the map list entry of the current net game's map, or null
U3X_FN uint8_t *u3x_map_entry_cur(uintptr_t base, void *s) {
    const uint8_t *game = u3h_netgame(base, s);
    if (!game) return nullptr;
    const auto symbol = rd<void *(*)(uint32_t)>(s, 872);   // native 13, called without a null test
    const uint8_t *list = static_cast<const uint8_t *>(symbol(g41::MAP_LIST_SYMBOL));
    if (!list) return nullptr;
    uint8_t *entry = rd<uint8_t *>(list, 8);
    if (!entry) return nullptr;
    const uint32_t map = rd<uint32_t>(game, 4);
    int32_t count = rd<int32_t>(list, 0);
    if (count > 128) count = 128;
    for (int32_t i = 0; i < count; i++, entry += 0x98)
        if (rd<uint32_t>(entry, 0) == map) return entry;
    return nullptr;
}

// starts or clears the out-of-bounds kill timer of one local player
U3X_FN int u3x_vis_oob_tick(uintptr_t base, void *s, uint8_t *p, uint8_t *pl, int32_t now, int test) {
    using namespace g41;
    uint32_t *oob = oob_words();
    int32_t &timer = *reinterpret_cast<int32_t *>(p + 0x848);

    const uint8_t *map = u3x_map_entry_cur(base, s);
    const uint32_t level = map ? rd<uint32_t>(map, 0x40) : 0u;
    oob[10] = level;
    if (!level) { oob[4]++; return 0; }

    oob[0]++;
    uint32_t point[3];
    u3x_vis_test_point(pl, point);

    // test mode: stand the test point in the grid's first out-of-bounds cell
    // (the grid pointer is read here only in test mode)
    const uint8_t *g = test ? rd<const uint8_t *>(reinterpret_cast<const void *>(base), GRID_POINTER) : nullptr;
    if (test && grid_usable(g)) {
        uint32_t *oob2 = oob2_words();
        const uint32_t grid_id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g));
        oob2[1]++;
        if (oob2[0] != grid_id) {
            oob2[0] = grid_id;
            oob[13] = oob[14] = oob[15] = 0;
        }
        if (!oob[13] && !oob[14] && !oob[15]) {
            int32_t cell[3];
            if (u3x_vis_find_kill(g, cell)) {
                for (int i = 0; i < 3; i++) oob[13 + i] = static_cast<uint32_t>(cell[i]);
            } else {
                oob[13] = NO_TEST_CELL;
            }
        }
        if (oob[13] != NO_TEST_CELL)
            for (int i = 0; i < 3; i++)
                point[i] = u3x_vis_cell_point(static_cast<int32_t>(oob[13 + i]), grid_scale(g, i), grid_origin(g, i));
    }

    for (int i = 0; i < 3; i++) oob[5 + i] = point[i];
    const int corners = u3x_vis_cheat_corners(base, point);
    oob[2] = static_cast<uint32_t>(corners);

    // three character states suspend the timer
    if (u3x_obj_state_is(pl, 0xb713a9fdu) || u3x_obj_state_is(pl, 0x50b12ba3u) || u3x_obj_state_is(pl, 0xc388fdabu)) {
        oob[8]++;
        timer = 0;
        return 1;
    }
    if (corners != 8) {
        timer = 0;
    } else if (timer <= 0) {
        timer = static_cast<int32_t>(static_cast<uint32_t>(now) + static_cast<uint32_t>(KILL_DELAY));
        oob[3]++;
    }
    return 1;
}

// -------------------------------------------------------------------------------- part C: death-animation guard

// 1 = the dying character has no such death animation: skip the game's death event constructor
U3X_FN int u3x_death_anim_check(uintptr_t base, void *s, const uint64_t *blk) {
    using namespace g41;
    uint32_t *dth = death_words();
    const uint32_t anim = static_cast<uint32_t>(blk[5]);
    const uint32_t player = static_cast<uint32_t>(blk[6]);
    const uint64_t caller = blk[10] - base;

    dth[0]++;
    dth[4] = static_cast<uint32_t>(caller);
    dth[5] = player;
    dth[7] = anim;

    // the co-op damage path: record the damage and its attacker
    if (caller == COOP_DAMAGE_CALLER) {
        dth[12]++;
        const uint8_t *damage = rd<const uint8_t *>(u3h_ext(), 0x3c20);
        if (!damage) {
            dth[9] = 0xffffffffu;
        } else {
            dth[9] = rd<uint32_t>(damage, 0x1c);
            for (int i = 0; i < 16; i++) dth[16 + i] = rd<uint32_t>(damage, 4 * i);
            for (int i = 0; i < 12; i++) dth[52 + i] = rd<uint32_t>(damage, 0x40 + 4 * i);
            const uint8_t *attacker = resolve(rd<const uint8_t *>(damage, 8), rd<uint32_t>(damage, 0x10));
            dth[10] = attacker ? rd<uint32_t>(attacker, 0x30) : 0u;
            dth[11] = attacker ? rd<uint32_t>(attacker, 0x28) : 0u;
            for (int i = 0; i < 8; i++) dth[32 + i] = attacker ? rd<uint32_t>(attacker, 0x20 + 4 * i) : 0u;
        }
    }

    if (!player) return 0;
    const uint8_t *record = nullptr;
    for (int i = 0; i < 10 && !record; i++) {
        const uint8_t *np = reinterpret_cast<const uint8_t *>(base + NET_PLAYERS + i * NET_PLAYER_SIZE);
        if (u3x_net_player_id(np) == player) record = np;
    }
    if (!record) return 0;
    for (int i = 0; i < 4; i++) dth[48 + i] = rd<uint32_t>(record, 4 * i);
    const uint8_t *character = resolve(rd<const uint8_t *>(record, 0x10), rd<uint32_t>(record, 0x18));
    if (!character) return 0;

    const uint32_t type = rd<uint32_t>(character, 0x30);
    dth[3] = type;
    const uint8_t *tables;
    if (type == TYPE_NET_CHARACTER) {
        tables = character + 0x468;
    } else if (type == TYPE_PLAYER) {
        tables = rd<const uint8_t *>(character, 0x1c20);
        if (!tables) return 0;
    } else {
        return 0;
    }

    uint32_t present = 0;
    for (int i = 0; i < 8; i++) {
        const uint8_t *list = anim_list(rd<const uint8_t *>(tables, 8 * i));
        if (!list) { dth[40 + i] = 0; continue; }
        const uint32_t count = rd<uint32_t>(list, 0);
        dth[40 + i] = count;
        present |= 1u << i;
        const int32_t found = u3x_sorted_index(rd<const uint8_t *>(list, 8), count, anim);
        if (found >= 0) {
            dth[8] = present;
            dth[13]++;
            dth[14] = static_cast<uint32_t>(i);
            dth[15] = static_cast<uint32_t>(found);
            return 0;
        }
    }

    dth[8] = present;
    dth[1]++;
    dth[2] = anim;
    if (rd<uint32_t>(s, 0x20c0) & 0x200) return 0;   // death guard off
    dth[6]++;
    return 1;
}
