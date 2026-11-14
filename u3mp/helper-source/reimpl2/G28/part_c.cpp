// G28 part C: stat bars, the viewed-profile object, deleting a LAN profile.
// Written from spec G28_medals_records.md (sections 3.7 to 3.13, 4.12 to 4.15).

extern "C" {
uint8_t *u3x_sym_table(void *s, uint32_t id);
uint8_t *u3h_player_profile(uintptr_t base);
void u3h_show_dialog(uintptr_t base, void *s, uint32_t body, uint32_t title, uint32_t action, int32_t buttons,
                     uint32_t extra, uint32_t close);
uint32_t u3h_active_page(uintptr_t base);
int u3h_show(uintptr_t base, void *s, uint32_t page, int silent);
uint8_t *u3h_lg_rec(uintptr_t base, void *s, uint8_t *p);
void *u3h_i05_state(void);
int u3x_same_player(const uint8_t *selected, const uint8_t *id);
void u3x_remove_name(uint8_t *names, uint32_t count, uint32_t index, uint32_t bytes);
unsigned u3_mp_path(char *out, uint32_t slot, int coop, int tmp);
int u3h_profiles_save(void *s, const void *store);
void u3h_ol_profile_deleted(void *s, uint32_t index, uint32_t count);
void u3h_j01_filled(int own);

int u3x_i02_bar(uintptr_t base, const uint64_t *r, uint32_t *counters);
void u3x_stats_defaults(uintptr_t base, void *s, uint8_t *object);
void u3x_view_stats(uintptr_t base, void *s, uint8_t *object, uint32_t *counters);
void u3x_lan_delete_profile(uintptr_t base, void *s, uint32_t action);
}

namespace g28c {

// image offsets
constexpr uintptr_t IMG_NAMED_COLOUR = 0x5b9580;    // (out[4], colour id), alpha factor in xmm0
constexpr uintptr_t IMG_FILL_RECT = 0x647470;       // (a0, a1, rect[4], colour[4])
constexpr uintptr_t IMG_RECORD_DEFAULTS = 0x662812; // 8 entries of 12 bytes inside the game's code
constexpr uintptr_t IMG_VIEWED_PROFILE = 0x17c3568;
constexpr uintptr_t IMG_SELECTED_ID = 0x17c3871;
constexpr uintptr_t IMG_OWN_ID = 0x17d6010;
constexpr uintptr_t IMG_LAN_ROW = 0x1b6a400;

constexpr uint32_t PAGE_CARD = 0x83f24f04u;
constexpr uint32_t ACTION_ASK = 0x6d6ad418u;

constexpr uint32_t RECORD_AT = 0x248;     // the multiplayer record inside a profile object
constexpr uint32_t RECORD_BYTES = 0x2828;
constexpr uint32_t LOAD_STATE_AT = 0x7ae8;
constexpr uint32_t STORE_AT = 0x78;       // the LAN profile store inside the state object
constexpr uint32_t STORE_BYTES = 0x210;

struct [[gnu::packed]] Word { uint32_t v; };   // a 32-bit word at any alignment

struct alignas(16) Quad { uint32_t f[4]; };    // four float patterns for the rectangle draw

template <typename T> [[gnu::always_inline]] inline T &field(void *p, uintptr_t off) {
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + off);
}

U3X_FN void copy_bytes(uint8_t *to, const uint8_t *from, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        to[i] = from[i];
        __asm__ volatile("");   // keeps this a plain loop (no library call)
    }
}

U3X_FN void clear_bytes(uint8_t *to, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        to[i] = 0;
        __asm__ volatile("");
    }
}

U3X_FN void fill_rect(uintptr_t base, void *a0, void *a1, const Quad *rect, const Quad *colour) {
    game_call<void>(base + IMG_FILL_RECT, a0, a1, rect->f, colour->f);
}

// "empty the file of slot": open for writing with truncation, close again
U3X_FN void empty_file(void *s, uint32_t slot, int coop) {
    char path[64];
    u3_mp_path(path, slot, coop, 0);
    const auto open = rd<int64_t (*)(const char *, int, int)>(s, 32);
    const int64_t handle = open(path, 0x601, 0x180);
    if (handle >= 0) rd<int64_t (*)(int)>(s, 56)(static_cast<int>(handle));
}

// the files of one kind move down one slot over the deleted one; n = profiles left
U3X_FN void shift_files(void *s, uint32_t index, uint32_t n, int coop) {
    for (uint32_t slot = index + 1; slot <= n; slot++) {
        empty_file(s, slot, coop);
        char from[64], to[64];
        u3_mp_path(from, slot + 1, coop, 0);
        u3_mp_path(to, slot, coop, 0);
        rd<int64_t (*)(const char *, const char *)>(s, 64)(from, to);
    }
    empty_file(s, n + 1, coop);
}

}  // namespace g28c

// 4.12: draws one stat bar (track, base fill, second segment). Always 1.
extern "C" U3X_FN int u3x_i02_bar(uintptr_t base, const uint64_t *r, uint32_t *counters) {
    using namespace g28c;
    uint8_t *frame = reinterpret_cast<uint8_t *>(r[0]);
    uint8_t *item = reinterpret_cast<uint8_t *>(r[1]);
    if (!frame || !item) return 1;

    const uint32_t red = static_cast<uint32_t>(r[2] >> 16) & 0xff;
    const uint32_t blue = static_cast<uint32_t>(r[3]) & 0xff;
    const uint32_t alpha = static_cast<uint32_t>(r[4] >> 8) & 0xff;
    const uint32_t green = static_cast<uint32_t>(r[5] >> 8) & 0xff;

    // Track colour: a plain two-integer call, made before any vector instruction of this function, so the game
    // function finds in xmm0 what the item draw left there (shipped behaviour, kept).
    Quad track;
    track.f[0] = 0; track.f[1] = 0; track.f[2] = 0; track.f[3] = 0;
    game_call<void>(base + IMG_NAMED_COLOUR, track.f, 0xb9f73e49u);

    void *a0 = rd<void *>(frame - 0x2060, 0);
    void *a1 = rd<void *>(frame - 0x2070, 0);
    Quad outer;
    for (int i = 0; i < 4; i++) outer.f[i] = rd<uint32_t>(frame - 0x1eb0, 4 * i);
    fill_rect(base, a0, a1, &outer, &track);

    // element colour, each part n * (1/255)
    constexpr uint32_t INV_255 = 0x3b808081u;
    Quad colour;
    colour.f[0] = f_mul(f_from_int(static_cast<int32_t>(red)), INV_255);
    colour.f[1] = f_mul(f_from_int(static_cast<int32_t>(green)), INV_255);
    colour.f[2] = f_mul(f_from_int(static_cast<int32_t>(blue)), INV_255);
    colour.f[3] = f_mul(f_from_int(static_cast<int32_t>(alpha)), INV_255);

    // inner rectangle: 2 units in on every side
    const uint32_t x = f_add(outer.f[0], 0x40000000u);
    const uint32_t y = f_add(outer.f[1], 0x40000000u);
    const uint32_t w = f_sub(outer.f[2], 0x40800000u);
    const uint32_t h = f_sub(outer.f[3], 0x40800000u);

    const uint32_t packed = rd<uint32_t>(item, 0x10);
    const uint32_t lo = packed & 0xffff, hi = packed >> 16;
    counters[0] += 1;
    counters[3] = packed;
    counters[4] = alpha << 24 | red << 16 | green << 8 | blue;
    const uint32_t page = u3h_active_page(base);
    counters[7] = page;
    if (page == 0x35957e36u) counters[5] += 1;
    if (page == 0x6d627b29u) counters[6] += 1;
    if (!lo) return 1;

    // base fill: lo percent of the inner width, at least 1.0 wide
    const uint32_t per_unit = f_mul(w, 0x3c23d70au);
    uint32_t fill = f_mul(f_from_int(static_cast<int32_t>(lo)), per_unit);
    if (f_less(fill, F_ONE)) fill = F_ONE;
    Quad rect;
    rect.f[0] = x; rect.f[1] = y; rect.f[2] = fill; rect.f[3] = h;
    fill_rect(base, a0, a1, &rect, &colour);
    counters[1] += 1;
    if (!hi) return 1;

    // second segment: the part the mods add, in a darker colour, right of the base fill
    const uint32_t extra = f_mul(f_from_int(static_cast<int32_t>(hi) - static_cast<int32_t>(lo)), per_unit);
    if (f_less(0, extra)) {
        Quad dark;
        rect.f[0] = f_add(x, fill); rect.f[1] = y; rect.f[2] = extra; rect.f[3] = h;
        dark.f[0] = f_mul(colour.f[0], 0x3f4ccccdu);
        dark.f[1] = f_mul(colour.f[1], 0x3f4ccccdu);
        dark.f[2] = f_mul(colour.f[2], 0x3ecccccdu);
        dark.f[3] = colour.f[3];
        fill_rect(base, a0, a1, &rect, &dark);
        counters[2] += 1;
    }
    return 1;
}

// 4.13: makes the multiplayer record inside the object a fresh one (the load state is not touched).
extern "C" U3X_FN void u3x_stats_defaults(uintptr_t base, void *s, uint8_t *object) {
    using namespace g28c;
    clear_bytes(object + RECORD_AT + 4, RECORD_BYTES - 4);
    if (const uint8_t *value = u3x_sym_table(s, 0xc27df04cu)) field<uint32_t>(object, 0x74c) = rd<uint32_t>(value, 0);
    field<uint32_t>(object, RECORD_AT) = 0x56;
    // the eight default words, as the game's own code holds them: 4 code bytes, s32 offset, u32 value
    const uint8_t *entry = reinterpret_cast<const uint8_t *>(base + IMG_RECORD_DEFAULTS);
    for (int k = 0; k < 8; k++, entry += 12) {
        const int32_t offset = static_cast<int32_t>(rd<Word>(entry, 4).v);
        reinterpret_cast<Word *>(object + offset)->v = rd<Word>(entry, 8).v;
    }
}

// 4.14: installs the helper's profile object as the viewed profile, shows the player card and fills the object.
extern "C" U3X_FN void u3x_view_stats(uintptr_t base, void *s, uint8_t *object, uint32_t *counters) {
    using namespace g28c;
    if (ext_eb()[1] & 0x80000u) {
        u3h_show(base, s, PAGE_CARD, 0);
        return;
    }
    uint8_t *&viewed = at<uint8_t *>(base + IMG_VIEWED_PROFILE);
    if (!viewed) {
        viewed = object;
        counters[0] += 1;
    }

    // whose card the party list click meant; void when the selection changed since
    uint8_t *mark = static_cast<uint8_t *>(u3h_i05_state());
    const uint8_t *selected = reinterpret_cast<const uint8_t *>(base + IMG_SELECTED_ID);
    uint32_t who = mark[0];
    mark[0] = 0;
    for (uint32_t i = 0; i < 0x24; i++)
        if (selected[i] != mark[1 + i]) { who = 0; break; }

    u3h_show(base, s, PAGE_CARD, 0);
    if (viewed != object) {   // the game shows another object: nothing to fill
        counters[3] += 1;
        return;
    }

    uint8_t *profile = u3h_player_profile(base);
    bool own = who == 1;
    if (who == 0)
        own = u3x_same_player(selected, reinterpret_cast<const uint8_t *>(base + IMG_OWN_ID)) != 0 ||
              (profile && u3x_same_player(selected, profile + 0x7ac0) != 0);
    const bool copied = own && profile;
    if (copied) {
        copy_bytes(object + RECORD_AT, u3h_lg_rec(base, s, profile), RECORD_BYTES);
        counters[1] += 1;
    } else {
        u3x_stats_defaults(base, s, object);
        counters[2] += 1;
    }
    field<uint32_t>(object, LOAD_STATE_AT) = 3;
    u3h_j01_filled(copied ? 1 : 0);
}

// 4.15: the Delete Profile question and the removal of the selected LAN profile with its files.
extern "C" U3X_FN void u3x_lan_delete_profile(uintptr_t base, void *s, uint32_t action) {
    using namespace g28c;
    uint32_t *ea = ext_ea();
    if (ea[0] & 2u) return;
    uint32_t &row = at<uint32_t>(base + IMG_LAN_ROW);
    if (action == ACTION_ASK) {
        if (row) {
            u3h_show_dialog(base, s, 0x1e01f666u, 0x0260a263u, 0xda18d7d3u, 0x075bc82d, 0xf99fbf09u, 0);
            ea[8] += 1;
        }
        return;
    }
    if (!row) return;
    const uint32_t picked = row;
    const uint32_t index = picked - 1;
    uint8_t *store = static_cast<uint8_t *>(s) + STORE_AT;
    const uint32_t count = rd<uint32_t>(store, 8);
    if (index < count) {
        uint8_t copy[STORE_BYTES];
        copy_bytes(copy, store, STORE_BYTES);
        u3x_remove_name(copy + 0x10, count, index, 64);
        const uint32_t left = count - 1;
        field<uint32_t>(copy, 8) = left;
        field<uint32_t>(copy, 0xc) = 0;
        if (!u3h_profiles_save(s, copy)) {
            field<uint32_t>(s, 0x50) = 4;
            return;
        }
        copy_bytes(store, copy, STORE_BYTES);
        shift_files(s, index, left, 0);
        shift_files(s, index, left, 1);
        u3h_ol_profile_deleted(s, index, left);
        ea[9] += 1;
        ea[10] = picked;
    }
    row = 0;
}
