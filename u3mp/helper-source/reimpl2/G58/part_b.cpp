// G58 part B: emblem manager state machine (written from spec G58_emblem_manager.md, sections 2.2, 2.3, 3, 4.5 to 4.13).
// No float anywhere. Game memory is read at the point the spec names and stores are made in the spec's order.

// ---- other groups and the C part (spec 2.3)
extern "C" int u3h_emb_is_record(uint64_t p);
extern "C" uint8_t *u3h_emb_records(uint8_t **flag);
extern "C" void u3h_emb_free_record(uintptr_t b, uint64_t r);
extern "C" void *u3h_emb_ralloc(uint32_t size);
extern "C" int u3x_emb_texcreate(uintptr_t b, void *rec, const uint8_t *pix, uint32_t size);
extern "C" const uint8_t *u3h_emb_zero(void);
extern "C" void u3h_emb_bind(uintptr_t b, void *s);
extern "C" void u3h_emb_frame_parts(uintptr_t b, void *s, int which);
extern "C" uint8_t *u3h_emb_np(uintptr_t b, uint32_t i);
extern "C" void u3h_emb_new_records(uintptr_t b, uint32_t off, uint32_t size);
extern "C" void u3h_emb_set_record(uintptr_t b, uint8_t *np, int32_t k, uint64_t r);
extern "C" uint8_t *u3h_emb_profile(uintptr_t b, uint32_t user);
extern "C" uint64_t u3h_emb_alloc_default(uintptr_t b);

// ---- this part (spec 2.2)
extern "C" uint8_t *u3x_emb_np(uintptr_t b, uint32_t i);
extern "C" int u3x_emb_valid(const uint8_t *np);
extern "C" uint32_t u3x_emb_local(uintptr_t b);
extern "C" uint8_t *u3x_emb_prof(uintptr_t b, uint32_t u);
extern "C" void u3x_emb_setrec(uintptr_t b, uint8_t *np, int32_t k, uint64_t r);
extern "C" void u3x_emb_newrecs(uintptr_t b, uint32_t off, uint32_t size);
extern "C" void u3x_emblem_players(uintptr_t b, void *s);
extern "C" void u3x_emblem_editor(uintptr_t b, void *s);
extern "C" void u3x_emblem_update(uintptr_t b, void *s);
extern "C" void u3x_emblem_sweep(uintptr_t b);
extern "C" void u3x_emblem_tick(uintptr_t b, void *s);
extern "C" int u3x_emblem_netinit(uintptr_t b, void *s);
extern "C" uint64_t u3x_emblem_default_record(uintptr_t b);
extern "C" void u3x_emblem_shutdown(uintptr_t b);

namespace g58b {

// image offsets (spec 3.1)
constexpr uintptr_t IMG_EM = 0x17c3440;          // emblem manager
constexpr uintptr_t IMG_NET_PLAYERS = 0x17cb3f0; // ten net player records
constexpr uintptr_t NET_PLAYER_SIZE = 0x9c0;
constexpr uintptr_t IMG_MP_ACTIVE = 0x17d5c61;
constexpr uintptr_t IMG_MENU_USER1 = 0x17bde75;
constexpr uintptr_t IMG_MENU_USER = 0x17bf714;
constexpr uintptr_t IMG_PROFILES = 0x17dd610;
constexpr uintptr_t IMG_LOAD_JOB = 0x1b37530;
constexpr uintptr_t FN_STORE_EDITOR = 0x66c190;  // (EM)
constexpr uintptr_t FN_SET_RECORD = 0x78fa70;    // (net player, k, record)

// emblem manager fields (spec 3.2)
constexpr uintptr_t EM_MODE = 0x00, EM_TOGGLE = 0x01, EM_FAILED = 0x02;
constexpr uintptr_t EM_ED_COUNT = 0x04, EM_ED_LAYERS = 0x08;
constexpr uintptr_t EM_PL_COUNT = 0x28, EM_PL_LAYERS = 0x2c;
constexpr uintptr_t EM_USER = 0x4c, EM_PL_SLOTS = 0x50, EM_PL_INDEX = 0x60;
constexpr uintptr_t EM_ED_SLOTS = 0x68, EM_DEFAULT = 0x78, EM_COUNTDOWN = 0x84;

// texture record (spec 3.3) and net player record (spec 3.5)
constexpr uintptr_t REC_SIZE = 0x50, REC_PIN = 0x24, REC_USED = 0x26;
constexpr uint32_t REC_COUNT = 24;
constexpr uintptr_t NP_IN_USE = 0xe1, NP_LEAVING = 0x5e4, NP_RECORDS = 0x7e8, NP_LAYERS = 0x980;

constexpr uint32_t OFF_BIT = 0x4000;             // ec[0]: emblems switched off

template <typename T> [[gnu::always_inline]] inline T &fld(uintptr_t address) {
    return *reinterpret_cast<T *>(address);
}

[[gnu::always_inline]] inline uint32_t *ec() {
    return reinterpret_cast<uint32_t *>(u3h_ext() + 0x4450);
}

[[gnu::always_inline]] inline bool off() { return (ec()[0] & OFF_BIT) != 0; }

// "pin r" / "unpin r": one is_record question when r is not 0, none when it is
U3X_FN void set_pin(uint64_t r, uint16_t v) {
    if (r != 0 && u3h_emb_is_record(r) != 0) fld<uint16_t>(r + REC_PIN) = v;
}

// unpin and free a value when it is a record of the store
U3X_FN void drop_record(uintptr_t b, uint64_t r) {
    if (r != 0 && u3h_emb_is_record(r) != 0) {
        fld<uint16_t>(r + REC_PIN) = 0;
        u3h_emb_free_record(b, r);
    }
}

// "Flag := v": one u3h_emb_records call per store
U3X_FN void set_flag(uint8_t v) {
    uint8_t *flag;
    u3h_emb_records(&flag);
    *flag = v;
}

// the 32 bytes of an emblem (four layers)
[[gnu::always_inline]] inline void copy_layers(uintptr_t to, const uint8_t *from) {
    for (uint32_t n = 0; n < 32; ++n) fld<uint8_t>(to + n) = from[n];
}

// editor layers := the profile emblem of user u (when the profiles exist), layer count := 4
U3X_FN void load_editor_layers(uintptr_t b, uint32_t u) {
    const uintptr_t em = b + IMG_EM;
    const uint8_t *p = u3h_emb_profile(b, u);
    if (p) copy_layers(em + EM_ED_LAYERS, p);
    fld<int32_t>(em + EM_ED_COUNT) = 4;
}

// player mode, nothing in work: take the first valid net player that lacks an emblem record
U3X_FN void pick_player(uintptr_t b) {
    const uintptr_t em = b + IMG_EM;
    const uint8_t *job = fld<const uint8_t *>(b + IMG_LOAD_JOB);
    if (job && rd<uint64_t>(job, 0x20) != 0) return;   // a texture load is pending
    for (uint32_t i = 0; i < 10; ++i) {
        const uint8_t *np = u3h_emb_np(b, i);
        if (!np || !u3x_emb_valid(np)) continue;
        if (rd<uint64_t>(np, NP_RECORDS) != 0 && rd<uint64_t>(np, NP_RECORDS + 8) != 0) continue;
        fld<int32_t>(em + EM_PL_INDEX) = static_cast<int32_t>(i);
        const uint8_t *src = u3h_emb_np(b, i);         // asked a second time
        copy_layers(em + EM_PL_LAYERS, src + NP_LAYERS);
        fld<int32_t>(em + EM_PL_COUNT) = 4;
        set_flag(1);
        fld<uint8_t>(em + EM_FAILED) = 0;
        fld<uint8_t>(em + EM_TOGGLE) = 1;
        u3h_emb_new_records(b, EM_PL_SLOTS, 0x80);
        return;
    }
}

// player mode, composed: give both records to the player, or free them
U3X_FN void hand_over(uintptr_t b, int32_t index) {
    const uintptr_t em = b + IMG_EM;
    for (int32_t k = 0; k < 2; ++k) {
        const uintptr_t slot = em + EM_PL_SLOTS + 8 * static_cast<uintptr_t>(k);
        const uint64_t r = fld<uint64_t>(slot);
        set_pin(r, 0);
        uint8_t *np = nullptr;
        if (fld<uint8_t>(em + EM_FAILED) == 0) np = u3h_emb_np(b, static_cast<uint32_t>(index));
        if (np) {
            u3h_emb_set_record(b, np, k, r);           // also with r = 0
            ec()[121] += 1;
        } else {
            u3h_emb_free_record(b, r);
        }
        fld<uint64_t>(slot) = 0;
    }
    fld<int32_t>(em + EM_PL_INDEX) = -1;
}

}  // namespace g58b

// ---- small services (spec 4.5)

extern "C" U3X_FN uint8_t *u3x_emb_np(uintptr_t b, uint32_t i) {
    if (g58b::fld<uint8_t>(b + g58b::IMG_MP_ACTIVE) == 0 || i > 9) return nullptr;
    return reinterpret_cast<uint8_t *>(b + g58b::IMG_NET_PLAYERS + g58b::NET_PLAYER_SIZE * i);
}

extern "C" U3X_FN int u3x_emb_valid(const uint8_t *np) {
    return rd<uint8_t>(np, g58b::NP_IN_USE) != 0 && rd<uint8_t>(np, g58b::NP_LEAVING) == 0 ? 1 : 0;
}

extern "C" U3X_FN uint32_t u3x_emb_local(uintptr_t b) {
    if (g58b::fld<uint8_t>(b + g58b::IMG_MENU_USER1) != 0) return 1;
    return g58b::fld<uint32_t>(b + g58b::IMG_MENU_USER);
}

extern "C" U3X_FN uint8_t *u3x_emb_prof(uintptr_t b, uint32_t u) {
    uint8_t *profiles = g58b::fld<uint8_t *>(b + g58b::IMG_PROFILES);
    if (!profiles) return nullptr;
    return profiles + static_cast<uintptr_t>(0x7b00) * u + 0x1510;
}

extern "C" U3X_FN void u3x_emb_setrec(uintptr_t b, uint8_t *np, int32_t k, uint64_t r) {
    const uint64_t old = rd<uint64_t>(np + static_cast<intptr_t>(k) * 8, g58b::NP_RECORDS);
    g58b::drop_record(b, old);
    game_call<void>(b + g58b::FN_SET_RECORD, np, k, r);
}

extern "C" U3X_FN void u3x_emb_newrecs(uintptr_t b, uint32_t off, uint32_t size) {
    for (uint32_t k = 0; k < 2; ++k) {
        const uintptr_t slot = b + g58b::IMG_EM + off + 8 * k;
        if (g58b::fld<uint64_t>(slot) != 0) continue;
        const uintptr_t r = reinterpret_cast<uintptr_t>(u3h_emb_ralloc(size));
        if (r) g58b::fld<uint16_t>(r + g58b::REC_PIN) = 1;
        g58b::fld<uint64_t>(slot) = r;
    }
}

// ---- player mode step (spec 4.6)

extern "C" U3X_FN void u3x_emblem_players(uintptr_t b, void *) {
    using namespace g58b;
    const uintptr_t em = b + IMG_EM;
    const int32_t index = fld<int32_t>(em + EM_PL_INDEX);
    set_flag(0);
    if (index == -1)
        pick_player(b);
    else if (fld<uint8_t>(em + EM_TOGGLE) == 0)
        hand_over(b, index);
    else
        fld<uint8_t>(em + EM_TOGGLE) = 0;
}

// ---- editor compose request (spec 4.7)

extern "C" U3X_FN void u3x_emblem_editor(uintptr_t b, void *s) {
    using namespace g58b;
    const uintptr_t em = b + IMG_EM;
    if (fld<uint8_t>(em + EM_MODE) == 0) return;
    ec()[120] += 1;
    const bool fresh = fld<int32_t>(em + EM_COUNTDOWN) < 1;
    fld<int32_t>(em + EM_COUNTDOWN) = 4;
    if (!fresh) return;                                // a request is already running
    fld<int32_t>(em + EM_PL_INDEX) = -1;               // the player slots are left as they are
    const uint32_t user = u3x_emb_local(b);
    fld<uint32_t>(em + EM_USER) = user;
    load_editor_layers(b, user);
    set_flag(1);
    fld<uint8_t>(em + EM_TOGGLE) = 1;
    u3h_emb_new_records(b, EM_ED_SLOTS, 0x100);
    u3h_emb_bind(b, s);
}

// ---- port (v7.214): emblem life-cycle notes in boot.log ("U3MP online: custom game: emblem ...")

extern "C" void u3h_cg_log(const char *text, int32_t value);
extern "C" uint32_t *u3h_emb_wall_diag(void);
U3X_RO const char emb_note_again[] = "emblem: menus without the editor emblem, asked again ";
U3X_RO const char emb_note_mode[] = "emblem: manager mode (1 menus / editor, 0 match / players) ";
U3X_RO const char emb_note_hand[] = "emblem: player emblems handed over so far ";
U3X_RO const char emb_note_look[] = "emblem: character tables written so far ";
U3X_RO const char emb_note_dds[] = "emblem: layer images that could not be read so far ";
U3X_RO const char emb_note_wall[] = "emblem: wall objects filled so far ";
U3X_RO const char emb_note_wallp[] = "emblem: wall objects given a player's emblem so far ";
U3X_BSS uint32_t emb_notes_left;   // notes of the 'asked again' kind still allowed (set on each mode change)
U3X_BSS uint8_t emb_mode_seen;     // the manager mode + 1 last seen, 0 = none yet

U3X_FN void emblem_note(const char *text, int32_t value) {
    if (text == emb_note_again) {
        if (emb_notes_left == 0) return;
        emb_notes_left -= 1;
    }
    u3h_cg_log(text, value);
}

// a change of the manager mode (the game's match setup 0x66b510 / match end 0x66b090) is noted with the counters of
// the phase that ended, so a test log shows whether the match composed player emblems and filled tables
U3X_FN void emblem_mode_watch(uintptr_t b) {
    using namespace g58b;
    const uint8_t mode = fld<uint8_t>(b + IMG_EM + EM_MODE);
    if (emb_mode_seen == static_cast<uint8_t>(mode + 1)) return;
    emb_mode_seen = static_cast<uint8_t>(mode + 1);
    emb_notes_left = 4;
    const uint32_t *ew = u3h_emb_wall_diag();
    u3h_cg_log(emb_note_mode, mode);
    u3h_cg_log(emb_note_hand, static_cast<int32_t>(ec()[121]));
    u3h_cg_log(emb_note_look, static_cast<int32_t>(ec()[119]));
    u3h_cg_log(emb_note_dds, static_cast<int32_t>(ec()[123]));
    u3h_cg_log(emb_note_wall, static_cast<int32_t>(ew[1]));
    u3h_cg_log(emb_note_wallp, static_cast<int32_t>(ec()[147]));
}

// ---- per-frame update (spec 4.8)

extern "C" U3X_FN void u3x_emblem_update(uintptr_t b, void *s) {
    using namespace g58b;
    const uintptr_t em = b + IMG_EM;

    if (fld<uint8_t>(em + EM_MODE) == 0) {             // player mode
        set_pin(fld<uint64_t>(em + EM_ED_SLOTS), 0);
        set_pin(fld<uint64_t>(em + EM_ED_SLOTS + 8), 0);
        u3x_emblem_players(b, s);
        return;
    }

    // editor mode: another menu user means another emblem
    const uint32_t user = u3x_emb_local(b);
    if (fld<uint32_t>(em + EM_USER) != user) {
        game_call<void>(b + FN_STORE_EDITOR, reinterpret_cast<uint8_t *>(em));
        fld<uint32_t>(em + EM_USER) = user;
        load_editor_layers(b, user);
        u3x_emblem_editor(b, s);
    }
    set_flag(0);
    if (fld<uint64_t>(em + EM_ED_SLOTS) == 0) {
        // port (v7.214): back in the menus after a match the editor records are gone (NetShutdown freed them, the
        // match setup emptied the slots) and nothing asks again: the PS3 menu pages ask on their event 9, which the PS4
        // lobby does not send. Ask for the editor emblem again (the countdown first, so the request is a fresh one).
        fld<int32_t>(em + EM_COUNTDOWN) = 0;
        u3x_emblem_editor(b, s);
        emblem_note(emb_note_again, 0);
        return;
    }

    int32_t count = fld<int32_t>(em + EM_COUNTDOWN);
    if (count > 0) fld<int32_t>(em + EM_COUNTDOWN) = --count;
    const uint64_t rec0 = fld<uint64_t>(em + EM_ED_SLOTS);
    const uint64_t rec1 = fld<uint64_t>(em + EM_ED_SLOTS + 8);

    if (fld<uint8_t>(em + EM_FAILED) == 0) {
        if (count == 0) {                              // the request has run out
            set_pin(rec0, 0);
            set_pin(rec1, 0);
            return;
        }
        if (count < 2) return;                         // 1 or negative
    } else {
        fld<int32_t>(em + EM_COUNTDOWN) = 4;           // keep the request alive
    }

    set_pin(rec0, 1);
    set_pin(rec1, 1);
    const uint8_t toggle = fld<uint8_t>(em + EM_TOGGLE);
    if (toggle == 0) set_flag(1);
    fld<uint8_t>(em + EM_TOGGLE) = toggle == 0 ? 1 : 0;
}

// ---- free the records nobody references (spec 4.9)

extern "C" U3X_FN void u3x_emblem_sweep(uintptr_t b) {
    using namespace g58b;
    const uintptr_t em = b + IMG_EM;
    uint8_t *flag;
    const uintptr_t recs = reinterpret_cast<uintptr_t>(u3h_emb_records(&flag));

    // every place that can hold a record: all read before the first question
    uint64_t held[25];
    held[0] = fld<uint64_t>(em + EM_PL_SLOTS);
    held[1] = fld<uint64_t>(em + EM_PL_SLOTS + 8);
    held[2] = fld<uint64_t>(em + EM_ED_SLOTS);
    held[3] = fld<uint64_t>(em + EM_ED_SLOTS + 8);
    held[4] = fld<uint64_t>(em + EM_DEFAULT);
    for (uint32_t i = 0; i < 10; ++i) {
        const uintptr_t np = b + IMG_NET_PLAYERS + NET_PLAYER_SIZE * i;
        held[5 + 2 * i] = fld<uint64_t>(np + NP_RECORDS);
        held[6 + 2 * i] = fld<uint64_t>(np + NP_RECORDS + 8);
    }

    uint32_t referenced = 0;                           // one bit per record of the store
    for (uint32_t n = 0; n < 25; ++n) {
        const uint64_t v = held[n];
        if (v != 0 && u3h_emb_is_record(v) != 0) referenced |= 1u << ((v - recs) / REC_SIZE);
    }

    for (uint32_t n = 0; n < REC_COUNT; ++n) {
        const uintptr_t r = recs + REC_SIZE * n;
        if (fld<uint16_t>(r + REC_USED) == 0 || (referenced >> n & 1) != 0) continue;
        fld<uint16_t>(r + REC_PIN) = 0;
        u3h_emb_free_record(b, r);
    }
}

// ---- once per frame (spec 4.10)

extern "C" U3X_FN void u3x_emblem_tick(uintptr_t b, void *s) {
    using namespace g58b;
    u3h_emb_frame_parts(b, s, 0);                      // the empty texture: also when emblems are off
    if (off()) return;
    u3h_emb_frame_parts(b, s, 1);
    u3x_emblem_sweep(b);
    if (fld<uint8_t>(b + IMG_MP_ACTIVE) == 0) return;
    emblem_mode_watch(b);
    u3x_emblem_update(b, s);
    u3h_emb_frame_parts(b, s, 2);
    ec()[126] += 1;
}

// ---- network start (spec 4.11)

extern "C" U3X_FN uint64_t u3x_emblem_default_record(uintptr_t b) {
    void *r = u3h_emb_ralloc(4);
    if (r) {
        g58b::fld<uint16_t>(reinterpret_cast<uintptr_t>(r) + g58b::REC_PIN) = 1;
        const uint8_t *pixels = u3h_emb_zero();
        u3x_emb_texcreate(b, r, pixels, 4);            // result not used
    }
    return reinterpret_cast<uint64_t>(r);
}

extern "C" U3X_FN int u3x_emblem_netinit(uintptr_t b, void *s) {
    using namespace g58b;
    const uintptr_t em = b + IMG_EM;
    if (off()) return 0;
    if (fld<uint64_t>(em + EM_DEFAULT) == 0) fld<uint64_t>(em + EM_DEFAULT) = u3h_emb_alloc_default(b);
    u3h_emb_bind(b, s);
    set_pin(fld<uint64_t>(em + EM_DEFAULT), 0);
    u3h_emb_bind(b, s);                                // bound a second time
    ec()[127] += 1;
    if (fld<uint8_t>(em + EM_MODE) != 0) {
        u3x_emblem_editor(b, s);
        ec()[146] += 1;
    }
    return 1;
}

// ---- network shutdown (spec 4.12)

extern "C" U3X_FN void u3x_emblem_shutdown(uintptr_t b) {
    using namespace g58b;
    if (off()) return;
    set_flag(0);
    for (uint32_t k = 0; k < 2; ++k) {
        const uintptr_t slot = b + IMG_EM + EM_ED_SLOTS + 8 * k;
        drop_record(b, fld<uint64_t>(slot));
        fld<uint64_t>(slot) = 0;
    }
    ec()[128] += 1;
}
