#ifdef ENABLE_APRS
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "app/messenger.h"

#include "settings.h"

// possibly read from EEPROM in the future
uint16_t msg_id = 100;
const char * aprs_destination = "APN000"; // apparently dependant on the device

int16_t find_offset(const char *arr, uint16_t arr_length, uint8_t target, uint16_t start_offset) {
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
uint16_t APRS_compute_fcs(const char *data, uint16_t len) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++)
        crc = ax25_crc_update(crc, data[i]);
    return ~crc;
}

uint8_t APRS_is_valid(AX25Frame frame) {
    const char *p = frame.buffer;
    if(p[0] != 0x7E || p[strlen(p) - 1] != 0x7E)
        return false;
    return APRS_check_fcs(&frame);
}

uint8_t APRS_parse_offsets(AX25Frame frame) {
    frame.control_offset = find_offset(frame.buffer, APRS_BUFFER_SIZE, AX25_CONTROL_UI, 1 + DEST_SIZE + SRC_SIZE);
    frame.fcs_offset = find_offset(frame.buffer, APRS_BUFFER_SIZE, AX25_FLAG, frame.control_offset) - 2;
    return frame.control_offset != -1 && frame.fcs_offset != -1;
}

// we only compare the message id for now
uint8_t APRS_is_ack_for_message(AX25Frame frame, uint16_t for_message_id) {
    const char *p = frame.buffer + frame.control_offset + 2;
    // Check that the payload starts with ':' and the fixed sequence ":ack" is at the correct offset.
    if (p[0] != ':' || memcmp(&p[10], ":ack", 4) != 0)
        return 0;
    // TODO: Check the message id
    
    return for_message_id == msg_id;
}

// we only compare the message id for now
uint8_t APRS_is_ack(AX25Frame frame) {
    const char *p = frame.buffer + frame.control_offset + 2;
    // Check that the payload starts with ':' and the fixed sequence ":ack" is at the correct offset.
    if (p[0] != ':' || memcmp(&p[10], ":ack", 4) != 0)
        return 0;
    
    return 1;
}

void APRS_set_fcs(AX25Frame *frame) {
    // Compute FCS over dest, source, control, digis, pid, and payload (excluding start flag)
    uint16_t crc = APRS_compute_fcs(frame->buffer + 1, frame->fcs_offset - 1);
    uint16_t * fcs_ptr = (uint16_t *) frame->buffer + frame->fcs_offset;
    *fcs_ptr = crc;
}

uint8_t APRS_check_fcs(AX25Frame *frame) {
    // Compute FCS over dest, source, control, digis, pid, and payload (excluding start flag)
    uint16_t crc = APRS_compute_fcs(frame->buffer + 1, frame->fcs_offset - 1);
    uint16_t * fcs_ptr = (uint16_t *) frame->buffer + frame->fcs_offset;
    return *fcs_ptr == crc;
}

// we check if we are the intended recipient of the message
uint8_t APRS_destined_to_user(AX25Frame frame) {
    const char *p = frame.buffer + frame.control_offset + 3;
    return memcmp(&p[0], gEeprom.APRS_CONFIG.callsign, CALLSIGN_SIZE);
}

uint16_t APRS_get_msg_id(AX25Frame frame) {
    int16_t offset = find_offset(frame.buffer, APRS_BUFFER_SIZE, APRS_ACK_TOKEN, frame.control_offset);
    if(offset != -1) {
        char * p = frame.buffer + offset;
        // set end of buffer to zero to ensure atoi works well. We won't use this anymore anyway
        memset(frame.buffer + frame.fcs_offset, 0, 3);
        return atoi(p);
    } else {
        return 0;
    }
}

#define ACK_SIZE 1 + ADDRESSEE_SIZE + 1 + 3 + 5 + 1

void APRS_prepare_ack(AX25Frame frame, uint16_t for_message_id, char * for_callsign) {
    char message[ACK_SIZE];
    memset(message, 0 , ACK_SIZE * sizeof(char));
    sprintf(message, ":%s:ack%d", for_callsign, for_message_id);
    APRS_prepare_message(frame, message);
}

// TODO: Bit stuffing per section 3.6 of AX25 spec if needed
void APRS_prepare_message(AX25Frame frame, const char * message) {
    frame.buffer[0] = AX25_FLAG;
    memset(frame.buffer, 0, APRS_BUFFER_SIZE);
    strncpy(frame.buffer + 1, aprs_destination, 7);
    strncpy(frame.buffer + 1 + DEST_SIZE, gEeprom.APRS_CONFIG.callsign, SRC_SIZE - 1);
    frame.buffer[1 + DEST_SIZE + SRC_SIZE - 1] = gEeprom.APRS_CONFIG.ssid;
    strncpy(frame.buffer + 1 + DEST_SIZE + SRC_SIZE, gEeprom.APRS_CONFIG.path1, DIGI_CALL_SIZE);
    strncpy(frame.buffer + 1 + DEST_SIZE + SRC_SIZE + DIGI_CALL_SIZE, gEeprom.APRS_CONFIG.path2, DIGI_CALL_SIZE);
    frame.control_offset = 1 + DEST_SIZE + SRC_SIZE + DIGI_CALL_SIZE * 2;
    frame.buffer[frame.control_offset] = AX25_CONTROL_UI;
    frame.buffer[frame.control_offset + 1] = AX25_PID_NO_LAYER3;
    snprintf(frame.buffer + frame.control_offset + 2, INFO_MAX_SIZE, ":%s", message);
    frame.fcs_offset = sizeof(frame.buffer);
    uint16_t fcs = APRS_compute_fcs(frame.buffer, frame.fcs_offset);
    frame.buffer[frame.fcs_offset] = (fcs >> 8) & 0xFF;  // MSB first
    frame.buffer[frame.fcs_offset + 1] = fcs & 0xFF;     // LSB
    frame.buffer[frame.fcs_offset + 1] = AX25_FLAG;
    msg_id++;
}

#endif