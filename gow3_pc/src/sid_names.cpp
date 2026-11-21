// Uncharted 2 and 3 1.02: names for name ids where the retail games answer "(no debug memory)".
//
// Names in the game data are 32-bit ids; the function that turns an id back into text (Uncharted 3 image 0xf38f40,
// Uncharted 2 0xce33a0) has no name table in the retail games and answers "(no debug memory)". The debug menus show
// that answer as item names (the Give Player Weapon list, issue 70), and the script messages printed by
// script_errors_log.cpp say which kind of thing was not found, but not which one. The function now runs as before;
// only when its answer is that text does it give the name read from the player's own game files instead
// (name_dictionary.cpp: the data files in u3data and u2data build/main/dc1/ps4 under /app0, the game's own folder
// first, read once on a runtime thread when the game starts), or "#" and the id in hex when those files do not spell
// the name out (from a small ring of buffers per thread). Found by their bytes.
#include "sid_names.h"

#include "name_dictionary.h"
#include "u3fix/u3fix_hook.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <string>
#include <thread>

extern "C" int runtime_file_translate(const char *guest, char *out, size_t size);

namespace {

const char *own_data = "u3data";   // the running game's data folder, read first: its names win a shared id

const name_ids::Dictionary &names() {
    static name_ids::Dictionary d;
    static std::once_flag once;
    std::call_once(once, [] {
        char root[1024];
        if (runtime_file_translate("/app0", root, sizeof root) != 0) return;
        size_t files = 0;
        for (const char *data : {own_data, std::strcmp(own_data, "u3data") ? "u3data" : "u2data"})
            files += d.add_folder(std::string(root) + "/" + data + "/build/main/dc1/ps4");
        std::printf("Runtime: %zu names read from %zu game data files for ids without debug memory\n", d.size(), files);
        std::fflush(stdout);
    });
    return d;
}

__attribute__((sysv_abi)) const char *name_or_id(uint32_t id, const char *name) {
    if (!name || std::strcmp(name, "(no debug memory)") != 0) return name;
    if (const char *known = names().find(id)) return known;
    thread_local char ring[8][16];
    thread_local unsigned next = 0;
    char *out = ring[next++ & 7];
    std::snprintf(out, 16, "#%08x", id);
    return out;
}

// push rbp; mov rbp,rsp; push r15; push r14; push rbx; sub rsp,0x18; mov r14d,esi; mov ebx,edi; lea r15,[canary]
constexpr unsigned char kSite3[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x53, 0x48, 0x83, 0xec,
                                    0x18, 0x41, 0x89, 0xf6, 0x89, 0xfb, 0x4c, 0x8d, 0x3d, 0xcf, 0xe1, 0x7a};
// the same with mov r15,[canary address]
constexpr unsigned char kSite2[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x53, 0x48, 0x83, 0xec,
                                    0x18, 0x41, 0x89, 0xf6, 0x89, 0xfb, 0x4c, 0x8b, 0x3d, 0x47, 0x78, 0x36};
constexpr unsigned kPrologue = 6;   // push rbp; mov rbp,rsp; push r15

bool install(unsigned char *image, uint64_t size, const unsigned char *site, size_t site_length, const char *game) {
    using namespace u3fix;
    const uint64_t at = find_unique(image, size, site, site_length);
    if (at == kNotFound) return false;
    unsigned char *stub = stub_page_near(image, size);
    if (!stub) return false;
    unsigned n = 0;
    auto put = [&](std::initializer_list<unsigned char> b) { for (unsigned char ch : b) stub[n++] = ch; };
    auto abs_jump = [&](uint64_t to) { put({0xff, 0x25, 0, 0, 0, 0}); std::memcpy(stub + n, &to, 8); n += 8; };
    // the entry: keep the id, run the original function (its prologue here, then on in the image), fix its answer
    put({0x57, 0x56, 0x48, 0x83, 0xec, 0x08});   // push rdi; push rsi; sub rsp,8 (aligned for the call)
    put({0xe8, 0, 0, 0, 0});                     // call trampoline (rel32 filled in below)
    const unsigned call_end = n;
    put({0x48, 0x83, 0xc4, 0x08, 0x5e, 0x5f});   // add rsp,8; pop rsi; pop rdi
    put({0x48, 0x83, 0xec, 0x08});               // sub rsp,8
    put({0x48, 0x89, 0xc6});                     // mov rsi,rax (the answer); rdi = the id
    put({0x48, 0xb8});                           // mov rax, name_or_id
    const uint64_t fixer = reinterpret_cast<uint64_t>(&name_or_id);
    std::memcpy(stub + n, &fixer, 8);
    n += 8;
    put({0xff, 0xd0, 0x48, 0x83, 0xc4, 0x08, 0xc3});   // call rax; add rsp,8; ret
    const unsigned trampoline = n;
    std::memcpy(stub + n, image + at, kPrologue);      // the original prologue
    n += kPrologue;
    abs_jump(reinterpret_cast<uint64_t>(image + at + kPrologue));
    const int32_t rel = static_cast<int32_t>(trampoline - call_end);
    std::memcpy(stub + call_end - 4, &rel, 4);
    if (!finish_stub(stub) || !jump_to(image, at, kPrologue, stub)) return false;
    std::printf("%s: names without debug memory come from the game's data files, else their ids (%#llx)\n", game,
                static_cast<unsigned long long>(at));
    std::fflush(stdout);
    return true;
}

}  // namespace

extern "C" int sid_names_install(unsigned char *image, uint64_t size) {
    if (!image || !size) return 0;
    bool done = install(image, size, kSite3, sizeof kSite3, "Uncharted 3");
    if (!done) {
        own_data = "u2data";
        done = install(image, size, kSite2, sizeof kSite2, "Uncharted 2");
    }
    if (!done) return 0;
    // the names are read on a thread of the runtime while the game starts; a game thread that asks earlier waits
    std::thread([] { names(); }).detach();
    return 1;
}
