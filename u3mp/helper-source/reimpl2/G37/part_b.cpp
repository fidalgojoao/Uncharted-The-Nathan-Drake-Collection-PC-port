// G37 part B: the matchmaking vote list (data source of the candidates' list on the matchmaking page), its lookups,
// the PS4 list control's two value queries, the five difficulty text ids and the shared image answers.
// Written from spec/G37_find_game.md (sections 2, 3, 5).

extern "C" {
uint8_t *u3x_sym_table(void *s, uint32_t id);
const char *u3h_text(uintptr_t base, void *s, uint32_t id);
int u3h_native_gate_closed(uintptr_t base, int32_t *ev);
uint32_t u3h_fr_tab(uintptr_t base, void *s);
int u3h_n06_on(void);
void u3h_n06_mapimg(uintptr_t base, void *s, int32_t *ev, uint8_t *map);
void u3h_h11_tag(int32_t *ev);
int u3h_n08_vq(uintptr_t base, int32_t *ev);

void u3x_retag_mm_vote(uint8_t *page);
uint8_t *u3x_mmv_netgame(void *s, int16_t i);
uint8_t *u3x_mmv_playlist(void *s, uint32_t id);
const uint8_t *u3x_mmv_rule(void *s, int32_t i);
int u3x_mmv_coop(uintptr_t base, void *s);
const char *u3x_mmv_diff(uintptr_t base, void *s, uint32_t n);
int u3x_mmv_source(uintptr_t base, int32_t *ev, void *s);
int u3x_vote_query(uint32_t query, int closed, int32_t my_vote, int32_t selected, int32_t winner, int32_t *answer);
const uint32_t *u3x_difficulty_texts(uintptr_t base);
void u3x_icon_answer(int32_t *ev, int32_t kind, int32_t a, int32_t b, int32_t c, int32_t index);
void u3x_icon_tinted(int32_t *ev, int32_t kind, int32_t value, int32_t tint);
}

namespace g37b {

// script symbols and the size of their entries
constexpr uint32_t SYM_NET_GAMES = 0x3b9a067du, SYM_MAPS = 0x0dec97a4u, SYM_RULES = 0xf985c7b6u, SYM_PLAYLISTS = 0xc1b0be0du;
constexpr uintptr_t NET_GAME_SIZE = 0xd0, MAP_SIZE = 0x98, RULE_SIZE = 0x58, PLAYLIST_SIZE = 0x68;

// game data and functions (image offsets)
constexpr uintptr_t SELECTED_NET_GAME = 0x17d167c, COOP_DIFFICULTY = 0x17d1aa8, LAN_BYTE = 0x17d5c60, COOP_BYTE = 0x17d6065;
constexpr uintptr_t MAP_VOTE = 0x17d7760, DIFFICULTY_VOTE = 0x17d7784;
constexpr uintptr_t FN_CHOSEN_PLAYLIST = 0x73f630, FN_PRINTF = 0x62aa60, DIFFICULTY_JUMPS = 0x7418c8;

// the vote list
constexpr uint32_t VOTE_PAGE = 0xe35f0f1au, VOTE_WIDGET = 0xdc6e89d8u, VOTE_TAG = 0x4c414e50u, VOTE_TAB = 0xbd1d2c53u;
constexpr uint32_t TEXT_VOTE_HEADER = 0x81cc5e93u, SLOT_NAME_SUBTITLE = 0x3cf3527au;
constexpr uint32_t NO_OWN_NAME_1 = 0x22a276ecu, NO_OWN_NAME_2 = 0x6f96651fu;

U3X_RO const char fmt_space[] = {'%', 's', ' ', '%', 's', 0};
U3X_RO const char fmt_dash[] = {'%', 's', ' ', '-', ' ', '%', 's', 0};
U3X_RO const char fmt_lines[] = {'%', 's', '\n', '%', 's', 0};
U3X_RO const char one_space[] = {' ', 0};
U3X_RO const char no_text[] = {0};
U3X_RO const uint8_t empty_rule[RULE_SIZE] = {};

U3X_BSS int32_t selected_row;          // the vote list's selected row
U3X_BSS uint32_t difficulty_ids[5];    // filled from the image by u3x_difficulty_texts

[[gnu::always_inline]] inline void set_text(int32_t *ev, const char *text) {
    *reinterpret_cast<const char **>(reinterpret_cast<uint8_t *>(ev) + 0x40) = text;
}

// the game's formatter (variadic) with two texts; the result is its static buffer
U3X_FN const char *format2(uintptr_t base, const char *format, const char *first, const char *second) {
    using Printf = const char *(*)(const char *, ...);
    return reinterpret_cast<Printf>(base + FN_PRINTF)(format, first, second);
}

// operation 2: the text of one column of a candidate (record and map entry are there)
U3X_FN void candidate_text(uintptr_t base, int32_t *ev, void *s, const uint8_t *record, const uint8_t *map) {
    const uint32_t slot = static_cast<uint32_t>(ev[0x15]);
    if (slot == 0) {
        set_text(ev, u3h_text(base, s, rd<uint32_t>(map, 4)));
    } else if (slot == 3) {
        const bool coop = u3x_mmv_coop(base, s) != 0;
        const uint8_t *rule = u3x_mmv_rule(s, rd<int32_t>(record, 0xc));
        const char *rules = u3h_text(base, s, rd<uint32_t>(rule, 0));
        if (!coop) {
            set_text(ev, rules);
            const uint32_t own = rd<uint32_t>(record, 8);
            if (own != NO_OWN_NAME_1 && own != NO_OWN_NAME_2) set_text(ev, u3h_text(base, s, own));
        } else if (at<uint8_t>(base + DIFFICULTY_VOTE + 1) == 0) {
            set_text(ev, format2(base, fmt_space, rules, no_text));
        } else {
            const char *difficulty = u3x_mmv_diff(base, s, at<uint8_t>(base + COOP_DIFFICULTY));
            set_text(ev, format2(base, fmt_dash, rules, difficulty));
        }
    } else if (slot == SLOT_NAME_SUBTITLE) {
        const bool no_subtitle = rd<uint32_t>(map, 8) == 0;
        const char *name = u3h_text(base, s, rd<uint32_t>(map, 4));
        if (no_subtitle) {
            set_text(ev, name);
        } else {
            // the subtitle id is read again from the map entry, after the name lookup
            const char *subtitle = u3h_text(base, s, rd<uint32_t>(map, 8));
            set_text(ev, format2(base, fmt_lines, name, subtitle));
        }
    }
}

// operation 4: the picture of a candidate
U3X_FN void candidate_picture(uintptr_t base, int32_t *ev, void *s, const uint8_t *record) {
    const bool card = u3h_n06_on() != 0;
    const uint32_t map_id = rd<uint32_t>(record, 4);   // read after the switch was asked
    if (card) {
        u3h_n06_mapimg(base, s, ev, u3x_mmv_playlist(s, map_id));
        u3h_h11_tag(ev);
    } else {
        u3x_icon_answer(ev, 36, static_cast<int32_t>(map_id), 0, 0, -1);
    }
}

// operation 11: votes and state of the list
U3X_FN void value_query(uintptr_t base, int32_t *ev, uint32_t candidate) {
    const uint8_t *vote = reinterpret_cast<const uint8_t *>(base + MAP_VOTE);
    const bool closed = rd<uint8_t>(vote, 1) != 0;
    const uint32_t query = static_cast<uint32_t>(ev[0x13]);
    if (query == 0xbedebbb3u) {            // the candidate's votes
        const int32_t count = rd<int32_t>(vote, 0x10 + 4 * candidate);
        ev[0xe] = count != 0 ? count : closed ? -1 : 0;
    } else if (query == 0x88abe7afu) {     // did the other candidate win
        if (!closed) ev[0xe] = -1;
        else ev[0xe] = at<int16_t>(base + SELECTED_NET_GAME) != rd<int32_t>(vote, 8) ? 1 : 0;
    } else if (query == 0xe87f9498u) {     // no own vote yet (unsigned test)
        ev[0xe] = !closed && rd<uint32_t>(vote, 4) > 1 ? 1 : 0;
    } else if (query == 0x7bc431fbu) {     // the own vote
        ev[0xe] = rd<int32_t>(vote, 4);
    } else {
        u3h_n08_vq(base, ev);
    }
}

}  // namespace g37b

// A loaded matchmaking page: its vote list widget gets the helper's source number and the vote list's tag.
extern "C" U3X_FN void u3x_retag_mm_vote(uint8_t *page) {
    if (rd<uint32_t>(page, 8) != g37b::VOTE_PAGE) return;
    const uint8_t *list = rd<const uint8_t *>(page, 0x48);
    if (!list) return;
    uint8_t *widget = rd<uint8_t *>(list, 8);
    const uint32_t count = rd<uint32_t>(list, 0);
    if (!widget || count > 64) return;
    for (uint32_t i = 0; i < count; i++, widget += 0x68) {
        if (rd<uint32_t>(widget, 8) != g37b::VOTE_WIDGET || rd<uint32_t>(widget, 0x30) != 21) continue;
        if (rd<uint32_t>(widget, 0x34) != 0x50 || rd<uint32_t>(widget, 0x40) != 0) continue;
        *reinterpret_cast<uint32_t *>(widget + 0x34) = 0xcf;
        *reinterpret_cast<uint32_t *>(widget + 0x40) = g37b::VOTE_TAG;
        ext_eb()[170]++;
    }
}

// Net game record i, or null (the record pointer itself is not tested).
extern "C" U3X_FN uint8_t *u3x_mmv_netgame(void *s, int16_t i) {
    const uint8_t *table = u3x_sym_table(s, g37b::SYM_NET_GAMES);
    if (!table || i < 0 || i >= rd<int32_t>(table, 0)) return nullptr;
    return rd<uint8_t *>(table, 8) + g37b::NET_GAME_SIZE * static_cast<uintptr_t>(i);
}

// The first entry of the map table with this map id, or null.
extern "C" U3X_FN uint8_t *u3x_mmv_playlist(void *s, uint32_t id) {
    const uint8_t *table = u3x_sym_table(s, g37b::SYM_MAPS);
    if (!table) return nullptr;
    uint8_t *entry = rd<uint8_t *>(table, 8);
    if (!entry) return nullptr;
    const int32_t count = rd<int32_t>(table, 0);
    for (int32_t i = 0; i < count; i++, entry += g37b::MAP_SIZE)
        if (rd<uint32_t>(entry, 0) == id) return entry;
    return nullptr;
}

// Rule set i (indexed by game type); the helper's empty rule set when there is none.
extern "C" U3X_FN const uint8_t *u3x_mmv_rule(void *s, int32_t i) {
    const uint8_t *table = u3x_sym_table(s, g37b::SYM_RULES);
    if (!table || i < 0 || i >= rd<int32_t>(table, 0)) return g37b::empty_rule;
    const uint8_t *entries = rd<const uint8_t *>(table, 8);
    if (!entries) return g37b::empty_rule;
    return entries + g37b::RULE_SIZE * static_cast<uintptr_t>(i);
}

// 1 when the playlist the player has chosen is a co-op one.
extern "C" U3X_FN int u3x_mmv_coop(uintptr_t base, void *s) {
    const uint32_t key = game_call<uint32_t>(base + g37b::FN_CHOSEN_PLAYLIST);   // always, and before the table test
    const uint8_t *table = u3x_sym_table(s, g37b::SYM_PLAYLISTS);
    if (!table) return 0;
    const uint8_t *entry = rd<const uint8_t *>(table, 8);
    if (!entry) return 0;
    const int32_t count = rd<int32_t>(table, 0);
    for (int32_t i = 0; i < count; i++, entry += g37b::PLAYLIST_SIZE) {
        const uint32_t coop = rd<uint32_t>(entry, 0x28);
        // on LAN only the playlists of the chosen side count
        if (at<uint8_t>(base + g37b::LAN_BYTE) != 0 && coop != at<uint8_t>(base + g37b::COOP_BYTE)) continue;
        if (key == 0 || rd<uint32_t>(entry, 0) == key) return coop != 0;
    }
    return 0;
}

// The text ids of the five co-op difficulty names, read from the game's own "name of a difficulty" code: its jump
// table gives the code place of each difficulty, and the id is the 32-bit immediate 0x18 bytes into it.
extern "C" U3X_FN const uint32_t *u3x_difficulty_texts(uintptr_t base) {
    const uint8_t *jumps = reinterpret_cast<const uint8_t *>(base + g37b::DIFFICULTY_JUMPS);
    for (uint32_t n = 0; n < 5; n++) {
        const uint8_t *id = jumps + rd<int32_t>(jumps, 4 * n) + 0x18;
        g37b::difficulty_ids[n] = static_cast<uint32_t>(id[0]) | static_cast<uint32_t>(id[1]) << 8 |
                                  static_cast<uint32_t>(id[2]) << 16 | static_cast<uint32_t>(id[3]) << 24;
    }
    return g37b::difficulty_ids;
}

// The name of difficulty n (0 .. 4); the empty text for any other number.
extern "C" U3X_FN const char *u3x_mmv_diff(uintptr_t base, void *s, uint32_t n) {
    if (n >= 5) return g37b::no_text;
    return u3h_text(base, s, u3x_difficulty_texts(base)[n]);
}

// The data source of the vote list: 1 when the request is the vote list's.
extern "C" U3X_FN int u3x_mmv_source(uintptr_t base, int32_t *ev, void *s) {
    const uint8_t *descriptor = rd<const uint8_t *>(ev, 0x98);
    if (!descriptor || rd<uint32_t>(descriptor, 0x1c) != g37b::VOTE_TAG) return 0;
    if (u3h_native_gate_closed(base, ev)) return 1;
    ext_eb()[171]++;

    const uint8_t *vote = reinterpret_cast<const uint8_t *>(base + g37b::MAP_VOTE);
    if (rd<uint8_t>(vote, 0) == 0) return 1;
    if (u3h_fr_tab(base, s) != g37b::VOTE_TAB) return 1;

    // the difficulty vote decides whether the list shows
    const bool card_on = u3h_n06_on() != 0;   // asked first; the two bytes are read afterwards
    const bool diff_open = at<uint8_t>(base + g37b::DIFFICULTY_VOTE) != 0;
    const bool diff_closed = at<uint8_t>(base + g37b::DIFFICULTY_VOTE + 1) != 0;
    if (card_on ?(diff_open && !diff_closed) : (!diff_open && !diff_closed)) return 1;

    uint32_t *eb = ext_eb();
    eb[172]++;
    eb[173] = static_cast<uint32_t>(ev[0]);

    const int32_t operation = ev[0];
    const uint32_t candidate = ev[1] != 0 ? 1 : 0;
    const int16_t net_game = static_cast<int16_t>(rd<int32_t>(vote, 8 + 4 * candidate));
    if (operation == 1) {                  // header
        g37b::set_text(ev, rd<uint8_t>(vote, 1) != 0 ? g37b::one_space : u3h_text(base, s, g37b::TEXT_VOTE_HEADER));
    } else if (operation == 2) {           // row text
        if (const uint8_t *record = u3x_mmv_netgame(s, net_game))
            if (const uint8_t *map = u3x_mmv_playlist(s, rd<uint32_t>(record, 4)))
                g37b::candidate_text(base, ev, s, record, map);
    } else if (operation == 4) {           // row picture
        if (const uint8_t *record = u3x_mmv_netgame(s, net_game)) g37b::candidate_picture(base, ev, s, record);
    } else if (operation == 5) {           // selected row
        ev[1] = g37b::selected_row;
    } else if (operation == 6 || operation == 8) {
        ev[1] = 1;
    } else if (operation == 10) {          // select a row
        g37b::selected_row = ev[1];
    } else if (operation == 11) {          // value query
        g37b::value_query(base, ev, candidate);
    }
    return 1;
}

// The PS4 list control's own two queries.
extern "C" U3X_FN int u3x_vote_query(uint32_t query, int closed, int32_t my_vote, int32_t selected, int32_t winner, int32_t *answer) {
    if (query == 0x7754e7afu) {          // which row won
        *answer = !closed ? -1 : selected == winner ? 0 : 1;
        return 1;
    }
    if (query == 0x17809498u) {          // may the list take the focus
        *answer = !closed && (my_vote < 0 || my_vote > 1) ? 1 : 0;
        return 1;
    }
    return 0;
}

// The image answer of a list source.
extern "C" U3X_FN void u3x_icon_answer(int32_t *ev, int32_t kind, int32_t a, int32_t b, int32_t c, int32_t index) {
    ev[2] = kind;
    ev[4] = 0;
    ev[5] = 0;
    ev[6] = a;
    ev[7] = b;
    ev[8] = c;
    ev[9] = index;
    ev[10] = 0;
    ev[11] = -1;
    ev[12] = static_cast<int32_t>(F_ONE);
}

// The image answer with a tint.
extern "C" U3X_FN void u3x_icon_tinted(int32_t *ev, int32_t kind, int32_t value, int32_t tint) {
    ev[2] = kind;
    ev[4] = 0;
    ev[5] = 0;
    ev[6] = value;
    ev[7] = 0;
    ev[8] = 0;
    ev[9] = -1;
    ev[10] = 0;
    ev[11] = tint;
    ev[12] = static_cast<int32_t>(F_ONE);
}
