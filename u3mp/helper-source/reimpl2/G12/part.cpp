// ---- G12: session member list and the EstablishConnection Enter, the connection monitor's two checks, the reliable
// header of game packets. Written from spec/G12_packets_monitor.md (section numbers in the comments are the spec's).

struct GtStream;   // the bit stream (defined earlier in this part); this group only passes it on

extern "C" {
// helpers of the C part and of other groups
uint8_t *u3h_ext(void);
int u3h_mp_code_is(uintptr_t base, uint32_t off, const char *hex12);
int u3x_key_equal(const uint8_t *a, const uint8_t *b);
uint8_t *u3x_find_member(uint8_t *session, uint32_t id);
int u3x_session_host(const uint8_t *s);
void u3x_session_kick(uint8_t *s, uint32_t id);
uint8_t *u3x_entry_by_id(uintptr_t base, int32_t id);
uint32_t u3x_net_player_id(const uint8_t *np);
int u3h_link_timeout(uintptr_t base, uint8_t *entry);
void u3h_gt_stamp(uintptr_t base, uint8_t *sub, int off);
void u3h_gt_free(uintptr_t base, void *p);
void u3x_gt_header(const uint8_t *sub, void *b, uint32_t flag);
uint32_t u3x_gt_read(GtStream *s, int count);
// this group
uint32_t u3x_session_members(uint8_t *session, uint8_t **out);
uint8_t *u3x_ec_find_np(uintptr_t base, const uint8_t *member, int32_t idx);
void u3x_ec_enter(uintptr_t base, uint8_t *st);
void u3x_members_check(uintptr_t base, int arg);
void u3x_players_check(uintptr_t base);
void u3x_gt_base_write(uintptr_t base, uint8_t *sub, void *b);
int u3x_gt_base_header(uintptr_t base, uint8_t *sub, void *b);
void u3x_gt_drop_expired(uintptr_t base, uint8_t *sub);
int u3x_gt_has_data(uintptr_t base, uint8_t *sub);
}

namespace {
namespace g12 {

// image offsets (3.1, 3.2, 3.5)
constexpr uintptr_t net_manager_at = 0x17cb060;
constexpr uintptr_t match_session_at = 0x17d6420;
constexpr uintptr_t party_session_at = 0x17dc298;
constexpr uintptr_t net_clock_at = 0x17c3580;
constexpr uintptr_t menu_mode_at = 0x17d603c;
constexpr uintptr_t net_active_at = 0x17d5c61;
constexpr uintptr_t net_flags_at = 0x17d5c98;
constexpr uintptr_t game_object_at = 0x1b24b80;
constexpr uintptr_t frame_clock_at = 0x1b24b98;
constexpr uint32_t reset_sends_fn = 0x74b290;   // resets the pending player-state sends
constexpr uint32_t lobby_data_fn = 0x7a3400;    // (session, member) -> lobby data record

constexpr uint32_t list_max = 10;        // member slots of a session, net player records of the manager
constexpr uintptr_t member_size = 0x120;
constexpr uintptr_t player_size = 0x9c0;

// the first 12 bytes both game functions must start with before they are called
U3X_RO const char frame_prologue[] = "554889e54157415641554154";

// a field to write
template <typename T> [[gnu::always_inline]] inline T &fld(void *p, uintptr_t off) {
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + off);
}

// virtual slot n of an object: the function at (function table) + 8 n
template <typename R, typename... A> [[gnu::always_inline]] inline R slot_call(const void *object, uint32_t slot, A... args) {
    return game_call<R>(rd<uintptr_t>(rd<const uint8_t *>(object, 0), 8u * slot), args...);
}

U3X_FN uint8_t *net_player(uintptr_t base, uint32_t index) {
    return reinterpret_cast<uint8_t *>(base + net_manager_at + 0x390 + index * player_size);
}

U3X_FN void count_eb(uint32_t index) { ++ext_eb()[index]; }

U3X_FN uint32_t *nd_words() { return reinterpret_cast<uint32_t *>(u3h_ext() + 0x3a80); }

// the backend kick request of 3.3; true when the backend call was made
U3X_FN bool kick_request(uint8_t *session, uint32_t id, int flag) {
    uint8_t *backend = rd<uint8_t *>(session, 8);
    if (!backend || !rd<const uint8_t *>(backend, 0)) return false;
    slot_call<uint64_t>(backend, 9, backend, session, id, flag);
    return true;
}

// the Enter's kick: counted in nd[32] only when the request reached the backend
U3X_FN void enter_kick(uint8_t *session, uint32_t id, int flag) {
    if (kick_request(session, id, flag)) ++nd_words()[32];
}

// 4.5 "stamp": the entry's last-heard time = net time now (0 without clock)
U3X_FN void stamp_heard(uint8_t *entry, const uint8_t *clock) {
    fld<int32_t>(entry, 0xe0) = clock ? rd<int32_t>(clock, 0xc) : 0;
    count_eb(123);
}

}  // namespace g12
}  // namespace

// 4.1: the members of a session, the own member first
extern "C" U3X_FN uint32_t u3x_session_members(uint8_t *session, uint8_t **out) {
    if (!rd<uint8_t *>(session, 8) || !rd<uint64_t>(session, 0x18)) return 0;
    const uint32_t own_id = rd<uint32_t>(session, 0x12e4);
    uint32_t count = 0;
    for (uint32_t k = 0; k < g12::list_max; ++k) {
        uint8_t *slot = session + 0x680 + k * g12::member_size;
        if (!rd<uint8_t>(slot, 0x25)) continue;
        if (count && rd<uint32_t>(slot, 0x2c) == own_id) {
            out[count] = out[0];   // the member that was first goes to the end
            out[0] = slot;
        } else {
            out[count] = slot;
        }
        ++count;
    }
    return count;
}

// 4.2: the net player with the member's key and the user number idx
extern "C" U3X_FN uint8_t *u3x_ec_find_np(uintptr_t base, const uint8_t *member, int32_t idx) {
    for (uint32_t i = 0; i < g12::list_max; ++i) {
        uint8_t *np = g12::net_player(base, i);
        if (u3x_key_equal(np + 0x5b8, member) && rd<int32_t>(np, 0x1e8) == idx) return np;
    }
    return nullptr;
}

// 4.3: Enter of the state EstablishConnection
extern "C" U3X_FN void u3x_ec_enter(uintptr_t base, uint8_t *st) {
    uint8_t *const match = reinterpret_cast<uint8_t *>(base + g12::match_session_at);
    uint8_t *const party = reinterpret_cast<uint8_t *>(base + g12::party_session_at);

    // 1. start time
    const uint8_t *clock = at<const uint8_t *>(base + g12::net_clock_at);
    if (clock) g12::fld<int32_t>(st, 0x1c) = rd<int32_t>(clock, 0xc);
    // 2. drop the pending player-state sends
    if (u3h_mp_code_is(base, g12::reset_sends_fn, g12::frame_prologue)) game_call<void>(base + g12::reset_sends_fn);
    // 3. settings not sent yet
    g12::fld<uint8_t>(st, 0x18) = 0;

    // 4. readiness reset for the players of every match member
    uint8_t *members[g12::list_max];
    uint32_t count = u3x_session_members(match, members);
    for (uint32_t m = 0; m < count; ++m) {
        uint8_t *member = members[m];
        if (uint8_t *first = u3x_ec_find_np(base, member, 0)) g12::fld<uint8_t>(first, 0x5ee) = 0;
        if (!u3h_mp_code_is(base, g12::lobby_data_fn, g12::frame_prologue)) continue;
        const uint8_t *lobby = game_call<const uint8_t *>(base + g12::lobby_data_fn, match, member);
        if (!lobby || !rd<uint8_t>(lobby, 25)) continue;   // no second player
        if (uint8_t *second = u3x_ec_find_np(base, member, 1)) g12::fld<uint8_t>(second, 0x5ee) = 0;
    }

    // 5. party / custom-game flow: the party list replaces the match list; party members without a player go
    if (at<uint32_t>(base + g12::menu_mode_at) == 2) {
        count = u3x_session_members(party, members);
        for (uint32_t m = 0; m < count; ++m)
            if (!u3x_ec_find_np(base, members[m], 0)) g12::enter_kick(party, rd<uint32_t>(members[m], 0x2c), 0);
    }

    // 6. players that belong to no member of the current list go (always asked from the match session)
    if (at<uint8_t>(base + g12::net_active_at)) {
        for (uint32_t i = 0; i < g12::list_max; ++i) {
            uint8_t *np = g12::net_player(base, i);
            if (rd<int32_t>(np, 0x1e8) != 0 || !(rd<uint32_t>(np, 0x1e4) & 0xfff)) continue;
            bool has_member = false;
            for (uint32_t m = 0; m < count && !has_member; ++m) has_member = u3x_ec_find_np(base, members[m], 0) == np;
            if (!has_member) g12::enter_kick(match, rd<uint32_t>(np, 0x5b0), 1);
        }
    }

    // 7. diagnostics
    uint32_t *nd = g12::nd_words();
    ++nd[33];
    nd[34] = rd<uint32_t>(st, 0x1c);
}

// 4.4: the host asks to kick every match member whose connection is gone or dropped
extern "C" U3X_FN void u3x_members_check(uintptr_t base, int arg) {
    uint8_t *const match = reinterpret_cast<uint8_t *>(base + g12::match_session_at);
    if (!u3x_session_host(match) || !arg) return;

    uint8_t *members[g12::list_max];
    const uint32_t count = u3x_session_members(match, members);
    const uint8_t *self = nullptr;
    if (rd<uint64_t>(match, 0x18)) self = u3x_find_member(match, rd<uint32_t>(match, 0x12e4));

    for (uint32_t m = 0; m < count; ++m) {
        uint8_t *member = members[m];
        if (member == self) continue;
        const uint8_t *entry = u3x_entry_by_id(base, rd<int32_t>(member, 0x34));
        if (entry && !rd<uint8_t>(entry, 5)) continue;   // connected and not dropped: stays
        u3x_session_kick(match, rd<uint32_t>(member, 0x2c));
        g12::count_eb(121);
    }
}

// 4.5: last-heard stamps, "main player gone" marks and the silence test for every remote net player
extern "C" U3X_FN void u3x_players_check(uintptr_t base) {
    uint8_t *const manager = reinterpret_cast<uint8_t *>(base + g12::net_manager_at);
    uint8_t *const match = reinterpret_cast<uint8_t *>(base + g12::match_session_at);
    const uint8_t *clock = at<const uint8_t *>(base + g12::net_clock_at);

    for (uint32_t i = 0; i < g12::list_max; ++i) {
        uint8_t *np = g12::net_player(base, i);
        if (np == rd<uint8_t *>(manager, 0x370) || np == rd<uint8_t *>(manager, 0x378)) continue;   // local
        if (!rd<uint8_t>(np, 0xe1) || rd<uint8_t>(np, 0x5e4)) continue;                            // not joined / leaving
        const uint32_t id = u3x_net_player_id(np);
        if (!id) continue;
        const int32_t connection = rd<int32_t>(np, 0x5b4);
        if (connection == -1) continue;

        uint8_t *entry = u3x_entry_by_id(base, connection);
        if (!entry || rd<uint8_t>(entry, 6)) {
            // no usable link: the main player is marked gone, another player's lost entry is only stamped
            const uint8_t *main_player = rd<const uint8_t *>(manager, 0x380);
            if (main_player && u3x_net_player_id(main_player) == id) {
                g12::fld<uint8_t>(np, 0x5e4) = 1;
                g12::fld<uint32_t>(np, 0x1e4) = 0;
                g12::count_eb(122);
            } else if (entry) {
                g12::stamp_heard(entry, clock);
            }
            continue;
        }

        // live link: stamp, then the silence test
        g12::stamp_heard(entry, clock);
        const uint8_t *game = at<const uint8_t *>(base + g12::game_object_at);
        if (at<int32_t>(base + g12::net_flags_at) != 0 && !(game && rd<uint8_t>(game, 0x3a04))) continue;
        if (!u3h_link_timeout(base, entry)) continue;
        if (rd<uint8_t>(np, 0x1ec)) continue;
        if (u3x_session_host(match)) {
            u3x_session_kick(match, rd<uint32_t>(np, 0x1e4));   // the whole id word
            g12::count_eb(124);
        } else {
            g12::fld<uint8_t>(entry, 6) = 1;
            g12::count_eb(125);
        }
    }
}

// 4.6: the header of an outgoing data packet; a reliable one gets a sent record and its payload
extern "C" U3X_FN void u3x_gt_base_write(uintptr_t base, uint8_t *sub, void *b) {
    const uint32_t out = rd<uint32_t>(sub, 8);
    if (out - rd<uint32_t>(sub, 0x18) >= 31) {   // window full: plain header, nothing reliable
        u3x_gt_header(sub, b, 0);
        g12::count_eb(118);
        return;
    }
    const uint32_t number = out + 1;
    g12::fld<uint32_t>(sub, 8) = number;
    u3x_gt_header(sub, b, 1);
    g12::fld<uint32_t>(sub, 0x1c + 4 * (number & 31)) = rd<uint32_t>(sub, 0xc);
    u3h_gt_stamp(base, sub, 0xa4);

    uint8_t *record = g12::slot_call<uint8_t *>(sub, 0, sub);
    if (!record) return;
    g12::fld<uint32_t>(record, 4) = number;
    if (!rd<uint8_t *>(sub, 0x100))
        g12::fld<uint8_t *>(sub, 0x100) = record;
    else
        g12::fld<uint8_t *>(rd<uint8_t *>(sub, 0x108), 8) = record;
    g12::fld<uint8_t *>(sub, 0x108) = record;
    g12::fld<uint32_t>(record, 0) = rd<uint32_t>(sub, 0xa0);
    g12::fld<uint8_t *>(record, 8) = nullptr;

    g12::slot_call<void>(sub, 2, sub, b, record);
    g12::count_eb(101);
}

// 4.7: the header of a received data packet; 1 when its payload is new and reliable
extern "C" U3X_FN int u3x_gt_base_header(uintptr_t base, uint8_t *sub, void *b) {
    GtStream *stream = static_cast<GtStream *>(b);
    const uint32_t flag = u3x_gt_read(stream, 1);
    const uint32_t seq = u3x_gt_read(stream, 11);
    const uint32_t ack = u3x_gt_read(stream, 10);

    const uint32_t in = rd<uint32_t>(sub, 0xc);
    const uint32_t ahead = (seq - in) & 0x7ff;   // how far the packet is ahead of the newest one received
    if (ahead >= 32) return 0;
    const uint32_t acked = rd<uint32_t>(sub, 0x18);
    const uint32_t reported = (ack - acked) & 0x3ff;   // own packets the other side newly reports on
    if (acked + reported > rd<uint32_t>(sub, 8)) return 0;   // acknowledges something never sent
    const uint32_t bits = u3x_gt_read(stream, 32);

    const bool fresh = ahead != 0 && flag != 0;
    g12::fld<uint32_t>(sub, 0x14) = (rd<uint32_t>(sub, 0x14) << ahead) | (fresh ? 1u : 0u);

    // the oldest in-flight records leave the list, one per reported packet: acked or lost by the packet's bit
    for (uint32_t left = reported; left; --left) {
        uint8_t *record = rd<uint8_t *>(sub, 0x100);
        if (!record) break;
        g12::fld<uint8_t *>(sub, 0x100) = rd<uint8_t *>(record, 8);
        if ((bits >> ((left - 1) & 31)) & 1) {
            g12::slot_call<void>(sub, 3, sub, record, static_cast<void *>(nullptr));
            ++g12::fld<uint32_t>(sub, 0xb0);
            g12::count_eb(103);
        } else {
            g12::slot_call<void>(sub, 4, sub, record, static_cast<void *>(nullptr));
            ++g12::fld<uint32_t>(sub, 0xac);
            g12::count_eb(104);
        }
        u3h_gt_free(base, record);
    }

    g12::fld<uint32_t>(sub, 0xc) = in + ahead;
    g12::fld<uint32_t>(sub, 0x18) = acked + reported;
    return fresh ? 1 : 0;
}

// 4.8: expired events leave the queue of unreliable events
extern "C" U3X_FN void u3x_gt_drop_expired(uintptr_t base, uint8_t *sub) {
    uint8_t *kept = nullptr;   // the last node that stays
    uint8_t *node = rd<uint8_t *>(sub, 0x120);
    while (node) {
        uint8_t *next = rd<uint8_t *>(node, 0x20);
        uint8_t *event = rd<uint8_t *>(node, 0);
        if (!event || !(g12::slot_call<uint64_t>(event, 7, event) & 0xff)) {
            kept = node;
        } else {
            if (kept)
                g12::fld<uint8_t *>(kept, 0x20) = next;
            else
                g12::fld<uint8_t *>(sub, 0x120) = next;
            const uint8_t references = static_cast<uint8_t>(rd<uint8_t>(event, 10) - 1);
            g12::fld<uint8_t>(event, 10) = references;
            if (!references) g12::slot_call<void>(event, 1, event);
            if (void *extra = rd<void *>(node, 0x10)) u3h_gt_free(base, extra);
            u3h_gt_free(base, node);
            g12::count_eb(116);
        }
        node = next;
    }
    g12::fld<uint8_t *>(sub, 0x128) = kept;
}

// 4.9: is there anything worth a data packet
extern "C" U3X_FN int u3x_gt_has_data(uintptr_t base, uint8_t *sub) {
    // an in-flight packet that carries a real event (type 2 does not count)
    for (const uint8_t *record = rd<const uint8_t *>(sub, 0x100); record; record = rd<const uint8_t *>(record, 8))
        for (const uint8_t *node = rd<const uint8_t *>(record, 0x10); node; node = rd<const uint8_t *>(node, 0x20)) {
            const uint8_t *event = rd<const uint8_t *>(node, 0);
            if (event && rd<uint8_t>(event, 9) != 2) return 1;
        }
    // the keep-alive timer (150 ticks = 0.5 s)
    const int32_t now = rd<int32_t>(at<const uint8_t *>(base + g12::frame_clock_at), 0x114);
    if (rd<int32_t>(sub, 0x164) <= now) {
        g12::fld<int32_t>(sub, 0x164) = static_cast<int32_t>(static_cast<uint32_t>(now) + 150u);
        return 1;
    }
    // events waiting to be sent
    return rd<const uint8_t *>(sub, 0x110) || rd<const uint8_t *>(sub, 0x120) ? 1 : 0;
}
