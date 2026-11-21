/* sid_names: names for the name ids the retail games can no longer turn into text (sid_names.cpp). */
#ifndef GOW3_SID_NAMES_H
#define GOW3_SID_NAMES_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* image: the guest image base (still writable); size: its size. Hooks the Uncharted 3, 2 or 1 1.02 name
 * lookup, found by its bytes; nothing in other images. Returns 1 when installed. */
int sid_names_install(unsigned char *image, uint64_t size);

#ifdef __cplusplus
}
#endif
#endif
