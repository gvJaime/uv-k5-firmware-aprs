

#ifndef APRS_H
#define APRS

#include <stdint.h>

#ifdef ENABLE_APRS

#define PATH_SIZE 7
#define CALLSIGN_SIZE 6

typedef struct {
    char callsign[CALLSIGN_SIZE + 1];
    uint8_t ssid;
    char path1[PATH_SIZE + 1];
    char path2[PATH_SIZE + 1];
} APRSConfig;
#endif

#endif