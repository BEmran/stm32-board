/* Copyright 2020-2022 The MathWorks, Inc. */

#include <linuxTimeLogger.h>

uint64_t MW_GetTimeInMillis() {
    
    uint64_t tMillis;
    struct timespec ts;
    clock_gettime(CLOCK_BOOTTIME, &ts);
    tMillis  = (uint64_t)ts.tv_sec * (uint64_t)1000 + (uint64_t)(ts.tv_nsec / 1000000L);
    return tMillis;
}

uint64_t MW_GetTimeInMicros() {
    
    uint64_t tMicros;
    struct timespec ts;
    clock_gettime(CLOCK_BOOTTIME, &ts);
    tMicros  = (uint64_t)ts.tv_sec * (uint64_t)1000000L + (uint64_t)(ts.tv_nsec / 1000);
    return tMicros;
}