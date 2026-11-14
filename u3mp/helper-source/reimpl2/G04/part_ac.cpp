// G04, parts A and C: film actions, the 2D drawing primitives and the small helpers of the Cinema's film bar.
// Written from the behaviour description spec/G04_film_hud.md (Common, Part A, Part C). Every float is its 32-bit
// pattern in a uint32_t; game functions are called through a GameCall block (u3x_k26_call).

// ---- other groups and the C part
extern "C" {
uint8_t *u3x_live_object(uint8_t *const *handle, uint32_t id);   // the object behind a game handle, or null
uint32_t u3h_sincos(uint32_t x, uint32_t cosine);                // sine (0) or cosine (1) of x, float bits
}

// the bar layout Part B computes (four float bits)
struct FilmBarLayout { uint32_t width, left, baseline, top; };

// ---- what this file delivers
extern "C" {
int u3x_is_film_action(uint32_t action);
int u3x_film_action(uintptr_t base, const int32_t *event, volatile uint32_t *counters, uint64_t *boxes);
void u3x_k26_tp(uint32_t *params);
uint64_t u3x_k26_tex(uintptr_t base, uint32_t id);
void u3x_k26_quad(uintptr_t base, uint64_t context, uint64_t texture, uint32_t x0, uint32_t y0, uint32_t x1,
                  uint32_t y1, const uint32_t colour[4]);
void u3x_k26_sprite(uintptr_t base, uint64_t image, uint32_t index, const uint32_t colour[4], uint32_t x,
                    uint32_t y, uint32_t w, uint32_t h, uint32_t centred, uint32_t hud_flag);
int u3x_k26_callees(uintptr_t base);
uint64_t u3x_k26_f2d(uint32_t x);
uint32_t u3x_k26_fsel(uint32_t a, uint32_t b, uint32_t c);
uint32_t u3x_k26_rd(const void *p);
void u3x_k26_c4(uint32_t *out, uint32_t r, uint32_t g, uint32_t b, uint32_t a);
uint32_t u3x_k26_col(const uint32_t colour[4]);
void u3x_k26_text(uintptr_t base, uint32_t *params, const char *string);
const char *u3x_k26_str(uintptr_t base, uint32_t id);
void u3x_k26_time(uintptr_t base, char *buffer, int32_t seconds);
void u3x_k26_cursor(uintptr_t base, const uint8_t *element, const FilmBarLayout *layout, uint32_t opacity);
}

namespace {
namespace film_ac {

// game functions (image offsets)
constexpr uintptr_t FN_SEND_EVENT = 0x1197380;   // send an event to an object
constexpr uintptr_t FN_TEXTURE = 0x11b89b0;      // texture handle of a UI image
constexpr uintptr_t FN_QUAD = 0xe42310;          // draw a quad
constexpr uintptr_t FN_SPRITE = 0x11b8dd0;       // draw a HUD sprite
constexpr uintptr_t FN_TEXT = 0xf1d9f0;          // draw or measure a text
constexpr uintptr_t FN_TEXT_LOOKUP = 0xf32ae0;   // localized text of an id
constexpr uintptr_t FN_SNPRINTF = 0x139cab0;
constexpr uintptr_t FN_PANEL_MAKE = 0x640ac0;    // these three are only checked here (the bar draw calls them)
constexpr uintptr_t FN_PANEL_DRAW = 0x63d6f0;
constexpr uintptr_t FN_FILM_SECOND = 0x6a2700;

// game data (image offsets)
constexpr uintptr_t FILM_PLAYER_HANDLE = 0x1b3ee38;   // pointer to the film player's handle slot
constexpr uintptr_t FILM_PLAYER_ID = 0x1b3ee40;       // the id the player had when the handle was taken
constexpr uintptr_t UI_IMAGES = 0x1b24b80;            // +0x48: pointer to the UI image directory
constexpr uintptr_t TEXT_TABLE = 0x17026b8;           // the text table object; +0x10: the fallback text
constexpr uintptr_t FORMAT_H_M_S = 0x1496f61;         // time format with hours
constexpr uintptr_t FORMAT_M_S = 0x1496f6e;           // time format without hours
constexpr uintptr_t GAME_CLOCK = 0x17b4a90;           // +0x114: ticks of 1/300 s

constexpr uint32_t PROLOGUE = 0xe5894855u;   // the first four bytes of an ordinary game function

// float patterns
constexpr uint32_t F_ZERO = 0;
constexpr uint32_t F_MINUS_ONE = 0xbf800000u;
constexpr uint32_t F_255 = 0x437f0000u;
constexpr uint32_t F_72 = 0x42900000u;               // step from tab to tab
constexpr uint32_t F_3 = 0x40400000u;
constexpr uint32_t F_35 = 0x420c0000u;
constexpr uint32_t F_70 = 0x428c0000u;               // the glow's size
constexpr uint32_t F_HALF = 0x3f000000u;
constexpr uint32_t F_0_4 = 0x3ecccccdu;
constexpr uint32_t F_0_7 = 0x3f333333u;
constexpr uint32_t F_0_8 = 0x3f4ccccdu;
constexpr uint32_t F_RADIANS_PER_TICK = 0x3c5a740eu;   // 4/300

constexpr uint32_t NOT_FOUND = 0xffffffffu;

U3X_BSS uint8_t callees_answer;   // u3x_k26_callees: 0 = not decided yet, 1 = good, 2 = bad

// An empty call block for the game function at `function`: every argument slot 0.
U3X_FN void new_call(GameCall &call, uintptr_t function) {
    call.fn = function;
    for (uint64_t &slot : call.i) slot = 0;
    for (uint64_t &slot : call.x) slot = 0;
    for (uint64_t &slot : call.s) slot = 0;
    call.al = 0;
}

// x when it is not negative (-0 stays), else +0; NaN gives +0
U3X_FN uint32_t not_negative(uint32_t x) { return u3x_k26_fsel(x, x, F_ZERO); }

// 1.0 when x is 1 or more, else x
U3X_FN uint32_t at_most_one(uint32_t x) { return u3x_k26_fsel(f_sub(x, F_ONE), F_ONE, x); }

// from a (t = 0) to b (t = 1)
U3X_FN uint32_t blend(uint32_t a, uint32_t b, uint32_t t) { return f_madd(f_sub(F_ONE, t), a, f_mul(t, b)); }

// does the game function at this image offset start with the expected four bytes?
U3X_FN bool starts_with(uintptr_t base, uintptr_t function, uint32_t word) { return at<uint32_t>(base + function) == word; }

// Index of the first class-1 UI image with this id, or NOT_FOUND.
U3X_FN uint32_t image_index(uintptr_t base, uint32_t id) {
    const uint8_t *directory = at<const uint8_t *>(base + UI_IMAGES + 0x48);
    if (!ptr_ok(directory)) return NOT_FOUND;
    const uint8_t *table = rd<const uint8_t *>(directory, 0x28);
    if (!ptr_ok(table)) return NOT_FOUND;
    const uint8_t *entries = rd<const uint8_t *>(table, 8);
    if (!ptr_ok(entries)) return NOT_FOUND;
    const uint32_t count = rd<uint16_t>(table, 2);
    for (uint32_t n = 0; n < count; ++n)
        if (rd<uint32_t>(entries, n * 0x28 + 8) == id) return n;
    return NOT_FOUND;
}

}  // namespace film_ac
}  // namespace

// ================================================== Part A ==================================================

// Is this menu action one of the film player's 14 events?
extern "C" U3X_FN int u3x_is_film_action(uint32_t action) {
    switch (action) {
    case 0x38231b34u: case 0x3cf5deedu: case 0x43348860u: case 0x516bfbb8u: case 0x682c6868u:
    case 0x806c9824u: case 0x887c7d6eu: case 0x953b8f2au: case 0x9eb9632du: case 0xa65c61bau:
    case 0xbd8d7974u: case 0xc1f65aebu: case 0xe8184ce8u: case 0xff502160u:
        return 1;
    default:
        return 0;
    }
}

// A menu action nobody handled: when it is a film action, send it to the film player as an event (1 = handled).
// counters: 4 words, boxes: 5 argument boxes of 4 quadwords (both live in the C part).
extern "C" U3X_FN int u3x_film_action(uintptr_t base, const int32_t *event, volatile uint32_t *counters, uint64_t *boxes) {
    using namespace film_ac;
    counters[0] = counters[0] + 1;
    if (ext_eb()[1] & 0x80u) return 0;   // film events switched off
    const uint32_t id = static_cast<uint32_t>(event[0]);
    if (!u3x_is_film_action(id)) return 0;

    // the film player, or null: the event is sent in both cases
    uint8_t *player = u3x_live_object(at<uint8_t *const *>(base + FILM_PLAYER_HANDLE), at<uint32_t>(base + FILM_PLAYER_ID));

    for (int n = 0; n < 20; ++n) boxes[n] = 0;   // four empty arguments and the result box
    GameCall call;
    new_call(call, base + FN_SEND_EVENT);
    call.i[0] = reinterpret_cast<uint64_t>(boxes + 16);   // result
    call.i[2] = id;
    call.i[3] = reinterpret_cast<uint64_t>(player);
    call.i[4] = reinterpret_cast<uint64_t>(boxes);
    call.i[5] = reinterpret_cast<uint64_t>(boxes + 4);
    call.s[0] = reinterpret_cast<uint64_t>(boxes + 8);
    call.s[1] = reinterpret_cast<uint64_t>(boxes + 12);
    u3x_k26_call(&call);

    counters[1] = counters[1] + 1;
    counters[2] = id;
    if (!player) counters[3] = counters[3] + 1;
    return 1;
}

// The defaults of a text parameter block (48 words).
extern "C" U3X_FN void u3x_k26_tp(uint32_t *params) {
    using namespace film_ac;
    for (int n = 0; n < 48; ++n) params[n] = 0;
    params[0x14 / 4] = F_MINUS_ONE;   // no clipping
    params[0x1c / 4] = F_MINUS_ONE;
    params[0x2c / 4] = F_ONE;
    params[0x30 / 4] = 0xffffffffu;   // the language's font
    for (int n = 0; n < 4; ++n) params[0x50 / 4 + 5 * n] = F_ONE;   // unit matrix
    params[0x90 / 4] = F_ONE;
}

// The game's texture handle of UI image `id` (the game is asked also when the image is not in the table).
extern "C" U3X_FN uint64_t u3x_k26_tex(uintptr_t base, uint32_t id) {
    using namespace film_ac;
    GameCall call;
    new_call(call, base + FN_TEXTURE);
    call.i[0] = (1ull << 32) + id;   // image class 1
    call.i[1] = image_index(base, id);
    return u3x_k26_call(&call);
}

// One rectangle from (x0, y0) to (x1, y1), textured (texture not 0) or plain, in one colour.
extern "C" U3X_FN void u3x_k26_quad(uintptr_t base, uint64_t context, uint64_t texture, uint32_t x0, uint32_t y0,
                                    uint32_t x1, uint32_t y1, const uint32_t colour[4]) {
    using namespace film_ac;
    alignas(16) uint64_t context_block[6];
    alignas(16) uint64_t texture_block[4];
    alignas(16) uint32_t vertices[4 * 16 + 4];   // four vertices of 16 words, then 16 zero bytes

    for (uint64_t &q : context_block) q = 0;
    for (uint64_t &q : texture_block) q = 0;
    for (uint32_t &w : vertices) w = 0;
    context_block[0] = context;
    texture_block[0] = texture;

    // corners in the order left top, right top, right bottom, left bottom
    for (int corner = 0; corner < 4; ++corner) {
        const bool right = corner == 1 || corner == 2;
        const bool bottom = corner >= 2;
        uint32_t *v = vertices + 16 * corner;
        v[0] = right ? x1 : x0;
        v[1] = bottom ? y1 : y0;
        v[3] = right ? F_ONE : F_ZERO;    // u
        v[4] = bottom ? F_ONE : F_ZERO;   // v
        for (int n = 0; n < 4; ++n) {
            v[8 + n] = colour[n];
            v[12 + n] = F_ONE;
        }
    }

    GameCall call;
    new_call(call, base + FN_QUAD);
    call.i[0] = reinterpret_cast<uint64_t>(context_block);
    call.i[1] = reinterpret_cast<uint64_t>(texture_block);
    call.i[2] = reinterpret_cast<uint64_t>(vertices);
    u3x_k26_call(&call);
}

// One HUD sprite at (x, y) of size (w, h).
extern "C" U3X_FN void u3x_k26_sprite(uintptr_t base, uint64_t image, uint32_t index, const uint32_t colour[4], uint32_t x,
                                      uint32_t y, uint32_t w, uint32_t h, uint32_t centred, uint32_t hud_flag) {
    using namespace film_ac;
    alignas(16) uint32_t tint[4];
    for (int n = 0; n < 4; ++n) tint[n] = colour[n];

    GameCall call;
    new_call(call, base + FN_SPRITE);
    call.i[1] = image;
    call.i[2] = index;
    call.i[3] = reinterpret_cast<uint64_t>(tint);
    call.i[4] = centred;
    call.i[5] = hud_flag;
    // float registers: two singles each, the first in the low half
    call.x[0] = x | static_cast<uint64_t>(y) << 32;
    call.x[1] = w | static_cast<uint64_t>(h) << 32;
    call.x[2] = static_cast<uint64_t>(F_ONE) << 32;           // (0, 1)
    call.x[4] = F_ONE | static_cast<uint64_t>(F_ONE) << 32;   // (1, 1)
    u3x_k26_call(&call);
}

// Do the game functions the film bar uses look as expected? Decided on the first call, never again.
extern "C" U3X_FN int u3x_k26_callees(uintptr_t base) {
    using namespace film_ac;
    if (callees_answer == 0) {
        const bool good = starts_with(base, FN_SPRITE, PROLOGUE) && starts_with(base, FN_TEXTURE, PROLOGUE) &&
                          starts_with(base, FN_QUAD, PROLOGUE) && starts_with(base, FN_TEXT, PROLOGUE) &&
                          starts_with(base, FN_TEXT_LOOKUP, PROLOGUE) && starts_with(base, FN_PANEL_MAKE, PROLOGUE) &&
                          starts_with(base, FN_PANEL_DRAW, PROLOGUE) && starts_with(base, FN_FILM_SECOND, 0x009cbf80u) &&
                          starts_with(base, FN_SNPRINTF, 0x6aca25ffu);
        callees_answer = good ? 1 : 2;
    }
    return callees_answer == 1;
}

// ================================================== Part C ==================================================

// The double with the value of the single x (the processor's own conversion: a NaN comes out quiet).
extern "C" U3X_FN uint64_t u3x_k26_f2d(uint32_t x) {
    uint64_t r;
    __asm__("movd %1,%%xmm0\n\tcvtss2sd %%xmm0,%%xmm0\n\tmovq %%xmm0,%0" : "=r"(r) : "r"(x) : "xmm0");
    return r;
}

// c when a is NaN or below zero, else b.
extern "C" U3X_FN uint32_t u3x_k26_fsel(uint32_t a, uint32_t b, uint32_t c) {
    const bool nan = (a & 0x7fffffffu) > 0x7f800000u;
    return nan || f_less(a, film_ac::F_ZERO) ? c : b;
}

extern "C" U3X_FN uint32_t u3x_k26_rd(const void *p) { return rd<uint32_t>(p, 0); }

extern "C" U3X_FN void u3x_k26_c4(uint32_t *out, uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    out[0] = r;
    out[1] = g;
    out[2] = b;
    out[3] = a;
}

// A colour (r, g, b, a as floats 0..1) as the game's text colour: one byte per channel, r lowest.
extern "C" U3X_FN uint32_t u3x_k26_col(const uint32_t colour[4]) {
    using namespace film_ac;
    uint32_t packed = 0;
    for (int n = 0; n < 4; ++n) {
        const uint32_t level = at_most_one(not_negative(colour[n]));
        const uint32_t byte = static_cast<uint32_t>(f_to_int(f_mul(level, F_255))) & 0xffu;
        packed |= byte << (8 * n);
    }
    return packed;
}

// Draw or measure a text; what the game writes into the block stays there.
extern "C" U3X_FN void u3x_k26_text(uintptr_t base, uint32_t *params, const char *string) {
    using namespace film_ac;
    GameCall call;
    new_call(call, base + FN_TEXT);
    call.i[0] = reinterpret_cast<uint64_t>(params);
    call.i[1] = reinterpret_cast<uint64_t>(string);
    u3x_k26_call(&call);
}

// The localized text of a text id; the game's fallback text when the id has none.
extern "C" U3X_FN const char *u3x_k26_str(uintptr_t base, uint32_t id) {
    using namespace film_ac;
    GameCall call;
    new_call(call, base + FN_TEXT_LOOKUP);
    call.i[0] = base + TEXT_TABLE;
    call.i[1] = id;
    const char *text = reinterpret_cast<const char *>(u3x_k26_call(&call));
    return text ? text : at<const char *>(base + TEXT_TABLE + 0x10);
}

// A time in seconds as text (64-byte buffer), with the hours only when they are not 0. The hour wraps at 24.
extern "C" U3X_FN void u3x_k26_time(uintptr_t base, char *buffer, int32_t seconds) {
    using namespace film_ac;
    const int32_t hours = seconds / 3600 % 24;
    const int32_t minutes = seconds / 60 % 60;
    const int32_t rest = seconds % 60;

    GameCall call;
    new_call(call, base + FN_SNPRINTF);
    call.i[0] = reinterpret_cast<uint64_t>(buffer);
    call.i[1] = 64;
    uint64_t *value = &call.i[3];
    if (hours != 0) {
        call.i[2] = base + FORMAT_H_M_S;
        *value++ = static_cast<uint32_t>(hours);
    } else {
        call.i[2] = base + FORMAT_M_S;
    }
    *value++ = static_cast<uint32_t>(minutes);
    *value = static_cast<uint32_t>(rest);
    u3x_k26_call(&call);
}

// The glow under the selected tab; its opacity swings with the game clock between 0.4 and 0.7 of the bar's.
extern "C" U3X_FN void u3x_k26_cursor(uintptr_t base, const uint8_t *element, const FilmBarLayout *layout, uint32_t opacity) {
    using namespace film_ac;
    const uint32_t cursor = rd<uint32_t>(element, 4);   // smoothed position, in tabs
    const uint32_t x = f_sub(f_madd(cursor, F_72, layout->left), F_3);
    const uint32_t y = f_sub(layout->baseline, F_35);

    const uint32_t angle = f_mul(f_from_int(at<int32_t>(base + GAME_CLOCK + 0x114)), F_RADIANS_PER_TICK);
    const uint32_t wave = f_add(u3h_sincos(angle, 1), F_ONE);   // cosine + 1: 0 to 2
    const uint32_t pulse = at_most_one(not_negative(f_mul(wave, F_HALF)));   // 0 to 1; a second clamp would change nothing

    alignas(16) uint32_t colour[4];
    u3x_k26_c4(colour, F_ONE, F_ONE, F_0_8, f_mul(opacity, blend(F_0_4, F_0_7, pulse)));

    uint64_t image;   // not at a multiple of 8
    __builtin_memcpy(&image, element + 0x1dc, 8);
    u3x_k26_sprite(base, image, rd<uint32_t>(element, 0x1e4), colour, x, y, F_70, F_70, 0, 1);
}
