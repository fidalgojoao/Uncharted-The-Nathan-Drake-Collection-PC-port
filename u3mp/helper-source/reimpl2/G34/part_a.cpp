// G34, part A: the party member list of the lobby. Which members are shown in which order (the row model) and the
// answers to the requests of the "PARTY" box. Written from spec G34_party_members.md, sections 4.1 to 4.8.

extern "C" {
// the C part
void *u3h_i01_state(void);
uint32_t u3h_np_code(uint8_t *np);
uint8_t *u3h_ec_find_np(uintptr_t base, const uint8_t *m, int32_t idx);
void u3h_set_text(int32_t *ev, const char *t);
char *u3h_fmt_append(char *o, char *e, const char *t);
uint8_t *u3h_player_profile(uintptr_t base);
int32_t u3h_ox_rank_of(const char *nm);
void u3h_rank_icon(uintptr_t base, void *s, int32_t *ev);
int32_t u3h_gt_conn_state(uintptr_t base, int32_t id);
void u3h_clan_tag_poll(uintptr_t base, void *s);
void u3h_hud_alias_names(uintptr_t base, void *s);
void u3h_stage_tick(uintptr_t base, void *s);
void u3h_loadout_tick(uintptr_t base, void *s);
// other groups
uint8_t *u3x_netgame_cur(uintptr_t base, void *s);
int32_t u3x_netgame_type(uintptr_t base, void *s);
int u3x_pm_coop(uintptr_t base, void *s);
// this group: functions that stay, and part B
uint8_t *u3x_i01_md(uintptr_t base, uint8_t *m);
int16_t u3x_i01_ngi_push(uintptr_t base);
void u3x_i01_ngi_pop(uintptr_t base, int16_t keep);
void u3x_i01_focus(uintptr_t base, void *s);
// part A
uint8_t *u3x_np_by_id(uintptr_t base, uint32_t id);
uint8_t *u3x_i01_np_by_code(uintptr_t base, uint32_t id);
int u3x_i01_self(uintptr_t base, uint8_t *m);
void u3x_i01_rows(uintptr_t base);
int32_t u3x_i01_type(uintptr_t base, void *s);
uint32_t u3x_i01_colour(uintptr_t base, int32_t idx);
int u3x_i01_strcmp(const uint8_t *a, const uint8_t *b);
void u3x_i01_row_colour(uintptr_t base, void *s, int32_t *event, uint8_t *m, int guest, uint32_t row);
int u3x_i01_party(uintptr_t base, int32_t *event, void *s, const char *selfname);
}

namespace g34a {

// image offsets
constexpr uintptr_t NET_GAMES = 0x17cb060;      // the net game manager
constexpr uintptr_t NET_MENU_MODE = 0x17d603c;  // 2 = custom game
constexpr uintptr_t MATCH_SESSION = 0x17d6420;
constexpr uintptr_t PARTY_SESSION = 0x17dc298;
constexpr uintptr_t FN_TEAM_COLOURS = 0x7418e0;
constexpr uintptr_t FN_PARTY_PLAYERS = 0x7a2b10;
constexpr uintptr_t FN_TEAM_COUNT = 0x7210a0;

// a session
constexpr uintptr_t SES_ROOM = 0x18, SES_SLOTS = 0x680, SES_OWN_ID = 0x12e4, SES_HOST_ID = 0x12e8;
constexpr uint32_t SLOT_COUNT = 10;
constexpr uintptr_t SLOT_SIZE = 0x120;
// a member slot
constexpr uintptr_t SLOT_IN_USE = 0x25, SLOT_ID = 0x2c, SLOT_CONN = 0x34, SLOT_DATA_LEN = 0x3c, SLOT_FLAGS = 0x72,
                    SLOT_RANK = 0x7c;
// a member's lobby data
constexpr uintptr_t MD_TEAM = 9, MD_GUEST_ID = 0x19;
// a net player record
constexpr uintptr_t NP_FIRST = 0x390, NP_SIZE = 0x9c0, NP_ID = 0x1e4, NP_USER = 0x1e8, NP_TEAM = 0x21c;

constexpr uint32_t MAX_ROWS = 20;
constexpr uint32_t GUEST_ROW = 0x100;
constexpr uint32_t NO_TEAM = 0xff;

U3X_RO const char guest_suffix[] = {' ', '(', '1', ')', 0};
U3X_RO const char empty_text[] = {0};

// the party list state of the C part (spec 3.3); only the fields this part uses are named
struct PartyList {
    uint32_t d[12];             // diagnostics
    uint8_t *members[10];       // the members shown, in list order
    uint32_t member_count;
    uint32_t rows[MAX_ROWS];    // member index, GUEST_ROW for a second player
    uint32_t row_count;
    int32_t selected;
    uint8_t pseudo[SLOT_SIZE];  // stands for this PC while there is no party
    char name[64];              // the text of a guest row
    uint32_t in_box;
    uint32_t alone;
};
static_assert(__builtin_offsetof(PartyList, members) == 0x30 && __builtin_offsetof(PartyList, member_count) == 0x80 &&
              __builtin_offsetof(PartyList, rows) == 0x84 && __builtin_offsetof(PartyList, row_count) == 0xd4 &&
              __builtin_offsetof(PartyList, selected) == 0xd8 && __builtin_offsetof(PartyList, pseudo) == 0xdc &&
              __builtin_offsetof(PartyList, name) == 0x1fc && __builtin_offsetof(PartyList, in_box) == 0x23c &&
              __builtin_offsetof(PartyList, alone) == 0x240);

[[gnu::always_inline]] inline PartyList *party_list() { return static_cast<PartyList *>(u3h_i01_state()); }
[[gnu::always_inline]] inline uint8_t *image(uintptr_t base, uintptr_t offset) { return reinterpret_cast<uint8_t *>(base + offset); }
[[gnu::always_inline]] inline uint8_t *session_slot(uint8_t *session, uint32_t i) { return session + SES_SLOTS + i * SLOT_SIZE; }
[[gnu::always_inline]] inline uint32_t shown_colour(uint32_t c) { return (c & 0xffffffu) | 0x80000000u; }
[[gnu::always_inline]] inline bool focus_on() { return !(ext_eb()[1] & 0x20000u); }

// the first net player record with that code
U3X_FN uint8_t *net_player(uintptr_t base, uint32_t code) {
    if (!code) return nullptr;
    for (uint32_t i = 0; i < 10; i++) {
        uint8_t *np = image(base, NET_GAMES + NP_FIRST + i * NP_SIZE);
        if (u3h_np_code(np) == code) return np;
    }
    return nullptr;
}

// number of teams of the shown net game; the net game index is put back afterwards
U3X_FN int32_t team_count(uintptr_t base) {
    const int16_t keep = u3x_i01_ngi_push(base);
    const int32_t n = game_call<int32_t>(base + FN_TEAM_COUNT, image(base, NET_GAMES));
    u3x_i01_ngi_pop(base, keep);
    return n;
}

// the in-use member of the match session that has the same name as m
U3X_FN uint8_t *match_member(uintptr_t base, const uint8_t *m) {
    for (uint32_t i = 0; i < SLOT_COUNT; i++) {
        uint8_t *slot = session_slot(image(base, MATCH_SESSION), i);
        if (rd<uint8_t>(slot, SLOT_IN_USE) && u3x_i01_strcmp(slot, m) == 0) return slot;
    }
    return nullptr;
}

// operation 4: the rank badge of a member row
U3X_FN void rank_badge(uintptr_t base, void *s, int32_t *event, uint8_t *m, int own) {
    uint32_t rank;
    if (own) {
        const uintptr_t rank_of_profile = rd<uintptr_t>(s, 936);   // native 21
        uint8_t *profile = u3h_player_profile(base);
        if (!rank_of_profile || !profile) return;
        rank = game_call<uint32_t>(rank_of_profile, profile);
    } else {
        const int32_t fetched = u3h_ox_rank_of(reinterpret_cast<const char *>(m));
        if (fetched >= 0) {
            rank = static_cast<uint32_t>(fetched);
        } else {
            // the rank word of the member data, when it carries one
            const uint32_t word = rd<uint32_t>(m, SLOT_RANK);
            if (rd<uint32_t>(m, SLOT_DATA_LEN) != 0x40 || !(word & 0x20000000u)) return;
            rank = (rd<uint8_t>(m, SLOT_FLAGS) & 8) ? (word & 0xffffu) : (word & ~0x20000000u);
        }
    }
    // the image answer of kind 0x29 with the rank word
    event[2] = 0x29;
    event[4] = 0;
    event[5] = 0;
    event[6] = static_cast<int32_t>(rank);
    event[7] = 0;
    event[8] = 0;
    event[9] = -1;
    event[10] = 0;
    event[11] = -1;
    event[12] = static_cast<int32_t>(F_ONE);
    at<uint32_t>(reinterpret_cast<uintptr_t>(u3h_ext()) + 0x31c0)++;
    u3h_rank_icon(base, s, event);
}

// operation 2: the text of a row
U3X_FN void row_text(int32_t *event, PartyList *list, const uint8_t *m, bool guest, int own, const char *selfname) {
    const char *text = own ? selfname : reinterpret_cast<const char *>(m);
    if (guest) {
        char *const limit = list->name + 60;
        char *end = u3h_fmt_append(list->name, limit, text);
        end = u3h_fmt_append(end, limit, guest_suffix);
        *end = 0;
        text = list->name;
    }
    if (event[0x15] != 0) text = empty_text;   // only the name column has a text
    u3h_set_text(event, text);
}

}  // namespace g34a

extern "C" {

U3X_FN uint8_t *u3x_np_by_id(uintptr_t base, uint32_t id) { return g34a::net_player(base, id); }

U3X_FN uint8_t *u3x_i01_np_by_code(uintptr_t base, uint32_t id) { return g34a::net_player(base, id); }

// is this member this PC
U3X_FN int u3x_i01_self(uintptr_t base, uint8_t *m) {
    using namespace g34a;
    return rd<uint32_t>(m, SLOT_ID) == rd<uint32_t>(image(base, PARTY_SESSION), SES_OWN_ID) ? 1 : 0;
}

// rebuild the member list (host, host's team, the others) and the rows (a member, then its second player)
U3X_FN void u3x_i01_rows(uintptr_t base) {
    using namespace g34a;
    PartyList *const list = party_list();
    uint8_t *const session = image(base, PARTY_SESSION);
    // the counts live in the list state while the lists are rebuilt
    list->member_count = 0;
    list->row_count = 0;
    list->d[0]++;

    if (rd<uint64_t>(session, SES_ROOM) == 0) {
        // no party: the pseudo member stands for this PC
        uint8_t *const pseudo = list->pseudo;
        for (uintptr_t i = 0; i < SLOT_SIZE; i++) pseudo[i] = 0;
        at<uint32_t>(reinterpret_cast<uintptr_t>(pseudo) + SLOT_ID) = rd<uint32_t>(session, SES_OWN_ID);
        at<int32_t>(reinterpret_cast<uintptr_t>(pseudo) + SLOT_CONN) = -1;
        list->members[list->member_count++] = pseudo;
    } else {
        uint8_t *slot[SLOT_COUNT];
        int32_t team[SLOT_COUNT];
        bool host[SLOT_COUNT];
        uint32_t used = 0;
        int32_t host_team = 0;
        for (uint32_t i = 0; i < SLOT_COUNT; i++) {
            uint8_t *const m = session_slot(session, i);
            if (!rd<uint8_t>(m, SLOT_IN_USE)) continue;
            // the team from the lobby data (team number), replaced by the net player's team (team index) when it has one
            const uint8_t *const data = u3x_i01_md(base, m);
            int32_t t = data ? rd<uint8_t>(data, MD_TEAM) : 0;
            const uint8_t *const np = u3h_ec_find_np(base, m, 0);
            if (np && rd<int32_t>(np, NP_TEAM) != -1) t = rd<int32_t>(np, NP_TEAM);
            slot[used] = m;
            team[used] = t;
            // the host's member id is read anew for every slot, after the slot's two calls
            host[used] = rd<uint32_t>(m, SLOT_ID) == rd<uint32_t>(session, SES_HOST_ID);
            if (host[used]) host_team = t;
            used++;
        }
        for (int group = 0; group < 3; group++) {
            for (uint32_t k = 0; k < used; k++) {
                const int of_slot = host[k] ? 0 : team[k] == host_team ? 1 : 2;
                if (of_slot == group) list->members[list->member_count++] = slot[k];
            }
        }
    }
    for (uint32_t i = 0; i < list->member_count && list->row_count < MAX_ROWS; i++) {
        list->rows[list->row_count++] = i;
        const uint8_t *const data = u3x_i01_md(base, list->members[i]);
        if (data && rd<uint8_t>(data, MD_GUEST_ID) && list->row_count < MAX_ROWS) list->rows[list->row_count++] = i | GUEST_ROW;
    }
    list->d[1] = list->row_count;
}

// the game type of the shown net game, -1 for none
U3X_FN int32_t u3x_i01_type(uintptr_t base, void *s) {
    const int16_t keep = u3x_i01_ngi_push(base);
    const int32_t type = u3x_netgame_cur(base, s) ? u3x_netgame_type(base, s) : -1;
    u3x_i01_ngi_pop(base, keep);
    return type;
}

// the colour word of a team of the shown net game (not masked)
U3X_FN uint32_t u3x_i01_colour(uintptr_t base, int32_t idx) {
    using namespace g34a;
    if (idx < 0 || idx > 4) return 0;
    uint32_t colours[8];
    for (uint32_t i = 0; i < 8; i++) colours[i] = 0;
    const int16_t keep = u3x_i01_ngi_push(base);
    game_call<void>(base + FN_TEAM_COLOURS, colours, image(base, NET_GAMES), idx, int32_t{0});
    u3x_i01_ngi_pop(base, keep);
    return colours[3];
}

// compare two texts: 0 = equal, else the difference of the first two bytes that differ
U3X_FN int u3x_i01_strcmp(const uint8_t *a, const uint8_t *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return static_cast<int>(*a) - static_cast<int>(*b);
}

// operation 20: the team tint of a row
U3X_FN void u3x_i01_row_colour(uintptr_t base, void *s, int32_t *event, uint8_t *m, int guest, uint32_t row) {
    using namespace g34a;
    PartyList *const list = party_list();
    if (at<uint32_t>(base + NET_MENU_MODE) != 2) {
        u3h_set_text(event, empty_text);
        event[1] = 0;
        return;
    }
    const int32_t type = u3x_i01_type(base, s);
    // a co-op game has colours only with two or more players
    if (u3x_pm_coop(base, s) && game_call<int32_t>(base + FN_PARTY_PLAYERS, image(base, PARTY_SESSION)) < 2) return;
    if (event[0x16] != 2) return;
    const uint8_t *const data = u3x_i01_md(base, m);
    if (!data) {
        list->d[8]++;
        return;
    }

    // the team the lobby data gives
    uint32_t colour = 0x80000000u;
    uint32_t team = NO_TEAM;
    const uint8_t team_number = rd<uint8_t>(data, MD_TEAM);
    if (team_number) {
        team = team_number - 1u;
        if (guest && game_call<int32_t>(base + FN_PARTY_PLAYERS, image(base, PARTY_SESSION)) == 2 && team_count(base) == 2) {
            // two players on one PC in a game without fixed teams: the second player shows the other team
            if (type < 0 || type == 0 || type == 1 || type == 19 || type == 18 || type == 10) team = team == 0 ? 1 : 0;
        }
        colour = shown_colour(u3x_i01_colour(base, static_cast<int32_t>(team)));
    }
    event[0xf] = static_cast<int32_t>(colour);
    list->d[2]++;

    // the member's net player in the match overrides it once it has a team
    uint32_t np_team = NO_TEAM;
    if (m != list->pseudo) {
        if (const uint8_t *const member = match_member(base, m)) {
            const uint8_t *np = u3x_i01_np_by_code(base, rd<uint32_t>(member, SLOT_ID));
            if (guest && np) {
                const uint32_t code = (rd<uint32_t>(np, NP_ID) & 0xfffu) + (rd<int32_t>(np, NP_USER) != 0 ? 0x1000u : 0u);
                np = u3x_i01_np_by_code(base, code);
            }
            if (np && rd<int32_t>(np, NP_TEAM) >= 0) {
                np_team = rd<uint32_t>(np, NP_TEAM);
                event[0xf] = static_cast<int32_t>(shown_colour(u3x_i01_colour(base, static_cast<int32_t>(np_team))));
            }
        }
    }
    // the low byte is the team byte as the lobby data holds it now
    list->d[3] = rd<uint8_t>(data, MD_TEAM) | ((team & 0xffu) << 8) | ((np_team & 0xffu) << 16) | (row << 24);
    list->d[9] = static_cast<uint32_t>(event[0xf]);
}

// the data source of the party box: 1 = answered, 0 = the caller answers
U3X_FN int u3x_i01_party(uintptr_t base, int32_t *event, void *s, const char *selfname) {
    using namespace g34a;
    const bool focus = focus_on();   // the switch as it is on entry decides for the whole request
    PartyList *const list = party_list();
    const int32_t op = event[0];
    if (op == 9 || op == 6) u3x_i01_rows(base);

    if (op == 6) {   // last row index; also the per-frame jobs of the lobby
        event[1] = static_cast<int32_t>(list->row_count - 1);
        u3h_clan_tag_poll(base, s);
        u3h_hud_alias_names(base, s);
        u3h_stage_tick(base, s);
        u3h_loadout_tick(base, s);
        if (focus) u3x_i01_focus(base, s);
        return 1;
    }
    if (op == 5) {
        event[1] = list->selected;
        return 1;
    }
    if (op == 10) {
        list->selected = event[1];
        return 1;
    }
    if (op == 21) {   // may the box take the focus
        event[1] = focus && (list->in_box || list->alone) ? 1 : 0;
        return 1;
    }
    if (op == 8) {   // is the list shown
        event[1] = focus ? 1 : 0;
        return 1;
    }
    if (op == 1) return 0;   // the title is the caller's

    // a request about one row
    if (list->row_count == 0) u3x_i01_rows(base);
    const uint32_t r = static_cast<uint32_t>(event[1]);
    if (r >= list->row_count) return 1;
    const uint32_t index = list->rows[r] & 0xffu;
    if (index >= list->member_count) return 1;
    uint8_t *const m = list->members[index];
    const bool guest = (list->rows[r] & GUEST_ROW) != 0;
    const int own = u3x_i01_self(base, m);

    if (op == 2) {
        row_text(event, list, m, guest, own, selfname);
    } else if (op == 4) {
        if (!guest && static_cast<uint32_t>(event[0x14]) == 0xf5a294f3u) rank_badge(base, s, event, m, own);
    } else if (op == 11) {
        if (event[0x13] == 0) {   // is the row valid
            event[0xe] = 1;
        } else if (event[0x13] == 1 && rd<int32_t>(m, SLOT_CONN) != -1) {   // connection state
            event[0xe] = u3h_gt_conn_state(base, rd<int32_t>(m, SLOT_CONN));
        }
    } else if (op == 20) {
        if (!(rd<uint32_t>(s, 0x3d78) & 0x2000000u)) u3x_i01_row_colour(base, s, event, m, guest ? 1 : 0, r);
    }
    return 1;
}

}  // extern "C"
