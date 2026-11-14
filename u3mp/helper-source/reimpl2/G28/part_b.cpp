// G28 part B: the "new" marker of the Profile row, the two extra Network Profile rows, the Records rows in LAN
// play, and the player card's tab bar and body. Written from spec G28_medals_records.md (sections 4.5 to 4.11).

extern "C" {
int u3h_gate_closed(uintptr_t base, int32_t *ev);
const char *u3h_text(uintptr_t base, void *s, uint32_t id);
uint8_t *u3h_player_profile(uintptr_t base);
void u3h_show_dialog(uintptr_t base, void *s, uint32_t body, uint32_t title, uint32_t action, int32_t buttons,
                     uint32_t extra, uint32_t close);
int u3x_n32_mode(uintptr_t base);

int u3x_new_marker(int32_t *event, uint32_t flags);
void u3x_retag_net_profile(uint8_t *page);
int u3x_net_profile_source(uintptr_t base, int32_t *ev, void *s);
int u3x_n32_call(uintptr_t base, int32_t *ev, void *s, void (*native)(int32_t *));
void u3x_n32_retag(uint8_t *page);
int u3x_n32_source(uintptr_t base, int32_t *ev);
}

namespace g28b {

// request operations
constexpr int32_t OP_TEXT = 2, OP_PICTURE = 4, OP_LAST_ROW = 6, OP_ROW_TEXT_ID = 0x15, OP_CLICK = 0x16;
// request words
constexpr int W_ROW = 1, W_TEXT_SLOT = 0x15, W_LIST_ROW = 0x17;

// source tags the helper's sources recognise
constexpr uint32_t TAG_NET_PROFILE = 0x4c414ec6u, TAG_CARD_TABS = 0x4e4e32c3u, TAG_CARD_BODY = 0x4e4e32deu;

// menu pages and items
constexpr uint32_t PAGE_NET_PROFILE = 0xe0c0028bu, PAGE_PLAYER_CARD = 0x83f24f04u;
constexpr uint32_t ITEM_NET_PROFILE_LIST = 0xc073ebb7u;
constexpr uint32_t KIND_LIST = 21, MAX_ITEMS = 64, ITEM_BYTES = 0x68;
constexpr uint32_t SRC_HELPER = 0xcf, SRC_NET_PROFILE = 0xc6, SRC_TAB_BAR = 0xc3, SRC_CARD_BODY = 0xde;

// Network Profile rows of the helper
constexpr int32_t ROW_HAT_STATS = 10, ROW_DLC = 11;
constexpr uint32_t TITLE_HAT_STATS = 0x2e153e84u, TITLE_DLC = 0x9811415du;
constexpr uint32_t TEXT_ON = 0xd1831446u, TEXT_OFF = 0x32066ec9u;
constexpr uintptr_t PROFILE_HAT_STATS = 0x215c, PROFILE_DLC = 0xac8;

// Records: Message of the Day
constexpr uint32_t TITLE_MOTD = 0xa6694384u, MOTD_BODY = 0x60edaa4fu, MOTD_TITLE = 0xc6467f2du, MOTD_CLOSE = 0xe1e040aau;

// "new" marker item type
constexpr int32_t ITEM_TYPE_NEW = 0x2f;

// the game
constexpr uintptr_t IMG_LAN_BYTE = 0x17d5c60, IMG_MENU = 0x17bd080, IMG_ONLINE_RECORDS = 0x13a6620;
constexpr uintptr_t FN_NET_PROFILE = 0x76d2d0, FN_TAB_BAR = 0x76bbf0, FN_CARD_BODY = 0x76fa00;
constexpr uintptr_t FN_MENU_ACTION = 0x64ce90, FN_SHOW_PAGE = 0x651940;

// the source tag of a request, 0 when it has no source descriptor
[[gnu::always_inline]] inline uint32_t source_tag(const int32_t *ev) {
    const uint8_t *descriptor = rd<const uint8_t *>(ev, 0x98);
    return descriptor ? rd<uint32_t>(descriptor, 0x1c) : 0;
}

[[gnu::always_inline]] inline void text_answer(int32_t *ev, const char *text) {
    __builtin_memcpy(reinterpret_cast<uint8_t *>(ev) + 0x40, &text, sizeof text);
}

// The items of a page the hand-over may look at: null when the page has no item list, no items or more than 64.
[[gnu::always_inline]] inline uint8_t *page_items(const uint8_t *page, uint32_t &count) {
    const uint8_t *list = rd<const uint8_t *>(page, 0x48);
    if (!list) return nullptr;
    uint8_t *items = rd<uint8_t *>(list, 8);
    count = rd<uint32_t>(list, 0);
    return count > MAX_ITEMS ? nullptr : items;
}

// route a list to the helper's sources under the given tag
[[gnu::always_inline]] inline void hand_over(uint8_t *item, uint32_t tag) {
    *reinterpret_cast<uint32_t *>(item + 0x34) = SRC_HELPER;
    *reinterpret_cast<uint32_t *>(item + 0x40) = tag;
}

}  // namespace g28b

// 4.5: the picture of the Profile row when something is new
extern "C" U3X_FN int u3x_new_marker(int32_t *event, uint32_t flags) {
    if (!flags) return 0;
    event[2] = g28b::ITEM_TYPE_NEW;
    for (int w = 4; w <= 11; ++w) event[w] = 0;
    event[9] = -1;
    event[11] = -1;
    event[12] = static_cast<int32_t>(F_ONE);
    return 1;
}

// 4.6: the Network Profile list goes to the helper
extern "C" U3X_FN void u3x_retag_net_profile(uint8_t *page) {
    using namespace g28b;
    if (ext_ea()[0] & 0x40) return;
    if (rd<uint32_t>(page, 8) != PAGE_NET_PROFILE) return;
    uint32_t count = 0;
    uint8_t *items = page_items(page, count);
    if (!items) return;
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t *item = items + i * ITEM_BYTES;
        if (rd<uint32_t>(item, 8) != ITEM_NET_PROFILE_LIST || rd<uint32_t>(item, 0x30) != KIND_LIST) continue;
        if (rd<uint32_t>(item, 0x34) != SRC_NET_PROFILE || rd<uint32_t>(item, 0x40) != 0) continue;
        hand_over(item, TAG_NET_PROFILE);
        ext_ea()[24] += 1;
    }
}

// 4.7: rows 10 ("Hat Stats") and 11 ("DLC") are the helper's, the rest is the game's
extern "C" U3X_FN int u3x_net_profile_source(uintptr_t base, int32_t *ev, void *s) {
    using namespace g28b;
    if (source_tag(ev) != TAG_NET_PROFILE) return 0;
    if (u3h_gate_closed(base, ev)) return 1;

    const int32_t op = ev[0];
    int32_t row = -1;   // no row
    if (op == OP_ROW_TEXT_ID || op == OP_CLICK) row = ev[W_LIST_ROW];
    else if (op == OP_TEXT || op == OP_PICTURE) row = ev[W_ROW];

    if (row == ROW_HAT_STATS || row == ROW_DLC) {
        const bool hat = row == ROW_HAT_STATS;
        const uint32_t title = hat ? TITLE_HAT_STATS : TITLE_DLC;
        uint8_t *profile = u3h_player_profile(base);
        uint32_t *setting = profile ? reinterpret_cast<uint32_t *>(profile + (hat ? PROFILE_HAT_STATS : PROFILE_DLC)) : nullptr;
        ext_ea()[25] += 1;
        if (op == OP_TEXT) {
            const int32_t slot = ev[W_TEXT_SLOT];
            if (slot == 0) text_answer(ev, u3h_text(base, s, title));
            else if (slot == 1 && setting) text_answer(ev, u3h_text(base, s, *setting == 0 ? TEXT_ON : TEXT_OFF));
        } else if (op == OP_ROW_TEXT_ID) {
            ev[W_ROW] = static_cast<int32_t>(title);
        } else if (op == OP_CLICK && setting) {
            *setting = *setting == 0 ? 1 : 0;
            ext_ea()[26] += 1;
        }
        return 1;
    }

    game_call<void, int32_t *>(base + FN_NET_PROFILE, ev);
    if (op == OP_LAST_ROW && ev[W_ROW] == 9) ev[W_ROW] = 11;   // twelve rows
    return 1;
}

// 4.9: six Records rows in LAN play: 0, 1 online rows; 2, 3 LAN rows 0, 1; 4 Message of the Day; 5 LAN row 2
extern "C" U3X_FN int u3x_n32_call(uintptr_t base, int32_t *ev, void *s, void (*native)(int32_t *)) {
    using namespace g28b;
    const int32_t op = ev[0];

    if (op == OP_LAST_ROW) {
        native(ev);
        if (ev[W_ROW] >= 0) ev[W_ROW] += 3;
        uint32_t &counter = ext_ea()[125];
        counter = (counter & 0xffff0000u) | ((counter + 1) & 0xffffu);
        return 1;
    }

    int row_word;
    if (op == OP_TEXT || op == OP_PICTURE) row_word = W_ROW;
    else if (op == OP_ROW_TEXT_ID || op == OP_CLICK) row_word = W_LIST_ROW;
    else return 0;
    const int32_t row = ev[row_word];

    if (row == 5 || row == 2 || row == 3) {   // a LAN row of the game, under its own number
        ev[row_word] = row == 5 ? 2 : row - 2;
        native(ev);
        ev[row_word] = row;
        return 1;
    }

    if (row == 4) {
        if (op == OP_TEXT) {
            if (ev[W_TEXT_SLOT] == 0) text_answer(ev, u3h_text(base, s, TITLE_MOTD));
        } else if (op == OP_ROW_TEXT_ID) {
            ev[W_ROW] = static_cast<int32_t>(TITLE_MOTD);
        } else if (op == OP_CLICK) {
            u3h_show_dialog(base, s, MOTD_BODY, MOTD_TITLE, 0, 0, 0, MOTD_CLOSE);
        }
        return 1;
    }

    if (row < 0 || row >= 6) return 1;

    // row 0 or 1: the game's online row, read from the game
    const uint8_t *entry = reinterpret_cast<const uint8_t *>(base + IMG_ONLINE_RECORDS) + 16 * row;
    const uint32_t title = rd<uint32_t>(entry, 0);
    if (op == OP_TEXT) {
        if (ev[W_TEXT_SLOT] == 0) text_answer(ev, u3h_text(base, s, title));
    } else if (op == OP_ROW_TEXT_ID) {
        ev[W_ROW] = static_cast<int32_t>(title);
    } else if (op == OP_CLICK) {
        const uint32_t action = rd<uint32_t>(entry, 8);
        void *menu = reinterpret_cast<void *>(base + IMG_MENU);
        if (action) game_call<void, void *, uint32_t, int32_t>(base + FN_MENU_ACTION, menu, action, 0);
        const uint32_t page = rd<uint32_t>(entry, 4);   // read after the action call has returned
        if (page) game_call<void, void *, uint32_t, int32_t, int32_t>(base + FN_SHOW_PAGE, menu, page, 0, 0);
        ext_ea()[125] += 0x10000;
    }
    return 1;
}

// 4.10: the player card's tab bar and body go to the helper
extern "C" U3X_FN void u3x_n32_retag(uint8_t *page) {
    using namespace g28b;
    if (ext_ea()[122] & 2) return;
    if (rd<uint32_t>(page, 8) != PAGE_PLAYER_CARD) return;
    uint32_t count = 0;
    uint8_t *items = page_items(page, count);
    if (!items) return;
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t *item = items + i * ITEM_BYTES;
        if (rd<uint32_t>(item, 0x30) != KIND_LIST || rd<uint32_t>(item, 0x40) != 0) continue;
        const uint32_t source = rd<uint32_t>(item, 0x34);
        uint32_t tag;
        if (source == SRC_TAB_BAR && rd<uint32_t>(item, 0x38) == PAGE_PLAYER_CARD) tag = TAG_CARD_TABS;
        else if (source == SRC_CARD_BODY) tag = TAG_CARD_BODY;
        else continue;
        hand_over(item, tag);
        ext_ea()[127] += 0x1000000;
    }
}

// 4.11: the game's own card sources, run as if online
extern "C" U3X_FN int u3x_n32_source(uintptr_t base, int32_t *ev) {
    using namespace g28b;
    const uint32_t tag = source_tag(ev);
    if (tag != TAG_CARD_TABS && tag != TAG_CARD_BODY) return 0;
    if (u3h_gate_closed(base, ev)) return 1;

    uint8_t &lan = at<uint8_t>(base + IMG_LAN_BYTE);
    const uint8_t kept = lan;
    if (!(ext_ea()[122] & 2) && u3x_n32_mode(base)) {
        lan = 0;
        uint32_t &counter = ext_ea()[127];
        counter = (counter & 0xff000000u) | ((counter + 1) & 0xffffffu);
    }
    game_call<void, int32_t *>(base + (tag == TAG_CARD_TABS ? FN_TAB_BAR : FN_CARD_BODY), ev);
    lan = kept;
    return 1;
}
