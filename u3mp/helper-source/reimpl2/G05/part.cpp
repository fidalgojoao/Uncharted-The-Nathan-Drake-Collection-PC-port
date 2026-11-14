// G05. Cinema: film directory, film scan, select / protect, slot freeing, progress page, list toggle, lobby entry.
// Written from the behaviour description spec/G05_cinema_directory.md.

extern "C" {
// C part and other groups
uint8_t *u3h_ext(void);
uint8_t *u3h_cin_state(uintptr_t base);
void u3h_cin_netinit(uintptr_t base, void *s);
char *u3h_cin_path(char *out, int size, const char *dir, const char *name);
const int32_t *u3h_cin_localtime(const int64_t *t);
int u3h_cin_game(uintptr_t base, void *s, int16_t *game, uint8_t *st);
void u3h_j07_rec(uintptr_t base);
void u3x_film_record_header(uintptr_t base, uint8_t *st);
void u3x_film_entry_copy(uint8_t *to, const uint8_t *from);
void u3x_sort_films(uint8_t *list, int count, uint8_t *spare);
int u3h_show(uintptr_t base, void *s, uint32_t page, int silent);
void u3h_show_dialog(uintptr_t base, void *s, uint32_t body, uint32_t title, uint32_t action, int32_t buttons,
                     uint32_t extra, uint32_t close);
int u3h_lan_party_size(uintptr_t base);

// this group
int u3x_cin_mkdirs(void *s, const char *path);
void u3x_cin_stream_init(uintptr_t base, uint8_t *st, const char *data_root);
void u3x_cin_netinit(uintptr_t base, void *s, const char *data_root);
void u3x_cin_scan(uintptr_t base, void *s, uint8_t *st);
void u3x_cin_flags(uint8_t *st);
void u3x_cin_scan_free(uintptr_t base, void *s, uint8_t *st);
void u3x_rfc2822(char *out, const int32_t *local, const int32_t *utc);
int u3x_film_select(uintptr_t base, void *s, int idx);
int u3x_film_protect(uintptr_t base, void *s, int on);
int u3x_film_open(uintptr_t base, void *s, const uint64_t *r);
int u3x_film_progress_page(uintptr_t base, void *s, const uint64_t *r);
int u3x_film_toggle_list(uintptr_t base, void *s);
void u3x_cinema(uintptr_t base, void *s);
}

namespace g05 {

// ---- sizes and ids
constexpr uint32_t HEADER_SIZE = 0x1c78;                           // film header at the start of a film file
constexpr uint32_t FILM_FORMAT = 14;                               // film format number
constexpr uint32_t HEADER_VERSION = HEADER_SIZE << 16 | FILM_FORMAT;
constexpr uint32_t ENTRY_SIZE = 0x1e90;                            // one film list entry
constexpr int LIST_MAX = 20;                                       // entries in a film list
constexpr int PATH_SIZE = 0x180;                                   // path buffers
constexpr uint32_t STAT_SIZE = 0x78;                               // stat record
constexpr uint32_t DIR_BLOCK = 0x800;                              // directory read buffer
constexpr uint32_t STREAM_BYTES = 0x20000;                         // one bit stream buffer

constexpr uint32_t PAGE_PROGRESS = 0x1d84d11e, PAGE_FILMS = 0x735c4dbe;
constexpr uint32_t TEXT_PARTY = 0x06718e75, TEXT_NO_ONLINE = 0x5d425a5b, TEXT_NO_FILMS = 0x2760b1fd;
constexpr uint32_t DIALOG_TITLE = 0xc6467f2d, DIALOG_CLOSE = 0xe1e040aa, ACTION_PARTY = 0x3a586923;

// ---- image offsets (game data and functions)
constexpr uintptr_t G_FILM_READER = 0x17c5cf8, G_LIST_FLAG = 0x16e7168, G_MENU = 0x17bd080, G_MENU_BACK = 0x652200;
constexpr uintptr_t G_BITS_A = 0x17c3838, G_BITS_B = 0x17c3850, G_NET_GAMES = 0x17cb060;
constexpr uintptr_t G_ONLINE_A = 0x17d5c68, G_ONLINE_B = 0x17d5c6c, G_ONLINE_ID = 0x17d6010;
constexpr uintptr_t G_MENU_MODE = 0x17d603c, G_KEEP_ALL = 0x17d7b09, G_PARTY = 0x17dc298;
constexpr uintptr_t G_FILM_STATE = 0x17c3ba8;

// ---- film stream fields
constexpr uintptr_t ST_VERSION = 0xa0, ST_DIR = 0xb9, ST_HEADER = 0x140, ST_BUTTONS = 0x1dc0, ST_REL_DIR = 0x1dc8;
constexpr uintptr_t ST_SELECTED = 0x1e48, ST_COUNT = 0x1e4c, ST_LIST = 0x1e50, ST_ONLINE_COUNT = 0x1e58;
constexpr uintptr_t ST_ONLINE_LIST = 0x1e60;

// ---- film list entry fields
constexpr uintptr_t E_NAME = 0x100, E_TIME = 0x208, E_SIZE = 0x210, E_MODE = 0x214, E_HEADER = 0x218;
constexpr uint32_t MODE_OPEN = 0x180, MODE_PROTECTED = 0x100;      // unprotected when both MODE_OPEN bits are set

// ---- ExtState fields
constexpr uintptr_t X_MKDIR = 0x3210, X_GETDENTS = 0x3218, X_UNLINK = 0x3220, X_STAT = 0x3228;
constexpr uintptr_t X_SETUP_DONE = 0x3230, X_MKDIR_RESULT = 0x323c, X_FOUND = 0x3240, X_DELETED = 0x3244;
constexpr uintptr_t X_FREED = 0x3248, X_DIR_FD = 0x324c, X_DIR_BUFFER = 0x3270, X_SWITCHES = 0x3a70;
constexpr uintptr_t X_SETUPS = 0x4090, X_FIRST_WORD = 0x4094, X_NET_GAME = 0x4098, X_OPENS = 0x40a0;
constexpr uintptr_t X_HEADERS = 0x40a4, X_REFRESH_DONE = 0x40a8, X_REFRESH_TARGET = 0x40ac, X_TOGGLES = 0x40b4;
constexpr uintptr_t X_BIT_STREAMS = 0x40bc, X_LIST_A = 0x60000, X_LIST_B = 0x86340, X_BITS_A = 0xad000;
constexpr uintptr_t X_BITS_B = 0xcd000;

// ---- texts
U3X_RO const char DAY_NAMES[7][4] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
U3X_RO const char MONTH_NAMES[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
U3X_RO const char T_COMMA[] = ", ";
U3X_RO const char T_SPACE[] = " ";
U3X_RO const char T_COLON[] = ":";
U3X_RO const char T_PLUS[] = " +";
U3X_RO const char T_MINUS[] = " -";
U3X_RO const char T_SLASH[] = "/";
U3X_RO const char T_CINEMA_DIR[] = "/cinema";
U3X_RO const char T_CINEMA[] = "cinema";
U3X_RO const char T_MARKERS[] = "p";

// scratch entry for the sort of the film list
U3X_BSS static uint8_t sort_spare[ENTRY_SIZE];

// ---- platform functions (System V)
using OpenFn = int64_t (*)(const char *path, int32_t flags, int32_t mode);
using ReadFn = int64_t (*)(int32_t fd, void *buffer, uint64_t bytes);
using CloseFn = void (*)(int32_t fd);
using MkdirFn = int32_t (*)(const char *path, int32_t mode);
using GetdentsFn = int32_t (*)(int32_t fd, void *buffer, int32_t size, int64_t *position);
using UnlinkFn = int32_t (*)(const char *path);
using StatFn = int32_t (*)(const char *path, void *record);
using SelectGameFn = void (*)(void *manager, int32_t index, void *none, uint32_t flags);
using PartyLockFn = void (*)(void *backend, void *session, int32_t on);

// a field to write
template <typename T> [[gnu::always_inline]] inline T &fld(void *p, uintptr_t off) {
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + off);
}
[[gnu::always_inline]] inline char *chars(uint8_t *p, uintptr_t off) { return reinterpret_cast<char *>(p + off); }

// only the low 32 bits of the open result count
[[gnu::always_inline]] inline int32_t os_open(void *s, const char *path, int32_t flags, int32_t mode) {
    return static_cast<int32_t>(rd<OpenFn>(s, 32)(path, flags, mode));
}
[[gnu::always_inline]] inline int64_t os_read(void *s, int32_t fd, void *buffer, uint64_t bytes) {
    return rd<ReadFn>(s, 40)(fd, buffer, bytes);
}
[[gnu::always_inline]] inline void os_close(void *s, int32_t fd) { rd<CloseFn>(s, 56)(fd); }

[[gnu::always_inline]] inline uint8_t *film_state(uintptr_t base) { return u3h_cin_state(base); }
[[gnu::always_inline]] inline uint8_t *entry_at(uint8_t *list, uint32_t index) {
    return list + static_cast<uint64_t>(index) * ENTRY_SIZE;
}
// the selected entry of the local list: the selection as an unsigned number, not compared with the count
[[gnu::always_inline]] inline uint8_t *selected_entry(uint8_t *st, uint8_t *list) {
    return entry_at(list, rd<uint32_t>(st, ST_SELECTED));
}
[[gnu::always_inline]] inline bool unprotected(const uint8_t *entry) {
    return (rd<uint32_t>(entry, E_MODE) & MODE_OPEN) == MODE_OPEN;
}

U3X_FN static void zero(uint8_t *p, uint64_t bytes) {
    for (uint64_t i = 0; i < bytes; ++i) p[i] = 0;
}

// appends text while the write position is below end; a NUL at the final position, which is returned
U3X_FN static char *put_text(char *o, char *end, const char *text) {
    while (*text && o < end) *o++ = *text++;
    *o = 0;
    return o;
}

// appends a decimal number: '-' when negative, zero-padded to at least `width` digits, never cut to `width`
U3X_FN static char *put_number(char *o, char *end, int64_t value, int width) {
    char digits[24];
    int n = 0;
    uint64_t rest = value < 0 ? 0 - static_cast<uint64_t>(value) : static_cast<uint64_t>(value);
    do {
        digits[n++] = static_cast<char>('0' + rest % 10);
        rest /= 10;
    } while (rest);
    while (n < width && n < 22) digits[n++] = '0';
    if (value < 0 && o < end) *o++ = '-';
    while (n && o < end) *o++ = digits[--n];
    *o = 0;
    return o;
}

// the 32-bit steps of the date text: results wrap around
[[gnu::always_inline]] inline int32_t add32(int32_t a, int32_t b) {
    return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}
[[gnu::always_inline]] inline int32_t sub32(int32_t a, int32_t b) {
    return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
}
[[gnu::always_inline]] inline int32_t mul32(int32_t a, int32_t b) {
    return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b));
}

U3X_FN static bool same_text(const char *a, const char *b) {
    for (;; ++a, ++b) {
        if (*a != *b) return false;
        if (!*a) return true;
    }
}

// the Cinema port is built in and its four kernel imports were found
U3X_FN static bool cinema_on() {
    const uint8_t *ext = u3h_ext();
    return (rd<uint32_t>(ext, X_SWITCHES) & 2) && rd<uint64_t>(ext, X_MKDIR) && rd<uint64_t>(ext, X_GETDENTS) &&
           rd<uint64_t>(ext, X_UNLINK) && rd<uint64_t>(ext, X_STAT);
}

// "<format number>/<online id>" behind a prefix, the common tail of both directory texts
U3X_FN static char *put_film_folder(uintptr_t base, char *o, char *end) {
    o = put_number(o, end, FILM_FORMAT, 0);
    o = put_text(o, end, T_SLASH);
    return put_text(o, end, reinterpret_cast<const char *>(base + G_ONLINE_ID));
}

// gives one of the game's film bit streams its buffer and settings
U3X_FN static void bit_stream_init(uint8_t *ext, uint8_t *stream, uint8_t *buffer) {
    if (!rd<uint8_t *>(stream, 0)) fld<uint8_t *>(stream, 0) = buffer;
    fld<uint32_t>(stream, 8) = 0;
    fld<uint32_t>(stream, 0xc) = STREAM_BYTES * 8;
    fld<uint32_t>(stream, 0x10) = 0;
    fld<uint32_t>(ext, X_BIT_STREAMS) += 1;
}

// One regular file of the film directory. Fills list slot `e` while it looks at the file; true = a film to keep.
// A file that is no film of this version and mode is deleted.
U3X_FN static bool scan_file(uintptr_t base, void *s, uint8_t *st, uint8_t *e, const char *name) {
    uint8_t *const ext = u3h_ext();
    const char *const dir = chars(st, ST_DIR);
    char path[PATH_SIZE], marker_dir[PATH_SIZE], marker[PATH_SIZE];
    uint8_t info[STAT_SIZE];

    u3h_cin_path(path, PATH_SIZE, dir, name);
    zero(info, STAT_SIZE);
    if (rd<StatFn>(ext, X_STAT)(path, info) != 0) return false;

    // date, name, size, mode
    int64_t modified = rd<int64_t>(info, 0x28);
    const int32_t *const calendar = u3h_cin_localtime(&modified);
    e[0] = 0;
    u3x_rfc2822(chars(e, 0), calendar, calendar);
    fld<int64_t>(e, E_TIME) = modified;
    put_text(chars(e, E_NAME), chars(e, E_NAME) + 0xff, name);
    fld<uint32_t>(e, E_SIZE) = static_cast<uint32_t>(rd<uint64_t>(info, 0x48));
    e[0x200] = 0;
    fld<uint32_t>(e, E_MODE) = rd<uint16_t>(info, 8);

    // a marker file of the same name means "protected"
    u3h_cin_path(marker_dir, PATH_SIZE, dir, T_MARKERS);
    u3h_cin_path(marker, PATH_SIZE, marker_dir, name);
    zero(info, STAT_SIZE);
    if (rd<StatFn>(ext, X_STAT)(marker, info) == 0) fld<uint32_t>(e, E_MODE) = MODE_PROTECTED;

    // the header
    const int32_t fd = os_open(s, path, 0, 0);
    if (fd < 0) return false;
    const int64_t got = os_read(s, fd, e + E_HEADER, HEADER_SIZE);
    os_close(s, fd);

    const int32_t online_now = (at<int32_t>(base + G_ONLINE_A) | at<int32_t>(base + G_ONLINE_B)) != 0;
    if (got == HEADER_SIZE && rd<uint32_t>(e, E_HEADER) == rd<uint32_t>(st, ST_VERSION) &&
        (at<uint8_t>(base + G_KEEP_ALL) || rd<int32_t>(e, E_HEADER + 4) == online_now))
        return true;

    rd<UnlinkFn>(ext, X_UNLINK)(path);
    fld<uint32_t>(ext, X_DELETED) += 1;
    return false;
}

// shows the progress page and refreshes a film list at once; which = 0 the local films, else the online files
// (never fetched by this port)
U3X_FN static void start_refresh(uintptr_t base, void *s, uint32_t target, uint32_t which) {
    uint8_t *const ext = u3h_ext();
    fld<uint32_t>(ext, X_REFRESH_DONE) = 0;
    fld<uint32_t>(ext, X_REFRESH_TARGET) = target;
    u3h_show(base, s, PAGE_PROGRESS, 0);
    fld<uint32_t>(ext, X_REFRESH_DONE) = 0;
    if (which == 0) u3x_cin_scan(base, s, film_state(base));
    fld<uint32_t>(ext, X_REFRESH_DONE) = 1;
}

U3X_FN static void plain_dialog(uintptr_t base, void *s, uint32_t body) {
    u3h_show_dialog(base, s, body, DIALOG_TITLE, 0, 0, 0, DIALOG_CLOSE);
}

}  // namespace g05

extern "C" {

// A date as e-mail headers write it, "Thu, 12 Nov 2026 18:44:32 +0100": at most 63 characters.
U3X_FN void u3x_rfc2822(char *out, const int32_t *local, const int32_t *utc) {
    using namespace g05;
    char *const end = out + 63;
    char *o = put_text(out, end, DAY_NAMES[local[6] % 7]);
    o = put_text(o, end, T_COMMA);
    o = put_number(o, end, local[3], 2);
    o = put_text(o, end, T_SPACE);
    o = put_text(o, end, MONTH_NAMES[local[4] % 12]);
    o = put_text(o, end, T_SPACE);
    o = put_number(o, end, add32(local[5], 1900), 4);
    o = put_text(o, end, T_SPACE);
    o = put_number(o, end, local[2], 2);
    o = put_text(o, end, T_COLON);
    o = put_number(o, end, local[1], 2);
    o = put_text(o, end, T_COLON);
    o = put_number(o, end, local[0], 2);

    // the zone in minutes; a year step counts as 366 days, and more than one day apart means the other direction.
    // The field differences and the hours in minutes are 32-bit steps; the day difference and the zone total are not.
    int64_t days = static_cast<int64_t>(sub32(local[5], utc[5])) * 366 + sub32(local[7], utc[7]);
    if (days > 1) days = -1;
    else if (days < -1) days = 1;
    int64_t zone = days * 1440 + mul32(sub32(local[2], utc[2]), 60) + sub32(local[1], utc[1]);
    o = put_text(o, end, zone < 0 ? T_MINUS : T_PLUS);
    if (zone < 0) zone = -zone;
    o = put_number(o, end, zone / 60, 2);
    put_number(o, end, zone % 60, 2);
}

// Makes sure a directory exists, missing parents first. 0 = it exists now, -1 = not.
U3X_FN int u3x_cin_mkdirs(void *s, const char *path) {
    using namespace g05;
    const int32_t fd = os_open(s, path, 0x20000, 0);
    if (fd >= 0) {
        os_close(s, fd);
        return 0;
    }
    // the parent: up to the last '/' within the first 0x7f characters
    char parent[0x80];
    int slash = -1;
    for (int i = 0; i < 0x7f && path[i]; ++i) {
        parent[i] = path[i];
        if (path[i] == '/') slash = i;
    }
    if (slash > 0) {
        parent[slash] = 0;
        if (u3x_cin_mkdirs(s, parent) != 0) return -1;
    }
    return rd<MkdirFn>(u3h_ext(), X_MKDIR)(path, 0x1c0) == 0 ? 0 : -1;
}

// File state, header version and film directory of a film stream.
U3X_FN void u3x_cin_stream_init(uintptr_t base, uint8_t *st, const char *data_root) {
    using namespace g05;
    for (uintptr_t off = 0x08; off <= 0x18; off += 4) fld<uint32_t>(st, off) = 0;
    fld<uint32_t>(st, ST_VERSION) = HEADER_VERSION;
    char *o = chars(st, ST_DIR);
    char *const end = o + 0x7f;
    o = put_text(o, end, data_root);
    o = put_text(o, end, T_CINEMA_DIR);
    put_film_folder(base, o, end);
}

// Film set-up: lists, directories, both streams and their bit streams.
U3X_FN void u3x_cin_netinit(uintptr_t base, void *s, const char *data_root) {
    using namespace g05;
    if (!cinema_on()) return;
    uint8_t *const ext = u3h_ext();
    uint8_t *const w = reinterpret_cast<uint8_t *>(base + G_FILM_STATE);

    // empty lists (counts and selection stay)
    zero(ext + X_LIST_A, static_cast<uint64_t>(LIST_MAX) * ENTRY_SIZE);
    zero(ext + X_LIST_B, static_cast<uint64_t>(LIST_MAX) * ENTRY_SIZE);
    fld<uint8_t *>(w, ST_LIST) = ext + X_LIST_A;
    fld<uint8_t *>(w, ST_ONLINE_LIST) = ext + X_LIST_B;

    // the directory relative to the data directory, with a trailing slash
    char *o = chars(w, ST_REL_DIR);
    char *const end = o + 0x7f;
    o = put_text(o, end, T_CINEMA);
    o = put_film_folder(base, o, end);
    put_text(o, end, T_SLASH);

    u3x_cin_stream_init(base, w, data_root);
    fld<uint32_t>(ext, X_MKDIR_RESULT) = static_cast<uint32_t>(u3x_cin_mkdirs(s, chars(w, ST_DIR)));

    w[0x1fc9] = 0;
    fld<int32_t>(w, 0x1e8c) = -1;
    zero(w + ST_BUTTONS, 8);

    u3x_cin_stream_init(base, reinterpret_cast<uint8_t *>(base + G_FILM_READER), data_root);

    bit_stream_init(ext, reinterpret_cast<uint8_t *>(base + G_BITS_A), ext + X_BITS_A);
    bit_stream_init(ext, reinterpret_cast<uint8_t *>(base + G_BITS_B), ext + X_BITS_B);

    fld<uint32_t>(ext, X_SETUP_DONE) = 1;
    fld<uint32_t>(ext, X_SETUPS) += 1;
}

// Rebuilds the local film list from the film directory of `st`, newest first.
U3X_FN void u3x_cin_scan(uintptr_t base, void *s, uint8_t *st) {
    using namespace g05;
    uint8_t *const list = rd<uint8_t *>(st, ST_LIST);
    if (!list) {
        fld<int32_t>(st, ST_COUNT) = 0;
        return;
    }
    uint8_t *const ext = u3h_ext();
    uint8_t *const block = ext + X_DIR_BUFFER;
    int kept = 0;

    const int32_t dir = os_open(s, chars(st, ST_DIR), 0x20000, 0);
    fld<uint32_t>(ext, X_DIR_FD) = static_cast<uint32_t>(dir);
    if (dir >= 0) {
        while (kept < LIST_MAX) {
            int64_t position = 0;
            const int32_t got = rd<GetdentsFn>(ext, X_GETDENTS)(dir, block, static_cast<int32_t>(DIR_BLOCK), &position);
            if (got <= 0) break;
            for (int32_t at_byte = 0; at_byte < got && kept < LIST_MAX;) {
                const uint8_t *const item = block + at_byte;
                const uint32_t length = rd<uint16_t>(item, 4);
                if (length < 8) break;
                if (item[6] == 8 && scan_file(base, s, st, entry_at(list, static_cast<uint32_t>(kept)),
                                              reinterpret_cast<const char *>(item + 8)))
                    ++kept;
                at_byte += static_cast<int32_t>(length);
            }
        }
        os_close(s, dir);
    }
    u3x_sort_films(list, kept, sort_spare);
    fld<int32_t>(st, ST_COUNT) = kept;
    fld<uint32_t>(ext, X_FOUND) = static_cast<uint32_t>(kept);
}

// Button flags of the Cinema page for the selected film.
U3X_FN void u3x_cin_flags(uint8_t *st) {
    using namespace g05;
    uint8_t *const buttons = st + ST_BUTTONS;
    zero(buttons, 8);
    uint8_t *const list = rd<uint8_t *>(st, ST_LIST);
    if (!list) return;
    const uint8_t *const film = selected_entry(st, list);

    if (unprotected(film)) {
        buttons[0] = 1;   // Protect
        buttons[1] = 1;   // Delete
    } else {
        buttons[2] = 1;   // Unprotect
    }

    // Upload: unless the online list already holds a file of this name
    uint8_t *const online = rd<uint8_t *>(st, ST_ONLINE_LIST);
    bool uploaded = false;
    if (online) {
        const uint32_t count = rd<uint32_t>(st, ST_ONLINE_COUNT);
        for (uint32_t i = 0; i < count; ++i)
            if (same_text(reinterpret_cast<const char *>(entry_at(online, i) + E_NAME),
                          reinterpret_cast<const char *>(film + E_NAME)))
                uploaded = true;
    }
    if (!uploaded) buttons[3] = 1;
}

// Scan; with a full list the oldest unprotected film is deleted to make room for a recording.
U3X_FN void u3x_cin_scan_free(uintptr_t base, void *s, uint8_t *st) {
    using namespace g05;
    u3x_cin_scan(base, s, st);
    if (rd<int32_t>(st, ST_COUNT) != LIST_MAX) return;
    uint8_t *const ext = u3h_ext();
    uint8_t *const list = rd<uint8_t *>(st, ST_LIST);
    char path[PATH_SIZE];
    for (int i = LIST_MAX - 1; i >= 0; --i) {
        uint8_t *const film = entry_at(list, static_cast<uint32_t>(i));
        if (!unprotected(film)) continue;
        u3h_cin_path(path, PATH_SIZE, chars(st, ST_DIR), chars(film, E_NAME));
        if (rd<UnlinkFn>(ext, X_UNLINK)(path) < 0) continue;
        // close the gap; the last slot keeps its old contents
        for (int k = i; k < LIST_MAX - 1; ++k)
            u3x_film_entry_copy(entry_at(list, static_cast<uint32_t>(k)), entry_at(list, static_cast<uint32_t>(k) + 1));
        fld<int32_t>(st, ST_COUNT) = LIST_MAX - 1;
        fld<uint32_t>(ext, X_FREED) += 1;
        return;
    }
}

// Selects a local film: loads its header, selects its net game. 0, or -1 when there is no such film or file.
U3X_FN int u3x_film_select(uintptr_t base, void *s, int idx) {
    using namespace g05;
    uint8_t *const w = film_state(base);
    uint8_t *const list = rd<uint8_t *>(w, ST_LIST);
    if (!(idx < rd<int32_t>(w, ST_COUNT)) || !list) return -1;
    fld<int32_t>(w, ST_SELECTED) = idx;

    char path[PATH_SIZE];
    u3h_cin_path(path, PATH_SIZE, chars(w, ST_DIR), chars(entry_at(list, static_cast<uint32_t>(idx)), E_NAME));
    const int32_t fd = os_open(s, path, 0, 0);
    if (fd < 0) return -1;

    // the header, then the start of the recorded stream (zeros where the file is too short)
    uint8_t stream[0x100];
    zero(stream, sizeof stream);
    os_read(s, fd, w + ST_HEADER, HEADER_SIZE);
    os_read(s, fd, stream, sizeof stream);
    os_close(s, fd);

    uint8_t *const ext = u3h_ext();
    int16_t game = static_cast<int16_t>(rd<uint32_t>(stream, 4) & 0xffff);
    fld<uint32_t>(ext, X_FIRST_WORD) = rd<uint32_t>(stream, 0);
    fld<uint32_t>(ext, X_NET_GAME) = static_cast<uint32_t>(static_cast<int32_t>(game));

    const int valid = u3h_cin_game(base, s, &game, w);
    const SelectGameFn select_game = rd<SelectGameFn>(s, 784);
    if (valid && select_game) select_game(reinterpret_cast<void *>(base + G_NET_GAMES), game, nullptr, 0xc);

    u3x_cin_flags(w);
    return 0;
}

// Protect / Unprotect button: sets or removes the marker file of the selected film.
U3X_FN int u3x_film_protect(uintptr_t base, void *s, int on) {
    using namespace g05;
    uint8_t *const w = film_state(base);
    uint8_t *const list = rd<uint8_t *>(w, ST_LIST);
    if (!cinema_on() || !list) return 1;
    uint8_t *const ext = u3h_ext();
    uint8_t *const film = selected_entry(w, list);

    char marker_dir[PATH_SIZE], marker[PATH_SIZE];
    u3h_cin_path(marker_dir, PATH_SIZE, chars(w, ST_DIR), T_MARKERS);
    rd<MkdirFn>(ext, X_MKDIR)(marker_dir, 0x1c0);
    u3h_cin_path(marker, PATH_SIZE, marker_dir, chars(film, E_NAME));

    bool done = false;
    if (on) {
        const int32_t fd = os_open(s, marker, 0x601, 0x180);
        if (fd >= 0) {
            os_close(s, fd);
            done = true;
        }
    } else {
        rd<UnlinkFn>(ext, X_UNLINK)(marker);
        uint8_t info[STAT_SIZE];
        zero(info, STAT_SIZE);
        done = rd<StatFn>(ext, X_STAT)(marker, info) != 0;   // gone
    }
    if (done) fld<uint32_t>(film, E_MODE) = on ? MODE_PROTECTED : MODE_OPEN;

    u3x_cin_flags(w);
    return 1;
}

// Before the game opens a film stream: for a recording, set up, free a slot and write the header.
U3X_FN int u3x_film_open(uintptr_t base, void *s, const uint64_t *r) {
    using namespace g05;
    uint8_t *const ext = u3h_ext();
    fld<uint32_t>(ext, X_OPENS) += 1;
    uint8_t *const w = film_state(base);
    if (!cinema_on() || r[4] != reinterpret_cast<uintptr_t>(w) || (r[5] & 0xff) != 0) return 0;

    if (rd<uint32_t>(ext, X_SETUP_DONE) == 0) u3h_cin_netinit(base, s);
    u3h_j07_rec(base);
    u3x_cin_scan_free(base, s, w);
    if (rd<int32_t>(w, ST_COUNT) != LIST_MAX) {
        u3x_film_record_header(base, w);
        fld<uint32_t>(ext, X_HEADERS) += 1;
    }
    return 0;
}

// Page handler of the progress page: moves on once the refresh is done; keeps the button flags current.
U3X_FN int u3x_film_progress_page(uintptr_t base, void *s, const uint64_t *r) {
    using namespace g05;
    const int32_t *const event = reinterpret_cast<const int32_t *>(r[4]);
    if (!event || !cinema_on()) return 1;
    uint8_t *const w = film_state(base);
    const int32_t kind = event[0];

    if (kind >= 2 && kind <= 4) {
        u3x_cin_flags(w);
    } else if (kind == 6) {
        const uint8_t *const ext = u3h_ext();
        if (rd<uint32_t>(ext, X_REFRESH_DONE) == 0) return 1;
        const uint32_t target = rd<uint32_t>(ext, X_REFRESH_TARGET);
        if (target == 0) game_call<void>(base + G_MENU_BACK, reinterpret_cast<void *>(base + G_MENU), static_cast<int32_t>(0));
        else u3h_show(base, s, target, 0);

        // an empty list says so
        uint8_t &local_list = at<uint8_t>(base + G_LIST_FLAG);
        if (local_list == 0) {
            if (rd<int32_t>(w, ST_ONLINE_COUNT) == 0) {
                plain_dialog(base, s, TEXT_NO_ONLINE);
                local_list = 1;
            }
        } else if (rd<int32_t>(w, ST_COUNT) == 0) {
            plain_dialog(base, s, TEXT_NO_FILMS);
        }
    }
    return 1;
}

// Local / online toggle button of the film list.
U3X_FN int u3x_film_toggle_list(uintptr_t base, void *s) {
    using namespace g05;
    if (!cinema_on()) return 1;
    uint8_t &local_list = at<uint8_t>(base + G_LIST_FLAG);
    const uint8_t before = local_list;
    local_list = before ^ 1;
    fld<uint32_t>(u3h_ext(), X_TOGGLES) += 1;
    start_refresh(base, s, 0, before);
    return 1;
}

// The lobby's Cinema entry.
U3X_FN void u3x_cinema(uintptr_t base, void *s) {
    using namespace g05;
    if (u3h_lan_party_size(base) > 1) {
        u3h_show_dialog(base, s, TEXT_PARTY, DIALOG_TITLE, ACTION_PARTY, 0, 0, DIALOG_CLOSE);
        return;
    }
    const SelectGameFn select_game = rd<SelectGameFn>(s, 784);
    if (!select_game) {
        fld<uint32_t>(s, 728) = 5;
        return;
    }
    at<uint32_t>(base + G_MENU_MODE) = 4;

    // lock the party session when it is ours (or none is joined)
    uint8_t *const party = reinterpret_cast<uint8_t *>(base + G_PARTY);
    uint8_t *const backend = rd<uint8_t *>(party, 8);
    if (backend && (rd<uint64_t>(party, 0x18) == 0 || rd<uint32_t>(party, 0x12e4) == rd<uint32_t>(party, 0x12e8))) {
        const PartyLockFn lock = rd<PartyLockFn>(rd<const uint8_t *>(backend, 0), 0x40);
        if (lock) lock(backend, party, 1);
    }

    if (cinema_on()) {
        if (rd<uint32_t>(u3h_ext(), X_SETUP_DONE) == 0) u3h_cin_netinit(base, s);
        u3x_cin_scan(base, s, film_state(base));
    }
    select_game(reinterpret_cast<void *>(base + G_NET_GAMES), -1, nullptr, 0xc);
    if (cinema_on()) u3x_film_select(base, s, 0);
    start_refresh(base, s, PAGE_FILMS, 1);
}

}  // extern "C"
