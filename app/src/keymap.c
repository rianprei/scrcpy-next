#include "keymap.h"

#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL_keyboard.h>

#include "util/log.h"

static char *
trim(char *s) {
    while (*s == ' ' || *s == '\t') {
        ++s;
    }
    size_t len = strlen(s);
    while (len && strchr(" \t\r\n", s[len - 1])) {
        s[--len] = '\0';
    }
    return s;
}

// Split the last whitespace-separated token from s
static char *
pop_token(char *s) {
    char *sep = strrchr(s, ' ');
    char *tab = strrchr(s, '\t');
    if (!sep || (tab && tab > sep)) {
        sep = tab;
    }
    if (!sep) {
        return NULL;
    }
    *sep = '\0';
    trim(s);
    return sep + 1;
}

static bool
parse_coord(const char *s, float *out) {
    char *end;
    float value = strtof(s, &end);
    if (end == s || *end != '\0' || !(value >= 0 && value <= 1)) {
        return false;
    }
    *out = value;
    return true;
}

bool
sc_keymap_parse(struct sc_keymap *km, FILE *file) {
    km->count = 0;

    char line[256];
    unsigned line_number = 0;
    while (fgets(line, sizeof(line), file)) {
        ++line_number;
        char *s = trim(line);
        if (!*s || *s == '#') {
            continue;
        }

        char *ys = pop_token(s);
        char *xs = ys ? pop_token(s) : NULL;
        struct sc_keymap_entry entry;
        if (!xs || !parse_coord(xs, &entry.x) || !parse_coord(ys, &entry.y)) {
            LOGE("Keymap line %u: expected \"<key> <x> <y>\" with x and y in "
                 "[0; 1]", line_number);
            return false;
        }

        entry.key = SDL_GetKeyFromName(s);
        if (entry.key == SDLK_UNKNOWN) {
            LOGE("Keymap line %u: unknown key \"%s\"", line_number, s);
            return false;
        }

        if (sc_keymap_find(km, entry.key) != -1) {
            LOGE("Keymap line %u: key \"%s\" already mapped", line_number, s);
            return false;
        }

        if (km->count == SC_KEYMAP_MAX_ENTRIES) {
            LOGE("Keymap line %u: too many keys (max %d)", line_number,
                 SC_KEYMAP_MAX_ENTRIES);
            return false;
        }

        km->entries[km->count++] = entry;
    }

    return true;
}

bool
sc_keymap_load(struct sc_keymap *km, const char *path) {
    // shortcut: plain fopen(), so non-ASCII paths fail on Windows; use
    // _wfopen() like util/config_file.c if needed
    FILE *file = fopen(path, "r");
    if (!file) {
        LOGE("Could not open keymap file: %s", path);
        return false;
    }

    bool ok = sc_keymap_parse(km, file);
    fclose(file);
    if (ok) {
        LOGI("Keymap: %u key(s) loaded from %s", km->count, path);
    }
    return ok;
}

int
sc_keymap_find(const struct sc_keymap *km, SDL_Keycode key) {
    for (unsigned i = 0; i < km->count; ++i) {
        if (km->entries[i].key == key) {
            return (int) i;
        }
    }
    return -1;
}
