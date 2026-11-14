// G40 part B: the respawn of a local net player, frame by frame (spec G40_respawn.md, 4.10 to 4.13).
// Written from the behaviour description only.

extern "C" {
// other groups, the C part and shared ones (spec 2.2)
int u3x_type_is_coop(uintptr_t base, int32_t type);
int32_t u3x_netgame_type(uintptr_t base, void *s);
int32_t u3h_netgame_type(uintptr_t base, void *s);
int u3h_np_has_effect(void *s, uint8_t *p, uint32_t h);
int u3h_np_alive(void *s, uint8_t *p);
uint32_t u3h_respawn_delay(void *s, uint8_t *p);
int64_t u3h_f_ticks(uint32_t f, int64_t half);
uint8_t *u3h_screen_player(uintptr_t base, uint8_t *p);
int u3h_net_late_joining(uintptr_t base);
int u3h_net_coop_mode(uintptr_t base, void *s);
void u3h_spawn_fade(uintptr_t base, void *s, uint8_t *p, int on);
int u3h_spawn_locator(uintptr_t base, void *s, uint8_t *p, void *loc, uint32_t spawner, uint32_t index);
void u3h_spawn_region_request(uintptr_t base, void *s, uint8_t *p, uint32_t spawner);
int u3h_spawn_levels_ready(uintptr_t base, void *s, uint32_t spawner);
int u3h_vis_oob_tick(uintptr_t base, void *s, uint8_t *p, uint8_t *pl, int32_t now, int test);
void u3h_camera_tick(uintptr_t base, void *s, uint8_t *p, int idx);
void u3x_replication(uintptr_t base, void *s, uint8_t *np, uint8_t *player);

// this part
int u3x_late_join_fade(uintptr_t base, void *s, const uint8_t *p);
void u3x_respawn_tick(uintptr_t base, void *s, uint8_t *p, int idx);
int u3x_net_late_joining(uintptr_t base);
int u3x_net_coop_mode(uintptr_t base, void *s);
}

namespace g40b {

// image data
constexpr uintptr_t MANAGER = 0x17cb060;
constexpr uintptr_t ROUND_START = MANAGER + 0x66e8;
constexpr uintptr_t NET_NOW = 0x17b4ca4;
constexpr uintptr_t NET_CLOCK_OBJECT = 0x17c3580;
constexpr uintptr_t MP_ACTIVE = 0x17d5c61;
constexpr uintptr_t MATCH_SESSION = 0x17d6420;
constexpr uintptr_t LATE_JOIN_STATUS = 0x1b69530;
constexpr uintptr_t SCREEN_HANDLES = 0x1bb9500;
constexpr uintptr_t HUD_OBJECTS = 0x1805fa0;
constexpr uintptr_t HUD_ALIVE_FN = 0x11b7d60;

// ids
constexpr uint32_t EFFECT_NO_RESPAWN = 0xc054e78b;
constexpr uint64_t DAMAGE_OUT_OF_BOUNDS = 0x11a89844;
constexpr int32_t TYPE_ELIMINATION = 10;
constexpr int32_t TYPE_NO_LEVEL_WAIT = 0xb;

// net player fields
constexpr uintptr_t P_CHARACTER = 0x10, P_RESPAWN_OFF = 0x60, P_NO_SPAWN = 0x61, P_SCREEN = 0x1e8, P_TEAM = 0x21c,
                    P_DEATH = 0x39c, P_SPAWNED = 0x3a0, P_REQUEST = 0x3a8, P_SPAWNER = 0x3b4, P_PENDING = 0x420,
                    P_SPAWN_INDEX = 0x424, P_LATE = 0x5f9, P_ROUND_SPAWNED = 0x5fa, P_STEP = 0x7c0, P_OOB_DUE = 0x848;

// the guard bytes of the HUD's "alive" function (spec 3.2)
U3X_RO const uint8_t hud_alive_code[12] = {0x09, 0x77, 0x08, 0x8b, 0x47, 0x38, 0x85, 0xc0, 0x74, 0x4e, 0x48, 0x8d};

template <typename T> [[gnu::always_inline]] inline T &field(void *o, uintptr_t off) {
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(o) + off);
}
[[gnu::always_inline]] inline uint32_t *words(void *s, uintptr_t off) {
    return reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(s) + off);
}
[[gnu::always_inline]] inline uintptr_t native(void *s, int k) { return rd<uintptr_t>(s, 768 + 8 * k); }

// slot `off` of object o: the function in o's table, called with o alone
template <typename R> [[gnu::always_inline]] inline R slot(uint8_t *o, uintptr_t off) {
    return game_call<R, uint8_t *>(rd<uintptr_t>(rd<const uint8_t *>(o, 0), off), o);
}

// the object of the 16-byte handle at h, or null when the handle is not valid (spec 3.4)
[[gnu::always_inline]] inline uint8_t *handle_object(const uint8_t *h) {
    const uint8_t *holder = rd<const uint8_t *>(h, 0);
    const uint32_t id = rd<uint32_t>(h, 8);
    if (!holder || !id) return nullptr;
    uint8_t *o = rd<uint8_t *>(holder, 0);
    return o && rd<uint32_t>(o, 0x7c) == id ? o : nullptr;
}

// step 1: tell the HUD of the player's screen that he is alive
U3X_FN void hud_alive(uintptr_t base, void *s, uint8_t *p) {
    if (field<uint32_t>(s, 0x20c0) & 1) return;
    if (!u3h_np_alive(s, p)) return;
    for (int i = 0; i < 12; i++)
        if (at<uint8_t>(base + HUD_ALIVE_FN + i) != hud_alive_code[i]) return;
    const int32_t n = rd<int32_t>(p, P_SCREEN);
    if (n < 0 || n >= 2) return;
    game_call<void, uintptr_t, uint32_t>(base + HUD_ALIVE_FN, base + HUD_OBJECTS + 0x270 * n, 0x100000u);
    field<uint32_t>(s, 0x2098) += 1;
}

// step 4 (c): locator, then region request / wait for the levels / spawn
U3X_FN void spawn_sequence(uintptr_t base, void *s, uint8_t *p, int32_t death, int32_t request) {
    uint32_t *rs = words(s, 0x3d40), *rs2 = words(s, 0x3d78);
    const uint32_t spawner = rd<uint32_t>(p, P_SPAWNER);
    alignas(16) uint64_t locator[8];
    for (int i = 0; i < 8; i++) locator[i] = 0;
    if (!u3h_spawn_locator(base, s, p, locator, spawner, rd<uint32_t>(p, P_SPAWN_INDEX))) {
        rs[9] += 1;
        return;
    }
    const int32_t step = rd<int32_t>(p, P_STEP);
    if (step == 0) {
        u3h_spawn_region_request(base, s, p, spawner);
        u3h_spawn_fade(base, s, p, 1);
        field<int32_t>(p, P_STEP) = 1;
    } else if (step == 1) {
        u3h_spawn_fade(base, s, p, 1);
        const bool no_wait = u3h_netgame_type(base, s) == TYPE_NO_LEVEL_WAIT && rd<uint8_t>(p, P_LATE) == 0;
        if (no_wait || u3h_spawn_levels_ready(base, s, spawner)) field<int32_t>(p, P_STEP) = 2;
    } else if (step == 2) {
        u3h_spawn_fade(base, s, p, 0);
        field<uint8_t>(p, P_PENDING) = 0;
        uint8_t &spawn_now = field<uint8_t>(s, 0x3d40);   // low byte of rs[0], read by the builder's stub
        spawn_now = 1;
        const uint32_t result = game_call<uint8_t, uint8_t *, uint32_t, int32_t>(native(s, 53), p, spawner, 1);
        spawn_now = 0;
        field<int32_t>(p, P_DEATH) = death;       // the spawn function overwrote both
        field<int32_t>(p, P_REQUEST) = request;
        field<uint8_t>(p, P_PENDING) = 0;
        field<int32_t>(p, P_STEP) = 0;
        field<int32_t>(p, P_SPAWNED) = at<int32_t>(base + NET_NOW);
        rs[4] += 1;
        rs[6] = result;
        rs2[8] = result;
        rs2[9] += 1;
    }
}

// step 4: the player has no character
U3X_FN void without_character(uintptr_t base, void *s, uint8_t *p, int32_t death, int32_t request, int32_t team) {
    if (u3h_net_late_joining(base) && request == 0 && (!u3h_net_coop_mode(base, s) || team != 1)) {
        u3h_spawn_fade(base, s, p, u3x_late_join_fade(base, s, p));
    } else if (request < death || rd<uint8_t>(p, P_NO_SPAWN) != 0 ||
               (request < 1 && u3h_net_coop_mode(base, s) && team == 1)) {
        u3h_spawn_fade(base, s, p, 0);
    } else {
        spawn_sequence(base, s, p, death, request);
    }
}

// step 6: the out-of-bounds kill of the current life
U3X_FN void out_of_bounds(uintptr_t base, void *s, uint8_t *p, int idx, int32_t now) {
    uint32_t *rs2 = words(s, 0x3d78), *life = words(s, 0x3dc0);
    uint8_t *who = u3h_screen_player(base, p);
    const uint32_t life_id = at<uint32_t>(base + SCREEN_HANDLES + 8 + 0x10 * static_cast<intptr_t>(rd<int32_t>(p, P_SCREEN)));
    int32_t &due = field<int32_t>(p, P_OOB_DUE);

    if (who && life[2 + idx] != life_id) {
        life[2 + idx] = life_id;
        life[idx] = static_cast<uint32_t>(now);
    }
    const int32_t age = static_cast<int32_t>(static_cast<uint32_t>(now) - life[idx]);

    if (rs2[0] & 4) {
        if (who && age > 4500) {
            if (due <= 0) due = static_cast<int32_t>(static_cast<uint32_t>(now) + 1500u);
        } else {
            due = 0;
        }
    } else if (who) {
        u3h_vis_oob_tick(base, s, p, who, now, (rs2[0] & 0x20) && age > 6000 ? 1 : 0);
    }

    if (who && due > 0 && due <= now && u3h_np_alive(s, p)) {
        uint8_t *health = rd<uint8_t *>(who, 0x598);
        if (health) {
            slot<void>(health, 0x78);
            uint8_t *damage = slot<uint8_t *>(health, 0x118);
            if (damage) {
                const uint64_t type = DAMAGE_OUT_OF_BOUNDS;
                __builtin_memcpy(damage + 0x14, &type, 8);
                slot<void>(who, 0x530);
                rs2[10] += 1;
                if (!(rs2[0] & 4)) field<uint32_t>(u3h_ext(), 0x2094) += 1;
            }
        }
        due = 0;
    }
}

// step 8: remove the body once the delay has passed
U3X_FN void remove_body(void *s, uint8_t *p, uint32_t delay, int32_t now, int32_t death, int32_t request) {
    const int64_t since = static_cast<int32_t>(static_cast<uint32_t>(now) - static_cast<uint32_t>(death));
    const bool positive = !(delay >> 31) && (delay & 0x7fffffffu) != 0 && (delay & 0x7f800000u) != 0x7f800000u;
    if (!positive) return;
    if (!(death < request || u3h_f_ticks(delay, 2) < since)) return;
    if (!(u3h_f_ticks(delay, 1) < since)) return;
    uint8_t *body = handle_object(p + P_CHARACTER);
    if (!body) return;
    game_call<void, uint8_t *>(native(s, 57), body);
    words(s, 0x3d40)[5] += 1;
}

}  // namespace g40b

// 4.10: the fade of a late joiner without a spawn: black, except in Elimination 5 s after the round's start
extern "C" U3X_FN int u3x_late_join_fade(uintptr_t base, void *s, const uint8_t *p) {
    uint32_t *ec = reinterpret_cast<uint32_t *>(u3h_ext() + 0x4450);
    if (ec[0] & 2) return 1;
    if (u3h_netgame_type(base, s) != g40b::TYPE_ELIMINATION) return 1;
    if (rd<uint8_t>(p, g40b::P_ROUND_SPAWNED) == 0) return 1;
    const uint8_t *clock = at<const uint8_t *>(base + g40b::NET_CLOCK_OBJECT);
    if (!clock) return 1;
    const int32_t passed = static_cast<int32_t>(rd<uint32_t>(clock, 0xc) - at<uint32_t>(base + g40b::ROUND_START));
    ec[17] += 1;
    return passed >= 1500 ? 0 : 1;
}

// 4.11: one frame of local net player p (local index idx)
extern "C" U3X_FN void u3x_respawn_tick(uintptr_t base, void *s, uint8_t *p, int idx) {
    using namespace g40b;
    uint32_t *rs = words(s, 0x3d40);

    hud_alive(base, s, p);

    if (u3h_np_has_effect(s, p, EFFECT_NO_RESPAWN)) return;
    if (rd<uint8_t>(p, P_RESPAWN_OFF) != 0) return;

    const int32_t now = at<int32_t>(base + NET_NOW);
    const uint32_t delay = u3h_respawn_delay(s, p);
    rs[10] = delay;
    rs[11] = static_cast<uint32_t>(now);
    const int32_t death = rd<int32_t>(p, P_DEATH);
    const int32_t request = rd<int32_t>(p, P_REQUEST);
    const int32_t team = rd<int32_t>(p, P_TEAM);

    if (!u3h_screen_player(base, p)) {
        rs[2 + idx] = 0x100u | rd<uint32_t>(p, P_STEP);
        without_character(base, s, p, death, request, team);
        u3h_camera_tick(base, s, p, idx);
        return;
    }
    rs[2 + idx] = 0x200u | rd<uint32_t>(p, P_STEP);

    out_of_bounds(base, s, p, idx, now);

    u3x_replication(base, s, p, u3h_screen_player(base, p));

    if (!u3h_np_alive(s, p)) {
        remove_body(s, p, delay, now, death, request);
    } else {
        u3h_spawn_fade(base, s, p, 0);
    }
    u3h_camera_tick(base, s, p, idx);
}

// 4.12: this machine joined a running match and is past the first late-join steps
extern "C" U3X_FN int u3x_net_late_joining(uintptr_t base) {
    const uintptr_t session = base + g40b::MATCH_SESSION;
    if (at<uint64_t>(session + 0x18) == 0) return 0;
    if (at<uint32_t>(session + 0x12e4) == at<uint32_t>(session + 0x12e8)) return 0;
    return at<int32_t>(base + g40b::LATE_JOIN_STATUS) > 3 ? 1 : 0;
}

// 4.13: multiplayer is active and the current net game is a co-op type
extern "C" U3X_FN int u3x_net_coop_mode(uintptr_t base, void *s) {
    if (at<uint8_t>(base + g40b::MP_ACTIVE) == 0) return 0;
    return u3x_type_is_coop(base, u3x_netgame_type(base, s));
}
