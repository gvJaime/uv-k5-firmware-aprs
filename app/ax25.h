#ifndef APP_APRS_H
#define APP_APRS_H

#ifdef APRS

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define AX25_FLAG 0x7E
#define AX25_CONTROL_UI 0x03
#define AX25_PID_NO_LAYER3 0xF0
#define AX25_FCS_POLY 0x8408 // CRC-16-CCITT (X^16 + X^12 + X^5 + 1)

typedef struct {
    char dest[7];   // Destination callsign + SSID
    char source[7]; // Source callsign + SSID
    char payload[256];
    uint16_t fcs;
} AX25Frame;

uint16_t ax25_crc_update(uint16_t crc, uint8_t byte);

void ax25_encode_address(char *out, const char *callsign, uint8_t ssid);

void ax25_serialize_frame(AX25Frame *frame, uint8_t *buffer, size_t *len);

#endif

#endif