#include "gow3_mouse.h"
#include "gow3_settings.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace Gow3Mouse {
namespace {

std::mutex mutex;
float acc_x = 0, acc_y = 0;
float look_x = 0, look_y = 0;
std::chrono::steady_clock::time_point look_taken{};
unsigned look_calls = 0; ///< camera hook calls (gow3gpu_mouse_look_calls)
bool look_paused = false; ///< the pad gives the mouse to the stick: hooks get no movement
bool injected = false;
unsigned held = 0; // bits 0-4: SDL buttons 1-5
std::chrono::steady_clock::time_point wheel_until[2];
bool captured = false;
bool pad_in_use = false; ///< the controller was used last: the mouse stays free (window thread)

constexpr unsigned WheelUp = 1u << 5, WheelDown = 1u << 6;

} // namespace

// The settings' mouse switch (menu Controls tab, launcher; GOW3_MOUSE=0 overrides it at start).
bool Enabled() {
    return Gow3Settings::Get().mouse;
}

void Update(SDL_Window* window, bool may_capture) {
    const bool focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    const bool want = Enabled() && may_capture && focused && !pad_in_use;
    if (want == captured) {
        return;
    }
    captured = want;
    if (want) {
        // gow3 24 Oct 2026: raw, linear counts. SDL3 relative mode reads raw input and leaves the
        // system acceleration curve off by default; the hints and a user's SDL_* environment
        // variables could switch a curve or a scale on, so they are pinned here (override
        // priority beats the environment); nothing sets a motion transform. The counts then only
        // get the sensitivity: no acceleration or smoothing anywhere on the mouse-look path.
        SDL_SetHintWithPriority(SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, "0", SDL_HINT_OVERRIDE);
        SDL_SetHintWithPriority(SDL_HINT_MOUSE_RELATIVE_SPEED_SCALE, "1", SDL_HINT_OVERRIDE);
        SDL_SetHintWithPriority(SDL_HINT_MOUSE_RELATIVE_WARP_MOTION, "0", SDL_HINT_OVERRIDE);
    }
    SDL_SetWindowRelativeMouseMode(window, want);
    std::scoped_lock lock{mutex};
    acc_x = acc_y = 0;
    look_x = look_y = 0;
    held = 0; // no button stays pressed across a release (Alt+Tab with the trigger held)
}

bool OnEvent(const SDL_Event& event) {
    // gow3 21 Nov 2026 (owner: with the controller, the mouse must be free for other windows and
    // screens): using the controller (a button, or a stick or trigger past half way) lets the
    // mouse go; a click in the game window or the window getting the focus back (a click on it,
    // Alt+Tab) takes it for mouse look again. Only noted here: the events go on as before.
    switch (event.type) {
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        pad_in_use = true;
        return false;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (event.gaxis.value > 16384 || event.gaxis.value < -16384) {
            pad_in_use = true;
        }
        return false;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        pad_in_use = false;
        return false;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (!captured) {
            pad_in_use = false;
        }
        break;
    default:
        break;
    }
    if (!captured) {
        return false;
    }
    std::scoped_lock lock{mutex};
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
        acc_x += event.motion.xrel;
        acc_y += event.motion.yrel;
        look_x += event.motion.xrel;
        look_y += event.motion.yrel;
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.button >= 1 && event.button.button <= 5) {
            const unsigned bit = 1u << (event.button.button - 1);
            held = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? held | bit : held & ~bit;
        }
        return true;
    case SDL_EVENT_MOUSE_WHEEL:
        if (event.wheel.y != 0) {
            wheel_until[event.wheel.y > 0 ? 0 : 1] =
                std::chrono::steady_clock::now() + std::chrono::milliseconds{100};
        }
        return true;
    default:
        return false;
    }
}

} // namespace Gow3Mouse

extern "C" int gow3gpu_mouse_take_look(float* dx, float* dy) {
    using namespace Gow3Mouse;
    std::scoped_lock lock{mutex};
    const auto now = std::chrono::steady_clock::now();
    const bool fresh = now - look_taken < std::chrono::milliseconds{250};
    look_taken = now;
    ++look_calls;
    if (look_paused) {  // the mouse is the stick now: drop what the hook would turn
        look_x = look_y = 0;
        *dx = *dy = 0.0f;
        return 0;
    }
    *dx = fresh ? look_x : 0.0f;
    *dy = fresh ? look_y : 0.0f;
    look_x = look_y = 0;
    return captured || injected ? 1 : 0;
}

extern "C" void gow3gpu_mouse_inject(float dx, float dy) {
    using namespace Gow3Mouse;
    std::scoped_lock lock{mutex};
    look_x += dx;
    look_y += dy;
    acc_x += dx;
    acc_y += dy;
    injected = true;
}

extern "C" unsigned gow3gpu_mouse_look_calls(void) {
    using namespace Gow3Mouse;
    std::scoped_lock lock{mutex};
    return look_calls;
}

extern "C" void gow3gpu_mouse_look_pause(int paused) {
    using namespace Gow3Mouse;
    std::scoped_lock lock{mutex};
    look_paused = paused != 0;
}

extern "C" int gow3gpu_mouse_look_recent(void) {
    using namespace Gow3Mouse;
    std::scoped_lock lock{mutex};
    return std::chrono::steady_clock::now() - look_taken < std::chrono::milliseconds{200} ? 1 : 0;
}

extern "C" int gow3gpu_mouse_take(float* dx, float* dy, unsigned* buttons) {
    using namespace Gow3Mouse;
    std::scoped_lock lock{mutex};
    *dx = acc_x;
    *dy = acc_y;
    acc_x = acc_y = 0;
    if (injected) {  // tests (pad file mx= my=): the stick path too
        *buttons = held;
        return 1;
    }
    const auto now = std::chrono::steady_clock::now();
    *buttons = held | (now < wheel_until[0] ? WheelUp : 0) | (now < wheel_until[1] ? WheelDown : 0);
    return captured ? 1 : 0;
}
