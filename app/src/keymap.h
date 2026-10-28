#ifndef SC_KEYMAP_H
#define SC_KEYMAP_H

#include "common.h"

#include <stdbool.h>
#include <stdio.h>
#include <SDL3/SDL_keycode.h>

// Keys mapped to touches on the device screen (--keymap)
//
// File format, one mapping per line:
//
//     <SDL key name> <x> <y>
//
// where x and y are in [0; 1], relative to the device screen (0 0 is the
// top-left corner). Empty lines and lines starting with '#' are ignored.

#define SC_KEYMAP_MAX_ENTRIES 32

struct sc_keymap_entry {
    SDL_Keycode key;
    float x;
    float y;
};

struct sc_keymap {
    struct sc_keymap_entry entries[SC_KEYMAP_MAX_ENTRIES];
    unsigned count;
};

bool
sc_keymap_parse(struct sc_keymap *km, FILE *file);

bool
sc_keymap_load(struct sc_keymap *km, const char *path);

// Return the index of the entry for this key, or -1
int
sc_keymap_find(const struct sc_keymap *km, SDL_Keycode key);

#endif
