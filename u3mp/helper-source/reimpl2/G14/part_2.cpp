// G14 part 2: the online backend's slots for the life of a room: Create (4), Search (5), Join (6), Leave (7) and
// Kick (9). Each one turns a request of the game into one room message for the session service.
// Written from the behaviour description G14_online_backend_out.md (sections 3 and 5).

extern "C" {
// part 1 of this group
void u3x_onb_note(int slot);
int u3x_onb_bound(void *be, uint8_t *ss);
uint32_t u3x_onb_cookie(void *be, uint8_t *ss);
int32_t u3x_onb_send(const void *msg, uint32_t n);
void u3x_onb_wire(uintptr_t base, const char *what, uint8_t *ss, uint64_t room, const char *b, int32_t v);
void u3x_onb_name_time(uintptr_t base, char *out);
void u3x_onb_geo(uintptr_t base, uint16_t *a, uint16_t *b);
// the C part
uint8_t *u3h_onb_msg(void);
uint64_t *u3h_onb_pj(void);
uint64_t *u3h_lnb_match_room(void);
const char *u3h_fw_sess(uintptr_t base, uint8_t *ss);
void u3h_fw_line(const char *a, uint32_t hex, int digits, const char *b, int32_t v);
uint32_t u3h_fw_game_ra(uintptr_t base, uintptr_t ra, uintptr_t fp);
int32_t u3h_fw_host_link(uintptr_t base);
// other groups
uint8_t *u3x_find_member(uint8_t *session, uint32_t id);
uint8_t *u3x_entry_by_id(uintptr_t base, int32_t id);
int32_t u3x_connection_state(uintptr_t base, int32_t id);

int64_t u3x_onb_s4(void *be, uint8_t *ss, uint32_t cap, uint32_t a4, uint32_t a5, uint32_t a6);
int64_t u3x_onb_s5(void *be, uint8_t *ss, uint32_t a3, uint32_t a4, int32_t level, int32_t below, int32_t above, uint32_t a8);
int64_t u3x_onb_s6(void *be, uint8_t *ss, uint64_t room);
int64_t u3x_onb_s7(void *be, uint8_t *ss);
int64_t u3x_onb_s9(void *be, uint8_t *ss, uint32_t id, uint32_t flag);
}

namespace g14_room_life {

// ---- the helper's log texts
U3X_RO const char t_create[] = "Create (303): ";
U3X_RO const char t_old[] = " (the old one), cookie ";
U3X_RO const char t_join[] = "Join (304) asked by the ";
U3X_RO const char t_cookie[] = ", cookie ";
U3X_RO const char t_leave[] = "Leave (307) of the ";
U3X_RO const char t_game[] = " room asked by the game at ";
U3X_RO const char t_outside[] = " room asked from outside the game image (helper or runtime), first return ";
U3X_RO const char t_state[] = "  ... net state then ";
U3X_RO const char t_link[] = ", link to the match host + 10: ";
U3X_RO const char t_kick[] = "Kick (311) of member ";
U3X_RO const char t_from[] = " from the ";
U3X_RO const char t_entry[] = " room, its link state+1 ";
U3X_RO const char t_flags[] = ", entry/active/flag5/flag6 ";
U3X_RO const char t_slash[] = "/";
U3X_RO const char t_asked[] = ", asked at ";
U3X_RO const char t_gone[] = "Kick (311) asked for an id that is not in the room: not sent; so far ";
U3X_RO const char t_lead[] = "Kick (311) not sent: this PC does not lead that room, or the member is this PC; so far ";

static_assert(sizeof t_create == 14 + 1 && sizeof t_old == 23 + 1 && sizeof t_join == 24 + 1 && sizeof t_cookie == 9 + 1);
static_assert(sizeof t_leave == 19 + 1 && sizeof t_game == 27 + 1 && sizeof t_outside == 74 + 1);
static_assert(sizeof t_state == 21 + 1 && sizeof t_link == 31 + 1 && sizeof t_kick == 21 + 1 && sizeof t_from == 10 + 1);
static_assert(sizeof t_entry == 24 + 1 && sizeof t_flags == 27 + 1 && sizeof t_slash == 1 + 1 && sizeof t_asked == 11 + 1);
static_assert(sizeof t_gone == 69 + 1 && sizeof t_lead == 87 + 1);

// ---- refused kicks, counted for the whole run
U3X_BSS uint32_t kicks_not_in_room;   // the member id is not in the room
U3X_BSS uint32_t kicks_not_ours;      // this PC does not lead the room, or the member is this PC

// ---- image offsets
constexpr uintptr_t G_NET_STATE = 0x1b68810;       // u32: the game's net state word
constexpr uintptr_t G_MATCH_SESSION = 0x17d6420;   // the match session object
constexpr uintptr_t G_CLEAR_MEMBERS = 0x7a25f0;    // game function (session)

// ---- session fields
constexpr uintptr_t S_POOL = 0x14, S_ROOM = 0x18, S_CREATE_DONE = 0xa1, S_CREATE_RESULT = 0xa4, S_SEARCH_DONE = 0xb0,
                    S_SEARCH_RESULT = 0xb4, S_JOIN_DONE = 0xb8, S_JOIN_RESULT = 0xbc, S_LEAVE_DONE = 0xc8,
                    S_LEAVE_RESULT = 0xcc, S_GAME_TYPE = 0xf8, S_CAPACITY = 0x20c, S_SEARCH_COUNT = 0x218,
                    S_OWN_ID = 0x12e4, S_HOST_ID = 0x12e8, S_ATTRIBUTE = 0x12ec, S_DATA_LENGTH = 0x12f0, S_DATA = 0x12f4;
constexpr uint32_t DATA_BYTES = 64;                // size of the own member data
constexpr uintptr_t BE_BASE = 0x1058;              // backend object: the game image base
constexpr uint32_t GAME_TYPE_BIT15 = 0x8000;

// a field of a session or of a message
template <typename T> [[gnu::always_inline]] inline T &field(uint8_t *p, uintptr_t off) {
    return *reinterpret_cast<T *>(p + off);
}

[[gnu::always_inline]] inline uintptr_t base_of(void *be) { return static_cast<uintptr_t>(rd<uint64_t>(be, BE_BASE)); }

// ---- a built log line: at most 120 characters, what does not fit is dropped
constexpr uint32_t LINE_MAX = 120;
struct Text {
    char c[LINE_MAX + 1];
    uint32_t n;
};

// appends the characters of t up to its NUL, at most `most` of them
U3X_FN void add_text(Text &line, const char *t, uint32_t most) {
    for (uint32_t i = 0; i < most && t[i] && line.n < LINE_MAX; i++) line.c[line.n++] = t[i];
}

// appends v in decimal
U3X_FN void add_number(Text &line, uint32_t v) {
    char digit[10];
    uint32_t count = 0;
    do {
        digit[count++] = static_cast<char>('0' + v % 10);
        v /= 10;
    } while (v);
    while (count && line.n < LINE_MAX) line.c[line.n++] = digit[--count];
}

[[gnu::always_inline]] inline const char *finish(Text &line) {
    line.c[line.n] = 0;
    return line.c;
}

// ---- messages: the first `size` bytes of the shared buffer zeroed, the type stored
U3X_FN uint8_t *new_message(uint32_t type, uint32_t size) {
    uint8_t *m = u3h_onb_msg();
    for (uint32_t i = 0; i < size; i++) m[i] = 0;
    field<uint32_t>(m, 0) = type;
    return m;
}

// all 64 bytes of the session's own member data into a message
U3X_FN void copy_member_data(uint8_t *to, uint8_t *ss) {
    for (uint32_t i = 0; i < DATA_BYTES; i++) to[i] = ss[S_DATA + i];
}

U3X_FN void clear_members(uintptr_t base, uint8_t *ss) { game_call<void>(base + G_CLEAR_MEMBERS, ss); }

// a kick that is not sent: counted, and logged on the first, the 1801st, ... time
U3X_FN void refuse_kick(uint32_t &counter, const char *text) {
    const uint32_t before = counter++;
    if (before % 1800 == 0) u3h_fw_line(text, 0, 0, nullptr, static_cast<int32_t>(counter));
}

}  // namespace g14_room_life

// ---- slot 4, Create
extern "C" U3X_FN int64_t u3x_onb_s4(void *be, uint8_t *ss, uint32_t cap, uint32_t a4, uint32_t a5, uint32_t a6) {
    using namespace g14_room_life;
    u3x_onb_note(4);
    ext_eb()[138] += 1;
    const int k = u3x_onb_bound(be, ss);
    if (k < 0) return -1;
    const uintptr_t base = base_of(be);
    const uint32_t cookie = static_cast<uint32_t>(k) + 1;

    u3h_onb_pj()[k] = 0;
    u3x_onb_wire(base, t_create, ss, field<uint64_t>(ss, S_ROOM), t_old, static_cast<int32_t>(cookie));
    clear_members(base, ss);

    uint8_t *m = new_message(303, 232);
    field<uint32_t>(m, 8) = cookie;
    field<uint32_t>(m, 12) = field<uint32_t>(ss, S_POOL);
    field<uint32_t>(m, 20) = field<uint32_t>(ss, S_GAME_TYPE) | (a6 ? GAME_TYPE_BIT15 : 0);
    field<uint16_t>(m, 28) = static_cast<uint16_t>(a5);
    u3x_onb_geo(base, &field<uint16_t>(m, 32), &field<uint16_t>(m, 34));
    field<uint16_t>(m, 36) = static_cast<uint16_t>(cap);
    m[38] = static_cast<uint8_t>(field<uint32_t>(ss, S_DATA_LENGTH));
    m[39] = a4 ? 4 : 0;
    u3x_onb_name_time(base, reinterpret_cast<char *>(m + 40));
    copy_member_data(m + 168, ss);

    ss[S_ATTRIBUTE] = 0;
    field<uint32_t>(ss, S_CAPACITY) = cap;
    ss[S_CREATE_DONE] = 0;
    field<uint32_t>(ss, S_CREATE_RESULT) = 0;
    u3x_onb_send(m, 232);
    return 0;
}

// ---- slot 5, Search (sent also for a session that is not attached, then with cookie 0)
extern "C" U3X_FN int64_t u3x_onb_s5(void *be, uint8_t *ss, uint32_t a3, uint32_t, int32_t level, int32_t below, int32_t above,
                                     uint32_t a8) {
    using namespace g14_room_life;
    u3x_onb_note(5);
    ext_eb()[140] += 1;
    const uintptr_t base = base_of(be);

    uint8_t *m = new_message(309, 36);
    field<uint32_t>(m, 8) = u3x_onb_cookie(be, ss);
    field<uint32_t>(m, 12) = field<uint32_t>(ss, S_POOL);
    field<uint32_t>(m, 16) = field<uint32_t>(ss, S_GAME_TYPE) | (a8 ? GAME_TYPE_BIT15 : 0);
    u3x_onb_geo(base, &field<uint16_t>(m, 20), &field<uint16_t>(m, 22));
    field<uint16_t>(m, 24) = static_cast<uint16_t>(a3);
    if (level >= 0) {   // a range of levels around the given one (32-bit sums that wrap)
        const int32_t lowest = static_cast<int32_t>(static_cast<uint32_t>(level) - static_cast<uint32_t>(below));
        const int32_t highest = static_cast<int32_t>(static_cast<uint32_t>(level) + static_cast<uint32_t>(above));
        field<uint16_t>(m, 28) = static_cast<uint16_t>(lowest > 0 ? lowest : 0);
        field<uint16_t>(m, 30) = static_cast<uint16_t>(highest);
    }

    field<uint32_t>(ss, S_SEARCH_COUNT) = 0;
    u3x_onb_send(m, 36);
    ss[S_SEARCH_DONE] = 0;
    field<uint32_t>(ss, S_SEARCH_RESULT) = 0;
    return 0;
}

// ---- slot 6, Join
extern "C" U3X_FN int64_t u3x_onb_s6(void *be, uint8_t *ss, uint64_t room) {
    using namespace g14_room_life;
    u3x_onb_note(6);
    ext_eb()[139] += 1;
    const int k = u3x_onb_bound(be, ss);
    if (k < 0) return -1;
    const uintptr_t base = base_of(be);
    const uint32_t cookie = static_cast<uint32_t>(k) + 1;

    u3h_onb_pj()[k] = room;
    u3x_onb_wire(base, t_join, ss, room, t_cookie, static_cast<int32_t>(cookie));
    clear_members(base, ss);

    uint8_t *m = new_message(304, 88);
    field<uint32_t>(m, 8) = cookie;
    m[12] = static_cast<uint8_t>(field<uint32_t>(ss, S_DATA_LENGTH));
    field<uint64_t>(m, 16) = room;
    copy_member_data(m + 24, ss);

    field<uint32_t>(ss, S_JOIN_RESULT) = 0;
    ss[S_JOIN_DONE] = 0;
    u3x_onb_send(m, 88);
    return 0;
}

// ---- slot 7, Leave. Its two log lines name the caller and are written whether or not the session is attached.
extern "C" U3X_FN int64_t u3x_onb_s7(void *be, uint8_t *ss) {
    using namespace g14_room_life;
    const uintptr_t return_address = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    const uintptr_t frame_address = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
    u3x_onb_note(7);
    ext_eb()[141] += 1;
    const uintptr_t base = base_of(be);

    const uint32_t game_caller = u3h_fw_game_ra(base, return_address, frame_address);
    Text line;
    line.n = 0;
    add_text(line, t_leave, ~0u);
    add_text(line, u3h_fw_sess(base, ss), ~0u);
    add_text(line, game_caller ? t_game : t_outside, ~0u);
    if (game_caller)
        u3h_fw_line(finish(line), game_caller, 7, nullptr, -1);
    else
        u3h_fw_line(finish(line), static_cast<uint32_t>(return_address - base), 8, nullptr, -1);
    const int32_t host_link = u3h_fw_host_link(base);
    u3h_fw_line(t_state, at<uint32_t>(base + G_NET_STATE), 2, t_link, host_link + 10);

    const int k = u3x_onb_bound(be, ss);
    if (k < 0) return -1;
    u3h_onb_pj()[k] = 0;
    const uint64_t room = field<uint64_t>(ss, S_ROOM);
    ss[S_LEAVE_DONE] = 1;
    if (ss == reinterpret_cast<uint8_t *>(base + G_MATCH_SESSION)) *u3h_lnb_match_room() = 0;
    if (!room) return 0;

    field<uint32_t>(ss, S_LEAVE_RESULT) = 0;
    uint8_t *m = new_message(307, 16);
    field<uint64_t>(m, 8) = room;
    u3x_onb_send(m, 16);
    field<uint64_t>(ss, S_ROOM) = 0;
    clear_members(base, ss);
    return 0;
}

// ---- slot 9, Kick. Only the room's leader kicks, and never itself; the member leaves when the service answers.
extern "C" U3X_FN int64_t u3x_onb_s9(void *be, uint8_t *ss, uint32_t id, uint32_t flag) {
    using namespace g14_room_life;
    const uintptr_t return_address = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    const uintptr_t frame_address = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
    u3x_onb_note(9);
    if (u3x_onb_bound(be, ss) < 0) return -1;
    const uintptr_t base = base_of(be);

    uint8_t *member = u3x_find_member(ss, id);
    if (!member) {
        refuse_kick(kicks_not_in_room, t_gone);
        return 0;
    }
    const uint32_t own_id = field<uint32_t>(ss, S_OWN_ID);
    if (own_id != field<uint32_t>(ss, S_HOST_ID) || id == own_id) {
        refuse_kick(kicks_not_ours, t_lead);
        return 0;
    }

    // the diagnostic line: who is kicked, the state of the link to that member, who asked
    const int32_t connection = field<int32_t>(member, 0x34);
    const uint8_t *entry = u3x_entry_by_id(base, connection);
    Text line;
    line.n = 0;
    add_text(line, t_kick, ~0u);
    add_text(line, reinterpret_cast<const char *>(member), 16);
    add_text(line, t_from, ~0u);
    add_text(line, u3h_fw_sess(base, ss), ~0u);
    add_text(line, t_entry, ~0u);
    add_number(line, static_cast<uint32_t>(u3x_connection_state(base, connection)) + 1);
    add_text(line, t_flags, ~0u);
    add_number(line, entry ? 1 : 0);
    for (uintptr_t off = 4; off <= 6; off++) {
        add_text(line, t_slash, ~0u);
        add_number(line, entry ? rd<uint8_t>(entry, off) : 0);
    }
    add_text(line, t_asked, ~0u);
    const uint32_t game_caller = u3h_fw_game_ra(base, return_address, frame_address);
    u3h_fw_line(finish(line), game_caller, 7, nullptr, -1);

    const uint64_t room = field<uint64_t>(ss, S_ROOM);
    if (!room) return 0;
    uint8_t *m = new_message(311, 16);
    field<uint16_t>(m, 4) = static_cast<uint16_t>(id);
    field<uint16_t>(m, 6) = static_cast<uint8_t>(flag);
    field<uint64_t>(m, 8) = room;
    u3x_onb_send(m, 16);
    return 0;
}
