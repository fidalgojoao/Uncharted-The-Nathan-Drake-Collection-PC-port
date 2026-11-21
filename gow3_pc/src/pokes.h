/* pokes: values the runtime keeps set in guest memory for the whole session (GOW3_POKE, Windows build).
 *
 * The launcher's cheats, reward unlocks, extra-quality flags and debug mode are switches the games keep in their
 * data; the launcher passes their image offsets in GOW3_POKE and the runtime holds them (pokes.cpp). */
#ifndef GOW3_POKES_H
#define GOW3_POKES_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reads GOW3_POKE and, when it names any value, starts the thread that holds them. image: the guest image base. */
void pokes_start(unsigned char *image);

#ifdef __cplusplus
}

#include <vector>

namespace pokes {

struct Poke {
    bool deref = false;     // offset names a pointer; the value lives add bytes past where it points
    uint64_t offset = 0;    // image offset
    uint64_t add = 0;
    uint32_t value = 1;
    unsigned width = 1;     // 1 or 4 bytes
};

// The entries of a GOW3_POKE text, in order (see pokes.cpp for the format).
std::vector<Poke> parse(const char *text);

}  // namespace pokes
#endif
#endif
