// G13 part 2: the online backend's room messages in (member link, member record, one received message, the Update
// slot, the ping clock). Written from spec/G13_online_backend_in.md (sections 5.1 to 5.15).

extern "C" {
// other groups, the C part and the shared helpers of the sending slots (spec 2.3)
int32_t u3x_add_member(uintptr_t base, uint8_t *session, const void *info, int you, int host);
int32_t u3x_remove_member(uintptr_t base, uint8_t *session, const char *name);
uint8_t *u3x_find_member(uint8_t *session, uint32_t id);
void u3h_gt_stamp(uintptr_t base, uint8_t *sub, int off);
int u3h_onb_pl_np(uintptr_t base, uint8_t *entry, const char *name);
uint64_t *u3h_onb_pj(void);
uint64_t *u3h_lnb_match_room(void);
const char *u3h_fw_sess(uintptr_t base, uint8_t *ss);
void u3h_fw_line(const char *a, uint32_t hex, int digits, const char *b, int32_t v);
void u3h_cg_log(const char *a, int32_t v);
void u3x_fl_md(void *lan_be, uint8_t *ps);
uint8_t *u3x_onb_by_cookie(void *be, uint32_t cookie);
uint8_t *u3x_onb_by_room(void *be, uint64_t room);
void u3x_onb_strcpy(char *d, const char *s, uint32_t n);
void u3x_onb_note(int slot);
void u3x_onb_wire(uintptr_t base, const char *what, uint8_t *ss, uint64_t room, const char *b, int32_t v);
int32_t u3x_onb_send(const void *msg, uint32_t n);
int32_t u3h_smn_pull(uint8_t *rx, uint32_t *len, uint32_t cap);
uint32_t u3h_smn_len(const uint8_t *m, uint32_t n);
uint32_t u3h_smn_swap(uint8_t *m, uint32_t len);
void u3h_smn_got(const uint8_t *m, uint32_t id);
void u3h_smn_unknown(const uint8_t *m, uint32_t n);
int u3h_smn_up(void);
int u3h_ol_up(void);

// this part (spec 2.2)
void u3x_onb_member_link(uintptr_t base, uint8_t *ss, const char *name);
void u3x_onb_peer_begin(uintptr_t base, uint8_t *ss);
void u3x_onb_info(void *in, const uint8_t *r);
uint32_t u3x_onb_msg_in(void *be);
int64_t u3x_onb_s1(void *be);
void u3x_onb_ping_restart(void *be);
int u3x_onb_ping_due(void *be);
}

namespace g13p2 {

// ---- game data (image offsets)
constexpr uintptr_t LOCAL_CLOCK = 0x17b4ba4;     // s32, ticks of 1/300 s
constexpr uintptr_t NET_MENU_MODE = 0x17d603c;   // u32, 2 = Custom Game
constexpr uintptr_t MATCH_SESSION = 0x17d6420;
constexpr uintptr_t PARTY_SESSION = 0x17dc298;
constexpr uintptr_t CONN_TABLE = 0x17f31b0;
constexpr uintptr_t NET_STATE = 0x1b68810;       // u32, only printed
constexpr uintptr_t CLEAR_MEMBERS = 0x7a25f0;    // game function (session)

// ---- connection table and its entries
constexpr uintptr_t TABLE_ENTRIES = 8, ENTRY_SIZE = 0x220, TABLE_OWN_NAME = 0x4d90, TABLE_LAN_FLAG = 0x4db4;
constexpr int ENTRY_COUNT = 32;
constexpr uintptr_t ENTRY_USED = 4, ENTRY_NAME = 7, ENTRY_HANDLE = 0x34, ENTRY_SUB = 0x40;
constexpr int SUB_LAST_HEARD = 0xa8;

// ---- backend object
constexpr uintptr_t BE_BINDINGS = 0x30, BE_PING_TIME = 0x50, BE_COUNT = 0x54, BE_BUFFER = 0x58, BE_BASE = 0x1058;
constexpr uint32_t BUFFER_SIZE = 0x1000;

// ---- session object
constexpr uintptr_t SS_ROOM = 0x18, SS_ROOM_NAME = 0x20, SS_CREATE_DONE = 0xa1, SS_SEARCH_DONE = 0xb0,
                    SS_SEARCH_CODE = 0xb4, SS_JOIN_DONE = 0xb8, SS_JOIN_CODE = 0xbc, SS_JOINED = 0xc0,
                    SS_ROOM_FLAGS = 0x204, SS_CAPACITY = 0x20c, SS_RESULT_COUNT = 0x218, SS_RESULTS = 0x220,
                    SS_SLOTS = 0x680, SS_OWN_ID = 0x12e4, SS_HOST_ID = 0x12e8, SS_ATTRIBUTE = 0x12ec;
constexpr uintptr_t SLOT_SIZE = 0x120, SLOT_USED = 0x25, SLOT_ID = 0x2c, SLOT_DATA_LEN = 0x3c, SLOT_DATA = 0x40;
constexpr int SLOT_COUNT = 10;
constexpr uintptr_t RESULT_SIZE = 56;
constexpr uint32_t RESULT_ROOM = 20;
enum { EV_CREATED = 1, EV_JOINED = 2, EV_DISCONNECTED = 5, EV_KICKED = 6, EV_OWNER = 7 };

// ---- wire
constexpr uint32_t RECORD_SIZE = 104, ROW_SIZE = 56, MSG_PING = 325;

// ---- ping clock (32-bit float patterns)
constexpr uint32_t SECONDS_PER_TICK = 0x3b5a740e;   // 1/300
constexpr uint32_t PING_INTERVAL = 0x41f00000;      // 30.0

// ---- log fragments (spec 3.8)
U3X_RO const char t_moved[] = "room joined (305): the cookie points at another session; given to the one that asked: ";
U3X_RO const char t_joined[] = "room joined (305): ";
U3X_RO const char t_members[] = ", members ";
U3X_RO const char t_owner[] = ", owner id ";
U3X_RO const char t_our[] = ", our id ";
U3X_RO const char t_cookie[] = ", cookie ";
U3X_RO const char t_room[] = ", room ";
U3X_RO const char t_member[] = "member joined (306): ";
U3X_RO const char t_in[] = " in the ";
U3X_RO const char t_left[] = "room left (308) by member ";
U3X_RO const char t_of[] = " of the ";
U3X_RO const char t_roomend[] = " room";
U3X_RO const char t_kicked[] = "we were kicked out of the ";
U3X_RO const char t_destroyed[] = "the server destroyed the ";
U3X_RO const char t_open[] = " room (";
U3X_RO const char t_state[] = "), net state 0x";
U3X_RO const char t_newowner[] = "room owner changed (317): ";
U3X_RO const char t_ownerid[] = ", new owner id ";
U3X_RO const char t_follow[] = "match room made: party told to follow (member data)";

// a field to write
template <typename T> [[gnu::always_inline]] inline T &fld(void *p, uintptr_t off) {
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + off);
}

// ---- log text: at most 120 characters, built from fragments (spec 5.1)
constexpr uint32_t TEXT_MAX = 120;
struct Text {
    char b[TEXT_MAX + 1];
    uint32_t n;
};

U3X_FN static void put(Text &t, const char *s) {
    while (*s && t.n < TEXT_MAX) t.b[t.n++] = *s++;
}

U3X_FN static void put_number(Text &t, uint32_t v) {
    char digits[10];
    int k = 0;
    do {
        digits[k++] = static_cast<char>('0' + v % 10);
        v /= 10;
    } while (v);
    while (k && t.n < TEXT_MAX) t.b[t.n++] = digits[--k];
}

// a member name: at most 16 characters, each only while fewer than `limit` are in the text
U3X_FN static void put_name(Text &t, const char *name, uint32_t limit) {
    for (int i = 0; i < 16 && name[i] && t.n < limit && t.n < TEXT_MAX; i++) t.b[t.n++] = name[i];
}

U3X_FN static const char *finish(Text &t) {
    t.b[t.n] = 0;
    return t.b;
}

// ---- small readers
// name equality of the game's 0x24-byte name fields
U3X_FN static bool same_name(const char *a, const char *b) {
    for (int i = 0; i < 0x24; i++) {
        if (a[i] != b[i]) return false;
        if (!a[i]) return true;
    }
    return true;
}

[[gnu::always_inline]] inline uint8_t *binding(void *be, uint32_t k) { return rd<uint8_t *>(be, BE_BINDINGS + 8 * k); }
[[gnu::always_inline]] inline uint64_t room_of(const uint8_t *ss) { return rd<uint64_t>(ss, SS_ROOM); }
[[gnu::always_inline]] inline bool leads(const uint8_t *ss) {
    return rd<uint32_t>(ss, SS_OWN_ID) == rd<uint32_t>(ss, SS_HOST_ID);
}

// binding k's session when it takes part in "every bound session in room R", else null
U3X_FN static uint8_t *bound_in_room(void *be, uint32_t k, uint64_t room) {
    uint8_t *ss = binding(be, k);
    return ss && room && room_of(ss) == room ? ss : nullptr;
}

// raises a session event: entry k of the session's function table, called with the session
U3X_FN static void raise(uint8_t *ss, int k) {
    game_call<void>(rd<uintptr_t>(rd<const void *>(ss, 0), 8 * static_cast<uintptr_t>(k)), ss);
}

// ---- the messages (m = the message, complete; the result is its size)

// 305 room joined (also the answer to a create)
U3X_FN static uint32_t room_joined(void *be, uintptr_t base, uint8_t *m, uint32_t n) {
    if (n < 160) return 0;
    const uint32_t records = rd<uint16_t>(m, 26);
    const uint32_t size = 160 + RECORD_SIZE * records;
    if (n < size) return 0;

    const uint32_t cookie = rd<uint32_t>(m, 8);
    const uint32_t owner_id = rd<uint16_t>(m, 12);
    const uint32_t own_id = rd<uint16_t>(m, 14);
    const uint64_t room = rd<uint64_t>(m, 16);

    // which session: the cookie's, unless another binding's pending join asked for this room
    const bool cookie_ok = cookie >= 1 && cookie <= 4;
    uint8_t *ss = cookie_ok ? binding(be, cookie - 1) : nullptr;
    uint64_t *pending = u3h_onb_pj();
    int asked = -1;
    if (room)
        for (int j = 0; j < 4 && asked < 0; j++)
            if (binding(be, j) && pending[j] == room) asked = j;
    if (asked >= 0) {
        uint8_t *asker = binding(be, asked);
        if (asker != ss) {
            u3x_onb_wire(base, t_moved, asker, room, nullptr, -1);
            ss = binding(be, asked);   // what that binding holds now, after the call (may be null)
        }
        pending[asked] = 0;
    } else if (cookie_ok) {
        pending[cookie - 1] = 0;
    }
    if (!ss) {
        ext_eb()[148]++;
        return size;
    }

    Text t;
    t.n = 0;
    put(t, t_joined);
    put(t, u3h_fw_sess(base, ss));
    put(t, t_members);
    put_number(t, records);
    put(t, t_owner);
    put_number(t, owner_id);
    put(t, t_our);
    put_number(t, own_id);
    put(t, t_cookie);
    put_number(t, cookie);
    put(t, t_room);
    u3h_fw_line(finish(t), static_cast<uint32_t>(room), 8, nullptr, -1);

    fld<uint32_t>(ss, SS_CAPACITY) = rd<uint16_t>(m, 24);
    fld<uint64_t>(ss, SS_ROOM) = room;

    for (uint32_t i = 0; i < records; i++) {
        const uint8_t *record = m + 160 + RECORD_SIZE * i;
        const uint32_t id = rd<uint16_t>(record, 36);
        uint64_t description[5] = {};
        u3x_onb_info(description, record);
        u3x_add_member(base, ss, description, id == own_id, id == owner_id);
        ext_eb()[143]++;
    }

    // join finished
    if (!rd<uint8_t>(ss, SS_JOIN_DONE)) {
        if (room_of(ss)) {
            fld<int32_t>(ss, SS_JOIN_CODE) = 0;
            fld<uint32_t>(ss, SS_JOINED) = 1;
            raise(ss, EV_JOINED);
        } else {
            fld<uint32_t>(ss, SS_JOINED) = 0;
            fld<int32_t>(ss, SS_JOIN_CODE) = -1;
        }
        fld<uint8_t>(ss, SS_JOIN_DONE) = 1;
    }
    // create finished
    if (!rd<uint8_t>(ss, SS_CREATE_DONE)) {
        raise(ss, EV_CREATED);
        fld<uint8_t>(ss, SS_CREATE_DONE) = 1;
    }
    // a custom game's match room made by this machine: the party follows
    if (ss == reinterpret_cast<uint8_t *>(base + MATCH_SESSION) && room_of(ss) && leads(ss) &&
        at<uint32_t>(base + NET_MENU_MODE) == 2) {
        *u3h_lnb_match_room() = room_of(ss);
        uint8_t *party = reinterpret_cast<uint8_t *>(base + PARTY_SESSION);
        if (room_of(party) && leads(party)) {
            u3x_fl_md(nullptr, party);
            u3h_cg_log(t_follow, -1);
        }
    }
    u3x_onb_peer_begin(base, ss);
    return size;
}

// 306 member joined
U3X_FN static void member_joined(void *be, uintptr_t base, const uint8_t *m) {
    const uint64_t room = rd<uint64_t>(m, 8);
    const uint8_t *record = m + 16;
    for (uint32_t k = 0; k < 4; k++) {
        uint8_t *ss = bound_in_room(be, k, room);
        if (!ss) continue;
        uint64_t description[5] = {};
        u3x_onb_info(description, record);
        Text t;
        t.n = 0;
        put(t, t_member);
        put_name(t, reinterpret_cast<const char *>(record), 60);
        put(t, t_in);
        u3x_onb_wire(base, finish(t), ss, room, nullptr, -1);
        u3x_add_member(base, ss, description, 0, 0);
        ext_eb()[143]++;
        u3x_onb_member_link(base, ss, reinterpret_cast<const char *>(record));
    }
}

// 308 member left
U3X_FN static void member_left(void *be, uintptr_t base, const uint8_t *m) {
    const uint64_t room = rd<uint64_t>(m, 8);
    for (uint32_t k = 0; k < 4; k++) {
        uint8_t *ss = bound_in_room(be, k, room);
        if (!ss) continue;
        // the member id is read from the message anew for every session
        const uint8_t *slot = u3x_find_member(ss, rd<uint16_t>(m, 16));
        Text t;
        t.n = 0;
        put(t, t_left);
        if (slot) put_name(t, reinterpret_cast<const char *>(slot), 100);
        put(t, t_of);
        put(t, u3h_fw_sess(base, ss));
        put(t, t_roomend);
        u3h_fw_line(finish(t), 0, 0, nullptr, -1);
        if (slot) {
            u3x_remove_member(base, ss, reinterpret_cast<const char *>(slot));
            ext_eb()[144]++;
        }
    }
}

// 310 search results
U3X_FN static uint32_t search_results(void *be, const uint8_t *m, uint32_t n) {
    if (n < 16) return 0;
    const uint32_t rows = rd<uint32_t>(m, 12);
    const uint32_t size = 16 + ROW_SIZE * rows;   // 32 bits: wraps for a huge row count
    if (n < size) return 0;
    uint8_t *ss = u3x_onb_by_cookie(be, rd<uint32_t>(m, 8));
    if (!ss) return size;
    fld<uint32_t>(ss, SS_SEARCH_CODE) = 0;
    fld<uint8_t>(ss, SS_SEARCH_DONE) = 1;
    const uint32_t taken = rows < RESULT_ROOM ? rows : RESULT_ROOM;
    for (uint32_t i = 0; i < taken; i++) {
        const uint8_t *row = m + 16 + ROW_SIZE * i;
        uint8_t *result = ss + SS_RESULTS + RESULT_SIZE * i;
        fld<uint64_t>(result, 0) = rd<uint64_t>(row, 0);
        for (int c = 0; c < 36; c++) result[8 + c] = row[20 + c];
        fld<uint32_t>(result, 44) = rd<uint16_t>(row, 12);
        fld<uint32_t>(result, 52) = rd<uint16_t>(row, 16);
        fld<uint32_t>(ss, SS_RESULT_COUNT)++;
        ext_eb()[142]++;
    }
    return size;
}

// 312 kicked, 313 room destroyed
U3X_FN static void room_lost(void *be, uintptr_t base, const uint8_t *m, uint32_t type) {
    const uint64_t room = rd<uint64_t>(m, 8);
    for (uint32_t k = 0; k < 4; k++) {
        uint8_t *ss = bound_in_room(be, k, room);
        if (!ss) continue;
        Text t;
        t.n = 0;
        put(t, type == 312 ? t_kicked : t_destroyed);
        put(t, u3h_fw_sess(base, ss));
        put(t, t_open);
        put_number(t, type);
        put(t, t_state);
        u3h_fw_line(finish(t), at<uint32_t>(base + NET_STATE), 2, nullptr, -1);
        raise(ss, EV_DISCONNECTED);
        if (type == 312) raise(ss, EV_KICKED);
        fld<uint64_t>(ss, SS_ROOM) = 0;
        game_call<void>(base + CLEAR_MEMBERS, ss);
        ext_eb()[145]++;
    }
}

// 315 member data
U3X_FN static void member_data(void *be, const uint8_t *m) {
    const uint64_t room = rd<uint64_t>(m, 8);
    for (uint32_t k = 0; k < 4; k++) {
        uint8_t *ss = bound_in_room(be, k, room);
        if (!ss) continue;
        // the member id is read from the message anew for every session, the length byte after the lookup
        uint8_t *slot = u3x_find_member(ss, rd<uint16_t>(m, 4));
        if (!slot) continue;
        const uint32_t length = rd<uint8_t>(m, 6);
        fld<uint32_t>(slot, SLOT_DATA_LEN) = length;   // stored as it is, also above 64
        for (uint32_t i = 0; i < length && i < 64; i++) slot[SLOT_DATA + i] = m[16 + i];
    }
}

// 317 owner changed, 319 room attribute, 321 room flags: the first bound session with that room id
U3X_FN static void room_changed(void *be, uintptr_t base, const uint8_t *m, uint32_t type) {
    uint8_t *ss = u3x_onb_by_room(be, rd<uint64_t>(m, 8));
    if (!ss) return;
    const uint32_t value = rd<uint16_t>(m, 4);
    if (type == 317) {
        u3x_onb_wire(base, t_newowner, ss, room_of(ss), t_ownerid, static_cast<int32_t>(value));
        fld<uint32_t>(ss, SS_HOST_ID) = value;
        raise(ss, EV_OWNER);
        ext_eb()[146]++;
        u3x_onb_peer_begin(base, ss);
    } else if (type == 319) {
        fld<uint8_t>(ss, SS_ATTRIBUTE) = rd<uint8_t>(m, 4) & 1;
    } else {
        fld<uint32_t>(ss, SS_ROOM_FLAGS) = value;
    }
}

// 324 room name
U3X_FN static void room_name(void *be, const uint8_t *m) {
    uint8_t *ss = u3x_onb_by_room(be, rd<uint64_t>(m, 8));
    if (ss) u3x_onb_strcpy(reinterpret_cast<char *>(ss + SS_ROOM_NAME), reinterpret_cast<const char *>(m + 16), 128);
}

// ---- Update
// removes `used` bytes from the front of the receive buffer; bytes beyond the new count keep what they held
U3X_FN static void drop_front(void *be, uint32_t used) {
    uint8_t *buffer = static_cast<uint8_t *>(be) + BE_BUFFER;
    const uint32_t count = rd<uint32_t>(be, BE_COUNT);
    for (uint32_t i = used; i < count; i++) buffer[i - used] = buffer[i];
    fld<uint32_t>(be, BE_COUNT) = count - used;
}

// now in seconds, as a 32-bit float pattern
U3X_FN static uint32_t now_seconds(void *be) {
    const uintptr_t base = rd<uint64_t>(be, BE_BASE);
    return f_mul(f_from_int(at<int32_t>(base + LOCAL_CLOCK)), SECONDS_PER_TICK);
}

}  // namespace g13p2

extern "C" {

// Gets the connection to one room member ready (online only, never for the own name).
U3X_FN void u3x_onb_member_link(uintptr_t base, uint8_t *ss, const char *name) {
    using namespace g13p2;
    uint8_t *table = reinterpret_cast<uint8_t *>(base + CONN_TABLE);
    if (rd<uint8_t>(table, TABLE_LAN_FLAG)) return;
    if (same_name(name, reinterpret_cast<const char *>(table + TABLE_OWN_NAME))) return;
    for (int i = 0; i < ENTRY_COUNT; i++) {
        uint8_t *entry = table + TABLE_ENTRIES + ENTRY_SIZE * static_cast<uintptr_t>(i);
        if (!rd<uint8_t>(entry, ENTRY_USED)) continue;
        if (!same_name(reinterpret_cast<const char *>(entry + ENTRY_NAME), name)) continue;
        // the peer counts as heard now, so the match's connection monitor starts from here
        if (ss == reinterpret_cast<uint8_t *>(base + MATCH_SESSION)) u3h_gt_stamp(base, entry + ENTRY_SUB, SUB_LAST_HEARD);
        if (rd<int32_t>(entry, ENTRY_HANDLE) == 0) u3h_onb_pl_np(base, entry, name);
        return;
    }
}

// The member link for every other member of a session.
U3X_FN void u3x_onb_peer_begin(uintptr_t base, uint8_t *ss) {
    using namespace g13p2;
    for (int i = 0; i < SLOT_COUNT; i++) {
        uint8_t *slot = ss + SS_SLOTS + SLOT_SIZE * static_cast<uintptr_t>(i);
        if (rd<uint8_t>(slot, SLOT_USED) && rd<uint32_t>(slot, SLOT_ID) != rd<uint32_t>(ss, SS_OWN_ID))
            u3x_onb_member_link(base, ss, reinterpret_cast<const char *>(slot));
    }
}

// A wire member record as a member description (the two padding words stay unwritten).
U3X_FN void u3x_onb_info(void *in, const uint8_t *r) {
    using namespace g13p2;
    fld<const uint8_t *>(in, 0) = r;                    // name
    fld<uint32_t>(in, 8) = rd<uint16_t>(r, 36);         // member id
    fld<uint32_t>(in, 0xc) = rd<uint8_t>(r, 38);        // NAT type
    fld<uint32_t>(in, 0x10) = rd<uint8_t>(r, 39);       // data length
    fld<const uint8_t *>(in, 0x18) = r + 40;            // data
    fld<uint32_t>(in, 0x20) = 1;                        // carries data
}

// Applies the one message at the start of the receive buffer; answers the bytes it used (0 = incomplete or unknown).
U3X_FN uint32_t u3x_onb_msg_in(void *be) {
    using namespace g13p2;
    uint8_t *m = static_cast<uint8_t *>(be) + BE_BUFFER;
    const uint32_t n = rd<uint32_t>(be, BE_COUNT);
    const uintptr_t base = rd<uint64_t>(be, BE_BASE);
    if (n < 4) return 0;
    const uint32_t type = rd<uint32_t>(m, 0);

    if (type == 305) return room_joined(be, base, m, n);
    if (type == 310) return search_results(be, m, n);

    uint32_t size = 0;
    if (type == 306) size = 120;
    else if (type == 308) size = 24;
    else if (type == 312 || type == 313 || type == 317 || type == 319 || type == 321) size = 16;
    else if (type == 315) size = 80;
    else if (type == 324) size = 144;
    if (!size || n < size) return 0;

    if (type == 306) member_joined(be, base, m);
    else if (type == 308) member_left(be, base, m);
    else if (type == 312 || type == 313) room_lost(be, base, m, type);
    else if (type == 315) member_data(be, m);
    else if (type == 324) room_name(be, m);
    else room_changed(be, base, m, type);
    return size;
}

// The backend's Update slot: takes in what arrived, message by message.
U3X_FN int64_t u3x_onb_s1(void *be) {
    using namespace g13p2;
    uint8_t *buffer = static_cast<uint8_t *>(be) + BE_BUFFER;
    uint32_t *count = &fld<uint32_t>(be, BE_COUNT);

    u3x_onb_note(1);
    const int socket = u3h_smn_up();
    if (socket) {
        // the session socket: a framed big-endian stream, converted message by message
        if (socket == 1) u3h_smn_pull(buffer, count, BUFFER_SIZE);
        while (*count >= 4) {
            const uint32_t length = u3h_smn_len(buffer, *count);
            if (!length || length > BUFFER_SIZE) {
                u3h_smn_unknown(buffer, *count);
                *count = 0;
                break;
            }
            if (*count < length) break;
            const uint32_t id = u3h_smn_swap(buffer, length);
            u3h_smn_got(buffer, id);
            const uint32_t used = u3x_onb_msg_in(be);
            uint32_t *eb = ext_eb();
            eb[135]++;
            eb[136] = id;
            drop_front(be, used ? used : length);
        }
        return 0;
    }

    // the community link
    if (!u3h_ol_up()) {
        ext_eb()[149]++;
        return -1;
    }
    if (u3x_onb_ping_due(be)) {
        const uint32_t ping = MSG_PING;
        u3x_onb_send(&ping, 4);
        ext_eb()[137]++;
        u3x_onb_ping_restart(be);
    }
    while (*count) {
        const uint32_t used = u3x_onb_msg_in(be);
        if (!used) break;
        uint32_t *eb = ext_eb();
        eb[135]++;
        eb[136] = rd<uint32_t>(buffer, 0);
        drop_front(be, used);
    }
    return 0;
}

// The next ping is due 30 seconds from now.
U3X_FN void u3x_onb_ping_restart(void *be) {
    using namespace g13p2;
    fld<uint32_t>(be, BE_PING_TIME) = f_add(now_seconds(be), PING_INTERVAL);
}

// 1 when the ping time has passed.
U3X_FN int u3x_onb_ping_due(void *be) {
    using namespace g13p2;
    return f_less(rd<uint32_t>(be, BE_PING_TIME), now_seconds(be)) ? 1 : 0;
}

}  // extern "C"
