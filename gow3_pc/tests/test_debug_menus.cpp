// debug_menus.cpp on a stand-in image (no game needed): ninja -C out/gpu debug-menus-test
// The Uncharted 1 site bytes sit at their place in a zeroed 18 MiB image (so their rip-relative leas reach the joypad
// state and the engine settings in it), wrapped as a callable function that returns the debug-mode byte (al) and the
// combo byte the menu manager goes on with (ah). The test checks the combos, the edge, that the Options / touchpad
// press is taken out, that the game's own byte is kept, and that nothing happens with debug mode off.
#include "debug_menus.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <windows.h>

namespace {

constexpr uint64_t kSize = 0x1200000, kAt = 0xe9085;   // the site starts at 0xe9088, after push r12 / push rbx
constexpr uint64_t kPad = 0xa66f10 + 0x608, kDebug = 0x958208 + 0x4844;
constexpr unsigned char kCode[] = {
    0x41, 0x54, 0x53,                                                  // push r12, rbx
    0x4c, 0x8d, 0x25, 0x81, 0xde, 0x97, 0x00, 0x41, 0x8a, 0x8c, 0x24, 0xb5, 0x06, 0x00, 0x00,   // the game's site
    0x48, 0x8d, 0x1d, 0x6a, 0xf1, 0x86, 0x00, 0x8a, 0x83, 0x44, 0x48, 0x00, 0x00, 0xf6, 0xc1, 0x04,
    0x88, 0xcc,                                                        // mov ah,cl
    0x5b, 0x41, 0x5c, 0xc3};                                           // pop rbx, r12; ret

unsigned char *image;

uint32_t &pressed() { return *reinterpret_cast<uint32_t *>(image + kPad + 0x94); }
uint32_t &held() { return *reinterpret_cast<uint32_t *>(image + kPad + 0x9c); }
unsigned char &combo() { return image[kPad + 0xad]; }

// one menu manager check: returns (combo byte << 8) | debug-mode byte
unsigned check() {
    FlushInstructionCache(GetCurrentProcess(), image + kAt, sizeof kCode);
    return reinterpret_cast<unsigned (*)()>(image + kAt)() & 0xffff;
}

}  // namespace

int main() {
    image = static_cast<unsigned char *>(VirtualAlloc(nullptr, kSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    assert(image);
    std::memcpy(image + kAt, kCode, sizeof kCode);

    // debug mode off in the launcher: no hook, the image stays as it is
    _putenv_s("UNCHARTED_CHEATS", "invincible");
    assert(debug_menus_install(image, kSize) == 0);
    assert(std::memcmp(image + kAt, kCode, sizeof kCode) == 0);

    _putenv_s("UNCHARTED_CHEATS", "ammo, debug");
    assert(debug_menus_install(image, kSize) == 1);
    assert(image[0xe909e] == 0xe9 && image[0xe90a3] == 0x90);   // jmp rel32 + nop over mov al,[rbx+0x4844]

    image[kDebug] = 1;
    // L3 + Options: Dev Menu (bit 4) on the first frame only, and that frame's Options press is taken out
    held() = 0x2 | 0x8;
    pressed() = 0x8;
    assert(check() == ((0x4 << 8) | 1));
    assert(pressed() == 0);
    pressed() = 0x8;
    assert(check() == 1);
    assert(pressed() == 0x8);
    held() = 0;
    pressed() = 0;
    assert(check() == 1);
    // L3 + touchpad: Quick Menu (bit 8); the touchpad press is taken out
    held() = 0x2 | 0x100000;
    pressed() = 0x100000 | 0x1000;
    assert(check() == ((0x8 << 8) | 1));
    assert(pressed() == 0x1000);
    held() = 0;
    // Options or the touchpad without L3: nothing
    held() = 0x8 | 0x100000;
    assert(check() == 1);
    held() = 0;
    // the game's own byte is kept
    combo() = 0x10;
    assert(check() == ((0x10 << 8) | 1));
    combo() = 0;
    // the game's debug-mode byte at 0: no bits and the presses stay
    image[kDebug] = 0;
    held() = 0x2 | 0x8;
    pressed() = 0x8;
    assert(check() == 0);
    assert(pressed() == 0x8);
    std::puts("debug-menus-test: ok");
    return 0;
}
