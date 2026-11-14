// G33 part A: profile publish, clan tag keyboard, front-end prompts.
// Written from spec G33_publish_prompts.md (sections 3.1 to 3.4, 3.7, 4.1 to 4.5). No float work in this part.

extern "C" {
// the C part (u3h_ext is declared by the shared tools)
const char *u3h_text(uintptr_t base, void *s, uint32_t id);
int u3h_publish(uintptr_t base, void *s, uint8_t *profile);
void u3h_rank_fields(uint8_t *profile, uint32_t slot, int after);
int u3h_mp_write(uintptr_t base, void *s, uint32_t slot, const uint8_t *record);
int u3h_online_profile(void);
void u3h_server_queue(const uint8_t *record);
uint32_t u3h_active_page(uintptr_t base);
void u3h_rp_tick(uintptr_t base, void *s);

// this part
int u3x_publish(uintptr_t base, void *s, uint8_t *profile);
void u3x_clan_tag_open(uintptr_t base, void *s);
void u3x_clan_tag_poll(uintptr_t base, void *s);
void u3x_prompt_text(char *out, const char *token, const char *label);
int u3x_prompt_value(uintptr_t base, uint64_t *ev, int kind, void *s);
}

namespace g33a {

// ---- profile object
constexpr uintptr_t PROFILE_SIZE = 0x7b00;
constexpr uintptr_t PROFILE_LIVE_RECORD = 0x248;
constexpr uintptr_t PROFILE_SAVE_COUNT = 0x8bc;
constexpr uintptr_t PROFILE_CLAN_TAG = 0xac0;
constexpr uintptr_t PROFILE_SAVE_COPY = 0x5298;
constexpr uintptr_t PROFILE_PUBLISH_STATE = 0x7ae4;
constexpr uintptr_t PROFILE_LOAD_STATE = 0x7ae8;
constexpr uintptr_t PROFILE_IS_SLAVE = 0x7afc;
constexpr uintptr_t PROFILE_NO_SAVE = 0x7afd;
constexpr uintptr_t RECORD_SIZE = 0x2828;
constexpr uint32_t PUBLISH_SAVED = 0, PUBLISH_FAILED = 2, LOAD_DONE = 3;
constexpr int CLAN_TAG_LENGTH = 4;

// ---- state object s
constexpr uintptr_t S_KB_OPEN = 0, S_KB_STATUS = 8, S_KB_RESULT = 16, S_KB_CLOSE = 24;
constexpr uintptr_t S_KB_PHASE = 0x4c;
constexpr uintptr_t S_KB_ERROR = 0x50;
constexpr uintptr_t S_KB_TEXT = 0x58;          // 16 UTF-16 units
constexpr uintptr_t S_PROFILE_COUNT = 128;
constexpr uintptr_t S_PROFILE_SELECTED = 132;
constexpr uintptr_t S_KB_TITLE = 0x2d80;       // 32 UTF-16 units
constexpr uintptr_t S_SAVE_PROBE = 0x2f00;     // q[]
constexpr uintptr_t S_SWITCHES = 0x3d78;
constexpr uint32_t KB_FREE = 0, KB_CLAN_TAG = 3, KB_ERROR_OPEN = 6, KB_FINISHED = 2;
constexpr uint32_t SWITCH_KEEP_TITLE_MENU = 0x100;
constexpr int KB_TEXT_UNITS = 16, KB_TITLE_CHARS = 31;

// ---- helper state block
constexpr uintptr_t EXT_PROMPT_BUFFER_0 = 0x2258, EXT_PROMPT_BUFFER_1 = 0x22b0;
constexpr uintptr_t EXT_PROMPT_COUNTERS = 0x2308;   // p[]

// ---- game image
constexpr uintptr_t IMG_PROFILES = 0x17dd610;
constexpr uintptr_t IMG_MENU_USER_ONE = 0x17bde75;
constexpr uintptr_t IMG_MENU_USER = 0x17bf714;
constexpr uintptr_t IMG_MP_ACTIVE = 0x17d5c61;
constexpr uintptr_t IMG_FRONT_END_ID = 0x17b9d10;
constexpr uintptr_t IMG_EMPTY_TEXT = 0x13ac250;

// ---- ids
constexpr uint32_t TEXT_CLAN_TAG_TITLE = 0x639e0fa8;
constexpr uint32_t PAGE_A = 0xd45dfcca, PAGE_B = 0x82d5c00b;
constexpr uint32_t TEXT_OPTIONS = 0xfb08c743, TEXT_CREDITS = 0x2a6891c3;
constexpr uint32_t BUTTON_SQUARE = 8, BUTTON_TRIANGLE = 9;
constexpr uint32_t ACTION_OPTIONS = 0xfa872aa5, ACTION_CREDITS = 0x2be77c25;
constexpr uint32_t NO_TITLE_MENU_A = 0xc0e2aff8, NO_TITLE_MENU_B = 0x2c3d7d0f;
constexpr int KIND_TEXT = 45, KIND_BUTTON = 46, KIND_COUNT = 48;   // 47 = action: every remaining kind
constexpr int PROMPT_TOKEN_MAX = 80, PROMPT_TEXT_MAX = 86;

U3X_RO const char token_square[] = {'|', 'S', '|', ' ', 0};
U3X_RO const char token_triangle[] = {'|', 'T', '|', ' ', 0};

// the system keyboard's request block (the console's public IME dialog parameters)
struct KeyboardRequest {
    int32_t user;
    uint8_t unused_a[0x20];
    uint32_t max_length;
    uint16_t *text;
    uint8_t unused_b[0x18];
    const uint16_t *title;
    uint8_t reserved[16];
};
static_assert(sizeof(KeyboardRequest) == 96);
static_assert(__builtin_offsetof(KeyboardRequest, max_length) == 0x24);
static_assert(__builtin_offsetof(KeyboardRequest, text) == 0x28);
static_assert(__builtin_offsetof(KeyboardRequest, title) == 0x48);

using KeyboardOpen = int (*)(void *request, void *none);
using KeyboardStatus = int (*)();
using KeyboardResult = int (*)(uint32_t *outcome);
using KeyboardClose = int (*)();

[[gnu::always_inline]] inline uint32_t &word(void *p, uintptr_t off) {
    return *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(p) + off);
}
[[gnu::always_inline]] inline uint16_t *units(void *p, uintptr_t off) {
    return reinterpret_cast<uint16_t *>(static_cast<uint8_t *>(p) + off);
}

// the menu user's profile object, or null
U3X_FN uint8_t *menu_profile(uintptr_t base) {
    uint8_t *profiles = at<uint8_t *>(base + IMG_PROFILES);
    const uint32_t user = at<uint8_t>(base + IMG_MENU_USER_ONE) ? 1u : at<uint32_t>(base + IMG_MENU_USER);
    if (!profiles || user >= 4) return nullptr;
    return profiles + PROFILE_SIZE * user;
}

// The entry points are defined inside the namespace (C linkage, so they are the global u3x_ functions) so that the
// names above are found first, whatever else the file they are appended to defines.

extern "C" {

// Saves the profile's multiplayer record: live record -> save copy -> file (-> upload queue on the online account).
U3X_FN int u3x_publish(uintptr_t base, void *s, uint8_t *profile) {
    if (!profile) return 0;
    if (profile[PROFILE_NO_SAVE]) return 0;
    if (profile[PROFILE_IS_SLAVE]) return 0;
    if (!at<uint8_t>(base + IMG_MP_ACTIVE)) return 0;

    uint32_t *probe = &word(s, S_SAVE_PROBE);
    const uint32_t slot = probe[15];
    const bool allowed = word(profile, PROFILE_LOAD_STATE) == LOAD_DONE && slot != 0 &&
                         slot == word(s, S_PROFILE_SELECTED) && slot <= word(s, S_PROFILE_COUNT);
    if (!allowed) {
        word(profile, PROFILE_PUBLISH_STATE) = PUBLISH_FAILED;
        probe[7] += 1;
        return 0;
    }

    // the count goes up before the write and stays up when the write fails
    word(profile, PROFILE_SAVE_COUNT) += 1;

    uint8_t *copy = profile + PROFILE_SAVE_COPY;
    for (uintptr_t i = 0; i < RECORD_SIZE; i++) copy[i] = profile[PROFILE_LIVE_RECORD + i];

    u3h_rank_fields(profile, slot, 0);
    const int ok = u3h_mp_write(base, s, slot, copy);
    if (ok && u3h_online_profile()) u3h_server_queue(copy);
    u3h_rank_fields(profile, slot, 1);

    word(profile, PROFILE_PUBLISH_STATE) = ok ? PUBLISH_SAVED : PUBLISH_FAILED;
    probe[4] += 1;
    probe[ok ? 5 : 6] += 1;
    probe[8] = slot;
    probe[12] = word(profile, PROFILE_SAVE_COUNT);   // the live count as it is now, after the calls above
    return ok;
}

// Opens the system keyboard on the menu user's clan tag.
U3X_FN void u3x_clan_tag_open(uintptr_t base, void *s) {
    if (word(s, S_KB_PHASE) != KB_FREE) return;
    if (!rd<KeyboardOpen>(s, S_KB_OPEN)) return;
    const uint8_t *profile = menu_profile(base);
    if (!profile) return;

    // title: one unit per byte of the localized text
    const char *title_text = u3h_text(base, s, TEXT_CLAN_TAG_TITLE);
    uint16_t *title = units(s, S_KB_TITLE);
    int n = 0;
    while (n < KB_TITLE_CHARS && title_text[n]) {
        title[n] = static_cast<uint8_t>(title_text[n]);
        n++;
    }
    title[n] = 0;

    // start text: the current tag
    uint16_t *text = units(s, S_KB_TEXT);
    for (int i = 0; i < KB_TEXT_UNITS; i++) text[i] = 0;
    const uint8_t *tag = profile + PROFILE_CLAN_TAG;
    for (int i = 0; i < CLAN_TAG_LENGTH && tag[i]; i++) text[i] = tag[i];

    KeyboardRequest request;
    uint8_t *raw = reinterpret_cast<uint8_t *>(&request);
    for (uintptr_t i = 0; i < sizeof request; i++) raw[i] = 0;
    request.user = 1;   // always user 1, whoever the menu user is
    request.max_length = CLAN_TAG_LENGTH;
    request.text = text;
    request.title = title;

    // the "open" pointer is taken from the state object now, after the text lookup
    if (rd<KeyboardOpen>(s, S_KB_OPEN)(&request, nullptr) < 0)
        word(s, S_KB_ERROR) = KB_ERROR_OPEN;
    else
        word(s, S_KB_PHASE) = KB_CLAN_TAG;
}

// Per frame: takes the typed clan tag once the keyboard has finished, and saves the profile.
U3X_FN void u3x_clan_tag_poll(uintptr_t base, void *s) {
    if (word(s, S_KB_PHASE) != KB_CLAN_TAG) return;
    if (rd<KeyboardStatus>(s, S_KB_STATUS)() != static_cast<int>(KB_FINISHED)) return;

    uint32_t outcome = 1;   // the dialog stores 0 when the player confirmed
    const int result = rd<KeyboardResult>(s, S_KB_RESULT)(&outcome);
    rd<KeyboardClose>(s, S_KB_CLOSE)();
    word(s, S_KB_PHASE) = KB_FREE;
    if (result < 0 || outcome != 0) return;

    uint8_t *profile = menu_profile(base);
    if (!profile) return;

    const uint16_t *text = units(s, S_KB_TEXT);
    uint8_t *tag = profile + PROFILE_CLAN_TAG;
    for (int i = 0; i < CLAN_TAG_LENGTH; i++) {
        uint16_t c = text[i];
        if (c == '\\') c = ' ';
        tag[i] = (c >= 0x20 && c <= 0x7e) ? static_cast<uint8_t>(c) : 0;
        if (c == 0) {   // end of the text: the rest of the tag is empty
            for (int k = i + 1; k < CLAN_TAG_LENGTH; k++) tag[k] = 0;
            break;
        }
    }
    u3h_publish(base, s, profile);
}

// One prompt text: button token (at most 80 characters) + label, at most 86 characters in all.
U3X_FN void u3x_prompt_text(char *out, const char *token, const char *label) {
    int n = 0;
    while (n < PROMPT_TOKEN_MAX && token[n]) {
        out[n] = token[n];
        n++;
    }
    if (label)
        while (n < PROMPT_TEXT_MAX && *label) out[n++] = *label++;
    out[n] = 0;
}

// Answers the game's prompt source on the two front-end pages with the PS3 prompts "Options" and "Credits".
// 1 = the answer is in ev[8]; 0 = the game's own method runs.
U3X_FN int u3x_prompt_value(uintptr_t base, uint64_t *ev, int kind, void *s) {
    uint8_t *ext = u3h_ext();
    uint32_t *counters = &word(ext, EXT_PROMPT_COUNTERS);
    if (kind == KIND_COUNT)
        counters[6] += 1;
    else
        counters[kind - KIND_TEXT] += 1;
    if (!base) return 0;

    const uint32_t page = u3h_active_page(base);
    counters[4] = page;
    if (page != PAGE_A && page != PAGE_B) return 0;

    const bool keep_title_menu = (word(s, S_SWITCHES) & SWITCH_KEEP_TITLE_MENU) != 0;

    if (kind == KIND_COUNT) {   // the highest prompt index of the page
        const uint32_t front_end_id = at<uint32_t>(base + IMG_FRONT_END_ID);
        u3h_rp_tick(base, s);
        const bool title_menu_shown = keep_title_menu && front_end_id != NO_TITLE_MENU_A && front_end_id != NO_TITLE_MENU_B;
        const uint32_t highest = title_menu_shown ? 2 : 1;
        ev[8] = highest;
        counters[7] = highest;
        return 1;
    }

    int32_t index = static_cast<int32_t>(ev[5]);
    counters[5] = static_cast<uint32_t>(index);
    if (keep_title_menu) {
        if (index == 0) {   // the game's own "Title Menu" prompt, which is its index 1
            ev[5] = 1;
            return 0;
        }
        index -= 1;
    }
    counters[3] += 1;

    const bool options = index == 0, credits = index == 1;
    if (kind == KIND_TEXT) {
        const bool wants_text = static_cast<uint32_t>(ev[6]) == 0;
        if (wants_text && options) {
            char *buffer = reinterpret_cast<char *>(ext + EXT_PROMPT_BUFFER_0);
            u3x_prompt_text(buffer, token_square, u3h_text(base, s, TEXT_OPTIONS));
            ev[8] = reinterpret_cast<uintptr_t>(buffer);
        } else if (wants_text && credits) {
            char *buffer = reinterpret_cast<char *>(ext + EXT_PROMPT_BUFFER_1);
            u3x_prompt_text(buffer, token_triangle, u3h_text(base, s, TEXT_CREDITS));
            ev[8] = reinterpret_cast<uintptr_t>(buffer);
        } else {
            ev[8] = base + IMG_EMPTY_TEXT;
        }
    } else if (kind == KIND_BUTTON) {
        ev[8] = options ? BUTTON_SQUARE : credits ? BUTTON_TRIANGLE : 0;
    } else {   // the action; any other kind is answered like it
        ev[8] = options ? ACTION_OPTIONS : credits ? ACTION_CREDITS : 0;
    }
    return 1;
}

}  // extern "C"

}  // namespace g33a
