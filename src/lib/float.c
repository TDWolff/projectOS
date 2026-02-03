#include "float.h"

void enable_fpu() {
    uint64_t cr0;
    uint64_t cr4;

    // 1. Get CR0
    __asm__ volatile ("mov %%cr0, %0" : "=r" (cr0));

    // 2. Modify CR0
    // Clear EM (Bit 2) - Emulation (We have hardware FPU)
    cr0 &= ~(1 << 2);
    // Set MP (Bit 1) - Monitor Coprocessor (For task switching sync)
    cr0 |= (1 << 1);

    // 3. Write CR0
    __asm__ volatile ("mov %0, %%cr0" :: "r" (cr0));

    // 4. Get CR4
    __asm__ volatile ("mov %%cr4, %0" : "=r" (cr4));

    // 5. Modify CR4
    // Set OSFXSR (Bit 9) - Operating System Support for FXSAVE and FXRSTOR instructions
    cr4 |= (1 << 9);
    // Set OSXMMEXCPT (Bit 10) - Operating System Support for Unmasked SIMD Floating-Point Exceptions
    cr4 |= (1 << 10);

    // 6. Write CR4
    __asm__ volatile ("mov %0, %%cr4" :: "r" (cr4));

    // 7. Initialize FPU
    __asm__ volatile ("fninit");
}

// --------------------------------------------------------------------------
// Basic Floating Point Math Library
// Since we don't have libm, we implement wrappers for FPU/SSE instructions.
// --------------------------------------------------------------------------

float float_abs(float x) {
    // Clear the sign bit (IEEE 754: Bit 31)
    // We can do this with integer manipulation or just `return x < 0 ? -x : x;`
    // The compiler will optimize the ternary operator to a bitwise clear usually.
    return x < 0.0f ? -x : x;
}

int float_floor(float x) {
    return (int)x - (x < (int)x);
}

int float_ceil(float x) {
    return (int)x + (x > (int)x);
}

// Square Root using SSE instruction
float float_sqrt(float x) {
    float res;
    __asm__ volatile ("sqrtss %1, %0" : "=x" (res) : "x" (x));
    return res;
}

// Sine using x87 FPU (Legacy but simple)
// Note: SSE doesn't have a SIN instruction, so we must use the old x87 unit.
float float_sin(float x) {
    float res;
    __asm__ volatile (
        "flds %1;"     // Load X onto FPU stack (Single Precision)
        "fsin;"       // Compute Sine
        "fstps %0;"    // Store result (Single Precision)
        : "=m" (res)  // Output in memory
        : "m" (x)     // Input from memory
    );
    return res;
}

// Cosine using x87 FPU
float float_cos(float x) {
    float res;
    __asm__ volatile (
        "flds %1;"     // Load Single
        "fcos;"
        "fstps %0;"    // Store Single
        : "=m" (res)
        : "m" (x)
    );
    return res;
}

// Power function (Basic implementation for integer exponents)
float float_pow(float base, int exp) {
    float res = 1.0f;
    for (int i = 0; i < exp; i++) {
        res *= base;
    }
    return res;
}

#define PI 3.14159265359f