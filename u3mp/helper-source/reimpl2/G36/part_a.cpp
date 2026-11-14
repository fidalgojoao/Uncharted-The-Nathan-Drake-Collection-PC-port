// G36 part A: the answers of the lobby's players list while matchmaking runs: once for the offline stand-in
// (u3x_m03_players) and once for the online matchmaking (u3x_onm_players). Written from spec G36_matchmaking.md,
// sections 3 and 4.

// ---- functions of the C part and of other groups (spec 2.2)
extern "C" {
uint32_t *u3h_m03(void);
volatile uint32_t *u3h_m03_diag(void);
uint32_t *u3h_onm(void);
int u3h_onb_mode(void);
const char *u3h_text(uintptr_t base, void *s, uint32_t id);
uint8_t *u3h_player_profile(uintptr_t base);
int32_t u3h_ox_rank_of(const char *name);
void u3h_rank_icon(uintptr_t base, void *s, int32_t *ev);
uint32_t u3h_team_colour(uintptr_t base, int32_t team);
void u3h_hud_alias_names(uintptr_t base, void *s);
void u3h_stage_tick(uintptr_t base, void *s);
void u3h_loadout_tick(uintptr_t base, void *s);
void u3x_clan_tag_poll(uintptr_t base, void *s);
const char *u3x_other_member(const uint8_t *session, uint32_t row);
void u3x_icon_answer(int32_t *ev, int32_t kind, int32_t a, int32_t b, int32_t c, int32_t index);
void u3x_icon_tinted(int32_t *ev, int32_t kind, int32_t value, int32_t tint);

int u3x_m03_players(uintptr_t base, int32_t *ev, void *s, const char *self_name);
int u3x_onm_players(uintptr_t base, int32_t *ev, void *s, const char *self_name);
}

namespace g36a {

// image offsets
constexpr uintptr_t GAME_CLOCK = 0x17b4ba4;      // s32, 300 ticks per second
constexpr uintptr_t NET_STATUS = 0x17d5c58 + 0x40;   // s32 matchmaking status of the net state
constexpr uintptr_t MATCH_SESSION = 0x17d6420;
constexpr uintptr_t STATUS_LINE_ANSWER = 0x75c190;   // the game's own answer for the status line

// list request: operations, words, image slots, image kinds
enum : int32_t { OP_STATUS = 1, OP_TEXT = 2, OP_IMAGE = 4, OP_GET_SELECTED = 5, OP_LAST_ROW = 6, OP_SELECT = 10, OP_COLOUR = 20 };
enum : int { W_OP = 0, W_ROW = 1, W_COLOUR = 0xf, W_SLOT = 0x14, W_COLUMN = 0x15, W_SUBQUESTION = 0x16 };
constexpr uint32_t SLOT_RANK = 0xf5a294f3u, SLOT_CONNECTION = 0;
constexpr int32_t KIND_RANK = 0x29, KIND_SEARCHING = 16, KIND_BARS = 31;

constexpr uint32_t TEXT_FINDING = 0xc0759450u, TEXT_FOUND = 0xa91680a8u;
constexpr uint32_t JUST_FOUND_TICKS = 180;
constexpr int32_t BARS_COLOUR = static_cast<int32_t>(0x8000ff00u);

U3X_RO const char empty_text[1] = {0};
U3X_RO const char player_word[] = "Player ";
U3X_BSS char player_name[24];   // the last "Player N" answer

// the text answer of a request
U3X_FN void set_text(int32_t *ev, const char *text) {
    *reinterpret_cast<const char **>(reinterpret_cast<uint8_t *>(ev) + 0x40) = text;
}


// "Player " + number in decimal, in the buffer of this file
U3X_FN const char *player_text(uint32_t number) {
    char digits[10];
    int count = 0;
    do {
        digits[count++] = static_cast<char>('0' + number % 10);
        number /= 10;
    } while (number);
    int n = 0;
    for (const char *p = player_word; *p; ++p) player_name[n++] = *p;
    while (count) player_name[n++] = digits[--count];
    player_name[n] = 0;
    return player_name;
}

// the jobs the lobby list drives with its row count request
U3X_FN void list_jobs(uintptr_t base, void *s) {
    u3x_clan_tag_poll(base, s);
    u3h_hud_alias_names(base, s);
    u3h_stage_tick(base, s);
    u3h_loadout_tick(base, s);
}

// the rank word of the menu user; false when it cannot be read
U3X_FN bool own_rank(uintptr_t base, void *s, int32_t *rank) {
    using RankOf = uint32_t (*)(uint8_t *profile);
    const RankOf native21 = rd<RankOf>(s, 768 + 8 * 21);
    uint8_t *const profile = u3h_player_profile(base);
    if (!native21 || !profile) return false;
    *rank = static_cast<int32_t>(native21(profile));
    return true;
}

// the four operations both lists answer the same way; false = another operation
U3X_FN bool common_answer(uintptr_t base, int32_t *ev, void *s, uint32_t *selected, uint32_t rows) {
    switch (ev[W_OP]) {
    case OP_STATUS:
        game_call<void>(base + STATUS_LINE_ANSWER, ev);
        return true;
    case OP_LAST_ROW:
        ev[W_ROW] = static_cast<int32_t>(rows - 1);
        list_jobs(base, s);
        return true;
    case OP_GET_SELECTED:
        ev[W_ROW] = static_cast<int32_t>(*selected);
        return true;
    case OP_SELECT:
        *selected = static_cast<uint32_t>(ev[W_ROW]);
        return true;
    default:
        return false;
    }
}

enum class Row { searching, just_found, found };

// what a row of the stand-in's list shows
U3X_FN Row stand_in_row(uintptr_t base, const uint32_t *m, uint32_t row, bool fill) {
    const uint32_t phase = m[3], found = m[4];
    if (row >= found) return Row::searching;
    if (row == 0) return Row::found;
    if (fill) return phase <= 2 ? Row::just_found : Row::found;
    const uint32_t age = at<uint32_t>(base + GAME_CLOCK) - m[7];
    return row + 1 == found && phase == 2 && age < JUST_FOUND_TICKS ? Row::just_found : Row::found;
}

}  // namespace g36a

// players list of the offline stand-in (spec 4.1)
extern "C" U3X_FN int u3x_m03_players(uintptr_t base, int32_t *ev, void *s, const char *self_name) {
    using namespace g36a;
    uint32_t *const m = u3h_m03();
    if (!m[1] || at<int32_t>(base + NET_STATUS) <= 0) return 0;

    const int32_t op = ev[W_OP];
    const uint32_t row = static_cast<uint32_t>(ev[W_ROW]);
    u3h_m03_diag()[7] = u3h_m03_diag()[7] + 1;

    if (common_answer(base, ev, s, &m[6], m[5])) return 1;
    if (row >= m[5]) return 1;

    const uint32_t *const eb = ext_eb();
    const bool fill = !(eb[2] & 0x2000u) && !(eb[3] & 0x10000u);
    const Row kind = stand_in_row(base, m, row, fill);
    const uint32_t slot = static_cast<uint32_t>(ev[W_SLOT]);

    if (op == OP_TEXT) {
        if (ev[W_COLUMN] != 0) set_text(ev, empty_text);
        else if (kind == Row::searching) set_text(ev, u3h_text(base, s, TEXT_FINDING));
        else if (kind == Row::just_found) set_text(ev, u3h_text(base, s, TEXT_FOUND));
        else if (row == 0) set_text(ev, self_name);
        else set_text(ev, player_text(row + 1));
    } else if (op == OP_IMAGE && slot == SLOT_CONNECTION) {
        // connection bars of the stand-in players: 5, 4 or 3
        if (fill && row != 0 && kind != Row::searching)
            u3x_icon_tinted(ev, KIND_BARS, static_cast<int32_t>(5 - row * 7 % 3), BARS_COLOUR);
    } else if (op == OP_IMAGE && slot == SLOT_RANK) {
        if (kind == Row::searching) {
            u3x_icon_answer(ev, KIND_SEARCHING, 0, 0, 0, -1);
        } else if (kind == Row::found) {
            int32_t rank = static_cast<int32_t>(1 + (row * 17 + 5) % 60);
            if (row == 0 && !own_rank(base, s, &rank)) return 1;
            u3x_icon_tinted(ev, KIND_RANK, rank, -1);
            u3h_rank_icon(base, s, ev);
        }
    } else if (op == OP_COLOUR) {
        const uint32_t *const ec = reinterpret_cast<const uint32_t *>(u3h_ext() + 0x4450);
        if ((ec[2] & 0x10u) && ev[W_SUBQUESTION] == 2 && kind == Row::found)
            ev[W_COLOUR] = static_cast<int32_t>((u3h_team_colour(base, static_cast<int32_t>(row & 1)) & 0xffffffu) | 0x80000000u);
    }
    return 1;
}

// players list of the online matchmaking (spec 4.2)
extern "C" U3X_FN int u3x_onm_players(uintptr_t base, int32_t *ev, void *s, const char *self_name) {
    using namespace g36a;
    uint32_t *const o = u3h_onm();
    if (u3h_onb_mode() != 1 || !o[1] || at<int32_t>(base + NET_STATUS) <= 0) return 0;

    const int32_t op = ev[W_OP];
    const uint32_t row = static_cast<uint32_t>(ev[W_ROW]);

    if (common_answer(base, ev, s, &o[5], o[6])) return 1;
    if (row >= o[6]) return 1;

    const bool wants_text = op == OP_TEXT;
    const bool wants_rank = op == OP_IMAGE && static_cast<uint32_t>(ev[W_SLOT]) == SLOT_RANK;
    if (!wants_text && !wants_rank) return 1;
    if (wants_text && ev[W_COLUMN] != 0) {
        set_text(ev, empty_text);
        return 1;
    }

    // the row's player: this player, or a member of the match room; null = an empty row
    const uint8_t *const session = reinterpret_cast<const uint8_t *>(base + MATCH_SESSION);
    const char *name = self_name;
    if (row != 0) name = rd<uint64_t>(session, 0x18) ? u3x_other_member(session, row) : nullptr;

    if (wants_text) {
        set_text(ev, row == 0 || name ? name : u3h_text(base, s, TEXT_FINDING));
        return 1;
    }
    if (row != 0 && !name) {
        u3x_icon_answer(ev, KIND_SEARCHING, 0, 0, 0, -1);
        return 1;
    }
    int32_t rank;
    if (row == 0) {
        if (!own_rank(base, s, &rank)) return 1;
    } else {
        rank = u3h_ox_rank_of(name);
        if (rank < 0) rank = 1;
    }
    u3x_icon_answer(ev, KIND_RANK, rank, 0, 0, -1);
    u3h_rank_icon(base, s, ev);
    return 1;
}
