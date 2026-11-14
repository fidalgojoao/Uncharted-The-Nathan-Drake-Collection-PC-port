// G21 part A: party join, requests and messages. "Join Party" on the selected player, the pump that sorts the
// received messages, the hold queue for join requests the host cannot answer yet, and the late answer for a request
// the game first had to create a party for. Written from spec G21_party_join.md, sections 4.1 to 4.5.

extern "C" {
// other groups and the C part
void u3x_send_join(uintptr_t base, const uint8_t *id, uint64_t party, uintptr_t callback);
int32_t u3h_presence(uintptr_t base, const uint8_t *id, uint8_t *presence);
void u3h_show_dialog(uintptr_t base, void *s, uint32_t body, uint32_t title, uint32_t action, int32_t buttons,
                     uint32_t extra, uint32_t close);
void u3h_fj_note(const char *a, const uint8_t *id, const char *b, int32_t v);
int32_t u3h_msg_event(uintptr_t base, uint8_t *id, uint32_t *type, uint8_t *data, uint32_t max);
int u3h_join_host_answer(uintptr_t base, const uint8_t *id, const uint8_t *d, int32_t n);
int u3h_join_msg_in(uintptr_t base, const uint8_t *id, uint32_t type, const uint8_t *d, int32_t n);
int u3h_join_locked(uintptr_t base);
int u3h_join_can_accept(uint8_t *ps, uint32_t state);
uint64_t u3h_join_party_reply(uintptr_t base);
void u3h_join_info_send(uintptr_t base, const uint8_t *id);
void u3h_join_answer(uintptr_t base, const uint8_t *id, uint64_t party, uint8_t flag);
uint8_t *u3h_join_request(void);
uint8_t *u3h_join_request_on(void);
uint8_t *u3h_join_request_pc(void);
int32_t u3x_queue_post(uintptr_t base, const uint8_t *id, uint32_t type, const void *data, uint32_t length);
int32_t u3x_queue_send(uintptr_t base, const uint8_t *id, uint32_t type, const void *data, uint32_t length);
// this part
void u3x_join_party(uintptr_t base, void *s, uintptr_t callback);
void u3x_message_events(uintptr_t base);
void u3x_join_hold(uintptr_t base, const uint8_t *id, const uint8_t *d);
void u3x_join_hold_tick(uintptr_t base);
void u3x_join_reply_tick(uintptr_t base);
}

namespace g21a {

constexpr uintptr_t CLOCK = 0x17b4ba4;      // s32 game clock, 300 ticks per second
constexpr uintptr_t SELECTED = 0x17c3871;   // the selected player's id
constexpr uintptr_t PARTY = 0x17dc298;      // the party session
constexpr uintptr_t SES_ROOM = 0x18;        // u64 party id, 0 = no room
constexpr uintptr_t SES_IN_PARTY = 0xc8;    // u8 "leave done": with a room id, we are in a party
constexpr uintptr_t SES_STATE = 0x1338;     // u32 party state
constexpr uintptr_t SES_SERVING = 0x1348;   // u8: the game creates a party for a request
constexpr uintptr_t SES_REQUESTER = 0x1349; // id of that requester

constexpr uint32_t ID_SIZE = 0x24;
constexpr uint32_t REQUEST_SIZE = 0x10;
constexpr uint32_t MSG_JOIN_REQUEST = 0x10b;
constexpr uint32_t MSG_JOIN_ANSWER = 0x10c;
constexpr uint32_t MSG_MAX = 0x1fc;
constexpr int32_t FIVE_SECONDS = 1500;
constexpr uint32_t HOLD_MAX = 4;

U3X_RO static const char t_join[] = "join: ";
U3X_RO static const char t_locked[] = " party locked (presence): not asked";
U3X_RO static const char t_held[] = "held join request from ";
U3X_RO static const char t_accepted[] = " accepted";
U3X_RO static const char t_refused_locked[] = " refused: locked";
U3X_RO static const char t_refused_late[] = " refused after 5 s, state ";
U3X_RO static const char t_to_game[] = " to the game (state 0)";
U3X_RO static const char t_request[] = "join request from ";
U3X_RO static const char t_party_up[] = " answered: new party up";

// a join request that waits for the host's decision
struct Held {
    uint8_t id[ID_SIZE];             // who asked
    uint8_t request[REQUEST_SIZE];   // the 0x10b data
    int32_t since;                   // game clock when it was first held
    uint8_t flag;                    // the request's flag byte
};

U3X_BSS static Held held[HOLD_MAX];
U3X_BSS static uint32_t held_count;

U3X_FN static void zero(uint8_t *to, uint32_t size) {
    for (uint32_t i = 0; i < size; ++i) to[i] = 0;
}

U3X_FN static void copy(uint8_t *to, const uint8_t *from, uint32_t size) {
    for (uint32_t i = 0; i < size; ++i) to[i] = from[i];
}

U3X_FN static bool same(const uint8_t *a, const uint8_t *b, uint32_t size) {
    for (uint32_t i = 0; i < size; ++i)
        if (a[i] != b[i]) return false;
    return true;
}

// takes entry `index` out of the queue; the later ones move one place down
U3X_FN static void drop(uint32_t index) {
    for (uint32_t i = index; i + 1 < held_count; ++i)
        copy(reinterpret_cast<uint8_t *>(&held[i]), reinterpret_cast<const uint8_t *>(&held[i + 1]), sizeof(Held));
    --held_count;
}

enum class Verdict { wait, accept, to_game, refuse_locked, refuse_late };

}  // namespace g21a

// "Join Party" on the selected player: ask to join, unless the presence says it is pointless.
extern "C" U3X_FN void u3x_join_party(uintptr_t base, void *s, uintptr_t callback) {
    using namespace g21a;
    const uint8_t *target = reinterpret_cast<const uint8_t *>(base + SELECTED);
    uint8_t presence[0x190];
    zero(presence, sizeof presence);
    if (u3h_presence(base, target, presence) != 0) {   // no presence: ask without a party id
        u3x_send_join(base, target, 0, callback);
        return;
    }
    const uint8_t *data = presence + 0x100;   // the presence data, taken as it is
    if (data[7]) {                            // the friend's party is locked
        u3h_show_dialog(base, s, 0x4d972420u, 0xc6467f2du, 0x52a9f855u, 0, 0, 0);
        u3h_fj_note(t_join, target, t_locked, -1);
        return;
    }
    const uint64_t party = rd<uint64_t>(data, 0x28);
    const uint8_t *ses = reinterpret_cast<const uint8_t *>(base + PARTY);
    if (party && rd<uint8_t>(ses, SES_IN_PARTY) && rd<uint64_t>(ses, SES_ROOM) == party) return;   // already there
    u3x_send_join(base, target, party, callback);
}

// The received messages of this tick: join requests and the helper's own join messages are handled here, the rest
// goes to the game's message queue.
extern "C" U3X_FN void u3x_message_events(uintptr_t base) {
    using namespace g21a;
    for (int taken = 0; taken < 64; ++taken) {
        uint8_t id[ID_SIZE];
        uint8_t data[0x200];
        uint32_t type = 0;
        zero(id, sizeof id);
        zero(data, sizeof data);
        int32_t n = u3h_msg_event(base, id, &type, data, MSG_MAX);
        if (n < 0) return;   // no more messages
        if (n > static_cast<int32_t>(MSG_MAX)) n = static_cast<int32_t>(MSG_MAX);
        if (type == MSG_JOIN_REQUEST && n >= 9) {
            if (u3h_join_host_answer(base, id, data, n)) continue;
        } else if (type == 0x1f0 || type == 0x1f1) {
            if (u3h_join_msg_in(base, id, type, data, n)) continue;
        }
        u3x_queue_post(base, id, type, data, static_cast<uint32_t>(n));
    }
}

// Holds a join request (or replaces the request of a sender that is already held).
extern "C" U3X_FN void u3x_join_hold(uintptr_t base, const uint8_t *id, const uint8_t *d) {
    using namespace g21a;
    uint32_t i = 0;
    while (i < held_count && !same(held[i].id, id, ID_SIZE)) ++i;
    if (i == held_count) {
        if (held_count == HOLD_MAX) return;   // full: dropped without an answer
        copy(held[i].id, id, ID_SIZE);
        held[i].since = at<int32_t>(base + CLOCK);
        ++held_count;
    }
    held[i].flag = d[8];
    copy(held[i].request, d, REQUEST_SIZE);
}

// Decides every held request: answer it, hand it to the game, or let it wait.
extern "C" U3X_FN void u3x_join_hold_tick(uintptr_t base) {
    using namespace g21a;
    uint8_t *ses = reinterpret_cast<uint8_t *>(base + PARTY);
    const uint32_t state = rd<uint32_t>(ses, SES_STATE);
    const int32_t now = at<int32_t>(base + CLOCK);
    uint32_t i = 0;
    while (i < held_count) {
        const Held &e = held[i];
        const bool helper = e.request[9] == 0x4a;      // sent by this helper
        const bool fresh = helper && e.request[10] == 2;   // "make a fresh party"
        const int32_t waited = static_cast<int32_t>(static_cast<uint32_t>(now) - static_cast<uint32_t>(e.since));

        Verdict verdict;
        if (u3h_join_locked(base)) verdict = Verdict::refuse_locked;
        else if (fresh && state == 6 && waited <= FIVE_SECONDS) verdict = Verdict::wait;
        else if (u3h_join_can_accept(ses, state)) verdict = Verdict::accept;
        else if (state == 0) verdict = Verdict::to_game;
        else if (waited > FIVE_SECONDS) verdict = Verdict::refuse_late;
        else verdict = Verdict::wait;

        if (verdict == Verdict::wait) {
            ++i;
            continue;
        }
        if (verdict == Verdict::accept) {
            if (helper && u3h_join_party_reply(base) != 0) u3h_join_info_send(base, e.id);
            u3h_join_answer(base, e.id, u3h_join_party_reply(base), e.flag);
            u3h_fj_note(t_held, e.id, t_accepted, -1);
        } else if (verdict == Verdict::to_game) {
            *u3h_join_request_pc() = helper ? 1 : 0;
            u3x_queue_post(base, e.id, MSG_JOIN_REQUEST, e.request, REQUEST_SIZE);
            u3h_fj_note(t_held, e.id, t_to_game, -1);
        } else if (verdict == Verdict::refuse_locked) {
            u3h_join_answer(base, e.id, 0, e.flag);
            u3h_fj_note(t_held, e.id, t_refused_locked, -1);
        } else {
            u3h_join_answer(base, e.id, 0, e.flag);
            u3h_fj_note(t_held, e.id, t_refused_late, static_cast<int32_t>(state));
        }
        drop(i);   // the next entry is now at this place
    }
}

// Per tick: the held requests, then the 0x10c answer for the request the game created a party for.
extern "C" U3X_FN void u3x_join_reply_tick(uintptr_t base) {
    using namespace g21a;
    if (held_count) u3x_join_hold_tick(base);
    uint8_t *requester = u3h_join_request();
    uint8_t *serving = u3h_join_request_on();
    const uint8_t *ses = reinterpret_cast<const uint8_t *>(base + PARTY);
    if (rd<uint8_t>(ses, SES_SERVING)) {   // the game is creating the party: remember for whom
        copy(requester, ses + SES_REQUESTER, ID_SIZE);
        *serving = 1;
        return;
    }
    if (!*serving) return;
    *serving = 0;   // the game has finished that step
    if (rd<uint32_t>(ses, SES_STATE) != 6 || rd<uint64_t>(ses, SES_ROOM) == 0 || requester[0] == 0) return;
    uint8_t *runs_helper = u3h_join_request_pc();
    if (*runs_helper) {
        *runs_helper = 0;
        u3h_join_info_send(base, requester);
    }
    // 0x10 bytes: the party id as it is now (read again after the info message), the rest 0
    const uint64_t answer[2] = {rd<uint64_t>(ses, SES_ROOM), 0};
    u3x_queue_send(base, requester, MSG_JOIN_ANSWER, answer, REQUEST_SIZE);
    u3h_fj_note(t_request, requester, t_party_up, -1);
}
