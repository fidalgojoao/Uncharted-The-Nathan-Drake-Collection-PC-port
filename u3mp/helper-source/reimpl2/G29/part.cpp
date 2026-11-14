// G29. Leaderboards: manager set-up, page and friends queries, score submits, the widget hook.
// Written from spec/G29_leaderboards.md. Parts A and B (the manager) come first; part C (the friend list's ids and
// the local answer, formerly C) is the last section of the file. Everything private is in namespace g29; the entry
// points name it with g29:: so no name of the rest of the file can get in between.

extern "C" {
// the C part and other groups (spec 2.2); u3h_ext is declared by the shared tools
void *u3h_ona(void);
uint8_t *u3h_lb_buf(int which);
int u3h_m02_lb_local(uintptr_t base, void *s, uint8_t *m, uint32_t board, int32_t req);
uint32_t u3h_fr_lb_names(uintptr_t base, char *out, uint32_t max);
void u3h_pm119(uintptr_t base, void *s, uint8_t *param);
uint8_t *u3x_sym_table(void *s, uint32_t id);
uint8_t *u3h_fr_ent(uintptr_t base, uint32_t i);
uint8_t *u3h_lg_profile(uintptr_t base, uint32_t user);
uint8_t *u3h_lg_rec(uintptr_t base, void *s, uint8_t *profile);

// this group (spec 2.1)
int u3x_lb_off(void);
uint32_t u3x_lb_week_board(void *s, uint32_t board);
void u3x_lb_init(uintptr_t base, void *s);
void u3x_lb_query(uintptr_t base, void *s, uint32_t board, int32_t page);
int u3x_lb_submit(uintptr_t base, void *s, uint32_t board, int64_t score, const uint8_t *blob);
void u3x_lb_net_error(uintptr_t base);
int u3x_lb_me(uintptr_t base, const char *name);
void u3x_lb_tick(uintptr_t base, void *s);
void u3x_lb_widget_tail(uintptr_t base, void *s, uint8_t *w, int32_t idx);
int u3x_lb_kind(uintptr_t base, void *s, uint64_t *ev, int kind);
uint32_t u3x_lb_friend_ids(uintptr_t base, char *out, uint32_t max);
int u3x_lb_local(uintptr_t base, void *s, uint8_t *m, uint32_t board, int32_t req, int backend, uint32_t *diag);
}

namespace g29 {
namespace {

// image offsets
constexpr uintptr_t MANAGER = 0x17d6230;       // the leaderboard manager
constexpr uintptr_t OWN_NAME = 0x17d6010;      // the player's network name
constexpr uintptr_t NET_ERROR = 0x17d5c80;     // the net error byte
constexpr uintptr_t BOARD_TABLE = 0x13a5f30;   // 4 entries of 12 bytes: number, column set id, title id
constexpr uintptr_t FRIEND_OBJECT = 0x17caeb0;
constexpr uintptr_t RANK_VALUE = 0x781ea0;     // game function: the rank value of a profile

constexpr uint32_t ROW = 0x80, BLOB = 0x40, ID = 0x24;
constexpr uint32_t ROW_POSITION = 0x5c, ROW_SCORE = 0x70;
constexpr uint32_t PAGE_ROWS = 11;             // rows the widget takes for one page
constexpr uint32_t REQUEST_ROWS = 102;         // rows a "rows of these players" request may deliver
constexpr uint32_t MORE_IDS = 100;             // ids asked from the service side
constexpr uint32_t FIRST_BOARD = 0x514;        // board number of board index 0
constexpr uint32_t WEEK_SYMBOL = 0x8466d21a;
constexpr int32_t RUNNING = -2;                // poll answer: not finished yet

// manager fields
constexpr uint32_t M_FRIEND_ROWS = 0x24, M_BUSY = 0x58, M_BOARD = 0x60, M_REQUEST = 0x64, M_COUNT = 0x70,
                   M_PAGE = 0x74, M_FRIENDS = 0x80;

enum Buffer { ROWS, BLOBS, IDS, TROWS, TBLOBS };
enum Step : uint32_t { IDLE, START, MY_POSITION, PAGE, FRIENDS };

// service table slots
using PlayersRequest = int32_t (*)(uintptr_t base, uint32_t board, const char *ids, uint32_t n, uint8_t *rows,
                                   uint8_t *blobs, uint32_t max);
using RangeRequest = int32_t (*)(uintptr_t base, uint32_t board, uint32_t first, uint32_t last, uint8_t *rows,
                                 uint8_t *blobs, uint32_t max);
using SubmitRequest = int32_t (*)(uintptr_t base, uint32_t board, const char *id, int64_t score, const uint8_t *blob64);
using Poll = int32_t (*)(uintptr_t base, int32_t ticket);
using MoreFriendIds = uint32_t (*)(uintptr_t base, char *out, uint32_t max);

struct Work { uint32_t step; int32_t ticket; int32_t page; };
struct Submit { uint32_t used; int32_t ticket; uint8_t blob[BLOB]; };

U3X_BSS Work work;                         // the running query
U3X_BSS Submit submits[5];                 // submits on their way
U3X_BSS uint8_t more_ids[MORE_IDS * ID];   // answer of "more friend ids"

template <typename T> [[gnu::always_inline]] inline void put(void *p, uintptr_t off, T v) {
    *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + off) = v;
}
[[gnu::always_inline]] inline uint8_t *manager(uintptr_t base) { return reinterpret_cast<uint8_t *>(base + MANAGER); }
template <typename F> [[gnu::always_inline]] inline F service(uintptr_t slot) { return rd<F>(u3h_ona(), slot); }
[[gnu::always_inline]] inline int32_t poll(uintptr_t base, int32_t ticket) { return service<Poll>(0x38)(base, ticket); }

U3X_FN void copy(uint8_t *to, const uint8_t *from, uintptr_t n) {
    for (uintptr_t i = 0; i < n; ++i) to[i] = from[i];
}

U3X_FN void clear(uint8_t *p, uintptr_t n) {
    for (uintptr_t i = 0; i < n; ++i) p[i] = 0;
}

// equal texts: the same bytes up to and including the NUL
U3X_FN bool same_text(const char *a, const char *b) {
    for (;; ++a, ++b) {
        if (*a != *b) return false;
        if (!*a) return true;
    }
}

// is the text of `id` one of the first n ids of the list
U3X_FN bool in_list(const uint8_t *ids, uint32_t n, const uint8_t *id) {
    for (uint32_t i = 0; i < n; ++i)
        if (same_text(reinterpret_cast<const char *>(ids + i * ID), reinterpret_cast<const char *>(id))) return true;
    return false;
}

// ---- set-up, query, submit ----

// net start: an empty manager that points at the C part's buffers; nothing is on its way
[[gnu::always_inline]] inline void set_up(uintptr_t base) {
    if (u3x_lb_off()) return;
    uint8_t *m = manager(base);
    clear(m, ID);
    const uint8_t *name = reinterpret_cast<const uint8_t *>(base + OWN_NAME);
    for (uint32_t i = 0; i < 16 && name[i]; ++i) m[i] = name[i];
    put<uint32_t>(m, M_FRIEND_ROWS, 0);
    for (int which = ROWS; which <= TBLOBS; ++which) put<uint8_t *>(m, 0x28 + 8 * which, u3h_lb_buf(which));
    put<uint32_t>(m, 0x50, 0);
    put<uint64_t>(m, M_BUSY, 0);
    put<uint32_t>(m, M_BOARD, 0);
    put<int32_t>(m, M_REQUEST, 0);
    put<uint64_t>(m, 0x68, 0);
    put<uint32_t>(m, M_COUNT, 0);
    put<int32_t>(m, M_PAGE, -1);
    put<uint8_t>(m, 0x78, 1);   // the widget resets itself
    put<uint32_t>(m, 0x7c, 0);
    put<uint8_t>(m, M_FRIENDS, 0);
    put<uint8_t>(m, 0x81, 0);
    clear(m + 0x88, 0x160);
    for (Submit &slot : submits) slot.used = 0;
    work.step = IDLE;
    ext_ea()[48] += 1;
}

// the widget wants a page: accepted unless a query runs; the next tick starts it
[[gnu::always_inline]] inline void accept_query(uintptr_t base, void *s, uint32_t board, int32_t page) {
    uint8_t *m = manager(base);
    if (rd<uint64_t>(m, M_BUSY)) return;
    const uint32_t week_board = u3x_lb_week_board(s, board);
    put<int32_t>(m, M_REQUEST, page);
    put<uint32_t>(m, M_BOARD, week_board);
    put<uint64_t>(m, M_BUSY, 1);
    work.step = START;
    ext_ea()[49] += 1;
}

// a score with its blob goes to the service; five can be on their way
[[gnu::always_inline]] inline int submit(uintptr_t base, void *s, uint32_t board, int64_t score, const uint8_t *blob) {
    const uint32_t week_board = u3x_lb_week_board(s, board);
    for (Submit &slot : submits) {
        if (slot.used) continue;
        copy(slot.blob, blob, BLOB);
        slot.ticket = service<SubmitRequest>(0x30)(base, week_board, reinterpret_cast<const char *>(manager(base)),
                                                   score, slot.blob);
        slot.used = slot.ticket >= 0;   // a refused submit leaves the slot free
        ext_ea()[51] += 1;
        return 0;
    }
    ext_ea()[52] += 1;
    return -1;
}

// ---- the steps of a query ----

// the query is over: the widget may read the manager again
U3X_FN void finish(uintptr_t base) {
    work.step = IDLE;
    put<uint64_t>(manager(base), M_BUSY, 0);
    ext_ea()[54] += 1;
}

// asks for the 11 rows of page work.page of everyone's board
U3X_FN void range_request(uintptr_t base, uint32_t board) {
    uint8_t *m = manager(base);
    put<int32_t>(m, M_PAGE, work.page);
    put<uint32_t>(m, M_COUNT, 0);
    const uint32_t first = static_cast<uint32_t>(work.page) * PAGE_ROWS;
    work.ticket = service<RangeRequest>(0x28)(base, board, first, first + (PAGE_ROWS - 1), u3h_lb_buf(ROWS),
                                              u3h_lb_buf(BLOBS), PAGE_ROWS);
    if (work.ticket >= 0)
        work.step = PAGE;
    else
        finish(base);
}

// friends board: the friend list, the friends the service side knows and the player, then their rows are asked for
U3X_FN void start_friends(uintptr_t base, uint32_t board) {
    uint8_t *m = manager(base);
    uint8_t *ids = u3h_lb_buf(IDS);
    uint32_t n = u3h_fr_lb_names(base, reinterpret_cast<char *>(ids), REQUEST_ROWS - 1);
    uint32_t more = service<MoreFriendIds>(0x48)(base, reinterpret_cast<char *>(more_ids), MORE_IDS);
    if (more > MORE_IDS) more = MORE_IDS;
    for (uint32_t i = 0; i < more && n <= 100; ++i) {
        const uint8_t *id = more_ids + i * ID;
        if (in_list(ids, n, id)) continue;
        copy(ids + n * ID, id, ID);
        ++n;
    }
    copy(ids + n * ID, m, ID);   // the player, also when already in the list
    work.ticket = service<PlayersRequest>(0x20)(base, board, reinterpret_cast<const char *>(ids), n + 1,
                                                u3h_lb_buf(TROWS), u3h_lb_buf(TBLOBS), REQUEST_ROWS);
    if (work.ticket >= 0) {
        work.step = FRIENDS;
        return;
    }
    put<int32_t>(m, M_FRIEND_ROWS, -1);
    u3x_lb_net_error(base);
    finish(base);
}

// step 1: the local answer, or the first request of the query
U3X_FN void start_query(uintptr_t base, void *s, uint32_t board, int32_t request) {
    uint8_t *m = manager(base);
    if (u3h_m02_lb_local(base, s, m, board, request)) {
        finish(base);
        return;
    }
    put<uint32_t>(m, M_FRIEND_ROWS, 0);
    if (rd<uint8_t>(m, M_FRIENDS)) {
        start_friends(base, board);
        return;
    }
    put<uint32_t>(m, M_COUNT, 0);
    if (request != -1) {
        work.page = request;
        range_request(base, board);
        return;
    }
    // "my page": first the player's own position
    uint8_t *ids = u3h_lb_buf(IDS);
    copy(ids, m, ID);
    work.ticket = service<PlayersRequest>(0x20)(base, board, reinterpret_cast<const char *>(ids), 1, u3h_lb_buf(TROWS),
                                                u3h_lb_buf(TBLOBS), REQUEST_ROWS);
    if (work.ticket >= 0) {
        work.step = MY_POSITION;
        return;
    }
    work.page = 0;
    u3x_lb_net_error(base);
    range_request(base, board);
}

// step 2: the player's position gives the page to load
U3X_FN void my_position_answer(uintptr_t base, uint32_t board) {
    const int32_t r = poll(base, work.ticket);
    if (r == RUNNING) return;
    work.page = 0;
    if (r < 0)
        u3x_lb_net_error(base);
    else if (r > 0)
        work.page = static_cast<int32_t>(rd<uint32_t>(u3h_lb_buf(TROWS), ROW_POSITION) / PAGE_ROWS);
    range_request(base, board);
}

// step 3: the rows of the page get the positions shown
U3X_FN void page_answer(uintptr_t base) {
    const int32_t r = poll(base, work.ticket);
    if (r == RUNNING) return;
    if (r > 0) {
        const uint32_t count = static_cast<uint32_t>(r) > PAGE_ROWS ? PAGE_ROWS : static_cast<uint32_t>(r);
        const uint32_t first = static_cast<uint32_t>(work.page) * PAGE_ROWS + 1;
        uint8_t *rows = u3h_lb_buf(ROWS);
        for (uint32_t i = 0; i < count; ++i) put<uint32_t>(rows + i * ROW, ROW_POSITION, first + i);
        put<uint32_t>(manager(base), M_COUNT, count);
        ext_ea()[50] += count;
    }
    finish(base);
}

[[gnu::always_inline]] inline uint32_t position(const uint8_t *rows, uintptr_t i) {
    return rd<uint32_t>(rows + i * ROW, ROW_POSITION);
}

// rows into ascending order of position, equal positions in the order delivered; each blob goes with its row
U3X_FN void sort_rows(uint8_t *rows, uint8_t *blobs, uintptr_t count) {
    uint8_t row[ROW], blob[BLOB];
    for (uintptr_t i = 1; i < count; ++i) {
        const uint32_t pos = position(rows, i);
        if (position(rows, i - 1) <= pos) continue;
        copy(row, rows + i * ROW, ROW);
        copy(blob, blobs + i * BLOB, BLOB);
        uintptr_t to = i;
        for (; to > 0 && position(rows, to - 1) > pos; --to) {
            copy(rows + to * ROW, rows + (to - 1) * ROW, ROW);
            copy(blobs + to * BLOB, blobs + (to - 1) * BLOB, BLOB);
        }
        copy(rows + to * ROW, row, ROW);
        copy(blobs + to * BLOB, blob, BLOB);
    }
}

// step 4: the friends' rows are sorted and one page of them is shown
U3X_FN void friends_answer(uintptr_t base, int32_t request) {
    uint8_t *m = manager(base);
    const int32_t r = poll(base, work.ticket);
    if (r == RUNNING) return;
    if (r < 0) {
        put<int32_t>(m, M_FRIEND_ROWS, -1);
        u3x_lb_net_error(base);
        finish(base);
        return;
    }
    put<int32_t>(m, M_FRIEND_ROWS, r);
    if (r == 0) {
        put<uint32_t>(m, M_COUNT, 0);
        finish(base);
        return;
    }
    const uintptr_t count = static_cast<uint32_t>(r);
    uint8_t *rows = u3h_lb_buf(TROWS), *blobs = u3h_lb_buf(TBLOBS);
    for (uintptr_t i = 0; i < count; ++i) put<uint32_t>(rows + i * ROW, ROW_POSITION, position(rows, i) + 1);
    sort_rows(rows, blobs, count);

    uint32_t mine = 0;   // the last row of the player, the first row when there is none
    for (uintptr_t i = 0; i < count; ++i)
        if (u3x_lb_me(base, reinterpret_cast<const char *>(rows + i * ROW))) mine = static_cast<uint32_t>(i);

    int32_t page;
    if (request == -1) {
        page = static_cast<int32_t>(mine / PAGE_ROWS);
    } else {
        // not past the page after the one the query before showed, and that one only when it was full
        const uint32_t step = rd<uint32_t>(m, M_COUNT) > 10 ? 1 : 0;
        const int32_t limit = static_cast<int32_t>(rd<uint32_t>(m, M_PAGE) + step);
        page = request <= limit ? request : limit;
    }
    put<int32_t>(m, M_PAGE, page);

    uint8_t *shown = u3h_lb_buf(ROWS), *shown_blobs = u3h_lb_buf(BLOBS);
    uint32_t copied = 0;
    for (uintptr_t i = static_cast<uint32_t>(page) * PAGE_ROWS; i < count && copied < PAGE_ROWS; ++i, ++copied) {
        copy(shown + copied * ROW, rows + i * ROW, ROW);
        copy(shown_blobs + copied * BLOB, blobs + i * BLOB, BLOB);
    }
    put<uint32_t>(m, M_COUNT, copied);
    ext_ea()[50] += copied;
    finish(base);
}

// every frame: finished submits give their slot back, then one step of the running query
[[gnu::always_inline]] inline void tick(uintptr_t base, void *s) {
    if (u3x_lb_off()) return;
    for (Submit &slot : submits)
        if (slot.used && poll(base, slot.ticket) != RUNNING) slot.used = 0;
    if (work.step == IDLE) return;
    const uint8_t *m = manager(base);
    const uint32_t board = rd<uint32_t>(m, M_BOARD);
    const int32_t request = rd<int32_t>(m, M_REQUEST);
    if (work.step == START)
        start_query(base, s, board, request);
    else if (work.step == MY_POSITION)
        my_position_answer(base, board);
    else if (work.step == PAGE)
        page_answer(base);
    else if (work.step == FRIENDS)
        friends_answer(base, request);
}

// the end of the game's widget update (its two stores), and the query the PS4 game no longer starts
[[gnu::always_inline]] inline void widget_tail(uintptr_t base, void *s, uint8_t *w, int32_t idx) {
    put<int32_t>(w, 0x20, idx);
    const uint32_t board = idx >= 0 && idx <= 3 ? static_cast<uint32_t>(idx) : 0;
    if (!u3x_lb_off()) u3x_lb_query(base, s, FIRST_BOARD + board, rd<int32_t>(w, 0x1c));
    put<uint32_t>(w, 0x10, at<uint32_t>(base + BOARD_TABLE + 12 * board + 4));   // the board's column set id
    ext_ea()[55] += 1;
}

// ---- part C: the two pieces the query uses that were C (fr_lb_names, m02_lb_local) ----

// the ids of the friend list, for the friends board; nothing while the list is being fetched
[[gnu::always_inline]] inline uint32_t friend_ids(uintptr_t base, char *out, uint32_t max) {
    const uint8_t *friends = reinterpret_cast<const uint8_t *>(base + FRIEND_OBJECT);
    if (rd<uint64_t>(friends, 0xc8) || !rd<const uint8_t *>(friends, 8)) return 0;
    const uint32_t known = rd<uint32_t>(friends, 0);
    const uint32_t n = known < max ? known : max;
    for (uint32_t i = 0; i < n; ++i)
        copy(reinterpret_cast<uint8_t *>(out) + static_cast<uintptr_t>(i) * ID, u3h_fr_ent(base, i) + 1, ID);
    return n;
}

// the answer when no leaderboard service is there: one row, the player's own, from the profile
[[gnu::always_inline]] inline int local_answer(uintptr_t base, void *s, uint8_t *m, uint32_t board, int32_t req,
                                               int backend, uint32_t *diag) {
    if (ext_eb()[2] & 0x1000) return 0;
    if (backend) return 0;
    uint8_t *profile = u3h_lg_profile(base, 0);
    if (!profile) return 0;

    const uint8_t *record = u3h_lg_rec(base, s, profile);
    int32_t score = 0;
    if (board == FIRST_BOARD) score = rd<int32_t>(record, 0x1c54);
    else if (board == FIRST_BOARD + 1) score = rd<int32_t>(record, 0x1f38);
    else if (board == FIRST_BOARD + 2) score = rd<int32_t>(record, 0x1f34);
    else if (board == FIRST_BOARD + 3) score = rd<int32_t>(record, 0x1f2c);
    uint8_t *row = u3h_lb_buf(ROWS);
    clear(row, ROW);
    copy(row, m, ID);
    put<uint32_t>(row, ROW_POSITION, 1);
    put<int64_t>(row, ROW_SCORE, score);

    uint8_t *blob = u3h_lb_buf(BLOBS);
    clear(blob, BLOB);
    put<uint32_t>(blob, 0, game_call<uint32_t>(base + RANK_VALUE, profile));
    record = u3h_lg_rec(base, s, profile);
    put<uint32_t>(blob, 4, rd<uint32_t>(record, 0x1f30) + rd<uint32_t>(record, 0x1f28));
    put<uint32_t>(blob, 8, rd<uint32_t>(record, 0x1c58));
    put<uint32_t>(blob, 0xc, rd<uint32_t>(record, 0x1f24));
    put<uint32_t>(blob, 0x10, rd<uint32_t>(record, 0x1eac));

    const int32_t page = req > 0 ? req : 0;
    put<int32_t>(m, M_PAGE, page);
    put<uint32_t>(m, M_COUNT, page == 0 ? 1 : 0);   // pages after the first are empty
    if (rd<uint8_t>(m, M_FRIENDS)) put<int32_t>(m, M_FRIEND_ROWS, 1);
    diag[2] += 1;
    diag[3] = board;
    return 1;
}

}  // namespace
}  // namespace g29

// ---- entry points, parts A and B: the manager ----

// the "leaderboards off" switch
U3X_FN int u3x_lb_off(void) {
    return ext_ea()[1] & 0x10 ? 1 : 0;
}

// the board number a query or submit uses: the four boards move on by 4 every week
U3X_FN uint32_t u3x_lb_week_board(void *s, uint32_t board) {
    if (board < g29::FIRST_BOARD) return board;
    const uint8_t *week = u3x_sym_table(s, g29::WEEK_SYMBOL);
    if (!week) {
        ext_ea()[56] += 1;
        return board;
    }
    return board + 4u * rd<uint32_t>(week, 0);
}

U3X_FN void u3x_lb_init(uintptr_t base, void *) {
    g29::set_up(base);
}

U3X_FN void u3x_lb_query(uintptr_t base, void *s, uint32_t board, int32_t page) {
    g29::accept_query(base, s, board, page);
}

U3X_FN int u3x_lb_submit(uintptr_t base, void *s, uint32_t board, int64_t score, const uint8_t *blob) {
    return g29::submit(base, s, board, score, blob);
}

// the game shows "cannot connect"
U3X_FN void u3x_lb_net_error(uintptr_t base) {
    at<uint8_t>(base + g29::NET_ERROR) = 1;
    ext_ea()[53] += 1;
}

// is this the player's own id
U3X_FN int u3x_lb_me(uintptr_t base, const char *name) {
    return g29::same_text(name, reinterpret_cast<const char *>(g29::manager(base))) ? 1 : 0;
}

U3X_FN void u3x_lb_tick(uintptr_t base, void *s) {
    g29::tick(base, s);
}

U3X_FN void u3x_lb_widget_tail(uintptr_t base, void *s, uint8_t *w, int32_t idx) {
    g29::widget_tail(base, s, w, idx);
}

// stub kinds 66 (widget update) and 67 (post-match handler)
U3X_FN int u3x_lb_kind(uintptr_t base, void *s, uint64_t *ev, int kind) {
    if (kind == 66) {
        u3x_lb_widget_tail(base, s, reinterpret_cast<uint8_t *>(ev[0]), static_cast<int32_t>(static_cast<uint32_t>(ev[2])));
        return 1;
    }
    if (kind == 67) {
        u3h_pm119(base, s, reinterpret_cast<uint8_t *>(ev[0]));
        return 1;
    }
    return 0;
}

// ---- entry points, part C: called by the C functions fr_lb_names and m02_lb_local ----

U3X_FN uint32_t u3x_lb_friend_ids(uintptr_t base, char *out, uint32_t max) {
    return g29::friend_ids(base, out, max);
}

U3X_FN int u3x_lb_local(uintptr_t base, void *s, uint8_t *m, uint32_t board, int32_t req, int backend, uint32_t *diag) {
    return g29::local_answer(base, s, m, board, req, backend, diag);
}
