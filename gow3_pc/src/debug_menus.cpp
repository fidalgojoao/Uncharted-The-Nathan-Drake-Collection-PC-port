// Uncharted 1, 2 and 3 1.02, 21 Nov 2026 (issue 70): the debug mode's Dev Menu and Quick Menu open from a PC
// controller.
//
// With the launcher's debug mode on (the games' own debug-mode byte held at 1, launcher/native/runtime_env.cpp), the
// games' menu manager opens the Dev Menu or the Quick Menu once per frame when a "debug combo" byte of the active
// joypad has bit 4 (Dev Menu) or bit 8 (Quick Menu) newly set; a second request after 30 frames closes the menu. The
// joypad code fills that byte from a source the PC runtime never delivers, so the menus stayed shut while debug fly
// and the L3 + Triangle debug text (both read the ordinary buttons) worked. Where the manager reads the byte, it now
// also gets those bits from the ordinary held buttons, on the PS3 versions' combos:
//   L3 + Options                       Dev Menu    (Xbox pad: left stick click + Start; keyboard: Left Shift + Esc)
//   L3 + touchpad click                Quick Menu  (Xbox pad: left stick click + Back;  keyboard: Left Shift + Tab)
// A bit is set on the frame both buttons are first held together, and the press of Options / the touchpad is taken
// out of that frame's newly pressed buttons, so code reading those after the manager does not also open the pause
// menu or the touchpad action. Nothing else changes: with debug mode off the hook is not installed, and the game's
// own byte is kept (or-ed in). Uncharted 3: joypad = the active joypad (state + 0x7e0 + index * 0x1d8: buttons
// newly pressed at +0x94, held at +0x9c, the combo byte at +0xad); Uncharted 2: the first joypad (0x1116180 + 0x7d0,
// the same layout); Uncharted 1: the first joypad (0xa66f10 + 0x608, the same layout). Found by their bytes.
#include "debug_menus.h"
#include "u3fix/u3fix_hook.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// both combos' state at the previous check (bit 4 Dev Menu, bit 8 Quick Menu), one byte per game
unsigned char held_before[3];

// U3: imul r15,rax,0x1d8; mov al,[r12+r15+0x88d]; mov dl,1; mov sil,1; test al,4
constexpr unsigned char kSite3[] = {0x4c, 0x69, 0xf8, 0xd8, 0x01, 0x00, 0x00, 0x43, 0x8a, 0x84, 0x3c, 0x8d,
                                    0x08, 0x00, 0x00, 0xb2, 0x01, 0x40, 0xb6, 0x01, 0xa8, 0x04};
constexpr unsigned kAt3 = 7, kLength3 = 8;

// r12 = joypad state, r15 = joypad index * 0x1d8, rbx = engine settings (+0x4bfc debug mode); al = the combo byte
// (rdx, rsi are set again right after; rcx, the debug-keyboard flags, is kept)
constexpr unsigned char kStub3[] = {
    0x43, 0x8a, 0x84, 0x3c, 0x8d, 0x08, 0x00, 0x00,                    // mov al,[r12+r15+0x88d]   the game's byte
    0x80, 0xbb, 0xfc, 0x4b, 0x00, 0x00, 0x00,                          // cmp byte [rbx+0x4bfc],0  debug mode off?
    0x74, 0x61,                                                        // je done
    0x41, 0x50,                                                        // push r8
    0x43, 0x8b, 0x94, 0x3c, 0x7c, 0x08, 0x00, 0x00,                    // mov edx,[r12+r15+0x87c]  held
    0x31, 0xf6,                                                        // xor esi,esi
    0xf6, 0xc2, 0x02,                                                  // test dl,2                L3
    0x74, 0x13,                                                        // je edge
    0xf6, 0xc2, 0x08,                                                  // test dl,8                Options
    0x74, 0x03,                                                        // je +3
    0x83, 0xce, 0x04,                                                  // or esi,4
    0xf7, 0xc2, 0x00, 0x00, 0x10, 0x00,                                // test edx,0x100000        touchpad
    0x74, 0x03,                                                        // je edge
    0x83, 0xce, 0x08,                                                  // or esi,8
    0x48, 0xba, 0, 0, 0, 0, 0, 0, 0, 0,                                // edge: mov rdx,held_before
    0x44, 0x0f, 0xb6, 0x02,                                            // movzx r8d,byte [rdx]
    0x40, 0x88, 0x32,                                                  // mov [rdx],sil
    0x41, 0xf7, 0xd0,                                                  // not r8d
    0x44, 0x21, 0xc6,                                                  // and esi,r8d              newly held
    0x41, 0x58,                                                        // pop r8
    0x40, 0x08, 0xf0,                                                  // or al,sil
    0x40, 0xf6, 0xc6, 0x04,                                            // test sil,4
    0x74, 0x09,                                                        // je +9
    0x43, 0x83, 0xa4, 0x3c, 0x74, 0x08, 0x00, 0x00, 0xf7,              // and dword [r12+r15+0x874],~8
    0x40, 0xf6, 0xc6, 0x08,                                            // test sil,8
    0x74, 0x0c,                                                        // je done
    0x43, 0x81, 0xa4, 0x3c, 0x74, 0x08, 0x00, 0x00, 0xff, 0xff, 0xef, 0xff,   // and dword [..+0x874],~0x100000
    0xff, 0x25, 0, 0, 0, 0};                                           // done: jmp [rip] (back follows)
constexpr unsigned kHeld3 = 0x37;

// U2: lea rcx,[rip+0xc61d22] (0x1116180); mov cl,[rcx+0x87d]; test cl,4; je +4; xor esi,esi
constexpr unsigned char kSite2[] = {0x48, 0x8d, 0x0d, 0x22, 0x1d, 0xc6, 0x00, 0x8a, 0x89, 0x7d, 0x08,
                                    0x00, 0x00, 0xf6, 0xc1, 0x04, 0x74, 0x04, 0x31, 0xf6};
constexpr unsigned kAt2 = 7, kLength2 = 6;

// rcx = joypad state, al = debug mode, edx = the debug-keyboard flags; cl = the combo byte (rax, rdx, rsi, r8 kept)
constexpr unsigned char kStub2[] = {
    0x50, 0x52, 0x56, 0x41, 0x50,                                      // push rax, rdx, rsi, r8
    0x48, 0x89, 0xca,                                                  // mov rdx,rcx
    0x8a, 0x8a, 0x7d, 0x08, 0x00, 0x00,                                // mov cl,[rdx+0x87d]       the game's byte
    0x84, 0xc0,                                                        // test al,al               debug mode off?
    0x74, 0x54,                                                        // je done
    0x8b, 0x82, 0x6c, 0x08, 0x00, 0x00,                                // mov eax,[rdx+0x86c]      held
    0x31, 0xf6,                                                        // xor esi,esi
    0xa8, 0x02,                                                        // test al,2                L3
    0x74, 0x11,                                                        // je edge
    0xa8, 0x08,                                                        // test al,8                Options
    0x74, 0x03,                                                        // je +3
    0x83, 0xce, 0x04,                                                  // or esi,4
    0xa9, 0x00, 0x00, 0x10, 0x00,                                      // test eax,0x100000        touchpad
    0x74, 0x03,                                                        // je edge
    0x83, 0xce, 0x08,                                                  // or esi,8
    0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0,                                // edge: mov rax,held_before + 1
    0x44, 0x0f, 0xb6, 0x00,                                            // movzx r8d,byte [rax]
    0x40, 0x88, 0x30,                                                  // mov [rax],sil
    0x41, 0xf7, 0xd0,                                                  // not r8d
    0x44, 0x21, 0xc6,                                                  // and esi,r8d              newly held
    0x40, 0x08, 0xf1,                                                  // or cl,sil
    0x40, 0xf6, 0xc6, 0x04,                                            // test sil,4
    0x74, 0x07,                                                        // je +7
    0x83, 0xa2, 0x64, 0x08, 0x00, 0x00, 0xf7,                          // and dword [rdx+0x864],~8
    0x40, 0xf6, 0xc6, 0x08,                                            // test sil,8
    0x74, 0x0a,                                                        // je done
    0x81, 0xa2, 0x64, 0x08, 0x00, 0x00, 0xff, 0xff, 0xef, 0xff,        // and dword [rdx+0x864],~0x100000
    0x41, 0x58, 0x5e, 0x5a, 0x58,                                      // done: pop r8, rsi, rdx, rax
    0xff, 0x25, 0, 0, 0, 0};                                           // jmp [rip] (back follows)
constexpr unsigned kHeld2 = 0x31;

// U1: lea r12,[rip+0x97de81] (0xa66f10); mov cl,[r12+0x6b5]; lea rbx,[rip+0x86f16a] (0x958208);
//     mov al,[rbx+0x4844]; test cl,4
constexpr unsigned char kSite1[] = {0x4c, 0x8d, 0x25, 0x81, 0xde, 0x97, 0x00, 0x41, 0x8a, 0x8c, 0x24, 0xb5, 0x06,
                                    0x00, 0x00, 0x48, 0x8d, 0x1d, 0x6a, 0xf1, 0x86, 0x00, 0x8a, 0x83, 0x44, 0x48,
                                    0x00, 0x00, 0xf6, 0xc1, 0x04};
constexpr unsigned kAt1 = 22, kLength1 = 6;

// r12 = joypad state, rbx = engine settings (+0x4844 debug mode), cl = the combo byte; al = debug mode (rdx, rsi, r8
// kept)
constexpr unsigned char kStub1[] = {
    0x8a, 0x83, 0x44, 0x48, 0x00, 0x00,                                // mov al,[rbx+0x4844]      debug mode
    0x84, 0xc0,                                                        // test al,al               off?
    0x74, 0x65,                                                        // je done
    0x52, 0x56, 0x41, 0x50,                                            // push rdx, rsi, r8
    0x41, 0x8b, 0x94, 0x24, 0xa4, 0x06, 0x00, 0x00,                    // mov edx,[r12+0x6a4]      held
    0x31, 0xf6,                                                        // xor esi,esi
    0xf6, 0xc2, 0x02,                                                  // test dl,2                L3
    0x74, 0x13,                                                        // je edge
    0xf6, 0xc2, 0x08,                                                  // test dl,8                Options
    0x74, 0x03,                                                        // je +3
    0x83, 0xce, 0x04,                                                  // or esi,4
    0xf7, 0xc2, 0x00, 0x00, 0x10, 0x00,                                // test edx,0x100000        touchpad
    0x74, 0x03,                                                        // je edge
    0x83, 0xce, 0x08,                                                  // or esi,8
    0x48, 0xba, 0, 0, 0, 0, 0, 0, 0, 0,                                // edge: mov rdx,held_before + 2
    0x44, 0x0f, 0xb6, 0x02,                                            // movzx r8d,byte [rdx]
    0x40, 0x88, 0x32,                                                  // mov [rdx],sil
    0x41, 0xf7, 0xd0,                                                  // not r8d
    0x44, 0x21, 0xc6,                                                  // and esi,r8d              newly held
    0x40, 0x08, 0xf1,                                                  // or cl,sil
    0x40, 0xf6, 0xc6, 0x04,                                            // test sil,4
    0x74, 0x09,                                                        // je +9
    0x41, 0x83, 0xa4, 0x24, 0x9c, 0x06, 0x00, 0x00, 0xf7,              // and dword [r12+0x69c],~8
    0x40, 0xf6, 0xc6, 0x08,                                            // test sil,8
    0x74, 0x0c,                                                        // je pop
    0x41, 0x81, 0xa4, 0x24, 0x9c, 0x06, 0x00, 0x00, 0xff, 0xff, 0xef, 0xff,   // and dword [..+0x69c],~0x100000
    0x41, 0x58, 0x5e, 0x5a,                                            // pop: pop r8, rsi, rdx
    0xff, 0x25, 0, 0, 0, 0};                                           // done: jmp [rip] (back follows)
constexpr unsigned kHeld1 = 0x32;

bool debug_mode_wanted() {
    const char *list = std::getenv("UNCHARTED_CHEATS");
    if (!list) return false;
    for (const char *p = list; *p;) {
        while (*p == ',' || *p == ' ') ++p;
        const char *end = p;
        while (*end && *end != ',') ++end;
        if (end - p == 5 && std::strncmp(p, "debug", 5) == 0) return true;
        p = end;
    }
    return false;
}

bool install(unsigned char *image, uint64_t size, const unsigned char *site, unsigned site_length, unsigned at,
             unsigned length, const unsigned char *code, unsigned code_length, unsigned held_at, unsigned char *held) {
    const uint64_t found = u3fix::find_unique(image, size, site, site_length);
    if (found == u3fix::kNotFound) return false;
    unsigned char *stub = u3fix::stub_page_near(image, size);
    if (!stub) return false;
    std::memcpy(stub, code, code_length);
    const uint64_t state = reinterpret_cast<uint64_t>(held), back = reinterpret_cast<uint64_t>(image + found + at + length);
    std::memcpy(stub + held_at, &state, 8);
    std::memcpy(stub + code_length, &back, 8);
    return u3fix::finish_stub(stub) && u3fix::jump_to(image, found + at, length, stub);
}

}  // namespace

extern "C" int debug_menus_install(unsigned char *image, uint64_t size) {
    if (!image || !size || !debug_mode_wanted()) return 0;
    const char *game = nullptr;
    if (install(image, size, kSite3, sizeof kSite3, kAt3, kLength3, kStub3, sizeof kStub3, kHeld3, &held_before[0]))
        game = "Uncharted 3";
    else if (install(image, size, kSite2, sizeof kSite2, kAt2, kLength2, kStub2, sizeof kStub2, kHeld2, &held_before[1]))
        game = "Uncharted 2";
    else if (install(image, size, kSite1, sizeof kSite1, kAt1, kLength1, kStub1, sizeof kStub1, kHeld1, &held_before[2]))
        game = "Uncharted 1";
    if (!game) return 0;
    std::printf("%s: debug mode menus on L3 + Options (Dev Menu) and L3 + touchpad (Quick Menu)\n", game);
    std::fflush(stdout);
    return 1;
}
