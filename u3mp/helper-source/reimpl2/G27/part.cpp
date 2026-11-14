// G27: the treasures lists (treasure sets of a tab with a preview, the treasures of the chosen set, the "found in"
// line). Written from spec\G27_treasures.md; uses the shared tools of CONVENTIONS.h.

extern "C" {
// C part and other groups
int u3h_gate_closed(uintptr_t base, int32_t *ev);
const char *u3h_text(uintptr_t base, void *s, uint32_t id);
int32_t u3h_icon_index(uintptr_t base, uint32_t id);
void u3h_set_icon(uintptr_t base, int32_t *ev, uint32_t id);
int u3h_show(uintptr_t base, void *s, uint32_t page, int silent);
uint8_t *u3h_player_profile(uintptr_t base);
uint32_t u3h_crc_sid(const char *name);
uint8_t *u3x_sym_table(void *s, uint32_t id);
uint8_t *u3x_treasure_entry(void *s, uint32_t index);

// this group
int u3x_treasures_source(uintptr_t base, int32_t *ev, void *s);
int u3x_treasure_items_source(uintptr_t base, int32_t *ev, void *s);
void u3x_treasure_build_sets(void *s, int32_t group);
void u3x_treasure_build_items(void *s, uint32_t set);
uint8_t *u3x_treasure_set_entry(void *s, int32_t row);
uint8_t *u3x_treasure_tab(uintptr_t base, void *s);
void u3x_x66_subst(char *o, char *end, const char *t, const char *a, const char *b, const char *c);
void u3x_x66_found(uintptr_t base, int32_t *ev, void *s);
}

namespace g27 {

// script symbols
constexpr uint32_t SYM_SETS = 0xf1f296cbu;
constexpr uint32_t SYM_TREASURES = 0xb114fe00u;
constexpr uint32_t SYM_TABS = 0xf1514340u;
constexpr uint32_t SYM_MAPS = 0x0dec97a4u;
constexpr uint32_t SYM_RULES = 0xf985c7b6u;

// source tags and page ids
constexpr uint32_t TAG_SETS = 0x4c414ed4u;
constexpr uint32_t TAG_ITEMS = 0x4c414ed5u;
constexpr uint32_t PAGE_SETS = 0xda577facu;
constexpr uint32_t PAGE_ITEMS = 0xecfa738au;

// entry sizes of the script tables
constexpr uintptr_t SET_SIZE = 0xc, TREASURE_SIZE = 0x68, TAB_ROW_SIZE = 0x20, TAB_SIZE = 0xc, MAP_SIZE = 0x98,
                    RULE_SIZE = 0x58;

constexpr uint32_t MAX_ROWS = 256;
constexpr int FOUND_LINE_MAX = 230;   // characters of the "found in" line, without the NUL

// the helper's own texts
U3X_RO const char empty_text[] = "";
U3X_RO const char slash_text[] = "/";
U3X_RO const char placeholder_start[] = "[TEXT";
U3X_RO const char check_icon_name[] = "general/net/menu-icons/icon-check";

// the treasure list state at s + 0x3a00 (layout fixed)
struct ListState {
    uint8_t set_row[MAX_ROWS];         // set table index of each set row
    uint32_t set_count;
    uint16_t treasure_row[MAX_ROWS];   // treasure table index of each treasure row
    uint32_t treasure_count;
    int32_t selected;                  // highlighted set row
    uint32_t chosen_set;               // set id shown by the treasure list page
};
static_assert(sizeof(ListState) == 0x310);

[[gnu::always_inline]] inline ListState &state(void *s) {
    return *reinterpret_cast<ListState *>(static_cast<uint8_t *>(s) + 0x3a00);
}

[[gnu::always_inline]] inline void set_text(int32_t *ev, const char *text) {
    *reinterpret_cast<const char **>(reinterpret_cast<uint8_t *>(ev) + 0x40) = text;
}

// tag of the request's source descriptor matches
[[gnu::always_inline]] inline bool has_tag(const int32_t *ev, uint32_t tag) {
    const uint8_t *descriptor = rd<const uint8_t *>(ev, 0x98);
    return descriptor && rd<uint32_t>(descriptor, 0x1c) == tag;
}

// the player owns treasure `index`
[[gnu::always_inline]] inline bool owned(const uint8_t *profile, uint32_t index) {
    return profile && index < 0x400u && profile[0xacc + index] != 0;
}

[[gnu::always_inline]] inline void show_icon(uintptr_t base, int32_t *ev, uint32_t id) {
    if (u3h_icon_index(base, id) >= 0) u3h_set_icon(base, ev, id);
}

// row of the four-cell preview that list row r stands for (fewer than four treasures sit at the end)
[[gnu::always_inline]] inline int32_t preview_row(const ListState &st, int32_t r) {
    const uint32_t n = st.treasure_count;
    return n < 4 ? static_cast<int32_t>(static_cast<uint32_t>(r) - (4 - n)) : r;
}

// copies a text into a 64-byte buffer, cut after 63 characters
[[gnu::always_inline]] inline void copy_cut(char *to, const char *from) {
    int n = 0;
    while (n < 63 && from[n]) { to[n] = from[n]; ++n; }
    to[n] = 0;
}

// appends a text / a decimal number; returns the new end
[[gnu::always_inline]] inline char *put_text(char *o, const char *t) {
    while (*t) *o++ = *t++;
    return o;
}
[[gnu::always_inline]] inline char *put_decimal(char *o, uint32_t v) {
    char digits[10];
    int k = 0;
    do { digits[k++] = static_cast<char>('0' + v % 10); v /= 10; } while (v);
    while (k) *o++ = digits[--k];
    return o;
}

// a u32 field of set entry `index`: its byte position in the set table is a 32-bit number (wraps at 4 GB)
[[gnu::always_inline]] inline uint32_t set_field(const uint8_t *entries, uint32_t index, uint32_t field) {
    return rd<uint32_t>(entries, static_cast<uint32_t>(static_cast<uint32_t>(SET_SIZE) * index + field));
}

// name of a set: the text of the last set entry with this id, or null
U3X_FN static const char *set_name(uintptr_t base, void *s, uint32_t set) {
    const uint8_t *table = u3x_sym_table(s, SYM_SETS);
    if (!table) return nullptr;
    const uint8_t *entries = rd<const uint8_t *>(table, 8);
    if (!entries) return nullptr;
    const char *name = nullptr;
    const uint32_t count = rd<uint32_t>(table, 0);
    for (uint32_t i = 0; i < count; ++i)
        if (set_field(entries, i, 0) == set) name = u3h_text(base, s, set_field(entries, i, 4));
    return name;
}

// "owned / total" of the set of one set entry, composed in the shared text buffer
U3X_FN static void owned_of_total(int32_t *ev, void *s, const uint8_t *set_entry, const uint8_t *profile) {
    const uint32_t set = rd<uint32_t>(set_entry, 0);
    const uint8_t *table = u3x_sym_table(s, SYM_TREASURES);
    uint32_t total = 0, own = 0;
    const uint8_t *entry = rd<const uint8_t *>(table, 8);
    if (entry) {
        const uint32_t count = rd<uint32_t>(table, 0);
        for (uint32_t i = 0; i < count; ++i, entry += TREASURE_SIZE) {
            if (rd<uint32_t>(entry, 0x2c) != set) continue;
            ++total;
            if (owned(profile, i)) ++own;
        }
    }
    char *const buffer = static_cast<char *>(s) + 0x2c80;
    char *o = put_decimal(buffer, own);
    o = put_text(o, slash_text);
    o = put_decimal(o, total);
    *o = 0;
    set_text(ev, buffer);
}

// name or description of a preview treasure
U3X_FN static void preview_text(uintptr_t base, int32_t *ev, void *s, int32_t r, uintptr_t id_offset) {
    const ListState &st = state(s);
    const int32_t p = preview_row(st, r);
    if (p < 0) { set_text(ev, empty_text); return; }
    if (static_cast<uint32_t>(p) >= st.treasure_count) return;
    const uint8_t *treasure = u3x_treasure_entry(s, st.treasure_row[p]);
    if (treasure) set_text(ev, u3h_text(base, s, rd<uint32_t>(treasure, id_offset)));
}

// operation 2 of the sets list
U3X_FN static void row_text(uintptr_t base, int32_t *ev, void *s, int32_t r, const uint8_t *profile) {
    switch (static_cast<uint32_t>(ev[0x15])) {
    case 0: {
        const uint8_t *entry = u3x_treasure_set_entry(s, r);
        if (entry) set_text(ev, u3h_text(base, s, rd<uint32_t>(entry, 4)));
        break;
    }
    case 1: {
        const uint8_t *entry = u3x_treasure_set_entry(s, r);
        if (entry) owned_of_total(ev, s, entry, profile);
        break;
    }
    case 4: u3x_x66_found(base, ev, s); break;
    case 0x0843d4adu: preview_text(base, ev, s, r, 0); break;
    case 0x0a56c0abu: preview_text(base, ev, s, r, 0x18); break;
    default: break;
    }
}

// operation 4 of the sets list
U3X_FN static void row_picture(uintptr_t base, int32_t *ev, void *s, int32_t r, const uint8_t *profile) {
    const uint32_t slot = static_cast<uint32_t>(ev[0x14]);
    if (slot == 0xf5a294f3u) {
        const uint8_t *tab = u3x_treasure_tab(base, s);
        if (tab) show_icon(base, ev, rd<uint32_t>(tab, 4));
    } else if (slot == 0x21981e34u) {
        const ListState &st = state(s);
        const int32_t p = preview_row(st, r);
        if (p >= 0 && static_cast<uint32_t>(p) < st.treasure_count && owned(profile, st.treasure_row[p]))
            show_icon(base, ev, u3h_crc_sid(check_icon_name));
    }
}

// operation 9 of the sets list: the sets of the current tab and the treasures of the highlighted set
U3X_FN static void refresh(uintptr_t base, void *s) {
    const uint8_t *tab = u3x_treasure_tab(base, s);
    if (!tab) return;
    const uint32_t tab_id = rd<uint32_t>(tab, 0);
    u3x_treasure_build_sets(s, tab_id == 0x49d4b22du ? 2 : tab_id == 0x5baab3cdu ? 3 : 1);
    ListState &st = state(s);
    const uint8_t *entry = u3x_treasure_set_entry(s, st.selected);
    if (entry) u3x_treasure_build_items(s, rd<uint32_t>(entry, 0));
    else st.treasure_count = 0;
}

// operation 0x16 of the sets list: choose a set and open the treasure list page
U3X_FN static void click(uintptr_t base, void *s, int32_t row) {
    const uint8_t *entry = u3x_treasure_set_entry(s, row);
    if (!entry) return;
    const uint32_t set = rd<uint32_t>(entry, 0);
    state(s).chosen_set = set;
    u3x_treasure_build_items(s, set);
    if (rd<const void *>(s, 648)) u3h_show(base, s, PAGE_ITEMS, 0);
}

}  // namespace g27

extern "C" {

// the set rows: every set of a group, in table order
U3X_FN void u3x_treasure_build_sets(void *s, int32_t group) {
    const uint8_t *table = u3x_sym_table(s, g27::SYM_SETS);
    g27::ListState &st = g27::state(s);
    st.set_count = 0;
    if (!table) return;
    const uint8_t *entries = rd<const uint8_t *>(table, 8);
    if (!entries) return;
    const uint32_t count = rd<uint32_t>(table, 0);
    uint32_t rows = 0;
    for (uint32_t i = 0; i < count && rows < g27::MAX_ROWS; ++i)
        if (static_cast<int32_t>(g27::set_field(entries, i, 8)) == group) st.set_row[rows++] = static_cast<uint8_t>(i);
    st.set_count = rows;
}

// the treasure rows: every treasure of a set, in table order
U3X_FN void u3x_treasure_build_items(void *s, uint32_t set) {
    const uint8_t *table = u3x_sym_table(s, g27::SYM_TREASURES);
    g27::ListState &st = g27::state(s);
    st.treasure_count = 0;
    if (!table) return;
    const uint8_t *entry = rd<const uint8_t *>(table, 8);
    if (!entry) return;
    const uint32_t count = rd<uint32_t>(table, 0);
    uint32_t rows = 0;
    for (uint32_t i = 0; i < count && rows < g27::MAX_ROWS; ++i, entry += g27::TREASURE_SIZE)
        if (rd<uint32_t>(entry, 0x2c) == set) st.treasure_row[rows++] = static_cast<uint16_t>(i);
    st.treasure_count = rows;
}

// the set entry a set row stands for, or null
U3X_FN uint8_t *u3x_treasure_set_entry(void *s, int32_t row) {
    uint8_t *table = u3x_sym_table(s, g27::SYM_SETS);
    const g27::ListState &st = g27::state(s);
    if (!table || row < 0 || static_cast<uint32_t>(row) >= st.set_count) return nullptr;
    const uint32_t index = st.set_row[row];
    if (index >= rd<uint32_t>(table, 0)) return nullptr;
    return rd<uint8_t *>(table, 8) + g27::SET_SIZE * index;
}

// the selected tab of the sets page (the row of the page for the current LAN value decides), or null
U3X_FN uint8_t *u3x_treasure_tab(uintptr_t base, void *s) {
    const uint8_t *table = u3x_sym_table(s, g27::SYM_TABS);
    if (!table) return nullptr;
    const uint8_t *row = rd<const uint8_t *>(table, 8);
    if (!row) return nullptr;
    const uint32_t lan = at<uint8_t>(base + 0x17d5c60);
    for (uint32_t left = rd<uint32_t>(table, 0); left; --left, row += g27::TAB_ROW_SIZE) {
        if (rd<uint32_t>(row, 0) != g27::PAGE_SETS || rd<uint32_t>(row, 4) != lan) continue;
        uint8_t *tabs = rd<uint8_t *>(row, 0x10);
        const int32_t selected = rd<int32_t>(row, 0x18);
        if (!tabs || selected < 0) return nullptr;
        return tabs + g27::TAB_SIZE * static_cast<uintptr_t>(selected);
    }
    return nullptr;
}

// copies template t to [o, end], putting a / b / c in for "[TEXT]" / "[TEXT2]" / "[TEXT" + anything else
U3X_FN void u3x_x66_subst(char *o, char *end, const char *t, const char *a, const char *b, const char *c) {
    while (*t && o < end) {
        int same = 0;
        while (same < 5 && t[same] == g27::placeholder_start[same]) ++same;
        if (same < 5) { *o++ = *t++; continue; }
        const char *piece;
        if (t[5] == ']') { piece = a; t += 6; }
        else if (t[5] == '2') { piece = b; t += 7; }   // the seventh character is skipped unseen
        else { piece = c; t += 7; }
        while (*piece && o < end) *o++ = *piece++;
    }
    *o = 0;
}

// the "found in" line of the first treasure row: game type, map and difficulty
U3X_FN void u3x_x66_found(uintptr_t base, int32_t *ev, void *s) {
    const g27::ListState &st = g27::state(s);
    if (st.treasure_count == 0) return;
    const uint8_t *treasure = u3x_treasure_entry(s, st.treasure_row[0]);
    if (!treasure) return;
    const uint32_t map_id = rd<uint32_t>(treasure, 0x34);
    const int32_t game_type = rd<int32_t>(treasure, 0x38);
    const int32_t difficulty = rd<int32_t>(treasure, 0x3c);

    const uint8_t *maps = u3x_sym_table(s, g27::SYM_MAPS);
    const uint8_t *rules = u3x_sym_table(s, g27::SYM_RULES);

    const uint8_t *map = nullptr;
    if (maps) {
        const uint8_t *entry = rd<const uint8_t *>(maps, 8);
        if (entry)
            for (uint32_t left = rd<uint32_t>(maps, 0); left; --left, entry += g27::MAP_SIZE)
                if (rd<uint32_t>(entry, 0) == map_id) { map = entry; break; }
    }
    const uint8_t *rule = nullptr;
    if (rules && game_type >= 0 && game_type < rd<int32_t>(rules, 0))
        rule = rd<const uint8_t *>(rules, 8) + g27::RULE_SIZE * static_cast<uintptr_t>(game_type);

    char *const line = static_cast<char *>(s) + 0x2b00;
    if (map) {
        char type_name[64], map_name[64];
        g27::copy_cut(type_name, rule ? u3h_text(base, s, rd<uint32_t>(rule, 0)) : g27::empty_text);
        g27::copy_cut(map_name, u3h_text(base, s, rd<uint32_t>(map, 4)));
        const char *pattern = u3h_text(base, s, 0xdcdd1012u);
        const char *level = g27::empty_text;
        if (difficulty == 1 || difficulty == 2) level = u3h_text(base, s, 0x14956c33u);
        else if (difficulty == 3 || difficulty == 4) level = u3h_text(base, s, 0x87d40e88u);
        u3x_x66_subst(line, line + g27::FOUND_LINE_MAX, pattern, type_name, map_name, level);
        g27::set_text(ev, line);
    } else if (game_type == 21) {
        g27::set_text(ev, u3h_text(base, s, 0x35561665u));
    } else {
        uint32_t name_id = 0x85a5bbbeu;
        if (game_type == 20) name_id = 0xd5fc28f0u;
        else if ((game_type == 17 || game_type == 18) && rule) name_id = rd<uint32_t>(rule, 0);
        const char *pattern = u3h_text(base, s, 0x2b3ffa5bu);
        const char *name = u3h_text(base, s, name_id);
        u3x_x66_subst(line, line + g27::FOUND_LINE_MAX, pattern, name, g27::empty_text, g27::empty_text);
        g27::set_text(ev, line);
    }
}

// list source of the sets page (also the title of the treasure list page)
U3X_FN int u3x_treasures_source(uintptr_t base, int32_t *ev, void *s) {
    if (!g27::has_tag(ev, g27::TAG_SETS)) return 0;
    if (u3h_gate_closed(base, ev)) return 1;
    *reinterpret_cast<uint64_t *>(static_cast<uint8_t *>(s) + 760) = base;

    const int32_t op = ev[0];
    if (!u3x_sym_table(s, g27::SYM_SETS) || !u3x_sym_table(s, g27::SYM_TREASURES)) {
        if (op == 6) ev[1] = -1;
        return 1;
    }
    const uint8_t *profile = u3h_player_profile(base);
    const int32_t r = ev[1];
    g27::ListState &st = g27::state(s);

    // the page itself asks: only its title
    if (ev[0x24] == 1 && !(ext_ea()[0] & 8u)) {
        if (op == 1) {
            const char *name = g27::set_name(base, s, st.chosen_set);
            if (name) g27::set_text(ev, name);
        }
        return 1;
    }

    switch (op) {
    case 2: g27::row_text(base, ev, s, r, profile); break;
    case 4: g27::row_picture(base, ev, s, r, profile); break;
    case 5: ev[1] = st.selected; break;
    case 6: ev[1] = static_cast<int32_t>(st.set_count - 1); break;
    case 9: g27::refresh(base, s); break;
    case 10: st.selected = r; break;
    case 11:   // number of preview cells
        if (static_cast<uint32_t>(ev[0x13]) == 0xbcf5ea24u) {
            const uint32_t n = st.treasure_count;
            ev[0xe] = static_cast<int32_t>(n >= 1 && n <= 3 ? 4 : n);
        }
        break;
    case 0x14: if (ev[0x16] == 1) ev[0xf] = -1; break;
    case 0x16: g27::click(base, s, ev[0x17]); break;
    default: break;
    }
    return 1;
}

// list source of the chosen set's treasures: title and refresh here, the rest by the game's own source
U3X_FN int u3x_treasure_items_source(uintptr_t base, int32_t *ev, void *s) {
    if (!g27::has_tag(ev, g27::TAG_ITEMS)) return 0;
    if (u3h_gate_closed(base, ev)) return 1;
    uint8_t *table = u3x_sym_table(s, g27::SYM_TREASURES);
    g27::ListState &st = g27::state(s);

    const int32_t op = ev[0];
    if (op == 1) {
        const bool page = ev[0x24] == 1;
        g27::set_text(ev, g27::empty_text);
        if (page && u3x_sym_table(s, g27::SYM_SETS)) {
            const char *name = g27::set_name(base, s, st.chosen_set);
            if (name) g27::set_text(ev, name);
        }
    } else if (op == 9) {
        if (table) {
            u3x_treasure_build_items(s, st.chosen_set);
            // hand the rows to the game's treasure list: table, row indices, count
            at<uint8_t *>(base + 0x1b6aa38) = table;
            const uint32_t count = st.treasure_count;
            for (uint32_t i = 0; i < count; ++i) at<uint16_t>(base + 0x1b6aa40 + 2 * i) = st.treasure_row[i];
            at<uint32_t>(base + 0x1b6ac40) = count;
        }
    } else {
        game_call<void>(base + 0x759ba0, ev);
    }
    return 1;
}

}  // extern "C"
