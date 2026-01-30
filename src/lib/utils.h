#ifndef UTILS_H
#define UTILS_H

#include "../include/types.h"

// ---------------------------------------------------------
//                Standard Math / Utils
// ---------------------------------------------------------

#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define CLAMP(x, low, high)  (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))

int abs(int n);
int sign(int n);
int clamp(int n, int min, int max);

// Geometric helpers
// Removed float versions to fix linker errors. 
// Uses integer math. 't' is the numerator, 'total' is the denominator.
int lerp(int a, int b, int t, int total); 

// Integer Square Root
uint32_t isqrt(uint32_t n);

// Random Number Generator (LCG)
void srand(uint32_t seed);
uint32_t rand();
uint32_t rand_range(uint32_t min, uint32_t max);

#endif
