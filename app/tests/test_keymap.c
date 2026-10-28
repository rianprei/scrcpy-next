#include "common.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "keymap.h"

static FILE *
file_with(const char *text) {
    FILE *file = tmpfile();
    assert(file);
    fputs(text, file);
    rewind(file);
    return file;
}

static bool
parse(struct sc_keymap *km, const char *text) {
    FILE *file = file_with(text);
    bool ok = sc_keymap_parse(km, file);
    fclose(file);
    return ok;
}

static void test_parse(void) {
    struct sc_keymap km;
    bool ok = parse(&km, "# comment\n"
                         "\n"
                         "Space 0.5 0.75\n"
                         "  W\t0 1  \r\n"
                         "Left Shift 0.25 0.125\n");
    assert(ok);
    assert(km.count == 3);
    assert(km.entries[0].key == SDLK_SPACE);
    assert(km.entries[0].x == 0.5f && km.entries[0].y == 0.75f);
    assert(km.entries[1].key == SDLK_W);
    assert(km.entries[1].x == 0 && km.entries[1].y == 1);
    assert(km.entries[2].key == SDLK_LSHIFT);
    assert(km.entries[2].x == 0.25f && km.entries[2].y == 0.125f);

    assert(sc_keymap_find(&km, SDLK_W) == 1);
    assert(sc_keymap_find(&km, SDLK_Q) == -1);
}

static void test_parse_errors(void) {
    struct sc_keymap km;
    assert(!parse(&km, "Space 0.5\n"));            // missing coordinate
    assert(!parse(&km, "Space 0.5 1.5\n"));        // out of range
    assert(!parse(&km, "Space 0.5 -0.1\n"));       // out of range
    assert(!parse(&km, "Space 0.5 nan\n"));        // not a number in range
    assert(!parse(&km, "Space 0.5 0.5x\n"));       // trailing garbage
    assert(!parse(&km, "NoSuchKey 0.5 0.5\n"));    // unknown key
    assert(!parse(&km, "0.5 0.5\n"));              // no key
    assert(!parse(&km, "W 0 0\nW 1 1\n"));         // duplicate

    // valid keys A-Z then 0-9, one more than the limit
    static_assert(SC_KEYMAP_MAX_ENTRIES < 36, "not enough distinct keys");
    char text[37 * 8] = "";
    for (int i = 0; i < SC_KEYMAP_MAX_ENTRIES; ++i) {
        char line[8];
        char c = i < 26 ? 'A' + i : '0' + (i - 26);
        snprintf(line, sizeof(line), "%c 0 0\n", c);
        strcat(text, line);
    }
    assert(parse(&km, text) && km.count == SC_KEYMAP_MAX_ENTRIES);
    strcat(text, "Space 0 0\n");
    assert(!parse(&km, text));                     // too many keys
}

int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    test_parse();
    test_parse_errors();

    return 0;
}
