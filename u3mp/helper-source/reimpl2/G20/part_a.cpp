// G20 part A: the menu side of the party invites (spec G20_invites.md, sections 4.1 to 4.5): the rows of the
// Invites tab, the click on an invite row, Accept, the "let me join your party" request and the end of the
// "Creating Party..." popup. The invite manager (part B) is reached only through the C part's u3h_fi_... exports.

// ---- existing type of the C++ part (G18 2.2); drop this definition where the file already has it
struct QuickActions { uint32_t argument, page; int32_t count; uint32_t pairs[16]; };

// ---- functions of the C part, of other groups and of the shared tools (spec 2.2)
extern "C" {
int32_t u3h_fi_selected(void);
void u3h_fi_select(int32_t row);
int32_t *u3h_fi_list_selection(void);
uint64_t u3h_fi_time(int32_t i);
void u3h_fi_delete(uintptr_t base, int32_t row);
void u3h_fi_mark_seen(uintptr_t base, void *s);
void u3h_fi_refresh(uintptr_t base, void *s, int notify);
void u3h_fi_send(uintptr_t base);
const char *u3h_fi_ago(uintptr_t base, void *s, uint32_t seconds);
char *u3h_fr_buffer(void);
void u3h_so_note(const char *a, const char *nm, int32_t v, const char *b);
void u3h_fj_note(const char *a, const uint8_t *id, const char *b, int32_t v);
int u3h_show(uintptr_t base, void *s, uint32_t page, int silent);
void u3h_show_dialog(uintptr_t base, void *s, uint32_t body, uint32_t title, uint32_t action, int32_t buttons,
                     uint32_t extra, uint32_t close);
int u3h_key_ok(uintptr_t base, uint32_t key);
int u3h_gate_closed(uintptr_t base, int32_t *ev);
uint32_t u3h_fr_tab(uintptr_t base, void *s);
uint8_t *u3h_fr_ent(uintptr_t base, uint32_t i);
int u3h_fr_strcmp(uintptr_t base, const void *a, const void *b);
int32_t u3h_fr_state_icon(uintptr_t base, uint8_t *e);
void u3h_fr_icon22(int32_t *ev, int32_t v);
const char *u3h_text(uintptr_t base, void *s, uint32_t id);
int32_t u3h_mq_send(uintptr_t base, const uint8_t *id, uint32_t type, const void *data, uint32_t length);
void u3h_join_reset(void);
void u3h_join_waiting(uintptr_t base);
void u3h_wait_popup(uintptr_t base, uint32_t text, uintptr_t callback);
uint64_t u3x_network_usec();
int u3x_quick_action_add(QuickActions *q, uint32_t text, uint32_t action);

// this part
int u3x_invite_list(uintptr_t base, int32_t *ev, void *s);
void u3x_invite_click(uintptr_t base, void *s, const int32_t *ev);
void u3x_invite_accept(uintptr_t base, void *s, uintptr_t callback);
void u3x_send_join(uintptr_t base, const uint8_t *id, uint64_t party, uintptr_t callback);
void u3x_party_created(uintptr_t base, const int32_t *ev);
}

namespace g20a {

// ---- game data (image offsets, spec 3.6)
constexpr uintptr_t INVITES = 0x17d6088;        // the game's invite list: +0 count, +8 rows of 0x28, +0x198 busy
constexpr uintptr_t INVITES_BUSY = INVITES + 0x198;
constexpr uintptr_t FRIENDS = 0x17caeb0;        // the friend object: +0 count, +0xc8 busy marker
constexpr uintptr_t NET_MODE = 0x17d603c;       // the net menu mode
constexpr uintptr_t SELECTED_ID = 0x17c3871;    // the selected player's id
constexpr uintptr_t SELECTED_OTHER = 0x17c3898; // written as 0
constexpr uintptr_t SESSION = 0x17dc298;        // the party session: +0x18 party id, +0xc8 in a party
constexpr uintptr_t MENU = 0x17bd080;           // the menu: +0x898 the popup that is up

// ---- game functions (spec 3.7)
constexpr uintptr_t GAME_QUICK_MENU = 0x6528d0;
constexpr uintptr_t GAME_POPUP_CLOSE = 0x652200;

// ---- ids (spec 3.8)
constexpr uint32_t LIST_TAG = 0x4c414eb2;
constexpr uint32_t TAB_INVITES = 0x885ef7e9;
constexpr uint32_t ROW_ICON_SLOT = 0xf5a294f3;
constexpr uint32_t TEXT_NO_INVITES = 0x52237405;
constexpr uint32_t TEXT_JOINING = 0xe4e03363;
constexpr uint32_t PAGE_FRIEND_ACTIONS = 0xecc3219d;
constexpr uint32_t PAGE_LOBBY = 0xb99a3235;
constexpr uint32_t MESSAGE_JOIN = 0x10b;

constexpr int NAME_BYTES = 0x14;

// ---- the helper's own texts
U3X_RO const char t_none[] = "";
U3X_RO const char t_list[] = "Invites list: ";
U3X_RO const char t_rows[] = " rows";
U3X_RO const char t_other_tab[] = "Invites list: not the Invites tab (tab text ";
U3X_RO const char t_close[] = ")";
U3X_RO const char t_op[] = "Invites list: op ";
U3X_RO const char t_first_time[] = " asked (first time)";
U3X_RO const char t_asks_op[] = "Invites list asks op ";
U3X_RO const char t_row[] = ", row ";
U3X_RO const char t_row_from[] = "Invites list row from ";
U3X_RO const char t_friend[] = " (a friend)";
U3X_RO const char t_no_friend[] = " (not found among friends)";
U3X_RO const char t_join_asked[] = "join: asked ";
U3X_RO const char t_party_known[] = " (0x10b), party id known";
U3X_RO const char t_no_party[] = " (0x10b), no party id";

// ---- the list log: what was already written to the log
U3X_BSS static int32_t tab_marker_logged;
U3X_BSS static uint32_t operations_logged;
U3X_BSS static uint32_t row_keys_logged[8];
U3X_BSS static uint32_t row_key_count;

// row k of the game's invite list (k is not checked here)
U3X_FN static uint8_t *invite_row(uintptr_t base, int32_t k) {
    return reinterpret_cast<uint8_t *>(base + INVITES + 8) + static_cast<int64_t>(k) * 0x28;
}

// a 0x14-byte name field as a text: `to` gets the bytes and a NUL behind them (0x15 bytes)
U3X_FN static void name_text(char *to, const uint8_t *field) {
    for (int k = 0; k < NAME_BYTES; k++) to[k] = static_cast<char>(field[k]);
    to[NAME_BYTES] = 0;
}

U3X_FN static bool friends_busy(uintptr_t base) {
    return at<uint64_t>(base + FRIENDS + 0xc8) != 0;
}

// the friend entry whose id is `name`, or null; no comparison for an empty name or while the friends are busy
U3X_FN static uint8_t *friend_named(uintptr_t base, const char *name) {
    if (!name[0] || friends_busy(base)) return nullptr;
    for (uint32_t i = 0; i < at<uint32_t>(base + FRIENDS); i++) {
        uint8_t *entry = u3h_fr_ent(base, i);
        if (!entry[1]) continue;   // no id: skipped
        if (u3h_fr_strcmp(base, entry + 1, name) == 0) return entry;
    }
    return nullptr;
}

U3X_FN static void answer_text(int32_t *ev, const char *text) {
    *reinterpret_cast<const char **>(reinterpret_cast<uint8_t *>(ev) + 0x40) = text;
}

// operation 6: one log line whenever the tab or the number of rows is not the one logged last
U3X_FN static void log_tab(uintptr_t base, void *s) {
    const uint32_t tab = u3h_fr_tab(base, s);
    const uint32_t other = tab & 0xffff;
    const int32_t v = tab == TAB_INVITES ? static_cast<int32_t>(at<uint32_t>(base + INVITES))
                                         : static_cast<int32_t>(0xffffffffu - other);   // -1 - other
    const int32_t marker = static_cast<int32_t>(static_cast<uint32_t>(v) + 0x10002u);
    if (marker == tab_marker_logged) return;
    tab_marker_logged = marker;
    if (v >= 0) u3h_so_note(t_list, nullptr, v, t_rows);
    else u3h_so_note(t_other_tab, nullptr, static_cast<int32_t>(other), t_close);
}

// each operation 0 .. 31 is logged the first time it is asked
U3X_FN static void log_operation(int32_t operation) {
    if (operation < 0 || operation > 31) return;
    const uint32_t bit = 1u << operation;
    if (operations_logged & bit) return;
    operations_logged |= bit;
    u3h_so_note(t_op, nullptr, operation, t_first_time);
}

// the first 8 different row questions are logged
U3X_FN static void log_row(const int32_t *ev, int32_t operation, int32_t i, const char *name, bool found) {
    const uint32_t key = (found ? 1u : 0u) | 0x80u | ((static_cast<uint32_t>(i) & 0xffu) << 8) |
                         (static_cast<uint32_t>(operation) << 16) | ((static_cast<uint32_t>(ev[0x15]) & 0xffu) << 24);
    for (uint32_t k = 0; k < row_key_count; k++)
        if (row_keys_logged[k] == key) return;
    if (row_key_count >= 8) return;
    row_keys_logged[row_key_count++] = key;
    u3h_so_note(t_asks_op, nullptr, operation, t_row);
    u3h_so_note(nullptr, nullptr, i, nullptr);
    u3h_so_note(t_row_from, name, -1, found ? t_friend : t_no_friend);
}

// operation 0x17, the list is entered again: ask the service for a new list when nothing stands against it
U3X_FN static void list_entered(uintptr_t base, const int32_t *ev, void *s) {
    const uint8_t *popup = at<const uint8_t *>(base + MENU + 0x898);
    if (popup && rd<uint32_t>(popup, 0x10) != 0) return;
    if (ev[14] == 0) return;
    if (at<uint8_t>(base + INVITES_BUSY)) return;
    at<uint8_t>(base + INVITES_BUSY) = 1;
    u3h_fi_refresh(base, s, 0);
}

// the questions about one row (n = number of invites listed): only a friend's invite gets a name, an icon and an age
U3X_FN static void row_answer(uintptr_t base, int32_t *ev, void *s, int32_t operation, int32_t n) {
    const int32_t i = ev[1];
    if (i < 0 || i >= 10) return;

    char name[NAME_BYTES + 1];
    name_text(name, invite_row(base, i) + 0x10);
    uint8_t *inviter = friend_named(base, name);
    log_row(ev, operation, i, name, inviter != nullptr);
    if (!inviter) return;

    if (operation == 4) {
        if (static_cast<uint32_t>(ev[0x14]) == ROW_ICON_SLOT && n != 0)
            u3h_fr_icon22(ev, u3h_fr_state_icon(base, inviter));
        return;
    }
    if (operation != 2) return;

    const uint32_t column = static_cast<uint32_t>(ev[0x15]);
    if (column == 0) {
        if (n == 0) {
            answer_text(ev, u3h_text(base, s, TEXT_NO_INVITES));
        } else {
            char *b = u3h_fr_buffer();
            for (int k = 0; k <= NAME_BYTES; k++) b[k] = name[k];
            answer_text(ev, b);
        }
    } else if (column == 1 && n != 0) {
        // the age in whole seconds, 32-bit; an invite from the future counts as just sent
        const uint32_t now = static_cast<uint32_t>(u3x_network_usec() / 1000000u);
        const uint32_t sent = static_cast<uint32_t>(u3h_fi_time(i) / 1000000u);
        int32_t age = static_cast<int32_t>(now - sent);
        if (age < 0) age = 0;
        answer_text(ev, u3h_fi_ago(base, s, static_cast<uint32_t>(age)));
    }
}

}  // namespace g20a

extern "C" {

// 4.1: the list source of the Invites tab. 1 = request answered, 0 = not this list.
U3X_FN int u3x_invite_list(uintptr_t base, int32_t *ev, void *s) {
    using namespace g20a;
    const uint8_t *widget = rd<const uint8_t *>(ev, 0x98);
    if (!widget || rd<uint32_t>(widget, 0x1c) != LIST_TAG) return 0;
    if (u3h_gate_closed(base, ev)) return 1;

    const int32_t operation = ev[0];
    if (operation == 6) log_tab(base, s);
    if (u3h_fr_tab(base, s) != TAB_INVITES) {
        if (operation == 6) ev[1] = -1;
        return 1;
    }

    u3h_fi_mark_seen(base, s);
    const int32_t n = static_cast<int32_t>(at<uint32_t>(base + INVITES));
    ext_ea()[113]++;
    log_operation(operation);

    switch (operation) {
    case 6: {   // last index; an empty list still has row 0
        const int32_t last = static_cast<int32_t>(static_cast<uint32_t>(n) - 1u);
        ev[1] = last < 0 ? 0 : last;
        break;
    }
    case 1: answer_text(ev, t_none); break;
    case 5: ev[1] = *u3h_fi_list_selection(); break;
    case 10: *u3h_fi_list_selection() = ev[1]; break;
    case 8: ev[1] = 1; break;
    case 0x17: list_entered(base, ev, s); break;
    default: row_answer(base, ev, s, operation, n); break;
    }
    return 1;
}

// 4.2: an invite row is clicked: it becomes the selected player and the Join Party / Decline menu opens
U3X_FN void u3x_invite_click(uintptr_t base, void *s, const int32_t *ev) {
    using namespace g20a;
    if (at<uint32_t>(base + INVITES) == 0) return;
    if (at<uint32_t>(base + NET_MODE) == 1) {   // a match is running: not now
        u3h_show_dialog(base, s, 0xed3b546f, 0xc6467f2d, 0xa8cda6e0, 0, 0, 0xe1e040aa);
        return;
    }

    const int32_t row = ev[1];
    u3h_fi_select(row);
    const uint8_t *name = invite_row(base, row) + 0x10;
    uint8_t *selected = reinterpret_cast<uint8_t *>(base + SELECTED_ID);
    for (int k = 0; k < NAME_BYTES; k++) selected[k] = name[k];
    at<uint64_t>(base + SELECTED_OTHER) = 0;

    QuickActions q;
    q.argument = 0;
    q.page = PAGE_FRIEND_ACTIONS;
    q.count = 0;
    for (int k = 0; k < 16; k++) q.pairs[k] = 0;
    u3x_quick_action_add(&q, 0xb1600ce3, 0xd79bd5ed);   // Join Party
    u3x_quick_action_add(&q, 0xc6b89b8c, 0x39764828);   // Decline
    game_call<void>(base + GAME_QUICK_MENU, reinterpret_cast<void *>(base + MENU), &q, 1);
    ext_ea()[111]++;
}

// 4.3: Accept on the clicked invite: ask the inviter (when a friend) to let us in, then delete the invite
U3X_FN void u3x_invite_accept(uintptr_t base, void *s, uintptr_t callback) {
    using namespace g20a;
    const int32_t row = u3h_fi_selected();
    if (at<uint32_t>(base + INVITES) == 0 || row < 0) return;
    if (at<uint32_t>(base + NET_MODE) != 0) u3h_show(base, s, PAGE_LOBBY, 0);

    const uint8_t *invite = invite_row(base, row);
    if (u3h_key_ok(base, rd<uint32_t>(invite, 0))) {
        char name[NAME_BYTES + 1];
        name_text(name, invite + 0x10);
        if (name[0] && friends_busy(base)) return;   // the friend list is being rebuilt: the invite stays
        const uint8_t *inviter = friend_named(base, name);
        if (inviter) u3x_send_join(base, inviter + 1, rd<uint64_t>(invite, 8), callback);
    }

    at<uint8_t>(base + INVITES_BUSY) = 1;
    u3h_fi_delete(base, row);
    ext_ea()[112]++;
}

// 4.4: ask the player `id` to let us join the party
U3X_FN void u3x_send_join(uintptr_t base, const uint8_t *id, uint64_t party, uintptr_t callback) {
    using namespace g20a;
    u3h_join_reset();

    uint8_t message[0x10];
    for (int k = 0; k < 8; k++) message[k] = static_cast<uint8_t>(party >> (8 * k));
    for (int k = 8; k < 0x10; k++) message[k] = 0;
    message[9] = 0x4a;
    message[10] = 1;
    u3h_mq_send(base, id, MESSAGE_JOIN, message, 0x10);
    ext_ea()[107]++;

    u3h_fj_note(t_join_asked, id, party ? t_party_known : t_no_party, -1);
    u3h_join_waiting(base);
    u3h_wait_popup(base, TEXT_JOINING, callback);
}

// 4.5: the "Creating Party..." popup reports the party: leave our invite and close the popup
U3X_FN void u3x_party_created(uintptr_t base, const int32_t *ev) {
    using namespace g20a;
    if (ev[0] != 6) return;
    if (!at<uint8_t>(base + SESSION + 0xc8) || !at<uint64_t>(base + SESSION + 0x18)) return;
    u3h_fi_send(base);
    game_call<void>(base + GAME_POPUP_CLOSE, reinterpret_cast<void *>(base + MENU), 0);
}

}  // extern "C"
