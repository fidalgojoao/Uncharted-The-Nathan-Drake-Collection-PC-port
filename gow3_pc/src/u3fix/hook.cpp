// u3fix hook helpers (see u3fix_hook.h) and the entry point the C runtime calls.
#include "u3fix.h"
#include "u3fix_hook.h"

#include <climits>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace u3fix {

uint64_t find_unique(const unsigned char *image, uint64_t size, const unsigned char *bytes, uint64_t length) {
    if (!image || !length || size < length) return kNotFound;
    uint64_t found = kNotFound;
    for (uint64_t at = 0; at + length <= size; at++) {
        if (image[at] != bytes[0] || std::memcmp(image + at, bytes, length) != 0) continue;
        if (found != kNotFound) return kNotFound;   // more than one: not the build this was written for
        found = at;
    }
    return found;
}

unsigned char *stub_page_near(const unsigned char *image, uint64_t size) {
#ifdef _WIN32
    // Windows reserves memory in 64 KiB blocks: one block holds 16 stub pages, handed out in turn, so the fixes do not
    // use up the free space within jump range of the image (each page is made executable on its own by finish_stub)
    static unsigned char *pool = nullptr;
    static const unsigned char *pool_image = nullptr;
    static unsigned pool_used = 16;
    if (pool && pool_image == image && pool_used < 16) return pool + 0x1000 * pool_used++;
    // free regions within +-1.5 GiB of the image (below it too: modules and runtime reservations fill the space above)
    const uintptr_t img = reinterpret_cast<uintptr_t>(image);
    const uintptr_t lo = img > 0x60000000u ? img - 0x60000000u : 0x10000;
    const uintptr_t hi = img + static_cast<uintptr_t>(size) + 0x60000000u;
    MEMORY_BASIC_INFORMATION info;
    for (uintptr_t a = lo; a < hi && VirtualQuery(reinterpret_cast<void *>(a), &info, sizeof info);
         a = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize) {
        if (info.State != MEM_FREE) continue;
        const uintptr_t start = (reinterpret_cast<uintptr_t>(info.BaseAddress) + 0xffff) & ~uintptr_t{0xffff};
        if (start + 0x10000 > reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize) continue;
        void *p = VirtualAlloc(reinterpret_cast<void *>(start), 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!p) continue;
        pool = static_cast<unsigned char *>(p);
        pool_image = image;
        pool_used = 1;
        return pool;
    }
    return nullptr;
#else
    (void)size;
    void *p = mmap(nullptr, 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    (void)image;
    return p == MAP_FAILED ? nullptr : static_cast<unsigned char *>(p);
#endif
}

bool finish_stub(unsigned char *stub) {
#ifdef _WIN32
    DWORD old = 0;
    return VirtualProtect(stub, 0x1000, PAGE_EXECUTE_READ, &old) != 0;
#else
    return mprotect(stub, 0x1000, PROT_READ | PROT_EXEC) == 0;
#endif
}

bool jump_to(unsigned char *image, uint64_t at, unsigned length, const unsigned char *stub) {
    if (length < 5) return false;
    const int64_t delta = static_cast<int64_t>(reinterpret_cast<uintptr_t>(stub)) -
                          static_cast<int64_t>(reinterpret_cast<uintptr_t>(image + at + 5));
    if (delta < INT32_MIN || delta > INT32_MAX) return false;
    const int32_t rel = static_cast<int32_t>(delta);
    image[at] = 0xe9;
    std::memcpy(image + at + 1, &rel, 4);
    std::memset(image + at + 5, 0x90, length - 5);
    return true;
}

}  // namespace u3fix

extern "C" int u3fix_install(unsigned char *image, uint64_t size) {
    if (!image || !size) return 0;
    int applied = 0;
    applied += u3fix::raise_net_process_pool(image, size) ? 1 : 0;
    applied += u3fix::install_package_log(image, size) ? 1 : 0;
    applied += u3fix::install_counter_wait_log(image, size) ? 1 : 0;
    applied += u3fix::install_stuck_counter(image, size) ? 1 : 0;
    applied += u3fix::install_job_queue_full_log(image, size) ? 1 : 0;
    applied += u3fix::install_audio_mix_direct(image, size) ? 1 : 0;
    applied += u3fix::install_cmdbuf_borrow(image, size) ? 1 : 0;
    applied += u3fix::install_script_errors_log(image, size) ? 1 : 0;
    applied += u3fix::install_sid_names(image, size) ? 1 : 0;
    applied += u3fix::install_retarget_log(image, size) ? 1 : 0;
    applied += u3fix::install_retarget_mode(image, size) ? 1 : 0;
    applied += u3fix::install_npc_phase_guard(image, size) ? 1 : 0;
    applied += u3fix::install_improved_tonemap_curve(image, size) ? 1 : 0;
    applied += u3fix::exposure_filter_at_least_one(image, size) ? 1 : 0;
    applied += u3fix::install_hudnet_text_canvas(image, size) ? 1 : 0;
    return applied;
}
