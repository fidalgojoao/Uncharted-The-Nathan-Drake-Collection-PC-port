// G58 part A: emblem character binds (written from spec G58_emblem_manager.md, sections 2.1, 3, 4.1 to 4.4).
// Chooses the emblem texture records of a character and writes their textures into its render tables.

extern "C" int u3h_emb_is_record(uint64_t p);

extern "C" uint8_t *u3x_emblem_team_player(uintptr_t b, int32_t team);
extern "C" uint8_t *u3x_emblem_object_player(uintptr_t b, const uint8_t *obj);
extern "C" int u3x_emblem_tag(uintptr_t b, const uint8_t *spawner, uint32_t sid);
extern "C" int u3x_emblem_look(uintptr_t b, const uint64_t *regs);

namespace g58a {

// the three single ids of spec 3.9
constexpr uint32_t CLASS_LOCAL_PLAYER = 0xa5a63e9bu;
constexpr uint32_t CLASS_NET_CHARACTER = 0xf2c86c22u;
constexpr uint32_t TAG_EDITOR_BOARD = 0x32f2d823u;

// image offsets (spec 3.1, 3.8)
constexpr uintptr_t EM = 0x17c3440;               // emblem manager
constexpr uintptr_t NET_MANAGER = 0x17cb060;
constexpr uintptr_t NET_PLAYERS = 0x17cb3f0;      // 10 records of 0x9c0 bytes
constexpr uintptr_t NET_STATE = 0x17d1868;        // s32, 2 = match running
constexpr uintptr_t MP_ACTIVE = 0x17d5c61;        // u8
constexpr uintptr_t MEMBER_COUNT = 0x17dd780;     // u32
constexpr uintptr_t MEMBER_IDS = 0x17e2864;       // u32, step 0xa4
constexpr uintptr_t MEMBER_STATE = 0x17e384c;     // s16, step 0x130
constexpr uintptr_t DEFAULT_STATE = 0x1b7f114;    // s16
constexpr uintptr_t TAG_TABLE = 0x1b3efd8;
constexpr uintptr_t SCENE_SLOT = 0x17bcf10;       // pointer
constexpr uintptr_t FN_LOCAL_TEAM = 0x737eb0;
constexpr uintptr_t FN_MESH_INFO = 0xe48ea0;

constexpr uint32_t NET_PLAYER_COUNT = 10;
constexpr uintptr_t NET_PLAYER_SIZE = 0x9c0;

[[gnu::always_inline]] inline uint8_t *net_player(uintptr_t b, uint32_t i) {
    return reinterpret_cast<uint8_t *>(b + NET_PLAYERS + NET_PLAYER_SIZE * i);
}

// in use and not leaving (spec 3.5)
[[gnu::always_inline]] inline bool player_valid(const uint8_t *np) {
    return rd<uint8_t>(np, 0xe1) != 0 && rd<uint8_t>(np, 0x5e4) == 0;
}

// net player id (spec 2.3)
[[gnu::always_inline]] inline uint32_t player_id(const uint8_t *np) {
    uint32_t low = rd<uint32_t>(np, 0x1e4) & 0xfffu;
    if (rd<uint8_t>(np, 0x1ec) != 0 && low != 0) low += 0x800u;
    return low + 0x1000u * rd<uint32_t>(np, 0x1e8);
}

// "this player's emblem represents its team" word: the member record's, or the game's default
[[gnu::always_inline]] inline int16_t team_state_word(uintptr_t b, const uint8_t *np) {
    const uint32_t count = at<uint32_t>(b + MEMBER_COUNT);
    if (count != 0) {
        const uint32_t id = player_id(np);
        for (uint32_t j = 0; j < count; ++j)
            if (at<uint32_t>(b + MEMBER_IDS + 0xa4 * static_cast<uintptr_t>(j)) == id)
                return at<int16_t>(b + MEMBER_STATE + 0x130 * static_cast<uintptr_t>(j));
    }
    return at<int16_t>(b + DEFAULT_STATE);
}

[[gnu::always_inline]] inline uint32_t *diag() {   // the ec[] words
    return reinterpret_cast<uint32_t *>(u3h_ext() + 0x4450);
}

// diagnostic only: a word of the first ready instance's mesh render info (spec 4.4 step 5)
[[gnu::always_inline]] inline void probe_mesh(uintptr_t b, const uint8_t *obj, uint32_t *ec) {
    const uint8_t *draw = rd<const uint8_t *>(obj, 0x130);
    if (!draw || rd<uint32_t>(draw, 0x18) == 0) return;
    const uint8_t *inst = rd<const uint8_t *>(draw, 0x10);
    if (!inst || rd<uint8_t>(inst, 0x60) == 0) return;
    const uint8_t *mesh = rd<const uint8_t *>(inst, 0);
    if (!mesh) return;
    const uint8_t *info = game_call<const uint8_t *>(b + FN_MESH_INFO, mesh);
    if (info) ec[144] = rd<uint32_t>(info, 0xd4);
}

[[gnu::always_inline]] inline int32_t table_count(const uint8_t *obj) {   // the virtual 0x2c0 of the object
    const uint8_t *vtable = rd<const uint8_t *>(obj, 0);
    return game_call<int32_t>(rd<uintptr_t>(vtable, 0x2c0), obj);
}

}  // namespace g58a

// The first valid player of the team whose emblem stands for the team, or null.
extern "C" U3X_FN uint8_t *u3x_emblem_team_player(uintptr_t b, int32_t team) {
    using namespace g58a;
    for (uint32_t i = 0; i < NET_PLAYER_COUNT; ++i) {
        uint8_t *np = net_player(b, i);
        if (!player_valid(np) || rd<int32_t>(np, 0x21c) != team) continue;
        if (team_state_word(b, np) == 1) return np;
    }
    return nullptr;
}

// The net player record behind a character object, or null.
extern "C" U3X_FN uint8_t *u3x_emblem_object_player(uintptr_t b, const uint8_t *obj) {
    using namespace g58a;
    if (!obj || at<uint8_t>(b + MP_ACTIVE) == 0) return nullptr;
    const uint32_t cls = rd<uint32_t>(obj, 0x30);
    if (cls == CLASS_LOCAL_PLAYER) {
        const int64_t screen = rd<int32_t>(obj, 0x1c68);
        return at<uint8_t *>(b + NET_MANAGER + 0x370 + static_cast<uintptr_t>(screen * 8));
    }
    if (cls == CLASS_NET_CHARACTER) {
        const uint32_t id = rd<uint32_t>(obj, 0x3f8);
        if (id == 0) return nullptr;
        for (uint32_t i = 0; i < NET_PLAYER_COUNT; ++i) {
            uint8_t *np = net_player(b, i);
            if (player_id(np) == id) return np;
        }
    }
    return nullptr;
}

// 1 when the spawner carries the tag with this id.
extern "C" U3X_FN int u3x_emblem_tag(uintptr_t b, const uint8_t *spawner, uint32_t sid) {
    using namespace g58a;
    const uint8_t *table = reinterpret_cast<const uint8_t *>(b + TAG_TABLE);
    if (rd<uint8_t>(table, 0) == 0 || !spawner) return 0;
    const uint8_t *bucket = table + 8 + 0x10 * static_cast<uintptr_t>(sid % 0x101u);
    for (const uint8_t *e = rd<const uint8_t *>(bucket, 0); e && e != bucket; e = rd<const uint8_t *>(e, 0)) {
        if (rd<uint32_t>(e, 0x10) != sid) continue;
        const uint32_t bit = rd<uint32_t>(e, 0x14);
        return static_cast<int>((rd<uint64_t>(spawner, 0x20 + 8 * static_cast<uintptr_t>(bit >> 6)) >> (bit & 63)) & 1);
    }
    return 0;
}

// Hook of the game's net character look: binds the emblem textures to the character's render tables.
extern "C" U3X_FN int u3x_emblem_look(uintptr_t b, const uint64_t *regs) {
    using namespace g58a;
    uint32_t *ec = diag();
    if (ec[0] & 0x4000u) return 0;   // emblems switched off
    const uint8_t *obj = reinterpret_cast<const uint8_t *>(regs[5]);
    if (!obj) return 1;
    {
        const uint32_t cls = rd<uint32_t>(obj, 0x30);
        if ((cls == 0x2db62a42u || cls == 0xb95beddbu) && at<int32_t>(b + NET_STATE) == 2) {
            // port (v7.217): in a match, a wall-emblem actor (NdSimpleObject). The wall walk (G57 part B) fills its
            // table with the PS3 choice (walls +0x7f0, boards +0x7e8). What PS3 does per filled instance texture
            // (f_854140: the object's instance-texture count +0x194 + 1 after the last table) is done here: the game set
            // obj+0x258 to 0 just before this call and copies it into every render instance's +0xd4 right after it; at 0
            // the overlay in the table is not drawn. Characters get theirs from the game's own tint step. In the menus
            // these classes take the editor pair below like before (the lobby banner is one; v7.216 left it blank).
            const uint8_t *t0 = rd<const uint8_t *>(obj, 0x240);
            if (t0 && rd<uint64_t>(t0, 8) != 0) *reinterpret_cast<uint32_t *>(const_cast<uint8_t *>(obj) + 0x258) += 1;
            return 1;
        }
    }

    // the records for tables 0..3
    const uint8_t *em = reinterpret_cast<const uint8_t *>(b + EM);
    const uint64_t fallback = rd<uint64_t>(em, 0x78);
    uint64_t rec[4] = {fallback, fallback, fallback, 0};
    if (at<int32_t>(b + NET_STATE) == 2) {
        // match: the object's own player, else the team's emblem player (always looked up), else the default
        const int32_t team = game_call<int32_t>(b + FN_LOCAL_TEAM, b + NET_MANAGER);
        const uint8_t *team_player = u3x_emblem_team_player(b, team);
        const uint8_t *player = u3x_emblem_object_player(b, obj);
        if (!player) player = team_player;
        if (player) {
            rec[1] = rd<uint64_t>(player, 0x7f0);
            rec[0] = rd<uint64_t>(player, 0x7e8);
        }
    } else {
        // menus: the editor's pair; only the board preview character gets the slot 0 record
        const int tag = u3x_emblem_tag(b, rd<const uint8_t *>(obj, 0x38), TAG_EDITOR_BOARD);
        rec[1] = rd<uint64_t>(em, 0x70);
        rec[0] = tag == 1 ? rd<uint64_t>(em, 0x68) : rec[1];
    }

    ec[133]++;
    ec[141] = rd<uint32_t>(obj, 0x30);
    ec[143] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(obj));
    if (at<int32_t>(b + NET_STATE) == 2) ec[135]++;
    probe_mesh(b, obj, ec);

    for (int32_t i = 0;; ++i) {
        const int32_t count = table_count(obj);   // asked again every round
        if (count <= i) break;
        ec[134] = static_cast<uint32_t>(count);
        uint64_t r = i < 4 ? rec[i] : 0;
        ec[145] = r == 0 ? 3u : (r == rd<uint64_t>(em, 0x68) || r == rd<uint64_t>(em, 0x70)) ? 2u : 1u;
        if (r == 0) {
            r = rd<uint64_t>(em, 0x78);
            ec[142]++;
        }
        if (r == 0 || !u3h_emb_is_record(r)) continue;
        uint8_t *table = rd<uint8_t *>(obj, 0x240 + 8 * static_cast<uintptr_t>(i & 7));
        if (!table) {
            ec[136]++;
            continue;
        }
        if (i > 7) continue;   // only the first eight tables are written
        const uint64_t tex = at<uint64_t>(static_cast<uintptr_t>(r));
        if (tex == 0) continue;
        if (rd<uint64_t>(table, 8) != 0) ec[140]++;
        *reinterpret_cast<uint64_t *>(table + 8) = tex;
        ec[119]++;
        ec[138] = static_cast<uint32_t>(tex);
        ec[139] = static_cast<uint32_t>(tex >> 32);
        const uint8_t *scene = at<const uint8_t *>(b + SCENE_SLOT);
        if (scene && rd<const uint8_t *>(scene, 0) == obj) ec[137]++;
    }
    ec[118]++;
    return 1;
}
