// runtime_split.cpp: the second local player's pad devices (runtime_split.h, GitHub #31).
#include "runtime_split.h"

#include <cstdio>

namespace {

SDL_Gamepad *g_second = nullptr;
Uint64 g_last_read_ms = 0;
bool g_read_once = false;
constexpr Uint64 kActiveMs = 500;

}  // namespace

extern "C" int runtime_split_p2_source(int second_gamepad, int keyboard_on, int first_gamepad, int pad_index) {
    if (pad_index) return SPLIT_P2_NONE;   // one window per controller: no second player in this window
    if (second_gamepad) return SPLIT_P2_GAMEPAD;
    if (keyboard_on && first_gamepad) return SPLIT_P2_KEYS;
    return SPLIT_P2_NONE;
}

extern "C" int runtime_split_keys_taken(int p2_active, int p2_source) {
    return p2_active && p2_source == SPLIT_P2_KEYS;
}

extern "C" SDL_Gamepad *runtime_split_gamepad2(SDL_Gamepad *first, int pad_index) {
    if (g_second && !SDL_GamepadConnected(g_second)) {
        SDL_CloseGamepad(g_second);
        g_second = nullptr;
    }
    // player 1 took this controller (its own one went away): look for another
    if (g_second && first && SDL_GetGamepadID(g_second) == SDL_GetGamepadID(first)) {
        SDL_CloseGamepad(g_second);
        g_second = nullptr;
    }
    if (g_second || pad_index || !SDL_WasInit(SDL_INIT_GAMEPAD)) return g_second;
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    const SDL_JoystickID taken = first ? SDL_GetGamepadID(first) : 0;
    for (int i = 0; ids && i < count && !g_second; i++) {
        if (ids[i] == taken) continue;
        g_second = SDL_OpenGamepad(ids[i]);
        if (g_second) {
            std::printf("Runtime: second player's controller: %s\n", SDL_GetGamepadName(g_second));
            std::fflush(stdout);
        }
    }
    SDL_free(ids);
    return g_second;
}

extern "C" void runtime_split_p2_read(void) {
    if (!g_read_once) {
        g_read_once = true;
        std::printf("Runtime: splitscreen: the second player's pad is read (handle %d)\n", RUNTIME_PAD2_HANDLE);
        std::fflush(stdout);
    }
    g_last_read_ms = SDL_GetTicks() + 1;   // 0: never read (SDL's clock starts at 0)
}

extern "C" int runtime_split_p2_active(void) {
    return g_last_read_ms && SDL_GetTicks() + 1 - g_last_read_ms < kActiveMs;
}
