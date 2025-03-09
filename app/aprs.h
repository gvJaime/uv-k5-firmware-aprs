

#ifndef APRS_H
#define APRS

#include <stdint.h>

#ifdef ENABLE_APRS

#define PATH_SIZE 7
#define CALLSIGN_SIZE 6

typedef struct {
    char callsign[CALLSIGN_SIZE];
    uint8_t ssid;
    char path1[PATH_SIZE];
    char path2[PATH_SIZE];
} APRSConfig;
#endif

#endif