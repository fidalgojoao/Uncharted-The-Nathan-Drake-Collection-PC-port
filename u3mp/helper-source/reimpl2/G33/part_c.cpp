// G33 part C: the two script natives the PS4 game does not register ("is-camera-underwater?",
// "player-disable-zipline") and the HUD's spotted icon. Written from spec G33_publish_prompts.md (3.1, 3.4, 3.6,
// 3.7, 4.11, 4.12). No float arithmetic: float values are only moved as 32-bit patterns.

// ---- functions of the C part and the shared tools that this part calls
extern "C" uint8_t *u3h_ext(void);                               // the helper's state block
extern "C" uint8_t u3h_spot_flag(uintptr_t base, uint8_t *np);   // non-zero: net player np counts as spotted
extern "C" uint64_t u3x_k26_call(void *game_call_block);         // performs a GameCall block

// ---- this part
extern "C" int64_t u3x_native_underwater(int32_t argc, uint64_t *argv);
extern "C" int64_t u3x_native_disable_zipline(int32_t argc, uint64_t *argv);
extern "C" void u3x_hud_spotted(uintptr_t base);

namespace g33c {

// the helper's state block
constexpr uintptr_t EXT_IMAGE_BASE = 0x21b0;   // u64: image base for the natives (0 before they are linked)
constexpr uintptr_t EXT_NATIVE     = 0x2218;   // u32 n[]: counters of the natives
constexpr uintptr_t EXT_SPOTTED    = 0x2fe0;   // u32 h[]: counters of the spotted icon

enum : uint32_t { N_WATER_CALLS = 1, N_WATER_ANSWER, N_ZIP_CALLS, N_ZIP_PLAYER, N_ZIP_QUEUED };
enum : uint32_t { H_CALLS = 0, H_DRAWS, H_IMAGE, H_INDEX };

// game functions
constexpr uintptr_t FN_CAMERA_UNDERWATER = 0x1221430;   // (s32 screen) -> u8
constexpr uintptr_t FN_QUEUE_SETTING     = 0x12284e0;   // queues a change of a script setting -> queue entry
constexpr uintptr_t FN_COMPARE_BYTE      = 0x12284c0;   // argument only: 1-byte compare
constexpr uintptr_t FN_SET_SCALED        = 0x1228ca0;   // argument only: 4-byte scaled set
constexpr uintptr_t FN_SET_BYTE          = 0x12284d0;   // argument only: 1-byte set
constexpr uintptr_t FN_SYMBOL_DATA       = 0x11f2e30;   // (u32 symbol id) -> data or null
constexpr uintptr_t FN_SPRITE_DRAW       = 0x11b8dd0;   // the HUD sprite draw

// game data
constexpr uintptr_t G_PLAYER_SETTINGS = 0x17fad00;   // 0x20 bytes per player; u8 +0xf = zipline disabled
constexpr uintptr_t G_BLEND_WEIGHT    = 0x14481f0;   // float pattern: weight of a queued setting
constexpr uintptr_t G_CLOCK           = 0x1b24b98;   // pointer; s32 +0x94 = tick count
constexpr uintptr_t G_HUD             = 0x1806480;   // pointer: the HUD being drawn; s32 +0x1d0 = screen
constexpr uintptr_t G_LOCAL_PLAYERS   = 0x17cb3d0;   // pointer per screen: the local net player
constexpr uintptr_t G_IMAGE_DIRECTORY = 0x1b24bc8;   // pointer; +0x20 = pointer to the HUD image table

// ids
constexpr uint32_t EVENT_CANCEL_CHANGE = 0x309861e2;
constexpr uint32_t EVENT_SET_VALUE     = 0x4d95edb1;
constexpr uint32_t SYMBOL_HUD_SETTINGS = 0x9353ed16;

// queue entry of FN_QUEUE_SETTING
constexpr uintptr_t ENTRY_DUE = 0x10, ENTRY_PRIORITY = 0x28, ENTRY_FLAGS = 0x49;

U3X_FN static uint32_t *ext_words(uintptr_t offset)
{
    return reinterpret_cast<uint32_t *>(u3h_ext() + offset);
}

// the script call's first argument as a signed 32-bit number; 0 when there is none
U3X_FN static int32_t first_argument(int32_t argc, const uint64_t *argv)
{
    if (argc <= 0 || !argv) return 0;
    return static_cast<int32_t>(static_cast<uint32_t>(argv[0]));
}

// a call block with every slot 0
U3X_FN static void clear_call(GameCall *call)
{
    call->fn = 0;
    for (int k = 0; k < 6; k++) call->i[k] = 0;
    for (int k = 0; k < 5; k++) call->x[k] = 0;
    for (int k = 0; k < 2; k++) call->s[k] = 0;
    call->al = 0;
}

// two neighbouring 32-bit patterns as one 8-byte float slot (the first in the low half)
U3X_FN static uint64_t pattern_pair(const uint8_t *p)
{
    return static_cast<uint64_t>(rd<uint32_t>(p, 0)) | (static_cast<uint64_t>(rd<uint32_t>(p, 4)) << 32);
}

// queues the 1-byte `value` for a script setting; result = the queue entry or null
U3X_FN static uint8_t *queue_byte(uintptr_t image, uint32_t weight, uint32_t event, uint8_t *setting, uint8_t value,
                                  uintptr_t set_function)
{
    uint8_t held = value;   // has to be valid only during the call
    GameCall call;
    clear_call(&call);
    call.fn = image + FN_QUEUE_SETTING;
    call.i[0] = event;
    call.i[1] = reinterpret_cast<uintptr_t>(setting);
    call.i[2] = reinterpret_cast<uintptr_t>(&held);
    call.i[3] = 1;                                // size of the value in bytes
    call.i[4] = image + FN_COMPARE_BYTE;
    call.i[5] = image + set_function;
    call.x[0] = weight;                           // float register 0; both stack arguments and al stay 0
    return reinterpret_cast<uint8_t *>(u3x_k26_call(&call));
}

// position of `image` in the game's HUD image table; -1 when it is not there or there is no table
U3X_FN static int32_t image_index(uintptr_t base, uint32_t image)
{
    const uint8_t *directory = at<const uint8_t *>(base + G_IMAGE_DIRECTORY);
    if (!directory) return -1;
    const uint8_t *table = rd<const uint8_t *>(directory, 0x20);
    if (!table) return -1;
    const uint32_t count = rd<uint16_t>(table, 2);
    const uint8_t *entries = rd<const uint8_t *>(table, 8);
    if (!count || !entries) return -1;
    for (uint32_t k = 0; k < count; k++)
        if (rd<uint32_t>(entries + 0x28 * static_cast<uintptr_t>(k), 8) == image) return static_cast<int32_t>(k);
    return -1;
}

}  // namespace g33c

// "is-camera-underwater?": 1 when the camera of the screen given as first argument is under water
extern "C" U3X_FN int64_t u3x_native_underwater(int32_t argc, uint64_t *argv)
{
    using namespace g33c;
    uint32_t *n = ext_words(EXT_NATIVE);
    n[N_WATER_CALLS] += 1;
    const uintptr_t image = rd<uint64_t>(u3h_ext(), EXT_IMAGE_BASE);
    if (!image) return 0;
    const uint8_t under = game_call<uint8_t, int32_t>(image + FN_CAMERA_UNDERWATER, first_argument(argc, argv));
    n[N_WATER_ANSWER] = under;
    return under ? 1 : 0;
}

// "player-disable-zipline": queues "zipline disabled" = 1 for the player given as first argument
extern "C" U3X_FN int64_t u3x_native_disable_zipline(int32_t argc, uint64_t *argv)
{
    using namespace g33c;
    uint32_t *n = ext_words(EXT_NATIVE);
    n[N_ZIP_CALLS] += 1;
    const uintptr_t image = rd<uint64_t>(u3h_ext(), EXT_IMAGE_BASE);
    if (!image) return 0;

    const int32_t player = first_argument(argc, argv);
    n[N_ZIP_PLAYER] = static_cast<uint32_t>(player);
    // not range checked (kept): the player index is used as it comes
    uint8_t *setting = reinterpret_cast<uint8_t *>(image + G_PLAYER_SETTINGS) + static_cast<int64_t>(player) * 0x20 + 0xf;
    const uint32_t weight = at<uint32_t>(image + G_BLEND_WEIGHT);

    // cancel the pending change (this call names the 4-byte scaled set for the 1-byte setting: kept)
    if (uint8_t *entry = queue_byte(image, weight, EVENT_CANCEL_CHANGE, setting, 0, FN_SET_SCALED)) {
        *reinterpret_cast<uint32_t *>(entry + ENTRY_PRIORITY) = 0;
        entry[ENTRY_FLAGS] |= 8;
    }

    // queue the value 1
    if (uint8_t *entry = queue_byte(image, weight, EVENT_SET_VALUE, setting, 1, FN_SET_BYTE)) {
        entry[ENTRY_FLAGS] = static_cast<uint8_t>((entry[ENTRY_FLAGS] & 0xdb) | 4);
        *reinterpret_cast<uint32_t *>(entry + ENTRY_PRIORITY) = 101;
        const uint8_t *clock = at<const uint8_t *>(image + G_CLOCK);
        const uint32_t tick = clock ? rd<uint32_t>(clock, 0x94) : 0;
        *reinterpret_cast<uint32_t *>(entry + ENTRY_DUE) = 9 + tick;
        n[N_ZIP_QUEUED] += 1;
    }
    return 0;
}

// the HUD image PS3 shows while the local player of the screen being drawn is spotted
extern "C" U3X_FN void u3x_hud_spotted(uintptr_t base)
{
    using namespace g33c;
    uint32_t *h = ext_words(EXT_SPOTTED);
    h[H_CALLS] += 1;

    const uint8_t *settings = game_call<const uint8_t *, uint32_t>(base + FN_SYMBOL_DATA, SYMBOL_HUD_SETTINGS);
    if (!settings) return;
    const uint8_t *object = rd<const uint8_t *>(settings, 0x78);
    const uint8_t *hud = at<const uint8_t *>(base + G_HUD);
    if (!object || !hud) return;
    const int32_t screen = rd<int32_t>(hud, 0x1d0);
    if (screen != 0 && screen != 1) return;
    uint8_t *player = at<uint8_t *>(base + G_LOCAL_PLAYERS + 8 * static_cast<uintptr_t>(screen));
    if (!player) return;
    if (!u3h_spot_flag(base, player)) return;
    const uint8_t *element = rd<const uint8_t *>(object, 0x158);
    if (!element) return;

    // the element is drawn also when its image is not in the table (index -1: kept)
    const uint32_t image = rd<uint32_t>(element, 4);
    const int32_t index = image_index(base, image);
    // the colour is copied before the counters are written
    alignas(16) uint32_t colour[4];
    for (int k = 0; k < 4; k++) colour[k] = rd<uint32_t>(element, 0x30 + 4 * static_cast<uintptr_t>(k));

    h[H_IMAGE] = image;
    h[H_INDEX] = static_cast<uint32_t>(index);
    h[H_DRAWS] += 1;

    GameCall call;
    clear_call(&call);
    call.fn = base + FN_SPRITE_DRAW;
    call.i[1] = image;
    call.i[2] = static_cast<uint32_t>(index);
    call.i[3] = reinterpret_cast<uintptr_t>(colour);
    call.i[4] = rd<int32_t>(element, 0x24) != 0 ? 1 : 0;
    call.i[5] = 1;
    call.x[0] = pattern_pair(element + 0x1c);                        // position x, y
    call.x[1] = pattern_pair(element + 0x14);                        // width, height
    call.x[2] = pattern_pair(element + 0x28);                        // direction pair
    call.x[4] = static_cast<uint64_t>(F_ONE) << 32 | F_ONE;          // 1.0, 1.0
    u3x_k26_call(&call);
}
