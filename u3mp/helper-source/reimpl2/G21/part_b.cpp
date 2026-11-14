// G21 part B: the joiner's side of a party join. The "Joining Party..." popup waits for the friend's 0x10c answer,
// hands its room id to the game's party machine, watches the room join, checks that the friend is in the room, waits
// for the link to the party leader and opens the lobby (one retry, 20 s timeout). Written from
// spec/G21_party_join.md, sections 5.1 to 5.9.

extern "C" {
// the helper's C part and existing C++ functions (spec 2.2)
void u3h_show_dialog(uintptr_t base, void *s, uint32_t body, uint32_t title, uint32_t action, int32_t buttons,
                     uint32_t extra, uint32_t close);
int u3h_show(uintptr_t base, void *s, uint32_t page, int silent);
void u3h_fj_note(const char *a, const uint8_t *id, const char *b, int32_t v);
void u3h_so_note(const char *a, const char *nm, int32_t v, const char *b);
int32_t u3h_mq_send(uintptr_t base, const uint8_t *id, uint32_t type, const void *data, uint32_t length);
int32_t u3h_mq_find(uintptr_t base, uint32_t type, uint32_t *index, int remove);
int32_t u3h_mq_get_np(uintptr_t base, uint32_t index, uint8_t *npid, void *data, uint32_t *len);
void *u3h_join_state(void);
uint32_t u3h_members(uint8_t *session, uint8_t **out);
int u3x_party_leader(uintptr_t base);
uint8_t *u3h_gt_entry_by_id(uintptr_t base, int32_t id);
int32_t u3h_gt_conn_state(uintptr_t base, int32_t id);

// this part
uint8_t *u3x_fj_member(uint8_t *ps, const uint8_t *name);
void u3x_fj_fail(uintptr_t base, void *s, const char *why);
void u3x_pj_line(uintptr_t base, const char *what);
void u3x_pj_accept(uintptr_t base, uint64_t id, int flag);
void u3x_pj_giveup(uintptr_t base);
void u3x_pj_tick(uintptr_t base);
void u3x_fj_cb(uintptr_t base, void *s, int32_t *ev);
}

namespace g21b {
namespace {

// ---- game data (image offsets) and session fields
constexpr uintptr_t GAME_CLOCK = 0x17b4ba4;      // s32, 300 ticks per second
constexpr uintptr_t MENU = 0x17bd080;
constexpr uintptr_t NET_LAN = 0x17d5c60;         // u8
constexpr uintptr_t NET_ACTIVE = 0x17d5c61;      // u8 "multiplayer active"
constexpr uintptr_t NET_ERROR = 0x17d5c80;       // u8
constexpr uintptr_t MATCH_SESSION = 0x17d6420;
constexpr uintptr_t PARTY_SESSION = 0x17dc298;
constexpr uintptr_t CLOSE_POPUP = 0x652200;      // (menu, 0)
constexpr uintptr_t PARTY_STEP = 0x77d270;       // (party session)

constexpr uintptr_t S_BACKEND = 0x08;            // pointer
constexpr uintptr_t S_ROOM = 0x18;               // u64
constexpr uintptr_t S_CREATE_DONE = 0xa1;        // u8
constexpr uintptr_t S_JOIN_DONE = 0xb8;          // u8
constexpr uintptr_t S_JOIN_RESULT = 0xbc;        // s32
constexpr uintptr_t S_JOINED = 0xc0;             // u64
constexpr uintptr_t S_LEAVE_DONE = 0xc8;         // u8
constexpr uintptr_t S_MEMBERS = 0x680;           // ten slots
constexpr uintptr_t S_OWN_ID = 0x12e4;           // u32
constexpr uintptr_t S_HOST_ID = 0x12e8;          // u32
constexpr uintptr_t S_LEAVE_ASKED = 0x1334;      // u8
constexpr uintptr_t S_STATE = 0x1338;            // u32
constexpr uintptr_t S_WAITING_ID = 0x1340;       // u64
constexpr uintptr_t S_ID_FLAG = 0x1374;          // u8

constexpr uintptr_t MEMBER_SIZE = 0x120;
constexpr int MEMBER_SLOTS = 10;
constexpr uintptr_t M_IN_USE = 0x25;             // u8
constexpr uintptr_t M_INDEX = 0x28;              // s32
constexpr uintptr_t M_ID = 0x2c;                 // u32
constexpr uintptr_t M_CONNECTION = 0x34;         // s32

constexpr uint32_t FIVE_SECONDS = 1500, TWENTY_SECONDS = 6000, TWO_SECONDS = 600, ONE_SECOND = 300;

// ---- the join state block of the C part (spec 3.2)
struct Join {
    int32_t timeout;
    uint8_t answered;
    uint8_t waiting;
    uint8_t last_link;
    uint8_t retried;
    uint8_t seen;
    uint8_t pad_09[3];
    int32_t wait_since;
    int32_t noted_at;
    int32_t direct_since;
    uint8_t from[0x14];
    uint8_t pad_2c[4];
    uint8_t hint_flags;
    uint8_t hint_ok;
    uint8_t hint_rest[14];
    uint64_t pending_id;
    int32_t pending_since;
    uint8_t pending_phase;
    uint8_t pending_dropped;
    uint8_t pad_4e[2];
};
static_assert(sizeof(Join) == 0x50);
static_assert(__builtin_offsetof(Join, wait_since) == 0x0c && __builtin_offsetof(Join, from) == 0x18);
static_assert(__builtin_offsetof(Join, hint_flags) == 0x30 && __builtin_offsetof(Join, pending_id) == 0x40);
static_assert(__builtin_offsetof(Join, pending_phase) == 0x4c);

constexpr uint8_t SEEN_WAITING = 1, SEEN_ROOM = 2, SEEN_MATCH = 4;

enum class Room { pending, refused, joined, none };

// ---- log texts
U3X_RO const char t_state[] = ": party state ";
U3X_RO const char t_room[] = ", room ";
U3X_RO const char t_pending_id[] = ", pending id ";
U3X_RO const char t_join_done[] = ", join done/result/joined ";
U3X_RO const char t_create_done[] = ", create done ";
U3X_RO const char t_leave_done[] = ", leave done/asked ";
U3X_RO const char t_net_error[] = ", net error ";
U3X_RO const char t_we_lead[] = ", we lead ";
U3X_RO const char t_slash[] = "/";
U3X_RO const char t_minus[] = "-";

U3X_RO const char t_answer_in[] = "join: 0x10c in";
U3X_RO const char t_first_step[] = "join: after the first step";
U3X_RO const char t_party_state[] = "join: party state ";
U3X_RO const char t_earlier_join[] =
    " at the answer: an earlier join was never answered; dropped, the new id is joined now";
U3X_RO const char t_no_code[] = " at the answer: a state without code; reset to 0";
U3X_RO const char t_join_dropped[] =
    "join: the party room join was never answered: dropped (party state 5 -> 0); a room that still comes is left";
U3X_RO const char t_id_dropped[] = "join: given up with the id still waiting: id dropped, party state ";
U3X_RO const char t_id_by_game[] = "join: id set by the game itself (its own 0x10c step or a 0x116)";
U3X_RO const char t_id_taken[] = "join: id taken";
U3X_RO const char t_id_not_taken[] = "join: id NOT taken in 5 s";
U3X_RO const char t_forced[] = " for 5 s with an id waiting: forced on";
U3X_RO const char t_late_room[] = "join: a party room came after its join was dropped: left";
U3X_RO const char t_gave_up[] = "join: gave up after 20 s (PS3 timeout): ";
U3X_RO const char t_no_answer[] = "no 0x10c answer";
U3X_RO const char t_no_room[] = "the party room was not joined (no 305 for the party session)";
U3X_RO const char t_no_friend[] = "the friend is not in the room we got";
U3X_RO const char t_no_link[] =
    "the peer link to the party leader did not come up (see the peer link lines of both PCs)";
U3X_RO const char t_wait_room[] = "join: waiting for the party room (Join 304 sent)";
U3X_RO const char t_in_room[] = "join: in a party room: members ";
U3X_RO const char t_we_lead_it[] = ", we lead it ";
U3X_RO const char t_friend_in_it[] = ", the friend is in it ";
U3X_RO const char t_not_holding[] = "join: the room we got does not hold ";
U3X_RO const char t_ask_fresh[] = ": leaving it, asking for a fresh party (0x10b again)";
U3X_RO const char t_fail_again[] = "join: failed (again not in the friend's room)";
U3X_RO const char t_fail_server[] = "join: failed (the server refused the party room)";
U3X_RO const char t_link[] = "join: peer link to ";
U3X_RO const char t_link_up[] = ": up";
U3X_RO const char t_link_not_up[] = ": not up yet";
U3X_RO const char t_link_no_entry[] = ": no connection entry";
U3X_RO const char t_match_room[] =
    "join: our MATCH session got a room with the friend while the party session waits (see the 305 lines)";
U3X_RO const char t_leaving[] = "join: leaving the party room we got (nobody stays half joined)";
U3X_RO const char t_answer_from[] = "join: answer from ";
U3X_RO const char t_accepted[] = ": accepted";
U3X_RO const char t_refused[] = ": refused";
U3X_RO const char t_fail_refused[] = "join: failed (refused)";
U3X_RO const char t_lobby[] = "join: in the party lobby";

// ---- small accessors
template <typename T> [[gnu::always_inline]] inline T &field(uint8_t *p, uintptr_t offset) {
    return *reinterpret_cast<T *>(p + offset);
}
[[gnu::always_inline]] inline uint8_t *party(uintptr_t base) { return reinterpret_cast<uint8_t *>(base + PARTY_SESSION); }
[[gnu::always_inline]] inline int32_t clock_now(uintptr_t base) { return at<int32_t>(base + GAME_CLOCK); }
[[gnu::always_inline]] inline Join *join_state() { return static_cast<Join *>(u3h_join_state()); }
[[gnu::always_inline]] inline void note(const char *text) { u3h_so_note(text, nullptr, -1, nullptr); }

// clock values wrap; a difference is signed
[[gnu::always_inline]] inline int32_t ticks_since(int32_t now, int32_t then) {
    return static_cast<int32_t>(static_cast<uint32_t>(now) - static_cast<uint32_t>(then));
}
[[gnu::always_inline]] inline int32_t ticks_after(int32_t now, uint32_t ticks) {
    return static_cast<int32_t>(static_cast<uint32_t>(now) + ticks);
}

// this machine leads the session: our member id is the host's
[[gnu::always_inline]] inline bool leads(uint8_t *session) {
    return field<uint32_t>(session, S_OWN_ID) == field<uint32_t>(session, S_HOST_ID);
}

// a party state the game's machine has no code for
[[gnu::always_inline]] inline bool state_without_code(uint32_t state) { return state == 3 || state == 4 || state > 7; }

[[gnu::always_inline]] inline uint8_t *member_slot(uint8_t *session, int i) {
    return session + S_MEMBERS + MEMBER_SIZE * static_cast<uintptr_t>(i);
}

// the first member slot in use with this member id, or null
U3X_FN uint8_t *member_by_id(uint8_t *session, uint32_t id) {
    for (int i = 0; i < MEMBER_SLOTS; ++i) {
        uint8_t *m = member_slot(session, i);
        if (field<uint8_t>(m, M_IN_USE) && field<uint32_t>(m, M_ID) == id) return m;
    }
    return nullptr;
}

U3X_FN uint32_t members_in_use(uint8_t *session) {
    uint32_t n = 0;
    for (int i = 0; i < MEMBER_SLOTS; ++i)
        if (field<uint8_t>(member_slot(session, i), M_IN_USE)) ++n;
    return n;
}

U3X_FN Room room_status(uint8_t *session) {
    if (!field<uint8_t>(session, S_JOIN_DONE)) return Room::pending;
    if (field<int32_t>(session, S_JOIN_RESULT) < 0) return Room::refused;
    return field<uint64_t>(session, S_JOINED) ? Room::joined : Room::none;
}

U3X_FN void failed_dialog(uintptr_t base, void *s) {
    u3h_show_dialog(base, s, 0x4d972420u, 0xc6467f2du, 0xa8cda6e0u, 0, 0, 0xe1e040aau);
}

U3X_FN void party_step(uintptr_t base) { game_call<void>(base + PARTY_STEP, party(base)); }

// Leaves the room of a session through its backend: entry 7 (Leave) of the backend's function table, whose address
// is the first word of the backend object.
U3X_FN void backend_leave(uint8_t *backend, uint8_t *session) {
    uint8_t *table = field<uint8_t *>(backend, 0);
    game_call<void>(field<uintptr_t>(table, 7 * sizeof(uintptr_t)), backend, session);
}

// start watching a room id that waits for the party machine
U3X_FN void watch_id(Join *j, uint64_t id, int32_t now) {
    j->pending_id = id;
    j->pending_since = now;
    j->pending_phase = 1;
    j->pending_dropped = 0;
}

// ---- the party state line, with its own limits: texts and digits stop at 196 characters, a hex word is all or nothing
struct StateLine {
    char text[200];
    int length;
};
constexpr int LINE_FULL = 196, LINE_HEX_LIMIT = 186;

U3X_FN void put_text(StateLine &l, const char *t) {
    for (; *t && l.length < LINE_FULL; ++t) l.text[l.length++] = *t;
}

U3X_FN void put_number(StateLine &l, uint32_t v) {
    char digits[10];
    int n = 0;
    do {
        digits[n++] = static_cast<char>('0' + v % 10);
        v /= 10;
    } while (v);
    while (n && l.length < LINE_FULL) l.text[l.length++] = digits[--n];
}

U3X_FN void put_hex(StateLine &l, uint32_t v) {
    if (l.length > LINE_HEX_LIMIT) return;
    l.text[l.length++] = '0';
    l.text[l.length++] = 'x';
    for (int shift = 28; shift >= 0; shift -= 4) l.text[l.length++] = hexdig[(v >> shift) & 15];
}

// ---- the popup's steps

// 5.8: the 20 s are over
U3X_FN void timed_out(uintptr_t base, void *s, Room room, const uint8_t *mate) {
    Join *j = join_state();
    uint8_t *ses = party(base);

    game_call<void>(base + CLOSE_POPUP, reinterpret_cast<void *>(base + MENU), 0);

    // "answered" as it is now, after the popup closed
    const char *reason =
        !j->answered ? t_no_answer : room != Room::joined ? t_no_room : !mate ? t_no_friend : t_no_link;
    Line l;
    note(l.s(t_gave_up).s(reason).z());

    j->waiting = 0;
    u3x_pj_giveup(base);

    // a room was joined but the link never came up: nobody stays half joined ("answered" as it is after the give-up)
    if (j->answered && room == Room::joined && field<uint64_t>(ses, S_ROOM) != 0 && !leads(ses)) {
        field<uint8_t>(ses, S_LEAVE_ASKED) = 1;
        note(t_leaving);
        failed_dialog(base, s);
    }
}

// 5.9 step 5: our own party follows us into the friend's room
U3X_FN void tell_our_party(uintptr_t base, uint64_t room) {
    uint8_t *ses = party(base);
    uint8_t *list[MEMBER_SLOTS];
    const uint32_t n = u3h_members(ses, list);
    if (field<uint64_t>(ses, S_ROOM) == 0) return;
    uint8_t *me = member_by_id(ses, field<uint32_t>(ses, S_OWN_ID));
    if (!me) return;
    for (uint32_t k = 0; k < n; ++k) {
        const int32_t my_index = field<int32_t>(me, M_INDEX);   // read again for every member
        if (field<int32_t>(list[k], M_INDEX) != my_index) u3h_mq_send(base, list[k], 0x116, &room, 8);
    }
}

// 5.9: take the friend's 0x10c answer out of the game's message queue
U3X_FN void take_answer(uintptr_t base, void *s) {
    uint32_t index = 0;
    if (u3h_mq_find(base, 0x10c, &index, 0) != 0) return;

    uint8_t sender[0x24];
    alignas(8) uint8_t data[0x10];
    for (int i = 0; i < 0x24; ++i) sender[i] = 0;
    for (int i = 0; i < 0x10; ++i) data[i] = 0;
    uint32_t length = 0x10;
    if (u3h_mq_get_np(base, index, sender, data, &length) != 0) return;

    const uint64_t room = rd<uint64_t>(data, 0);
    u3h_fj_note(t_answer_from, sender, room ? t_accepted : t_refused, -1);
    if (!room) {
        u3x_fj_fail(base, s, t_fail_refused);
        return;
    }
    if (u3x_party_leader(base)) tell_our_party(base, room);

    Join *j = join_state();
    j->answered = 1;
    j->seen = 0;
    for (int i = 0; i < 0x14; ++i) j->from[i] = sender[i];

    u3x_pj_accept(base, room, data[8]);
    ++ext_ea()[108];
}

// 5.7 step 6: the party room is not there yet
U3X_FN void wait_for_room(uintptr_t base, int32_t now) {
    Join *j = join_state();
    if (!(j->seen & SEEN_WAITING)) {
        j->seen |= SEEN_WAITING;
        j->noted_at = now;
        note(t_wait_room);
    }
    if (!(j->seen & SEEN_MATCH) && ticks_since(now, j->noted_at) > static_cast<int32_t>(TWO_SECONDS)) {
        uint8_t *match = reinterpret_cast<uint8_t *>(base + MATCH_SESSION);
        if (field<uint64_t>(match, S_ROOM) != 0 && u3x_fj_member(match, j->from)) {
            j->seen |= SEEN_MATCH;
            note(t_match_room);
        }
    }
}

// 5.7 step 8: the room we joined does not hold the friend
U3X_FN void not_the_friends_room(uintptr_t base, void *s, int32_t now) {
    Join *j = join_state();
    if (ticks_since(now, j->noted_at) < static_cast<int32_t>(ONE_SECOND)) return;   // a member can be announced late

    field<uint8_t>(party(base), S_LEAVE_ASKED) = 1;
    if (j->retried) {
        u3x_fj_fail(base, s, t_fail_again);
        return;
    }

    // once: ask the friend for a fresh party
    j->retried = 1;
    u3h_fj_note(t_not_holding, j->from, t_ask_fresh, -1);
    uint8_t to[0x24];
    uint8_t request[0x10];
    for (int i = 0; i < 0x24; ++i) to[i] = i < 0x14 ? j->from[i] : 0;
    for (int i = 0; i < 0x10; ++i) request[i] = 0;
    request[9] = 0x4a;    // sent by this helper
    request[10] = 2;      // "that room was not yours: make a fresh party"
    u3h_mq_send(base, to, 0x10b, request, 0x10);

    j->answered = 0;
    j->seen = 0;
    j->timeout = ticks_after(now, TWENTY_SECONDS);
    j->wait_since = now;
}

// 5.7 steps 7 to 11: the party session has a room
U3X_FN void in_room(uintptr_t base, void *s, int32_t now, uint8_t *mate) {
    Join *j = join_state();
    uint8_t *ses = party(base);

    if (!(j->seen & SEEN_ROOM)) {
        j->seen |= SEEN_ROOM;
        j->noted_at = now;
        Line l;
        l.s(t_in_room).d(members_in_use(ses)).s(t_we_lead_it).d(leads(ses) ? 1 : 0);
        note(l.s(t_friend_in_it).d(mate ? 1 : 0).z());
    }
    if (!mate) {
        not_the_friends_room(base, s, now);
        return;
    }

    // the link to the party leader: the friend, or the host's slot when somebody else leads
    uint8_t *leader = mate;
    if (!leads(ses)) {
        uint8_t *host = member_by_id(ses, field<uint32_t>(ses, S_HOST_ID));
        if (host) leader = host;
    }
    const uint8_t *entry = u3h_gt_entry_by_id(base, field<int32_t>(leader, M_CONNECTION));
    const int32_t link = entry ? u3h_gt_conn_state(base, rd<int32_t>(entry, 0)) : -9;
    const uint8_t link_mark = static_cast<uint8_t>(link + 16);
    if (link_mark != j->last_link) {
        j->last_link = link_mark;
        u3h_fj_note(t_link, leader, link == 2 ? t_link_up : entry ? t_link_not_up : t_link_no_entry, -1);
    }
    if (link != 2) return;

    // done: the lobby
    u3h_show(base, s, 0x53cbb5f2u, 0);
    u3h_show(base, s, 0xb99a3235u, 0);
    ++ext_ea()[109];
    note(t_lobby);
    j->waiting = 0;
    if (j->hint_ok && (j->hint_flags & 1)) j->direct_since = now ? now : 1;
}

}  // namespace

// The entry points are defined inside the namespace so that the private names above win over any name of the file.

extern "C" {

// 5.1: the member slot in use whose name equals `name` (16 characters compared), or null
U3X_FN uint8_t *u3x_fj_member(uint8_t *ps, const uint8_t *name) {
    for (int i = 0; i < MEMBER_SLOTS; ++i) {
        uint8_t *m = member_slot(ps, i);
        if (!field<uint8_t>(m, M_IN_USE)) continue;
        int k = 0;
        while (k < 16 && m[k] == name[k] && m[k] != 0) ++k;
        if (k == 16 || m[k] == name[k]) return m;   // 16 equal characters, or both names end here
    }
    return nullptr;
}

// 5.2: the join failed: counter, dialog, log line, the join is over
U3X_FN void u3x_fj_fail(uintptr_t base, void *s, const char *why) {
    ++ext_ea()[110];
    failed_dialog(base, s);
    note(why);
    join_state()->waiting = 0;
}

// 5.3: one log line with the party session's state
U3X_FN void u3x_pj_line(uintptr_t base, const char *what) {
    uint8_t *ses = party(base);
    StateLine l;
    l.length = 0;
    put_text(l, what);
    put_text(l, t_state);
    put_number(l, field<uint32_t>(ses, S_STATE));
    put_text(l, t_room);
    put_hex(l, static_cast<uint32_t>(field<uint64_t>(ses, S_ROOM)));
    put_text(l, t_pending_id);
    put_hex(l, static_cast<uint32_t>(field<uint64_t>(ses, S_WAITING_ID)));
    put_text(l, t_join_done);
    put_number(l, field<uint8_t>(ses, S_JOIN_DONE));
    put_text(l, t_slash);
    const int32_t result = field<int32_t>(ses, S_JOIN_RESULT);
    if (result < 0) {
        put_text(l, t_minus);
        put_number(l, 0u - static_cast<uint32_t>(result));
    } else {
        put_number(l, static_cast<uint32_t>(result));
    }
    put_text(l, t_slash);
    put_number(l, field<uint64_t>(ses, S_JOINED) ? 1 : 0);
    put_text(l, t_create_done);
    put_number(l, field<uint8_t>(ses, S_CREATE_DONE));
    put_text(l, t_leave_done);
    put_number(l, field<uint8_t>(ses, S_LEAVE_DONE));
    put_text(l, t_slash);
    put_number(l, field<uint8_t>(ses, S_LEAVE_ASKED));
    put_text(l, t_net_error);
    put_number(l, at<uint8_t>(base + NET_ERROR));
    put_text(l, t_we_lead);
    put_number(l, static_cast<uint32_t>(u3x_party_leader(base)));
    l.text[l.length] = 0;
    note(l.text);
}

// 5.4: hand a room id to the game's party machine
U3X_FN void u3x_pj_accept(uintptr_t base, uint64_t id, int flag) {
    uint8_t *ses = party(base);
    u3x_pj_line(base, t_answer_in);

    // a join that has just finished is taken in first
    if (field<uint32_t>(ses, S_STATE) == 5 && field<uint8_t>(ses, S_JOIN_DONE)) party_step(base);

    // the machine reads the waiting id only in its start state: clear what would keep it away from there
    const uint32_t state = field<uint32_t>(ses, S_STATE);
    if (state == 5 || state_without_code(state)) {
        field<uint32_t>(ses, S_STATE) = state == 5 ? 1 : 0;
        u3h_so_note(t_party_state, nullptr, static_cast<int32_t>(state), state == 5 ? t_earlier_join : t_no_code);
    }

    field<uint64_t>(ses, S_WAITING_ID) = id;
    field<uint8_t>(ses, S_JOIN_DONE) = 0;
    field<uint8_t>(ses, S_ID_FLAG) = flag != 0;
    watch_id(join_state(), id, clock_now(base));

    party_step(base);
    u3x_pj_line(base, t_first_step);
}

// 5.6: give the join up
U3X_FN void u3x_pj_giveup(uintptr_t base) {
    uint8_t *ses = party(base);
    Join *j = join_state();

    if (field<uint64_t>(ses, S_WAITING_ID) != 0) {
        field<uint64_t>(ses, S_WAITING_ID) = 0;
        u3h_so_note(t_id_dropped, nullptr, static_cast<int32_t>(field<uint32_t>(ses, S_STATE)), nullptr);
    }
    // a room join that was never answered is abandoned; its room is left if it still comes
    if (field<uint32_t>(ses, S_STATE) == 5 && !field<uint8_t>(ses, S_JOIN_DONE)) {
        field<uint8_t>(ses, S_JOIN_DONE) = 1;
        field<int32_t>(ses, S_JOIN_RESULT) = 0;
        field<uint64_t>(ses, S_JOINED) = 0;
        field<uint32_t>(ses, S_STATE) = at<uint8_t>(base + NET_LAN) ? 1 : 0;
        j->pending_dropped = 1;
        note(t_join_dropped);
    }
    j->pending_phase = 0;
}

// 5.5: watch the room id that waits for the party machine
U3X_FN void u3x_pj_tick(uintptr_t base) {
    uint8_t *ses = party(base);
    uint8_t *backend = field<uint8_t *>(ses, S_BACKEND);
    if (!backend || !at<uint8_t>(base + NET_ACTIVE)) return;

    Join *j = join_state();
    const uint64_t waiting = field<uint64_t>(ses, S_WAITING_ID);
    const uint32_t state = field<uint32_t>(ses, S_STATE);
    const int32_t now = clock_now(base);

    if (waiting == 0) {
        if (j->pending_phase) {
            j->pending_phase = 0;
            u3x_pj_line(base, t_id_taken);
        }
        if (!j->pending_dropped) return;
        if (state != 0) {
            j->pending_dropped = 0;   // a party is being made or joined in the normal way
        } else if (field<uint64_t>(ses, S_ROOM) != 0) {
            j->pending_dropped = 0;   // the abandoned join's room came after all
            note(t_late_room);
            backend_leave(backend, ses);
        }
        return;
    }

    if (j->pending_phase == 0 || waiting != j->pending_id) {   // the game put an id there itself
        watch_id(j, waiting, now);
        u3x_pj_line(base, t_id_by_game);
        return;
    }
    if (j->pending_phase != 1 || ticks_since(now, j->pending_since) < static_cast<int32_t>(FIVE_SECONDS)) return;

    // the id has waited 5 s: report it once and push the machine on where it is stuck
    j->pending_phase = 2;
    u3x_pj_line(base, t_id_not_taken);
    if (state == 5) {
        field<uint32_t>(ses, S_STATE) = 1;
    } else if (state == 2) {
        field<uint8_t>(ses, S_CREATE_DONE) = 1;
        field<uint32_t>(ses, S_STATE) = 1;
    } else if (state == 7) {
        field<uint8_t>(ses, S_LEAVE_DONE) = 1;
    } else if (state_without_code(state)) {
        field<uint32_t>(ses, S_STATE) = 0;
    } else {
        return;   // 0, 1, 6: reported only
    }
    u3h_so_note(t_party_state, nullptr, static_cast<int32_t>(state), t_forced);
}

// 5.7: events of the "Joining Party..." popup
U3X_FN void u3x_fj_cb(uintptr_t base, void *s, int32_t *ev) {
    const int32_t event = ev[0];
    const int32_t now = clock_now(base);
    if (event != 0 && event != 6) return;

    Join *j = join_state();
    if (event == 0) {   // the popup opens
        j->answered = 0;
        j->seen = 0;
        j->timeout = ticks_after(now, TWENTY_SECONDS);
        return;
    }

    // every frame while the popup is up
    uint8_t *ses = party(base);
    const Room room = room_status(ses);
    uint8_t *mate = j->answered ? u3x_fj_member(ses, j->from) : nullptr;   // the friend's member slot

    if (now > j->timeout) {
        timed_out(base, s, room, mate);
    } else if (!j->answered) {
        take_answer(base, s);
    } else if (room == Room::refused) {
        u3x_fj_fail(base, s, t_fail_server);
    } else if (room != Room::joined) {
        wait_for_room(base, now);
    } else {
        in_room(base, s, now, mate);
    }
}

}  // extern "C"
}  // namespace g21b
