// G40 part A: the respawn helpers (spec G40_respawn.md, sections 4.1 to 4.9), written from the description.

// ---- functions of the C part and of other groups (spec 2.2)
extern "C" int u3h_mp_code_is(uintptr_t base, uint32_t off, const char *hex12);
extern "C" uint32_t u3h_crc_sid(const char *p);
extern "C" uint8_t *u3x_sym_table(void *s, uint32_t id);

// ---- this part
extern "C" int64_t u3x_f_ticks(uint32_t f, int64_t half);
extern "C" void *u3x_dc_type_object(uintptr_t base, uint32_t key);
extern "C" int u3x_default_spawner_exists(uintptr_t base, void *s);
extern "C" int u3x_spawn_locator(uintptr_t base, void *s, uint8_t *p, void *loc, uint32_t spawner, uint32_t index);
extern "C" void u3x_spawn_fade(uintptr_t base, void *s, uint8_t *p, int on);
extern "C" void u3x_spawn_region_request(uintptr_t base, void *s, uint8_t *p, uint32_t spawner);
extern "C" int u3x_spawn_levels_ready(uintptr_t base, void *s, uint32_t spawner);
extern "C" uint8_t *u3x_netgame_cur(uintptr_t base, void *s);

namespace g40a {

// image offsets (spec 3.1, 3.2)
constexpr uintptr_t MANAGER = 0x17cb060;
constexpr uintptr_t STREAMER = 0x1afcf10;
constexpr uintptr_t NET_PROCESS = 0x1b3ee38;
constexpr uintptr_t TYPE_TABLE = 0x1b3ee70;
constexpr uintptr_t STRING_ID_FN = 0x1702720;
constexpr uintptr_t ARG_FLOAT = 0x17006d0;
constexpr uintptr_t ARG_INTEGER = 0x1700670;
constexpr uint32_t FN_SPAWNER_LEVEL = 0x72fb00;
constexpr uint32_t FN_SEND_EVENT = 0x1197380;

constexpr uint32_t TAG_DEFAULT_SPAWNER = 0x5bfae217u;
constexpr uint32_t TAG_NEEDED_LEVEL = 0x38a0841fu;

// the helper's own guard values: the first 12 bytes of the two game functions called directly
U3X_RO const char code_spawner_level[] = {'5','5','4','8','8','9','e','5','5','3','5','0','4','8','8','9','f','b','b','f','7','d','0','6',0};
U3X_RO const char code_send_event[] = {'5','5','4','8','8','9','e','5','4','1','5','7','4','1','5','6','4','1','5','5','4','1','5','4',0};

using cptr = const uint8_t *;

[[gnu::always_inline]] inline cptr ptr_at(const void *p, uintptr_t off) { return rd<cptr>(p, off); }

// native k of the state object
template <typename R, typename... A> [[gnu::always_inline]] inline R native(void *s, int k, A... args) {
    return game_call<R>(rd<uintptr_t>(s, 768 + 8 * static_cast<uintptr_t>(k)), args...);
}

[[gnu::always_inline]] inline uint32_t *words(void *s, uintptr_t off) {
    return reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(s) + off);
}
[[gnu::always_inline]] inline uint32_t *rs(void *s) { return words(s, 0x3d40); }
[[gnu::always_inline]] inline uint32_t *rs2(void *s) { return words(s, 0x3d78); }

[[gnu::always_inline]] inline bool text_empty(const char *t) { return !t || !*t; }

// tag table entries: 16 bytes each, tag id at +0, value pointer at +8
[[gnu::always_inline]] inline uint32_t tag_id(cptr entries, uint64_t i) { return rd<uint32_t>(entries, i * 16); }
[[gnu::always_inline]] inline cptr tag_value(cptr entries, uint64_t i) { return ptr_at(entries, i * 16 + 8); }

// spec 4.3: lowest index whose tag id is not below `tag` (lower bound), or `count`
U3X_FN uint32_t tag_search(cptr entries, uint32_t count, uint32_t tag) {
    uint32_t first = 0, left = count;
    while (left) {
        const uint32_t half = left / 2;
        if (tag_id(entries, first + half) < tag) {
            first += half + 1;
            left -= half + 1;
        } else {
            left = half;
        }
    }
    return first;
}

// spec 3.4: the object of a 16-byte handle, or null
[[gnu::always_inline]] inline void *handle_object(cptr handle) {
    const cptr slot = ptr_at(handle, 0);
    const uint32_t id = rd<uint32_t>(handle, 8);
    if (!slot || !id) return nullptr;
    const cptr obj = ptr_at(slot, 0);
    if (!obj || rd<uint32_t>(obj, 0x7c) != id) return nullptr;
    return const_cast<uint8_t *>(obj);
}

// is this spawner marked as a default one (first entry of the tag only)
[[gnu::always_inline]] inline bool marked_default(cptr spawner) {
    const cptr table = ptr_at(spawner, 0x68);
    if (!table) return false;
    const cptr entries = ptr_at(table, 0);
    if (!entries) return false;
    const uint32_t count = rd<uint32_t>(table, 8);
    const uint32_t i = tag_search(entries, count, TAG_DEFAULT_SPAWNER);
    if (i >= count || tag_id(entries, i) != TAG_DEFAULT_SPAWNER) return false;
    const cptr value = tag_value(entries, i);
    if (!value || rd<uint16_t>(value, 0) != 1) return false;
    const cptr flag = ptr_at(value, 4);
    return flag && rd<uint32_t>(flag, 0) != 0;
}

struct PendingLevels {   // native 59's list block, 264 bytes
    uint32_t count;
    uint32_t unused;
    const char *name[32];
};

struct LevelRequest {   // native 60's request block, 0x20 bytes
    const void *position;
    uint64_t type_id;
    uint32_t word;
    uint32_t game_id;
    uint8_t zero18;
    uint8_t streamer_flag;
    uint8_t zero1a[6];
};

}  // namespace g40a

// spec 4.1: a delay in seconds (float pattern) to ticks, minus half * 150; integer arithmetic only
extern "C" U3X_FN int64_t u3x_f_ticks(uint32_t f, int64_t half) {
    const int64_t t = half * 150;
    const uint32_t exponent = (f >> 23) & 0xff;
    const uint32_t mantissa = f & 0x7fffff;
    if (!exponent && !mantissa) return -t;   // +0.0 and -0.0

    // the magnitude in 1/65536 tick
    const int64_t a = static_cast<int64_t>(mantissa + 0x800000u) * 300;
    const int k = static_cast<int>(exponent) - 134;
    int64_t q;
    if (k > 30) q = int64_t{1} << 46;
    else if (k >= 0) q = a << k;
    else if (k > -33) q = a >> -k;
    else q = 0;

    const int64_t v = ((f >> 31) ? -q : q) - t * 65536;
    return v / 65536;   // cut toward zero
}

// spec 4.2: the game's type object of an id
extern "C" U3X_FN void *u3x_dc_type_object(uintptr_t base, uint32_t key) {
    using namespace g40a;
    const cptr table = reinterpret_cast<cptr>(base + TYPE_TABLE);
    const cptr buckets = ptr_at(table, 0);
    const int32_t n = rd<int32_t>(table, 0x3c);
    if (!buckets || n <= 0) return nullptr;

    uint32_t h = 0x811c9dc5u;   // FNV-1a over the key's bytes, low byte first
    for (int i = 0; i < 4; i++) h = (h ^ ((key >> (8 * i)) & 0xff)) * 0x01000193u;

    for (cptr node = ptr_at(buckets, 8 * static_cast<uintptr_t>(h % static_cast<uint32_t>(n))); node; node = ptr_at(node, 0)) {
        if (rd<uint32_t>(node, 0x10) != key) continue;
        if (node == ptr_at(buckets, 8 * static_cast<uintptr_t>(n))) return nullptr;   // the table's end node
        return rd<void *>(node, 0x18);
    }
    return nullptr;
}

// spec 4.4: does the match's level have a usable default spawner
extern "C" U3X_FN int u3x_default_spawner_exists(uintptr_t base, void *s) {
    using namespace g40a;
    if (!u3h_mp_code_is(base, FN_SPAWNER_LEVEL, code_spawner_level)) return 1;
    void *const manager = reinterpret_cast<void *>(base + MANAGER);
    const cptr level = game_call<cptr>(base + FN_SPAWNER_LEVEL, manager);
    if (!level) return 0;
    const cptr list = ptr_at(level, 0x190);
    if (!list) return 0;
    const int32_t count = rd<int32_t>(list, 0x10);
    const cptr spawners = ptr_at(list, 8);

    const uintptr_t filter = reinterpret_cast<uintptr_t>(s) - 0xfce0;
    if (at<uint32_t>(filter) != 0x68468b48u) return 1;   // 48 8B 46 68: the builder's filter stub

    if (!spawners) return 0;
    for (int32_t i = 0; i < count && i < 4096; i++) {
        const cptr spawner = ptr_at(spawners, 8 * static_cast<uintptr_t>(i));
        if (!spawner) continue;
        if (!game_call<uint8_t>(filter, manager, spawner)) continue;
        if (marked_default(spawner)) return 1;
    }
    return 0;
}

// spec 4.5: the locator of a spawn point; refused when neither the spawner nor a default one exists
extern "C" U3X_FN int u3x_spawn_locator(uintptr_t base, void *s, uint8_t *p, void *loc, uint32_t spawner, uint32_t index) {
    using namespace g40a;
    if (!rd<uint8_t>(p, 0x3b8)) {
        void *const streamer = reinterpret_cast<void *>(base + STREAMER);
        if (!native<void *>(s, 58, streamer, spawner) && !u3x_default_spawner_exists(base, s)) {
            rs2(s)[7] += 1;
            return 0;
        }
    }
    return native<uint8_t>(s, 54, p, loc, spawner, index) ? 1 : 0;
}

// spec 4.6: send the fade level of the player's screen to the net game process
extern "C" U3X_FN void u3x_spawn_fade(uintptr_t base, void *s, uint8_t *p, int on) {
    using namespace g40a;
    if (rs2(s)[0] & 2) return;
    if (!u3h_mp_code_is(base, FN_SEND_EVENT, code_send_event)) return;

    const uint32_t level = (on || at<int32_t>(base + MANAGER + 0x320) > 0) ? F_ONE : 0;
    void *const process = handle_object(reinterpret_cast<cptr>(base + NET_PROCESS));
    const int32_t screen = rd<int32_t>(p, 0x1e8);

    uint64_t arg[4][3];   // four argument records, consecutive
    alignas(16) uint64_t result[8];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 3; j++) arg[i][j] = 0;
    for (int i = 0; i < 8; i++) result[i] = 0;
    arg[0][0] = level;
    arg[0][2] = base + ARG_FLOAT;
    arg[1][0] = static_cast<uint64_t>(static_cast<int64_t>(screen));
    arg[1][2] = base + ARG_INTEGER;

    game_call<void>(base + FN_SEND_EVENT, static_cast<void *>(result), static_cast<void *>(nullptr), uint32_t{0xa1a13050u}, process,
                    static_cast<void *>(arg[0]), static_cast<void *>(arg[1]), static_cast<void *>(arg[2]), static_cast<void *>(arg[3]));

    rs2(s)[3] += 1;
    rs2(s)[4] = (static_cast<uint32_t>(screen) << 31) | (level >> 1);
}

// spec 4.7: want-load the levels around the spawn point and the map's own level
extern "C" U3X_FN void u3x_spawn_region_request(uintptr_t base, void *s, uint8_t *p, uint32_t spawner) {
    using namespace g40a;
    const cptr game = u3x_netgame_cur(base, s);

    alignas(16) uint64_t locator[8];
    for (int i = 0; i < 8; i++) locator[i] = 0;
    if (!u3x_spawn_locator(base, s, p, locator, spawner, 0)) return;

    alignas(16) uint64_t position[2];
    position[0] = locator[0];
    position[1] = locator[1];

    void *const streamer = reinterpret_cast<void *>(base + STREAMER);
    void *const type = u3x_dc_type_object(base, 0x6e51b48fu);
    const uint64_t type_id = native<uint64_t>(s, 62, type, uint32_t{0x9f2aeafbu}, uint32_t{0xd3b86d60u});

    alignas(16) LevelRequest request;
    request.position = position;
    request.type_id = type_id;
    request.word = 0x2743c4b0u;
    request.game_id = game ? rd<uint32_t>(game, 0) : 0;
    request.zero18 = 0;
    request.streamer_flag = rd<uint8_t>(streamer, 0x8a10);
    for (int i = 0; i < 6; i++) request.zero1a[i] = 0;
    native<void>(s, 60, streamer, static_cast<void *>(&request));
    rs(s)[7] += 1;

    // the map's own level
    if (!game) return;
    const cptr maps = native<cptr>(s, 13, uint32_t{0x0dec97a4u});
    if (!maps) return;
    const int32_t count = rd<int32_t>(maps, 0);
    const cptr entries = ptr_at(maps, 8);
    if (!entries) return;
    const uint32_t map_id = rd<uint32_t>(game, 4);
    for (int32_t i = 0; i < count; i++) {
        const cptr entry = entries + 0x98 * static_cast<uintptr_t>(i);
        if (rd<uint32_t>(entry, 0) != map_id) continue;
        const char *const name = rd<const char *>(entry, 0x70);
        if (text_empty(name)) return;
        const cptr level = native<cptr>(s, 42, streamer, u3h_crc_sid(name));
        if (level && rd<uint32_t>(level, 0x30) == 8) return;   // loaded
        native<void>(s, 61, streamer, name);
        return;
    }
}

// spec 4.8: no level pending, or a pending one is a level the spawner names
extern "C" U3X_FN int u3x_spawn_levels_ready(uintptr_t base, void *s, uint32_t spawner) {
    using namespace g40a;
    void *const streamer = reinterpret_cast<void *>(base + STREAMER);

    PendingLevels pending;
    pending.count = 0;
    pending.unused = 0;
    for (int i = 0; i < 32; i++) pending.name[i] = nullptr;
    native<void>(s, 59, streamer, static_cast<void *>(&pending));
    const uint32_t reported = pending.count;
    rs(s)[8] = reported;
    rs2(s)[5] = reported;

    // drop the failed levels, keep the others at the front
    // (the switch word is read again for each level found in the failed state, after its native 42 call)
    const uint32_t listed = reported < 32 ? reported : 32;
    uint32_t kept = 0, dropped = 0;
    for (uint32_t j = 0; j < listed; j++) {
        const char *const name = pending.name[j];
        const cptr level = text_empty(name) ? nullptr : native<cptr>(s, 42, streamer, u3h_crc_sid(name));
        if (level && rd<uint32_t>(level, 0x30) == 0xc && !(rd<uint32_t>(s, 0x20c0) & 0x20)) dropped++;
        else pending.name[kept++] = name;
    }
    const uint32_t waiting = kept + (reported - listed);
    rs2(s)[6] = dropped;

    const cptr point = native<cptr>(s, 58, streamer, spawner);
    if (!point || !waiting) return waiting ? 0 : 1;

    const cptr table = ptr_at(point, 0x68);
    if (!table) return 0;
    const cptr entries = ptr_at(table, 0);
    const uint32_t count = rd<uint32_t>(table, 8);
    if (!entries || !count) return 0;

    const uintptr_t string_id = at<uintptr_t>(base + STRING_ID_FN);
    const auto id_of = [string_id](const char *text) __attribute__((always_inline)) -> uint32_t {
        return text_empty(text) ? 0 : game_call<uint32_t>(string_id, int32_t{0}, text, int32_t{1});
    };
    const uint32_t first = tag_search(entries, count, TAG_NEEDED_LEVEL);

    const uint32_t compared = waiting < 32 ? waiting : 32;
    for (uint32_t k = 0; k < compared; k++) {
        const uint32_t wanted = id_of(pending.name[k]);
        for (uint32_t i = first; i < count && tag_id(entries, i) == TAG_NEEDED_LEVEL; i++) {
            const cptr value = tag_value(entries, i);
            uint32_t named = 0;
            if (value) {
                const int16_t type = rd<int16_t>(value, 0);
                // type 5: the pointer at +4 leads to a text pointer cell; the level name is the text it holds
                if (type == 5) named = id_of(rd<const char *>(ptr_at(value, 4), 0));
                else if (type == 0xd) named = rd<uint32_t>(ptr_at(value, 4), 0);
            }
            if (named == wanted) return 1;
        }
    }
    return 0;
}

// spec 4.9: the record of the net game the match uses
extern "C" U3X_FN uint8_t *u3x_netgame_cur(uintptr_t base, void *s) {
    using namespace g40a;
    uint8_t *const table = u3x_sym_table(s, 0x3b9a067du);
    const int32_t i = at<int16_t>(base + MANAGER + 0x661c);
    if (!table || i < 0 || i >= rd<int32_t>(table, 0)) return nullptr;
    uint8_t *const records = rd<uint8_t *>(table, 8);
    if (!records) return nullptr;
    return records + 0xd0 * static_cast<uintptr_t>(i);
}
