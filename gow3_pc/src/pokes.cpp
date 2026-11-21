// GOW3_POKE=<entry>[,<entry>...]: values the runtime keeps set in guest memory, rewritten every 250 ms.
//
// An entry is <offset>[=<value>] (hex image offset; the value is 1 when it is left out) or *<offset>+<add>[=<value>]
// (the value lives <add> bytes past the pointer stored at <offset>). A value of one or two hex digits is one byte; a
// longer one (the launcher always writes eight digits) is a 32-bit little-endian word, for example the bits of a float
// such as a clock multiplier. Reads and
// writes go through Read/WriteProcessMemory, so an address that is not mapped (yet) is skipped, and a value is only
// written when it differs. The launcher builds the list (launcher/native/runtime_env.cpp).
#include "pokes.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <windows.h>

namespace pokes {

std::vector<Poke> parse(const char *text) {
    std::vector<Poke> list;
    const char *p = text ? text : "";
    while (*p) {
        Poke k;
        char *end = nullptr;
        if (*p == '*') {
            k.deref = true;
            ++p;
        }
        k.offset = std::strtoull(p, &end, 16);
        p = end;
        if (k.deref && *p == '+') {
            k.add = std::strtoull(p + 1, &end, 16);
            p = end;
        }
        if (*p == '=') {
            k.value = static_cast<uint32_t>(std::strtoul(p + 1, &end, 16));
            k.width = end - (p + 1) > 2 ? 4 : 1;
            if (k.width == 1) k.value &= 0xff;
            p = end;
        }
        list.push_back(k);
        while (*p && *p != ',') ++p;
        if (*p == ',') ++p;
    }
    return list;
}

namespace {

unsigned char *base;
std::vector<Poke> held;

DWORD WINAPI hold(LPVOID) {
    const HANDLE self = GetCurrentProcess();
    for (;;) {
        for (const Poke &k : held) {
            uintptr_t address = reinterpret_cast<uintptr_t>(base) + static_cast<uintptr_t>(k.offset);
            SIZE_T got = 0;
            if (k.deref) {
                uint64_t pointer = 0;
                if (!ReadProcessMemory(self, reinterpret_cast<LPCVOID>(address), &pointer, sizeof pointer, &got) ||
                    got != sizeof pointer || !pointer)
                    continue;
                address = static_cast<uintptr_t>(pointer) + static_cast<uintptr_t>(k.add);
            }
            uint32_t current = 0;
            if (ReadProcessMemory(self, reinterpret_cast<LPCVOID>(address), &current, k.width, &got) &&
                got == k.width && std::memcmp(&current, &k.value, k.width) != 0)
                WriteProcessMemory(self, reinterpret_cast<LPVOID>(address), &k.value, k.width, nullptr);
        }
        Sleep(250);
    }
    return 0;
}

}  // namespace

}  // namespace pokes

extern "C" void pokes_start(unsigned char *image) {
    const char *env = std::getenv("GOW3_POKE");
    if (!env || !*env || !image) return;
    pokes::base = image;
    pokes::held = pokes::parse(env);
    std::printf("Runtime: %zu memory switches kept set (GOW3_POKE: %s)\n", pokes::held.size(), env);
    std::fflush(stdout);
    const HANDLE thread = CreateThread(nullptr, 0, pokes::hold, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
}
