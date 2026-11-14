// G23 part B: booster row icons, level disc, pick sounds, weapon stats bars, unlock status.
// Written from spec/G23_boosters.md (sections 3 and 4.0, 4.2 to 4.7).

extern "C" {
// part A of this group
uint8_t *u3x_booster_def(uint8_t *list, int32_t index);
int u3x_booster_unlocked(void *s, uint32_t user, uint32_t id);
// C part
int u3h_gate_closed(uintptr_t base, int32_t *ev);
int32_t u3h_icon_index(uintptr_t base, uint32_t id);
void u3h_set_icon(uintptr_t base, int32_t *ev, uint32_t id);
void u3h_k49_snd(uintptr_t base, int n);
// other groups
void u3x_icon_answer(int32_t *ev, int32_t kind, int32_t a, int32_t b, int32_t c, int32_t index);
uint32_t u3x_unlock_icon(int status);
int u3x_click_sound(int unlock_status);
int u3x_booster_already(uint32_t slot_word, uint32_t player_word, uint32_t booster);
uint32_t u3x_booster_level_mark(int level);
int u3x_comp_weapon_answer(uint32_t query, uint32_t weapon, int locked, int32_t *answer);
uint8_t *u3x_sym_table(void *s, uint32_t id);
uint8_t *u3x_kickback_nth(void *s, int32_t n, int coop);
// this part
void u3x_booster_icons(uintptr_t base, void *s, int32_t *ev, uint8_t *list, uint32_t slot, uint32_t user,
                       int32_t loadout, uint8_t *profile);
int u3x_k46_op4(uintptr_t base, void *s, int32_t *ev, uint8_t *list, uint32_t slot, uint32_t user, int32_t loadout,
                uint8_t *profile, uint32_t *counters);
int u3x_k46_draw(uintptr_t base, const uint64_t *r, uint32_t *counters);
uint32_t u3x_booster_level_icon(uint32_t level);
void u3x_k49_booster(uintptr_t base, void *s, uint8_t *list, uint32_t slot, uint32_t user, int32_t row);
void u3x_k49_kickback(uintptr_t base, void *s, uint32_t user, int coop, int32_t row);
int u3x_comp_weapon_source(uintptr_t base, int32_t *ev, void *s);
int u3x_unlock_status(uintptr_t base, void *s, uint32_t user, uint32_t id, uint32_t mod);
}

namespace g23b {

// request words
constexpr int W_ROW = 1, W_TYPE = 2, W_MARK = 4, W_ICON = 7, W_VALUE = 0xe, W_VALUE_ID = 0x13, W_PICTURE = 0x14;

constexpr uint32_t PICTURE_ICON = 0xf5a294f3u;     // image slot: the booster's own icon (0 = the status picture)
constexpr uint32_t ICON_EQUIPPED = 0x08208d0au;    // UI icon: the "equipped" check
constexpr uint32_t ICON_FALLBACK = 0xd8b04236u;    // UI icon for a booster whose own icon is not in the UI table
constexpr uint32_t MARK_TAG = 0x2e4cu;             // upper half of a level mark
constexpr uint32_t TAG_WEAPONS = 0x4c414ea2u;      // source tag of the competitive weapon list
constexpr uint32_t SYM_WEAPONS_1 = 0xa94f33c0u, SYM_WEAPONS_2 = 0xe77d8fbau;
constexpr uint32_t VALUE_LOCKED = 0xa1443de7u;

// switches
constexpr uint32_t EA0_ICONS_OFF = 0x20u, EB1_NO_LEVEL = 0x1000u;

// image offsets
constexpr uintptr_t IMG_PAID = 0x17c3b40, IMG_OVERRIDE = 0x17c3b48, IMG_UNLOCKS = 0x17f3068;
constexpr uintptr_t IMG_TEXTURE_OF = 0x11b89b0, IMG_ITEM_DRAW = 0x641560, IMG_WEAPON_SOURCE = 0x766870;

// the texture name of a level disc is head + one digit + tail
U3X_RO const char disc_head[] = "general/net/boosters-";
U3X_RO const char disc_tail[] = "/background";

// "native k" of the state object (0 when the helper did not find it)
[[gnu::always_inline]] inline uintptr_t native(const void *s, int k) { return rd<uintptr_t>(s, 768 + 8 * k); }

template <typename T> [[gnu::always_inline]] inline void put(void *p, uintptr_t off, T v) {
    __builtin_memcpy(static_cast<uint8_t *>(p) + off, &v, sizeof v);
}

// the paid-slot booster word of a user (the game keeps two)
U3X_FN uint32_t paid_booster(uintptr_t base, uint32_t user) { return at<uint32_t>(base + IMG_PAID + 4 * (user & 1)); }

U3X_FN uint32_t menu_user(uintptr_t base) {
    return at<uint8_t>(base + 0x17bde75) ? 1u : at<uint32_t>(base + 0x17bf714);
}

// the booster's level for the user (native 23); 0 without the native
U3X_FN int32_t booster_level(uintptr_t base, void *s, uint32_t user, uint8_t *def) {
    const uintptr_t fn = native(s, 23);
    return fn ? game_call<int32_t>(fn, base + IMG_PAID, user, def) : 0;
}

// the unlock status of an item as the game returns it (natives 24 and 35); false when it cannot be asked
U3X_FN bool ask_status(uintptr_t base, void *s, uint32_t user, uint32_t id, uint32_t mod, int32_t *status) {
    const uintptr_t find = native(s, 24), ask = native(s, 35);
    if (!find || !ask) return false;
    void *entry = game_call<void *>(find, base + IMG_UNLOCKS, static_cast<int32_t>(id), static_cast<int32_t>(mod));
    *status = game_call<int32_t>(ask, base + IMG_UNLOCKS, user, entry);
    return true;
}

// string id of a name, continued over `count` more characters: CRC-32 with polynomial 0x04C11DB7, most significant
// bit first, start value 0, nothing inverted
U3X_FN uint32_t sid_more(uint32_t crc, const char *text, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        crc ^= static_cast<uint32_t>(static_cast<uint8_t>(text[i])) << 24;
        for (int bit = 0; bit < 8; ++bit) crc = (crc << 1) ^ ((crc >> 31) ? 0x04c11db7u : 0u);
    }
    return crc;
}

// image slot "icon": the booster's own icon, or the fallback icon when the UI table does not have it
U3X_FN void own_icon(uintptr_t base, void *s, int32_t *ev, uint32_t user, uint8_t *def) {
    booster_level(base, s, user, def);   // asked, not used
    ext_ea()[20] += 1;
    const uint32_t icon = rd<uint32_t>(def, 0x28);
    u3h_set_icon(base, ev, u3h_icon_index(base, icon) >= 0 ? icon : ICON_FALLBACK);
}

// the booster shown as equipped in a slot (0 = none)
U3X_FN uint32_t worn_booster(uintptr_t base, void *s, uint32_t slot, uint32_t user, int32_t loadout, uint8_t *profile) {
    const uint32_t forced = at<uint32_t>(base + IMG_OVERRIDE + 4 * (slot & 3));
    if (forced) return forced;
    if (slot <= 2) {
        const uintptr_t fn = native(s, 34);
        return profile && fn ? game_call<uint32_t>(fn, profile, static_cast<int32_t>(slot + 4), loadout) : 0u;
    }
    return slot == 3 ? paid_booster(base, user) : 0u;
}

// image slot 0: the "equipped" check, else the "new" marker, else the lock-status icon
U3X_FN void status_picture(uintptr_t base, void *s, int32_t *ev, uint32_t slot, uint32_t user, int32_t loadout,
                           uint8_t *profile, uint8_t *def) {
    const uint32_t id = rd<uint32_t>(def, 4);
    const uint32_t worn = worn_booster(base, s, slot, user, loadout, profile);
    if (worn && worn == id) {
        u3h_set_icon(base, ev, ICON_EQUIPPED);
        ext_ea()[21] += 1;
        return;
    }
    const uintptr_t is_new = native(s, 36);
    if (is_new && (game_call<uint32_t>(is_new, base + IMG_UNLOCKS, user, id, 0u) & 0xff)) {
        u3x_icon_answer(ev, 0x2f, 0, 0, 0, -1);
        ext_ea()[22] += 1;
        return;
    }
    int32_t status;
    if (!ask_status(base, s, user, id, 0, &status)) return;
    const uint32_t icon = u3x_unlock_icon(status);   // the full status value, not only its low byte
    if (!icon) return;
    u3h_set_icon(base, ev, icon);
    ext_ea()[23] += 1;
}

// sound of a click on an item with mods-less unlock status
U3X_FN void status_sound(uintptr_t base, void *s, uint32_t user, uint32_t id) {
    u3h_k49_snd(base, u3x_click_sound(u3x_unlock_status(base, s, user, id, 0)));
}

}  // namespace g23b

extern "C" {

// ---- 4.0 unlock status of an item: 0 owned, 1 can be bought, others locked; 3 when it cannot ask
U3X_FN int u3x_unlock_status(uintptr_t base, void *s, uint32_t user, uint32_t id, uint32_t mod) {
    int32_t status;
    if (!g23b::ask_status(base, s, user, id, mod, &status)) return 3;
    return status & 0xff;
}

// ---- 4.2 the picture of a booster row
U3X_FN void u3x_booster_icons(uintptr_t base, void *s, int32_t *ev, uint8_t *list, uint32_t slot, uint32_t user,
                              int32_t loadout, uint8_t *profile) {
    uint8_t *def = u3x_booster_def(list, ev[g23b::W_ROW]);
    if (!def) return;
    const uint32_t picture = static_cast<uint32_t>(ev[g23b::W_PICTURE]);
    if (picture == g23b::PICTURE_ICON) g23b::own_icon(base, s, ev, user, def);
    else if (picture == 0) g23b::status_picture(base, s, ev, slot, user, loadout, profile, def);
}

// ---- 4.3 the picture of a row, with the level mark on a booster icon
U3X_FN int u3x_k46_op4(uintptr_t base, void *s, int32_t *ev, uint8_t *list, uint32_t slot, uint32_t user,
                       int32_t loadout, uint8_t *profile, uint32_t *counters) {
    if (ext_ea()[0] & g23b::EA0_ICONS_OFF) return 0;
    u3x_booster_icons(base, s, ev, list, slot, user, loadout, profile);
    if (ext_eb()[1] & g23b::EB1_NO_LEVEL) return 1;
    if (static_cast<uint32_t>(ev[g23b::W_PICTURE]) != g23b::PICTURE_ICON) return 1;
    if (ev[g23b::W_TYPE] != 3) return 1;   // no plain icon was answered
    uint8_t *def = u3x_booster_def(list, ev[g23b::W_ROW]);
    if (!def) return 1;
    int32_t level = 0;
    if (static_cast<uint32_t>(ev[g23b::W_ICON]) != g23b::ICON_FALLBACK) {
        level = g23b::booster_level(base, s, user, def);
        if (level < 0) level = 0;
    }
    ev[g23b::W_MARK] = static_cast<int32_t>(u3x_booster_level_mark(level));
    counters[1] += 1;
    return 1;
}

// ---- 4.4 the UI icon of the disc of a booster level (0 = no disc): the string id of its texture name
U3X_FN uint32_t u3x_booster_level_icon(uint32_t level) {
    if (level > 2) return 0;
    const char digit = static_cast<char>('0' + level);
    uint32_t id = g23b::sid_more(0, g23b::disc_head, sizeof g23b::disc_head - 1);
    id = g23b::sid_more(id, &digit, 1);
    return g23b::sid_more(id, g23b::disc_tail, sizeof g23b::disc_tail - 1);
}

// ---- 4.5 called at the entry of every UI item draw: draws the level disc under a marked booster icon.
// Integer code only up to the nested draw, so the float argument the game passed is still in its register there.
U3X_FN int u3x_k46_draw(uintptr_t base, const uint64_t *r, uint32_t *counters) {
    if (ext_eb()[1] & g23b::EB1_NO_LEVEL) return 0;
    const uint8_t *layout = reinterpret_cast<const uint8_t *>(r[9]);
    if (!ptr_ok(layout) || rd<uint32_t>(layout, 0) != 3) return 0;
    const uint32_t mark = rd<uint32_t>(layout, 8);
    if ((mark >> 16) != g23b::MARK_TAG) return 0;

    const uint32_t icon = u3x_booster_level_icon(mark & 0xff);
    if (!icon) return 0;
    const int32_t index = u3h_icon_index(base, icon);
    if (index < 0) { counters[2] += 1; return 0; }
    void *texture = game_call<void *>(base + g23b::IMG_TEXTURE_OF, (1ull << 32) | icon, index);
    if (!texture) { counters[3] += 1; return 0; }

    // the disc item: the icon's layout without the mark, showing the disc icon (on the stack: the nested draw
    // comes back in here)
    uint64_t disc[0x70 / 8];
    for (int i = 0; i < 0x70 / 8; ++i) disc[i] = rd<uint64_t>(layout, 8 * i);
    g23b::put<uint32_t>(disc, 8, 0);
    g23b::put<uint32_t>(disc, 0x14, icon);
    g23b::put<uint32_t>(disc, 0x18, 1);
    g23b::put<int32_t>(disc, 0x1c, index);
    g23b::put<void *>(disc, 0x48, texture);
    game_call<void>(base + g23b::IMG_ITEM_DRAW, static_cast<void *>(disc), r[10], r[11], r[12], r[8], r[7]);
    counters[0] += 1;
    return 0;
}

// ---- 4.6 the sound of a click on a booster row
U3X_FN void u3x_k49_booster(uintptr_t base, void *s, uint8_t *list, uint32_t slot, uint32_t user, int32_t row) {
    const uint8_t *def = u3x_booster_def(list, row);
    if (!def) return;
    const uint32_t id = rd<uint32_t>(def, 4);
    if (slot != 3) { g23b::status_sound(base, s, user, id); return; }
    // the paid slot: silent when the booster is already there or is locked
    if (u3x_booster_already(at<uint32_t>(base + g23b::IMG_OVERRIDE + 4 * 3), g23b::paid_booster(base, user), id)) return;
    if (!u3x_booster_unlocked(s, user, id)) return;
    u3h_k49_snd(base, 0);
}

// ---- 4.6 the sound of a click on a kickback row (row 0 = "None")
U3X_FN void u3x_k49_kickback(uintptr_t base, void *s, uint32_t user, int coop, int32_t row) {
    if (row <= 0) { u3h_k49_snd(base, 0); return; }
    const uint8_t *entry = u3x_kickback_nth(s, row - 1, coop);
    if (entry) g23b::status_sound(base, s, user, rd<uint32_t>(entry, 0));
}

// ---- 4.7 the competitive weapon list: the weapon and the locked flag of a row; the rest is the game's own source
U3X_FN int u3x_comp_weapon_source(uintptr_t base, int32_t *ev, void *s) {
    const uint8_t *source = rd<const uint8_t *>(ev, 0x98);
    if (!source || rd<uint32_t>(source, 0x1c) != g23b::TAG_WEAPONS) return 0;
    if (u3h_gate_closed(base, ev)) return 1;
    if (ev[0] != 11) {
        game_call<void>(base + g23b::IMG_WEAPON_SOURCE, ev);
        return 1;
    }
    g23b::put<uint64_t>(s, 760, base);
    const uint32_t row = static_cast<uint32_t>(ev[g23b::W_ROW]);
    if (row >= at<uint32_t>(base + 0x1b6a6b0)) return 1;
    const uint8_t *table = u3x_sym_table(s, at<uint8_t>(base + 0x17d9994) ? g23b::SYM_WEAPONS_2 : g23b::SYM_WEAPONS_1);
    if (!table) return 1;
    const uint8_t *entries = rd<const uint8_t *>(table, 8);
    if (!entries) return 1;
    const uint32_t weapon = rd<uint32_t>(entries, 0x28 * static_cast<uintptr_t>(at<uint8_t>(base + 0x1b6a630 + row)));
    const uint32_t user = g23b::menu_user(base) & 3;
    const uint32_t query = static_cast<uint32_t>(ev[g23b::W_VALUE_ID]);
    const int locked = query == g23b::VALUE_LOCKED && !u3x_booster_unlocked(s, user, weapon);
    if (u3x_comp_weapon_answer(query, weapon, locked, &ev[g23b::W_VALUE])) ext_ea()[114] += 1;
    return 1;
}

}  // extern "C"
