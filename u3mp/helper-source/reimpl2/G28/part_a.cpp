// G28 part A: the medals list (PAGE_MULTIPLAYER_MEDALS). Written from spec G28_medals_records.md, sections 3 and
// 4.1 to 4.4. Which medals the current tab shows, and every request of the list.

extern "C" {
// other groups and the C part
int u3h_gate_closed(uintptr_t base, int32_t *ev);
const char *u3h_text(uintptr_t base, void *s, uint32_t id);
const char *u3h_fmt(void *s, const char *format, int64_t a0, const char *s0, const char *s1, const char *s2);
char *u3h_fmt_int(char *o, char *end, int64_t v, int32_t commas);
uint32_t u3h_current_tab(uintptr_t base, void *s);
uint8_t *u3x_sym_table(void *s, uint32_t id);
// this part
int u3x_medals_source(uintptr_t base, int32_t *event, void *s);
void u3x_medal_build(uintptr_t base, void *s);
uint32_t u3x_w04_tab(uintptr_t base, void *s);
uint8_t *u3x_medal_record(void *s, uint32_t i);
}

namespace g28a {

// script symbols
constexpr uint32_t SYM_MEDALS = 0x980d6543u;      // the medal table (through native 13)
constexpr uint32_t SYM_TABS = 0xf1514340u;        // the tab table
// pages, tags, tabs
constexpr uint32_t PAGE_MEDALS = 0xc98ee52au;
constexpr uint32_t TAG_MEDALS = 0x4c414ed3u;
constexpr uint32_t TAB_CLASS_1 = 0x091026d1u;     // shows medals with class bit 1
constexpr uint32_t TAB_CLASS_2 = 0xfb0dd522u;     // class bit 2
constexpr uint32_t TAB_CLASS_3 = 0xa13b68fbu;     // class bit 3
constexpr uint32_t TAB_CLASS_4 = 0xadbd5585u;     // class bit 4
constexpr uint32_t MEDAL_OLD = 0xa8e90238u;       // name id of the replaced medal
constexpr uint32_t MEDAL_NEW = 0x9befafdeu;       // name id of its replacement
constexpr uint32_t NONE = 0xffffffffu;            // "no position"
// request slots and ids
constexpr uint32_t SLOT_NAME = 0;
constexpr uint32_t SLOT_DESCRIPTION = 0x80d4cc7bu;
constexpr uint32_t SLOT_EARNED = 0xadacdbf3u;
constexpr uint32_t SLOT_POINTS = 0xb684d68bu;
constexpr uint32_t SLOT_PICTURE = 0xf5a294f3u;
constexpr uint32_t VALUE_ID = 0x67a48eb9u;
constexpr uint32_t TEXT_EARNED = 0xe9a0ddd7u;     // template with "%d"
constexpr uint32_t TEXT_POINTS = 0xb4b0d5c2u;     // points label
constexpr uint32_t F_2_25 = 0x40100000u;
// switch
constexpr uint32_t EB3_OLD_MEDALS = 0x08000000u;  // eb[3] bit 27
// state object
constexpr uintptr_t S_NATIVES = 768;
constexpr uintptr_t S_LIST = 0x2800;
constexpr uintptr_t S_TEXT = 0x2b00;
constexpr uint32_t MAX_ROWS = 256;
constexpr uint32_t LINE_MAX = 120;
// image
constexpr uintptr_t IMG_LAN = 0x17d5c60;
constexpr uintptr_t IMG_MEDAL_STATS = 0x17bb878;
constexpr uintptr_t IMG_TEXTURES = 0x1b31128;
constexpr uintptr_t IMG_MENU_USER_FLAG = 0x17bde75;
constexpr uintptr_t IMG_MENU_USER = 0x17bf714;

U3X_RO const char points_separator[] = " |@Cff7cce96|$";

// the medal list state at s + 0x2800
struct List {
    uint16_t rows[256];
    uint16_t count;
    uint16_t unused;
    uint32_t selected;
    uint32_t tab;
    uint32_t built;
};

[[gnu::always_inline]] inline List *list_of(void *s) {
    return reinterpret_cast<List *>(static_cast<uint8_t *>(s) + S_LIST);
}
[[gnu::always_inline]] inline uint32_t *ext_ec() {
    return reinterpret_cast<uint32_t *>(u3h_ext() + 0x4450);
}
template <typename F> [[gnu::always_inline]] inline F native(void *s, unsigned k) {
    return rd<F>(s, S_NATIVES + 8u * k);
}
[[gnu::always_inline]] inline uint32_t &word(int32_t *event, unsigned n) {
    return reinterpret_cast<uint32_t *>(event)[n];
}
[[gnu::always_inline]] inline void text_answer(int32_t *event, const char *text) {
    *reinterpret_cast<const char **>(reinterpret_cast<uint8_t *>(event) + 0x40) = text;
}

// one lookup of the medal table; null without native 13
U3X_FN const uint8_t *medal_table(void *s) {
    const auto lookup = native<const uint8_t *(*)(uint32_t)>(s, 13);
    return lookup ? lookup(SYM_MEDALS) : nullptr;
}

// the class bit a medal needs to be shown on a tab; 0 = the tab shows nothing
U3X_FN uint32_t tab_class(uint32_t tab) {
    if (tab == TAB_CLASS_1) return 2;
    if (tab == TAB_CLASS_2) return 4;
    if (tab == TAB_CLASS_3) return 8;
    if (tab == TAB_CLASS_4) return 0x10;
    return 0;
}

[[gnu::always_inline]] inline uint32_t menu_user(uintptr_t base) {
    return at<uint8_t>(base + IMG_MENU_USER_FLAG) ? 1u : at<uint32_t>(base + IMG_MENU_USER);
}

// adds text to the line at position n, never beyond LINE_MAX characters; returns the new length
U3X_FN uint32_t append(char *line, uint32_t n, const char *text) {
    while (*text && n < LINE_MAX) line[n++] = *text++;
    return n;
}

// "label |colour|$ 1,234" into the buffer shared with the treasures list
U3X_FN void points_line(uintptr_t base, void *s, const uint8_t *record) {
    char number[24];
    *u3h_fmt_int(number, number + 23, rd<int32_t>(record, 0x20), 1) = 0;
    const char *label = u3h_text(base, s, TEXT_POINTS);
    char *line = static_cast<char *>(s) + S_TEXT;
    uint32_t n = append(line, 0, label);
    n = append(line, n, points_separator);
    n = append(line, n, number);
    line[n] = 0;
}

// the text of a row, by the text slot asked
U3X_FN void row_text(uintptr_t base, int32_t *event, void *s, uint8_t *record) {
    const uint32_t slot = word(event, 0x15);
    if (slot == SLOT_NAME) {
        text_answer(event, u3h_text(base, s, rd<uint32_t>(record, 0)));
    } else if (slot == SLOT_DESCRIPTION) {
        text_answer(event, u3h_text(base, s, rd<uint32_t>(record, 4)));
    } else if (slot == SLOT_EARNED) {
        ext_ec()[250] += 1;
        const auto earned = native<uint32_t (*)(uintptr_t, uint8_t *, uint32_t)>(s, 19);
        const uint32_t times = earned ? earned(base + IMG_MEDAL_STATS, record, menu_user(base)) : 0;
        const char *format = u3h_text(base, s, TEXT_EARNED);
        text_answer(event, u3h_fmt(s, format, static_cast<int64_t>(times), nullptr, nullptr, nullptr));
    } else if (slot == SLOT_POINTS) {
        ext_ec()[251] += 1;
        points_line(base, s, record);
        text_answer(event, static_cast<char *>(s) + S_TEXT);
    }
}

// a usable texture name: 1 to 47 printable ASCII characters, one of them a slash
U3X_FN bool picture_name_ok(const char *name) {
    if (!name) return false;
    bool slash = false;
    for (unsigned i = 0; i < 48; i++) {
        const uint8_t c = static_cast<uint8_t>(name[i]);
        if (!c) return i != 0 && slash;
        if (c < 0x20 || c > 0x7e) return false;
        if (c == '/') slash = true;
    }
    return false;
}

// the picture of a row: the medal's texture, or the game's placeholder
U3X_FN void row_picture(uintptr_t base, int32_t *event, void *s, const uint8_t *record) {
    const char *name = rd<const char *>(record, 0x18);
    const auto find = native<void *(*)(uintptr_t, const char *)>(s, 20);
    void *texture = nullptr;
    if (find && picture_name_ok(name)) texture = find(base + IMG_TEXTURES, name);

    for (unsigned n = 4; n <= 11; n++) word(event, n) = 0;
    word(event, 9) = 0xffffffffu;
    word(event, 11) = 0xffffffffu;
    if (texture) {
        const uint64_t value = reinterpret_cast<uintptr_t>(texture);
        word(event, 2) = 2;
        word(event, 4) = static_cast<uint32_t>(value);          // pointer at byte 0x10
        word(event, 5) = static_cast<uint32_t>(value >> 32);
        word(event, 12) = F_ONE;
    } else {
        word(event, 2) = 0x10;
        word(event, 12) = F_2_25;
    }
}

// the medal record of a list row; null beyond the list
U3X_FN uint8_t *row_record(void *s, uint32_t row) {
    const List *list = list_of(s);
    return row < list->count ? u3x_medal_record(s, list->rows[row]) : nullptr;
}

}  // namespace g28a

extern "C" {

// record i of the medal table, or null
U3X_FN uint8_t *u3x_medal_record(void *s, uint32_t i) {
    const uint8_t *table = g28a::medal_table(s);
    if (!table || i >= rd<uint32_t>(table, 0)) return nullptr;
    return rd<uint8_t *>(table, 8) + 0x48ull * i;
}

// text id of the tab the medals page shows, or 0
U3X_FN uint32_t u3x_w04_tab(uintptr_t base, void *s) {
    if (ext_eb()[3] & g28a::EB3_OLD_MEDALS) return u3h_current_tab(base, s);
    const uint8_t *table = u3x_sym_table(s, g28a::SYM_TABS);
    if (!table) return 0;
    const uint8_t *rows = rd<const uint8_t *>(table, 8);
    if (!rows) return 0;
    uint32_t n = rd<uint32_t>(table, 0);
    if (n > 1024) n = 1024;
    const uint32_t lan = at<uint8_t>(base + g28a::IMG_LAN);
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *row = rows + 0x20ull * i;
        if (rd<uint32_t>(row, 0) != g28a::PAGE_MEDALS || rd<uint32_t>(row, 4) != lan) continue;
        // the first row of the page decides, usable or not
        const uint8_t *tabs = rd<const uint8_t *>(row, 0x10);
        const int32_t selected = rd<int32_t>(row, 0x18);
        if (!tabs || selected < 0) return 0;
        return rd<uint32_t>(tabs, 0xcull * static_cast<uint32_t>(selected));
    }
    return 0;
}

// fills the list with the medals of the current tab
U3X_FN void u3x_medal_build(uintptr_t base, void *s) {
    g28a::List *list = g28a::list_of(s);
    const uint32_t tab = u3x_w04_tab(base, s);
    const uint32_t mask = g28a::tab_class(tab);
    list->tab = tab;
    list->built = 1;
    list->count = 0;

    const uint8_t *table = g28a::medal_table(s);
    if (!table || !mask) return;
    const uint32_t n = rd<uint32_t>(table, 0);

    // where the replaced medal and its replacement are (the first of each)
    uint32_t old_at = g28a::NONE, new_at = g28a::NONE;
    if (!(ext_eb()[3] & g28a::EB3_OLD_MEDALS)) {
        for (uint32_t i = 0; i < n; i++) {
            const uint8_t *record = u3x_medal_record(s, i);
            if (!record) continue;
            const uint32_t name = rd<uint32_t>(record, 0);
            if (name == g28a::MEDAL_OLD && old_at == g28a::NONE) old_at = i;
            if (name == g28a::MEDAL_NEW && new_at == g28a::NONE) new_at = i;
        }
        if (old_at == g28a::NONE || new_at == g28a::NONE || new_at < old_at) old_at = new_at = g28a::NONE;
    }

    // the row count is the list's own: it is read from the list again after every table lookup
    for (uint32_t i = 0; i < n && list->count < g28a::MAX_ROWS; i++) {
        if (i == new_at) continue;   // shown at the old medal's place
        const uint32_t shown = i == old_at ? new_at : i;
        const uint8_t *record = u3x_medal_record(s, shown);
        if (record && (rd<uint32_t>(record, 0x10) & mask) == mask) {
            const uint16_t at_row = list->count;
            list->rows[at_row] = static_cast<uint16_t>(shown);
            list->count = static_cast<uint16_t>(at_row + 1u);
        }
    }
    if (list->selected >= list->count) list->selected = 0;
}

// every request of the medals list
U3X_FN int u3x_medals_source(uintptr_t base, int32_t *event, void *s) {
    const uint8_t *descriptor = rd<const uint8_t *>(event, 0x98);
    if (!descriptor || rd<uint32_t>(descriptor, 0x1c) != g28a::TAG_MEDALS) return 0;
    if (u3h_gate_closed(base, event)) return 1;

    g28a::List *list = g28a::list_of(s);
    if (!list->built) {
        u3x_medal_build(base, s);
    } else {
        const uint32_t stored_tab = list->tab;   // as it was before the tab is asked
        if (u3x_w04_tab(base, s) != stored_tab) u3x_medal_build(base, s);
    }

    const uint32_t row = g28a::word(event, 1);
    const uint32_t op = g28a::word(event, 0);
    if (op == 2 || op == 10) {
        if (op == 10) list->selected = row;
        if (uint8_t *record = g28a::row_record(s, row)) g28a::row_text(base, event, s, record);
    } else if (op == 4) {
        if (row < list->count && g28a::word(event, 0x14) == g28a::SLOT_PICTURE) {
            if (const uint8_t *record = g28a::row_record(s, row)) g28a::row_picture(base, event, s, record);
        }
    } else if (op == 5) {
        g28a::word(event, 1) = list->selected;
    } else if (op == 6) {
        g28a::word(event, 1) = static_cast<uint32_t>(list->count) - 1u;
    } else if (op == 9) {
        u3x_medal_build(base, s);
    } else if (op == 11) {
        uint32_t *ec = g28a::ext_ec();
        const uint32_t id = g28a::word(event, 0x13);
        ec[252] += 1;
        ec[253] = id;
        if (id == g28a::VALUE_ID) g28a::word(event, 0xe) = 4;
    }
    return 1;
}

}  // extern "C"
