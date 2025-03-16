#ifdef ENABLE_APRS

#include <string.h>
#include <stdint.h>

#include "app/ax25.h"

AX25Frame ax25frame;

int16_t AX25_find_offset(const char *arr, uint16_t arr_length, uint8_t target, uint16_t start_offset) {
    for (uint16_t i = start_offset; i < arr_length; ++i) {
        if (arr[i] == target) {
            return i; // Return the index (offset) where the byte is found
        }
    }
    return -1; // Return -1 if the target byte isn't found
}

// Updates the CRC for one byte.
static uint16_t ax25_crc_update(uint16_t crc, char byte) {
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
uint16_t AX25_compute_fcs(const char *data, uint16_t len) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++)
        crc = ax25_crc_update(crc, data[i]);
    return ~crc;
}

uint8_t APRS_parse_offsets(AX25Frame* frame) {
    frame->control_offset = AX25_find_offset(frame->buffer, AX25_IFRAME_MAX_SIZE, AX25_CONTROL_UI, 1 + CALLSIGN_SIZE + CALLSIGN_SIZE);
    frame->fcs_offset = AX25_find_offset(frame->buffer, AX25_IFRAME_MAX_SIZE, AX25_FLAG, frame->control_offset) - 2;
    return frame->control_offset != -1 && frame->fcs_offset != -1;
}

void AX25_set_fcs(AX25Frame *frame) {
    // Compute FCS over dest, source, control, digis, pid, and payload (excluding start flag)
    uint16_t crc = AX25_compute_fcs(frame->buffer + 1, frame->fcs_offset - 1);
    uint16_t * fcs_ptr = (uint16_t *) frame->buffer + frame->fcs_offset;
    *fcs_ptr = crc;
}

uint8_t AX25_check_fcs(AX25Frame *frame) {
    // Compute FCS over dest, source, control, digis, pid, and payload (excluding start flag)
    uint16_t crc = AX25_compute_fcs(frame->buffer + 1, frame->fcs_offset - 1);
    uint16_t * fcs_ptr = (uint16_t *) frame->buffer + frame->fcs_offset;
    return *fcs_ptr == crc;
}

void AX25_clear(AX25Frame* frame) {
    memset(frame->buffer, 0, AX25_IFRAME_MAX_SIZE);
    frame->len = 0;
    frame->control_offset = -1;
    frame->fcs_offset = -1;
}
// **Bit Stuffing Function**
uint16_t AX25_bit_stuff(AX25Frame *frame) {
    uint16_t out_index = 0;
    uint8_t bit_count = 0;
    uint8_t current_byte = 0;
    uint8_t bit_pos = 0;

    for (uint16_t i = 0; i < frame->len; i++) {
        uint8_t byte = frame->buffer[i];

        for (uint8_t j = 0; j < 8; j++) {
            uint8_t bit = (byte >> j) & 1;

            // Insert bit into the output buffer
            current_byte |= (bit << bit_pos);
            bit_pos++;

            // Count consecutive ones
            if (bit) {
                bit_count++;
            } else {
                bit_count = 0;
            }

            // If 5 consecutive 1s, stuff a 0
            if (bit_count == 5) {
                bit_count = 0;
                if (bit_pos == 8) {
                    frame->stuff_buffer[out_index++] = current_byte;
                    current_byte = 0;
                    bit_pos = 0;
                }
                bit_pos++; // Add extra 0
            }

            if (bit_pos == 8) {
                frame->stuff_buffer[out_index++] = current_byte;
                current_byte = 0;
                bit_pos = 0;
            }
        }
    }

    // Store any remaining bits
    if (bit_pos > 0) {
        frame->stuff_buffer[out_index++] = current_byte;
    }

    return out_index; // Return stuffed size
}

// **Bit De-stuffing Function**
// frame length must be defined by now
uint16_t AX25_bit_unstuff(AX25Frame *frame) {
    uint16_t out_index = 0;
    uint8_t bit_count = 0;
    uint8_t current_byte = 0;
    uint8_t bit_pos = 0;

    for (uint16_t i = 0; i < frame->len; i++) {
        uint8_t byte = frame->stuff_buffer[i];

        for (uint8_t j = 0; j < 8; j++) {
            uint8_t bit = (byte >> j) & 1;

            // Add bit to the current byte
            current_byte |= (bit << bit_pos);
            bit_pos++;

            // Count consecutive ones
            if (bit) {
                bit_count++;
            } else {
                bit_count = 0;
            }

            // If 5 consecutive 1s, skip next bit (stuffed 0)
            if (bit_count == 5) {
                j++; // Skip next bit
                bit_count = 0;
            }

            // Store full byte
            if (bit_pos == 8) {
                frame->buffer[out_index++] = current_byte;
                current_byte = 0;
                bit_pos = 0;
            }
        }
    }

    // Store any remaining bits
    if (bit_pos > 0) {
        frame->buffer[out_index++] = current_byte;
    }

    frame->len = out_index;
    return out_index; // Return unstuffed size
}

uint8_t AX25_validate(AX25Frame* frame) {
    // must be unstuffable
    if(AX25_bit_unstuff(frame)) {
        return 0;
    }
    // must start with correct flag
    if(frame->buffer[0] != AX25_FLAG) {
        frame->len = 0;
        return 0;
    }

    // seek end flag
    uint16_t i = 1;
    while(i < AX25_IFRAME_MAX_SIZE && frame->buffer[i] != AX25_FLAG) {
        i++;
    }
    if(i == AX25_IFRAME_MAX_SIZE) {
        return 0;
    }

    frame->len = i;

    APRS_parse_offsets(frame);

    return AX25_check_fcs(frame);
}

#endif