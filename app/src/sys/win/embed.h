#ifndef SC_EMBED_H
#define SC_EMBED_H

#include "common.h"

#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL_video.h>

// Make the window a child of the given parent window (--window-parent)
bool
sc_win_embed_window(SDL_Window *window, uint64_t parent);

#endif
