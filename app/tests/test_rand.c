#include "common.h"

#include <assert.h>
#include <string.h>

#include "util/rand.h"

static void test_rand_secure(void) {
    uint8_t a[32];
    uint8_t b[32];
    memset(a, 0, sizeof(a));
    memset(b, 0, sizeof(b));
    assert(sc_rand_secure(a, sizeof(a)));
    assert(sc_rand_secure(b, sizeof(b)));

    uint8_t zero[32] = {0};
    assert(memcmp(a, zero, sizeof(a))); // probability 2^-256
    assert(memcmp(a, b, sizeof(a)));

    // a length which is not a multiple of 4 (rand_s() on Windows)
    uint8_t c[7];
    assert(sc_rand_secure(c, sizeof(c)));
}

int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    test_rand_secure();
    return 0;
}
