// G43 part B: the script native "net-apply-npc-fight-damage" (melee damage of a net player on a tracked NPC).
// Written from spec G43_coop_spectate.md, sections 3.1 to 3.3, 3.5, 3.6, 3.9, 4.5.

extern "C" {
uint8_t *u3h_ext(void);
uint32_t u3x_net_player_id(const uint8_t *np);

int64_t u3x_native_npc_fight(int32_t argc, uint64_t *argv, void *ctx);
}

namespace g43b {
namespace {

constexpr uintptr_t MANAGER = 0x17cb060;      // the net game manager
constexpr uintptr_t NET_CLOCK = 0x17b4ca4;
constexpr uintptr_t MP_ACTIVE = 0x17d5c61;
constexpr uintptr_t SESSION = 0x17d6420;      // the match session
constexpr uintptr_t NAME_DESC_1 = 0x16f0ae0;  // the reader's "argument is a name" descriptor, word 1
constexpr uintptr_t NAME_DESC_2 = 0x16f0ae8;  // word 2, also the reader's "step" switch
constexpr uintptr_t FN_READER_INIT = 0x11cc560;
constexpr uintptr_t FN_READER_NAME = 0x11cc9d0;
constexpr uintptr_t FN_HIT_ATTACKER = 0x55cb20;
constexpr uintptr_t FN_SEND_NPC_KILLED = 0x6ae7a0;

template <typename T> [[gnu::always_inline]] inline T &field(uint8_t *p, uintptr_t off) {
    return *reinterpret_cast<T *>(p + off);
}

// the object of a handle (pointer, then u32 id 8 bytes later), or null when the handle is not valid
U3X_FN uint8_t *handle_object(const uint8_t *handle) {
    const uint8_t *const *holder = rd<const uint8_t *const *>(handle, 0);
    const uint32_t id = rd<uint32_t>(handle, 8);
    if (!holder || !id) return nullptr;
    const uint8_t *object = *holder;
    if (!object || rd<uint32_t>(object, 0x7c) != id) return nullptr;
    return const_cast<uint8_t *>(object);
}

// the function in slot `slot` (a byte offset) of an object's function table
[[gnu::always_inline]] inline uintptr_t slot_of(const uint8_t *object, uintptr_t slot) {
    return rd<uintptr_t>(rd<const uint8_t *>(object, 0), slot);
}

// the game's script argument reader block (40 bytes)
struct Reader {
    uint32_t count;        // number of arguments
    uint32_t pad;
    const uint64_t *args;  // the argument values
    uint32_t next;         // position of the next argument
    uint32_t expected;
    void *ctx;
    uint64_t error;
};

// the next argument as a number
U3X_FN uint32_t take_number(Reader *r, uint64_t step) {
    if (r->next >= r->count) return 0;
    const uint32_t v = static_cast<uint32_t>(r->args[r->next]);
    if (step) r->next += 1;
    return v;
}

// 32-bit FNV-1a over the four bytes of a name id, lowest byte first
U3X_FN uint32_t name_hash(uint32_t name) {
    uint32_t h = 0x811c9dc5u;
    for (int i = 0; i < 4; i++) {
        h ^= (name >> (8 * i)) & 0xff;
        h *= 0x01000193u;
    }
    return h;
}

// the tracker of the NPC with that name, or null
U3X_FN uint8_t *find_tracker(uint8_t *manager, uint32_t name) {
    const int32_t buckets = rd<int32_t>(manager, 0x655c);
    uint8_t *const *array = rd<uint8_t *const *>(manager, 0x6520);
    if (buckets <= 0 || !array) return nullptr;
    uint8_t *node = array[name_hash(name) % static_cast<uint32_t>(buckets)];
    if (!node) return nullptr;                             // empty bucket
    uint8_t *const end = array[buckets];                   // the chain's end marker, read before the walk
    while (node && rd<uint32_t>(node, 0x10) != name) node = rd<uint8_t *>(node, 0);
    if (!node || node == end) return nullptr;              // not there, or the end marker
    return node + 0x18;
}

// the host notes who did how much damage: three attacker slots per tracker
U3X_FN void note_damage(uint8_t *tracker, uint32_t player, uint32_t damage) {
    for (int i = 0; i < 3; i++) {
        uint32_t &who = field<uint32_t>(tracker, 0x50 + 4 * i);
        uint32_t &sum = field<uint32_t>(tracker, 0x80 + 4 * i);
        if (who == player) {
            sum += damage;
            return;
        }
        if (who == 0) {
            who = player;
            sum = damage;
            return;
        }
    }
}

U3X_FN bool npc_dead(uint8_t *npc) { return game_call<uint8_t>(slot_of(npc, 0x3a8), npc) != 0; }

}  // namespace
}  // namespace g43b

// Script arguments: (net player id, NPC name, damage, attack name).
extern "C" U3X_FN int64_t u3x_native_npc_fight(int32_t argc, uint64_t *argv, void *ctx) {
    using namespace g43b;
    const uintptr_t base = rd<uint64_t>(u3h_ext(), 0x21b0);
    if (!base) return 0;
    if (!at<uint8_t>(base + MP_ACTIVE)) return 0;

    Reader reader;
    game_call<void>(base + FN_READER_INIT, &reader, argc, argv, ctx, int32_t{4});
    const uint32_t desc1 = at<uint32_t>(base + NAME_DESC_1);
    const uint64_t desc2 = at<uint64_t>(base + NAME_DESC_2);
    const uint32_t player = take_number(&reader, desc2);
    const uint32_t npc_name = game_call<uint32_t>(base + FN_READER_NAME, &reader, desc1, desc2);
    const uint32_t damage = take_number(&reader, desc2);
    const uint32_t attack = game_call<uint32_t>(base + FN_READER_NAME, &reader, desc1, desc2);

    // the attacker: the character of the first record with that id
    if (!player) return 0;
    uint8_t *manager = reinterpret_cast<uint8_t *>(base + MANAGER);
    uint8_t *rec = nullptr;
    for (int i = 0; i < 10 && !rec; i++) {
        uint8_t *r = manager + 0x390 + 0x9c0 * i;
        if (u3x_net_player_id(r) == player) rec = r;
    }
    if (!rec) return 0;
    uint8_t *attacker = handle_object(rec + 0x10);
    if (!attacker) return 0;

    uint8_t *tracker = find_tracker(manager, npc_name);
    if (!tracker) return 0;
    uint8_t *npc = handle_object(tracker);
    if (!npc) return 0;

    const uint8_t *session = reinterpret_cast<const uint8_t *>(base + SESSION);
    const bool host = rd<uint64_t>(session, 0x18) == 0 || rd<uint32_t>(session, 0x12e4) == rd<uint32_t>(session, 0x12e8);
    if (host) note_damage(tracker, player, damage);

    uint8_t *health_part = rd<uint8_t *>(npc, 0x310);
    if (!health_part) return 0;
    const int32_t health = game_call<int32_t>(slot_of(health_part, 0x78), health_part);
    // a guest applies only a hit of at least the remaining health
    if (!host && int64_t{damage} < int64_t{health}) return 0;

    const bool dead_before = npc_dead(npc);
    game_call<void>(slot_of(npc, 0x5e0), npc, damage);
    const bool dead_after = npc_dead(npc);
    ext_ea()[46] += 1;
    if (dead_before == dead_after || !host) return 0;

    // the host tells the others that this hit killed the NPC
    alignas(16) uint32_t hit[0x90 / 4];
    for (uint32_t &w : hit) w = 0;
    hit[0x10 / 4] = hit[0x14 / 4] = 0x7fccccccu;   // the game's "not set" pattern
    hit[0x4c / 4] = hit[0x6c / 4] = 0x3f800000u;   // 1.0
    game_call<void>(base + FN_HIT_ATTACKER, hit, attacker);

    int32_t now = at<int32_t>(base + NET_CLOCK);
    game_call<void>(base + FN_SEND_NPC_KILLED, attack, attacker, npc, &now, uint64_t{0}, uint64_t{0}, uint64_t{0},
                    uint64_t{0}, uint64_t{0}, hit);
    npc[0x70a] |= 1;   // kill sent
    ext_ea()[47] += 1;
    return 0;
}
