// G08. LAN backend: room client, NetInit / frame / shutdown, party rows.
// Written from spec/G08_lan_room_client.md (reimpl2 clean room).

// ---- functions of other groups and of the C part (spec 2.2)
extern "C" {
int64_t u3h_lnb_call(void *object, int slot);
void u3h_cm_lanaddr(uintptr_t base, const char *name, uint32_t ip);
volatile uint8_t *u3h_elim_received(void);
uint8_t *u3h_adv_rules(void);
uint8_t *u3h_adv_received(void);
int32_t u3x_add_member(uintptr_t base, uint8_t *session, const void *info, int you, int host);
int32_t u3x_remove_member(uintptr_t base, uint8_t *session, const char *name);
uint8_t *u3x_find_member(uint8_t *session, uint32_t id);
int u3x_sock_open(const void *socket);
int32_t u3x_sock_close(void *socket);
int32_t u3x_sock_poll(const void *socket);
int32_t u3x_sock_recv(void *socket, void *buffer, uint32_t size, int block);
void *u3h_lnb_object(uintptr_t base);
void *u3h_online_manager(uintptr_t base);
int u3h_onb_lan_flag(void);
int32_t u3h_gt_open(uintptr_t base);
void u3h_gt_close(uintptr_t base);
void u3h_cm_init_lan(uintptr_t base);
void u3h_cm_shutdown(uintptr_t base);
uint32_t u3h_active_page(uintptr_t base);
void u3x_lnb_vtable(void **vtable, void *slot0);
void u3x_lnb_ctor(void *object, void **vtable);
int64_t u3x_lnb_s2(void *object);
void u3x_lnb_srv_stop(void *room_server);
void u3x_lobby_scene(uintptr_t base, void *s);
void *u3h_lnb_setup(uintptr_t base, void *s);   // kept C export: adds the C part's table, storage and slot 0

// the per-frame parts of the C part (one-line exports added by the integrator)
void u3h_gt_frame(uintptr_t base);
void u3h_gt_net_time_block(uintptr_t base);
void u3h_e03_side(uintptr_t base, void *s);
void u3h_f05_place(uintptr_t base);
void u3h_k42_clock(uintptr_t base);
void u3h_k47_hud(uintptr_t base, void *s);
void u3h_e01_hud(uintptr_t base, void *s);
void u3h_gt_master_channel(uintptr_t base);
void u3h_onb_probe_tick(uintptr_t base);

// this group
void u3x_lan_client_lost(uintptr_t base, const void *view, int kicked);
uint32_t u3x_lan_client_message(uintptr_t base, const void *view);
void u3x_lan_client_update(uintptr_t base, const void *view);
void u3x_lan_netinit(uintptr_t base, void *s);
void *u3x_lan_setup(uintptr_t base, void *s, void **vtable, void *storage, void *slot0);
void u3x_lan_tick(uintptr_t base, void *s);
void u3x_lan_frame_parts(uintptr_t base, void *s, int which);
void u3x_lan_shutdown(uintptr_t base);
void u3x_lan_refresh(uintptr_t base);
const char *u3x_lan_row_name(uintptr_t base, int row);
int u3x_lan_rows(uintptr_t base);
}

namespace g08 {

// ---- game data (image offsets)
constexpr uintptr_t G_LOCAL_CLOCK = 0x17b4ba4;    // s32, ticks of 1/300 s
constexpr uintptr_t G_NET_GAME = 0x17d167c;       // s16, selected net game
constexpr uintptr_t G_LAN_FLAG = 0x17d5c60;       // u8
constexpr uintptr_t G_MP_ACTIVE = 0x17d5c61;      // u8
constexpr uintptr_t G_NET_ERROR = 0x17d5c80;      // u8
constexpr uintptr_t G_OWN_NAME = 0x17d6010;       // 0x24 bytes
constexpr uintptr_t G_MATCH_SESSION = 0x17d6420;
constexpr uintptr_t G_PARTY_SESSION = 0x17dc298;
constexpr uintptr_t G_PROFILES = 0x17dd610;       // pointer
constexpr uintptr_t G_LAN_OBJECT = 0x17dd758;     // pointer
constexpr uintptr_t G_MANAGER = 0x17dd768;        // pointer

// ---- game functions (one session argument)
constexpr uintptr_t FN_PARTY_UPDATE = 0x77d270;
constexpr uintptr_t FN_SESSION_INIT = 0x7a2360;
constexpr uintptr_t FN_SESSION_LEAVE = 0x7a2470;
constexpr uintptr_t FN_CLEAR_MEMBERS = 0x7a25f0;

// ---- session fields
constexpr uintptr_t S_BACKEND = 0x8;
constexpr uintptr_t S_ROOM = 0x18;
constexpr uintptr_t S_CREATE_DONE = 0xa1;
constexpr uintptr_t S_JOIN_DONE = 0xb8;
constexpr uintptr_t S_JOINED = 0xc0;
constexpr uintptr_t S_CAPACITY = 0x20c;
constexpr uintptr_t S_MEMBERS = 0x680;
constexpr uintptr_t S_OWN_ID = 0x12e4;
constexpr uintptr_t S_HOST_ID = 0x12e8;
constexpr uintptr_t MEMBER_SIZE = 0x120;
constexpr int MEMBER_COUNT = 10;
constexpr uintptr_t M_IN_USE = 0x25;
constexpr uintptr_t M_ID = 0x2c;
constexpr uintptr_t M_DATA_LEN = 0x3c;
constexpr uintptr_t M_DATA = 0x40;
constexpr uint32_t NAME_SIZE = 0x24;
constexpr uint32_t DATA_SIZE = 0x40;

// session events (entries of the session's function table)
constexpr int EV_CREATE_FINISHED = 1;
constexpr int EV_JOIN_FINISHED = 2;
constexpr int EV_DISCONNECTED = 5;
constexpr int EV_KICKED = 6;

// ---- backend object
constexpr uint32_t BE_SIZE = 0x249d8;
constexpr uintptr_t BE_NAME = 0x8;
constexpr uintptr_t BE_BINDINGS = 0x4b8;
constexpr uintptr_t BE_BINDING_SIZE = 0x9140;
constexpr uintptr_t BE_BIND_SERVER = 0x8;
constexpr uintptr_t BE_BIND_SOCKET = 0x8930;
constexpr uintptr_t BE_BASE = 0x249c8;
constexpr uintptr_t BE_STATE = 0x249d0;

// entries of a backend's function table
constexpr int BE_INIT = 0;
constexpr int BE_UPDATE = 1;
constexpr int BE_SHUTDOWN = 2;
constexpr int BE_ATTACH = 3;
constexpr int BE_SEARCH = 5;
constexpr int BE_ENTRY8 = 8;

// ---- room messages
constexpr uint32_t MSG_ROOM_STATE = 0xc9;
constexpr uint32_t MSG_MEMBER_JOINED = 0xca;
constexpr uint32_t MSG_MEMBER_LEFT = 0xcb;
constexpr uint32_t MSG_KICKED = 0xcc;
constexpr uint32_t MSG_MEMBER_DATA = 0xce;
constexpr uint32_t MSG_ADV_SETTINGS = 0xd2;
constexpr uint32_t MSG_ELIM_NOTICE = 0xd3;
constexpr uint32_t RECORD_SIZE = 0x70;
constexpr uint32_t BUFFER_SIZE = 0x800;

constexpr uint32_t PAGE_LAN_PARTIES = 0xbb3595bc;
constexpr uint32_t SEARCH_PERIOD = 1500;          // 5 seconds of the local clock

// eb[] words
constexpr int EB_SWITCHES = 0;                    // bit 1: LAN solo off; bit 2: search refresh off
constexpr int EB_OBJECT = 64;
constexpr int EB_SETUPS = 65;
constexpr int EB_INIT_RESULT = 66;
constexpr int EB_MESSAGES = 78;
constexpr int EB_LAST_MESSAGE = 79;
constexpr int EB_PARTY_UPDATES = 85;
constexpr int EB_SHUTDOWNS = 90;
constexpr int EB_LAST_SEARCH = 94;
constexpr int EB_REATTACHES = 230;

// the member description u3x_add_member takes
struct MemberInfo {
    const char *name;
    uint32_t id;
    uint32_t nat;
    uint32_t data_len;
    uint32_t unused0;
    const uint8_t *data;
    uint32_t has_data;
    uint32_t unused1;
};
static_assert(sizeof(MemberInfo) == 0x28);

// a field of an object, to read or write
template <typename T> [[gnu::always_inline]] inline T &fld(void *p, uintptr_t off) {
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + off);
}

// the four parts of a client view
[[gnu::always_inline]] inline uint8_t *view_session(const void *view) { return rd<uint8_t *>(view, 0); }
[[gnu::always_inline]] inline void *view_socket(const void *view) { return rd<void *>(view, 8); }
[[gnu::always_inline]] inline uint32_t *view_count(const void *view) { return rd<uint32_t *>(view, 0x10); }
[[gnu::always_inline]] inline uint8_t *view_buffer(const void *view) { return rd<uint8_t *>(view, 0x18); }

[[gnu::always_inline]] inline uint8_t *match_session(uintptr_t base) { return reinterpret_cast<uint8_t *>(base + G_MATCH_SESSION); }
[[gnu::always_inline]] inline uint8_t *party_session(uintptr_t base) { return reinterpret_cast<uint8_t *>(base + G_PARTY_SESSION); }
[[gnu::always_inline]] inline bool lan_solo_off() { return (ext_eb()[EB_SWITCHES] & 2u) != 0; }

// entry `entry` of a backend's function table, called with the object first
template <typename... A> [[gnu::always_inline]] inline int64_t backend_call(void *object, int entry, A... args) {
    const void *table = rd<const void *>(object, 0);
    return game_call<int64_t>(rd<uintptr_t>(table, static_cast<uintptr_t>(entry) * 8), object, args...);
}

[[gnu::always_inline]] inline void count_message(uint32_t id) {
    uint32_t *eb = ext_eb();
    eb[EB_MESSAGES] += 1;
    eb[EB_LAST_MESSAGE] = id;
}

// a wire member record joins the session
U3X_FN static void member_joined(uintptr_t base, uint8_t *session, const uint8_t *record) {
    const int you = rd<uint8_t>(record, 0x2c);
    const int host = rd<uint8_t>(record, 0x2d);
    if (!you) u3h_cm_lanaddr(base, reinterpret_cast<const char *>(record), rd<uint32_t>(record, 0x24));
    MemberInfo info;
    info.name = reinterpret_cast<const char *>(record);
    info.id = rd<uint16_t>(record, 0x28);
    info.nat = rd<uint8_t>(record, 0x2a);
    info.data_len = rd<uint8_t>(record, 0x2b);
    info.unused0 = 0;
    info.data = record + 0x2e;
    info.has_data = 1;
    info.unused1 = 0;
    u3x_add_member(base, session, &info, you, host);
}

// 0xc9: the whole room
U3X_FN static void room_state(uintptr_t base, uint8_t *session, const uint8_t *msg) {
    fld<uint32_t>(session, S_CAPACITY) = rd<uint16_t>(msg, 0x10);
    fld<uint64_t>(session, S_ROOM) = rd<uint64_t>(msg, 8);
    uint32_t records = rd<uint16_t>(msg, 0x12);
    if (records > 10) records = 10;
    for (uint32_t i = 0; i < records; ++i) member_joined(base, session, msg + 0x18 + i * RECORD_SIZE);
    if (!fld<uint8_t>(session, S_JOIN_DONE)) {
        fld<uint64_t>(session, S_JOINED) = 1;
        u3h_lnb_call(session, EV_JOIN_FINISHED);
        fld<uint8_t>(session, S_JOIN_DONE) = 1;
    }
    if (!fld<uint8_t>(session, S_CREATE_DONE)) {
        u3h_lnb_call(session, EV_CREATE_FINISHED);
        fld<uint8_t>(session, S_CREATE_DONE) = 1;
    }
}

// 0xce: new data of one member
U3X_FN static void member_data(uint8_t *session, const uint8_t *msg) {
    uint8_t *member = u3x_find_member(session, rd<uint16_t>(msg, 4));
    if (!member) return;
    fld<uint32_t>(member, M_DATA_LEN) = rd<uint32_t>(msg, 0x10);   // not limited
    for (uint32_t i = 0; i < DATA_SIZE; ++i) member[M_DATA + i] = msg[0x14 + i];
}

// 0xd2: the host's Advanced Settings, for a guest of the party
U3X_FN static void adv_settings(uintptr_t base, uint8_t *session, const uint8_t *msg) {
    if (session != party_session(base)) return;
    const uint32_t host = rd<uint32_t>(session, S_HOST_ID);
    if (rd<uint32_t>(msg, 4) != host) return;
    if (rd<uint32_t>(session, S_OWN_ID) == host) return;
    if (rd<uint32_t>(u3h_ext(), 0x3a70) & 1u) return;              // the rows are switched off
    uint8_t *rules = u3h_adv_rules();
    fld<uint32_t>(rules, 4) = rd<uint32_t>(msg, 0x10);
    fld<uint32_t>(rules, 8) = rd<uint32_t>(msg, 0x14);
    fld<uint32_t>(rules, 0xc) = rd<uint32_t>(msg, 0x18);
    fld<uint8_t>(rules, 0x10) = rd<uint8_t>(msg, 0x1c);
    fld<uint8_t>(rules, 0x11) = rd<uint8_t>(msg, 0x1d);
    const int16_t selection = rd<int16_t>(msg, 0x1e);
    at<int16_t>(base + G_NET_GAME) = selection;
    fld<uint32_t>(rules, 0) = static_cast<uint32_t>(static_cast<int32_t>(selection) + 1);
    *u3h_adv_received() = 1;
}

}  // namespace g08

// ---- A. the room client

// The connection to the room ended (kicked: the room server said so).
extern "C" U3X_FN void u3x_lan_client_lost(uintptr_t base, const void *view, int kicked) {
    using namespace g08;
    uint8_t *session = view_session(view);
    u3x_sock_close(view_socket(view));
    u3h_lnb_call(session, EV_DISCONNECTED);
    if (kicked) u3h_lnb_call(session, EV_KICKED);
    fld<uint64_t>(session, S_ROOM) = 0;
    game_call<void>(base + FN_CLEAR_MEMBERS, session);
}

// Applies the message at the start of the receive buffer; result = its size, 0 = nothing handled.
extern "C" U3X_FN uint32_t u3x_lan_client_message(uintptr_t base, const void *view) {
    using namespace g08;
    uint8_t *session = view_session(view);
    const uint8_t *msg = view_buffer(view);
    const uint32_t have = *view_count(view);
    const uint32_t id = rd<uint32_t>(msg, 0);                      // read whatever `have` is

    if (id == MSG_ROOM_STATE) {                                    // counted only when complete
        if (have < 0x478) return 0;
        count_message(id);
        room_state(base, session, msg);
        return 0x478;
    }
    count_message(id);                                             // every other id: on every call

    if (id == MSG_MEMBER_JOINED) {
        if (have < 0x80) return 0;
        member_joined(base, session, msg + 0x10);
        return 0x80;
    }
    if (id == MSG_MEMBER_LEFT) {
        if (have < 0x38) return 0;
        u3x_remove_member(base, session, reinterpret_cast<const char *>(msg + 0x10));
        return 0x38;
    }
    if (id == MSG_MEMBER_DATA) {
        if (have < 0x58) return 0;
        member_data(session, msg);
        return 0x58;
    }
    if (id == MSG_ELIM_NOTICE) {
        if (have < 0x10) return 0;
        if (session == party_session(base) && rd<uint32_t>(session, S_OWN_ID) != rd<uint32_t>(session, S_HOST_ID))
            *u3h_elim_received() = 1;
        return 0x10;
    }
    if (id == MSG_ADV_SETTINGS) {
        if (have < 0x20) return 0;
        adv_settings(base, session, msg);
        return 0x20;
    }
    if (id == MSG_KICKED) {
        if (have < 0x10) return 0;
        u3x_lan_client_lost(base, view, 1);
        return 0x10;
    }
    return 0;                                                      // unknown: never consumed
}

// Reads what the room server sent and applies every complete message.
extern "C" U3X_FN void u3x_lan_client_update(uintptr_t base, const void *view) {
    using namespace g08;
    void *socket = view_socket(view);
    uint32_t *count = view_count(view);
    uint8_t *buffer = view_buffer(view);

    if (!u3x_sock_open(socket)) return;
    const int32_t ready = u3x_sock_poll(socket);
    if (ready < 0) {
        u3x_lan_client_lost(base, view, 0);
        return;
    }
    if (ready == 0) return;

    const uint32_t waiting = *count;
    const int32_t got = u3x_sock_recv(socket, buffer + waiting, BUFFER_SIZE - waiting, 1);
    if (got > 0) *count += static_cast<uint32_t>(got);
    else if (got == 0) u3x_lan_client_lost(base, view, 0);         // the peer closed
    // an error: the socket layer closed the socket, nothing more here

    while (*count != 0) {
        uint32_t used = u3x_lan_client_message(base, view);
        if (used == 0) break;
        const uint32_t have = *count;
        if (used > have) used = have;
        const uint32_t rest = have - used;
        for (uint32_t i = 0; i < rest; ++i) buffer[i] = buffer[used + i];
        *count = rest;
    }
}

// ---- B. NetInit, per frame, NetShutdown

// Builds (or rebuilds) the LAN backend object and registers it.
extern "C" U3X_FN void *u3x_lan_setup(uintptr_t base, void *s, void **vtable, void *storage, void *slot0) {
    using namespace g08;
    ext_eb()[EB_SETUPS] += 1;
    u3x_lnb_vtable(vtable, slot0);

    void *object = u3h_lnb_object(base);
    if (!object) {
        object = storage;
    } else {                                                       // shut the old one down first
        u3x_lnb_s2(object);
        for (uintptr_t i = 0; i < 4; ++i) {
            uint8_t *binding = static_cast<uint8_t *>(object) + BE_BINDINGS + i * BE_BINDING_SIZE;
            u3x_sock_close(binding + BE_BIND_SOCKET);
            u3x_lnb_srv_stop(binding + BE_BIND_SERVER);
        }
    }

    uint8_t *bytes = static_cast<uint8_t *>(object);
    for (uint32_t i = 0; i < BE_SIZE; ++i) bytes[i] = 0;
    u3x_lnb_ctor(object, vtable);

    fld<uintptr_t>(object, BE_BASE) = base;
    fld<void *>(object, BE_STATE) = s;
    ext_eb()[EB_OBJECT] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(object));
    at<void *>(base + G_MANAGER) = object;
    at<void *>(base + G_LAN_OBJECT) = object;

    const uint8_t *name = reinterpret_cast<const uint8_t *>(base + G_OWN_NAME);
    for (uint32_t i = 0; i < NAME_SIZE; ++i) bytes[BE_NAME + i] = name[i];
    return object;
}

// The LAN branch of the helper's NetInit.
extern "C" U3X_FN void u3x_lan_netinit(uintptr_t base, void *s) {
    using namespace g08;
    void *backend = u3h_lnb_setup(base, s);

    const int64_t init = backend_call(backend, BE_INIT);
    ext_eb()[EB_INIT_RESULT] = static_cast<uint32_t>(init);
    if (init != 0 && lan_solo_off()) return;

    uint8_t *match = match_session(base);
    uint8_t *party = party_session(base);
    game_call<void>(base + FN_SESSION_INIT, party);
    fld<uint8_t>(party, 0x1334) = 0;                               // the party state
    fld<uint32_t>(party, 0x1338) = 0;
    fld<uint64_t>(party, 0x1340) = 0;
    fld<uint8_t>(party, 0x1348) = 0;
    fld<uint32_t>(party, 0x1370) = 0;
    fld<uint8_t>(party, 0x1374) = 0;
    fld<uint8_t>(party, 0x1375) = 0;

    backend_call(backend, BE_ATTACH, match);
    backend_call(backend, BE_ATTACH, party);

    if (at<uint8_t>(base + G_LAN_FLAG)) {
        if (fld<uint32_t>(party, 0x1338) == 0) fld<uint32_t>(party, 0x1338) = 1;
        fld<uint8_t>(party, 0x1374) = 0;
    }
    backend_call(backend, BE_ENTRY8, party, static_cast<int32_t>(1));

    if (u3h_gt_open(base) != 0 && lan_solo_off()) return;
    u3h_cm_init_lan(base);
}

// Three fixed groups of per-frame calls into other parts of the helper.
extern "C" U3X_FN void u3x_lan_frame_parts(uintptr_t base, void *s, int which) {
    if (which == 0) {
        u3h_gt_frame(base);
        u3h_gt_net_time_block(base);
        u3x_lobby_scene(base, s);
        u3h_e03_side(base, s);
        u3h_f05_place(base);
        u3h_k42_clock(base);
        u3h_k47_hud(base, s);
        u3h_e01_hud(base, s);
    } else if (which == 1) {
        u3h_gt_master_channel(base);
    } else {
        u3h_onb_probe_tick(base);
    }
}

// The LAN parts of the per-frame network update.
extern "C" U3X_FN void u3x_lan_tick(uintptr_t base, void *s) {
    using namespace g08;
    if (!at<uint8_t>(base + G_MP_ACTIVE)) return;
    u3x_lan_frame_parts(base, s, 0);

    void *lan = u3h_lnb_object(base);
    void *manager = at<void *>(base + G_MANAGER);
    void *online = u3h_online_manager(base);
    if (!manager || (manager != lan && manager != online)) return;

    if (backend_call(manager, BE_UPDATE) != 0) at<uint8_t>(base + G_NET_ERROR) = 1;

    uint8_t *party = party_session(base);
    if (at<void *>(base + G_PROFILES)) {
        ext_eb()[EB_PARTY_UPDATES] += 1;
        game_call<void>(base + FN_PARTY_UPDATE, party);
    }
    u3x_lan_frame_parts(base, s, 1);

    // online: the match session follows the party session to the online backend
    uint8_t *match = match_session(base);
    if (online && manager == online && u3h_onb_lan_flag() == 0 && rd<void *>(party, S_BACKEND) &&
        !rd<void *>(match, S_BACKEND)) {
        backend_call(online, BE_ATTACH, match);
        ext_eb()[EB_REATTACHES] += 1;
    }
    u3x_lan_frame_parts(base, s, 2);
}

// The LAN part of NetShutdown.
extern "C" U3X_FN void u3x_lan_shutdown(uintptr_t base) {
    using namespace g08;
    void *lan = u3h_lnb_object(base);
    void *online = u3h_online_manager(base);
    if (!lan && !online) return;
    ext_eb()[EB_SHUTDOWNS] += 1;

    game_call<void>(base + FN_SESSION_LEAVE, party_session(base));
    game_call<void>(base + FN_SESSION_LEAVE, match_session(base));

    void *manager = at<void *>(base + G_MANAGER);
    if (manager && (manager == lan || manager == online)) backend_call(manager, BE_SHUTDOWN);
    at<void *>(base + G_MANAGER) = nullptr;

    u3h_gt_close(base);
    u3h_cm_shutdown(base);
}

// Repeats the LAN room search every 5 seconds while the "LAN Parties" page is open.
extern "C" U3X_FN void u3x_lan_refresh(uintptr_t base) {
    using namespace g08;
    if (ext_eb()[EB_SWITCHES] & 4u) return;
    void *lan = u3h_lnb_object(base);
    if (!lan) return;
    if (u3h_active_page(base) != PAGE_LAN_PARTIES) return;

    const uint32_t now = static_cast<uint32_t>(at<int32_t>(base + G_LOCAL_CLOCK));
    if (now - ext_eb()[EB_LAST_SEARCH] < SEARCH_PERIOD) return;
    backend_call(lan, BE_SEARCH);
    ext_eb()[EB_LAST_SEARCH] = static_cast<uint32_t>(at<int32_t>(base + G_LOCAL_CLOCK));
}

// ---- C. the party rows of the lobby's player list

// The member slot of the row-th other party member (row 1 = the first), or null.
extern "C" U3X_FN const char *u3x_lan_row_name(uintptr_t base, int row) {
    using namespace g08;
    const uint8_t *party = party_session(base);
    if (rd<uint64_t>(party, S_ROOM) == 0) return nullptr;
    const uint32_t own = rd<uint32_t>(party, S_OWN_ID);
    int others = 0;
    for (int i = 0; i < MEMBER_COUNT; ++i) {
        const uint8_t *member = party + S_MEMBERS + static_cast<uintptr_t>(i) * MEMBER_SIZE;
        if (!rd<uint8_t>(member, M_IN_USE) || rd<uint32_t>(member, M_ID) == own) continue;
        if (++others == row) return reinterpret_cast<const char *>(member);
    }
    return nullptr;
}

// The number of rows: the party's members (the own one included), at least 1.
extern "C" U3X_FN int u3x_lan_rows(uintptr_t base) {
    using namespace g08;
    const uint8_t *party = party_session(base);
    if (rd<uint64_t>(party, S_ROOM) == 0) return 1;
    int used = 0;
    for (int i = 0; i < MEMBER_COUNT; ++i)
        if (rd<uint8_t>(party + S_MEMBERS + static_cast<uintptr_t>(i) * MEMBER_SIZE, M_IN_USE)) ++used;
    return used < 1 ? 1 : used;
}
