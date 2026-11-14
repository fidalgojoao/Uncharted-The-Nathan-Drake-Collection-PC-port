// G31 part B: the list source of the lobby's main menu list and of the party list (u3x_lobby_source).
// Written from spec G31_mp_page_events.md, section 4.4.

extern "C" {
// the C part and other groups (spec section 2)
int u3h_gate_closed(uintptr_t base, int32_t *ev);
const char *u3h_text(uintptr_t base, void *s, uint32_t id);
int32_t u3h_icon_index(uintptr_t base, uint32_t id);
void u3h_set_icon_plain(uintptr_t base, int32_t *ev, uint32_t id);
const uint32_t *u3h_lobby_rows(uintptr_t base, uint32_t *n);
int u3h_profile_new_marker(uintptr_t base, int32_t *ev);
const char *u3h_onp_id(void *s);
uint8_t *u3h_player_profile(uintptr_t base);
int u3h_onm_players(uintptr_t base, int32_t *ev, void *s, const char *name);
int u3x_m03_players(uintptr_t base, int32_t *ev, void *s, const char *self_name);
int u3h_i01_party(uintptr_t base, int32_t *ev, void *s, const char *name);
const char *u3x_lan_row_name(uintptr_t base, int row);
int u3x_lan_rows(uintptr_t base);
void u3h_rank_icon(uintptr_t base, void *s, int32_t *ev);
void u3x_clan_tag_poll(uintptr_t base, void *s);
void u3h_hud_alias_names(uintptr_t base, void *s);
void u3h_stage_tick(uintptr_t base, void *s);
void u3h_loadout_tick(uintptr_t base, void *s);
void u3x_icon_answer(int32_t *ev, int32_t kind, int32_t a, int32_t b, int32_t c, int32_t index);

int u3x_lobby_source(uintptr_t base, int32_t *ev, void *s);
}

namespace g31b {
namespace {

U3X_RO const char empty_text[] = {0};
U3X_RO const char party_title[] = {'P', 'A', 'R', 'T', 'Y', 0};

// source tags
constexpr uint32_t TAG_MAIN_MENU = 0x4c414eb3u;
constexpr uint32_t TAG_PARTY = 0x4c414eadu;

constexpr uint32_t SLOT_ROW_ICON = 0xf5a294f3u;    // image slot: the row's icon (slot 0 = the status picture)
constexpr uint32_t ACTION_PROFILE = 0xc4acc827u;   // action id of the Profile row

// request words
constexpr int RQ_OP = 0, RQ_ROW = 1, RQ_IMAGE = 2, RQ_VALUE = 0xe, RQ_COLOUR = 0xf, RQ_VALUE_ID = 0x13,
              RQ_IMAGE_SLOT = 0x14, RQ_TEXT_SLOT = 0x15, RQ_COLOUR_ASK = 0x16;

// state object
constexpr uintptr_t S_LAN_PROFILES = 128, S_LAN_SELECTED = 132, S_MENU_ROW = 692, S_NATIVE_RANK = 768 + 8 * 21,
                    S_MENU_DIAG = 0x2300, S_NAME_BUFFER = 0x2b80, S_SWITCHES = 0x3d78;
constexpr int NAME_MAX = 100;   // characters of the own party row text (the buffer has one more for the NUL)

// one row of the lobby's main menu, as u3h_lobby_rows delivers it
struct MenuRow { uint32_t text, icon, action; };

using RankFn = uint32_t (*)(uint8_t *profile);   // native 21: the rank word of a player profile

[[gnu::always_inline]] inline uint32_t word(const int32_t *ev, int n) { return static_cast<uint32_t>(ev[n]); }
[[gnu::always_inline]] inline void set_word(int32_t *ev, int n, uint32_t v) { ev[n] = static_cast<int32_t>(v); }
[[gnu::always_inline]] inline void answer_text(int32_t *ev, const char *text) {
    *reinterpret_cast<const char **>(reinterpret_cast<uint8_t *>(ev) + 0x40) = text;
}
[[gnu::always_inline]] inline uint32_t *state_words(void *s, uintptr_t offset) {
    return reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(s) + offset);
}
[[gnu::always_inline]] inline uint32_t *ext_words(uintptr_t offset) {
    return reinterpret_cast<uint32_t *>(u3h_ext() + offset);
}

// ---- the main menu list ----------------------------------------------------------------------------------------

// operation 4: the diagnostics words, then the row's icon, the "new" marker of the Profile row, or no picture.
// item = the row asked, null when the row index is out of range.
U3X_FN void main_menu_image(uintptr_t base, int32_t *ev, void *s, uint32_t row, const MenuRow *item) {
    const uint32_t slot = word(ev, RQ_IMAGE_SLOT);

    uint32_t *diag = state_words(s, S_MENU_DIAG);
    diag[0] += 1;
    diag[1] = slot;
    diag[2] = row;
    const uint8_t *ui = at<const uint8_t *>(base + 0x1b24bc8);
    const uint8_t *textures = ui ? rd<const uint8_t *>(ui, 0x28) : nullptr;
    diag[4] = textures ? rd<uint16_t>(textures, 2) : 0xffffffffu;
    if (item) diag[3] = static_cast<uint32_t>(u3h_icon_index(base, item->icon));
    diag[5 + (diag[0] & 7)] = slot;

    if (item && slot == SLOT_ROW_ICON) {
        u3h_set_icon_plain(base, ev, item->icon);   // answered even when the icon is not in the texture table
        return;
    }
    if (item && slot == 0 && item->action == ACTION_PROFILE && u3h_profile_new_marker(base, ev)) return;
    ev[RQ_IMAGE] = 0;   // no picture
}

U3X_FN void main_menu(uintptr_t base, int32_t *ev, void *s, uint32_t row) {
    uint32_t count = 0;
    const MenuRow *rows = reinterpret_cast<const MenuRow *>(u3h_lobby_rows(base, &count));
    const MenuRow *item = row < count ? rows + row : nullptr;
    uint32_t &selected = *state_words(s, S_MENU_ROW);

    switch (ev[RQ_OP]) {
    case 1:   // no title
        answer_text(ev, empty_text);
        break;
    case 2:
        if (word(ev, RQ_TEXT_SLOT) == 0 && item) answer_text(ev, u3h_text(base, s, item->text));
        break;
    case 4:
        main_menu_image(base, ev, s, row, item);
        break;
    case 5:
        set_word(ev, RQ_ROW, selected);
        break;
    case 6:
        set_word(ev, RQ_ROW, count - 1);
        break;
    case 7:
        ev[RQ_ROW] = 0;
        break;
    case 10:
        if (item) selected = row;
        break;
    case 11: {
        uint32_t *query = ext_words(0x3a70);
        query[1] = word(ev, RQ_VALUE_ID);
        query[2] += 1;
        if (query[0] & 4) ev[RQ_VALUE] = item ? 1 : 0;
        break;
    }
    case 23:
        if (selected >= count) selected = 0;
        break;
    default:
        break;
    }
}

// ---- the party list --------------------------------------------------------------------------------------------

// The text of the own row: the title for operation 1, else the player's name (with the clan tag in front when the
// menu user has one) while a LAN profile is selected, else the empty text.
U3X_FN const char *own_row_name(uintptr_t base, const int32_t *ev, void *s) {
    if (ev[RQ_OP] == 1) return party_title;

    const uint32_t selected = *state_words(s, S_LAN_SELECTED);
    if (selected == 0 || selected > *state_words(s, S_LAN_PROFILES)) return empty_text;

    const char *name = u3h_onp_id(s);
    const uint8_t *profile = u3h_player_profile(base);
    const uint8_t *tag = profile ? profile + 0xac0 : nullptr;
    if (!tag || tag[0] == 0) return name;

    // "[TAG] name", cut after NAME_MAX characters
    char *text = static_cast<char *>(s) + S_NAME_BUFFER;
    int n = 0;
    text[n++] = '[';
    for (int i = 0; i < 4 && tag[i] != 0; i++) text[n++] = static_cast<char>(tag[i]);
    text[n++] = ']';
    text[n++] = ' ';
    while (*name && n < NAME_MAX) text[n++] = *name++;
    text[n] = 0;
    return text;
}

// operation 4 diagnostics of the simple view: the request, and the first four different non-zero image slots seen
U3X_FN void note_party_image(const int32_t *ev, uint32_t row) {
    const uint32_t slot = word(ev, RQ_IMAGE_SLOT);
    uint32_t *diag = ext_words(0x2130);
    diag[8] += 1;
    diag[9] = slot;
    diag[10] = row;
    for (int i = 11; i <= 14; i++)
        if (diag[i] == slot) return;
    for (int i = 11; i <= 14; i++)
        if (diag[i] == 0) { diag[i] = slot; return; }
}

// The rank badge of the own row. Native 21 is asked twice on purpose: once for the note, once for the answer.
U3X_FN void own_rank_badge(uintptr_t base, int32_t *ev, void *s) {
    const RankFn rank_of = rd<RankFn>(s, S_NATIVE_RANK);
    uint8_t *profile = u3h_player_profile(base);
    const bool both = rank_of && profile;

    uint32_t note = (rank_of ? 1u : 0u) + (profile ? 2u : 0u);
    if (both) note += rank_of(profile) << 8;
    ext_words(0x2130)[15] = note;
    if (!both) return;

    // the head of the answer (kind and the two zero words) is in the request before native 21 is asked again;
    // the rest of the answer follows the call
    ev[RQ_IMAGE] = 0x29;
    ev[4] = 0;
    ev[5] = 0;
    const uint32_t rank = rank_of(profile);
    u3x_icon_answer(ev, 0x29, static_cast<int32_t>(rank), 0, 0, -1);
    ext_words(0x31b0)[4] += 1;
    u3h_rank_icon(base, s, ev);
}

// operation 20 for a row's colour in a Custom Game: the colour of team / player number `row`, rows 0 to 4 only
U3X_FN void row_colour(uintptr_t base, int32_t *ev, void *s, uint32_t row) {
    if (at<uint32_t>(base + 0x17d603c) != 2 || word(ev, RQ_COLOUR_ASK) != 2) return;
    if ((*state_words(s, S_SWITCHES) & 0x2000000u) || row >= 5) return;

    uint32_t look[8];
    for (int i = 0; i < 8; i++) look[i] = 0;
    game_call<void *>(base + 0x7418e0, static_cast<void *>(look), reinterpret_cast<void *>(base + 0x17cb060),
                      static_cast<int32_t>(row), static_cast<int32_t>(0));
    set_word(ev, RQ_COLOUR, (look[3] & 0xffffffu) | 0x80000000u);
    ext_words(0x31b0)[7] += 0x10000;
}

// The party list when no matchmaking and no full member list answers: the own row and the other members' names.
U3X_FN void simple_party_view(uintptr_t base, int32_t *ev, void *s, uint32_t row, const char *name) {
    const int32_t op = ev[RQ_OP];

    if (op != 1 && row != 0) {
        name = u3x_lan_row_name(base, static_cast<int>(row));
        if (!name) name = empty_text;
    }
    if (op == 4) {
        note_party_image(ev, row);
        if (word(ev, RQ_IMAGE_SLOT) == SLOT_ROW_ICON && row == 0) own_rank_badge(base, ev, s);
        return;   // no other picture
    }
    if (op == 20) {
        row_colour(base, ev, s, row);
        return;
    }

    switch (op) {
    case 1:
        answer_text(ev, name);
        break;
    case 2:
        answer_text(ev, word(ev, RQ_TEXT_SLOT) == 0 ? name : empty_text);
        break;
    case 5:
    case 7:
    case 21:
        ev[RQ_ROW] = 0;
        break;
    case 6:   // the last row index; this request also drives the lobby's per-frame steps
        ev[RQ_ROW] = u3x_lan_rows(base) - 1;
        u3x_clan_tag_poll(base, s);
        u3h_hud_alias_names(base, s);
        u3h_stage_tick(base, s);
        u3h_loadout_tick(base, s);
        break;
    case 11:
        ev[RQ_VALUE] = row == 0 ? 1 : 0;
        break;
    default:
        break;
    }
}

U3X_FN void party_list(uintptr_t base, int32_t *ev, void *s, uint32_t row) {
    // first row of the game's own party row model: member 0, local
    at<uint32_t>(base + 0x1b68a80) = 0;
    at<uint8_t>(base + 0x1b68a84) = 1;

    const char *name = own_row_name(base, ev, s);
    if (u3h_onm_players(base, ev, s, name)) return;
    if (u3x_m03_players(base, ev, s, name)) return;
    if (!(ext_eb()[1] & 0x10000u) && u3h_i01_party(base, ev, s, name)) return;
    simple_party_view(base, ev, s, row, name);
}

}  // namespace
}  // namespace g31b

extern "C" {

// Every list request of the menus passes here: 1 = it was a request of the lobby's main menu list or of the party
// list (answered or not), 0 = another source.
U3X_FN int u3x_lobby_source(uintptr_t base, int32_t *ev, void *s) {
    const uint8_t *source = rd<const uint8_t *>(ev, 0x98);
    if (!source) return 0;
    const uint32_t tag = rd<uint32_t>(source, 0x1c);
    if (tag != g31b::TAG_MAIN_MENU && tag != g31b::TAG_PARTY) return 0;
    if (u3h_gate_closed(base, ev)) return 1;

    const uint32_t row = g31b::word(ev, g31b::RQ_ROW);
    if (tag == g31b::TAG_MAIN_MENU) g31b::main_menu(base, ev, s, row);
    else g31b::party_list(base, ev, s, row);
    return 1;
}

}  // extern "C"
