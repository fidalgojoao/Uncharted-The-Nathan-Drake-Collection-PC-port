// G09. LAN backend: the room server (written from spec G09_lan_room_server.md).
// One TCP server per session binding: it accepts the members' connections, gives each a member id, keeps the member
// list and tells the members who joined, who left, whose data changed; it relays two host notices and a kick.

extern "C" {
// neighbours (C part and group G07)
uint8_t *u3h_ext(void);
void u3x_sock_init(void *s);
int32_t u3x_sock_close(void *s);
uint32_t u3x_sock_local_ip(void);
int32_t u3x_sock_listen(void *s, uint32_t port);
void u3x_sock_adopt(void *s, int32_t fd, uint32_t ip, uint32_t port);
int32_t u3x_sock_poll(const void *s);
int32_t u3x_sock_recv(void *s, void *b, uint32_t n, int block);
int32_t u3x_sock_send(const void *s, const void *b, uint32_t n, int block);

// this group
void u3x_lnb_srv_ctor(void *v);
int u3x_lnb_srv_running(const void *v);
uint32_t u3x_lnb_srv_count(const void *v);
int32_t u3x_lnb_srv_start(void *v, uint64_t arg, uint32_t port, uint32_t maxp);
void u3x_lnb_srv_stop(void *v);
uint32_t u3x_lnb_srv_msg(void *v, int k);
void u3x_lnb_srv_update(void *v);
int32_t u3x_sock_accept(const void *s, uint32_t *ip, uint32_t *port);
}

namespace g09 {
namespace {

constexpr int SLOTS = 16;            // client slots of a server
constexpr int KICKS = 16;            // kick list entries
constexpr int NAME = 0x24;           // bytes of a player name field
constexpr int DATA = 0x40;           // bytes of a member data block
constexpr uint32_t BUFFER = 0x800;   // bytes of a slot's receive buffer
constexpr int LISTED = 10;           // member records a join reply has room for

// ---- the objects (layouts fixed: G10 and the C part rely on them)

struct Sock {            // socket record
    int32_t fd;          // open when > 0
    uint32_t ip;         // socket-address form
    uint32_t port;       // host order
};

struct Client {          // one client slot
    uint8_t name[NAME];
    uint32_t id;         // member id
    uint32_t nat;
    uint8_t unused[8];
    uint32_t data_len;
    uint8_t data[DATA];
    uint8_t in_use;      // a connection occupies the slot
    uint8_t joined;      // it has sent a valid join
    uint8_t host;
    uint8_t unused2;
    Sock sock;
    uint32_t waiting;    // bytes in the receive buffer
    uint8_t buffer[BUFFER];
};

struct Server {
    uint32_t next_id;    // the id the next connection gets
    uint32_t capacity;
    uint64_t arg;        // stored only
    uint64_t room;       // room id
    Sock listening;
    uint32_t state;      // 1 = running
    int32_t kicked[KICKS];
    Client client[SLOTS];
};

static_assert(sizeof(Sock) == 12);
static_assert(sizeof(Client) == 0x88c);
static_assert(sizeof(Server) == 0x8928);
static_assert(__builtin_offsetof(Client, in_use) == 0x78 && __builtin_offsetof(Client, sock) == 0x7c);
static_assert(__builtin_offsetof(Client, waiting) == 0x88 && __builtin_offsetof(Client, buffer) == 0x8c);
static_assert(__builtin_offsetof(Server, listening) == 0x18 && __builtin_offsetof(Server, state) == 0x24);
static_assert(__builtin_offsetof(Server, kicked) == 0x28 && __builtin_offsetof(Server, client) == 0x68);

// ---- messages to clients (every byte not set is zero)

struct MemberRecord {    // 0x70 bytes, 0x6e written
    uint8_t name[NAME];
    uint32_t ip;
    uint16_t id;
    uint8_t nat;
    uint8_t data_len;
    uint8_t you;
    uint8_t host;
    uint8_t data[DATA];
    uint8_t zero[2];
};

struct JoinReply {       // 0xc9
    uint32_t id;
    uint32_t zero;
    uint64_t room;
    uint16_t capacity;
    uint16_t count;
    uint32_t zero2;
    MemberRecord member[LISTED];
};

struct MemberJoined {    // 0xca
    uint32_t id;
    uint32_t zero;
    uint64_t room;
    MemberRecord member;
};

struct MemberLeft {      // 0xcb
    uint32_t id;
    uint16_t member;
    uint16_t zero;
    uint64_t room;
    uint8_t name[NAME];
    uint8_t zero2[4];
};

struct MemberData {      // 0xce
    uint32_t id;
    uint16_t member;
    uint16_t zero;
    uint64_t room;
    uint32_t data_len;
    uint8_t data[DATA];
    uint8_t zero2[4];
};

struct Kicked {          // 0xcc
    uint32_t id;
    uint32_t zero;
    uint8_t room[8];     // as the kick message had it
};

static_assert(sizeof(MemberRecord) == 0x70 && __builtin_offsetof(MemberRecord, data) == 0x2e);
static_assert(sizeof(JoinReply) == 0x478 && __builtin_offsetof(JoinReply, member) == 0x18);
static_assert(sizeof(MemberJoined) == 0x80);
static_assert(sizeof(MemberLeft) == 0x38);
static_assert(sizeof(MemberData) == 0x58 && __builtin_offsetof(MemberData, data) == 0x14);
static_assert(sizeof(Kicked) == 0x10);

// message ids and lengths from a client
constexpr uint32_t MSG_JOIN = 0xcf, MSG_DATA = 0xcd, MSG_KICK = 0xd1, MSG_SETTINGS = 0xd2, MSG_ELIMINATION = 0xd3;
constexpr uint32_t LEN_JOIN = 0x80, LEN_DATA = 0x50, LEN_KICK = 0x10, LEN_SETTINGS = 0x20, LEN_ELIMINATION = 0x10;

U3X_BSS uint32_t room_counter;   // one for all servers, only ever grows
U3X_BSS JoinReply join_reply;    // where a join reply is built (main thread only)

U3X_FN void copy(void *to, const void *from, uint32_t n) {
    uint8_t *d = static_cast<uint8_t *>(to);
    const uint8_t *s = static_cast<const uint8_t *>(from);
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

U3X_FN void clear(void *p, uint32_t n) {
    uint8_t *d = static_cast<uint8_t *>(p);
    for (uint32_t i = 0; i < n; i++) d[i] = 0;
}

// name equality: equal up to a common NUL, or in all 0x24 bytes
U3X_FN bool same_name(const uint8_t *a, const uint8_t *b) {
    for (int i = 0; i < NAME; i++) {
        if (a[i] != b[i]) return false;
        if (!a[i]) return true;
    }
    return true;
}

U3X_FN bool is_member(const Client *c) { return c->in_use && c->joined; }

// one waiting send of a whole message; the result is not looked at
U3X_FN void send(const Client *c, const void *message, uint32_t n) { u3x_sock_send(&c->sock, message, n, 1); }

// to every member except slot `except`, in slot order
U3X_FN void broadcast(const Server *s, int except, const void *message, uint32_t n) {
    for (int j = 0; j < SLOTS; j++)
        if (j != except && is_member(&s->client[j])) send(&s->client[j], message, n);
}

// the member record of a slot (its last two bytes are left alone)
U3X_FN void member_record(MemberRecord *r, const Client *c, uint8_t you) {
    copy(r->name, c->name, NAME);
    r->ip = c->sock.ip;
    r->id = static_cast<uint16_t>(c->id);
    r->nat = static_cast<uint8_t>(c->nat);
    r->data_len = static_cast<uint8_t>(c->data_len);
    r->you = you;
    r->host = c->host;
    copy(r->data, c->data, DATA);
}

// a connection is gone: tell the others, close it, free the slot
U3X_FN void drop(Server *s, int k) {
    Client *c = &s->client[k];
    MemberLeft m;
    clear(&m, sizeof m);
    m.id = 0xcb;
    m.member = static_cast<uint16_t>(c->id);
    m.room = s->room;
    copy(m.name, c->name, NAME);
    broadcast(s, k, &m, sizeof m);
    u3x_sock_close(&c->sock);
    c->in_use = 0;
}

// a complete join message is at the start of slot k's buffer
U3X_FN void join(Server *s, int k) {
    Client *c = &s->client[k];
    const uint8_t *m = c->buffer;
    JoinReply *reply = &join_reply;
    clear(reply, sizeof *reply);
    reply->id = 0xc9;
    reply->capacity = static_cast<uint16_t>(s->capacity);

    // name taken: a reply without room and members, and the slot stays as it is
    for (int j = 0; j < SLOTS; j++)
        if (is_member(&s->client[j]) && same_name(s->client[j].name, m + 0x10)) {
            send(c, reply, sizeof *reply);
            return;
        }

    c->nat = rd<uint32_t>(m, 4);
    copy(c->name, m + 0x10, NAME);
    c->joined = 1;
    c->data_len = rd<uint32_t>(m, 0x38);
    c->host = m[0x34];
    copy(c->data, m + 0x3c, DATA);

    // the reply: the first members in slot order, the joiner's own record marked
    reply->room = s->room;
    uint16_t count = 0;
    for (int j = 0; j < SLOTS && count < LISTED; j++)
        if (is_member(&s->client[j])) member_record(&reply->member[count++], &s->client[j], j == k);
    reply->count = count;
    send(c, reply, sizeof *reply);

    MemberJoined joined;
    clear(&joined, sizeof joined);
    joined.id = 0xca;
    joined.room = s->room;
    member_record(&joined.member, c, 0);
    broadcast(s, k, &joined, sizeof joined);
}

// slot k's member data changed
U3X_FN void data_changed(Server *s, int k) {
    Client *c = &s->client[k];
    c->data_len = rd<uint32_t>(c->buffer, 4);
    copy(c->data, c->buffer + 0x10, DATA);
    MemberData m;
    clear(&m, sizeof m);
    m.id = 0xce;
    m.member = static_cast<uint16_t>(c->id);
    m.room = s->room;
    m.data_len = c->data_len;
    copy(m.data, c->data, DATA);
    broadcast(s, k, &m, sizeof m);
}

// the host's advanced settings: passed on with the sender's member id in the second word
U3X_FN void relay_settings(const Server *s, int k) {
    const Client *c = &s->client[k];
    uint32_t m[LEN_SETTINGS / 4];
    copy(m, c->buffer, LEN_SETTINGS);
    m[1] = c->id;
    broadcast(s, k, m, LEN_SETTINGS);
}

// a kick: note the target's address when the list has an empty (0) entry, and tell the target
U3X_FN void kick(Server *s, const uint8_t *message) {
    const uint32_t target = rd<uint32_t>(message, 4);
    for (int j = 0; j < SLOTS; j++) {
        const Client *c = &s->client[j];
        if (!is_member(c) || c->id != target) continue;
        for (int e = 0; e < KICKS; e++)
            if (s->kicked[e] == 0) {
                s->kicked[e] = static_cast<int32_t>(c->sock.ip);
                break;
            }
        Kicked m;
        m.id = 0xcc;
        m.zero = 0;
        copy(m.room, message + 8, 8);
        send(c, &m, sizeof m);
        return;
    }
}

// a new connection: refuse it or give it the first free slot
U3X_FN void admit(Server *s, int32_t fd, uint32_t ip, uint32_t port) {
    Sock peer = {0, 0, 0};
    u3x_sock_adopt(&peer, fd, ip, port);
    ext_eb()[76]++;

    bool refused = u3x_lnb_srv_count(s) >= s->capacity;
    for (int j = 0; j < SLOTS; j++) {
        const Client *c = &s->client[j];
        if (c->in_use && c->sock.ip == peer.ip && c->sock.port == peer.port) refused = true;
    }
    for (int e = 0; e < KICKS; e++)
        if (s->kicked[e] == static_cast<int32_t>(peer.ip)) refused = true;
    if (refused) {
        u3x_sock_close(&peer);
        return;
    }

    for (int j = 0; j < SLOTS; j++) {
        Client *c = &s->client[j];
        if (c->in_use) continue;
        c->sock = peer;
        c->id = s->next_id++;
        c->in_use = 1;
        c->joined = 0;
        c->waiting = 0;
        return;
    }
    // no free slot (capacity above 16): the descriptor is left as it is
}

// one connection's turn: one receive, then every complete message
U3X_FN void serve(Server *s, int k) {
    Client *c = &s->client[k];
    const int32_t readable = u3x_sock_poll(&c->sock);
    if (readable < 0) {
        drop(s, k);
        return;
    }
    if (readable == 0) return;

    const int32_t got = u3x_sock_recv(&c->sock, c->buffer + c->waiting, BUFFER - c->waiting, 1);
    if (got == 0) {
        drop(s, k);
        return;
    }
    if (got > 0) c->waiting += static_cast<uint32_t>(got);

    while (c->waiting) {
        const uint32_t used = u3x_lnb_srv_msg(s, k);
        if (!used) break;
        c->waiting -= used;
        for (uint32_t i = 0; i < c->waiting; i++) c->buffer[i] = c->buffer[i + used];
    }
}

}  // namespace
}  // namespace g09

// ---- A. life cycle

extern "C" U3X_FN void u3x_lnb_srv_ctor(void *v) {
    g09::Server *s = static_cast<g09::Server *>(v);
    u3x_sock_init(&s->listening);
    for (int k = 0; k < g09::SLOTS; k++) {
        g09::Client *c = &s->client[k];
        u3x_sock_init(&c->sock);
        c->in_use = 0;
        c->waiting = 0;
    }
    s->next_id = 1;
    for (int e = 0; e < g09::KICKS; e++) s->kicked[e] = -1;
}

extern "C" U3X_FN int u3x_lnb_srv_running(const void *v) { return static_cast<const g09::Server *>(v)->state == 1; }

extern "C" U3X_FN uint32_t u3x_lnb_srv_count(const void *v) {
    const g09::Server *s = static_cast<const g09::Server *>(v);
    uint32_t n = 0;
    for (int k = 0; k < g09::SLOTS; k++)
        if (s->client[k].in_use) n++;
    return n;
}

extern "C" U3X_FN int32_t u3x_lnb_srv_start(void *v, uint64_t arg, uint32_t port, uint32_t maxp) {
    g09::Server *s = static_cast<g09::Server *>(v);
    const uint32_t ip = u3x_sock_local_ip();
    const uint32_t number = g09::room_counter++;

    // room id: own address (first octet on top), then port and running number
    const uint32_t address = (ip >> 24) | ((ip >> 8) & 0xff00u) | ((ip << 8) & 0xff0000u) | (ip << 24);
    s->room = (static_cast<uint64_t>(address) << 32) | ((port << 16) | number);
    s->arg = arg;
    s->capacity = maxp;

    const int32_t r = u3x_sock_listen(&s->listening, port);
    if (r < 0)
        u3x_sock_close(&s->listening);
    else
        s->state = 1;
    return r;
}

extern "C" U3X_FN void u3x_lnb_srv_stop(void *v) {
    g09::Server *s = static_cast<g09::Server *>(v);
    s->state = 0;
    for (int k = 0; k < g09::SLOTS; k++) {
        g09::Client *c = &s->client[k];
        if (!c->in_use) continue;
        u3x_sock_close(&c->sock);
        c->in_use = 0;
    }
    u3x_sock_close(&s->listening);
}

// ---- B. per-frame work

// a waiting connection of a listening socket: its descriptor, or 0 = none
extern "C" U3X_FN int32_t u3x_sock_accept(const void *s, uint32_t *ip, uint32_t *port) {
    using Accept = int32_t (*)(int32_t fd, void *address, uint32_t *length);
    const Accept accept = rd<Accept>(u3h_ext(), 0x200 + 8 * 9);
    if (!accept) return 0;

    uint32_t address[4] = {0, 0, 0, 0};   // socket address: port (big-endian) at byte 2, IPv4 address at byte 4
    uint32_t length = sizeof address;
    const int32_t fd = accept(rd<int32_t>(s, 0), address, &length);
    if (fd < 0) return 0;

    *ip = address[1];
    *port = ((address[0] >> 8) & 0xff00u) | (address[0] >> 24);
    return fd;
}

extern "C" U3X_FN void u3x_lnb_srv_update(void *v) {
    g09::Server *s = static_cast<g09::Server *>(v);
    if (s->state != 1) return;

    // at most one new connection
    uint32_t ip = 0, port = 0;
    const int32_t fd = u3x_sock_accept(&s->listening, &ip, &port);
    if (fd != 0) g09::admit(s, fd, ip, port);

    for (int k = 0; k < g09::SLOTS; k++)
        if (s->client[k].in_use) g09::serve(s, k);
}

// ---- C. messages

// the message at the start of slot k's receive buffer: the bytes it used, 0 = incomplete or unknown
extern "C" U3X_FN uint32_t u3x_lnb_srv_msg(void *v, int k) {
    ext_eb()[77]++;
    g09::Server *s = static_cast<g09::Server *>(v);
    const g09::Client *c = &s->client[k];
    const uint32_t waiting = c->waiting;
    const uint32_t id = rd<uint32_t>(c->buffer, 0);

    if (id == g09::MSG_JOIN && waiting >= g09::LEN_JOIN) {
        g09::join(s, k);
        return g09::LEN_JOIN;
    }
    if (id == g09::MSG_DATA && waiting >= g09::LEN_DATA) {
        g09::data_changed(s, k);
        return g09::LEN_DATA;
    }
    if (id == g09::MSG_ELIMINATION && waiting >= g09::LEN_ELIMINATION) {
        g09::broadcast(s, k, c->buffer, g09::LEN_ELIMINATION);
        return g09::LEN_ELIMINATION;
    }
    if (id == g09::MSG_SETTINGS && waiting >= g09::LEN_SETTINGS) {
        g09::relay_settings(s, k);
        return g09::LEN_SETTINGS;
    }
    if (id == g09::MSG_KICK && waiting >= g09::LEN_KICK) {
        g09::kick(s, c->buffer);
        return g09::LEN_KICK;
    }
    return 0;
}
