#include "rtc.h"
#include "../include/ports.h"

// CMOS Registers
#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

// Check if update is in progress
static int get_update_in_progress_flag() {
    outb(CMOS_ADDRESS, 0x0A);
    return (inb(CMOS_DATA) & 0x80);
}

static uint8_t get_rtc_register(int reg) {
    outb(CMOS_ADDRESS, reg);
    return inb(CMOS_DATA);
}

rtc_time_t rtc_get_time() {
    rtc_time_t time;

    // Wait until update is not in progress
    // If we read while it's updating, we get garbage.
    while (get_update_in_progress_flag()); 

    time.seconds = get_rtc_register(0x00);
    time.minutes = get_rtc_register(0x02);
    time.hours   = get_rtc_register(0x04);
    time.day     = get_rtc_register(0x07);
    time.month   = get_rtc_register(0x08);
    time.year    = get_rtc_register(0x09);

    // Register B (Status Register B)
    // Bit 2 (0x04) = Binary Mode (1) or BCD Mode (0)
    // Most RTCs return BCD data by default.
    outb(CMOS_ADDRESS, 0x0B);
    uint8_t registerB = inb(CMOS_DATA);

    // Convert BCD to Binary if necessary
    // BCD (Binary Coded Decimal): 0x59 = 59 decimal, not 89 decimal
    if (!(registerB & 0x04)) {
        time.seconds = (time.seconds & 0x0F) + ((time.seconds / 16) * 10);
        time.minutes = (time.minutes & 0x0F) + ((time.minutes / 16) * 10);
        time.hours   = ( (time.hours & 0x0F) + (((time.hours & 0x70) / 16) * 10) ) | (time.hours & 0x80);
        time.day     = (time.day & 0x0F) + ((time.day / 16) * 10);
        time.month   = (time.month & 0x0F) + ((time.month / 16) * 10);
        time.year    = (time.year & 0x0F) + ((time.year / 16) * 10);
    }

    // Convert 12 hour clock to 24 hour clock if necessary & handle AM/PM
    // Usually top bit of hour is set for PM if in 12h mode? 
    // This is complex, but for standard QEMU/PC, it's usually 24h or BCD.
    
    // For now, assume 2000s century
    time.year += 2000;

    return time;
}
