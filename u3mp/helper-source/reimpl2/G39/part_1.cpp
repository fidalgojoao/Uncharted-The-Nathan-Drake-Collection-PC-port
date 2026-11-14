// G39 part 1: the level manager's want lists (spec G39_mp_startup.md, sections 4.1 to 4.5).
// Written from the behaviour description only.

extern "C" {
// other groups and the C part (spec 2.4)
uint32_t u3h_crc_sid(const char *text);
int u3h_stage_unwant(uintptr_t base, uint32_t sid);
uint32_t u3x_level_state(uintptr_t base, void *s, uint32_t sid);

// this part
uint8_t *u3x_level_want_entry(uintptr_t base, uint32_t sid);
int u3x_actor_want(uintptr_t base, void *s, const char *name);
void u3x_title_unwant(uintptr_t base, void *s);
void u3x_net_init_levels(uintptr_t base, void *s);
void u3x_net_shutdown_levels(uintptr_t base, void *s);
}

namespace g39p1 {

// image offsets (spec 3.1, 3.7)
constexpr uintptr_t LEVEL_MANAGER = 0x1afcf10;
constexpr uintptr_t TITLE_NAME_1 = 0x145ba00;   // the game's own texts of the two title levels
constexpr uintptr_t TITLE_NAME_2 = 0x145ba0d;

// level manager fields (spec 3.2)
constexpr uintptr_t LEVELS_COUNT = 0x28, LEVELS_PTR = 0x30;
constexpr uintptr_t ACTORS_COUNT = 0x38, ACTORS_CAPACITY = 0x3c, ACTORS_PTR = 0x40;

constexpr uint32_t LIST_MAX = 4096;
constexpr uintptr_t ENTRY_SIZE = 0x18;

// script facts (spec 3.3, 3.7)
constexpr uint32_t BOOT_LIST_SYMBOL = 0x1020810b;
constexpr uint32_t SP_PROTO_SID = 0xe0fdf8f6;
constexpr int32_t BOOT_LIST_MAX = 256;    // largest list accepted
constexpr int32_t BOOT_LIST_USED = 32;    // names handled

// natives of the state object (spec 3.5)
constexpr int NATIVE_SYMBOL = 13, NATIVE_HASH = 40, NATIVE_REQUEST = 41;

using SymbolFn = const uint8_t *(*)(uint32_t symbol);
using HashFn = uint64_t (*)(const char *name);
using RequestFn = void (*)(void *manager, void *request);

// an entry of a want list / a request block (spec 3.2)
struct Want {
    uint64_t hash;
    uint32_t sid;
    uint16_t number;
    uint8_t flags;
    uint8_t zero_f;
    uint64_t zero_10;
};

[[gnu::always_inline]] inline uint8_t *manager(uintptr_t base) { return reinterpret_cast<uint8_t *>(base + LEVEL_MANAGER); }

template <typename F> [[gnu::always_inline]] inline F native(void *s, int k) {
    return rd<F>(s, 768 + 8 * static_cast<uintptr_t>(k));
}

[[gnu::always_inline]] inline uint32_t *lob() { return reinterpret_cast<uint32_t *>(u3h_ext() + 0x1ff0); }
[[gnu::always_inline]] inline uint32_t *lob2() { return reinterpret_cast<uint32_t *>(u3h_ext() + 0x2030); }

// a name pointer of the boot list: any address in the user range, no alignment asked
[[gnu::always_inline]] inline bool name_ok(const char *p) {
    const uintptr_t v = reinterpret_cast<uintptr_t>(p);
    return v >= 0x10000u && v < (1ull << 47);
}

// One want list searched for a sid: -1 = the list cannot be read, 1 = found, 0 = not in it.
U3X_FN int list_has(const uint8_t *mgr, uintptr_t count_at, uintptr_t ptr_at, uint32_t sid) {
    const uint32_t count = rd<uint32_t>(mgr, count_at);
    const uint8_t *entries = rd<const uint8_t *>(mgr, ptr_at);
    if (!ptr_ok(entries) || count > LIST_MAX) return -1;
    for (uint32_t i = 0; i < count; i++)
        if (rd<uint32_t>(entries, i * ENTRY_SIZE + 8) == sid) return 1;
    return 0;
}

// The boot level list as the script holds it now, asked for through native 13 (may be null): its address when it
// is valid, else null.
U3X_FN const uint8_t *boot_list(SymbolFn symbol) {
    const uint8_t *list = symbol ? symbol(BOOT_LIST_SYMBOL) : nullptr;
    if (!ptr_ok(list)) return nullptr;
    const int32_t count = rd<int32_t>(list, 0);
    return count >= 0 && count <= BOOT_LIST_MAX ? list : nullptr;
}

// Name of record i of the boot list's records, or null when its pointer is not usable.
[[gnu::always_inline]] inline const char *boot_name(const uint8_t *records, int32_t i) {
    const char *name = rd<const char *>(records, static_cast<uintptr_t>(i) * 0x10);
    return name_ok(name) ? name : nullptr;
}

// One title level: out of the want lists when the level want list has it.
U3X_FN uint32_t title_level_out(uintptr_t base, uintptr_t name_at) {
    const uint32_t sid = u3h_crc_sid(reinterpret_cast<const char *>(base + name_at));
    if (u3x_level_want_entry(base, sid)) lob()[1] += static_cast<uint32_t>(u3h_stage_unwant(base, sid));
    return sid;
}

}  // namespace g39p1

// 4.1: the first entry of the level want list with this sid.
extern "C" U3X_FN uint8_t *u3x_level_want_entry(uintptr_t base, uint32_t sid) {
    using namespace g39p1;
    const uint8_t *mgr = manager(base);
    const uint32_t count = rd<uint32_t>(mgr, LEVELS_COUNT);
    uint8_t *entries = rd<uint8_t *>(mgr, LEVELS_PTR);
    if (!ptr_ok(entries) || count > LIST_MAX) return nullptr;
    for (uint32_t i = 0; i < count; i++, entries += ENTRY_SIZE)
        if (rd<uint32_t>(entries, 8) == sid) return entries;
    return nullptr;
}

// 4.2: wants an actor package by adding it to the actor want list. 1 = wanted already, 2 = added, 0 = cannot.
extern "C" U3X_FN int u3x_actor_want(uintptr_t base, void *s, const char *name) {
    using namespace g39p1;
    const HashFn hash = native<HashFn>(s, NATIVE_HASH);
    if (!hash) return 0;
    const uint32_t sid = u3h_crc_sid(name);
    uint8_t *mgr = manager(base);

    // both want lists must be readable, although only the actor want list is written
    int found = list_has(mgr, LEVELS_COUNT, LEVELS_PTR, sid);
    if (found) return found > 0;
    found = list_has(mgr, ACTORS_COUNT, ACTORS_PTR, sid);
    if (found) return found > 0;

    // the new entry goes behind the last one of the actor want list, when there is room
    uint32_t &actors = at<uint32_t>(reinterpret_cast<uintptr_t>(mgr) + ACTORS_COUNT);
    const uint32_t count = actors;
    const uint32_t capacity = rd<uint32_t>(mgr, ACTORS_CAPACITY);
    uint8_t *entries = rd<uint8_t *>(mgr, ACTORS_PTR);
    if (!ptr_ok(entries) || count >= capacity || capacity > LIST_MAX) return 0;

    Want *w = reinterpret_cast<Want *>(entries + count * ENTRY_SIZE);
    *w = Want{};
    w->hash = hash(name);
    w->sid = sid;
    w->flags = 1;   // an actor package, in the foreground
    actors = actors + 1;   // the count as it is after the hash call
    return 2;
}

// 4.3: takes the title scene's two levels out of the want lists and notes their states.
extern "C" U3X_FN void u3x_title_unwant(uintptr_t base, void *s) {
    using namespace g39p1;
    const uint32_t first = title_level_out(base, TITLE_NAME_1);
    const uint32_t second = title_level_out(base, TITLE_NAME_2);
    const uint32_t state_1 = u3x_level_state(base, s, first);
    const uint32_t state_2 = u3x_level_state(base, s, second);
    lob()[15] = state_1 + 256 * state_2;
}

// 4.4: the level step of NetInit: the single-player boot levels go, then the title scene.
extern "C" U3X_FN void u3x_net_init_levels(uintptr_t base, void *s) {
    using namespace g39p1;
    uint32_t *const l = lob();
    uint32_t *const l2 = lob2();
    l[0]++;

    const uint8_t *list = boot_list(native<SymbolFn>(s, NATIVE_SYMBOL));
    if (!list) {
        l[2] = 0xffffffffu;   // the words of an earlier run stay
    } else {
        l[2] = static_cast<uint32_t>(rd<int32_t>(list, 0));
        l[11] = 0;
        const uint8_t *records = rd<const uint8_t *>(list, 8);
        if (ptr_ok(records)) {
            // the game's list may change while its levels go: its count is read anew on every pass
            for (int32_t i = 0; i < rd<int32_t>(list, 0) && i < BOOT_LIST_USED; i++) {
                const char *name = boot_name(records, i);
                if (!name) continue;
                const uint32_t sid = u3h_crc_sid(name);
                if (i < 8) l[3 + i] = sid;
                if (u3x_level_want_entry(base, sid)) l[11] |= 1u << i;
                if (!(l2[2] & 1)) {
                    const uint32_t removed = static_cast<uint32_t>(u3h_stage_unwant(base, sid));
                    l2[0] += removed;
                    if (removed) l2[4] = 1;   // NetShutdown has levels to want again
                }
            }
        }
    }

    if (l2[2] & 2) l2[3] += static_cast<uint32_t>(u3h_stage_unwant(base, SP_PROTO_SID));
    u3x_title_unwant(base, s);
}

// 4.5: the level step of NetShutdown: wants the boot levels again that NetInit removed.
extern "C" U3X_FN void u3x_net_shutdown_levels(uintptr_t base, void *s) {
    using namespace g39p1;
    uint32_t *const l2 = lob2();
    if (!l2[4]) return;
    l2[4] = 0;   // cleared first: no second try when something is missing below

    // the three natives as they are before the list is asked for
    const SymbolFn symbol = native<SymbolFn>(s, NATIVE_SYMBOL);
    const HashFn hash = native<HashFn>(s, NATIVE_HASH);
    const RequestFn request = native<RequestFn>(s, NATIVE_REQUEST);
    const uint8_t *list = boot_list(symbol);   // asked for before the natives are tested
    if (!hash || !request || !list) return;

    const uint8_t *records = rd<const uint8_t *>(list, 8);
    if (!ptr_ok(records)) return;
    for (int32_t i = 0; i < rd<int32_t>(list, 0) && i < BOOT_LIST_USED; i++) {   // the count as it is on each pass
        const char *name = boot_name(records, i);
        if (!name) continue;
        const uint32_t sid = u3h_crc_sid(name);
        if (u3x_level_want_entry(base, sid)) continue;   // still wanted
        Want w{};
        w.hash = hash(name);
        w.sid = sid;
        w.number = 4;   // as the boot code sets it
        w.flags = 3;    // wanted and shown
        request(manager(base), &w);
        l2[1]++;
    }
}
