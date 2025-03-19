#ifndef NUNU_H
#define NUNU_H

#include <stdint.h>

enum {
	NONCE_LENGTH = 13,
    PAYLOAD_LENGTH = 30
};

typedef enum PacketType {
    MESSAGE_PACKET = 100u,
    ENCRYPTED_MESSAGE_PACKET,
    ACK_PACKET,
    INVALID_PACKET
} PacketType;

// Data Packet definition                            // 2024 kamilsss655
typedef union
{
  struct{
    uint8_t header;
    uint8_t payload[PAYLOAD_LENGTH];
    unsigned char nonce[NONCE_LENGTH];
    // uint8_t signature[SIGNATURE_LENGTH];
  } data;
  // header + payload + nonce = must be an even number
  uint8_t serializedArray[1+PAYLOAD_LENGTH+NONCE_LENGTH];
} DataPacket;

void NUNU_prepare_message(DataPacket *dataPacket, const char * message);
void NUNU_prepare_ack(DataPacket *dataPacket);
void NUNU_clear(DataPacket *dataPacket);
uint8_t NUNU_parse(DataPacket *dataPacket, uint8_t * origin);

#endif