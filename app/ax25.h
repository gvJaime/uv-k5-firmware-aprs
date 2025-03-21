#ifndef AX25_H
#define AX25_H

#include <stdint.h>

#define CALLSIGN_SIZE 7
#define MAX_DIGIS 8
#define DIGI_MAX_SIZE CALLSIGN_SIZE * MAX_DIGIS
#define INFO_MAX_SIZE 256

#define AX25_CONTROL_UI      0x03
#define AX25_PID_NO_LAYER3   0xF0

#define AX25_IFRAME_MAX_SIZE CALLSIGN_SIZE + CALLSIGN_SIZE + DIGI_MAX_SIZE + 1 + 1 + INFO_MAX_SIZE // does not consider flag and CRC
#define AX25_BITSTUFFED_MAX_SIZE ((AX25_IFRAME_MAX_SIZE * 13) / 10)

typedef struct {
    char * control;
    char * pid;
    char * info;
    char raw_buffer[AX25_IFRAME_MAX_SIZE];
    uint16_t len;
    uint8_t readable;
} AX25UIFrame;

int16_t AX25_find_offset(const char *arr, uint16_t arr_length, uint8_t target, uint16_t start_offset);

uint8_t AX25_insert_destination(AX25UIFrame * self, const char * callsign, uint8_t ssid);

uint8_t AX25_insert_source(AX25UIFrame * self, const char * callsign, uint8_t ssid);

uint8_t AX25_get_source(AX25UIFrame * self, char * dst);

uint8_t AX25_insert_paths(AX25UIFrame * self, const char ** path_strings, uint8_t paths);

uint8_t AX25_insert_info(AX25UIFrame * self, const char* format, ...);


void AX25_clear(AX25UIFrame* frame);

#endif