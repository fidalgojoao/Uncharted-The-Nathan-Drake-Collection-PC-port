// Uncharted 3 1.02, 21 Nov 2026 (GitHub #31): in splitscreen the second local player's pad feeds the game's pad
// port 1.
//
// The game's input manager keeps two pad ports. Once per frame its pad update (image 0x10ddd90, called from the input
// manager at 0x10ff783 with the ports' converted state at 0x1d61400) reads the pad of the handle kept in a 0x7c-byte
// work block (image 0x1d60150, handle at +0x78), turns the report into the block's values (sticks, buttons, presses
// and releases against the previous report, touches), copies the block to port 0's block (0x1d601d0) and converts both
// port blocks (port 1's at 0x1d6024c) into the ports the logical pads read. The PS4 build opens one pad, for its
// initial user, and never fills port 1's block, so a second local player has no input.
//
// While the game has a second local player (a net game, image 0x17d5c61, with the split switch at 0x17d7b06 or the
// second player's state at 0x17f2fc0 equal to 3: the game's own rule, see its local player count at 0x7a2c24), the
// update now runs twice per frame. First with the second player's own work block (handle 2: the runtime's second
// player pad, src/runtime_split.h); what that gives port 0's block is copied to port 1's. Then with player 1's work
// block as usual, so player 1's presses and releases are worked out against player 1's previous report only. The
// sample queue the update shifts at the end (count at 0x1d60728, unused by the PS4 game, its shift overwrites port 1's
// block) is kept off in these frames. Without a second player the update runs once, exactly as before. Found by its
// bytes.
#include "u3fix.h"
#include "u3fix_hook.h"

#include <climits>
#include <cstdio>
#include <cstring>

namespace u3fix {

namespace {

constexpr uint64_t kWork = 0x1d60150, kPort0 = 0x1d601d0, kPort1 = 0x1d6024c, kQueueCount = 0x1d60728;
constexpr uint64_t kBlock = 0x7c, kHandleAt = 0x78, kReadAt = 0x74;   // kReadAt: set by a successful read
constexpr uint64_t kNetGame = 0x17d5c61, kSplitSwitch = 0x17d7b06, kSecondState = 0x17f2fc0;
constexpr int32_t kSecondPad = 2;   // RUNTIME_PAD2_HANDLE (src/runtime_split.h)

// lea rdi,[rip+0xc61c80] (the ports); mov r14,rdi; call 0x10ddd90; mov [r15+0x1380],eax
constexpr unsigned char kSite[] = {0x48, 0x8d, 0x3d, 0x80, 0x1c, 0xc6, 0x00, 0x49, 0x89, 0xfe, 0xe8, 0x08,
                                   0xe6, 0xfd, 0xff, 0x41, 0x89, 0x87, 0x80, 0x13, 0x00, 0x00};
constexpr uint64_t kSiteAt = 0x10ff779, kCall = 10, kUpdate = 0x10ddd90;
// the update's read of the handle: mov edi,[rip+0xc8240d] (= the work block's +0x78), at image 0x10dddb5
constexpr unsigned char kHandleRead[] = {0x8b, 0x3d, 0x0d, 0x24, 0xc8, 0x00};
constexpr uint64_t kHandleReadAt = 0x10dddb5;

unsigned char *g_image = nullptr;
SplitPadUpdate g_update = nullptr;
unsigned char g_second[kBlock];   // the second player's work block between frames
bool g_second_on = false;

int32_t read32(const unsigned char *p) { int32_t v; std::memcpy(&v, p, 4); return v; }

bool second_local_player(const unsigned char *image) {
    return image[kNetGame] && (image[kSplitSwitch] || read32(image + kSecondState) == 3);
}

}  // namespace

uint32_t split_pad_frame(unsigned char *image, unsigned char *ports, SplitPadUpdate update) {
    if (!second_local_player(image)) {
        if (g_second_on) {
            g_second_on = false;
            std::memset(image + kPort1, 0, kBlock);   // port 1 disconnected again
        }
        return update(ports);
    }
    if (!g_second_on) {
        g_second_on = true;
        std::memset(g_second, 0, kBlock);
        std::memcpy(g_second + kHandleAt, &kSecondPad, 4);
        std::printf("Uncharted 3: splitscreen: the second player's pad feeds pad port 1\n");
        std::fflush(stdout);
    }
    const int32_t off = -1;
    unsigned char first[kBlock];
    std::memcpy(first, image + kWork, kBlock);
    std::memcpy(image + kWork, g_second, kBlock);
    std::memcpy(image + kQueueCount, &off, 4);
    update(ports);
    std::memcpy(g_second, image + kWork, kBlock);
    // the update copies the work block to port 0's block only after a successful read: without one (no second
    // device) port 0's block still holds player 1's last state, so port 1 gets an empty (disconnected) block
    if (g_second[kReadAt]) std::memcpy(image + kPort1, image + kPort0, kBlock);
    else std::memset(image + kPort1, 0, kBlock);
    std::memcpy(image + kWork, first, kBlock);
    std::memcpy(image + kQueueCount, &off, 4);
    return update(ports);
}

namespace {

__attribute__((sysv_abi)) uint32_t split_pad_update(unsigned char *ports) { return split_pad_frame(g_image, ports, g_update); }

}  // namespace

bool install_split_pad(unsigned char *image, uint64_t size) {
    if (!image || size <= kQueueCount + 4) return false;
    if (find_unique(image, size, kSite, sizeof kSite) != kSiteAt) return false;
    if (std::memcmp(image + kHandleReadAt, kHandleRead, sizeof kHandleRead) != 0) return false;
    unsigned char *stub = stub_page_near(image, size);
    if (!stub) return false;
    g_image = image;
    g_update = reinterpret_cast<SplitPadUpdate>(image + kUpdate);
    // jmp [rip]; the host function's address
    const unsigned char jump[] = {0xff, 0x25, 0, 0, 0, 0};
    const uint64_t target = reinterpret_cast<uint64_t>(&split_pad_update);
    std::memcpy(stub, jump, sizeof jump);
    std::memcpy(stub + sizeof jump, &target, 8);
    if (!finish_stub(stub)) return false;
    // call stub, in place of the call of the update (same length; the stub returns to the caller's next instruction)
    const int64_t delta = static_cast<int64_t>(reinterpret_cast<uintptr_t>(stub)) -
                          static_cast<int64_t>(reinterpret_cast<uintptr_t>(image + kSiteAt + kCall + 5));
    if (delta < INT32_MIN || delta > INT32_MAX) return false;
    const int32_t rel = static_cast<int32_t>(delta);
    image[kSiteAt + kCall] = 0xe8;
    std::memcpy(image + kSiteAt + kCall + 1, &rel, 4);
    std::printf("Uncharted 3: splitscreen: pad port 1 reads the second player's pad (%#llx)\n",
                static_cast<unsigned long long>(kSiteAt + kCall));
    std::fflush(stdout);
    return true;
}

}  // namespace u3fix
