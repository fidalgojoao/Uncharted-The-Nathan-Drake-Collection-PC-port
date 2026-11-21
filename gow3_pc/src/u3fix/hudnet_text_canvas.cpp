// Uncharted 3 1.02, 21 Nov 2026: the multiplayer HUD's player names and markers use the HUD's own 1280x720 canvas.
//
// The multiplayer HUD (the names over other players, their markers, point pop-ups, centred match messages) lays its
// elements out on a fixed 1280x720 canvas that the renderer scales to the screen. Some of its steps, though, read the
// game's display size (a width/height pair the game sets to 1920x1080 on this build) where the canvas size belongs: the
// name over a player is projected with it, so it lands at 1.5 times its right place from the top-left corner (GitHub
// #64: the name far right of and below the player); the right-edge limit for names and the centring of messages use it
// too. On PS3 the display size was 1280x720, the same as the canvas, so both agreed.
//
// The HUD code reaches the display size through 13 "load the address of the display size" instructions. Each now
// loads the address of our own copy whose width and height are 1280 and 720; the rest of the game keeps reading the
// real one. All 13 are found from one anchor by their target, and only when exactly 13 are there. Found by its bytes.
#include "u3fix.h"
#include "u3fix_hook.h"

#include <climits>
#include <cstdio>
#include <cstring>

namespace u3fix {

namespace {

// (lea rax,[display size]) mov rcx,rax; mov eax,[rcx+8]; mov ecx,[rcx+0xc]; vcvtsi2ss xmm3,xmm0,rax
// (the name's projection in the HUD's per-player update)
constexpr unsigned char kAnchor[] = {0x48, 0x89, 0xc1, 0x8b, 0x41, 0x08, 0x8b, 0x49, 0x0c, 0xc4, 0xe1, 0xfa, 0x2a, 0xd8};
constexpr unsigned kLea = 7;                 // lea rax,[rip+rel32]: 48 8d 05 rel32
constexpr uint64_t kBefore = 0x3800;         // the HUD code around the anchor that is searched
constexpr uint64_t kAfter = 0xa000;
constexpr unsigned kSites = 13;
constexpr uint32_t kWidth = 1280, kHeight = 720;

int32_t rel32(const unsigned char *p) {
    int32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

bool is_lea_rax(const unsigned char *p) { return p[0] == 0x48 && p[1] == 0x8d && p[2] == 0x05; }

}  // namespace

bool install_hudnet_text_canvas(unsigned char *image, uint64_t size) {
    const uint64_t anchor = find_unique(image, size, kAnchor, sizeof kAnchor);
    if (anchor == kNotFound || anchor < kLea) return false;
    const uint64_t lea = anchor - kLea;
    if (!is_lea_rax(image + lea)) return false;
    const int64_t display = static_cast<int64_t>(anchor) + rel32(image + lea + 3);   // image offset of the display size
    if (display < 0 || static_cast<uint64_t>(display) + 16 > size) return false;

    uint64_t sites[kSites];
    unsigned found = 0;
    const uint64_t from = lea > kBefore ? lea - kBefore : 0;
    const uint64_t to = lea + kAfter + kLea < size ? lea + kAfter : size - kLea;
    for (uint64_t at = from; at < to; at++) {
        if (!is_lea_rax(image + at) || static_cast<int64_t>(at + kLea) + rel32(image + at + 3) != display) continue;
        if (found == kSites) return false;   // more than expected: not the build this was written for
        sites[found++] = at;
    }
    if (found != kSites) return false;

    // our copy of the display size: the same 16 bytes with the canvas's width and height
    unsigned char *copy = stub_page_near(image, size);
    if (!copy) return false;
    for (unsigned i = 0; i < kSites; i++) {   // every site must reach the copy before any is changed
        const int64_t delta = reinterpret_cast<intptr_t>(copy) - reinterpret_cast<intptr_t>(image + sites[i] + kLea);
        if (delta < INT32_MIN || delta > INT32_MAX) return false;
    }
    std::memcpy(copy, image + display, 16);
    std::memcpy(copy + 8, &kWidth, 4);
    std::memcpy(copy + 12, &kHeight, 4);
    if (!finish_stub(copy)) return false;
    for (unsigned i = 0; i < kSites; i++) {
        const int32_t delta = static_cast<int32_t>(reinterpret_cast<intptr_t>(copy) -
                                                   reinterpret_cast<intptr_t>(image + sites[i] + kLea));
        std::memcpy(image + sites[i] + 3, &delta, 4);
    }
    std::printf("Uncharted 3: multiplayer HUD names and markers use its 1280x720 canvas (%u places from %#llx)\n", kSites,
                static_cast<unsigned long long>(lea));
    std::fflush(stdout);
    return true;
}

}  // namespace u3fix
