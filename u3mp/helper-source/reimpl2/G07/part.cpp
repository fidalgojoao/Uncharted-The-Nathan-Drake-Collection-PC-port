// G07. LAN backend: socket class, byte stream, connection table, session member slots, join message, backend slot 0.
// Written from spec/G07_lan_sockets.md (section numbers in the comments are that document's).

// ---- neighbours (spec 2.3): the C part and other groups
extern "C" {
uint8_t *u3h_ext(void);
void u3h_net_pick(void);
int32_t u3x_sock_error(int32_t r);
int32_t u3x_sock_close(void *s);
int32_t u3x_sock_setopt(int fd, int level, int opt, int32_t v);
int32_t u3x_sock_bind(int fd, uint32_t port);
uint32_t u3x_sock_local_ip(void);
void u3x_sock_local(void *s, uint32_t port);
int32_t u3x_sock_send(const void *s, const void *b, uint32_t n, int block);
int u3x_party_players(const uint8_t *session);
uint8_t u3x_member_status(int custom_game, int guest_waiting, int host_playing);
uint8_t u3x_member_start(int at_start, uint64_t followed, uint64_t room);
uint8_t *u3h_player_profile(uintptr_t base);
uint64_t *u3h_lnb_match_room(void);
void u3h_lnb_marks(uint8_t *guest_wait, uint8_t *host_playing, uint8_t *at_start, uint64_t *followed);
void u3h_ng_stamp(uintptr_t base, void *s, uint8_t *md);
void u3h_gt_entry_reset(uint8_t *entry);
void u3h_onb_cm_np(uintptr_t base, uint8_t *entry, const char *name);
void u3h_pl_drop(const char *name);

// ---- this group (spec 2.1, 2.2)
int32_t u3x_sock_udp(void *s, uint32_t port, int broadcast, int32_t receive_buffer);
int32_t u3x_sock_listen(void *s, uint32_t port);
int32_t u3x_sock_connect(void *s, uint32_t ip, uint32_t port, int nodelay, int nonblocking);
int32_t u3x_sock_poll(const void *s);
int32_t u3x_sock_recv(void *s, void *b, uint32_t n, int block);
void u3x_sock_adopt(void *s, int32_t fd, uint32_t ip, uint32_t port);
void u3x_stream_put(void *w, uint64_t v, int bytes);
uint64_t u3x_stream_get(void *r, int bytes);
void u3x_cm_reset_entry(uint8_t *entry);
void u3x_cm_init_lan(uintptr_t base);
int32_t u3x_cm_get(uintptr_t base, const char *name);
int32_t u3x_cm_release(uintptr_t base, int32_t id);
void u3x_cm_shutdown(uintptr_t base);
void u3x_lnb_rank_stamp(void *be, uint8_t *md, uint32_t len);
void u3x_lnb_send_join(void *be, void *bind, uint8_t *ss, int host, int32_t *res);
int64_t u3x_lnb_s0(void *be);
int32_t u3x_add_member(uintptr_t base, uint8_t *session, const void *info, int you, int host);
int32_t u3x_remove_member(uintptr_t base, uint8_t *session, const char *name);
}

namespace g07 {

// ---- field access at any alignment
template <typename T> struct [[gnu::packed]] Raw { T v; };
template <typename T> [[gnu::always_inline]] inline T get(const void *p, uintptr_t off) {
    return reinterpret_cast<const Raw<T> *>(static_cast<const uint8_t *>(p) + off)->v;
}
template <typename T> [[gnu::always_inline]] inline void put(void *p, uintptr_t off, T v) {
    reinterpret_cast<Raw<T> *>(static_cast<uint8_t *>(p) + off)->v = v;
}

U3X_FN void copy_bytes(uint8_t *to, const uint8_t *from, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) to[i] = from[i];
}
U3X_FN void zero_bytes(uint8_t *to, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) to[i] = 0;
}

// ---- player names (3.1)
constexpr uint32_t NAME = 0x24;

U3X_FN bool same_name(const char *a, const char *b) {
    for (uint32_t i = 0; i < NAME; i++) {
        if (a[i] != b[i]) return false;
        if (!a[i]) return true;
    }
    return true;
}

// ---- the platform's socket API (3.2)
struct Sock { int32_t fd; uint32_t ip; uint32_t port; };   // the socket record (3.3)

using SocketFn = int32_t (*)(const char *name, int family, int type, int protocol);
using ConnectFn = int32_t (*)(int fd, const void *address, uint32_t length);
using ListenFn = int32_t (*)(int fd, int backlog);
using ReceiveFn = int32_t (*)(int fd, void *to, uint32_t length, int flags);

enum : int { IMP_SOCKET = 0, IMP_BIND = 2, IMP_SETOPT = 5, IMP_CONNECT = 7, IMP_LISTEN = 8, IMP_RECEIVE = 11 };
enum : int { IPV4 = 2, TCP = 1, UDP = 2, LEVEL_SOCKET = 0xffff, LEVEL_TCP = 6 };
enum : int { OPT_NONBLOCK = 0x1200, OPT_BROADCAST = 0x20, OPT_REUSE = 4, OPT_BUFFER = 0x1001,
             OPT_RECV_TIMEOUT = 0x1006, OPT_SEND_TIMEOUT = 0x1005, OPT_TCP_NODELAY = 1 };
enum : int { FLAG_PEEK = 2, FLAG_DONTWAIT = 0x80, BACKLOG = 12 };
enum : uint32_t { ERR_AGAIN = 35, ERR_INPROGRESS = 36, ERR_ALREADY = 37, ERR_ISCONN = 56, ERR_NOTCONN = 57 };
constexpr uint32_t LOOPBACK = 0x0100007fu;
constexpr int32_t TIMEOUT_US = 10000000;
constexpr uint32_t CONNECT_TRIES = 100000;

U3X_RO const char name_lan[] = "u3mp-lan";
U3X_RO const char name_room[] = "u3mp-room";

template <typename F> [[gnu::always_inline]] inline F import(int k) {
    return reinterpret_cast<F>(get<uintptr_t>(u3h_ext(), 0x200 + 8 * static_cast<uintptr_t>(k)));
}
[[gnu::always_inline]] inline bool has_import(int k) { return import<uintptr_t>(k) != 0; }
[[gnu::always_inline]] inline uint32_t error_code(int32_t r) { return static_cast<uint32_t>(r) & 0xff; }
[[gnu::always_inline]] inline bool is_open(const Sock *s) { return s->fd > 0; }

// new descriptor into the record; a negative one is noted and leaves the record closed
U3X_FN bool create(Sock *s, const char *debug_name, int type) {
    s->fd = import<SocketFn>(IMP_SOCKET)(debug_name, IPV4, type, 0);
    if (s->fd >= 0) return true;
    u3x_sock_error(s->fd);
    s->fd = 0;
    return false;
}

// the 16-byte socket address of (ip, port)
U3X_FN void socket_address(uint8_t *a, uint32_t ip, uint32_t port) {
    zero_bytes(a, 16);
    a[0] = 16;
    a[1] = IPV4;
    a[2] = static_cast<uint8_t>(port >> 8);
    a[3] = static_cast<uint8_t>(port);
    put<uint32_t>(a, 4, ip);
}

// ---- the stream object (3.4)
struct Stream { uint8_t *buffer; uint32_t position; uint32_t limit; };

// ---- the connection table (3.5)
constexpr uintptr_t CM_TABLE = 0x17f31b0, OWN_NAME = 0x17d6010, PACKET_RESET = 0x6aa150;
constexpr uintptr_t CM_NEXT_ID = 0, CM_ENTRIES = 8, CM_ENTRY_SIZE = 0x220, CM_BLOCKED_COUNT = 0x4408, CM_BLOCKED = 0x440c,
                    CM_LAN_RECORDS = 0x4890, CM_LAN_RECORD_SIZE = 0x28, CM_OWN_NAME = 0x4d90, CM_LAN_MODE = 0x4db4;
constexpr uint32_t CM_COUNT = 32;
constexpr uintptr_t CE_ID = 0, CE_USED = 4, CE_NAME = 7, CE_IP = 0x2c, CE_PORT = 0x30, CE_HANDLE = 0x34, CE_REFS = 0x38, CE_PACKET = 0x40;
constexpr uint16_t GAME_PORT = 3233;

[[gnu::always_inline]] inline uint8_t *cm_table(uintptr_t base) { return reinterpret_cast<uint8_t *>(base + CM_TABLE); }
[[gnu::always_inline]] inline uint8_t *cm_entry(uint8_t *table, uint32_t i) { return table + CM_ENTRIES + i * CM_ENTRY_SIZE; }
[[gnu::always_inline]] inline const char *text(const uint8_t *p) { return reinterpret_cast<const char *>(p); }

U3X_FN void packet_reset(uintptr_t base, uint8_t *entry) { game_call<void, uint8_t *>(base + PACKET_RESET, entry + CE_PACKET); }

// address of a LAN peer from the address records, 0 when there is none
U3X_FN bool lan_address(const uint8_t *table, const char *name, uint32_t *ip) {
    for (uint32_t k = 0; k < CM_COUNT; k++) {
        const uint8_t *record = table + CM_LAN_RECORDS + k * CM_LAN_RECORD_SIZE;
        if (same_name(name, text(record))) { *ip = get<uint32_t>(record, NAME); return true; }
    }
    *ip = 0;
    return false;
}

// ---- the session object (3.6), the member description (3.7)
constexpr uintptr_t S_BACKEND = 8, S_ROOM = 0x18, S_CAPACITY = 0x20c, S_SLOTS = 0x680, S_SLOT_SIZE = 0x120, S_NEXT_INDEX = 0x12e0,
                    S_OWN_ID = 0x12e4, S_HOST_ID = 0x12e8, S_DATA_LENGTH = 0x12f0, S_DATA = 0x12f4;
constexpr uint32_t S_SLOT_COUNT = 10, DATA_MAX = 0x40;
constexpr uintptr_t M_USED = 0x25, M_INDEX = 0x28, M_ID = 0x2c, M_NAT = 0x30, M_CONNECTION = 0x34, M_DATA_LENGTH = 0x3c, M_DATA = 0x40;
constexpr uintptr_t SECOND_PLAYER_FLAG = 0x17d7b06, SECOND_PLAYER_MODE = 0x17f2fc0;
enum : int { SESSION_JOINED = 3, SESSION_LEFT = 4, BACKEND_KICK = 9 };

[[gnu::always_inline]] inline uint8_t *slot_at(uint8_t *session, uint32_t i) { return session + S_SLOTS + i * S_SLOT_SIZE; }
[[gnu::always_inline]] inline uintptr_t method(const void *object, int k) {
    return get<uintptr_t>(get<const uint8_t *>(object, 0), 8 * static_cast<uintptr_t>(k));
}

// the slot in use that holds this name, or null
U3X_FN uint8_t *member_named(uint8_t *session, const char *name) {
    for (uint32_t i = 0; i < S_SLOT_COUNT; i++) {
        uint8_t *slot = slot_at(session, i);
        if (slot[M_USED] && same_name(name, text(slot))) return slot;
    }
    return nullptr;
}

// players the session holds now (4.17)
U3X_FN uint32_t player_count(uintptr_t base, const uint8_t *session) {
    if (get<uint64_t>(session, S_ROOM)) return static_cast<uint32_t>(u3x_party_players(session));
    const bool second = at<uint8_t>(base + SECOND_PLAYER_FLAG) || at<int32_t>(base + SECOND_PLAYER_MODE) == 3;
    return second ? 2 : 1;
}

// ---- the backend object, the binding, the state object, the join message (3.8, 3.9)
constexpr uintptr_t BE_PORT = 0x249b8, BE_SOCKET = 0x249bc, BE_BASE = 0x249c8, BE_STATE = 0x249d0, BIND_CLIENT = 0x8930;
constexpr uintptr_t ST_RANK_NATIVE = 936, ST_SLOT_CALLS = 1456, ST_LAST_SLOT = 1460, ST_HISTORY_COUNT = 1900, ST_HISTORY = 1904;
constexpr uintptr_t NAT_TYPE = 0x17d5c90, CUSTOM_SELECTION = 0x17d167c, NET_MENU_MODE = 0x17d603c;
constexpr uint32_t DISCOVERY_PORT = 9012, MSG_JOIN = 0xcf, JOIN_SIZE = 0x80, STAMPED = 0x20000000u;
enum : int { EB_SLOT_CALLS = 67, EB_LAST_SLOT = 68, EB_ADDED = 80, EB_REMOVED = 81, EB_CONNECTIONS = 86 };

// the helper's note of a backend slot call (4.16 step 1)
U3X_FN void note_slot(void *be, uint32_t slot) {
    uint32_t *eb = ext_eb();
    eb[EB_SLOT_CALLS]++;
    eb[EB_LAST_SLOT] = slot;
    uint8_t *state = get<uint8_t *>(be, BE_STATE);
    if (!state) return;
    put<uint32_t>(state, ST_SLOT_CALLS, get<uint32_t>(state, ST_SLOT_CALLS) + 1);
    put<uint32_t>(state, ST_LAST_SLOT, slot);
    const uint32_t recorded = get<uint32_t>(state, ST_HISTORY_COUNT);
    state[ST_HISTORY + recorded % 32] = static_cast<uint8_t>(slot);
    put<uint32_t>(state, ST_HISTORY_COUNT, recorded + 1);
}

}  // namespace g07

// ================================================================ A. socket class

// 4.1: the discovery socket
U3X_FN int32_t u3x_sock_udp(void *s, uint32_t port, int broadcast, int32_t receive_buffer) {
    using namespace g07;
    Sock *sock = static_cast<Sock *>(s);
    if (is_open(sock) && sock->port == port) return 0;
    u3h_net_pick();
    if (!has_import(IMP_SOCKET) || !has_import(IMP_SETOPT) || !has_import(IMP_BIND)) return -1;
    if (!create(sock, name_lan, UDP)) return -1;

    int32_t r = u3x_sock_setopt(sock->fd, LEVEL_SOCKET, OPT_NONBLOCK, 1);
    if (r >= 0 && broadcast) r = u3x_sock_setopt(sock->fd, LEVEL_SOCKET, OPT_BROADCAST, 1);
    if (r < 0) { u3x_sock_error(-1); return -1; }   // the descriptor stays in the record
    if (receive_buffer) u3x_sock_setopt(sock->fd, LEVEL_SOCKET, OPT_BUFFER, receive_buffer);

    r = u3x_sock_bind(sock->fd, port);
    if (r < 0) { u3x_sock_error(r); return -1; }    // the descriptor stays in the record
    u3x_sock_local(sock, port);
    return 0;
}

// 4.2: a listening TCP socket
U3X_FN int32_t u3x_sock_listen(void *s, uint32_t port) {
    using namespace g07;
    Sock *sock = static_cast<Sock *>(s);
    if (is_open(sock) && sock->port == port) return 0;
    u3h_net_pick();
    if (!has_import(IMP_SOCKET) || !has_import(IMP_LISTEN)) return -1;
    if (!create(sock, name_room, TCP)) return -1;

    int32_t r = u3x_sock_setopt(sock->fd, LEVEL_SOCKET, OPT_REUSE, 1);
    if (r >= 0) r = u3x_sock_bind(sock->fd, port);
    if (r >= 0) r = import<ListenFn>(IMP_LISTEN)(sock->fd, BACKLOG);
    if (r < 0) {
        u3x_sock_error(r);
        u3x_sock_close(sock);
        return -1;
    }
    u3x_sock_setopt(sock->fd, LEVEL_SOCKET, OPT_NONBLOCK, 1);
    u3x_sock_local(sock, port);
    return 0;
}

// 4.3: a connected TCP client socket (whatever the record held is overwritten)
U3X_FN int32_t u3x_sock_connect(void *s, uint32_t ip, uint32_t port, int nodelay, int nonblocking) {
    using namespace g07;
    Sock *sock = static_cast<Sock *>(s);
    u3h_net_pick();
    if (!has_import(IMP_SOCKET) || !has_import(IMP_CONNECT)) return -1;
    if (!create(sock, name_room, TCP)) return -1;

    if (nodelay) u3x_sock_setopt(sock->fd, LEVEL_TCP, OPT_TCP_NODELAY, 1);
    u3x_sock_setopt(sock->fd, LEVEL_SOCKET, OPT_RECV_TIMEOUT, TIMEOUT_US);
    u3x_sock_setopt(sock->fd, LEVEL_SOCKET, OPT_SEND_TIMEOUT, TIMEOUT_US);
    if (nonblocking && u3x_sock_setopt(sock->fd, LEVEL_SOCKET, OPT_NONBLOCK, 1) < 0) {
        u3x_sock_close(sock);
        return -1;
    }

    uint8_t address[16];
    socket_address(address, ip, port);
    const ConnectFn connect = import<ConnectFn>(IMP_CONNECT);
    int32_t r = 0;
    for (uint32_t tries = 0; tries < CONNECT_TRIES; tries++) {
        r = connect(sock->fd, address, 16);
        const bool under_way = r < 0 && (error_code(r) == ERR_INPROGRESS || error_code(r) == ERR_ALREADY);
        if (!under_way) break;
    }
    if (r >= 0 || error_code(r) == ERR_ISCONN) {
        sock->ip = ip;
        sock->port = port;
        return 0;
    }
    u3x_sock_error(r);
    u3x_sock_close(sock);
    return -1;
}

// 4.4: 1 = something to read (data or end of stream), 0 = nothing yet, -1 = broken or not open
U3X_FN int32_t u3x_sock_poll(const void *s) {
    using namespace g07;
    const Sock *sock = static_cast<const Sock *>(s);
    const ReceiveFn receive = import<ReceiveFn>(IMP_RECEIVE);
    if (!is_open(sock) || !receive) return -1;
    uint8_t scratch = 0;
    const int32_t r = receive(sock->fd, &scratch, 1, FLAG_PEEK | FLAG_DONTWAIT);
    if (r >= 0) return 1;
    const uint32_t code = error_code(r);
    return code == ERR_AGAIN || code == ERR_INPROGRESS || code == ERR_ALREADY || code == ERR_NOTCONN ? 0 : -1;
}

// 4.5: one receive; an error closes the record
U3X_FN int32_t u3x_sock_recv(void *s, void *b, uint32_t n, int block) {
    using namespace g07;
    Sock *sock = static_cast<Sock *>(s);
    const int32_t r = import<ReceiveFn>(IMP_RECEIVE)(sock->fd, b, n, block ? 0 : FLAG_DONTWAIT);
    if (r >= 0) return r;
    if (!block && error_code(r) == ERR_AGAIN) return r;
    u3x_sock_error(r);
    u3x_sock_close(sock);
    return -1;
}

// 4.6: an accepted descriptor becomes a record; a loopback or unknown peer gets this machine's address
U3X_FN void u3x_sock_adopt(void *s, int32_t fd, uint32_t ip, uint32_t port) {
    using namespace g07;
    Sock *sock = static_cast<Sock *>(s);
    sock->fd = fd;
    sock->port = port;
    sock->ip = ip == LOOPBACK || ip == 0 ? u3x_sock_local_ip() : ip;
}

// ================================================================ B. byte stream (big-endian fields)

// 4.7: what does not fit is dropped, the position advances all the same
U3X_FN void u3x_stream_put(void *w, uint64_t v, int bytes) {
    g07::Stream *stream = static_cast<g07::Stream *>(w);
    for (int i = bytes - 1; i >= 0; i--) {
        const uint8_t byte = i < 8 ? static_cast<uint8_t>(v >> (8 * i)) : 0;
        if (stream->position < stream->limit) stream->buffer[stream->position] = byte;
        stream->position++;
    }
}

// 4.8: bytes behind the limit read as 0
U3X_FN uint64_t u3x_stream_get(void *r, int bytes) {
    g07::Stream *stream = static_cast<g07::Stream *>(r);
    uint64_t v = 0;
    for (int i = 0; i < bytes; i++) {
        v <<= 8;
        if (stream->position < stream->limit) v += stream->buffer[stream->position];
        stream->position++;
    }
    return v;
}

// ================================================================ C. connection table

// 4.9: the address (0x2c) and the packet object stay
U3X_FN void u3x_cm_reset_entry(uint8_t *entry) {
    using namespace g07;
    zero_bytes(entry, CE_NAME + NAME);   // id, in use, the two state bytes, the name
    put<uint16_t>(entry, CE_PORT, 0);
    put<uint32_t>(entry, CE_HANDLE, 0);
    put<int32_t>(entry, CE_REFS, 0);
    u3h_gt_entry_reset(entry);
}

// 4.10
U3X_FN void u3x_cm_init_lan(uintptr_t base) {
    using namespace g07;
    uint8_t *table = cm_table(base);
    table[CM_LAN_MODE] = 1;
    copy_bytes(table + CM_OWN_NAME, reinterpret_cast<const uint8_t *>(base + OWN_NAME), NAME);
    for (uint32_t i = 0; i < CM_COUNT; i++) u3x_cm_reset_entry(cm_entry(table, i));
    put<uint32_t>(table, CM_BLOCKED_COUNT, 0);
    put<int32_t>(table, CM_NEXT_ID, 0x1234);
}

// 4.11: the connection id for a name (one more reference, or a new entry), -1 = blocked or table full
U3X_FN int32_t u3x_cm_get(uintptr_t base, const char *name) {
    using namespace g07;
    uint8_t *table = cm_table(base);

    uint32_t blocked = get<uint32_t>(table, CM_BLOCKED_COUNT);
    if (blocked > CM_COUNT) blocked = CM_COUNT;
    for (uint32_t i = 0; i < blocked; i++)
        if (same_name(name, text(table + CM_BLOCKED + i * NAME))) return -1;

    uint8_t *unused = nullptr;
    for (uint32_t i = 0; i < CM_COUNT; i++) {
        uint8_t *entry = cm_entry(table, i);
        if (!entry[CE_USED]) {
            if (!unused) unused = entry;
        } else if (same_name(name, text(entry + CE_NAME))) {
            put<int32_t>(entry, CE_REFS, get<int32_t>(entry, CE_REFS) + 1);
            return get<int32_t>(entry, CE_ID);
        }
    }
    if (!unused) return -1;

    uint8_t *entry = unused;
    u3x_cm_reset_entry(entry);
    const int32_t id = get<int32_t>(table, CM_NEXT_ID);
    put<int32_t>(entry, CE_ID, id);
    put<int32_t>(table, CM_NEXT_ID, static_cast<int32_t>(static_cast<uint32_t>(id) + 1));
    entry[CE_USED] = 1;
    put<int32_t>(entry, CE_REFS, 1);
    copy_bytes(entry + CE_NAME, reinterpret_cast<const uint8_t *>(name), NAME);
    put<uint32_t>(entry, CE_HANDLE, 0);
    put<uint16_t>(entry, CE_PORT, 0);
    packet_reset(base, entry);
    ext_eb()[EB_CONNECTIONS]++;

    // the peer's address; this machine's own entry gets none
    if (same_name(name, text(table + CM_OWN_NAME))) return id;
    if (!table[CM_LAN_MODE]) {
        u3h_onb_cm_np(base, entry, name);
        return id;
    }
    uint32_t ip;
    const bool known = lan_address(table, name, &ip);
    put<uint32_t>(entry, CE_IP, ip);
    put<uint16_t>(entry, CE_PORT, known ? GAME_PORT : 0);
    put<uint32_t>(entry, CE_HANDLE, 1);
    return id;
}

// 4.12: one reference less; the last one frees the entry (id, name, address and port stay in it)
U3X_FN int32_t u3x_cm_release(uintptr_t base, int32_t id) {
    using namespace g07;
    uint8_t *table = cm_table(base);
    for (uint32_t i = 0; i < CM_COUNT; i++) {
        uint8_t *entry = cm_entry(table, i);
        if (get<int32_t>(entry, CE_ID) != id) continue;
        if (!entry[CE_USED]) return -1;   // the search ends at the first entry with this id
        const int32_t before = get<int32_t>(entry, CE_REFS);
        put<int32_t>(entry, CE_REFS, static_cast<int32_t>(static_cast<uint32_t>(before) - 1));
        if (before < 2) {
            packet_reset(base, entry);
            entry[CE_USED] = 0;
            if (table[CM_LAN_MODE]) put<uint32_t>(entry, CE_HANDLE, 0);
            else u3h_pl_drop(text(entry + CE_NAME));
        }
        return 0;
    }
    return -1;
}

// 4.13
U3X_FN void u3x_cm_shutdown(uintptr_t base) {
    uint8_t *table = g07::cm_table(base);
    for (uint32_t i = 0; i < g07::CM_COUNT; i++) g07::packet_reset(base, g07::cm_entry(table, i));
}

// ================================================================ E. join message and backend slot 0

// 4.14: the helper's fields in the last 16 bytes of a full member data block
U3X_FN void u3x_lnb_rank_stamp(void *be, uint8_t *md, uint32_t len) {
    using namespace g07;
    uint8_t *state = get<uint8_t *>(be, BE_STATE);
    if (!state || len != DATA_MAX) return;
    const uintptr_t base = get<uint64_t>(be, BE_BASE);

    // the profile is looked up first in every stamping call, also when there is no rank native
    uint8_t *profile = u3h_player_profile(base);
    const uintptr_t rank_native = get<uintptr_t>(state, ST_RANK_NATIVE);
    if (rank_native && profile) put<uint32_t>(md, 0x3c, game_call<uint32_t, uint8_t *>(rank_native, profile) | STAMPED);

    const uint64_t room = *u3h_lnb_match_room();
    put<uint64_t>(md, 0x34, room);
    put<int16_t>(md, 0x30, at<int16_t>(base + CUSTOM_SELECTION));
    uint8_t guest_wait = 0, host_playing = 0, at_start = 0;
    uint64_t followed = 0;
    u3h_lnb_marks(&guest_wait, &host_playing, &at_start, &followed);
    md[0x32] = u3x_member_status(at<uint32_t>(base + NET_MENU_MODE) == 2, guest_wait, host_playing);
    md[0x33] = u3x_member_start(at_start, followed, room);

    u3h_ng_stamp(base, state, md);
}

// 4.15: the 0x80-byte room join message on the binding's client socket
U3X_FN void u3x_lnb_send_join(void *be, void *bind, uint8_t *ss, int host, int32_t *res) {
    using namespace g07;
    const uintptr_t base = get<uint64_t>(be, BE_BASE);
    uint8_t message[JOIN_SIZE];
    zero_bytes(message, JOIN_SIZE);
    put<uint32_t>(message, 0, MSG_JOIN);
    put<uint32_t>(message, 4, at<uint32_t>(base + NAT_TYPE));
    copy_bytes(message + 0x10, reinterpret_cast<const uint8_t *>(base + OWN_NAME), NAME);
    message[0x34] = static_cast<uint8_t>(host);

    const uint32_t own = get<uint32_t>(ss, S_DATA_LENGTH);
    const uint32_t length = own ? own : DATA_MAX;   // no data: an empty block that only carries the stamp
    copy_bytes(message + 0x3c, ss + S_DATA, own < DATA_MAX ? own : DATA_MAX);
    put<uint32_t>(message, 0x38, length);
    u3x_lnb_rank_stamp(be, message + 0x3c, length);

    *res = u3x_sock_send(static_cast<uint8_t *>(bind) + BIND_CLIENT, message, JOIN_SIZE, 1);
}

// 4.16: backend slot 0 "Init": opens the discovery socket
U3X_FN int64_t u3x_lnb_s0(void *be) {
    using namespace g07;
    note_slot(be, 0);
    put<uint32_t>(be, BE_PORT, DISCOVERY_PORT);
    return u3x_sock_udp(static_cast<uint8_t *>(be) + BE_SOCKET, DISCOVERY_PORT, 1, 0);
}

// ================================================================ D. session member slots

// 4.17: the member's index (the one it already has, or a new slot), -1 = no slot free
U3X_FN int32_t u3x_add_member(uintptr_t base, uint8_t *session, const void *info, int you, int host) {
    using namespace g07;
    const char *name = get<const char *>(info, 0);
    if (const uint8_t *member = member_named(session, name)) return get<int32_t>(member, M_INDEX);

    uint8_t *slot = nullptr;
    for (uint32_t i = 0; i < S_SLOT_COUNT && !slot; i++)
        if (!slot_at(session, i)[M_USED]) slot = slot_at(session, i);
    if (!slot) return -1;

    ext_eb()[EB_ADDED]++;
    const int32_t connection = u3x_cm_get(base, name);   // before anything is written to the slot

    const uint32_t id = get<uint32_t>(info, 8) & 0xffff;
    const int32_t index = get<int32_t>(session, S_NEXT_INDEX);
    put<int32_t>(session, S_NEXT_INDEX, static_cast<int32_t>(static_cast<uint32_t>(index) + 1));
    put<int32_t>(slot, M_CONNECTION, connection);
    put<uint32_t>(slot, M_ID, id);
    put<uint32_t>(slot, M_NAT, get<uint32_t>(info, 0xc) & 0xff);
    copy_bytes(slot, reinterpret_cast<const uint8_t *>(name), NAME);
    put<int32_t>(slot, M_INDEX, index);
    slot[M_USED] = 1;
    uint32_t data_length = 0;
    if (get<uint32_t>(info, 0x20) == 1) {
        data_length = get<uint32_t>(info, 0x10);
        if (data_length > DATA_MAX) data_length = DATA_MAX;
        copy_bytes(slot + M_DATA, get<const uint8_t *>(info, 0x18), data_length);
    }
    put<uint32_t>(slot, M_DATA_LENGTH, data_length);

    if (you) put<uint32_t>(session, S_OWN_ID, id);
    if (host) put<uint32_t>(session, S_HOST_ID, id);

    const uint32_t players = player_count(base, session);
    const uint32_t capacity = get<uint32_t>(session, S_CAPACITY);
    if (capacity && capacity < players) {
        // over capacity: the member stays, the backend is asked to kick it
        void *backend = get<void *>(session, S_BACKEND);
        if (backend) game_call<void, void *, uint8_t *, uint32_t, uint32_t>(method(backend, BACKEND_KICK), backend, session, id, 0u);
    } else if (capacity) {
        game_call<void, uint8_t *, uint8_t *>(method(session, SESSION_JOINED), session, slot);
    }
    return get<int32_t>(slot, M_INDEX);   // as it stands now: the call above may have changed it
}

// 4.18
U3X_FN int32_t u3x_remove_member(uintptr_t base, uint8_t *session, const char *name) {
    using namespace g07;
    uint8_t *slot = member_named(session, name);
    if (!slot) return -1;
    ext_eb()[EB_REMOVED]++;
    game_call<void, uint8_t *, uint8_t *>(method(session, SESSION_LEFT), session, slot);
    u3x_cm_release(base, get<int32_t>(slot, M_CONNECTION));
    slot[M_USED] = 0;
    put<int32_t>(slot, M_CONNECTION, -1);
    return 0;
}
