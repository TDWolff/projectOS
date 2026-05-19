#include "stdio.h"
#include "string.h"
#include "../drivers/vga.h"
#include <stdarg.h> // Provided by Clang (freestanding) for varargs

// Helper: Convert Integer to String
void itoa(int64_t n, char* str, int base) {
    int i = 0;
    int isNegative = 0;

    // Handle 0 explicitly, otherwise empty string is printed for 0
    if (n == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }

    // Handle negative numbers only if base is 10
    if (n < 0 && base == 10) {
        isNegative = 1;
        n = -n;
    }

    // Process individual digits
    while (n != 0) {
        int rem = n % base;
        str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        n = n / base;
    }

    // Append negative sign
    if (isNegative)
        str[i++] = '-';

    str[i] = '\0';
    reverse(str);
}

void kprintf(const char* format, ...) {
    va_list args;
    va_start(args, format);

    for (size_t i = 0; format[i] != '\0'; i++) {
        if (format[i] != '%') {
            kprint_char(format[i]);
            continue;
        }

        i++; // Skip the %
        
        switch (format[i]) {
            case '%': {
                kprint_char('%');
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                kprint_char(c);
                break;
            }
            case 's': {
                const char* s = va_arg(args, const char*);
                // kprint(s);
                break;
            }
            case 'd': {
                int64_t d = va_arg(args, int64_t);
                char buffer[32];
                itoa(d, buffer, 10);
                // kprint(buffer);
                break;
            }
            case 'x': {
                uint64_t x = va_arg(args, uint64_t);
                char buffer[32];
                itoa(x, buffer, 16);
                // kprint("0x");
                // kprint(buffer);
                break;
            }
            default:
                kprint_char('%');
                kprint_char(format[i]);
                break;
        }
    }

    va_end(args);
}