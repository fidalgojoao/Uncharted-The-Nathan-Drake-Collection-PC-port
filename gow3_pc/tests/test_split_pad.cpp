// The second local player's pad (issue 31): which device each player uses (src/runtime_split.cpp) and the game's pad
// port 1 fed from it (src/u3fix/split_pad.cpp) on a stand-in image. No game needed: ninja -C out/gpu split-pad-test.
#include "runtime_split.h"
#include "u3fix/u3fix_hook.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

// image offsets of the game's pad blocks (split_pad.cpp)
constexpr uint64_t kWork = 0x1d60150, kPort0 = 0x1d601d0, kPort1 = 0x1d6024c, kCount = 0x1d60728;
constexpr uint64_t kNetGame = 0x17d5c61, kSplitSwitch = 0x17d7b06, kSecondState = 0x17f2fc0;

std::vector<unsigned char> g_image(0x1d70000);
unsigned char *image() { return g_image.data(); }
bool g_second_device = true;
int g_calls = 0;

int32_t get32(uint64_t at) { int32_t v; std::memcpy(&v, image() + at, 4); return v; }
void put32(uint64_t at, int32_t v) { std::memcpy(image() + at, &v, 4); }

// stands in for the game's update: reads the pad of the work block's handle (+0x78); a read stores its buttons at +0
// and the previous ones at +0x68, marks +0x74 and is copied to port 0's block; the sample queue shift (count >= 0)
// wipes port 1's block
__attribute__((sysv_abi)) uint32_t fake_update(unsigned char *ports) {
    (void)ports;
    ++g_calls;
    unsigned char *work = image() + kWork;
    const int32_t handle = get32(kWork + 0x78);
    const bool ok = handle == 1 || (handle == RUNTIME_PAD2_HANDLE && g_second_device);
    work[0x74] = ok;
    if (ok) {
        std::memcpy(work + 0x68, work, 4);
        const int32_t buttons = handle * 0x100 + g_calls;
        std::memcpy(work, &buttons, 4);
        std::memcpy(image() + kPort0, work, 0x7c);
    }
    if (get32(kCount) >= 0) std::memset(image() + kPort1 + 4, 0, 0x78);
    put32(kCount, 0);
    return 7;
}

void devices() {
    // a second controller is player 2's; else the keyboard and mouse when player 1 has a controller
    assert(runtime_split_p2_source(1, 1, 1, 0) == SPLIT_P2_GAMEPAD);
    assert(runtime_split_p2_source(1, 0, 1, 0) == SPLIT_P2_GAMEPAD);
    assert(runtime_split_p2_source(0, 1, 1, 0) == SPLIT_P2_KEYS);
    assert(runtime_split_p2_source(0, 0, 1, 0) == SPLIT_P2_NONE);   // keyboard and mouse off
    assert(runtime_split_p2_source(0, 1, 0, 0) == SPLIT_P2_NONE);   // the keyboard is player 1's only device
    assert(runtime_split_p2_source(1, 1, 1, 2) == SPLIT_P2_NONE);   // GOW3_PAD_INDEX: one window per controller
    // the keyboard and mouse leave player 1 only while player 2 is read and they are player 2's
    assert(runtime_split_keys_taken(1, SPLIT_P2_KEYS));
    assert(!runtime_split_keys_taken(0, SPLIT_P2_KEYS));
    assert(!runtime_split_keys_taken(1, SPLIT_P2_GAMEPAD));
    assert(!runtime_split_keys_taken(1, SPLIT_P2_NONE));
    // no second controller without SDL's gamepad subsystem; player 2 counts as read for half a second
    assert(runtime_split_gamepad2(nullptr, 0) == nullptr);
    assert(!runtime_split_p2_active());
    runtime_split_p2_read();
    assert(runtime_split_p2_active());
}

void port_one() {
    put32(kWork + 0x78, 1);   // the game's own pad
    // no net game: one update, port 1 untouched
    assert(u3fix::split_pad_frame(image(), nullptr, fake_update) == 7 && g_calls == 1);
    assert(get32(kPort0) == 0x101 && get32(kPort1) == 0);
    // net game without a second local player: still one update
    image()[kNetGame] = 1;
    assert(u3fix::split_pad_frame(image(), nullptr, fake_update) == 7 && g_calls == 2);
    // the second player's state 3: player 2 first into port 1, then player 1 into port 0, with the game's work block
    put32(kSecondState, 3);
    assert(u3fix::split_pad_frame(image(), nullptr, fake_update) == 7 && g_calls == 4);
    assert(get32(kPort1) == 0x203 && get32(kPort1 + 0x78) == RUNTIME_PAD2_HANDLE && image()[kPort1 + 0x74] == 1);
    assert(get32(kPort0) == 0x104 && get32(kWork + 0x78) == 1);
    assert(get32(kWork + 0x68) == 0x102);   // player 1's previous report is player 1's
    u3fix::split_pad_frame(image(), nullptr, fake_update);
    assert(get32(kPort1) == 0x205 && get32(kPort1 + 0x68) == 0x203);   // player 2's previous report is player 2's
    assert(get32(kPort0) == 0x106 && get32(kWork + 0x68) == 0x104);
    // the second device goes away: port 1 disconnected, port 0 still player 1
    g_second_device = false;
    u3fix::split_pad_frame(image(), nullptr, fake_update);
    assert(image()[kPort1 + 0x74] == 0 && get32(kPort1) == 0 && get32(kPort0) == 0x108);
    g_second_device = true;
    u3fix::split_pad_frame(image(), nullptr, fake_update);
    assert(get32(kPort1) == 0x209 && image()[kPort1 + 0x74] == 1);
    // the split switch alone counts too; leaving the net game ends it and empties port 1
    put32(kSecondState, 0);
    image()[kSplitSwitch] = 1;
    u3fix::split_pad_frame(image(), nullptr, fake_update);
    assert(g_calls == 12 && get32(kPort1) == 0x20b);
    image()[kNetGame] = 0;
    u3fix::split_pad_frame(image(), nullptr, fake_update);
    assert(g_calls == 13 && get32(kPort1) == 0 && get32(kPort0) == 0x10d);
}

}  // namespace

int main() {
    devices();
    port_one();
    std::puts("Second player pad tests passed");
    return 0;
}
