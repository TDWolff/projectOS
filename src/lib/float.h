#ifndef FLOAT_H
#define FLOAT_H

#include "../include/types.h"

// Initializes the FPU (Floating Point Unit) and SSE (Streaming SIMD Extensions)
// Sets CR0 and CR4 registers to enable coprocessor support.
void enable_fpu();

// ---------------------------------------------------------
//                Math Functions
// ---------------------------------------------------------

#define PI 3.14159265359f

float float_abs(float x);
int float_floor(float x);
int float_ceil(float x);

float float_sqrt(float x);
float float_sin(float x);
float float_cos(float x);
float float_pow(float base, int exp);

#endif