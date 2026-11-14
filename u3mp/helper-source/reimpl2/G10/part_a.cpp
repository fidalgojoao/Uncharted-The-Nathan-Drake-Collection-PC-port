// G10 part A: the LAN backend object and its function table, LAN discovery (Update / Search), Shutdown,
// AttachSession, the two datagram functions and the party size. Written from spec G10_lan_backend_slots.md.

extern "C" {
// the C part and other groups (spec 2.3)
int64_t u3h_lnb_call(void *object, int slot);
void u3h_lnb_follow_tick(void *be, uintptr_t base, void *s);
uint8_t *u3h_lnb_udpbuf(void);
void u3h_lnb_cli_update(uintptr_t base, void *bind);
void u3x_lnb_srv_update(void *v);
int u3x_lnb_srv_running(const void *v);
uint32_t u3x_lnb_srv_count(const void *v);
void u3x_lnb_srv_ctor(void *v);
void u3x_stream_put(void *w, uint64_t v, int bytes);
uint64_t u3x_stream_get(void *r, int bytes);
int u3x_sock_open(const void *s);
void u3x_sock_init(void *s);
int32_t u3x_sock_close(void *s);
int u3x_party_players(const uint8_t *session);
int64_t u3x_lnb_s14(void *be, uint8_t *ss);
int64_t u3x_lnb_s_nop(void *);
// part B's slots (only their addresses are taken here)
int64_t u3x_lnb_s4(void *be, uint8_t *ss, uint32_t maxp);
int64_t u3x_lnb_s6(void *be, uint8_t *ss, uint64_t room);
int64_t u3x_lnb_s7(void *be, uint8_t *ss);
int64_t u3x_lnb_s9(void *be, uint8_t *ss, uint32_t id);
int64_t u3x_lnb_s11(void *be, uint8_t *ss, int32_t lock);
int64_t u3x_lnb_s12(void *be, uint8_t *ss, const uint8_t *data, uint32_t len);
// part A (spec 2.1)
void u3x_lnb_note(void *be, int slot);
int u3x_lnb_bound(void *be, const uint8_t *ss);
void u3x_lnb_ctor(void *be, void **vt);
void u3x_lnb_vtable(void **vt, void *slot0);
int64_t u3x_lnb_s1(void *be);
int64_t u3x_lnb_s2(void *be);
int64_t u3x_lnb_s3(void *be, uint8_t *ss);
int64_t u3x_lnb_s5(void *be);
int32_t u3x_sock_recvfrom(const void *s, void *b, uint32_t n, uint32_t *ip, uint32_t *port);
int32_t u3x_sock_sendto(const void *s, const void *b, uint32_t n, uint32_t ip, uint32_t port);
int u3x_lan_party_size(uintptr_t base);
}

namespace g10a {

// ---- layouts (spec 3.2 to 3.5)
constexpr uintptr_t BE_NAME = 8;              // 0x24 bytes: this player's network name
constexpr uintptr_t BE_ROOM_COUNT = 0x50;     // s32
constexpr uintptr_t BE_ROOMS = 0x58;          // 20 entries of 0x38 bytes
constexpr uintptr_t BE_BINDINGS = 0x4b8;      // 4 bindings of 0x9140 bytes
constexpr uintptr_t BE_DISC_PORT = 0x249b8;   // u32
constexpr uintptr_t BE_DISC_SOCK = 0x249bc;   // socket record
constexpr uintptr_t BE_BASE = 0x249c8;        // u64: game image base
constexpr uintptr_t BE_STATE = 0x249d0;       // the helper's state object, or null

constexpr int BINDINGS = 4;
constexpr uintptr_t BIND_SIZE = 0x9140;
constexpr uintptr_t BIND_SERVER = 8;          // room server object
constexpr uintptr_t BIND_CLIENT = 0x8930;     // socket record
constexpr uintptr_t BIND_RECEIVED = 0x893c;   // u32

constexpr uintptr_t SRV_CAPACITY = 4;         // u32
constexpr uintptr_t SRV_ROOM_ID = 0x10;       // u64
constexpr uintptr_t SRV_LISTEN_PORT = 0x20;   // port field of the listening socket record

constexpr int ROOM_LIST_MAX = 20;
constexpr uintptr_t ROOM_ENTRY = 0x38;
constexpr uint32_t NAME_LEN = 0x24;
constexpr uint32_t NAME_BLOCK = 0x80;         // the name block of a room record on the wire
constexpr uint32_t UDP_BUF = 0x1000;          // the C part's datagram buffer

constexpr uint8_t MSG_QUERY = 0xc8, MSG_ANSWER = 0xc9;

// the G07 byte stream object
struct Stream {
    uint8_t *buf;
    uint32_t pos, limit;
};

// the platform's socket address
struct SockAddr {
    uint8_t len, family, port_hi, port_lo;
    uint32_t ip;
    uint8_t zero[8];
};

template <typename T> [[gnu::always_inline]] inline T &fld(void *p, uintptr_t off) {
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + off);
}
[[gnu::always_inline]] inline uint8_t *binding(void *be, int i) {
    return static_cast<uint8_t *>(be) + BE_BINDINGS + static_cast<uintptr_t>(i) * BIND_SIZE;
}
[[gnu::always_inline]] inline void *disc_sock(void *be) { return static_cast<uint8_t *>(be) + BE_DISC_SOCK; }

// entry k of the socket import table (may be null)
template <typename F> [[gnu::always_inline]] inline F sock_import(int k) {
    return rd<F>(u3h_ext(), 0x200 + 8 * static_cast<uintptr_t>(k));
}

[[gnu::always_inline]] inline void fill_addr(SockAddr &a, uint32_t ip, uint32_t port) {
    a.len = 16;
    a.family = 2;
    a.port_hi = static_cast<uint8_t>(port >> 8);
    a.port_lo = static_cast<uint8_t>(port);
    a.ip = ip;
    for (int i = 0; i < 8; ++i) a.zero[i] = 0;
}

// a binding is advertised: it has a session, the session is a party session, its room server runs
U3X_FN bool advertised(uint8_t *bind) {
    void *session = rd<void *>(bind, 0);
    if (!session) return false;
    if (!(u3h_lnb_call(session, 0) & 0xff)) return false;
    return u3x_lnb_srv_running(bind + BIND_SERVER) != 0;
}

// answers a discovery query with the list of this machine's party rooms
U3X_FN void answer_query(void *be, uint32_t ip, uint32_t port) {
    uint8_t out[0x800];
    Stream w{out, 0, sizeof out};

    // the id byte goes in first, then the bindings are counted, then the count is written
    u3x_stream_put(&w, MSG_ANSWER, 1);
    uint32_t rooms = 0;
    for (int i = 0; i < BINDINGS; ++i)
        if (advertised(binding(be, i))) ++rooms;
    u3x_stream_put(&w, rooms, 4);

    for (int i = 0; i < BINDINGS; ++i) {
        uint8_t *bind = binding(be, i);
        if (!advertised(bind)) continue;
        const uint8_t *srv = bind + BIND_SERVER;
        u3x_stream_put(&w, rd<uint32_t>(srv, SRV_LISTEN_PORT) & 0xffff, 2);
        u3x_stream_put(&w, rd<uint64_t>(srv, SRV_ROOM_ID), 8);
        u3x_stream_put(&w, u3x_lnb_srv_count(srv), 2);
        u3x_stream_put(&w, rd<uint32_t>(srv, SRV_CAPACITY) & 0xffff, 2);
        for (uint32_t j = 0; j < NAME_BLOCK; ++j)
            u3x_stream_put(&w, j < NAME_LEN ? rd<uint8_t>(be, BE_NAME + j) : 0, 1);
    }

    u3x_sock_sendto(disc_sock(be), out, w.pos < w.limit ? w.pos : w.limit, ip, port);
    ext_eb()[74]++;
}

// takes the room records of a discovery answer into the room list
U3X_FN void read_answer(void *be, Stream *r, uint32_t ip) {
    const uint32_t records = static_cast<uint32_t>(u3x_stream_get(r, 4));
    for (uint32_t k = 0; k < records && r->pos < UDP_BUF; ++k) {
        const uint16_t port = static_cast<uint16_t>(u3x_stream_get(r, 2));
        const uint64_t room = u3x_stream_get(r, 8);
        const uint16_t connections = static_cast<uint16_t>(u3x_stream_get(r, 2));
        const uint16_t capacity = static_cast<uint16_t>(u3x_stream_get(r, 2));
        uint8_t name[NAME_LEN];
        for (uint32_t j = 0; j < NAME_BLOCK; ++j) {
            const uint8_t c = static_cast<uint8_t>(u3x_stream_get(r, 1));
            if (j < NAME_LEN) name[j] = c;
        }

        const int32_t listed = rd<int32_t>(be, BE_ROOM_COUNT);
        if (listed >= ROOM_LIST_MAX) continue;   // list full: the record is dropped
        uint8_t *entry = static_cast<uint8_t *>(be) + BE_ROOMS + static_cast<intptr_t>(listed) * static_cast<intptr_t>(ROOM_ENTRY);
        fld<uint64_t>(entry, 0) = room;
        fld<uint32_t>(entry, 8) = ip;
        fld<uint16_t>(entry, 0xc) = port;
        fld<uint16_t>(entry, 0xe) = connections;
        fld<uint16_t>(entry, 0x10) = capacity;
        for (uint32_t j = 0; j < NAME_LEN; ++j) entry[0x12 + j] = name[j];
        // the count is taken from the object again: with a count of -1 the entry's name lies over the count field
        fld<int32_t>(be, BE_ROOM_COUNT)++;
        ext_eb()[75]++;
    }
}

// empties the discovery socket: answers queries, collects answers
U3X_FN void discovery(void *be, uint8_t *buf) {
    using RecvFrom = int32_t (*)(int, void *, uint32_t, int, void *, uint32_t *);
    void *sock = disc_sock(be);
    if (!u3x_sock_open(sock) || !sock_import<RecvFrom>(4)) return;
    for (;;) {
        for (uint32_t i = 0; i < UDP_BUF; ++i) buf[i] = 0;
        uint32_t ip = 0, port = 0;
        const int32_t got = u3x_sock_recvfrom(sock, buf, UDP_BUF - 1, &ip, &port);
        if (got < 1) return;

        Stream r{buf, 0, static_cast<uint32_t>(got)};
        const uint8_t id = static_cast<uint8_t>(u3x_stream_get(&r, 1));
        if (id == MSG_QUERY) {
            if (ip != rd<uint32_t>(sock, 4)) answer_query(be, ip, port);   // not the own broadcast
        } else if (id == MSG_ANSWER) {
            read_answer(be, &r, ip);
        } else {
            ext_eb()[83]++;
        }
    }
}

}  // namespace g10a

// the helper's record of a slot call
extern "C" U3X_FN void u3x_lnb_note(void *be, int slot) {
    uint32_t *eb = ext_eb();
    eb[67]++;
    eb[68] = static_cast<uint32_t>(slot);
    void *state = rd<void *>(be, g10a::BE_STATE);
    if (!state) return;
    g10a::fld<uint32_t>(state, 1456)++;
    g10a::fld<uint32_t>(state, 1460) = static_cast<uint32_t>(slot);
    const uint32_t h = rd<uint32_t>(state, 1900);
    g10a::fld<uint8_t>(state, 1904 + h % 32) = static_cast<uint8_t>(slot);
    g10a::fld<uint32_t>(state, 1900) = h + 1;
}

// index of the first binding that holds this session pointer, or -1
extern "C" U3X_FN int u3x_lnb_bound(void *be, const uint8_t *ss) {
    for (int i = 0; i < g10a::BINDINGS; ++i)
        if (rd<const uint8_t *>(g10a::binding(be, i), 0) == ss) return i;
    return -1;
}

// constructor, on a zeroed object
extern "C" U3X_FN void u3x_lnb_ctor(void *be, void **vt) {
    for (int i = 0; i < g10a::BINDINGS; ++i) {
        uint8_t *bind = g10a::binding(be, i);
        u3x_lnb_srv_ctor(bind + g10a::BIND_SERVER);
        u3x_sock_init(bind + g10a::BIND_CLIENT);
        g10a::fld<void *>(bind, 0) = nullptr;
        g10a::fld<uint32_t>(bind, g10a::BIND_RECEIVED) = 0;
    }
    u3x_sock_init(g10a::disc_sock(be));
    g10a::fld<void **>(be, 0) = vt;
}

// fills the object's function table; the addresses are taken in code (no static pointer table)
extern "C" U3X_FN void u3x_lnb_vtable(void **vt, void *slot0) {
    for (int i = 0; i < 32; ++i) vt[i] = reinterpret_cast<void *>(&u3x_lnb_s_nop);
    vt[0] = slot0;
    vt[1] = reinterpret_cast<void *>(&u3x_lnb_s1);
    vt[2] = reinterpret_cast<void *>(&u3x_lnb_s2);
    vt[3] = reinterpret_cast<void *>(&u3x_lnb_s3);
    vt[4] = reinterpret_cast<void *>(&u3x_lnb_s4);
    vt[5] = reinterpret_cast<void *>(&u3x_lnb_s5);
    vt[6] = reinterpret_cast<void *>(&u3x_lnb_s6);
    vt[7] = reinterpret_cast<void *>(&u3x_lnb_s7);
    vt[9] = reinterpret_cast<void *>(&u3x_lnb_s9);
    vt[11] = reinterpret_cast<void *>(&u3x_lnb_s11);
    vt[12] = reinterpret_cast<void *>(&u3x_lnb_s12);
    vt[14] = reinterpret_cast<void *>(&u3x_lnb_s14);
}

// slot 1, Update: party follow, discovery, then every attached binding's room server and room client
extern "C" U3X_FN int64_t u3x_lnb_s1(void *be) {
    ext_eb()[84]++;   // no slot note for this slot
    u3h_lnb_follow_tick(be, rd<uint64_t>(be, g10a::BE_BASE), rd<void *>(be, g10a::BE_STATE));
    g10a::discovery(be, u3h_lnb_udpbuf());
    for (int i = 0; i < g10a::BINDINGS; ++i) {
        uint8_t *bind = g10a::binding(be, i);
        if (!rd<void *>(bind, 0)) continue;
        u3x_lnb_srv_update(bind + g10a::BIND_SERVER);
        u3h_lnb_cli_update(rd<uint64_t>(be, g10a::BE_BASE), bind);
    }
    return 0;
}

// slot 2, Shutdown: closes the discovery socket and the open client sockets of attached bindings
extern "C" U3X_FN int64_t u3x_lnb_s2(void *be) {
    u3x_lnb_note(be, 2);
    int32_t last = u3x_sock_close(g10a::disc_sock(be));
    for (int i = 0; i < g10a::BINDINGS; ++i) {
        uint8_t *bind = g10a::binding(be, i);
        if (rd<void *>(bind, 0) && u3x_sock_open(bind + g10a::BIND_CLIENT)) last = u3x_sock_close(bind + g10a::BIND_CLIENT);
    }
    return last;
}

// slot 3, AttachSession: the session's binding, or the first free one
extern "C" U3X_FN int64_t u3x_lnb_s3(void *be, uint8_t *ss) {
    u3x_lnb_note(be, 3);
    const int have = u3x_lnb_bound(be, ss);
    if (have >= 0) return have;   // also a null session "found" in a free binding: nothing is written
    for (int i = 0; i < g10a::BINDINGS; ++i) {
        uint8_t *bind = g10a::binding(be, i);
        if (rd<void *>(bind, 0)) continue;
        g10a::fld<uint8_t *>(bind, 0) = ss;
        g10a::fld<void *>(ss, 8) = be;
        return i;
    }
    return -1;
}

// slot 5, Search: empties the room list and broadcasts a discovery query
extern "C" U3X_FN int64_t u3x_lnb_s5(void *be) {
    using SendTo = int32_t (*)(int, const void *, uint32_t, int, const void *, uint32_t);
    u3x_lnb_note(be, 5);
    g10a::fld<int32_t>(be, g10a::BE_ROOM_COUNT) = 0;
    // the query is built on every Search, also when it is not sent
    uint8_t query[0x10];
    g10a::Stream w{query, 0, sizeof query};
    u3x_stream_put(&w, g10a::MSG_QUERY, 1);
    void *sock = g10a::disc_sock(be);
    if (u3x_sock_open(sock) && g10a::sock_import<SendTo>(3)) {
        u3x_sock_sendto(sock, query, w.pos, 0xffffffffu, rd<uint32_t>(be, g10a::BE_DISC_PORT) & 0xffff);
        ext_eb()[73]++;
    }
    return 0;
}

// one datagram in; the sender's address and port are given back when something arrived
extern "C" U3X_FN int32_t u3x_sock_recvfrom(const void *s, void *b, uint32_t n, uint32_t *ip, uint32_t *port) {
    using RecvFrom = int32_t (*)(int, void *, uint32_t, int, void *, uint32_t *);
    g10a::SockAddr from;
    g10a::fill_addr(from, 0, *port);
    uint32_t from_len = 16;
    const int32_t got = g10a::sock_import<RecvFrom>(4)(rd<int32_t>(s, 0), b, n, 0, &from, &from_len);
    if (got > 0) {
        *ip = from.ip;
        *port = static_cast<uint32_t>(from.port_hi) << 8 | from.port_lo;
    }
    return got;
}

// one datagram out
extern "C" U3X_FN int32_t u3x_sock_sendto(const void *s, const void *b, uint32_t n, uint32_t ip, uint32_t port) {
    using SendTo = int32_t (*)(int, const void *, uint32_t, int, const void *, uint32_t);
    g10a::SockAddr to;
    g10a::fill_addr(to, ip, port);
    return g10a::sock_import<SendTo>(3)(rd<int32_t>(s, 0), b, n, 0, &to, 16);
}

// number of players in the party; without a party room, the local players
extern "C" U3X_FN int u3x_lan_party_size(uintptr_t base) {
    const uint8_t *party = reinterpret_cast<const uint8_t *>(base + 0x17dc298);
    if (rd<uint64_t>(party, 0x18) != 0) return u3x_party_players(party);
    const bool second_local = at<uint8_t>(base + 0x17d7b06) != 0 || at<int32_t>(base + 0x17f2fc0) == 3;
    return second_local ? 2 : 1;
}
