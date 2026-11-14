// G23 part A: the boosters list source of PAGE_BOOSTERS and the three booster list accessors.
// Written from spec/G23_boosters.md (sections 3, 4.0 and 4.1); it follows CONVENTIONS.h in the same file.

extern "C" {
// the helper's C part, and part B of this group through its C exports
int u3h_gate_closed(uintptr_t base, int32_t *ev);
int u3h_sym_ready(uintptr_t base, void *s, uint32_t id);
void u3h_j05_inc(int i);
const char *u3h_text(uintptr_t base, void *s, uint32_t id);
int u3h_publish(uintptr_t base, void *s, uint8_t *profile);
int u3h_k46_op4(uintptr_t base, void *s, int32_t *ev, uint8_t *list, uint32_t slot, uint32_t user, int32_t loadout,
                uint8_t *profile);
void u3h_k49_booster(uintptr_t base, void *s, uint8_t *list, uint32_t slot, uint32_t user, int32_t loadout,
                     uint8_t *profile, int32_t row);
void u3h_booster_icons(uintptr_t base, void *s, int32_t *ev, uint8_t *list, uint32_t slot, uint32_t user,
                       int32_t loadout, uint8_t *profile);

// this part
int u3x_boosters_source(uintptr_t base, int32_t *ev, void *s);
uint8_t *u3x_booster_list(uintptr_t base, void *s);
uint8_t *u3x_booster_def(uint8_t *list, int32_t index);
int u3x_booster_unlocked(void *s, uint32_t user, uint32_t id);
}

namespace g23a {
namespace {

// ---- ids (interface facts of the spec)
constexpr uint32_t TAG_BOOSTERS = 0x4c414e43u;     // source tag of the boosters list and of its page
constexpr uint32_t FROM_PAGE = 0x89e96364u;        // word 0x24 of a request of the page itself
constexpr uint32_t SYM_BOOSTERS = 0x916e03b0u;     // script table of the booster definitions

constexpr uint32_t TEXT_PROMPT = 0x2239ab55u;      // the page's button prompt
constexpr uint32_t TEXT_TITLE_0 = 0x77789975u;     // list titles of slot 0, 1 and 3
constexpr uint32_t TEXT_TITLE_1 = 0x73b984c2u;
constexpr uint32_t TEXT_TITLE_3 = 0xf917b294u;
constexpr uint32_t TEXT_LEVEL = 0xadfc7da6u;       // the word "Level"
constexpr uint32_t TEXT_COST = 0x6ba044a1u;        // cost label
constexpr uint32_t TEXT_CASH = 0xba6d58f7u;        // cash label

constexpr uint32_t SLOT_NAME = 0, SLOT_LEVEL = 3, SLOT_COST = 4, SLOT_CASH = 8;
constexpr uint32_t SLOT_TIER_1 = 0x80d4cc7bu, SLOT_TIER_2 = 0xadacdbf3u, SLOT_TIER_3 = 0xb684d68bu;

constexpr uint32_t VALUE_LEVEL = 0x67a48eb9u;      // value query: level number
constexpr uint32_t VALUE_MISSION = 0x18567c73u;    // value query: a mission tier is open
constexpr uint32_t ACTION_BUTTON = 0x92f7fefau;    // the page's button action
constexpr uint32_t CELL_SIZE = 0x43000000u;        // 128.0

// ---- image data
constexpr uintptr_t IMG_MENU_USER_FLAG = 0x17bde75, IMG_MENU_USER = 0x17bf714, IMG_PROFILES = 0x17dd610;
constexpr uintptr_t IMG_LOADOUT = 0x17d9988, IMG_SLOT = 0x17d9990;
constexpr uintptr_t IMG_PAID = 0x17c3b40, IMG_OVERRIDE = 0x17c3b48;
constexpr uintptr_t IMG_UNLOCKS = 0x17f3068, IMG_NETGAME = 0x17cb060, IMG_MISSIONS = 0x17da200;
constexpr uintptr_t PROFILE_SIZE = 0x7b00, MISSIONS_SIZE = 0x1018;

// ---- state object
constexpr uintptr_t ST_BASE = 760, ST_NATIVES = 768, ST_TEXT = 0x2c80, ST_LISTS = 0x3000, ST_SELECTED = 0x3840,
                    ST_BUILT = 0x3850;
constexpr uintptr_t LIST_SIZE = 0x210, LIST_COUNT = 0x208;
constexpr int TEXT_MAX = 250;

// ---- the helper's own texts
U3X_RO const char txt_empty[] = "";
U3X_RO const char txt_space[] = " ";
U3X_RO const char txt_break[] = "\n";
U3X_RO const char txt_colon[] = ": ";
U3X_RO const char col_too_dear[] = "|@Cffff0000|";
U3X_RO const char col_in_reach[] = "|@Cff7cce96|";
U3X_RO const char col_cash[] = "|@Cff00ff00|";

template <typename T> [[gnu::always_inline]] inline T &field(void *p, uintptr_t off) {
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + off);
}
template <typename T> [[gnu::always_inline]] inline T &image(uintptr_t base, uintptr_t off) {
    return *reinterpret_cast<T *>(base + off);
}
template <typename R, typename... A> [[gnu::always_inline]] inline R call(uintptr_t fn, A... args) {
    return reinterpret_cast<R (*)(A...)>(fn)(args...);
}
// "native k" of the state object: the address of a game function, or 0
[[gnu::always_inline]] inline uintptr_t native(const void *s, int k) {
    return rd<uintptr_t>(s, ST_NATIVES + 8 * static_cast<uintptr_t>(k));
}
[[gnu::always_inline]] inline void answer_text(int32_t *ev, const char *text) { field<const char *>(ev, 0x40) = text; }

// what the operations of one request share
struct Request {
    uintptr_t base;
    int32_t *ev;
    uint8_t *s;
    uint8_t *list;
    uint32_t slot;
    uint32_t user;
    int32_t loadout;
    uint8_t *profile;   // the menu user's profile object, or null
    uint32_t cash;
};

[[gnu::always_inline]] inline int32_t &selected_row(const Request *rq) {
    return field<int32_t>(rq->s, ST_SELECTED + 4 * rq->user);
}
[[gnu::always_inline]] inline uint32_t def_id(const uint8_t *def) { return rd<uint32_t>(def, 4); }

// the level the user has of a booster; -1 for none or when the game function is not known
U3X_FN int32_t level_of(const Request *rq, const uint8_t *def) {
    const uintptr_t n23 = native(rq->s, 23);
    return n23 ? call<int32_t>(n23, rq->base + IMG_PAID, rq->user, def) : -1;
}

// lets the game fill the list of the slot (the caller checked native 29) and notes that it is built
U3X_FN void fill_list(const Request *rq) {
    call<void>(native(rq->s, 29), rq->list, rq->loadout == 4 ? 1u : 0u);
    field<uint32_t>(rq->s, ST_BUILT) |= 1u << rq->slot;
}

// ---- the shared text buffer: pieces one after the other, cut after 250 characters, always NUL-terminated
struct Text {
    char *b;
    int n;
};

U3X_FN void put(Text *t, const char *piece) {
    while (*piece && t->n < TEXT_MAX) t->b[t->n++] = *piece++;
    t->b[t->n] = 0;
}

// decimal digits; with `grouped`, a comma between groups of three digits counted from the right
U3X_FN void put_number(Text *t, uint32_t v, bool grouped) {
    char rev[16];
    int k = 0;
    do {
        if (grouped && k % 4 == 3) rev[k++] = ',';
        rev[k++] = static_cast<char>('0' + v % 10);
        v /= 10;
    } while (v);
    while (k && t->n < TEXT_MAX) t->b[t->n++] = rev[--k];
    t->b[t->n] = 0;
}

U3X_FN const char *tier_text(const Request *rq, const uint8_t *tiers, int32_t k) {
    return u3h_text(rq->base, rq->s, rd<uint32_t>(tiers, 0x10 * static_cast<uintptr_t>(k)));
}

// ---- operation 2 of the list: the texts of a row
U3X_FN void row_text(const Request *rq) {
    int32_t *const ev = rq->ev;
    const uint8_t *def = u3x_booster_def(rq->list, ev[1]);
    if (!def) return;

    const int32_t own = level_of(rq, def);
    char *const buffer = &field<char>(rq->s, ST_TEXT);
    buffer[0] = 0;
    Text t{buffer, 0};

    const uint32_t id = def_id(def);
    const int32_t tier_count = rd<int32_t>(def, 0x10);
    const uint8_t *tiers = rd<const uint8_t *>(def, 0x18);
    const uint32_t slot = static_cast<uint32_t>(ev[0x15]);

    if (slot == SLOT_NAME) {
        answer_text(ev, u3h_text(rq->base, rq->s, rd<uint32_t>(def, 8)));
        return;
    }
    if (slot == SLOT_LEVEL) {   // "Level 2" and the description of the tier the user has
        if (own < 0 || own >= tier_count || !tiers) return;
        put(&t, u3h_text(rq->base, rq->s, TEXT_LEVEL));
        put(&t, txt_space);
        put_number(&t, static_cast<uint32_t>(own) + 1, false);
        put(&t, txt_break);
        put(&t, tier_text(rq, tiers, own));
        answer_text(ev, buffer);
        return;
    }
    if (slot == SLOT_COST) {
        if (!u3x_booster_unlocked(rq->s, rq->user, id)) {   // what unlocks it
            const uintptr_t n26 = native(rq->s, 26);
            answer_text(ev, n26 ? call<const char *>(n26, id, 0u) : txt_empty);
            return;
        }
        const int32_t cost = rd<int32_t>(def, 0x20);
        const uintptr_t n27 = native(rq->s, 27);
        if (cost < 1 || !n27 || !(call<uint32_t>(n27, rq->base + IMG_NETGAME) & 0xff)) {
            answer_text(ev, txt_empty);
            return;
        }
        put(&t, u3h_text(rq->base, rq->s, TEXT_COST));
        put(&t, rq->cash < static_cast<uint32_t>(cost) ? col_too_dear : col_in_reach);
        put_number(&t, static_cast<uint32_t>(cost), true);
        answer_text(ev, buffer);
        return;
    }
    if (slot == SLOT_CASH) {
        put(&t, u3h_text(rq->base, rq->s, TEXT_CASH));
        put(&t, col_cash);
        put_number(&t, rq->cash, true);
        answer_text(ev, buffer);
        return;
    }

    int32_t n;   // the three tier lines
    if (slot == SLOT_TIER_1) n = 0;
    else if (slot == SLOT_TIER_2) n = 1;
    else if (slot == SLOT_TIER_3) n = 2;
    else return;
    if (!tiers || n >= tier_count) return;
    if (n == 0 && tier_count == 1) {   // a booster with a single tier shows its description alone
        answer_text(ev, tier_text(rq, tiers, 0));
        return;
    }
    put(&t, u3h_text(rq->base, rq->s, TEXT_LEVEL));
    put(&t, txt_space);
    put_number(&t, static_cast<uint32_t>(n) + 1, false);
    put(&t, txt_colon);
    put(&t, tier_text(rq, tiers, n));
    answer_text(ev, buffer);
}

// ---- operations 9 and 0x17: the list is filled again and the selected booster stops being "new"
U3X_FN void refresh(const Request *rq) {
    if (native(rq->s, 29)) fill_list(rq);
    const uint8_t *def = u3x_booster_def(rq->list, selected_row(rq));
    const uintptr_t n32 = native(rq->s, 32);
    if (def && n32) call<void>(n32, rq->base + IMG_UNLOCKS, rq->user, def_id(def), 0u);
}

// ---- operation 11 of the list: values of a row
U3X_FN void row_value(const Request *rq) {
    int32_t *const ev = rq->ev;
    const uint8_t *def = u3x_booster_def(rq->list, ev[1]);
    if (!def) return;
    const uint32_t what = static_cast<uint32_t>(ev[0x13]);
    if (what == VALUE_LEVEL) {
        if (u3x_booster_unlocked(rq->s, rq->user, def_id(def)) && native(rq->s, 23)) ev[0xe] = level_of(rq, def) + 1;
    } else if (what == VALUE_MISSION) {
        const uintptr_t n31 = native(rq->s, 31);
        if (n31)
            ev[0xe] = call<const void *>(n31, rq->base + IMG_MISSIONS + MISSIONS_SIZE * rq->user, def_id(def)) ? 1 : 0;
    }
}

// ---- operation 0x16: the clicked booster is equipped and the profile saved
U3X_FN void equip(const Request *rq) {
    const uint8_t *def = u3x_booster_def(rq->list, rq->ev[0x17]);
    if (!def) return;
    const uint32_t id = def_id(def);
    if (!u3x_booster_unlocked(rq->s, rq->user, id)) return;

    const int32_t home = rd<int32_t>(def, 0xc);   // the slot the booster belongs to
    if (home >= 0 && home <= 3) image<uint32_t>(rq->base, IMG_OVERRIDE + 4 * static_cast<uintptr_t>(home)) = 0;
    if (home >= 0 && home <= 2) {
        const uintptr_t n28 = native(rq->s, 28);
        if (rq->profile && n28) call<void>(n28, rq->profile, home + 4, id, rq->loadout);
    } else if (home == 3) {
        image<uint32_t>(rq->base, IMG_PAID + 4 * (rq->user & 1)) = id;
    }

    // saved in every case, with the profile object of the (limited) user index
    uint8_t *const profiles = image<uint8_t *>(rq->base, IMG_PROFILES);
    u3h_publish(rq->base, rq->s, profiles ? profiles + PROFILE_SIZE * rq->user : nullptr);
}

// ---- requests of the page itself
U3X_FN void page_request(const Request *rq) {
    int32_t *const ev = rq->ev;
    const int32_t op = ev[0];
    if (op == 6) {   // the button is offered while an unlocked booster has a further tier
        ev[1] = -1;
        const uint8_t *def = u3x_booster_def(rq->list, selected_row(rq));
        if (!def) return;
        const uint32_t own = static_cast<uint32_t>(level_of(rq, def));
        if (own < rd<uint32_t>(def, 0x10) - 1u && u3x_booster_unlocked(rq->s, rq->user, def_id(def))) ev[1] = 0;
    } else if (op == 2) {
        if (ev[0x15] == 0) answer_text(ev, u3h_text(rq->base, rq->s, TEXT_PROMPT));
    } else if (op == 11) {
        ev[0xe] = 8;
    } else if (op == 19) {
        ev[0xe] = static_cast<int32_t>(ACTION_BUTTON);
    } else if (op == 1) {
        answer_text(ev, txt_empty);
    }
}

// ---- requests of the list (after the icon and sound steps)
U3X_FN void list_request(const Request *rq) {
    int32_t *const ev = rq->ev;
    const int32_t op = ev[0];
    if (op == 1) {   // title of the slot's list; slot 2 has none
        uint32_t title = 0;
        if (rq->slot == 0) title = TEXT_TITLE_0;
        else if (rq->slot == 1) title = TEXT_TITLE_1;
        else if (rq->slot == 3) title = TEXT_TITLE_3;
        answer_text(ev, rq->slot == 2 ? txt_empty : u3h_text(rq->base, rq->s, title));
    } else if (op == 2) {
        row_text(rq);
    } else if (op == 4) {
        if (!(ext_ea()[0] & 0x20))
            u3h_booster_icons(rq->base, rq->s, ev, rq->list, rq->slot, rq->user, rq->loadout, rq->profile);
    } else if (op == 5) {
        ev[1] = selected_row(rq);
    } else if (op == 6) {
        ev[1] = rd<int32_t>(rq->list, LIST_COUNT) - 1;
    } else if (op == 9 || op == 0x17) {
        refresh(rq);
    } else if (op == 10) {
        if (u3x_booster_def(rq->list, ev[1])) selected_row(rq) = ev[1];
    } else if (op == 11) {
        row_value(rq);
    } else if (op == 12) {
        if (!(ext_ea()[0] & 0x20)) ev[0x1f] = ev[0x20] = static_cast<int32_t>(CELL_SIZE);
    } else if (op == 0x11) {
        ev[1] = 8;
    } else if (op == 0x16) {
        equip(rq);
    }
}

}  // namespace
}  // namespace g23a

// The list of the booster slot being edited (a slot number above 3 counts as 0); its first byte names the slot.
extern "C" U3X_FN uint8_t *u3x_booster_list(uintptr_t base, void *s) {
    uint32_t slot = g23a::image<uint32_t>(base, g23a::IMG_SLOT);
    if (slot > 3) slot = 0;
    uint8_t *list = &g23a::field<uint8_t>(s, g23a::ST_LISTS + g23a::LIST_SIZE * slot);
    list[0] = static_cast<uint8_t>(slot);
    return list;
}

// The booster definition of a row, or null when the list has no such row.
extern "C" U3X_FN uint8_t *u3x_booster_def(uint8_t *list, int32_t index) {
    if (index < 0 || index >= rd<int32_t>(list, g23a::LIST_COUNT)) return nullptr;
    return rd<uint8_t *>(list, 8 + 8 * static_cast<uintptr_t>(index));
}

// 1 when the user has unlocked the item. The image base is the one a list source left in the state object.
extern "C" U3X_FN int u3x_booster_unlocked(void *s, uint32_t user, uint32_t id) {
    const uintptr_t n24 = g23a::native(s, 24), n25 = g23a::native(s, 25);
    if (!n24 || !n25) return 0;
    const uintptr_t unlocks = static_cast<uintptr_t>(rd<uint64_t>(s, g23a::ST_BASE)) + g23a::IMG_UNLOCKS;
    void *entry = g23a::call<void *>(n24, unlocks, static_cast<int32_t>(id), static_cast<int32_t>(0));
    return (g23a::call<uint32_t>(n25, unlocks, user, entry) & 0xff) ? 1 : 0;
}

// The data source of the boosters list and of PAGE_BOOSTERS: 1 when the request is theirs, else 0.
extern "C" U3X_FN int u3x_boosters_source(uintptr_t base, int32_t *ev, void *s) {
    using namespace g23a;
    const uint8_t *source = rd<const uint8_t *>(ev, 0x98);
    if (!source || rd<uint32_t>(source, 0x1c) != TAG_BOOSTERS) return 0;
    if (u3h_gate_closed(base, ev)) return 1;
    field<uint64_t>(s, ST_BASE) = base;

    const int32_t op = ev[0];
    const bool from_page = static_cast<uint32_t>(ev[0x24]) == FROM_PAGE;

    // the page title, answered before the booster table is asked for
    if (op == 1 && from_page && !(ext_ea()[0] & 8) && !(ext_eb()[1] & 0x400000)) {
        answer_text(ev, txt_empty);
        u3h_j05_inc(0);
        return 1;
    }

    if (!u3h_sym_ready(base, s, SYM_BOOSTERS)) {   // the booster table is not loaded
        if (op == 6) ev[1] = -1;
        if (op == 1) u3h_j05_inc(1);
        return 1;
    }

    Request rq;
    rq.base = base;
    rq.ev = ev;
    rq.s = static_cast<uint8_t *>(s);
    rq.list = u3x_booster_list(base, s);
    rq.slot = rq.list[0];
    const uint32_t menu_user = image<uint8_t>(base, IMG_MENU_USER_FLAG) ? 1u : image<uint32_t>(base, IMG_MENU_USER);
    rq.user = menu_user & 3;
    rq.loadout = image<int32_t>(base, IMG_LOADOUT);
    uint8_t *const profiles = image<uint8_t *>(base, IMG_PROFILES);
    rq.profile = (profiles && menu_user < 4) ? profiles + PROFILE_SIZE * menu_user : nullptr;
    rq.cash = rq.profile ? rd<uint32_t>(rq.profile, 0x74c) : 0;
    if (!(field<uint32_t>(s, ST_BUILT) & (1u << rq.slot)) && native(s, 29)) fill_list(&rq);

    if (from_page && !(ext_ea()[0] & 8)) {
        ext_ea()[15] += 1;
        page_request(&rq);
        return 1;
    }

    if (op == 4 && u3h_k46_op4(base, s, ev, rq.list, rq.slot, rq.user, rq.loadout, rq.profile)) return 1;
    if (op == 0x16) u3h_k49_booster(base, s, rq.list, rq.slot, rq.user, rq.loadout, rq.profile, ev[0x17]);
    list_request(&rq);
    return 1;
}
