// Uncharted 3 1.02, 12 Nov 2026: the network-process memory pool is too small for multiplayer.
//
// The game creates its memory pools from a table in its data (image 0x16f8d90, read by the memory setup at 0x5ac2f1):
// 32-byte entries {u32 pool id, u32 type, u64 size, u64 alignment, u64 0}. Pool 0x14 (network) has 16 MiB, pool 0x15
// (network process, a "Wrapped Malloc" type-5 pool) only 343,040 bytes: the PS4 game has no multiplayer. With the
// restored multiplayer, players joining and leaving and round after round fill it: "Allocation failed: 576 bytes ...
// Wrapped Malloc [F:336 T:343040]", then a write through the null result (0x6ab7fb / 0x6ae308), and the game closes
// (four logs of 2026-11-12: a co-op host after two rounds, a host after players left, and a host that kept failing
// 279 times after a late joiner left). The pool's budget is raised to 2 MiB before the game starts.
// 21 Nov 2026: not 16 MiB. The pool is part of the game's fixed CPU memory budget ("Context(kAllocAppCpu) Size Does Not
// Add Up!"): taken out of the CPU heap at start, so 16 MiB left the heap about 16 MB short and a start-up allocation of
// 8 MiB (FgData) failed, the game did not start (0.10.119). 2 MiB is six times the old size and leaves the heap about
// 6 MB of room at that point.
#include "u3fix.h"
#include "u3fix_hook.h"

#include <cstdio>
#include <cstring>

namespace u3fix {

namespace {

struct PoolEntry {   // the game's pool table entry
    uint32_t id, type;
    uint64_t size, align, zero;
};
static_assert(sizeof(PoolEntry) == 32);

constexpr uint64_t kOldSize = 0x53c00;    // 343,040 bytes
constexpr uint64_t kNewSize = 0x200000;   // 2 MiB (16 MiB did not fit the game's CPU memory budget)

}  // namespace

bool raise_net_process_pool(unsigned char *image, uint64_t size) {
    // pool 0x14 (type 5, 16 MiB, align 0x80) followed by pool 0x15 (type 5, 343,040 bytes, align 0x80). Only the first
    // 24 bytes of each entry are compared: the last word is zero in the file but set when the image's relocations are
    // applied, which happens before this runs (21 Nov 2026: compared with it, the pool was never found and never raised,
    // and four-player matches ran out of it again)
    auto is = [&](uint64_t at, uint32_t id, uint64_t bytes) {
        PoolEntry e;
        std::memcpy(&e, image + at, sizeof(e));
        return e.id == id && e.type == 5 && e.size == bytes && e.align == 0x80;
    };
    for (uint64_t at = 0; at + 64 <= size; at += 8) {
        if (!is(at, 0x14, 0x1000000) || !is(at + 32, 0x15, kOldSize)) continue;
        PoolEntry e;
        std::memcpy(&e, image + at + 32, sizeof(e));
        e.size = kNewSize;
        std::memcpy(image + at + 32, &e, sizeof(e));
        std::printf("Uncharted 3: network-process memory pool (0x15) raised from %llu bytes to %llu (pool table at %#llx)\n",
                    static_cast<unsigned long long>(kOldSize), static_cast<unsigned long long>(kNewSize),
                    static_cast<unsigned long long>(at));
        std::fflush(stdout);
        return true;
    }
    return false;
}

}  // namespace u3fix
