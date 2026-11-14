// G38 part A: the custom game's map card, the pause header, the last-game title and the co-op type test.
// Written from spec/G38_custom_settings.md (sections 3, 4 and 7).

extern "C" {
// C part, other groups and first-batch functions (not written here)
int u3h_gate_closed(uintptr_t base, int32_t *ev);
const char *u3h_text(uintptr_t base, void *s, uint32_t id);
uint8_t *u3h_nmh(uintptr_t base);
uint8_t *u3h_netgame(uintptr_t base, void *s);
int32_t u3h_netgame_type(uintptr_t base, void *s);
int u3h_net_coop_mode(uintptr_t base, void *s);
uint8_t *u3x_map_entry_cur(uintptr_t base, void *s);
uint8_t *u3x_sym_table(void *s, uint32_t id);
void u3x_image_answer(int32_t *ev, void *texture);
int u3x_map_name_id(const int32_t *maps, uint32_t map, uint32_t *text_id);
const uint32_t *u3x_difficulty_texts(uintptr_t base);
int u3x_last_game_third(int32_t game_type, uint32_t subtitle, uint32_t difficulty, const uint32_t *difficulty_texts,
                        uint32_t *text_id);
void u3x_join_dash(char *out, char *end, const char *a, const char *b, const char *c);
void u3x_two_lines(char *out, char *end, const char *first, const char *second);

// this part
int u3x_type_is_coop(uintptr_t base, int32_t type);
uint8_t *u3x_custom_map(uintptr_t base, void *s, int32_t *gametype);
int u3x_custom_load_source(uintptr_t base, int32_t *ev, void *s);
int u3x_pause_info_source(uintptr_t base, int32_t *ev, void *s);
int u3x_last_game_source(uintptr_t base, int32_t *ev, void *s);
}

namespace g38a {

// source tags
constexpr uint32_t TAG_MAP_CARD = 0x4c414e31u;
constexpr uint32_t TAG_PAUSE = 0x4c414ec2u;
constexpr uint32_t TAG_LAST_GAME = 0x4c414e6eu;

// slots of the two cards
constexpr uint32_t SLOT_MAP = 0x16094dd2u;
constexpr uint32_t SLOT_GAME_TYPE = 0xa42c91c5u;
constexpr uint32_t SLOT_DIFFICULTY = 0xaceaf52du;
constexpr uint32_t SLOT_DIFFICULTY_ALT = 0xacebf52du;   // accepted as well (spec 7.1)

// script symbols
constexpr uint32_t SYM_NET_GAMES = 0x3b9a067du;
constexpr uint32_t SYM_RULE_SETS = 0xf985c7b6u;
constexpr uint32_t SYM_MAPS = 0x0dec97a4u;

// image offsets
constexpr uintptr_t IMG_MODE = 0x17d603c;         // u32, 2 = a custom game is being set up
constexpr uintptr_t IMG_SEL = 0x17d167c;          // s16, the selected net game
constexpr uintptr_t IMG_DIFFICULTY = 0x17d1aa8;   // u8, co-op difficulty
constexpr uintptr_t IMG_TEXTURES = 0x1b31128;     // texture dictionary
constexpr uintptr_t IMG_COOP_MASK = 0x721100;     // u32 (unaligned), the game's co-op game type mask

constexpr uintptr_t STATE_TEXT = 0x2c80;          // the shared text buffer of the state object
constexpr uint32_t GAME_TYPE_NO_DIFFICULTY = 0x12;
constexpr uint32_t SCALE_LOADING = 0x40100000u;   // 2.25

U3X_RO const char empty_text[] = {0};

// request fields
enum : int { W_OP = 0, W_INDEX = 1, W_SCALE = 12, W_VALUE = 0xe, W_QUERY = 0x13, W_PICTURE = 0x14, W_TEXT_INDEX = 0x15 };

U3X_FN void set_text(int32_t *ev, const char *text) {
    *reinterpret_cast<const char **>(reinterpret_cast<uint8_t *>(ev) + 0x40) = text;
}

// 1 when the request's source descriptor carries this tag
U3X_FN int has_tag(const int32_t *ev, uint32_t tag) {
    const uint8_t *descriptor = rd<const uint8_t *>(ev, 0x98);
    return descriptor && rd<uint32_t>(descriptor, 0x1c) == tag;
}

U3X_FN uint32_t *sw() { return reinterpret_cast<uint32_t *>(u3h_ext() + 0x2130); }

template <typename F> [[gnu::always_inline]] inline F native(void *s, int k) { return rd<F>(s, 768 + 8 * static_cast<uintptr_t>(k)); }

U3X_FN uint32_t difficulty(uintptr_t base) { return at<uint8_t>(base + IMG_DIFFICULTY); }

// the name of co-op difficulty d; the empty text from 5 on
U3X_FN const char *difficulty_text(uintptr_t base, void *s, uint32_t d) {
    if (d >= 5) return empty_text;
    return u3h_text(base, s, u3x_difficulty_texts(base)[d]);
}

// the name of game type gt from the rule sets, or null when they do not hold it
U3X_FN const char *game_type_text(uintptr_t base, void *s, int32_t gt) {
    const uint8_t *rules = u3x_sym_table(s, SYM_RULE_SETS);
    if (!rules || gt < 0 || static_cast<uint32_t>(gt) >= rd<uint32_t>(rules, 0)) return nullptr;
    const uint8_t *entry = rd<const uint8_t *>(rules, 8) + static_cast<uintptr_t>(gt) * 0x58;
    return u3h_text(base, s, rd<uint32_t>(entry, 0));
}

// the preview texture of a map entry, or null (a miss queues the load in the game)
U3X_FN void *map_texture(uintptr_t base, void *s, const uint8_t *map) {
    const char *name = rd<const char *>(map, 0x10);
    const auto find = native<void *(*)(uintptr_t, const char *)>(s, 20);
    return find && name ? find(base + IMG_TEXTURES, name) : nullptr;
}

// appends a text to the card's buffer at position n, 250 characters at most in all; returns the new position
U3X_FN int append_text(char *buffer, int n, const char *text) {
    while (*text && n < 250) buffer[n++] = *text++;
    return n;
}

}  // namespace g38a

// 1 when the game counts this game type as a co-op type (the mask is the game's own)
U3X_FN int u3x_type_is_coop(uintptr_t base, int32_t type) {
    const uint32_t d = static_cast<uint32_t>(type) - 4;
    if (d > 14) return 0;
    uint32_t mask;
    __builtin_memcpy(&mask, reinterpret_cast<const void *>(base + g38a::IMG_COOP_MASK), 4);
    return (mask >> d) & 1;
}

// the map entry of the map chosen for the custom game; *gametype = the chosen game's type
U3X_FN uint8_t *u3x_custom_map(uintptr_t base, void *s, int32_t *gametype) {
    using namespace g38a;
    if (at<uint32_t>(base + IMG_MODE) != 2) return nullptr;
    if (!u3x_sym_table(s, SYM_NET_GAMES)) return nullptr;   // never fill the menu table before the net games are there

    uint8_t *const menu = u3h_nmh(base);
    uint32_t games = rd<uint8_t>(menu, 0);
    if (!games) {
        if (const auto fill = native<void (*)(uint8_t *)>(s, 1)) fill(menu);
        games = rd<uint8_t>(menu, 0);
        if (!games) return nullptr;
    }

    // the game of the selected net game's type; game 0 when there is none
    uint32_t game = 0;
    const uint8_t *net = u3x_sym_table(s, SYM_NET_GAMES);
    const int32_t sel = at<int16_t>(base + IMG_SEL);
    if (net && sel >= 0 && static_cast<uint32_t>(sel) < rd<uint32_t>(net, 0)) {
        const uint32_t type = rd<uint32_t>(rd<const uint8_t *>(net, 8) + static_cast<uintptr_t>(sel) * 0xd0, 0xc);
        for (uint32_t i = 0; i < games; i++) {
            if (rd<uint32_t>(menu, 8 + i * 0x308) == type) {
                game = i;
                break;
            }
        }
    }

    int32_t map = 0;
    if (const auto chosen = native<int32_t (*)(uint8_t *)>(s, 44)) map = chosen(menu);
    if (map < 0) map = 0;

    uint8_t *const entry = menu + 8 + game * 0x308;
    if (gametype) *gametype = rd<int32_t>(entry, 0);
    return rd<uint8_t *>(entry, 8 + static_cast<uintptr_t>(map) * 0x18);
}

// the custom game's map card
U3X_FN int u3x_custom_load_source(uintptr_t base, int32_t *ev, void *s) {
    using namespace g38a;
    if (!has_tag(ev, TAG_MAP_CARD)) return 0;
    if (u3h_gate_closed(base, ev)) return 1;
    if (at<uint32_t>(base + IMG_MODE) != 2) {
        set_text(ev, empty_text);
        ev[W_INDEX] = 0;
        return 1;
    }
    int32_t gt = 0;
    const uint8_t *map = u3x_custom_map(base, s, &gt);
    if (!map) return 1;

    const int32_t op = ev[W_OP];
    if (op == 4) {
        if (static_cast<uint32_t>(ev[W_PICTURE]) != SLOT_MAP) return 1;
        void *texture = map_texture(base, s, map);
        u3x_image_answer(ev, texture);
        if (!texture) ev[W_SCALE] = static_cast<int32_t>(SCALE_LOADING);   // the "loading" picture is drawn larger here
    } else if (op == 11) {
        const uint32_t query = static_cast<uint32_t>(ev[W_QUERY]);
        if (query == 0x17809498u || query == 0xbedebbb3u || query == 0x7754e7afu || query == 0x7bc431fbu) ev[W_VALUE] = 0;
    } else if (op == 2 && ev[W_TEXT_INDEX] == 0) {
        const uint32_t slot = static_cast<uint32_t>(ev[W_INDEX]);
        uint32_t *const d = sw();
        if (slot == SLOT_MAP) {
            const uint32_t subtitle_id = rd<uint32_t>(map, 8);
            const char *name = u3h_text(base, s, rd<uint32_t>(map, 4));
            if (!subtitle_id) {
                set_text(ev, name);
            } else {
                // name, backslash, subtitle: the name is in the buffer before the subtitle is looked up
                char *const buffer = reinterpret_cast<char *>(s) + STATE_TEXT;
                int n = append_text(buffer, 0, name);
                if (n < 250) buffer[n++] = '\\';
                n = append_text(buffer, n, u3h_text(base, s, rd<uint32_t>(map, 8)));   // the id is read again
                buffer[n] = 0;
                set_text(ev, buffer);
            }
        } else if (slot == SLOT_GAME_TYPE) {
            if (const char *text = game_type_text(base, s, gt)) set_text(ev, text);
        } else if (slot == SLOT_DIFFICULTY || slot == SLOT_DIFFICULTY_ALT) {
            d[30] = static_cast<uint32_t>(gt);
            if (!u3x_type_is_coop(base, gt) || static_cast<uint32_t>(gt) == GAME_TYPE_NO_DIFFICULTY) return 1;
            const uint32_t level = difficulty(base);
            d[29] = level;
            set_text(ev, difficulty_text(base, s, level));
            d[28]++;
        } else {
            d[24 + (d[31] & 3)] = slot;   // remember the last four slots the card does not know
            d[31]++;
        }
    }
    return 1;
}

// the pause header: the same card for the map being played
U3X_FN int u3x_pause_info_source(uintptr_t base, int32_t *ev, void *s) {
    using namespace g38a;
    if (!has_tag(ev, TAG_PAUSE)) return 0;
    if (u3h_gate_closed(base, ev)) return 1;
    uint32_t *const d = sw();
    d[0]++;
    const int32_t gt = u3h_netgame_type(base, s);
    const uint8_t *map = u3x_map_entry_cur(base, s);
    if (!map) {
        d[5]++;
        return 1;
    }

    const int32_t op = ev[W_OP];
    if (op == 4) {
        if (static_cast<uint32_t>(ev[W_PICTURE]) != SLOT_MAP) return 1;
        u3x_image_answer(ev, map_texture(base, s, map));
        d[1]++;
    } else if (op == 11) {
        if (static_cast<uint32_t>(ev[W_QUERY]) == 0x17809498u) ev[W_VALUE] = 0;
    } else if (op == 2 && ev[W_TEXT_INDEX] == 0) {
        const uint32_t slot = static_cast<uint32_t>(ev[W_INDEX]);
        if (slot == SLOT_DIFFICULTY || slot == SLOT_DIFFICULTY_ALT) {
            if (!u3h_net_coop_mode(base, s) || static_cast<uint32_t>(gt) == GAME_TYPE_NO_DIFFICULTY) return 1;
            set_text(ev, difficulty_text(base, s, difficulty(base)));
            d[4]++;
        } else if (slot == SLOT_MAP) {
            const uint32_t subtitle_id = rd<uint32_t>(map, 8);
            const char *name = u3h_text(base, s, rd<uint32_t>(map, 4));   // the name is looked up first
            if (!subtitle_id) {
                set_text(ev, name);
            } else {
                const char *subtitle = u3h_text(base, s, rd<uint32_t>(map, 8));   // the id is read again
                char *const buffer = reinterpret_cast<char *>(s) + STATE_TEXT;
                u3x_two_lines(buffer, buffer + 250, name, subtitle);
                set_text(ev, buffer);
            }
            d[2]++;
        } else if (slot == SLOT_GAME_TYPE) {
            if (const char *text = game_type_text(base, s, gt)) {
                set_text(ev, text);
                d[3]++;
            }
        }
    }
    return 1;
}

// the last-game title: "map - game type - third part"
U3X_FN int u3x_last_game_source(uintptr_t base, int32_t *ev, void *s) {
    using namespace g38a;
    if (!has_tag(ev, TAG_LAST_GAME)) return 0;
    if (u3h_gate_closed(base, ev)) return 1;
    if (ev[W_OP] != 1) return 1;
    uint32_t *const d = sw();
    const uint8_t *game = u3h_netgame(base, s);
    if (!game) {
        d[7]++;
        return 1;
    }
    const uint32_t map_id = rd<uint32_t>(game, 4);
    const uint32_t subtitle_id = rd<uint32_t>(game, 8);
    const int32_t gt = rd<int32_t>(game, 0xc);
    uint32_t id = 0;

    // the map's name, when the map table knows the map
    const auto symbol = native<const int32_t *(*)(uint32_t)>(s, 13);
    const int32_t *maps = symbol ? symbol(SYM_MAPS) : nullptr;
    const char *map_part = u3x_map_name_id(maps, map_id, &id) == 1 ? u3h_text(base, s, id) : nullptr;

    // the game type: always a part, empty when unknown
    const char *type_part = game_type_text(base, s, gt);
    if (!type_part) type_part = empty_text;

    // difficulty or subtitle
    const char *third_part = nullptr;
    const int kind = u3x_last_game_third(gt, subtitle_id, difficulty(base), u3x_difficulty_texts(base), &id);
    if (kind == 1) third_part = u3h_text(base, s, id);
    else if (kind == 2) third_part = empty_text;

    char *const buffer = reinterpret_cast<char *>(s) + STATE_TEXT;
    u3x_join_dash(buffer, buffer + 0x7f, map_part, type_part, third_part);
    set_text(ev, buffer);
    d[6]++;
    return 1;
}
