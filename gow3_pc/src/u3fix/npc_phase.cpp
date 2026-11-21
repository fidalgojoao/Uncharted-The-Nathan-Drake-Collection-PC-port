// Uncharted 3 1.02, 21 Nov 2026: an NPC spawned on a co-op guest starts its base animation at a finite phase.
//
// When a guest makes its copy of an NPC (the host's NPC spawn event, issue 58), the NPC's set-up (image 0x34e4ca, run
// only on a guest of a match session) starts the base animation layer (key 0x7efa5a21) at the phase "clip value +0x28
// divided by clip value +0x1c, times 0.25". The division is not checked: for a clip whose +0x1c is 0 the phase is
// 0/0 or x/0, and the clamp after it lets a non-finite value through. The NPC's pose, its weapon aim, its ragdoll and
// then the guest's own camera turn non-finite (co-op Dig, 21 Nov 2026: 160,000 assertions on the guest, none on the
// host, the guest's aim drifting). A zero divisor or a non-finite result now takes 1.0, the value the same code uses
// when the NPC has no such clip; every other clip gets exactly the phase it got before. Found by its bytes.
#include "u3fix.h"
#include "u3fix_hook.h"

#include <cstdio>
#include <cstring>

namespace u3fix {

namespace {

// test rax,rax; je +0x18; mov rax,[rax+0x28]; vmovss xmm0,[rax+0x28]; vdivss xmm0,xmm0,[rax+0x1c];
// vmulss xmm0,xmm0,[rip+0x10f1f6d]; jmp +8
constexpr unsigned char kSite[] = {0x48, 0x85, 0xc0, 0x74, 0x18, 0x48, 0x8b, 0x40, 0x28, 0xc5, 0xfa, 0x10, 0x40, 0x28,
                                   0xc5, 0xfa, 0x5e, 0x40, 0x1c, 0xc5, 0xfa, 0x59, 0x05, 0x6d, 0x1f, 0x0f, 0x01, 0xeb, 0x08};
constexpr unsigned kAt = 9;        // the vmovss
constexpr unsigned kLength = 20;   // vmovss, vdivss, vmulss, jmp
constexpr unsigned kBack = 9 + 20 + 8;   // the jmp's target, where the phase is used

// the phase from the clip at rax into xmm0 (xmm1, xmm2, eax changed; all are set again before the code after uses them)
constexpr unsigned char kStub[] = {
    0xc5, 0xfa, 0x10, 0x48, 0x1c,   // vmovss xmm1,[rax+0x1c]          the divisor
    0xc5, 0xe8, 0x57, 0xd2,         // vxorps xmm2,xmm2,xmm2
    0xc5, 0xf8, 0x2e, 0xca,         // vucomiss xmm1,xmm2
    0x75, 0x02,                     // jne divide
    0x7b, 0x20,                     // jnp one (exactly zero)
    0xc5, 0xfa, 0x10, 0x40, 0x28,   // divide: vmovss xmm0,[rax+0x28]
    0xc5, 0xfa, 0x5e, 0xc1,         // vdivss xmm0,xmm0,xmm1
    0xb8, 0x00, 0x00, 0x80, 0x3e,   // mov eax,0.25
    0xc5, 0xf9, 0x6e, 0xc8,         // vmovd xmm1,eax
    0xc5, 0xfa, 0x59, 0xc1,         // vmulss xmm0,xmm0,xmm1
    0xc5, 0xfa, 0x5c, 0xc8,         // vsubss xmm1,xmm0,xmm0           NaN when xmm0 is not finite
    0xc5, 0xf8, 0x2e, 0xc9,         // vucomiss xmm1,xmm1
    0x7b, 0x09,                     // jnp done
    0xb8, 0x00, 0x00, 0x80, 0x3f,   // one: mov eax,1.0
    0xc5, 0xf9, 0x6e, 0xc0,         // vmovd xmm0,eax
    0xff, 0x25, 0, 0, 0, 0};        // done: jmp [rip] (back, the address follows)

}  // namespace

bool install_npc_phase_guard(unsigned char *image, uint64_t size) {
    const uint64_t at = find_unique(image, size, kSite, sizeof(kSite));
    if (at == kNotFound) return false;
    unsigned char *stub = stub_page_near(image, size);
    if (!stub) return false;
    std::memcpy(stub, kStub, sizeof kStub);
    const uint64_t back = reinterpret_cast<uint64_t>(image + at + kBack);
    std::memcpy(stub + sizeof kStub, &back, 8);
    if (!finish_stub(stub) || !jump_to(image, at + kAt, kLength, stub)) return false;
    std::printf("Uncharted 3: NPC copies on a co-op guest start their animation at a finite phase (%#llx)\n",
                static_cast<unsigned long long>(at + kAt));
    std::fflush(stdout);
    return true;
}

}  // namespace u3fix
