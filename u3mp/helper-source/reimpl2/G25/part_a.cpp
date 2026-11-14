// G25, part A: the co-op weapon list (source 0xa6), the co-op weapon mods list (source 0xa7) and their accessors.
// Written from the behaviour description spec\G25_coop_lists.md, sections 4.1 to 4.7.

extern "C" {
// other groups and the C part (u3h_ext comes with the shared tools)
int u3h_gate_closed(uintptr_t base, int32_t *ev);
int u3h_sym_ready(uintptr_t base, void *s, uint32_t id);
const char *u3h_text(uintptr_t base, void *s, uint32_t id);
int32_t u3h_icon_index(uintptr_t base, uint32_t id);
void u3h_set_icon(uintptr_t base, int32_t *ev, uint32_t id);
uint32_t u3h_crc_sid(const char *name);
uint8_t *u3x_sym_table(void *s, uint32_t id);
int u3x_unlock_status(uintptr_t base, void *s, uint32_t user, uint32_t id, uint32_t mod);
uint32_t u3x_unlock_icon(int status);
int u3x_mod_id(const uint8_t *mods, uint32_t count, int32_t row, uint32_t *id);
int u3x_click_sound(int unlock_status);
void u3h_k49_snd(uintptr_t base, int n);
void u3h_coop_mod_sound(uintptr_t base, void *s, uint32_t user, uint32_t weapon, uint8_t *mods, uint32_t n, int32_t row);
void u3h_lock_overlay(int32_t *ev);
int u3h_j11_rowimg(int32_t *ev, uint32_t weapon);
void u3h_preview_mods(uintptr_t base, uint32_t mask);
int u3h_show(uintptr_t base, void *s, uint32_t page, int silent);

// this part
int u3x_coop_weapon_source(uintptr_t base, int32_t *ev, void *s);
int u3x_coop_mods(uintptr_t base, int32_t *ev, void *s);
uint8_t *u3x_coop_weapon(void *s, uint32_t i);
int32_t u3x_coop_weapon_count(void *s);
uint8_t *u3x_weapon_def(void *s, uint32_t id);
uint32_t u3x_mod_icon(void *s, uint32_t bit);
void u3x_money_text(uintptr_t base, void *s, int32_t *ev);
void u3x_k49_coopmod(uintptr_t base, void *s, uint32_t user, uint32_t weapon, uint8_t *mods, uint32_t n, int32_t row);
}

namespace g25a {
namespace {

// script symbols
constexpr uint32_t SYM_COOP_WEAPONS = 0x556e3fb5u;   // the co-op weapon table
constexpr uint32_t SYM_WEAPON_DEFS = 0x61d589d9u;    // the weapon definitions (hash table)
constexpr uint32_t SYM_MOD_ICONS = 0x36a4c63bu;      // the icon of each mod bit

// source tags
constexpr uint32_t TAG_WEAPON_LIST = 0x4c414ea6u;
constexpr uint32_t TAG_MODS_LIST = 0x4c414ea7u;

// image offsets
constexpr uintptr_t IMG_MENU_USER_FLAG = 0x17bde75;   // u8: non-zero = menu user 1
constexpr uintptr_t IMG_MENU_USER = 0x17bf714;        // u32: the menu user otherwise
constexpr uintptr_t IMG_PROFILES = 0x17dd610;         // pointer: profile objects, 0x7b00 bytes each
constexpr uintptr_t IMG_CHOSEN_WEAPON = 0x17d9998;    // the weapon index chosen in the weapon list
constexpr uintptr_t IMG_MODS_SELECTED = 0x1b6ae68;    // s32: the selected row of the mods list
constexpr uintptr_t IMG_MENU_SCENE = 0x17bba90;
constexpr uintptr_t IMG_UNLOCKS = 0x17f3068;          // the unlock manager
constexpr uintptr_t FN_PREVIEW_REFRESH = 0x5b3e40;    // (scene, int, int, int)
constexpr uintptr_t FN_MOD_NAME_ID = 0xd47450;        // (s32 mod bit) -> text id
constexpr uintptr_t FN_MOD_DESC_ID = 0xd47570;        // (s32 mod bit) -> text id

// profile object fields
constexpr uintptr_t PROFILE_CASH = 0x74c;
constexpr intptr_t PROFILE_COOP_MODS = 0x1534;        // u8 per weapon index: the fitted mods

// state object fields
constexpr uintptr_t STATE_SHOW_PAGE = 648;
constexpr uintptr_t STATE_BASE = 760;
constexpr uintptr_t STATE_NATIVES = 768;
constexpr uintptr_t STATE_TEXT = 0x2c80;              // 251 characters
constexpr uintptr_t STATE_WEAPON_SELECTED = 0x3900;   // s32: the selected row of the weapon list

constexpr uint32_t TEXT_CASH_LABEL = 0xba6d58f7u;
constexpr uint32_t PAGE_COOP_MODS = 0xba7ae3b4u;
constexpr int TEXT_LIMIT = 250;

U3X_RO const char cash_colour[] = "|@Cff00ff00|";
U3X_RO const char check_icon_name[] = "general/net/menu-icons/icon-check";

[[gnu::always_inline]] inline uint8_t *bytes(void *p) { return static_cast<uint8_t *>(p); }

// native k of the state object, as a function pointer of type F (may be null)
template <typename F> [[gnu::always_inline]] inline F native(void *s, int k) {
    return rd<F>(s, STATE_NATIVES + 8 * static_cast<uintptr_t>(k));
}

[[gnu::always_inline]] inline uint32_t menu_user(uintptr_t base) {
    return at<uint8_t>(base + IMG_MENU_USER_FLAG) ? 1u : at<uint32_t>(base + IMG_MENU_USER);
}

// the profile object of the menu user, or null
[[gnu::always_inline]] inline uint8_t *menu_profile(uintptr_t base) {
    uint8_t *profiles = at<uint8_t *>(base + IMG_PROFILES);
    const uint32_t user = menu_user(base);
    return (profiles && user < 4) ? profiles + 0x7b00u * user : nullptr;
}

[[gnu::always_inline]] inline void set_text(int32_t *ev, const char *text) {
    *reinterpret_cast<const char **>(reinterpret_cast<uint8_t *>(ev) + 0x40) = text;
}

// fields of a weapon of the co-op weapon table
[[gnu::always_inline]] inline uint32_t weapon_id(const uint8_t *w) { return rd<uint32_t>(w, 0); }
[[gnu::always_inline]] inline uint8_t *weapon_mods(const uint8_t *w) { return rd<uint8_t *>(w, 8); }
[[gnu::always_inline]] inline uint32_t weapon_mod_count(const uint8_t *w) { return rd<uint32_t>(w, 0x10); }
[[gnu::always_inline]] inline uint32_t mod_bit(const uint8_t *mods, uint32_t row) {
    return rd<uint32_t>(mods, 0x14 * static_cast<uintptr_t>(row));
}

// writes pieces one after the other into the state's text buffer, at most 250 characters
struct Composer {
    char *out;
    int n = 0;
    [[gnu::always_inline]] void put(char c) { if (n < TEXT_LIMIT) out[n++] = c; }
    [[gnu::always_inline]] void text(const char *t) { while (*t && n < TEXT_LIMIT) out[n++] = *t++; }
    // decimal, with a comma between groups of three digits counted from the right
    [[gnu::always_inline]] void grouped(uint32_t v) {
        char digit[10];
        int k = 0;
        do { digit[k++] = static_cast<char>('0' + v % 10); v /= 10; } while (v);
        while (k) {
            put(digit[--k]);
            if (k && k % 3 == 0) put(',');
        }
    }
    [[gnu::always_inline]] void end() { out[n] = 0; }
};

// "show icon": only an icon of the UI texture table becomes the answer
U3X_FN void show_icon(uintptr_t base, int32_t *ev, uint32_t icon) {
    if (u3h_icon_index(base, icon) >= 0) u3h_set_icon(base, ev, icon);
}

// text answer = the name of weapon `id`, when it has a definition
U3X_FN void answer_weapon_name(uintptr_t base, void *s, int32_t *ev, uint32_t id) {
    const uint8_t *def = u3x_weapon_def(s, id);
    if (def) set_text(ev, u3h_text(base, s, rd<uint32_t>(def, 4)));
}

// The start both lists share: tag, gate, base, the two script tables.
enum class Entry { not_ours, answered, go_on };

U3X_FN Entry enter_list(uintptr_t base, int32_t *ev, void *s, uint32_t tag) {
    const uint8_t *descriptor = rd<const uint8_t *>(ev, 0x98);
    if (!descriptor || rd<uint32_t>(descriptor, 0x1c) != tag) return Entry::not_ours;
    if (u3h_gate_closed(base, ev)) return Entry::answered;
    *reinterpret_cast<uint64_t *>(bytes(s) + STATE_BASE) = base;
    if (!u3h_sym_ready(base, s, SYM_COOP_WEAPONS) || !u3h_sym_ready(base, s, SYM_WEAPON_DEFS)) {
        if (ev[0] == 6) ev[1] = -1;
        return Entry::answered;
    }
    return Entry::go_on;
}

// ---- the weapon list ----

// the icon of the first (which = 0) or second (which = 1) fitted mod of the row's weapon
U3X_FN void fitted_mod_picture(uintptr_t base, void *s, int32_t *ev, int32_t row, uint32_t which) {
    const uint8_t *profile = menu_profile(base);
    // read with the row as it came, before the row is known to be a weapon
    const uint32_t mask = profile ? profile[PROFILE_COOP_MODS + static_cast<intptr_t>(row)] : 0u;
    if (!mask) return;
    const uint8_t *w = u3x_coop_weapon(s, static_cast<uint32_t>(row));
    if (!w) return;
    const uint8_t *mods = weapon_mods(w);
    if (!mods) return;
    uint32_t rows = weapon_mod_count(w);
    if (rows > 8) rows = 8;
    uint32_t fitted = 0;
    for (uint32_t k = 0; k < rows; ++k) {
        if (!((mask >> k) & 1)) continue;
        if (fitted++ != which) continue;
        show_icon(base, ev, u3x_mod_icon(s, mod_bit(mods, k)));
        return;
    }
}

U3X_FN void weapon_row_picture(uintptr_t base, void *s, int32_t *ev, int32_t row, uint32_t user) {
    const uint32_t slot = static_cast<uint32_t>(ev[0x14]);
    if (slot == 0) {   // the gun picture
        const uint8_t *w = u3x_coop_weapon(s, static_cast<uint32_t>(row));
        if (w) u3h_j11_rowimg(ev, weapon_id(w));
    } else if (slot == 0xbb36238eu) {   // the "new" marker
        const uint8_t *w = u3x_coop_weapon(s, static_cast<uint32_t>(row));
        const auto is_new = native<uint64_t (*)(uintptr_t, uint32_t, uint32_t)>(s, 37);
        if (w && is_new && (is_new(base + IMG_UNLOCKS, user, weapon_id(w)) & 0xff)) u3h_lock_overlay(ev);
    } else if (slot == 0x5095aa91u) {
        fitted_mod_picture(base, s, ev, row, 0);
    } else if (slot == 0x5454b726u) {
        fitted_mod_picture(base, s, ev, row, 1);
    }
}

// the click: choose the weapon, refresh the preview, open the mods page on its first row
U3X_FN void weapon_click(uintptr_t base, void *s, int32_t *ev) {
    at<int32_t>(base + IMG_CHOSEN_WEAPON) = ev[0x17];
    game_call<void>(base + FN_PREVIEW_REFRESH, reinterpret_cast<void *>(base + IMG_MENU_SCENE), 0, 0, 1);
    at<int32_t>(base + IMG_MODS_SELECTED) = 0;
    if (rd<void *>(s, STATE_SHOW_PAGE)) u3h_show(base, s, PAGE_COOP_MODS, 0);
}

// ---- the mods list ----

// what the mods list knows about its weapon and the row asked
struct ModsRequest {
    uintptr_t base;
    void *s;
    int32_t *ev;
    uint32_t index;     // the weapon index
    uint32_t weapon;    // its id
    uint8_t *mods;      // its mod rows (may be null)
    uint32_t count;     // their number
    uint8_t *profile;   // of the menu user (may be null)
    uint32_t user;
    int32_t row;
    uint32_t bit;       // the mod bit of `row`, 0 when there is no such row

    [[gnu::always_inline]] bool has_row(int32_t r) const { return mods && r >= 0 && static_cast<uint32_t>(r) < count; }
    [[gnu::always_inline]] uint8_t &mask() const { return profile[PROFILE_COOP_MODS + static_cast<uintptr_t>(index)]; }
    [[gnu::always_inline]] int status(uint32_t b) const { return u3x_unlock_status(base, s, user, weapon, b); }
};

U3X_FN void mods_row_text(const ModsRequest &q) {
    const uint32_t slot = static_cast<uint32_t>(q.ev[0x15]);
    if (slot == 0 || slot == 3) {   // the mod's name / description: the game knows the text id
        if (!q.bit) return;
        const uintptr_t fn = q.base + (slot == 0 ? FN_MOD_NAME_ID : FN_MOD_DESC_ID);
        const uint32_t id = game_call<uint32_t>(fn, static_cast<int32_t>(q.bit));
        set_text(q.ev, u3h_text(q.base, q.s, id));
    } else if (slot == 1) {   // what unlocks it
        if (!q.bit || q.status(q.bit) == 0) return;
        const auto unlock_text = native<const char *(*)(uint32_t, uint32_t)>(q.s, 26);
        if (unlock_text) set_text(q.ev, unlock_text(q.weapon, q.bit));
    } else if (slot == 8) {   // the cash, for a mod that costs money
        const int status = q.bit ? q.status(q.bit) : 0;
        if (status == 1 || status == 2) u3x_money_text(q.base, q.s, q.ev);
    } else if (slot == 0x5d822913u) {   // the weapon's name, whatever the row
        answer_weapon_name(q.base, q.s, q.ev, q.weapon);
    }
}

U3X_FN void mods_row_picture(const ModsRequest &q) {
    if (!q.bit) return;
    const uint32_t slot = static_cast<uint32_t>(q.ev[0x14]);
    if (slot == 0xf5a294f3u) {   // the mod's icon
        u3h_set_icon(q.base, q.ev, u3x_mod_icon(q.s, q.bit));
        return;
    }
    if (slot != 0) return;
    // the status picture: "new" first, then equipped / lock status
    const auto is_new = native<uint64_t (*)(uintptr_t, uint32_t, uint32_t, uint32_t)>(q.s, 36);
    if (is_new && (is_new(q.base + IMG_UNLOCKS, q.user, q.weapon, q.bit) & 0xff)) {
        u3h_lock_overlay(q.ev);
        return;
    }
    const int status = q.status(q.bit);
    if (status == 0) {
        // the mask has 8 bits; the row number is taken modulo 32, so rows 8 and above never show the check
        const uint32_t n = static_cast<uint32_t>(q.row) & 31;
        if (q.profile && ((static_cast<uint32_t>(q.mask()) >> n) & 1)) u3h_set_icon(q.base, q.ev, u3h_crc_sid(check_icon_name));
    } else if (status >= 1 && status <= 3) {
        u3h_set_icon(q.base, q.ev, u3x_unlock_icon(status));
    }
}

// page update: show the saved mods again and mark the selected mod as seen
U3X_FN void mods_refresh(const ModsRequest &q) {
    if (q.profile) u3h_preview_mods(q.base, q.mask());
    const int32_t selected = at<int32_t>(q.base + IMG_MODS_SELECTED);
    const auto mark_seen = native<void (*)(uintptr_t, uint32_t, uint32_t, uint32_t)>(q.s, 32);
    if (mark_seen && q.has_row(selected))
        mark_seen(q.base + IMG_UNLOCKS, q.user, q.weapon, mod_bit(q.mods, static_cast<uint32_t>(selected)));
}

// the click: fit or remove an owned mod, then refresh the preview
U3X_FN void mods_toggle(const ModsRequest &q) {
    const int32_t clicked = q.ev[0x17];
    if (!q.profile || !q.has_row(clicked)) return;
    if (q.status(mod_bit(q.mods, static_cast<uint32_t>(clicked))) == 0)
        q.mask() = static_cast<uint8_t>(q.mask() ^ (1u << (clicked & 7)));   // modulo 8: the mask is one byte
    u3h_preview_mods(q.base, q.mask());
}

}  // namespace
}  // namespace g25a

// The co-op weapon of table index i, or null.
extern "C" U3X_FN uint8_t *u3x_coop_weapon(void *s, uint32_t i) {
    const uint8_t *table = u3x_sym_table(s, g25a::SYM_COOP_WEAPONS);
    if (!table || i >= rd<uint32_t>(table, 0)) return nullptr;
    return rd<uint8_t *>(table, 8) + 0x18 * static_cast<uintptr_t>(i);
}

// The number of co-op weapons (0 without the table).
extern "C" U3X_FN int32_t u3x_coop_weapon_count(void *s) {
    const uint8_t *table = u3x_sym_table(s, g25a::SYM_COOP_WEAPONS);
    return table ? rd<int32_t>(table, 0) : 0;
}

// The definition of weapon `id` from the game's hash table (open addressing, home slot id mod N), or null.
extern "C" U3X_FN uint8_t *u3x_weapon_def(void *s, uint32_t id) {
    const auto symbol_data = g25a::native<uint8_t *(*)(uint32_t)>(s, 13);
    if (!symbol_data) return nullptr;
    const uint8_t *table = symbol_data(g25a::SYM_WEAPON_DEFS);
    if (!table) return nullptr;
    const uint32_t slots = rd<uint32_t>(table, 0);
    if (!slots) return nullptr;
    const uint32_t *keys = rd<const uint32_t *>(table, 8);
    uint8_t *const *defs = rd<uint8_t *const *>(table, 0x10);
    uint32_t slot = id % slots;
    for (uint32_t probed = 0; probed < slots; ++probed) {
        const uint32_t key = keys[slot];
        if (key == id) return defs[slot];   // tested first: id 0 "finds" the first empty slot
        if (key == 0) return nullptr;
        if (++slot == slots) slot = 0;
    }
    return nullptr;
}

// The UI icon of a mod bit: the icon at position k for bit 1 << k, 0 for anything else.
extern "C" U3X_FN uint32_t u3x_mod_icon(void *s, uint32_t bit) {
    const uint8_t *table = u3x_sym_table(s, g25a::SYM_MOD_ICONS);
    if (!table) return 0;
    const uint32_t count = rd<uint32_t>(table, 0);
    for (uint32_t k = 0; k < 32 && k < count; ++k)
        if (bit == (1u << k)) return rd<uint32_t>(rd<const uint8_t *>(table, 8), 4 * static_cast<uintptr_t>(k));
    return 0;
}

// Text answer = cash label, colour, the menu user's cash with thousands separators.
extern "C" U3X_FN void u3x_money_text(uintptr_t base, void *s, int32_t *ev) {
    const uint8_t *profile = g25a::menu_profile(base);
    const uint32_t cash = profile ? rd<uint32_t>(profile, g25a::PROFILE_CASH) : 0u;
    g25a::Composer c{reinterpret_cast<char *>(g25a::bytes(s) + g25a::STATE_TEXT)};
    c.text(u3h_text(base, s, g25a::TEXT_CASH_LABEL));
    c.text(g25a::cash_colour);
    c.grouped(cash);
    c.end();
    g25a::set_text(ev, c.out);
}

// The pick sound of a mod row: "picked" for an owned or buyable mod, "refused" otherwise; silent without the row.
extern "C" U3X_FN void u3x_k49_coopmod(uintptr_t base, void *s, uint32_t user, uint32_t weapon, uint8_t *mods, uint32_t n,
                                       int32_t row) {
    uint32_t bit = 0;
    if (!u3x_mod_id(mods, n, row, &bit)) return;
    u3h_k49_snd(base, u3x_click_sound(u3x_unlock_status(base, s, user, weapon, bit)));
}

// The co-op weapon list (source 0xa6).
extern "C" U3X_FN int u3x_coop_weapon_source(uintptr_t base, int32_t *ev, void *s) {
    using namespace g25a;
    const Entry entry = enter_list(base, ev, s, TAG_WEAPON_LIST);
    if (entry != Entry::go_on) return entry == Entry::answered;

    const int32_t row = ev[1];
    const uint32_t user = menu_user(base) & 3;
    int32_t &selected = *reinterpret_cast<int32_t *>(bytes(s) + STATE_WEAPON_SELECTED);
    switch (ev[0]) {
    case 2:   // the weapon's name
        if (ev[0x15] == 0) {
            const uint8_t *w = u3x_coop_weapon(s, static_cast<uint32_t>(row));
            if (w) answer_weapon_name(base, s, ev, weapon_id(w));
        }
        break;
    case 4:
        weapon_row_picture(base, s, ev, row, user);
        break;
    case 5:
        ev[1] = selected;
        break;
    case 6:
        ev[1] = u3x_coop_weapon_count(s) - 1;
        break;
    case 10:
        selected = row;
        break;
    case 12:   // cell size 256 x 128, unless the booster icons are switched off
        if (!(ext_ea()[0] & 0x20)) {
            ev[0x1f] = 0x43800000;
            ev[0x20] = 0x43000000;
        }
        break;
    case 0x11:
        ev[1] = 4;
        break;
    case 0x16:
        weapon_click(base, s, ev);
        break;
    default:
        break;
    }
    return 1;
}

// The mods list of the chosen co-op weapon (source 0xa7).
extern "C" U3X_FN int u3x_coop_mods(uintptr_t base, int32_t *ev, void *s) {
    using namespace g25a;
    const Entry entry = enter_list(base, ev, s, TAG_MODS_LIST);
    if (entry != Entry::go_on) return entry == Entry::answered;

    const uint32_t index = at<uint32_t>(base + IMG_CHOSEN_WEAPON);
    const uint8_t *w = u3x_coop_weapon(s, index);
    if (!w) {
        if (ev[0] == 6) ev[1] = -1;
        return 1;
    }

    ModsRequest q;
    q.base = base;
    q.s = s;
    q.ev = ev;
    q.index = index;
    q.weapon = weapon_id(w);
    q.count = weapon_mod_count(w);
    q.mods = weapon_mods(w);
    q.profile = menu_profile(base);
    q.user = menu_user(base) & 3;
    q.row = ev[1];
    q.bit = q.has_row(q.row) ? mod_bit(q.mods, static_cast<uint32_t>(q.row)) : 0u;

    if (ev[0] == 0x16) u3h_coop_mod_sound(base, s, q.user, q.weapon, q.mods, q.count, ev[0x17]);

    switch (ev[0]) {
    case 2:
        mods_row_text(q);
        break;
    case 4:
        mods_row_picture(q);
        break;
    case 5:
        ev[1] = at<int32_t>(base + IMG_MODS_SELECTED);
        break;
    case 6:
        ev[1] = static_cast<int32_t>(q.count - 1);
        break;
    case 9:
        mods_refresh(q);
        break;
    case 10:
        at<int32_t>(base + IMG_MODS_SELECTED) = q.row;
        break;
    case 0x16:
        mods_toggle(q);
        break;
    default:
        break;
    }
    return 1;
}
