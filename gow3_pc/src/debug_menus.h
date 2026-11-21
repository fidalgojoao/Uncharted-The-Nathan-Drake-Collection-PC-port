/* debug_menus: the Dev Menu and Quick Menu combos of the games' debug mode on a PC controller (debug_menus.cpp). */
#ifndef GOW3_DEBUG_MENUS_H
#define GOW3_DEBUG_MENUS_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* image: the guest image base (still writable); size: its size. Does nothing unless the launcher's debug mode is on
 * (UNCHARTED_CHEATS lists "debug") and the image is the Uncharted 2 or 3 1.02 build. Returns 1 when installed. */
int debug_menus_install(unsigned char *image, uint64_t size);

#ifdef __cplusplus
}
#endif
#endif
