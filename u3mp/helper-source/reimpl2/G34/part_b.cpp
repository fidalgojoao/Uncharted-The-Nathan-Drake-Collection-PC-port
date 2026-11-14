// G34 part B: row click of the party member list, the focus keeper and the report popup.
// Written from spec G34_party_members.md (sections 2.2, 3, 5).

// The quick-action list is an existing type at file scope of the C++ part (G18 2.2); it is only defined here for a
// stand-alone compile of this part.
#ifdef G34_STANDALONE
struct QuickActions { uint32_t argument, page; int32_t count; uint32_t pairs[16]; };
#endif

extern "C" {
// part A and the pieces of this group that stay
void u3x_i01_rows(uintptr_t base);
int u3x_i01_self(uintptr_t base, uint8_t *m);
uint8_t *u3x_i01_md(uintptr_t base, uint8_t *m);
// other groups and the C part
void *u3h_i01_state(void);
void *u3h_i05_state(void);
int u3x_session_host(const uint8_t *s);
int u3x_quick_action_add(QuickActions *q, uint32_t text, uint32_t action);
void u3h_fr_qa_push(uintptr_t base, void *q);
uint32_t u3h_pad_pressed(uintptr_t base, int32_t screen);
uint8_t *u3h_active_page_object(uintptr_t base);
uint8_t *u3x_sym_table(void *s, uint32_t id);
uint32_t u3x_report_rows(const uint8_t *table);
// this part
void u3x_i01_click(uintptr_t base, int32_t *ev);
int u3x_i01_canfocus(uint8_t *c);
int u3x_i01_leftctl(uint32_t pid, uint32_t id);
void u3x_i01_focus(uintptr_t base, void *s);
int u3x_i01_user_party(void);
void u3x_report_popup(uintptr_t base, void *s);
}

namespace g34b {

// the party list state (spec 3.3)
struct PartyList {
    uint32_t d[12];            // 0x000 diagnostics
    uint8_t *members[10];      // 0x030
    uint32_t member_count;     // 0x080
    uint32_t rows[20];         // 0x084
    uint32_t row_count;        // 0x0d4
    int32_t selected;          // 0x0d8
    uint8_t pseudo[0x120];     // 0x0dc
    char name[64];             // 0x1fc
    uint32_t in_box;           // 0x23c
    uint32_t alone;            // 0x240
    uint32_t page;             // 0x244
    uint32_t popup;            // 0x248
    uint32_t tick;             // 0x24c
    uint8_t *previous;         // 0x250
    uint8_t *last;             // 0x258
    uint8_t *box;              // 0x260
    uint32_t remembered[4];    // 0x268
    uint32_t remembered_next;  // 0x278
    uint32_t pad;
};
static_assert(sizeof(PartyList) == 0x280);
static_assert(__builtin_offsetof(PartyList, row_count) == 0xd4);
static_assert(__builtin_offsetof(PartyList, in_box) == 0x23c);
static_assert(__builtin_offsetof(PartyList, previous) == 0x250);
static_assert(__builtin_offsetof(PartyList, remembered) == 0x268);

inline PartyList &party_list() { return *static_cast<PartyList *>(u3h_i01_state()); }

// image offsets (spec 3.6, 3.7)
constexpr uintptr_t GAME_CLOCK = 0x17b4ba4, MENU = 0x17bd080, PARTY_LOCKED = 0x17c3570, SELECTED_ID = 0x17c3871,
                    CLICK_WORD = 0x17c3898, NET = 0x17d5c58, PARTY_SESSION = 0x17dc298, SHOW_QUICK_ACTIONS = 0x6528d0;
constexpr uint32_t ID_BYTES = 0x24;
constexpr uint32_t ROW_GUEST = 0x100;

// ids (spec 3.8)
constexpr uint32_t BOX_ID = 0x3406420c, PROFILE_ID = 0x649e809a, QUICK_PAGE = 0xecc3219d;
constexpr uint32_t PAD_RIGHT = 0x20u | 0x1000000u, PAD_LEFT = 0x80u | 0x800000u;

inline void copy_id(uint8_t *to, const uint8_t *from) {
    for (uint32_t i = 0; i < ID_BYTES; i++) to[i] = from[i];
}

inline void new_list(QuickActions &q) {
    q.argument = 0;
    q.page = QUICK_PAGE;
    q.count = 0;
    for (uint32_t i = 0; i < 16; i++) q.pairs[i] = 0;
}

// number of members of the party: one without a room, else the member slots in use
inline uint32_t party_size(const uint8_t *session) {
    if (rd<uint64_t>(session, 0x18) == 0) return 1;
    uint32_t n = 0;
    for (uint32_t i = 0; i < 10; i++)
        if (rd<uint8_t>(session, 0x680 + i * 0x120 + 0x25)) n++;
    return n;
}

inline uint32_t control_id(const uint8_t *c) { return rd<uint32_t>(c, 0x10); }
inline bool has_focus(const uint8_t *c) { return rd<uint8_t>(c, 8) != 0; }

// the last control of the list whose focus byte is set, or null
inline uint8_t *focused_control(uint8_t *const *list, uint32_t n) {
    uint8_t *f = nullptr;
    for (uint32_t i = 0; i < n; i++)
        if (list[i] && has_focus(list[i])) f = list[i];
    return f;
}

}  // namespace g34b

// 5.2: asks a control whether it can take the focus
extern "C" U3X_FN int u3x_i01_canfocus(uint8_t *c) {
    if (!c) return 0;
    const uint8_t *table = rd<const uint8_t *>(c, 0);
    const uint64_t answer = game_call<uint64_t>(rd<uintptr_t>(table, 0x40), c);
    return (answer & 0xff) ? 1 : 0;
}

// 5.3: is this control one of the controls of the page that lie left of the party box
extern "C" U3X_FN int u3x_i01_leftctl(uint32_t pid, uint32_t id) {
    if (pid == 0x53cbb5f2u) return id == 0xf75ffe57u;
    if (pid == 0xb99a3235u)
        return id == 0x70e80076u || id == 0xef031de1u || id == 0x9247257au || id == 0x382fa1fdu || id == 0x002b2bd6u;
    return 0;
}

// 5.5: the focus of the user is in the party box
extern "C" U3X_FN int u3x_i01_user_party(void) {
    const g34b::PartyList &L = g34b::party_list();
    return L.in_box != 0 && L.last != nullptr && L.last == L.box;
}

// 5.1: a party row was clicked: the quick-action menu of that member
extern "C" U3X_FN void u3x_i01_click(uintptr_t base, int32_t *ev) {
    using namespace g34b;
    PartyList &L = party_list();
    uint8_t *target = static_cast<uint8_t *>(u3h_i05_state());

    target[0] = 0;
    L.d[4]++;
    u3x_i01_rows(base);
    at<uint64_t>(base + CLICK_WORD) = 0;

    const int32_t row = ev[1];
    if (row < 0 || static_cast<uint32_t>(row) >= L.row_count) return;
    const uint32_t entry = L.rows[row];
    const uint32_t index = entry & 0xff;
    if (index >= L.member_count) return;
    uint8_t *m = L.members[index];
    const bool guest = (entry & ROW_GUEST) != 0;

    // the selected player: the member, or its second player
    uint8_t *selected = &at<uint8_t>(base + SELECTED_ID);
    if (!guest) {
        copy_id(selected, m);
    } else {
        const uint8_t *lobby = u3x_i01_md(base, m);
        if (lobby) copy_id(selected, lobby + 0x19);
    }

    const uint8_t *net = &at<uint8_t>(base + NET);
    const bool lan = rd<uint8_t>(net, 8) != 0;
    const int own = u3x_i01_self(base, m);

    target[0] = (own && !guest) ? 1 : 2;
    copy_id(target + 1, selected);

    const uint8_t *session = &at<uint8_t>(base + PARTY_SESSION);
    QuickActions q;
    new_list(q);
    if (!own) {
        u3x_quick_action_add(&q, 0xffbbac12u, 0x6547d63cu);                                   // Voice
        if (u3x_session_host(session)) {
            if (!lan && !guest) u3x_quick_action_add(&q, 0x9fdf8887u, 0x2d32c8b0u);           // Promote to leader
            if (rd<int32_t>(net, 0x408) == 0) u3x_quick_action_add(&q, 0x1e2955afu, 0x7cc46b94u);   // Kick
        }
        u3x_quick_action_add(&q, 0x2962edc6u, 0x089143eau);                                   // Statistics
        if (!lan) u3x_quick_action_add(&q, 0xc5790997u, 0x0ecbd5bdu);                         // Report Player
    } else {
        if (u3x_session_host(session) || party_size(session) == 1) {
            if (at<uint8_t>(base + PARTY_LOCKED))
                u3x_quick_action_add(&q, 0x6253c96au, 0x0d0c91cdu);                           // Unlock Party
            else
                u3x_quick_action_add(&q, 0xa095f551u, 0xccbf832eu);                           // Lock Party
        }
        u3x_quick_action_add(&q, 0x2962edc6u, 0x089143eau);                                   // Statistics
    }

    L.d[5] = static_cast<uint32_t>(row) | (static_cast<uint32_t>(q.count) << 8) |
             (static_cast<uint32_t>(own) << 16) | (static_cast<uint32_t>(lan) << 24);
    u3h_fr_qa_push(base, &q);
}

// 5.4: the focus keeper: Right goes from the left controls into the party box, Left goes back
extern "C" U3X_FN void u3x_i01_focus(uintptr_t base, void *s) {
    using namespace g34b;
    using SetFocus = void (*)(uint8_t *page, uint8_t *control);
    PartyList &L = party_list();
    uint32_t &busy = *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(s) + 708);

    uint8_t *page = u3h_active_page_object(base);
    const SetFocus set_focus = rd<SetFocus>(s, 768);
    if (!page || !set_focus || busy) return;

    // once per game tick
    const uint32_t clock = at<uint32_t>(base + GAME_CLOCK);
    if (clock == L.tick) return;
    L.tick = clock;

    // page change
    const uint32_t pid = rd<uint32_t>(page, 0x10);
    if (pid != L.page) {
        if (L.page != 0 && L.last != nullptr && L.last == L.box) {   // left the old page while in the box
            L.remembered[L.remembered_next & 3] = L.page;
            L.remembered_next++;
        }
        L.page = pid;
        L.last = nullptr;
        L.previous = nullptr;
        L.box = nullptr;
        L.popup = 0;
        L.in_box = 0;
        L.alone = 0;
        for (uint32_t i = 0; i < 4; i++)
            if (L.remembered[i] == pid) {
                L.remembered[i] = 0;
                L.in_box = 1;
            }
    }

    // nothing is moved under a popup
    const uint8_t *popup = at<const uint8_t *>(base + MENU + 0x898);
    if (popup && rd<uint32_t>(popup, 0x10) != 0) {
        L.popup = 1;
        return;
    }
    L.popup = 0;

    const uint32_t n = rd<uint32_t>(page, 0xac);
    uint8_t *const *list = rd<uint8_t *const *>(page, 0xb0);
    if (!list || n > 64) return;

    uint8_t *box = nullptr, *profile = nullptr;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t *c = list[i];
        if (!c) continue;
        if (control_id(c) == BOX_ID) box = c;
        if (control_id(c) == PROFILE_ID) profile = c;
    }
    uint8_t *focused = focused_control(list, n);
    if (!box || rd<uint8_t>(page, 8) == 0) return;
    L.box = box;

    // the first other control that can take the focus
    uint8_t *first = nullptr;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t *c = list[i];
        if (!c || c == box || c == profile || control_id(c) == 0) continue;
        if (u3x_i01_canfocus(c)) {
            first = c;
            break;
        }
    }
    L.alone = first ? 0 : 1;

    // the control the user came from, when it is still usable (asked once per list position that holds it)
    bool previous_ok = false;
    for (uint32_t i = 0; i < n; i++)
        if (list[i] && list[i] == L.previous && u3x_i01_canfocus(list[i])) previous_ok = true;
    uint8_t *previous = previous_ok ? L.previous : nullptr;

    const uint32_t pressed = u3h_pad_pressed(base, 0);
    L.d[11] = (focused ? (control_id(focused) & 0xffff) : 0xffffu) | (pressed << 16);

    uint8_t *back = previous ? previous : first;
    uint8_t *target = nullptr;
    if (focused && focused != box && focused != profile && (pressed & PAD_RIGHT) &&
        u3x_i01_leftctl(pid, control_id(focused)) == 1) {
        L.in_box = 1;                       // into the box
        target = box;
        L.d[6]++;
    } else if (focused && focused == box && first && (pressed & PAD_LEFT)) {
        L.in_box = 0;                       // out of the box
        target = back;
        L.d[7]++;
    } else if (focused && focused == profile) {
        if (first) {                        // the profile box never keeps the focus
            L.in_box = 0;
            target = back;
        } else {
            target = box;
        }
        L.d[7]++;
    } else if (focused && focused == box && L.in_box == 0 && first) {
        target = back;                      // the game put the focus into the box by itself
        L.d[10]++;
    }

    if (target && target != focused) {
        busy = 1;
        set_focus(page, target);
        busy = 0;
        focused = target;
        uint8_t *now = focused_control(list, n);
        if (now) focused = now;
    }

    L.last = focused;
    if (focused && focused != box && focused != profile) L.previous = focused;
}

// 5.6: the list of report reasons, read from the script symbol of the game
extern "C" U3X_FN void u3x_report_popup(uintptr_t base, void *s) {
    using namespace g34b;
    const uint8_t *table = u3x_sym_table(s, 0xffac56f2u);
    const uint32_t n = u3x_report_rows(table);
    if (n == 0) return;
    const uint8_t *entries = rd<const uint8_t *>(table, 8);
    QuickActions q;
    new_list(q);
    for (uint32_t k = 0; k < n; k++) u3x_quick_action_add(&q, rd<uint32_t>(entries, k * 0x10), 0x7ebae89cu + k);
    game_call<void>(base + SHOW_QUICK_ACTIONS, &at<uint8_t>(base + MENU), &q, static_cast<int32_t>(1));
}
