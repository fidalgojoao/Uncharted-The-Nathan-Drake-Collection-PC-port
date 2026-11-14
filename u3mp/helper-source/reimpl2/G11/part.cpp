// G11: game sockets, packet pump, net clock block, time sync.
// Written from spec/G11_game_sockets_time.md (section numbers in the comments are that document's).

// The bit stream (3.10). INTEGRATOR: the C++ part defines this type before this group's code; drop this copy there.
struct GtStream { uint32_t *words; uint32_t pos, max, x, pad; };

extern "C" {
// the C part and other groups (2.4)
uint8_t *u3h_ext(void);
void u3h_net_pick(void);
int32_t u3x_sock_setopt(int fd, int level, int opt, int32_t v);
int32_t u3x_sock_bind(int fd, uint32_t port);
uint8_t *u3h_gt_state(int which);
int32_t u3h_pl_send(const void *data, uint32_t size, uint32_t ip, uint32_t port);
int32_t u3h_pl_recv(uintptr_t base, void *buffer, uint32_t size, uint32_t *ip, uint32_t *port);
void u3x_gt_write(GtStream *s, uint32_t value, int count);
uint32_t u3x_gt_read(GtStream *s, int count);
uint32_t u3x_gt_bytes(const GtStream *s);
uint8_t *u3x_entry_by_address(uint8_t *entries, uint32_t ip, uint32_t port);
uint8_t *u3x_find_member(uint8_t *session, uint32_t id);
void u3x_connection_monitor(uintptr_t base, int arg);
int u3x_gt_base_read(uintptr_t base, uint8_t *sub, void *b);
void u3x_gt_base_write(uintptr_t base, uint8_t *sub, void *b);
void u3x_gt_drop_expired(uintptr_t base, uint8_t *sub);
int u3x_gt_has_data(uintptr_t base, uint8_t *sub);
void u3x_ev_scan(uintptr_t b, uint32_t peer, int32_t id, const uint8_t *sub, uint32_t queue, uint32_t frame, Log log);
uint32_t u3x_sync_random(uint32_t *state);
int32_t u3x_net_clock(uintptr_t base);
uint32_t *u3h_time_sync(int which);
void u3h_sync_reset(void);
void u3h_so_note(const char *a, const char *name, int32_t v, const char *b);

// this group (2.1, 2.2)
int u3x_context_push(uint8_t *context, uintptr_t allocator, uintptr_t name);
void u3x_context_pop(uint8_t *context);
void u3x_time_synced(uintptr_t base, int32_t offset);
int u3x_round_start(uintptr_t base, const uint64_t *r);
void u3x_time_sample(uintptr_t base, int32_t t0, int32_t th);
int32_t u3x_gt_socket_open(void *sock, uint32_t port);
int32_t u3x_gt_recvfrom(void *sock, void *buffer, uint32_t size, uint32_t *ip, uint32_t *port);
int32_t u3x_gt_sendto(void *sock, const void *data, uint32_t size, uint32_t ip, uint32_t port);
int32_t u3x_gt_open(uintptr_t base);
void u3x_gt_entry_reset(uint8_t *entry);
void u3x_gt_sync_request(uintptr_t base);
void u3x_gt_pump(uintptr_t base, Log log);
void u3x_gt_frame(uintptr_t base, Log log);
void u3x_gt_net_time(uintptr_t base);
void u3x_gt_master_channel(uintptr_t base);
}

namespace g11 {
namespace {

// the helper's own records, shared with the C part (3.3, 3.4, 3.5)
struct Sock { int32_t fd; uint32_t open, sent, received; };
struct Timer { uint32_t ping, data, keep; int32_t trip; };          // three game times (float patterns), round trip
struct Sample { uint64_t unused; int32_t half, difference; };
// a socket address of the platform (3.2)
struct Address { uint8_t size, family, port_high, port_low; uint32_t ip; uint32_t zero[2]; };

// image offsets (3.6)
constexpr uintptr_t TIME_SCALE = 0x16e716c, TIME_PAUSED = 0x17c3571, TIME_PAUSED_NEXT = 0x17c3572,
                    TIME_FRACTION = 0x17c3574, NET_TIME = 0x17c3578, NET_CLOCK = 0x17c3580, NET_MANAGER = 0x17cb060,
                    LAN_MODE = 0x17d5c60, NET_ACTIVE = 0x17d5c61, MATCH_START = 0x17d5c64, SESSION = 0x17d6420,
                    CONNECTIONS = 0x17f31b0, NET_ALLOCATOR = 0x1841850, EMPTY_TEXT = 0x13ac250,
                    FRAME_CLOCK = 0x1b24b98, FN_THREAD_CONTEXT = 0xda0d60, FN_SET_CLOCK = 0x1196cb0;
constexpr uintptr_t ENTRY_SIZE = 0x220;
constexpr uint32_t ENTRIES = 32, CONTEXT_MAX = 0x2e, PACKET_WORDS = 0x900, RECEIVE_SIZE = 0x23ff;
constexpr uint32_t NET_TIME_OFF = 8;                                 // eb[0] bit 3

// float patterns (3.11)
constexpr uint32_t F_TICK = 0x3b5a740e, F_RANDOM_SCALE = 0x37800000, F_SYNC_SPREAD = 0x3e99999a,
                   F_SYNC_GAP = 0x3dcccccd, F_TRIP_UNITS = 0x43fa0000, F_PING_GAP = 0x41200000,
                   F_DATA_GAP = 0x3d4ccccd, F_KEEP_ALIVE = 0x40000000, F_TICKS_PER_SECOND = 0x43960000;

U3X_RO const char sock_name[] = "u3mp-game";
U3X_RO const char note_synced[] = "net time synced with the match host, offset (1/300 s) ";
U3X_RO const char note_set_back[] = "round start set back to the host's time ";
U3X_RO const char note_host_start[] = "round start (this PC is the match host), net time ";
U3X_RO const char note_guest_start[] = "round start from the match host, its net time ";
U3X_RO const char note_own_time[] = "  ... this PC's net time ";
U3X_RO const char note_samples[] = "  ... time samples so far ";
U3X_RO const char note_ahead[] = "  ... the start is ahead of this PC's clock (grenades would stay locked): this PC's time "
                                 "is used until the clocks are synced; ticks ahead ";

// the group's own state (2.3)
U3X_BSS uint32_t packet[PACKET_WORDS];   // the packet buffer, 0x2400 bytes
U3X_BSS uint8_t *table;                  // the connection table once the sockets were opened
U3X_BSS uint8_t frame_toggle;
U3X_BSS uint8_t rate_zeroed;
U3X_BSS uint32_t syncs_seen;

template <typename T> [[gnu::always_inline]] inline T &field(uint8_t *p, uintptr_t offset) {
    return *reinterpret_cast<T *>(p + offset);
}
[[gnu::always_inline]] inline Sock *sockets() { return reinterpret_cast<Sock *>(u3h_gt_state(0)); }   // [0] game, [1] master
[[gnu::always_inline]] inline Timer *timers() { return reinterpret_cast<Timer *>(u3h_gt_state(1)); }
[[gnu::always_inline]] inline uint32_t &next_request() { return *reinterpret_cast<uint32_t *>(u3h_gt_state(2)); }
[[gnu::always_inline]] inline int32_t &sync_word(int which) { return *reinterpret_cast<int32_t *>(u3h_time_sync(which)); }
[[gnu::always_inline]] inline uint8_t &sync_byte(int which) { return *reinterpret_cast<uint8_t *>(u3h_time_sync(which)); }
[[gnu::always_inline]] inline uintptr_t socket_import(int k) { return rd<uintptr_t>(u3h_ext(), 0x200 + 8 * k); }
[[gnu::always_inline]] inline uint8_t *entry_at(uintptr_t base, uint32_t index) {
    return reinterpret_cast<uint8_t *>(base + CONNECTIONS + 8 + index * ENTRY_SIZE);
}

// float tests on patterns (4.0)
[[gnu::always_inline]] inline bool f_nan(uint32_t a) { return (a & 0x7fffffffu) > 0x7f800000u; }
[[gnu::always_inline]] inline bool f_zero(uint32_t a) { return !(a & 0x7fffffffu); }
[[gnu::always_inline]] inline bool f_not_above(uint32_t a, uint32_t b) { return !f_nan(a) && !f_nan(b) && !f_less(b, a); }

// the game time in seconds
[[gnu::always_inline]] inline uint32_t game_now(uintptr_t base) {
    return f_mul(f_from_int(rd<int32_t>(at<uint8_t *>(base + FRAME_CLOCK), 0x114)), F_TICK);
}

U3X_FN void clear_packet() {
    for (uint32_t i = 0; i < PACKET_WORDS; i++) packet[i] = 0;
}

// a stream over a buffer of that many bytes (3.10)
U3X_FN void open_stream(GtStream *s, uint32_t *words, uint32_t bytes) {
    s->words = words;
    s->pos = 0;
    s->max = 8 * bytes;
    s->x = 0;
    s->pad = 0;
}

// starts a short packet in a 16-byte area: the area zeroed, a stream over it, the type written
U3X_FN void short_packet(GtStream *s, uint32_t *area, uint32_t type) {
    for (int i = 0; i < 4; i++) area[i] = 0;
    open_stream(s, area, 16);
    u3x_gt_write(s, type, 8);
}

// sends what a stream holds on the game socket
U3X_FN int32_t send_stream(const GtStream *s, uint32_t ip, uint32_t port) {
    return u3x_gt_sendto(sockets(), s->words, u3x_gt_bytes(s), ip, port);
}

// 4.8, first half: takes and answers the waiting datagrams
U3X_FN void receive_all(uintptr_t base, uint32_t now, Log log) {
    uint32_t *eb = ext_eb();
    uint8_t *first = entry_at(base, 0);
    for (int round = 0; round < 256; round++) {
        clear_packet();
        uint32_t ip = 0, port = 0;
        int32_t length = u3h_pl_recv(base, packet, RECEIVE_SIZE, &ip, &port);
        if (length == -2) length = u3x_gt_recvfrom(sockets(), packet, RECEIVE_SIZE, &ip, &port);
        if (length < 1) break;
        eb[98]++;
        eb[117] = static_cast<uint32_t>(length);

        uint8_t *entry = u3x_entry_by_address(first, ip, port);
        if (!entry && !at<uint8_t>(base + LAN_MODE)) {   // online only known peers are heard
            eb[111]++;
            continue;
        }
        const uint32_t index = entry ? static_cast<uint32_t>((entry - first) / ENTRY_SIZE) : 0;
        if (entry) field<uint32_t>(entry, 0xf8) += static_cast<uint32_t>(length);

        GtStream in;
        open_stream(&in, packet, static_cast<uint32_t>(length));
        const uint32_t type = u3x_gt_read(&in, 8);
        uint32_t area[4];
        GtStream out;
        if (type == 2) {          // time request: answered with this machine's net time
            const uint32_t asked = u3x_gt_read(&in, 32);
            short_packet(&out, area, 3);
            u3x_gt_write(&out, asked, 32);
            u3x_gt_write(&out, at<uint32_t>(base + NET_TIME), 32);
            send_stream(&out, ip, port);
            eb[107]++;
        } else if (type == 3) {   // time answer: one sample
            const uint32_t asked = u3x_gt_read(&in, 32);
            const uint32_t host_time = u3x_gt_read(&in, 32);
            if (!(eb[0] & NET_TIME_OFF))
                u3x_time_sample(base, static_cast<int32_t>(asked), static_cast<int32_t>(host_time));
        } else if (type == 4) {   // ping: echoed as a pong
            const uint32_t stamp = u3x_gt_read(&in, 32);
            short_packet(&out, area, 5);
            u3x_gt_write(&out, stamp, 32);
            send_stream(&out, ip, port);
        } else if (type == 5) {   // pong: the round trip of a known peer
            const uint32_t stamp = u3x_gt_read(&in, 32);
            eb[106]++;
            if (entry) timers()[index].trip = f_to_int(f_mul(f_sub(now, stamp), F_TRIP_UNITS));
        } else if (type == 0) {   // data: the game reads it
            eb[100]++;
            if (entry) {
                u3x_gt_base_read(base, entry + 0x40, &in);
                u3x_ev_scan(base, index, rd<int32_t>(entry, 0x34), entry + 0x40, 2, eb[113], log);
            }
        } else {
            eb[111]++;
        }
    }
}

// 4.8, second half: ping and data packet of each connection that has a peer
U3X_FN void send_all(uintptr_t base, uint32_t now, Log log) {
    uint32_t *eb = ext_eb();
    Timer *timer = timers();
    for (uint32_t index = 0; index < ENTRIES; index++) {
        uint8_t *entry = entry_at(base, index);
        const int32_t handle = rd<int32_t>(entry, 0x34);
        const uint32_t port = rd<uint16_t>(entry, 0x30);
        if (!rd<uint8_t>(entry, 4) || !handle || !port) continue;
        const uint32_t ip = rd<uint32_t>(entry, 0x2c);
        uint8_t *sub = entry + 0x40;
        Timer *t = &timer[index];

        u3x_ev_scan(base, index, handle, sub, 0, eb[113], log);
        u3x_ev_scan(base, index, handle, sub, 1, eb[113], log);
        u3x_gt_drop_expired(base, sub);

        if (f_not_above(t->ping, now)) {
            GtStream out;
            short_packet(&out, packet, 4);
            u3x_gt_write(&out, now, 32);
            send_stream(&out, ip, port);
            t->ping = f_add(now, F_PING_GAP);
            eb[105]++;
        }
        if (!f_not_above(t->data, now)) continue;
        t->data = f_add(now, F_DATA_GAP);
        if (!f_not_above(t->keep, now) && !u3x_gt_has_data(base, sub)) continue;
        t->keep = f_add(now, F_KEEP_ALIVE);
        clear_packet();
        GtStream out;
        open_stream(&out, packet, RECEIVE_SIZE);
        u3x_gt_write(&out, 0, 8);
        u3x_gt_base_write(base, sub, &out);
        u3x_gt_write(&out, 0, 1);   // no voice data
        const int32_t sent = send_stream(&out, ip, port);
        if (sent > 0) field<uint32_t>(entry, 0xf4) += static_cast<uint32_t>(sent);
    }
}

// 4.12 step 5: the eight samples in ascending order of their half round trip, equal ones in their old order
U3X_FN void sort_samples(Sample *samples) {
    for (int i = 1; i < 8; i++) {
        const Sample moved = samples[i];
        int k = i;
        for (; k > 0 && samples[k - 1].half > moved.half; k--) samples[k] = samples[k - 1];
        samples[k] = moved;
    }
}

// 4.12 step 5: the time stamps of the joined players follow the clock
U3X_FN void shift_players(uintptr_t base, uint32_t offset) {
    for (uint32_t i = 0; i < 10; i++) {
        uint8_t *player = reinterpret_cast<uint8_t *>(base + NET_MANAGER + 0x390 + i * 0x9c0);
        if (!rd<uint8_t>(player, 0xe1)) continue;
        if (field<uint32_t>(player, 0x39c)) field<uint32_t>(player, 0x39c) += offset;
        if (field<uint32_t>(player, 0x3a0)) field<uint32_t>(player, 0x3a0) += offset;
        if (field<uint32_t>(player, 0x3a8)) field<uint32_t>(player, 0x3a8) += offset;
        field<uint32_t>(player, 0x5f0) = 0;
        field<uint32_t>(player, 0x5f4) = 0;
    }
}

// 3.8: no session, or the own member is the host member
U3X_FN bool is_match_host(uintptr_t base) {
    const uint8_t *session = reinterpret_cast<const uint8_t *>(base + SESSION);
    return !rd<uint64_t>(session, 0x18) || rd<uint32_t>(session, 0x12e4) == rd<uint32_t>(session, 0x12e8);
}

// 4.11: the connection entry of the match host when this machine is a guest and the entry can be sent to, else null
U3X_FN uint8_t *host_entry(uintptr_t base) {
    uint8_t *session = reinterpret_cast<uint8_t *>(base + SESSION);
    if (!rd<uint64_t>(session, 0x18)) return nullptr;
    const uint8_t *own = u3x_find_member(session, rd<uint32_t>(session, 0x12e4));
    const uint8_t *host = u3x_find_member(session, rd<uint32_t>(session, 0x12e8));
    if (!own || !host || own == host) return nullptr;
    const int32_t id = rd<int32_t>(host, 0x34);
    if (id < 0) return nullptr;
    for (uint32_t index = 0; index < ENTRIES; index++) {
        uint8_t *entry = entry_at(base, index);
        if (rd<int32_t>(entry, 0) != id) continue;
        // only the first entry with the id counts, in use or not
        const bool usable = rd<uint8_t>(entry, 4) && rd<int32_t>(entry, 0x34) != -1 && rd<uint16_t>(entry, 0x30);
        return usable ? entry : nullptr;
    }
    return nullptr;
}

}  // namespace
}  // namespace g11

extern "C" {

// ---- A. game sockets

// 4.1: one non-blocking UDP socket bound to a port
U3X_FN int32_t u3x_gt_socket_open(void *sock, uint32_t port) {
    g11::Sock *s = static_cast<g11::Sock *>(sock);
    if (s->open && s->fd > 0) return 0;
    s->fd = 0;
    s->open = 0;
    s->sent = 0;
    s->received = 0;
    u3h_net_pick();
    const uintptr_t make = g11::socket_import(0);
    if (!make || !g11::socket_import(5) || !g11::socket_import(2)) return -1;
    const int32_t fd = game_call<int32_t>(make, static_cast<const char *>(g11::sock_name), 2, 2, 0);
    if (fd < 0) {
        ext_eb()[97] = static_cast<uint32_t>(fd);
        return -1;
    }
    s->fd = fd;
    int32_t result = u3x_sock_setopt(fd, 0xffff, 0x1200, 1);
    if (result >= 0) result = u3x_sock_bind(fd, port);
    if (result < 0) {   // the descriptor stays in the record, which is not open
        ext_eb()[97] = static_cast<uint32_t>(result);
        return -1;
    }
    s->open = 1;
    return 0;
}

// 4.2: one datagram in
U3X_FN int32_t u3x_gt_recvfrom(void *sock, void *buffer, uint32_t size, uint32_t *ip, uint32_t *port) {
    g11::Sock *s = static_cast<g11::Sock *>(sock);
    const uintptr_t receive = g11::socket_import(4);
    if (s->fd <= 0 || !receive) return -1;
    g11::Address from = {0, 0, 0, 0, 0, {0, 0}};
    uint32_t length = 16;
    const int32_t got = game_call<int32_t>(receive, s->fd, buffer, size, 0, static_cast<void *>(&from), &length);
    if (got > 0) {
        *ip = from.ip;
        *port = static_cast<uint32_t>(from.port_high) << 8 | from.port_low;
        s->received += static_cast<uint32_t>(got) + 0x34;
    }
    return got;
}

// 4.3: one datagram out; the game socket's packets go over the peer link when that takes them
U3X_FN int32_t u3x_gt_sendto(void *sock, const void *data, uint32_t size, uint32_t ip, uint32_t port) {
    if (sock == static_cast<void *>(u3h_gt_state(0))) {
        const int32_t taken = u3h_pl_send(data, size, ip, port);
        if (taken != -2) return taken;
    }
    g11::Sock *s = static_cast<g11::Sock *>(sock);
    const uintptr_t send = g11::socket_import(3);
    if (s->fd <= 0 || !send) return -1;
    const g11::Address to = {16, 2, static_cast<uint8_t>(port >> 8), static_cast<uint8_t>(port), ip, {0, 0}};
    const int32_t sent = game_call<int32_t>(send, s->fd, data, size, 0, static_cast<const void *>(&to), 16u);
    if (sent > 0) {
        uint32_t *eb = ext_eb();
        s->sent += static_cast<uint32_t>(sent) + 0x34;
        eb[99]++;
        eb[119] += static_cast<uint32_t>(sent);
    }
    return sent;
}

// 4.4: the two game sockets, and the transport's state reset (on every call, whatever the result)
U3X_FN int32_t u3x_gt_open(uintptr_t base) {
    uint32_t *eb = ext_eb();
    g11::Sock *socks = g11::sockets();
    bool both = false;
    eb[96] = 0;
    if (u3x_gt_socket_open(&socks[0], 3233) == 0) {
        eb[96] |= 1;
        if (u3x_gt_socket_open(&socks[1], 3232) == 0) {
            eb[96] |= 2;
            both = true;
        }
    }
    g11::sync_word(0) = 0;
    g11::next_request() = 0;
    g11::Timer *timer = g11::timers();
    for (uint32_t i = 0; i < g11::ENTRIES; i++) {
        timer[i].ping = 0;
        timer[i].data = 0;
        timer[i].keep = 0;
        timer[i].trip = 0;
    }
    g11::table = reinterpret_cast<uint8_t *>(base + g11::CONNECTIONS);
    return both ? 0 : -1;
}

// 4.5: the three timers of one connection entry start again; the round trip stays
U3X_FN void u3x_gt_entry_reset(uint8_t *entry) {
    if (!g11::table) return;
    const uintptr_t first = reinterpret_cast<uintptr_t>(g11::table) + 8;
    const uintptr_t address = reinterpret_cast<uintptr_t>(entry);
    if (address < first || address >= first + g11::ENTRIES * g11::ENTRY_SIZE) return;
    g11::Timer *t = &g11::timers()[(address - first) / g11::ENTRY_SIZE];
    t->ping = 0;
    t->data = 0;
    t->keep = 0;
}

// ---- B. net frame and packet pump

// 4.6: one entry {allocator, name, 0, name, 1} on the allocator context stack
U3X_FN int u3x_context_push(uint8_t *context, uintptr_t allocator, uintptr_t name) {
    uint32_t &count = g11::field<uint32_t>(context, 0x730);
    if (count >= g11::CONTEXT_MAX) return 0;
    uint64_t *e = &g11::field<uint64_t>(context, 40 * static_cast<uintptr_t>(count));
    e[0] = allocator;
    e[1] = name;
    e[2] = 0;
    e[3] = name;
    e[4] = 1;
    count++;
    return 1;
}

// 4.6: the top entry taken off and zeroed (no test for an empty stack)
U3X_FN void u3x_context_pop(uint8_t *context) {
    uint32_t &count = g11::field<uint32_t>(context, 0x730);
    count--;
    uint64_t *e = &g11::field<uint64_t>(context, 40 * static_cast<uintptr_t>(count));
    for (int i = 0; i < 5; i++) e[i] = 0;
}

// 4.8: the waiting datagrams, then what each connection has to send
U3X_FN void u3x_gt_pump(uintptr_t base, Log log) {
    const uint32_t now = g11::game_now(base);
    g11::receive_all(base, now, log);
    g11::send_all(base, now, log);
}

// 4.7: the net frame: monitor and pump inside an allocator context entry
U3X_FN void u3x_gt_frame(uintptr_t base, Log log) {
    g11::frame_toggle ^= 1;   // on every call, before any test
    if (!at<uintptr_t>(base + g11::NET_CLOCK) || !at<uint8_t>(base + g11::NET_ACTIVE) || !g11::sockets()[0].open) return;
    ext_eb()[113]++;

    uint8_t *context = game_call<uint8_t *>(base + g11::FN_THREAD_CONTEXT);
    bool pushed = false;
    if (context && rd<uint32_t>(context, 0x730) < g11::CONTEXT_MAX) {
        const uintptr_t allocator = at<uintptr_t>(base + g11::NET_ALLOCATOR);
        if (allocator) {
            u3x_context_push(context, allocator, base + g11::EMPTY_TEXT);
            pushed = true;
        }
    }
    u3x_connection_monitor(base, g11::frame_toggle);
    u3x_gt_pump(base, log);
    if (pushed) u3x_context_pop(context);
}

// 4.9: the master socket is emptied, at most 8 datagrams a call, none looked at
U3X_FN void u3x_gt_master_channel(uintptr_t) {
    g11::Sock *master = &g11::sockets()[1];
    if (!master->open) return;
    for (int i = 0; i < 8; i++) {
        uint32_t ip = 0, port = 0;
        if (u3x_gt_recvfrom(master, g11::packet, 0xff, &ip, &port) < 1) break;
        ext_eb()[112]++;
    }
}

// ---- C. net clock block

// 4.10: the net time advances by the frame time and the net clock is set to it
U3X_FN void u3x_gt_net_time(uintptr_t base) {
    uint32_t *eb = ext_eb();
    uint8_t *clock = at<uint8_t *>(base + g11::NET_CLOCK);
    const uint8_t *frame_clock = at<uint8_t *>(base + g11::FRAME_CLOCK);
    if ((eb[0] & g11::NET_TIME_OFF) || !at<uint8_t>(base + g11::NET_ACTIVE) || !clock || !frame_clock) return;
    eb[114]++;
    if (eb[109] != g11::syncs_seen) {
        g11::syncs_seen = eb[109];
        u3x_time_synced(base, static_cast<int32_t>(eb[110]));
    }
    if (!at<uint8_t>(base + g11::TIME_PAUSED)) {
        uint32_t rate = rd<uint32_t>(clock, 4);
        if (g11::f_zero(rate) && g11::rate_zeroed) rate = F_ONE;   // the 0 this block left stands for 1.0
        const uint32_t frame_ticks =
            f_mul(f_mul(rd<uint32_t>(frame_clock, 0xc), at<uint32_t>(base + g11::TIME_SCALE)), g11::F_TICKS_PER_SECOND);
        const uint32_t sum = f_add(at<uint32_t>(base + g11::TIME_FRACTION), f_mul(rate, frame_ticks));
        const int32_t whole = f_to_int(sum);
        at<uint8_t>(base + g11::TIME_PAUSED) = at<uint8_t>(base + g11::TIME_PAUSED_NEXT);
        at<uint32_t>(base + g11::NET_TIME) += static_cast<uint32_t>(whole);
        at<uint32_t>(base + g11::TIME_FRACTION) = f_sub(sum, f_from_int(whole));
    }
    game_call<void>(base + g11::FN_SET_CLOCK, clock, reinterpret_cast<int32_t *>(base + g11::NET_TIME), 1);
    g11::field<uint32_t>(clock, 4) = 0;   // the engine's own update must not move the net clock
    g11::rate_zeroed = 1;
}

// ---- D. time sync

// 4.11: a guest asks the match host for its net time
U3X_FN void u3x_gt_sync_request(uintptr_t base) {
    const uint32_t now = g11::game_now(base);
    if (!f_less(g11::next_request(), now)) return;
    if (g11::sync_word(0) >= 8) return;
    if (ext_eb()[0] & g11::NET_TIME_OFF) return;
    const uint8_t *entry = g11::host_entry(base);
    if (!entry) return;

    const uint32_t random = u3x_sync_random(u3h_time_sync(2)) & 0xffff;
    const uint32_t spread = f_mul(f_mul(f_from_int(static_cast<int32_t>(random)), g11::F_RANDOM_SCALE), g11::F_SYNC_SPREAD);
    g11::next_request() = f_add(f_add(spread, now), g11::F_SYNC_GAP);

    GtStream out;
    g11::short_packet(&out, g11::packet, 2);
    u3x_gt_write(&out, at<uint32_t>(base + g11::NET_TIME), 32);
    g11::send_stream(&out, rd<uint32_t>(entry, 0x2c), rd<uint16_t>(entry, 0x30));
}

// 4.12: one answer of the host; the eighth sample completes the sync
U3X_FN void u3x_time_sample(uintptr_t base, int32_t t0, int32_t th) {
    const int32_t count = g11::sync_word(0);
    if (count >= 8) return;
    const uint32_t clock = static_cast<uint32_t>(u3x_net_clock(base));
    g11::Sample *samples = reinterpret_cast<g11::Sample *>(u3h_time_sync(1));
    g11::Sample *sample = samples + count;

    const int32_t half = static_cast<int32_t>(clock - static_cast<uint32_t>(t0)) / 2;
    sample->half = half;   // written before the answer is judged
    if (half <= 0) return;
    sample->difference = static_cast<int32_t>(static_cast<uint32_t>(th) - clock + static_cast<uint32_t>(half));
    uint32_t *eb = ext_eb();
    eb[108]++;

    if (count == 7) {
        g11::sort_samples(samples);
        uint32_t sum = 0;
        for (int i = 0; i < 4; i++) sum += static_cast<uint32_t>(samples[i].difference);
        const uint32_t offset = static_cast<uint32_t>(static_cast<int32_t>(sum) / 4);
        at<uint32_t>(base + g11::NET_TIME) = clock + offset;
        eb[109]++;
        eb[110] = offset;
        if (g11::sync_byte(3)) {
            g11::sync_byte(3) = 0;
            g11::shift_players(base, offset);
        }
    }
    g11::sync_word(0) = count + 1;
}

// 4.13: after a completed sync: the note, and a pending round start goes back to the host's time
U3X_FN void u3x_time_synced(uintptr_t base, int32_t offset) {
    u3h_so_note(g11::note_synced, nullptr, offset, nullptr);
    if (!g11::sync_byte(6)) return;
    g11::sync_byte(6) = 0;   // cleared before the test: a start still ahead drops the fix for good
    const int32_t host_start = g11::sync_word(4), used = g11::sync_word(5);
    if (host_start > u3x_net_clock(base)) return;
    int32_t *manager_times = &at<int32_t>(base + g11::NET_MANAGER + 0x66ec);   // three words, the round start first
    for (int i = 0; i < 3; i++)
        if (manager_times[i] == used) manager_times[i] = host_start;
    if (at<int32_t>(base + g11::MATCH_START) == used) at<int32_t>(base + g11::MATCH_START) = host_start;
    u3h_so_note(g11::note_set_back, nullptr, host_start, nullptr);
}

// 4.14: the game is about to start a round with the time r[6] (rdx) points to
U3X_FN int u3x_round_start(uintptr_t base, const uint64_t *r) {
    int32_t *start = reinterpret_cast<int32_t *>(r[6]);
    if (!start) return 0;
    const int32_t host_time = *start;
    const int32_t clock = u3x_net_clock(base);
    if (g11::is_match_host(base)) {
        u3h_so_note(g11::note_host_start, nullptr, host_time, nullptr);
        return 0;
    }
    u3h_so_note(g11::note_guest_start, nullptr, host_time, nullptr);
    u3h_so_note(g11::note_own_time, nullptr, clock, nullptr);
    u3h_so_note(g11::note_samples, nullptr, g11::sync_word(0), nullptr);
    if (host_time > clock) {   // this machine's time is used until the clocks are synced
        *start = clock;
        g11::sync_word(4) = host_time;
        g11::sync_word(5) = clock;
        g11::sync_byte(6) = 1;
        const uint32_t ahead = static_cast<uint32_t>(host_time) - static_cast<uint32_t>(clock);
        u3h_so_note(g11::note_ahead, nullptr, static_cast<int32_t>(ahead), nullptr);
    }
    if (!(ext_eb()[0] & g11::NET_TIME_OFF)) {   // every guest round start asks for a resync
        g11::sync_byte(3) = 1;
        u3h_sync_reset();
    }
    return 0;
}

}  // extern "C"
