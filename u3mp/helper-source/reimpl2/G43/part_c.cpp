// G43 part C: Co-op Adventure story heroes (co-op slot of a net player, hero look of a slot from the net game
// table, repair of that table's looks, and the lobby placement / spawn that take the look).
// Written from spec G43_coop_spectate.md, sections 3.1, 3.2, 3.8, 3.9, 4.6 to 4.9.

extern "C" {
uint8_t *u3h_ext(void);
uint8_t *u3x_sym_table(void *s, uint32_t id);
int u3x_fix_hero_look(uint8_t *look);

void u3x_hero_fix_all(void *s, uint32_t *w11);
const uint8_t *u3x_hero_desc(uintptr_t base, void *s, int slot, int32_t *gi, uint32_t *w11);
int u3x_hero_slot(uintptr_t base, const uint8_t *pl);
int u3x_hero_kind(uintptr_t base, void *s, uint64_t *r, uint32_t *w09, uint32_t *w11);
}

namespace g43c {
namespace {

constexpr uintptr_t MANAGER = 0x17cb060;       // the net game manager
constexpr uintptr_t FORCED_SLOT = 0x17d7ae0;
constexpr uintptr_t FN_LOOK_LOADED = 0x996a60;
constexpr uint32_t NET_GAME_TABLE = 0x3b9a067du;   // script symbol of the net game table
constexpr int32_t COOP_ADVENTURE = 11;
constexpr uintptr_t RECORD_SIZE = 0xd0, LOOK_SIZE = 0x28, P_LOOK = 0x620;

// repairs one look of the table and notes it
U3X_FN void repair_look(uint8_t *look, uint32_t *w11) {
    if (!ptr_ok(look)) return;
    const uint32_t before = rd<uint32_t>(look, 0);
    if (u3x_fix_hero_look(look) == 1) {
        w11[3] = before;
        w11[0] += 1;
    }
}

U3X_FN bool same_look(const uint8_t *a, const uint8_t *b) {
    for (uintptr_t i = 0; i < LOOK_SIZE; i++)
        if (a[i] != b[i]) return false;
    return true;
}

U3X_FN void copy_look(uint8_t *to, const uint8_t *from) {
    for (uintptr_t i = 0; i < LOOK_SIZE; i++) to[i] = from[i];
}

// true when every art package the look needs is loaded
U3X_FN bool look_loaded(uintptr_t base, const uint8_t *look) {
    return game_call<uint8_t>(base + FN_LOOK_LOADED, static_cast<void *>(nullptr), look) != 0;
}

[[gnu::always_inline]] inline uint32_t summary(const uint8_t *look, int slot, int32_t game) {
    return look[0] + (static_cast<uint32_t>(slot) << 8) + ((static_cast<uint32_t>(game) & 0xffff) << 16);
}

// lobby placement: the look in the placement function's frame takes the slot's hero look
U3X_FN void lobby_placement(uintptr_t base, void *s, uint8_t *frame, uint32_t *w09, uint32_t *w11) {
    w09[0] += 1;
    if (!ptr_ok(frame)) return;
    const uint8_t *pl = at<const uint8_t *>(base + MANAGER + 0x370);
    if (!ptr_ok(pl)) {
        w09[6] += 1;
        return;
    }
    int32_t game = -1;
    const int slot = u3x_hero_slot(base, pl);
    const uint8_t *look = u3x_hero_desc(base, s, slot, &game, w11);
    if (!look) {
        w09[6] += 1;
        return;
    }
    w09[7] = pl[P_LOOK] + (uint32_t{look[0]} << 8) + 0x10000;
    if (!look_loaded(base, look)) {
        w11[1] += 1;
        return;
    }
    w11[2] += 1;
    copy_look(frame - 0x58, look);
    w09[1] += 1;
    w09[5] = summary(look, slot, game);
}

// spawn: the net player's look becomes the slot's hero look
U3X_FN void spawn(uintptr_t base, void *s, uint8_t *pl, uint32_t *w09, uint32_t *w11) {
    if (!ptr_ok(pl)) return;
    int32_t game = -1;
    const int slot = u3x_hero_slot(base, pl);
    const uint8_t *look = u3x_hero_desc(base, s, slot, &game, w11);
    if (!look) {
        w09[6] += 1;
        return;
    }
    w09[2] += 1;
    w09[7] = pl[P_LOOK] + (uint32_t{look[0]} << 8) + 0x20000;
    if (same_look(pl + P_LOOK, look)) {
        w09[3] += 1;
    } else if (!look_loaded(base, look)) {
        w09[6] += 0x10000;   // art not loaded: the player keeps its look
    } else {
        copy_look(pl + P_LOOK, look);
        w09[4] += 1;
    }
    w09[5] = summary(look, slot, game);
}

}  // namespace
}  // namespace g43c

// Repairs the looks of every Co-op Adventure record of the net game table.
extern "C" U3X_FN void u3x_hero_fix_all(void *s, uint32_t *w11) {
    using namespace g43c;
    const uint8_t *table = u3x_sym_table(s, NET_GAME_TABLE);
    if (!table) return;
    const int32_t count = rd<int32_t>(table, 0);
    const uint8_t *records = rd<const uint8_t *>(table, 8);
    if (!ptr_ok(records) || count < 1 || count > 1024) return;
    for (int32_t i = 0; i < count; i++) {
        const uint8_t *rec = records + RECORD_SIZE * i;
        if (rd<int32_t>(rec, 0xc) != COOP_ADVENTURE) continue;
        uint8_t *const *looks = rd<uint8_t *const *>(rec, 0x70);
        if (!ptr_ok(looks)) continue;
        for (int k = 0; k < 3; k++) repair_look(looks[k], w11);
    }
}

// The hero look of a co-op slot in the current net game's record, or null.
extern "C" U3X_FN const uint8_t *u3x_hero_desc(uintptr_t base, void *s, int slot, int32_t *gi, uint32_t *w11) {
    using namespace g43c;
    const uint8_t *table = u3x_sym_table(s, NET_GAME_TABLE);
    if (!table) return nullptr;
    const int32_t number = at<int16_t>(base + MANAGER + 0x661c);
    *gi = number;
    if (number < 0 || number >= rd<int32_t>(table, 0)) return nullptr;
    const uint8_t *records = rd<const uint8_t *>(table, 8);
    if (!records) return nullptr;
    const uint8_t *rec = records + RECORD_SIZE * number;
    if (rd<int32_t>(rec, 0xc) != COOP_ADVENTURE) return nullptr;
    uint8_t *const *looks = rd<uint8_t *const *>(rec, 0x70);
    if (!looks || slot < 0 || slot > 2) return nullptr;
    uint8_t *look = looks[slot];
    if (!look) return nullptr;
    repair_look(look, w11);
    look = looks[slot];   // read again after the repair
    return look;
}

// The co-op slot of a net player: 0, 1, 2 or -1.
extern "C" U3X_FN int u3x_hero_slot(uintptr_t base, const uint8_t *pl) {
    using namespace g43c;
    const int32_t given = rd<int32_t>(pl, 0x42c);
    if (given >= 0 && given <= 2) return given;
    const int32_t forced = at<int32_t>(base + FORCED_SLOT);
    if (forced >= 0 && forced <= 2) return forced;
    // its place among the records that are in the game
    int before = 0;
    for (int i = 0; i < 10; i++) {
        const uint8_t *rec = reinterpret_cast<const uint8_t *>(base + MANAGER) + 0x390 + 0x9c0 * i;
        if (!rec[0xe1] || rec[0x5e4]) continue;
        if (rec == pl) return before <= 2 ? before : -1;
        before++;
    }
    return 0;
}

// Kind 248: the value of `r` is the stub's argument (odd = lobby placement frame, even = net player record).
extern "C" U3X_FN int u3x_hero_kind(uintptr_t base, void *s, uint64_t *r, uint32_t *w09, uint32_t *w11) {
    using namespace g43c;
    if (ext_eb()[3] & 0x80000000u) return 0;
    u3x_hero_fix_all(s, w11);
    const uintptr_t arg = reinterpret_cast<uintptr_t>(r);
    if (arg & 1) lobby_placement(base, s, reinterpret_cast<uint8_t *>(arg & ~uintptr_t{1}), w09, w11);
    else spawn(base, s, reinterpret_cast<uint8_t *>(arg), w09, w11);
    return 0;
}
