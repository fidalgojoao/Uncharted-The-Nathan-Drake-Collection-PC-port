// G19 part 1: what the Friends page and the Friend Actions page show (list rows, friend header, small helpers).
// Written from spec/G19_friends_rows.md, sections 4.1 to 4.9.

extern "C" {
// the C part and other groups
int u3h_gate_closed(uintptr_t base, int32_t *ev);
void u3h_so_note(const char *a, const char *name, int32_t v, const char *b);
const char *u3h_text(uintptr_t base, void *s, uint32_t id);
void u3h_text_num(uintptr_t base, void *s, char *buf, uint32_t size, uint32_t id, int32_t v);
char *u3h_fr_buffer(void);
uint8_t *u3h_fr_ent(uintptr_t base, uint32_t i);
uint8_t *u3h_fr_find(uintptr_t base, const uint8_t *name);
int u3h_fr_in_session(uintptr_t base, uint8_t *e);
int u3h_fr_in_title(uint8_t *e);
uint8_t *u3h_fr_pdata(uintptr_t base, uint8_t *e);
void u3h_fr_refresh(uintptr_t base);
void u3h_set_icon_plain(uintptr_t base, int32_t *ev, uint32_t id);
uint32_t u3x_menu_page(const uint8_t *m);
uint8_t *u3x_sym_table(void *s, uint32_t id);

// this part
void u3x_friend_party_suffix(uintptr_t base, void *s, char *buf, uint8_t size);
const char *u3x_friend_join(const char *a, const char *b);
uint32_t u3x_friend_activity(uint8_t a);
void u3x_friend_icon(int32_t *ev, int32_t v);
int32_t u3x_friend_state_icon(uintptr_t base, uint8_t *e);
uint32_t u3x_page_tab_text(uintptr_t base, void *s);
int u3x_friend_list(uintptr_t base, int32_t *ev, void *s);
int u3x_friend_header(uintptr_t base, int32_t *ev, void *s);
}

namespace g19p1 {

// image offsets
constexpr uintptr_t FRIENDS = 0x17caeb0;        // the friend object: +0 count, +8 entries, +0xc8 busy marker
constexpr uintptr_t MENU = 0x17bd080;
constexpr uintptr_t LAN_BYTE = 0x17d5c60;
constexpr uintptr_t SELECTED_ID = 0x17c3871;
constexpr uintptr_t OWN_NAME = 0x17d6010;
constexpr uintptr_t PARTY_LOCK_SETTING = 0x17c3570;
constexpr uintptr_t PARTY_LOCKED = 0x17dd584;
constexpr uintptr_t TEXT_DATABASE = 0x17026b8;
constexpr uintptr_t FN_TIME_TEXT = 0xf33010;
constexpr uintptr_t FN_DIFFICULTY_NAME = 0x741600;

// source tags, script symbols, image slots
constexpr uint32_t TAG_LIST = 0x4c414eae, TAG_HEADER = 0x4c414e13;
constexpr uint32_t SYM_TABS = 0xf1514340, SYM_NET_GAMES = 0x3b9a067d, SYM_RULE_SETS = 0xf985c7b6, SYM_MAPS = 0x0dec97a4;
constexpr uint32_t SLOT_ROW_ICON = 0xf5a294f3, SLOT_AVATAR = 0xb81e2263, SLOT_NET_GAME = 0x0a071025,
                   SLOT_GAME_TYPE = 0x57135906, SLOT_STATE = 0x8e39ed40;

// text ids
constexpr uint32_t TAB_FRIENDS = 0x6e83df53;
constexpr uint32_t TXT_ONLINE = 0x51663192, TXT_OFFLINE = 0x6ea711d6, TXT_CAMPAIGN = 0xf461ef26, TXT_TITLE = 0xfd2de693,
                   TXT_OTHER_GAME = 0x953825d0, TXT_PARTY_SIZE = 0xba8db526, TXT_MATCH_ENDS = 0x93ac6446,
                   TXT_PARTY_IS_LOCKED = 0x40d3b7a2, TXT_PARTY_IS_OPEN = 0x052ac89a, TXT_LOBBY = 0x84041d79,
                   TXT_MATCHMAKING = 0xbd1d2c53, TXT_IN_MATCH = 0xcf292b57, TXT_MATCH_ENDED = 0xea3bb70e;

U3X_RO const char empty_text[] = "";
U3X_RO const char note_head[] = "Friends list: op ";
U3X_RO const char note_tail[] = " asked (first time)";
U3X_RO const char dash[] = "- ";
U3X_RO const char slash[] = " / ";
U3X_RO const char next_line[] = "\n- ";
U3X_RO const char second_name_markup[] = "|@C0xa0a0a0||FB||@R0xffffffff| ";
static_assert(sizeof(second_name_markup) == 32);
U3X_RO const uint8_t empty_rule_set[0x58] = {};   // what an unknown game type gets: every field 0

U3X_BSS static int32_t selection;      // the Friends list's remembered selection
U3X_BSS static uint32_t noted_ops;     // bit k = the log line of operation k was written

// ---- the list request
U3X_FN static void set_text(int32_t *ev, const char *text) {
    *reinterpret_cast<const char **>(reinterpret_cast<uint8_t *>(ev) + 0x40) = text;
}

U3X_FN static void set_image(int32_t *ev, int32_t kind, int32_t value) {
    ev[2] = kind;
    ev[4] = 0;
    ev[5] = 0;
    ev[6] = value;
    ev[7] = 0;
    ev[8] = 0;
    ev[9] = -1;
    ev[10] = 0;
    ev[11] = -1;
    ev[12] = static_cast<int32_t>(F_ONE);
}

// is this request for the source with this tag
U3X_FN static bool for_source(const int32_t *ev, uint32_t tag) {
    const uint8_t *descriptor = rd<const uint8_t *>(ev, 0x98);
    return descriptor && rd<uint32_t>(descriptor, 0x1c) == tag;
}

// ---- bounded join: texts one after the other in a 0x100-byte buffer, at most 0xff characters and a NUL
struct Join {
    char *buf;
    uint32_t n;
};

U3X_FN static void join_add(Join &j, const char *text) {
    while (j.n < 0xff && *text) j.buf[j.n++] = *text++;
}

U3X_FN static char *join_end(Join &j) {
    j.buf[j.n] = 0;
    return j.buf;
}

// ---- script tables
// the net game record of a presence data's index, or null
U3X_FN static const uint8_t *net_game(void *s, int32_t index) {
    const int16_t i = static_cast<int16_t>(index);
    const uint8_t *table = u3x_sym_table(s, SYM_NET_GAMES);
    if (!table || i < 0 || i >= rd<int32_t>(table, 0)) return nullptr;
    return rd<const uint8_t *>(table, 8) + static_cast<int64_t>(i) * 0xd0;
}

// the rule set of a net game record's game type; the empty one for an unknown type
U3X_FN static const uint8_t *rule_set(void *s, const uint8_t *game) {
    const uint8_t *table = u3x_sym_table(s, SYM_RULE_SETS);
    const int32_t type = rd<int32_t>(game, 0xc);
    if (table && type < rd<int32_t>(table, 0)) return rd<const uint8_t *>(table, 8) + static_cast<int64_t>(type) * 0x58;
    return empty_rule_set;
}

// the map table's entry of a map id, or null (the entry's byte offset is a 32-bit product)
U3X_FN static const uint8_t *map_entry(void *s, uint32_t map_id) {
    const uint8_t *table = u3x_sym_table(s, SYM_MAPS);
    if (!table) return nullptr;
    const int32_t count = rd<int32_t>(table, 0);
    for (int32_t i = 0; i < count; i++) {
        const uint8_t *entry = rd<const uint8_t *>(table, 8) + static_cast<uint32_t>(i) * 0x98u;
        if (rd<uint32_t>(entry, 0) == map_id) return entry;
    }
    return nullptr;
}

// ---- the Friends list
// the friend entry of the request's row, or null when the row cannot be shown
U3X_FN static uint8_t *row_entry(uintptr_t base, const int32_t *ev) {
    const uintptr_t friends = base + FRIENDS;
    const int32_t row = ev[1];
    if (at<uint64_t>(friends + 0xc8)) return nullptr;
    if (row < 0 || static_cast<uint32_t>(row) >= at<uint32_t>(friends)) return nullptr;
    if (!at<uint64_t>(friends + 8)) return nullptr;
    return u3h_fr_ent(base, static_cast<uint32_t>(row));
}

// the status column: Online / Offline, Campaign, or the activity with the party size
U3X_FN static void status_text(uintptr_t base, int32_t *ev, void *s, uint8_t *e) {
    const int32_t state = rd<int32_t>(e, 0x1b0);
    set_text(ev, u3h_text(base, s, state ? TXT_ONLINE : TXT_OFFLINE));
    if (u3h_fr_in_title(e)) set_text(ev, u3h_text(base, s, TXT_CAMPAIGN));
    const uint8_t *data = u3h_fr_pdata(base, e);
    if (!data || state != 1) return;
    char suffix[0x80];
    u3x_friend_party_suffix(base, s, suffix, data[5]);
    const uint32_t id = u3x_friend_activity(data[4]);
    if (!id) return;
    const char *activity = u3h_text(base, s, id);
    set_text(ev, u3x_friend_join(activity, suffix));
}

U3X_FN static void row_text(uintptr_t base, int32_t *ev, void *s, uint8_t *e) {
    const uint32_t column = static_cast<uint32_t>(ev[0x15]);
    if (column == 0) {
        set_text(ev, reinterpret_cast<const char *>(e + 1));
    } else if (column == 1) {
        status_text(base, ev, s, e);
    } else if (column == 2) {
        const char *second_name = reinterpret_cast<const char *>(e + 0x1c0);
        if (!*second_name) return;
        Join j{u3h_fr_buffer(), 0};
        join_add(j, second_name_markup);
        join_add(j, second_name);
        set_text(ev, join_end(j));
    }
}

// ---- the friend header
// no friend is picked: the own name, the title, the own party's lock state
U3X_FN static void own_header(uintptr_t base, int32_t *ev, void *s) {
    const int32_t op = ev[0];
    if (op == 1) {
        set_text(ev, reinterpret_cast<const char *>(base + OWN_NAME));
    } else if (op == 2 && ev[1] == 0) {
        set_text(ev, u3h_text(base, s, TXT_TITLE));
    } else if (op == 2 && ev[1] == 1) {
        const bool locked = at<uint8_t>(base + PARTY_LOCKED) || at<uint8_t>(base + PARTY_LOCK_SETTING);
        set_text(ev, u3h_text(base, s, locked ? TXT_PARTY_IS_LOCKED : TXT_PARTY_IS_OPEN));
    }
}

U3X_FN static void header_image(uintptr_t base, int32_t *ev, void *s, uint8_t *e) {
    const uint8_t *data = u3h_fr_pdata(base, e);
    const uint8_t *game = data ? net_game(s, rd<int32_t>(data, 0x14)) : nullptr;
    const uint32_t slot = static_cast<uint32_t>(ev[0x14]);
    if (slot == SLOT_AVATAR) {
        set_image(ev, 0, 0);
    } else if (slot == SLOT_NET_GAME) {
        if (game) set_image(ev, 0x24, rd<int32_t>(game, 4));
    } else if (slot == SLOT_GAME_TYPE) {
        if (game) u3h_set_icon_plain(base, ev, rd<uint32_t>(rule_set(s, game), 8));
    } else if (slot == SLOT_STATE) {
        u3x_friend_icon(ev, u3x_friend_state_icon(base, e));
    }
}

// row 0: what the friend plays
U3X_FN static void header_game(uintptr_t base, int32_t *ev, void *s, uint8_t *e) {
    const int32_t state = rd<int32_t>(e, 0x1b0);
    if ((u3h_fr_pdata(base, e) && state == 1) || u3h_fr_in_title(e))
        set_text(ev, u3h_text(base, s, TXT_TITLE));
    else if (state != 0)
        set_text(ev, u3h_text(base, s, TXT_OTHER_GAME));
}

// "Match ends in ..." into a 0x80-byte buffer; an empty text when the match has no end time
U3X_FN static void time_text(uintptr_t base, const uint8_t *data, char *buf) {
    for (int i = 0; i < 0x80; i++) buf[i] = 0;
    const int64_t end = rd<int64_t>(data, 0x20);
    if (!end) return;
    const uintptr_t clock = rd<uintptr_t>(u3h_ext(), 0x4060);
    const int64_t now = clock ? game_call<int64_t>(clock, static_cast<void *>(nullptr)) : 0;
    const int64_t left = static_cast<int64_t>(static_cast<uint64_t>(end) - static_cast<uint64_t>(now));
    const int64_t shown = left < 1 ? 0 : left > 215999 ? 216000 : left;
    game_call<void>(base + FN_TIME_TEXT, reinterpret_cast<void *>(base + TEXT_DATABASE), buf, uint32_t{0x80}, TXT_MATCH_ENDS,
                    shown, int64_t{0});
}

// row 1 of a friend in a match: map, game type, then the difficulty or the rules and the time left
U3X_FN static void match_line(uintptr_t base, int32_t *ev, void *s, const uint8_t *data) {
    const uint8_t *game = net_game(s, rd<int32_t>(data, 0x14));
    if (!game) return;
    const uint8_t *map = map_entry(s, rd<uint32_t>(game, 4));
    if (!map) return;
    char time_left[0x80];
    time_text(base, data, time_left);
    const char *map_name = u3h_text(base, s, rd<uint32_t>(map, 4));
    const uint8_t *rules = rule_set(s, game);
    const char *type_name = u3h_text(base, s, rd<uint32_t>(rules, 0));

    Join j{u3h_fr_buffer(), 0};
    join_add(j, dash);
    join_add(j, map_name);
    join_add(j, slash);
    join_add(j, type_name);
    const uint32_t type = rd<uint32_t>(game, 0xc);
    const bool coop = type == 4 || type == 11 || type == 16;
    const bool short_form = type == 8 || type == 9 || type == 13 || type == 17 || type == 18;
    if (coop) {
        join_add(j, slash);
        join_add(j, game_call<const char *>(base + FN_DIFFICULTY_NAME, rd<int32_t>(data, 0x18)));
    } else if (!short_form) {
        join_add(j, slash);
        join_add(j, u3h_text(base, s, rd<uint32_t>(game, 8)));
        join_add(j, next_line);
        join_add(j, time_left);
    }
    set_text(ev, join_end(j));
}

// row 1: what the friend does
U3X_FN static void header_activity(uintptr_t base, int32_t *ev, void *s, uint8_t *e) {
    const uint8_t *data = u3h_fr_pdata(base, e);
    if (!data || rd<int32_t>(e, 0x1b0) != 1) return;
    char suffix[0x80];
    u3x_friend_party_suffix(base, s, suffix, data[5]);
    const uint8_t activity = data[4];
    if (activity == 2) {
        match_line(base, ev, s, data);
    } else if (activity <= 1 || activity == 3) {
        const char *text = u3h_text(base, s, activity == 3 ? TXT_MATCH_ENDED : TXT_LOBBY);
        set_text(ev, u3x_friend_join(text, suffix));
    }
}

}  // namespace g19p1

// 4.1 " - [n] players in party" for a party of two or more, else an empty text; buf is 0x80 bytes
U3X_FN void u3x_friend_party_suffix(uintptr_t base, void *s, char *buf, uint8_t size) {
    for (int i = 0; i < 0x80; i++) buf[i] = 0;
    if (size < 2) return;
    buf[0] = ' ';
    buf[1] = '-';
    buf[2] = ' ';
    u3h_text_num(base, s, buf + 3, 0x7d, g19p1::TXT_PARTY_SIZE, size);
}

// 4.2 a followed by b in the next rotating buffer
U3X_FN const char *u3x_friend_join(const char *a, const char *b) {
    g19p1::Join j{u3h_fr_buffer(), 0};
    g19p1::join_add(j, a);
    g19p1::join_add(j, b);
    return g19p1::join_end(j);
}

// 4.3 the text id of an activity value, 0 for an unknown one
U3X_FN uint32_t u3x_friend_activity(uint8_t a) {
    if (a == 0) return g19p1::TXT_LOBBY;
    if (a == 1) return g19p1::TXT_MATCHMAKING;
    if (a == 2) return g19p1::TXT_IN_MATCH;
    if (a == 3) return g19p1::TXT_MATCH_ENDED;
    return 0;
}

// 4.4 the state icon answer
U3X_FN void u3x_friend_icon(int32_t *ev, int32_t v) {
    g19p1::set_image(ev, 0x22, v);
}

// 4.5 2 = in the own party session, 1 = online, 0 = offline
U3X_FN int32_t u3x_friend_state_icon(uintptr_t base, uint8_t *e) {
    if (u3h_fr_in_session(base, e)) return 2;
    return rd<int32_t>(e, 0x1b0) ? 1 : 0;
}

// 4.6 the text id of the selected tab of the page on top, 0 when the tab table has no row for it. The byte offsets
// of a row and of the selected tab are 32-bit products (they wrap at 2^32) added to the 64-bit pointers.
U3X_FN uint32_t u3x_page_tab_text(uintptr_t base, void *s) {
    const uint8_t *table = u3x_sym_table(s, g19p1::SYM_TABS);
    if (!table) return 0;
    const uint32_t page = u3x_menu_page(reinterpret_cast<const uint8_t *>(base + g19p1::MENU));
    const uint32_t lan = at<uint8_t>(base + g19p1::LAN_BYTE);
    const uint32_t rows = rd<uint32_t>(table, 0);
    for (uint32_t i = 0; i < rows; i++) {
        const uint8_t *row = rd<const uint8_t *>(table, 8) + i * 0x20u;
        if (rd<uint32_t>(row, 0) != page || rd<uint32_t>(row, 4) != lan) continue;
        const uint8_t *tabs = rd<const uint8_t *>(row, 0x10);
        return rd<uint32_t>(tabs, rd<uint32_t>(row, 0x18) * 0xcu);
    }
    return 0;
}

// 4.7 the Friends list source
U3X_FN int u3x_friend_list(uintptr_t base, int32_t *ev, void *s) {
    using namespace g19p1;
    if (!for_source(ev, TAG_LIST)) return 0;
    if (u3h_gate_closed(base, ev)) return 1;
    ext_ea()[85]++;
    const int32_t op = ev[0];

    // each operation is noted once in the online log, the first time it arrives on the Friends tab
    if (op >= 0 && op <= 31 && !(noted_ops >> op & 1) && u3x_page_tab_text(base, s) == TAB_FRIENDS) {
        noted_ops |= 1u << op;
        u3h_so_note(note_head, nullptr, op, note_tail);
    }

    if (u3x_page_tab_text(base, s) != TAB_FRIENDS) {
        if (op == 6) ev[1] = -1;
        return 1;
    }

    const uintptr_t friends = base + FRIENDS;
    if (op == 1) {
        set_text(ev, empty_text);
    } else if (op == 5) {
        ev[1] = selection;
    } else if (op == 6) {   // the last row index
        // count - 1 as a signed 32-bit number; an answer below 0 becomes 0
        const int32_t last = static_cast<int32_t>(at<uint32_t>(friends) - 1);
        ev[1] = at<uint64_t>(friends + 0xc8) || last < 0 ? 0 : last;
    } else if (op == 8) {
        ev[1] = 1;
    } else if (op == 9) {
        u3h_fr_refresh(base);
    } else if (op == 10) {
        selection = ev[1];
    } else if (op == 2 || op == 4) {
        uint8_t *e = row_entry(base, ev);
        if (!e) return 1;
        if (op == 2)
            row_text(base, ev, s, e);
        else if (static_cast<uint32_t>(ev[0x14]) == SLOT_ROW_ICON)
            u3x_friend_icon(ev, u3x_friend_state_icon(base, e));
    }
    return 1;
}

// 4.9 the friend header source: the friend that was picked, or the own player when it is no friend
U3X_FN int u3x_friend_header(uintptr_t base, int32_t *ev, void *s) {
    using namespace g19p1;
    if (!for_source(ev, TAG_HEADER)) return 0;
    if (u3h_gate_closed(base, ev)) return 1;
    uint8_t *e = u3h_fr_find(base, reinterpret_cast<const uint8_t *>(base + SELECTED_ID));
    ext_ea()[86]++;
    const int32_t op = ev[0];

    if (!e) {
        own_header(base, ev, s);
    } else if (op == 1) {
        set_text(ev, reinterpret_cast<const char *>(e + 1));
    } else if (op == 4) {
        header_image(base, ev, s, e);
    } else if (op == 2 && ev[1] == 0) {
        header_game(base, ev, s, e);
    } else if (op == 2 && ev[1] == 1) {
        header_activity(base, ev, s, e);
    }
    return 1;
}
