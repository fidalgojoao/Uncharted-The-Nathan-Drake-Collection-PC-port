/* runtime_split.h: the second local player's pad (Uncharted 3 splitscreen, GitHub #31), C++ (runtime_split.cpp).
 *
 * The PS4 game opens one pad, for its initial user. The second player's pad is a runtime extension: handle
 * RUNTIME_PAD2_HANDLE, read by the u3fix hook src/u3fix/split_pad.cpp (the game's pad port 1) and asked for by the
 * helper's Splitscreen row (is a second pad there?). Which devices each player uses:
 *   - a second controller (any connected one that is not player 1's) is player 2's pad;
 *   - else, with player 1 on a controller, the keyboard and mouse are player 2's pad;
 *   - while player 2's pad is being read (splitscreen on), the keyboard and mouse no longer drive player 1 when they
 *     are player 2's. Without splitscreen nothing changes: every game reads only handle 1.
 * GOW3_PAD_INDEX (one game window per controller) gives no second player. */
#ifndef RUNTIME_SPLIT_H
#define RUNTIME_SPLIT_H
#include <SDL3/SDL.h>

#define RUNTIME_PAD2_HANDLE 2

#ifdef __cplusplus
extern "C" {
#endif

enum { SPLIT_P2_NONE, SPLIT_P2_GAMEPAD, SPLIT_P2_KEYS };
/* Player 2's device: a second controller, else the keyboard and mouse when player 1 has a controller, else none. */
int runtime_split_p2_source(int second_gamepad, int keyboard_on, int first_gamepad, int pad_index);
/* 1 when the keyboard and mouse leave player 1: player 2 is being read and they are player 2's. */
int runtime_split_keys_taken(int p2_active, int p2_source);

/* State, called under the pad lock (runtime_pad.c). */
/* Player 2's controller: an open connected controller other than player 1's, NULL when none (or pad_index set). */
SDL_Gamepad *runtime_split_gamepad2(SDL_Gamepad *first, int pad_index);
/* Player 2's pad was read now / was read in the last half second (splitscreen is on). */
void runtime_split_p2_read(void);
int runtime_split_p2_active(void);

#ifdef __cplusplus
}
#endif
#endif
