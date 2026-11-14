// G04 part B: the film bar draw (u3x_k26_draw). Written from spec/G04_film_hud.md (Common and Part B) only.
// Floats are 32-bit patterns; every f_* operation rounds to single (spec G.1). The operands of the spec's formulas
// are kept in the order the spec writes them (the order decides which NaN comes out).

struct FilmBarLayout { uint32_t width, left, baseline, top; };

extern "C" {
// Part A: drawing primitives
int u3x_k26_callees(uintptr_t base);
void u3x_k26_tp(uint32_t *params);
uint64_t u3x_k26_tex(uintptr_t base, uint32_t id);
void u3x_k26_quad(uintptr_t base, uint64_t context, uint64_t texture, uint32_t x0, uint32_t y0, uint32_t x1,
                  uint32_t y1, const uint32_t colour[4]);
void u3x_k26_sprite(uintptr_t base, uint64_t image, uint32_t index, const uint32_t colour[4], uint32_t x,
                    uint32_t y, uint32_t w, uint32_t h, uint32_t centred, uint32_t hud_flag);
// Part C: small helpers
uint64_t u3x_k26_f2d(uint32_t x);
uint32_t u3x_k26_fsel(uint32_t a, uint32_t b, uint32_t c);
void u3x_k26_c4(uint32_t *out, uint32_t r, uint32_t g, uint32_t b, uint32_t a);
uint32_t u3x_k26_col(const uint32_t colour[4]);
void u3x_k26_text(uintptr_t base, uint32_t *params, const char *string);
const char *u3x_k26_str(uintptr_t base, uint32_t id);
void u3x_k26_time(uintptr_t base, char *buffer, int32_t seconds);
void u3x_k26_cursor(uintptr_t base, const uint8_t *element, const FilmBarLayout *layout, uint32_t opacity);
// other groups and the C part
int u3x_k29_eok(const void *p);
volatile uint32_t *u3h_i03_d(void);
}

namespace {
namespace film_bar {

// float constants (bit patterns)
constexpr uint32_t F_0_001 = 0x3a83126fu, F_0_2 = 0x3e4ccccdu, F_0_3 = 0x3e99999au, F_HALF = 0x3f000000u,
                   F_0_7 = 0x3f333333u, F_0_8 = 0x3f4ccccdu, F_0_9 = 0x3f666666u, F_2_5 = 0x40200000u,
                   F_4 = 0x40800000u, F_5 = 0x40a00000u, F_6 = 0x40c00000u, F_10 = 0x41200000u,
                   F_12 = 0x41400000u, F_16 = 0x41800000u, F_20 = 0x41a00000u, F_21 = 0x41a80000u,
                   F_24 = 0x41c00000u, F_32 = 0x42000000u, F_38 = 0x42180000u, F_42 = 0x42280000u,
                   F_51 = 0x424c0000u, F_64 = 0x42800000u, F_72 = 0x42900000u, F_80 = 0x42a00000u,
                   F_84 = 0x42a80000u, F_100 = 0x42c80000u, F_150 = 0x43160000u, F_160 = 0x43200000u,
                   F_400 = 0x43c80000u, F_468 = 0x43ea0000u, F_550 = 0x44098000u, F_650 = 0x44228000u,
                   F_1000 = 0x447a0000u, F_MINUS_HALF = 0xbf000000u, F_MINUS_8 = 0xc1000000u;
constexpr uint32_t F_PER_TICK = 0x3b5a740eu;      // 1/300: seconds per tick
constexpr uint32_t F_PANEL_ALPHA = 0x3f008081u;   // 128 * (1/255 as single)

constexpr uint32_t HEAD_IMAGE = 0xeae7862au;      // play head and markers
constexpr uintptr_t FILM_STATE = 0x17c3ba8;

// words of a text parameter block
enum : uint32_t {
    TP_X = 0, TP_Y = 1, TP_SCALE_X = 8, TP_SCALE_Y = 9, TP_COLOUR = 10, TP_ONE = 11, TP_FONT = 12, TP_FLAGS = 13,
    TP_WIDTH = 37, TP_WORDS = 48
};

U3X_RO const char speed_format[8] = {'x', '%', '2', '.', '1', 'f', 0, 0};

// what the pieces of one draw share
struct Bar {
    uintptr_t base;
    uint8_t *element;
    uint32_t *eb;
    uint64_t context;     // the HUD's draw context, 0 in deferred mode
    bool deferred;
    FilmBarLayout lay;
    uint32_t screen;      // display width
    uint32_t opacity;
    uint32_t length;      // film length in seconds
    uint32_t current;     // current second
    uint32_t head_x, head_y;
};

U3X_FN uint32_t nonneg(uint32_t x) { return u3x_k26_fsel(x, x, 0); }
U3X_FN uint32_t cap1(uint32_t x) { return u3x_k26_fsel(f_sub(x, F_ONE), F_ONE, x); }
U3X_FN uint32_t floor1(uint32_t x) { return u3x_k26_fsel(f_sub(F_ONE, x), F_ONE, x); }
U3X_FN uint32_t mix(uint32_t a, uint32_t b, uint32_t t) { return f_madd(f_sub(F_ONE, t), a, f_mul(t, b)); }
U3X_FN bool is_zero(uint32_t x) { return (x & 0x7fffffffu) == 0; }

U3X_FN void zero(void *block, uintptr_t bytes) {
    uint8_t *p = static_cast<uint8_t *>(block);
    for (uintptr_t k = 0; k < bytes; k++) p[k] = 0;
}

// a call block that is all zero except the function address
U3X_FN void new_call(GameCall *call, uintptr_t function) {
    zero(call, sizeof *call);
    call->fn = function;
}

U3X_FN uint64_t as_int(const void *p) { return reinterpret_cast<uintptr_t>(p); }

// B.4.3: the dark panel behind the bar
U3X_FN void draw_panel(const Bar *bar) {
    const FilmBarLayout &lay = bar->lay;
    uint32_t *eb = bar->eb;
    const uint32_t x = f_sub(lay.left, F_10);
    const uint32_t y = f_sub(lay.top, F_10);
    const uint32_t w = f_add(lay.width, F_20);
    const uint32_t h = f_add(f_sub(f_add(lay.baseline, F_21), lay.top), F_20);

    alignas(16) uint8_t made[0x200];
    alignas(16) uint32_t description[12];
    alignas(16) uint32_t size[2];
    alignas(16) uint32_t zeros[4];
    zero(made, sizeof made);
    zero(description, sizeof description);
    zero(zeros, sizeof zeros);
    description[0] = 24;
    description[7] = 0xffffffffu;
    description[9] = 0xffffffffu;
    description[10] = F_ONE;
    size[0] = w;
    size[1] = h;

    GameCall call;
    new_call(&call, bar->base + 0x640ac0);   // make a panel
    call.i[0] = as_int(made);
    call.i[1] = as_int(description);
    call.i[3] = as_int(size);
    call.i[4] = as_int(zeros);
    call.i[5] = 0xffffffffu;
    call.x[0] = F_ONE;
    u3x_k26_call(&call);
    eb[243] += 1;
    eb[244] = rd<uint32_t>(made, 0);
    eb[245] = rd<uint32_t>(made, 0x3c);
    eb[246] = rd<uint32_t>(made, 0x40);

    if ((eb[1] & 0x20u) || bar->deferred) {
        eb[46] += 1;
        alignas(16) uint32_t black[4];
        u3x_k26_c4(black, 0, 0, 0, F_PANEL_ALPHA);
        u3x_k26_quad(bar->base, bar->context, 0, x, y, f_add(x, w), f_add(y, h), black);
    } else {
        alignas(16) uint32_t style[10];
        alignas(16) uint32_t rectangle[4];
        zero(style, sizeof style);
        style[0] = F_ONE;
        style[4] = F_PANEL_ALPHA;
        style[7] = F_ONE;
        u3x_k26_c4(rectangle, x, y, w, h);
        new_call(&call, bar->base + 0x63d6f0);   // draw a panel
        call.i[0] = bar->base + 0x17bd090;
        call.i[1] = bar->context;
        call.i[2] = as_int(style);
        call.i[3] = as_int(made);
        call.i[4] = as_int(rectangle);
        call.i[5] = 1;
        call.s[0] = 1;
        call.s[1] = 0xffffffffu;
        u3x_k26_call(&call);
    }
    eb[47] = x;
    eb[63] = y;
}

// B.4.5 steps 3 to 7: the label under a tab near the cursor
U3X_FN void draw_label(const Bar *bar, const uint8_t *entry, uint32_t tab_x, uint32_t tab_y, uint32_t nearness) {
    const FilmBarLayout &lay = bar->lay;
    alignas(16) uint32_t colour[4];
    u3x_k26_c4(colour, F_ONE, F_ONE, F_0_8, f_mul(bar->opacity, nearness));
    const uint32_t packed = u3x_k26_col(colour);

    alignas(16) uint32_t tp[TP_WORDS];
    u3x_k26_tp(tp);
    const uint32_t y = f_add(tab_y, F_42);

    // measure
    tp[TP_X] = f_sub(tab_x, F_468);
    tp[TP_Y] = y;
    tp[TP_SCALE_X] = F_0_7;
    tp[TP_SCALE_Y] = F_0_7;
    tp[TP_COLOUR] = packed;
    tp[TP_FLAGS] = 5;
    tp[TP_WIDTH] = F_1000;
    const char *text = u3x_k26_str(bar->base, rd<uint32_t>(entry, 0x14));
    u3x_k26_text(bar->base, tp, text);

    // centred under the icon, but inside the bar; the right end wins
    const uint32_t width = f_add(tp[TP_WIDTH], F_ONE);
    const uint32_t middle = f_add(tab_x, F_32);
    const uint32_t limit = f_sub(f_add(lay.left, lay.width), width);
    const uint32_t wanted = f_madd(width, F_MINUS_HALF, middle);
    const uint32_t held = u3x_k26_fsel(f_sub(wanted, lay.left), wanted, lay.left);
    const uint32_t x = u3x_k26_fsel(f_sub(held, limit), limit, held);

    // draw: fields the measuring call may have changed are set again
    tp[TP_FLAGS] = 1;
    tp[TP_FONT] = 0xffffffffu;
    tp[TP_COLOUR] = packed;
    tp[TP_Y] = y;
    tp[TP_SCALE_X] = F_0_7;
    tp[TP_SCALE_Y] = F_0_7;
    tp[TP_ONE] = F_ONE;
    tp[TP_WIDTH] = width;
    tp[TP_X] = x;
    u3x_k26_text(bar->base, tp, text);
    bar->eb[225] += 1;
}

// B.4.5: the tab icons
U3X_FN void draw_tabs(const Bar *bar) {
    const uint8_t *element = bar->element;
    for (int32_t i = 0; i < 14 && i < rd<int32_t>(element, 0x1d0); i++) {
        const uint8_t *entry = rd<const uint8_t *>(element, 0x160 + 8 * static_cast<uintptr_t>(i));
        if (!u3x_k29_eok(entry)) {
            bar->eb[221] += 1;
            continue;
        }
        const uint32_t index = f_from_int(i);
        const uint32_t x = f_madd(index, F_72, bar->lay.left);
        const uint32_t y = f_sub(bar->lay.baseline, F_16);
        const uint32_t cursor = rd<uint32_t>(element, 4);
        const uint32_t nearness = f_sub(F_ONE, f_sub(cursor, index) & 0x7fffffffu);
        const uint32_t glow = cap1(nonneg(nearness));

        alignas(16) uint32_t colour[4];
        u3x_k26_c4(colour, F_ONE, F_ONE, F_0_8, f_mul(bar->opacity, mix(F_0_3, F_0_8, glow)));
        u3x_k26_sprite(bar->base, rd<uint64_t>(entry, 0x08), rd<uint32_t>(entry, 0x10), colour, x, y, F_64, F_32, 0,
                       0);
        if (f_less(nearness, F_0_001)) continue;   // too far for a label (a NaN is not)
        draw_label(bar, entry, x, y, nearness);
    }
}

// B.4.6: track, played part, play head and the markers of the current clip
U3X_FN void draw_timeline(Bar *bar) {
    const uintptr_t base = bar->base;
    const uint8_t *film = reinterpret_cast<const uint8_t *>(base + FILM_STATE);
    const FilmBarLayout &lay = bar->lay;
    uint32_t *eb = bar->eb;
    alignas(16) uint32_t colour[4];

    const int32_t length = rd<int32_t>(film, 0x158);
    bar->length = f_from_int(length);
    GameCall call;
    new_call(&call, base + 0x6a2700);   // the film's current second
    call.i[0] = as_int(film);
    const int32_t second = static_cast<int32_t>(static_cast<uint32_t>(u3x_k26_call(&call)));
    eb[223] = static_cast<uint32_t>(second);
    eb[224] = rd<uint32_t>(film, 0x158);   // the length word as it is after the call; the draw keeps the first read
    bar->current = f_from_int(second);
    const bool no_length = is_zero(bar->length);
    const uint32_t played = no_length ? 0 : f_div(bar->current, bar->length);

    u3x_k26_c4(colour, F_ONE, F_ONE, F_0_8, F_0_2);
    u3x_k26_quad(base, bar->context, 0, lay.left, lay.top, f_add(lay.left, lay.width), f_add(lay.top, F_20), colour);

    const uint32_t strip_w = f_sub(lay.width, F_160);
    const uint32_t strip_y = f_add(lay.top, F_4);
    const uint32_t strip_x = f_add(lay.left, F_80);
    const uint32_t played_w = floor1(f_mul(strip_w, played));
    u3x_k26_c4(colour, F_ONE, F_ONE, F_0_8, F_HALF);
    u3x_k26_quad(base, bar->context, 0, strip_x, strip_y, f_add(strip_x, played_w), f_add(strip_y, F_12), colour);

    bar->head_x = f_sub(f_add(played_w, strip_x), F_12);
    bar->head_y = f_sub(strip_y, F_6);
    uint64_t texture = u3x_k26_tex(base, HEAD_IMAGE);
    u3x_k26_c4(colour, F_ONE, F_ONE, F_0_8, F_ONE);
    u3x_k26_quad(base, bar->context, texture, bar->head_x, bar->head_y, f_add(bar->head_x, F_24),
                 f_add(bar->head_y, F_24), colour);

    const int32_t clip_index = rd<int32_t>(film, 0x1e8c);
    if (clip_index <= -1) return;
    if (clip_index >= 10) {
        eb[227] += 1;
        return;
    }
    const uint8_t *clip = film + 0x3c8 + static_cast<uintptr_t>(clip_index) * 0x2a8;
    for (uint32_t n = 0; n < rd<uint8_t>(clip, 0); n++) {
        if (n == 64) {
            eb[227] += 1;
            break;
        }
        const uint8_t *marker = clip + 4 + 8 * n;
        const int32_t kind = rd<int16_t>(marker, 0);
        const uint32_t ticks = rd<uint32_t>(marker, 4) - rd<uint32_t>(film, 0x1dbc);   // since the film's start
        uint32_t where = 0;
        if (!no_length) {
            const uint32_t seconds = f_mul(f_from_int(static_cast<int32_t>(ticks)), F_PER_TICK);
            where = f_mul(f_div(F_ONE, bar->length), seconds);
        }
        if (kind == -1) u3x_k26_c4(colour, F_ONE, 0, 0, F_ONE);                   // a cut
        else if (kind == -2) u3x_k26_c4(colour, F_0_3, F_0_3, F_0_3, F_ONE);
        else u3x_k26_c4(colour, F_ONE, F_ONE, F_ONE, F_ONE);
        const uint32_t x = f_sub(f_add(floor1(f_mul(strip_w, where)), strip_x), F_2_5);
        const uint32_t y = f_sub(strip_y, F_6);
        texture = u3x_k26_tex(base, HEAD_IMAGE);
        u3x_k26_quad(base, bar->context, texture, x, y, f_add(x, F_5), f_add(y, F_24), colour);
        eb[226] += 1;
    }
}

// B.4.7: current and total time, film name, play speed
U3X_FN void draw_texts(const Bar *bar) {
    const uintptr_t base = bar->base;
    const uint8_t *image = reinterpret_cast<const uint8_t *>(base);
    const FilmBarLayout &lay = bar->lay;
    alignas(16) uint32_t tp[TP_WORDS];
    alignas(16) char buffer[64];
    alignas(16) uint32_t colour[4];
    u3x_k26_tp(tp);
    u3x_k26_c4(colour, F_ONE, F_ONE, F_0_8, F_0_9);
    const uint32_t packed = u3x_k26_col(colour);

    // current time, at the left end of the strip
    tp[TP_SCALE_X] = F_HALF;
    tp[TP_SCALE_Y] = F_HALF;
    tp[TP_COLOUR] = packed;
    tp[TP_WIDTH] = F_80;
    tp[TP_X] = f_add(lay.left, F_4);
    tp[TP_Y] = lay.top;
    u3x_k26_time(base, buffer, f_to_int(f_add(bar->current, F_HALF)));
    u3x_k26_text(base, tp, buffer);

    // total time, right aligned at the right end
    tp[TP_Y] = lay.top;
    tp[TP_FLAGS] = 2;
    tp[TP_WIDTH] = F_80;
    tp[TP_X] = f_sub(f_add(lay.width, lay.left), F_84);
    u3x_k26_time(base, buffer, f_to_int(f_add(bar->length, F_HALF)));
    u3x_k26_text(base, tp, buffer);

    // film name
    tp[TP_WIDTH] = F_400;
    tp[TP_SCALE_X] = F_ONE;
    tp[TP_SCALE_Y] = F_ONE;
    tp[TP_Y] = F_150;
    tp[TP_FLAGS] = 9;
    tp[TP_X] = f_mul(f_sub(bar->screen, F_400), F_HALF);
    u3x_k26_text(base, tp, reinterpret_cast<const char *>(image + FILM_STATE + 0x1e6c));

    // play speed, only while the film does not run at normal speed (a test of the bit pattern)
    if (rd<uint32_t>(image, 0x16e716c) == F_ONE) return;
    if (rd<uint8_t>(image, 0x17c3571) == 0) return;
    const uint32_t speed = rd<uint32_t>(image, 0x17b4a90 + 0x8c);   // the time scale
    tp[TP_WIDTH] = F_100;
    tp[TP_FLAGS] = 1;
    tp[TP_SCALE_X] = F_HALF;
    tp[TP_SCALE_Y] = F_HALF;
    tp[TP_X] = f_sub(bar->head_x, F_38);
    tp[TP_Y] = f_sub(bar->head_y, F_20);
    alignas(16) char format[8];
    for (int k = 0; k < 8; k++) format[k] = speed_format[k];
    GameCall call;
    new_call(&call, base + 0x139cab0);   // snprintf
    call.i[0] = as_int(buffer);
    call.i[1] = 64;
    call.i[2] = as_int(format);
    call.x[0] = u3x_k26_f2d(speed);
    call.al = 1;
    u3x_k26_call(&call);
    u3x_k26_text(base, tp, buffer);
    bar->eb[228] += 1;
}

}  // namespace film_bar
}  // namespace

// The film bar of the Cinema's HUD, once per HUD draw. Always 0.
extern "C" U3X_FN int u3x_k26_draw(uintptr_t base, uint8_t *element, uint8_t *frame) {
    using namespace film_bar;
    const uint8_t *image = reinterpret_cast<const uint8_t *>(base);
    Bar bar;
    bar.base = base;
    bar.element = element;
    bar.eb = ext_eb();
    uint32_t *eb = bar.eb;

    // B.4.1 guards
    eb[220] += 1;
    if (rd<uint8_t>(image, 0x1acab60 + 0x13) != 0) {   // a screenshot is taken
        eb[229] += 1;
        return 0;
    }
    const int callees = u3x_k26_callees(base);
    if (!callees || !ptr_ok(frame)) {
        eb[221] += 1;
        return 0;
    }
    bar.context = *reinterpret_cast<const volatile uint64_t *>(frame - 0x11f8);   // read in every case
    bar.deferred = (eb[1] & 0x40000u) == 0;
    if (bar.deferred) {
        bar.context = 0;
        volatile uint32_t *frames = u3h_i03_d();
        frames[0] = frames[0] + 1;
    }
    const int32_t count = rd<int32_t>(element, 0x1d0);
    eb[222] = static_cast<uint32_t>(count);
    if (count < 0 || count > 14) {
        eb[221] += 1;
        return 0;
    }

    // B.4.2 layout: centred, sliding from 550 down to 650 as the bar fades
    bar.screen = f_from_int(rd<int32_t>(image, 0x18c6b34 + 8));
    const uint32_t hide = cap1(nonneg(f_sub(F_ONE, rd<uint32_t>(element, 0x1d4))));
    bar.lay.width = f_madd(f_from_int(count), F_72, F_MINUS_8);
    bar.lay.baseline = mix(F_550, F_650, hide);
    bar.lay.left = f_mul(f_sub(bar.screen, bar.lay.width), F_HALF);
    bar.lay.top = f_sub(bar.lay.baseline, F_51);

    draw_panel(&bar);
    bar.opacity = rd<uint32_t>(element, 0x1d4);   // read again after the panel calls
    u3x_k26_cursor(base, element, &bar.lay, bar.opacity);
    draw_tabs(&bar);
    draw_timeline(&bar);
    draw_texts(&bar);
    return 0;
}
