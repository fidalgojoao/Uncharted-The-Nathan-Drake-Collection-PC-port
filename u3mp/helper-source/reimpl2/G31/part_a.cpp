// G31 part A: the events of the multiplayer lobby page (PAGE_MULTIPLAYER), the party lock and the content notices.
// Written from spec G31_mp_page_events.md (sections 4.1 to 4.3).

extern "C" {
// the C part and other groups
uint8_t *u3h_lg_profile(uintptr_t base, uint32_t user);
uint8_t *u3h_lg_rec(uintptr_t base, void *s, uint8_t *profile);
void u3h_mp_quit(uintptr_t base, void *s);
void u3h_show_dialog(uintptr_t base, void *s, uint32_t body, uint32_t title, uint32_t action, int32_t buttons,
                     uint32_t extra, uint32_t close);
int u3h_party_send(uintptr_t base, uint32_t message, const void *data, uint32_t length);
int32_t u3h_server_notice(uintptr_t base, int32_t *arg);
int u3h_chat_restricted(uintptr_t base);
uint8_t *u3x_sym_table(void *s, uint32_t id);
const char *u3x_ms_text(uintptr_t base, uint32_t id);
int u3x_ms_hud(uintptr_t base, int user, const char *txt, uint32_t type, int32_t level, const uint32_t *ic);
int u3x_publish(uintptr_t base, void *s, uint8_t *profile);
int u3x_party_leader(uintptr_t base);
int u3x_split_screen(uintptr_t base);
uint32_t u3x_popup_id(uintptr_t base);
void u3x_send_player_info(uintptr_t base);
void u3x_page_show(uintptr_t base, void *s, uint32_t page, int a);

// this part
void u3x_party_lock(uintptr_t base, int v);
void u3x_content_notice(uintptr_t base, void *s, uint32_t pack, uint32_t dc, uint32_t bit, uint32_t txt, int items);
void u3x_multiplayer_page(uintptr_t base, void *s, int32_t *ev);
}

namespace g31a {

// game data (image offsets)
constexpr uintptr_t IMG_MENU = 0x17bd080;           // the menu object
constexpr uintptr_t IMG_CONTENT_FLAGS = 0x17c3a40;  // u8 per content pack: non-zero = owned
constexpr uintptr_t IMG_MP_ACTIVE = 0x17d5c61;      // u8: multiplayer active
constexpr uintptr_t IMG_NET_ERROR = 0x17d5c80;      // u8: network error
constexpr uintptr_t IMG_NET_MENU_MODE = 0x17d603c;  // u32: net menu mode
constexpr uintptr_t IMG_NOTICE_KIND2 = 0x17d6040;   // u8: set by server notice kind 2
constexpr uintptr_t IMG_SES = 0x17dc298;            // the party session
constexpr uintptr_t IMG_SECOND_PLAYER = 0x17f2fc0;  // s32: second-player state
constexpr uintptr_t IMG_BUSY = 0x1b68a48;           // u8: no disconnect handling while set

// game functions (image offsets)
constexpr uintptr_t FN_GIVE_CASH = 0x660fb0;   // (null, s32 local user, s32 amount)
constexpr uintptr_t FN_SHOW_POPUP = 0x65a000;  // (menu, u32 popup page id)

// party session and its backend
constexpr uintptr_t SES_BACKEND = 8;         // pointer, null = none
constexpr uintptr_t BACKEND_LOCK = 8 * 8;    // entry 8 of the backend's function table

// profile object and its statistics record
constexpr uintptr_t PROFILE_LOAD_STATE = 0x7ae8;  // u32, 3 = loaded
constexpr uint32_t PROFILE_LOADED = 3;
constexpr uintptr_t REC_TREASURES = 0x884;        // u8 per treasure id
constexpr uint32_t TREASURE_IDS = 0x400;
constexpr uintptr_t REC_WELCOME = 0x12e8;         // u32, bit 0 = welcome dialog shown
constexpr uintptr_t REC_NOTICES = 0x1ad0;         // u32, the content notice bits

// page events
constexpr int32_t EV_OPENED = 0, EV_SHOWN = 1, EV_MAY_LEAVE = 3, EV_FRAME = 6;
constexpr uintptr_t EV_HANDLED = 0x10;  // u8 answer of the event

// the helper's counters ea[]
constexpr int EA_SWITCHES = 1;          // bit 3 = the page events are off
constexpr uint32_t EA_PAGE_OFF = 8;
constexpr int EA_OPENED = 40, EA_WELCOMES = 41, EA_CONTENT_NOTICES = 42, EA_SERVER_NOTICES = 43,
              EA_DISCONNECTS = 44, EA_FOLLOWS = 45;

// pages, popups, messages
constexpr uint32_t PAGE_ROOT = 0x37656388;
constexpr uint32_t PAGE_GAME_LOBBY = 0xb99a3235;
constexpr uint32_t POPUP_SERVER_NOTICE = 0x587a7162;
constexpr uint32_t PARTY_MSG_LOBBY = 0x110;
constexpr uint32_t SYM_SERVER_NOTICE_TEXTS = 0xffac56f2;

// dialog text and action ids
constexpr uint32_t TXT_DIALOG_TITLE = 0xc6467f2d;
constexpr uint32_t TXT_CLOSE = 0xe1e040aa;
constexpr uint32_t TXT_WELCOME = 0xa2a50fec;
constexpr uint32_t TXT_LEAVE_TITLE = 0x0260a263;
constexpr uint32_t TXT_LEAVE = 0xd1dab365;
constexpr uint32_t TXT_LEAVE_SPLIT_SCREEN = 0x98ec2aa5;
constexpr uint32_t ACT_LEAVE = 0xafb7834c;
constexpr int32_t LEAVE_BUTTONS = 0x075bc82d;
constexpr uint32_t LEAVE_EXTRA = 0x375737e9;
constexpr uint32_t TXT_CHAT_RESTRICTED = 0x62501513;
constexpr uint32_t TXT_DISCONNECTED = 0x61a3662e;
constexpr uint32_t ACT_DISCONNECTED = 0xd4326d90;

// the three content notices: pack number, script symbol, record bit, text id
constexpr uint32_t NOTICE1_PACK = 69, NOTICE1_SYMBOL = 0x1ab455f4, NOTICE1_BIT = 1, NOTICE1_TEXT = 0x43263d81;  // cash
constexpr uint32_t NOTICE2_PACK = 82, NOTICE2_SYMBOL = 0xf6bd7840, NOTICE2_BIT = 2, NOTICE2_TEXT = 0x7c862ff1;  // cash
constexpr uint32_t NOTICE3_PACK = 81, NOTICE3_SYMBOL = 0xad9e0b32, NOTICE3_BIT = 4, NOTICE3_TEXT = 0xce81cfb2;  // treasures

// each of the two notices is shown once per run of the game: set when shown, never cleared
U3X_BSS uint8_t server_notice_shown;
U3X_BSS uint8_t chat_notice_shown;

// the bonus of a content notice for one local user: a cash amount, or one more of each listed treasure
U3X_FN static void give_bonus(uintptr_t base, const uint8_t *data, int user, uint8_t *rec, int items) {
    if (!items) {
        game_call<void>(base + FN_GIVE_CASH, static_cast<void *>(nullptr), static_cast<int32_t>(user),
                        rd<int32_t>(data, 0));
        return;
    }
    const uint32_t count = rd<uint32_t>(data, 0);
    const uint32_t *ids = rd<const uint32_t *>(data, 8);
    if (!ids) return;
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t id = ids[i];
        if (id < TREASURE_IDS) rec[REC_TREASURES + id] = static_cast<uint8_t>(rec[REC_TREASURES + id] + 1);
    }
}

// event 0: the page was opened
U3X_FN static void page_opened(uintptr_t base) {
    u3x_party_lock(base, 0);
    u3x_send_player_info(base);
    ext_ea()[EA_OPENED] += 1;
}

// event 1: the page is shown
U3X_FN static void page_shown(uintptr_t base, void *s) {
    if (uint8_t *profile = u3h_lg_profile(base, 0)) {   // the load state is not looked at here
        uint8_t *rec = u3h_lg_rec(base, s, profile);
        const uint32_t seen = rd<uint32_t>(rec, REC_WELCOME);
        if (!(seen & 1)) {
            *reinterpret_cast<uint32_t *>(rec + REC_WELCOME) = seen | 1;
            u3h_show_dialog(base, s, TXT_WELCOME, TXT_DIALOG_TITLE, 0, 0, 0, 0);
            ext_ea()[EA_WELCOMES] += 1;
        }
    }
    at<uint32_t>(base + IMG_NET_MENU_MODE) = 0;
    if (u3x_party_leader(base)) {
        uint32_t zero = 0;
        u3h_party_send(base, PARTY_MSG_LOBBY, &zero, 4);
    }
}

// event 3: may the page be left? While multiplayer is active: no, ask "Leave Multiplayer?" first
U3X_FN static void page_may_leave(uintptr_t base, void *s, int32_t *ev) {
    if (!at<uint8_t>(base + IMG_MP_ACTIVE)) return;
    reinterpret_cast<uint8_t *>(ev)[EV_HANDLED] = 1;
    const uint32_t body = u3x_split_screen(base) ? TXT_LEAVE_SPLIT_SCREEN : TXT_LEAVE;
    u3h_show_dialog(base, s, body, TXT_LEAVE_TITLE, ACT_LEAVE, LEAVE_BUTTONS, LEAVE_EXTRA, 0);
}

// the server's notice, until one was shown; true = shown in this frame. A notice handed over while it cannot be
// shown is lost (the online layer gives it once).
U3X_FN static bool server_notice(uintptr_t base, void *s) {
    if (server_notice_shown) return false;
    int32_t unused = 0;
    const int32_t kind = u3h_server_notice(base, &unused);
    const uint8_t *texts = u3x_sym_table(s, SYM_SERVER_NOTICE_TEXTS);
    if (u3x_popup_id(base) != 0 || kind < 0 || !texts) return false;
    server_notice_shown = 1;
    ext_ea()[EA_SERVER_NOTICES] += 1;
    if (kind == 2) at<uint8_t>(base + IMG_NOTICE_KIND2) = 1;
    else if (kind == 0) u3x_page_show(base, s, PAGE_ROOT, 1);
    game_call<int>(base + FN_SHOW_POPUP, reinterpret_cast<void *>(base + IMG_MENU), POPUP_SERVER_NOTICE);
    return true;
}

// the chat restriction notice, until it was shown
U3X_FN static void chat_notice(uintptr_t base, void *s) {
    if (chat_notice_shown) return;
    if (!u3h_chat_restricted(base)) return;
    if (u3x_popup_id(base) != 0) return;
    chat_notice_shown = 1;
    u3h_show_dialog(base, s, TXT_CHAT_RESTRICTED, TXT_DIALOG_TITLE, 0, 0, 0, TXT_CLOSE);
}

// event 6: once per frame while the page is up
U3X_FN static void page_frame(uintptr_t base, void *s) {
    u3x_content_notice(base, s, NOTICE1_PACK, NOTICE1_SYMBOL, NOTICE1_BIT, NOTICE1_TEXT, 0);
    u3x_content_notice(base, s, NOTICE2_PACK, NOTICE2_SYMBOL, NOTICE2_BIT, NOTICE2_TEXT, 0);
    u3x_content_notice(base, s, NOTICE3_PACK, NOTICE3_SYMBOL, NOTICE3_BIT, NOTICE3_TEXT, 1);

    if (!server_notice(base, s)) chat_notice(base, s);

    u3h_mp_quit(base, s);

    // disconnected: back to the root page with a message
    if (at<uint8_t>(base + IMG_NET_ERROR) && !at<uint8_t>(base + IMG_BUSY) && at<int32_t>(base + IMG_SECOND_PLAYER) != 1) {
        u3x_page_show(base, s, PAGE_ROOT, 1);
        u3h_show_dialog(base, s, TXT_DISCONNECTED, TXT_DIALOG_TITLE, ACT_DISCONNECTED, 0, 0, TXT_CLOSE);
        ext_ea()[EA_DISCONNECTS] += 1;
        return;
    }
    // a party member follows the leader to the game lobby
    if (!u3x_party_leader(base)) {
        u3x_page_show(base, s, PAGE_GAME_LOBBY, 0);
        ext_ea()[EA_FOLLOWS] += 1;
    }
}

}  // namespace g31a

// The party leader locks (v = 1) or unlocks (v = 0) the party session through its backend.
extern "C" U3X_FN void u3x_party_lock(uintptr_t base, int v) {
    using namespace g31a;
    uint8_t *ses = reinterpret_cast<uint8_t *>(base + IMG_SES);
    uint8_t *backend = rd<uint8_t *>(ses, SES_BACKEND);
    if (!backend || !u3x_party_leader(base)) return;
    const uintptr_t lock = rd<uintptr_t>(rd<const uint8_t *>(backend, 0), BACKEND_LOCK);   // the table pointer is not tested
    if (lock) game_call<void>(lock, backend, ses, static_cast<int32_t>(v));
}

// One content notice: both local users with a loaded profile get the bonus of a content pack they own, once per
// profile (the record's bit says "already given").
extern "C" U3X_FN void u3x_content_notice(uintptr_t base, void *s, uint32_t pack, uint32_t dc, uint32_t bit,
                                          uint32_t txt, int items) {
    using namespace g31a;
    if (!at<uint8_t>(base + IMG_CONTENT_FLAGS + pack)) return;
    const uint8_t *data = u3x_sym_table(s, dc);
    if (!data) return;
    for (int user = 0; user < 2; user++) {
        uint8_t *profile = u3h_lg_profile(base, static_cast<uint32_t>(user));
        if (!profile || rd<uint32_t>(profile, PROFILE_LOAD_STATE) != PROFILE_LOADED) continue;
        uint8_t *rec = u3h_lg_rec(base, s, profile);
        const uint32_t given = rd<uint32_t>(rec, REC_NOTICES);
        if (given & bit) continue;
        give_bonus(base, data, user, rec, items);
        *reinterpret_cast<uint32_t *>(rec + REC_NOTICES) = given | bit;
        uint32_t no_icon[3];
        no_icon[0] = 0;
        no_icon[1] = 0;
        no_icon[2] = 0xffffffffu;
        u3x_ms_hud(base, 0, u3x_ms_text(base, txt), 0, 0, no_icon);   // the line always goes to menu 0
        u3x_publish(base, s, profile);
        ext_ea()[EA_CONTENT_NOTICES] += 1;
    }
}

// Every page event of the multiplayer lobby page.
extern "C" U3X_FN void u3x_multiplayer_page(uintptr_t base, void *s, int32_t *ev) {
    using namespace g31a;
    const int32_t event = ev[0];
    if (ext_ea()[EA_SWITCHES] & EA_PAGE_OFF) {   // switched off: only the quit check stays
        if (event == EV_FRAME) u3h_mp_quit(base, s);
        return;
    }
    if (event == EV_OPENED) page_opened(base);
    else if (event == EV_SHOWN) page_shown(base, s);
    else if (event == EV_MAY_LEAVE) page_may_leave(base, s, ev);
    else if (event == EV_FRAME) page_frame(base, s);
}
