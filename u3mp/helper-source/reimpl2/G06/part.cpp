// G06: the Cinema film list (title, row texts, row images, selection) and the film HUD's tab bar hook.
// Written from spec/G06_cinema_rows.md.

extern "C" {
// the helper's C part and other groups
uint8_t *u3h_ext(void);
uint8_t *u3h_cin_state(uintptr_t base);
char *u3h_cin_buffer(int which);
const char *u3h_text(uintptr_t base, void *s, uint32_t id);
char *u3h_cin_cat(char *o, char *end, const char *t);
char *u3h_cin_num(char *o, char *end, int64_t v, int width);
const int32_t *u3h_cin_localtime(const int64_t *t);
int u3x_film_select(uintptr_t base, void *s, int idx);
int u3x_k23_callees(uintptr_t base);
void u3x_k23_update(uintptr_t base, uint8_t *el);
int u3h_k26_draw(uintptr_t b, void *el, void *rbp);
// this group
int32_t u3x_cin_icon_index(uintptr_t base, uint32_t id, int which);
void u3x_cin_image(int32_t *ev, int32_t type, void *ptr, int32_t a4, int32_t a5, int32_t a6, int32_t a7);
uint8_t *u3x_cin_netgame(void *s, int idx);
int u3x_cinema_rows(uintptr_t base, int32_t *ev, void *s);
void u3x_cin_megabytes(uintptr_t base, void *s, char *b, const uint8_t *e);
int u3x_k23_rep(uintptr_t base, uint8_t *pad, uint32_t m);
int u3x_k23_hud(uintptr_t base, uint64_t *r);
}

namespace {
namespace g06 {

// ---- the helper's state block (ExtState) ----
constexpr uintptr_t EXT_KERNEL_IMPORTS = 0x3210;   // four pointers
constexpr uintptr_t EXT_ROWS_CALLS = 0x3250;
constexpr uintptr_t EXT_LAST_LOCAL_PICK = 0x3254;
constexpr uintptr_t EXT_SWITCHES = 0x3a70;         // bit 1: Cinema port on
constexpr uintptr_t EXT_BAR_SWITCHES = 0x4054;     // bit 0: no tab bar, bit 1: update without draw
constexpr uintptr_t EXT_ONLINE_PICK = 0x40b0;
constexpr uintptr_t EXT_HUD_ENTRIES = 0x4370;
constexpr uintptr_t EXT_BAR_UPDATES = 0x4374;
constexpr uintptr_t EXT_DRAWS_SKIPPED = 0x437c;

// ---- the state object ----
constexpr uintptr_t S_SYMBOL_LOOKUP = 656;
constexpr uintptr_t S_TEXT_NATIVE = 800;
constexpr uintptr_t S_SYMBOL_VALUE = 872;          // native 13
constexpr uintptr_t S_TEXTURE_BY_NAME = 928;       // native 20

// ---- game data and functions (image offsets) ----
constexpr uintptr_t LIST_IS_LOCAL = 0x16e7168;
constexpr uintptr_t NET_MENU_MODE = 0x17d603c;
constexpr uintptr_t NET_GAME_MANAGER = 0x17cb060;
constexpr uintptr_t CUSTOM_RULES = 0x17d1a98;
constexpr uintptr_t LANGUAGES = 0x1acd198;
constexpr uintptr_t REGION = 0x17b9cfc;
constexpr uintptr_t UI_OBJECT = 0x1b24bc8;
constexpr uintptr_t TEXTURE_MANAGER = 0x1b31128;
constexpr uintptr_t CLOCKS = 0x1b24b98;
constexpr uintptr_t CUT_SCENE = 0x17b4a68;
constexpr uintptr_t FN_NET_GAME_NAME = 0x741260;
constexpr uintptr_t FN_GAME_TYPE = 0x741070;

// ---- ids ----
constexpr uint32_t SYM_NET_GAMES = 0x3b9a067d;
constexpr uint32_t SYM_MAPS = 0x0dec97a4;
constexpr uint32_t TEXT_LOCAL_FILMS = 0xe626e948;
constexpr uint32_t TEXT_ONLINE_FILES = 0xdc27f8a0;
constexpr uint32_t TEXT_DECIMAL_POINT = 0xaaa1ef9b;
constexpr uint32_t COL_TITLE = 0;
constexpr uint32_t COL_USER = 0x8adf5c31;
constexpr uint32_t COL_NUMBER = 0x7cc44c67;
constexpr uint32_t COL_DURATION = 0x9586358c;
constexpr uint32_t COL_SIZE = 0x0ed4812e;
constexpr uint32_t COL_DATE = 0x8419ca2a;
constexpr uint32_t COL_MAP = 0x16094dd2;
constexpr uint32_t COL_GAME_TYPE = 0xa42c91c5;
constexpr uint32_t SLOT_NET_GAME = 0x2138a9b2;
constexpr uint32_t SLOT_GAME_TYPE = 0xa42c91c5;
constexpr uint32_t SLOT_LOCK = 0;
constexpr uint32_t ICON_LOCK = 0xea36b17f;

// ---- sizes ----
constexpr uintptr_t FILM_ENTRY_SIZE = 0x1e90;
constexpr uint32_t FILM_ROWS_MAX = 20;
constexpr int ROW_TEXT_MAX = 0x1ff;
constexpr int SCRATCH_SIZE = 0x100;
constexpr int MEGABYTES_MAX = 0x3f;
constexpr int DATE_PART_MAX = 15;

// ---- float constants of the auto-repeat (32-bit patterns) ----
constexpr uint32_t F_SECONDS_PER_TICK = 0x3b5a740e;   // 1/300
constexpr uint32_t F_REPEAT_DELAY = 0x3e4ccccd;       // 0.2
constexpr uint32_t F_FIRST_STEP = 0x3dcccccd;         // 0.1
constexpr uint32_t F_HALF = 0x3f000000;
constexpr uint32_t F_TWO = 0x40000000;
constexpr uint32_t F_FOUR = 0x40800000;
constexpr uint32_t F_MINUS_ONE = 0xbf800000;

U3X_RO const char TXT_EMPTY[] = "";
U3X_RO const char TXT_DASH[] = " - ";
U3X_RO const char TXT_COLON[] = ":";
U3X_RO const char TXT_SLASH[] = "/";
U3X_RO const char TXT_SPACE[] = " ";
U3X_RO const char TXT_AM[] = "AM";
U3X_RO const char TXT_PM[] = "PM";
U3X_RO const char TXT_MB[] = " MB";

// a writable field of a structure
template <typename T> [[gnu::always_inline]] inline T &field(void *p, uintptr_t off) {
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + off);
}

// a text being built: everything is cut at `end` and NUL-terminated
struct Text {
    char *start;
    char *o;
    char *end;
    [[gnu::always_inline]] Text(char *buffer, int max) : start(buffer), o(buffer), end(buffer + max) {}
    [[gnu::always_inline]] Text &s(const char *t) { o = u3h_cin_cat(o, end, t); return *this; }
    [[gnu::always_inline]] Text &n(int64_t v, int width) { o = u3h_cin_num(o, end, v, width); return *this; }
};

[[gnu::always_inline]] inline void set_text(int32_t *ev, const char *text) { field<const char *>(ev, 0x40) = text; }

[[gnu::always_inline]] inline bool cinema_on(uint8_t *ext) {
    if (!(field<uint32_t>(ext, EXT_SWITCHES) & 2)) return false;
    for (int i = 0; i < 4; ++i)
        if (!field<void *>(ext, EXT_KERNEL_IMPORTS + 8 * i)) return false;
    return true;
}

// the net game record of a film entry (the entry's index is a signed 16-bit value)
U3X_FN uint8_t *film_net_game(void *s, const uint8_t *entry) {
    return u3x_cin_netgame(s, static_cast<int16_t>(rd<uint32_t>(entry, 0x220)));
}

// the game type record of a net game record, or null
U3X_FN const uint8_t *game_type_of(uintptr_t base, const uint8_t *record) {
    return game_call<const uint8_t *>(base + FN_GAME_TYPE, reinterpret_cast<uint8_t *>(base + NET_GAME_MANAGER),
                                      rd<int32_t>(record, 0xc));
}

// text id of the name of a map, 0 when the map table does not have it
U3X_FN uint32_t map_name_id(void *s, uint32_t map_id) {
    using SymbolValue = const uint8_t *(*)(uint32_t);
    const uint8_t *table = rd<SymbolValue>(s, S_SYMBOL_VALUE)(SYM_MAPS);
    if (!table) return 0;
    const uint8_t *maps = rd<const uint8_t *>(table, 8);
    if (!maps) return 0;
    const uint32_t count = rd<uint32_t>(table, 0);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *map = maps + uintptr_t{i} * 0x98;
        if (rd<uint32_t>(map, 0) == map_id) return rd<uint32_t>(map, 4);
    }
    return 0;
}

// "day/month/year time" of a film, in the order and clock of the region
U3X_FN void date_text(uintptr_t base, int32_t *ev, const uint8_t *entry) {
    const int32_t *cal = u3h_cin_localtime(reinterpret_cast<const int64_t *>(entry + 0x238));
    if (!cal) return;
    const int32_t minute = cal[1], hour = cal[2];
    const uint8_t *languages = reinterpret_cast<const uint8_t *>(base + LANGUAGES);
    const uint32_t language = rd<uint32_t>(languages, 8 + uintptr_t{rd<uint32_t>(languages, 0x604)} * 0x38);
    const bool twelve_hours = at<uint32_t>(base + REGION) == 1;

    char time_b[DATE_PART_MAX + 1], day_b[DATE_PART_MAX + 1], month_b[DATE_PART_MAX + 1], year_b[DATE_PART_MAX + 1];
    Text time(time_b, DATE_PART_MAX);
    if (twelve_hours) {
        const int32_t h = hour % 12;
        time.n(h ? h : 12, 2).s(TXT_COLON).n(minute, 2).s(hour < 12 ? TXT_AM : TXT_PM);
    } else {
        time.n(hour, 2).s(TXT_COLON).n(minute, 2);
    }
    Text(day_b, DATE_PART_MAX).n(cal[3], 2);
    Text(month_b, DATE_PART_MAX).n(int64_t{cal[4]} + 1, 2);
    Text(year_b, DATE_PART_MAX).n(int64_t{cal[5]} + 1900, 4);

    // one leading zero goes, but never from the year
    const char *time_s = time_b + (time_b[0] == '0');
    const char *day_s = day_b + (day_b[0] == '0');
    const char *month_s = month_b + (month_b[0] == '0');
    const bool month_first = language == 0 && twelve_hours;

    Text row(u3h_cin_buffer(0), ROW_TEXT_MAX);
    row.s(month_first ? month_s : day_s).s(TXT_SLASH).s(month_first ? day_s : month_s).s(TXT_SLASH)
       .s(year_b).s(TXT_SPACE).s(time_s);
    set_text(ev, row.start);
}

// operation 2: the text of one column of a film row
U3X_FN void row_text(uintptr_t base, int32_t *ev, void *s, const uint8_t *entry) {
    switch (static_cast<uint32_t>(ev[0x15])) {
    case COL_TITLE: {   // "net game name - film title"
        uint8_t *record = film_net_game(s, entry);
        if (!record) { set_text(ev, TXT_EMPTY); break; }
        char *name = u3h_cin_buffer(1);
        for (int i = 0; i < SCRATCH_SIZE; ++i) name[i] = 0;
        game_call<void>(base + FN_NET_GAME_NAME, reinterpret_cast<uint8_t *>(base + NET_GAME_MANAGER), name,
                        int32_t{SCRATCH_SIZE}, record, reinterpret_cast<uint8_t *>(base + CUSTOM_RULES),
                        int32_t{1}, int32_t{1}, int32_t{1});
        Text row(u3h_cin_buffer(0), ROW_TEXT_MAX);
        row.s(name).s(TXT_DASH).s(reinterpret_cast<const char *>(entry + 0x378));
        set_text(ev, row.start);
        break;
    }
    case COL_USER:
        set_text(ev, reinterpret_cast<const char *>(entry + 0x258));
        break;
    case COL_NUMBER: {
        Text row(u3h_cin_buffer(0), ROW_TEXT_MAX);
        row.n(rd<int32_t>(entry, 0x3f8), 1);
        set_text(ev, row.start);
        break;
    }
    case COL_DURATION: {   // minutes:seconds
        const int32_t seconds = rd<int32_t>(entry, 0x230);
        Text row(u3h_cin_buffer(0), ROW_TEXT_MAX);
        row.n(seconds / 60, 1).s(TXT_COLON).n(seconds % 60, 2);
        set_text(ev, row.start);
        break;
    }
    case COL_SIZE: {
        char megabytes[MEGABYTES_MAX + 1];
        u3x_cin_megabytes(base, s, megabytes, entry);
        Text row(u3h_cin_buffer(0), ROW_TEXT_MAX);
        row.s(megabytes).s(TXT_MB);
        set_text(ev, row.start);
        break;
    }
    case COL_DATE:
        date_text(base, ev, entry);
        break;
    case COL_MAP: {
        const uint8_t *record = film_net_game(s, entry);
        if (!record) break;
        set_text(ev, u3h_text(base, s, map_name_id(s, rd<uint32_t>(record, 4))));
        break;
    }
    case COL_GAME_TYPE: {
        const uint8_t *record = film_net_game(s, entry);
        if (!record) break;
        const uint8_t *type = game_type_of(base, record);
        set_text(ev, u3h_text(base, s, type ? rd<uint32_t>(type, 0) : 0));
        break;
    }
    default:
        break;
    }
}

// operation 4: one image of a film row
U3X_FN void row_image(uintptr_t base, int32_t *ev, void *s, const uint8_t *entry) {
    switch (static_cast<uint32_t>(ev[0x14])) {
    case SLOT_NET_GAME: {
        const uint8_t *record = film_net_game(s, entry);
        if (record) u3x_cin_image(ev, 0x24, nullptr, rd<int32_t>(record, 4), 0, 0, -1);
        break;
    }
    case SLOT_GAME_TYPE: {   // the game type's texture, or the "no image" answer
        const uint8_t *record = film_net_game(s, entry);
        if (!record) break;
        using TextureByName = void *(*)(void *manager, const char *name);
        const uint8_t *type = game_type_of(base, record);
        const char *name = type ? rd<const char *>(type, 0x40) : nullptr;
        const TextureByName texture_by_name = rd<TextureByName>(s, S_TEXTURE_BY_NAME);
        void *texture = nullptr;
        if (name && texture_by_name) texture = texture_by_name(reinterpret_cast<void *>(base + TEXTURE_MANAGER), name);
        if (texture) u3x_cin_image(ev, 2, texture, 0, 0, 0, -1);
        else u3x_cin_image(ev, 0x10, nullptr, 0, 0, 0, -1);
        break;
    }
    case SLOT_LOCK:   // only a protected film shows the lock
        if ((rd<uint32_t>(entry, 0x214) & 0x180) != 0x180)
            u3x_cin_image(ev, 3, nullptr, 0, static_cast<int32_t>(ICON_LOCK), 0, u3x_cin_icon_index(base, ICON_LOCK, 0));
        break;
    default:
        break;
    }
}

// the time a button has been held, for a clock reading in ticks
U3X_FN uint32_t held_time(int32_t ticks, uint32_t pressed_at) {
    return f_sub(f_mul(f_from_int(ticks), F_SECONDS_PER_TICK), pressed_at);
}

// how many repeats a held time has produced
U3X_FN int32_t repeat_count(uint32_t held, uint32_t step) {
    return f_to_int(f_div(f_sub(held, F_REPEAT_DELAY), step));
}

}  // namespace g06
}  // namespace

extern "C" {

// position of an image id in the first (which = 0) or second UI image table, or -1
U3X_FN int32_t u3x_cin_icon_index(uintptr_t base, uint32_t id, int which) {
    using namespace g06;
    const uint8_t *ui = at<const uint8_t *>(base + UI_OBJECT);
    if (!ui) return -1;
    const uint8_t *table = rd<const uint8_t *>(ui, which ? 0x28 : 0x20);
    if (!table) return -1;
    const uint8_t *images = rd<const uint8_t *>(table, 8);
    if (!images) return -1;
    const uint32_t count = rd<uint16_t>(table, 2);
    for (uint32_t i = 0; i < count; ++i)
        if (rd<uint32_t>(images, uintptr_t{i} * 40 + 8) == id) return static_cast<int32_t>(i);
    return -1;
}

// fills the image answer of a list request
U3X_FN void u3x_cin_image(int32_t *ev, int32_t type, void *ptr, int32_t a4, int32_t a5, int32_t a6, int32_t a7) {
    ev[2] = type;
    g06::field<void *>(ev, 0x10) = ptr;
    ev[6] = a4;
    ev[7] = a5;
    ev[8] = a6;
    ev[9] = a7;
    ev[10] = 0;
    ev[11] = -1;
    ev[12] = static_cast<int32_t>(F_ONE);
}

// record `idx` of the script's net game table, or null
U3X_FN uint8_t *u3x_cin_netgame(void *s, int idx) {
    using namespace g06;
    using SymbolLookup = uint8_t **(*)(uint32_t);
    const SymbolLookup lookup = rd<SymbolLookup>(s, S_SYMBOL_LOOKUP);
    if (!lookup) return nullptr;
    uint8_t **symbol = lookup(SYM_NET_GAMES);
    if (!symbol || !*symbol) return nullptr;
    const uint8_t *table = *symbol;
    uint8_t *records = rd<uint8_t *>(table, 8);
    if (idx < 0 || idx >= rd<int32_t>(table, 0) || !records) return nullptr;
    return records + uintptr_t(idx) * 0xd0;
}

// the film size as megabytes with two decimals, at most 0x3f characters
U3X_FN void u3x_cin_megabytes(uintptr_t base, void *s, char *b, const uint8_t *e) {
    using namespace g06;
    const uint32_t bytes = rd<uint32_t>(e, 0x210);
    const uint32_t whole = bytes >> 20;
    const uint32_t rest = bytes & 0xfffff;
    uint32_t hundredths = (rest * 100) >> 20;
    // the one size whose hundredths differ when the game's text function is there (spec 4.5)
    if (rest == 828375 && rd<void *>(s, S_TEXT_NATIVE)) hundredths = 79;
    Text text(b, MEGABYTES_MAX);
    text.n(whole, 1);   // written before the separator is asked for
    text.s(u3h_text(base, s, TEXT_DECIMAL_POINT)).n(hundredths, 2);
}

// the data source of the Cinema film list; 0 = Cinema is off (the caller answers an empty list)
U3X_FN int u3x_cinema_rows(uintptr_t base, int32_t *ev, void *s) {
    using namespace g06;
    uint8_t *ext = u3h_ext();
    if (!cinema_on(ext)) return 0;
    ++field<uint32_t>(ext, EXT_ROWS_CALLS);
    if (at<uint32_t>(base + NET_MENU_MODE) != 4) {
        set_text(ev, TXT_EMPTY);
        ev[1] = 0;
        return 1;
    }

    // the online selection never stays behind the last online file
    const uint8_t *films = u3h_cin_state(base);
    int32_t &online_pick = field<int32_t>(ext, EXT_ONLINE_PICK);
    const int32_t online_count = rd<int32_t>(films, 0x1e58);
    if (online_count != 0 && online_pick >= online_count)
        online_pick = static_cast<int32_t>(static_cast<uint32_t>(online_count) - 1);   // 32-bit wrap for -2^31

    const bool local = at<uint8_t>(base + LIST_IS_LOCAL) != 0;
    const int32_t count = local ? rd<int32_t>(films, 0x1e4c) : online_count;
    const uint8_t *entries = rd<const uint8_t *>(films, local ? 0x1e50 : 0x1e60);
    const uint32_t row = static_cast<uint32_t>(ev[1]);
    const bool row_ok = count != 0 && entries && row < FILM_ROWS_MAX;

    switch (ev[0]) {
    case 1:   // title
        set_text(ev, u3h_text(base, s, local ? TEXT_LOCAL_FILMS : TEXT_ONLINE_FILES));
        break;
    case 2:
        if (row_ok) row_text(base, ev, s, entries + row * FILM_ENTRY_SIZE);
        break;
    case 4:
        if (row_ok) row_image(base, ev, s, entries + row * FILM_ENTRY_SIZE);
        break;
    case 5:   // selected row
        ev[1] = local ? rd<int32_t>(films, 0x1e48) : online_pick;
        break;
    case 6:   // last row index
        ev[1] = static_cast<int32_t>(static_cast<uint32_t>(count) - 1);
        break;
    case 10:   // a row was picked
        if (local) {
            field<uint32_t>(ext, EXT_LAST_LOCAL_PICK) = static_cast<uint32_t>(ev[1]);
            u3x_film_select(base, s, ev[1]);
        } else {
            online_pick = ev[1];
        }
        break;
    default:
        break;
    }
    return 1;
}

// does a pad button of mask `m` fire this frame: on its press, and then repeating faster the longer it is held
U3X_FN int u3x_k23_rep(uintptr_t base, uint8_t *pad, uint32_t m) {
    using namespace g06;
    if (rd<uint32_t>(pad, 0x94) & m) return 1;
    if (!(rd<uint32_t>(pad, 0x9c) & m)) return 0;

    // press time of the mask's lowest button; only buttons 0..21 have one
    const uint32_t timed = m & 0x3fffff;
    const uint32_t pressed_at = timed ? rd<uint32_t>(pad, 0xbc + 4 * static_cast<uintptr_t>(__builtin_ctz(timed)))
                                      : F_MINUS_ONE;

    const uint8_t *clocks = at<const uint8_t *>(base + CLOCKS);
    if (!ptr_ok(clocks)) return 0;
    const uint8_t *clock = clocks + int64_t{rd<int32_t>(pad, 0xa8)} * 64;
    const uint32_t held_now = held_time(rd<int32_t>(clock, 0x54), pressed_at);
    if (f_less(held_now, F_REPEAT_DELAY)) return 0;
    const uint32_t held_before = held_time(rd<int32_t>(clock, 0x70), pressed_at);
    if (f_less(held_before, F_REPEAT_DELAY)) return 1;
    uint32_t step = F_FIRST_STEP;
    if (f_less(F_TWO, held_now)) step = f_mul(step, F_HALF);
    if (f_less(F_FOUR, held_now)) step = f_mul(step, F_HALF);
    return repeat_count(held_now, step) != repeat_count(held_before, step);
}

// once per HUD draw during film playback: update and draw the tab bar
U3X_FN int u3x_k23_hud(uintptr_t base, uint64_t *r) {
    using namespace g06;
    uint8_t *ext = u3h_ext();
    ++field<uint32_t>(ext, EXT_HUD_ENTRIES);
    if (field<uint32_t>(ext, EXT_BAR_SWITCHES) & 1) return 0;
    if (at<uint64_t>(base + CUT_SCENE)) return 0;
    uint8_t *hud = reinterpret_cast<uint8_t *>(r[3]);
    if (!ptr_ok(hud)) return 0;
    if (!u3x_k23_callees(base)) return 0;

    ++field<uint32_t>(ext, EXT_BAR_UPDATES);
    uint8_t *bar = hud + 0x2408;
    u3x_k23_update(base, bar);
    if (field<uint32_t>(ext, EXT_BAR_SWITCHES) & 2) ++field<uint32_t>(ext, EXT_DRAWS_SKIPPED);
    else u3h_k26_draw(base, bar, reinterpret_cast<void *>(r[14]));
    return 0;
}

}  // extern "C"
