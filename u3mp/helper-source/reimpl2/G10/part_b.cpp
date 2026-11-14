// G10 part B: the LAN backend's room slots (solo room, Create, Join, Leave, Kick, slot 11, MemberData).
// Written from spec/G10_lan_backend_slots.md (sections 3 and 5).

extern "C" {
// part A of this group
void u3x_lnb_note(void *be, int slot);
int u3x_lnb_bound(void *be, const uint8_t *ss);
// other groups and the C part
int64_t u3h_lnb_call(void *object, int slot);
void u3x_lnb_srv_stop(void *v);
int32_t u3x_lnb_srv_start(void *v, uint64_t arg, uint32_t port, uint32_t maxp);
int32_t u3x_sock_close(void *s);
int32_t u3x_sock_connect(void *s, uint32_t ip, uint32_t port, int nodelay, int nonblocking);
int32_t u3x_sock_send(const void *s, const void *b, uint32_t n, int block);
void u3h_lnb_send_join(void *be, void *bind, uint8_t *ss, int host, int32_t *res);
void u3h_lnb_rank_stamp(void *be, uint8_t *md, uint32_t len);
uint64_t *u3h_lnb_match_room(void);
const char *u3h_onp_id(void *s);
// this part
void u3x_lnb_solo_create(void *be, uint8_t *ss, uint32_t maxp);
int64_t u3x_lnb_s4(void *be, uint8_t *ss, uint32_t maxp);
int64_t u3x_lnb_s6(void *be, uint8_t *ss, uint64_t room);
int64_t u3x_lnb_s7(void *be, uint8_t *ss);
int64_t u3x_lnb_s9(void *be, uint8_t *ss, uint32_t id);
int64_t u3x_lnb_s11(void *be, uint8_t *ss, int32_t lock);
int64_t u3x_lnb_s12(void *be, uint8_t *ss, const uint8_t *data, uint32_t len);
}

namespace g10b {

// backend object
constexpr uintptr_t BE_BINDINGS = 0x4b8, BINDING_SIZE = 0x9140, BE_BASE = 0x249c8, BE_STATE = 0x249d0;
// binding
constexpr uintptr_t BIND_SERVER = 8, BIND_CLIENT = 0x8930, BIND_RECEIVED = 0x893c;
// session
constexpr uintptr_t SS_BACKEND = 8, SS_ROOM = 0x18, SS_CREATE_DONE = 0xa1, SS_CREATE_RESULT = 0xa4, SS_JOIN_DONE = 0xb8,
                    SS_JOIN_RESULT = 0xbc, SS_CAPACITY = 0x20c, SS_MEMBERS = 0x680, MEMBER_SIZE = 0x120, SS_OWN_ID = 0x12e4,
                    SS_HOST_ID = 0x12e8, SS_DATA_LEN = 0x12f0, SS_DATA = 0x12f4;
constexpr int MEMBER_COUNT = 10;
// member slot
constexpr uintptr_t MB_IN_USE = 0x25, MB_ID = 0x2c, MB_CONNECTION = 0x34;
// image offsets
constexpr uintptr_t IMG_MATCH_SESSION = 0x17d6420, IMG_PARTY_SESSION = 0x17dc298, IMG_CLEAR_MEMBERS = 0x7a25f0,
                    IMG_NET_MANAGER = 0x17cb060, IMG_NET_PLAYERS = 0x17cb3e0;
// numbers
constexpr uint32_t PORT_PARTY = 0x2333, PORT_MATCH = 0x2332, LOOPBACK = 0x0100007f, MSG_KICK = 0xd1, MSG_MEMBER_DATA = 0xcd,
                   MAX_DATA = 0x40, WAIT_MICROSECONDS = 100000;
constexpr int WAIT_ROUNDS = 30;
constexpr uint32_t FAILED = 0xffffffffu;

U3X_RO const char default_name[] = {'P', 'l', 'a', 'y', 'e', 'r', 0};

// the two room messages this part sends (raw little-endian structures, no padding)
struct KickMessage { uint32_t id, member; uint64_t room; };
struct MemberDataMessage { uint32_t id, length; uint64_t room; uint8_t data[MAX_DATA]; };

// a field to write (or to read and write)
template <typename T> [[gnu::always_inline]] inline T &fld(void *p, uintptr_t off) {
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + off);
}

[[gnu::always_inline]] inline uint8_t *binding_of(void *be, int index) {
    return static_cast<uint8_t *>(be) + BE_BINDINGS + static_cast<uintptr_t>(index) * BINDING_SIZE;
}
[[gnu::always_inline]] inline uintptr_t image_base(const void *be) { return rd<uint64_t>(be, BE_BASE); }
[[gnu::always_inline]] inline uint8_t *state_of(const void *be) { return rd<uint8_t *>(be, BE_STATE); }
[[gnu::always_inline]] inline uint64_t room_of(const uint8_t *ss) { return rd<uint64_t>(ss, SS_ROOM); }

[[gnu::always_inline]] inline bool solo_on() { return (ext_eb()[0] & 2u) == 0; }
[[gnu::always_inline]] inline bool is_party(uint8_t *ss) { return (u3h_lnb_call(ss, 0) & 0xff) != 0; }
[[gnu::always_inline]] inline void clear_members(const void *be, uint8_t *ss) {
    game_call<void, uint8_t *>(image_base(be) + IMG_CLEAR_MEMBERS, ss);
}
[[gnu::always_inline]] inline void run_update(void *be) { u3h_lnb_call(be, 1); }

// the wait: through the platform's sleep pointer at eb[10..11]; no wait at all when it is null
[[gnu::always_inline]] inline void wait_round() {
    using Sleep = int (*)(uint32_t microseconds);
    const Sleep sleep = rd<Sleep>(ext_eb(), 10 * 4);
    if (sleep) sleep(WAIT_MICROSECONDS);
}

}  // namespace g10b

// 5.1: a one-member room without a room server
extern "C" U3X_FN void u3x_lnb_solo_create(void *be, uint8_t *ss, uint32_t maxp) {
    using namespace g10b;
    ext_eb()[82] += 1;
    uint8_t *const state = state_of(be);
    if (state) fld<uint32_t>(state, 1452) += 1;

    fld<void *>(ss, SS_BACKEND) = be;

    uint8_t *const member = ss + SS_MEMBERS;
    for (uintptr_t i = 0; i < MEMBER_SIZE; ++i) member[i] = 0;
    const char *const name = state ? u3h_onp_id(state) : default_name;
    for (int i = 0; i < 31 && name[i]; ++i) member[i] = static_cast<uint8_t>(name[i]);
    fld<uint8_t>(member, MB_IN_USE) = 1;
    fld<uint32_t>(member, MB_ID) = 1;
    fld<int32_t>(member, MB_CONNECTION) = 0;

    fld<uint32_t>(ss, SS_ROOM) = 1;   // the low half only
    fld<uint32_t>(ss, SS_OWN_ID) = 1;
    fld<uint32_t>(ss, SS_HOST_ID) = 1;
    fld<uint32_t>(ss, SS_CAPACITY) = maxp;
    fld<int32_t>(ss, SS_CREATE_RESULT) = 0;
    fld<uint8_t>(ss, SS_CREATE_DONE) = 1;
}

// 5.2: slot 4, Create
extern "C" U3X_FN int64_t u3x_lnb_s4(void *be, uint8_t *ss, uint32_t maxp) {
    using namespace g10b;
    uint32_t *const eb = ext_eb();
    u3x_lnb_note(be, 4);
    eb[69] += 1;
    const int index = u3x_lnb_bound(be, ss);
    if (index < 0) {
        eb[70] = FAILED;
        return -1;
    }
    uint8_t *const bind = binding_of(be, index);
    const uint32_t port = is_party(ss) ? PORT_PARTY : PORT_MATCH;

    if (u3x_lnb_srv_start(bind + BIND_SERVER, 1, port, maxp) < 0 && solo_on()) {
        u3x_lnb_solo_create(be, ss, maxp);
        eb[70] = 0;
        return 0;
    }

    // connect to the own room server and ask to join as host
    clear_members(be, ss);
    fld<uint32_t>(ss, SS_CAPACITY) = maxp;
    fld<uint32_t>(bind, BIND_RECEIVED) = 0;
    u3x_sock_connect(bind + BIND_CLIENT, LOOPBACK, port, 0, 0);
    run_update(be);
    int32_t sent = -1;
    u3h_lnb_send_join(be, bind, ss, 1, &sent);
    eb[92] = static_cast<uint32_t>(sent);
    fld<uint8_t>(ss, SS_CREATE_DONE) = 0;

    for (int round = 0; round < WAIT_ROUNDS; ++round) {
        wait_round();
        run_update(be);
        if (!rd<uint8_t>(ss, SS_CREATE_DONE)) continue;

        // the room answered
        fld<uint8_t>(ss, SS_CREATE_DONE) = 1;
        if (!room_of(ss)) fld<int32_t>(ss, SS_CREATE_RESULT) = -1;
        eb[70] = static_cast<uint32_t>(sent);

        // a match room: the party learns it, and a party host sends its member data again
        const uintptr_t base = image_base(be);
        if (ss == reinterpret_cast<uint8_t *>(base + IMG_MATCH_SESSION) && room_of(ss)) {
            *u3h_lnb_match_room() = room_of(ss);
            uint8_t *const party = reinterpret_cast<uint8_t *>(base + IMG_PARTY_SESSION);
            if (room_of(party) && rd<uint32_t>(party, SS_OWN_ID) == rd<uint32_t>(party, SS_HOST_ID) &&
                rd<uint32_t>(party, SS_DATA_LEN) == MAX_DATA) {
                u3x_lnb_s12(be, party, party + SS_DATA, MAX_DATA);
                fld<uint32_t>(u3h_ext(), 0xbc0 + 4 * 24) += 1;
            }
        }
        return sent;
    }

    // no answer
    u3x_lnb_srv_stop(bind + BIND_SERVER);
    if (solo_on()) {
        u3x_sock_close(bind + BIND_CLIENT);
        u3x_lnb_solo_create(be, ss, maxp);
        eb[70] = 0;
        return 0;
    }
    eb[70] = FAILED;
    return -1;
}

// 5.3: slot 6, Join
extern "C" U3X_FN int64_t u3x_lnb_s6(void *be, uint8_t *ss, uint64_t room) {
    using namespace g10b;
    uint32_t *const eb = ext_eb();
    u3x_lnb_note(be, 6);
    eb[71] += 1;
    const int index = u3x_lnb_bound(be, ss);
    if (index < 0) {
        eb[72] = FAILED;
        return -1;
    }
    uint8_t *const bind = binding_of(be, index);

    u3x_sock_close(bind + BIND_CLIENT);
    u3x_lnb_srv_stop(bind + BIND_SERVER);
    clear_members(be, ss);
    fld<uint32_t>(bind, BIND_RECEIVED) = 0;

    // the room id holds the host's address (first octet highest) and the room's port
    const uint32_t address = __builtin_bswap32(static_cast<uint32_t>(room >> 32));
    const uint32_t port = static_cast<uint32_t>(room >> 16) & 0xffffu;
    const int32_t connected = u3x_sock_connect(bind + BIND_CLIENT, address, port, 0, 0);
    if (connected < 0) {
        fld<int32_t>(ss, SS_JOIN_RESULT) = connected;
        fld<uint8_t>(ss, SS_JOIN_DONE) = 1;
        eb[72] = static_cast<uint32_t>(connected);
        return connected;
    }

    run_update(be);
    int32_t sent = -1;
    u3h_lnb_send_join(be, bind, ss, 0, &sent);
    fld<uint64_t>(ss, SS_ROOM) = room;
    fld<uint8_t>(ss, SS_JOIN_DONE) = 0;
    fld<int32_t>(ss, SS_JOIN_RESULT) = 0;

    // wait for the room
    for (int round = 0;; ++round) {
        if (!room_of(ss)) {   // the connection ended (or the room id was 0)
            fld<int32_t>(ss, SS_JOIN_RESULT) = -1;
            break;
        }
        if (round == WAIT_ROUNDS) {   // timeout
            u3x_lnb_srv_stop(bind + BIND_SERVER);
            eb[72] = FAILED;
            return -1;
        }
        wait_round();
        run_update(be);
        if (rd<uint8_t>(ss, SS_JOIN_DONE)) {
            if (!room_of(ss)) fld<int32_t>(ss, SS_JOIN_RESULT) = -1;
            break;
        }
    }
    fld<uint8_t>(ss, SS_JOIN_DONE) = 1;
    eb[72] = static_cast<uint32_t>(sent);
    return sent;
}

// 5.4: slot 7, Leave
extern "C" U3X_FN int64_t u3x_lnb_s7(void *be, uint8_t *ss) {
    using namespace g10b;
    u3x_lnb_note(be, 7);
    const int index = u3x_lnb_bound(be, ss);
    if (index < 0) return -1;
    uint8_t *const bind = binding_of(be, index);

    if (ss == reinterpret_cast<uint8_t *>(image_base(be) + IMG_MATCH_SESSION)) *u3h_lnb_match_room() = 0;
    u3x_sock_close(bind + BIND_CLIENT);
    u3x_lnb_srv_stop(bind + BIND_SERVER);
    fld<uint64_t>(ss, SS_ROOM) = 0;
    clear_members(be, ss);
    return 0;
}

// 5.5: slot 9, Kick
extern "C" U3X_FN int64_t u3x_lnb_s9(void *be, uint8_t *ss, uint32_t id) {
    using namespace g10b;
    u3x_lnb_note(be, 9);
    const int index = u3x_lnb_bound(be, ss);
    if (index < 0) return -1;

    // who asked for a kick: counter, member id, and up to six return addresses relative to the image
    uint32_t *const words = reinterpret_cast<uint32_t *>(u3h_ext() + 0xbc0);
    words[16] += 1;
    words[17] = id;
    const uintptr_t base = image_base(be);
    uintptr_t frame = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));   // gives this function a frame pointer
    for (int k = 0; k < 6; ++k) {
        if (!frame || (frame & 7)) break;
        words[18 + k] = static_cast<uint32_t>(at<uint64_t>(frame + 8) - base);
        frame = at<uint64_t>(frame);
    }

    const uint64_t room = room_of(ss);
    if (room) {
        const KickMessage message = {MSG_KICK, id, room};
        u3x_sock_send(binding_of(be, index) + BIND_CLIENT, &message, sizeof message, 1);
        ext_eb()[88] += 1;
    }
    return 0;
}

// 5.6: slot 11, the net players of a solo match
extern "C" U3X_FN int64_t u3x_lnb_s11(void *be, uint8_t *ss, int32_t lock) {
    using namespace g10b;
    using CreatePlayer = uint8_t *(*)(void *manager, uint8_t *session, uint8_t *member, uint32_t, uint32_t);
    u3x_lnb_note(be, 11);
    if (!solo_on()) return 0;
    uint8_t *const state = state_of(be);
    if (!state || !lock || !ss) return 0;
    const CreatePlayer create_player = rd<CreatePlayer>(state, 768 + 8 * 8);   // native 8
    if (!create_player) return 0;
    const uintptr_t base = image_base(be);
    if (at<void *>(base + IMG_NET_PLAYERS)) return 0;   // the net players exist already

    for (int i = 0; i < MEMBER_COUNT; ++i) {
        uint8_t *const member = ss + SS_MEMBERS + static_cast<uintptr_t>(i) * MEMBER_SIZE;
        if (!rd<uint8_t>(member, MB_IN_USE)) continue;
        uint8_t *const player = create_player(reinterpret_cast<void *>(base + IMG_NET_MANAGER), ss, member, 0, 0);
        if (!player) continue;
        fld<int32_t>(player, 0x21c) = -1;   // no team
        fld<uint8_t>(player, 0x365) = 0;    // not ready
        ext_eb()[93] += 1;
    }
    return 0;
}

// 5.7: slot 12, MemberData
extern "C" U3X_FN int64_t u3x_lnb_s12(void *be, uint8_t *ss, const uint8_t *data, uint32_t len) {
    using namespace g10b;
    u3x_lnb_note(be, 12);
    if (len > MAX_DATA) return -1;

    // the session keeps the caller's data (also when it is not attached)
    fld<uint32_t>(ss, SS_DATA_LEN) = len;
    for (uint32_t i = 0; i < len; ++i) ss[SS_DATA + i] = data[i];

    const int index = u3x_lnb_bound(be, ss);
    if (index < 0) return -1;

    const uint64_t room = room_of(ss);
    if (room) {
        MemberDataMessage message;
        message.id = MSG_MEMBER_DATA;
        message.length = len;
        message.room = room;
        for (uint32_t i = 0; i < MAX_DATA; ++i) message.data[i] = i < len ? data[i] : 0;
        u3h_lnb_rank_stamp(be, message.data, len);   // only the message carries the stamp
        u3x_sock_send(binding_of(be, index) + BIND_CLIENT, &message, sizeof message, 1);
        ext_eb()[89] += 1;
    }
    return 0;
}
