// G20 part B: the invite manager. It asks the service for the invites left for us, filters the answer, fills the
// game's invite list, shows a HUD notice per new invite, keeps the "unseen" number and the "seen" mark, deletes an
// invite, leaves our own invite for another player and does the per-tick work.
// Written from spec/G20_invites.md (sections 3 and 4.6 to 4.12).

extern "C" {
// the C part
void *u3h_fi_state(void);
int32_t u3h_inv_list(uintptr_t base, const uint8_t *me, uint8_t *blobs, uint32_t max);
int32_t u3h_inv_delete(uintptr_t base, const uint8_t *me, const uint8_t *blob48);
int32_t u3h_inv_poll(uintptr_t base, int32_t ticket);
int32_t u3h_inv_send(uintptr_t base, const uint8_t *target, const uint8_t *blob48);
void u3h_so_inv_diag(uintptr_t base, uint32_t rows, uint32_t kept, uint32_t w1, uint32_t w2, uint32_t w3, uint32_t wf,
                     uint32_t bad, const uint8_t *first, uint32_t fw);
void u3h_so_note(const char *a, const char *nm, int32_t v, const char *b);
int u3h_key_ok(uintptr_t base, uint32_t key);
uint32_t u3h_fr_key(uintptr_t base);
uint8_t *u3h_fr_ent(uintptr_t base, uint32_t i);
int u3h_fr_strcmp(uintptr_t base, const void *a, const void *b);
const char *u3h_text(uintptr_t base, void *s, uint32_t id);
uint8_t *u3h_lg_profile(uintptr_t base, uint32_t user);
uint8_t *u3h_lg_rec(uintptr_t base, void *s, uint8_t *profile);
int u3h_publish(uintptr_t base, void *s, uint8_t *profile);
// existing C++ functions of other groups
int32_t u3x_queue_send(uintptr_t base, const uint8_t *id, uint32_t type, const void *data, uint32_t length);
int32_t u3x_queue_find(uint8_t *entries, uint32_t *count, uint32_t type, uint32_t *index, int remove);
uint64_t u3x_network_usec();
int u3x_ms_hud(uintptr_t base, int user, const char *txt, uint32_t type, int32_t level, const uint32_t *ic);
// this part
void u3x_fi_finish(uintptr_t base, void *s, int32_t rows);
void u3x_fi_tick(uintptr_t base, void *s);
uint64_t u3x_fi_rec_seen(uintptr_t base, void *s);
uint32_t u3x_fi_row_why(uintptr_t base, const uint8_t *b, uint64_t now);
int u3x_fi_is_friend(uintptr_t base, const uint8_t *nm20);
void u3x_fi_refresh(uintptr_t base, void *s, int32_t notify);
void u3x_fi_delete(uintptr_t base, int32_t i);
void u3x_fi_delete_done(uintptr_t base);
void u3x_fi_mark_seen(uintptr_t base, void *s);
void u3x_fi_send(uintptr_t base);
}

namespace g20b {

constexpr int NAME_SIZE = 0x14;      // a name field
constexpr int LIST_ROOM = 10;        // invites the game's list holds
constexpr uint32_t ANSWER_ROOM = 12; // records the answer buffer holds

// an invite record as the service delivers and takes it
struct Invite {
    uint32_t key;
    uint32_t unused0;
    uint64_t party;
    uint8_t name[NAME_SIZE];
    uint32_t unused1;
    uint64_t sent;   // network time, microseconds
};
static_assert(sizeof(Invite) == 0x30);

// the helper's invite state (block of the C part)
struct State {
    uint64_t time[LIST_ROOM];
    Invite record[LIST_ROOM];
    Invite answer[ANSWER_ROOM];
    int32_t clicked;
    int32_t selection;
    int32_t list_ticket;
    int32_t delete_ticket;
    int32_t notify;
    int32_t previous_count;
    int32_t deleting;
    int32_t unused;
    uint64_t previous_newest;
    uint64_t seen;
};
static_assert(sizeof(State) == 0x4a0 && __builtin_offsetof(State, answer) == 0x230 &&
              __builtin_offsetof(State, list_ticket) == 0x478 && __builtin_offsetof(State, seen) == 0x498);

// the game's invite list
struct Row {
    uint32_t key;
    uint32_t zero0;
    uint64_t party;
    uint8_t name[NAME_SIZE];
    uint32_t zero1;
};
struct List {
    uint32_t count;
    uint32_t unseen;
    Row row[LIST_ROOM];
    uint8_t busy;
};
static_assert(sizeof(Row) == 0x28 && __builtin_offsetof(List, busy) == 0x198);

// a name field as a text
struct Name {
    char text[NAME_SIZE + 1];
};

// image offsets
constexpr uintptr_t INVITE_LIST = 0x17d6088, OWN_NAME = 0x17d6010, SELECTED_ID = 0x17c3871, FRIENDS = 0x17caeb0,
                    SESSION = 0x17dc298, MESSAGE_COUNT = 0x1b6d3c0, MESSAGES = 0x1b6d3d0, GAME_CLOCK = 0x17b4ba4,
                    TEXT_SYSTEM = 0x17026b8, FORMAT_TEXT = 0xf32bd0;

constexpr uint32_t MESSAGE_INVITED = 0x11d;
constexpr uint32_t TEXT_INVITE_FROM = 0x7f845fbdu, TEXT_INVITE_SHORT = 0x82c61354u;
constexpr uint32_t USEC = 1000000, DAY_SECONDS = 86400;
constexpr int32_t STUCK_TICKS = 9000;   // 30 seconds of the game clock

U3X_RO const char note_listed[] = "invite in the list from ";
U3X_RO const char note_getting[] = "invite notice: getting the invite list";
U3X_RO const char note_busy[] = "invite notice: invite list busy, getting it when free";
U3X_RO const char note_stuck[] = "invite list was busy for 30 s with no request: cleared";
U3X_RO const uint32_t no_icon[3] = {0, 0, 0xffffffffu};   // the three icon words of a HUD message without icon

U3X_BSS static int32_t message_waiting;   // an invite message arrived, the list is still to be asked for
U3X_BSS static int32_t busy_since;        // game clock when busy was first seen without a request; 0 = not measuring

[[gnu::always_inline]] inline State *state() { return static_cast<State *>(u3h_fi_state()); }
[[gnu::always_inline]] inline List &list(uintptr_t base) { return at<List>(base + INVITE_LIST); }
[[gnu::always_inline]] inline const uint8_t *own_name(uintptr_t base) { return &at<uint8_t>(base + OWN_NAME); }

[[gnu::always_inline]] inline void copy_bytes(void *to, const void *from, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) static_cast<uint8_t *>(to)[i] = static_cast<const uint8_t *>(from)[i];
}
[[gnu::always_inline]] inline void zero_bytes(void *to, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) static_cast<uint8_t *>(to)[i] = 0;
}
[[gnu::always_inline]] inline void name_text(Name &out, const uint8_t *field) {
    copy_bytes(out.text, field, NAME_SIZE);
    out.text[NAME_SIZE] = 0;
}
// whole seconds of a microsecond time, low 32 bits
[[gnu::always_inline]] inline uint32_t seconds_of(uint64_t usec) { return static_cast<uint32_t>(usec / USEC); }

// an empty list has nothing unseen
[[gnu::always_inline]] inline void empty_has_none_unseen(List &io) {
    if (io.count == 0) io.unseen = 0;
}

// is the name field equal to our own name (up to our NUL, or all of it)
U3X_FN static bool same_name(const uint8_t *ours, const uint8_t *field) {
    for (int i = 0; i < NAME_SIZE; i++) {
        if (ours[i] != field[i]) return false;
        if (ours[i] == 0) return true;
    }
    return true;
}

// the HUD notice "You received an invite from <name>"
U3X_FN static void show_notice(uintptr_t base, void *s, const char *name) {
    char text[0x80];
    text[0] = 0;
    game_call<void, void *, char *, int, uint32_t, const char *, void *, void *>(
        base + FORMAT_TEXT, &at<uint8_t>(base + TEXT_SYSTEM), text, 0x80, TEXT_INVITE_FROM, name, nullptr, nullptr);
    int length = 0;
    while (length <= 34 && text[length]) length++;
    if (length > 34) {   // too long for the HUD: "<label>: <name>"
        int n = 0;
        const char *label = u3h_text(base, s, TEXT_INVITE_SHORT);
        if (label)
            while (n < 60 && label[n]) { text[n] = label[n]; n++; }
        text[n++] = ':';
        text[n++] = ' ';
        for (const char *c = name; *c && n < 0x7e; c++) text[n++] = *c;
        text[n] = 0;
    }
    u3x_ms_hud(base, 0, text, 0, 0, no_icon);
    ext_ea()[101]++;
}

}  // namespace g20b

// the profile's "seen" time
extern "C" U3X_FN uint64_t u3x_fi_rec_seen(uintptr_t base, void *s) {
    const uint8_t *record = u3h_lg_rec(base, s, u3h_lg_profile(base, 0));
    return static_cast<uint64_t>(rd<uint32_t>(record, 0x1610)) << 32 | rd<uint32_t>(record, 0x1614);
}

// why a delivered record is not listed: 0 listed, 1 not this game's key, 2 a day old, 3 our own
extern "C" U3X_FN uint32_t u3x_fi_row_why(uintptr_t base, const uint8_t *b, uint64_t now) {
    using namespace g20b;
    const Invite *invite = reinterpret_cast<const Invite *>(b);
    if (!u3h_key_ok(base, invite->key)) return 1;
    const int32_t age = static_cast<int32_t>(seconds_of(now) - seconds_of(invite->sent));
    if (age >= static_cast<int32_t>(DAY_SECONDS)) return 2;
    const uint8_t *ours = own_name(base);
    if (ours[0] && same_name(ours, invite->name)) return 3;
    return 0;
}

// is the sender one of the friends
extern "C" U3X_FN int u3x_fi_is_friend(uintptr_t base, const uint8_t *nm20) {
    using namespace g20b;
    Name name;
    name_text(name, nm20);
    if (!name.text[0]) return 0;
    if (at<uint64_t>(base + FRIENDS + 0xc8)) return 0;   // the friend list is being fetched
    for (uint32_t i = 0; i < at<uint32_t>(base + FRIENDS); i++) {
        const uint8_t *entry = u3h_fr_ent(base, i);
        if (!entry[1]) continue;
        if (u3h_fr_strcmp(base, entry + 1, name.text) == 0) return 1;
    }
    return 0;
}

// ask the service for the list
extern "C" U3X_FN void u3x_fi_refresh(uintptr_t base, void *s, int32_t notify) {
    using namespace g20b;
    State *st = state();
    List &io = list(base);
    st->seen = u3x_fi_rec_seen(base, s);
    st->previous_count = static_cast<int32_t>(io.count);
    st->previous_newest = io.count ? st->time[0] : 0;
    st->notify = notify;
    io.unseen = 0;
    io.busy = 1;
    st->list_ticket = u3h_inv_list(base, own_name(base), reinterpret_cast<uint8_t *>(st->answer), ANSWER_ROOM);
    ext_ea()[99]++;
    if (st->list_ticket < 0) {   // refused: an empty answer
        st->list_ticket = -1;
        u3x_fi_finish(base, s, 0);
    }
}

// the service's answer: `rows` records are in the answer buffer
extern "C" U3X_FN void u3x_fi_finish(uintptr_t base, void *s, int32_t rows) {
    using namespace g20b;
    State *st = state();
    List &io = list(base);
    const uint64_t now = u3x_network_usec();

    uint32_t listed = 0, dropped[4] = {0, 0, 0, 0}, strangers = 0, bad_key = 0, first_why = 0;
    const Invite *first_dropped = nullptr;
    for (int32_t j = 0; j < rows && listed < LIST_ROOM; j++) {
        const Invite *in = &st->answer[j];
        const uint32_t why = u3x_fi_row_why(base, reinterpret_cast<const uint8_t *>(in), now);
        if (why) {
            dropped[why & 3]++;
            if (!first_dropped) { first_dropped = in; first_why = why; }
            if (why == 1 && !bad_key) bad_key = in->key;
            continue;
        }
        if (!u3x_fi_is_friend(base, in->name)) strangers++;   // listed all the same
        copy_bytes(&st->record[listed], in, sizeof(Invite));
        st->time[listed] = in->sent;
        Row &row = io.row[listed];
        zero_bytes(&row, sizeof(Row));
        row.key = in->key;
        row.party = in->party;
        copy_bytes(row.name, in->name, NAME_SIZE);
        listed++;
    }
    io.count = listed;
    ext_ea()[100] = listed;
    u3h_so_inv_diag(base, rows < 0 ? 0 : static_cast<uint32_t>(rows), listed, dropped[1], dropped[2], dropped[3],
                    strangers, bad_key, reinterpret_cast<const uint8_t *>(first_dropped), first_why);

    Name name;
    for (uint32_t k = 0; k < listed; k++) {
        name_text(name, st->record[k].name);
        u3h_so_note(note_listed, name.text, -1, nullptr);
    }
    // a notice per invite newer than the newest one we had (not for the net-start request on an empty list)
    if (!(st->previous_count == 0 && st->notify != 0))
        for (uint32_t k = 0; k < listed; k++)
            if (st->time[k] > st->previous_newest) {
                name_text(name, st->record[k].name);
                show_notice(base, s, name.text);
            }
    uint32_t unseen = 0;
    for (uint32_t k = 0; k < listed; k++)
        if (st->time[k] > st->seen) unseen++;
    io.unseen = unseen;
    io.busy = 0;
}

// the player looks at the list: remember the newest invite's time in the profile
extern "C" U3X_FN void u3x_fi_mark_seen(uintptr_t base, void *s) {
    using namespace g20b;
    List &io = list(base);
    if (io.busy || io.count == 0) return;
    const uint64_t newest = state()->time[0];
    if (u3x_fi_rec_seen(base, s) >= newest) return;
    uint8_t *profile = u3h_lg_profile(base, 0);
    uint8_t *record = u3h_lg_rec(base, s, profile);
    *reinterpret_cast<uint32_t *>(record + 0x1610) = static_cast<uint32_t>(newest >> 32);
    *reinterpret_cast<uint32_t *>(record + 0x1614) = static_cast<uint32_t>(newest);
    io.unseen = 0;
    u3h_publish(base, s, profile);
    ext_ea()[103]++;
}

// leave our invite for the selected player and tell that player
extern "C" U3X_FN void u3x_fi_send(uintptr_t base) {
    using namespace g20b;
    Invite invite;
    zero_bytes(&invite, sizeof(Invite));
    invite.key = u3h_fr_key(base);
    if (at<uint8_t>(base + SESSION + 0xc8)) invite.party = at<uint64_t>(base + SESSION + 0x18);
    copy_bytes(invite.name, own_name(base), NAME_SIZE);
    invite.sent = u3x_network_usec();
    const uint8_t *target = &at<uint8_t>(base + SELECTED_ID);
    const int32_t result = u3h_inv_send(base, target, reinterpret_cast<const uint8_t *>(&invite));
    ext_ea()[result < 0 ? 106 : 105]++;
    u3x_queue_send(base, target, MESSAGE_INVITED, nullptr, 0);
}

// ask the service to delete listed invite i
extern "C" U3X_FN void u3x_fi_delete(uintptr_t base, int32_t i) {
    using namespace g20b;
    List &io = list(base);
    if (i < 0 || i >= static_cast<int32_t>(io.count)) {
        io.busy = 0;
        return;
    }
    State *st = state();
    io.busy = 1;
    st->deleting = i;
    st->delete_ticket = u3h_inv_delete(base, own_name(base), reinterpret_cast<const uint8_t *>(&st->record[i]));
    ext_ea()[102]++;
    if (st->delete_ticket < 0) {   // refused
        st->delete_ticket = -1;
        empty_has_none_unseen(io);
        io.busy = 0;
    }
}

// the delete was confirmed: take the invite out of the list
extern "C" U3X_FN void u3x_fi_delete_done(uintptr_t base) {
    using namespace g20b;
    State *st = state();
    List &io = list(base);
    const int32_t gone = st->deleting, count = static_cast<int32_t>(io.count);
    if (gone >= 0 && gone < count) {
        for (int32_t k = gone + 1; k < count; k++) {   // the later ones move one place down
            copy_bytes(&io.row[k - 1], &io.row[k], sizeof(Row));
            copy_bytes(&st->record[k - 1], &st->record[k], sizeof(Invite));
            st->time[k - 1] = st->time[k];
        }
        io.count = static_cast<uint32_t>(count - 1);
    }
    empty_has_none_unseen(io);
    io.busy = 0;
}

// once per helper tick
extern "C" U3X_FN void u3x_fi_tick(uintptr_t base, void *s) {
    using namespace g20b;
    State *st = state();
    List &io = list(base);

    // the list request
    if (st->list_ticket >= 0) {
        const int32_t answer = u3h_inv_poll(base, st->list_ticket);
        if (answer != -2) {
            st->list_ticket = -1;
            u3x_fi_finish(base, s, answer < 0 ? 0 : answer);
        }
    }
    // the delete request
    if (st->delete_ticket >= 0) {
        const int32_t answer = u3h_inv_poll(base, st->delete_ticket);
        if (answer != -2) {
            st->delete_ticket = -1;
            if (answer >= 0) {
                u3x_fi_delete_done(base);
            } else {
                empty_has_none_unseen(io);
                io.busy = 0;
            }
        }
    }
    // "you were invited" messages: take them all out of the queue
    bool invited = false;
    uint32_t index = 0;
    while (u3x_queue_find(&at<uint8_t>(base + MESSAGES), &at<uint32_t>(base + MESSAGE_COUNT), MESSAGE_INVITED, &index, 1) == 0)
        invited = true;
    if (invited) {
        if (io.busy && !message_waiting) u3h_so_note(note_busy, nullptr, -1, nullptr);
        message_waiting = 1;
    }
    // a busy flag with no request out is released after 30 seconds
    if (!io.busy) {
        busy_since = 0;
    } else if (st->list_ticket < 0 && st->delete_ticket < 0) {
        const int32_t clock = at<int32_t>(base + GAME_CLOCK);
        if (busy_since == 0) {
            busy_since = clock;
        } else if (static_cast<int32_t>(static_cast<uint32_t>(clock) - static_cast<uint32_t>(busy_since)) > STUCK_TICKS) {
            io.busy = 0;
            busy_since = 0;
            u3h_so_note(note_stuck, nullptr, -1, nullptr);
        }
    }
    // an invite message waits and the list is free: get the list
    if (message_waiting && !io.busy) {
        message_waiting = 0;
        u3h_so_note(note_getting, nullptr, -1, nullptr);
        io.busy = 1;
        u3x_fi_refresh(base, s, 0);
        ext_ea()[104]++;
    }
}
