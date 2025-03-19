#include "nunu.h"

#include <string.h>

void NUNU_prepare_message(DataPacket *dataPacket, const char * message) {
    dataPacket->data.header=MESSAGE_PACKET;
    memcpy(dataPacket->data.payload, message, sizeof(dataPacket->data.payload));
}

void NUNU_prepare_ack(DataPacket *dataPacket) {
    // in the future we might reply with received payload and then the sending radio
    // could compare it and determine if the messegage was read correctly (kamilsss655)
    dataPacket->data.header = ACK_PACKET;
    // sending only empty header seems to not work, so set few bytes of payload to increase reliability (kamilsss655)
    memset(dataPacket->data.payload, 255, 5);
}

void NUNU_clear(DataPacket *dataPacket) {
    memset(dataPacket->serializedArray, 0, sizeof(dataPacket->serializedArray));
}

uint8_t NUNU_parse(DataPacket *dataPacket, uint8_t * origin) {
    memcpy(dataPacket->serializedArray, origin, sizeof(dataPacket->serializedArray));
    return dataPacket->data.header >= INVALID_PACKET; 
}