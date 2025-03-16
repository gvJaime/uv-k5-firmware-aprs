#ifdef ENABLE_APRS

#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include "external/printf/printf.h"

#include "app/messenger.h"

#include "settings.h"

// possibly read from EEPROM in the future
uint16_t msg_id = 100;
const char * aprs_destination = "APN000"; // apparently dependant on the device


// we only compare the message id for now
uint8_t APRS_is_ack_for_message(AX25Frame* frame, uint16_t for_message_id) {
    const char *p = frame->buffer + frame->control_offset + 2;
    // Check that the payload starts with ':' and the fixed sequence ":ack" is at the correct offset.
    if (p[0] != ':' || memcmp(&p[10], ":ack", 4) != 0)
        return 0;
    // TODO: Check the message id
    
    return for_message_id == msg_id;
}

// we only compare the message id for now
uint8_t APRS_is_ack(AX25Frame* frame) {
    const char *p = frame->buffer + frame->control_offset + 2;
    // Check that the payload starts with ':' and the fixed sequence ":ack" is at the correct offset.
    if (p[0] != ':' || memcmp(&p[10], ":ack", 4) != 0)
        return 0;
    
    return 1;
}

// we check if we are the intended recipient of the message
uint8_t APRS_destined_to_user(AX25Frame* frame) {
    const char *p = frame->buffer + frame->control_offset + 3;
    return memcmp(&p[0], gEeprom.APRS_CONFIG.callsign, CALLSIGN_SIZE);
}

uint16_t APRS_get_msg_id(AX25Frame* frame) {
    int16_t offset = AX25_find_offset(frame->buffer, AX25_IFRAME_MAX_SIZE, APRS_ACK_TOKEN, frame->control_offset);
    if(offset != -1) {
        char * p = frame->buffer + offset;
        // set end of buffer to zero to ensure atoi works well. We won't use this anymore anyway
        memset(frame->buffer + frame->fcs_offset, 0, 3);
        return atoi(p);
    } else {
        return 0;
    }
}

#define ACK_SIZE 1 + ADDRESSEE_SIZE + 1 + 3 + 5 + 1

char ack_message[ACK_SIZE];

void APRS_prepare_ack(AX25Frame* frame, uint16_t for_message_id, char * for_callsign) {
    memset(ack_message, 0 , ACK_SIZE * sizeof(char));
    sprintf(ack_message, ":%s:ack%d", for_callsign, for_message_id);
    APRS_prepare_message(frame, ack_message, true);
}

// TODO: Bit stuffing per section 3.6 of AX25 spec if needed
void APRS_prepare_message(AX25Frame* frame, const char * message, uint8_t is_ack) {
    memset(frame->buffer, 0, AX25_IFRAME_MAX_SIZE);

    // start bit
    frame->buffer[0] = AX25_FLAG;

    // source, destination and digis
    strncpy(frame->buffer + 1, aprs_destination, CALLSIGN_SIZE);
    strncpy(frame->buffer + 1 + CALLSIGN_SIZE, gEeprom.APRS_CONFIG.callsign, CALLSIGN_SIZE);
    frame->buffer[1 + CALLSIGN_SIZE + CALLSIGN_SIZE] = gEeprom.APRS_CONFIG.ssid;
    strncpy(frame->buffer + 1 + CALLSIGN_SIZE + CALLSIGN_SIZE, gEeprom.APRS_CONFIG.path1, CALLSIGN_SIZE);
    strncpy(frame->buffer + 1 + CALLSIGN_SIZE + CALLSIGN_SIZE + CALLSIGN_SIZE, gEeprom.APRS_CONFIG.path2, CALLSIGN_SIZE);

    // mark control byte offset
    frame->control_offset = 1 + CALLSIGN_SIZE + CALLSIGN_SIZE + CALLSIGN_SIZE * 2;

    // set control bytes
    frame->buffer[frame->control_offset] = AX25_CONTROL_UI;
    frame->buffer[frame->control_offset + 1] = AX25_PID_NO_LAYER3;

    // dump message
    snprintf(frame->buffer + frame->control_offset + 2, INFO_MAX_SIZE, ":%s{%d", message,msg_id);

    // mark fcs word offset
    frame->fcs_offset = strlen(frame->buffer + frame->control_offset + 2) + frame->control_offset + 2;

    // calculate and write
    uint16_t fcs = AX25_compute_fcs(frame->buffer, frame->fcs_offset);
    frame->buffer[frame->fcs_offset] = (fcs >> 8) & 0xFF;  // MSB first
    frame->buffer[frame->fcs_offset + 1] = fcs & 0xFF;     // LSB
    frame->buffer[frame->fcs_offset + 2] = AX25_FLAG;

    frame->len = frame->fcs_offset + 3;

    // increase message count
    if(!is_ack)
        msg_id++;
}

#endif