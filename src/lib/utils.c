#include "utils.h"

int abs(int n) {
    return (n < 0) ? -n : n;
}

int sign(int n) {
    if (n > 0) return 1;
    if (n < 0) return -1;
    return 0;
}

int clamp(int n, int min, int max) {
    if (n < min) return min;
    if (n > max) return max;
    return n;
}

int lerp(int a, int b, int t, int total) {
    if (total == 0) return b;
    return a + ((b - a) * t) / total;
}

// Integer square root using binary search
uint32_t isqrt(uint32_t n) {
    if (n < 2) return n;

    uint32_t start = 1;
    uint32_t end = n / 2; // Optimization ?
    uint32_t ans = 0;
    
    // Better: Standard integer sqrt algorithm (e.g. shifts) is faster, 
    // but binary search is safe and easy to implement.
    // OR we can use the Newton method on ints.
    
    // Let's use a simple binary search for now.
    while (start <= end) {
        uint32_t mid = (start + end) / 2;
        // Check for overflow: mid*mid
        // if mid > 65535, mid*mid overflows 32-bit.
        // We are in 64-bit env but u32 is 32. Cast to u64 for check.
        
        unsigned long long sq = (unsigned long long)mid * mid;
        
        if (sq == n) return mid;
        
        if (sq < n) {
            start = mid + 1;
            ans = mid;
        } else {
            end = mid - 1;
        }
    }
    return ans;
}

// Random Number Generator
static uint32_t next = 1;

void srand(uint32_t seed) {
    next = seed;
}

uint32_t rand() {
    next = next * 1103515245 + 12345;
    return (uint32_t)(next / 65536) % 32768;
}

uint32_t rand_range(uint32_t min, uint32_t max) {
    return min + (rand() % (max - min + 1));
}
