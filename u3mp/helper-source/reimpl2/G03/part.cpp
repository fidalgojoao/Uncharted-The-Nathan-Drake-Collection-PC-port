// G03: film playback frame. The per-frame update of the game's RePlaying state (u3x_rp_update) and the step to the
// next followed player (u3x_rp_cycle_player). Written from spec/G03_film_playback.md; the phase numbers in the
// comments are the ones of its section 4.2.

// ---- functions that exist already (other groups, the helper's C part); declared again, not rewritten ----
extern "C" {
uint8_t *u3x_rp_h(const uint8_t *h);
uint8_t *u3x_rp_self(uint8_t *own);
uint8_t *u3x_rp_cammgr(uintptr_t base);
uint8_t *u3x_rp_trk(uintptr_t base, uint32_t i);
int u3x_rp_trk_live(uint8_t *t);
uint32_t *u3x_rp_pad(uintptr_t base);
int u3x_rp_enter_cross(uintptr_t base);
int u3x_rp_menu_up(uintptr_t base);
void u3x_rp_send(uintptr_t base, uint32_t msg, uint8_t *target);
void u3x_ovr_scale(uintptr_t base, uint8_t *addr, const void *val);
void u3x_render_defaults(uintptr_t base);
void u3x_clock_reset(uint8_t *clock, uint32_t game_time, int32_t t);
uint32_t u3x_stream_read(uintptr_t base, uint8_t *bs, uint32_t n);
void u3x_stream_write(uintptr_t base, uint8_t *bs, uint32_t value, uint32_t n);
void u3x_bookmark_move(uint8_t *d, const uint8_t *s);
void u3x_bookmark_create(uintptr_t base, uint8_t *bm, int32_t t);
void u3h_rp_cam_mode12(uintptr_t base, uint8_t *st, uint8_t *own);
void u3h_rp_render_vars(uintptr_t base, uint8_t *own);
void u3h_rp_cam_target(uintptr_t base, uint8_t *own);
int u3x_rp_update(uintptr_t base, uint8_t *st);
void u3x_rp_cycle_player(uintptr_t base, uint8_t *own, int32_t dir, const uint8_t *cur);
}
namespace {
// file-private functions of the C++ part, as the spec names them (ext_eb comes from the shared tools)
void rp_rumble_off(uintptr_t base, int k);
int rp_screenshot_step(uintptr_t base, uint8_t *st, uint8_t *own);
}  // namespace

namespace {

// game functions (image offsets)
constexpr uintptr_t G03_FN_NET_STEP = 0x7230f0, G03_FN_STREAM_STEP = 0x6a28c0, G03_FN_CAMMGR_STEP = 0x1019550,
                    G03_FN_PAUSE_CHANGED = 0x560280, G03_FN_FILM_ELAPSED = 0x6a2700, G03_FN_MEM_ENTER = 0xda1590,
                    G03_FN_MEM_LEAVE = 0xda18b0, G03_FN_READER_CLOSE = 0x6a3130, G03_FN_EVENT_OF_TYPE = 0x6c2b50,
                    G03_FN_SET_CAMERA = 0x101b530, G03_FN_STATE_UPDATE = 0x125c770;

// function table offsets of the virtual calls
constexpr uintptr_t G03_V_CAMERA_AWAY = 0x160, G03_V_OWN_LEAVE = 0x1d8, G03_V_OWN_TIMEOUT = 0x1c0,
                    G03_V_OWN_TIMED = 0x1c8, G03_V_EVENT_READ = 0x10, G03_V_EVENT_APPLY = 0x20,
                    G03_V_EVENT_WRITE = 0x18, G03_V_EVENT_RELEASE = 0x08;

// game data (image offsets)
constexpr uintptr_t G03_MODE = 0x17c5a10, G03_PAUSE_NOW = 0x17c3570, G03_PAUSE_WANT = 0x17c3571,
                    G03_PAUSE_AUX = 0x17c3572, G03_SPEED = 0x16e716c, G03_LEAVE = 0x16e7170, G03_CLOCK = 0x17c3580,
                    G03_EXPORT = 0x17c5b70, G03_B2C = 0x17d1d10, G03_B2D = 0x17d1d11, G03_GAME = 0x1b24b98,
                    G03_HUD = 0x1805fa8, G03_HOLD = 0x17b4a68, G03_CLOCKSYS_PAUSE = 0x17b4a5d,
                    G03_WORLD1_PAUSES = 0x17b4b34, G03_WORLD0_PAUSES = 0x17b4af4, G03_WORLD1_SCALE = 0x17b4b1c,
                    G03_WORLD0_SCALE = 0x17b4adc, G03_SPEED_OBJECT = 0x1b377d8, G03_WRITER_FOLLOWED = 0x17c5a34,
                    G03_NET_MANAGER = 0x17cb060, G03_WRITER = 0x17c3ba8, G03_READER = 0x17c5cf8,
                    G03_STREAM = 0x17c3838, G03_RERECORD = 0x17c3850, G03_NO_CAMERA_TYPE = 0x1ae95e0,
                    G03_SOURCE_NAME = 0x13ac250;

// owner fields
constexpr uintptr_t G03_OWN_BLEND_A = 0x198, G03_OWN_BLEND_B = 0x19c, G03_OWN_CAMERA = 0x1a0, G03_OWN_FOLLOWS = 0x1b0,
                    G03_OWN_RESUME = 0x1b1, G03_OWN_SCREENSHOT = 0x1b2, G03_OWN_INDEX = 0x1b8, G03_OWN_HELP = 0x1bc,
                    G03_OWN_HUD = 0x1bd, G03_OWN_PAUSE = 0x1be, G03_OWN_DEFAULTS = 0x1bf, G03_OWN_TIMEOUT_AT = 0x1c4,
                    G03_OWN_TIMEOUT_ON = 0x1c8, G03_OWN_BOOKMARKS = 0x1f8, G03_OWN_BM_COUNT = 0x6a8,
                    G03_OWN_BM_TIME = 0x6ac, G03_OWN_BM_SLOT = 0x6b0, G03_OWN_TIMED_AT = 0x6b4;

constexpr uint32_t G03_PAD_UP = 0x10, G03_PAD_DOWN = 0x40, G03_PAD_R2 = 0x200, G03_PAD_TRIANGLE = 0x1000,
                   G03_PAD_CIRCLE = 0x2000, G03_PAD_CROSS = 0x4000, G03_PAD_SQUARE = 0x8000;

constexpr uint32_t G03_MSG_PLAY = 0x953b8f2au, G03_MSG_PAUSE = 0x43348860u, G03_MSG_MODE_FROM_0 = 0x887c7d6eu,
                   G03_MSG_MODE_FROM_2 = 0x806c9824u, G03_MSG_MODE_OTHER = 0xff502160u;

constexpr uint32_t G03_HUD_FILM = 0x72800u, G03_HUD_HELP = 0x80000u;

constexpr uint32_t G03_F_TWENTY = 0x41a00000u, G03_F_THREE = 0x40400000u, G03_F_TICKS_PER_SECOND = 0x43960000u;

constexpr int32_t G03_TRACKERS = 10, G03_BOOKMARK_TICKS = 3000, G03_TIMEOUT_TICKS = 750, G03_LAST_SLOT = 24;
constexpr uint32_t G03_RERECORD_LIMIT = 0x1fc00u;   // bytes
constexpr int G03_COUNT_FRAMES = 40, G03_COUNT_OVERFLOWS = 41, G03_COUNT_BOOKMARKS = 42;

// a field of a game object, to read and to write
template <typename T> [[gnu::always_inline]] inline T &g03_field(uint8_t *object, uintptr_t offset) {
    return *reinterpret_cast<T *>(object + offset);
}

// the function at byte offset `slot` of an object's function table
[[gnu::always_inline]] inline uintptr_t g03_virtual(const uint8_t *object, uintptr_t slot) {
    return rd<uintptr_t>(rd<const uint8_t *>(object, 0), slot);
}

[[gnu::always_inline]] inline uint8_t *g03_image(uintptr_t base, uintptr_t offset) {
    return reinterpret_cast<uint8_t *>(base + offset);
}

// the current playback time (ticks)
[[gnu::always_inline]] inline int32_t g03_playback_time(uintptr_t base) {
    return rd<int32_t>(at<const uint8_t *>(base + G03_CLOCK), 0xc);
}

// pad controls work: no menu over the film and no video export
[[gnu::always_inline]] inline bool g03_controls(uintptr_t base, bool menu) {
    return !menu && !at<uint8_t>(base + G03_EXPORT);
}

[[gnu::always_inline]] inline void g03_want_pause(uintptr_t base) {
    at<uint8_t>(base + G03_PAUSE_WANT) = 1;
    at<uint8_t>(base + G03_PAUSE_AUX) = 1;
}

[[gnu::always_inline]] inline void g03_want_play(uintptr_t base) {
    at<uint8_t>(base + G03_PAUSE_WANT) = 0;
    at<uint8_t>(base + G03_PAUSE_AUX) = 1;
}

[[gnu::always_inline]] inline void g03_send(uintptr_t base, uint8_t *own, uint32_t message) {
    u3x_rp_send(base, message, u3x_rp_self(own));
}

[[gnu::always_inline]] inline uint8_t *g03_bookmark(uint8_t *own, int32_t slot) {
    return own + G03_OWN_BOOKMARKS + static_cast<intptr_t>(slot) * 0x30;
}

// Of cross and circle one is the system's enter button, the other the back button. True when the pad has the
// enter button (enter = true) or the back button (enter = false). The system is asked which one is enter exactly
// once per call, whatever the pad holds: the moment and the number of these queries are part of the frame.
U3X_FN bool g03_pressed(uintptr_t base, uint32_t pad, bool enter) {
    const bool cross_is_enter = u3x_rp_enter_cross(base) != 0;
    return (pad & (cross_is_enter == enter ? G03_PAD_CROSS : G03_PAD_CIRCLE)) != 0;
}

// phases 5 to 7: the HUD toggles and flags, the play / pause key, the camera mode key
U3X_FN void g03_hud_and_keys(uintptr_t base, uint8_t *own, bool menu, uint32_t pad) {
    const bool controls = g03_controls(base, menu);
    if (controls) {
        // first enter-button query of the frame: before either toggle changes
        if (g03_pressed(base, pad, false)) own[G03_OWN_HUD] ^= 1;
        if (pad & G03_PAD_SQUARE) own[G03_OWN_HELP] ^= 1;
    } else if (!own[G03_OWN_DEFAULTS]) {
        u3x_render_defaults(base);
        own[G03_OWN_DEFAULTS] = 1;
    }

    // the film HUD bits contain the "always on" bits 0x70000, so the toggle decides about all of them
    const bool help = own[G03_OWN_HELP] && !at<uint8_t>(base + G03_EXPORT);
    uint32_t hud = at<uint32_t>(base + G03_HUD);
    hud = own[G03_OWN_HUD] ? hud | G03_HUD_FILM : hud & ~G03_HUD_FILM;
    hud = help ? hud | G03_HUD_HELP : hud & ~G03_HUD_HELP;
    at<uint32_t>(base + G03_HUD) = hud;

    if (!help && controls) {
        // second query: after the HUD word is final, before PAUSE_WANT is read and the owner's object is asked for
        if (g03_pressed(base, pad, true))
            g03_send(base, own, at<uint8_t>(base + G03_PAUSE_WANT) ? G03_MSG_PLAY : G03_MSG_PAUSE);
        if (pad & G03_PAD_TRIANGLE) {
            const int32_t mode = at<int32_t>(base + G03_MODE);
            g03_send(base, own, mode == 0 ? G03_MSG_MODE_FROM_0
                              : mode == 2 && !at<uint8_t>(base + G03_B2D) ? G03_MSG_MODE_FROM_2
                              : G03_MSG_MODE_OTHER);
        }
    }

    // camera mode 1 is not allowed under B2D, with or without controls
    if (at<int32_t>(base + G03_MODE) == 1 && at<uint8_t>(base + G03_B2D)) g03_send(base, own, G03_MSG_MODE_OTHER);
}

// phase 8: keep the followed player on a usable tracker; up / down step to another one
U3X_FN void g03_followed_player(uintptr_t base, uint8_t *own, bool menu, uint32_t pad) {
    if (!own[G03_OWN_FOLLOWS] || at<uint8_t>(base + G03_B2C)) return;
    int32_t direction = 0;
    if (g03_controls(base, menu)) direction = pad & G03_PAD_DOWN ? -1 : pad & G03_PAD_UP ? 1 : 0;
    const uint8_t *current = u3x_rp_trk(base, g03_field<uint32_t>(own, G03_OWN_INDEX));
    const bool usable = current && current[0xe1] && !current[0x5e4];
    if (!usable) u3x_rp_cycle_player(base, own, 1, current);
    else if (direction) u3x_rp_cycle_player(base, own, direction, current);
}

// phase 11: the play speed goes to the two world clocks, capped by the net manager's restrictions
U3X_FN void g03_speed(uintptr_t base) {
    const uint32_t speed = at<uint32_t>(base + G03_SPEED);
    const float s = x87_float(speed);

    // the product stays on the x87 (no 32-bit float store) and is cut toward zero
    const int32_t twentyfold = static_cast<int32_t>(static_cast<long double>(s) * x87_const(G03_F_TWENTY));
    g03_field<uint32_t>(at<uint8_t *>(base + G03_SPEED_OBJECT), 0x90) = static_cast<uint32_t>(twentyfold) + 20u;

    uint32_t scale = speed;   // the pattern of the capped speed
    if (at<uint8_t>(base + G03_B2C) && !(s <= x87_const(F_ONE))) scale = F_ONE;
    if (at<uint8_t>(base + G03_B2D) && x87_float(scale) > x87_const(G03_F_THREE)) {
        at<uint32_t>(base + G03_SPEED) = G03_F_THREE;
        scale = G03_F_THREE;
    }
    u3x_ovr_scale(base, g03_image(base, G03_WORLD1_SCALE), &scale);
    u3x_ovr_scale(base, g03_image(base, G03_WORLD0_SCALE), &scale);
}

// one world clock's pause counter follows a pause change
U3X_FN void g03_count_pause(uint16_t *counter, uint8_t paused) {
    if (paused) ++*counter;
    else if (*counter) --*counter;
}

// phases 12 and 13: the pause wish, and the change of the pause state when the wish differs from it
U3X_FN void g03_pause(uintptr_t base, uint8_t *own, bool menu, uint32_t pad) {
    if (at<uint64_t>(base + G03_HOLD) == 0 && !menu) {
        rp_rumble_off(base, 2);
        rp_rumble_off(base, 0);
        if (at<uint8_t>(base + G03_PAUSE_WANT)) {
            if (own[G03_OWN_RESUME]) {
                g03_want_play(base);
                own[G03_OWN_RESUME] = 0;
            } else if (pad & G03_PAD_R2) {
                g03_want_play(base);
            }
        }
    } else {
        g03_want_pause(base);
    }

    const uint8_t want = at<uint8_t>(base + G03_PAUSE_WANT);
    if (at<uint8_t>(base + G03_PAUSE_NOW) == want) return;
    at<uint8_t>(base + G03_CLOCKSYS_PAUSE) = want;
    g03_count_pause(&at<uint16_t>(base + G03_WORLD1_PAUSES), want);
    g03_count_pause(&at<uint16_t>(base + G03_WORLD0_PAUSES), want);
    game_call<void>(base + G03_FN_PAUSE_CHANGED, static_cast<uint64_t>(0), static_cast<int>(want));
    at<uint8_t>(base + G03_PAUSE_NOW) = want;
}

// phase 14: past the film's duration the playback clock is set to the film's end
U3X_FN void g03_film_end(uintptr_t base) {
    uint8_t *writer = g03_image(base, G03_WRITER);
    const int32_t elapsed = game_call<int32_t>(base + G03_FN_FILM_ELAPSED, writer);
    const int32_t duration = rd<int32_t>(writer, 0x158);
    if (!(duration < elapsed)) return;
    // seconds to ticks on the x87: no 32-bit float in between, cut toward zero
    const int32_t ticks = static_cast<int32_t>(static_cast<long double>(duration) * x87_const(G03_F_TICKS_PER_SECOND));
    const uint32_t end = rd<uint32_t>(writer, 0x1dbc) + static_cast<uint32_t>(ticks);
    u3x_clock_reset(at<uint8_t *>(base + G03_CLOCK), rd<uint32_t>(at<const uint8_t *>(base + G03_GAME), 0x20),
                    static_cast<int32_t>(end));
}

// phase 16: the film's events up to `now` go into the game, and into the re-recording when one is open.
// False when the re-record stream is full: the memory context is left and the frame ends there.
U3X_FN bool g03_events(uintptr_t base, uint8_t *own, int32_t now) {
    uint8_t *stream = g03_image(base, G03_STREAM);
    uint8_t *rerecord = g03_image(base, G03_RERECORD);
    uint8_t *reader = g03_image(base, G03_READER);
    for (;;) {
        const uint32_t position = rd<uint32_t>(stream, 8);
        const uint32_t time_word = u3x_stream_read(base, stream, 32);
        const int32_t time = static_cast<int32_t>(time_word);
        if (time == 0) {   // end of the stream
            if (at<uint8_t>(base + G03_EXPORT)) {
                game_call<void>(g03_virtual(own, G03_V_OWN_LEAVE), own);
            } else {
                g03_field<uint32_t>(stream, 8) = position;
                g03_want_pause(base);
                game_call<void>(base + G03_FN_READER_CLOSE, reader);
            }
            return true;
        }
        if (time > now) {   // not due yet
            g03_field<uint32_t>(stream, 8) = position;
            return true;
        }

        const uint32_t type = u3x_stream_read(base, stream, 8);
        uint8_t *event = game_call<uint8_t *>(base + G03_FN_EVENT_OF_TYPE, type);
        game_call<void>(g03_virtual(event, G03_V_EVENT_READ), event, stream);
        game_call<void>(g03_virtual(event, G03_V_EVENT_APPLY), event, 0);

        if (rd<uint32_t>(reader, 8)) {   // a re-recording is open
            const uint32_t bytes_used = (rd<uint32_t>(rerecord, 8) + 7u) / 8u;
            if (bytes_used > G03_RERECORD_LIMIT) {
                game_call<void>(base + G03_FN_MEM_LEAVE);
                ++ext_eb()[G03_COUNT_OVERFLOWS];
                return false;
            }
            u3x_stream_write(base, rerecord, time_word, 32);
            u3x_stream_write(base, rerecord, event[8], 8);
            game_call<void>(g03_virtual(event, G03_V_EVENT_WRITE), event, rerecord);
        }

        const uint8_t uses = static_cast<uint8_t>(event[0xa] - 1);
        event[0xa] = uses;
        if (!uses) game_call<void>(g03_virtual(event, G03_V_EVENT_RELEASE), event);
    }
}

// phase 17: a bookmark every 10 seconds; a full list is thinned to 12 first
U3X_FN void g03_bookmarks(uintptr_t base, uint8_t *own, int32_t now) {
    if (at<uint8_t>(base + G03_B2C)) return;
    const uint32_t since = static_cast<uint32_t>(now) - g03_field<uint32_t>(own, G03_OWN_BM_TIME);
    if (!(static_cast<int32_t>(since) > G03_BOOKMARK_TICKS)) return;

    if (g03_field<int32_t>(own, G03_OWN_BM_COUNT) > G03_LAST_SLOT) {
        g03_field<int32_t>(own, G03_OWN_BM_COUNT) = 12;
        // slots 1..5 take every other one, slots 6..11 take 13..18 (kept as it is), slot 0 stays
        for (int32_t slot = 1; slot <= 5; ++slot) u3x_bookmark_move(g03_bookmark(own, slot), g03_bookmark(own, 2 * slot));
        for (int32_t slot = 6; slot <= 11; ++slot) u3x_bookmark_move(g03_bookmark(own, slot), g03_bookmark(own, slot + 7));
        for (int32_t slot = 12; slot <= G03_LAST_SLOT; ++slot) {
            uint8_t *free_slot = g03_bookmark(own, slot);
            g03_field<uint32_t>(free_slot, 0x08) = 0;
            g03_field<uint32_t>(free_slot, 0x10) = 0;
            g03_field<uint32_t>(free_slot, 0x18) = 0;
            g03_field<uint32_t>(free_slot, 0x1c) = 0;
            g03_field<uint64_t>(free_slot, 0x28) = 0;
        }
    }

    const int32_t slot = g03_field<int32_t>(own, G03_OWN_BM_COUNT);
    g03_field<int32_t>(own, G03_OWN_BM_SLOT) = slot;
    u3x_bookmark_create(base, g03_bookmark(own, slot), g03_playback_time(base));
    const uint32_t count = g03_field<uint32_t>(own, G03_OWN_BM_COUNT) + 1u;
    g03_field<uint32_t>(own, G03_OWN_BM_COUNT) = count;
    g03_field<uint32_t>(g03_image(base, G03_WRITER), 0x1fc4) = count;
    g03_field<int32_t>(own, G03_OWN_BM_TIME) = now;
    ++ext_eb()[G03_COUNT_BOOKMARKS];
}

// phase 18: the camera follows the player, or there is none under B2C
U3X_FN void g03_camera(uintptr_t base, uint8_t *own) {
    at<int32_t>(base + G03_WRITER_FOLLOWED) = -1;
    if (!at<uint8_t>(base + G03_B2C)) {
        u3h_rp_cam_target(base, own);
        return;
    }
    uint64_t camera_type = base + G03_NO_CAMERA_TYPE;
    uint64_t zero = 0;
    game_call<uint8_t *>(base + G03_FN_SET_CAMERA, u3x_rp_cammgr(base), &camera_type, &zero, static_cast<void *>(nullptr));
    g03_field<uint32_t>(own, G03_OWN_BLEND_A) = F_ONE;
}

// phases 19 and 20: the owner's pause request, then leaving, the timeout or the timed action
U3X_FN void g03_owner_requests(uintptr_t base, uint8_t *own) {
    if (own[G03_OWN_PAUSE]) {
        g03_want_pause(base);
        own[G03_OWN_PAUSE] = 0;
    }

    if (at<uint8_t>(base + G03_LEAVE)) {
        game_call<void>(g03_virtual(own, G03_V_OWN_LEAVE), own);
        return;
    }
    if (own[G03_OWN_TIMEOUT_ON]) {
        const uint32_t armed_for = static_cast<uint32_t>(g03_playback_time(base)) - g03_field<uint32_t>(own, G03_OWN_TIMEOUT_AT);
        if (static_cast<int32_t>(armed_for) > G03_TIMEOUT_TICKS) {
            g03_field<uint32_t>(own, G03_OWN_BLEND_B) = F_ONE;
            g03_field<uint32_t>(own, G03_OWN_BLEND_A) = F_ONE;
            game_call<void>(g03_virtual(own, G03_V_OWN_TIMEOUT), own);
            return;
        }
    }
    const int32_t due = g03_field<int32_t>(own, G03_OWN_TIMED_AT);
    if (due > 0 && due <= rd<int32_t>(at<const uint8_t *>(base + G03_GAME), 0x114))
        game_call<void>(g03_virtual(own, G03_V_OWN_TIMED), own);
}

}  // namespace

extern "C" {

// Moves the followed-player index in steps of `dir` to the next live tracker, at most 10 tries. A new tracker
// (not `cur`) sends the film camera away.
U3X_FN void u3x_rp_cycle_player(uintptr_t base, uint8_t *own, int32_t dir, const uint8_t *cur) {
    for (int32_t tries = 0; tries < G03_TRACKERS; ++tries) {
        int32_t index = static_cast<int32_t>(g03_field<uint32_t>(own, G03_OWN_INDEX) + static_cast<uint32_t>(dir));
        if (index < 0) index += G03_TRACKERS;
        g03_field<int32_t>(own, G03_OWN_INDEX) = index;

        const uint32_t number = static_cast<uint32_t>(index) % static_cast<uint32_t>(G03_TRACKERS);
        uint8_t *tracker = u3x_rp_trk(base, number);
        if (!u3x_rp_trk_live(tracker)) continue;

        if (tracker != cur) {
            if (uint8_t *camera = u3x_rp_h(own + G03_OWN_CAMERA)) {
                uint32_t zero = 0;
                game_call<void>(g03_virtual(camera, G03_V_CAMERA_AWAY), camera, &zero, 2);
            }
            g03_field<uint64_t>(own, G03_OWN_CAMERA) = 0;
            g03_field<uint32_t>(own, G03_OWN_CAMERA + 8) = 0;
        }
        g03_field<uint32_t>(own, G03_OWN_INDEX) = number;
        return;
    }
}

// One frame of film playback. Always 1 (the hooked game function's own body is not run), except that a running
// screenshot request returns its step's result.
U3X_FN int u3x_rp_update(uintptr_t base, uint8_t *st) {
    uint8_t *own = rd<uint8_t *>(st, 8);

    ++ext_eb()[G03_COUNT_FRAMES];                                                       // 1

    const int32_t mode = at<int32_t>(base + G03_MODE);                                  // 2
    if (mode == 1 || mode == 2) u3h_rp_cam_mode12(base, st, own);
    u3h_rp_render_vars(base, own);

    if (own[G03_OWN_SCREENSHOT]) return rp_screenshot_step(base, st, own);              // 3

    game_call<void>(base + G03_FN_NET_STEP, g03_image(base, G03_NET_MANAGER), own, 0);  // 4
    const bool menu = u3x_rp_menu_up(base) != 0;
    const uint32_t pad = *u3x_rp_pad(base);

    g03_hud_and_keys(base, own, menu, pad);                                             // 5, 6, 7
    g03_followed_player(base, own, menu, pad);                                          // 8

    game_call<void>(base + G03_FN_STREAM_STEP, g03_image(base, G03_WRITER));            // 9
    game_call<void>(base + G03_FN_STREAM_STEP, g03_image(base, G03_READER));

    if (at<uint64_t>(base + G03_HOLD) == 0)                                             // 10
        game_call<void>(base + G03_FN_CAMMGR_STEP, u3x_rp_cammgr(base));

    g03_speed(base);                                                                    // 11
    g03_pause(base, own, menu, pad);                                                    // 12, 13
    g03_film_end(base);                                                                 // 14

    const int32_t now = g03_playback_time(base);                                        // 15
    uint32_t heap = 0x14;
    game_call<void>(base + G03_FN_MEM_ENTER, &heap, g03_image(base, G03_SOURCE_NAME), static_cast<uint64_t>(0),
                    g03_image(base, G03_SOURCE_NAME));

    if (!g03_events(base, own, now)) return 1;                                          // 16
    g03_bookmarks(base, own, now);                                                      // 17
    g03_camera(base, own);                                                              // 18
    g03_owner_requests(base, own);                                                      // 19, 20

    game_call<void>(base + G03_FN_STATE_UPDATE, st);                                    // 21
    game_call<void>(base + G03_FN_MEM_LEAVE);
    return 1;
}

}  // extern "C"
