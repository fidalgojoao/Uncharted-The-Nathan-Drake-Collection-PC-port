// G24: the kickbacks list (medal kickbacks), its row icons, and the lock-status icon.
// Written from spec/G24_kickbacks.md (names and layouts shared with G23_boosters.md).

extern "C" {
// the C part, other groups, existing exports
int u3h_gate_closed(uintptr_t base, int32_t *ev);
int u3h_sym_ready(uintptr_t base, void *s, uint32_t id);
const char *u3h_text(uintptr_t base, void *s, uint32_t id);
void u3h_text_num(uintptr_t base, void *s, char *buf, uint32_t size, uint32_t id, int32_t value);
const char *u3h_fmt(void *s, const char *format, int64_t a0, const char *s0, const char *s1, const char *s2);
int32_t u3h_icon_index(uintptr_t base, uint32_t id);
void u3h_set_icon(uintptr_t base, int32_t *ev, uint32_t id);
void u3x_icon_answer(int32_t *ev, int32_t kind, int32_t a, int32_t b, int32_t c, int32_t index);
void u3h_k49_kickback(uintptr_t base, void *s, uint32_t user, int coop, int32_t row);
int u3x_booster_unlocked(void *s, uint32_t user, uint32_t id);
void *u3x_unlock_entry(uintptr_t base, void *s, uint32_t id);
uint32_t u3h_crc_sid(const char *name);

// this group
int u3x_kickback_source(uintptr_t base, int32_t *ev, void *s);
void u3x_kickback_icons(uintptr_t base, void *s, int32_t *ev, int32_t row, int coop, uint32_t user, uint8_t *profile,
                        int32_t loadout);
uint8_t *u3x_kickback_nth(void *s, int32_t n, int coop);
int32_t u3x_kickback_count(void *s, int coop);
uint8_t *u3x_kickback_by_id(void *s, uint32_t id);
uint32_t u3x_unlock_icon(int status);
}

namespace g24 {

// texture names of the icons; the icon id is the string id of the name
U3X_RO const char ICON_CAN_BUY[] = "general/net/menu-icons/money";
U3X_RO const char ICON_CANNOT_BUY[] = "general/net/menu-icons/money-slashed";
U3X_RO const char ICON_LOCKED[] = "general/net/menu-icons/icon-locked";
U3X_RO const char ICON_CHECK[] = "general/net/menu-icons/icon-check";
U3X_RO const char ICON_EMPTY[] = "general/net/medal-streak/empty";
U3X_RO const char ICON_FALLBACK[] = "general/net/medal-streak/medal-streak";

// the helper's own texts
U3X_RO const char TEXT_EMPTY[] = "";
U3X_RO const char TEXT_SPACE[] = " ";
U3X_RO const char TEXT_SEPARATOR[] = "\\";              // line separator of this list's menu text
U3X_RO const char TEXT_CASH_COLOUR[] = "|@Cff00ff00|";

// image offsets
constexpr uintptr_t IMG_UNLOCKS = 0x17f3068;        // unlock manager
constexpr uintptr_t IMG_LOADOUT = 0x17d9988;        // s32: loadout being edited
constexpr uintptr_t IMG_SELECTED = 0x17d998c;       // s32: selected row of this list
constexpr uintptr_t IMG_SECOND_USER = 0x17bde75;    // u8: non-zero = menu user 1
constexpr uintptr_t IMG_MENU_USER = 0x17bf714;      // u32: menu user otherwise
constexpr uintptr_t IMG_PROFILES = 0x17dd610;       // pointer: profile objects, 0x7b00 bytes each

constexpr uint32_t SOURCE_TAG = 0x4c414e44;
constexpr uint32_t KICKBACK_TABLE = 0xd9f6ada2;     // script symbol
constexpr uint32_t PICTURE_SLOT = 0xf5a294f3;
constexpr int32_t ITEM_KICKBACK = 7;                // loadout item: the equipped kickback
constexpr int TEXT_LIMIT = 250;                     // characters of the shared text buffer

using Symbol = uint8_t *(*)(uint32_t id);                                             // native 13
using UnlockText = const char *(*)(uint32_t id, uint32_t zero);                       // native 26
using StoreItem = void (*)(uint8_t *profile, int32_t item, uint32_t value, int32_t loadout);   // native 28
using MarkSeen = void (*)(uintptr_t unlocks, uint32_t user, uint32_t id, uint32_t zero);       // native 32
using Level = int32_t (*)(void *block, uint32_t id);                                  // native 33
using LoadItem = uint32_t (*)(uint8_t *profile, int32_t item, int32_t loadout);       // native 34
using Status = int32_t (*)(uintptr_t unlocks, uint32_t user, void *entry);            // native 35
using IsNew = uint32_t (*)(uintptr_t unlocks, uint32_t user, uint32_t id, uint32_t zero);      // native 36

// native k of the state object (may be null)
template <typename F> [[gnu::always_inline]] inline F native(void *s, int k) {
    return rd<F>(s, 768 + 8 * static_cast<uintptr_t>(k));
}

// float pattern to a signed 64-bit integer, fraction cut off (NaN and out of range: the smallest 64-bit integer)
[[gnu::always_inline]] inline int64_t f_to_int64(uint32_t a) {
    int64_t r;
    __asm__("movd %1,%%xmm0\n\tcvttss2siq %%xmm0,%0" : "=r"(r) : "r"(a) : "xmm0");
    return r;
}

// ---- the kickback table (game data, read on every call)

struct Table {
    uint8_t *entries;   // 0x28 bytes each
    uint32_t count;     // 0 when there is nothing to walk
};

[[gnu::always_inline]] inline uint8_t *entry_at(const Table &t, uint32_t i) { return t.entries + 0x28 * static_cast<uintptr_t>(i); }
[[gnu::always_inline]] inline uint32_t id_of(const uint8_t *entry) { return rd<uint32_t>(entry, 0); }
[[gnu::always_inline]] inline uint8_t *tiers_of(const uint8_t *entry) { return rd<uint8_t *>(entry, 0x18); }
[[gnu::always_inline]] inline uint8_t *tier_at(uint8_t *tiers, int32_t level) { return tiers + 0x18 * static_cast<intptr_t>(level); }
[[gnu::always_inline]] inline uint32_t kind_flag(int coop) { return coop ? 2u : 1u; }

U3X_FN static Table table(void *s) {
    Table t = {nullptr, 0};
    const Symbol symbol = native<Symbol>(s, 13);
    uint8_t *data = symbol ? symbol(KICKBACK_TABLE) : nullptr;
    if (data && rd<uint8_t *>(data, 8)) {
        t.entries = rd<uint8_t *>(data, 8);
        t.count = rd<uint32_t>(data, 0);
    }
    return t;
}

// ---- small shared steps

[[gnu::always_inline]] inline void answer_text(int32_t *ev, const char *text) {
    *reinterpret_cast<const char **>(reinterpret_cast<uint8_t *>(ev) + 0x40) = text;
}

// the level the user has reached for a kickback (0 = first tier)
U3X_FN static int32_t level_of(void *s, uint32_t user, uint32_t id) {
    const Level ask = native<Level>(s, 33);
    if (!ask) return 0;
    uint32_t block[8];                      // the native reads only the user, at +0xc
    for (int i = 0; i < 8; ++i) block[i] = 0;
    block[3] = user;
    const int32_t level = ask(block, id);
    return level < 0 ? 0 : level;
}

// answer with a UI icon, when the UI texture table has it
U3X_FN static void show_icon(uintptr_t base, int32_t *ev, uint32_t icon) {
    if (u3h_icon_index(base, icon) >= 0) u3h_set_icon(base, ev, icon);
}

// unlock status of an item (the full s32); the entry may be null
U3X_FN static int32_t status_of(uintptr_t base, void *s, uint32_t user, uint32_t id, Status ask) {
    void *entry = u3x_unlock_entry(base, s, id);
    return ask(base + IMG_UNLOCKS, user, entry);
}

// ---- composing into the shared text buffer

struct Text {
    char *chars;
    int length;
};

U3X_FN static void put(Text &t, const char *piece) {
    while (*piece && t.length < TEXT_LIMIT) t.chars[t.length++] = *piece++;
}

// decimal, "-" for a negative number, optionally a comma between groups of three digits
U3X_FN static void put_number(Text &t, int64_t value, bool grouped) {
    char digits[32];
    int at = 31, count = 0;
    uint64_t rest = value < 0 ? 0 - static_cast<uint64_t>(value) : static_cast<uint64_t>(value);
    digits[at] = 0;
    do {
        if (grouped && count && count % 3 == 0) digits[--at] = ',';
        digits[--at] = static_cast<char>('0' + rest % 10);
        rest /= 10;
        ++count;
    } while (rest);
    if (value < 0) digits[--at] = '-';
    put(t, digits + at);
}

U3X_FN static const char *finish(Text &t) {
    t.chars[t.length] = 0;
    return t.chars;
}

// ---- one request of the list, after the set-up

struct Request {
    uintptr_t base;
    void *s;
    int32_t *ev;
    int32_t row;
    int32_t loadout;
    int coop;
    uint32_t user;
    uint8_t *profile;   // of the menu user, or null
    uint32_t cash;
};

[[gnu::always_inline]] inline Text text_buffer(const Request &q) { return {static_cast<char *>(q.s) + 0x2c80, 0}; }
[[gnu::always_inline]] inline const char *text(const Request &q, uint32_t id) { return u3h_text(q.base, q.s, id); }

// text slot 3: "Level n", the tier's description, the medal cost line
U3X_FN static void level_line(const Request &q, uint8_t *entry) {
    const int32_t level = level_of(q.s, q.user, id_of(entry));
    char cost_line[128];
    cost_line[0] = 0;
    u3h_text_num(q.base, q.s, cost_line, 128, 0x69443c24, rd<int32_t>(entry, 8));

    Text t = text_buffer(q);
    put(t, text(q, 0xadfc7da6));
    put(t, TEXT_SPACE);
    put_number(t, static_cast<int64_t>(static_cast<uint32_t>(level) + 1), false);   // 1 .. 2147483648, not wrapped
    put(t, TEXT_SEPARATOR);
    uint8_t *tiers = tiers_of(entry);
    if (tiers && level < rd<int32_t>(entry, 0xc)) put(t, text(q, rd<uint32_t>(tier_at(tiers, level), 4)));
    put(t, TEXT_SEPARATOR);
    put(t, cost_line);
    answer_text(q.ev, finish(t));
}

// text slot 0xadacdbf3: how long the first tier lasts
U3X_FN static void duration_line(const Request &q, uint8_t *tiers) {
    const uint32_t seconds = rd<uint32_t>(tiers, 8);   // float pattern
    Text t = text_buffer(q);
    put(t, text(q, 0x3badfdd5));
    put(t, TEXT_SPACE);
    const bool until_death = f_less(seconds, 0) || (seconds & 0x7fffffffu) == 0;   // <= 0, false for a NaN
    if (until_death) {
        put(t, text(q, 0xa80e2d11));
    } else {
        put_number(t, f_to_int64(f_add(seconds, 0x3f000000u)), false);   // + 0.5, fraction cut off
        put(t, TEXT_SPACE);
        put(t, text(q, 0xe7624faa));
    }
    answer_text(q.ev, finish(t));
}

// operation 2: the texts of a row
U3X_FN static void row_text(const Request &q) {
    uint8_t *entry = u3x_kickback_nth(q.s, static_cast<int32_t>(static_cast<uint32_t>(q.row) - 1), q.coop);
    uint8_t *tiers = entry ? tiers_of(entry) : nullptr;

    switch (static_cast<uint32_t>(q.ev[0x15])) {
    case 0:   // name
        if (q.row == 0) answer_text(q.ev, text(q, 0x9d3c6e80));
        else if (entry) answer_text(q.ev, text(q, rd<uint32_t>(entry, 4)));
        break;
    case 3:   // level, tier description, cost
        if (q.row == 0) answer_text(q.ev, text(q, 0x034c8e1e));
        else if (entry) level_line(q, entry);
        break;
    case 4:   // owned, or what unlocks it
        if (q.row == 0 || !entry) break;
        if (u3x_booster_unlocked(q.s, q.user, id_of(entry))) {
            answer_text(q.ev, text(q, 0xecf338f6));
        } else {
            const UnlockText how = native<UnlockText>(q.s, 26);
            answer_text(q.ev, how ? how(id_of(entry), 0) : TEXT_EMPTY);
        }
        break;
    case 8: {   // cash, whatever the row
        Text t = text_buffer(q);
        put(t, text(q, 0xba6d58f7));
        put(t, TEXT_CASH_COLOUR);
        put_number(t, q.cash, true);
        answer_text(q.ev, finish(t));
        break;
    }
    case 0x80d4cc7b:   // description of the first tier
        if (tiers) answer_text(q.ev, text(q, rd<uint32_t>(tiers, 4)));
        break;
    case 0xadacdbf3:   // duration of the first tier
        if (tiers) duration_line(q, tiers);
        break;
    case 0xb684d68b:   // medal cost
        if (entry) answer_text(q.ev, u3h_fmt(q.s, text(q, 0x819103cc), rd<int32_t>(entry, 8), nullptr, nullptr, nullptr));
        break;
    default:
        break;
    }
}

// operations 1 and 9: the selected kickback is no longer "new"
U3X_FN static void mark_selected_seen(const Request &q) {
    const int32_t selected = at<int32_t>(q.base + IMG_SELECTED);
    uint8_t *entry = u3x_kickback_nth(q.s, static_cast<int32_t>(static_cast<uint32_t>(selected) - 1), q.coop);
    const MarkSeen seen = native<MarkSeen>(q.s, 32);
    if (entry && seen) seen(q.base + IMG_UNLOCKS, q.user, id_of(entry), 0);
}

// operation 0x16: the clicked row becomes the loadout's kickback, when it is "None" or owned
U3X_FN static void equip(const Request &q) {
    const StoreItem store = native<StoreItem>(q.s, 28);
    if (!store || !q.profile) return;
    const int32_t clicked = q.ev[0x17];
    if (clicked <= 0) {
        store(q.profile, ITEM_KICKBACK, 0, q.loadout);
        return;
    }
    uint8_t *entry = u3x_kickback_nth(q.s, clicked - 1, q.coop);
    if (!entry) return;
    const Status ask = native<Status>(q.s, 35);
    if (ask && status_of(q.base, q.s, q.user, id_of(entry), ask) == 0) store(q.profile, ITEM_KICKBACK, id_of(entry), q.loadout);
}

}  // namespace g24

extern "C" {

// which UI icon stands for unlock status 1, 2, 3 (0 for every other status)
U3X_FN uint32_t u3x_unlock_icon(int status) {
    if (status == 1) return u3h_crc_sid(g24::ICON_CAN_BUY);
    if (status == 2) return u3h_crc_sid(g24::ICON_CANNOT_BUY);
    if (status == 3) return u3h_crc_sid(g24::ICON_LOCKED);
    return 0;
}

// entry number n (from 0) of the co-op or the competitive list
U3X_FN uint8_t *u3x_kickback_nth(void *s, int32_t n, int coop) {
    const g24::Table t = g24::table(s);
    if (n < 0) return nullptr;
    const uint32_t flag = g24::kind_flag(coop);
    for (uint32_t i = 0; i < t.count; ++i) {
        uint8_t *entry = g24::entry_at(t, i);
        if (!(rd<uint32_t>(entry, 0x20) & flag)) continue;
        if (n == 0) return entry;
        --n;
    }
    return nullptr;
}

U3X_FN int32_t u3x_kickback_count(void *s, int coop) {
    const g24::Table t = g24::table(s);
    const uint32_t flag = g24::kind_flag(coop);
    int32_t found = 0;
    for (uint32_t i = 0; i < t.count; ++i)
        if (rd<uint32_t>(g24::entry_at(t, i), 0x20) & flag) ++found;
    return found;
}

// the first entry with this id, whatever list it is in
U3X_FN uint8_t *u3x_kickback_by_id(void *s, uint32_t id) {
    const g24::Table t = g24::table(s);
    for (uint32_t i = 0; i < t.count; ++i)
        if (g24::id_of(g24::entry_at(t, i)) == id) return g24::entry_at(t, i);
    return nullptr;
}

// operation 4: the picture of a row, or its status picture
U3X_FN void u3x_kickback_icons(uintptr_t base, void *s, int32_t *ev, int32_t row, int coop, uint32_t user, uint8_t *profile,
                               int32_t loadout) {
    // row 0 ("None") and rows beyond the list have no entry
    uint8_t *entry = u3x_kickback_nth(s, static_cast<int32_t>(static_cast<uint32_t>(row) - 1), coop);
    const uint32_t image_slot = static_cast<uint32_t>(ev[0x14]);

    if (image_slot == g24::PICTURE_SLOT) {
        ext_ea()[16] += 1;
        if (row == 0 || !entry) {
            g24::show_icon(base, ev, u3h_crc_sid(g24::ICON_EMPTY));
            return;
        }
        // the picture of the tier the player has reached
        const int32_t level = g24::level_of(s, user, g24::id_of(entry));
        uint8_t *tiers = g24::tiers_of(entry);
        const uint32_t icon = tiers ? rd<uint32_t>(g24::tier_at(tiers, level), 0) : 0;
        if (icon && u3h_icon_index(base, icon) >= 0) u3h_set_icon(base, ev, icon);
        else g24::show_icon(base, ev, u3h_crc_sid(g24::ICON_FALLBACK));
        return;
    }
    if (image_slot != 0) return;

    // equipped: the row's entry is the loadout's kickback (also "None" while nothing is equipped)
    const g24::LoadItem load = g24::native<g24::LoadItem>(s, 34);
    uint8_t *worn = (profile && load) ? u3x_kickback_by_id(s, load(profile, g24::ITEM_KICKBACK, loadout)) : nullptr;
    if (entry == worn) {
        g24::show_icon(base, ev, u3h_crc_sid(g24::ICON_CHECK));
        ext_ea()[17] += 1;
        return;
    }
    if (!entry) return;

    // new
    const g24::IsNew is_new = g24::native<g24::IsNew>(s, 36);
    if (is_new && (is_new(base + g24::IMG_UNLOCKS, user, g24::id_of(entry), 0) & 0xff)) {
        u3x_icon_answer(ev, 0x2f, 0, 0, 0, -1);
        ext_ea()[18] += 1;
        return;
    }

    // lock status
    const g24::Status ask = g24::native<g24::Status>(s, 35);
    if (!ask) return;
    const uint32_t icon = u3x_unlock_icon(g24::status_of(base, s, user, g24::id_of(entry), ask));
    if (icon) {
        g24::show_icon(base, ev, icon);
        ext_ea()[19] += 1;
    }
}

// every request of the kickbacks list; 1 = the request was this list's
U3X_FN int u3x_kickback_source(uintptr_t base, int32_t *ev, void *s) {
    const uint8_t *descriptor = rd<const uint8_t *>(ev, 0x98);
    if (!descriptor || rd<uint32_t>(descriptor, 0x1c) != g24::SOURCE_TAG) return 0;
    if (u3h_gate_closed(base, ev)) return 1;
    *reinterpret_cast<uint64_t *>(static_cast<uint8_t *>(s) + 760) = base;

    const int32_t operation = ev[0];
    if (!u3h_sym_ready(base, s, g24::KICKBACK_TABLE)) {   // the kickback table is not loaded
        if (operation == 6) ev[1] = -1;
        return 1;
    }

    g24::Request q;
    q.base = base;
    q.s = s;
    q.ev = ev;
    q.loadout = at<int32_t>(base + g24::IMG_LOADOUT);
    q.coop = q.loadout == 4 ? 1 : 0;
    const uint32_t menu_user = at<uint8_t>(base + g24::IMG_SECOND_USER) ? 1u : at<uint32_t>(base + g24::IMG_MENU_USER);
    q.user = menu_user & 3;
    uint8_t *profiles = at<uint8_t *>(base + g24::IMG_PROFILES);
    q.profile = (profiles && menu_user < 4) ? profiles + 0x7b00 * static_cast<uintptr_t>(menu_user) : nullptr;
    q.cash = q.profile ? rd<uint32_t>(q.profile, 0x74c) : 0;
    q.row = ev[1];
    static_cast<char *>(s)[0x2c80] = 0;   // the shared text buffer starts empty, for every operation

    if (operation == 0x16) u3h_k49_kickback(base, s, q.user, q.coop, ev[0x17]);   // the pick sound

    switch (operation) {
    case 1:   // title; the selected row is seen
        g24::answer_text(ev, u3h_text(base, s, 0x87b43182));
        g24::mark_selected_seen(q);
        break;
    case 9:
        g24::mark_selected_seen(q);
        break;
    case 2:
        g24::row_text(q);
        break;
    case 3: {   // locked flag
        if (q.row == 0) break;
        uint8_t *entry = u3x_kickback_nth(s, static_cast<int32_t>(static_cast<uint32_t>(q.row) - 1), q.coop);
        if (entry && !u3x_booster_unlocked(s, q.user, g24::id_of(entry))) ev[0xe] = 2;
        break;
    }
    case 4:   // row icons, unless switched off
        if (!(ext_ea()[0] & 0x10)) u3x_kickback_icons(base, s, ev, q.row, q.coop, q.user, q.profile, q.loadout);
        break;
    case 5:   // selected row
        ev[1] = at<int32_t>(base + g24::IMG_SELECTED);
        break;
    case 6:   // index of the last row ("None" is row 0)
        ev[1] = u3x_kickback_count(s, q.coop);
        break;
    case 10:   // select a row, as it comes
        at<int32_t>(base + g24::IMG_SELECTED) = q.row;
        break;
    case 11:   // value query
        if (static_cast<uint32_t>(ev[0x13]) == 0x67a48eb9u) ev[0xe] = 4;
        break;
    case 12:   // cell size 128 x 128, on the boosters' icon switch
        if (!(ext_ea()[0] & 0x20)) ev[0x1f] = ev[0x20] = 0x43000000;
        break;
    case 0x11:
        ev[1] = 8;
        break;
    case 0x16:
        g24::equip(q);
        break;
    default:
        break;
    }
    return 1;
}

}  // extern "C"
