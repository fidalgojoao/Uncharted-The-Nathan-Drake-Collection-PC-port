// G33 part B: screen menus. The menu operations "state change", "show a page", "back" and "close" on a per-screen
// menu object, written from spec G33_publish_prompts.md (sections 3.1, 3.4, 3.5, 3.6, 4.6 to 4.10).

extern "C" uint8_t *u3h_ext(void);   // the helper's own state block (C part)

extern "C" uint8_t *u3x_screen_menu(uintptr_t base, int32_t screen);
extern "C" int u3x_menu_open(const uint8_t *m);
extern "C" uint32_t u3x_menu_page(const uint8_t *m);
extern "C" int u3x_menu_calls_ok(uintptr_t base);
extern "C" void u3x_menu_set_state(uintptr_t base, uint8_t *m, uint32_t state);
extern "C" void u3x_menu_show(uintptr_t base, uint8_t *m, uint32_t page);
extern "C" int u3x_menu_back(uintptr_t base, uint8_t *m, int silent);
extern "C" void u3x_menu_close(uintptr_t base, uint8_t *m);

namespace g33b {

// game data (image offsets)
constexpr uintptr_t SCREEN0_MENU_PTR = 0x16e3188;   // pointer to the menu object of screen 0 (may be null)
constexpr uintptr_t SCREEN1_MENU = 0x17bfa10;       // the menu object of screen 1

// game functions (image offsets)
constexpr uintptr_t FN_EVENT_INIT = 0xf23970;     // (event block, 0, 0): makes the block an empty event
constexpr uintptr_t FN_MENU_NOTIFY = 0x6534e0;    // (menu, event block, phase): tells the menu's listeners
constexpr uintptr_t FN_MENU_ACTIVATE = 0x64c0a0;  // (menu, 1, 1): activates an idle menu
constexpr uintptr_t FN_MENU_SHOW_PAGE = 0x651940; // (menu, page id, fresh, silent) -> int

// menu object fields
constexpr uintptr_t M_STATE = 8;        // u32
constexpr uintptr_t M_ROOT = 0xc;       // u32, 1 = a root menu
constexpr uintptr_t M_POPUP = 0x898;    // u64, 0 = no popup up
constexpr uintptr_t M_STACK = 0x8b0;    // page pointers, 8 bytes each
constexpr uintptr_t M_DEPTH = 0xdb0;    // s32, index of the top entry
constexpr uintptr_t M_TIMER = 0xdb4;    // u32 transition timer
constexpr uintptr_t M_SPEED = 0xdb8;    // u32 transition speed (float pattern)
constexpr uintptr_t PAGE_ID = 0x10;     // u32 in a page object
constexpr int32_t STACK_ENTRIES = 160;

constexpr uint32_t STATE_IDLE = 0, STATE_CLOSING = 5, STATE_CLOSED = 6, STATE_SHOWN = 0xc;
constexpr int PHASE_BEFORE = 3, PHASE_AFTER = 0;

constexpr uintptr_t EXT_MENU_COUNTER = 0x128;   // u32: backs in steps of 1, closes in steps of 0x10000
constexpr unsigned EVENT_BYTES = 160;

// build guard: the first code bytes each of the four game functions must have (the usual function entry: save the
// frame pointer, set it, save r15, r14, r13, r12)
constexpr unsigned ENTRY_BYTES = 12;
U3X_RO const uint8_t usual_entry[ENTRY_BYTES] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54};

// 1 when the code at `address` starts with the usual entry bytes
U3X_FN static int starts_as_expected(uintptr_t address) {
    const uint8_t *code = reinterpret_cast<const uint8_t *>(address);
    for (unsigned i = 0; i < ENTRY_BYTES; i++)
        if (code[i] != usual_entry[i]) return 0;
    return 1;
}

template <typename T> [[gnu::always_inline]] inline T &field(uint8_t *object, uintptr_t offset) {
    return *reinterpret_cast<T *>(object + offset);
}

// the page object at stack entry `index` (may be null)
[[gnu::always_inline]] inline const uint8_t *stack_entry(const uint8_t *m, int32_t index) {
    return rd<const uint8_t *>(m, M_STACK + 8 * static_cast<uintptr_t>(index));
}

[[gnu::always_inline]] inline uint32_t &menu_counter() {
    return *reinterpret_cast<uint32_t *>(u3h_ext() + EXT_MENU_COUNTER);
}

}  // namespace g33b

// the menu object of a screen: screen 1's object, or screen 0's pointer (may be null)
extern "C" U3X_FN uint8_t *u3x_screen_menu(uintptr_t base, int32_t screen) {
    if (screen != 0) return reinterpret_cast<uint8_t *>(base + g33b::SCREEN1_MENU);
    return at<uint8_t *>(base + g33b::SCREEN0_MENU_PTR);
}

// 1 when the menu is a root menu that is not idle
extern "C" U3X_FN int u3x_menu_open(const uint8_t *m) {
    if (!m) return 0;
    return rd<uint32_t>(m, g33b::M_STATE) != g33b::STATE_IDLE && rd<uint32_t>(m, g33b::M_ROOT) == 1 ? 1 : 0;
}

// the page id on top of the menu's page stack; 0 without one
extern "C" U3X_FN uint32_t u3x_menu_page(const uint8_t *m) {
    if (!m) return 0;
    const int32_t depth = rd<int32_t>(m, g33b::M_DEPTH);
    if (depth < 0 || depth >= g33b::STACK_ENTRIES) return 0;
    const uint8_t *top = g33b::stack_entry(m, depth);
    return top ? rd<uint32_t>(top, g33b::PAGE_ID) : 0;
}

// 1 when the four game functions this part calls start as expected (guard against another game build)
extern "C" U3X_FN int u3x_menu_calls_ok(uintptr_t base) {
    return g33b::starts_as_expected(base + g33b::FN_EVENT_INIT) &&
           g33b::starts_as_expected(base + g33b::FN_MENU_NOTIFY) &&
           g33b::starts_as_expected(base + g33b::FN_MENU_ACTIVATE) &&
           g33b::starts_as_expected(base + g33b::FN_MENU_SHOW_PAGE) ? 1 : 0;
}

// state change with the listeners told before and after. Tests nothing: the callers check `m` and the guard.
extern "C" U3X_FN void u3x_menu_set_state(uintptr_t base, uint8_t *m, uint32_t state) {
    alignas(16) uint64_t event[g33b::EVENT_BYTES / 8];
    for (unsigned i = 0; i < g33b::EVENT_BYTES / 8; i++) event[i] = 0;

    game_call<void>(base + g33b::FN_EVENT_INIT, event, 0, 0);
    game_call<void>(base + g33b::FN_MENU_NOTIFY, m, event, g33b::PHASE_BEFORE);
    g33b::field<uint32_t>(m, g33b::M_STATE) = state;
    g33b::field<uint32_t>(m, g33b::M_TIMER) = 0;
    g33b::field<uint32_t>(m, g33b::M_SPEED) = F_ONE;
    game_call<void>(base + g33b::FN_MENU_NOTIFY, m, event, g33b::PHASE_AFTER);
}

// show a page: an idle menu is activated first and gets the page as a fresh one
extern "C" U3X_FN void u3x_menu_show(uintptr_t base, uint8_t *m, uint32_t page) {
    if (!m || !u3x_menu_calls_ok(base)) return;
    const bool idle = rd<uint32_t>(m, g33b::M_STATE) == g33b::STATE_IDLE;
    if (idle) game_call<void>(base + g33b::FN_MENU_ACTIVATE, m, 1, 1);
    game_call<int>(base + g33b::FN_MENU_SHOW_PAGE, m, page, idle ? 1 : 0, 0);
    u3x_menu_set_state(base, m, g33b::STATE_SHOWN);
}

// back: to the page under the top (or the top again while a popup is up); on the first page a root menu closes.
// Result: the low 8 bits of the game's answer, or 1 for the closing root menu, or 0.
extern "C" U3X_FN int u3x_menu_back(uintptr_t base, uint8_t *m, int silent) {
    if (!m || !u3x_menu_calls_ok(base)) return 0;
    const uint32_t state = rd<uint32_t>(m, g33b::M_STATE);
    if (state == g33b::STATE_IDLE) return 0;

    const int32_t depth = rd<int32_t>(m, g33b::M_DEPTH);
    if (depth >= 1 && depth < g33b::STACK_ENTRIES) {
        const bool popup = rd<uint64_t>(m, g33b::M_POPUP) != 0;
        const uint8_t *target = g33b::stack_entry(m, popup ? depth : depth - 1);
        g33b::menu_counter() += 1;   // counted also when there is no target page
        if (!target) return 0;
        const int shown = game_call<int>(base + g33b::FN_MENU_SHOW_PAGE, m, rd<uint32_t>(target, g33b::PAGE_ID), 0, silent);
        return shown & 0xff;
    }

    if (rd<uint32_t>(m, g33b::M_ROOT) != 1) return 0;
    if (state != g33b::STATE_CLOSING && state != g33b::STATE_CLOSED) u3x_menu_set_state(base, m, g33b::STATE_CLOSING);
    return 1;
}

// close: a menu that is neither idle nor closing nor closed starts closing
extern "C" U3X_FN void u3x_menu_close(uintptr_t base, uint8_t *m) {
    if (!m || !u3x_menu_calls_ok(base)) return;
    const uint32_t state = rd<uint32_t>(m, g33b::M_STATE);
    if (state == g33b::STATE_IDLE || state == g33b::STATE_CLOSING || state == g33b::STATE_CLOSED) return;
    u3x_menu_set_state(base, m, g33b::STATE_CLOSING);
    g33b::menu_counter() += 0x10000;
}
