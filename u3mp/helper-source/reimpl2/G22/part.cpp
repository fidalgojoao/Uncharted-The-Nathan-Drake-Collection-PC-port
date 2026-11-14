// ---- G22: legacy rank and XP (levels, thresholds, profile box, Records legacy picture, challenge counters, rank
// badges). Written from spec/G22_rank_xp.md; section numbers in the comments are that document's.

extern "C" {
// C part
uint8_t *u3h_lg_profile(uintptr_t base, uint32_t user);
uint8_t *u3h_l01_state(int which);
int32_t u3h_icon_index(uintptr_t base, uint32_t id);
const char *u3h_text(uintptr_t base, void *s, uint32_t id);
void u3h_set_text(int32_t *ev, const char *t);
int u3h_gate_closed(uintptr_t base, int32_t *ev);
// existing C++ functions and other groups
uint8_t *u3x_sym_table(void *s, uint32_t id);
int32_t u3x_xp_scale(int32_t xp, int32_t legacy);
int32_t u3x_level_threshold(const int32_t *table, int32_t count, int32_t level, int32_t legacy);
int32_t u3x_level_for_xp(const int32_t *table, int32_t count, int32_t xp, int32_t legacy);
int32_t u3x_xp_percent(int32_t low, int32_t high, int32_t xp);
uint32_t u3x_rank_word(int32_t level, uint32_t legacy, uint32_t badge_bits);
void u3x_legacy_back_name(char *out, char *end, int32_t legacy);
void u3x_image_answer(int32_t *ev, void *texture);
const uint8_t *u3x_challenge_entry(const uint8_t *table, int32_t number);
int u3x_n32_records(uintptr_t base);
int u3x_n32_call(uintptr_t base, int32_t *ev, void *s, void (*native)(int32_t *));
uint8_t *u3x_pm_ngentry(uintptr_t base, void *s);
int32_t u3x_pm_type(uintptr_t base, void *s);
int u3x_pm_coop(uintptr_t base, void *s);
// this group
uint8_t *u3x_lg_rec(uintptr_t base, void *s, uint8_t *p);
int u3x_lg_beta(uintptr_t base);
int32_t u3x_lg_thresh(void *s, int32_t level, int32_t legacy);
int32_t u3x_lg_from_xp(uintptr_t base, void *s, int32_t xp, int32_t legacy);
int32_t u3x_lg_cur_level(uintptr_t base, void *s, uint32_t user);
int u3x_legacy_kind(uintptr_t base, void *s, uint64_t *ev, int kind);
int u3x_lg_profile_box(uintptr_t base, void *s, int32_t *event);
int u3x_records_source(uintptr_t base, int32_t *event, void *s);
void u3x_pm_challenge(uintptr_t base, void *s, uint32_t u, uint32_t id, int32_t v, int bit);
void u3x_pm_365c5c(uintptr_t base, void *s, uint32_t u, int32_t mine, int32_t theirs);
void u3x_pm_365da8(uintptr_t base, void *s, uint32_t u, uint32_t map, int32_t a3);
uint32_t u3x_rank_bits(const uint8_t *rec, uint32_t star_old);
void u3x_rank_tag(uintptr_t base, void *s, int32_t *ev, uint32_t v);
void *u3x_rank_back(uintptr_t base, void *s, uint32_t level);
void u3x_texture_sharp(void *t);
int u3x_rank_draw(uintptr_t base, void *s, const uint64_t *r);
}

namespace g22 {

// profile object and multiplayer record (3.4, 3.5)
constexpr uintptr_t P_RECORD = 0x248, P_LOAD_STATE = 0x7ae8, P_NO_CHALLENGES = 0x7afc;
constexpr uintptr_t R_BOX_XP = 0x4fc, R_LEVEL_XP = 0x500, R_RESET_VALUE = 0x504, R_LEGACY = 0x1618;
constexpr uintptr_t R_COOP_BITS = 0x1624, R_PAW = 0x1f1c;
// game data (3.8)
constexpr uintptr_t G_MENU_USER1 = 0x17bde75, G_MENU_USER = 0x17bf714, G_LAN = 0x17d5c60, G_MP_ACTIVE = 0x17d5c61;
constexpr uintptr_t G_NET_ERROR = 0x17d5c80, G_TEXTURES = 0x1b31128, G_FORMAT_BUFFER = 0x1b676f0, G_EMPTY_TEXT = 0x13ac250;
// script symbols
constexpr uint32_t SYM_XP_TABLE = 0xc85e199du, SYM_RECORD_RESET = 0xc27df04cu, SYM_CHALLENGES = 0xb630d01cu;
// badge (3.6, 3.7)
constexpr uint32_t BADGE_MARKER = 0x4c303142u, STAR_IMAGE = 0x0242a063u, BADGE_OFF = 1u << 20;
constexpr uint32_t F_HALF = 0x3f000000u, F_64 = 0x42800000u;

U3X_RO const char back_prefix[] = "ranks/prestige-back-";
U3X_RO const char paw_name[] = "ranks/ndpaw-icon";
U3X_RO const char format_number[] = "|@Cffcece7c|%s";
U3X_RO const char format_number_label[] = "|@Cffcece7c|%s %s";

struct [[gnu::packed]] Raw32 { uint32_t v; };   // a 32-bit value at any address

using FindTexture = void *(*)(void *dictionary, const char *name);   // "native 20"
using Formatter = void (*)(const char *format, ...);
using ListSource = void (*)(int32_t *request);

U3X_FN static void clear_bytes(uint8_t *to, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) to[i] = 0;
}

U3X_FN static void copy_bytes(uint8_t *to, const uint8_t *from, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) to[i] = from[i];
}

template <typename T> [[gnu::always_inline]] inline T &field(void *p, uintptr_t off) {
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + off);
}

U3X_FN static uint32_t menu_user(uintptr_t base) {
    return at<uint8_t>(base + G_MENU_USER1) ? 1u : at<uint32_t>(base + G_MENU_USER);
}

// legacy level of a profile, through the record getter
U3X_FN static int32_t legacy_of(uintptr_t base, void *s, uint8_t *p) {
    return rd<int32_t>(u3x_lg_rec(base, s, p), R_LEGACY);
}

// XP shown in the profile box: 0 while the profile is not loaded
U3X_FN static int32_t box_xp(uintptr_t base, void *s, uint8_t *p) {
    if (rd<uint32_t>(p, P_LOAD_STATE) != 3) return 0;
    return rd<int32_t>(u3x_lg_rec(base, s, p), R_BOX_XP);
}

// threshold of a level for a profile; the legacy is read again each time
U3X_FN static int32_t threshold_of(uintptr_t base, void *s, uint8_t *p, int32_t level) {
    const int32_t legacy = legacy_of(base, s, p);
    return u3x_lg_thresh(s, level, legacy);
}

[[gnu::always_inline]] inline int32_t next_level(int32_t level) {
    return static_cast<int32_t>(static_cast<uint32_t>(level) + 1u);
}

[[gnu::always_inline]] inline int32_t wrap_sub(int32_t a, int32_t b) {
    return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
}

U3X_FN static bool same_text(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

// "ranks/prestige-back-" + one digit (level 1 .. 5); out holds at least 24 characters
U3X_FN static void back_name(char *out, uint32_t level) {
    int n = 0;
    for (; back_prefix[n]; n++) out[n] = back_prefix[n];
    out[n++] = static_cast<char>('0' + level);
    out[n] = 0;
}

U3X_FN static void *find_texture(uintptr_t base, FindTexture find, const char *name) {
    return find(reinterpret_cast<void *>(base + G_TEXTURES), name);
}

// the game's textured UI quad with the item draw's context
U3X_FN static void quad(uintptr_t base, const uint64_t *r, uint64_t texture, uint64_t vertices) {
    game_call<void>(base + 0x63dc40, r[0], r[1], texture, vertices);
}

// one more game completed on a co-op map (4.12 step 1)
U3X_FN static void count_coop_map(uint8_t *rec, uint32_t map) {
    if (map < 0x20) field<uint32_t>(rec, 0x1630 + 4 * map) += 1;
    else if (map < 0x40) field<uint32_t>(rec, 0x1cdc + 4 * (map - 0x20)) += 1;
}

// the record of a user without any test (4.11 type 0x13, 4.12 step 1)
U3X_FN static uint8_t *record_of_user(uintptr_t base, void *s, uint32_t u) {
    return u3x_lg_rec(base, s, u3h_lg_profile(base, u));
}

// the number text, formatted with the colour escape into the game's static buffer; returns that buffer
U3X_FN static const char *format_xp(uintptr_t base, const char *format, const char *number, const char *label) {
    reinterpret_cast<Formatter>(base + 0x62aa60)(format, number, label);
    return reinterpret_cast<const char *>(base + G_FORMAT_BUFFER);
}

}  // namespace g22

// 4.1: the multiplayer record of a profile; resets it first when it is loaded but the net is in error outside MP
U3X_FN uint8_t *u3x_lg_rec(uintptr_t base, void *s, uint8_t *p) {
    using namespace g22;
    uint8_t *rec = p + P_RECORD;
    if (rd<uint32_t>(p, P_LOAD_STATE) != 3 || at<uint8_t>(base + G_MP_ACTIVE) != 0 || at<uint8_t>(base + G_NET_ERROR) == 0)
        return rec;
    field<uint32_t>(rec, 0) = 0x56;
    clear_bytes(rec + 4, 0x2824);
    const uint8_t *reset = u3x_sym_table(s, SYM_RECORD_RESET);
    if (reset) field<uint32_t>(rec, R_RESET_VALUE) = rd<uint32_t>(reset, 0);
    // the eight default words, as the game's own reset stores them (3.11 a): offset from the profile, then the value
    const uint8_t *store = reinterpret_cast<const uint8_t *>(base + 0x662812);
    for (int k = 0; k < 8; k++, store += 12) {
        const int32_t offset = static_cast<int32_t>(rd<Raw32>(store, 4).v);
        field<Raw32>(p, static_cast<uintptr_t>(static_cast<intptr_t>(offset))).v = rd<Raw32>(store, 8).v;
    }
    field<uint32_t>(p, P_LOAD_STATE) = 2;
    return rec;
}

// 4.2: is the running title one of the demo / beta titles
U3X_FN int u3x_lg_beta(uintptr_t base) {
    using namespace g22;
    const char *title = reinterpret_cast<const char *>(base + 0x17b9ca0);
    for (int k = 0; k < 3; k++)
        if (same_text(title, reinterpret_cast<const char *>(base + 0x145b9a4 + 10 * k))) return 1;
    const uint32_t content = at<uint32_t>(base + 0x17b9d10);
    return content == at<Raw32>(base + 0x66265a).v || content == at<Raw32>(base + 0x662661).v;
}

// 4.3: XP threshold of a level at a legacy level
U3X_FN int32_t u3x_lg_thresh(void *s, int32_t level, int32_t legacy) {
    using namespace g22;
    const uint8_t *table = u3x_sym_table(s, SYM_XP_TABLE);
    if (!table) return 0;
    if (legacy == 4 || legacy == 5) ext_ea()[30] += 1;
    return u3x_level_threshold(rd<const int32_t *>(table, 0x70), rd<int32_t>(table, 0x68), level, legacy);
}

// 4.4: level of an XP amount at a legacy level
U3X_FN int32_t u3x_lg_from_xp(uintptr_t base, void *s, int32_t xp, int32_t legacy) {
    using namespace g22;
    uint32_t *ea = ext_ea();
    ea[29] += 1;
    ea[31] = static_cast<uint32_t>(legacy);
    const uint8_t *table = u3x_sym_table(s, SYM_XP_TABLE);
    if (!table) return 0;
    const int32_t level = u3x_level_for_xp(rd<const int32_t *>(table, 0x70), rd<int32_t>(table, 0x68), xp, legacy);
    if (level > 49 && u3x_lg_beta(base)) return 50;
    return level;
}

// 4.5: current level of a local user
U3X_FN int32_t u3x_lg_cur_level(uintptr_t base, void *s, uint32_t user) {
    using namespace g22;
    ext_ea()[28] += 1;
    const int32_t forced = at<int32_t>(base + 0x17d7abc);
    if (forced) return forced - 1;
    uint8_t *p = u3h_lg_profile(base, user);
    if (!p) return 0;
    int32_t xp = 0;
    if (rd<uint32_t>(p, P_LOAD_STATE) == 3) xp = rd<int32_t>(u3x_lg_rec(base, s, p), R_LEVEL_XP);
    const int32_t legacy = legacy_of(base, s, p);   // also for a profile that is not loaded
    return u3x_lg_from_xp(base, s, xp, legacy);
}

// 4.6: the answers of the six planted thunks
U3X_FN int u3x_legacy_kind(uintptr_t base, void *s, uint64_t *ev, int kind) {
    using namespace g22;
    if (kind == 60) {   // current level
        ev[3] = static_cast<uint32_t>(u3x_lg_cur_level(base, s, static_cast<uint32_t>(ev[1])));
        return 1;
    }
    if (kind == 61) {   // level from XP; legacy -1 = the menu user's
        int32_t legacy = static_cast<int32_t>(ev[2]);
        if (legacy == -1) {
            uint8_t *p = u3h_lg_profile(base, menu_user(base));
            legacy = p ? legacy_of(base, s, p) : 0;
        }
        ev[3] = static_cast<uint32_t>(u3x_lg_from_xp(base, s, static_cast<int32_t>(ev[1]), legacy));
        return 1;
    }
    if (kind == 62) {   // tier
        uint32_t tier = 1;
        if (at<uint8_t>(base + G_MP_ACTIVE)) {
            const uint32_t *value = game_call<const uint32_t *>(base + 0x11f2e30, uint32_t{0x6426ff70u});
            if (value) tier = *value;
            else {
                const int32_t level = u3x_lg_cur_level(base, s, 0);
                if (level < 10 && at<uint8_t>(base + 0x17c3a45)) tier = 2;
            }
        }
        ev[3] = tier;
        return 1;
    }
    if (kind == 63) {   // XP gate: enough box XP, and not below the unscaled threshold of the level
        const uint32_t user = static_cast<uint32_t>(ev[1]);
        const uint32_t amount = static_cast<uint32_t>(ev[2]);
        uint32_t open = 1;
        if (amount) {
            uint8_t *p = u3h_lg_profile(base, user);
            const uint32_t have = p ? static_cast<uint32_t>(box_xp(base, s, p)) : 0;
            if (amount > have) open = 0;
            else {
                const int32_t level = u3x_lg_cur_level(base, s, user);
                const uint8_t *table = u3x_sym_table(s, SYM_XP_TABLE);
                if (table) {
                    const int32_t last = rd<int32_t>(table, 0x68) - 1;
                    int32_t i = level < 0 ? 0 : level;
                    if (i > last) i = last;
                    if (have < static_cast<uint32_t>(rd<const int32_t *>(table, 0x70)[i])) open = 0;
                }
            }
        }
        ev[3] = open;
        return 1;
    }
    if (kind == 64) {   // rank word of a profile
        uint8_t *p = reinterpret_cast<uint8_t *>(ev[0]);
        const int32_t xp = rd<int32_t>(u3x_lg_rec(base, s, p), R_LEVEL_XP);   // no load-state test here
        const int32_t level = u3x_lg_from_xp(base, s, xp, legacy_of(base, s, p));
        // after the level, the record is taken again for each of the three reads (clarification 2)
        const uint32_t legacy = static_cast<uint32_t>(legacy_of(base, s, p));
        const uint32_t old_star = (rd<uint8_t>(u3x_lg_rec(base, s, p), R_COOP_BITS + 3) & 0x1f) == 0x1f;   // the word's top byte
        const uint32_t bits = u3x_rank_bits(u3x_lg_rec(base, s, p), old_star);
        ev[3] = u3x_rank_word(level, legacy, bits);
        ext_ea()[35] += 1;
        return 1;
    }
    if (kind == 65) {   // thresholds of a level and of the next one, for the menu user
        uint8_t *p = u3h_lg_profile(base, menu_user(base));
        const int32_t legacy = p ? legacy_of(base, s, p) : 0;
        const int32_t level = static_cast<int32_t>(ev[0]);
        ev[1] = static_cast<uint32_t>(u3x_lg_thresh(s, level, legacy));
        ev[2] = static_cast<uint32_t>(u3x_lg_thresh(s, next_level(level), legacy));
        ext_ea()[34] += 1;
        return 1;
    }
    return 0;
}

// 4.7: the profile box requests that depend on XP thresholds
U3X_FN int u3x_lg_profile_box(uintptr_t base, void *s, int32_t *event) {
    using namespace g22;
    constexpr uint32_t V_PERCENT = 0x4c4594dbu, V_BELOW = 0xdaf38e1eu, V_TOP = 0x771d7cb7u;
    constexpr uint32_t T_THIS = 0x38ece8efu, T_NEXT = 0x05134917u, T_TO_GO = 0xe607d83du;
    if (ext_ea()[1] & 1) return 0;
    const int32_t op = event[0];
    const uint32_t value_id = static_cast<uint32_t>(event[0x13]);
    const uint32_t text_id = static_cast<uint32_t>(event[1]);
    if (op == 0xb) {
        if (value_id != V_PERCENT && value_id != V_BELOW && value_id != V_TOP) return 0;
    } else if (op == 2) {
        if (event[0x15] != 0) return 0;
        if (text_id != T_THIS && text_id != T_NEXT && text_id != T_TO_GO) return 0;
    } else {
        return 0;
    }
    const uint32_t user = menu_user(base);
    uint8_t *p = u3h_lg_profile(base, user);
    if (!p) return 0;

    ext_ea()[32] += 1;
    const int32_t level = u3x_lg_cur_level(base, s, user);

    if (op == 0xb) {
        int32_t answer;
        if (value_id == V_PERCENT) {   // progress inside the level
            const int32_t low = threshold_of(base, s, p, level);
            const int32_t high = threshold_of(base, s, p, next_level(level));
            answer = u3x_xp_percent(low, high, box_xp(base, s, p));
        } else if (value_id == V_BELOW) {   // XP below the level's threshold (unsigned)
            const uint32_t low = static_cast<uint32_t>(threshold_of(base, s, p, level));
            answer = static_cast<uint32_t>(box_xp(base, s, p)) < low;
        } else {   // nothing left to the next level: threshold <= XP, both signed (clarification 1)
            const int32_t high = threshold_of(base, s, p, next_level(level));
            answer = high <= box_xp(base, s, p);
        }
        event[0xe] = answer;
        return 1;
    }

    const char *label = u3h_text(base, s, 0x9f5e40e4u);   // "XP"
    char number[0x20];
    number[0] = 0;
    const char *answer;
    if (text_id == T_THIS) {
        game_call<void>(base + 0xf32080, static_cast<char *>(number), int32_t{0x20}, threshold_of(base, s, p, level));
        answer = format_xp(base, format_number, number, label);   // the label is passed although unused
    } else if (text_id == T_NEXT) {
        game_call<void>(base + 0xf31e60, static_cast<char *>(number), int32_t{0x20}, threshold_of(base, s, p, next_level(level)));
        answer = format_xp(base, format_number_label, number, label);
    } else {
        const int32_t high = threshold_of(base, s, p, next_level(level));
        const int32_t xp = box_xp(base, s, p);
        if (high <= xp) {   // signed comparison, not the wrapped difference (clarification 1)
            answer = u3h_text(base, s, 0x235a194eu);   // the "maximum" text
        } else {
            game_call<void>(base + 0xf31e60, static_cast<char *>(number), int32_t{0x20}, wrap_sub(high, xp));
            answer = format_xp(base, format_number_label, number, label);
        }
    }
    u3h_set_text(event, answer);
    return 1;
}

// 4.8: the Records list; only the Legacy row's icon is ours
U3X_FN int u3x_records_source(uintptr_t base, int32_t *event, void *s) {
    using namespace g22;
    const uint8_t *source = rd<const uint8_t *>(event, 0x98);
    if (!source || rd<uint32_t>(source, 0x1c) != 0x4c414ec7u) return 0;
    if (u3h_gate_closed(base, event)) return 1;
    const int full = u3x_n32_records(base);
    const ListSource native = reinterpret_cast<ListSource>(base + 0x76e460);

    if (event[0] == 4 && event[0x14] == 0) {
        const bool lan = at<uint8_t>(base + G_LAN) != 0 && !full;
        const uint8_t *rows = reinterpret_cast<const uint8_t *>(base + (lan ? 0x13a65f0u : 0x13a6620u));
        const uint32_t count = lan ? 3 : 5;
        uint32_t row = static_cast<uint32_t>(event[1]);
        if (full) {   // the online rows with one extra row at 4
            if (row == 4) row = count;
            else if (row == 5) row = 4;
        }
        if (row < count && rd<uint32_t>(rows, 16 * row + 8) == 0xa0bacf62u) {
            const uint32_t user = menu_user(base);
            uint8_t *p = u3h_lg_profile(base, user);
            if (p && !(at<uint32_t>(base + 0x17f3180 + 4 * static_cast<uintptr_t>(user)) & rd<uint32_t>(rows, 16 * row + 12))) {
                const int32_t legacy = legacy_of(base, s, p);
                char name[40];
                u3x_legacy_back_name(name, name + 39, legacy);
                const FindTexture find = rd<FindTexture>(s, 928);
                if (find) {   // looked up for any legacy value
                    void *texture = find_texture(base, find, name);
                    if (texture && legacy >= 1 && legacy <= 5) {
                        u3x_image_answer(event, texture);
                        ext_ea()[33] += 1;
                    }
                }
                return 1;
            }
        }
    }
    if (full && u3x_n32_call(base, event, s, native)) return 1;
    native(event);
    return 1;
}

// 4.9: paw and co-op star bits of a record
U3X_FN uint32_t u3x_rank_bits(const uint8_t *rec, uint32_t star_old) {
    using namespace g22;
    uint32_t star = star_old & 1;
    if ((rd<uint32_t>(rec, R_COOP_BITS) & 0x1f) == 0x1f) star = 1;
    return (star << 31) | (rd<uint32_t>(rec, R_PAW) ? 1u << 30 : 0u);
}

// 4.10: one challenge counter of a user
U3X_FN void u3x_pm_challenge(uintptr_t base, void *s, uint32_t u, uint32_t id, int32_t v, int bit) {
    using namespace g22;
    ext_ea()[57] += 1;
    uint8_t *p = u3h_lg_profile(base, u);
    if (!p || rd<uint8_t>(p, P_NO_CHALLENGES)) return;
    if (id < 0x755 || id > 0x795) return;
    if (at<int32_t>(base + 0x17d5c98) == 0 && !u3x_pm_coop(base, s)) return;
    if (at<uint8_t>(base + 0x17d1a84)) return;
    const uint8_t *definitions = u3x_sym_table(s, SYM_CHALLENGES);
    if (!definitions) return;
    const uint8_t *entry = u3x_challenge_entry(definitions, static_cast<int32_t>(id - 0x755));
    if (!entry || !rd<uint32_t>(entry, 0x20)) return;

    uint32_t &counter = field<uint32_t>(u3x_lg_rec(base, s, p), 4 * static_cast<uintptr_t>(id) + 8);
    if (!bit) {
        counter += static_cast<uint32_t>(v);
    } else {   // the bit of the current map; maps 32 .. 63 have none
        const uint8_t *game = u3x_pm_ngentry(base, s);
        if (!game) return;
        const uint32_t map = rd<uint32_t>(game, 0xb8) & 0x3f;
        if (map < 32) counter |= 1u << map;
    }
    ext_ea()[57] += 0x10000;
}

// 4.11: the win challenge of the game type
U3X_FN void u3x_pm_365c5c(uintptr_t base, void *s, uint32_t u, int32_t mine, int32_t theirs) {
    using namespace g22;
    const int32_t type = u3x_pm_type(base, s);
    if (type == 2) {
        if (mine == 5 && theirs == 0) u3x_pm_challenge(base, s, u, 0x776, 1, 0);
        u3x_pm_challenge(base, s, u, 0x768, 1, 0);
    } else if (type == 0) {
        u3x_pm_challenge(base, s, u, 0x76f, 1, 0);
    } else if (type == 1) {
        u3x_pm_challenge(base, s, u, 0x77c, 1, 0);
    } else if (type == 0xf) {
        u3x_pm_challenge(base, s, u, 0x780, 1, 0);
    } else if (type == 10) {
        u3x_pm_challenge(base, s, u, 0x777, 1, 0);
    } else if (type == 0x13) {
        field<uint32_t>(record_of_user(base, s, u), 0x1d78) += 1;   // no tests at all
    }
}

// 4.12: co-op map counters and difficulty challenges
U3X_FN void u3x_pm_365da8(uintptr_t base, void *s, uint32_t u, uint32_t map, int32_t a3) {
    using namespace g22;
    if (map < 0x40) count_coop_map(record_of_user(base, s, u), map);
    const uint32_t step = at<uint32_t>(base + 0x17d1aa8);
    const int32_t type = u3x_pm_type(base, s);
    if (a3 == 0) {
        if (type == 0xb) {
            if (step >= 2) u3x_pm_challenge(base, s, u, 0x75d, 1, 0);
            if (step >= 3) u3x_pm_challenge(base, s, u, 0x769, 1, 0);
            if (step >= 4) u3x_pm_challenge(base, s, u, 0x773, 1, 0);
        } else if (type == 0x11) {
            if (step >= 4) u3x_pm_challenge(base, s, u, 0x77b, 1, 0);
            if (step >= 3) u3x_pm_challenge(base, s, u, 0x76a, 1, 0);
        } else if (type == 8) {
            if (step >= 2) u3x_pm_challenge(base, s, u, 0x75e, 1, 0);
        }
    }
    if (step == 4) {
        if (type == 0xb) u3x_pm_challenge(base, s, u, 0x77f, 1, 1);
        else if (type == 0x11) u3x_pm_challenge(base, s, u, 0x771, 1, 1);
    }
}

// 4.13: marks an image answer as a rank badge and asks for the layers' textures
U3X_FN void u3x_rank_tag(uintptr_t base, void *s, int32_t *ev, uint32_t v) {
    using namespace g22;
    if (ext_eb()[2] & BADGE_OFF) return;
    const uint32_t level = (v >> 16) & 0x3fff;
    const bool with_back = level >= 1 && level <= 5;
    const bool star = (v >> 31) != 0, paw = ((v >> 30) & 1) != 0;
    if (!with_back && !star && !paw) return;
    const FindTexture find = rd<FindTexture>(s, 928);
    if (!find) return;
    uint32_t *d = reinterpret_cast<uint32_t *>(u3h_l01_state(0));
    if (with_back) {
        char name[24];
        back_name(name, level);
        void *texture = find_texture(base, find, name);
        reinterpret_cast<void **>(u3h_l01_state(3))[level] = texture;
        if (!texture) d[11] += 1;
    }
    if (paw) {
        void *texture = find_texture(base, find, paw_name);
        *reinterpret_cast<void **>(u3h_l01_state(4)) = texture;
        if (!texture) d[11] += 1;
    }
    if (star) {
        const int32_t value = u3h_icon_index(base, STAR_IMAGE) + 1;
        *reinterpret_cast<int32_t *>(u3h_l01_state(5)) = value;
        d[10] = static_cast<uint32_t>(value);
    }
    ev[6] = static_cast<int32_t>(BADGE_MARKER);
    ev[7] = static_cast<int32_t>(v);
    d[5] += 1;
    d[12] = v;
}

// 4.13: the loaded prestige-back texture of a legacy level, or null
U3X_FN void *u3x_rank_back(uintptr_t base, void *s, uint32_t level) {
    using namespace g22;
    if (level < 1 || level > 5) return nullptr;
    const FindTexture find = rd<FindTexture>(s, 928);
    if (!find) return nullptr;
    char name[24];
    back_name(name, level);
    void *texture = find_texture(base, find, name);
    uint32_t *ea = ext_ea();
    ea[212] += 1;
    if (!texture || !rd<uint8_t>(texture, 0x10)) return nullptr;   // missing or not loaded yet
    ea[213] += 1;
    reinterpret_cast<void **>(u3h_l01_state(3))[level] = texture;
    return texture;
}

// 4.13: limits a texture's sampler field (bits 16..19) to 1
U3X_FN void u3x_texture_sharp(void *t) {
    using namespace g22;
    if (!ptr_ok(t) || !(rd<uint32_t>(t, 0) & 2)) return;
    uint8_t *m = rd<uint8_t *>(t, 8);
    if (!ptr_ok(m)) return;
    const uint32_t w = rd<uint32_t>(m, 0xc);
    if (((w >> 16) & 0xf) > 1) field<uint32_t>(m, 0xc) = (w & ~0xf0000u) | 0x10000u;
}

// 4.14: the item draw's quad; a tagged rank badge gets its back, star and paw layers
U3X_FN int u3x_rank_draw(uintptr_t base, void *s, const uint64_t *r) {
    using namespace g22;
    const uint8_t *layout = reinterpret_cast<const uint8_t *>(r[4]);
    const uint8_t *vertices = reinterpret_cast<const uint8_t *>(r[3]);
    if ((ext_eb()[2] & BADGE_OFF) || !ptr_ok(layout) || rd<uint32_t>(layout, 0) != 2 ||
        rd<uint32_t>(layout, 0x10) != BADGE_MARKER || !ptr_ok(vertices)) {
        quad(base, r, r[2], r[3]);
        return 1;
    }

    uint32_t *d = reinterpret_cast<uint32_t *>(u3h_l01_state(0));
    d[6] += 1;
    const uint32_t v = rd<uint32_t>(layout, 0x14);
    const uint32_t level = (v >> 16) & 0x3fff;
    const uint32_t x0 = rd<uint32_t>(vertices, 0), y0 = rd<uint32_t>(vertices, 4);
    const uint32_t x1 = rd<uint32_t>(vertices, 0x80), y1 = rd<uint32_t>(vertices, 0x84);
    const uint32_t xm = f_mul(f_add(x0, x1), F_HALF), ym = f_mul(f_add(y0, y1), F_HALF);

    // diagnostics: the rectangle and the head of the texture object
    uint32_t *q = reinterpret_cast<uint32_t *>(u3h_l01_state(1));
    q[0] = x0; q[1] = y0; q[2] = x1; q[3] = y1;
    const uint32_t *art = reinterpret_cast<const uint32_t *>(r[2]);
    if (ptr_ok(art)) {
        uint32_t *t = reinterpret_cast<uint32_t *>(u3h_l01_state(2));
        for (int i = 0; i < 16; i++) t[i] = art[i];
    }

    // 1. prestige back
    if (level >= 1 && level <= 5) {
        void *back = u3x_rank_back(base, s, level);
        if (back) {
            quad(base, r, reinterpret_cast<uint64_t>(back), r[3]);
            d[7] += 1;
        } else {
            ext_ea()[214] += 1;
        }
    }

    // 2. the rank art itself
    quad(base, r, r[2], r[3]);

    // 3. co-op star: an image item laid out in the upper-right quarter's size, drawn at the centre
    const int32_t star = *reinterpret_cast<const int32_t *>(u3h_l01_state(5));
    if ((v >> 31) && star > 0) {
        alignas(16) uint32_t item[12];
        alignas(16) uint8_t box[0x80];
        alignas(16) uint32_t size[2], zeros[4], position[2];
        GameCall call;
        clear_bytes(reinterpret_cast<uint8_t *>(item), sizeof item);
        clear_bytes(box, sizeof box);
        clear_bytes(reinterpret_cast<uint8_t *>(zeros), sizeof zeros);
        item[0] = 3;
        item[5] = STAR_IMAGE;
        item[6] = 1;
        item[7] = static_cast<uint32_t>(star - 1);
        item[9] = 0xffffffffu;
        item[10] = F_ONE;
        size[0] = f_sub(x1, xm);
        size[1] = f_sub(y1, ym);
        position[0] = xm;
        position[1] = ym;

        clear_bytes(reinterpret_cast<uint8_t *>(&call), sizeof call);
        call.fn = base + 0x640ac0;
        call.i[0] = reinterpret_cast<uint64_t>(box);
        call.i[1] = reinterpret_cast<uint64_t>(item);
        call.i[2] = base + G_EMPTY_TEXT;
        call.i[3] = reinterpret_cast<uint64_t>(size);
        call.i[4] = reinterpret_cast<uint64_t>(zeros);
        call.i[5] = 0xffffffffu;
        call.x[0] = F_64;
        call.al = 1;
        u3x_k26_call(&call);

        if (rd<uint64_t>(box, 0x48) && rd<uint32_t>(box, 0x3c)) {
            const uint32_t flag = r[6] ? rd<uint32_t>(reinterpret_cast<const void *>(r[6] - 0x2080), 0) : 0;
            clear_bytes(reinterpret_cast<uint8_t *>(&call), sizeof call);
            call.fn = base + 0x641560;
            call.i[0] = reinterpret_cast<uint64_t>(box);
            call.i[1] = r[1];
            call.i[2] = r[0];
            call.i[3] = reinterpret_cast<uint64_t>(position);
            call.i[4] = static_cast<uint32_t>(r[5]);
            call.i[5] = flag;
            call.x[0] = 0;
            call.al = 1;
            u3x_k26_call(&call);
            d[8] += 1;
        } else {
            d[13] += 1;
        }
    }

    // 4. ND paw in the lower-left quarter, on a copy of the vertex block
    const uint8_t *paw = *reinterpret_cast<uint8_t **>(u3h_l01_state(4));
    if (((v >> 30) & 1) && paw && rd<uint8_t>(paw, 0x10)) {
        alignas(16) uint8_t quarter[0x110];
        copy_bytes(quarter, vertices, sizeof quarter);
        field<uint32_t>(quarter, 0x00) = x0; field<uint32_t>(quarter, 0x04) = ym;
        field<uint32_t>(quarter, 0x40) = xm; field<uint32_t>(quarter, 0x44) = ym;
        field<uint32_t>(quarter, 0x80) = xm; field<uint32_t>(quarter, 0x84) = y1;
        field<uint32_t>(quarter, 0xc0) = x0; field<uint32_t>(quarter, 0xc4) = y1;
        quad(base, r, reinterpret_cast<uint64_t>(paw), reinterpret_cast<uint64_t>(quarter));
        d[9] += 1;
    }
    return 1;
}
