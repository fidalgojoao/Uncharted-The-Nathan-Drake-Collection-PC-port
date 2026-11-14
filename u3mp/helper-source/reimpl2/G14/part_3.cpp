// G14 part 3: online backend, room settings and member data (slots 8 and 10 to 16, the member data stamp and the
// party member data resend). Written from spec/G14_online_backend_out.md, sections 2.3, 3 and 6.

extern "C" {
// part 1 of this group (spec 2.1)
void u3x_onb_strcpy(char *d, const char *s, uint32_t n);
void u3x_onb_note(int slot);
int u3x_onb_bound(void *be, uint8_t *ss);
uint32_t u3x_onb_cookie(void *be, uint8_t *ss);
int32_t u3x_onb_send(const void *msg, uint32_t n);
void u3x_onb_wire(uintptr_t base, const char *what, uint8_t *ss, uint64_t room, const char *b, int32_t v);
void u3x_onb_name_time(uintptr_t base, char *out);
void u3x_onb_geo(uintptr_t base, uint16_t *a, uint16_t *b);

// the C part and other groups (spec 2.4)
void **u3h_onb_vt(void);
uint8_t *u3h_onb_msg(void);
uint64_t *u3h_lnb_match_room(void);
void u3h_cg_log(const char *a, int32_t v);
void *u3h_fl_state(void);
uint8_t *u3h_player_profile(uintptr_t base);
void u3h_lnb_marks(uint8_t *guest_wait, uint8_t *host_playing, uint8_t *at_start, uint64_t *followed);
void u3h_ng_stamp(uintptr_t base, void *s, uint8_t *md);
uint8_t u3x_member_status(int custom_game, int guest_waiting, int host_playing);
uint8_t u3x_member_start(int at_start, uint64_t followed, uint64_t room);
int u3x_session_host(const uint8_t *s);
int64_t u3x_lnb_s12(void *be, uint8_t *ss, const uint8_t *data, uint32_t len);

// this part (spec 2.3)
int64_t u3x_onb_s8(void *be, uint8_t *ss, uint32_t v);
int64_t u3x_onb_s10(void *be, uint8_t *ss, uint32_t id);
int64_t u3x_onb_s11(void *be, uint8_t *ss, uint32_t v);
void u3x_onb_md_stamp(uintptr_t base, uint8_t *md);
int64_t u3x_onb_s12(void *be, uint8_t *ss, const uint8_t *data, uint32_t len);
void u3x_fl_md(void *lan_be, uint8_t *ps);
int64_t u3x_onb_s13(void *be, uint8_t *ss, uint32_t v);
int64_t u3x_onb_s14(void *be, uint8_t *ss, const uint16_t *rank, uint32_t n);
int64_t u3x_onb_s15(void *be, uint8_t *ss);
int64_t u3x_onb_s16(void *be, uint8_t *ss);
}

namespace g14p3 {

// ---- the helper's own log texts (spec 3.7)
U3X_RO const char t_made[] = "match start (slot 11): net players created ";
U3X_RO const char t_local[] = "  ... local net player present ";
U3X_RO const char t_there[] = "match start (slot 11): local net player already there";
U3X_RO const char t_loaded[] = "every player connected (slot 14): start-load flag raised, players ";
U3X_RO const char t_notonb[] = "party session is not on the online backend: member data not sent";
U3X_RO const char t_mdsent[] = "party member data sent: room-follow marks, at start ";
static_assert(sizeof t_made == 43 + 1 && sizeof t_local == 31 + 1 && sizeof t_there == 53 + 1);
static_assert(sizeof t_loaded == 66 + 1 && sizeof t_notonb == 64 + 1 && sizeof t_mdsent == 52 + 1);

// ---- variables of this part (spec 3.4), zero at load
U3X_BSS uint32_t md_count;   // calls of u3x_fl_md that reached the online slot
U3X_BSS uint8_t md_once;     // the "not on the online backend" line was written

// ---- image offsets (spec 3.1)
constexpr uintptr_t IMG_MENU_MODE = 0x17d603c;      // u32, 2 = Custom Game
constexpr uintptr_t IMG_NET_GAME_INDEX = 0x17d167c; // s16
constexpr uintptr_t IMG_MATCH_SESSION = 0x17d6420;
constexpr uintptr_t IMG_PARTY_SESSION = 0x17dc298;
constexpr uintptr_t IMG_NET_GAME_MANAGER = 0x17cb060;
constexpr uintptr_t IMG_LOCAL_NET_PLAYER = 0x17cb3e0; // pointer
constexpr uintptr_t IMG_NET_STATE = 0x1b68810;        // u32
constexpr uintptr_t IMG_MAKE_NET_PLAYER = 0x72eca0;   // game function

// ---- session fields (spec 3.3)
constexpr uintptr_t SS_BACKEND = 8, SS_ROOM = 0x18, SS_NAME = 0x20, SS_RANK_DONE = 0xe0, SS_NAME_DONE = 0xe8;
constexpr uintptr_t SS_FLAGS_SET = 0xf0, SS_ROOM_FLAGS = 0x204, SS_MEMBERS = 0x680, SS_MEMBER_SIZE = 0x120;
constexpr uintptr_t SS_OWN_ID = 0x12e4, SS_HOST_ID = 0x12e8, SS_ATTR = 0x12ec, SS_MD_LEN = 0x12f0, SS_MD = 0x12f4;
constexpr uintptr_t SS_START_LOADING = 0x1338;

// ---- room message types (spec 3.5)
constexpr uint32_t MSG_MEMBER_DATA = 314, MSG_PROMOTE = 316, MSG_ATTR = 318, MSG_ROOM_FLAGS = 320;
constexpr uint32_t MSG_HOST_RANK = 322, MSG_ROOM_NAME = 323;

template <typename T> [[gnu::always_inline]] inline void put(void *p, uintptr_t off, T v) {
    *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + off) = v;
}

[[gnu::always_inline]] inline uintptr_t base_of(const void *be) { return rd<uint64_t>(be, 0x1058); }
[[gnu::always_inline]] inline uint64_t room_of(const uint8_t *ss) { return rd<uint64_t>(ss, SS_ROOM); }
[[gnu::always_inline]] inline bool attached(void *be, uint8_t *ss) { return u3x_onb_bound(be, ss) != -1; }

// Starts a message in the shared buffer (spec 3.9): the first `size` bytes zero, then the type.
U3X_FN static uint8_t *new_message(uint32_t type, uint32_t size) {
    uint8_t *m = u3h_onb_msg();
    for (uint32_t i = 0; i < size; ++i) m[i] = 0;
    put<uint32_t>(m, 0, type);
    return m;
}

// The attribute message 318 for a room.
U3X_FN static void send_attr(uint64_t room, uint8_t value, uint8_t mask) {
    uint8_t *m = new_message(MSG_ATTR, 16);
    m[4] = value;
    m[5] = mask;
    put<uint64_t>(m, 8, room);
    u3x_onb_send(m, 16);
}

// "Send the room name" (spec 6.9): nothing without a room.
U3X_FN static void send_room_name(uint8_t *ss) {
    const uint64_t room = room_of(ss);
    if (!room) return;
    uint8_t *m = new_message(MSG_ROOM_NAME, 144);
    put<uint64_t>(m, 8, room);
    u3x_onb_strcpy(reinterpret_cast<char *>(m + 16), reinterpret_cast<const char *>(ss + SS_NAME), 128);
    u3x_onb_send(m, 144);
}

// Match start (spec 6.3 step 2): a net player for every member, unless the local one exists already.
U3X_FN static void make_net_players(uintptr_t base, uint8_t *ss) {
    if (at<void *>(base + IMG_LOCAL_NET_PLAYER)) {
        u3h_cg_log(t_there, -1);
        return;
    }
    int32_t made = 0;
    for (uint32_t i = 0; i < 10; ++i) {
        uint8_t *member = ss + SS_MEMBERS + i * SS_MEMBER_SIZE;
        if (!member[0x25]) continue;   // slot not in use
        uint8_t *player = game_call<uint8_t *>(base + IMG_MAKE_NET_PLAYER, reinterpret_cast<void *>(base + IMG_NET_GAME_MANAGER),
                                               ss, member, uint32_t{0}, uint32_t{0});
        if (!player) continue;
        put<int32_t>(player, 0x21c, -1);   // no team
        player[0x365] = 0;                 // not ready
        ++ext_eb()[93];
        ++made;
    }
    u3h_cg_log(t_made, made);
    u3h_cg_log(t_local, at<void *>(base + IMG_LOCAL_NET_PLAYER) ? 1 : 0);
}

// ---- the exported functions (C names; defined inside the namespace so that its helpers are found first)
extern "C" {

// Slot 8, SetAttr: the room attribute flag on or off.
U3X_FN int64_t u3x_onb_s8(void *be, uint8_t *ss, uint32_t v) {
    u3x_onb_note(8);
    if (!attached(be, ss)) return -1;
    const uint8_t value = static_cast<uint8_t>(v);
    if ((value != 0) == (ss[SS_ATTR] != 0)) return -1;   // no change
    ss[SS_ATTR] = value;
    const uint64_t room = room_of(ss);
    if (!room) return 0;
    const bool custom_game = at<uint32_t>(base_of(be) + IMG_MENU_MODE) == 2;
    send_attr(room, custom_game ? value : static_cast<uint8_t>(value ? 3 : 0), 3);
    return 0;
}

// Slot 10, Promote.
U3X_FN int64_t u3x_onb_s10(void *be, uint8_t *ss, uint32_t id) {
    u3x_onb_note(10);
    if (!attached(be, ss)) return -1;
    const uint64_t room = room_of(ss);
    if (!room) return 0;
    uint8_t *m = new_message(MSG_PROMOTE, 16);
    put<uint16_t>(m, 4, static_cast<uint16_t>(id));
    put<uint64_t>(m, 8, room);
    u3x_onb_send(m, 16);
    return 0;
}

// Slot 11, RoomFlags; a non-zero value on the match session also makes the match's net players.
U3X_FN int64_t u3x_onb_s11(void *be, uint8_t *ss, uint32_t v) {
    u3x_onb_note(11);
    const uintptr_t base = base_of(be);
    if (v && ss == reinterpret_cast<uint8_t *>(base + IMG_MATCH_SESSION)) make_net_players(base, ss);
    if (!attached(be, ss)) return -1;
    const uint64_t room = room_of(ss);
    if (room) {
        uint8_t *m = new_message(MSG_ROOM_FLAGS, 16);
        put<uint16_t>(m, 4, static_cast<uint16_t>(v));
        put<uint64_t>(m, 8, room);
        u3x_onb_send(m, 16);
    } else if (u3x_session_host(ss)) {
        put<uint32_t>(ss, SS_ROOM_FLAGS, v);
        ss[SS_FLAGS_SET] = 1;
    }
    return 0;
}

// The helper's own fields in the last 16 bytes of a 0x40-byte member data block.
U3X_FN void u3x_onb_md_stamp(uintptr_t base, uint8_t *md) {
    void *state = u3h_fl_state();
    uint8_t *profile = u3h_player_profile(base);
    if (state) {
        const uintptr_t rank_word = rd<uintptr_t>(state, 936);   // native 21: rank word of a profile
        if (rank_word && profile) put<uint32_t>(md, 0x3c, game_call<uint32_t>(rank_word, profile) | 0x20000000u);
    }

    uint8_t guest_wait = 0, host_playing = 0, at_start = 0;
    uint64_t followed = 0;
    u3h_lnb_marks(&guest_wait, &host_playing, &at_start, &followed);
    const uint64_t match_room = *u3h_lnb_match_room();
    put<uint64_t>(md, 0x34, match_room);
    put<int16_t>(md, 0x30, at<int16_t>(base + IMG_NET_GAME_INDEX));
    md[0x32] = u3x_member_status(at<uint32_t>(base + IMG_MENU_MODE) == 2, guest_wait, host_playing);
    md[0x33] = u3x_member_start(at_start, followed, match_room);

    u3h_ng_stamp(base, state, md);
}

// Slot 12, MemberData: keeps the data in the session and sends it to the room.
U3X_FN int64_t u3x_onb_s12(void *be, uint8_t *ss, const uint8_t *data, uint32_t len) {
    u3x_onb_note(12);
    if (len > 64) return -1;
    put<uint32_t>(ss, SS_MD_LEN, len);
    for (uint32_t i = 0; i < len; ++i) ss[SS_MD + i] = data[i];
    if (!attached(be, ss)) return -1;
    const uint64_t room = room_of(ss);
    if (!room) return 0;
    uint8_t *m = new_message(MSG_MEMBER_DATA, 80);
    m[4] = static_cast<uint8_t>(len);
    put<uint64_t>(m, 8, room);
    for (uint32_t i = 0; i < len; ++i) m[16 + i] = data[i];
    const uintptr_t base = base_of(be);
    if (ss == reinterpret_cast<uint8_t *>(base + IMG_PARTY_SESSION) && len == 0x40) u3x_onb_md_stamp(base, m + 16);
    u3x_onb_send(m, 80);
    return 0;
}

// Sends this machine's party member data again (LAN or online).
U3X_FN void u3x_fl_md(void *lan_be, uint8_t *ps) {
    if (lan_be) {
        u3x_lnb_s12(lan_be, ps, ps + SS_MD, 0x40);
        return;
    }
    void *backend = rd<void *>(ps, SS_BACKEND);
    if (!backend || rd<void **>(backend, 0) != u3h_onb_vt()) {
        if (!md_once) {
            md_once = 1;
            u3h_cg_log(t_notonb, -1);
        }
        return;
    }
    put<uint32_t>(ps, SS_MD_LEN, 0x40);
    u3x_onb_s12(backend, ps, ps + SS_MD, 0x40);
    const uint32_t before = md_count++;
    if (before < 40) {
        uint8_t guest_wait = 0, host_playing = 0, at_start = 0;
        uint64_t followed = 0;
        u3h_lnb_marks(&guest_wait, &host_playing, &at_start, &followed);
        u3h_cg_log(t_mdsent, at_start);
    }
}

// Slot 13, SetAttr4: attribute bit 4 of the room.
U3X_FN int64_t u3x_onb_s13(void *be, uint8_t *ss, uint32_t v) {
    u3x_onb_note(13);
    if (!attached(be, ss)) return -1;
    const uint64_t room = room_of(ss);
    if (!room) return 0;
    send_attr(room, static_cast<uint8_t>(v) ? 4 : 0, 4);
    return 0;
}

// Slot 14, HostRank; the match host waiting for everyone also raises "start loading" here.
U3X_FN int64_t u3x_onb_s14(void *be, uint8_t *ss, const uint16_t *rank, uint32_t n) {
    u3x_onb_note(14);
    const uintptr_t base = base_of(be);
    if (ss == reinterpret_cast<uint8_t *>(base + IMG_MATCH_SESSION) && at<uint32_t>(base + IMG_NET_STATE) == 0x30 &&
        !ss[SS_START_LOADING] && rd<uint32_t>(ss, SS_OWN_ID) == rd<uint32_t>(ss, SS_HOST_ID)) {
        ss[SS_START_LOADING] = 1;
        u3h_cg_log(t_loaded, static_cast<int32_t>(n));
    }
    if (!attached(be, ss)) return -1;
    const uint64_t room = room_of(ss);
    if (!room) {
        ss[SS_RANK_DONE] = 1;
        return 0;
    }
    const uint32_t count = n < 120 ? n : 120;
    const uint32_t size = 16 + 2 * count;
    uint8_t *m = new_message(MSG_HOST_RANK, size);
    put<uint16_t>(m, 4, static_cast<uint16_t>(count));
    put<uint64_t>(m, 8, room);
    for (uint32_t i = 0; i < count; ++i) put<uint16_t>(m, 16 + 2 * i, rank[i]);
    u3x_onb_send(m, size);
    return 0;
}

// Slot 15, RoomNameAuto: the room is named "own name, dot, clock".
U3X_FN int64_t u3x_onb_s15(void *be, uint8_t *ss) {
    u3x_onb_note(15);
    ss[SS_NAME_DONE] = 1;
    u3x_onb_name_time(base_of(be), reinterpret_cast<char *>(ss + SS_NAME));
    send_room_name(ss);
    return 0;
}

// Slot 16, RoomName: the session's name as it is.
U3X_FN int64_t u3x_onb_s16(void *, uint8_t *ss) {
    u3x_onb_note(16);
    ss[SS_NAME_DONE] = 1;
    send_room_name(ss);
    return 0;
}

}  // extern "C"
}  // namespace g14p3
