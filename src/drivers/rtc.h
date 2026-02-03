#ifndef RTC_H
#define RTC_H

#include "../include/types.h"

typedef struct {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} rtc_time_t;

// Read the current time from the CMOS RTC
rtc_time_t rtc_get_time();

#endif
