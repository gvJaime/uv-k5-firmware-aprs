#ifdef ENABLE_APRS
#include "app/messenger.h"

uint16_t find_offset(const uint8_t *arr, uint16_t arr_length, uint8_t target, uint16_t start_offset) {
    for (uint16_t i = start_offset; i < arr_length; ++i) {
        if (arr[i] == target) {
            return i; // Return the index (offset) where the byte is found
        }
    }
    return -1; // Return -1 if the target byte isn't found
}


// Updates the CRC for one byte.
static uint16_t ax25_crc_update(uint16_t crc, uint8_t byte) {
    crc ^= byte;
    for (int i = 0; i < 8; i++) {
        if (crc & 1)
            crc = (crc >> 1) ^ AX25_FCS_POLY;
        else
            crc >>= 1;
    }
    return crc;
}

// Computes the Frame Check Sequence (CRC-16) over the provided data.
uint16_t ax25_compute_fcs(const uint8_t *data, uint8_t len) {
    uint16_t crc = 0xFFFF;
    for (uint8_t i = 0; i < len; i++)
        crc = ax25_crc_update(crc, data[i]);
    return ~crc;
}

uint8_t is_valid(struct AX25Frame frame) {
    const uint16_t *p = frame.buffer;
    if(p[0] != 0x7E || p[strlen(p) - 1] != 0x7E)
        return false;
    return ax25_check_fcs(frame);
}

void ax25_parse_offsets(struct AX25Frame frame) {
    frame.control_offset = find_offset(frame.buffer, BUFFER_SIZE, AX25_CONTROL_UI, 1 + DEST_SIZE + SRC_SIZE);
    frame.fcs_offset = find_offset(frame.buffer, BUFFER_SIZE, AX25_FLAG, frame.control_offset) - 2;
}

// we only compare the message id for now
uint8_t is_ack(struct AX25Frame frame, uint16_t for_message) {
    const uint16_t *p = frame.buffer + control_offset + 2;
    // Check that the payload starts with ':' and the fixed sequence ":ack" is at the correct offset.
    if (p[0] != ':' || memcmp(&p[10], ":ack", 4) != 0)
        return 0;
    // Check that the message number (starting at index 14) is not empty.
    
    return 1;
}

void ax25_set_fcs(struct AX25Frame *frame) {
    // Compute FCS over dest, source, control, digis, pid, and payload (excluding start flag)
    uint16_t crc = ax25_compute_fcs(frame.buffer + 1, frame.fcs_offset - 1);
    uint16_t * fcs_ptr = frame.buffer + frame.fcs_offset;
    *fcs_ptr = crc;
}

uint8_t ax25_check_fcs(struct AX25Frame *frame) {
    // Compute FCS over dest, source, control, digis, pid, and payload (excluding start flag)
    uint16_t crc = ax25_compute_fcs(frame.buffer + 1, frame.fcs_offset - 1);
    uint16_t * fcs_ptr = frame.buffer + frame.fcs_offset;
    return *fcs_ptr == crc;
}

#endif