

#ifndef APRS_H
#define APRS_H

#ifdef ENABLE_APRS

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

uint8_t APRS_is_ack_for_message(AX25Frame* frame, uint16_t for_message_id);
uint8_t APRS_is_ack(AX25Frame* frame);
uint8_t APRS_destined_to_user(AX25Frame* frame);
uint16_t APRS_get_msg_id(AX25Frame* frame);
void APRS_prepare_ack(AX25Frame* frame, uint16_t for_message_id, char * for_callsign);
void APRS_prepare_message(AX25Frame* frame, const char * message, uint8_t is_ack);



#endif

#endif