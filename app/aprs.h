

#ifndef APRS_H
#define APRS_H

#include <stdint.h>
#include "app/ax25.h"


typedef struct {
    char callsign[CALLSIGN_SIZE];
    uint8_t ssid;
    char path1[CALLSIGN_SIZE];
    char path2[CALLSIGN_SIZE];
} APRSConfig;

extern const char * aprs_destination;
#define APRS_ACK_TOKEN       '{'

#define ADDRESSEE_SIZE 9

extern uint16_t msg_id;

uint8_t APRS_is_ack_for_message(AX25UIFrame* frame, uint16_t for_message_id);
uint8_t APRS_is_ack(AX25UIFrame* frame);
uint8_t APRS_destined_to_user(AX25UIFrame* frame);
uint16_t APRS_get_msg_id(AX25UIFrame* frame);
void APRS_prepare_ack(AX25UIFrame* frame, uint16_t for_message_id, char * for_callsign);

/**
 * Inserts the message into the provided AX.25 frame.
 * 
 * Please mind that the [readable] flag will become false, as the packet becomes unreadable
 * due to bit stuffing.
 */
uint16_t APRS_prepare_message(AX25UIFrame* frame, const char * message, uint8_t is_ack);
void APRS_display_received(AX25UIFrame* frame, char * field);
uint8_t APRS_parse(AX25UIFrame* frame, char * origin, uint16_t len);


#endif