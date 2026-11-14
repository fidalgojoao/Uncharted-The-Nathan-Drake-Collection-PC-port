// G38 part B: Advanced Settings rows, the Time Limit row of Custom Game > Settings, the EULA pager.
// Written from spec G38_custom_settings.md (sections 2, 3, 5, 7).

extern "C" {
// C part, other groups, part A and the first-batch functions (declared, not written here)
int u3h_gate_closed(uintptr_t base, int32_t *ev);
const char *u3h_text(uintptr_t base, void *s, uint32_t id);
uint8_t *u3h_custom_map(uintptr_t base, void *s, int32_t *gametype);
uint8_t *u3x_sym_table(void *s, uint32_t id);
void u3x_image_answer(int32_t *ev, void *texture);
int u3x_type_is_coop(uintptr_t base, int32_t type);
void u3x_time_limit_text(char *out, char *end, const uint8_t *rule, int32_t count, const char *label);
int u3x_has_digit(const char *text);
uint32_t u3x_retag_widget(uint8_t *page, uint32_t widget, uint32_t source, uint32_t tag, uint32_t max_items);
int u3x_pager_answer(int32_t *ev, int32_t *current, int32_t pages);
void u3x_pager_step(int32_t *current, int32_t pages, int step);
int u3x_party_guest(const uint8_t *party);

// this part
void u3x_adv_defaults(uintptr_t base, void *s, int32_t gt);
void u3x_adv_subst(char *out, char *end, const char *text, int64_t value);
int u3x_adv_source(uintptr_t base, int32_t *ev, void *s);
void u3x_k04_retag(uint8_t *page, uint32_t *counters);
int u3x_k_time_source(uintptr_t base, int32_t *ev, void *s, uint32_t *counters);
int u3x_eu_source(uintptr_t base, int32_t *ev, void *s);
}

namespace g38b {

// ---- ids and offsets the spec gives as interface facts
constexpr uint32_t SYM_NET_GAMES = 0x3b9a067du;
constexpr uint32_t SYM_RULE_SETS = 0xf985c7b6u;
constexpr uint32_t SYM_WEAPON_MODES = 0x2982116fu;

constexpr uintptr_t IMG_MODE = 0x17d603c;       // u32, 2 = a custom game is being set up
constexpr uintptr_t IMG_SEL = 0x17d167c;        // s16, the selected net game
constexpr uintptr_t IMG_PARTY = 0x17dc298;
constexpr uintptr_t IMG_PROFILES = 0x17dd610;
constexpr uintptr_t IMG_SCREEN_MENU = 0x16e3188;
constexpr uintptr_t IMG_TEXTURES = 0x1b31128;
constexpr uintptr_t FN_TIME_SOURCE = 0x753e40;
constexpr uintptr_t FN_EULA_ENTRY = 0x6a8010;
constexpr uintptr_t FN_MENU_INPUT = 0x658050;

constexpr uint32_t TAG_TIME_LIMIT = 0x4c414e2au;
constexpr uint32_t TAG_EULA = 0x4c414850u;
constexpr int32_t EULA_PAGES = 7;

// the custom rules block (state block + 0x31f0)
constexpr uintptr_t RULES = 0x31f0;
constexpr uintptr_t R_APPLIED = 0, R_RESPAWN = 4, R_HEALTH = 8, R_WEAPON = 0xc, R_FLAGS1 = 0x10, R_FLAGS2 = 0x11,
                    R_TYPE = 0x14, R_MODES = 0x18, R_COUNTERS = 0x1c;

// the row numbers of the Advanced Settings (low 8 bits of the tag)
constexpr uint32_t ROW_BUTTON = 0x38, ROW_RESPAWN = 0xe7, ROW_BUDDY = 0xe8, ROW_HEALTH = 0xe9, ROW_SPRINT = 0xea,
                   ROW_WEAPON = 0xec, ROW_KICKBACKS = 0xed, ROW_THROWBACK = 0xee, ROW_BOOSTERS = 0xef;

U3X_RO const char empty_text[1] = {0};
U3X_RO const char eula_prefix[6] = {'e', 'u', 'l', 'a', '/', 'p'};
// the first 12 code bytes of game function 0x658050 in the game build the helper is made for
U3X_RO const uint8_t menu_input_code[12] = {0x53, 0x48, 0x8d, 0x0d, 0x38, 0xca, 0x15, 0x01, 0x8b, 0x99, 0x7c, 0x08};

// ---- storage of this part
U3X_BSS int32_t eula_page;          // the page shown, 0 .. 6
U3X_BSS void *eula_texture;         // the texture last answered
U3X_BSS char time_text[48];         // the Time Limit text

// ---- small accessors
template <typename T> [[gnu::always_inline]] inline T &field(void *p, uintptr_t off) {
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + off);
}
[[gnu::always_inline]] inline uint8_t *descriptor(int32_t *ev) { return rd<uint8_t *>(ev, 0x98); }
[[gnu::always_inline]] inline void answer_text(int32_t *ev, const char *text) { field<const char *>(ev, 0x40) = text; }
[[gnu::always_inline]] inline int32_t selection(uintptr_t base) { return at<int16_t>(base + IMG_SEL); }
[[gnu::always_inline]] inline bool first_text(const int32_t *ev) { return ev[0x15] == 0; }

// decimal form of a value; stops at `end`; returns the new write position (no NUL)
U3X_FN char *put_decimal(char *out, char *end, int64_t value) {
    uint64_t v = static_cast<uint64_t>(value);
    if (value < 0) {
        v = 0 - v;
        if (out < end) *out++ = '-';
    }
    char digits[20];
    int n = 0;
    do {
        digits[n++] = static_cast<char>('0' + v % 10);
        v /= 10;
    } while (v);
    while (n && out < end) *out++ = digits[--n];
    return out;
}

// "empty text, word 1 = 0": the answer of a row that does not exist right now
[[gnu::always_inline]] inline int answer_nothing(int32_t *ev) {
    answer_text(ev, empty_text);
    ev[1] = 0;
    return 1;
}

[[gnu::always_inline]] inline void count_set(uint8_t *rules) { field<uint32_t>(rules, R_COUNTERS) += 0x10000u; }

// one on / off rule: a bit of one of the two flag bytes
U3X_FN void flag_row(uintptr_t base, int32_t op, int32_t *ev, void *s, uint8_t *rules, uintptr_t byte, uint8_t bit) {
    uint8_t &flags = field<uint8_t>(rules, byte);
    switch (op) {
    case 5: ev[1] = (flags & bit) ? 1 : 0; break;
    case 6: ev[1] = 1; break;
    case 10:
        flags = ev[1] == 1 ? static_cast<uint8_t>(flags | bit) : static_cast<uint8_t>(flags & ~bit);
        count_set(rules);
        break;
    case 2:
        if (first_text(ev)) answer_text(ev, u3h_text(base, s, ev[1] == 1 ? 0xd1831446u : 0x32066ec9u));
        break;
    default: break;
    }
}

// respawn time: index i = 2 i + 3 seconds
U3X_FN void respawn_row(uintptr_t base, int32_t op, int32_t *ev, void *s, uint8_t *rules) {
    uint32_t &respawn = field<uint32_t>(rules, R_RESPAWN);
    switch (op) {
    case 5: ev[1] = static_cast<int32_t>(static_cast<uint32_t>(f_to_int(respawn)) - 3u) / 2; break;
    case 6: ev[1] = 28; break;
    case 10:
        respawn = f_from_int(static_cast<int32_t>(static_cast<uint32_t>(ev[1]) * 2u + 3u));   // wraps in 32 bits
        count_set(rules);
        break;
    case 2:
        if (first_text(ev)) {
            char *buffer = static_cast<char *>(s) + 0x2c80;
            const char *text = u3h_text(base, s, 0x61275961u);
            // the index is read after the lookup; the number of the text is 64-bit and does not wrap
            u3x_adv_subst(buffer, buffer + 120, text, static_cast<int64_t>(ev[1]) * 2 + 3);
            answer_text(ev, buffer);
        }
        break;
    default: break;
    }
}

// health: index i = 20 i + 20 percent, kept as a multiplier
U3X_FN void health_row(int32_t op, int32_t *ev, void *s, uint8_t *rules) {
    uint32_t &health = field<uint32_t>(rules, R_HEALTH);
    switch (op) {
    case 5: ev[1] = static_cast<int32_t>(static_cast<uint32_t>(f_to_int(f_mul(health, 0x42c80000u))) - 10u) / 20; break;
    case 6: ev[1] = 19; break;
    case 10:
        // the percent value wraps in 32 bits
        health = f_mul(f_add(f_from_int(static_cast<int32_t>(static_cast<uint32_t>(ev[1]) * 20u + 20u)), 0x3e99999au),
                       0x3c23d70au);
        count_set(rules);
        break;
    case 2:
        if (first_text(ev)) {
            char *buffer = static_cast<char *>(s) + 0x2c80;
            // the number of the text is 64-bit and does not wrap
            *put_decimal(buffer, buffer + 120, static_cast<int64_t>(ev[1]) * 20 + 20) = 0;
            answer_text(ev, buffer);
        }
        break;
    default: break;
    }
}

// weapon mode: the modes come from the game's script table (pairs of value and name id)
U3X_FN void weapon_row(uintptr_t base, int32_t op, int32_t *ev, void *s, uint8_t *rules) {
    const uint8_t *table = u3x_sym_table(s, SYM_WEAPON_MODES);
    if (!table) return;
    const uint32_t count = rd<uint32_t>(table, 0);
    field<uint32_t>(rules, R_MODES) = count;
    const uint32_t *modes = rd<const uint32_t *>(table, 8);
    if (!modes) return;
    const uint32_t index = static_cast<uint32_t>(ev[1]);
    switch (op) {
    case 5: {
        const uint32_t current = field<uint32_t>(rules, R_WEAPON);
        uint32_t found = 0;
        for (uint32_t i = 0; i < count; i++)
            if (modes[2 * i] == current) found = i;
        ev[1] = static_cast<int32_t>(found);
        break;
    }
    case 6: ev[1] = static_cast<int32_t>(count - 1u); break;
    case 10:
        if (index < count) {
            field<uint32_t>(rules, R_WEAPON) = modes[2 * static_cast<uintptr_t>(index)];
            count_set(rules);
        }
        break;
    case 2:
        if (first_text(ev) && index < count) answer_text(ev, u3h_text(base, s, modes[2 * static_cast<uintptr_t>(index) + 1]));
        break;
    default: break;
    }
}

[[gnu::always_inline]] inline bool is_row(uint32_t r) {
    return r == ROW_BUTTON || (r >= ROW_RESPAWN && r <= ROW_BOOSTERS && r != 0xeb);
}

// rows that the game type does not have
[[gnu::always_inline]] inline bool row_hidden(uint32_t r, int32_t gt) {
    switch (r) {
    case ROW_RESPAWN: return gt == 10;
    case ROW_BUDDY: return gt == 10 || gt == 14;
    case ROW_KICKBACKS:
    case ROW_BOOSTERS: return gt == 19;
    default: return false;
    }
}

// does game function 0x658050 start with the code of the known game build
U3X_FN bool menu_input_known(uintptr_t base) {
    const uint8_t *code = reinterpret_cast<const uint8_t *>(base + FN_MENU_INPUT);
    for (int i = 0; i < 12; i++)
        if (code[i] != menu_input_code[i]) return false;
    return true;
}

// EULA row polled: keep the accepted version current, then page on the menu's previous / next input
U3X_FN void eula_poll(uintptr_t base) {
    uint8_t *menu = at<uint8_t *>(base + IMG_SCREEN_MENU);
    uint8_t *profile = at<uint8_t *>(base + IMG_PROFILES);
    const uint8_t *entry = game_call<const uint8_t *>(base + FN_EULA_ENTRY);
    if (profile && entry) {
        const uint32_t version = rd<uint32_t>(entry, 0xc);
        if (field<uint32_t>(profile, 0x778) != version) field<uint32_t>(profile, 0x778) = version;
    }
    if (!menu || !eula_texture) return;
    const uint8_t *dialog = rd<const uint8_t *>(menu, 0x898);
    if (dialog && rd<uint32_t>(dialog, 0x10) != 0) return;
    if (!menu_input_known(base)) return;
    const int step = game_call<int, uint8_t *, int, int>(base + FN_MENU_INPUT, menu, 1, 0);
    u3x_pager_step(&eula_page, EULA_PAGES, step);
}

}  // namespace g38b

// 5.1: the defaults of the custom rules, once per selection
extern "C" U3X_FN void u3x_adv_defaults(uintptr_t base, void *s, int32_t gt) {
    using namespace g38b;
    uint8_t *rules = u3h_ext() + RULES;
    const int32_t sel = selection(base);
    const uint32_t mark = static_cast<uint32_t>(sel) + 1u;
    if (field<uint32_t>(rules, R_APPLIED) == mark) return;

    uint32_t respawn = 0x41200000u;   // 10.0 seconds
    const uint8_t *games = u3x_sym_table(s, SYM_NET_GAMES);
    if (games && sel >= 0 && static_cast<uint32_t>(sel) < rd<uint32_t>(games, 0))
        respawn = rd<uint32_t>(rd<const uint8_t *>(games, 8), static_cast<uintptr_t>(sel) * 0xd0 + 0x20);

    field<uint32_t>(rules, R_RESPAWN) = respawn;
    field<uint32_t>(rules, R_HEALTH) = F_ONE;
    field<uint32_t>(rules, R_WEAPON) = 0;
    field<uint8_t>(rules, R_FLAGS1) = 3;
    field<uint8_t>(rules, R_FLAGS2) = 0xe0;
    field<uint32_t>(rules, R_TYPE) = static_cast<uint32_t>(gt);
    field<uint32_t>(rules, R_APPLIED) = mark;
}

// 5.2: a text with every "[A]" replaced by a number
extern "C" U3X_FN void u3x_adv_subst(char *out, char *end, const char *text, int64_t value) {
    while (*text && out < end) {
        if (text[0] == '[' && text[1] == 'A' && text[2] == ']') {
            out = g38b::put_decimal(out, end, value);
            text += 3;
        } else {
            *out++ = *text++;
        }
    }
    *out = 0;
}

// 5.3: the Advanced Settings button and rows
extern "C" U3X_FN int u3x_adv_source(uintptr_t base, int32_t *ev, void *s) {
    using namespace g38b;
    const uint8_t *desc = descriptor(ev);
    if (!desc) return 0;
    const uint32_t tag = rd<uint32_t>(desc, 0x1c);
    const uint32_t r = tag & 0xffu;
    if ((tag >> 8) != 0x4c414fu || !is_row(r)) return 0;
    uint8_t *ext = u3h_ext();
    if (rd<uint32_t>(ext, 0x3a70) & 1u) return 0;
    uint8_t *rules = ext + RULES;

    uint32_t &counters = field<uint32_t>(rules, R_COUNTERS);
    counters = (counters & 0xffff0000u) | ((counters + 1u) & 0xffffu);

    const int32_t op = ev[0];   // the operation: read once, used for the whole request

    // a party guest gets no button
    if (u3x_party_guest(reinterpret_cast<const uint8_t *>(base + IMG_PARTY)) && r == ROW_BUTTON) {
        if (op == 1) answer_text(ev, empty_text);
        else if (op == 8) ev[1] = 0;
        return 1;
    }

    const bool custom = at<uint32_t>(base + IMG_MODE) == 2;
    int32_t gt = -1;
    if (custom) {
        u3h_custom_map(base, s, &gt);
        u3x_adv_defaults(base, s, gt);
    }
    const char *label = u3h_text(base, s, rd<uint32_t>(desc, 8));

    if (r == ROW_BUTTON) {
        if (!custom) return answer_nothing(ev);
        if (op == 1) answer_text(ev, u3x_type_is_coop(base, gt) == 1 ? empty_text : label);
        else if (op == 8) ev[1] = 1;
        return 1;
    }
    if ((r == ROW_RESPAWN || r == ROW_HEALTH) && !custom) return answer_nothing(ev);

    if (op == 1) {
        answer_text(ev, row_hidden(r, gt) ? empty_text : label);
        return 1;
    }
    if (op == 8) {
        ev[1] = 1;
        return 1;
    }

    switch (r) {
    case ROW_BUDDY: flag_row(base, op, ev, s, rules, R_FLAGS1, 0x02); break;
    case ROW_SPRINT: flag_row(base, op, ev, s, rules, R_FLAGS1, 0x01); break;
    case ROW_KICKBACKS: flag_row(base, op, ev, s, rules, R_FLAGS2, 0x80); break;
    case ROW_THROWBACK: flag_row(base, op, ev, s, rules, R_FLAGS2, 0x40); break;
    case ROW_BOOSTERS: flag_row(base, op, ev, s, rules, R_FLAGS2, 0x20); break;
    case ROW_RESPAWN: respawn_row(base, op, ev, s, rules); break;
    case ROW_HEALTH: health_row(op, ev, s, rules); break;
    case ROW_WEAPON: weapon_row(base, op, ev, s, rules); break;
    default: break;
    }
    return 1;
}

// 5.4: Custom Game > Settings when the menu data is prepared
extern "C" U3X_FN void u3x_k04_retag(uint8_t *page, uint32_t *counters) {
    using namespace g38b;
    if (ext_eb()[2] & 0x200u) return;
    if (rd<uint32_t>(page, 8) != 0x86799eeeu) return;
    const uint8_t *list = rd<const uint8_t *>(page, 0x48);
    if (!list) return;
    uint8_t *items = rd<uint8_t *>(list, 8);
    const uint32_t count = rd<uint32_t>(list, 0);
    if (!items || count > 32) return;

    // the Friendly Fire row: the plain value row, as PS3 draws it
    for (uint32_t i = 0; i < count; i++) {
        uint8_t *item = items + i * 0x68;
        if (rd<uint32_t>(item, 0x10) == 13 && rd<uint32_t>(item, 0x28) == 0x9b7aa1a9u) {
            field<uint32_t>(item, 0x10) = 6;
            counters[3]++;
        }
    }
    // the Time Limit row asks this part's source
    counters[4] += u3x_retag_widget(page, 0, 0x2a, TAG_TIME_LIMIT, 32);
}

// 5.5: the Time Limit row: the game's source, then the minutes in front of a label without a number
extern "C" U3X_FN int u3x_k_time_source(uintptr_t base, int32_t *ev, void *s, uint32_t *counters) {
    using namespace g38b;
    const uint8_t *desc = descriptor(ev);
    if (!desc || rd<uint32_t>(desc, 0x1c) != TAG_TIME_LIMIT) return 0;
    if (u3h_gate_closed(base, ev)) return 1;

    game_call<void, int32_t *>(base + FN_TIME_SOURCE, ev);
    if (ev[0] != 2 || !first_text(ev)) return 1;
    const char *label = rd<const char *>(ev, 0x40);
    if (!label || u3x_has_digit(label) == 1) return 1;

    const uint8_t *games = u3x_sym_table(s, SYM_NET_GAMES);
    const uint8_t *rule_sets = u3x_sym_table(s, SYM_RULE_SETS);
    if (!games || !rule_sets) return 1;
    const int32_t sel = selection(base);
    if (sel < 0 || sel >= rd<int32_t>(games, 0)) return 1;
    const int32_t type = rd<int32_t>(rd<const uint8_t *>(games, 8), static_cast<uintptr_t>(sel) * 0xd0 + 0xc);
    if (type < 0 || type >= rd<int32_t>(rule_sets, 0)) return 1;

    const uint8_t *rule = rd<const uint8_t *>(rule_sets, 8) + static_cast<uintptr_t>(type) * 0x58;
    u3x_time_limit_text(time_text, time_text + 47, rule, ev[1], label);
    answer_text(ev, time_text);
    counters[5]++;
    return 1;
}

// 5.6: the EULA pictures, page by page
extern "C" U3X_FN int u3x_eu_source(uintptr_t base, int32_t *ev, void *s) {
    using namespace g38b;
    const uint8_t *desc = descriptor(ev);
    if (!desc || rd<uint32_t>(desc, 0x1c) != TAG_EULA) return 0;
    if (!base) return 1;

    switch (ev[0]) {
    case 4: {
        char name[8];
        for (int i = 0; i < 6; i++) name[i] = eula_prefix[i];
        name[6] = static_cast<char>('1' + eula_page);
        name[7] = 0;
        using FindTexture = void *(*)(uintptr_t dictionary, const char *name);
        const FindTexture find = rd<FindTexture>(s, 768 + 8 * 20);
        eula_texture = find ? find(base + IMG_TEXTURES, name) : nullptr;
        u3x_image_answer(ev, eula_texture);
        break;
    }
    case 5:
    case 6:
    case 7:
    case 10:
    case 23: u3x_pager_answer(ev, &eula_page, EULA_PAGES); break;
    case 9: eula_poll(base); break;
    default: break;
    }
    return 1;
}
