#include "entropy.h"

static int cpu_has_rdrand(void) {
    uint32_t ecx = 0;
    __asm__ volatile("cpuid" : "=c"(ecx) : "a"(1) : "ebx", "edx");
    return (ecx >> 30) & 1;
}

/* Simple LCG fallback used when RDRAND is unavailable (e.g. QEMU without +rdrand). */
static uint64_t lcg_state = 0xdeadbeefcafe1234ULL;
static uint64_t lcg_next(void) {
    lcg_state = lcg_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return lcg_state;
}

uint64_t rdrand64(void) {
    if (!cpu_has_rdrand()) return lcg_next();
    uint64_t v = 0;
    int ok = 0;
    for (int i = 0; i < 10 && !ok; i++) {
        __asm__ volatile(
            "rdrand %0\n\t"
            "setc   %b1"
            : "=r"(v), "=r"(ok)
            :
            : "cc"
        );
    }
    return ok ? v : lcg_next();
}

int mbedtls_hardware_poll(void* data, unsigned char* output,
                          size_t len, size_t* olen) {
    (void)data;
    size_t i = 0;
    while (i < len) {
        uint64_t r = rdrand64();
        size_t chunk = len - i;
        if (chunk > 8) chunk = 8;
        unsigned char* rp = (unsigned char*)&r;
        for (size_t j = 0; j < chunk; j++) output[i + j] = rp[j];
        i += chunk;
    }
    *olen = len;
    return 0;
}
