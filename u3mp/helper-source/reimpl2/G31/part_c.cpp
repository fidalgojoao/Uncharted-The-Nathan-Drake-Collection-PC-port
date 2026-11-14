// G31 part C: the two game-lobby buttons, the plain icon answer and the rows of the lobby's main menu.
// Written from spec G31_mp_page_events.md (sections 2, 3.4, 3.5, 3.7, 4.5, 4.6, 4.7).

extern "C" {
// other groups and the C part (spec section 2)
int u3h_gate_closed(uintptr_t base, int32_t *ev);
const char *u3h_text(uintptr_t base, void *s, uint32_t id);
int32_t u3h_icon_index(uintptr_t base, uint32_t id);
void u3x_icon_answer(int32_t *ev, int32_t kind, int32_t a, int32_t b, int32_t c, int32_t index);
int u3x_party_guest(const uint8_t *party);
int u3x_lobby_button_shown(int setup_button, int guest);
uint32_t u3x_lobby_button_label(int setup_button, int guest, int custom_game, uint32_t own_label);

// this part
int u3x_lobby_buttons(uintptr_t base, int32_t *ev, void *s);
void u3x_set_icon(uintptr_t base, int32_t *ev, uint32_t id);
uint32_t u3x_lobby_menu_rows(uintptr_t base, int online, int coop, uint32_t *out);
}

namespace g31c {

// list request fields
constexpr uintptr_t REQ_TEXT = 0x40;         // pointer: the text answer
constexpr uintptr_t REQ_SOURCE = 0x98;       // pointer: the source descriptor
constexpr uintptr_t SOURCE_LABEL = 8;        // u32: the control's own label text id
constexpr uintptr_t SOURCE_TAG = 0x1c;       // u32: the source tag

constexpr uint32_t TAG_SETUP_BUTTONS = 0x4c414e32u;   // Game Type / Map / Settings
constexpr uint32_t TAG_START_BUTTON = 0x4c414e1eu;

// game image
constexpr uintptr_t IMG_ROWS_LAN = 0x13a63f0;      // 6 rows {text id, icon id, action id}
constexpr uintptr_t IMG_ROWS_ONLINE = 0x13a6440;   // 6 rows, same layout
constexpr uintptr_t IMG_NET_MENU_MODE = 0x17d603c; // u32; 2 = Custom Game
constexpr uintptr_t IMG_PARTY_SESSION = 0x17dc298;

constexpr uint32_t MENU_ROWS = 6;
constexpr uint32_t ROW_WORDS = 3;

U3X_RO const char empty_text[] = "";
U3X_RO const char waiting_text[] = "Waiting on party leader";

}  // namespace g31c

// Label (operation 1) and visibility (operation 8) of the game lobby's set-up buttons and start button.
// 1 = the request belongs to one of the two sources, 0 = not ours.
U3X_FN int u3x_lobby_buttons(uintptr_t base, int32_t *ev, void *s) {
    using namespace g31c;
    const uint8_t *source = rd<const uint8_t *>(ev, REQ_SOURCE);
    if (!source) return 0;
    const uint32_t tag = rd<uint32_t>(source, SOURCE_TAG);
    if (tag != TAG_SETUP_BUTTONS && tag != TAG_START_BUTTON) return 0;
    if (u3h_gate_closed(base, ev)) return 1;

    const int setup = tag == TAG_SETUP_BUTTONS;
    const int guest = u3x_party_guest(reinterpret_cast<const uint8_t *>(base + IMG_PARTY_SESSION));

    if (ev[0] == 1) {
        const int custom = at<uint32_t>(base + IMG_NET_MENU_MODE) == 2;
        const uint32_t label = u3x_lobby_button_label(setup, guest, custom, rd<uint32_t>(source, SOURCE_LABEL));
        const char *text = empty_text;
        if (label == 1) text = waiting_text;
        else if (label != 0) text = u3h_text(base, s, label);
        *reinterpret_cast<const char **>(reinterpret_cast<uint8_t *>(ev) + REQ_TEXT) = text;
    } else if (ev[0] == 8) {
        ev[1] = u3x_lobby_button_shown(setup, guest);
    }
    return 1;
}

// The plain icon answer of UI icon `id`, also when the icon is not in the UI texture table (index -1).
// The words that do not depend on the icon's index are in the request before the index is asked.
U3X_FN void u3x_set_icon(uintptr_t base, int32_t *ev, uint32_t id) {
    ev[2] = 3;
    ev[4] = 0;
    ev[5] = 0;
    ev[6] = 0;
    ev[7] = static_cast<int32_t>(id);
    ev[8] = 1;
    const int32_t index = u3h_icon_index(base, id);
    ev[9] = index;
    ev[10] = 0;
    ev[11] = -1;
    ev[12] = 0x3f800000;
}

// Copies the rows of one lobby menu from the game's own tables to `out`; returns the number of rows.
// Online menu: the online table. LAN menu: the LAN table. Co-op menu: the LAN table without rows 1 and 3
// (Find a Party, Cinema).
U3X_FN uint32_t u3x_lobby_menu_rows(uintptr_t base, int online, int coop, uint32_t *out) {
    using namespace g31c;
    const uint32_t *table = reinterpret_cast<const uint32_t *>(base + (online ? IMG_ROWS_ONLINE : IMG_ROWS_LAN));
    const bool coop_menu = !online && coop;
    uint32_t count = 0;
    for (uint32_t row = 0; row < MENU_ROWS; row++) {
        if (coop_menu && (row == 1 || row == 3)) continue;
        for (uint32_t w = 0; w < ROW_WORDS; w++) out[count * ROW_WORDS + w] = table[row * ROW_WORDS + w];
        count++;
    }
    return count;
}
