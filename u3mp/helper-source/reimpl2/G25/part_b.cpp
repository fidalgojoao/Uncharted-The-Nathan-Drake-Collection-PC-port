// G25 part B: the hunter class list (list source 0xbf, the page a co-op hunter gets while dead) and its building
// blocks. Written from the behaviour description G25_coop_lists.md, sections 4.8 to 4.11.

extern "C" {
// the helper's C part and other groups
int u3h_gate_closed(uintptr_t base, int32_t *ev);
int u3h_sym_ready(uintptr_t base, void *s, uint32_t id);
const char *u3h_text(uintptr_t base, void *s, uint32_t id);
void u3h_set_icon(uintptr_t base, int32_t *ev, uint32_t id);
uint32_t u3h_crc_sid(const char *name);
int u3h_show(uintptr_t base, void *s, uint32_t page, int silent);
int u3x_menu_back(uintptr_t base, uint8_t *m, int silent);
void u3x_menu_close(uintptr_t base, uint8_t *m);
int u3h_net_coop_mode(uintptr_t base, void *s);
int u3h_np_alive(void *s, uint8_t *p);
uint32_t u3h_respawn_delay(void *s, uint8_t *p);
int64_t u3h_f_ticks(uint32_t f, int64_t half);

// this part
int u3x_spoiler_list(uintptr_t base, int32_t *ev, void *s);
uint8_t *u3x_spoiler_entry(uintptr_t base, void *s, int32_t row, int32_t *index);
int32_t u3x_spoiler_rows(uintptr_t base);
int u3x_spoiler_can_pick(uintptr_t base, void *s, uint8_t *p);
}

namespace g25b {

// ---- interface facts ----
constexpr uint32_t LIST_TAG = 0x4c414ebfu;        // source tag of the hunter class list
constexpr uint32_t CLASS_TABLE = 0x2b6127a0u;     // script symbol: the class table
constexpr uint32_t POINTS_SLOT = 0xee5350e3u;     // text slot and image slot of the kickback points
constexpr uint32_t PAGE_ON_INPUT_7 = 0xf3026799u; // page shown on menu input result 7

constexpr uintptr_t IMG_MENU_USER = 0x17c23a0;    // u32: local user of the menu (low 2 bits)
constexpr uintptr_t IMG_POINTS = 0x17d79f0;       // u32 every 0x30 bytes: kickback points of a local user
constexpr uintptr_t IMG_SCREEN_MENU = 0x16e3188;  // pointer: screen menu object of screen 0
constexpr uintptr_t IMG_NET_GAME = 0x17cb060;     // net game manager
constexpr uintptr_t IMG_NET_CLOCK = 0x17b4ca4;    // s32: net clock, 300 ticks per second

constexpr uintptr_t FN_ROUND = 0x723060;          // (net game manager) -> entry of the current round
constexpr uintptr_t FN_GAME_SOURCE = 0x76b0f0;    // (request): the game's own source of this list
constexpr uintptr_t FN_MENU_INPUT = 0x658050;     // (menu, int, int) -> button input of this frame

// the first 12 code bytes each of the three game functions must have (guard against another game build)
U3X_RO const uint8_t code_round[12] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x56, 0x53, 0x48, 0x89, 0xFB, 0xBF, 0x7D};
U3X_RO const uint8_t code_game_source[12] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x53, 0x48, 0x83, 0xEC};
U3X_RO const uint8_t code_menu_input[12] = {0x53, 0x48, 0x8D, 0x0D, 0x38, 0xCA, 0x15, 0x01, 0x8B, 0x99, 0x7C, 0x08};

// texture names of the hunter medals: this stem, then "blank" or a level number
U3X_RO const char medal_stem[] = "general/net/hunter-medals/hunter-medal-";
U3X_RO const char medal_blank[] = "blank";

U3X_BSS static char points_text[24];   // "x" + the points in decimal; must outlive the request

// ---- small helpers ----

// are the 12 bytes at `code` the expected ones?
U3X_FN static bool code_is(uintptr_t code, const uint8_t *expected) {
    for (int i = 0; i < 12; ++i)
        if (at<uint8_t>(code + i) != expected[i]) return false;
    return true;
}

// the class list of the current round (s32 count at +0, pointer to u32 class ids at +8), or null
U3X_FN static uint8_t *round_classes(uintptr_t base) {
    if (!code_is(base + FN_ROUND, code_round)) return nullptr;
    uint8_t *round = game_call<uint8_t *>(base + FN_ROUND, reinterpret_cast<uint8_t *>(base + IMG_NET_GAME));
    return round ? rd<uint8_t *>(round, 0x40) : nullptr;
}

// UI icon id of the hunter medal whose texture name ends in `tail`
U3X_FN static uint32_t medal_icon(const char *tail) {
    char name[64];
    int n = 0;
    for (const char *c = medal_stem; *c; ++c) name[n++] = *c;
    for (; *tail && n < 63; ++tail) name[n++] = *tail;
    name[n] = 0;
    return u3h_crc_sid(name);
}

// UI icon id of the class level picture, level 0 .. 10
U3X_FN static uint32_t level_icon(uint32_t level) {
    char number[4];
    int n = 0;
    if (level >= 10) number[n++] = static_cast<char>('0' + level / 10);
    number[n++] = static_cast<char>('0' + level % 10);
    number[n] = 0;
    return medal_icon(number);
}

// fills the points buffer with "x<points>" and returns it
U3X_FN static const char *points_answer(uint32_t points) {
    char digits[10];
    int k = 0;
    do { digits[k++] = static_cast<char>('0' + points % 10); points /= 10; } while (points);
    int n = 0;
    points_text[n++] = 'x';
    while (k) points_text[n++] = digits[--k];
    points_text[n] = 0;
    return points_text;
}

[[gnu::always_inline]] inline uint32_t points_of(uintptr_t base, uint32_t user) {
    return at<uint32_t>(base + IMG_POINTS + 0x30 * user);
}
[[gnu::always_inline]] inline void set_text(int32_t *ev, const char *text) {
    *reinterpret_cast<const char **>(reinterpret_cast<uint8_t *>(ev) + 0x40) = text;
}
[[gnu::always_inline]] inline uint32_t &counter(int k) {   // state block: 7 requests, 8 picks, 9 last position
    return *reinterpret_cast<uint32_t *>(u3h_ext() + 0x100 + 4 * k);
}
[[gnu::always_inline]] inline int32_t &selected_row(uint32_t user) {
    return *reinterpret_cast<int32_t *>(u3h_ext() + 0x180 + 4 * user);
}
[[gnu::always_inline]] inline uint8_t &no_pick_toggle(uint32_t user) {
    return *(u3h_ext() + 0x190 + user);
}

// operation 0x16: the hunter picks the class of the clicked row
U3X_FN static void pick(uintptr_t base, int32_t *ev, void *s, uint32_t user) {
    int32_t position = 0;
    const uint8_t *cls = u3x_spoiler_entry(base, s, ev[0x17], &position);
    if (!cls) return;
    if (points_of(base, user) < rd<uint32_t>(cls, 0x1c)) return;   // cannot afford it
    uint8_t *player = at<uint8_t *>(base + IMG_NET_GAME + 0x370 + 8 * user);
    if (!player) return;
    if (!u3x_spoiler_can_pick(base, s, player)) return;

    *(player + 0x60c) = 1;
    *reinterpret_cast<int32_t *>(player + 0x608) = position;
    counter(8) += 1;
    counter(9) = static_cast<uint32_t>(position);

    uint8_t *menu = at<uint8_t *>(base + IMG_SCREEN_MENU);   // may be null: both calls are made all the same
    u3x_menu_back(base, menu, 0);
    u3x_menu_close(base, menu);
}

}  // namespace g25b

extern "C" {

// 4.8: the class (entry of the class table) of a list row; its table position goes to *index when found
U3X_FN uint8_t *u3x_spoiler_entry(uintptr_t base, void *s, int32_t row, int32_t *index) {
    const auto lookup = rd<uint8_t *(*)(uint32_t)>(s, 768 + 8 * 13);   // native 13
    if (!lookup) return nullptr;
    const uint8_t *table = lookup(g25b::CLASS_TABLE);
    if (!table) return nullptr;

    const uint8_t *round = g25b::round_classes(base);
    if (!round || row < 0 || row >= rd<int32_t>(round, 0)) return nullptr;
    const uint32_t want = rd<uint32_t>(rd<const uint8_t *>(round, 8), 4 * static_cast<uintptr_t>(row));

    uint8_t *classes = rd<uint8_t *>(table, 8);
    if (!classes) return nullptr;
    const int32_t count = rd<int32_t>(table, 0);
    for (int32_t i = 0; i < count; ++i) {
        uint8_t *cls = classes + 0x28 * static_cast<uintptr_t>(i);
        if (rd<uint32_t>(cls, 0) == want) {
            if (index) *index = i;
            return cls;
        }
    }
    return nullptr;
}

// 4.9: the number of rows = classes of the current round
U3X_FN int32_t u3x_spoiler_rows(uintptr_t base) {
    const uint8_t *round = g25b::round_classes(base);
    return round ? rd<int32_t>(round, 0) : 0;
}

// 4.11: a hunter may pick while dead, past the respawn delay, with no request pending
U3X_FN int u3x_spoiler_can_pick(uintptr_t base, void *s, uint8_t *p) {
    if (!u3h_net_coop_mode(base, s)) return 0;
    if (rd<int32_t>(p, 0x21c) != 1) return 0;   // not a hunter
    if (u3h_np_alive(s, p)) return 0;
    if (rd<uint8_t>(p, 0x420)) return 0;        // respawn request pending
    if (rd<uint8_t>(p, 0x5e4)) return 0;        // leaving

    const uint32_t delay = u3h_respawn_delay(s, p);   // seconds, float pattern: must be positive and finite
    if ((delay >> 31) || !(delay & 0x7fffffffu) || ((delay >> 23) & 0xff) == 0xff) return 0;

    // time since the stamp: both s32 values widened first, so the 64-bit difference is exact (section 8, item 1)
    const int64_t clock = at<int32_t>(base + g25b::IMG_NET_CLOCK);
    const int64_t stamp = rd<int32_t>(p, 0x39c);
    return u3h_f_ticks(delay, 0) < clock - stamp ? 1 : 0;
}

// 4.10: the list source
U3X_FN int u3x_spoiler_list(uintptr_t base, int32_t *ev, void *s) {
    using namespace g25b;
    const uint8_t *source = rd<const uint8_t *>(ev, 0x98);
    if (!source || rd<uint32_t>(source, 0x1c) != LIST_TAG) return 0;
    if (u3h_gate_closed(base, ev)) return 1;

    counter(7) += 1;
    const uint32_t user = at<uint32_t>(base + IMG_MENU_USER) & 3;
    const int32_t op = ev[0];

    if (op == 1) {   // the title: still answered by the game's own source
        if (code_is(base + FN_GAME_SOURCE, code_game_source)) game_call<void>(base + FN_GAME_SOURCE, ev);
        return 1;
    }
    if (!u3h_sym_ready(base, s, CLASS_TABLE)) return 1;

    if (op == 2) {   // texts
        const uint32_t slot = static_cast<uint32_t>(ev[0x15]);
        if (slot == 0) {
            const uint8_t *cls = u3x_spoiler_entry(base, s, ev[1], nullptr);
            if (cls) set_text(ev, u3h_text(base, s, rd<uint32_t>(cls, 4)));
        } else if (slot == POINTS_SLOT) {
            set_text(ev, points_answer(points_of(base, user)));
        }
    } else if (op == 4) {   // pictures
        if (static_cast<uint32_t>(ev[0x14]) == POINTS_SLOT) u3h_set_icon(base, ev, medal_icon(medal_blank));
    } else if (op == 5) {
        ev[1] = selected_row(user);
    } else if (op == 6) {
        ev[1] = u3x_spoiler_rows(base) - 1;
    } else if (op == 9) {   // the page's buttons
        uint8_t *menu = at<uint8_t *>(base + IMG_SCREEN_MENU);
        if (menu && code_is(base + FN_MENU_INPUT, code_menu_input)) {
            const int input = game_call<int>(base + FN_MENU_INPUT, menu, 1, 0);
            if (input == 7) u3h_show(base, s, PAGE_ON_INPUT_7, 0);
            else if (input == 0xc) no_pick_toggle(user) ^= 1;
        }
    } else if (op == 10) {
        selected_row(user) = ev[1];
    } else if (op == 0xe) {   // cell picture: the level column
        if (ev[0x19] == 2) {
            const uint8_t *cls = u3x_spoiler_entry(base, s, ev[0x18], nullptr);
            if (cls) {
                const uint32_t level = rd<uint32_t>(cls, 0x1c);
                if (level < 11) u3h_set_icon(base, ev, level_icon(level));
            }
        }
    } else if (op == 0x11) {
        ev[1] = 3;
    } else if (op == 0x15) {   // can this row be picked
        if (no_pick_toggle(user)) {
            ev[1] = 0;
        } else {
            const uint8_t *cls = u3x_spoiler_entry(base, s, ev[0x17], nullptr);
            ev[1] = cls && rd<uint32_t>(cls, 0x1c) <= points_of(base, user) ? 1 : 0;
        }
    } else if (op == 0x16) {
        pick(base, ev, s, user);
    } else if (op == 0x17) {
        if (ev[0xd] != 0) selected_row(user) = 0;
    } else if (op == 0x18) {
        ev[1] = 1;
    }
    return 1;
}

}  // extern "C"
