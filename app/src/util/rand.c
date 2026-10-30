#ifdef _WIN32
// for rand_s()
# define _CRT_RAND_S
#endif

#include "rand.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tick.h"
#include "util/log.h"

void sc_rand_init(struct sc_rand *rand) {
    sc_tick seed = sc_tick_now(); // microsecond precision
    rand->xsubi[0] = (seed >> 32) & 0xFFFF;
    rand->xsubi[1] = (seed >> 16) & 0xFFFF;
    rand->xsubi[2] = seed & 0xFFFF;
}

uint32_t sc_rand_u32(struct sc_rand *rand) {
    // jrand returns a value in range [-2^31, 2^31]
    // conversion from signed to unsigned is well-defined to wrap-around
    return jrand48(rand->xsubi);
}

uint64_t sc_rand_u64(struct sc_rand *rand) {
    uint32_t msb = sc_rand_u32(rand);
    uint32_t lsb = sc_rand_u32(rand);
    return ((uint64_t) msb << 32) | lsb;
}

bool sc_rand_secure(void *buf, size_t len) {
#ifdef _WIN32
    // rand_s() uses RtlGenRandom()
    unsigned char *p = buf;
    while (len) {
        unsigned int value;
        if (rand_s(&value)) {
            LOGE("Could not generate random bytes");
            return false;
        }
        size_t n = len < sizeof(value) ? len : sizeof(value);
        memcpy(p, &value, n);
        p += n;
        len -= n;
    }
    return true;
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) {
        LOGE("Could not open /dev/urandom");
        return false;
    }
    size_t r = fread(buf, 1, len, f);
    fclose(f);
    if (r != len) {
        LOGE("Could not read /dev/urandom");
        return false;
    }
    return true;
#endif
}
