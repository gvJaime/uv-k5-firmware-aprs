#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "aprs.h"

#include "ax25.h"
#include "external/printf/printf.h"

#include "app/messenger.h"

#include "settings.h"

// possibly read from EEPROM in the future
uint16_t msg_id = 100;
const char * aprs_destination = "APN000"; // apparently dependant on the device

// we only compare the message id for now
uint8_t APRS_is_ack_for_message(AX25UIFrame* frame, uint16_t for_message_id) {
    const char *p = frame->info;
    // Check that the payload starts with ':' and the fixed sequence ":ack" is at the correct offset.
    if (p[0] != ':' || memcmp(&p[10], ":ack", 4) != 0)
        return 0;
    // TODO: Check the message id
    
    return for_message_id == msg_id;
}

// we only compare the message id for now
uint8_t APRS_is_ack(AX25UIFrame* frame) {
    const char *p = frame->info;
    // Check that the payload starts with ':' and the fixed sequence ":ack" is at the correct offset.
    if (p[0] != ':' || memcmp(&p[10], ":ack", 4) != 0)
        return 0;
    
    return 1;
}

// we check if we are the intended recipient of the message
uint8_t APRS_destined_to_user(AX25UIFrame* frame) {
    const char *p = frame->info + 1; // skip the ":"
    return memcmp(&p[0], gEeprom.APRS_CONFIG.callsign, CALLSIGN_SIZE - 1);
}

uint16_t APRS_get_msg_id(AX25UIFrame* frame) {
    int16_t offset = AX25_find_offset(frame->info, AX25_IFRAME_MAX_SIZE, APRS_ACK_TOKEN, 0);
    if(offset != -1) {
        char p[6];
        p[5] = 0;
        strncpy(p, frame->info + offset, 5);
        return atoi(p);
    } else {
        return 0;
    }
}

#define ACK_SIZE 1 + ADDRESSEE_SIZE + 1 + 3 + 5 + 1

char ack_message[ACK_SIZE];

void APRS_prepare_ack(AX25UIFrame* frame, uint16_t for_message_id, char * for_callsign) {
    memset(ack_message, 0 , ACK_SIZE * sizeof(char));
    sprintf(ack_message, ":%s:ack%d", for_callsign, for_message_id);
    APRS_prepare_message(frame, ack_message, true);
}

// TODO: Bit stuffing per section 3.6 of AX25 spec if needed
void APRS_prepare_message(AX25UIFrame* frame, const char * message, uint8_t is_ack) {
    memset(frame->info, 0, AX25_IFRAME_MAX_SIZE);

    // source, destination and digis
    AX25_insert_destination(frame, aprs_destination, 0);
    AX25_insert_source(frame, gEeprom.APRS_CONFIG.callsign, gEeprom.APRS_CONFIG.ssid);
    const char * paths[2];
    paths[0] = gEeprom.APRS_CONFIG.path1;
    if(*gEeprom.APRS_CONFIG.path2) {
        paths[1] = gEeprom.APRS_CONFIG.path2;
    }
    AX25_insert_paths(frame, paths, 2);

    // dump message
    AX25_insert_info(frame, ":%s{%d", message ,msg_id);

    // increase message count
    if(!is_ack)
        msg_id++;
}

void APRS_display_received(AX25UIFrame* frame, char * field) {
    if(!frame->readable) {
        return;
    }
    // dump the message onto the display
        AX25_get_source(frame, field);
		sprintf(
			field + strlen(field), // copy exactly after the destination
			"> %s",
			frame->info + 1 // skip the ":"
		);
}

uint8_t APRS_parse(AX25UIFrame* frame, uint8_t * origin) {
    // TODO:
    frame->readable = *origin;
    return false;
}
