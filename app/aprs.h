

#ifndef APRS_H
#define APRS_H

#ifdef ENABLE_APRS

#include <stdint.h>

#define PATH_SIZE 7
#define CALLSIGN_SIZE 6

typedef struct {
    char callsign[CALLSIGN_SIZE];
    uint8_t ssid;
    char path1[PATH_SIZE];
    char path2[PATH_SIZE];
} APRSConfig;

#define DEST_SIZE 7
#define SRC_SIZE 7
#define DIGI_CALL_SIZE 7
#define MAX_DIGIS 8
#define DIGI_MAX_SIZE DIGI_CALL_SIZE * MAX_DIGIS
#define INFO_MAX_SIZE 256

extern const aprs_destination;

#define AX25_FLAG            0x7E
#define AX25_CONTROL_UI      0x03
#define AX25_PID_NO_LAYER3   0xF0
#define AX25_FCS_POLY        0x8408  // Reversed polynomial for CRC-16-CCITT
#define APRS_ACK_TOKEN       '{'

#define BUFFER_SIZE 1 + DEST_SIZE + SRC_SIZE + DIGI_MAX_SIZE + 1 + 1 + INFO_MAX_SIZE + 2 + 1

#define ADDRESSEE_SIZE 9

// you are supposed to lay out the data onto the buffer as it is received,
// parse the buffer and find the offsets of the variable sections,
// namely the digis section and the info section.
// if all AX25 packets were the same length, it would have this structure:
//typedef struct {
//    uint8_t start_flag; // for alignment purposes
//    uint8_t dest[DEST_SIZE];     // Destination callsign field (7 bytes)
//    uint8_t source[SRC_SIZE];   // Source callsign field (7 bytes)
//    uint8_t digis[DIGI_MAX_SIZE];   // digipeater callsigns
//    uint8_t control;     // Control field (UI frame)
//    uint8_t pid;         // PID field (No layer 3)
//    uint8_t payload[INFO_MAX_SIZE]; // APRS text payload
//    uint16_t fcs;        // Frame Check Sequence (CRC)
//    uint8_t end_flag; // for alignment purposes
//  } AX25Frame;
typedef struct {
    struct {
        uint8_t control_offset;
        uint16_t fcs_offset;
    }
    uint16_t buffer [BUFFER_SIZE];
} AX25Frame;

extern uint16_t msg_id;

uint16_t ax25_compute_fcs(const uint8_t *data, uint8_t len);
uint8_t is_ack(struct AX25Frame frame, uint16_t for_message_id);
void ax25_set_fcs(struct AX25Frame *frame);
uint8_t ax25_check_fcs(struct AX25Frame *frame);
uint8_t is_valid(struct AX25Frame frame);
uint8_t destined_to_user(struct AX25Frame frame);
uint16_t get_msg_id(struct AX25Frame frame);
uint8_t parse_offsets(struct AX25Frame frame);
void prepare_ack(struct AX25Frame frame, uint16_t for_message_id, uint8_t * for_callsign);
void prepare_message(struct AX25Frame frame, uint8_t * message);


#endif

#endif