#ifndef AX25_H
#define AX25_H

#ifdef ENABLE_APRS

#define CALLSIGN_SIZE 7
#define MAX_DIGIS 8
#define DIGI_MAX_SIZE CALLSIGN_SIZE * MAX_DIGIS
#define INFO_MAX_SIZE 256
#define AX25_FLAG            0x7E
#define AX25_CONTROL_UI      0x03
#define AX25_PID_NO_LAYER3   0xF0
#define AX25_FCS_POLY        0x8408  // Reversed polynomial for CRC-16-CCITT

#define AX25_IFRAME_MAX_SIZE 1 + CALLSIGN_SIZE + CALLSIGN_SIZE + DIGI_MAX_SIZE + 1 + 1 + INFO_MAX_SIZE + 2 + 1 + 1 // last one added because it needs to be even


// you are supposed to lay out the data onto the buffer as it is received,
// parse the buffer and find the offsets of the variable sections,
// namely the digis section and the info section.
// if all AX25 packets were the same length, it would have this structure:
//typedef struct {
//    uint8_t start_flag; // for alignment purposes
//    uint8_t dest[CALLSIGN_SIZE];     // Destination callsign field (7 bytes)
//    uint8_t source[CALLSIGN_SIZE];   // Source callsign field (7 bytes)
//    uint8_t digis[DIGI_MAX_SIZE];   // digipeater callsigns
//    uint8_t control;     // Control field (UI frame)
//    uint8_t pid;         // PID field (No layer 3)
//    uint8_t payload[INFO_MAX_SIZE]; // APRS text payload
//    uint16_t fcs;        // Frame Check Sequence (CRC)
//    uint8_t end_flag; // for alignment purposes
//  } AX25Frame;
typedef struct {
    int8_t control_offset;
    int16_t fcs_offset;
    uint16_t len;
    char buffer[AX25_IFRAME_MAX_SIZE];
} AX25Frame;

uint16_t AX25_compute_fcs(const char *data, uint16_t len);
void AX25_set_fcs(AX25Frame *frame);
uint8_t AX25_check_fcs(AX25Frame *frame);
uint8_t AX25_validate(AX25Frame* frame);
void AX25_clear(AX25Frame* frame);
int16_t AX25_find_offset(const char *arr, uint16_t arr_length, uint8_t target, uint16_t start_offset);

#endif

#endif