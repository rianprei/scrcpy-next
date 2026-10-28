#ifndef SC_DARKMODE_H
#define SC_DARKMODE_H

#include "common.h"

#include <SDL3/SDL_video.h>

// Use a dark title bar if the Windows apps theme is dark
void
sc_darkmode_apply_to_window(SDL_Window *window);

#endif
